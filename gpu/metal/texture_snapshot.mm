// SPDX-License-Identifier: GPL-2.0-or-later
#include "texture_snapshot.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <bit>
#include <cstring>

namespace Gpu::Metal::TextureSnapshot {
namespace {
struct Mip {std::size_t width,height,row,offset,size;};
bool Layout(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLTexture> texture,
            std::vector<Mip>& mips,std::size_t& total,std::string& error) {
    if(!device || !queue || !texture || queue.device!=device || texture.device!=device ||
       texture.textureType!=MTLTextureType2D || texture.sampleCount!=1 ||
       texture.storageMode==MTLStorageModeMemoryless ||
       (texture.pixelFormat!=MTLPixelFormatRGBA8Unorm && texture.pixelFormat!=MTLPixelFormatRGBA8Unorm_sRGB) ||
       !texture.width || !texture.height || texture.width>1024 || texture.height>1024 ||
       !texture.mipmapLevelCount || texture.mipmapLevelCount>NSUInteger(std::bit_width(std::max(texture.width,texture.height)))) {
        error="Unsupported checkpoint texture or Metal device.";return false;
    }
    total=0;
    for(NSUInteger level=0;level<texture.mipmapLevelCount;++level) {
        Mip mip{};mip.width=std::max<NSUInteger>(texture.width>>level,1);
        mip.height=std::max<NSUInteger>(texture.height>>level,1);
        mip.row=(mip.width*4+255)&~std::size_t(255);
        mip.offset=total;mip.size=mip.row*mip.height;total+=mip.size;mips.push_back(mip);
    }
    return true;
}
bool Complete(id<MTLCommandBuffer> command,std::string& error) {
    [command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted) {
        error="Metal checkpoint transfer failed.";
        if(const char* message=command.error.localizedDescription.UTF8String)error+=" "+std::string(message);
        return false;
    }
    return true;
}
}
bool Capture(void* device_handle,void* queue_handle,void* texture_handle,
             MaterialSnapshot::Texture& out,std::string& error) {
    @autoreleasepool {
        id<MTLDevice> device=(__bridge id<MTLDevice>)device_handle;
        id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)queue_handle;
        id<MTLTexture> texture=(__bridge id<MTLTexture>)texture_handle;
        std::vector<Mip> mips;std::size_t total=0;
        if(!Layout(device,queue,texture,mips,total,error))return false;
        id<MTLBuffer> buffer=[device newBufferWithLength:total options:MTLResourceStorageModeShared];
        if(!buffer){error="Could not allocate Metal checkpoint readback.";return false;}
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
        if(!command || !encoder){error="Could not create Metal checkpoint readback.";return false;}
        for(std::size_t level=0;level<mips.size();++level) {
            const auto& mip=mips[level];
            [encoder copyFromTexture:texture sourceSlice:0 sourceLevel:level sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(mip.width,mip.height,1) toBuffer:buffer destinationOffset:mip.offset
                destinationBytesPerRow:mip.row destinationBytesPerImage:mip.size];
        }
        [encoder endEncoding];if(!Complete(command,error))return false;
        MaterialSnapshot::Texture captured;captured.width=texture.width;captured.height=texture.height;
        captured.format=texture.pixelFormat;
        for(const auto& mip:mips) {
            std::vector<std::uint8_t> bytes(mip.width*mip.height*4);
            const auto* data=static_cast<const std::uint8_t*>(buffer.contents)+mip.offset;
            for(std::size_t row=0;row<mip.height;++row)
                std::memcpy(bytes.data()+row*mip.width*4,data+row*mip.row,mip.width*4);
            captured.mips.push_back(std::move(bytes));
        }
        out=std::move(captured);error.clear();return true;
    }
}
bool Restore(void* device_handle,void* queue_handle,void* texture_handle,
             const MaterialSnapshot::Texture& captured,std::string& error) {
    @autoreleasepool {
        id<MTLDevice> device=(__bridge id<MTLDevice>)device_handle;
        id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)queue_handle;
        id<MTLTexture> texture=(__bridge id<MTLTexture>)texture_handle;
        std::vector<Mip> mips;std::size_t total=0;
        if(!Layout(device,queue,texture,mips,total,error))return false;
        if(captured.width!=texture.width || captured.height!=texture.height ||
           captured.format!=texture.pixelFormat || captured.mips.size()!=mips.size()) {
            error="Checkpoint texture layout differs from the destination.";return false;
        }
        for(std::size_t level=0;level<mips.size();++level)
            if(captured.mips[level].size()!=mips[level].width*mips[level].height*4) {
                error="Invalid checkpoint texture mip size.";return false;
            }
        id<MTLBuffer> buffer=[device newBufferWithLength:total options:MTLResourceStorageModeShared];
        if(!buffer){error="Could not allocate Metal checkpoint upload.";return false;}
        std::memset(buffer.contents,0,total);
        for(std::size_t level=0;level<mips.size();++level) {
            const auto& mip=mips[level];auto* data=static_cast<std::uint8_t*>(buffer.contents)+mip.offset;
            for(std::size_t row=0;row<mip.height;++row)
                std::memcpy(data+row*mip.row,captured.mips[level].data()+row*mip.width*4,mip.width*4);
        }
        id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
        if(!command || !encoder){error="Could not create Metal checkpoint upload.";return false;}
        for(std::size_t level=0;level<mips.size();++level) {
            const auto& mip=mips[level];
            [encoder copyFromBuffer:buffer sourceOffset:mip.offset sourceBytesPerRow:mip.row sourceBytesPerImage:mip.size
                sourceSize:MTLSizeMake(mip.width,mip.height,1) toTexture:texture destinationSlice:0 destinationLevel:level
                destinationOrigin:MTLOriginMake(0,0,0)];
        }
        [encoder endEncoding];if(!Complete(command,error))return false;
        error.clear();return true;
    }
}
}
