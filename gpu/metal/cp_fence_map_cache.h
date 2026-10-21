// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include "gpu/metal/cp_fence_destination_index.h"

namespace Gpu::Metal {

// Mapping metadata only. Caller holds its existing fence lock for this whole
// query, including iteration of the CURRENT pending destination table. The
// immutable map is never a cached readiness result, payload or fence value.
class CpFenceMapCache {
public:
    using U64=std::uint64_t;
    struct Mapping {U64 start=0,size=0,phys=0;};
    struct Counters {
        U64 queries=0,rebuilds=0,reused=0,rows=0,lookups=0,probes=0;
        U64 generation_rejects=0,invalid_maps=0,allocation_failures=0;
        U64 zero_bytes=0,empty_pending=0,unresolved_spans=0,snapshot_rows=0;
        CpFenceDestinationIndex::Counters destinations;
    };
    static constexpr std::size_t MaximumRows=65536;
    static constexpr U64 MaximumExtent=64ull<<30;
    static constexpr U64 Maximum=std::numeric_limits<U64>::max();
    static bool ConfiguredEnabled(const char* value) noexcept {
        return value && std::strcmp(value,"1")==0;
    }
    explicit CpFenceMapCache(bool enabled=false,U64 extent=0,bool index=false)
        :enabled(enabled),extent(extent),destination_index(enabled && index) {}
    bool DestinationIndexEnabled() const noexcept {return destination_index.Enabled();}
    bool Enabled() const noexcept {return enabled;}
    // Read under the same caller lock; no callback thread mutates these fields.
    Counters Statistics() const noexcept {
        auto result=counters;result.destinations=destination_index.Statistics();return result;
    }

    // Existing map-only callers retain the original current-destination scan.
    // An enabled index requires the explicit current pending-epoch overload.
    template<class Pending,class Generation,class Mappings>
    bool Overlaps(U64 address,U64 bytes,const Pending& pending,Generation generation,Mappings mappings) {
        return Overlaps(address,bytes,pending,[] {return U64{0};},generation,mappings);
    }
    template<class Pending,class PendingEpoch,class Generation,class Mappings>
    bool Overlaps(U64 address,U64 bytes,const Pending& pending,PendingEpoch pending_epoch,
                  Generation generation,Mappings mappings) {
        if(!enabled)return true; // production OFF uses its original implementation
        Increment(counters.queries);
        if(!bytes) {Increment(counters.zero_bytes);return false;}
        if(address>Maximum-bytes)return true;
        if(pending.empty()) {Increment(counters.empty_pending);return false;}
        const U64 before=generation();
        if(!AcceptGeneration(before))return GenerationRejected();
        if(snapshot && snapshot->generation==before) {
            Increment(counters.reused);
        } else {
            Increment(counters.rebuilds);
            try {
                // A temporary vector returned by Core::Memory remains alive
                // through this scope. Provider data is copied before commit.
                const auto& current=mappings();
                auto next=Build(current,before);
                const U64 after=generation();
                if(!AcceptGeneration(after) || after!=before)return GenerationRejected();
                if(!next) {
                    Increment(counters.invalid_maps);Invalidate();return true;
                }
                snapshot=std::move(next);
                counters.snapshot_rows=snapshot->rows.size();
            } catch(const std::bad_alloc&) {
                Increment(counters.allocation_failures);Invalidate();return true;
            }
        }
        U64 physical=0;
        bool overlap=!Physical(address,bytes,physical);
        if(overlap)Increment(counters.unresolved_spans);
        if(!overlap) {
            if(destination_index.Enabled()) {
                overlap=destination_index.Overlaps(physical,bytes,pending,before,pending_epoch,
                    [&](U64 at,U64 count,U64& translated) {return Physical(at,count,translated);});
            } else for(const auto& entry:pending) {
                U64 destination=0;
                // FenceWrite keeps an independent4-byte ticket for BOTH halves of
                // an8-byte destination. All current entries are checked here.
                if(!Physical(entry.first,4,destination)) {
                    Increment(counters.unresolved_spans);overlap=true;break;
                }
                if(destination<physical+bytes && physical<destination+4) {overlap=true;break;}
            }
        }
        // No false Disjoint if a remap started/finished after refresh/reuse or
        // during either address lookup. True remains conservative as well.
        const U64 final=generation();
        if(!AcceptGeneration(final) || final!=before)return GenerationRejected();
        return overlap;
    }

private:
    struct Snapshot {
        U64 generation;
        std::vector<Mapping> rows;
        Snapshot(U64 generation,std::vector<Mapping>&& rows)
            :generation(generation),rows(std::move(rows)) {}
    };
    bool enabled=false;
    U64 extent=0,highest_generation=0;
    bool observed_generation=false,exhausted_generation=false;
    std::unique_ptr<const Snapshot> snapshot;
    Counters counters;
    CpFenceDestinationIndex destination_index;

    static void Increment(U64& value) noexcept {if(value!=Maximum)++value;}
    void Invalidate() noexcept {
        snapshot.reset();counters.snapshot_rows=0;destination_index.Invalidate();
    }
    bool GenerationRejected() noexcept {
        Increment(counters.generation_rejects);Invalidate();return true;
    }
    bool AcceptGeneration(U64 value) noexcept {
        // Runtime epochs are odd while a writer owns its table lock. Refuse
        // observed regression/wrap and the last even value before exhaustion.
        if(exhausted_generation || value>=Maximum-1 ||
                (observed_generation && value<highest_generation)) {
            exhausted_generation=true;return false;
        }
        if(value&1)return false;
        highest_generation=value;observed_generation=true;return true;
    }
    template<class Mappings>
    std::unique_ptr<const Snapshot> Build(const Mappings& mappings,U64 generation) {
        if(!extent || extent>MaximumExtent || mappings.size()>MaximumRows)return {};
        std::vector<Mapping> rows;
        rows.reserve(mappings.size());
        for(const auto& mapping:mappings) {
            Increment(counters.rows);
            if(!mapping.size || mapping.start>Maximum-mapping.size ||
                    mapping.phys>=extent || mapping.size>extent-mapping.phys)return {};
            rows.push_back({mapping.start,mapping.size,mapping.phys});
        }
        std::sort(rows.begin(),rows.end(),[](const Mapping& a,const Mapping& b) {return a.start<b.start;});
        for(std::size_t i=1;i<rows.size();++i)
            if(rows[i-1].start+rows[i-1].size>rows[i].start)return {};
        // Physical overlap is intentional(alias mappings). Only overlapping
        // VIRTUAL rows are ambiguous and invalidate the complete snapshot.
        return std::make_unique<const Snapshot>(generation,std::move(rows));
    }
    bool Physical(U64 address,U64 bytes,U64& physical) noexcept {
        Increment(counters.lookups);
        if(!bytes || address>Maximum-bytes || !snapshot)return false;
        const auto& rows=snapshot->rows;
        std::size_t lo=0,hi=rows.size();
        while(lo<hi) {
            Increment(counters.probes);
            const auto mid=lo+(hi-lo)/2;
            if(rows[mid].start<=address)lo=mid+1;else hi=mid;
        }
        if(!lo)return false;
        Increment(counters.probes);
        const auto& row=rows[lo-1];
        if(address<row.start || address-row.start>=row.size || bytes>row.size-(address-row.start))return false;
        physical=row.phys+(address-row.start);
        return physical<=Maximum-bytes;
    }
};

} // namespace Gpu::Metal
