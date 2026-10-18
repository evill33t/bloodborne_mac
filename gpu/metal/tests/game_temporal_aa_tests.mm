// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/temporal_aa.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice(); if (!device) return 77;
        auto queue=[device newCommandQueue];
        Gpu::Metal::TemporalAA taa((__bridge void*)device);
        Gpu::Metal::SceneCamera camera;
        camera.width=camera.height=64; camera.projection={1,1,1,-.1};
        for (unsigned i:{0u,5u,10u}) camera.view[i]=camera.inverse[i]=1;
        auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:64 height:64 mipmapped:NO];
        desc.storageMode=MTLStorageModeManaged; desc.usage=MTLTextureUsageShaderRead;
        auto color=[device newTextureWithDescriptor:desc]; assert(color);
        desc.pixelFormat=MTLPixelFormatDepth32Float; desc.storageMode=MTLStorageModePrivate;
        desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        auto depth=[device newTextureWithDescriptor:desc]; assert(depth);
        auto render=[&](unsigned phase,float z,bool observe) {
            std::vector<_Float16> pixels(64*64*4);
            for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
                const auto k=(y*64+x)*4;
                pixels[k]=((x+y+phase)&1) ? .75f : .25f;
                pixels[k+1]=.25f; pixels[k+2]=2.f; pixels[k+3]=.5f;
            }
            [color replaceRegion:MTLRegionMake2D(0,0,64,64) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:64*8];
            auto cmd=[queue commandBuffer];
            auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.depthAttachment.texture=depth; pass.depthAttachment.loadAction=MTLLoadActionClear;
            pass.depthAttachment.storeAction=MTLStoreActionStore; pass.depthAttachment.clearDepth=z;
            [[cmd renderCommandEncoderWithDescriptor:pass] endEncoding];
            if (observe) taa.ObserveCamera(camera);
            auto result=(__bridge id<MTLTexture>)taa.Encode((__bridge void*)cmd,(__bridge void*)color,(__bridge void*)depth);
            if (!observe) { assert(!result); [cmd commit]; [cmd waitUntilCompleted]; taa.EndFrame(); return 0.f; }
            assert(result);
            auto data=[device newBufferWithLength:64*64*8 options:MTLResourceStorageModeShared];
            auto blit=[cmd blitCommandEncoder];
            [blit copyFromTexture:result sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(64,64,1) toBuffer:data destinationOffset:0 destinationBytesPerRow:64*8 destinationBytesPerImage:64*64*8];
            [blit endEncoding]; [cmd commit]; [cmd waitUntilCompleted]; assert(!cmd.error);
            auto px=static_cast<const _Float16*>(data.contents)+(32*64+32)*4;
            assert(std::abs(float(px[1])-.25f)<.001 && std::abs(float(px[2])-2)<.005 && float(px[3])==.5f);
            for (unsigned i=0;i<64*64*4;++i) assert(std::isfinite(float(static_cast<const _Float16*>(data.contents)[i])));
            const float red=px[0]; taa.EndFrame(); return red;
        };
        assert(std::abs(render(0,.5,true)-.25f)<.001); // cold history preserves HDR and alpha
        const float accumulated=render(1,.5,true);
        assert(accumulated>.25f && accumulated<.74f); // actual temporal accumulation
        assert(std::abs(render(0,.8,true)-.25f)<.001); // disocclusion rejects stale history
        taa.Invalidate();
        assert(std::abs(render(1,.8,true)-.75f)<.001);
        render(0,.8,false); // menu/missing camera invalidates history
        assert(std::abs(render(0,.8,true)-.25f)<.001);
        camera.inverse[3]=100; camera.view[3]=-100; // teleport/camera cut
        assert(std::abs(render(1,.8,true)-.75f)<.001);
        assert(taa.ResolvedFrames()==6);
        puts("Metal temporal AA: GPU accumulation, HDR/alpha, depth rejection, missing scenes and camera cuts passed");
    }
}
