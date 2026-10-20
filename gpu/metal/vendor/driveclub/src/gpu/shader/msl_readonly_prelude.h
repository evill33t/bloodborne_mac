// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/shader/msl_prelude.h"

namespace Gpu::Shader {
// Only a proven writer-free ordinary VS/PS may use this prelude. Values remain
// GPU-time device loads. Store/atomic helpers are absent, and no pointer casts
// discard const. Numeric conversion, packed formats and ALU code stay shared.
inline constexpr char kMslReadOnlyTypes[] = R"MSL(#include <metal_stdlib>
using namespace metal;

// Guest VA -> physical page table: a header followed by one entry per page,
// the page's (phys - va) in 16 KB units (INT_MIN = unmapped).
struct DcMap { ulong base; ulong pages; ulong split; uint shift; uint pad; ulong limit; };
struct DcDraw { float4 vp_scale; float4 vp_offset; float2 rt_scale; uint vte; uint pad; };
struct DcMem { device const uint* lo; device const uint* hi; device const DcMap* map; };
struct DcTess { uint base_vertex; uint patch_count; uint index_size; uint reserved; ulong index_base; };

)MSL";

inline constexpr char kMslReadOnlyMemoryRead[] = R"MSL(// ---- guest memory ----
static inline device const uint* dc_ptr(thread const DcMem& m, ulong va) {
    ulong i = (va - m.map->base) >> m.map->shift;
    if (i >= m.map->pages) return nullptr;
    int e = ((device const int*)(m.map + 1))[i];
    if (e == int(0x80000000u)) return nullptr;
    ulong p = va + ulong(long(e) << 14);
    if (p >= m.map->limit) return nullptr;
    return p < m.map->split ? m.lo + (p >> 2) : m.hi + ((p - m.map->split) >> 2);
}
// `bytes` contiguous bytes at dword-aligned `va` (one translation for a
// multi-dword load), or nullptr.
static inline device const uint* dc_ptr_n(thread const DcMem& m, ulong va, uint bytes) {
    device const uint* p = dc_ptr(m, va);
    ulong last = va + ulong(bytes - 4u);
    if (((va ^ last) >> m.map->shift) != 0ul && dc_ptr(m, last) != p + ((bytes - 4u) >> 2)) return nullptr;
    return p;
}
static inline uint dc_ld(thread const DcMem& m, ulong va) {
    device const uint* p = dc_ptr(m, va & ~3ul);
    uint lo = p ? *p : 0u;
    uint sh = uint(va & 3ul) * 8u;
    if (sh == 0u) return lo;
    device const uint* q = dc_ptr(m, (va & ~3ul) + 4ul);
    uint hi = q ? *q : 0u;
    return (lo >> sh) | (hi << (32u - sh));
}
)MSL";

inline constexpr auto kMslReadOnlyPreludeStorage=MslPreludeConcat(
    kMslReadOnlyTypes,kMslReadOnlyMemoryRead,
    kMslPreludeFormatUtils,kMslPreludeFormatLoadPrefix,"const uint",kMslPreludeFormatLoadSuffix,
    kMslPreludeCommon);
inline constexpr const char* kMslReadOnlyPrelude=kMslReadOnlyPreludeStorage.data();
} // namespace Gpu::Shader
