/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_x86_64.h"
#include <stddef.h>
#include <string.h>

_Static_assert(sizeof(BbCheckpointCpu)==960,"checkpoint CPU record size");
_Static_assert(offsetof(BbCheckpointCpu,rip)==128,"checkpoint RIP offset");
_Static_assert(offsetof(BbCheckpointCpu,fx)==160,"checkpoint FXSAVE offset");
_Static_assert(offsetof(BbCheckpointCpu,ymm_high)==672,"checkpoint YMM offset");

int bb_checkpoint_cpu_valid(const BbCheckpointCpu *cpu) {
    if (!cpu || cpu->features!=1 || !cpu->rip || !cpu->gpr[BB_CK_RSP] ||
        cpu->rip>=UINT64_C(0x10000000000) || cpu->gpr[BB_CK_RSP]>=UINT64_C(0x10000000000) ||
        (cpu->gpr[BB_CK_RSP]&7) || (cpu->rflags&UINT64_C(0xffffffffffdff528)) ||
        (cpu->rflags&0x202)!=0x202) return 0;
    for (unsigned k=0;k<3;++k) if (cpu->reserved[k]) return 0;
    for (unsigned k=0;k<sizeof(cpu->padding);++k) if (cpu->padding[k]) return 0;
    uint32_t mxcsr;
    memcpy(&mxcsr,cpu->fx+24,sizeof(mxcsr));
    /* Reserved MXCSR bits cause a general protection fault in FXRSTOR. */
    return !(mxcsr&~UINT32_C(0xffff));
}

#if defined(__x86_64__)
#define SYM(name) "_" name
#define SAVE_YMM(n,off) "vextractf128 $1,%ymm" #n "," #off "(%rdi)\n"
#define LOAD_YMM(n,off) "vinsertf128 $1," #off "(%rdi),%ymm" #n ",%ymm" #n "\n"
__asm__(".text\n.p2align 4\n.globl " SYM("bb_checkpoint_cpu_save") "\n"
        SYM("bb_checkpoint_cpu_save") ":\n"
        "mov %rax,0(%rdi)\nmov %rbx,8(%rdi)\nmov %rcx,16(%rdi)\nmov %rdx,24(%rdi)\n"
        "mov %rsi,32(%rdi)\nmov %rdi,40(%rdi)\nmov %rbp,48(%rdi)\n"
        "mov %r8,64(%rdi)\nmov %r9,72(%rdi)\nmov %r10,80(%rdi)\nmov %r11,88(%rdi)\n"
        "mov %r12,96(%rdi)\nmov %r13,104(%rdi)\nmov %r14,112(%rdi)\nmov %r15,120(%rdi)\n"
        "lea 8(%rsp),%rax\nmov %rax,56(%rdi)\nmov (%rsp),%rax\nmov %rax,128(%rdi)\n"
        "pushfq\npopq 136(%rdi)\nmovl $1,144(%rdi)\n"
        "fxsave64 160(%rdi)\n"
        SAVE_YMM(0,672) SAVE_YMM(1,688) SAVE_YMM(2,704) SAVE_YMM(3,720)
        SAVE_YMM(4,736) SAVE_YMM(5,752) SAVE_YMM(6,768) SAVE_YMM(7,784)
        SAVE_YMM(8,800) SAVE_YMM(9,816) SAVE_YMM(10,832) SAVE_YMM(11,848)
        SAVE_YMM(12,864) SAVE_YMM(13,880) SAVE_YMM(14,896) SAVE_YMM(15,912)
        "xor %eax,%eax\nret\n"
        ".p2align 4\n.globl " SYM("bb_checkpoint_cpu_restore") "\n"
        SYM("bb_checkpoint_cpu_restore") ":\n"
        "fxrstor64 160(%rdi)\n"
        LOAD_YMM(0,672) LOAD_YMM(1,688) LOAD_YMM(2,704) LOAD_YMM(3,720)
        LOAD_YMM(4,736) LOAD_YMM(5,752) LOAD_YMM(6,768) LOAD_YMM(7,784)
        LOAD_YMM(8,800) LOAD_YMM(9,816) LOAD_YMM(10,832) LOAD_YMM(11,848)
        LOAD_YMM(12,864) LOAD_YMM(13,880) LOAD_YMM(14,896) LOAD_YMM(15,912)
        "pushq 136(%rdi)\npopfq\nmov 56(%rdi),%rsp\npushq 128(%rdi)\n"
        "mov 0(%rdi),%rax\nmov 8(%rdi),%rbx\nmov 16(%rdi),%rcx\nmov 24(%rdi),%rdx\n"
        "mov 32(%rdi),%rsi\nmov 48(%rdi),%rbp\n"
        "mov 64(%rdi),%r8\nmov 72(%rdi),%r9\nmov 80(%rdi),%r10\nmov 88(%rdi),%r11\n"
        "mov 96(%rdi),%r12\nmov 104(%rdi),%r13\nmov 112(%rdi),%r14\nmov 120(%rdi),%r15\n"
        "mov 40(%rdi),%rdi\nret\n");
#endif
