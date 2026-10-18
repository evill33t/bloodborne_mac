// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <memory>
namespace Gpu::Metal {
class SpatialUpscaler {
public:
    explicit SpatialUpscaler(void* device);
    ~SpatialUpscaler();
    bool Supported() const;
    // command buffer and texture are native Metal objects. Returns the input
    // when no upscale is needed or MetalFX cannot create a matching scaler.
    void* Encode(void* command,void* input,int width,int height);
    const char* Status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
