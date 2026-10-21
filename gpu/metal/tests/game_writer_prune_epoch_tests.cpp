// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/guest_writer_prune_epoch.h"
#include <cassert>
#include <cstdio>
#include <vector>

using Gate=Gpu::Metal::GuestWriterPruneEpoch;
struct Writer {
    unsigned id;
    Gate::Flag completed=std::make_shared<std::atomic<bool>>(false);
};

static void ConfigurationAndLegacy() {
    assert(!Gate::ConfiguredEnabled(nullptr));
    for(const char* invalid:{"","0","01","true","1 "," 1","-1","2"})
        assert(!Gate::ConfiguredEnabled(invalid));
    assert(Gate::ConfiguredEnabled("1"));
    Gate legacy;
    assert(!legacy.Enabled() && !legacy.SharedState());
    std::vector<Writer> writers{{1},{2}};
    legacy.Prune(writers);assert(writers.size()==2);
    writers[0].completed->store(true,std::memory_order_release);
    legacy.Prune(writers);assert(writers.size()==1 && writers[0].id==2);
    // OFF always compacts, even without an epoch notification. Conditional
    // probes add no atomic reads/counters to the legacy admission path.
    assert(!legacy.RetainedCompleted(writers[0].completed,Gate::Use::GpuWitness));
    assert(!legacy.Statistics().calls && !legacy.Statistics().conditional_checks);
}

static void RepeatedReadsAndNewPendingWriter() {
    Gate gate(true);std::vector<Writer> writers{{1},{2}};
    gate.Prune(writers);
    for(unsigned i=0;i<1000;++i)gate.Prune(writers);
    writers.push_back({3}); // appending an incomplete ticket needs no rescan
    gate.Prune(writers);
    assert(writers.size()==3 && gate.Statistics().scans==1);
    assert(gate.Statistics().examined==2 && gate.Statistics().skipped==1001);
    assert(!gate.RetainedCompleted(writers.back().completed,Gate::Use::GpuWitness));
    Gate::Complete(writers[0].completed,gate.SharedState());
    gate.Prune(writers);
    assert(writers.size()==2 && writers[0].id==2 && writers[1].id==3);
    assert(gate.CompletionEpoch()==1 && gate.Statistics().removed==1);
    assert(gate.Statistics().scans==2 && gate.Statistics().examined==5);
}

static void FlagBeforeEpochGap() {
    Gate gate(true);std::vector<Writer> writers{{1},{2}};
    gate.Prune(writers);
    // Deterministically pause the same completion transaction between its
    // release flag store and its epoch publication. Compaction can skip.
    writers[0].completed->store(true,std::memory_order_release);
    gate.Prune(writers);assert(writers.size()==2 && gate.Statistics().skipped==1);
    // A retained item cannot reject a CPU read, block as unknown GPU work,
    // OR qualify as the pending GPU producer needed by texture snapshots.
    for(auto use:{Gate::Use::CpuReject,Gate::Use::GpuUnknown,Gate::Use::GpuWitness})
        assert(gate.RetainedCompleted(writers[0].completed,use));
    assert(!gate.RetainedCompleted(writers[1].completed,Gate::Use::GpuWitness));
    gate.SharedState()->Advance();
    gate.Prune(writers);assert(writers.size()==1 && writers[0].id==2);
    const auto& s=gate.Statistics();
    assert(s.completed_ignored[0]==1 && s.completed_ignored[1]==1 && s.completed_ignored[2]==1);
    assert(s.conditional_checks==4);
}

static void CompletionDuringScan() {
    Gate gate(true);std::vector<Writer> writers{{1},{2}};
    const auto first=writers[0].completed;
    bool completed=false;
    gate.Prune(writers,[&](const Writer& writer) {
        if(writer.id==2 && !completed) {
            Gate::Complete(first,gate.SharedState());completed=true;
        }
        return writer.completed->load(std::memory_order_acquire);
    });
    // Writer1 was inspected before it completed. The post-scan state must
    // remember epoch0, not reload epoch1 and accidentally suppress this scan.
    assert(completed && writers.size()==2 && gate.CompletionEpoch()==1);
    gate.Prune(writers);
    assert(writers.size()==1 && writers[0].id==2 && gate.Statistics().scans==2);
}

static void CompletionAfterReplacementAndOwnerDestruction() {
    Gate::State surviving;
    auto old=std::make_shared<std::atomic<bool>>(false);
    {
        Gate gate(true);surviving=gate.SharedState();
        std::vector<Writer> writers{{1,old}};
        gate.Prune(writers);
        writers.clear();writers.push_back({2}); // bounded-registry replacement
        gate.Prune(writers);assert(gate.Statistics().skipped==1);
        Gate::Complete(old,surviving); // old callback only causes an extra scan
        gate.Prune(writers);assert(writers.size()==1 && writers[0].id==2);
        assert(gate.Statistics().scans==2 && gate.Statistics().removed==0);
    }
    auto after=std::make_shared<std::atomic<bool>>(false);
    // This is the exact completion helper used by Metal blocks. Neither gate
    // nor registry/renderer lifetime is needed by a delayed completion.
    Gate::Complete(after,surviving);
    assert(after->load(std::memory_order_acquire));
    assert(surviving->epoch.load(std::memory_order_acquire)==2);
    Gate independent(true);std::vector<Writer> other{{3}};
    independent.Prune(other);
    Gate::Complete(after,surviving); // cannot invalidate another renderer's gate
    independent.Prune(other);
    assert(independent.CompletionEpoch()==0 && independent.Statistics().skipped==1);
}

static void SaturationNeverSkips() {
    Gate gate(true,Gate::Maximum-1);std::vector<Writer> writers{{1},{2}};
    gate.Prune(writers);gate.Prune(writers);
    assert(gate.Statistics().scans==1 && gate.Statistics().skipped==1);
    Gate::Complete(writers[0].completed,gate.SharedState());
    assert(gate.CompletionEpoch()==Gate::Maximum);
    gate.Prune(writers);assert(writers.size()==1);
    Gate::Complete(writers[0].completed,gate.SharedState());
    assert(gate.CompletionEpoch()==Gate::Maximum); // no wrapping/ABA
    gate.Prune(writers);gate.Prune(writers);
    assert(writers.empty() && gate.Statistics().scans==4);
    assert(gate.Statistics().skipped==1);
}

int main() {
    ConfigurationAndLegacy();RepeatedReadsAndNewPendingWriter();FlagBeforeEpochGap();
    CompletionDuringScan();CompletionAfterReplacementAndOwnerDestruction();SaturationNeverSkips();
    std::puts("writer prune epoch CPU contracts passed");
}
