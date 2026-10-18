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
size_t runtime_gpu_memory_mappings(BbDirectMapping *out,size_t capacity);
int runtime_gpu_memory_allocate(uint64_t start,uint64_t end,uint64_t size,uint64_t alignment,int type,uint64_t *physical);
int runtime_gpu_memory_map(uintptr_t *address,uint64_t size,int prot,int flags,uint64_t physical,uint64_t alignment);
void *bbmetal_guest_allocate(size_t size);
#ifdef __cplusplus
}
#endif
