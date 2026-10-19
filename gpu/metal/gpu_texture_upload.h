// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Gpu::Metal {

// An immutable CPU snapshot followed by raw-word GPU detiling and buffer-to-
// texture copies. No guest-memory reads occur after Encode returns. The caller
// supplies a command buffer with no active encoder and owns submission/waits.
class GpuTextureUploader {
public:
    struct Level {
        std::uint64_t src_offset=0;
        std::uint32_t width=0,height=0,pitch=0,tile=0,slice=0,level=0;
    };
    struct Upload {
        const void* source=nullptr;
        std::uint64_t bytes=0; // full physical allocation, including padding
        std::vector<Level> levels; // every visible slice/mip exactly once
    };
    enum class Result {
        Fallback, // no GPU commands encoded; the CPU path remains safe
        Encoded,
        Failed // commands already encoded; caller must fail execution
    };
    explicit GpuTextureUploader(void* device); // precompile during renderer Init
    ~GpuTextureUploader();
    bool Ready() const;
    // Exact raw-copy formats only; zero means the CPU fallback is required.
    static std::uint32_t BytesPerPixel(std::uint64_t pixel_format);
    static bool SupportsFormat(std::uint64_t pixel_format);
    Result Encode(void* command_buffer,void* destination_texture,const Upload&);
    const std::string& Error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Gpu::Metal
