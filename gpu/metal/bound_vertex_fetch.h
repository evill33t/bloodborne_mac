// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Admission metadata only. This prototype never binds or reads vertex payloads.
#include "gpu/metal/native_vertex_fetch.h"
#include "gpu/metal/static_smrd.h"
#include <array>
#include <cstring>
#include <span>

namespace Gpu::Metal::BoundVertexFetch {
using Address=std::uint64_t;
using Word=std::uint32_t;
inline constexpr Address CacheTag=0x424f554e44564631ull; // reserved for a future GPU variant
enum class Reject : Word { None,Layout,Root,Ownership,Ambiguous,Flags,Format,Empty,Base,Overflow,Count };
inline constexpr std::array<const char*,Word(Reject::Count)> Names{
    "none","layout","root","ownership","ambiguous","flags","format","empty","base","overflow"};
struct Prepared {
    std::array<Word,24> descriptors{};
    Address table=0,payload=0,bytes=0;
    Word stride=0,attributes=0,min_records=0,max_records=0;
};
// `snapshots` MUST come only from the current PatchUserData transaction after
// its complete source dump passed the CP/current callback/GPU-writer guard.
// Valid proves pinned ownership, never the readiness of an arbitrary source.
inline Reject Prepare(Address hash,std::span<const Word,16> ud,
    std::span<const StaticSmrd::Snapshot> snapshots,const StaticSmrd::Window& window,
    Prepared& output) {
    output={};Prepared candidate;
    const auto* layout=NativeVertexFetch::Layout(hash);
    if(!layout)return Reject::Layout;
    const Address root=(Address(ud[2])|(Address(ud[3])<<32))&0xffffffffffffull;
    const Address table=root&~Address(3),table_bytes=Address(layout->attributes)*16;
    if(!root || root>0xffffffffffffull-table_bytes)return Reject::Root;
    const StaticSmrd::Snapshot* selected=nullptr;
    for(const auto& snapshot:snapshots) {
        if(!StaticSmrd::Valid(snapshot,window) || root<snapshot.address ||
            root-snapshot.address>=snapshot.bytes || table<snapshot.address)continue;
        const Address offset=table-snapshot.address;
        if(offset>snapshot.bytes || table_bytes>snapshot.bytes-offset)continue;
        if(selected)return Reject::Ambiguous;
        selected=&snapshot;
    }
    if(!selected)return Reject::Ownership;
    std::memcpy(candidate.descriptors.data(),selected->data+(table-selected->address),table_bytes);
    candidate.table=table;candidate.stride=layout->stride;candidate.attributes=layout->attributes;
    candidate.min_records=UINT32_MAX;
    constexpr Address limit=Address(1)<<44; // dc_vbase's complete address domain
    for(Word k=0;k<layout->attributes;++k) {
        const Word* d=candidate.descriptors.data()+4*k;
        if((d[1]&0xfffff000u)!=(layout->stride<<16))return Reject::Flags;
        if(d[3]!=NativeVertexFetch::Formats[k])return Reject::Format;
        if(!d[2])return Reject::Empty;
        const Address base=Address(d[0])|(Address(d[1]&0xfff)<<32);
        if(!k)candidate.payload=base;
        if(candidate.payload>limit-1-NativeVertexFetch::Offsets[k] ||
            base!=candidate.payload+NativeVertexFetch::Offsets[k])return Reject::Base;
        candidate.min_records=std::min(candidate.min_records,d[2]);
        candidate.max_records=std::max(candidate.max_records,d[2]);
    }
    if(candidate.payload&3)return Reject::Base;
    // Conservative full-record coverage of every declared attribute count.
    // This is not a proof of the indexed draw's actual vertex-ID domain.
    candidate.bytes=Address(candidate.max_records)*candidate.stride;
    if(!candidate.bytes || candidate.payload>=limit || candidate.bytes>limit-candidate.payload)
        return Reject::Overflow;
    output=candidate;return Reject::None;
}
} // namespace Gpu::Metal::BoundVertexFetch
