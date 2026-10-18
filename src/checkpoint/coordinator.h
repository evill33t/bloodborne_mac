// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cpu_x86_64.h"
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace Bb::Checkpoint {
// A cooperative rendezvous. Participants reach a boundary with a guest-only
// continuation and without holding runtime-internal native locks. HLE waiters
// need an explicit interrupt/export path before joining this protocol.
class Coordinator {
public:
    struct Thread {std::uint64_t id;BbCheckpointCpu cpu;};
    class Frozen {
    public:
        Frozen(const Frozen&)=delete;
        Frozen& operator=(const Frozen&)=delete;
        Frozen(Frozen&&) noexcept;
        Frozen& operator=(Frozen&&) noexcept;
        ~Frozen();
        const std::vector<Thread>& Threads() const {return threads;}
        void Resume();
    private:
        friend class Coordinator;
        Frozen(Coordinator* c,std::uint64_t generation,std::vector<Thread> t);
        Coordinator* coordinator=nullptr;
        std::uint64_t generation=0;
        std::vector<Thread> threads;
    };
    ~Coordinator(); // Must outlive all participants and Frozen tokens.
    bool Register(std::uint64_t id);
    bool Unregister(std::uint64_t id);
    bool Boundary(std::uint64_t id,const BbCheckpointCpu& cpu);
    std::optional<Frozen> Freeze(std::chrono::milliseconds timeout,std::string& error,
                                 const std::function<void()>& interrupt={});
    bool Requested() const;
    const std::atomic<bool>* RequestFlag() const {return &wanted;}
private:
    struct Participant {
        std::thread::id owner;
        std::uint64_t parked_generation=0;
        BbCheckpointCpu cpu{};
    };
    void Resume(std::uint64_t generation);
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::map<std::uint64_t,Participant> participants;
    std::uint64_t generation=0;
    bool requested=false;
    std::atomic<bool> wanted{false};
};
}
