// SPDX-License-Identifier: GPL-2.0-or-later
// Public address/protection helper contracts. No guest words or GPU are read.
#include "gpu/metal/ps_root_read.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <utility>
#include <vector>

namespace Root=Gpu::Metal::PsRootRead;
namespace Span=Gpu::Metal::DirectRootSpan;
namespace Static=Gpu::Metal::StaticSmrd;
namespace Shader=Gpu::Shader;
using U64=std::uint64_t;
constexpr U64 H68=0x68fcb859536adcd4ull,HAff=0xaff190d9945a53e0ull;
static bool Overlap(U64 a,U64 bytes,U64 b,U64 count) {
    return bytes && count && a<b+count && b<a+bytes;
}
struct Guards {
    struct Write {U64 physical,bytes,submission;bool unknown=false;};
    std::array<std::int32_t,32> entries{};
    Shader::GpuMemMap map{0,entries.size(),0x40000,14,0,0x80000};
    U64 expected_generation=7,current_generation=7,command=10;
    U64 backing_offset=0,backing_length=0x40000;
    unsigned backing_selector=0;
    bool context=true,read_only=true,predicate=true,callbacks=true;
    bool physical_override=false;
    unsigned generation_checks=0,change_generation_at=0,capacity=512;
    unsigned mapping_calls=0,cpu_calls=0,callback_calls=0,writer_calls=0,publications=0;
    U64 observed_address=0,observed_bytes=0;
    Span::Span span;
    std::vector<Write> cpu_writes,gpu_writes;
    std::vector<Root::Request> leases;
    bool Context() const{return context;}
    bool StagesReadOnly() const{return read_only;}
    bool HasCpuPredicate() const{return predicate;}
    bool GenerationStable() {
        if(++generation_checks==change_generation_at)++current_generation;
        return expected_generation==current_generation;
    }
    bool Mapping(const Root::Request& request) {
        ++mapping_calls;
        if(!Span::TranslateSpan(request.address,request.bytes,map,entries,span))return false;
        if(!physical_override){backing_offset=span.offset;backing_selector=span.selector;}
        return true;
    }
    bool PhysicalMatch(const Root::Request& request) const {
        return Span::MatchBacking(span,request.encoded,backing_selector,backing_offset,
            backing_length,expected_generation,current_generation);
    }
    bool CpuReady(const Root::Request& request) {
        ++cpu_calls;observed_address=request.address;observed_bytes=request.bytes;
        for(const auto& write:cpu_writes)
            if(Overlap(span.physical,span.bytes,write.physical,write.bytes))return false;
        return true;
    }
    bool CallbacksReady(){++callback_calls;return callbacks;}
    Root::Reject WritersReady() {
        ++writer_calls;
        for(const auto& write:gpu_writes) {
            if(write.unknown)return Root::Reject::GpuUnknown;
            if(write.submission>=command && Overlap(span.physical,span.bytes,write.physical,write.bytes))
                return Root::Reject::GpuCurrentCommand;
        }
        return Root::Reject::None;
    }
    bool PublishRead(const Root::Request& request) {
        if(leases.size()>=capacity)return false;
        ++publications;leases.push_back(request);return true;
    }
};
static std::array<std::uint32_t,16> UserData(U64 root2=0x10003,U64 root12=0x18007) {
    std::array<std::uint32_t,16> result{};
    for(const auto& pair:{std::pair{2u,root2},std::pair{12u,root12}}) {
        result[pair.first]=std::uint32_t(pair.second);result[pair.first+1]=std::uint32_t(pair.second>>32);
    }
    return result;
}
static Root::Request Request(U64 hash,unsigned slot,const Guards& guard) {
    const auto ud=UserData();
    return Root::MakeRequest(hash,slot,ud,Static::TranslateRoots(hash,ud,guard.map,guard.entries).roots[slot]);
}
static void Layouts() {
    for(const auto hash:{H68,HAff}) {
        Guards guard;
        const auto first=Request(hash,0,guard),second=Request(hash,1,guard);
        assert(first.address==0x10000 && first.bytes==(hash==H68 ? 144 : 160));
        assert(second.address==0x18004 && second.bytes==896);
        auto ud=UserData();ud[3]|=0xabcd0000;ud[13]|=0x12340000;
        assert(Root::MakeRequest(hash,0,ud,first.encoded).address==first.address);
        assert(Root::MakeRequest(hash,1,ud,second.encoded).address==second.address);
        // A high scalar-root bit is not a V# flag or a40-bit mask exemption.
        ud[3]|=0x100;
        assert(Root::MakeRequest(hash,0,ud,first.encoded).address==first.address+(U64(1)<<40));
        assert(Root::MakeRequest(hash^1,0,ud,first.encoded).bytes==0);
        assert(!Root::MakeRequest(hash,2,ud,first.encoded).bytes);
        // No ownership/snapshot/source pointer exists in this address-only API.
        assert(Root::Protect(first,guard)==Root::Reject::None && guard.publications==1);
        assert(guard.observed_address==first.address && guard.observed_bytes==first.bytes);
    }
}
static void GuardFailures() {
    using R=Root::Reject;
    for(unsigned kind=0;kind<14;++kind) {
        Guards guard;auto request=Request(H68,0,guard);R expected=R::None;
        switch(kind) {
        case 0:request.encoded=Span::Fallback;expected=R::Untranslated;break;
        case 1:request.bytes=0;expected=R::Layout;break;
        case 2:guard.context=false;expected=R::Context;break;
        case 3:guard.read_only=false;expected=R::StageWrites;break;
        case 4:guard.predicate=false;expected=R::MissingGuard;break;
        case 5:++guard.current_generation;expected=R::MapGeneration;break;
        case 6:guard.entries[request.address>>14]=Shader::GpuMemMap::kUnmapped;expected=R::Mapping;break;
        case 7:request.encoded^=2;expected=R::PhysicalMismatch;break;
        case 8:guard.physical_override=true;guard.backing_offset=request.address+4;expected=R::PhysicalMismatch;break;
        case 9:guard.cpu_writes.push_back({request.address+4,4,0});expected=R::CpuPending;break;
        case 10:guard.callbacks=false;expected=R::Callback;break;
        case 11:guard.gpu_writes.push_back({0,0,0,true});expected=R::GpuUnknown;break;
        case 12:guard.gpu_writes.push_back({request.address,4,guard.command});expected=R::GpuCurrentCommand;break;
        case 13:guard.capacity=0;expected=R::Lease;break;
        }
        assert(Root::Protect(request,guard)==expected && !guard.publications && guard.leases.empty());
        if(kind<=5)assert(!guard.mapping_calls && !guard.cpu_calls && !guard.callback_calls && !guard.writer_calls);
        if(expected==R::CpuPending)assert(!guard.callback_calls && !guard.writer_calls);
        if(expected==R::Callback)assert(!guard.writer_calls);
    }
    Guards guard;const auto request=Request(H68,0,guard);
    guard.change_generation_at=2;
    assert(Root::Protect(request,guard)==R::MapGeneration && !guard.publications);
    // Disjoint current-command writers and known prior-command producers need
    // no CPU content read or global drain. New/current overlapping work falls back.
    guard={};guard.gpu_writes={{request.address,request.bytes,guard.command-1},
        {request.address+request.bytes,4,guard.command}};
    assert(Root::Protect(request,guard)==R::None && guard.publications==1);
    guard={};guard.cpu_writes={{request.address+request.bytes,4,0}};
    assert(Root::Protect(request,guard)==R::None);
}
static void PhysicalAndCapacity() {
    Guards guard;
    // Map a prepared high VA to physical byte0: encoded0 is valid, not fallback.
    guard.entries[4]=-4;
    auto request=Request(H68,0,guard);assert(request.encoded==0);
    assert(Root::Protect(request,guard)==Root::Reject::None && guard.span.physical==0);
    guard={};guard.entries[4]=-4;request=Request(H68,0,guard);
    guard.cpu_writes={{4,4,0}}; // another VA alias would write physical byte4
    assert(Root::Protect(request,guard)==Root::Reject::CpuPending && !guard.publications);
    guard={};guard.entries[4]=16;
    request=Request(H68,0,guard);assert(request.encoded&1);
    assert(Root::Protect(request,guard)==Root::Reject::None && guard.span.selector==1);
    guard={};auto ud=UserData(0x13ff0,0x18000);
    auto translated=Static::TranslateRoots(HAff,ud,guard.map,guard.entries);
    request=Root::MakeRequest(HAff,0,ud,translated.roots[0]);
    assert(Root::Protect(request,guard)==Root::Reject::None);
    guard={};guard.entries[5]=Shader::GpuMemMap::kUnmapped;
    assert(Root::Protect(request,guard)==Root::Reject::Mapping && !guard.publications);
    // One root can fall back without discarding the other root's valid lease.
    guard={};auto a=Request(H68,0,guard),b=Request(H68,1,guard);
    guard.cpu_writes={{a.address,4,0}};
    assert(Root::Protect(a,guard)==Root::Reject::CpuPending);
    assert(Root::Protect(b,guard)==Root::Reject::None && guard.leases.size()==1);
    assert(guard.leases[0].address==b.address && guard.leases[0].bytes==b.bytes);
    guard={};a=Request(H68,0,guard);
    for(unsigned k=0;k<512;++k)assert(Root::Protect(a,guard)==Root::Reject::None);
    assert(Root::Protect(a,guard)==Root::Reject::Lease && guard.leases.size()==512 && guard.publications==512);
    // Model completion releasing bounded tickets; no pool growth or wait exists.
    guard.leases.clear();assert(Root::Protect(a,guard)==Root::Reject::None);
}
int main() {
    Layouts();GuardFailures();PhysicalAndCapacity();
    puts("PS GPU-time roots: exact extents/masks, current backing, physical aliases, proof ordering and bounded publication contracts passed");
}
