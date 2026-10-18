/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_RUNTIME_H
#define BB_CHECKPOINT_RUNTIME_H
#include "cpu_x86_64.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Opt-in while the complete provider set is being integrated. None of these
 * APIs claims to have saved a playable checkpoint. */
int bb_checkpoint_runtime_enabled(void);
int bb_checkpoint_runtime_code(uint64_t address,uint64_t size);
uintptr_t bb_checkpoint_runtime_import(void *code,uintptr_t target,uint64_t identity,int replayable);
void bb_checkpoint_runtime_import_label(uint64_t identity,const char *label);
uint64_t bb_checkpoint_runtime_run(uint64_t id,void *entry,void *arg0,void *arg1,
                                 void *stack,size_t stack_size,const BbCheckpointCpu *restored);
int bb_checkpoint_runtime_exit(void *result);
int bb_checkpoint_runtime_requested(void);
int bb_checkpoint_runtime_can_replay(void);
int bb_checkpoint_runtime_replay(void);
void bb_checkpoint_runtime_enter_callback(void);
void bb_checkpoint_runtime_leave_callback(void);
int bb_checkpoint_runtime_replayable_import(const char *name);
uint64_t bb_checkpoint_runtime_current(void);
uint64_t bb_checkpoint_runtime_return_stub(void);
int bb_checkpoint_runtime_bind_return_stub(uint64_t address);
int bb_checkpoint_runtime_valid_continuation(const BbCheckpointCpu *cpu,uint64_t stack,uint64_t size);
typedef struct {
    uint64_t id,stack,stack_size;
    BbCheckpointCpu cpu;
} BbCheckpointRuntimeThread;
/* Freeze on a separate host coordinator thread. The UI must continue servicing
 * presentation. A failed request resumes every participant before returning. */
int bb_checkpoint_runtime_freeze(uint32_t timeout_ms,void **token,char *error,size_t error_size);
size_t bb_checkpoint_runtime_threads(void *token,const BbCheckpointRuntimeThread **threads);
void bb_checkpoint_runtime_thaw(void *token);
int32_t bb_checkpoint_sema_create(uint32_t *out,uint32_t attr,int32_t initial,int32_t maximum);
int32_t bb_checkpoint_sema_wait(uint32_t id,int32_t need,uint32_t *timeout);
int32_t bb_checkpoint_sema_poll(uint32_t id,int32_t need);
int32_t bb_checkpoint_sema_signal(uint32_t id,int32_t count);
int32_t bb_checkpoint_sema_cancel(uint32_t id,int32_t count,int32_t *waiters);
int32_t bb_checkpoint_sema_delete(uint32_t id);
unsigned bb_checkpoint_sema_waiters(uint32_t id);
size_t bb_checkpoint_runtime_semaphores(void *token,const unsigned char **bytes);
int bb_checkpoint_runtime_restore_semaphores(const unsigned char *bytes,size_t size,char *error,size_t capacity);
void bb_checkpoint_runtime_activate_semaphores(void);
uintptr_t bb_checkpoint_mutex_resolve(const char *name);
uintptr_t bb_checkpoint_rwlock_resolve(const char *name);
size_t bb_checkpoint_runtime_synchronization(void *token,const unsigned char **bytes);
int bb_checkpoint_runtime_restore_synchronization(const unsigned char *bytes,size_t size,char *error,size_t capacity);
void bb_checkpoint_runtime_activate_synchronization(void);
size_t bb_checkpoint_runtime_rwlocks(void *token,const unsigned char **bytes);
int bb_checkpoint_runtime_restore_rwlocks(const unsigned char *bytes,size_t size,char *error,size_t capacity);
void bb_checkpoint_runtime_activate_rwlocks(void);
#ifdef __cplusplus
}
#endif
#endif
