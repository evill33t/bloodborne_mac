// SPDX-License-Identifier: GPL-2.0-or-later
// Production CP, private guest mappings and a deferred CPU renderer; no GPU.
#include "gpu/liverpool/command_processor.h"
#include "gpu/renderer.h"
#include "gpu/metal/guest_label.h"
#include "src/runtime_gpu_memory.h"
#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>

extern "C" void runtime_restart() {std::abort();}
namespace {
using namespace Gpu::Liverpool;
using namespace std::chrono_literals;
struct DeferredRenderer final : Gpu::Renderer {
    std::mutex lock;
    std::condition_variable changed;
    unsigned submits=0;
    std::vector<std::function<void()>> callbacks;
    void Draw(const RegFile&,const Gpu::DrawCall&) override {}
    void Dispatch(const RegFile&,std::uint32_t,std::uint32_t,std::uint32_t) override {}
    void OnSubmit() override {
        std::lock_guard guard(lock);++submits;changed.notify_all();
    }
    void WaitIdle() override {Drain();}
    void Signal(std::function<void()> done) override {
        std::lock_guard guard(lock);callbacks.push_back(std::move(done));
    }
    void NoteConstDump(std::uint64_t,std::uint32_t) override {}
    void Flip(const Gpu::FlipInfo&) override {}
    std::uint8_t* Gds() override {return nullptr;}
    bool WaitSubmits(unsigned count) {
        std::unique_lock guard(lock);return changed.wait_for(guard,5s,[&]{return submits>=count;});
    }
    std::size_t Pending() {std::lock_guard guard(lock);return callbacks.size();}
    void Drain() {
        std::vector<std::function<void()>> ready;
        {std::lock_guard guard(lock);ready.swap(callbacks);}
        for(auto& done:ready)done();
    }
};
std::vector<std::uint32_t> Marker(std::uint64_t address,std::uint32_t value,bool interrupt) {
    return {Type3(Op::Nop,4),interrupt ? 0x68750781u : 0x68750778u,
        std::uint32_t(address),std::uint32_t(address>>32),value};
}
void CrossRing(std::uintptr_t address,bool compute_marker) {
    DeferredRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
    Gpu::Metal::GuestLabel first,last,compute;
    assert(first.Capture(address,4) && last.Capture(address+8,4) && compute.Capture(address+16,4));
    assert(first.Store(0) && last.Store(0) && compute.Store(0));
    const auto earlier=Marker(address,11,false),final=Marker(address+8,22,true);
    auto* child=reinterpret_cast<std::uint32_t*>(address+4096);
    std::copy(final.begin(),final.end(),child);
    // A later span and nested graphics IB still belong to the SAME job. The
    // existing last-marker-wins rule must not be changed to per-span state.
    const std::uint32_t indirect[]{Type3(Op::IndirectBuffer,3),std::uint32_t(address+4096),
        std::uint32_t((address+4096)>>32),std::uint32_t(final.size())};
    std::promise<void> entered,release;
    auto entered_future=entered.get_future();auto proceed=release.get_future().share();
    std::atomic<unsigned> order{0},interrupts{0};
    cp.SetInterruptHandler([&](std::uint32_t irq) {
        std::uint64_t value=0;
        assert(irq==CommandProcessor::kIrqGfxEop && last.Load(value) && value==22);
        assert(order.fetch_add(1)==2);++interrupts;
    });
    cp.SubmitGraphics({{{earlier.data(),earlier.size()},{}},{{indirect,std::size(indirect)},{}}},[&] {
        // Original graphics markers have been decoded, but presentation and
        // its completion notification have not happened yet.
        entered.set_value();proceed.wait();
        assert(order.fetch_add(1)==0);
        renderer.SignalNoDirectMemoryWrite([&]{assert(order.fetch_add(1)==1);});
    });
    assert(entered_future.wait_for(5s)==std::future_status::ready);
    const auto other=compute_marker ? Marker(address+16,33,true) : std::vector<std::uint32_t>{Type3(Op::Nop,1),0};
    cp.SubmitCompute(1,other);
    // Graphics is gated before its after/flip; this submit can only be the
    // compute job. Observing OnSubmit proves its marker-consume point ran.
    assert(renderer.WaitSubmits(1));
    std::uint64_t value=0;
    assert(renderer.Pending()==0 && order==0 && interrupts==0);
    assert(last.Load(value) && value==0 && compute.Load(value) && value==0);
    release.set_value();cp.WaitIdle();
    assert(renderer.Pending()==2 && order==1 && interrupts==0);
    assert(first.Load(value) && value==0 && last.Load(value) && value==0);
    renderer.Drain();
    assert(order==3 && interrupts==1 && last.Load(value) && value==22);
    assert(first.Load(value) && value==0 && compute.Load(value) && value==0);
    // Retirement is not carried into the next graphics job.
    cp.SubmitGraphics({});cp.WaitIdle();renderer.Drain();
    assert(renderer.Pending()==0 && order==3 && interrupts==1 && !cp.Execution()->Failed());
}
void MarkerKinds(std::uintptr_t address) {
    DeferredRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
    Gpu::Metal::GuestLabel label;assert(label.Capture(address,4) && label.Store(0));
    std::atomic<unsigned> interrupts{0};cp.SetInterruptHandler([&](std::uint32_t irq) {
        assert(irq==CommandProcessor::kIrqGfxEop);++interrupts;
    });
    const auto label_only=Marker(address,55,false);
    cp.SubmitGraphics({{{label_only.data(),label_only.size()},{}}});cp.WaitIdle();
    std::uint64_t value=0;assert(label.Load(value) && value==0 && interrupts==0);
    renderer.Drain();assert(label.Load(value) && value==55 && interrupts==0);
    const std::uint32_t irq_only[]{Type3(Op::Nop,1),0x68750780};
    cp.SubmitGraphics({{{irq_only,std::size(irq_only)},{}}});cp.WaitIdle();
    assert(interrupts==0);renderer.Drain();assert(interrupts==1 && label.Load(value) && value==55);
    assert(!cp.Execution()->Failed());
}
}
int main() {
    std::uint64_t physical=0;std::uintptr_t address=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),16384,16384,0,&physical));
    assert(!runtime_gpu_memory_map(&address,16384,3,0,physical,16384));
    CrossRing(address,false);CrossRing(address,true);MarkerKinds(address);
    puts("PASS: graphics job owns flip retirement across spans/IBs; compute cannot retire or overwrite it; after, label and IRQ ordering preserved (CPU only)");
}
