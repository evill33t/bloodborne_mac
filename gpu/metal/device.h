// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "shader.h"

namespace Bb::Metal {

// Objective-C ownership is handled by ARC. Encoders and resources stay native
// Metal objects so a future PM4 rasterizer need not emulate Vulkan semantics.
class Device {
public:
    Device();
    id<MTLDevice> Native() const { return device_; }
    id<MTLCommandQueue> Queue() const { return queue_; }
    id<MTLFunction> Compile(const Shader& shader) const;
    id<MTLComputePipelineState> ComputePipeline(const Shader& shader) const;
    id<MTLRenderPipelineState> RenderPipeline(const Shader& vertex, const Shader& fragment,
                                             MTLPixelFormat format) const;
    id<MTLBuffer> Buffer(size_t size, const void* initial = nullptr) const;
    id<MTLTexture> Texture2D(uint32_t width, uint32_t height, MTLPixelFormat format,
                             MTLTextureUsage usage) const;
    id<MTLCommandBuffer> Commands(const char* label) const;
    static void SubmitAndWait(id<MTLCommandBuffer> commands);
    // Synchronizes through the queue and stages private GPU memory into shared
    // CPU memory, with Metal's buffer-copy row alignment.
    std::vector<uint8_t> ReadTexture(id<MTLTexture> texture) const;
    void UploadTexture(id<MTLTexture> texture, std::span<const uint8_t> data) const;
    void ConfigureLayer(CAMetalLayer* layer, uint32_t pixel_width, uint32_t pixel_height) const;
    void Present(CAMetalLayer* layer, id<MTLTexture> source) const;

private:
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
};
} // namespace Bb::Metal
