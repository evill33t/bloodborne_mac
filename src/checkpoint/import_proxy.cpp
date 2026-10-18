// SPDX-License-Identifier: GPL-2.0-or-later
#include "import_proxy.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <exception>
#include <vector>

namespace Bb::Checkpoint {
static_assert(offsetof(ImportProxy,target)==0 && offsetof(ImportProxy,requested)==48 && offsetof(ImportProxy,leaf)==56);
static_assert(sizeof(std::atomic<bool>)==1 && std::atomic<bool>::is_always_lock_free);
namespace {
thread_local std::size_t native_callbacks=0;
struct Frame {
    const ImportProxy* import;
    std::uint64_t return_pc,return_sp;
    BbCheckpointCpu entry;
    bool replay=false;
};
thread_local std::vector<Frame> frames;
thread_local bool observing=false;
std::atomic<bool> healthy{true};
void Canonicalize(BbCheckpointCpu& cpu) {
    cpu.features=1;std::fill(std::begin(cpu.reserved),std::end(cpu.reserved),0);
    std::memset(cpu.padding,0,sizeof(cpu.padding));
    cpu.fx[5]=0;
    for(unsigned k=0;k<8;++k)std::memset(cpu.fx+32+k*16+10,0,6);
    std::memset(cpu.fx+416,0,96);
}
void Notify(BoundaryKind kind,const ImportProxy& import,const BbCheckpointCpu& cpu) noexcept {
    if(!import.observe || observing || native_callbacks || !healthy.load(std::memory_order_relaxed))return;
    observing=true;
    try {import.observe(import.context,kind,import,cpu);}
    catch(...) {healthy.store(false,std::memory_order_relaxed);}
    observing=false;
}
}
extern "C" void bb_checkpoint_proxy_entry();
extern "C" void bb_checkpoint_proxy_return();
extern "C" void bb_checkpoint_proxy_enter(BbCheckpointCpu* cpu,const ImportProxy* import) noexcept {
    const auto original_pc=cpu->rip,post_sp=cpu->gpr[BB_CK_RSP];
    Canonicalize(*cpu);
    // The before-import continuation enters this freshly rebound proxy with
    // the original guest return address still on its stack.
    cpu->rip=reinterpret_cast<std::uintptr_t>(import->code);cpu->gpr[BB_CK_RSP]=post_sp-8;
    if(frames.empty() && (!import->guest_only || original_pc<(1ull<<40)))Notify(BoundaryKind::BeforeImport,*import,*cpu);
    cpu->rip=reinterpret_cast<std::uintptr_t>(import->target);
    if(!healthy.load(std::memory_order_relaxed))return; // keep the original return slot
    const auto target=cpu->rip;cpu->rip=reinterpret_cast<std::uintptr_t>(import->code);
    try {frames.push_back({import,original_pc,post_sp,*cpu,false});}
    catch(...) {cpu->rip=target;healthy.store(false,std::memory_order_relaxed);return;}
    cpu->rip=target;
    *reinterpret_cast<std::uint64_t*>(post_sp-8)=reinterpret_cast<std::uintptr_t>(&bb_checkpoint_proxy_return);
}
extern "C" void bb_checkpoint_proxy_leave(BbCheckpointCpu* cpu) noexcept {
    // A return stub is installed only after successfully publishing its frame.
    if(frames.empty())std::terminate();
    const auto frame=frames.back();
    if(cpu->gpr[BB_CK_RSP]!=frame.return_sp)std::terminate();
    Canonicalize(*cpu);cpu->rip=frame.return_pc;
    if(frame.replay) {
        *cpu=frame.entry;
        *reinterpret_cast<std::uint64_t*>(frame.return_sp-8)=frame.return_pc;
        frames.pop_back();return;
    }
    *reinterpret_cast<std::uint64_t*>(frame.return_sp-8)=frame.return_pc;
    if(frames.size()==1 && (!frame.import->guest_only || frame.return_pc<(1ull<<40)))Notify(BoundaryKind::AfterImport,*frame.import,*cpu);
    frames.pop_back();
}
bool EmitImportProxy(std::span<std::uint8_t> code,ImportProxy& import) {
    if(code.size()<32 || !import.target)return false;
    std::fill(code.begin(),code.begin()+32,0xcc);
    const std::uint8_t prefix[]{0x41,0x53,0x49,0xbb}; // push r11; movabs r11, descriptor
    std::memcpy(code.data(),prefix,sizeof(prefix));
    const auto descriptor=reinterpret_cast<std::uintptr_t>(&import);
    std::memcpy(code.data()+4,&descriptor,8);
    const std::uint8_t jump[]{0xff,0x25,0,0,0,0};
    std::memcpy(code.data()+12,jump,sizeof(jump));
    const auto entry=reinterpret_cast<std::uintptr_t>(&bb_checkpoint_proxy_entry);
    std::memcpy(code.data()+18,&entry,8);import.code=code.data();return true;
}
bool ImportProxiesHealthy() {return healthy.load(std::memory_order_relaxed);}
std::size_t ImportDepth() {return frames.size();}
bool ReplayCurrentImport() {
    if(!CanReplayCurrentImport())return false;
    frames.back().replay=true;return true;
}
bool CanReplayCurrentImport() {
    return healthy.load(std::memory_order_relaxed) && !native_callbacks && frames.size()==1 && frames.back().import->replayable &&
        (!frames.back().import->guest_only || frames.back().return_pc<(1ull<<40));
}
void AbandonCurrentImports() {frames.clear();observing=false;native_callbacks=0;}
bool CurrentImportEntry(BbCheckpointCpu& cpu) {
    if(frames.empty())return false;cpu=frames.back().entry;cpu.rip=frames.back().return_pc;return true;
}
void EnterNativeGuestCallback() {++native_callbacks;}
void LeaveNativeGuestCallback() {if(!native_callbacks)std::terminate();--native_callbacks;}

#define SYM(n) "_" n
#define SAVE_YMM(n,off) "vextractf128 $1,%ymm" #n "," #off "(%rsp)\n"
#define SAVE_REGISTERS \
    "mov %rax,0(%rsp)\nmov %rbx,8(%rsp)\nmov %rcx,16(%rsp)\nmov %rdx,24(%rsp)\n" \
    "mov %rsi,32(%rsp)\nmov %rdi,40(%rsp)\nmov %rbp,48(%rsp)\n" \
    "mov %r8,64(%rsp)\nmov %r9,72(%rsp)\nmov %r10,80(%rsp)\nmov %r11,88(%rsp)\n" \
    "mov %r12,96(%rsp)\nmov %r13,104(%rsp)\nmov %r14,112(%rsp)\nmov %r15,120(%rsp)\n" \
    "pushfq\npopq 136(%rsp)\nfxsave64 160(%rsp)\n" \
    SAVE_YMM(0,672) SAVE_YMM(1,688) SAVE_YMM(2,704) SAVE_YMM(3,720) \
    SAVE_YMM(4,736) SAVE_YMM(5,752) SAVE_YMM(6,768) SAVE_YMM(7,784) \
    SAVE_YMM(8,800) SAVE_YMM(9,816) SAVE_YMM(10,832) SAVE_YMM(11,848) \
    SAVE_YMM(12,864) SAVE_YMM(13,880) SAVE_YMM(14,896) SAVE_YMM(15,912)
__asm__(".text\n.p2align 4\n.globl " SYM("bb_checkpoint_proxy_entry") "\n"
        SYM("bb_checkpoint_proxy_entry") ":\npush %rax\npushfq\n"
        "cmpb $0,56(%r11)\nje 9f\nmov 48(%r11),%rax\ntest %rax,%rax\nje 9f\n"
        "cmpb $0,(%rax)\njne 9f\nmov 0(%r11),%rax\npopfq\n"
        // Native tail entry with the original RAX, R11, flags, argument
        // registers and stack arguments intact. The temporary target lives
        // below the real return slot and ret $8 discards the saved R11 slot.
        "xchg %rax,(%rsp)\nmov 8(%rsp),%r11\nret $8\n"
        "9: popfq\npop %rax\nlea -960(%rsp),%rsp\n"
        SAVE_REGISTERS
        "mov 88(%rsp),%rsi\nmov 960(%rsp),%rax\nmov %rax,88(%rsp)\n"
        "mov 968(%rsp),%rax\nmov %rax,128(%rsp)\nlea 976(%rsp),%rax\nmov %rax,56(%rsp)\n"
        "fninit\nmovl $0x1f80,148(%rsp)\nldmxcsr 148(%rsp)\n"
        "mov %rsp,%rdi\ncall " SYM("bb_checkpoint_proxy_enter") "\n"
        "mov %rsp,%rdi\njmp " SYM("bb_checkpoint_cpu_restore") "\n"
        ".p2align 4\n.globl " SYM("bb_checkpoint_proxy_return") "\n"
        // Keep the consumed return-address slot outside the CPU record. Replay
        // restores that slot before reentering the import, and CPU padding must
        // remain zero for a portable persisted context.
        SYM("bb_checkpoint_proxy_return") ":\nlea -976(%rsp),%rsp\n"
        SAVE_REGISTERS
        "lea 976(%rsp),%rax\nmov %rax,56(%rsp)\nmov %rsp,%rdi\n"
        "fninit\nmovl $0x1f80,148(%rsp)\nldmxcsr 148(%rsp)\n"
        "call " SYM("bb_checkpoint_proxy_leave") "\nmov %rsp,%rdi\njmp " SYM("bb_checkpoint_cpu_restore") "\n");
}
