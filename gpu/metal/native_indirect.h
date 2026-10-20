// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstdint>
#include <limits>

namespace Gpu::Metal::NativeIndirect {
struct Range {
    std::uint64_t address=0,bytes=0;
};
inline constexpr bool Valid(Range r) {
    return r.address && r.bytes && !(r.address&3) &&
        r.address<=std::numeric_limits<std::uint64_t>::max()-r.bytes;
}
inline constexpr bool Overlap(Range a,Range b) {
    // Writes need not be DWORD-aligned. Overflowing write ranges conservatively
    // conflict; the CP validates its own mapping before calling the renderer.
    if(!a.bytes || !b.bytes) return false;
    if(a.address>UINT64_MAX-a.bytes || b.address>UINT64_MAX-b.bytes) return true;
    return a.address<b.address+b.bytes && b.address<a.address+a.bytes;
}
// The current Metal vertex ABI supplies VGT_INDX_OFFSET as vertex_id and zero
// as the first instance. The PM4 scalar destinations need deferred register
// state when nonzero, so those draws deliberately retain the drained path.
inline constexpr bool DrawDestinationsSupported(std::uint32_t vertex,std::uint32_t instance) {
    return vertex==0 && instance==0;
}
inline constexpr const char* DrawAdapterMsl=R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void dc_indirect_draw(device const uint* source [[buffer(0)]],
        device uint* destination [[buffer(1)]],constant uint& vertex_start [[buffer(2)]]) {
    destination[0]=source[0];
    destination[1]=source[1];
    destination[2]=vertex_start;
    destination[3]=0u;
}
)MSL";
} // namespace Gpu::Metal::NativeIndirect
