// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace Gpu {
// Shared by recording, GPU completion handlers and CPU waiters. Failure is sticky.
class ExecutionState {
public:
    using Clock = std::chrono::steady_clock;
    bool Failed() const { return failed.load(std::memory_order_acquire); }
    std::string Error() const { std::lock_guard guard(lock); return error; }
    bool Fail(std::string message) {
        {
            std::lock_guard guard(lock);
            if (failed.load(std::memory_order_relaxed)) return false;
            error = std::move(message);
            failed.store(true,std::memory_order_release);
            ++epoch;
        }
        changed.notify_all();return true;
    }
    std::uint64_t Epoch() const { std::lock_guard guard(lock); return epoch; }
    void Notify() {
        { std::lock_guard guard(lock); ++epoch; }
        changed.notify_all();
    }
    // Capture Epoch BEFORE reading the memory predicate, avoiding lost wakeups.
    void Wait(std::uint64_t observed, Clock::time_point deadline) {
        std::unique_lock guard(lock);
        changed.wait_until(guard,deadline,[&] { return epoch!=observed || Failed(); });
    }
private:
    mutable std::mutex lock;
    std::condition_variable changed;
    std::uint64_t epoch=0;
    std::atomic<bool> failed{false};
    std::string error;
};
}
