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
        // Visible width/height in pixels; pitch in texels or 4x4 BC blocks.
        std::uint32_t width=0,height=0,pitch=0,tile=0,slice=0,level=0;
    };
    struct Upload {
        const void* source=nullptr;
        std::uint64_t bytes=0; // full physical allocation, including padding
        std::vector<Level> levels; // every visible slice/mip exactly once
    };
    struct InitialHashes {
        // Published only for Encoded. Fallback/Failed leave this empty.
        std::vector<std::uint64_t> chunks;
        // Actual CPU bytes hashed, even if a later encoder cannot be created.
        std::uint64_t bytes_hashed=0;
    };
    struct Stats {
        // Successful uploads only. Packed uploads preserve every original
        // mip/slice job while issuing one compute dispatch for the group.
        std::uint64_t uploads=0,jobs=0,compute_dispatches=0,packed_uploads=0;
        std::uint64_t mapped_snapshots=0,mapped_bytes=0,mach_snapshots=0,mach_bytes=0;
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
    static std::uint32_t BytesPerElement(std::uint64_t pixel_format);
    static std::uint32_t BlockExtent(std::uint64_t pixel_format);
    bool SupportsFormat(std::uint64_t pixel_format) const;
    // Optional hashes cover the immutable snapshot, including its final partial
    // ContentHash chunk. Omitting this output avoids the additional CPU scan.
    Result Encode(void* command_buffer,void* destination_texture,const Upload&,
                  InitialHashes* initial_hashes=nullptr);
    Stats GetStats() const;
    const std::string& Error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Gpu::Metal
