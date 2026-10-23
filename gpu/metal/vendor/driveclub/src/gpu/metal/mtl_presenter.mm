#include "gpu/metal/mtl_presenter.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#import <Foundation/Foundation.h>

#include "gpu/metal/mtl_device.h"
#include "gpu/metal/spatial_upscaler.h"
#include "gpu/metal/frame_generation.h"
#include "gpu/metal/stutter_trace.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <string>
#include <pthread.h>

@interface BBMetalDisplayTarget : NSObject<CAMetalDisplayLinkDelegate>
@property(copy) void (^render)(CAMetalDisplayLinkUpdate*);
@end
@implementation BBMetalDisplayTarget
- (void)metalDisplayLink:(CAMetalDisplayLink*)link needsUpdate:(CAMetalDisplayLinkUpdate*)update {
    (void)link;
    if(self.render) self.render(update);
}
@end

namespace Gpu::Metal {

namespace {

constexpr const char* kPresentShaderSrc = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut vs_present(uint vid [[vertex_id]]) {
    VertexOut out;
    out.uv = float2((vid << 1) & 2, vid & 2);
    out.position = float4(out.uv.x * 2.0 - 1.0, 1.0 - out.uv.y * 2.0, 0.0, 1.0);
    return out;
}

fragment float4 fs_present(VertexOut in [[stage_in]],
                           texture2d<float> tex [[texture(0)]],
                           sampler smp [[sampler(0)]],
                           constant float& sharpness [[buffer(0)]]) {
    float4 color=tex.sample(smp, in.uv);
    if (sharpness>0.0f) {
        float2 px=1.0f/float2(tex.get_width(),tex.get_height());
        float3 near=(tex.sample(smp,in.uv+float2(px.x,0)).rgb+
            tex.sample(smp,in.uv-float2(px.x,0)).rgb+
            tex.sample(smp,in.uv+float2(0,px.y)).rgb+
            tex.sample(smp,in.uv-float2(0,px.y)).rgb)*0.25f;
        color.rgb=clamp(color.rgb+(color.rgb-near)*sharpness,0.0f,1.0f);
    }
    return color;
}
)";

} // namespace

// A presented callback may arrive after GPU completion (and after Presenter
// destruction). Each callback owns this small state independently of Impl.
struct PresentationState {
    std::atomic<std::uint64_t> displayed{0},generated{0};
    std::mutex mutex;
    std::deque<PresentedFrame> samples;
};

// Optional, independent of Presenter lifetime: a drawable's presentation
// callback can follow GPU completion and destruction of the presenter.
struct PresentationTimingLog {
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::string> pending;
    std::jthread writer;
    unsigned dropped=0;
    FILE* file=nullptr;
    void Start() {
        writer=std::jthread([this](std::stop_token stop) {
            for(;;) {
                std::string line;
                {
                    std::unique_lock lock(mutex);
                    ready.wait(lock,[&] {return stop.stop_requested() || !pending.empty();});
                    if(pending.empty())return;
                    line=std::move(pending.front());pending.pop_front();
                }
                std::fputs(line.c_str(),file);std::fflush(file);
            }
        });
    }
    void Enqueue(const char* line) {
        {
            std::scoped_lock lock(mutex);
            // A slow diagnostic disk must never hold up GPU/presentation
            // callbacks or consume unbounded memory.
            if(pending.size()==4096) {pending.pop_front();++dropped;}
            pending.emplace_back(line);
        }
        ready.notify_one();
    }
    ~PresentationTimingLog() {
        if(writer.joinable()) {writer.request_stop();ready.notify_one();writer.join();}
        if(file)std::fclose(file);
        if(dropped)NSLog(@"Optional presentation timing CSV dropped %u records",dropped);
    }
};

struct PresentationTiming {
    std::shared_ptr<PresentationTimingLog> log;
    std::mutex mutex;
    std::uint64_t sequence=0;
    bool display_link=false,cpu_done=false,gpu_done=false,present_done=false,written=false;
    double callback=0,deadline=0,target_presentation=0,submitted=0;
    double commit_begin=0,commit_end=0,scheduled=0,gpu_start=0,gpu_end=0,completed=0;
    double presented_callback=0,presented=0;
    long status=0,error_code=0;

    // Caller holds mutex. Report completed records only, regardless of which
    // callback arrives first. No per-frame diagnostics exist unless enabled.
    void WriteIfComplete() {
        if(written || !cpu_done || !gpu_done || !present_done)return;
        written=true;
        char line[512];
        std::snprintf(line,sizeof(line),"%llu,%d,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%ld,%ld\n",
            (unsigned long long)sequence,int(display_link),callback,deadline,target_presentation,
            submitted,commit_begin,commit_end,scheduled,gpu_start,gpu_end,completed,
            presented_callback,presented,status,error_code);
        log->Enqueue(line);
    }
};

struct Presenter::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLCommandQueue> interpolation_queue;
    CAMetalLayer* layer;
    id<MTLRenderPipelineState> pipeline;
    id<MTLSamplerState> sampler;
    dispatch_semaphore_t frames_in_flight;
    std::unique_ptr<SpatialUpscaler> upscaler;
    bool metalfx=false,frame_generation=false,frame_generation_supported=false;
    float sharpness=0;
    std::shared_ptr<PresentationState> presentations=std::make_shared<PresentationState>();
    std::atomic<std::uint64_t> next_sequence{0},display_updates{0};
    struct Settings {bool vsync=true,metalfx=false,frame_generation=false;float sharpness=0,refresh_rate=60;int width=0,height=0;};
    std::mutex settings_mutex;
    Settings requested,applied;
    std::uint64_t settings_generation=1,applied_generation=0;
    std::atomic<const char*> status{"Off"};
    std::atomic<bool> frame_loop{false};
    std::atomic<std::uint64_t> skipped{0};
    std::jthread worker;
    std::function<void*()> pending;
    std::function<void()> queued;
    std::function<std::shared_ptr<const GeneratedFrame>()> frame_provider;
    std::shared_ptr<const GeneratedFrame> held_real,ready_pair;
    double real_due=0;
    void Display(CAMetalDisplayLinkUpdate*,double);
    CAMetalDisplayLink* display_link=nil;
    BBMetalDisplayTarget* display_target=nil;
    double last_display_update=0;
    float display_link_latency=1;
    std::shared_ptr<PresentationTimingLog> timing_log;

    void ApplySettings(bool threaded);
    void EncodeTexture(id<MTLTexture>,id<CAMetalDrawable>,bool display_link,
                       double callback=0,double deadline=0,double target_presentation=0,
                       bool generated=false,std::uint64_t source_frame=0,
                       std::shared_ptr<const GeneratedFrame> packet={});
    void PresentLegacy(void*,std::shared_ptr<const GeneratedFrame> packet={});
    void Run(std::stop_token);

    void Track(id<CAMetalDrawable> drawable,std::shared_ptr<PresentationTiming> timing={},bool generated=false,std::uint64_t source_frame=0) {
        auto state=presentations;
        const auto sequence=++next_sequence;
        const double submitted=CACurrentMediaTime();
        if(timing) {timing->sequence=sequence;timing->submitted=submitted;}
        [drawable addPresentedHandler:^(id<MTLDrawable> frame) {
            const double presented=frame.presentedTime;
            if(timing) {
                std::scoped_lock lock(timing->mutex);
                timing->presented=presented;timing->presented_callback=CACurrentMediaTime();
                timing->present_done=true;timing->WriteIfComplete();
            }
            if (presented>0) {
                state->displayed.fetch_add(1,std::memory_order_relaxed);
                if(generated)state->generated.fetch_add(1,std::memory_order_relaxed);
            }
            std::scoped_lock lock(state->mutex);
            // Keep memory bounded even if the UI thread is stalled.
            if (state->samples.size()==4096) state->samples.pop_front();
            state->samples.push_back({sequence,submitted,presented,generated,source_frame});
        }];
    }
};

Presenter::Presenter(Device& device, void* layer, const PresenterConfig& config)
    : impl(std::make_unique<Impl>()) {
    impl->device = (__bridge id<MTLDevice>)device.Handle();
    impl->queue = (__bridge id<MTLCommandQueue>)device.Queue();
    impl->upscaler=std::make_unique<SpatialUpscaler>(device.Handle());
    impl->frame_generation_supported=FrameGeneration::Supported(device.Handle());
    impl->layer = (__bridge CAMetalLayer*)layer;
    impl->frames_in_flight = dispatch_semaphore_create(kFramesInFlight);

    CAMetalLayer* l = impl->layer;
    l.device = impl->device;
    l.pixelFormat = MTLPixelFormatBGRA8Unorm;
    // Only ever rendered to, never sampled: lets the driver skip extra copies.
    l.framebufferOnly = YES;
    l.maximumDrawableCount = kFramesInFlight;
    l.displaySyncEnabled = config.vsync;
    l.allowsNextDrawableTimeout = YES;
    impl->requested.vsync=config.vsync;
    impl->requested.refresh_rate=config.refresh_rate;
    // CAMetalDisplayLink supports only 1 or 2 frames of preferred latency.
    // Keep the existing default; 2 is an explicit pacing experiment.
    if(const char* latency=std::getenv("BB_METAL_DISPLAY_LINK_LATENCY"))
        if(!std::strcmp(latency,"2"))impl->display_link_latency=2;
    if(const char* path=std::getenv("BB_METAL_PRESENT_TIMING_CSV")) {
        if(*path) {
            auto log=std::make_shared<PresentationTimingLog>();
            log->file=std::fopen(path,"w");
            if(log->file) {
                std::fputs("sequence,display_link,callback_s,deadline_s,target_presentation_s,submitted_s,commit_begin_s,commit_end_s,scheduled_s,gpu_start_s,gpu_end_s,completed_s,presented_callback_s,presented_s,status,error_code\n",log->file);
                log->Start();
                impl->timing_log=std::move(log);
            } else NSLog(@"Cannot open optional Metal presentation timing CSV: %s",path);
        }
    }

    // Create fullscreen present pipeline
    NSError* err = nil;
    NSString* src = [NSString stringWithUTF8String:kPresentShaderSrc];
    id<MTLLibrary> lib = [impl->device newLibraryWithSource:src options:nil error:&err];
    if (err) {
        NSLog(@"Presenter shader compilation failed: %@", err);
    }
    id<MTLFunction> vs = [lib newFunctionWithName:@"vs_present"];
    id<MTLFunction> fs = [lib newFunctionWithName:@"fs_present"];

    MTLRenderPipelineDescriptor* pDesc = [MTLRenderPipelineDescriptor new];
    pDesc.vertexFunction = vs;
    pDesc.fragmentFunction = fs;
    pDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    impl->pipeline = [impl->device newRenderPipelineStateWithDescriptor:pDesc error:&err];
    if (err) {
        NSLog(@"Presenter pipeline creation failed: %@", err);
    }

    MTLSamplerDescriptor* sDesc = [MTLSamplerDescriptor new];
    sDesc.minFilter = MTLSamplerMinMagFilterLinear;
    sDesc.magFilter = MTLSamplerMinMagFilterLinear;
    impl->sampler = [impl->device newSamplerStateWithDescriptor:sDesc];
}

Presenter::~Presenter() {
    if(impl->worker.joinable()) {impl->worker.request_stop();impl->worker.join();}
    // Drain in-flight frames so their completion handlers don't outlive us.
    for (int i = 0; i < kFramesInFlight; ++i) {
        dispatch_semaphore_wait(impl->frames_in_flight, DISPATCH_TIME_FOREVER);
    }
    // libdispatch traps if a semaphore dies below its initial value.
    for (int i = 0; i < kFramesInFlight; ++i) {
        dispatch_semaphore_signal(impl->frames_in_flight);
    }
}

void Presenter::Resize(int width_px, int height_px) {
    std::lock_guard lock(impl->settings_mutex);
    impl->requested.width=width_px;impl->requested.height=height_px;++impl->settings_generation;
}

void Presenter::SetVsync(bool enabled) {
    std::lock_guard lock(impl->settings_mutex);
    impl->requested.vsync=enabled;++impl->settings_generation;
}
void Presenter::SetRefreshRate(float hz) {
    if(!std::isfinite(hz) || hz<30 || hz>1000)return;
    std::lock_guard lock(impl->settings_mutex);
    impl->requested.refresh_rate=hz;++impl->settings_generation;
}

void Presenter::SetUpscaling(bool enabled,float sharpness) {
    std::lock_guard lock(impl->settings_mutex);
    impl->requested.metalfx=enabled;impl->requested.sharpness=std::clamp(sharpness,0.0f,1.0f);++impl->settings_generation;
}
bool Presenter::SupportsMetalFX() const { return impl->upscaler->Supported(); }
bool Presenter::SupportsFrameGeneration() const {return impl->frame_generation_supported;}
void Presenter::SetFrameGeneration(bool enabled) {
    std::lock_guard lock(impl->settings_mutex);
    impl->requested.frame_generation=enabled && impl->frame_generation_supported;++impl->settings_generation;
}
void Presenter::SetFrameProvider(std::function<std::shared_ptr<const GeneratedFrame>()> provider) {
    if(!HasFrameLoop())impl->frame_provider=std::move(provider);
}
std::uint64_t Presenter::GeneratedPresentedFrameCount() const {return impl->presentations->generated.load(std::memory_order_relaxed);}
const char* Presenter::UpscalingStatus() const {
    return impl->status.load(std::memory_order_acquire);
}

std::uint64_t Presenter::PresentedFrameCount() const {
    return impl->presentations->displayed.load(std::memory_order_relaxed);
}

std::vector<PresentedFrame> Presenter::TakePresentations() {
    auto& state=*impl->presentations;
    std::scoped_lock lock(state.mutex);
    std::vector<PresentedFrame> samples(state.samples.begin(),state.samples.end());
    state.samples.clear();
    return samples;
}

void Presenter::PresentClear(float r, float g, float b) {
    if(HasFrameLoop())return;
    impl->ApplySettings(false);
    dispatch_semaphore_wait(impl->frames_in_flight, DISPATCH_TIME_FOREVER);

    @autoreleasepool {
        id<CAMetalDrawable> drawable = [impl->layer nextDrawable];
        if (!drawable) {
            dispatch_semaphore_signal(impl->frames_in_flight);
            return;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(r, g, b, 1.0);

        id<MTLCommandBuffer> cmd = [impl->queue commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc endEncoding];

        dispatch_semaphore_t sem = impl->frames_in_flight;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
          dispatch_semaphore_signal(sem);
        }];
        impl->Track(drawable);
        [cmd presentDrawable:drawable];
        [cmd commit];
    }
}

void Presenter::PresentTexture(void* texture_handle) {
    if(HasFrameLoop())return;
    impl->ApplySettings(false);impl->PresentLegacy(texture_handle);
}

void Presenter::Impl::PresentLegacy(void* texture_handle,std::shared_ptr<const GeneratedFrame> packet) {
    StutterTrace::Scope timing("present",next_sequence+1,0,0,50);
    if (!texture_handle || !pipeline) {
        return;
    }
    id<MTLTexture> tex = (__bridge id<MTLTexture>)texture_handle;

    {
        StutterTrace::Scope wait("present_semaphore",next_sequence+1);
        dispatch_semaphore_wait(frames_in_flight, DISPATCH_TIME_FOREVER);
    }

    @autoreleasepool {
        StutterTrace::Scope acquire("next_drawable",next_sequence+1);
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        acquire.End();
        if (!drawable) {
            dispatch_semaphore_signal(frames_in_flight);
            return;
        }

        EncodeTexture(tex,drawable,false,0,0,0,false,packet ? packet->source_frame : 0,packet);
    }
}

void Presenter::Impl::EncodeTexture(id<MTLTexture> tex,id<CAMetalDrawable> drawable,bool from_display_link,
                                  double callback,double deadline,double target_presentation,
                                  bool generated,std::uint64_t source_frame,std::shared_ptr<const GeneratedFrame> packet) {
        std::shared_ptr<PresentationTiming> timing;
        if(timing_log) {
            timing=std::make_shared<PresentationTiming>();timing->log=timing_log;
            timing->display_link=from_display_link;timing->callback=callback;
            timing->deadline=deadline;timing->target_presentation=target_presentation;
        }
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> cmd = [packet && interpolation_queue ? interpolation_queue : queue commandBuffer];
        if(packet && packet->ready_event)
            [cmd encodeWaitForEvent:(__bridge id<MTLSharedEvent>)packet->ready_event value:packet->ready_value];
        const double scale=std::min(double(drawable.texture.width)/tex.width,
                                    double(drawable.texture.height)/tex.height);
        const int width=std::max(1,int(tex.width*scale)), height=std::max(1,int(tex.height*scale));
        if (metalfx) tex=(__bridge id<MTLTexture>)upscaler->Encode(
            (__bridge void*)cmd,(__bridge void*)tex,width,height);
        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:pass];
        [enc setViewport:MTLViewport{(drawable.texture.width-width)*0.5,
            (drawable.texture.height-height)*0.5,double(width),double(height),0,1}];
        [enc setRenderPipelineState:pipeline];
        [enc setFragmentTexture:tex atIndex:0];
        [enc setFragmentSamplerState:sampler atIndex:0];
        [enc setFragmentBytes:&sharpness length:sizeof(float) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];

        dispatch_semaphore_t sem = frames_in_flight;
        [cmd addCompletedHandler:^(id<MTLCommandBuffer> buffer) {
          (void)packet; // Pins both private frame textures until this GPU read ends.
          if(timing) {
              std::scoped_lock lock(timing->mutex);
              timing->gpu_start=buffer.GPUStartTime;timing->gpu_end=buffer.GPUEndTime;
              timing->completed=CACurrentMediaTime();timing->status=long(buffer.status);
              timing->error_code=long(buffer.error.code);timing->gpu_done=true;timing->WriteIfComplete();
          }
          dispatch_semaphore_signal(sem);
        }];
        if(timing) {
            [cmd addScheduledHandler:^(id<MTLCommandBuffer>) {
                std::scoped_lock lock(timing->mutex);timing->scheduled=CACurrentMediaTime();
            }];
        }
        Track(drawable,timing,generated,source_frame);
        // Apple's CAMetalDisplayLink sample uses command-buffer presentation.
        // Timed presentation alternatives assert with CAMetalDisplayLink.
        [cmd presentDrawable:drawable];
        if(timing)timing->commit_begin=CACurrentMediaTime();
        [cmd commit];
        if(timing) {
            std::scoped_lock lock(timing->mutex);timing->commit_end=CACurrentMediaTime();
            timing->cpu_done=true;timing->WriteIfComplete();
        }
    status.store(metalfx ? upscaler->Status() : "Bilinear scaling",std::memory_order_release);
}

#include "gpu/metal/presentation_loop.inc"

} // namespace Gpu::Metal
