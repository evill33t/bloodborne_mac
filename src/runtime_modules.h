#ifndef BB_RUNTIME_MODULES_H
#define BB_RUNTIME_MODULES_H
#include <stddef.h>
#include <stdint.h>
#include "runtime.h"

/* PS4 ModuleInfoEx ABI, shared with BBPROBE6's little-endian metadata table.
 * Layout follows OpenOrbis include/orbis/_types/kernel.h. Addresses in the
 * prepared file are image RVAs; the loader validates and rebases them once. */
typedef struct {
    uint64_t address;
    uint32_t size;
    int32_t prot; /* ELF PF_R/PF_W/PF_X, as reported by the module API */
} RuntimeModuleSegment;
typedef struct {
    uint64_t size;
    char name[256];
    int32_t id;
    uint32_t tls_index;
    uint64_t tls_init_addr;
    uint32_t tls_init_size,tls_size,tls_offset,tls_align;
    uint64_t init_proc_addr,fini_proc_addr,reserved1,reserved2;
    uint64_t eh_frame_hdr_addr,eh_frame_addr;
    uint32_t eh_frame_hdr_size,eh_frame_size;
    RuntimeModuleSegment segments[4];
    uint32_t segment_count,ref_count;
} RuntimeModuleInfo;
#define RUNTIME_MODULE_LIMIT 17

/* Internal loader API. Install is transactional; count0 clears readiness.
 * Records describe loaded images, never host executables or allocated heaps. */
int runtime_modules_install(const RuntimeModuleInfo *records,size_t count);
int32_t ABI runtime_module_info_from_addr(uint64_t address,uint32_t flags,RuntimeModuleInfo *out);
uintptr_t runtime_modules_resolve(const char *name);
void runtime_modules_report(void);
#endif
