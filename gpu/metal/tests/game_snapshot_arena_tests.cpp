// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/snapshot_arena.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace A=Gpu::Metal::SnapshotArena;
namespace {
// A small byte-backed arena: verify preserved contents and lease decisions,
// including allocations that have not been submitted to the simulated GPU.
struct Arena {
    static constexpr A::Counter Capacity=1024;
    std::array<unsigned char,Capacity> bytes{};
    A::Counter head=0,epoch=0,pin=A::NoLease;
    std::atomic<A::Counter> released{0};
    A::Counter Allocate(A::Counter count,unsigned char value) {
        auto plan=A::Allocate(Capacity,head,released.load(),count);
        assert(plan.status==A::Status::Ready);
        A::Publish(released,plan.released);head=plan.end;
        std::memset(bytes.data()+plan.begin%Capacity,value,count);
        return plan.begin;
    }
    void CommitAndComplete() {epoch=head;A::Publish(released,A::ReleasePoint(head,pin));}
    void Check(A::Counter begin,unsigned count,unsigned char value) const {
        for(unsigned k=0;k<count;++k) assert(bytes[(begin+k)%Capacity]==value);
    }
};
void WrappedCache() {
    Arena a;
    const auto old=a.Allocate(256,0x19);
    assert(A::Reusable(old,256,a.head,a.epoch,a.Capacity));
    a.CommitAndComplete();
    a.Allocate(768,0x44);a.CommitAndComplete();
    a.Allocate(256,0xa7); // same physical bytes as the old cached snapshot
    assert(a.bytes[0]==0xa7);
    assert(!A::Reusable(old,256,a.head,a.epoch,a.Capacity));
}
void ReleasedThenRebound() {
    Arena a;
    const auto old=a.Allocate(256,0x29);a.CommitAndComplete();
    // Its source generation is unchanged and the old bytes still exist, but
    // their GPU allocation is already released. Rebinding needs a new copy.
    assert(!A::Reusable(old,256,a.head,a.epoch,a.Capacity));
    const auto rebound=a.Allocate(256,0x29);
    assert(rebound!=old && A::Reusable(rebound,256,a.head,a.epoch,a.Capacity));
    a.Allocate(512,0x51);
    auto blocked=A::Allocate(a.Capacity,a.head,a.released.load(),512);
    assert(blocked.status==A::Status::NeedCompletion);
    a.Check(rebound,256,0x29); // refusal preserved the rebound bytes
}
void OpenCommandAndBatch() {
    Arena a;
    const auto first=a.Allocate(768,0x31);
    const auto before=a.bytes;
    // Waiting only for previously committed work cannot free these bytes:
    // there is no submitted command buffer at all yet.
    assert(A::Allocate(a.Capacity,a.head,a.released.load(),512).status==A::Status::NeedCompletion);
    assert(a.bytes==before);a.Check(first,768,0x31);
    a.CommitAndComplete(); // safe pre-encoder boundary submits the open work
    const auto next=a.Allocate(512,0x62);a.Check(next,512,0x62);
    // A whole multi-stage batch is refused before any stage is copied.
    const auto saved=a.bytes;
    assert(A::Allocate(a.Capacity,a.head,a.released.load(),1280).status==A::Status::Invalid);
    assert(a.bytes==saved);
}
void PreparationAcrossCommit() {
    Arena a;
    {
        A::Pin guard(a.pin);
        const auto prepared=a.Allocate(768,0x73);a.pin=prepared;
        a.CommitAndComplete(); // e.g. a diagnostic capture before the draw
        assert(a.released.load()==prepared);
        assert(A::Allocate(a.Capacity,a.head,a.released.load(),512).status==A::Status::NeedCompletion);
        a.Check(prepared,768,0x73);
        // The subsequent encoder can still use the entire prepared batch.
    }
    assert(a.pin==A::NoLease);
    a.CommitAndComplete();assert(a.released.load()==a.head);
    a.Allocate(512,0x84);
}
void ReleasedPinStartsNewEpoch() {
    Arena a;
    A::Counter lease=0;
    {
        A::Pin guard(a.pin);
        lease=a.Allocate(256,0x95);a.pin=lease;
        a.CommitAndComplete(); // submitted while preparation is pinned
        assert(a.released.load()==lease);
    }
    // A full drain with no open command buffer still starts a new reuse epoch:
    // the allocation is now reclaimable, although its bytes have not changed.
    A::Publish(a.released,a.head);a.epoch=a.head;
    assert(!A::Reusable(lease,256,a.head,a.epoch,a.Capacity));
    const auto rebound=a.Allocate(256,0x95);
    assert(rebound!=lease);a.Check(rebound,256,0x95);
}
void EmptyTailAndMonotonicCompletion() {
    Arena a;a.Allocate(768,0x12);a.CommitAndComplete();
    const auto next=a.Allocate(768,0x23);
    assert(next==1024 && a.released.load()==1024);a.Check(next,768,0x23);
    // Completion callbacks can arrive after a newer callback or padding skip.
    A::Publish(a.released,768);assert(a.released.load()==1024);
    std::vector<std::thread> callbacks;
    for(unsigned k=0;k<8;++k) callbacks.emplace_back([&,k] {A::Publish(a.released,k*256);});
    for(auto& t:callbacks)t.join();assert(a.released.load()==1792);
    assert(A::Allocate(1024,A::NoLease-128,A::NoLease-128,256).status==A::Status::Invalid);
}
}
int main() {
    WrappedCache();ReleasedThenRebound();OpenCommandAndBatch();
    PreparationAcrossCommit();ReleasedPinStartsNewEpoch();EmptyTailAndMonotonicCompletion();
    std::puts("Snapshot arena: wrap, released/rebound leases, open commands, stage batches, pins and concurrent completion passed");
}
