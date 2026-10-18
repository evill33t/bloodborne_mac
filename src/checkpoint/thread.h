/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_THREAD_H
#define BB_CHECKPOINT_THREAD_H
#include "runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int32_t error;
    uint32_t guard_owner;
    uint64_t module_tls[8],module_tls_size[8],keys[256];
    uint32_t signal_mask[4];
} BbCheckpointThreadLocal;
typedef struct {
    uint64_t id,stack,stack_size,tls,tls_size,tcb,entry,argument,result;
    uint64_t attr_stack,attr_guard,attr_affinity;
    uint32_t flags; /* detached=1, finished=2, joined=4, host-owned=8 */
    int32_t policy,priority,inherit;
    char name[32];
    uint64_t module_tls[8],module_tls_size[8];
    uint64_t reserved[2];
} BbCheckpointThreadRecord;
typedef struct {
    uint64_t id,stack,guard,affinity;
    int32_t detached,policy,priority,inherit;
    uint64_t reserved[2];
} BbCheckpointThreadAttrRecord;
BbCheckpointThreadLocal *runtime_thread_checkpoint_local(void);
/* Caller holds the runtime's frozen token. Records contain no pthread, native
 * jump buffer, host stack or native TLS pointers. */
int runtime_thread_checkpoint_export(BbCheckpointThreadRecord *records,size_t capacity,size_t *count);
int runtime_thread_checkpoint_attr_export(BbCheckpointThreadAttrRecord *records,size_t capacity,size_t *count);
int runtime_thread_checkpoint_attr_restore(const BbCheckpointThreadAttrRecord *records,size_t count);
size_t bb_checkpoint_runtime_thread_records(void *token,const BbCheckpointThreadRecord **records);
size_t bb_checkpoint_runtime_thread_attributes(void *token,const BbCheckpointThreadAttrRecord **records);
/* Fresh runtime only; copies TLS/stack bytes into these owned mappings before
 * calling Start. Start creates native workers gated until Activate. */
int runtime_thread_checkpoint_restore(const BbCheckpointThreadRecord *records,size_t count);
int runtime_thread_checkpoint_start(const BbCheckpointRuntimeThread *threads,size_t count);
void runtime_thread_checkpoint_activate(void);
void runtime_checkpoint_bind_module_tls(const BbCheckpointThreadLocal *local);
uint64_t runtime_checkpoint_module_tls_size(unsigned module);
#ifdef __cplusplus
}
#endif
#endif
