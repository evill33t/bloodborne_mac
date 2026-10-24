/* Loaded-image metadata for native guest C++ exception unwinding. No success
 * fallback is supplied when a legacy prepared image lacks unwind metadata. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_modules.h"
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

_Static_assert(sizeof(RuntimeModuleSegment)==16,"module segment ABI");
_Static_assert(sizeof(RuntimeModuleInfo)==424,"module info ABI");
_Static_assert(offsetof(RuntimeModuleInfo,eh_frame_hdr_addr)==328,"module EH header ABI");
_Static_assert(offsetof(RuntimeModuleInfo,segments)==352,"module segment table ABI");
static pthread_mutex_t module_lock=PTHREAD_MUTEX_INITIALIZER;
static RuntimeModuleInfo modules[RUNTIME_MODULE_LIMIT];
static size_t module_count;
static _Atomic uint64_t queries,unknown,unavailable;
#define KERNEL_ERROR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))

static int contains(const RuntimeModuleInfo *m,uint64_t address,uint64_t size) {
    if(!size || address>UINT64_MAX-size)return 0;
    for(uint32_t i=0;i<m->segment_count;++i) {
        const RuntimeModuleSegment *s=&m->segments[i];
        if(address>=s->address && size<=s->size && address-s->address<=s->size-size)return 1;
    }
    return 0;
}
int runtime_modules_install(const RuntimeModuleInfo *records,size_t count) {
    if(count>RUNTIME_MODULE_LIMIT || (count && !records))return EINVAL;
    for(size_t n=0;n<count;++n) {
        const RuntimeModuleInfo *m=&records[n];
        if(m->size!=sizeof(*m) || !memchr(m->name,0,sizeof(m->name)) || !m->name[0] ||
           m->id<0 || !m->segment_count || m->segment_count>4 || m->reserved1 || m->reserved2 ||
           m->tls_init_size>m->tls_size || (m->tls_align && (m->tls_align&(m->tls_align-1))))return EINVAL;
        for(uint32_t s=0;s<m->segment_count;++s)
            if(!m->segments[s].size || m->segments[s].address>UINT64_MAX-m->segments[s].size ||
               m->segments[s].prot<0 || m->segments[s].prot>7)return EINVAL;
        if((m->tls_size && !contains(m,m->tls_init_addr,m->tls_size)) ||
           (m->init_proc_addr && !contains(m,m->init_proc_addr,1)) ||
           (m->fini_proc_addr && !contains(m,m->fini_proc_addr,1)) ||
           (m->eh_frame_hdr_size && !contains(m,m->eh_frame_hdr_addr,m->eh_frame_hdr_size)) ||
           (m->eh_frame_size && !contains(m,m->eh_frame_addr,m->eh_frame_size)) ||
           (!!m->eh_frame_hdr_addr!=!!m->eh_frame_hdr_size) ||
           (!!m->eh_frame_addr!=!!m->eh_frame_size))return EINVAL;
        for(size_t prior=0;prior<n;++prior) {
            if(records[prior].id==m->id)return EINVAL;
            for(uint32_t a=0;a<records[prior].segment_count;++a)
                for(uint32_t b=0;b<m->segment_count;++b) {
                    const RuntimeModuleSegment *x=&records[prior].segments[a],*y=&m->segments[b];
                    if(x->address<y->address+y->size && y->address<x->address+x->size)return EINVAL;
                }
        }
    }
    pthread_mutex_lock(&module_lock);
    if(count)memcpy(modules,records,count*sizeof(*records));
    module_count=count;
    pthread_mutex_unlock(&module_lock);
    return 0;
}

/* A kernel API rejects inaccessible output instead of causing a host fault.
 * These bounded self-process copies honor VM read/write permissions. */
static int copy_from_guest(void *out,const void *guest,size_t size) {
    if(!guest || (uintptr_t)guest>UINTPTR_MAX-size)return 0;
    mach_vm_size_t read=0;
    return mach_vm_read_overwrite(mach_task_self(),(mach_vm_address_t)(uintptr_t)guest,size,
        (mach_vm_address_t)(uintptr_t)out,&read)==KERN_SUCCESS && read==size;
}
static int copy_to_guest(void *guest,const void *value,size_t size) {
    if(!guest || (uintptr_t)guest>UINTPTR_MAX-size)return 0;
    /* Several 4 KiB guest load segments can share a 16 KiB writable host page.
     * Respect their original ELF permissions even when host VM protection has
     * to use a union of those permissions (notably native tiny-image tests). */
    int permitted=1;
    pthread_mutex_lock(&module_lock);
    for(size_t n=0;n<module_count && permitted;++n)
        for(uint32_t k=0;k<modules[n].segment_count;++k) {
            const RuntimeModuleSegment *s=&modules[n].segments[k];
            if((uintptr_t)guest<s->address+s->size && s->address<(uintptr_t)guest+size && !(s->prot&2)) {
                permitted=0;break;
            }
        }
    pthread_mutex_unlock(&module_lock);
    if(!permitted)return 0;
    return mach_vm_write(mach_task_self(),(mach_vm_address_t)(uintptr_t)guest,
        (vm_offset_t)(uintptr_t)value,(mach_msg_type_number_t)size)==KERN_SUCCESS;
}
ABI int32_t runtime_module_info_from_addr(uint64_t address,uint32_t flags,RuntimeModuleInfo *out) {
    uint64_t output_size=0;
    if(!copy_from_guest(&output_size,out,sizeof(output_size)))return KERNEL_ERROR(14);
    if(flags>2 || output_size<sizeof(*out))return KERNEL_ERROR(22);
    atomic_fetch_add_explicit(&queries,1,memory_order_relaxed);
    RuntimeModuleInfo result;
    int found=0;
    pthread_mutex_lock(&module_lock);
    const size_t count=module_count;
    for(size_t n=0;n<count;++n)if(contains(&modules[n],address,1)) {
        result=modules[n];found=1;break;
    }
    pthread_mutex_unlock(&module_lock);
    if(!count) {
        if(!atomic_fetch_add_explicit(&unavailable,1,memory_order_relaxed))
            fputs("Runtime: module unwind metadata unavailable; regenerate the prepared image as BBPROBE6\n",stderr);
        return KERNEL_ERROR(78);
    }
    if(!found) {atomic_fetch_add_explicit(&unknown,1,memory_order_relaxed);return KERNEL_ERROR(3);}
    return copy_to_guest(out,&result,sizeof(result)) ? 0 : KERNEL_ERROR(14);
}
static const RuntimeExport exports[]={
    {"sceKernelGetModuleInfoFromAddr",runtime_module_info_from_addr},
};
uintptr_t runtime_modules_resolve(const char *name) {return RUNTIME_LOOKUP(exports,name);}
void runtime_modules_report(void) {
    printf("Runtime: module-info queries=%llu, unknown=%llu, metadata-unavailable=%llu\n",
        (unsigned long long)atomic_load_explicit(&queries,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&unknown,memory_order_relaxed),
        (unsigned long long)atomic_load_explicit(&unavailable,memory_order_relaxed));
}
