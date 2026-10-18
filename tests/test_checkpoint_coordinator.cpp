// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/checkpoint/coordinator.h"
#include <array>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <future>
#include <thread>

int main() {
    using namespace Bb::Checkpoint;
    using namespace std::chrono_literals;
    Coordinator coordinator;
    std::string error;
    assert(!coordinator.Freeze(10ms,error));
    std::array<std::atomic<std::uint64_t>,4> counters{};
    std::atomic<unsigned> ready=0;
    std::atomic<bool> stop=false;
    std::array<std::thread,4> workers;
    for(unsigned k=0;k<workers.size();++k)workers[k]=std::thread([&,k] {
        assert(coordinator.Register(k+1));
        assert(!coordinator.Register(k+100)); // one identity per execution thread
        ++ready;
        while(!stop) {
            BbCheckpointCpu cpu{};cpu.features=1;cpu.rip=0x8001000100;cpu.rflags=0x202;
            cpu.gpr[BB_CK_RSP]=0x8002001000;cpu.gpr[BB_CK_RBX]=++counters[k];
            assert(coordinator.Boundary(k+1,cpu));
            std::this_thread::yield();
        }
        assert(coordinator.Unregister(k+1));
    });
    while(ready!=workers.size())std::this_thread::yield();
    assert(!coordinator.Boundary(1,{})); // a different thread cannot submit state
    for(unsigned n=0;n<20;++n) {
        auto frozen=coordinator.Freeze(1s,error);assert(frozen);
        assert(frozen->Threads().size()==4 && coordinator.Requested());
        assert(!coordinator.Register(99));
        assert(!coordinator.Freeze(10ms,error));
        for(const auto& thread:frozen->Threads())assert(counters[thread.id-1]==thread.cpu.gpr[BB_CK_RBX]);
        std::this_thread::sleep_for(1ms);
        for(const auto& thread:frozen->Threads())assert(counters[thread.id-1]==thread.cpu.gpr[BB_CK_RBX]);
        // Moving the token preserves the pause; destruction resumes all threads.
        auto moved=std::move(*frozen);frozen.reset();assert(coordinator.Requested());
    }
    assert(!coordinator.Requested());stop=true;
    for(auto& worker:workers)worker.join();
    // A thread that fails to reach a safe boundary must time out without leaving
    // any other participant parked or an operation active.
    std::promise<void> registered;
    std::atomic<bool> release=false;
    std::thread slow([&] {
        assert(coordinator.Register(17));registered.set_value();
        while(!release)std::this_thread::yield();
        assert(coordinator.Unregister(17));
    });
    registered.get_future().wait();assert(!coordinator.Freeze(10ms,error));
    assert(!coordinator.Requested());release=true;slow.join();
    puts("Checkpoint coordinator: consistent thread rendezvous, admission, ownership, RAII resume and timeout recovery passed");
}
