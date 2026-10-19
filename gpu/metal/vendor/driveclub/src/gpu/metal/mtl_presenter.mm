#include "gpu/metal/mtl_presenter.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#import <Foundation/Foundation.h>

#include "gpu/metal/mtl_device.h"
#include "gpu/metal/spatial_upscaler.h"
#include "gpu/metal/stutter_trace.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <cmath>
#include <cstring>
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
    std::atomic<std::uint64_t> displayed{0};
    std::mutex mutex;
    std::deque<PresentedFrame> samples;
};

struct Presenter::Impl {
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    CAMetalLayer* layer;
    id<MTLRenderPipelineState> pipeline;
    id<MTLSamplerState> sampler;
    dispatch_semaphore_t frames_in_flight;
    std::unique_ptr<SpatialUpscaler> upscaler;
    bool metalfx=false;
    float sharpness=0;
    std::shared_ptr<PresentationState> presentations=std::make_shared<PresentationState>();
    std::atomic<std::uint64_t> next_sequence{0},display_updates{0};
    struct Settings {bool vsync=true,metalfx=false;float sharpness=0,refresh_rate=60;int width=0,height=0;};
    std::mutex settings_mutex;
    Settings requested,applied;
    std::uint64_t settings_generation=1,applied_generation=0;
    std::atomic<const char*> status{"Off"};
    std::atomic<bool> frame_loop{false};
    std::atomic<std::uint64_t> skipped{0};
    std::jthread worker;
    std::function<void*()> pending;
    std::function<void()> queued;
    CAMetalDisplayLink* display_link=nil;
    BBMetalDisplayTarget* display_target=nil;
    double last_display_update=0;

    void ApplySettings(bool threaded);
    void EncodeTexture(id<MTLTexture>,id<CAMetalDrawable>);
    void PresentLegacy(void*);
    void Run(std::stop_token);

    void Track(id<CAMetalDrawable> drawable) {
        auto state=presentations;
        const auto sequence=++next_sequence;
        const double submitted=CACurrentMediaTime();
        [drawable addPresentedHandler:^(id<MTLDrawable> frame) {
            const double presented=frame.presentedTime;
            if (presented>0) state->displayed.fetch_add(1,std::memory_order_relaxed);
            std::scoped_lock lock(state->mutex);
            // Keep memory bounded even if the UI thread is stalled.
            if (state->samples.size()==4096) state->samples.pop_front();
            state->samples.push_back({sequence,submitted,presented});
        }];
    }
};

Presenter::Presenter(Device& device, void* layer, const PresenterConfig& config)
    : impl(std::make_unique<Impl>()) {
    impl->device = (__bridge id<MTLDevice>)device.Handle();
    impl->queue = (__bridge id<MTLCommandQueue>)device.Queue();
    impl->upscaler=std::make_unique<SpatialUpscaler>(device.Handle());
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

void Presenter::Impl::PresentLegacy(void* texture_handle) {
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

        EncodeTexture(tex,drawable);
    }
}

void Presenter::Impl::EncodeTexture(id<MTLTexture> tex,id<CAMetalDrawable> drawable) {
        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,1);
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLCommandBuffer> cmd = [queue commandBuffer];
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
        [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
          dispatch_semaphore_signal(sem);
        }];
        Track(drawable);
        [cmd presentDrawable:drawable];
        [cmd commit];
    status.store(metalfx ? upscaler->Status() : "Bilinear scaling",std::memory_order_release);
}

#include "gpu/metal/presentation_loop.inc"

} // namespace Gpu::Metal
