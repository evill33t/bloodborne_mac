// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Exact existing per-chunk CRC comparisons, synchronously joined. The caller
// must hold current whole-source CPU-read/mapping/callback/GPU-writer proof and
// renderer ownership until return. This helper grants no source-read permission.
#include "common/content_hash.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <limits>
#include <span>

namespace Gpu::Metal::ParallelChunkHash {
struct Result {
    bool valid=false,changed=true,parallel=false;
    unsigned workers=0;
    std::uint64_t bytes=0,chunks=0;
};
inline unsigned ConfiguredWorkers(const char* value) noexcept {
    return value && value[0] && !value[1] && (value[0]=='2' || value[0]=='4')
        ? unsigned(value[0]-'0') : 0;
}
inline Result Compare(const void* data,std::size_t size,
        std::span<const std::uint64_t> expected,unsigned requested,
        std::size_t minimum_bytes=1u<<20) {
    const std::size_t count=size/ContentHash::kChunkBytes+(size%ContentHash::kChunkBytes!=0);
    if(!data || !size || expected.size()!=count || (requested!=2 && requested!=4) ||
        std::uintptr_t(data)>std::numeric_limits<std::uintptr_t>::max()-size)return {};
    // Preserve the earliest changed-chunk exit before any task dispatch.
    // Unchanged large scans pay one serial chunk, then split the remainder.
    const bool split=size>=minimum_bytes && count>1;
    const std::size_t first=split ? 1 : 0;
    const std::size_t first_bytes=first ? std::min(ContentHash::kChunkBytes,size) : 0;
    if(first && ContentHash::Bytes(data,first_bytes)!=expected[0])
        return {true,true,false,1,first_bytes,1};
    struct alignas(64) Work {std::uint64_t bytes=0,chunks=0;};
    struct Context {
        const unsigned char* source;
        std::size_t size,count,first;
        const std::uint64_t* expected;
        unsigned workers;
        std::atomic<bool> changed{false};
        std::array<Work,4> work{};
    } context{static_cast<const unsigned char*>(data),size,count,first,expected.data(),
        unsigned(split ? std::min<std::size_t>(requested,count-first) : 1)};
    const auto compare=[](void* opaque,std::size_t worker) noexcept {
        auto& c=*static_cast<Context*>(opaque);auto& result=c.work[worker];
        const std::size_t remaining=c.count-c.first;
        const std::size_t begin=c.first+worker*remaining/c.workers;
        const std::size_t end=c.first+(worker+1)*remaining/c.workers;
        for(std::size_t index=begin;index<end;++index) {
            if(c.changed.load(std::memory_order_relaxed))break;
            const auto offset=index*ContentHash::kChunkBytes;
            const auto bytes=std::min(ContentHash::kChunkBytes,c.size-offset);
            const auto hash=ContentHash::Bytes(c.source+offset,bytes);
            result.bytes+=bytes;++result.chunks;
            if(hash!=c.expected[index]) {c.changed.store(true,std::memory_order_relaxed);break;}
        }
    };
    // dispatch_apply_f returns only after every callback has returned. No task
    // retains the source, expected vector, or this stack context past return.
    if(context.workers==1)compare(&context,0);
    else dispatch_apply_f(context.workers,dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0),
        &context,compare);
    Result result{true,context.changed.load(std::memory_order_relaxed),context.workers>1,
        context.workers,first_bytes,first};
    for(unsigned k=0;k<context.workers;++k) {
        result.bytes+=context.work[k].bytes;result.chunks+=context.work[k].chunks;
    }
    // Even an internal scheduling defect cannot validate an incomplete scan.
    if(!result.changed && result.chunks!=count) {result.valid=false;result.changed=true;}
    return result;
}
// Optional cumulative size/actual-read histogram. No timing calls or worker
// writes; renderer owner records once after every synchronous comparison.
struct Histogram {
    static constexpr std::array<const char*,6> Names{
        "le64KiB","le256KiB","le1MiB","le4MiB","le16MiB","gt16MiB"};
    struct Bin {
        std::uint64_t checks=0,full=0,eligible=0,parallel=0,changed=0,bytes=0;
    };
    std::array<Bin,6> bins{};
    static bool ConfiguredEnabled(const char* value) noexcept {
        return value && value[0]=='1' && !value[1];
    }
    static unsigned Slot(std::uint64_t size) noexcept {
        unsigned slot=0;for(auto limit:{65536ull,262144ull,1048576ull,4194304ull,16777216ull}) {
            if(size<=limit)return slot;++slot;
        }
        return slot;
    }
    static void Add(std::uint64_t& value,std::uint64_t increment) noexcept {
        const auto maximum=std::numeric_limits<std::uint64_t>::max();
        value=increment>maximum-value ? maximum : value+increment;
    }
    void Note(std::uint64_t size,bool full,bool eligible,bool parallel,
            bool changed,std::uint64_t actual_bytes) noexcept {
        auto& bin=bins[Slot(size)];Add(bin.checks,1);Add(bin.full,full);
        Add(bin.eligible,eligible);Add(bin.parallel,parallel);Add(bin.changed,changed);
        Add(bin.bytes,actual_bytes);
    }
};
} // namespace Gpu::Metal::ParallelChunkHash
