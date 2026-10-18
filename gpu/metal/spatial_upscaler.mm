// SPDX-License-Identifier: GPL-2.0-or-later
#include "spatial_upscaler.h"
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

namespace Gpu::Metal {
struct SpatialUpscaler::Impl {
    id<MTLDevice> device;
    id<MTLFXSpatialScaler> scaler;
    id<MTLTexture> input, output;
    bool supported=false;
    const char* status="Off";
};
SpatialUpscaler::SpatialUpscaler(void* device):impl(std::make_unique<Impl>()) {
    impl->device=(__bridge id<MTLDevice>)device;
    impl->supported=[MTLFXSpatialScalerDescriptor supportsDevice:impl->device];
    if (!impl->supported) impl->status="MetalFX is unavailable on this GPU.";
}
SpatialUpscaler::~SpatialUpscaler()=default;
bool SpatialUpscaler::Supported() const { return impl->supported; }
const char* SpatialUpscaler::Status() const { return impl->status; }
void* SpatialUpscaler::Encode(void* command,void* input,int width,int height) {
    auto tex=(__bridge id<MTLTexture>)input;
    auto cmd=(__bridge id<MTLCommandBuffer>)command;
    if (!impl->supported || !tex || !cmd || width<1 || height<1) return input;
    if (width<int(tex.width) || height<int(tex.height) ||
        (width==int(tex.width) && height==int(tex.height))) {
        impl->status="MetalFX needs an output larger than the render resolution.";
        return input;
    }
    if (!impl->scaler || impl->scaler.inputWidth!=tex.width || impl->scaler.inputHeight!=tex.height ||
        impl->scaler.outputWidth!=NSUInteger(width) || impl->scaler.outputHeight!=NSUInteger(height) ||
        impl->scaler.colorTextureFormat!=tex.pixelFormat) {
        impl->scaler=nil; impl->input=nil; impl->output=nil;
        auto desc=[MTLFXSpatialScalerDescriptor new];
        desc.inputWidth=tex.width; desc.inputHeight=tex.height;
        desc.outputWidth=width; desc.outputHeight=height;
        desc.colorTextureFormat=tex.pixelFormat; desc.outputTextureFormat=MTLPixelFormatBGRA8Unorm;
        desc.colorProcessingMode=MTLFXSpatialScalerColorProcessingModePerceptual;
        impl->scaler=[desc newSpatialScalerWithDevice:impl->device];
        if (!impl->scaler) { impl->status="MetalFX could not create this scale; using bilinear."; return input; }
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:tex.pixelFormat
            width:tex.width height:tex.height mipmapped:NO];
        d.storageMode=MTLStorageModePrivate; d.usage=impl->scaler.colorTextureUsage;
        impl->input=[impl->device newTextureWithDescriptor:d];
        d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
            width:width height:height mipmapped:NO];
        d.storageMode=MTLStorageModePrivate;
        d.usage=impl->scaler.outputTextureUsage|MTLTextureUsageShaderRead;
        impl->output=[impl->device newTextureWithDescriptor:d];
        if (!impl->input || !impl->output) {
            impl->status="MetalFX could not allocate its textures; using bilinear.";
            impl->scaler=nil; return input;
        }
    }
    // MetalFX exposes its required usage flags; copy only when the game's
    // texture does not have them. Commands share the renderer's ordered queue.
    id<MTLTexture> color=tex;
    if ((tex.usage & impl->scaler.colorTextureUsage)!=impl->scaler.colorTextureUsage) {
        auto blit=[cmd blitCommandEncoder];
        [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(tex.width,tex.height,1) toTexture:impl->input destinationSlice:0
            destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit endEncoding]; color=impl->input;
    }
    impl->scaler.colorTexture=color;
    impl->scaler.outputTexture=impl->output;
    [impl->scaler encodeToCommandBuffer:cmd];
    impl->status="MetalFX Spatial active";
    return (__bridge void*)impl->output;
}
}
