// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/frame_generation.h"
#include <cassert>
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>
#include <cstring>

int main(int argc,char** argv){@autoreleasepool {
    const bool stale=argc>1 && std::strcmp(argv[1],"--stale")==0;
    const bool swizzle=argc>1 && std::strcmp(argv[1],"--swizzle")==0;
    using namespace Gpu::Metal;
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if(!FrameGeneration::Supported((__bridge void*)device))return 77;
    auto queue=[device newCommandQueue];assert(queue);
    auto texture=[&](MTLPixelFormat format,MTLTextureUsage usage) {
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:1920 height:1080 mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;d.usage=usage;return [device newTextureWithDescriptor:d];
    };
    auto image=texture(MTLPixelFormatBGRA8Unorm,MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite|MTLTextureUsagePixelFormatView);
    auto input=swizzle ? [image newTextureViewWithPixelFormat:image.pixelFormat textureType:MTLTextureType2D
        levels:NSMakeRange(0,1) slices:NSMakeRange(0,1)
        swizzle:MTLTextureSwizzleChannelsMake(MTLTextureSwizzleBlue,MTLTextureSwizzleGreen,MTLTextureSwizzleRed,MTLTextureSwizzleAlpha)] : image;
    assert(input);
    auto display=texture(MTLPixelFormatBGRA8Unorm,MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite);
    auto depth=texture(MTLPixelFormatDepth32Float,MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead);
    auto nativeDesc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:960 height:540 mipmapped:NO];
    nativeDesc.storageMode=MTLStorageModeShared;nativeDesc.usage=MTLTextureUsageShaderRead;
    auto native=[device newTextureWithDescriptor:nativeDesc];assert(native);
    std::vector<std::uint32_t> nativeBytes(960*540,126u|(127u<<8)|(5u<<16)|(255u<<24));
    [native replaceRegion:MTLRegionMake2D(0,0,960,540) mipmapLevel:0 withBytes:nativeBytes.data() bytesPerRow:960*4];
    auto lutDesc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:256 height:3 mipmapped:NO];
    lutDesc.textureType=MTLTextureType2DArray;lutDesc.arrayLength=1;lutDesc.storageMode=MTLStorageModeShared;lutDesc.usage=MTLTextureUsageShaderRead;
    auto lut=[device newTextureWithDescriptor:lutDesc];assert(lut);
    std::vector<float> ramp(256*3);for(unsigned y=0;y<3;++y)for(unsigned x=0;x<256;++x)ramp[y*256+x]=float(x)/255;
    [lut replaceRegion:MTLRegionMake2D(0,0,256,3) mipmapLevel:0 slice:0 withBytes:ramp.data() bytesPerRow:256*4 bytesPerImage:256*3*4];
    auto samplerDesc=[MTLSamplerDescriptor new];samplerDesc.normalizedCoordinates=YES;
    samplerDesc.minFilter=samplerDesc.magFilter=MTLSamplerMinMagFilterLinear;
    samplerDesc.sAddressMode=samplerDesc.tAddressMode=MTLSamplerAddressModeClampToEdge;
    auto sampler=[device newSamplerStateWithDescriptor:samplerDesc];
    NSString* source=@R"(
#include <metal_stdlib>
using namespace metal;
kernel void fill(texture2d<float,access::write> image [[texture(0)]],constant uint2& p [[buffer(0)]],uint2 q [[thread_position_in_grid]]) {
 if(q.x>=1920 || q.y>=1080)return;
 bool box=q.x>=640+p.x*4 && q.x<1200+p.x*4 && q.y>=320 && q.y<760;
 float4 c=box ? float4(.8,.3,.1,1) : float4(.04,.08,.12,1);
 if(p.y && q.x>=50 && q.x<350 && q.y>=50 && q.y<120)c=float4(.9,.8,.7,1);
 image.write(c,q);
}
kernel void copy(texture2d<float,access::read> image [[texture(0)]],texture2d_array<float> lut [[texture(1)]],
 texture2d<float,access::write> result [[texture(2)]],sampler smp [[sampler(0)]],uint2 q [[thread_position_in_grid]]) {
 if(q.x>=1920 || q.y>=1080)return;float4 c=image.read(q);
 result.write(float4(half4(lut.sample(smp,float2(c.r,0),0).r,lut.sample(smp,float2(c.g,1),0).r,
     lut.sample(smp,float2(c.b,2),0).r,c.a)),q);
})";
    NSError* error=nil;auto library=[device newLibraryWithSource:source options:nil error:&error];assert(library && !error);
    auto fill=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"fill"] error:&error];assert(fill);
    auto copy=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"copy"] error:&error];assert(copy);
    FrameGeneration effect((__bridge void*)device);
    auto read=[&](id<MTLTexture> t) {
        auto bytes=[device newBufferWithLength:1920*1080*4 options:MTLResourceStorageModeShared];assert(bytes);
        auto cmd=[queue commandBuffer];auto e=[cmd blitCommandEncoder];
        [e copyFromTexture:t sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(1920,1080,1)
            toBuffer:bytes destinationOffset:0 destinationBytesPerRow:1920*4 destinationBytesPerImage:1920*1080*4];[e endEncoding];[cmd commit];[cmd waitUntilCompleted];assert(!cmd.error);
        const auto* p=static_cast<const unsigned char*>(bytes.contents);return std::vector<unsigned char>(p,p+bytes.length);
    };
    unsigned generated=0,bad_edges=0;std::shared_ptr<const GeneratedFrame> pinned;std::vector<unsigned char> pinnedBytes;
    for(unsigned frame=0;frame<16;++frame) {
        auto cmd=[queue commandBuffer];
        if(frame) {
            const auto copy_frame=frame-1;
            assert(!effect.DisplayCopy((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)image,(__bridge void*)lut,(__bridge void*)sampler,frame+50));
            assert(!effect.DisplayCopy((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)input,(__bridge void*)lut,(__bridge void*)sampler,copy_frame+1));
            assert(effect.DisplayCopy((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)input,(__bridge void*)lut,(__bridge void*)sampler,copy_frame));
            auto e=[cmd computeCommandEncoder];[e setComputePipelineState:copy];[e setTexture:input atIndex:0];[e setTexture:lut atIndex:1];[e setTexture:display atIndex:2];[e setSamplerState:sampler atIndex:0];
            [e dispatchThreads:MTLSizeMake(1920,1080,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];
            effect.Draw((__bridge void*)display,frame,true);
            if(frame==1)assert(!effect.Finish((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)display,frame+1,copy_frame));
            auto packet=effect.Finish((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)display,frame,copy_frame);assert(packet && packet->real);
            [cmd commit];[cmd waitUntilCompleted];assert(cmd.status==MTLCommandBufferStatusCompleted && !cmd.error);
            if(packet->generated) {
                ++generated;auto real=read((__bridge id<MTLTexture>)packet->real),fake=read((__bridge id<MTLTexture>)packet->generated);
                for(unsigned y=50;y<120;++y)for(unsigned x=50;x<350;++x)for(unsigned k=0;k<4;++k)
                    assert(real[(y*1920+x)*4+k]==fake[(y*1920+x)*4+k]);
                // Current-to-previous pixel motion must put the moving edge
                // between its real-frame positions, while HUD remains exact.
                unsigned edge=1920;
                auto scene=read((__bridge id<MTLTexture>)packet->scene),motion=read((__bridge id<MTLTexture>)packet->motion);
                const auto* velocity=reinterpret_cast<const _Float16*>(motion.data());
                unsigned differences=0;
                for(unsigned y=300;y<800;++y)for(unsigned x=600;x<1280;++x)
                    differences+=scene[(y*1920+x)*4+2]!=real[(y*1920+x)*4+2];
                std::printf("scene/real differences away from HUD=%u; center motion=(%.4f,%.4f)\n",differences,
                    float(velocity[(540*1920+950)*2]),float(velocity[(540*1920+950)*2+1]));
                for(unsigned x=0;x<1920;++x)if(fake[(540*1920+x)*4+(swizzle ? 0 : 2)]>100){edge=x;break;}
                const unsigned current=640+(frame-1)*4;
                std::printf("frame %u: moving edge %u, current %u; exact HUD preserved\n",frame,edge,current);
                bad_edges+=!(edge>current-4 && edge<current);
                if(!pinned){pinned=packet;pinnedBytes=real;}
            }
            cmd=[queue commandBuffer];
        }
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.depthAttachment.texture=depth;
        pass.depthAttachment.loadAction=MTLLoadActionClear;pass.depthAttachment.storeAction=MTLStoreActionStore;pass.depthAttachment.clearDepth=.5;
        auto clear=[cmd renderCommandEncoderWithDescriptor:pass];[clear endEncoding];
        SceneCamera camera;camera.width=1920;camera.height=1080;camera.projection={1,1920.f/1080,1.0000333f,-.1f};
        for(unsigned k:{0u,5u,10u})camera.view[k]=camera.inverse[k]=1;
        camera.view[3]=std::min(frame,7u)*(.1f/(1.0000333f-.5f))*8/1920;camera.inverse[3]=-camera.view[3];
        MotionInput objectMotion{(__bridge void*)native,frame,frame-1,{.3541666667f,-1e-6f}};
        assert(effect.Scene((__bridge void*)cmd,(__bridge void*)depth,camera,frame,frame>=8 ? &objectMotion : nullptr));
        assert(!effect.Scene((__bridge void*)cmd,(__bridge void*)depth,camera,frame,nullptr));
        for(unsigned overlay:{0u,1u}) {
            unsigned p[]{frame,overlay};auto e=[cmd computeCommandEncoder];[e setComputePipelineState:fill];[e setTexture:image atIndex:0];[e setBytes:p length:sizeof(p) atIndex:0];
            [e dispatchThreads:MTLSizeMake(1920,1080,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];
            if(!overlay){assert(effect.NeedsHud(frame));assert(effect.Hud((__bridge void*)cmd,(__bridge void*)image,frame));assert(!effect.NeedsHud(frame));}
        }
        [cmd commit];[cmd waitUntilCompleted];if(cmd.error)std::fprintf(stderr,"%s\n",cmd.error.description.UTF8String);assert(!cmd.error);
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    assert(generated>=8 && pinned && pinnedBytes==read((__bridge id<MTLTexture>)pinned->real));
    assert(bad_edges==0);
    if(stale) {
        // Same UI texture, later epoch, no new scene: old camera/depth/HUD
        // metadata must never turn a changed image into a generated frame.
        auto cmd=[queue commandBuffer];
        unsigned p[]{100,1};auto e=[cmd computeCommandEncoder];[e setComputePipelineState:fill];[e setTexture:image atIndex:0];[e setBytes:p length:sizeof(p) atIndex:0];
        [e dispatchThreads:MTLSizeMake(1920,1080,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];
        assert(!effect.DisplayCopy((__bridge void*)cmd,(__bridge void*)display,(__bridge void*)input,(__bridge void*)lut,(__bridge void*)sampler,16));
        auto real=effect.RealFrame((__bridge void*)cmd,(__bridge void*)image,16);assert(real && !real->generated);
        [cmd commit];[cmd waitUntilCompleted];assert(!cmd.error);
        assert(read((__bridge id<MTLTexture>)real->real)==read(image));
    }
    effect.Invalidate();assert(!effect.NeedsHud(16));
    auto fallbackCommand=[queue commandBuffer];
    auto fallback=effect.RealFrame((__bridge void*)fallbackCommand,(__bridge void*)image,16);
    assert(fallback && fallback->real && !fallback->generated && fallback->ready_event);
    [fallbackCommand commit];[fallbackCommand waitUntilCompleted];assert(!fallbackCommand.error);
    assert([(__bridge id<MTLSharedEvent>)fallback->ready_event signaledValue]>=fallback->ready_value);
    assert(read((__bridge id<MTLTexture>)fallback->real)==read(image));
    std::puts("Production MetalFX: exact HUD, intermediate object motion, frame/revision gates, pinned snapshot lifetime and history invalidation passed.");
}}
