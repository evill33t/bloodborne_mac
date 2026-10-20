// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Physical address metadata only. Never reads guest source/descriptor bytes.
#include "gpu/shader/msl.h"
#include <cstdint>
#include <span>

namespace Gpu::Metal::DirectRootSpan {
using U64=std::uint64_t;
inline constexpr U64 Fallback=~U64(0),AddressLimit=U64(1)<<48,MaximumBytes=U64(1)<<20;
struct Span {
    U64 address=0,bytes=0,physical=0,offset=0,encoded=Fallback;
    unsigned selector=0;
};
inline U64 SmrdRoot(std::uint32_t low,std::uint32_t high) {
    return ((U64(low)|(U64(high)<<32))&(AddressLimit-1))&~U64(3);
}
inline bool TranslateSpan(U64 address,U64 bytes,const Shader::GpuMemMap& map,
                          std::span<const std::int32_t> entries,Span& output) {
    output={};
    if(!bytes || bytes>MaximumBytes || (address&3) || (bytes&3) || address>=AddressLimit || bytes>AddressLimit-address ||
        map.shift<Shader::GpuMemMap::kMinShift || map.shift>Shader::GpuMemMap::kMaxShift ||
        map.pages>entries.size() || !map.limit || map.split>map.limit || (map.split&3))return false;
    const U64 page=U64(1)<<map.shift;
    if(map.base&(page-1))return false;
    const auto physical=[&](U64 va,U64& result) {
        if(va<map.base)return false;
        const U64 index=(va-map.base)>>map.shift;
        if(index>=map.pages)return false;
        const auto delta=entries[static_cast<std::size_t>(index)];
        if(delta==Shader::GpuMemMap::kUnmapped)return false;
        const auto signed_bytes=std::int64_t(delta)*std::int64_t(U64(1)<<14);
        if(signed_bytes<0) {
            const U64 magnitude=U64(-signed_bytes);if(va<magnitude)return false;
            result=va-magnitude;
        } else {
            const U64 magnitude=U64(signed_bytes);if(va>Fallback-magnitude)return false;
            result=va+magnitude;
        }
        return result<map.limit;
    };
    U64 first=0,last=0;
    if(!physical(address,first) || bytes>map.limit-first ||
        !physical(address+bytes-4,last) || last!=first+bytes-4)return false;
    for(U64 at=(address&~(page-1))+page;at<address+bytes;at+=page) {
        U64 mapped=0;if(!physical(at,mapped) || mapped!=first+(at-address))return false;
    }
    if(first<map.split && bytes>map.split-first)return false;
    const unsigned selector=unsigned(first>=map.split);
    const U64 offset=selector ? first-map.split : first;
    if(offset&3)return false;
    output={address,bytes,first,offset,((offset>>2)<<1)|selector,selector};return true;
}
inline bool MatchBacking(const Span& span,U64 encoded,unsigned selector,U64 offset,U64 buffer_length,
                         U64 expected_generation,U64 current_generation) {
    return span.encoded!=Fallback && encoded==span.encoded && selector==span.selector &&
        offset==span.offset && offset<=buffer_length && span.bytes<=buffer_length-offset &&
        expected_generation==current_generation;
}
} // namespace Gpu::Metal::DirectRootSpan
