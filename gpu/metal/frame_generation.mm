// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame_generation.h"
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <QuartzCore/QuartzCore.h>
#import <Foundation/Foundation.h>
#include <atomic>
#include <map>
#include <vector>
#include <fstream>
#include <filesystem>
#include <cstdio>

namespace Gpu::Metal {
namespace {
constexpr unsigned Width=1920,Height=1080,PoolLimit=8;
constexpr const char* Source=R"(
#include <metal_stdlib>
using namespace metal;
struct Params {float4 transform[3];float4 projection;float4 previous_projection;float2 scale;uint native_motion;uint reset;};
kernel void inputs(depth2d<float,access::sample> z [[texture(0)]],
 texture2d<float,access::read> native [[texture(1)]],texture2d<float,access::write> depth [[texture(2)]],
 texture2d<float,access::write> motion [[texture(3)]],constant Params& p [[buffer(0)]],uint2 q [[thread_position_in_grid]]) {
 uint2 size=uint2(depth.get_width(),depth.get_height());if(any(q>=size))return;
 float d=z.read(q);if(!isfinite(d))d=1;d=clamp(d,0.f,1.f);depth.write(float4(d),q);
 float2 uv=(float2(q)+.5f)/float2(size),previous=uv;
 if(!p.reset) {
  bool sky=d>=1;float distance=sky ? 1.f : p.projection.w/min(d-p.projection.z,-1e-8f);
  float4 view=float4((uv.x*2-1)*distance/p.projection.x,(1-uv.y*2)*distance/p.projection.y,distance,sky ? 0.f : 1.f);
  float3 v=float3(dot(p.transform[0],view),dot(p.transform[1],view),dot(p.transform[2],view));
  if(v.z>0)previous=float2(v.x*p.previous_projection.x,-v.y*p.previous_projection.y)/v.z*.5f+.5f;
  if(p.native_motion && !sky && distance>0 && isfinite(distance)) {
   float4 n=native.read(q/2);float2 object=uv+(n.xy-.5f)*p.scale;
   bool valid=all(isfinite(n)) && n.a==1 && all(n.xy>0.f) && all(n.xy<1.f) && abs(n.b-distance/(distance+10.f))<=1.5f/255.f+1e-6f;
   if(valid && all(isfinite(object)) && any(abs(object-previous)>abs(p.scale)/255.f))previous=object;
  }
 }
 float2 velocity=(previous-uv)*float2(size);if(!all(isfinite(velocity)))velocity=0;
 motion.write(float4(clamp(velocity,-float2(size)*2,float2(size)*2),0,0),q);
}
// Exact displayed color convention of retail PS 1c472a7564e37de2:
// integer-pixel read, then the bound gamma LUT, separately for each channel.
kernel void display_color(texture2d<float,access::read> scene [[texture(0)]],
 texture2d_array<float,access::sample> lut [[texture(1)]],texture2d<float,access::write> result [[texture(2)]],
 sampler smp [[sampler(0)]],uint2 q [[thread_position_in_grid]]) {
 if(q.x>=result.get_width() || q.y>=result.get_height())return;float4 c=scene.read(q);
 c.rgb=float3(lut.sample(smp,float2(c.r,0),0).r,lut.sample(smp,float2(c.g,1),0).r,lut.sample(smp,float2(c.b,2),0).r);
 // The translated PS packs through half before the UNORM render-target store.
 result.write(float4(half4(c)),q);
}
kernel void preserve_ui(texture2d<float,access::read> scene [[texture(0)]],
 texture2d<float,access::read> real [[texture(1)]],texture2d<float,access::read_write> generated [[texture(2)]],
 uint2 q [[thread_position_in_grid]]) {
 int2 size=int2(real.get_width(),real.get_height());if(any(int2(q)>=size))return;bool ui=false;
 for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x){uint2 p=uint2(clamp(int2(q)+int2(x,y),int2(0),size-1));
  ui=ui || any(abs(scene.read(p).rgb-real.read(p).rgb)>float3(1.5f/255.f));}
 if(ui)generated.write(real.read(q),q);
}
)";
struct alignas(16) Parameters {
    std::array<float,12> transform;
    std::array<float,4> projection,previous_projection;
    std::array<float,2> scale;
    std::uint32_t native_motion,reset;
};
static_assert(sizeof(Parameters)==96);
bool Continuous(const SceneCamera& a,const SceneCamera& b) {
    if(a.width!=b.width || a.height!=b.height || a.projection!=b.projection)return false;
    float distance=0,alignment=0;
    for(unsigned k:{3u,7u,11u})distance+=std::pow(a.inverse[k]-b.inverse[k],2);
    for(unsigned k:{0u,1u,2u})alignment+=a.view[k]*b.view[k];
    return distance<=100 && alignment>=.5f;
}
id<MTLTexture> Parent(id<MTLTexture> texture){return texture.parentTexture ?: texture;}
}
struct FrameGeneration::Impl {
    struct Slot {
        id<MTLTexture> depth,motion,pre_ui,color,real,generated,ui_source;
        id<MTLSharedEvent> ready_event;
        SceneCamera camera;
        std::uint64_t frame=~0ull;
        double time=0;
        bool hud=false,converted=false,reset=true;
    };
    struct Copy {std::shared_ptr<Slot> slot;std::uint64_t revision=0,frame=0;};
    id<MTLDevice> device;
    id<MTLSharedEvent> ready_event;
    std::uint64_t ready_value=0;
    id<MTLComputePipelineState> inputs,convert,ui;
    id effect;
    std::vector<std::shared_ptr<Slot>> pool;
    std::shared_ptr<Slot> current,previous_scene,history,prepared;
    std::map<void*,std::shared_ptr<Slot>> hud;
    std::map<void*,Copy> copies;
    std::atomic<const char*> status{"Off"};
    std::uint64_t generated=0,resets=0,scenes=0,huds=0,display_copies=0,rejects=0,copy_attempts=0;
    bool failed=false;
    unsigned captures=0;
    unsigned history_frames=0;
    std::uint64_t prepared_frame=0;
    std::string capture_root,capture_trigger,capture_token;
    bool Initialize() {
        if(failed)return false;if(effect)return true;
        if(@available(macOS 26.0,*)) {
            if(![MTLFXFrameInterpolatorDescriptor supportsDevice:device]){status="MetalFX frame generation unavailable";failed=true;return false;}
            auto descriptor=[MTLFXFrameInterpolatorDescriptor new];
            descriptor.inputWidth=descriptor.outputWidth=Width;descriptor.inputHeight=descriptor.outputHeight=Height;
            descriptor.colorTextureFormat=descriptor.outputTextureFormat=MTLPixelFormatBGRA8Unorm;
            descriptor.uiTextureFormat=MTLPixelFormatBGRA8Unorm;
            descriptor.depthTextureFormat=MTLPixelFormatR32Float;descriptor.motionTextureFormat=MTLPixelFormatRG16Float;
            effect=[descriptor newFrameInterpolatorWithDevice:device];
            NSError* error=nil;auto library=[device newLibraryWithSource:@(Source) options:nil error:&error];
            for(auto pair:{std::pair{&inputs,@"inputs"},std::pair{&convert,@"display_color"},std::pair{&ui,@"preserve_ui"}})
                *pair.first=[device newComputePipelineStateWithFunction:[library newFunctionWithName:pair.second] error:&error];
            if(!effect || !inputs || !convert || !ui){failed=true;status="MetalFX frame generation initialization failed";
                std::fprintf(stderr,"[framegen] Initialization failed: %s\n",error.localizedDescription.UTF8String ?: "interpolator unavailable");return false;}
            status="2× MetalFX waiting for matching scene/HUD";return true;
        }
        status="MetalFX frame generation requires macOS 26";failed=true;return false;
    }
    id<MTLTexture> Texture(MTLPixelFormat format,MTLTextureUsage usage) {
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:Width height:Height mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;d.usage=usage;return [device newTextureWithDescriptor:d];
    }
    void Dispatch(id<MTLComputeCommandEncoder> encoder) {
        [encoder dispatchThreads:MTLSizeMake(Width,Height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[encoder endEncoding];
    }
    void Capture(id<MTLCommandBuffer> command,const std::shared_ptr<Slot>& slot,const std::shared_ptr<Slot>& previous) {
        if(capture_root.empty() || capture_trigger.empty() || captures>=8)return;
        std::string token;std::ifstream(capture_trigger)>>token;
        if(token.empty() || token==capture_token)return;capture_token=token;
        const auto prefix=capture_root+"/pair-"+std::to_string(++captures)+"-f"+std::to_string(slot->frame);
        std::error_code error;std::filesystem::create_directories(capture_root,error);if(error)return;
        std::ofstream metadata(prefix+".json");metadata<<"{\"width\":1920,\"height\":1080,\"source_frame\":"<<slot->frame
            <<",\"previous_frame\":"<<previous->frame<<",\"delta_s\":"<<slot->time-previous->time
            <<",\"colors\":\"BGRA8Unorm\",\"motion\":\"RG16Float current-to-previous pixels\",\"depth\":\"R32Float nonreversed\"}\n";
        for(auto item:{std::pair{slot->real,"real"},std::pair{slot->generated,"generated"},std::pair{slot->color,"scene"},
                       std::pair{slot->motion,"motion"},std::pair{slot->depth,"depth"},std::pair{previous->real,"previous"}}) {
            const auto name=prefix+"-"+item.second;auto texture=item.first;
            auto buffer=[device newBufferWithLength:Width*Height*4 options:MTLResourceStorageModeShared];if(!buffer)continue;
            auto blit=[command blitCommandEncoder];[blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(Width,Height,1) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:Width*4 destinationBytesPerImage:Width*Height*4];[blit endEncoding];
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if(completed.status!=MTLCommandBufferStatusCompleted)return;
                dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY,0),^{@autoreleasepool {
                    std::error_code ec;std::filesystem::create_directories(std::filesystem::path(name).parent_path(),ec);if(ec)return;
                    std::ofstream file(name+".bin",std::ios::binary);file.write(static_cast<const char*>(buffer.contents),buffer.length);

                }});
            }];
        }
    }
};
bool FrameGeneration::Supported(void* device) {
    if(@available(macOS 26.0,*))return device && [MTLFXFrameInterpolatorDescriptor supportsDevice:(__bridge id<MTLDevice>)device];
    return false;
}
FrameGeneration::FrameGeneration(void* device):impl(std::make_unique<Impl>()) {
    impl->device=(__bridge id<MTLDevice>)device;
    impl->ready_event=[impl->device newSharedEvent];
    if(const char* root=std::getenv("BB_METAL_FRAMEGEN_CAPTURE"))impl->capture_root=root;
    if(const char* trigger=std::getenv("BB_METAL_FRAMEGEN_CAPTURE_TRIGGER"))impl->capture_trigger=trigger;
}
FrameGeneration::~FrameGeneration()=default;
const char* FrameGeneration::Status()const{return impl->status.load();}
std::uint64_t FrameGeneration::Generated()const{return impl->generated;}
void FrameGeneration::Report()const {
    const auto& s=*impl;
    std::fprintf(stderr,"[framegen state] generated=%llu resets=%llu scenes=%llu hud=%llu copies=%llu attempts=%llu slots=%zu slot_rejects=%llu status=%s\n",
        s.generated,s.resets,s.scenes,s.huds,s.display_copies,s.copy_attempts,s.pool.size(),s.rejects,s.status.load());
}
void FrameGeneration::Invalidate(){impl->current.reset();impl->previous_scene.reset();impl->history.reset();impl->prepared.reset();impl->hud.clear();impl->copies.clear();impl->history_frames=0;impl->status="Off";}
bool FrameGeneration::Scene(void* command,void* depth,const SceneCamera& camera,std::uint64_t frame,const MotionInput* native) {
    auto& s=*impl;auto z=(__bridge id<MTLTexture>)depth;auto cmd=(__bridge id<MTLCommandBuffer>)command;
    if(s.current && s.current->frame==frame)return false;
    s.current.reset();
    if(!cmd || !z || camera.width!=Width || camera.height!=Height || z.width!=Width || z.height!=Height ||
        z.textureType!=MTLTextureType2D || z.sampleCount!=1 || z.mipmapLevelCount!=1 ||
        (z.pixelFormat!=MTLPixelFormatDepth32Float && z.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8 && z.pixelFormat!=MTLPixelFormatDepth16Unorm) || !s.Initialize())return false;
    for(auto it=s.hud.begin();it!=s.hud.end();)if(it->second->frame+2<frame)it=s.hud.erase(it);else ++it;
    for(auto it=s.copies.begin();it!=s.copies.end();)if(it->second.frame+1<frame)it=s.copies.erase(it);else ++it;
    for(const auto& slot:s.pool)if(slot.use_count()==1){s.current=slot;break;}
    if(!s.current && s.pool.size()<PoolLimit){s.current=std::make_shared<Impl::Slot>();s.pool.push_back(s.current);}
    if(!s.current){s.status="2× MetalFX waiting for snapshot slot";++s.rejects;return false;}
    auto& slot=*s.current;slot.frame=frame;slot.camera=camera;slot.time=CACurrentMediaTime();slot.hud=slot.converted=false;slot.ui_source=nil;
    const bool continuous=s.previous_scene && s.previous_scene->frame+1==frame && Continuous(s.previous_scene->camera,camera);
    slot.reset=!continuous;
    if(!slot.depth) {
        const auto rw=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        slot.depth=s.Texture(MTLPixelFormatR32Float,rw);slot.motion=s.Texture(MTLPixelFormatRG16Float,rw);
        if(@available(macOS 26.0,*)) {
            id<MTLFXFrameInterpolator> effect=s.effect;
            slot.color=s.Texture(MTLPixelFormatBGRA8Unorm,rw|effect.colorTextureUsage);
            slot.real=s.Texture(MTLPixelFormatBGRA8Unorm,rw|effect.uiTextureUsage);
            slot.generated=s.Texture(MTLPixelFormatBGRA8Unorm,rw|effect.outputTextureUsage);
        }
    }
    if(!slot.depth || !slot.motion || !slot.color || !slot.real || !slot.generated){s.current.reset();return false;}
    auto motion=native ? (__bridge id<MTLTexture>)native->texture : nil;
    bool use_native=continuous && motion && native->current_frame==frame && native->previous_frame==frame-1 &&
        motion.pixelFormat==MTLPixelFormatRGBA8Unorm && motion.textureType==MTLTextureType2D && motion.width*2==Width && motion.height*2==Height &&
        std::isfinite(native->uv_scale[0]) && std::isfinite(native->uv_scale[1]) && native->uv_scale[0]!=0 && native->uv_scale[1]!=0;
    const auto previous=continuous ? s.previous_scene->camera : camera;
    Parameters p{Reprojection(previous,camera),camera.projection,previous.projection,
        use_native ? native->uv_scale : std::array<float,2>{},std::uint32_t(use_native),std::uint32_t(!continuous)};
    auto encoder=[cmd computeCommandEncoder];[encoder setComputePipelineState:s.inputs];
    [encoder setTexture:z atIndex:0];[encoder setTexture:use_native ? motion : slot.color atIndex:1];
    [encoder setTexture:slot.depth atIndex:2];[encoder setTexture:slot.motion atIndex:3];[encoder setBytes:&p length:sizeof(p) atIndex:0];s.Dispatch(encoder);
    auto lease=s.current;[cmd addCompletedHandler:^(id<MTLCommandBuffer>){(void)lease;}];
    s.previous_scene=s.current;++s.scenes;return true;
}
bool FrameGeneration::NeedsHud(std::uint64_t frame) const {return impl->current && impl->current->frame==frame && !impl->current->hud;}
bool FrameGeneration::Hud(void* command,void* color,std::uint64_t frame) {
    auto& s=*impl;auto t=(__bridge id<MTLTexture>)color;
    if(!s.current || s.current->frame!=frame || s.current->hud || !t || t.width!=Width || t.height!=Height ||
        t.textureType!=MTLTextureType2D || t.sampleCount!=1 || t.mipmapLevelCount!=1 ||
        (t.pixelFormat!=MTLPixelFormatRGBA8Unorm && t.pixelFormat!=MTLPixelFormatRGBA8Unorm_sRGB &&
         t.pixelFormat!=MTLPixelFormatBGRA8Unorm && t.pixelFormat!=MTLPixelFormatBGRA8Unorm_sRGB))return false;
    auto& slot=*s.current;
    if(!slot.pre_ui || slot.pre_ui.pixelFormat!=t.pixelFormat)slot.pre_ui=s.Texture(t.pixelFormat,MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView);
    if(!slot.pre_ui)return false;
    auto cmd=(__bridge id<MTLCommandBuffer>)command;auto blit=[cmd blitCommandEncoder];[blit copyFromTexture:t toTexture:slot.pre_ui];[blit endEncoding];
    slot.hud=true;slot.ui_source=Parent(t);s.hud[(__bridge void*)Parent(t)]=s.current;++s.huds;return true;
}
bool FrameGeneration::DisplayCopy(void* command,void* destination,void* source,void* lut,void* sampler,std::uint64_t frame) {
    auto& s=*impl;++s.copy_attempts;s.prepared.reset();auto input=(__bridge id<MTLTexture>)source;auto gamma=(__bridge id<MTLTexture>)lut;
    auto it=s.hud.find((__bridge void*)Parent(input));
    if(std::getenv("BB_METAL_FRAMEGEN_TRACE") && frame%300==0)
        std::fprintf(stderr,"[framegen copy input] frame=%llu source=%p parent=%p found=%d scene_frame=%llu lut_type=%lu source_format=%lu\n",
            frame,source,(__bridge void*)Parent(input),int(it!=s.hud.end()),it!=s.hud.end() ? it->second->frame : ~0ull,
            (unsigned long)gamma.textureType,(unsigned long)input.pixelFormat);
    // The verified Metal retail pass copies the UI within the same emulated
    // flip epoch. A matching texture address alone cannot admit stale inputs:
    // the game can overwrite that texture while a scene observation is absent.
    if(it==s.hud.end() || it->second->frame!=frame || !gamma || gamma.textureType!=MTLTextureType2DArray || !sampler)return false;
    auto slot=it->second;
    // A T# view can remap components without changing pixelFormat. Clone its
    // swizzle as well: omitting BGRA/RGBA remapping makes almost the whole
    // scene look like HUD and would overwrite interpolation with real pixels.
    auto scene=[slot->pre_ui newTextureViewWithPixelFormat:input.pixelFormat textureType:MTLTextureType2D
        levels:NSMakeRange(0,1) slices:NSMakeRange(0,1) swizzle:input.swizzle];
    if(!scene)return false;
    auto cmd=(__bridge id<MTLCommandBuffer>)command;auto encoder=[cmd computeCommandEncoder];
    [encoder setComputePipelineState:s.convert];[encoder setTexture:scene atIndex:0];[encoder setTexture:gamma atIndex:1];
    [encoder setTexture:slot->color atIndex:2];[encoder setSamplerState:(__bridge id<MTLSamplerState>)sampler atIndex:0];s.Dispatch(encoder);
    slot->converted=true;s.prepared=slot;s.prepared_frame=frame;++s.display_copies;
    if(s.display_copies==1)std::fprintf(stderr,"[framegen copy binding] destination_frame=%llu scene_frame=%llu\n",frame,slot->frame);
    (void)destination;return true;
}
void FrameGeneration::Draw(void* destination,std::uint64_t revision,bool display_copy) {
    auto& s=*impl;s.copies.erase(destination);
    if(display_copy && s.prepared)s.copies[destination]={s.prepared,revision,s.prepared_frame};
}
std::shared_ptr<const GeneratedFrame> FrameGeneration::Finish(void* command,void* display,void* real,std::uint64_t revision,std::uint64_t frame) {
    auto& s=*impl;auto found=s.copies.find(display);
    if(found==s.copies.end() || found->second.revision!=revision || found->second.frame!=frame) {
        s.history.reset();s.status="2× MetalFX waiting for matching scene/HUD";return {};
    }
    auto slot=found->second.slot;s.copies.erase(found);
    auto cmd=(__bridge id<MTLCommandBuffer>)command;auto tex=(__bridge id<MTLTexture>)real;
    if(!cmd || !tex || !s.ready_event || tex.width!=Width || tex.height!=Height || tex.pixelFormat!=MTLPixelFormatBGRA8Unorm)return {};
    auto copy=[cmd blitCommandEncoder];[copy copyFromTexture:tex toTexture:slot->real];[copy endEncoding];
    const double delta=s.history ? slot->time-s.history->time : 0;
    bool reset=!s.history || s.history->frame+1!=slot->frame || slot->reset || !Continuous(s.history->camera,slot->camera) || delta<1./240 || delta>.1;
    if(@available(macOS 26.0,*)) {
        id<MTLFXFrameInterpolator> effect=s.effect;
        effect.colorTexture=slot->color;effect.prevColorTexture=reset ? slot->color : s.history->color;
        effect.depthTexture=slot->depth;effect.motionTexture=slot->motion;effect.outputTexture=slot->generated;
        // Interpolate HUD-free images only. The following mask pass restores
        // current HUD pixels exactly, without heuristic UI unblending in FX.
        effect.uiTexture=nil;effect.uiTextureComposited=NO;effect.shouldResetHistory=reset;
        effect.motionVectorScaleX=effect.motionVectorScaleY=1;effect.deltaTime=reset ? 1.f/30.f : float(delta);
        effect.depthReversed=NO;effect.nearPlane=-slot->camera.projection[3]/slot->camera.projection[2];effect.farPlane=3000;
        effect.fieldOfView=2*std::atan(1/std::abs(slot->camera.projection[1]))*180/float(M_PI);effect.aspectRatio=float(Width)/Height;
        effect.jitterOffsetX=effect.jitterOffsetY=0;
        if(@available(macOS 27.0,*)) {
            effect.contentWidth=Width;effect.contentHeight=Height;

        }
        [effect encodeToCommandBuffer:cmd];
        auto encoder=[cmd computeCommandEncoder];[encoder setComputePipelineState:s.ui];[encoder setTexture:slot->color atIndex:0];
        [encoder setTexture:slot->real atIndex:1];[encoder setTexture:slot->generated atIndex:2];s.Dispatch(encoder);
    }
    auto previous=s.history;[cmd addCompletedHandler:^(id<MTLCommandBuffer>){(void)slot;(void)previous;}];
    s.history=slot;
    s.history_frames=reset ? 0 : s.history_frames+1;
    auto output=std::make_shared<GeneratedFrame>();output->real=(__bridge void*)slot->real;output->storage=slot;
    slot->ready_event=s.ready_event;output->ready_event=(__bridge void*)s.ready_event;output->ready_value=++s.ready_value;
    [cmd encodeSignalEvent:s.ready_event value:output->ready_value];
    output->scene=(__bridge void*)slot->color;output->motion=(__bridge void*)slot->motion;output->depth=(__bridge void*)slot->depth;
    output->source_frame=slot->frame;output->interval=reset ? 0 : delta;
    // Cold-start Rosetta and a long source interval can require an extra
    // priming encode. Show only real frames until three consecutive history
    // encodes have completed the reset sequence; never count warmup copies.
    if(s.history_frames>=3){output->generated=(__bridge void*)slot->generated;++s.generated;s.Capture(cmd,slot,previous);}else if(reset)++s.resets;
    s.status=s.history_frames<3 ? "2× MetalFX warming history" : "2× MetalFX active · HUD preserved";
    if(s.generated && s.generated%300==0)std::fprintf(stderr,"[framegen] generated=%llu resets=%llu scenes=%llu hud=%llu copies=%llu slot_rejects=%llu\n",
        s.generated,s.resets,s.scenes,s.huds,s.display_copies,s.rejects);
    return output;
}
std::shared_ptr<const GeneratedFrame> FrameGeneration::RealFrame(void* command,void* real,std::uint64_t frame) {
    auto& s=*impl;auto texture=(__bridge id<MTLTexture>)real;auto cmd=(__bridge id<MTLCommandBuffer>)command;
    if(!s.ready_event || !texture || !cmd || texture.width!=Width || texture.height!=Height || texture.pixelFormat!=MTLPixelFormatBGRA8Unorm)return {};
    std::shared_ptr<Impl::Slot> slot;
    for(const auto& entry:s.pool)if(entry.use_count()==1){slot=entry;break;}
    if(!slot && s.pool.size()<PoolLimit){slot=std::make_shared<Impl::Slot>();s.pool.push_back(slot);}
    if(!slot){++s.rejects;return {};}
    if(!slot->real)slot->real=s.Texture(MTLPixelFormatBGRA8Unorm,MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite);
    if(!slot->real)return {};
    auto blit=[cmd blitCommandEncoder];[blit copyFromTexture:texture toTexture:slot->real];[blit endEncoding];
    auto output=std::make_shared<GeneratedFrame>();output->real=(__bridge void*)slot->real;output->source_frame=frame;output->storage=slot;
    slot->ready_event=s.ready_event;output->ready_event=(__bridge void*)s.ready_event;output->ready_value=++s.ready_value;
    [cmd encodeSignalEvent:s.ready_event value:output->ready_value];
    [cmd addCompletedHandler:^(id<MTLCommandBuffer>){(void)slot;}];return output;
}
}
