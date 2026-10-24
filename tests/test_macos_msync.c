/* Real syscall tests with owned anonymous/file mappings. The observation
 * wrapper records actual host flags/ranges and can inject an I/O failure. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stddef.h>
static int observed_host_msync(void *,size_t,int);
#define msync observed_host_msync
#include "../src/runtime_memory.c"
#undef msync
#include <assert.h>

static struct {uintptr_t address;size_t size;int flags;unsigned calls;} observed;
static int injected_error;
static int observed_host_msync(void *address,size_t size,int flags) {
    observed.address=(uintptr_t)address;observed.size=size;observed.flags=flags;++observed.calls;
    if(injected_error) {errno=injected_error;return -1;}
    return msync(address,size,flags);
}
int32_t *runtime_errno(void) {static _Thread_local int32_t value;return &value;}
int32_t runtime_guest_errno(int error) {
    switch(error) {
    case 0:return 0;case EIO:return 5;case ENOMEM:return 12;case EFAULT:return 14;
    case EBUSY:return 16;case EINVAL:return 22;default:return 5;
    }
}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for(size_t k=0;k<count;++k)if(!strcmp(table[k].name,name))return (uintptr_t)table[k].function;
    return 0;
}
typedef int32_t (ABI *GuestMsync)(void *,uint64_t,int);
static GuestMsync guest_msync;
static void error(void *address,uint64_t length,int flags,int expected,int host_call) {
    const unsigned before=observed.calls;
    *runtime_errno()=0;
    assert(guest_msync(address,length,flags)==-1 && *runtime_errno()==expected);
    assert(observed.calls==before+(unsigned)host_call);
}
static void *owned_file_map(size_t length,int descriptor,int flags) {
    unsigned char *reservation=mmap(NULL,length+PAGE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(reservation!=MAP_FAILED);
    const uintptr_t aligned=align_up((uintptr_t)reservation,PAGE);
    const size_t prefix=aligned-(uintptr_t)reservation;
    const size_t suffix=PAGE-prefix;
    if(prefix)assert(!munmap(reservation,prefix));
    if(suffix)assert(!munmap((void *)(aligned+length),suffix));
    void *mapping=mmap((void *)aligned,length,PROT_READ|PROT_WRITE,flags|MAP_FIXED,descriptor,0);
    assert(mapping==(void *)aligned);return mapping;
}
int main(void) {
    guest_msync=(GuestMsync)runtime_memory_resolve("tZY4+SZNFhA#p#J");assert(guest_msync);
    assert((uintptr_t)guest_msync==runtime_memory_resolve("msync"));
    unsigned char *low=runtime_low_map(PAGE*3,PROT_READ|PROT_WRITE);assert(low);
    memset(low,0xa5,PAGE*3);
    for(unsigned mode=0;mode<3;++mode)for(unsigned offset=0;offset<PAGE;offset+=4096) {
        const unsigned before=observed.calls;
        *runtime_errno()=91;
        assert(!guest_msync(low+offset,4096,(int)mode));
        assert(*runtime_errno()==91); /* successful POSIX calls retain errno */
        assert(observed.calls==before+1 && observed.address==(uintptr_t)low && observed.size==PAGE);
        assert(observed.flags==(mode==1 ? MS_ASYNC : MS_SYNC|(mode==2 ? MS_INVALIDATE : 0)));
    }
    assert(!guest_msync(low+PAGE-4096,8192,1));
    assert(observed.address==(uintptr_t)low && observed.size==PAGE*2);
    assert(!guest_msync(low+PAGE,0,0));
    assert(observed.address==(uintptr_t)low && observed.size==PAGE*3);
    assert(!guest_msync(low+1,0,0));
    assert(observed.address==(uintptr_t)low && observed.size==PAGE);
    /* Unknown FreeBSD9 bits are ignored, never forwarded as destructive
     * Darwin-specific KILLPAGES/DEACTIVATE. ASYNC+INVALIDATE is rejected. */
    assert(!guest_msync(low,PAGE,4) && observed.flags==MS_SYNC);
    assert(!guest_msync(low,PAGE,0x101) && observed.flags==MS_ASYNC);
    error(low,PAGE,3,22,0);error(low,PAGE,-1,22,0);
    error((void *)(UINTPTR_MAX-3),8,0,22,0);
    error(low,UINT64_MAX,0,22,0);
    for(size_t k=0;k<PAGE*3;++k)assert(low[k]==0xa5);
    injected_error=EIO;error(low,PAGE,0,5,1);
    injected_error=EBUSY;error(low,PAGE,2,16,1);
    injected_error=ENOMEM;error(low,PAGE,1,22,1);
    injected_error=0;
    assert(!munmap(low,PAGE*3));
    error(low,PAGE,1,22,1);error(low,0,0,22,0);

    runtime_memory_reserve();
    void *reserved=NULL;assert(!reserve_range(&reserved,PAGE,0,0));
    error(reserved,PAGE,1,22,0);error(reserved,0,0,22,0);
    unsigned char *flex=posix_mmap(NULL,PAGE*3,3,0x1002,-1,0);
    assert(flex!=(void *)UINTPTR_MAX);memset(flex,0x64,PAGE*3);
    assert(!guest_msync(flex+4096,4096,1));
    assert(!guest_msync(flex+PAGE,0,0));
    assert(observed.address==(uintptr_t)flex && observed.size==PAGE*3);
    assert(!posix_munmap(flex+PAGE,PAGE));
    error(flex,PAGE*3,1,22,0);
    assert(flex[0]==0x64 && flex[PAGE*2]==0x64);
    assert(!posix_munmap(flex,PAGE) && !posix_munmap(flex+PAGE*2,PAGE));

    char path[]="/tmp/bb-msync-owned-XXXXXX";
    int descriptor=mkstemp(path);assert(descriptor>=0);assert(!unlink(path));
    assert(!ftruncate(descriptor,PAGE*4));
    unsigned char *shared=owned_file_map(PAGE*4,descriptor,MAP_SHARED);
    const uint64_t payload=UINT64_C(0x123456789abcdef0);memcpy(shared+4096,&payload,8);
    assert(!guest_msync(shared+4096,4096,0) && observed.flags==MS_SYNC);
    uint64_t readback=0;assert(pread(descriptor,&readback,8,4096)==8 && readback==payload);
    assert(!guest_msync(shared,PAGE*4,2));
    assert(!munmap(shared,PAGE*4));
    unsigned char *private_copy=owned_file_map(PAGE*4,descriptor,MAP_PRIVATE);
    memset(private_copy+4096,0x77,8);
    assert(!guest_msync(private_copy+4096,4096,0));
    assert(pread(descriptor,&readback,8,4096)==8 && readback==payload);
    assert(!munmap(private_copy,PAGE*4));assert(!close(descriptor));
    /* File-backed guest mmap is still explicitly unsupported, rather than
     * silently copying data and claiming persistence through msync. */
    assert(posix_mmap(NULL,PAGE,3,1,descriptor,0)==(void *)UINTPTR_MAX && *runtime_errno()==19);
    puts("PASS: real guest msync flags/ranges,4KiB probes/16KiB pages,zero-length regions,anonymous/shared/private memory,holes,overflow and I/O errors");
    return 0;
}
