// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Exact retail shader layouts plus a pure, fail-closed snapshot packing contract.
// Nested descriptor payloads are deliberately never read here.
#include "gpu/metal/snapshot_arena.h"
#include "gpu/shader/msl.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace Gpu::Metal::StaticSmrd {
using Word=std::uint32_t;
using Address=std::uint64_t;
constexpr Word MaxWords=104;
inline constexpr Address CacheTag=0x53534d5244000001ull;
inline constexpr Address AddressCacheTag=0x44534d5244000001ull;
struct Load {Word pc,root,offset,words,destination;};
inline constexpr std::array<Load,12> Gbuffer68{{
    {20,2,112,8,16},{380,2,48,8,24},{396,12,96,8,32},
    {400,2,0,4,40},{404,12,832,16,44},{408,2,16,4,60},
    {488,12,64,8,36},{512,12,32,8,44},{584,12,0,8,8},
    {588,2,96,4,24},{664,2,80,4,4},{840,2,32,4,12}}};
inline constexpr std::array<Load,15> GbufferAff{{
    {20,2,64,8,16},{32,12,448,8,28},{36,2,32,4,36},
    {92,2,128,8,24},{520,12,96,8,36},{524,2,0,4,44},
    {528,12,832,16,48},{532,2,16,4,64},{620,12,64,8,40},
    {648,12,32,8,48},{724,12,0,8,32},{728,12,224,8,8},
    {740,2,112,4,40},{848,2,96,4,4},{1024,2,48,4,12}}};
inline std::span<const Load> Layout(Address hash) {
    if(hash==0x68fcb859536adcd4ull) return Gbuffer68;
    if(hash==0xaff190d9945a53e0ull) return GbufferAff;
    return {};
}
inline Word WordCount(std::span<const Load> layout) {
    Word words=0;for(const auto& load:layout) words+=load.words;return words;
}
// Supplied only by the current PatchUserData transaction. The renderer must
// retain the source generation used for that preparation, not infer ownership
// merely because an arbitrary guest pointer happens to fall inside the arena.
struct Snapshot {
    Address address=0,bytes=0,generation=0,current_generation=0;
    Address lease=SnapshotArena::NoLease;
    const std::byte* data=nullptr;
};
struct Window {
    Address address=0,capacity=0,head=0,epoch=0,pin=SnapshotArena::NoLease;
};
struct Packed {
    std::array<Word,MaxWords> words{};
    Word count=0;
};
inline bool Valid(const Snapshot& snapshot,const Window& window) {
    return window.address && window.capacity && !(window.capacity%SnapshotArena::Alignment) &&
        window.address<=~Address(0)-window.capacity &&
        window.pin!=SnapshotArena::NoLease && window.pin<=window.head && window.pin<=snapshot.lease &&
        !(window.head%SnapshotArena::Alignment) && !(window.epoch%SnapshotArena::Alignment) &&
        !(window.pin%SnapshotArena::Alignment) && !(snapshot.lease%SnapshotArena::Alignment) &&
        snapshot.data && snapshot.bytes && snapshot.generation &&
        snapshot.generation==snapshot.current_generation &&
        SnapshotArena::Reusable(snapshot.lease,snapshot.bytes,window.head,window.epoch,window.capacity) &&
        snapshot.address==window.address+snapshot.lease%window.capacity &&
        snapshot.bytes<=window.capacity-snapshot.lease%window.capacity;
}
inline bool Pack(Address hash,std::span<const Word,16> user_data,
                 std::span<const Snapshot> snapshots,const Window& window,Packed& output) {
    output={};
    const auto layout=Layout(hash);
    if(layout.empty() || WordCount(layout)>MaxWords) return false;
    Packed candidate;
    for(const auto& load:layout) {
        // Match EmitSmrd's 48-bit S_LOAD base and dword address alignment.
        const Address root=(Address(user_data[load.root])|
            (Address(user_data[load.root+1])<<32))&0xffffffffffffull;
        const Address address=(root+load.offset)&~Address(3);
        const Address bytes=Address(load.words)*4;
        const Snapshot* selected=nullptr;
        for(const auto& snapshot:snapshots) {
            if(!Valid(snapshot,window) || root<snapshot.address ||
               root-snapshot.address>=snapshot.bytes || address<snapshot.address) continue;
            const Address offset=address-snapshot.address;
            if(offset>snapshot.bytes || bytes>snapshot.bytes-offset) continue;
            if(selected) return false; // Ambiguous ownership is not proof.
            selected=&snapshot;
        }
        if(!selected) return false;
        std::memcpy(candidate.words.data()+candidate.count,
            selected->data+(address-selected->address),bytes);
        candidate.count+=load.words;
    }
    output=candidate;
    return true;
}

// Address-only mode reads all actual table values on the GPU. Each entry is a
// physical dword offset in mem[0/1], shifted left once with the buffer selector
// in bit zero. The sentinel requests the unchanged virtual-memory load path.
struct AddressBlock {
    static constexpr Address Fallback=~Address(0);
    std::array<Address,2> roots{Fallback,Fallback};
    Word ValidRoots() const {return Word(roots[0]!=Fallback)+Word(roots[1]!=Fallback);}
};
static_assert(sizeof(AddressBlock)==16);

inline AddressBlock TranslateRoots(Address hash,std::span<const Word,16> user_data,
        const Shader::GpuMemMap& map,std::span<const std::int32_t> entries) {
    AddressBlock result;
    const auto layout=Layout(hash);
    if(layout.empty() || map.shift<Shader::GpuMemMap::kMinShift ||
       map.shift>Shader::GpuMemMap::kMaxShift || map.pages>entries.size() ||
       !map.limit || map.split>map.limit || (map.split&3)) return result;
    const Address page=Address(1)<<map.shift;
    if(map.base&(page-1)) return result;
    auto physical=[&](Address va) {
        if(va<map.base) return AddressBlock::Fallback;
        const Address index=(va-map.base)>>map.shift;
        if(index>=map.pages) return AddressBlock::Fallback;
        const auto entry=entries[static_cast<size_t>(index)];
        if(entry==Shader::GpuMemMap::kUnmapped) return AddressBlock::Fallback;
        // Multiplication avoids C++ undefined behavior for negative left shifts.
        const Address phys=va+Address(std::int64_t(entry)*std::int64_t(Address(1)<<14));
        return phys<map.limit ? phys : AddressBlock::Fallback;
    };
    for(Word slot=0;slot<2;++slot) {
        const Word root=slot ? 12 : 2;
        Address extent=0;
        for(const auto& load:layout) if(load.root==root)
            extent=std::max(extent,Address(load.offset)+Address(load.words)*4);
        const Address va=((Address(user_data[root])|(Address(user_data[root+1])<<32))&0xffffffffffffull)&~Address(3);
        if(!extent || va>~Address(0)-(extent-4)) continue;
        const Address phys=physical(va),last=va+extent-4;
        if(phys==AddressBlock::Fallback || extent>map.limit-phys) continue;
        if(phys<map.split && extent>map.split-phys) continue;
        bool contiguous=physical(last)==phys+extent-4;
        // Check every crossed page, including gaps even if both endpoints map.
        for(Address at=(va&~(page-1))+page;contiguous && at<=last;) {
            contiguous=physical(at)==phys+(at-va);
            if(at>~Address(0)-page) break;
            at+=page;
        }
        const Address offset=phys<map.split ? phys : phys-map.split;
        if(!contiguous || (offset&3) || (offset>>2)>=(AddressBlock::Fallback>>1)) continue;
        result.roots[slot]=((offset>>2)<<1)|Address(phys>=map.split);
    }
    return result;
}
} // namespace Gpu::Metal::StaticSmrd
