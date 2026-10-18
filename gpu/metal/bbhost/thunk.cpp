// SPDX-License-Identifier: GPL-3.0-or-later
// Adapted from bbhost core/thunk.cpp; see vendor/bbhost/UPSTREAM.md.
#include "core/thunk.h"
#include "src/checkpoint/runtime.h"
#include <cstring>
std::uint8_t* thunk_emit_prologue_stub(std::uint8_t* c, std::uint64_t id, void* host, std::uint64_t resume,
                                      const std::uint8_t* displaced, std::size_t n, bool keep_rax) {
    // Below the six registers: a return slot (zeroed; what a skipped call
    // returns, saved[-1] to the host) and xmm0-7, which carry a hooked
    // function's float arguments and which host code would otherwise clobber.
    // 48 + 136 bytes over the entry's 8 keeps the host call 16-byte aligned.
    static constexpr std::uint8_t kSave[] = {
        0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51,                    // push rdi rsi rdx rcx r8 r9
        0x48, 0x81, 0xec, 0x88, 0x00, 0x00, 0x00,                          // sub rsp, 136
        0xf3, 0x0f, 0x7f, 0x44, 0x24, 0x00,                                // movdqu [rsp], xmm0
        0xf3, 0x0f, 0x7f, 0x4c, 0x24, 0x10,                                // movdqu [rsp+16], xmm1
        0xf3, 0x0f, 0x7f, 0x54, 0x24, 0x20,                                // movdqu [rsp+32], xmm2
        0xf3, 0x0f, 0x7f, 0x5c, 0x24, 0x30,                                // movdqu [rsp+48], xmm3
        0xf3, 0x0f, 0x7f, 0x64, 0x24, 0x40,                                // movdqu [rsp+64], xmm4
        0xf3, 0x0f, 0x7f, 0x6c, 0x24, 0x50,                                // movdqu [rsp+80], xmm5
        0xf3, 0x0f, 0x7f, 0x74, 0x24, 0x60,                                // movdqu [rsp+96], xmm6
        0xf3, 0x0f, 0x7f, 0x7c, 0x24, 0x70,                                // movdqu [rsp+112], xmm7
        0x48, 0xc7, 0x84, 0x24, 0x80, 0x00, 0x00, 0x00, 0, 0, 0, 0,        // mov qword [rsp+128], 0
        0x48, 0x8d, 0xb4, 0x24, 0x88, 0x00, 0x00, 0x00,                    // lea rsi, [rsp+136]
    };
    static constexpr std::uint8_t kRestore[] = {
        0x4c, 0x8b, 0x94, 0x24, 0x80, 0x00, 0x00, 0x00,                    // mov r10, [rsp+128]
        0xf3, 0x0f, 0x6f, 0x44, 0x24, 0x00,                                // movdqu xmm0, [rsp]
        0xf3, 0x0f, 0x6f, 0x4c, 0x24, 0x10,                                // movdqu xmm1, [rsp+16]
        0xf3, 0x0f, 0x6f, 0x54, 0x24, 0x20,                                // movdqu xmm2, [rsp+32]
        0xf3, 0x0f, 0x6f, 0x5c, 0x24, 0x30,                                // movdqu xmm3, [rsp+48]
        0xf3, 0x0f, 0x6f, 0x64, 0x24, 0x40,                                // movdqu xmm4, [rsp+64]
        0xf3, 0x0f, 0x6f, 0x6c, 0x24, 0x50,                                // movdqu xmm5, [rsp+80]
        0xf3, 0x0f, 0x6f, 0x74, 0x24, 0x60,                                // movdqu xmm6, [rsp+96]
        0xf3, 0x0f, 0x6f, 0x7c, 0x24, 0x70,                                // movdqu xmm7, [rsp+112]
        0x48, 0x81, 0xc4, 0x88, 0x00, 0x00, 0x00,                          // add rsp, 136
        0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f,                    // pop r9 r8 rcx rdx rsi rdi
    };
    // The same, with rax pushed where the alignment pad was: seven pushes are
    // 56 bytes, which aligns the call as the pad did.
    static constexpr std::uint8_t kSaveRax[] = {
        0x57, 0x56, 0x52, 0x51, 0x41, 0x50, 0x41, 0x51,  // push rdi rsi rdx rcx r8 r9
        0x50,                                            // push rax
        0x48, 0x8d, 0x74, 0x24, 0x08,                    // lea rsi, [rsp+8]
    };
    static constexpr std::uint8_t kRestoreRax[] = {
        0x58,                                            // pop rax (after mov r11, rax)
        0x41, 0x59, 0x41, 0x58, 0x59, 0x5a, 0x5e, 0x5f,  // pop r9 r8 rcx rdx rsi rdi
    };
    if (keep_rax) {
        std::memcpy(c, kSaveRax, sizeof(kSaveRax));
        c += sizeof(kSaveRax);
    } else {
        std::memcpy(c, kSave, sizeof(kSave));
        c += sizeof(kSave);
    }
    *c++ = 0x48;
    *c++ = 0xbf;  // movabs rdi, id
    std::memcpy(c, &id, 8);
    c += 8;
    const std::uint64_t fn = reinterpret_cast<std::uint64_t>(host);
    *c++ = 0x48;
    *c++ = 0xb8;  // movabs rax, host
    std::memcpy(c, &fn, 8);
    c += 8;
    *c++ = 0xff;
    *c++ = 0xd0;  // call rax
    static constexpr std::uint8_t kKeep[] = {0x49, 0x89, 0xc3};  // mov r11, rax (the pops leave r11 alone)
    std::memcpy(c, kKeep, sizeof(kKeep));
    c += sizeof(kKeep);
    if (keep_rax) {
        std::memcpy(c, kRestoreRax, sizeof(kRestoreRax));
        c += sizeof(kRestoreRax);
    } else {
        std::memcpy(c, kRestore, sizeof(kRestore));
        c += sizeof(kRestore);
    }
    static constexpr std::uint8_t kReturn[] = {
        0x4d, 0x85, 0xdb,  // test r11, r11
        0x74, 0x04,        // jz +4: run the method
        0x4c, 0x89, 0xd0,  // mov rax, r10: the return slot
        0xc3,              // ret to the method's caller
    };
    static constexpr std::uint8_t kReturnRax[] = {
        0x4d, 0x85, 0xdb,  // test r11, r11
        0x74, 0x03,        // jz +3: run the method
        0x31, 0xc0,        // xor eax, eax
        0xc3,              // ret to the method's caller
    };
    if (keep_rax) {
        std::memcpy(c, kReturnRax, sizeof(kReturnRax));
        c += sizeof(kReturnRax);
    } else {
        std::memcpy(c, kReturn, sizeof(kReturn));
        c += sizeof(kReturn);
    }
    if (n) std::memcpy(c, displaced, n);  // none for a redirected tail call
    c += n;
    const std::uint8_t jmp[6] = {0xff, 0x25, 0, 0, 0, 0};  // jmp [rip+0]
    std::memcpy(c, jmp, 6);
    c += 6;
    std::memcpy(c, &resume, 8);
    return c + 8;
}

void* thunk_wrap(void* p) { return p; }
void* hle_wrap_fn(void* p) { return p; }
namespace {
struct GuestCallbackScope {
    GuestCallbackScope() {bb_checkpoint_runtime_enter_callback();}
    ~GuestCallbackScope() {bb_checkpoint_runtime_leave_callback();}
};
}
extern "C" std::int64_t hle_call_guest6(void* f,std::int64_t a,std::int64_t b,std::int64_t c,std::int64_t d,std::int64_t e,std::int64_t g) {
    GuestCallbackScope scope;
    return reinterpret_cast<std::int64_t(*)(std::int64_t,std::int64_t,std::int64_t,std::int64_t,std::int64_t,std::int64_t)>(f)(a,b,c,d,e,g);
}
extern "C" void hle_call_guest_regs_raw(GuestCallRegs*);
extern "C" void hle_call_guest_regs(GuestCallRegs* regs) {GuestCallbackScope scope;hle_call_guest_regs_raw(regs);}
asm(R"(
    .text
    .globl _hle_call_guest_regs_raw
_hle_call_guest_regs_raw:
    .cfi_startproc
    push %rbp
    .cfi_def_cfa_offset 16
    .cfi_offset %rbp, -16
    mov %rsp, %rbp
    .cfi_def_cfa_register %rbp
    push %rbx
    .cfi_offset %rbx, -24
    push %r12
    .cfi_offset %r12, -32
    mov %rdi, %rbx
    sub $32, %rsp
    mov 120(%rbx), %r11
    mov %r11, 0(%rsp)
    mov 128(%rbx), %r11
    mov %r11, 8(%rsp)
    mov 136(%rbx), %r11
    mov %r11, 16(%rsp)
    mov 144(%rbx), %r11
    mov %r11, 24(%rsp)
    mov 8(%rbx), %rdi
    mov 16(%rbx), %rsi
    mov 24(%rbx), %rdx
    mov 32(%rbx), %rcx
    mov 40(%rbx), %r8
    mov 48(%rbx), %r9
    movq 56(%rbx), %xmm0
    movq 64(%rbx), %xmm1
    movq 72(%rbx), %xmm2
    movq 80(%rbx), %xmm3
    movq 88(%rbx), %xmm4
    movq 96(%rbx), %xmm5
    movq 104(%rbx), %xmm6
    movq 112(%rbx), %xmm7
    mov $8, %eax
    call *0(%rbx)
    add $32, %rsp
    mov %rax, 152(%rbx)
    movq %xmm0, 160(%rbx)
    pop %r12
    pop %rbx
    pop %rbp
    .cfi_def_cfa %rsp, 8
    ret
    .cfi_endproc
)");
