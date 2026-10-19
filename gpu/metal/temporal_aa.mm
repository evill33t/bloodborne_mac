// SPDX-License-Identifier: GPL-2.0-or-later
// Direct Metal camera reprojection, depth rejection and neighbourhood clipping.
// Based on bbport's camera_motion.comp / taa.comp; no Vulkan dependency.
#include "temporal_aa.h"
#import <Metal/Metal.h>
#include <cstdio>

namespace Gpu::Metal {
namespace {
constexpr const char* Source=R"(
#include <metal_stdlib>
using namespace metal;
struct Params {
    float4 transform[3]; float4 projection; float4 previous_projection;
    float2 motion_scale; uint reset; uint native_motion;
};
float3 tone(float3 c) { c=max(c,0.0f); return c/(1.0f+dot(c,float3(.2126,.7152,.0722))); }
float3 untone(float3 c) { return c/max(1.0f-dot(c,float3(.2126,.7152,.0722)),1e-4f); }
kernel void resolve(texture2d<float,access::read> color [[texture(0)]],
                    depth2d<float,access::sample> depth [[texture(1)]],
                    texture2d<float,access::sample> history [[texture(2)]],
                    texture2d<float,access::read> old_depth [[texture(3)]],
                    texture2d<float,access::write> result [[texture(4)]],
                    texture2d<float,access::write> new_depth [[texture(5)]],
                    texture2d<float,access::read> motion [[texture(6)]],
                    constant Params& p [[buffer(0)]], uint2 pixel [[thread_position_in_grid]]) {
    uint2 size=uint2(color.get_width(),color.get_height());
    if (any(pixel>=size)) return;
    float4 current=color.read(pixel);
    if (!all(isfinite(current))) current=float4(0,0,0,1);
    float d=depth.read(pixel);
    if (!isfinite(d)) d=1;
    float3 resolved=current.rgb;
    if (!p.reset) {
        float2 uv=(float2(pixel)+.5f)/float2(size);
        bool sky=d>=1;
        float z=sky ? 1.0f : p.projection.w/min(d-p.projection.z,-1e-8f);
        float4 view=float4((uv.x*2-1)*z/p.projection.x,(1-uv.y*2)*z/p.projection.y,z,sky ? 0.0f : 1.0f);
        float3 v=float3(dot(p.transform[0],view),dot(p.transform[1],view),dot(p.transform[2],view));
        float2 previous=float2(v.x*p.previous_projection.x,-v.y*p.previous_projection.y)/max(v.z,1e-4f)*.5f+.5f;
        if (p.native_motion && !sky && z>0 && isfinite(z)) {
            // Nearest sampling preserves object coverage and depth at half size.
            float4 native=motion.read(pixel/2);
            float encoded_depth=z/(z+10.0f);
            float2 object_previous=uv+(native.xy-.5f)*p.motion_scale;
            bool valid=all(isfinite(native)) && native.a==1.0f &&
                all(native.xy>0.0f) && all(native.xy<1.0f) &&
                abs(native.b-encoded_depth)<=1.5f/255.0f+1e-6f;
            // One code step is already about 1.7 scene pixels at 1080p. Keep
            // precise camera motion when the native residual is only rounding.
            if (valid && all(isfinite(object_previous)) &&
                any(abs(object_previous-previous)>abs(p.motion_scale)/255.0f))
                previous=object_previous;
        }
        if (v.z>0 && all(isfinite(previous)) && all(previous>=0.0f) && all(previous<1.0f)) {
            uint2 q=uint2(previous*float2(size));
            float pd=old_depth.read(q).r;
            float expected=sky ? 1.0f : p.previous_projection.z+p.previous_projection.w/v.z;
            float tolerance=max(2e-7f,abs(p.previous_projection.z-expected)*.01f);
            // Reject newly revealed surfaces. Sky is an infinite direction;
            // camera translation must not drag its history across geometry.
            bool matches=sky ? pd>=1.0f : pd<1.0f && abs(pd-expected)<=tolerance;
            if (matches) {
                float3 lo=float3(1e20f),hi=float3(-1e20f),mean=0,second=0;
                for (int y=-1;y<=1;++y) for (int x=-1;x<=1;++x) {
                    uint2 at=uint2(clamp(int2(pixel)+int2(x,y),int2(0),int2(size)-1));
                    float3 c=color.read(at).rgb;
                    if (!all(isfinite(c))) c=0;
                    c=tone(c); lo=min(lo,c); hi=max(hi,c); mean+=c; second+=c*c;
                }
                mean/=9; second/=9;
                float3 deviation=sqrt(max(second-mean*mean,0.0f));
                lo=max(lo,mean-deviation*1.25f); hi=min(hi,mean+deviation*1.25f);
                constexpr sampler linear(coord::normalized,address::clamp_to_edge,filter::linear);
                float3 old=tone(history.sample(linear,previous).rgb);
                if (all(isfinite(old))) {
                    old=clamp(old,lo,hi);
                    float displacement=length((previous-uv)*float2(size));
                    float weight=mix(.75f,.35f,clamp(displacement/16.0f,0.0f,1.0f));
                    // Camera reprojection cannot follow independently moving
                    // objects. Reduce stale history when the local HDR color
                    // changes, while retaining full weight for a stable scene.
                    float3 present=tone(current.rgb);
                    float3 delta=abs(old-present);
                    float change=max(delta.r,max(delta.g,delta.b))/max(.1f,max(present.r,max(present.g,present.b)));
                    weight/=1.0f+16.0f*change*change;
                    resolved=untone(mix(tone(current.rgb),old,weight));
                }
            }
        }
    }
    result.write(float4(resolved,current.a),pixel);
    new_depth.write(float4(d),pixel);
}
)";
struct alignas(16) Parameters {
    std::array<float,12> transform;
    std::array<float,4> projection,previous_projection;
    std::array<float,2> motion_scale;
    std::uint32_t reset,native_motion;
};
static_assert(sizeof(Parameters)==96);
}
struct TemporalAA::Impl {
    id<MTLDevice> device;
    id<MTLComputePipelineState> pipeline;
    id<MTLTexture> color[2]{nil,nil},depth[2]{nil,nil};
    SceneCamera current,previous;
    bool observed=false,resolved=false,history=false;
    bool failed=false;
    unsigned next=0;
    std::uint64_t frames=0,native_frames=0,last_frame=0;
};
TemporalAA::TemporalAA(void* device):impl(std::make_unique<Impl>()) {
    impl->device=(__bridge id<MTLDevice>)device;
}
TemporalAA::~TemporalAA()=default;
void TemporalAA::ObserveCamera(const SceneCamera& camera) {
    if (!impl->observed) { impl->current=camera; impl->observed=true; }
}
bool TemporalAA::HasCamera() const { return impl->observed; }
std::uint64_t TemporalAA::ResolvedFrames() const { return impl->frames; }
std::uint64_t TemporalAA::NativeMotionFrames() const { return impl->native_frames; }
void TemporalAA::Invalidate() { impl->history=impl->observed=impl->resolved=false; }
void TemporalAA::EndFrame() {
    if (!impl->resolved) impl->history=false;
    else impl->previous=impl->current;
    impl->observed=impl->resolved=false;
}
void* TemporalAA::Encode(void* command,void* color,void* depth,std::uint64_t frame,const MotionInput* motion) {
    auto cmd=(__bridge id<MTLCommandBuffer>)command;
    auto scene=(__bridge id<MTLTexture>)color;
    auto z=(__bridge id<MTLTexture>)depth;
    if (!impl->observed || impl->resolved || impl->failed || !cmd || !scene || !z ||
        scene.pixelFormat!=MTLPixelFormatRGBA16Float || scene.width!=z.width || scene.height!=z.height ||
        (z.pixelFormat!=MTLPixelFormatDepth32Float && z.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8 &&
         z.pixelFormat!=MTLPixelFormatDepth16Unorm) ||
        scene.textureType!=MTLTextureType2D || z.textureType!=MTLTextureType2D ||
        scene.width<impl->current.width || scene.height<impl->current.height ||
        scene.width-impl->current.width>8 || scene.height-impl->current.height>8) return nullptr;
    if (!impl->pipeline) {
        NSError* error=nil;
        auto library=[impl->device newLibraryWithSource:@(Source) options:nil error:&error];
        impl->pipeline=library ? [impl->device newComputePipelineStateWithFunction:[library newFunctionWithName:@"resolve"] error:&error] : nil;
        if (!impl->pipeline) {
            std::fprintf(stderr,"Metal temporal AA unavailable: %s\n",error.localizedDescription.UTF8String ?: "pipeline creation failed");
            impl->failed=true; return nullptr;
        }
    }
    if (!impl->color[0] || impl->color[0].width!=scene.width || impl->color[0].height!=scene.height) {
        impl->history=false;
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:scene.width height:scene.height mipmapped:NO];
        descriptor.storageMode=MTLStorageModePrivate;
        descriptor.usage=MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        for (int i=0;i<2;++i) impl->color[i]=[impl->device newTextureWithDescriptor:descriptor];
        descriptor.pixelFormat=MTLPixelFormatR32Float;
        for (int i=0;i<2;++i) impl->depth[i]=[impl->device newTextureWithDescriptor:descriptor];
        if (!impl->color[0] || !impl->color[1] || !impl->depth[0] || !impl->depth[1]) {
            for (int i=0;i<2;++i) { impl->color[i]=nil; impl->depth[i]=nil; }
            return nullptr;
        }
    }
    bool reset=!impl->history || impl->current.width!=impl->previous.width || impl->current.height!=impl->previous.height;
    if (!reset) {
        float distance=0;
        for (unsigned k:{3u,7u,11u}) distance+=std::pow(impl->current.inverse[k]-impl->previous.inverse[k],2);
        float alignment=0;
        for (unsigned k:{0u,1u,2u}) alignment+=impl->current.view[k]*impl->previous.view[k];
        reset=distance>100 || alignment<.5f || impl->current.projection!=impl->previous.projection;
    }
    auto native=motion ? (__bridge id<MTLTexture>)motion->texture : nil;
    const bool use_native=!reset && native && frame && motion->current_frame==frame &&
        motion->previous_frame==frame-1 && impl->last_frame==frame-1 &&
        native.pixelFormat==MTLPixelFormatRGBA8Unorm && native.textureType==MTLTextureType2D &&
        native.width*2==scene.width && native.height*2==scene.height &&
        std::isfinite(motion->uv_scale[0]) && std::isfinite(motion->uv_scale[1]) &&
        motion->uv_scale[0]!=0 && motion->uv_scale[1]!=0;
    const Parameters params{Reprojection(impl->previous,impl->current),impl->current.projection,
        impl->previous.projection,use_native ? motion->uv_scale : std::array<float,2>{},
        std::uint32_t(reset),std::uint32_t(use_native)};
    auto encoder=[cmd computeCommandEncoder];
    if (!encoder) return nullptr;
    [encoder setComputePipelineState:impl->pipeline];
    [encoder setTexture:scene atIndex:0]; [encoder setTexture:z atIndex:1];
    [encoder setTexture:impl->color[1-impl->next] atIndex:2];
    [encoder setTexture:impl->depth[1-impl->next] atIndex:3];
    [encoder setTexture:impl->color[impl->next] atIndex:4];
    [encoder setTexture:impl->depth[impl->next] atIndex:5];
    [encoder setTexture:use_native ? native : scene atIndex:6];
    [encoder setBytes:&params length:sizeof(params) atIndex:0];
    [encoder dispatchThreads:MTLSizeMake(scene.width,scene.height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    [encoder endEncoding];
    auto result=impl->color[impl->next];
    impl->next=1-impl->next; impl->resolved=impl->history=true; ++impl->frames;
    impl->last_frame=frame;
    if (use_native) ++impl->native_frames;
    return (__bridge void*)result;
}
}
