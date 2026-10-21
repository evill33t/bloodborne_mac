// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Optional actual-renderer GPU fixture support; never linked into game code.
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace RootLeaseFixture {
using Renderer=Gpu::Metal::MetalRenderer;
using Device=Gpu::Metal::Device;
using Clock=std::chrono::steady_clock;
inline void Require(bool value,const std::string& text) {
    if(!value)throw std::runtime_error("root lease fixture: "+text);
}
inline void Drain(Renderer& renderer) {
    std::string error;
    Require(renderer.WaitIdleUntil(Clock::now()+std::chrono::seconds(15),error),"drain: "+error);
}
inline std::uint32_t Word(Gpu::GuestAddr address) {
    std::uint32_t word=0;std::memcpy(&word,reinterpret_cast<const void*>(address),4);return word;
}
inline void Completed(id<MTLCommandBuffer> command,const char* label) {
    Require(command!=nil,std::string(label)+" command missing");
    const auto deadline=Clock::now()+std::chrono::seconds(10);
    while(command.status!=MTLCommandBufferStatusCompleted) {
        Require(command.status!=MTLCommandBufferStatusError,std::string(label)+" command failed");
        Require(Clock::now()<deadline,std::string(label)+" completion timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(!command.error,std::string(label)+" command error");
}
struct QueueGate {
    id<MTLSharedEvent> event;
    std::atomic<bool> expired=false;
    std::jthread watchdog;
    explicit QueueGate(Device& device) {
        auto metal=(__bridge id<MTLDevice>)device.Handle();
        auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
        event=[metal newSharedEvent];Require(event!=nil,"event allocation");
        auto blocked=[queue commandBuffer];Require(blocked!=nil,"blocked command allocation");
        [blocked encodeWaitForEvent:event value:1];
        watchdog=std::jthread([this](std::stop_token stop) {
            for(unsigned k=0;k<500 && !stop.stop_requested();++k)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(!stop.stop_requested()){expired.store(true,std::memory_order_release);event.signaledValue=1;}
        });
        // Thread creation can fail: never leave a committed queue wait without
        // its bounded release mechanism already running.
        [blocked commit];
    }
    void Open(){event.signaledValue=1;}
    ~QueueGate(){Open();watchdog.request_stop();if(watchdog.joinable())watchdog.join();}
};
// Callers must prewarm BOTH target pipelines/targets and leave the renderer
// idle, so compilation/resource preparation cannot introduce an unrelated cut.
// recordA must issue exactly one ordinary draw to a fixed target;
// recordB must issue one nonzero eligible draw to a DIFFERENT target and assert
// positive root admission. This helper adds GPU work only in the test process.
template<class RecordA,class RecordB,class Mutate>
void HoldConsumer(Device& device,Renderer& renderer,Gpu::GuestAddr probe,
                  RecordA&& recordA,RecordB&& recordB,Mutate&& mutate,const char* label) {
    Drain(renderer);
    const std::uint32_t zero=0,magic=0x5a5a5a5au,producer_magic=0xa5a5a5a5u;
    const std::uint32_t markers[2]{};renderer.CpuWrite(probe,markers,sizeof(markers),false);
    recordA();
    Require(renderer.DmaData(0,probe+4,2,producer_magic,4),"producer A GPU marker encoding");
    // Empty no-program calls advance the existing batch-work counter without
    // encoding more GPU reads or exhausting the512-entry source-read registry.
    for(unsigned k=0;k<400;++k)renderer.Draw(Gpu::Liverpool::RegFile{},Gpu::DrawCall{});
    // Draw increments work_since_commit; the target change's existing
    // BeginPass sees >=400, commits producer A and creates consumer B.
    recordB();
    Require(renderer.DmaData(0,probe,2,magic,4),"consumer B GPU marker encoding");
    auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    auto completed=std::make_shared<std::atomic<bool>>(false);
    auto marker=[queue commandBuffer];Require(marker!=nil,"producer marker allocation");
    [marker addCompletedHandler:^(id<MTLCommandBuffer>){completed->store(true,std::memory_order_release);}];
    [marker commit];Completed(marker,"producer A queue marker");
    const auto callbacks_deadline=Clock::now()+std::chrono::seconds(5);
    while(!completed->load(std::memory_order_acquire)) {
        Require(Clock::now()<callbacks_deadline,"producer marker completion callback timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    // Marker completion alone could also pass if A were never submitted.
    // Its disjoint GPU-written word proves A really executed; B stays pending.
    Require(Word(probe+4)==producer_magic,"natural BeginPass did not submit producer A");
    Require(Word(probe)==zero,"consumer B unexpectedly submitted before mutation");
    const auto waits=renderer.IndirectCpuWriteWaitCount();
    QueueGate gate(device);
    std::atomic<bool> started=false,done=false;std::exception_ptr failure;
    std::jthread worker([&] {
        started.store(true,std::memory_order_release);
        try {mutate();}catch(...){failure=std::current_exception();}
        done.store(true,std::memory_order_release);
    });
    const auto started_deadline=Clock::now()+std::chrono::seconds(1);
    while(!started.load(std::memory_order_acquire) && Clock::now()<started_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const bool began=started.load(std::memory_order_acquire);
    const bool held=!done.load(std::memory_order_acquire) && Word(probe)==zero;
    gate.Open();worker.join();gate.watchdog.request_stop();gate.watchdog.join();
    if(failure)std::rethrow_exception(failure);
    Require(began && held,std::string(label)+" mutation escaped blocked consumer");
    Require(!gate.expired.load(std::memory_order_acquire),std::string(label)+" watchdog released consumer");
    Require(Word(probe)==magic,std::string(label)+" returned before actual consumer B marker");
    Require(renderer.IndirectCpuWriteWaitCount()==waits+1,std::string(label)+" must wait exactly once");
    Drain(renderer);
    std::printf("%s: A complete/B unsubmitted, blocked actual consumer, exactly one alias/mutation wait passed.\n",label);
}
} // namespace RootLeaseFixture
