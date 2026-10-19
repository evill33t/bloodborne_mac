// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace Gpu::Metal::NativeVertexFetch {
inline constexpr std::uint64_t Hash32=0xcabdbcf01724aad6ull;
inline constexpr std::uint64_t Hash36=0x3f3f6839e54e0136ull;
inline constexpr std::uint64_t CacheTag=0x5645525446583031ull;
struct Description {
    std::uint32_t stride,attributes,first_pc,last_pc,fetch_bytes;
};
inline constexpr Description Layout32{32,5,0x100018,0x100038,72};
inline constexpr Description Layout36{36,6,0x10001c,0x100044,84};
inline constexpr const Description* Layout(std::uint64_t hash) {
    return hash==Hash32 ? &Layout32 : hash==Hash36 ? &Layout36 : nullptr;
}
// Captured descriptor metadata only; no original executable/shader bytes.
inline constexpr std::array<std::uint32_t,6> Formats{
    0x2006f3ac,0x20054fac,0x20054fac,0x20050fac,0x20065fac,0x2002d02c};
inline constexpr std::array<std::uint32_t,6> Offsets{0,12,16,20,24,32};
inline constexpr const char* Msl() {
    return R"MSL(
struct DcVertexFetch { uint4 a0,a1,a2,a3,a4,a5; };
// A single translation is safe only when the complete record stays inside
// one guest page, one physical buffer, and its physical byte extent. Boundary
// records use the original individual typed fetches instead.
static inline device uint* dc_vertex_record(thread const DcMem& m,ulong a,uint bytes) {
    uint shift=m.map->shift;
    if((a&3ul)!=0ul || shift<14u || shift>21u ||
       ((a^(a+ulong(bytes-1u)))>>shift)!=0ul) return nullptr;
    ulong page=(a-m.map->base)>>shift;
    if(page>=m.map->pages) return nullptr;
    int delta=((device const int*)(m.map+1))[page];
    if(delta==int(0x80000000u)) return nullptr;
    ulong physical=a+ulong(long(delta)<<14);
    if(physical>=m.map->limit || ulong(bytes)>m.map->limit-physical) return nullptr;
    if(physical<m.map->split) {
        if(ulong(bytes)>m.map->split-physical) return nullptr;
        return m.lo+(physical>>2);
    }
    return m.hi+((physical-m.map->split)>>2);
}
static inline uint4 dc_vertex_bytes(uint word) {
    return uint4(word&255u,(word>>8)&255u,(word>>16)&255u,word>>24);
}
static inline bool dc_native_vertex_fetch(thread DcVertexFetch& o,thread const DcMem& m,
        uint vertex,uint stride,uint4 d0,uint4 d1,uint4 d2,uint4 d3,uint4 d4,uint4 d5) {
    if(stride!=32u && stride!=36u) return false;
    uint layout=stride<<16;
    if((d0.y&0xfffff000u)!=layout || (d1.y&0xfffff000u)!=layout ||
       (d2.y&0xfffff000u)!=layout || (d3.y&0xfffff000u)!=layout ||
       (d4.y&0xfffff000u)!=layout || (stride==36u && (d5.y&0xfffff000u)!=layout)) return false;
    if(d0.w!=0x2006f3acu || d1.w!=0x20054facu || d2.w!=0x20054facu ||
       d3.w!=0x20050facu || d4.w!=0x20065facu || (stride==36u && d5.w!=0x2002d02cu)) return false;
    if(vertex>=d0.z || vertex>=d1.z || vertex>=d2.z || vertex>=d3.z || vertex>=d4.z ||
       (stride==36u && vertex>=d5.z)) return false;
    ulong base=dc_vbase(d0.x,d0.y);
    if(dc_vbase(d1.x,d1.y)!=base+12ul || dc_vbase(d2.x,d2.y)!=base+16ul ||
       dc_vbase(d3.x,d3.y)!=base+20ul || dc_vbase(d4.x,d4.y)!=base+24ul ||
       (stride==36u && dc_vbase(d5.x,d5.y)!=base+32ul)) return false;
    device uint* p=dc_vertex_record(m,base+ulong(vertex)*ulong(stride),stride);
    if(!p) return false;
    o.a0=uint4(p[0],p[1],p[2],0x3f800000u);
    o.a1=dc_vertex_bytes(p[3]);o.a2=dc_vertex_bytes(p[4]);
    // Reuse the exact reference conversion contract with fixed formats. Safe
    // Metal math preserves UNORM division and signed conversion rounding.
    o.a3=dc_fmt_convert(dc_vertex_bytes(p[5]),uint4(8u),0u,4u);
    o.a4=dc_fmt_convert(uint4(p[6]&65535u,p[6]>>16,p[7]&65535u,p[7]>>16),uint4(16u),5u,4u);
    o.a5=uint4(0u);
    if(stride==36u) {
        uint4 v=dc_fmt_convert(uint4(p[8]&65535u,p[8]>>16,0u,0u),uint4(16u),5u,2u);
        o.a5=uint4(v.xy,0u,0u);
    }
    return true;
}
)MSL";
}
} // namespace Gpu::Metal::NativeVertexFetch
