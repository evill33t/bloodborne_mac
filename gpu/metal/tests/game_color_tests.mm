// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
#include "gpu/metal/mtl_formats.h"
#include <cassert>
#include <cstdio>

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        const auto format=Gpu::Metal::ColorTargetFormat(10,0,1).format;
        MTLTextureDescriptor* desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
            width:1 height:1 mipmapped:NO];
        desc.storageMode=MTLStorageModeShared;
        desc.usage=MTLTextureUsageRenderTarget|MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
        id<MTLTexture> texture=[device newTextureWithDescriptor:desc]; assert(texture);
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        MTLRenderPassDescriptor* pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=texture;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(1,.25,0,1);
        [[command renderCommandEncoderWithDescriptor:pass] endEncoding];
        [command commit]; [command waitUntilCompleted]; assert(!command.error);
        unsigned char bytes[4]={};
        [texture getBytes:bytes bytesPerRow:4 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
        assert(bytes[0]==0 && bytes[1]==64 && bytes[2]==255 && bytes[3]==255);
        // A guest RGBA descriptor sees raw BGR bytes. Its BGR swizzle restores
        // logical red when sampling, independent of the render-target format.
        id<MTLTexture> raw=[texture newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm];
        assert(raw);
        [raw getBytes:bytes bytesPerRow:4 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
        assert(bytes[0]==0 && bytes[2]==255);
        assert(Gpu::Metal::ColorTargetFormat(10,6,1).format==MTLPixelFormatBGRA8Unorm_sRGB);
        puts("Metal color: alternate CB stores red in the correct guest byte; RGBA views preserve it");
    }
}
