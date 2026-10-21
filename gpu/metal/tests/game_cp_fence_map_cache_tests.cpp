// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/cp_fence_map_cache.h"
#include <cassert>
#include <cstdio>
#include <functional>
#include <unordered_map>

using Cache=Gpu::Metal::CpFenceMapCache;
using U64=Cache::U64;
using Mapping=Cache::Mapping;
using Pending=std::unordered_map<U64,unsigned>;
constexpr U64 source=0x100000,alias=0x200000,other=0x300000,private_address=0x900000;
constexpr U64 extent=0x10000;

struct Provider {
    U64 epoch=2;
    unsigned reads=0,fetches=0;
    bool allocation_failure=false;
    std::vector<Mapping> rows{{alias,0x4000,0},{source,0x4000,0},
                              {other,0x4000,0x4000},{source+0x4000,0x4000,0x8000}};
    std::function<void(unsigned)> on_generation;
    std::function<void()> on_fetch;
    U64 Generation() {
        ++reads;if(on_generation)on_generation(reads);return epoch;
    }
    std::vector<Mapping> Mappings() {
        ++fetches;
        if(allocation_failure)throw std::bad_alloc();
        auto snapshot=rows;
        if(on_fetch)on_fetch();
        return snapshot;
    }
};

static bool Query(Cache& cache,Provider& provider,const Pending& pending,U64 address,U64 bytes) {
    return cache.Overlaps(address,bytes,pending,[&] {return provider.Generation();},
        [&] {return provider.Mappings();});
}

// Independent copy of the original single-span linear mapping semantics.
// Only used with stable, valid, nonoverlapping virtual rows below; it neither
// calls the cache's translation helper nor uses its sorting/binary lookup.
static bool Legacy(const std::vector<Mapping>& rows,const Pending& pending,U64 address,U64 bytes) {
    if(!bytes)return false;
    if(address>Cache::Maximum-bytes)return true;
    if(pending.empty())return false;
    const auto physical=[&](U64 at,U64 count,U64& result) {
        for(const auto& row:rows)if(at>=row.start && at-row.start<row.size &&
                count<=row.size-(at-row.start)) {
            if(row.phys>Cache::Maximum-(at-row.start))return false;
            result=row.phys+(at-row.start);return result<=Cache::Maximum-count;
        }
        return false;
    };
    U64 read=0;
    if(!physical(address,bytes,read))return true;
    for(const auto& entry:pending) {
        U64 write=0;
        if(!physical(entry.first,4,write))return true;
        if(write<read+bytes && read<write+4)return true;
    }
    return false;
}

static void ConfigurationAndEarlyOuts() {
    assert(!Cache{}.Enabled() && Cache::ConfiguredEnabled("1"));
    assert(!Cache::ConfiguredEnabled(nullptr));
    for(const char* value:{"","0","01","true","1 "," 1","-1","2"})
        assert(!Cache::ConfiguredEnabled(value));
    Cache cache(true,extent);Provider provider;Pending none,pending{{other,1}};
    assert(!Query(cache,provider,pending,private_address,0));
    assert(!Query(cache,provider,none,private_address,4));
    assert(Query(cache,provider,none,Cache::Maximum-1,4)); // overflow precedes empty
    assert(!provider.fetches && !provider.reads);
    assert(cache.Statistics().queries==3 && cache.Statistics().zero_bytes==1 &&
        cache.Statistics().empty_pending==1);
}

static void PhysicalAliasesAndCurrentPendingTable() {
    Cache cache(true,extent);Provider provider;Pending pending{{other,123}};
    assert(!Query(cache,provider,pending,source,4)); // physical0 is valid
    pending.clear();pending.emplace(alias+0x100,99);
    assert(Query(cache,provider,pending,source+0x100,4));
    assert(!Query(cache,provider,pending,source+0x104,4));
    // Real FenceWrite stores BOTH4-byte keys for an8-byte label. Reading its
    // second half through another VA must still conflict physically.
    pending.emplace(alias+0x104,77);
    assert(Query(cache,provider,pending,source+0x104,4));
    pending.erase(alias+0x100);
    assert(Query(cache,provider,pending,source+0x104,4));
    pending.clear();pending.emplace(private_address,0);
    assert(Query(cache,provider,pending,source,4)); // unresolved destination
    pending.clear();pending.emplace(other,0);
    assert(Query(cache,provider,pending,private_address,4)); // private source
    pending.clear();assert(!Query(cache,provider,pending,private_address,4));
    assert(provider.fetches==1 && cache.Statistics().reused==6);
    assert(cache.Statistics().unresolved_spans==2);
}

static void GapsAndSingleSpanBoundaries() {
    Cache cache(true,extent);Provider provider;Pending pending{{other,1}};
    assert(Query(cache,provider,pending,source+0x3ffc,8)); // adjacent rows still two spans
    assert(!Query(cache,provider,pending,source+0x4000,4)); // exactly next row
    assert(Query(cache,provider,pending,source+0x8000,4)); // exact end, unmapped
    assert(Query(cache,provider,pending,source-4,8));
    provider.rows.erase(provider.rows.begin()+3);provider.epoch=4;
    assert(Query(cache,provider,pending,source+0x4000,4)); // removed row/gap
    Cache zero_va(true,extent);Provider zero;
    zero.rows={{0,0x4000,0},{other,0x4000,0x4000}};
    assert(!Query(zero_va,zero,pending,0,4)); // explicit VA0 map is not a hole
}

static void RemapsAndInvalidTables() {
    Cache cache(true,extent);Provider provider;Pending pending{{alias,1}};
    assert(Query(cache,provider,pending,source,4));
    provider.rows[0].phys=0x4000;provider.epoch=4;
    assert(!Query(cache,provider,pending,source,4)); // formerly aliased VA moved
    provider.rows[0].phys=0;provider.epoch=6;
    assert(Query(cache,provider,pending,source,4));
    assert(provider.fetches==3 && cache.Statistics().rebuilds==3);
    for(const Mapping invalid:std::vector<Mapping>{{source+8,16,0},
            {0x700000,0,0},{Cache::Maximum-3,8,0},{0x700000,4,extent},
            {0x700000,8,extent-4},{0x700000,8,Cache::Maximum-3}}) {
        Cache rejected(true,extent);Provider bad;
        bad.rows.push_back(invalid);
        assert(Query(rejected,bad,pending,source,4));
        assert(rejected.Statistics().invalid_maps==1 && !rejected.Statistics().snapshot_rows);
    }
    Cache duplicate(true,extent);Provider same;
    same.rows.push_back({source,0x4000,0});
    assert(Query(duplicate,same,Pending{{other,0}},source,4)); // ambiguous virtual rows
    Cache oversized(true,extent);Provider many;
    many.rows.assign(Cache::MaximumRows+1,{source,4,0});
    assert(Query(oversized,many,pending,source,4));
    assert(oversized.Statistics().invalid_maps==1);
    Cache unsupported(true,Cache::MaximumExtent+1);Provider large;
    assert(Query(unsupported,large,pending,source,4));
    Cache empty(true,extent);Provider no_maps;no_maps.rows.clear();
    assert(Query(empty,no_maps,pending,source,4));
    assert(Query(empty,no_maps,pending,source,4));
    assert(no_maps.fetches==1 && empty.Statistics().reused==1);
}

static void GenerationInterleavings() {
    Pending pending{{other,1}};
    // A change while the runtime provider is copying: discard old captured rows.
    Cache during_fetch(true,extent);Provider fetch;
    fetch.on_fetch=[&] {fetch.epoch=4;};
    assert(Query(during_fetch,fetch,pending,source,4));
    assert(during_fetch.Statistics().generation_rejects==1 && !during_fetch.Statistics().snapshot_rows);
    fetch.on_fetch={};
    assert(!Query(during_fetch,fetch,pending,source,4) && fetch.fetches==2);
    // Change AFTER rebuild validation, at final proof. The two equal refresh
    // epochs alone cannot authorize Disjoint on a newly changed map.
    Cache final_build(true,extent);Provider build;
    build.on_generation=[&](unsigned read) {if(read==3)build.epoch=4;};
    assert(Query(final_build,build,pending,source,4));
    build.on_generation={};
    assert(!Query(final_build,build,pending,source,4) && build.fetches==2);
    // Same final guard on the no-fetch/reuse route.
    Cache final_reuse(true,extent);Provider reuse;
    assert(!Query(final_reuse,reuse,pending,source,4));
    reuse.reads=0;
    reuse.on_generation=[&](unsigned read) {if(read==2)reuse.epoch=4;};
    assert(Query(final_reuse,reuse,pending,source,4) && reuse.fetches==1);
    reuse.on_generation={};
    assert(!Query(final_reuse,reuse,pending,source,4) && reuse.fetches==2);
    assert(final_reuse.Statistics().generation_rejects==1);
}

static void OddRegressionAndExhaustion() {
    Pending pending{{other,1}};Cache odd(true,extent);Provider provider;
    provider.epoch=3;
    assert(Query(odd,provider,pending,source,4) && !provider.fetches);
    provider.epoch=4;assert(!Query(odd,provider,pending,source,4));
    provider.epoch=2;assert(Query(odd,provider,pending,source,4));
    provider.epoch=6;assert(Query(odd,provider,pending,source,4)); // permanently closed after observed regression
    Cache exhausted(true,extent);Provider limit;
    limit.epoch=Cache::Maximum-3;
    assert(!Query(exhausted,limit,pending,source,4));
    limit.epoch=Cache::Maximum-1;
    assert(Query(exhausted,limit,pending,source,4));
    limit.epoch=0;
    assert(Query(exhausted,limit,pending,source,4) && limit.fetches==1);
}

static void AllocationFailureDoesNotReuseOldMap() {
    Cache cache(true,extent);Provider provider;Pending pending{{other,1}};
    assert(!Query(cache,provider,pending,source,4));
    provider.rows[2].phys=0;provider.epoch=4;provider.allocation_failure=true;
    assert(Query(cache,provider,pending,source,4));
    assert(cache.Statistics().allocation_failures==1 && !cache.Statistics().snapshot_rows);
    provider.allocation_failure=false;
    assert(Query(cache,provider,pending,source,4)); // fresh physical alias
    assert(provider.fetches==3 && cache.Statistics().rebuilds==3);
}

static void StableDifferentialAndMetadataPressure() {
    Cache cache(true,extent);Provider provider;provider.rows.clear();
    for(unsigned i=0;i<128;++i)provider.rows.push_back({source+U64(i)*0x10000,0x4000,U64(i%4)*0x4000});
    Pending pending;
    U64 random=0x471cafe52;
    const auto next=[&] {random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(unsigned i=0;i<32;++i)pending.emplace(source+U64(i*3)*0x10000+((i*31)&0xfff),i);
    unsigned disjoint=0,blocked=0;
    for(unsigned query=0;query<2000;++query) {
        // Current pending entries change without any mapping-generation change.
        if(query%23==0) {
            pending.erase(pending.begin());
            const auto row=next()%provider.rows.size();
            pending.emplace(provider.rows[row].start+(next()%0x4000),query);
        }
        const auto row=next()%provider.rows.size();
        const U64 address=provider.rows[row].start+(next()%0x5000);
        const U64 bytes=query%7==0 ? 0x8000 : query%3==0 ? 8 : 4;
        const bool expected=Legacy(provider.rows,pending,address,bytes);
        const bool actual=Query(cache,provider,pending,address,bytes);
        assert(actual==expected);disjoint+=!actual;blocked+=actual;
    }
    const auto counts=cache.Statistics();
    assert(disjoint && blocked && provider.fetches==1);
    assert(counts.queries==2000 && counts.rebuilds==1 && counts.reused==1999);
    assert(counts.rows==128 && counts.snapshot_rows==128);
    assert(counts.lookups>=2000 && counts.probes<=10*counts.lookups);
}

int main() {
    ConfigurationAndEarlyOuts();PhysicalAliasesAndCurrentPendingTable();GapsAndSingleSpanBoundaries();
    RemapsAndInvalidTables();GenerationInterleavings();OddRegressionAndExhaustion();
    AllocationFailureDoesNotReuseOldMap();StableDifferentialAndMetadataPressure();
    std::puts("CP fence map cache CPU contracts passed");
}
