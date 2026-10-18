/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_GRAPHICS_H
#define BB_CHECKPOINT_GRAPHICS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t id,size; } BbCheckpointGraphicsChunk;
/* Native callbacks are rebound on each launch and never enter the archive.
 * pause runs after guest CPUs park, before logical HLE providers are paused,
 * so draining GPU fence callbacks may still signal those providers. Failure
 * must reopen GPU admission and leave *token null. release unlocks resources
 * and reopens admission after HLE providers thaw, before guest CPUs resume. */
typedef struct {
    int (*pause)(uint32_t timeout_ms,void **token,char *error,size_t capacity);
    void (*release)(void *token);
    int (*describe)(void *token,BbCheckpointGraphicsChunk *chunks,size_t capacity,size_t *count);
    int (*read)(void *token,uint64_t id,uint64_t offset,void *bytes,size_t size,char *error,size_t capacity);
} BbCheckpointGraphicsHooks;
int bb_checkpoint_runtime_graphics_hooks(const BbCheckpointGraphicsHooks *hooks);
size_t bb_checkpoint_runtime_graphics_chunks(void *frozen,const BbCheckpointGraphicsChunk **chunks);
int bb_checkpoint_runtime_graphics_read(void *frozen,uint64_t id,uint64_t offset,void *bytes,size_t size,char *error,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
