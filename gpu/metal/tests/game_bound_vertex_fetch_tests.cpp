// SPDX-License-Identifier: GPL-2.0-or-later
// Pure ownership/descriptor metadata fixtures. No renderer, GPU or assets.
#include "gpu/metal/bound_vertex_fetch.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>
namespace B=Gpu::Metal::BoundVertexFetch;
namespace N=Gpu::Metal::NativeVertexFetch;
namespace S=Gpu::Metal::StaticSmrd;
int main() {
    constexpr B::Address arena=0x10000,payload=0x8002000000ull,capacity=16384;
    std::vector<std::byte> storage(4096);
    S::Window window{arena,capacity,4096,0,0};
    std::array<S::Snapshot,1> snapshots{{{arena,storage.size(),3,3,0,storage.data()}}};
    for(const auto hash:N::Hashes)for(B::Word alignment=0;alignment<4;++alignment) {
        const auto* layout=N::Layout(hash);assert(layout);
        std::array<B::Word,16> ud{};ud[2]=B::Word(arena+alignment);
        auto reset=[&] {
            std::fill(storage.begin(),storage.end(),std::byte{});
            for(B::Word k=0;k<layout->attributes;++k) {
                const B::Address at=payload+N::Offsets[k];
                const std::array<B::Word,4> descriptor{B::Word(at),B::Word(at>>32)|(layout->stride<<16),128,N::Formats[k]};
                std::memcpy(storage.data()+16*k,descriptor.data(),16);
            }
        };
        reset();B::Prepared output;
        assert(B::Prepare(hash,ud,snapshots,window,output)==B::Reject::None);
        assert(output.table==arena && output.payload==payload && output.bytes==128*layout->stride &&
            output.stride==layout->stride && output.attributes==layout->attributes &&
            output.min_records==128 && output.max_records==128);
        const auto reject=[&](B::Reject reason,std::span<const S::Snapshot> owners) {
            output.descriptors.fill(UINT32_MAX);output.payload=123;
            assert(B::Prepare(hash,ud,owners,window,output)==reason);
            assert(!output.payload && !output.bytes && !output.attributes);
            for(const auto word:output.descriptors)assert(!word);
        };
        auto stale=snapshots;stale[0].current_generation=4;reject(B::Reject::Ownership,stale);
        reject(B::Reject::Ownership,{});
        auto short_owner=snapshots;short_owner[0].bytes=16*layout->attributes-1;
        reject(B::Reject::Ownership,short_owner);
        const std::array<S::Snapshot,2> duplicate{snapshots[0],snapshots[0]};
        reject(B::Reject::Ambiguous,duplicate);
        const auto change=[&](B::Word word,B::Word value) {std::memcpy(storage.data()+word*4,&value,4);};
        change(1,(layout->stride<<16)|0x1000u|B::Word(payload>>32));reject(B::Reject::Flags,snapshots);
        reset();change(3,N::Formats[0]^1);reject(B::Reject::Format,snapshots);
        reset();change(2,0);reject(B::Reject::Empty,snapshots);
        reset();change(4,B::Word(payload+N::Offsets[1]+4));reject(B::Reject::Base,snapshots);
        reset();change(2,3);
        assert(B::Prepare(hash,ud,snapshots,window,output)==B::Reject::None);
        assert(output.min_records==3 && output.max_records==128 && output.bytes==128*layout->stride);
        reset();
        for(B::Word k=0;k<layout->attributes;++k) {
            const B::Address at=(B::Address(1)<<44)-64+N::Offsets[k];
            change(4*k,B::Word(at));change(4*k+1,B::Word(at>>32)|(layout->stride<<16));
        }
        reject(B::Reject::Overflow,snapshots);
    }
    std::puts("bound vertex metadata: six identities, owned snapshots, alignment, ambiguity, exact descriptors, counts and overflow prepared; GPU binding is absent");
}
