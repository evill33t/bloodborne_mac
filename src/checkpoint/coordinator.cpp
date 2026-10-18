// SPDX-License-Identifier: GPL-2.0-or-later
#include "coordinator.h"
#include <algorithm>
#include <cassert>
#include <utility>

namespace Bb::Checkpoint {
Coordinator::Frozen::Frozen(Coordinator* c,std::uint64_t g,std::vector<Thread> t)
    :coordinator(c),generation(g),threads(std::move(t)) {}
Coordinator::Frozen::Frozen(Frozen&& other) noexcept
    :coordinator(std::exchange(other.coordinator,nullptr)),generation(other.generation),threads(std::move(other.threads)) {}
Coordinator::Frozen& Coordinator::Frozen::operator=(Frozen&& other) noexcept {
    if(this!=&other) {
        Resume();coordinator=std::exchange(other.coordinator,nullptr);
        generation=other.generation;threads=std::move(other.threads);
    }
    return *this;
}
Coordinator::Frozen::~Frozen() {Resume();}
void Coordinator::Frozen::Resume() {
    if(auto* c=std::exchange(coordinator,nullptr))c->Resume(generation);
}
Coordinator::~Coordinator() {
    std::lock_guard lock(mutex);
    assert(participants.empty() && !requested);
}
bool Coordinator::Register(std::uint64_t id) {
    std::lock_guard lock(mutex);
    if(!id || requested)return false;
    for(const auto& [key,p]:participants) {
        (void)key;
        if(p.owner==std::this_thread::get_id())return false;
    }
    return participants.emplace(id,Participant{std::this_thread::get_id(),0,{}}).second;
}
bool Coordinator::Unregister(std::uint64_t id) {
    std::lock_guard lock(mutex);
    const auto found=participants.find(id);
    if(found==participants.end() || found->second.owner!=std::this_thread::get_id() ||
       found->second.parked_generation)return false;
    participants.erase(found);changed.notify_all();return true;
}
bool Coordinator::Boundary(std::uint64_t id,const BbCheckpointCpu& cpu) {
    std::unique_lock lock(mutex);
    const auto found=participants.find(id);
    if(found==participants.end() || found->second.owner!=std::this_thread::get_id())return false;
    if(!requested)return true;
    if(!bb_checkpoint_cpu_valid(&cpu))return false;
    auto& participant=found->second;
    const auto arrived=generation;
    participant.cpu=cpu;participant.parked_generation=arrived;
    changed.notify_all();
    changed.wait(lock,[&]{return !requested || generation!=arrived;});
    participant.parked_generation=0;changed.notify_all();return true;
}
std::optional<Coordinator::Frozen> Coordinator::Freeze(std::chrono::milliseconds timeout,std::string& error,
                                                     const std::function<void()>& interrupt) {
    std::unique_lock lock(mutex);
    if(requested){error="Another checkpoint operation is active.";return std::nullopt;}
    if(timeout<=std::chrono::milliseconds::zero() || participants.empty()) {
        error="No guest threads or invalid checkpoint timeout.";return std::nullopt;
    }
    for(const auto& [id,p]:participants) {
        (void)id;
        if(p.owner==std::this_thread::get_id()) {
            error="A guest participant cannot coordinate its own checkpoint.";return std::nullopt;
        }
    }
    ++generation;requested=true;wanted.store(true,std::memory_order_release);changed.notify_all();
    try {if(interrupt){lock.unlock();interrupt();lock.lock();}}
    catch(...) {if(!lock.owns_lock())lock.lock();requested=false;wanted.store(false,std::memory_order_release);changed.notify_all();throw;}
    const auto ready=[&] {
        return std::all_of(participants.begin(),participants.end(),[&](const auto& p) {
            return p.second.parked_generation==generation;
        });
    };
    if(!changed.wait_for(lock,timeout,ready) || participants.empty()) {
        requested=false;wanted.store(false,std::memory_order_release);changed.notify_all();
        error="Guest threads did not all reach checkpoint boundaries before the timeout.";return std::nullopt;
    }
    try {
        std::vector<Thread> threads;threads.reserve(participants.size());
        for(const auto& [id,p]:participants)threads.push_back({id,p.cpu});
        error.clear();return Frozen(this,generation,std::move(threads));
    } catch(...) {
        requested=false;wanted.store(false,std::memory_order_release);changed.notify_all();throw;
    }
}
void Coordinator::Resume(std::uint64_t g) {
    std::lock_guard lock(mutex);
    if(g==generation){requested=false;wanted.store(false,std::memory_order_release);changed.notify_all();}
}
bool Coordinator::Requested() const {return wanted.load(std::memory_order_acquire);}
}
