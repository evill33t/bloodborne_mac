// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/liverpool/command_processor.h"
#include "gpu/renderer.h"
#include "gpu/metal/guest_label.h"
extern "C" {
#include "src/runtime.h"
}
#include <cassert>
#include <future>
#include <cstdio>
#include <cstring>

extern "C" void runtime_restart() {std::abort();}
using namespace std::chrono_literals;
using namespace Gpu::Liverpool;
struct FakeRenderer final : Gpu::Renderer {
    std::vector<std::function<void()>> completed;
    std::uint32_t* arguments=nullptr;
    unsigned draws=0,drains=0;
    Gpu::DrawCall last{};
    void Draw(const RegFile&,const Gpu::DrawCall& call) override {++draws;last=call;}
    void Dispatch(const RegFile&,std::uint32_t,std::uint32_t,std::uint32_t) override {
        completed.push_back([this] {arguments[0]=17;arguments[1]=2;arguments[2]=3;arguments[3]=0;});
    }
    void OnSubmit() override {}
    void WaitIdle() override {std::string error;WaitIdleUntil(std::chrono::steady_clock::now()+1s,error);}
    bool WaitIdleUntil(std::chrono::steady_clock::time_point,std::string&) override {
        ++drains;
        for(auto& fn:completed) fn();completed.clear();Execution()->Notify();return !Execution()->Failed();
    }
    void Signal(std::function<void()> fn) override {completed.push_back(std::move(fn));}
    void NoteConstDump(std::uint64_t,std::uint32_t) override {}
    void Flip(const Gpu::FlipInfo&) override {}
    std::uint8_t* Gds() override {return nullptr;}
};
int main() {
    std::uint64_t physical=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),32768,16384,0,&physical));
    std::uintptr_t address=0;
    assert(!runtime_gpu_memory_map(&address,32768,3,0,physical,16384));
    Gpu::Metal::GuestLabel label;assert(label.Capture(address,8));
    assert(label.Store(0x12345678abcdef01ull));std::uint64_t value=0;
    assert(label.Load(value) && value==0x12345678abcdef01ull);
    assert(!Gpu::Metal::GuestLabel{}.Capture(address+1,8));
    auto unmap=reinterpret_cast<int(*)(void*,std::uint64_t)>(runtime_resolve("cQke9UuBQOk",0));assert(unmap);
    assert(!unmap(reinterpret_cast<void*>(address),16384));
    auto same=address;assert(!runtime_gpu_memory_map(&same,16384,3,0x10,physical,16384) && same==address);
    *reinterpret_cast<std::uint64_t*>(address)=99;
    assert(!label.Store(7) && !label.Load(value)); // stale callback cannot corrupt recycled memory
    assert(*reinterpret_cast<std::uint64_t*>(address)==99);
    assert(label.Capture(address,8) && label.Store(0));

    // Lost wakeup protection and sticky first-error semantics.
    Gpu::ExecutionState state;auto epoch=state.Epoch();state.Notify();
    auto changed=std::async(std::launch::async,[&] {state.Wait(epoch,std::chrono::steady_clock::now()+1s);});
    assert(changed.wait_for(50ms)==std::future_status::ready);changed.get();
    epoch=state.Epoch();
    auto failed=std::async(std::launch::async,[&] {state.Wait(epoch,std::chrono::steady_clock::now()+1s);});
    assert(state.Fail("first") && !state.Fail("second"));
    assert(failed.wait_for(50ms)==std::future_status::ready);failed.get();assert(state.Error()=="first");

    {
        FakeRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
        auto waiter=std::async(std::launch::async,[&] {return cp.WaitLabel(address,42,8,3,~0ull,std::chrono::steady_clock::now()+1s);});
        assert(waiter.wait_for(5ms)==std::future_status::timeout);
        assert(label.Store(42));renderer.Execution()->Notify();
        assert(waiter.wait_for(100ms)==std::future_status::ready && waiter.get());
        assert(!cp.WaitLabel(address,43,8,3,~0ull,std::chrono::steady_clock::now()+2ms));
        assert(!renderer.Execution()->Failed());
        auto fault_waiter=std::async(std::launch::async,[&] {return cp.WaitLabel(address,43,8,3,~0ull,std::chrono::steady_clock::now()+1s);});
        renderer.Execution()->Fail("injected command error");
        assert(fault_waiter.wait_for(100ms)==std::future_status::ready && !fault_waiter.get());
    }
    {
        FakeRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
        renderer.arguments=reinterpret_cast<std::uint32_t*>(address+128);
        std::memset(renderer.arguments,0,16);
        const auto base=address+128;
        const std::uint32_t commands[]{Type3(Op::SetBase,3),1,std::uint32_t(base),std::uint32_t(base>>32),
            Type3(Op::DispatchDirect,4,true),1,1,1,1,
            Type3(Op::DrawIndirect,4),0,0,0,0};
        cp.SubmitGraphics({{{commands,std::size(commands)},{}}});cp.WaitIdle();
        assert(renderer.drains==1 && renderer.draws==1 && renderer.last.count==17 && renderer.last.instances==2);
    }
    {
        // Error aborts subsequent packets and never invents a completed label.
        FakeRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
        const std::uint32_t invalid[]{Type3(Op::EventWriteEop,5),0,0,1u<<29,123,0,Type3(Op::DrawIndexAuto,2),3,0};
        cp.SubmitGraphics({{{invalid,std::size(invalid)},{}}});cp.WaitIdle();
        assert(renderer.Execution()->Failed() && renderer.draws==0);
    }
    puts("GPU failure, label lifetime, parked wait and indirect producer checks passed");
}
