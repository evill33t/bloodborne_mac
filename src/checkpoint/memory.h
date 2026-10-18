/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_CHECKPOINT_MEMORY_H
#define BB_CHECKPOINT_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Fixed-width little-endian records. This provider covers the runtime's direct,
 * flexible and reserved guest VMAs. Image/stack/TLS low mappings are a separate
 * provider and are deliberately not inferred from arbitrary host mappings. */
typedef struct {
    uint32_t version,page_size;
    uint64_t pool_size;
    uint32_t block_count,vma_count;
    uint64_t reserved;
} BbCheckpointMemoryLayout;
typedef struct {
    uint64_t physical,size;
    uint32_t slot;
    int32_t type;
    uint64_t reserved;
} BbCheckpointMemoryBlock;
typedef struct {
    uint64_t start,size,physical;
    uint32_t kind;
    int32_t protection,type;
    uint32_t reserved;
} BbCheckpointMemoryVma;
enum { BB_CK_MEMORY_RESERVED=1, BB_CK_MEMORY_DIRECT=2, BB_CK_MEMORY_FLEXIBLE=3 };
/* Returns positive host errno. With null arrays, returns required counts in
 * layout. Call only while guest writers and the GPU are quiesced. */
int runtime_memory_checkpoint_export(BbCheckpointMemoryLayout *layout,
    BbCheckpointMemoryBlock *blocks,size_t blocks_capacity,
    BbCheckpointMemoryVma *vmas,size_t vmas_capacity);
/* Validate all records before creating any mapping. Requires an empty guest
 * memory table and no GPU hooks. On OS allocation failure, returns to an empty
 * table; the restore coordinator must not release guest execution on failure. */
int runtime_memory_checkpoint_restore(const BbCheckpointMemoryLayout *layout,
    const BbCheckpointMemoryBlock *blocks,const BbCheckpointMemoryVma *vmas);
int runtime_memory_checkpoint_read(uint64_t physical,void *bytes,size_t size);
int runtime_memory_checkpoint_write(uint64_t physical,const void *bytes,size_t size);
#ifdef __cplusplus
}
#endif
#endif
