// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>

namespace Gpu::Metal::SnapshotArena {
using Counter=std::uint64_t;
constexpr Counter NoLease=std::numeric_limits<Counter>::max();
constexpr Counter Alignment=256;
enum class Status { Ready, NeedCompletion, Invalid };
struct Plan {
    Status status=Status::Invalid;
    Counter begin=0,end=0,released=0;
};
inline Counter Aligned(Counter bytes) {
    return bytes && bytes<=NoLease-(Alignment-1) ? (bytes+Alignment-1)&~(Alignment-1) : 0;
}
// Counters are absolute. Only the physical arena offset wraps. Empty rings may
// discard tail padding immediately; no command buffer can still own that tail.
inline Plan Allocate(Counter capacity,Counter head,Counter released,Counter bytes) {
    const auto size=Aligned(bytes);
    if(!capacity || capacity%Alignment || !size || size>capacity || released>head) return {};
    const auto offset=head%capacity;
    const auto padding=size>capacity-offset ? capacity-offset : 0;
    if(head>NoLease-padding || head+padding>NoLease-size) return {};
    const auto begin=head+padding,end=begin+size;
    if(released==head) released=begin;
    return {end-released<=capacity ? Status::Ready : Status::NeedCompletion,begin,end,released};
}
// A completed allocation cannot simply be rebound in a later command buffer:
// its old completion watermark no longer protects that new GPU use. Recopy
// across command-buffer epochs, even if the source generation is unchanged.
inline bool Reusable(Counter begin,Counter bytes,Counter head,Counter epoch,Counter capacity) {
    const auto size=Aligned(bytes);
    return begin!=NoLease && size && size<=capacity && begin>=epoch && begin<=head &&
        size<=head-begin && head-begin<=capacity;
}
inline Counter ReleasePoint(Counter head,Counter pin) {return std::min(head,pin);}
inline void Publish(std::atomic<Counter>& released,Counter value) {
    auto previous=released.load(std::memory_order_acquire);
    while(previous<value && !released.compare_exchange_weak(previous,value,
        std::memory_order_release,std::memory_order_relaxed)) {}
}
// Preparation can precede debug/automatic commits. Those commits must not
// release prepared bytes before the later encoder actually binds them.
class Pin {
public:
    explicit Pin(Counter& value):value(value),previous(value) {}
    ~Pin() {value=previous;}
    Pin(const Pin&)=delete;
    Pin& operator=(const Pin&)=delete;
private:
    Counter& value;
    Counter previous;
};
}
