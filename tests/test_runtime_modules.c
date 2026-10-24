/* Loaded-image API tests use owned VM mappings, never original game binaries. */
#define _GNU_SOURCE
#include "runtime_modules.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Link the isolated production implementation without the full runtime.
 * Actual NID binding and loader registration are covered by test_probe.py. */
uintptr_t runtime_lookup(const RuntimeExport *exports,size_t count,const char *nid) {
    (void)exports;(void)count;(void)nid;return 0;
}
static RuntimeModuleInfo records[2];
static void expect_error(uint64_t address,uint32_t flags,RuntimeModuleInfo *out,uint32_t expected) {
    assert((uint32_t)runtime_module_info_from_addr(address,flags,out)==expected);
}
static void *query_thread(void *argument) {
    const uintptr_t index=(uintptr_t)argument;
    for(unsigned n=0;n<1000;++n) {
        RuntimeModuleInfo out={.size=sizeof(out)};
        assert(!runtime_module_info_from_addr(records[index].segments[0].address+n%256,n%3,&out));
        assert(!memcmp(&out,&records[index],sizeof(out)));
    }
    return NULL;
}
int main(void) {
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *mapped=mmap(NULL,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(mapped!=MAP_FAILED);
    for(unsigned n=0;n<2;++n) {
        RuntimeModuleInfo *m=&records[n];
        *m=(RuntimeModuleInfo){.size=sizeof(*m),.id=(int32_t)n,.segment_count=2,.ref_count=1};
        snprintf(m->name,sizeof(m->name),"owned-%u.prx",n);
        m->segments[0]=(RuntimeModuleSegment){(uintptr_t)mapped+n*page*2,(uint32_t)page,5};
        m->segments[1]=(RuntimeModuleSegment){(uintptr_t)mapped+n*page*2+page,(uint32_t)page,6};
        m->eh_frame_addr=m->segments[0].address+32;m->eh_frame_size=64;
        m->eh_frame_hdr_addr=m->segments[0].address+128;m->eh_frame_hdr_size=32;
        m->tls_index=n+1;m->tls_init_addr=m->segments[1].address;
        m->tls_init_size=16;m->tls_size=32;m->tls_align=16;
    }
    RuntimeModuleInfo out={.size=sizeof(out)};
    assert(!runtime_modules_install(NULL,0));
    expect_error(records[0].segments[0].address,0,&out,0x8002004e); /* ENOSYS, legacy image */
    assert(!runtime_modules_install(records,2));
    for(unsigned n=0;n<2;++n) {
        struct {RuntimeModuleInfo info;unsigned char tail[16];} guarded;
        memset(&guarded,0xa5,sizeof(guarded));guarded.info.size=sizeof(guarded);
        assert(!runtime_module_info_from_addr(records[n].segments[0].address,0,&guarded.info));
        assert(!memcmp(&guarded.info,&records[n],sizeof(out)));
        for(unsigned k=0;k<sizeof(guarded.tail);++k)assert(guarded.tail[k]==0xa5);
        assert(!runtime_module_info_from_addr(records[n].segments[1].address+page-1,2,&guarded.info));
    }
    out=(RuntimeModuleInfo){.size=sizeof(out)};
    const RuntimeModuleInfo unchanged=out;
    expect_error((uintptr_t)mapped+page*4,0,&out,0x80020003); /* end-exclusive */
    assert(!memcmp(&out,&unchanged,sizeof(out)));
    expect_error(1,3,&out,0x80020016);
    expect_error(1,UINT32_MAX,&out,0x80020016);
    expect_error(1,0,NULL,0x8002000e);
    expect_error(1,0,(void *)1,0x8002000e);
    expect_error(1,0,(void *)(UINTPTR_MAX-3),0x8002000e);
    out.size=sizeof(out)-1;
    expect_error(records[0].segments[0].address,0,&out,0x80020016);
    /* A guest's read-only code can share a writable native host page. */
    RuntimeModuleInfo *guest_readonly=(void *)(uintptr_t)records[0].segments[0].address;
    guest_readonly->size=sizeof(*guest_readonly);
    expect_error(records[0].segments[0].address,0,guest_readonly,0x8002000e);
    unsigned char *output=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(output!=MAP_FAILED);
    RuntimeModuleInfo *readonly=(void *)output;readonly->size=sizeof(*readonly);
    assert(!mprotect(output,page,PROT_READ));
    expect_error(records[0].segments[0].address,0,readonly,0x8002000e);
    assert(readonly->size==sizeof(*readonly));
    assert(!mprotect(output,page,PROT_READ|PROT_WRITE));
    RuntimeModuleInfo *partial=(void *)(output+page-8);partial->size=sizeof(*partial);
    assert(!mprotect(output+page,page,PROT_NONE));
    expect_error(records[0].segments[0].address,0,partial,0x8002000e);

    RuntimeModuleInfo bad[2];memcpy(bad,records,sizeof(bad));
    assert(runtime_modules_install(records,RUNTIME_MODULE_LIMIT+1)==EINVAL);
    assert(runtime_modules_install(NULL,1)==EINVAL);
    bad[0].segment_count=5;assert(runtime_modules_install(bad,2)==EINVAL);
    memcpy(bad,records,sizeof(bad));bad[1].id=0;assert(runtime_modules_install(bad,2)==EINVAL);
    memcpy(bad,records,sizeof(bad));bad[1].segments[0]=bad[0].segments[0];assert(runtime_modules_install(bad,2)==EINVAL);
    memcpy(bad,records,sizeof(bad));bad[0].eh_frame_size=(uint32_t)page;assert(runtime_modules_install(bad,2)==EINVAL);
    memcpy(bad,records,sizeof(bad));bad[0].tls_align=3;assert(runtime_modules_install(bad,2)==EINVAL);
    memcpy(bad,records,sizeof(bad));memset(bad[0].name,'x',sizeof(bad[0].name));assert(runtime_modules_install(bad,2)==EINVAL);
    /* Invalid replacement records leave the previous immutable table intact. */
    pthread_t workers[8];
    for(uintptr_t n=0;n<8;++n)assert(!pthread_create(&workers[n],NULL,query_thread,(void *)(n%2)));
    for(unsigned n=0;n<8;++n)assert(!pthread_join(workers[n],NULL));
    assert(!runtime_modules_install(NULL,0));out.size=sizeof(out);
    expect_error(records[0].segments[0].address,0,&out,0x8002004e);
    assert(!munmap(output,page*2));assert(!munmap(mapped,page*4));
    puts("PASS: module-info exact ABI, owned segments/EH/TLS, boundary errors, pointer permissions, transactional readiness and concurrent queries");
    return 0;
}
