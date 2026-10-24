// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/texture_write_journal.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

namespace {thread_local int fail_allocation_after=-1;}
void* operator new(std::size_t bytes) {
    if(fail_allocation_after==0){fail_allocation_after=-1;throw std::bad_alloc();}
    if(fail_allocation_after>0)--fail_allocation_after;
    if(auto* memory=std::malloc(bytes ? bytes : 1))return memory;
    throw std::bad_alloc();
}
void operator delete(void* pointer)noexcept{std::free(pointer);}
void* operator new[](std::size_t bytes){return ::operator new(bytes);}
void operator delete[](void* pointer)noexcept{std::free(pointer);}

using Gpu::Metal::TextureWriteJournal;
using R=TextureWriteJournal::Range;
using Relation=TextureWriteJournal::Relation;

static void ConfigurationTests() {
    using J=TextureWriteJournal;
    assert(J{}.Capacity()==512);
    assert(J::ConfiguredLimit(nullptr)==512);
    for(const char* invalid:{"","0","-1","+512"," 4096","4096 ","4k","4096x",
            "8193","18446744073709551615"})assert(J::ConfiguredLimit(invalid)==512);
    assert(J::ConfiguredLimit("1")==1);
    assert(J::ConfiguredLimit("512")==512);
    assert(J::ConfiguredLimit("4096")==4096);
    assert(J::ConfiguredLimit("00004096")==4096);
    assert(J::ConfiguredLimit("8192")==8192);
    assert(J(J::kMaximumRecords+1).Capacity()==8192);
    for(const char* disabled:{"","0","true","01","1 ","-1","02","2 ","3"})assert(!J::PageIndexEnabled(disabled));
    assert(!J::PageIndexEnabled(nullptr) && J::PageIndexEnabled("1") && J::PageIndexEnabled("2"));
    for(const char* disabled:{"","0","1","true","02","2 ","-2","3"})assert(!J::PageIndexUsesBoundedHistory(disabled));
    assert(!J::PageIndexUsesBoundedHistory(nullptr) && J::PageIndexUsesBoundedHistory("2"));
    assert(!J{}.PageIndexActive() && !J{}.FullFrameValidation());
    J oversized(2,0,J::kMaximumIndexedExtent+1);assert(!oversized.PageIndexActive());
    J maximum(2,0,J::kMaximumIndexedExtent);
    assert(maximum.PageIndexActive() && maximum.PageIndexExtent()==(64ull<<30));
    assert(!maximum.PageIndexBytes()); // construction does not allocate the 64 MiB maximum
}

// Independent unbounded, byte-exact history: no page rounding, pruning,
// eviction, or index helpers. Indexed answers may be more conservative.
class ExactHistory {
public:
    void Publish(std::span<const R> physical,bool unknown=false,bool cpu=false) {
        bool nonempty=unknown;
        for(const auto& range:physical) {
            nonempty|=range.bytes!=0;
            unknown|=range.address>UINT64_MAX-range.bytes;
        }
        if(!nonempty)return;
        ++epoch;
        if(unknown)events.push_back({epoch,{},true,cpu});
        else for(const auto& range:physical)if(range.bytes)events.push_back({epoch,range,false,cpu});
    }
    TextureWriteJournal::Query Inspect(std::uint64_t checked,std::span<const R> source,bool complete=true) const {
        if(checked>epoch || !complete || source.empty())return {Relation::Unknown,true};
        for(const auto& range:source)
            if(!range.bytes || range.address>UINT64_MAX-range.bytes)return {Relation::Unknown,true};
        if(checked==epoch)return {Relation::Unchanged,false};
        TextureWriteJournal::Query answer{Relation::Disjoint,false};
        for(const auto& event:events)if(event.epoch>checked) {
            bool hit=event.unknown;
            for(const auto& range:source)
                hit|=event.range.bytes && range.address<event.range.address+event.range.bytes &&
                    event.range.address<range.address+range.bytes;
            if(hit) {
                if(event.unknown)answer.relation=Relation::Unknown;
                else if(answer.relation!=Relation::Unknown)answer.relation=Relation::Overlap;
                answer.cpu_callbacks|=event.cpu;
            }
        }
        return answer;
    }
    std::uint64_t Epoch() const {return epoch;}
private:
    struct Event {std::uint64_t epoch;R range;bool unknown,cpu;};
    std::vector<Event> events;
    std::uint64_t epoch=0;
};

// Independent original bounded-deque policy using a vector, byte arithmetic
// and its own watermark/coalescing logic. No production overlap/index helper.
class LegacyBoundedHistory {
public:
    explicit LegacyBoundedHistory(std::size_t limit,std::uint64_t initial=0)
        :capacity(std::min(limit,std::size_t(8192))),epoch(initial){}
    void Publish(std::span<const R> ranges,bool unknown=false,bool cpu=false) {
        bool any=unknown;
        for(const auto range:ranges){any|=range.bytes!=0;unknown|=range.address>UINT64_MAX-range.bytes;}
        if(!any)return;
        if(epoch==UINT64_MAX){saturated=true;forgotten=epoch;records.clear();return;}
        ++epoch;
        const auto append=[&](R range,bool uncertain) {
            if(!capacity){forgotten=epoch;return;}
            if(records.size()==capacity){forgotten=std::max(forgotten,records.front().epoch);records.erase(records.begin());}
            records.push_back({epoch,range,uncertain,cpu});
        };
        if(unknown){append({},true);return;}
        for(const auto range:ranges)if(range.bytes) {
            if(!records.empty()) {
                auto& prior=records.back();
                if(prior.epoch==epoch && !prior.unknown &&
                    range.address<=prior.range.address+prior.range.bytes &&
                    prior.range.address<=range.address+range.bytes) {
                    const auto end=std::max(prior.range.address+prior.range.bytes,range.address+range.bytes);
                    prior.range.address=std::min(prior.range.address,range.address);
                    prior.range.bytes=end-prior.range.address;continue;
                }
            }
            append(range,false);
        }
    }
    TextureWriteJournal::Query Inspect(std::uint64_t checked,std::span<const R> source,bool complete=true) const {
        if(saturated || checked>epoch || checked<forgotten)return {Relation::Unknown,true};
        if(checked==epoch)return {Relation::Unchanged,false};
        if(!complete)return {Relation::Unknown,true};
        TextureWriteJournal::Query answer{Relation::Disjoint,false};
        for(auto it=records.rbegin();it!=records.rend() && it->epoch>checked;++it) {
            bool hit=it->unknown;
            for(const auto range:source) {
                if(!range.bytes || range.address>UINT64_MAX-range.bytes)return {Relation::Unknown,true};
                hit|=range.bytes && it->range.bytes && range.address<it->range.address+it->range.bytes &&
                    it->range.address<range.address+range.bytes;
            }
            if(hit) {
                if(it->unknown)answer.relation=Relation::Unknown;
                else if(answer.relation!=Relation::Unknown)answer.relation=Relation::Overlap;
                answer.cpu_callbacks|=it->cpu;
            }
        }
        return answer;
    }
    std::uint64_t Epoch()const{return epoch;}
    std::uint64_t ForgottenThrough()const{return forgotten;}
private:
    struct Record {std::uint64_t epoch;R range;bool unknown,cpu;};
    std::size_t capacity;
    std::uint64_t epoch=0,forgotten=0;
    bool saturated=false;
    std::vector<Record> records;
};

static void CheckBounded(const TextureWriteJournal& indexed,const LegacyBoundedHistory& old,
                         std::uint64_t checked,std::span<const R> source,bool complete=true) {
    assert(!indexed.FullFrameValidation());
    assert(indexed.Epoch()==old.Epoch() && indexed.ForgottenThrough()==old.ForgottenThrough());
    const auto expected=old.Inspect(checked,source,complete),actual=indexed.Inspect(checked,source,complete);
    if(expected.FullCheck())assert(actual.FullCheck());
    if(expected.cpu_callbacks)assert(actual.cpu_callbacks);
    if(expected.relation==Relation::Unknown)assert(actual.relation==Relation::Unknown);
}

static void BoundedIndexTests() {
    using J=TextureWriteJournal;
    constexpr auto extent=4*J::kBlockPages*J::kPageBytes+17;
    const std::array<R,1> source{{{0,64}}},other{{{2*J::kPageBytes,4}}};
    J bounded(2,0,extent,true),persistent(2,0,extent);LegacyBoundedHistory old(2);
    for(unsigned k=0;k<8;++k){bounded.Publish(other);persistent.Publish(other);old.Publish(other);}
    assert(bounded.PageIndexActive() && !bounded.FullFrameValidation() && persistent.FullFrameValidation());
    // Mode2 retains the old forgotten check rather than trusting persistent
    // disjoint history; this preserves its raw CPU content-validation policy.
    assert(bounded.Inspect(0,source).relation==Relation::Unknown && bounded.Inspect(0,source).cpu_callbacks);
    assert(persistent.Inspect(0,source).relation==Relation::Disjoint);
    CheckBounded(bounded,old,0,source);CheckBounded(bounded,old,bounded.ForgottenThrough(),source);
    assert(bounded.Inspect(bounded.ForgottenThrough(),source).relation==Relation::Disjoint);
    assert(bounded.GetStats().forgotten_queries && bounded.GetStats().indexed_queries && !bounded.GetStats().examined_records);
    // Page rounding may conservatively introduce a check, never remove one.
    J rounded(512,0,extent,true);LegacyBoundedHistory rounded_old(512);
    const std::array<R,1> same_page{{{128,4}}};rounded.Publish(same_page,false,true);rounded_old.Publish(same_page,false,true);
    assert(rounded_old.Inspect(0,source).relation==Relation::Disjoint);
    assert(rounded.Inspect(0,source).relation==Relation::Overlap && rounded.Inspect(0,source).cpu_callbacks);
    CheckBounded(rounded,rounded_old,0,source);
    std::uint64_t random=0x9e3779b97f4a7c15ull;
    const auto next=[&](){random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(const std::size_t capacity:{0u,1u,2u,7u,64u,512u,8192u}) {
        J index(capacity,0,extent,true);LegacyBoundedHistory reference(capacity);
        for(unsigned step=0;step<320;++step) {
            std::array<R,8> writes{};
            for(auto& range:writes){range.address=next()%extent;range.bytes=std::min(1+next()%65536,extent-range.address);}
            if(step%23==0)writes[0]={extent,4};
            if(step%47==0)writes[1]={UINT64_MAX-2,4};
            if(step%19==0)writes[2].bytes=0;
            const bool unknown=step%31==0,cpu=next()%3==0;
            index.Publish(writes,unknown,cpu);reference.Publish(writes,unknown,cpu);
            for(unsigned query=0;query<7;++query) {
                std::array<R,3> sources{};
                for(auto& range:sources){range.address=next()%extent;range.bytes=std::min(1+next()%(2*J::kBlockPages*J::kPageBytes),extent-range.address);}
                const auto checked=query==0 ? index.ForgottenThrough() : query==1 && index.ForgottenThrough()
                    ? index.ForgottenThrough()-1 : next()%(index.Epoch()+2);
                CheckBounded(index,reference,checked,sources,step%29!=0);
            }
        }
        const std::array<R,1> outside{{{extent,1}}},overflow{{{UINT64_MAX-1,4}}},empty{{{0,0}}};
        for(const auto* bad:{&outside,&overflow,&empty})CheckBounded(index,reference,index.Epoch(),*bad);
        CheckBounded(index,reference,index.Epoch(),source,false);
    }
    for(const auto initial:{std::uint64_t(7),UINT64_MAX-1}) {
        J index(1,initial,extent,true);LegacyBoundedHistory reference(1,initial);
        for(unsigned k=0;k<3;++k) {
            index.Publish(source,false,true);reference.Publish(source,false,true);
            CheckBounded(index,reference,initial,source);
            CheckBounded(index,reference,index.Epoch(),source);
        }
    }
    for(int failure=0;failure<2;++failure) {
        J fallback(512,0,extent,true);LegacyBoundedHistory reference(512);
        fail_allocation_after=failure;fallback.Publish(source,false,true);fail_allocation_after=-1;
        reference.Publish(source,false,true);
        assert(!fallback.PageIndexActive() && !fallback.FullFrameValidation());
        CheckBounded(fallback,reference,0,source);
        assert(fallback.Inspect(0,source).relation==Relation::Overlap && fallback.Inspect(0,source).cpu_callbacks);
        fallback.Publish(other);reference.Publish(other);
        CheckBounded(fallback,reference,0,source);CheckBounded(fallback,reference,1,source);
        assert(fallback.Inspect(1,source).relation==Relation::Disjoint);
    }
    std::puts("Bounded indexed journal: independent legacy-policy oracle, unchanged watermark/full-check requirements, CPU flags, block edges, allocation fallback and saturation passed");
}

static void CheckConservative(const TextureWriteJournal& indexed,const ExactHistory& exact,
                              std::uint64_t checked,std::span<const R> source,bool complete=true) {
    assert(indexed.Epoch()==exact.Epoch());
    const auto expected=exact.Inspect(checked,source,complete),actual=indexed.Inspect(checked,source,complete);
    if(expected.FullCheck())assert(actual.FullCheck()); // never false Disjoint or Unchanged
    if(expected.relation==Relation::Unknown)assert(actual.relation==Relation::Unknown);
    if(expected.cpu_callbacks)assert(actual.cpu_callbacks); // never lose an older CPU effect
}

static void PageIndexTests() {
    using J=TextureWriteJournal;
    constexpr auto page=J::kPageBytes,extent=4*page+7;
    const std::array<R,1> zero{{{0,4}}},same_page{{{100,4}}},next_page{{{page,4}}};
    J indexed(2,0,extent);assert(indexed.PageIndexActive() && !indexed.PageIndexBytes());
    indexed.Publish(zero,false,true);
    assert(indexed.PageIndexBytes()==6*16 && indexed.FullFrameValidation());
    assert(indexed.Inspect(0,zero).relation==Relation::Overlap && indexed.Inspect(0,zero).cpu_callbacks);
    const auto conservative=indexed.Inspect(0,same_page);
    assert(conservative.relation==Relation::Overlap && conservative.cpu_callbacks); // allowed page false positive
    assert(indexed.Inspect(0,next_page).relation==Relation::Disjoint);
    indexed.Publish({},true,false);
    const auto older_cpu=indexed.Inspect(0,zero);
    assert(older_cpu.relation==Relation::Unknown && older_cpu.cpu_callbacks);
    assert(!indexed.Inspect(1,zero).cpu_callbacks);
    indexed.Publish({},true,true);const auto observed=indexed.Epoch();
    indexed.Publish(next_page); // a newer GPU store must not erase the unknown CPU epoch
    assert(indexed.Inspect(0,next_page).cpu_callbacks);
    assert(indexed.Inspect(observed,next_page).relation==Relation::Overlap &&
        !indexed.Inspect(observed,next_page).cpu_callbacks);
    J successive(1,0,extent);successive.Publish(zero,false,true);successive.Publish(zero);
    assert(successive.Inspect(0,zero).cpu_callbacks && !successive.Inspect(1,zero).cpu_callbacks);

    J boundary(2,0,extent);
    const std::array<R,1> end{{{page-1,1}}},cross{{{page-1,2}}},tail{{{extent-1,1}}};
    boundary.Publish(end);assert(boundary.Inspect(0,next_page).relation==Relation::Disjoint);
    boundary.Publish(cross);assert(boundary.Inspect(0,next_page).relation==Relation::Overlap);
    boundary.Publish(tail,false,true);
    assert(boundary.Inspect(0,tail).cpu_callbacks); // non-page-aligned pool end
    const std::array<R,2> holes{{{0,4},{3*page,4}}};
    J sparse(1,0,extent);sparse.Publish(next_page,false,true);
    assert(sparse.Inspect(0,holes).relation==Relation::Disjoint && !sparse.Inspect(0,holes).cpu_callbacks);
    // Remapping a VA changes its caller-supplied physical source. A previous
    // physical alias still conflicts; a separate physical page remains clean.
    assert(sparse.Inspect(0,next_page).cpu_callbacks && sparse.Inspect(0,zero).relation==Relation::Disjoint);

    const std::array<R,1> empty{{{0,0}}},outside{{{extent,1}}},overrun{{{extent-1,2}}},
        overflow{{{UINT64_MAX-1,4}}};
    for(const auto* invalid:{&empty,&outside,&overrun,&overflow}) {
        const auto bad=sparse.Inspect(sparse.Epoch(),*invalid);
        assert(bad.relation==Relation::Unknown && bad.cpu_callbacks);
    }
    assert(sparse.Inspect(0,{},true).relation==Relation::Unknown);
    assert(sparse.Inspect(sparse.Epoch(),zero,false).cpu_callbacks);
    const auto before=sparse.Epoch();sparse.Publish(empty);assert(sparse.Epoch()==before);
    sparse.Publish(outside);assert(sparse.Inspect(before,zero).relation==Relation::Unknown);
    const auto outside_gpu=sparse.Epoch();sparse.Publish(overrun,false,true);
    assert(sparse.Inspect(outside_gpu,zero).cpu_callbacks);
    sparse.Publish(overflow);assert(sparse.Inspect(outside_gpu,zero).relation==Relation::Unknown);

    // Unknown-only journals need no page allocation, but retain both globals.
    J global(0,0,extent);global.Publish({},true,true);global.Publish({},true,false);
    assert(!global.PageIndexBytes());
    assert(global.Inspect(0,zero).relation==Relation::Unknown && global.Inspect(0,zero).cpu_callbacks);
    assert(!global.Inspect(1,zero).cpu_callbacks);
    J partial(1,0,extent);partial.Publish(holes,false,true);
    assert(partial.ForgottenThrough()==partial.Epoch() && partial.Inspect(0,holes).cpu_callbacks);
    assert(!partial.GetStats().forgotten_queries && !partial.GetStats().examined_records);
    assert(partial.GetStats().indexed_queries==1 && partial.GetStats().page_checks==2);
    J saturated(1,UINT64_MAX-1,extent);saturated.Publish(zero);assert(saturated.Current(UINT64_MAX));
    saturated.Publish(zero);assert(!saturated.Current(UINT64_MAX));
    assert(saturated.Inspect(UINT64_MAX,zero).relation==Relation::Unknown &&
        saturated.Inspect(UINT64_MAX,zero).cpu_callbacks);
    J future(1,0,extent);assert(future.Inspect(1,zero).relation==Relation::Unknown);
    J nonzero_start(1,7,extent);
    assert(nonzero_start.Inspect(0,zero).relation==Relation::Unknown && nonzero_start.Inspect(0,zero).cpu_callbacks);
    assert(nonzero_start.Inspect(7,zero).relation==Relation::Unchanged);
}

static void RandomizedPageIndexTests() {
    using J=TextureWriteJournal;
    constexpr std::uint64_t extent=3*J::kBlockPages*J::kPageBytes+19;
    std::uint64_t random=0xd1b54a32d192ed03ull;
    const auto next=[&]() {random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(unsigned trial=0;trial<8;++trial) {
        J indexed(1+trial,0,extent);ExactHistory exact;
        for(unsigned step=0;step<300;++step) {
            std::array<R,4> writes{};
            for(auto& range:writes) {
                range.address=next()%extent;range.bytes=1+next()%1024;
                range.bytes=std::min(range.bytes,extent-range.address);
            }
            if(trial>=4 && step%23==0)writes[0]={extent,4};
            if(trial>=4 && step%47==0)writes[1]={UINT64_MAX-2,4};
            if(step%19==0)writes[2].bytes=0;
            const bool unknown=trial>=4 && step%31==0,cpu=next()%3==0;
            indexed.Publish(writes,unknown,cpu);exact.Publish(writes,unknown,cpu);
            for(unsigned query=0;query<5;++query) {
                std::array<R,3> source{};
                for(auto& range:source) {
                    range.address=next()%extent;range.bytes=std::min(1+next()%(2*J::kBlockPages*J::kPageBytes),extent-range.address);
                }
                const auto checked=next()%(exact.Epoch()+2);
                CheckConservative(indexed,exact,checked,source);
                if(step%29==0)CheckConservative(indexed,exact,checked,source,false);
            }
        }
        assert(indexed.GetStats().indexed_queries && indexed.GetStats().page_checks &&
            indexed.GetStats().evicted_records && !indexed.GetStats().examined_records);
    }
}

static void BlockSummaryTests() {
    using J=TextureWriteJournal;
    constexpr auto page=J::kPageBytes,block=page*J::kBlockPages,extent=3*block+7;
    const std::array<R,1> middle{{{block,block}}},edge{{{block-1,2}}},outside{{{0,1}}};
    J journal(1,0,extent);ExactHistory exact;
    journal.Publish(outside,false,true);exact.Publish(outside,false,true);
    assert(journal.Inspect(0,middle).relation==Relation::Disjoint);
    assert(journal.GetStats().block_checks==1 && !journal.GetStats().page_checks);
    assert(!journal.Inspect(0,middle).cpu_callbacks); // outside the wholly covered block
    journal.Publish(edge,false,true);exact.Publish(edge,false,true);
    assert(journal.Inspect(1,middle).relation==Relation::Overlap && journal.Inspect(1,middle).cpu_callbacks);
    const auto checked=journal.Epoch();
    journal.Publish(middle);exact.Publish(middle);
    assert(journal.Inspect(checked,middle).relation==Relation::Overlap && !journal.Inspect(checked,middle).cpu_callbacks);
    // Partial query edges must not borrow a summary from an unrelated page.
    const std::array<R,1> untouched_edge{{{block+2*page,7}}},tail{{{extent-1,1}}};
    J partial(1,0,extent);partial.Publish(edge,false,true);
    assert(partial.Inspect(0,untouched_edge).relation==Relation::Disjoint);
    assert(!partial.Inspect(0,untouched_edge).cpu_callbacks);
    partial.Publish(tail,false,true);assert(partial.Inspect(1,tail).cpu_callbacks);
    for(const auto* source:{&middle,&edge,&outside,&untouched_edge,&tail})
        for(std::uint64_t observed=0;observed<=journal.Epoch();++observed)
            CheckConservative(journal,exact,observed,*source);
    // Both allocation sites must restore the bounded conservative history,
    // including a completed CPU effect and the triggering publish's epoch.
    for(int failure=0;failure<2;++failure) {
        J fallback(1,0,extent);
        fail_allocation_after=failure;fallback.Publish(middle,false,true);fail_allocation_after=-1;
        assert(!fallback.PageIndexActive() && !fallback.PageIndexBytes());
        assert(fallback.GetStats().index_allocation_failures==1);
        const auto unknown=fallback.Inspect(0,middle);
        assert(unknown.relation==Relation::Unknown && unknown.cpu_callbacks);
        fallback.Publish(outside);
        assert(fallback.Inspect(0,middle).relation==Relation::Unknown);
    }
}

template<class Journal> static void PublishPressureFrame(Journal& journal,unsigned frame) {
    // 51 worst-case refined worklists, each at the existing 64-physical-range
    // limit, plus 380 typed CPU stores: 3644 records, with no adjacent merges.
    for(unsigned writer=0;writer<51;++writer) {
        std::array<R,64> ranges{};
        for(unsigned k=0;k<ranges.size();++k)
            ranges[k]={0x100000+std::uint64_t(frame*51+writer)*1024+k*16,4};
        journal.Publish(ranges);
    }
    for(unsigned k=0;k<380;++k) {
        const std::array<R,1> label{{{0x400000+k*16,4}}};
        journal.Publish(label,false,true);
    }
}

static void IndexedPressureTests() {
    using J=TextureWriteJournal;
    J indexed(512,0,16ull<<20),legacy(512);ExactHistory exact;
    const std::array<R,1> untouched{{{0,64}}},affected{{{0x800000,4}}};
    indexed.Publish(affected,false,true);legacy.Publish(affected,false,true);exact.Publish(affected,false,true);
    for(unsigned frame=0;frame<3;++frame) {
        PublishPressureFrame(indexed,frame);PublishPressureFrame(legacy,frame);PublishPressureFrame(exact,frame);
    }
    // 10933 records exceeds even the legacy 8192 maximum. The complete page
    // history must distinguish an untouched source from a completed CPU write.
    assert(indexed.GetStats().evicted_records==10933-512);
    assert(indexed.Inspect(0,untouched).relation==Relation::Disjoint &&
        !indexed.Inspect(0,untouched).cpu_callbacks);
    assert(indexed.Inspect(0,affected).relation==Relation::Overlap && indexed.Inspect(0,affected).cpu_callbacks);
    CheckConservative(indexed,exact,0,untouched);CheckConservative(indexed,exact,0,affected);
    assert(legacy.Inspect(0,untouched).relation==Relation::Unknown);
    assert(!indexed.GetStats().forgotten_queries && !indexed.GetStats().examined_records);
    assert(indexed.PageIndexBytes()==16640 && indexed.GetStats().page_checks);

    // Invalid configuration uses the exact legacy path instead of allocating
    // outside the fixed 64 GiB extent bound. Allocation failure uses that same
    // permanently disabled mode (both bad_alloc sites are exercised above).
    J fallback(1,0,J::kMaximumIndexedExtent+1);
    fallback.Publish(affected);fallback.Publish(untouched);
    assert(!fallback.PageIndexActive() && !fallback.PageIndexBytes());
    assert(fallback.Inspect(0,affected).relation==Relation::Unknown && fallback.GetStats().forgotten_queries==1);
}

static void PressureTests() {
    using J=TextureWriteJournal;
    constexpr std::uint64_t frame_records=51*64+380;
    static_assert(frame_records==3644);
    const std::array<R,1> source{{{0,64}}};
    J small(512),medium(4096),large(8192);
    for(auto* journal:{&small,&medium,&large})PublishPressureFrame(*journal,0);
    assert(small.Size()==512 && small.GetStats().evicted_records==frame_records-512);
    const auto forgotten=small.Inspect(0,source);
    assert(forgotten.relation==Relation::Unknown && forgotten.cpu_callbacks);
    assert(small.GetStats().queries==1 && small.GetStats().queried_source_bytes==64);
    assert(small.GetStats().forgotten_queries==1 && small.GetStats().forgotten_source_bytes==64);
    assert(!small.GetStats().examined_records);
    for(auto* journal:{&medium,&large}) {
        assert(journal->Size()==frame_records && !journal->GetStats().evicted_records);
        const auto unaffected=journal->Inspect(0,source);
        assert(unaffected.relation==Relation::Disjoint && !unaffected.cpu_callbacks);
        assert(journal->GetStats().queries==1 && journal->GetStats().queried_source_bytes==64);
        assert(!journal->GetStats().forgotten_queries && !journal->GetStats().forgotten_source_bytes);
        assert(journal->GetStats().examined_records==frame_records);
    }
    PublishPressureFrame(medium,1);PublishPressureFrame(large,1);
    assert(medium.Size()==4096 && medium.GetStats().evicted_records==frame_records*2-4096);
    assert(medium.Inspect(0,source).relation==Relation::Unknown);
    assert(large.Size()==frame_records*2 && !large.GetStats().evicted_records);
    assert(large.Inspect(0,source).relation==Relation::Disjoint);
    PublishPressureFrame(large,2);
    assert(large.Size()==8192 && large.GetStats().evicted_records==frame_records*3-8192);
    assert(large.Inspect(0,source).relation==Relation::Unknown);

    // Keeping enough history must preserve a real affected/deferred write,
    // rather than converting every retained old source into Disjoint.
    J affected(4096);affected.Publish(source,false,true);
    PublishPressureFrame(affected,0);
    const auto actual_write=affected.Inspect(0,source);
    assert(actual_write.relation==Relation::Overlap && actual_write.cpu_callbacks);
    assert(!affected.GetStats().forgotten_queries);

    // A recent consumer only examines newer records, even with a full frame
    // of retained history. All ranges belonging to the new epoch are checked.
    J recent(4096);PublishPressureFrame(recent,0);
    const auto checked=recent.Epoch();
    const std::array<R,3> newest{{{0x800000,4},{0x800010,4},{32,4}}};recent.Publish(newest);
    const auto change=recent.Inspect(checked,source);
    assert(change.relation==Relation::Overlap && !change.cpu_callbacks);
    assert(recent.GetStats().examined_records==3);
    assert(recent.Inspect(recent.Epoch(),source).relation==Relation::Unchanged);
    assert(recent.GetStats().examined_records==3);
}

static void ReverseAndPartialEpochTests() {
    using J=TextureWriteJournal;
    const std::array<R,1> source{{{16,4}}},unrelated{{{4096,4}}};
    J reverse(4);
    reverse.Publish(source,false,true); // older CPU effect must survive a newer unknown GPU record
    const auto after_cpu=reverse.Epoch();
    reverse.Publish(unrelated);
    reverse.Publish({},true,false);
    const auto old_source=reverse.Inspect(0,source);
    assert(old_source.relation==Relation::Unknown && old_source.cpu_callbacks);
    assert(reverse.GetStats().examined_records==3);
    const auto cpu_observed=reverse.Inspect(after_cpu,source);
    assert(cpu_observed.relation==Relation::Unknown && !cpu_observed.cpu_callbacks);
    assert(reverse.GetStats().examined_records==5);

    // Eviction partway through ONE publish loses its entire epoch for any
    // earlier observer. An observer at that epoch already saw every store.
    J partial(2);
    const std::array<R,3> batch{{{16,4},{32,4},{48,4}}};partial.Publish(batch,false,true);
    assert(partial.Size()==2 && partial.ForgottenThrough()==partial.Epoch());
    assert(partial.GetStats().evicted_records==1);
    assert(partial.Inspect(0,source).relation==Relation::Unknown);
    assert(partial.Inspect(partial.Epoch(),source).relation==Relation::Unchanged);
    assert(partial.GetStats().forgotten_queries==1 && partial.GetStats().forgotten_source_bytes==4);
    assert(partial.GetStats().queries==2 && partial.GetStats().queried_source_bytes==8);
    assert(!partial.GetStats().examined_records);
}

int main() {
    using J=TextureWriteJournal;
    ConfigurationTests();PressureTests();ReverseAndPartialEpochTests();
    PageIndexTests();RandomizedPageIndexTests();BlockSummaryTests();IndexedPressureTests();BoundedIndexTests();
    assert(!J::Enabled(false,nullptr));assert(!J::Enabled(false,"1"));
    assert(J::Enabled(true,nullptr));assert(J::Enabled(true,"1"));
    assert(!J::Enabled(true,"0"));assert(!J::Enabled(true,"invalid"));
    J journal;
    const std::array<R,2> source{{{0,64},{1024,64}}}; // physical zero is valid
    assert(journal.Inspect(0,source).relation==Relation::Unchanged);
    const std::array<R,1> unrelated{{{4096,128}}};journal.Publish(unrelated);
    assert(journal.Inspect(0,source).relation==Relation::Disjoint);
    const auto first=journal.Epoch();
    const std::array<R,1> alias{{{1040,4}}};journal.Publish(alias);
    assert(journal.Inspect(first,source).relation==Relation::Overlap);
    assert(!journal.Inspect(first,source).cpu_callbacks);
    const auto completed=journal.Epoch();
    // There is intentionally no Flip or completion clear. An unused texture
    // still observes writes when it returns many frames later.
    assert(journal.Inspect(first,source).FullCheck());
    assert(journal.Current(completed));assert(!journal.Current(first));
    journal.Publish(alias,false,true);
    const auto scheduled=journal.Inspect(completed,source);
    assert(scheduled.FullCheck() && scheduled.cpu_callbacks);
    assert(journal.Inspect(journal.Epoch(),source).relation==Relation::Unchanged);
    journal.Publish({},true);
    assert(journal.Inspect(completed,unrelated).relation==Relation::Unknown);
    assert(journal.Inspect(completed,source).cpu_callbacks);
    assert(journal.Inspect(completed,source,false).relation==Relation::Unknown);

    J bounded(2);bounded.Publish(source);
    assert(bounded.Size()==2);const auto observed=bounded.Epoch();
    bounded.Publish(unrelated);assert(bounded.Size()==2 && bounded.ForgottenThrough()==observed);
    assert(bounded.Inspect(0,alias).relation==Relation::Unknown);
    assert(bounded.Inspect(observed,alias).relation==Relation::Disjoint);
    bounded.Publish(alias);bounded.Publish(alias);
    assert(bounded.Inspect(observed,source).relation==Relation::Unknown);
    const auto refreshed=bounded.Epoch();assert(bounded.Current(refreshed));
    assert(bounded.Inspect(refreshed,source).relation==Relation::Unchanged);

    J merging;
    const std::array<R,4> adjacent{{{16,16},{32,16},{24,8},{128,4}}};merging.Publish(adjacent);
    assert(merging.Size()==2);
    const std::array<R,1> tail{{{47,1}}},gap{{{48,80}}};
    assert(merging.Inspect(0,tail).relation==Relation::Overlap);
    assert(merging.Inspect(0,gap).relation==Relation::Disjoint);
    const std::array<R,1> overflow{{{UINT64_MAX-1,4}}};merging.Publish(overflow);
    assert(merging.Inspect(0,gap).relation==Relation::Unknown);
    assert(merging.Inspect(merging.Epoch()+1,gap).relation==Relation::Unknown);
    J saturated(2,UINT64_MAX);saturated.Publish(alias);
    assert(!saturated.Current(UINT64_MAX));assert(saturated.Inspect(UINT64_MAX,source).FullCheck());
    J none(0);none.Publish(alias);assert(!none.Size());assert(none.Inspect(0,source).FullCheck());
    const auto before=journal.Epoch();journal.Publish({});assert(journal.Epoch()==before);
    std::puts("texture write journal: strict bounded configuration, legacy pressure/reverse history, opt-in physical page epochs, 10933-record retention, randomized unbounded-reference differential checks, CPU/unknown ordering, span bounds, aliases, saturation and gating passed");
}
