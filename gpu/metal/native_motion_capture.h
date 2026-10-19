// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>

struct ElfImage;
namespace MacFeatures { void InstallMotionCapture(ElfImage* image); }

namespace Gpu::Metal::NativeMotionCapture {
// Copied observations only. These pointers never authorize a guest call or
// establish that the corresponding GPU texture belongs to the current frame.
struct Texture {
    std::uint64_t object=0,native_resource=0;
    std::uint32_t width=0,height=0;
    bool readable=false;
};
struct Observation {
    std::uint64_t sequence=0,monotonic_ns=0,setter_object=0;
    // Original SysV register arguments: rdi, rsi, rdx, rcx, r8, r9.
    std::array<std::uint64_t,6> arguments{};
    std::int32_t mode=0,velocity_format=0;
    std::uint8_t flag=0;
    std::array<float,2> scale{};
    bool scale_readable=false;
    Texture external,internal;
    // Exact S# copied from SetMotionBlurVelocitySource's descriptor argument
    // (RVA bb5430), not guessed from the resolved YEBIS object (RVA bd3c50).
    std::uint64_t descriptor_address=0,registration_sequence=0;
    std::array<std::uint32_t,8> source_descriptor{};
    std::uint32_t source_width=0,source_height=0,source_format=0;
    bool descriptor_readable=false,registration_matched=false;
};
// Disabled unless the checked hooks installed with BB_METAL_CAPTURE_DIR set.
bool Enabled();
// Thread-safe metadata snapshot for correlation at a renderer boundary. A
// latest CPU registration is not proof of GPU ordering, freshness or coverage.
bool Latest(Observation& observation);
}
