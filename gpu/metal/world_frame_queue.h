// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gpu/metal/gpu_execution.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace Gpu::Metal {

// Producer-owned frame-boundary admission. GPU callbacks retain only their
// independent ticket and ExecutionState, never this queue or renderer state.
// Call Track before committing the final world command, then wait AFTER the
// frame-ready callback has committed presentation and returned. With limit2,
// fewer than two unfinished worlds remain before recording the next world.
// The display/presentation callback never waits on this policy.
class WorldFrameQueue {
public:
    using Ticket=std::shared_ptr<std::atomic<bool>>;
    using Clock=std::chrono::steady_clock;
    enum class Result {Ready,Failed,TimedOut};
    static constexpr unsigned Capacity=2;

    // The experiment is deliberately narrow. All other values preserve the
    // existing queue behavior; callers skip Track and Wait when disabled.
    static unsigned ConfiguredLimit() {
        const char* value=std::getenv("BB_METAL_WORLD_FRAME_LIMIT");
        return value && !std::strcmp(value,"2") ? 2u : 0u;
    }

    // Null is a caller-admission error: the previous boundary did not wait.
    // Never grow memory or silently discard an unfinished frame's ticket.
    Ticket Track() {
        Prune();
        for(auto& slot:tickets) if(!slot) {
            slot=std::make_shared<std::atomic<bool>>(false);
            const auto count=Outstanding();
            if(count>peak)peak=count;
            return slot;
        }
        return {};
    }

    std::size_t Outstanding() {
        Prune();
        std::size_t count=0;
        for(const auto& ticket:tickets)count+=bool(ticket);
        return count;
    }
    std::size_t PeakOutstanding() const {return peak;}
    std::size_t WaitCount() const {return waits;}

    static void Complete(const Ticket& ticket,const std::shared_ptr<ExecutionState>& execution,
                         bool successful=true) {
        if(!ticket)return;
        if(execution && !successful)execution->Fail("Metal world frame failed before completion.");
        ticket->store(true,std::memory_order_release);
        if(execution)execution->Notify();
    }

    Result WaitUntilOutstandingBelow(unsigned limit,const std::shared_ptr<ExecutionState>& execution,
                                     Clock::time_point deadline) {
        if(!execution || (limit && limit!=Capacity))return Result::Failed;
        bool counted=false;
        for(;;) {
            // Capture before checking the atomics, as required by
            // ExecutionState::Wait, so completion cannot lose its wakeup.
            const auto epoch=execution->Epoch();
            if(execution->Failed())return Result::Failed;
            if(!limit || Outstanding()<limit)
                return execution->Failed() ? Result::Failed : Result::Ready;
            if(Clock::now()>=deadline)return Result::TimedOut;
            if(!counted){++waits;counted=true;}
            execution->Wait(epoch,deadline);
        }
    }

private:
    std::array<Ticket,Capacity> tickets{};
    std::size_t peak=0,waits=0;
    void Prune() {
        for(auto& ticket:tickets)
            if(ticket && ticket->load(std::memory_order_acquire))ticket.reset();
    }
};

} // namespace Gpu::Metal
