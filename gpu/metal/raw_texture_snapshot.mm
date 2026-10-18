// SPDX-License-Identifier: GPL-2.0-or-later
#include "raw_texture_snapshot.h"
#import <Metal/Metal.h>
#include <algorithm>
#include <bit>
#include <cstring>
#include <thread>

namespace Gpu::Metal::RawTextureSnapshot {
namespace {
std::uint32_t TexelBytes(MTLPixelFormat format) {
    switch(format) {
    case MTLPixelFormatA8Unorm:case MTLPixelFormatR8Unorm:case MTLPixelFormatR8Snorm:
    case MTLPixelFormatR8Uint:case MTLPixelFormatR8Sint:case MTLPixelFormatStencil8:return 1;
    case MTLPixelFormatR16Unorm:case MTLPixelFormatR16Snorm:case MTLPixelFormatR16Uint:case MTLPixelFormatR16Sint:case MTLPixelFormatR16Float:
    case MTLPixelFormatRG8Unorm:case MTLPixelFormatRG8Snorm:case MTLPixelFormatRG8Uint:case MTLPixelFormatRG8Sint:case MTLPixelFormatDepth16Unorm:return 2;
    case MTLPixelFormatR32Uint:case MTLPixelFormatR32Sint:case MTLPixelFormatR32Float:
    case MTLPixelFormatRG16Unorm:case MTLPixelFormatRG16Snorm:case MTLPixelFormatRG16Uint:case MTLPixelFormatRG16Sint:case MTLPixelFormatRG16Float:
    case MTLPixelFormatRGBA8Unorm:case MTLPixelFormatRGBA8Unorm_sRGB:case MTLPixelFormatRGBA8Snorm:case MTLPixelFormatRGBA8Uint:case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatBGRA8Unorm:case MTLPixelFormatBGRA8Unorm_sRGB:case MTLPixelFormatRGB10A2Unorm:case MTLPixelFormatRGB10A2Uint:
    case MTLPixelFormatBGR10A2Unorm:case MTLPixelFormatRG11B10Float:case MTLPixelFormatRGB9E5Float:case MTLPixelFormatDepth32Float:return 4;
    case MTLPixelFormatRG32Uint:case MTLPixelFormatRG32Sint:case MTLPixelFormatRG32Float:
    case MTLPixelFormatRGBA16Unorm:case MTLPixelFormatRGBA16Snorm:case MTLPixelFormatRGBA16Uint:case MTLPixelFormatRGBA16Sint:case MTLPixelFormatRGBA16Float:return 8;
    case MTLPixelFormatRGBA32Uint:case MTLPixelFormatRGBA32Sint:case MTLPixelFormatRGBA32Float:return 16;
    default:return 0;
    }
}
bool DescriptorLayout(id<MTLDevice> device,const Descriptor& d,std::vector<Plane>& planes,std::string& error) {
    if(!device || !d.width || !d.height || !d.depth || !d.layers || d.width>16384 || d.height>16384 ||
       d.depth>2048 || d.layers>2048 || !d.levels || d.levels>std::bit_width(std::max({d.width,d.height,d.depth}))) {
        error="Unsupported checkpoint texture dimensions.";return false;
    }
    const auto type=MTLTextureType(d.type);
    const bool volume=type==MTLTextureType3D;
    std::uint32_t slices=1;
    switch(type) {
    case MTLTextureType2D:if(d.layers!=1 || d.depth!=1) {error="Invalid 2D checkpoint texture.";return false;}break;
    case MTLTextureType3D:if(d.layers!=1) {error="Invalid 3D checkpoint texture.";return false;}break;
    case MTLTextureType2DArray:if(d.depth!=1) {error="Invalid array checkpoint texture.";return false;}slices=d.layers;break;
    case MTLTextureTypeCube:case MTLTextureTypeCubeArray:
        if(d.depth!=1 || d.width!=d.height || (type==MTLTextureTypeCube && d.layers!=1)) {
            error="Invalid cube checkpoint texture.";return false;
        }
        slices=d.layers*6;break;
    default:error="Unsupported checkpoint texture type.";return false;
    }
    const auto format=MTLPixelFormat(d.format);
    const bool combined=format==MTLPixelFormatDepth32Float_Stencil8;
    const auto bytes=combined?4:TexelBytes(format);
    if(!bytes){error="Unsupported raw checkpoint texture format.";return false;}
    if(std::uint64_t(slices)*d.levels*(combined?2:1)>65536) {error="Too many checkpoint texture planes.";return false;}
    std::vector<Plane> result;
    for(std::uint32_t level=0;level<d.levels;++level)for(std::uint32_t slice=0;slice<slices;++slice) {
        for(unsigned aspect=0;aspect<(combined?2u:1u);++aspect) {
            Plane p;p.level=level;p.slice=slice;p.width=std::max(1u,d.width>>level);
            p.height=std::max(1u,d.height>>level);p.depth=volume?std::max(1u,d.depth>>level):1;
            p.texel_bytes=combined && aspect?1:bytes;
            p.aspect=combined?(aspect?Aspect::Stencil:Aspect::Depth):format==MTLPixelFormatStencil8?Aspect::Stencil:
                (format==MTLPixelFormatDepth32Float || format==MTLPixelFormatDepth16Unorm)?Aspect::Depth:Aspect::Color;
            p.row_bytes=(std::uint64_t(p.width)*p.texel_bytes+255)&~255ull;
            p.image_bytes=p.row_bytes*p.height;p.size=p.image_bytes*p.depth;
            if(p.size>device.maxBufferLength || p.size>(1ull<<30)) {error="Texture plane exceeds the checkpoint staging limit.";return false;}
            result.push_back(p);
        }
    }
    planes=std::move(result);error.clear();return true;
}
bool TextureLayout(id<MTLTexture> texture,Descriptor& out,std::vector<Plane>& planes,std::string& error) {
    if(!texture || texture.sampleCount!=1 || texture.storageMode==MTLStorageModeMemoryless) {
        error="Unsupported checkpoint texture storage/sample count.";return false;
    }
    Descriptor d{std::uint32_t(texture.pixelFormat),std::uint32_t(texture.textureType),std::uint32_t(texture.width),
        std::uint32_t(texture.height),std::uint32_t(texture.depth),std::uint32_t(texture.arrayLength),std::uint32_t(texture.mipmapLevelCount)};
    if(!DescriptorLayout(texture.device,d,planes,error))return false;
    out=d;return true;
}
MTLBlitOption Option(id<MTLTexture> texture,const Plane& p) {
    if(texture.pixelFormat!=MTLPixelFormatDepth32Float_Stencil8)return MTLBlitOptionNone;
    return p.aspect==Aspect::Stencil?MTLBlitOptionStencilFromDepthStencil:MTLBlitOptionDepthFromDepthStencil;
}
bool Complete(id<MTLCommandBuffer> command,std::chrono::steady_clock::time_point deadline,std::string& error) {
    [command commit];
    while(command.status!=MTLCommandBufferStatusCompleted) {
        if(command.status==MTLCommandBufferStatusError) {
            error="Raw Metal checkpoint transfer failed.";
            if(const char* message=command.error.localizedDescription.UTF8String)error+=" "+std::string(message);return false;
        }
        if(std::chrono::steady_clock::now()>=deadline){error="Raw Metal checkpoint transfer timed out.";return false;}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
bool Setup(void* d,void* q,void* t,std::size_t index,Plane& p,std::string& error) {
    id<MTLDevice> device=(__bridge id<MTLDevice>)d;id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)q;
    id<MTLTexture> texture=(__bridge id<MTLTexture>)t;
    if(!device || !queue || !texture || queue.device!=device || texture.device!=device) {error="Checkpoint texture belongs to another Metal device.";return false;}
    Descriptor descriptor;std::vector<Plane> planes;
    if(!TextureLayout(texture,descriptor,planes,error))return false;
    if(index>=planes.size()){error="Checkpoint texture plane is out of range.";return false;}
    p=planes[index];return true;
}
}
bool Describe(void* texture,Descriptor& descriptor,std::vector<Plane>& planes,std::string& error) {
    @autoreleasepool {return TextureLayout((__bridge id<MTLTexture>)texture,descriptor,planes,error);}
}
bool Layout(void* device,const Descriptor& descriptor,std::vector<Plane>& planes,std::string& error) {
    @autoreleasepool {return DescriptorLayout((__bridge id<MTLDevice>)device,descriptor,planes,error);}
}
bool Capture(void* d,void* q,void* t,std::size_t index,std::vector<std::uint8_t>& out,
             std::chrono::steady_clock::time_point deadline,std::string& error) {
    @autoreleasepool {
        Plane p;if(!Setup(d,q,t,index,p,error))return false;
        if(std::chrono::steady_clock::now()>=deadline){error="Raw texture capture deadline has expired.";return false;}
        id<MTLDevice> device=(__bridge id<MTLDevice>)d;id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)q;
        id<MTLTexture> texture=(__bridge id<MTLTexture>)t;
        id<MTLBuffer> buffer=[device newBufferWithLength:p.size options:MTLResourceStorageModeShared];
        if(!buffer){error="Could not allocate raw Metal checkpoint readback.";return false;}
        id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
        if(!command || !blit){error="Could not create raw Metal checkpoint readback.";return false;}
        std::memset(buffer.contents,0,p.size);
        [blit copyFromTexture:texture sourceSlice:p.slice sourceLevel:p.level sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(p.width,p.height,p.depth) toBuffer:buffer destinationOffset:0 destinationBytesPerRow:p.row_bytes
            destinationBytesPerImage:p.image_bytes options:Option(texture,p)];
        [blit endEncoding];if(!Complete(command,deadline,error))return false;
        std::vector<std::uint8_t> bytes(p.size);std::memcpy(bytes.data(),buffer.contents,p.size);out=std::move(bytes);error.clear();return true;
    }
}
bool Restore(void* d,void* q,void* t,std::size_t index,std::span<const std::uint8_t> bytes,
             std::chrono::steady_clock::time_point deadline,std::string& error) {
    @autoreleasepool {
        Plane p;if(!Setup(d,q,t,index,p,error))return false;
        if(bytes.size()!=p.size){error="Raw checkpoint texture plane has the wrong size.";return false;}
        if(std::chrono::steady_clock::now()>=deadline){error="Raw texture restore deadline has expired.";return false;}
        id<MTLDevice> device=(__bridge id<MTLDevice>)d;id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)q;
        id<MTLTexture> texture=(__bridge id<MTLTexture>)t;
        id<MTLBuffer> buffer=[device newBufferWithLength:p.size options:MTLResourceStorageModeShared];
        if(!buffer){error="Could not allocate raw Metal checkpoint upload.";return false;}
        id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
        if(!command || !blit){error="Could not create raw Metal checkpoint upload.";return false;}
        std::memcpy(buffer.contents,bytes.data(),p.size);
        [blit copyFromBuffer:buffer sourceOffset:0 sourceBytesPerRow:p.row_bytes sourceBytesPerImage:p.image_bytes
            sourceSize:MTLSizeMake(p.width,p.height,p.depth) toTexture:texture destinationSlice:p.slice destinationLevel:p.level
            destinationOrigin:MTLOriginMake(0,0,0) options:Option(texture,p)];
        [blit endEncoding];if(!Complete(command,deadline,error))return false;error.clear();return true;
    }
}
}
