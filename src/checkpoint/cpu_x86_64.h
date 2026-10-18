/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_CPU_X86_64_H
#define BB_CHECKPOINT_CPU_X86_64_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    BB_CK_RAX, BB_CK_RBX, BB_CK_RCX, BB_CK_RDX, BB_CK_RSI, BB_CK_RDI,
    BB_CK_RBP, BB_CK_RSP, BB_CK_R8, BB_CK_R9, BB_CK_R10, BB_CK_R11,
    BB_CK_R12, BB_CK_R13, BB_CK_R14, BB_CK_R15
};

/* Fixed, pointer-free CPU record. Only guest PCs/stacks belong in a persisted
 * record. FS/LDT, host TLS and native thread state are rebuilt separately.
 * AVX is part of this runtime's minimum x86-64 CPU contract. */
typedef struct __attribute__((aligned(16))) {
    uint64_t gpr[16];
    uint64_t rip, rflags;
    uint32_t features, reserved[3];
    unsigned char fx[512];
    unsigned char ymm_high[256];
    unsigned char padding[32];
} BbCheckpointCpu;

#if defined(__x86_64__)
/* Zero-initialize the record before its first use. Save the continuation
 * following this call, without retaining a host frame.
 * Return zero initially; restore resumes with the saved RAX value. */
int bb_checkpoint_cpu_save(BbCheckpointCpu *) __attribute__((returns_twice));
void bb_checkpoint_cpu_restore(const BbCheckpointCpu *) __attribute__((noreturn));
#endif

/* Structural checks only. The coordinator must also check RIP against its
 * executable guest ranges and RSP against this thread's writable guest stack. */
int bb_checkpoint_cpu_valid(const BbCheckpointCpu *);

#ifdef __cplusplus
}
#endif
#endif
