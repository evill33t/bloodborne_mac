// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/frame_handoff.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <thread>
#include <cstdio>

int main() {
    using namespace std::chrono_literals;
    Gpu::Metal::FrameHandoff handoff;
    int first=1,second=2;
    std::promise<void> published;
    std::atomic<bool> returned=false;
    auto producer=std::async(std::launch::async,[&] {
        handoff.PublishAndWait(&first,[&] { published.set_value(); });
        returned=true;
        handoff.PublishAndWait(&second,[] {});
    });
    published.get_future().wait();
    assert(handoff.Pending()==&first);
    assert(producer.wait_for(10ms)==std::future_status::timeout && !returned);
    // Reading a pending frame does not release its storage to the producer.
    assert(handoff.Pending()==&first);
    handoff.Acknowledge();
    auto deadline=std::chrono::steady_clock::now()+2s;
    while (handoff.Pending()!=&second && std::chrono::steady_clock::now()<deadline) std::this_thread::yield();
    assert(returned && handoff.Pending()==&second);
    handoff.Acknowledge();
    assert(producer.wait_for(2s)==std::future_status::ready);
    producer.get();
    assert(!handoff.Pending());
    // A fast consumer may acknowledge during wake, before producer begins wait.
    handoff.PublishAndWait(&first,[&] { assert(handoff.Pending()==&first);handoff.Acknowledge(); });
    assert(!handoff.Pending());
    puts("Frame handoff: ordered delivery, producer backpressure and early acknowledgement passed");
}
