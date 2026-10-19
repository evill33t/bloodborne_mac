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
#include <array>
#include <algorithm>
#include <fstream>
#include <CommonCrypto/CommonDigest.h>
#include <sys/mman.h>
#include "core/elf.h"
#include "engine/addr.h"

extern "C" void runtime_restart() {std::abort();}
extern "C" std::uint64_t runtime_process_time_us();
extern "C" void* runtime_low_map(std::size_t,int);
namespace MacFeatures {void InstallGpuFenceWaits(ElfImage*);}
extern "C" std::uint64_t test_flush_stub(void*,std::uint64_t,std::uint64_t);
extern "C" std::uint64_t test_counter_stub(void*,void*,std::uint64_t,std::uint64_t,std::uint64_t);
asm(R"(
.text
.globl _test_flush_stub
_test_flush_stub:
 push %rbp
 mov %rsp,%rbp
 push %r14
 push %r15
 mov %rdi,%rax
 mov %rsi,%r14
 mov %rdx,%r15
 call *%rax
 pop %r15
 pop %r14
 pop %rbp
 ret
.globl _test_counter_stub
_test_counter_stub:
 push %rbp
 mov %rsp,%rbp
 push %rbx
 push %r15
 sub $64,%rsp
 mov %rdi,%rax
 mov %rsi,%rbx
 mov %rdx,-0x38(%rbp)
 mov %rcx,%r15
 mov %r8,-0x40(%rbp)
 call *%rax
 add $64,%rsp
 pop %r15
 pop %rbx
 pop %rbp
 ret
)");
template<class T>T Read(const std::vector<unsigned char>& bytes,std::size_t at) {
    assert(at<=bytes.size() && sizeof(T)<=bytes.size()-at);T value;std::memcpy(&value,bytes.data()+at,sizeof(T));return value;
}
void VerifyOriginalHooks(const char* path,std::uintptr_t label) {
    std::ifstream input(path,std::ios::binary);assert(input);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes.data(),CC_LONG(bytes.size()),digest);
    std::string hash;for(unsigned char b:digest){hash+="0123456789abcdef"[b>>4];hash+="0123456789abcdef"[b&15];}
    assert(engine_eboot_supported(hash) && !std::memcmp(bytes.data(),"\x7f" "ELF",4));
    const auto ph=Read<std::uint64_t>(bytes,32);const auto count=Read<std::uint16_t>(bytes,56);
    std::size_t size=0;
    for(unsigned i=0;i<count;++i) if(Read<std::uint32_t>(bytes,ph+i*56)==1)
        size=std::max(size,std::size_t(Read<std::uint64_t>(bytes,ph+i*56+16)+Read<std::uint64_t>(bytes,ph+i*56+40)));
    auto* memory=static_cast<unsigned char*>(runtime_low_map(size,PROT_READ|PROT_WRITE|PROT_EXEC));assert(memory);
    for(unsigned i=0;i<count;++i) if(Read<std::uint32_t>(bytes,ph+i*56)==1) {
        auto off=Read<std::uint64_t>(bytes,ph+i*56+8),rva=Read<std::uint64_t>(bytes,ph+i*56+16),n=Read<std::uint64_t>(bytes,ph+i*56+32);
        assert(off<=bytes.size() && n<=bytes.size()-off);std::memcpy(memory+rva,bytes.data()+off,n);
    }
    ElfImage image;image.sha256="unsupported";image.mem={memory,size,reinterpret_cast<std::uint64_t>(memory)};
    const auto displacement=[&](std::size_t rva) {std::int32_t d;std::memcpy(&d,memory+rva+1,4);return d;};
    const auto flush=displacement(0x33c969),counter=displacement(0x3a18be);
    MacFeatures::InstallGpuFenceWaits(&image);assert(displacement(0x33c969)==flush);
    image.sha256=hash;memory[0x3a18b2]^=1;
    MacFeatures::InstallGpuFenceWaits(&image);assert(displacement(0x33c969)==flush && displacement(0x3a18be)==counter);
    memory[0x3a18b2]^=1;MacFeatures::InstallGpuFenceWaits(&image);
    assert(displacement(0x33c969)!=flush && displacement(0x3a18be)!=counter);
    auto* a=memory+0x33c969+5+displacement(0x33c969);
    auto* b=memory+0x3a18be + 5+displacement(0x3a18be);
    auto now=runtime_process_time_us();
    *reinterpret_cast<std::uint64_t*>(label)=1;
    assert(test_flush_stub(a,label,now)>=now);
    std::array<std::uint64_t,32> object{};object[0xd8/8]=label;
    now=runtime_process_time_us();assert(test_counter_stub(b,object.data(),1,0,now)>=now);
    // Pending labels park in bounded slices; neither trampoline completes them.
    *reinterpret_cast<std::uint64_t*>(label)=0;
    now=runtime_process_time_us();auto end=test_flush_stub(a,label,now);
    assert(end-now<50000 && *reinterpret_cast<std::uint64_t*>(label)==0);
    now=runtime_process_time_us();assert(test_counter_stub(b,object.data(),1,1,now)-now<50000);
    puts("Original 1.09 clock callsites: hash/byte refusal and both register/stack trampolines passed");
}
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
int main(int argc,char** argv) {
    std::uint64_t physical=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),32768,16384,0,&physical));
    std::uintptr_t address=0;
    assert(!runtime_gpu_memory_map(&address,32768,3,0,physical,16384));
    Gpu::Metal::GuestLabel label;assert(label.Capture(address,8));
    assert(label.Store(0x12345678abcdef01ull));std::uint64_t value=0;
    assert(label.Load(value) && value==0x12345678abcdef01ull);
    assert(!Gpu::Metal::GuestLabel{}.Capture(address+1,8));
    Gpu::Metal::GuestLabel other_half;assert(other_half.Capture(address+16384,8));
    auto unmap=reinterpret_cast<int(*)(void*,std::uint64_t)>(runtime_memory_resolve("cQke9UuBQOk#p#J"));assert(unmap);
    assert(!unmap(reinterpret_cast<void*>(address),16384));
    assert(other_half.Store(123) && other_half.Load(value) && value==123); // splitting retains live identities
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
    {
        FakeRenderer renderer;
        assert(label.Store(0));
        {
            CommandProcessor cp;cp.SetRenderer(&renderer);
            const std::uint32_t fence[]{Type3(Op::EventWriteEop,5),0,std::uint32_t(address),
                (std::uint32_t(address>>32)&0xffffu)|(2u<<29),77,0};
            cp.SubmitGraphics({{{fence,std::size(fence)},{}}});cp.WaitIdle();
            assert(label.Load(value) && value==0); // recording is not completion
            renderer.Execution()->Fail("injected Metal execution failure");
            renderer.WaitIdle();assert(label.Load(value) && value==0);
        }
    }
    {
        FakeRenderer renderer;
        {
            CommandProcessor cp;cp.SetRenderer(&renderer);
            const std::uint32_t fence[]{Type3(Op::EventWriteEop,5),0,std::uint32_t(address),
                (std::uint32_t(address>>32)&0xffffu)|(2u<<29),88,0};
            cp.SubmitGraphics({{{fence,std::size(fence)},{}}});cp.WaitIdle();
        }
        renderer.WaitIdle();assert(label.Load(value) && value==0); // callbacks cannot outlive a frontend
    }
    {
        FakeRenderer renderer;CommandProcessor cp;cp.SetRenderer(&renderer);
        assert(!cp.WaitLabel(address,42,8,3,~0ull,std::chrono::steady_clock::now()+1ms,true));
        assert(renderer.Execution()->Failed() && renderer.Execution()->Error().find("timed out")!=std::string::npos);
        const std::uint32_t draw[]{Type3(Op::DrawIndexAuto,2),3,0};
        cp.SubmitGraphics({{{draw,std::size(draw)},{}}});cp.WaitIdle();assert(!renderer.draws);
    }
    puts("GPU failure, label lifetime, parked wait and indirect producer checks passed");
    if(argc==3 && std::strcmp(argv[1],"--elf")==0) VerifyOriginalHooks(argv[2],address);
}
