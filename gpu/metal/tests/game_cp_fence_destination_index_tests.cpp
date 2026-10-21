// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/cp_fence_map_cache.h"
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <unordered_map>

using Cache=Gpu::Metal::CpFenceMapCache;
using Epoch=Gpu::Metal::PendingFenceEpoch;
using U64=Cache::U64;
using Mapping=Cache::Mapping;
constexpr U64 source=0x100000,alias=0x200000,other=0x300000,private_address=0x900000;
constexpr U64 extent=0x20000;
struct Provider {
    U64 generation=2;
    unsigned reads=0,fetches=0;
    bool allocation_failure=false;
    std::vector<Mapping> rows{{alias,0x10000,0},{source,0x10000,0},{other,0x10000,0x10000}};
    std::function<void(unsigned)> on_generation;
    U64 Generation() {++reads;if(on_generation)on_generation(reads);return generation;}
    std::vector<Mapping> Mappings() {
        ++fetches;if(allocation_failure)throw std::bad_alloc();return rows;
    }
};
// Same logical transactions as CP, with independent assertions on the epoch.
struct Table {
    struct Write {unsigned value=0;U64 ticket=0;bool known=true;};
    Epoch epoch;
    std::unordered_map<U64,Write> rows;
    unsigned epoch_reads=0;
    std::function<void(unsigned)> on_epoch;
    U64 Current() {++epoch_reads;if(on_epoch)on_epoch(epoch_reads);return epoch.Current();}
    void Put(U64 address,unsigned bytes,U64 ticket,unsigned value=0,bool known=true) {
        if(!bytes)return;
        assert(bytes==4 || bytes==8);
        epoch.Change();
        rows[address]={value,ticket,known};
        if(bytes==8)rows[address+4]={value,ticket,known};
    }
    void Retire(U64 address,unsigned bytes,U64 ticket) {
        bool changed=false;
        auto it=rows.find(address);
        if(it!=rows.end() && it->second.ticket==ticket) {
            epoch.Change();changed=true;rows.erase(it);
        }
        if(bytes==8) {
            it=rows.find(address+4);
            if(it!=rows.end() && it->second.ticket==ticket) {
                if(!changed)epoch.Change();rows.erase(it);
            }
        }
    }
};
static bool Query(Cache& cache,Provider& provider,Table& pending,U64 address,U64 bytes) {
    return cache.Overlaps(address,bytes,pending.rows,[&] {return pending.Current();},
        [&] {return provider.Generation();},[&] {return provider.Mappings();});
}
// Independent original oracle: whole-span, linear mapping and current-key scan.
static bool Legacy(const Provider& provider,const Table& pending,U64 address,U64 bytes) {
    if(!bytes)return false;
    if(address>Cache::Maximum-bytes)return true;
    if(pending.rows.empty())return false;
    const auto physical=[&](U64 at,U64 count,U64& result) {
        for(const auto& row:provider.rows)if(at>=row.start && at-row.start<row.size &&
            count<=row.size-(at-row.start)) {
            if(row.phys>Cache::Maximum-(at-row.start))return false;
            result=row.phys+(at-row.start);return result<=Cache::Maximum-count;
        }
        return false;
    };
    U64 read=0;if(!physical(address,bytes,read))return true;
    for(const auto& [at,write]:pending.rows) {
        (void)write;U64 destination=0;
        if(!physical(at,4,destination))return true;
        if(destination<read+bytes && read<destination+4)return true;
    }
    return false;
}
static void AliasesBoundariesAndCurrentTransactions() {
    Cache cache(true,extent,true);Provider provider;Table table;
    assert(cache.DestinationIndexEnabled());
    assert(!Query(cache,provider,table,private_address,0));
    assert(!Query(cache,provider,table,private_address,4));
    assert(Query(cache,provider,table,Cache::Maximum-1,4));
    table.Put(alias+0x100,8,1);
    const auto after_qword=table.epoch.Current();
    assert(after_qword==2 && table.rows.size()==2);
    for(const auto [at,bytes]:std::array<std::pair<U64,U64>,7>{{
        {source+0x100,4},{source+0x104,4},{source+0x108,4},{source+0xff,1},
        {source+0xff,2},{source+0x107,1},{source+0x107,2}}})
        assert(Query(cache,provider,table,at,bytes)==Legacy(provider,table,at,bytes));
    // Overwrite the first qword half. Its older callback can retire only half2.
    table.Put(alias+0x100,4,2,99,false);
    assert(table.epoch.Current()==after_qword+1);
    table.Retire(alias+0x100,8,1);
    assert(table.epoch.Current()==after_qword+2 && table.rows.size()==1);
    assert(Query(cache,provider,table,source+0x100,4));
    assert(!Query(cache,provider,table,source+0x104,4));
    const auto before_stale=table.epoch.Current();
    const auto rebuilds=cache.Statistics().destinations.rebuilds;
    table.Retire(alias+0x100,8,1); // no ticket match: table and epoch unchanged
    assert(table.epoch.Current()==before_stale);
    assert(Query(cache,provider,table,source+0x100,4));
    assert(cache.Statistics().destinations.rebuilds==rebuilds);
    table.Put(alias+0x100,4,3); // same key still invalidates metadata by contract
    assert(Query(cache,provider,table,source+0x100,4));
    assert(cache.Statistics().destinations.rebuilds==rebuilds+1);
    table.Retire(alias+0x100,4,3);
    assert(!Query(cache,provider,table,source+0x100,4));
    table.Put(alias+0x101,4,4);table.Put(source+0x101,4,5); // duplicate physical alias
    assert(Query(cache,provider,table,source+0x104,1));
    assert(!Query(cache,provider,table,source+0x105,1));
    assert(cache.Statistics().destinations.current_rows==1);
    table.Put(private_address,4,6);
    assert(Query(cache,provider,table,other,4)); // incomplete destination metadata
    assert(Query(cache,provider,table,other,4)); // incomplete snapshot may be reused
    table.Retire(private_address,4,6);
    assert(!Query(cache,provider,table,other,4));
    assert(Query(cache,provider,table,private_address,4)); // source still unresolved
}
static void RemapAndGenerationGuards() {
    Cache cache(true,extent,true);Provider provider;Table table;table.Put(alias,4,1);
    assert(Query(cache,provider,table,source,4));
    provider.rows[0].phys=0x10000;provider.generation=4;
    assert(!Query(cache,provider,table,source,4));
    assert(cache.Statistics().destinations.rebuilds==2);
    provider.rows[0].phys=0;provider.generation=6;
    assert(Query(cache,provider,table,source,4));
    provider.generation=7;assert(Query(cache,provider,table,other,4));
    assert(!cache.Statistics().destinations.current_rows);
    provider.generation=8;assert(!Query(cache,provider,table,other,4));
    provider.reads=0;
    provider.on_generation=[&](unsigned read) {if(read==2)provider.generation=10;};
    assert(Query(cache,provider,table,other,4)); // change at final reuse proof
    assert(!cache.Statistics().destinations.current_rows);
    provider.on_generation={};assert(!Query(cache,provider,table,other,4));
    provider.generation=12;provider.allocation_failure=true;
    assert(Query(cache,provider,table,other,4));
    assert(!cache.Statistics().destinations.current_rows);
    provider.allocation_failure=false;assert(!Query(cache,provider,table,other,4));
    provider.rows.erase(provider.rows.begin());provider.generation=14;
    assert(Query(cache,provider,table,other,4)); // destination became private
    // A final map change rejects and invalidates even a newly built index.
    Cache fresh(true,extent,true);Provider building;Table labels;labels.Put(other,4,1);
    building.on_generation=[&](unsigned read) {
        if(read==3) {building.generation=4;building.rows[2].phys=0;}
    };
    assert(Query(fresh,building,labels,source,4));
    assert(fresh.Statistics().generation_rejects==1 && !fresh.Statistics().destinations.current_rows);
    building.on_generation={};assert(Query(fresh,building,labels,source,4));
    // Newly invalid map metadata cannot retain an older disjoint index.
    building.rows.push_back({source+4,16,0});building.generation=6;
    assert(Query(fresh,building,labels,source,4));
    assert(fresh.Statistics().invalid_maps==1 && !fresh.Statistics().destinations.current_rows);
}
static void PendingEpochGuardsAndBounds() {
    Epoch exhausted(Cache::Maximum-2);
    exhausted.Change();assert(exhausted.Current()==Cache::Maximum-1);
    exhausted.Change();assert(exhausted.Current()==0);
    exhausted.Change();assert(exhausted.Current()==0);
    Cache cache(true,extent,true);Provider provider;Table table;table.Put(other,4,1);
    assert(!Query(cache,provider,table,source,4));
    table.epoch_reads=0;
    table.on_epoch=[&](unsigned read) {if(read==2)table.epoch.Change();};
    assert(Query(cache,provider,table,source,4));
    assert(cache.Statistics().destinations.epoch_rejects==1);
    table.on_epoch={};assert(!Query(cache,provider,table,source,4));
    Cache changed_during_build(true,extent,true);Table building;building.Put(other,4,1);
    building.on_epoch=[&](unsigned read) {if(read==2)building.epoch.Change();};
    assert(Query(changed_during_build,provider,building,source,4));
    assert(changed_during_build.Statistics().destinations.epoch_rejects==1);
    building.on_epoch={};assert(!Query(changed_during_build,provider,building,source,4));
    // Independent caller epoch regressions/exhaustion cannot recover old proofs.
    U64 version=10;
    const auto query=[&] {return cache.Overlaps(source,4,table.rows,[&] {return version;},
        [&] {return provider.Generation();},[&] {return provider.Mappings();});};
    assert(!query());version=8;assert(query());version=12;assert(query());
    Cache sealed(true,extent,true);Table zero;zero.epoch=Epoch(0);zero.Put(other,4,1);
    assert(Query(sealed,provider,zero,source,4));
    Cache oversized(true,extent,true);Table large;
    for(U64 i=0;i<=Gpu::Metal::CpFenceDestinationIndex::MaximumDestinations;++i)
        large.Put(other+4*i,4,i);
    assert(Query(oversized,provider,large,source,4));
    assert(oversized.Statistics().destinations.size_rejects==1);
}
static void DestinationAllocationFailureInvalidatesOldProof() {
    Gpu::Metal::CpFenceDestinationIndex index(true);
    std::unordered_map<U64,unsigned> pending{{other,1}};
    U64 version=1,destination=0x10000;bool fail=false;
    const auto query=[&] {return index.Overlaps(0,4,pending,2,[&] {return version;},
        [&](U64,U64,U64& physical) {
            if(fail)throw std::bad_alloc();physical=destination;return true;
        });};
    assert(!query());
    ++version;destination=0;fail=true;
    assert(query());
    assert(index.Statistics().allocation_failures==1 && !index.Statistics().current_rows);
    fail=false;assert(query()); // newly overlapping metadata; no stale disjoint reuse
}
static void CurrentPendingOracleDifferential() {
    Cache cache(true,extent,true);Provider provider;Table table;
    U64 random=0x471cafe52;
    const auto next=[&] {random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(unsigned i=0;i<400;++i)table.Put(other+4*i,4,i+1);
    unsigned disjoint=0,overlap=0;
    for(unsigned i=0;i<10000;++i) {
        const unsigned key=i%400;
        if(i%17==0)table.Put(alias+4*key,4,1000+i);
        if(i%23==0) {
            auto at=table.rows.begin()->first;auto ticket=table.rows.begin()->second.ticket;
            table.Retire(at,4,ticket);
        }
        if(i%101==0) {
            provider.rows[0].phys=provider.rows[0].phys ? 0 : 0x10000;
            provider.generation+=2;
        }
        const U64 bases[]{source,alias,other,private_address};
        const U64 at=bases[next()%4]+(next()%0x10004);
        const U64 bytes=i%7==0 ? 8 : i%11==0 ? 0x10000 : 4;
        const auto expected=Legacy(provider,table,at,bytes);
        const auto actual=Query(cache,provider,table,at,bytes);
        assert(actual==expected);disjoint+=!actual;overlap+=actual;
    }
    assert(disjoint && overlap);
}
static void Benchmark(unsigned mutate_every) {
    constexpr unsigned queries=100000,destinations=400;
    Cache linear(true,extent,false),indexed(true,extent,true);
    Provider old_maps,new_maps;Table old_pending,new_pending;
    for(unsigned i=0;i<destinations;++i) {
        old_pending.Put(other+4*i,4,i+1);new_pending.Put(other+4*i,4,i+1);
    }
    const auto run=[&](Cache& cache,Provider& provider,Table& table) {
        unsigned conflicts=0;const auto begin=std::chrono::steady_clock::now();
        for(unsigned q=0;q<queries;++q) {
            if(mutate_every && q%mutate_every==0)
                table.Put(other+4*(q%destinations),4,1000+q);
            conflicts+=Query(cache,provider,table,source+((4*q)&0x3fffu),4);
        }
        assert(!conflicts);
        return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
    };
    const auto old_ms=run(linear,old_maps,old_pending),new_ms=run(indexed,new_maps,new_pending);
    const auto old=linear.Statistics(),now=indexed.Statistics();
    assert(old.lookups==U64(queries)*(destinations+1));
    assert(now.destinations.queries==queries);
    assert(now.destinations.rebuilds+now.destinations.reused==queries);
    assert(now.lookups==queries+destinations*now.destinations.rebuilds);
    assert(now.destinations.probes<=10*U64(queries));
    std::printf("CPU ONLY 400 destinations x 100000 queries; mutate-every=%u: map-cache-linear %.3f ms, destination-index %.3f ms; old-lookups=%llu new-lookups=%llu rebuilds=%llu reuses=%llu interval-probes=%llu; no GPU/FPS conclusion\n",
        mutate_every,old_ms,new_ms,(unsigned long long)old.lookups,(unsigned long long)now.lookups,
        (unsigned long long)now.destinations.rebuilds,(unsigned long long)now.destinations.reused,
        (unsigned long long)now.destinations.probes);
}
int main() {
    AliasesBoundariesAndCurrentTransactions();RemapAndGenerationGuards();
    PendingEpochGuardsAndBounds();DestinationAllocationFailureInvalidatesOldProof();
    CurrentPendingOracleDifferential();
    unsigned mutate_every=0;
    if(const char* value=std::getenv("BB_CP_INDEX_BENCH_MUTATE_EVERY")) {
        char* end=nullptr;const auto parsed=std::strtoul(value,&end,10);
        assert(end!=value && !*end && parsed<=100000);mutate_every=unsigned(parsed);
    }
    Benchmark(mutate_every);
    std::puts("CP destination index CPU contracts passed");
}
