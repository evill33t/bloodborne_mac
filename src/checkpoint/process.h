/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_PROCESS_H
#define BB_CHECKPOINT_PROCESS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t version,reserved;
    uint64_t capabilities,globals,canary,process_param,heap_api;
    int32_t need_libc_internal;
    uint32_t handlers;
    uint64_t reserved2[2];
} BbCheckpointProcessRecord;
typedef struct {
    uint64_t callback,argument,dso;
    uint32_t flags,reserved; /* with argument=1, active=2 */
} BbCheckpointExitRecord;
/* Bind before resolving guest data imports during fresh restore. Their
 * addresses must not depend on the layout of the rebuilt native executable. */
int runtime_checkpoint_process_bind_globals(uint64_t address);
int runtime_checkpoint_process_export(BbCheckpointProcessRecord*,BbCheckpointExitRecord*,size_t capacity);
int runtime_checkpoint_process_restore(const BbCheckpointProcessRecord*,const BbCheckpointExitRecord*);
int bb_checkpoint_runtime_process(void *token,const BbCheckpointProcessRecord**,const BbCheckpointExitRecord**);
#ifdef __cplusplus
}
#endif
#endif
