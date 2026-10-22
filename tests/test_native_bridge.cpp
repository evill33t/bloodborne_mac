// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic guest bytes exercise the actual runtime and bbhost call bridges.
#include "cpu/bbcpu.h"
#include "core/thunk.h"
#include "core/elf.h"
#include "engine/graphics_patch.h"
#include "gpu/metal/bbhost/command_guards.h"
extern "C" {
#include "src/runtime.h"
}
#include <cassert>
#include <cstring>
#include <cstdlib>
#include <array>
#include <thread>
#include <sys/mman.h>
extern "C" void runtime_restart() { std::abort(); }

namespace {
unsigned char* code;
std::uint64_t nested(std::uint64_t a,std::uint64_t b) {
    const std::uint64_t args[]{a,b};
    return runtime_guest_call(code,2,args)+7;
}
std::int64_t rewrite(std::uint64_t id,const std::uint64_t* saved) {
    const_cast<std::uint64_t*>(saved)[5]+=id;return 0;
}
void yield_host() { std::this_thread::yield(); }
}
int main() {
    code=static_cast<unsigned char*>(runtime_low_map(16384,PROT_READ|PROT_WRITE));
    assert(code);
    // mov rax,rdi; add rax,rsi; ret
    const unsigned char add[]{0x48,0x89,0xf8,0x48,0x01,0xf0,0xc3};
    std::memcpy(code,add,sizeof(add));
    // Sum six integer registers and four stack arguments; add two doubles.
    const unsigned char frame[]{0x48,0x89,0xf8,0x48,0x01,0xf0,0x48,0x01,0xd0,
        0x48,0x01,0xc8,0x4c,0x01,0xc0,0x4c,0x01,0xc8,
        0x48,0x03,0x44,0x24,0x08,0x48,0x03,0x44,0x24,0x10,
        0x48,0x03,0x44,0x24,0x18,0x48,0x03,0x44,0x24,0x20,0xf2,0x0f,0x58,0xc1,0xc3};
    std::memcpy(code+64,frame,sizeof(frame));
    code[128]=0x48;code[129]=0xb8;
    const auto callback=reinterpret_cast<std::uintptr_t>(&nested);
    std::memcpy(code+130,&callback,8);code[138]=0xff;code[139]=0xe0;
    const unsigned char tls[]{0x64,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    std::memcpy(code+192,tls,sizeof(tls));
    const unsigned char prologue[]{0x55,0x48,0x89,0xe5,0x48,0x89,0xf8,
        0x48,0x01,0xf0,0x48,0x83,0xc0,0x01};
    std::memcpy(code+256,prologue,sizeof(prologue));code[270]=0x5d;code[271]=0xc3;
    auto* counter=static_cast<std::uint64_t*>(runtime_low_map(16384,PROT_READ|PROT_WRITE));
    assert(counter);*counter=0;
    const unsigned char increment[]{0x48,0xb8,0,0,0,0,0,0,0,0,0xf0,0x48,0xff,0x00,0xc3};
    std::memcpy(code+320,increment,sizeof(increment));
    const std::uint64_t counter_address=reinterpret_cast<std::uintptr_t>(counter);
    std::memcpy(code+322,&counter_address,8);
    // Read/yield/write creates lost updates without a per-buffer writer guard.
    const unsigned char concurrent[]{0x55,0x48,0x89,0xe5,0x48,0x83,0xec,0x10,
        0x48,0x89,0x7d,0xf0,0x48,0x8b,0x07,0x48,0x89,0x45,0xf8,
        0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x8b,0x7d,0xf0,
        0x48,0x8b,0x45,0xf8,0x48,0x83,0xc0,0x01,0x48,0x89,0x07,0xc9,0xc3};
    std::memcpy(code+384,concurrent,sizeof(concurrent));
    const auto yield=reinterpret_cast<std::uintptr_t>(&yield_host);
    std::memcpy(code+405,&yield,8);
    bbcpu_add_guest_code(reinterpret_cast<std::uintptr_t>(code),16384);
    MacCommandGuards::RegisterMethod(reinterpret_cast<std::uintptr_t>(code+384));
    ElfImage image;image.mem={code,16384,reinterpret_cast<std::uintptr_t>(code)};
    image.sha256="941f887a562aae054fac35af8cc8f27cf075f3d4cc2e029fb5ae2a663aaa5ae7";
    assert(engine_prologue_hook(&image,reinterpret_cast<std::uintptr_t>(code+256),prologue,
        sizeof(prologue),reinterpret_cast<void*>(&rewrite),42));
    runtime_thread_attach_main();
    const std::uint64_t args[]{17,25};
    assert(runtime_guest_call(code,2,args)==42);
    assert(hle_call_guest6(code,17,25,0,0,0,0)==42);
    assert(runtime_guest_call(code+128,2,args)==49);
    assert(runtime_guest_call(code+256,2,args)==85);
    assert(runtime_guest_call(code,2,args)==42); // nested state restored
    const auto tcb=runtime_guest_call(code+192,0,nullptr);
    assert(tcb && *reinterpret_cast<std::uint64_t*>(tcb)==tcb);
    using Create=int(*)(void**,void**,void*(*)(void*),void*,const char*);
    using Join=int(*)(void*,void**);
    auto create=reinterpret_cast<Create>(runtime_thread_resolve("6UgtwV+0zb4#p#J"));
    auto join=reinterpret_cast<Join>(runtime_thread_resolve("onNY9Byn-W8#p#J"));
    assert(create && join);void* thread=nullptr;void* thread_tcb=nullptr;
    assert(create(&thread,nullptr,reinterpret_cast<void*(*)(void*)>(code+192),nullptr,"native fixture")==0);
    assert(join(thread,&thread_tcb)==0 && thread_tcb && reinterpret_cast<std::uintptr_t>(thread_tcb)!=tcb);
    using Once=int(*)(std::int32_t*,void(*)());
    auto once=reinterpret_cast<Once>(runtime_kernel_resolve("14bOACANTBo#p#J"));
    assert(once);std::int32_t once_state=0;
    assert(once(&once_state,reinterpret_cast<void(*)()>(code+320))==0);
    assert(once(&once_state,reinterpret_cast<void(*)()>(code+320))==0 && *counter==1);
    std::array<std::thread,4> writers;
    for (auto& writer:writers) writer=std::thread([&] {
        for (unsigned i=0;i<100;++i) runtime_guest_call(code+384,1,&counter_address);
    });
    for (auto& writer:writers) writer.join();
    assert(*counter==401);
    GuestCallRegs regs{};regs.fn=code+64;
    for (unsigned i=0;i<6;++i) regs.ints[i]=i+1;
    for (unsigned i=0;i<4;++i) regs.stack[i]=i+7;
    const double a=1.5,b=2.25;
    std::memcpy(&regs.xmm[0],&a,8);std::memcpy(&regs.xmm[1],&b,8);
    hle_call_guest_regs(&regs);
    double result=0;std::memcpy(&result,&regs.ret_xmm0,8);
    assert(regs.ret==55 && result==3.75);
    bbcpu_report();
}
