// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/spatial_upscaler.h"
#include <cassert>
#include <cmath>
#include <cstdio>

int main() {
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice(); if (!device) return 77;
        Gpu::Metal::SpatialUpscaler upscaler((__bridge void*)device);
        if (!upscaler.Supported()) return 77;
        auto queue=[device newCommandQueue];
        // A deliberately asymmetric uniform colour detects channel swaps and
        // colour-space mistakes while tolerating spatial filter edge behavior.
        auto desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:64 height:36 mipmapped:NO];
        desc.storageMode=MTLStorageModePrivate;
        desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead;
        auto input=[device newTextureWithDescriptor:desc]; assert(input);
        for (int width : {128,192,128}) {
            const int height=width*9/16;
            auto cmd=[queue commandBuffer];
            auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture=input;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;
            pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            pass.colorAttachments[0].clearColor=MTLClearColorMake(.75,.25,.125,1);
            [[cmd renderCommandEncoderWithDescriptor:pass] endEncoding];
            auto output=(__bridge id<MTLTexture>)upscaler.Encode((__bridge void*)cmd,(__bridge void*)input,width,height);
            assert(output && output!=input && output.width==NSUInteger(width) && output.height==NSUInteger(height));
            auto data=[device newBufferWithLength:width*height*4 options:MTLResourceStorageModeShared];
            auto blit=[cmd blitCommandEncoder];
            [blit copyFromTexture:output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:data destinationOffset:0
                destinationBytesPerRow:width*4 destinationBytesPerImage:width*height*4];
            [blit endEncoding]; [cmd commit]; [cmd waitUntilCompleted]; assert(!cmd.error);
            auto px=static_cast<const unsigned char*>(data.contents)+((height/2)*width+width/2)*4;
            const int expected[]{32,64,191,255};
            for (int k=0;k<3;++k) {
                if (std::abs(int(px[k])-expected[k])>4) {
                    fprintf(stderr,"MetalFX channel %d expected %d got %u\n",k,expected[k],px[k]);
                    std::abort();
                }
            }
        }
        auto cmd=[queue commandBuffer];
        assert(upscaler.Encode((__bridge void*)cmd,(__bridge void*)input,32,18)==(__bridge void*)input);
        puts("MetalFX: actual GPU upscale, color preservation, resize and downscale fallback passed");
    }
}
