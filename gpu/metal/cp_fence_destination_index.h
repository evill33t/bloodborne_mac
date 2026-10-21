// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace Gpu::Metal {
// Caller holds the same fence lock for Change, Current and all pending-table
// mutation/iteration. Zero is permanently exhausted; no wrap/ABA is admitted.
class PendingFenceEpoch {
public:
    using U64=std::uint64_t;
    explicit PendingFenceEpoch(U64 initial=1) noexcept:value(initial) {}
    void Change() noexcept {
        if(value)value=value>=std::numeric_limits<U64>::max()-1 ? 0 : value+1;
    }
    U64 Current() const noexcept {return value;}
private:
    U64 value;
};

// Opt-in immutable destination ADDRESS metadata, not a cached
// overlap/readiness answer, label content, ticket value or completion status.
// Caller holds fence_lock for this complete query, supplies the CURRENT pending
// table and a mutation epoch changed BEFORE every insertion/overwrite/erase.
// Caller independently proves map generation even/unchanged at query start/end
// and invalidates this index whenever its immutable map snapshot is invalidated.
class CpFenceDestinationIndex {
public:
    using U64=std::uint64_t;
    static constexpr U64 Maximum=std::numeric_limits<U64>::max();
    static constexpr std::size_t MaximumDestinations=65536;
    struct Counters {
        U64 queries=0,rebuilds=0,reused=0,rows=0,probes=0;
        U64 unresolved=0,epoch_rejects=0,size_rejects=0,allocation_failures=0;
        U64 current_rows=0;
    };
    explicit CpFenceDestinationIndex(bool enabled=false) noexcept:enabled(enabled) {}
    bool Enabled() const noexcept {return enabled;}
    Counters Statistics() const noexcept {return counters;}
    void Invalidate() noexcept {snapshot.reset();counters.current_rows=0;}

    template<class Pending,class Epoch,class Physical>
    bool Overlaps(U64 source,U64 bytes,const Pending& pending,U64 map_generation,
                  Epoch epoch,Physical physical) {
        if(!enabled)return true;
        Increment(counters.queries);
        if(!bytes)return false;
        if(source>Maximum-bytes)return true;
        if(pending.empty())return false;
        const U64 before=epoch();
        if(!Accept(before))return EpochRejected();
        if(pending.size()>MaximumDestinations) {
            Increment(counters.size_rejects);Invalidate();return true;
        }
        if(snapshot && snapshot->mapping==map_generation &&
                snapshot->pending==before && snapshot->pending_count==pending.size()) {
            Increment(counters.reused);
        } else {
            Increment(counters.rebuilds);
            try {
                auto next=std::make_unique<Snapshot>();
                next->mapping=map_generation;next->pending=before;
                next->pending_count=pending.size();
                next->starts.reserve(pending.size());
                for(const auto& entry:pending) {
                    Increment(counters.rows);
                    U64 destination=0;
                    if(!physical(entry.first,4,destination) || destination>Maximum-4) {
                        Increment(counters.unresolved);next->complete=false;
                        next->starts.clear();break;
                    }
                    next->starts.push_back(destination);
                }
                std::sort(next->starts.begin(),next->starts.end());
                next->starts.erase(std::unique(next->starts.begin(),next->starts.end()),next->starts.end());
                const U64 after=epoch();
                if(!Accept(after) || after!=before)return EpochRejected();
                counters.current_rows=next->starts.size();
                snapshot=std::move(next);
            } catch(const std::bad_alloc&) {
                Increment(counters.allocation_failures);Invalidate();return true;
            }
        }
        bool overlap=!snapshot->complete;
        if(!overlap) {
            // Every destination is a four-byte half-open interval. Among all
            // starts < source_end, the last start has the greatest end, even
            // with overlapping/unaligned labels and aliases. No prefix-max tree
            // is required while all lengths remain exactly four bytes.
            const U64 end=source+bytes;
            std::size_t lo=0,hi=snapshot->starts.size();
            while(lo<hi) {
                Increment(counters.probes);
                const auto mid=lo+(hi-lo)/2;
                if(snapshot->starts[mid]<end)lo=mid+1;else hi=mid;
            }
            overlap=lo && source<snapshot->starts[lo-1]+4;
        }
        const U64 final=epoch();
        if(!Accept(final) || final!=before)return EpochRejected();
        return overlap;
    }
private:
    struct Snapshot {
        U64 mapping=0,pending=0;
        std::size_t pending_count=0;
        bool complete=true;
        std::vector<U64> starts;
    };
    const bool enabled;
    bool observed=false,exhausted=false;
    U64 highest=0;
    std::unique_ptr<const Snapshot> snapshot;
    Counters counters;
    static void Increment(U64& value) noexcept {if(value!=Maximum)++value;}
    bool Accept(U64 value) noexcept {
        if(exhausted || !value || value>=Maximum-1 || (observed && value<highest)) {
            exhausted=true;return false;
        }
        observed=true;highest=value;return true;
    }
    bool EpochRejected() noexcept {
        Increment(counters.epoch_rejects);Invalidate();return true;
    }
};
}
