// SPDX-License-Identifier: GPL-3.0-or-later
// bbhost's parked word-wait implementation, using Darwin condition variables.
#include "core/futex.h"
#include <array>
namespace {
struct Bucket { std::mutex mutex; std::condition_variable condition; };
std::array<Bucket,64> buckets;
Bucket& bucket(std::atomic<std::uint32_t>* word) { return buckets[(reinterpret_cast<std::uintptr_t>(word)>>2)%buckets.size()]; }
std::atomic<std::uint64_t> sleeps{},timeouts{},wakes{};
}
unsigned host_futex_spin() { return 0; }
bool host_futex_native() { return false; }
HostFutexCounts host_futex_counts() { return {sleeps.load(),timeouts.load(),wakes.load()}; }
bool host_futex_wait(std::atomic<std::uint32_t>* word,std::uint32_t expected,const HostDeadline* deadline) {
    auto& b=bucket(word); std::unique_lock lock(b.mutex);
    if (word->load(std::memory_order_acquire)!=expected) return true;
    ++sleeps;
    if (!deadline) { b.condition.wait(lock); return true; }
    if (b.condition.wait_until(lock,*deadline)==std::cv_status::timeout) { ++timeouts; return false; }
    return true;
}
void host_futex_wake_all(std::atomic<std::uint32_t>* word) {
    auto& b=bucket(word); std::scoped_lock lock(b.mutex); ++wakes; b.condition.notify_all();
}
void host_futex_wake_one(std::atomic<std::uint32_t>* word) { host_futex_wake_all(word); }
void HostLock::lock_slow(std::uint32_t c) {
    if (c!=2) c=w_.exchange(2,std::memory_order_acquire);
    while (c!=0) { host_futex_wait(&w_,2,nullptr); c=w_.exchange(2,std::memory_order_acquire); }
}
