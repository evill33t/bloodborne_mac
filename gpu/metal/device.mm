// SPDX-License-Identifier: GPL-2.0-or-later
#include "device.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace Bb::Metal {
namespace {
std::runtime_error Error(const char* operation, NSError* error) {
    return std::runtime_error(std::string(operation) + ": " +
        (error ? error.localizedDescription.UTF8String : "Metal returned no object"));
}
size_t PixelBytes(MTLPixelFormat format) {
    switch (format) {
    case MTLPixelFormatR8Unorm: case MTLPixelFormatR8Uint: return 1;
    case MTLPixelFormatRG8Unorm: case MTLPixelFormatR16Float: case MTLPixelFormatR16Uint: return 2;
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRG16Float: case MTLPixelFormatR32Float: case MTLPixelFormatR32Uint: return 4;
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatRG32Float: return 8;
    case MTLPixelFormatRGBA32Float: return 16;
    default: throw std::runtime_error("Unsupported Metal texture transfer format");
    }
}
}

Device::Device() : device_(MTLCreateSystemDefaultDevice()), queue_([device_ newCommandQueue]) {
    if (!device_ || !queue_) throw std::runtime_error("No accessible Metal device/command queue");
    queue_.label = @"Bloodborne Metal";
}

id<MTLFunction> Device::Compile(const Shader& shader) const {
    NSError* error = nil;
    MTLCompileOptions* options = [MTLCompileOptions new];
    options.languageVersion = MTLLanguageVersion2_4;
    // Preserve shader arithmetic until game-specific quality/performance tests exist.
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    NSString* source = [[NSString alloc] initWithBytes:shader.source.data()
        length:shader.source.size() encoding:NSUTF8StringEncoding];
    if (!source) throw std::runtime_error("Invalid UTF-8 Metal shader source");
    id<MTLLibrary> library = [device_ newLibraryWithSource:source options:options error:&error];
    if (!library) throw Error("Metal shader compilation", error);
    // SPIRV-Cross preserves specialization constants, including defaults. Metal
    // requires specialization even when no override values are supplied.
    MTLFunctionConstantValues* constants = [MTLFunctionConstantValues new];
    id<MTLFunction> function = [library newFunctionWithName:
        [NSString stringWithUTF8String:shader.entry_point.c_str()]
        constantValues:constants error:&error];
    if (!function) throw Error("Metal shader entry point/specialization", error);
    return function;
}

id<MTLComputePipelineState> Device::ComputePipeline(const Shader& shader) const {
    if (shader.stage != Stage::Compute) throw std::runtime_error("Expected a compute shader");
    NSError* error = nil;
    id<MTLComputePipelineState> pipeline = [device_ newComputePipelineStateWithFunction:Compile(shader)
        error:&error];
    if (!pipeline) throw Error("Metal compute pipeline", error);
    const auto& w = shader.workgroup_size;
    const MTLSize limit = device_.maxThreadsPerThreadgroup;
    const uint64_t total = uint64_t(w[0]) * w[1] * w[2];
    if (w[0] > limit.width || w[1] > limit.height || w[2] > limit.depth ||
        total > pipeline.maxTotalThreadsPerThreadgroup) {
        throw std::runtime_error("Shader workgroup exceeds Metal device limits");
    }
    return pipeline;
}

id<MTLRenderPipelineState> Device::RenderPipeline(const Shader& vertex, const Shader& fragment,
                                                MTLPixelFormat format) const {
    if (vertex.stage != Stage::Vertex || fragment.stage != Stage::Fragment) {
        throw std::runtime_error("Expected vertex and fragment shaders");
    }
    MTLRenderPipelineDescriptor* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = Compile(vertex);
    descriptor.fragmentFunction = Compile(fragment);
    descriptor.colorAttachments[0].pixelFormat = format;
    NSError* error = nil;
    id<MTLRenderPipelineState> pipeline = [device_ newRenderPipelineStateWithDescriptor:descriptor
        error:&error];
    if (!pipeline) throw Error("Metal render pipeline", error);
    return pipeline;
}

id<MTLBuffer> Device::Buffer(size_t size, const void* initial) const {
    if (!size || size > device_.maxBufferLength) throw std::runtime_error("Invalid Metal buffer size");
    id<MTLBuffer> buffer = [device_ newBufferWithLength:size options:MTLResourceStorageModeShared];
    if (!buffer) throw std::runtime_error("Metal buffer allocation failed");
    if (initial) std::memcpy(buffer.contents, initial, size);
    return buffer;
}

id<MTLTexture> Device::Texture2D(uint32_t width, uint32_t height, MTLPixelFormat format,
                               MTLTextureUsage usage) const {
    if (!width || !height) throw std::runtime_error("Metal textures require nonzero dimensions");
    MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:width height:height mipmapped:NO];
    descriptor.storageMode = MTLStorageModePrivate;
    descriptor.usage = usage;
    id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
    if (!texture) throw std::runtime_error("Metal texture allocation failed");
    return texture;
}

id<MTLCommandBuffer> Device::Commands(const char* label) const {
    id<MTLCommandBuffer> commands = [queue_ commandBuffer];
    if (!commands) throw std::runtime_error("Metal command buffer allocation failed");
    commands.label = [NSString stringWithUTF8String:label];
    return commands;
}

void Device::SubmitAndWait(id<MTLCommandBuffer> commands) {
    if (!commands) throw std::runtime_error("No Metal command buffer to submit");
    [commands commit];
    [commands waitUntilCompleted];
    if (commands.status != MTLCommandBufferStatusCompleted) throw Error("Metal submission", commands.error);
}

std::vector<uint8_t> Device::ReadTexture(id<MTLTexture> texture) const {
    if (!texture || texture.device != device_ || texture.textureType != MTLTextureType2D ||
        texture.sampleCount != 1) throw std::runtime_error("Unsupported Metal readback");
    const size_t bytes_per_pixel = PixelBytes(texture.pixelFormat);
    const size_t row_size = texture.width * bytes_per_pixel;
    const size_t row_stride = (row_size + 255) & ~size_t{255};
    if (texture.height > std::numeric_limits<size_t>::max() / row_stride) {
        throw std::runtime_error("Metal readback size overflow");
    }
    id<MTLBuffer> staging = Buffer(row_stride * texture.height);
    id<MTLCommandBuffer> commands = Commands("Texture readback");
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(texture.width, texture.height, 1) toBuffer:staging destinationOffset:0
        destinationBytesPerRow:row_stride destinationBytesPerImage:row_stride * texture.height];
    [blit endEncoding];
    SubmitAndWait(commands);
    std::vector<uint8_t> data(row_size * texture.height);
    for (size_t y = 0; y < texture.height; ++y) {
        std::memcpy(data.data() + y * row_size,
            static_cast<const uint8_t*>(staging.contents) + y * row_stride, row_size);
    }
    return data;
}

void Device::UploadTexture(id<MTLTexture> texture, std::span<const uint8_t> data) const {
    if (!texture || texture.device != device_ || texture.textureType != MTLTextureType2D ||
        texture.sampleCount != 1) throw std::runtime_error("Unsupported Metal upload");
    const size_t row_size = texture.width * PixelBytes(texture.pixelFormat);
    if (texture.height > std::numeric_limits<size_t>::max() / row_size ||
        data.size() != row_size * texture.height) throw std::runtime_error("Metal upload size mismatch");
    const size_t row_stride = (row_size + 255) & ~size_t{255};
    if (texture.height > std::numeric_limits<size_t>::max() / row_stride) {
        throw std::runtime_error("Metal upload size overflow");
    }
    id<MTLBuffer> staging = Buffer(row_stride * texture.height);
    for (size_t y = 0; y < texture.height; ++y) {
        std::memcpy(static_cast<uint8_t*>(staging.contents) + y * row_stride,
            data.data() + y * row_size, row_size);
    }
    id<MTLCommandBuffer> commands = Commands("Texture upload");
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromBuffer:staging sourceOffset:0 sourceBytesPerRow:row_stride
        sourceBytesPerImage:row_stride * texture.height sourceSize:MTLSizeMake(texture.width, texture.height, 1)
        toTexture:texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    SubmitAndWait(commands);
}

void Device::ConfigureLayer(CAMetalLayer* layer, uint32_t width, uint32_t height) const {
    if (!layer || !width || !height) throw std::runtime_error("Invalid Metal presentation layer");
    layer.device = device_;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO; // presentation currently uses a blit
    layer.drawableSize = CGSizeMake(width, height);
    layer.maximumDrawableCount = 3;
}

void Device::Present(CAMetalLayer* layer, id<MTLTexture> source) const {
    if (!layer || layer.device != device_ || !source || source.device != device_) {
        throw std::runtime_error("Metal presentation resources belong to different devices");
    }
    id<CAMetalDrawable> drawable = [layer nextDrawable];
    if (!drawable) return; // minimized/occluded window
    if (source.width != drawable.texture.width || source.height != drawable.texture.height ||
        source.pixelFormat != drawable.texture.pixelFormat || source.sampleCount != 1) {
        throw std::runtime_error("Metal presentation texture does not match the drawable");
    }
    id<MTLCommandBuffer> commands = Commands("Present");
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromTexture:source sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(source.width, source.height, 1) toTexture:drawable.texture
        destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [commands presentDrawable:drawable];
    // Keeping the initial path bounded also propagates GPU errors to its caller.
    SubmitAndWait(commands);
}
} // namespace Bb::Metal
