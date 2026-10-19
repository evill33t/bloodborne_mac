// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>

namespace Gpu::Metal {
struct NativeTessellationConfig {
    std::uint32_t local_layout=0,outer_factor=0;
    explicit operator bool() const { return local_layout!=0 && outer_factor!=0; }
};
// Exact original GCN identities observed in Bloodborne's light-volume draws.
// Unknown programs retain the translated local/hull preparation path.
inline NativeTessellationConfig NativeTessellation(std::uint64_t local,std::uint64_t hull,
                                                  std::uint64_t domain) {
    if (domain!=0xb9885bd6a84245b4ull && domain!=0x164fc97094ae66d7ull &&
        domain!=0x59911a4cf68be57cull && domain!=0xc4b45ba023e435cfull) return {};
    const auto layout=local==0x030976bf71b6a530ull ? 128u : local==0x53b4731b549fb1f0ull ? 144u : 0u;
    const auto factor=hull==0xf9f5fc457bc359fdull ? 1u : hull==0xf916abafa71dddf5ull ? 3u : 0u;
    return {layout,factor};
}
inline const char* NativeTessellationMsl() {
    return R"MSL(
// Sparse equivalent of the checked local/hull shaders' zeroed per-patch LDS.
// Preparation stays in the domain invocation: no compute encoder or render-pass
// break, and no CPU read of live guest geometry or reordered game commands.
struct DcNativeTess { uint words[36]; uint hull_base; uint outer_factor; };
static inline uint4 dc_native_tess_fetch(thread const DcMem& mem,ulong table,uint slot,uint vertex_index) {
    device uint* p=dc_ptr_n(mem,(table+ulong(slot)*16ul)&~3ul,16u);
    uint4 d=p ? uint4(p[0],p[1],p[2],p[3]) : uint4(0u);
    if(vertex_index>=d.z)return uint4(0u);
    ulong address=dc_vbase(d.x,d.y)+ulong(vertex_index)*ulong(dc_vstride(d.y));
    return dc_swizzle(dc_load_fmt(mem,address,(d.w>>15)&15u,(d.w>>12)&7u),d.w);
}
static inline void dc_native_tess_put(thread DcNativeTess& out,uint at,uint4 value) {
    out.words[at]=value.x;out.words[at+1]=value.y;
    out.words[at+2]=value.z;out.words[at+3]=value.w;
}
static inline void dc_native_tess_init(thread DcNativeTess& out,thread const DcMem& mem,
        constant uint* local_ud,constant uint* hull_ud,constant DcTess& params,
        uint patch,uint layout,uint outer_factor) {
    for(uint i=0;i<36;++i)out.words[i]=0u;
    uint vertex_index=patch;
    if(params.index_size) {
        vertex_index=dc_ld(mem,params.index_base+ulong(patch)*params.index_size);
        if(params.index_size==2u)vertex_index&=0xffffu;
    }
    vertex_index+=params.base_vertex;
    ulong table=(ulong(local_ud[2])|(ulong(local_ud[3])<<32))&0xfffffffffffful;
    uint4 p=dc_native_tess_fetch(mem,table,0u,vertex_index);
    dc_native_tess_put(out,0u,uint4(p.xyz,0x3f800000u));
    dc_native_tess_put(out,4u,dc_native_tess_fetch(mem,table,1u,vertex_index).zyxw);
    dc_native_tess_put(out,8u,dc_native_tess_fetch(mem,table,2u,vertex_index));
    if(layout==128u) {
        dc_native_tess_put(out,12u,dc_native_tess_fetch(mem,table,5u,vertex_index));
        dc_native_tess_put(out,16u,dc_native_tess_fetch(mem,table,6u,vertex_index));
        dc_native_tess_put(out,20u,dc_native_tess_fetch(mem,table,7u,vertex_index));
        p=dc_native_tess_fetch(mem,table,3u,vertex_index);
        dc_native_tess_put(out,24u,uint4(p.xyz,0u));
        p=dc_native_tess_fetch(mem,table,4u,vertex_index);
        dc_native_tess_put(out,28u,uint4(p.xy,0u,0u));
    } else {
        dc_native_tess_put(out,12u,dc_native_tess_fetch(mem,table,3u,vertex_index));
        dc_native_tess_put(out,16u,dc_native_tess_fetch(mem,table,4u,vertex_index));
        dc_native_tess_put(out,20u,dc_native_tess_fetch(mem,table,6u,vertex_index));
        dc_native_tess_put(out,24u,dc_native_tess_fetch(mem,table,7u,vertex_index));
        dc_native_tess_put(out,28u,dc_native_tess_fetch(mem,table,8u,vertex_index));
        p=dc_native_tess_fetch(mem,table,5u,vertex_index);
        dc_native_tess_put(out,32u,uint4(p.xy,0u,0u));
    }
    device uint* d=dc_ptr_n(mem,dc_vbase(hull_ud[4],hull_ud[5])&~3ul,16u);
    uint x=d ? d[0] : 0u,z=d ? d[2] : 0u;
    // Signed 24-bit MAD from the original hull shader, modulo uint32.
    out.hull_base=uint(int(x<<8)>>8)*uint(int(z<<8)>>8);
    out.outer_factor=as_type<uint>(float(outer_factor));
}
static inline uint dc_native_tess_load(thread const DcNativeTess& data,uint byte_address) {
    uint word=byte_address>>2;
    uint result=word<36u ? data.words[word] : 0u;
    for(uint i=0;i<6;++i) {
        if(word==((data.hull_base+i*16u)>>2))
            result=i<4u ? data.outer_factor : 0x3f800000u;
    }
    return result;
}
)MSL";
}
}
