/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uintptr_t start; uint64_t size,physical; } BbDirectMapping;
uint64_t runtime_gpu_memory_size(void);
void *runtime_gpu_memory_backing(void);
uint64_t runtime_gpu_memory_generation(void);
/* Aligned 32/64-bit GPU labels. Identity survives splits/protection changes,
 * but not unmap/reuse. Access holds the map lock and uses the backing alias. */
typedef struct { uintptr_t address; uint64_t identity; uint32_t bytes; } BbGpuLabel;
int runtime_gpu_label_capture(uintptr_t address,uint32_t bytes,BbGpuLabel *label);
int runtime_gpu_label_load(const BbGpuLabel *label,uint64_t *value);
int runtime_gpu_label_store(const BbGpuLabel *label,uint64_t value);
/* Copy readable runtime mappings through their stable backing alias while
 * holding the map read lock. Validates the entire source before writing any
 * destination bytes. The destination must be a caller-owned writable host
 * buffer outside runtime mappings/backing. Zero means use a checked fallback;
 * no destination bytes were changed. Does not synchronize GPU producers. */
int runtime_gpu_memory_snapshot(uintptr_t address,void *destination,size_t bytes);
/* Serialize mapping changes with backend admission. Register before starting
 * guest writers; unregister after stopping them, outside backend locks. Begin
 * runs before the VMA write lock and end after unlocking. A failed begin must
 * release its own locks; the mapping operation is refused and end is not run.
 * Nested mapping operations share the outer guard. Registration waits for
 * active guards and returns zero for an incomplete pair or recursive use. */
typedef int (*BbGpuMemoryMutationBegin)(void *context);
typedef void (*BbGpuMemoryMutationEnd)(void *context);
int runtime_gpu_memory_set_mutation_hooks(void *context,
    BbGpuMemoryMutationBegin begin,BbGpuMemoryMutationEnd end);
size_t runtime_gpu_memory_mappings(BbDirectMapping *out,size_t capacity);
int runtime_gpu_memory_allocate(uint64_t start,uint64_t end,uint64_t size,uint64_t alignment,int type,uint64_t *physical);
int runtime_gpu_memory_map(uintptr_t *address,uint64_t size,int prot,int flags,uint64_t physical,uint64_t alignment);
void *bbmetal_guest_allocate(size_t size);
#ifdef __cplusplus
}
#endif
