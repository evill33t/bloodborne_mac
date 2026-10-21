// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Bounded diagnostic contract only. Does not change shaders or animation time.
#include <array>
#include <cstdint>

namespace Gpu::Metal::WindCapture {
using U64=std::uint64_t;
inline constexpr U64 ShaderHash=0xe4abfca709b00ef3ull;
inline constexpr U64 AddressLimit=U64(1)<<40;
inline constexpr unsigned MaxFrames=120,DrawsPerFrame=2;
// One strict parser for renderer activation and bridge hook installation.
// Decimal positive values enable capture, saturating at the hard frame cap;
// empty, signed, whitespace, trailing garbage and zero values stay disabled.
inline unsigned ConfiguredFrames(const char* value) {
    if(!value || !*value)return 0;
    unsigned frames=0;
    for(const char* at=value;*at;++at) {
        if(*at<'0' || *at>'9')return 0;
        if(frames<MaxFrames) {
            frames=frames*10+unsigned(*at-'0');
            if(frames>MaxFrames)frames=MaxFrames;
        }
    }
    return frames;
}
inline bool NeedsMappingHooks(const char* native,const char* frames) {
    return (native && native[0]=='1' && !native[1]) || ConfiguredFrames(frames)>0;
}
struct Layout {const char* name;unsigned table_offset,bytes;};
inline constexpr std::array<Layout,2> Leaves{{{"wind",16,656},{"view",32,864}}};
struct Range {U64 address=0,bytes=0;};
inline bool OutsideArena(Range range,U64 arena,U64 capacity) {
    if(!range.bytes || range.address>~U64(0)-range.bytes)return false;
    if(!arena)return true;
    if(!capacity || arena>~U64(0)-capacity)return false;
    return range.address+range.bytes<=arena || range.address>=arena+capacity;
}
inline bool StableGeneration(U64 expected,U64 current) {
    return !(expected&1) && expected==current;
}
enum class Reject:unsigned {None,MissingGuard,MapGeneration,Mapping,CpuPending,Callback,Writer,Count};
inline constexpr std::array Names{"none","missing-cp-guard","map-generation","mapping",
    "pending-cpu-fence","pending-callback-effect","gpu-writer-or-cpu-read-proof"};
// All callbacks below inspect ownership/mapping metadata only. The caller may
// copy source words only after None, and must recheck after all copies. In
// particular, a denied proof never requests a GPU/callback drain.
template<class Guards> Reject Ready(const Range& range,Guards& guard) {
    if(!guard.HasPredicate())return Reject::MissingGuard;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    if(!guard.Mapping(range))return Reject::Mapping;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    if(!guard.CpuReady(range))return Reject::CpuPending;
    if(!guard.CallbacksReady(range))return Reject::Callback;
    if(!guard.WritersReady(range))return Reject::Writer;
    if(!guard.GenerationStable())return Reject::MapGeneration;
    return Reject::None;
}
inline bool Decode(const std::array<std::uint32_t,4>& words,unsigned expected,Range& out) {
    out={};
    const U64 base=(U64(words[0])|(U64(words[1]&0xfff)<<32));
    const unsigned stride=(words[1]>>16)&0x3fff;
    const U64 bytes=U64(words[2])*(stride ? stride : 1);
    if(!base || (base&3) || bytes!=expected || base>=AddressLimit || bytes>AddressLimit-base)return false;
    out={base,bytes};return true;
}
}
