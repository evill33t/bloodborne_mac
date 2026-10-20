// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/metal/native_indirect.h"
#include <algorithm>
#include <cstring>
#include <deque>
#include <new>
#include <span>
#include <vector>

namespace Gpu::Metal {
// Renderer-thread metadata only. Completed writes deliberately remain here:
// completion does not make an already uploaded CPU texture current again.
class TextureWriteJournal {
public:
    using Range=NativeIndirect::Range;
    enum class Relation {Unchanged,Disjoint,Overlap,Unknown};
    struct Query {
        Relation relation=Relation::Unknown;
        bool cpu_callbacks=false;
        bool FullCheck() const {return relation==Relation::Overlap || relation==Relation::Unknown;}
    };
    struct Stats {
        std::uint64_t evicted_records=0,queries=0,queried_source_bytes=0;
        std::uint64_t forgotten_queries=0,forgotten_source_bytes=0,examined_records=0;
        std::uint64_t indexed_queries=0,page_checks=0,page_updates=0,index_allocation_failures=0;
    };
    static constexpr std::size_t kLimit=512;
    static constexpr std::size_t kMaximumRecords=8192;
    static constexpr std::uint64_t kPageBytes=16384;
    static constexpr std::uint64_t kMaximumIndexedExtent=64ull<<30;
    // Separate, strictly opt-in experiment. The renderer supplies the actual
    // direct-pool extent; no virtual addresses or guessed pool size enter it.
    static bool PageIndexEnabled(const char* value) {return value && !std::strcmp(value,"1");}
    // The renderer reads the environment once. Only a complete unsigned
    // decimal in [1,8192] is accepted; invalid values retain the default.
    static std::size_t ConfiguredLimit(const char* value) {
        if(!value || !*value)return kLimit;
        std::size_t count=0;
        for(const char* p=value;*p;++p) {
            if(*p<'0' || *p>'9')return kLimit;
            const auto digit=std::size_t(*p-'0');
            if(count>(kMaximumRecords-digit)/10)return kLimit;
            count=count*10+digit;
        }
        return count ? count : kLimit;
    }
    static bool Enabled(bool native,const char* value) {
        return native && (!value || !std::strcmp(value,"1"));
    }
    explicit TextureWriteJournal(std::size_t limit=kLimit,std::uint64_t initial_epoch=0,
                                 std::uint64_t indexed_extent=0)
        : capacity(std::min(limit,kMaximumRecords)),epoch(initial_epoch),
          index_extent(indexed_extent<=kMaximumIndexedExtent ? indexed_extent : 0),
          unknown_epoch(initial_epoch),unknown_cpu_epoch(initial_epoch) {}
    std::uint64_t Epoch() const {return epoch;}
    bool Current(std::uint64_t checked) const {return !saturated && checked==epoch;}
    std::size_t Size() const {return records.size();}
    std::size_t Capacity() const {return capacity;}
    Stats GetStats() const {return stats;}
    std::uint64_t ForgottenThrough() const {return forgotten;}
    bool PageIndexActive() const {return index_extent!=0;}
    std::uint64_t PageIndexExtent() const {return index_extent;}
    std::size_t PageIndexBytes() const {return page_epochs.size()*sizeof(PageEpoch);}
    void Publish(std::span<const Range> physical,bool unknown=false,bool deferred_cpu=false) {
        bool nonempty=unknown;
        for(const auto& range:physical) {
            nonempty|=range.bytes!=0;
            unknown|=range.address>UINT64_MAX-range.bytes;
        }
        if(!nonempty)return;
        if(epoch==UINT64_MAX) {saturated=true;forgotten=epoch;records.clear();return;}
        ++epoch;
        if(index_extent)IndexPublish(physical,unknown,deferred_cpu);
        const auto append=[&](Range range,bool uncertain) {
            if(!capacity){forgotten=epoch;Count(stats.evicted_records,1);return;}
            if(records.size()==capacity) {
                forgotten=std::max(forgotten,records.front().epoch);records.pop_front();
                Count(stats.evicted_records,1);
            }
            records.push_back({epoch,range,uncertain,deferred_cpu});
        };
        if(unknown){append({},true);return;}
        for(const auto& range:physical)if(range.bytes) {
            if(!records.empty()) {
                auto& previous=records.back();
                if(previous.epoch==epoch && !previous.unknown &&
                    range.address<=previous.physical.address+previous.physical.bytes &&
                    previous.physical.address<=range.address+range.bytes) {
                    const auto end=std::max(previous.physical.address+previous.physical.bytes,range.address+range.bytes);
                    previous.physical.address=std::min(previous.physical.address,range.address);
                    previous.physical.bytes=end-previous.physical.address;continue;
                }
            }
            append(range,false);
        }
    }
    Query Inspect(std::uint64_t checked,std::span<const Range> source,bool complete=true) const {
        // These are supplied physical-span bytes, not CPU hash/transfer bytes.
        // Incomplete sources contribute only the spans supplied by the caller.
        std::uint64_t source_bytes=0;
        for(const auto& range:source)Count(source_bytes,range.bytes);
        Count(stats.queries,1);Count(stats.queried_source_bytes,source_bytes);
        if(saturated || checked>epoch)return {Relation::Unknown,true};
        if(index_extent)return IndexInspect(checked,source,complete);
        if(checked<forgotten) {
            Count(stats.forgotten_queries,1);Count(stats.forgotten_source_bytes,source_bytes);
            return {Relation::Unknown,true};
        }
        if(checked==epoch)return {Relation::Unchanged,false};
        if(!complete)return {Relation::Unknown,true};
        Query answer{Relation::Disjoint,false};
        // Appends are monotonic by epoch, including every range of one publish.
        // Recent consumers need not scan older retained history. Keep scanning
        // all newer records to preserve deferred-CPU flags behind an unknown.
        for(auto it=records.rbegin();it!=records.rend() && it->epoch>checked;++it) {
            const auto& record=*it;Count(stats.examined_records,1);
            bool overlap=record.unknown;
            for(const auto& range:source) {
                if(!range.bytes || range.address>UINT64_MAX-range.bytes)return {Relation::Unknown,true};
                overlap|=NativeIndirect::Overlap(range,record.physical);
            }
            if(overlap) {
                if(record.unknown)answer.relation=Relation::Unknown;
                else if(answer.relation!=Relation::Unknown)answer.relation=Relation::Overlap;
                answer.cpu_callbacks|=record.deferred_cpu;
            }
        }
        return answer;
    }
private:
    static void Count(std::uint64_t& total,std::uint64_t amount) {
        total=amount>UINT64_MAX-total ? UINT64_MAX : total+amount;
    }
    struct Record {std::uint64_t epoch;Range physical;bool unknown,deferred_cpu;};
    struct PageEpoch {std::uint64_t write=0,cpu=0;};
    static_assert(sizeof(PageEpoch)==16);
    bool IndexedRange(Range range) const {
        return range.bytes && range.address<index_extent && range.bytes<=index_extent-range.address;
    }
    void IndexPublish(std::span<const Range> physical,bool unknown,bool deferred_cpu) {
        for(const auto& range:physical)if(range.bytes && !IndexedRange(range))unknown=true;
        if(unknown) {
            unknown_epoch=epoch;
            if(deferred_cpu)unknown_cpu_epoch=epoch;
            return;
        }
        // Allocate once, lazily at the first known write. Failure permanently
        // restores the unchanged bounded deque path, including its forgotten
        // watermark. No partially constructed index can answer a query.
        if(page_epochs.empty()) {
            try {page_epochs.resize(static_cast<std::size_t>((index_extent+kPageBytes-1)/kPageBytes));}
            catch(const std::bad_alloc&) {
                // A nonzero initial epoch may describe history absent from
                // the deque. Preserve that uncertainty when abandoning the
                // index, including the allocation-triggering publish.
                forgotten=epoch;index_extent=0;Count(stats.index_allocation_failures,1);return;
            }
        }
        for(const auto& range:physical)if(range.bytes) {
            const auto last=(range.address+range.bytes-1)/kPageBytes;
            for(auto page=range.address/kPageBytes;page<=last;++page) {
                auto& latest=page_epochs[static_cast<std::size_t>(page)];latest.write=epoch;
                if(deferred_cpu)latest.cpu=epoch;
                Count(stats.page_updates,1);
            }
        }
    }
    Query IndexInspect(std::uint64_t checked,std::span<const Range> source,bool complete) const {
        Count(stats.indexed_queries,1);
        if(!complete || source.empty())return {Relation::Unknown,true};
        for(const auto& range:source)if(!IndexedRange(range))return {Relation::Unknown,true};
        if(checked==epoch)return {Relation::Unchanged,false};
        Query answer{unknown_epoch>checked ? Relation::Unknown : Relation::Disjoint,unknown_cpu_epoch>checked};
        for(const auto& range:source) {
            const auto last=(range.address+range.bytes-1)/kPageBytes;
            for(auto page=range.address/kPageBytes;page<=last;++page) {
                Count(stats.page_checks,1);
                if(page_epochs.empty())continue; // only global unknown writes so far
                const auto& latest=page_epochs[static_cast<std::size_t>(page)];
                if(latest.write>checked && answer.relation!=Relation::Unknown)answer.relation=Relation::Overlap;
                answer.cpu_callbacks|=latest.cpu>checked;
            }
        }
        return answer;
    }
    std::size_t capacity;
    std::uint64_t epoch=0,forgotten=0;
    bool saturated=false;
    std::deque<Record> records;
    // These epochs survive deque eviction and completion. Per-page overlap
    // may conservatively include unrelated bytes on the same physical page.
    std::uint64_t index_extent=0,unknown_epoch=0,unknown_cpu_epoch=0;
    std::vector<PageEpoch> page_epochs;
    mutable Stats stats; // renderer-thread metadata only, like the journal
};
} // namespace Gpu::Metal
