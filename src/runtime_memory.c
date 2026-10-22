/* PS4 virtual memory: direct (physical) pool, flexible memory, reservations,
 * protection changes and queries. One sparse memfd backs the whole direct
 * pool, so any mapping of any physical range aliases the same storage.
 * Windows: a pagefile-backed section plays the memfd's part, and the PS4 user range is
 * reserved as one placeholder that views of the section replace (VirtualAlloc2/MapViewOfFile3,
 * as shadPS4 does); see the "host mappings" section below.
 * Guest addresses are placed below 1 TiB (PS4 user range): GPU descriptors
 * encode 40-bit addresses, so host-default 0x7f... addresses would not fit. */
#define _GNU_SOURCE
#include "runtime.h"
#include "runtime_gpu_memory.h"
#include "checkpoint/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/stat.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif
#endif
/* sceKernelGetDirectMemorySize on retail PS4: 5056 MiB. BB_DMEM_MB raises it
 * for resolution-dependent targets; the macOS launcher sizes it from scene pixels. */
static uint64_t pool_size_bytes(void) {
    static uint64_t size;
    if (!size) {
        const char *env = getenv("BB_DMEM_MB");
        uint64_t mb = env ? strtoull(env, NULL, 10) : 0;
        if (mb < 5056 || mb > 16384) mb = 5056;
        size = mb * 1024 * 1024;
    }
    return size;
}
#define POOL_SIZE pool_size_bytes()
#define FLEXIBLE_SIZE (UINT64_C(448) * 1024 * 1024)
#define PAGE UINT64_C(16384)
#ifdef __APPLE__
/* Darwin reserves [64,448) GiB even under Rosetta. Keep guest mappings above
 * that aperture and the host allocator, while retaining 40-bit GPU pointers. */
#define USER_MIN UINT64_C(0x8000000000)
#else
#define USER_MIN UINT64_C(0x1000000000)
#endif
#define USER_MAX UINT64_C(0xfc00000000)
#define LIMIT 4096
#define INVALID ((int32_t)UINT32_C(0x80020016))
#define NO_MEMORY ((int32_t)UINT32_C(0x8002000c))
#define ACCESS ((int32_t)UINT32_C(0x8002000d))
#define NO_SPACE ((int32_t)UINT32_C(0x80020023))
#define MAP_FIXED_FLAG 0x10
#define MAP_NO_OVERWRITE 0x80
enum { KIND_RESERVED=1, KIND_DIRECT, KIND_FLEXIBLE };
typedef struct { uint64_t start, size; int type, used; } Block;
typedef struct { uintptr_t start, end; int kind, prot, type; uint64_t phys, identity; } Vma;
static uint64_t next_mapping_identity;
typedef struct {
    uintptr_t start, end; uint64_t offset; int32_t protection, memory_type;
    uint32_t flags; char name[32];
} VirtualQueryInfo; /* flags: flexible, direct, stack, pooled, committed */
typedef struct { void *start; uint64_t offset, length; int8_t prot, type; int16_t reserved; int32_t operation; } BatchEntry;
_Static_assert(sizeof(VirtualQueryInfo)==72,"PS4 virtual query info layout");
_Static_assert(sizeof(BatchEntry)==32,"PS4 batch map entry layout");

/* Region table lock. Lookups (GPU per-draw queries, VirtualQuery, GPU page
 * protection) share it; map/unmap/protect take it exclusively. Both nest: code
 * under the lock may write guest memory, and a GPU-tracked page then faults into
 * runtime_memory_gpu_protect on the same thread. Readers nest because glibc's
 * default rwlock prefers readers; a thread holding it exclusively skips relocking. */
static pthread_rwlock_t lock=PTHREAD_RWLOCK_INITIALIZER;
static __thread unsigned exclusive_depth;
/* Odd while a writer holds the lock; readers' region caches are valid for one even value. */
static uint64_t table_generation;
static void write_lock(void) {
    if (!exclusive_depth++) { pthread_rwlock_wrlock(&lock); __atomic_add_fetch(&table_generation,1,__ATOMIC_ACQ_REL); }
}
static void write_unlock(void) {
    if (!--exclusive_depth) { __atomic_add_fetch(&table_generation,1,__ATOMIC_RELEASE); pthread_rwlock_unlock(&lock); }
}
static void read_lock(void) { if (!exclusive_depth) pthread_rwlock_rdlock(&lock); }
static void read_unlock(void) { if (!exclusive_depth) pthread_rwlock_unlock(&lock); }
typedef struct {
    void *context; BbGpuMemoryMutationBegin begin; BbGpuMemoryMutationEnd end;
} MutationHooks;
static MutationHooks mutation_hooks;
static pthread_mutex_t mutation_hooks_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mutation_hooks_changed=PTHREAD_COND_INITIALIZER;
static unsigned mutation_users,changing_mutation_hooks;
static __thread unsigned mutation_depth;
static __thread MutationHooks active_mutation_hooks;
int runtime_gpu_memory_set_mutation_hooks(void *context,
    BbGpuMemoryMutationBegin begin,BbGpuMemoryMutationEnd end) {
    if (mutation_depth || !!begin!=!!end) return 0;
    pthread_mutex_lock(&mutation_hooks_lock);
    while (changing_mutation_hooks) pthread_cond_wait(&mutation_hooks_changed,&mutation_hooks_lock);
    changing_mutation_hooks=1;
    while (mutation_users) pthread_cond_wait(&mutation_hooks_changed,&mutation_hooks_lock);
    mutation_hooks=(MutationHooks){context,begin,end};
    changing_mutation_hooks=0;
    pthread_cond_broadcast(&mutation_hooks_changed);
    pthread_mutex_unlock(&mutation_hooks_lock);return 1;
}
static void release_mutation_user(void) {
    pthread_mutex_lock(&mutation_hooks_lock);
    if (!--mutation_users) pthread_cond_broadcast(&mutation_hooks_changed);
    pthread_mutex_unlock(&mutation_hooks_lock);
}
static int begin_memory_mutation(void) {
    if (mutation_depth) { ++mutation_depth;return 1; }
    pthread_mutex_lock(&mutation_hooks_lock);
    while (changing_mutation_hooks) pthread_cond_wait(&mutation_hooks_changed,&mutation_hooks_lock);
    active_mutation_hooks=mutation_hooks;++mutation_users;
    pthread_mutex_unlock(&mutation_hooks_lock);
    /* Never hold the registration mutex while acquiring a renderer lock.
     * Lazy renderer arena allocation can itself enter a mapping operation. */
    mutation_depth=1;
    if (active_mutation_hooks.begin && !active_mutation_hooks.begin(active_mutation_hooks.context)) {
        mutation_depth=0;active_mutation_hooks=(MutationHooks){0};release_mutation_user();return 0;
    }
    return 1;
}
static void end_memory_mutation(void) {
    if (mutation_depth>1) { --mutation_depth;return; }
    /* Keep depth nonzero through end, so a callback's nested mapping operation
     * cannot wait for its own registration lease. */
    if (active_mutation_hooks.end) active_mutation_hooks.end(active_mutation_hooks.context);
    mutation_depth=0;active_mutation_hooks=(MutationHooks){0};release_mutation_user();
}
static Block blocks[LIMIT];
static Vma *vmas; static size_t vma_count, vma_capacity;
#ifndef _WIN32
static int pool_fd=-1;
#endif
static unsigned char *backing_base; /* second view of the pool: host writes bypass guest/GPU page protection */
#define FLEX_SPAN (UINT64_C(1024) * 1024 * 1024)
static uint64_t flex_bitmap[FLEX_SPAN/PAGE/64];
/* GPU hooks (bbgpu): notified outside the lock, in order, after each operation. */
typedef void (*GpuRange)(uintptr_t address, uint64_t size);
static GpuRange hook_map, hook_unmap, hook_invalidate;
enum { HOOK_MAP, HOOK_UNMAP, HOOK_INVALIDATE };
typedef struct { int kind; uintptr_t address; uint64_t size; } PendingHook;
static PendingHook pending[64]; static size_t pending_count;
static size_t allocations, maps, flexible_maps, protects, queries;
static uint64_t live_bytes, flexible_bytes;

static int valid_alignment(uint64_t a) { return a >= PAGE && a <= POOL_SIZE && !(a & (a-1)); }
static uint64_t align_up(uint64_t n, uint64_t a) { return (n+a-1) & ~(a-1); }
static int host_prot(int prot) {
    /* GPU read/write bits (0x10/0x20) need host access for the future GPU backend. */
    return ((prot & 0x11) ? PROT_READ : 0) | ((prot & 0x22) ? PROT_WRITE|PROT_READ : 0) | ((prot & 4) ? PROT_EXEC|PROT_READ : 0);
}
#ifdef _WIN32
/* ---- host mappings (Windows) ----
 * [USER_MIN,USER_MAX) is covered by regions, each one placeholder or one view of the
 * section. Mapping a range carves it out as a single placeholder first: views overlapping
 * it are unmapped, placeholders coalesced and split, and the parts of views outside the
 * range mapped again with the page protections they had (GPU page tracking included). */
typedef struct { uintptr_t start, end; uint64_t phys; int mapped; } HostRegion;
typedef struct { uintptr_t start, end; DWORD protect; } ProtRun;
static HostRegion *regions; static size_t region_count, region_capacity;
static HANDLE section;
static DWORD win_prot(int prot) {
    if (prot & PROT_EXEC) return (prot & PROT_WRITE) ? PAGE_EXECUTE_READWRITE : (prot & PROT_READ) ? PAGE_EXECUTE_READ : PAGE_EXECUTE;
    if (prot & PROT_WRITE) return PAGE_READWRITE;
    return (prot & PROT_READ) ? PAGE_READONLY : PAGE_NOACCESS;
}
static void win_fail(const char *what, uintptr_t address, uint64_t size) {
    fprintf(stderr,"STOP: %s failed at %p (+0x%" PRIx64 "), error %lu\n",what,(void *)address,size,GetLastError());
    exit(21);
}
static size_t region_index(uintptr_t a) { /* first region with end > a */
    size_t lo=0, hi=region_count;
    while (lo<hi) { size_t mid=(lo+hi)/2; if (regions[mid].end<=a) lo=mid+1; else hi=mid; }
    return lo;
}
static void region_insert(size_t at, HostRegion r) {
    if (region_count==region_capacity) {
        size_t capacity=region_capacity ? region_capacity*2 : 256;
        HostRegion *next=realloc(regions,capacity*sizeof(*regions));
        if (!next) { fputs("Out of memory (host regions)\n",stderr); exit(1); }
        regions=next; region_capacity=capacity;
    }
    memmove(regions+at+1,regions+at,(region_count-at)*sizeof(*regions));
    regions[at]=r; ++region_count;
}
static void region_erase(size_t at, size_t n) {
    memmove(regions+at,regions+at+n,(region_count-at-n)*sizeof(*regions)); region_count-=n;
}
/* The guest range is reserved before anything else can be placed there (runtime_start). */
void runtime_memory_reserve(void) {
    if (region_count) return;
    if (!VirtualAlloc2(NULL,(void *)USER_MIN,USER_MAX-USER_MIN,MEM_RESERVE|MEM_RESERVE_PLACEHOLDER,PAGE_NOACCESS,NULL,0))
        win_fail("reserving the PS4 address range (link with --disable-high-entropy-va)",USER_MIN,USER_MAX-USER_MIN);
    region_insert(0,(HostRegion){USER_MIN,USER_MAX,0,0});
}
/* Guest memory committed only as the game maps it: the whole pool did not fit Windows' commit
 * limit (RAM + page file) at once (pool()). */
static int commit_on_map;
static void commit_backing(uint64_t phys, uint64_t size) {
    if (!commit_on_map || VirtualAlloc(backing_base+phys,size,MEM_COMMIT,PAGE_READWRITE)) return;
    fprintf(stderr,"Runtime: cannot commit %" PRIu64 " MiB more guest memory (error %lu): Windows' "
            "page file is too small for this resolution; let Windows manage its size, or use a "
            "lower output resolution\n",size>>20,GetLastError());
    win_fail("committing guest memory",(uintptr_t)(backing_base+phys),size);
}
static void view_map(const HostRegion *r, DWORD protect) {
    DWORD initial=protect==PAGE_NOACCESS ? PAGE_READWRITE : protect, old;
    commit_backing(r->phys,r->end-r->start);
    if (!MapViewOfFile3(section,GetCurrentProcess(),(void *)r->start,r->phys,r->end-r->start,MEM_REPLACE_PLACEHOLDER,initial,NULL,0))
        win_fail("MapViewOfFile3",r->start,r->end-r->start);
    if (initial!=protect && !VirtualProtect((void *)r->start,r->end-r->start,protect,&old))
        win_fail("VirtualProtect",r->start,r->end-r->start);
}
static size_t save_protections(uintptr_t start, uintptr_t end, ProtRun **runs, size_t count, size_t *capacity) {
    for (uintptr_t at=start; at<end;) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery((void *)at,&m,sizeof(m))) win_fail("VirtualQuery",at,0);
        uintptr_t next=(uintptr_t)m.BaseAddress+m.RegionSize;
        if (next>end) next=end;
        if (count==*capacity) { *capacity=*capacity ? *capacity*2 : 16; *runs=realloc(*runs,*capacity*sizeof(**runs)); }
        (*runs)[count++]=(ProtRun){at,next,m.Protect};
        at=next;
    }
    return count;
}
/* Leaves [a,b) as exactly one unmapped placeholder; returns its region index. */
static size_t carve_host(uintptr_t a, uintptr_t b) {
    size_t first=region_index(a), last=first;
    if (first==region_count || regions[first].start>a) win_fail("guest range outside the reservation",a,b-a);
    while (last<region_count && regions[last].start<b) ++last;
    if (regions[last-1].end<b) win_fail("guest range outside the reservation",a,b-a);
    uintptr_t lo=regions[first].start, hi=regions[last-1].end;
    HostRegion left=regions[first], right=regions[last-1];
    int keep_left=left.mapped && lo<a, keep_right=right.mapped && b<hi;
    if (first==last-1 && !left.mapped && lo==a && hi==b) return first;
    ProtRun *runs=NULL; size_t nruns=0, capacity=0, left_runs=0;
    if (keep_left) left_runs=nruns=save_protections(lo,a,&runs,nruns,&capacity);
    if (keep_right) nruns=save_protections(b,hi,&runs,nruns,&capacity);
    for (size_t i=first;i<last;++i) if (regions[i].mapped) {
        if (!UnmapViewOfFile2(GetCurrentProcess(),(void *)regions[i].start,MEM_PRESERVE_PLACEHOLDER))
            win_fail("UnmapViewOfFile2",regions[i].start,regions[i].end-regions[i].start);
    }
    if (last-first>1 && !VirtualFree((void *)lo,hi-lo,MEM_RELEASE|MEM_COALESCE_PLACEHOLDERS))
        win_fail("coalescing placeholders",lo,hi-lo);
    region_erase(first,last-first);
    region_insert(first,(HostRegion){lo,hi,0,0});
    size_t at=first;
    if (lo<a) {
        if (!VirtualFree((void *)lo,a-lo,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) win_fail("splitting a placeholder",lo,a-lo);
        regions[at].end=a;
        region_insert(at+1,(HostRegion){a,hi,0,0});
        if (keep_left) {
            regions[at].phys=left.phys; regions[at].mapped=1;
            view_map(&regions[at],PAGE_READWRITE);
        }
        ++at;
    }
    if (b<hi) {
        if (!VirtualFree((void *)a,b-a,MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)) win_fail("splitting a placeholder",a,b-a);
        regions[at].end=b;
        region_insert(at+1,(HostRegion){b,hi,0,0});
        if (keep_right) {
            regions[at+1].phys=right.phys+(b-right.start); regions[at+1].mapped=1;
            view_map(&regions[at+1],PAGE_READWRITE);
        }
    }
    for (size_t i=0;i<nruns;++i) {
        DWORD old;
        if (runs[i].protect!=PAGE_READWRITE && !VirtualProtect((void *)runs[i].start,runs[i].end-runs[i].start,runs[i].protect,&old))
            win_fail("restoring page protection",runs[i].start,runs[i].end-runs[i].start);
    }
    (void)left_runs;
    free(runs);
    return at;
}
static int host_map(uintptr_t address, uint64_t size, int prot, int reserved, uint64_t phys) {
    size_t i=carve_host(address,address+size);
    if (reserved) return 0;
    regions[i].phys=phys; regions[i].mapped=1;
    view_map(&regions[i],win_prot(prot));
    return 0;
}
static int host_unmap(uintptr_t address, uint64_t size) { carve_host(address,address+size); return 0; }
static int host_protect(uintptr_t address, uint64_t size, int prot) {
    DWORD protect=win_prot(prot), old;
    uintptr_t end=address+size;
    for (size_t i=region_index(address); i<region_count && regions[i].start<end; ++i) {
        if (!regions[i].mapped) continue;
        uintptr_t a=regions[i].start>address ? regions[i].start : address, b=regions[i].end<end ? regions[i].end : end;
        if (!VirtualProtect((void *)a,b-a,protect,&old)) return -1;
    }
    return 0;
}
/* Released pages read as zero when reused, like the memfd's punched holes. */
static void host_discard(uint64_t phys, uint64_t size) { commit_backing(phys,size); memset(backing_base+phys,0,size); }
/* Direct memory occupies [0,POOL_SIZE) of the section, flexible memory [POOL_SIZE,+FLEX_SPAN). */
static int pool(void) {
    if (backing_base) return 0;
    runtime_memory_reserve();
    uint64_t bytes=POOL_SIZE+FLEX_SPAN;
    /* BB_GUEST_COMMIT_ON_MAP=1: the fallback below from the start (tests). */
    const char *on_map=getenv("BB_GUEST_COMMIT_ON_MAP");
    if (on_map && on_map[0]=='1') SetLastError(ERROR_COMMITMENT_LIMIT);
    else section=CreateFileMapping2(INVALID_HANDLE_VALUE,NULL,FILE_MAP_ALL_ACCESS,PAGE_EXECUTE_READWRITE,SEC_COMMIT,bytes,NULL,NULL,0);
    if (!section && (GetLastError()==ERROR_NO_SYSTEM_RESOURCES || GetLastError()==ERROR_COMMITMENT_LIMIT)) {
        /* The pool is sized for the largest resolutions (BB_DMEM_MB, 12+ GiB) and committed at
         * once it exceeded RAM + page file (error 1450) although the game maps much less: then
         * it is reserved and committed as the game maps it (commit_backing). */
        DWORD error=GetLastError();
        section=CreateFileMapping2(INVALID_HANDLE_VALUE,NULL,FILE_MAP_ALL_ACCESS,PAGE_EXECUTE_READWRITE,SEC_RESERVE,bytes,NULL,NULL,0);
        if (section) {
            commit_on_map=1;
            printf("Runtime: %" PRIu64 " MiB of guest memory do not fit Windows' commit limit at once "
                   "(error %lu): committed as the game maps it\n",bytes>>20,error);
        }
    }
    if (!section) { fprintf(stderr,"Runtime: cannot create %" PRIu64 " MiB of guest memory (error %lu); is the page file large enough?\n",bytes>>20,GetLastError()); return -1; }
    backing_base=MapViewOfFile3(section,GetCurrentProcess(),NULL,0,bytes,0,PAGE_READWRITE,NULL,0);
    return backing_base ? 0 : -1;
}
#else
/* Direct memory occupies [0,POOL_SIZE) of the memfd, flexible memory [POOL_SIZE,+FLEX_SPAN). */
#ifdef __APPLE__
/* Darwin has no memfd_create. An immediately unlinked, sparse temporary file
 * provides the same aliasing semantics; only committed guest pages consume
 * storage. Never create anything in the game or save directories. */
static int darwin_pool_fd(void) {
    const char *directory=getenv("TMPDIR");
    if (!directory || !*directory) directory="/tmp";
    char path[PATH_MAX];
    if (snprintf(path,sizeof(path),"%s/bbport-memory-XXXXXX",directory)>=(int)sizeof(path)) {
        errno=ENAMETOOLONG; return -1;
    }
    int fd=mkstemp(path);
    if (fd<0) return -1;
    int error=0;
    if (unlink(path)) error=errno;
    if (!error && fcntl(fd,F_SETFD,FD_CLOEXEC)<0) error=errno;
    if (error) { close(fd); errno=error; return -1; }
    return fd;
}
static pthread_once_t darwin_reserve_once=PTHREAD_ONCE_INIT;
static void darwin_reserve_range(void) {
    /* A hint never replaces an existing host mapping. MAP_FIXED is used only
     * inside this owned reservation when guest mappings are installed. */
    const uint64_t size=USER_MAX-USER_MIN;
    void *p=mmap((void *)USER_MIN,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if (p!=(void *)USER_MIN) {
        if (p!=MAP_FAILED) munmap(p,size);
        fprintf(stderr,"STOP: the PS4 virtual address range is unavailable on this macOS process (mapping=%p, errno=%d)\n",p,errno);
        exit(21);
    }
}
void runtime_memory_reserve(void) {
    pthread_once(&darwin_reserve_once,darwin_reserve_range);
}
#endif
static int pool(void) {
    if (pool_fd>=0) return 0;
#ifdef __APPLE__
    runtime_memory_reserve();
    pool_fd=darwin_pool_fd();
#else
    pool_fd=memfd_create("bb-guest-memory", MFD_CLOEXEC);
#endif
    if (pool_fd<0) return -1;
    if (ftruncate(pool_fd,(off_t)(POOL_SIZE+FLEX_SPAN))) {
        close(pool_fd); pool_fd=-1; return -1;
    }
    void *view=mmap(NULL,POOL_SIZE+FLEX_SPAN,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_NORESERVE,pool_fd,0);
    if (view==MAP_FAILED) { close(pool_fd); pool_fd=-1; return -1; }
    backing_base=view;
    return 0;
}
#ifndef __APPLE__
void runtime_memory_reserve(void) {}
#endif
static int host_map(uintptr_t address, uint64_t size, int prot, int reserved, uint64_t phys) {
#ifdef __APPLE__
    runtime_memory_reserve();
    if (address<USER_MIN || address>=USER_MAX || size>USER_MAX-address) return -1;
#endif
    void *mapped = !reserved
        ? mmap((void *)address,size,prot,MAP_SHARED|MAP_FIXED,pool_fd,(off_t)phys)
        : mmap((void *)address,size,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_FIXED,-1,0);
    return mapped==MAP_FAILED ? -1 : 0;
}
static int host_unmap(uintptr_t address, uint64_t size) {
#ifdef __APPLE__
    if (address<USER_MIN || address>=USER_MAX || size>USER_MAX-address) return -1;
    /* Keep ownership after unmapping so a host allocation cannot enter the
     * address range before the next fixed guest mapping. */
    return mmap((void *)address,size,PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|MAP_FIXED,-1,0)==MAP_FAILED ? -1 : 0;
#else
    return munmap((void *)address,size);
#endif
}
static int host_protect(uintptr_t address, uint64_t size, int prot) { return mprotect((void *)address,size,prot); }
static void host_discard(uint64_t phys, uint64_t size) {
#ifdef __APPLE__
    struct fpunchhole hole={.fp_offset=(off_t)phys,.fp_length=(off_t)size};
    if (fcntl(pool_fd,F_PUNCHHOLE,&hole)) {
        /* A non-APFS temporary volume may not implement hole punching. Reuse
         * must still return zeroed pages, including through every alias. */
        memset(backing_base+phys,0,size);
    }
#else
    fallocate(pool_fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,(off_t)phys,(off_t)size);
#endif
}
#endif
static void queue_hook(int kind, uintptr_t address, uint64_t size) {
    GpuRange hook = kind==HOOK_MAP ? hook_map : kind==HOOK_UNMAP ? hook_unmap : hook_invalidate;
    if (!hook) return;
    if (pending_count==sizeof(pending)/sizeof(*pending)) { fputs("STOP: GPU hook queue overflow\n",stderr); exit(21); }
    pending[pending_count++]=(PendingHook){kind,address,size};
}
/* Runs queued GPU notifications; callers invoke it after dropping the lock. */
static void flush_hooks(void) {
    for (;;) {
        write_lock();
        if (!pending_count) { write_unlock(); return; }
        PendingHook h=pending[0];
        memmove(pending,pending+1,--pending_count*sizeof(*pending));
        GpuRange hook = h.kind==HOOK_MAP ? hook_map : h.kind==HOOK_UNMAP ? hook_unmap : hook_invalidate;
        write_unlock();
        if (hook) hook(h.address,h.size);
    }
}
static int flex_test(uint64_t page) { return (int)((flex_bitmap[page/64]>>(page%64))&1); }
static void flex_set(uint64_t first, uint64_t count, int used) {
    for (uint64_t p=first;p<first+count;++p) {
        if (used) flex_bitmap[p/64]|=UINT64_C(1)<<(p%64); else flex_bitmap[p/64]&=~(UINT64_C(1)<<(p%64));
    }
}
static uint64_t flex_alloc(uint64_t size) {
    uint64_t pages=size/PAGE, total=FLEX_SPAN/PAGE, run=0;
    for (uint64_t p=0;p<total;++p) {
        run = flex_test(p) ? 0 : run+1;
        if (run==pages) { flex_set(p+1-pages,pages,1); return POOL_SIZE+(p+1-pages)*PAGE; }
    }
    return UINT64_MAX;
}
static void flex_free(uint64_t phys, uint64_t size) {
    flex_set((phys-POOL_SIZE)/PAGE,size/PAGE,0);
    host_discard(phys,size);
}
static size_t vma_index(uintptr_t a) { /* first VMA with end > a */
    size_t lo=0, hi=vma_count;
    while (lo<hi) { size_t mid=(lo+hi)/2; if (vmas[mid].end<=a) lo=mid+1; else hi=mid; }
    return lo;
}
static int vma_insert(size_t at, Vma v) {
    if (vma_count==vma_capacity) {
        size_t capacity=vma_capacity ? vma_capacity*2 : 256;
        Vma *next=realloc(vmas,capacity*sizeof(*vmas));
        if (!next) return -1;
        vmas=next; vma_capacity=capacity;
    }
    memmove(vmas+at+1,vmas+at,(vma_count-at)*sizeof(*vmas));
    vmas[at]=v; ++vma_count; return 0;
}
static void vma_erase(size_t at, size_t n) {
    memmove(vmas+at,vmas+at+n,(vma_count-at-n)*sizeof(*vmas)); vma_count-=n;
}
static int split_at(uintptr_t a) {
    size_t i=vma_index(a);
    if (i==vma_count || vmas[i].start>=a) return 0;
    Vma right=vmas[i];
    right.start=a;
    /* Flexible mappings also occupy distinct physical pages in the backing
     * pool. Losing this offset makes a later partial unmap discard/reuse the
     * first page of the original allocation while it is still live. */
    if (right.kind!=KIND_RESERVED) right.phys+=a-vmas[i].start;
    vmas[i].end=a;
    return vma_insert(i+1,right);
}
/* Range [start,end) split on its edges; returns the first index inside it. */
static size_t carve(uintptr_t start, uintptr_t end, int *error) {
    *error = split_at(start) || split_at(end);
    return vma_index(start);
}
static int covered(uintptr_t start, uintptr_t end, int allow_reserved) {
    size_t i=vma_index(start); uintptr_t at=start;
    for (; at<end; ++i) {
        if (i==vma_count || vmas[i].start>at || (!allow_reserved && vmas[i].kind==KIND_RESERVED)) return 0;
        at=vmas[i].end;
    }
    return 1;
}
static int overlaps(uintptr_t start, uintptr_t end, int ignore_reserved) {
    for (size_t i=vma_index(start); i<vma_count && vmas[i].start<end; ++i)
        if (!ignore_reserved || vmas[i].kind!=KIND_RESERVED) return 1;
    return 0;
}
/* First-fit search in the PS4 user range, starting from the hint. */
static uintptr_t find_free(uintptr_t hint, uint64_t size, uint64_t alignment) {
    uintptr_t at=align_up(hint<USER_MIN ? USER_MIN : hint, alignment);
    for (size_t i=vma_index(at); at+size<=USER_MAX; ++i) {
        if (i==vma_count || vmas[i].start>=at+size) return at;
        at=align_up(vmas[i].end,alignment);
    }
    return 0;
}
static void drop_range(uintptr_t start, uintptr_t end) {
    int error; size_t i=carve(start,end,&error), n=0;
    while (i+n<vma_count && vmas[i+n].end<=end && vmas[i+n].start>=start) {
        if (vmas[i+n].kind==KIND_FLEXIBLE) {
            flexible_bytes-=vmas[i+n].end-vmas[i+n].start;
            flex_free(vmas[i+n].phys,vmas[i+n].end-vmas[i+n].start);
        }
        ++n;
    }
    vma_erase(i,n);
}
/* Places a host mapping; with MAP_FIXED it replaces what the guest had there. */
static int32_t place(void **inout, uint64_t size, int prot, int flags, uint64_t alignment,
                     int kind, int type, uint64_t phys) {
    uintptr_t address=(uintptr_t)*inout;
    if (flags & MAP_FIXED_FLAG) {
        if (!address || address%PAGE) return INVALID;
        if ((flags & MAP_NO_OVERWRITE) && overlaps(address,address+size,1)) return NO_MEMORY;
    } else {
        address=find_free(address,size,alignment);
        if (!address) return NO_MEMORY;
    }
    if (host_map(address,size,host_prot(prot),kind==KIND_RESERVED,phys)) return NO_MEMORY;
    void *mapped=(void *)address;
    drop_range(address,address+size);
    if (vma_insert(vma_index(address),(Vma){address,address+size,kind,prot,type,phys,++next_mapping_identity})) return NO_MEMORY;
    if (kind==KIND_FLEXIBLE) flexible_bytes+=size;
    if (kind!=KIND_RESERVED) queue_hook(HOOK_MAP,address,size);
    *inout=mapped;
    return 0;
}
static int block_type(uint64_t phys) {
    for (int i=0;i<LIMIT;++i)
        if (blocks[i].used && phys>=blocks[i].start && phys-blocks[i].start<blocks[i].size) return blocks[i].type;
    return -1;
}
static int direct_range_allocated(uint64_t phys, uint64_t size) {
    for (uint64_t at=phys; at<phys+size;) {
        int found=0;
        for (int i=0;i<LIMIT;++i) if (blocks[i].used && at>=blocks[i].start && at-blocks[i].start<blocks[i].size) {
            at=blocks[i].start+blocks[i].size; found=1; break;
        }
        if (!found) return 0;
    }
    return 1;
}

static ABI uint64_t direct_size(void) { return POOL_SIZE; }
static ABI int32_t direct_allocate(int64_t low, int64_t high, uint64_t size,
                                  uint64_t alignment, int type, int64_t *out) {
    if (!alignment) alignment = PAGE;
    if (!out || low < 0 || high < 0 || !size || size % PAGE || !valid_alignment(alignment) || type < 0 || type > 10)
        return INVALID;
    uint64_t end = (uint64_t)high < POOL_SIZE ? (uint64_t)high : POOL_SIZE;
    if ((uint64_t)low >= end || size > end - (uint64_t)low) return NO_SPACE;
    write_lock();
    if (pool()) { write_unlock(); return NO_MEMORY; }
    uint64_t start = align_up((uint64_t)low, alignment);
    int slot = -1;
    for (int i=0; i<LIMIT; ++i) if (!blocks[i].used) { slot=i; break; }
    for (;;) {
        if (slot<0 || start > end || size > end-start) { write_unlock(); return NO_SPACE; }
        int overlap = 0;
        for (int i=0; i<LIMIT; ++i) {
            Block b = blocks[i];
            if (b.used && start < b.start+b.size && b.start < start+size) {
                start = align_up(b.start+b.size, alignment); overlap = 1; break;
            }
        }
        if (!overlap) break;
    }
    blocks[slot] = (Block){start, size, type, 1}; *out = (int64_t)start;
    ++allocations; live_bytes += size;
    write_unlock();
    printf("Runtime: direct allocation offset=0x%" PRIx64 ", size=%" PRIu64 " bytes, type=%d\n", start, size, type);
    return 0;
}
static int32_t map_direct_locked(void **out, uint64_t size, int prot, int flags, int64_t physical, uint64_t alignment) {
    if (!alignment) alignment=PAGE;
    if (!out || physical < 0 || (uint64_t)physical % PAGE || !size || size % PAGE || !valid_alignment(alignment) ||
        (prot & ~0x37)) return INVALID;
    if (!direct_range_allocated((uint64_t)physical,size)) return INVALID;
    int32_t result=place(out,size,prot,flags,alignment,KIND_DIRECT,block_type((uint64_t)physical),(uint64_t)physical);
    if (!result) ++maps;
    return result;
}
static ABI int32_t direct_map(void **out, uint64_t size, int prot, int flags, int64_t physical, uint64_t alignment) {
    if (!begin_memory_mutation()) return ACCESS;
    write_lock();
    int32_t result=map_direct_locked(out,size,prot,flags,physical,alignment);
    write_unlock();
    flush_hooks();
    end_memory_mutation();
    if (!result) printf("Runtime: mapped direct memory %p, size=%" PRIu64 ", prot=0x%x\n", *out, size, prot);
    return result;
}
static ABI int32_t direct_map_named(void **out, uint64_t size, int prot, int flags, int64_t physical,
                                    uint64_t alignment, const char *name) {
    (void)name; return direct_map(out,size,prot,flags,physical,alignment);
}
static int32_t unmap_locked(uintptr_t start, uint64_t size) {
    if (start%PAGE || !size) return INVALID;
    uint64_t end=start+align_up(size,PAGE);
    for (size_t i=vma_index(start); i<vma_count && vmas[i].start<end; ++i)
        if (vmas[i].kind!=KIND_RESERVED) {
            uintptr_t a=vmas[i].start>start ? vmas[i].start : start, b=vmas[i].end<end ? vmas[i].end : end;
            queue_hook(HOOK_UNMAP,a,b-a);
        }
    for (size_t i=vma_index(start); i<vma_count && vmas[i].start<end; ++i) {
        uintptr_t a=vmas[i].start>start ? vmas[i].start : start, b=vmas[i].end<end ? vmas[i].end : end;
        if (host_unmap(a,b-a)) return INVALID;
    }
    drop_range(start,end);
    return 0;
}
static ABI int32_t direct_unmap(void *address, uint64_t size) {
    if (!begin_memory_mutation()) return ACCESS;
    /* The GPU forgets the range before it disappears (as shadPS4's early GPU unmap). */
    if (hook_unmap && !((uintptr_t)address%PAGE) && size) hook_unmap((uintptr_t)address,align_up(size,PAGE));
    write_lock();
    size_t before=pending_count;
    int32_t result=unmap_locked((uintptr_t)address,size);
    pending_count=before; /* already notified above */
    write_unlock();
    end_memory_mutation();
    return result;
}
static ABI int32_t direct_release(uint64_t start, uint64_t size) {
    if (start % PAGE || size % PAGE) return INVALID;
    if (!size) return 0;
    if (!begin_memory_mutation()) return ACCESS;
    write_lock();
    if (!direct_range_allocated(start,size)) { write_unlock();end_memory_mutation();return INVALID; }
    /* Mappings of released physical pages disappear with them. */
    for (size_t i=0; i<vma_count;) {
        Vma v=vmas[i];
        uint64_t length=v.end-v.start;
        if (v.kind==KIND_DIRECT && v.phys<start+size && start<v.phys+length) {
            uint64_t a=v.phys>start ? v.phys : start, b=v.phys+length<start+size ? v.phys+length : start+size;
            unmap_locked(v.start+(a-v.phys),b-a);
            i=vma_index(v.start);
            continue;
        }
        ++i;
    }
    for (int i=0; i<LIMIT; ++i) {
        Block *b=&blocks[i];
        if (!b->used || b->start>=start+size || start>=b->start+b->size) continue;
        uint64_t a=b->start>start ? b->start : start, e=b->start+b->size<start+size ? b->start+b->size : start+size;
        Block left=*b, right=*b;
        left.size=a-b->start; right.start=e; right.size=b->start+b->size-e;
        b->used=0;
        if (left.size) *b=left;
        if (right.size) for (int j=0;j<LIMIT;++j) if (!blocks[j].used) { blocks[j]=right; break; }
        live_bytes-=e-a;
    }
    host_discard(start,size); /* zero on reuse */
    write_unlock();
    flush_hooks();
    end_memory_mutation();
    return 0;
}
static int32_t map_flexible_locked(void **inout, uint64_t size, int prot, int flags) {
    if (!inout || !size || size%PAGE || (prot & ~0x37)) return INVALID;
    if (flexible_bytes+size>FLEXIBLE_SIZE || pool()) return NO_MEMORY;
    uint64_t phys=flex_alloc(size);
    if (phys==UINT64_MAX) return NO_MEMORY;
    int32_t result=place(inout,size,prot,flags,PAGE,KIND_FLEXIBLE,0,phys);
    if (result) flex_free(phys,size); else ++flexible_maps;
    return result;
}
static ABI int32_t map_flexible(void **inout, uint64_t size, int prot, int flags) {
    if (!begin_memory_mutation()) return ACCESS;
    write_lock();
    int32_t result=map_flexible_locked(inout,size,prot,flags);
    write_unlock();
    flush_hooks();
    end_memory_mutation();
    if (!result) printf("Runtime: mapped flexible memory %p, size=%" PRIu64 ", prot=0x%x\n", *inout, size, prot);
    else fprintf(stderr,"Runtime: flexible map of %" PRIu64 " bytes failed (0x%x), in use=%" PRIu64 "\n",
                 size, (unsigned)result, flexible_bytes);
    return result;
}
static ABI int32_t map_flexible_named(void **inout, uint64_t size, int prot, int flags, const char *name) {
    (void)name; return map_flexible(inout,size,prot,flags);
}
static ABI int32_t release_flexible(void *address, uint64_t size) { return direct_unmap(address,size); }
/* FreeBSD/PS4 flags are guest constants, not Darwin or Linux MAP_* values.
 * libc uses anonymous mmap for heap growth. Keep these allocations in the
 * tracked guest flexible pool so munmap, protection and GPU queries agree. */
static int32_t map_anonymous(void **out, uint64_t size, int prot, int flags,
                             int fd, int64_t offset) {
    const unsigned shared=1, private_map=2, anonymous=0x1000, no_core=0x20000;
    const unsigned allowed=shared|private_map|anonymous|no_core|MAP_FIXED_FLAG|MAP_NO_OVERWRITE;
    const uintptr_t address=out ? (uintptr_t)*out : 0;
    if (!out || !size || size>USER_MAX-USER_MIN || (prot&~7) ||
        ((unsigned)flags&~allowed) ||
        ((flags&(shared|private_map))!=shared && (flags&(shared|private_map))!=private_map))
        return INVALID;
    if (!(flags&anonymous)) return (int32_t)UINT32_C(0x80020013); /* ENODEV: file mappings */
    if (fd!=-1 || offset) return INVALID;
    size=align_up(size,PAGE);
    if ((flags&MAP_FIXED_FLAG) &&
        (address<USER_MIN || address%PAGE || address>=USER_MAX || size>USER_MAX-address))
        return INVALID;
    return map_flexible(out,size,prot,flags&(MAP_FIXED_FLAG|MAP_NO_OVERWRITE));
}
static ABI int32_t kernel_mmap(void *address, uint64_t size, int prot, int flags,
                              int fd, int64_t offset, void **out) {
    if (!out) return INVALID;
    void *mapped=address;
    int32_t result=map_anonymous(&mapped,size,prot,flags,fd,offset);
    if (!result) *out=mapped;
    return result;
}
static ABI void *posix_mmap(void *address, uint64_t size, int prot, int flags,
                           int fd, int64_t offset) {
    int32_t result=map_anonymous(&address,size,prot,flags,fd,offset);
    if (!result) return address;
    *runtime_errno()=(uint32_t)result&0xffff;
    return (void *)(uintptr_t)UINTPTR_MAX;
}
static ABI int32_t posix_munmap(void *address, uint64_t size) {
    const uintptr_t start=(uintptr_t)address;
    int32_t result=start<USER_MIN || start>=USER_MAX || !size || size>USER_MAX-start
        ? INVALID : direct_unmap(address,size);
    if (!result) return 0;
    *runtime_errno()=(uint32_t)result&0xffff;
    return -1;
}
static ABI int32_t reserve_range(void **inout, uint64_t size, int flags, uint64_t alignment) {
    if (!alignment) alignment=PAGE;
    if (!inout || !size || size%PAGE || !valid_alignment(alignment)) return INVALID;
    if (!begin_memory_mutation()) return ACCESS;
    write_lock();
    if ((flags & MAP_FIXED_FLAG) && overlaps((uintptr_t)*inout,(uintptr_t)*inout+size,0)) {
        write_unlock();end_memory_mutation();return NO_MEMORY;
    }
    int32_t result=place(inout,size,0,flags,alignment,KIND_RESERVED,0,0);
    write_unlock();
    end_memory_mutation();
    if (!result) printf("Runtime: reserved virtual range %p, size=%" PRIu64 "\n", *inout, size);
    return result;
}
static int32_t protect_locked(uintptr_t start, uint64_t size, int prot, int type) {
    uintptr_t end=start+align_up(size,PAGE);
    start&=~(uintptr_t)(PAGE-1);
    if ((prot & ~0x37) || !covered(start,end,0)) return INVALID;
    if (host_protect(start,end-start,host_prot(prot))) return INVALID;
    int error; size_t i=carve(start,end,&error);
    if (error) return NO_MEMORY;
    for (; i<vma_count && vmas[i].start<end; ++i) { vmas[i].prot=prot; if (type>=0) vmas[i].type=type; }
    queue_hook(HOOK_INVALIDATE,start,end-start);
    ++protects;
    return 0;
}
static ABI int32_t kernel_mprotect(const void *address, uint64_t size, int prot) {
    if (!begin_memory_mutation()) return ACCESS;
    write_lock();
    int32_t result=protect_locked((uintptr_t)address,size,prot,-1);
    write_unlock();
    flush_hooks();
    end_memory_mutation();
    return result;
}
static ABI int32_t query_protection(void *address, void **start, void **end, uint32_t *prot) {
    read_lock();
    size_t i=vma_index((uintptr_t)address);
    if (i==vma_count || vmas[i].start>(uintptr_t)address || vmas[i].kind==KIND_RESERVED) {
        read_unlock(); return ACCESS;
    }
    /* Report the whole contiguous run with identical protection. */
    size_t first=i, last=i;
    while (first && vmas[first-1].end==vmas[first].start && vmas[first-1].kind!=KIND_RESERVED && vmas[first-1].prot==vmas[i].prot) --first;
    while (last+1<vma_count && vmas[last+1].start==vmas[last].end && vmas[last+1].kind!=KIND_RESERVED && vmas[last+1].prot==vmas[i].prot) ++last;
    if (start) *start=(void *)vmas[first].start;
    if (end) *end=(void *)vmas[last].end;
    if (prot) *prot=(uint32_t)vmas[i].prot;
    __atomic_add_fetch(&queries,1,__ATOMIC_RELAXED);
    read_unlock();
    return 0;
}
static ABI int32_t virtual_query(const void *address, int flags, VirtualQueryInfo *info, uint64_t info_size) {
    if (!info || info_size!=sizeof(*info)) return INVALID;
    read_lock();
    size_t i=vma_index((uintptr_t)address);
    if (i==vma_count || (vmas[i].start>(uintptr_t)address && !(flags & 1))) { read_unlock(); return ACCESS; }
    Vma v=vmas[i];
    memset(info,0,sizeof(*info));
    info->start=v.start; info->end=v.end;
    info->offset=v.kind==KIND_DIRECT ? v.phys : 0;
    info->protection=v.prot; info->memory_type=v.type;
    info->flags=(v.kind==KIND_FLEXIBLE ? 1u : 0) | (v.kind==KIND_DIRECT ? 2u : 0) | (v.kind!=KIND_RESERVED ? 16u : 0);
    strcpy(info->name, v.kind==KIND_RESERVED ? "reserved" : v.kind==KIND_DIRECT ? "direct" : "flexible");
    __atomic_add_fetch(&queries,1,__ATOMIC_RELAXED);
    read_unlock();
    return 0;
}
static ABI int32_t direct_memory_type(uint64_t phys, int *type, void **start, void **end) {
    read_lock();
    for (int i=0;i<LIMIT;++i) if (blocks[i].used && phys>=blocks[i].start && phys-blocks[i].start<blocks[i].size) {
        if (type) *type=blocks[i].type;
        if (start) *start=(void *)(uintptr_t)blocks[i].start;
        if (end) *end=(void *)(uintptr_t)(blocks[i].start+blocks[i].size);
        read_unlock(); return 0;
    }
    read_unlock();
    return ACCESS;
}
static ABI int32_t batch_map2(BatchEntry *entries, int count, int *processed, int flags) {
    if (!entries || count<0) return INVALID;
    int32_t result=0; int done=0;
    if (!begin_memory_mutation()) {if (processed) *processed=0;return ACCESS;}
    write_lock();
    for (; done<count; ++done) {
        BatchEntry *e=&entries[done];
        switch (e->operation) {
        case 0: result=map_direct_locked(&e->start,e->length,(uint8_t)e->prot,flags,(int64_t)e->offset,0); break;
        case 1: result=unmap_locked((uintptr_t)e->start,e->length); break;
        case 2: result=protect_locked((uintptr_t)e->start,e->length,(uint8_t)e->prot,-1); break;
        case 3: result=map_flexible_locked(&e->start,e->length,(uint8_t)e->prot,flags); break;
        case 4: result=protect_locked((uintptr_t)e->start,e->length,(uint8_t)e->prot,e->type); break;
        default: result=INVALID;
        }
        if (result) break;
    }
    write_unlock();
    flush_hooks();
    end_memory_mutation();
    if (processed) *processed=done;
    return result;
}
static ABI int32_t batch_map(BatchEntry *entries, int count, int *processed) {
    return batch_map2(entries,count,processed,MAP_FIXED_FLAG);
}
static const RuntimeExport exports[]={
    {"sceKernelGetDirectMemorySize",direct_size}, {"sceKernelAllocateDirectMemory",direct_allocate},
    {"sceKernelMapDirectMemory",direct_map}, {"sceKernelMapNamedDirectMemory",direct_map_named},
    {"sceKernelReleaseDirectMemory",direct_release}, {"sceKernelMunmap",direct_unmap}, {"munmap",posix_munmap},
    {"sceKernelMmap",kernel_mmap}, {"mmap",posix_mmap},
    {"sceKernelMapFlexibleMemory",map_flexible}, {"sceKernelMapNamedFlexibleMemory",map_flexible_named},
    {"sceKernelReleaseFlexibleMemory",release_flexible}, {"sceKernelReserveVirtualRange",reserve_range},
    {"sceKernelMprotect",kernel_mprotect}, {"sceKernelQueryMemoryProtection",query_protection},
    {"sceKernelVirtualQuery",virtual_query}, {"sceKernelGetDirectMemoryType",direct_memory_type},
    {"sceKernelBatchMap",batch_map}, {"sceKernelBatchMap2",batch_map2},
};
uintptr_t runtime_memory_resolve(const char *name) {
    /* Direct-memory NIDs are exercised by test_runtime.c before names are loaded. */
    if (!strcmp(name, "pO96TwzOm5E#p#J")) return (uintptr_t)direct_size;
    if (!strcmp(name, "rTXw65xmLIA#p#J")) return (uintptr_t)direct_allocate;
    if (!strcmp(name, "L-Q3LEjIbgA#p#J")) return (uintptr_t)direct_map;
    if (!strcmp(name, "MBuItvba6z8#p#J")) return (uintptr_t)direct_release;
    if (!strcmp(name, "cQke9UuBQOk#p#J")) return (uintptr_t)direct_unmap;
    if (!strcmp(name, "BPE9s9vQQXo#p#J")) return (uintptr_t)posix_mmap;
    return RUNTIME_LOOKUP(exports,name);
}
uint64_t runtime_gpu_memory_size(void) { return POOL_SIZE; }
void *runtime_gpu_memory_backing(void) {
    write_lock();
    int failed=pool();
    void *view=failed ? NULL : backing_base;
    write_unlock();
    return view;
}
uint64_t runtime_gpu_memory_generation(void) {
    return __atomic_load_n(&table_generation,__ATOMIC_ACQUIRE);
}
int runtime_gpu_label_capture(uintptr_t address,uint32_t bytes,BbGpuLabel *label) {
    if (!label || (bytes!=4 && bytes!=8) || address%bytes || address>UINTPTR_MAX-bytes) return 0;
    read_lock();
    size_t i=vma_index(address);
    int ok=i<vma_count && vmas[i].start<=address && bytes<=vmas[i].end-address &&
        vmas[i].kind!=KIND_RESERVED && (vmas[i].prot & 0x33);
    if (ok) *label=(BbGpuLabel){address,vmas[i].identity,bytes};
    read_unlock();return ok;
}
static int gpu_label_access(const BbGpuLabel *label,uint64_t *value,int store) {
    if (!label || !value || (label->bytes!=4 && label->bytes!=8) || label->address%label->bytes) return 0;
    read_lock();
    size_t i=vma_index(label->address);
    int ok=i<vma_count && vmas[i].start<=label->address && label->bytes<=vmas[i].end-label->address &&
        vmas[i].identity==label->identity && vmas[i].kind!=KIND_RESERVED &&
        (vmas[i].prot & (store ? 0x22 : 0x33));
    if (ok && backing_base) {
        unsigned char *p=backing_base+vmas[i].phys+(label->address-vmas[i].start);
        if (store) {
            if (label->bytes==8) __atomic_store_n((uint64_t*)p,*value,__ATOMIC_RELEASE);
            else __atomic_store_n((uint32_t*)p,(uint32_t)*value,__ATOMIC_RELEASE);
        } else *value=label->bytes==8 ? __atomic_load_n((uint64_t*)p,__ATOMIC_ACQUIRE) :
            __atomic_load_n((uint32_t*)p,__ATOMIC_ACQUIRE);
    } else ok=0;
    read_unlock();return ok;
}
int runtime_gpu_label_load(const BbGpuLabel *label,uint64_t *value) {return gpu_label_access(label,value,0);}
int runtime_gpu_label_store(const BbGpuLabel *label,uint64_t value) {return gpu_label_access(label,&value,1);}
int runtime_gpu_memory_snapshot(uintptr_t address,void *destination,size_t bytes) {
    const uintptr_t dst=(uintptr_t)destination;
    if (!address || !destination || !bytes || address>UINTPTR_MAX-bytes || dst>UINTPTR_MAX-bytes) return 0;
    const uintptr_t end=address+bytes,dst_end=dst+bytes;
    read_lock();
    int ok=backing_base!=NULL;
    /* The destination is uploader-owned host storage. Reject runtime aliases
     * so copying a segment cannot overwrite a later segment's source. */
    const uintptr_t backing=(uintptr_t)backing_base;
    if (ok && dst<backing+POOL_SIZE+FLEX_SPAN && dst_end>backing) ok=0;
    const size_t destination_vma=vma_index(dst);
    if (ok && destination_vma<vma_count && vmas[destination_vma].start<dst_end) ok=0;
    const size_t first=vma_index(address);
    uintptr_t at=address;
    for (size_t i=first;ok && at<end;++i) {
        if (i>=vma_count || vmas[i].start>at ||
            (vmas[i].kind!=KIND_DIRECT && vmas[i].kind!=KIND_FLEXIBLE) ||
            !(host_prot(vmas[i].prot)&PROT_READ)) {ok=0;break;}
        const uintptr_t part_end=vmas[i].end<end ? vmas[i].end : end;
        const uint64_t extent=vmas[i].kind==KIND_DIRECT ? POOL_SIZE : POOL_SIZE+FLEX_SPAN;
        if (vmas[i].phys>extent || at-vmas[i].start>extent-vmas[i].phys) {ok=0;break;}
        const uint64_t physical=vmas[i].phys+(at-vmas[i].start);
        if (physical>extent || part_end-at>extent-physical) {ok=0;break;}
        at=part_end;
    }
    if (ok) {
        at=address;
        for (size_t i=first;at<end;++i) {
            const uintptr_t part_end=vmas[i].end<end ? vmas[i].end : end;
            memcpy((unsigned char*)destination+(at-address),
                backing_base+vmas[i].phys+(at-vmas[i].start),part_end-at);
            at=part_end;
        }
    }
    read_unlock();return ok;
}
size_t runtime_gpu_memory_mappings(BbDirectMapping *out,size_t capacity) {
    read_lock();
    size_t count=0;
    for (size_t i=0;i<vma_count;++i) if (vmas[i].kind==KIND_DIRECT) {
        if (out && count<capacity) out[count]=(BbDirectMapping){vmas[i].start,vmas[i].end-vmas[i].start,vmas[i].phys};
        ++count;
    }
    read_unlock();
    return count;
}

_Static_assert(sizeof(BbCheckpointMemoryLayout)==32,"memory checkpoint header size");
_Static_assert(sizeof(BbCheckpointMemoryBlock)==32,"memory checkpoint block size");
_Static_assert(sizeof(BbCheckpointMemoryVma)==40,"memory checkpoint VMA size");
int runtime_memory_checkpoint_export(BbCheckpointMemoryLayout *layout,
    BbCheckpointMemoryBlock *out_blocks,size_t blocks_capacity,
    BbCheckpointMemoryVma *out_vmas,size_t vmas_capacity) {
    if (!layout) return EINVAL;
    read_lock();
    size_t block_count=0;
    for (unsigned i=0;i<LIMIT;++i) if (blocks[i].used) ++block_count;
    if (vma_count>65536) { read_unlock(); return EOVERFLOW; }
    *layout=(BbCheckpointMemoryLayout){1,PAGE,POOL_SIZE,(uint32_t)block_count,(uint32_t)vma_count,0};
    if ((!out_blocks && blocks_capacity) || (!out_vmas && vmas_capacity) ||
        (out_blocks && blocks_capacity<block_count) || (out_vmas && vmas_capacity<vma_count)) {
        read_unlock(); return EOVERFLOW;
    }
    size_t at=0;
    if (out_blocks) for (unsigned i=0;i<LIMIT;++i) if (blocks[i].used) {
        const Block *b=&blocks[i];
        out_blocks[at++]=(BbCheckpointMemoryBlock){b->start,b->size,i,b->type,0};
    }
    if (out_vmas) for (size_t i=0;i<vma_count;++i) {
        const Vma *v=&vmas[i];
        out_vmas[i]=(BbCheckpointMemoryVma){v->start,v->end-v->start,v->phys,(uint32_t)v->kind,v->prot,v->type,0};
    }
    read_unlock(); return 0;
}
static int checkpoint_block_range(const BbCheckpointMemoryLayout *layout,
    const BbCheckpointMemoryBlock *saved,uint64_t physical,uint64_t size) {
    const uint64_t end=physical+size;
    for (uint64_t at=physical;at<end;) {
        uint64_t next=at;
        for (unsigned i=0;i<layout->block_count;++i)
            if (saved[i].physical<=at && at-saved[i].physical<saved[i].size) {
                next=saved[i].physical+saved[i].size;break;
            }
        if (next==at) return 0;
        at=next;
    }
    return 1;
}
int runtime_memory_checkpoint_restore(const BbCheckpointMemoryLayout *layout,
    const BbCheckpointMemoryBlock *saved_blocks,const BbCheckpointMemoryVma *saved_vmas) {
    if (!layout || layout->version!=1 || layout->page_size!=PAGE || layout->pool_size!=POOL_SIZE ||
        layout->reserved || layout->block_count>LIMIT || layout->vma_count>65536 ||
        (layout->block_count && !saved_blocks) || (layout->vma_count && !saved_vmas)) return EINVAL;
    Block new_blocks[LIMIT]={0};
    uint64_t new_flex_bitmap[FLEX_SPAN/PAGE/64]={0},new_live=0,new_flexible=0;
    for (unsigned i=0;i<layout->block_count;++i) {
        const BbCheckpointMemoryBlock *b=&saved_blocks[i];
        if (b->reserved || b->slot>=LIMIT || new_blocks[b->slot].used || !b->size || b->size%PAGE ||
            b->physical%PAGE || b->physical>=POOL_SIZE || b->size>POOL_SIZE-b->physical || b->type<0 || b->type>10) return EINVAL;
        for (unsigned j=0;j<i;++j)
            if (b->physical<saved_blocks[j].physical+saved_blocks[j].size && saved_blocks[j].physical<b->physical+b->size)
                return EINVAL;
        new_blocks[b->slot]=(Block){b->physical,b->size,b->type,1};new_live+=b->size;
    }
    uint64_t previous_end=USER_MIN;
    for (unsigned i=0;i<layout->vma_count;++i) {
        const BbCheckpointMemoryVma *v=&saved_vmas[i];
        if (v->reserved || v->start<previous_end || v->start%PAGE || v->start>=USER_MAX ||
            !v->size || v->size%PAGE || v->size>USER_MAX-v->start || v->physical%PAGE ||
            (v->protection&~0x37) || v->type<0 || v->kind<KIND_RESERVED || v->kind>KIND_FLEXIBLE) return EINVAL;
        previous_end=v->start+v->size;
        if (v->kind==KIND_RESERVED) {
            if (v->physical || v->protection || v->type) return EINVAL;
        } else if (v->kind==KIND_DIRECT) {
            if (v->physical>=POOL_SIZE || v->size>POOL_SIZE-v->physical ||
                !checkpoint_block_range(layout,saved_blocks,v->physical,v->size)) return EINVAL;
        } else {
            if (v->physical<POOL_SIZE || v->physical>=POOL_SIZE+FLEX_SPAN || v->size>POOL_SIZE+FLEX_SPAN-v->physical ||
                v->size>FLEXIBLE_SIZE-new_flexible) return EINVAL;
            new_flexible+=v->size;
            const uint64_t first=(v->physical-POOL_SIZE)/PAGE,count=v->size/PAGE;
            for (uint64_t p=first;p<first+count;++p) {
                const uint64_t bit=UINT64_C(1)<<(p%64);
                if (new_flex_bitmap[p/64]&bit) return EINVAL;
                new_flex_bitmap[p/64]|=bit;
            }
        }
    }
    Vma *new_vmas=layout->vma_count ? calloc(layout->vma_count,sizeof(*new_vmas)) : NULL;
    if (layout->vma_count && !new_vmas) return ENOMEM;
    write_lock();
    int busy=vma_count || flexible_bytes || live_bytes || pending_count || hook_map || hook_unmap || hook_invalidate;
    for (unsigned i=0;i<LIMIT;++i) if (blocks[i].used) busy=1;
    if (busy) { write_unlock(); free(new_vmas); return EBUSY; }
    if (pool()) { write_unlock(); free(new_vmas); return ENOMEM; }
    unsigned mapped=0;
    for (;mapped<layout->vma_count;++mapped) {
        const BbCheckpointMemoryVma *v=&saved_vmas[mapped];
        if (host_map((uintptr_t)v->start,v->size,host_prot(v->protection),v->kind==KIND_RESERVED,v->physical)) break;
        new_vmas[mapped]=(Vma){(uintptr_t)v->start,(uintptr_t)(v->start+v->size),(int)v->kind,v->protection,v->type,v->physical,++next_mapping_identity};
    }
    if (mapped!=layout->vma_count) {
        for (unsigned i=0;i<mapped;++i) (void)host_unmap(new_vmas[i].start,new_vmas[i].end-new_vmas[i].start);
        write_unlock(); free(new_vmas); return ENOMEM;
    }
    free(vmas);vmas=new_vmas;vma_count=vma_capacity=layout->vma_count;
    memcpy(blocks,new_blocks,sizeof(blocks));memcpy(flex_bitmap,new_flex_bitmap,sizeof(flex_bitmap));
    live_bytes=new_live;flexible_bytes=new_flexible;
    write_unlock(); return 0;
}
static int checkpoint_physical_range(uint64_t physical,size_t size) {
    if (!size || physical>=POOL_SIZE+FLEX_SPAN || size>POOL_SIZE+FLEX_SPAN-physical) return 0;
    if (physical<POOL_SIZE) return size<=POOL_SIZE-physical && direct_range_allocated(physical,size);
    const uint64_t last=(physical+size-1-POOL_SIZE)/PAGE;
    for (uint64_t p=(physical-POOL_SIZE)/PAGE;p<=last;++p) if (!flex_test(p)) return 0;
    return 1;
}
int runtime_memory_checkpoint_read(uint64_t physical,void *bytes,size_t size) {
    if (!bytes) return EINVAL;
    read_lock();
    if (!backing_base || !checkpoint_physical_range(physical,size)) { read_unlock(); return EINVAL; }
    memcpy(bytes,backing_base+physical,size);read_unlock();return 0;
}
int runtime_memory_checkpoint_write(uint64_t physical,const void *bytes,size_t size) {
    if (!bytes) return EINVAL;
    read_lock();
    if (!backing_base || !checkpoint_physical_range(physical,size)) { read_unlock(); return EINVAL; }
    memcpy(backing_base+physical,bytes,size);read_unlock();return 0;
}
int runtime_gpu_memory_allocate(uint64_t start,uint64_t end,uint64_t size,uint64_t alignment,int type,uint64_t *physical) {
    if (!physical) return EINVAL;
    int64_t offset=0;
    int32_t result=direct_allocate((int64_t)start,(int64_t)end,size,alignment,type,&offset);
    if (!result) *physical=(uint64_t)offset;
    return result ? (int)((uint32_t)result&0xffff) : 0;
}
int runtime_gpu_memory_map(uintptr_t *address,uint64_t size,int prot,int flags,uint64_t physical,uint64_t alignment) {
    if (!address) return EINVAL;
    void *pointer=(void *)*address;
    int32_t result=direct_map(&pointer,size,prot,flags,(int64_t)physical,alignment);
    if (!result) *address=(uintptr_t)pointer;
    return result ? (int)((uint32_t)result&0xffff) : 0;
}
void *bbmetal_guest_allocate(size_t size) { return runtime_low_map(size,PROT_READ|PROT_WRITE); }
/* Host-owned memory the guest can see (image, stacks, trampolines) lives in
 * [LOW_MIN, USER_MIN): PS4 code packs pointers into 40-bit fields. */
#ifdef __APPLE__
#define LOW_MIN UINT64_C(0x7000000000)
#else
#define LOW_MIN UINT64_C(0x0800000000)
#endif
static uintptr_t low_next=LOW_MIN;
void *runtime_low_map_at(uintptr_t address,size_t size,int prot) {
    if (!size || address<LOW_MIN || address>=USER_MIN || address%PAGE ||
        size>USER_MIN-address || size>USER_MIN-LOW_MIN-PAGE || (prot&~7)) return NULL;
    size=align_up(size,PAGE);
    if (size>USER_MIN-address) return NULL;
    write_lock();
#ifdef _WIN32
    void *p=VirtualAlloc((void *)address,size,MEM_RESERVE|MEM_COMMIT,win_prot(prot));
#elif defined(__APPLE__)
    void *p=mmap((void *)address,size,prot,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (p!=MAP_FAILED && p!=(void *)address) { munmap(p,size); p=MAP_FAILED; }
    if (p==MAP_FAILED) p=NULL;
#else
    void *p=mmap((void *)address,size,prot,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    if (p==MAP_FAILED) p=NULL;
#endif
    if (p && address+size+PAGE>low_next) low_next=address+size+PAGE;
    write_unlock();
    return p;
}
void *runtime_low_map(size_t size, int prot) {
    if (!size || size>USER_MIN-LOW_MIN-PAGE) return NULL;
    size=align_up(size,PAGE);
    write_lock();
#ifdef _WIN32
    void *p=NULL;
    while (!p && low_next+size<=USER_MIN) {
        p=VirtualAlloc((void *)low_next,size,MEM_RESERVE|MEM_COMMIT,win_prot(prot));
        low_next+=size+UINT64_C(65536); /* unmapped gap catches overruns; allocation granularity */
    }
    write_unlock();
    return p;
#else
    void *p=MAP_FAILED;
    while (low_next+size<=USER_MIN) {
#ifdef __APPLE__
        uintptr_t attempted=low_next;
        p=mmap((void *)low_next,size,prot,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if (p!=MAP_FAILED && p!=(void *)low_next) {
            munmap(p,size); p=MAP_FAILED;
        }
#else
        p=mmap((void *)low_next,size,prot,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
#endif
        low_next+=size+PAGE; /* unmapped gap catches overruns */
#ifdef __APPLE__
        if (p==MAP_FAILED) {
            /* Darwin may put a multi-GiB host mapping in this band. Jump past
             * it instead of retrying one 16-KiB page at a time. */
            mach_vm_address_t region=attempted;
            mach_vm_size_t length=0;
            vm_region_basic_info_data_64_t info;
            mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
            mach_port_t object=MACH_PORT_NULL;
            kern_return_t result=mach_vm_region(mach_task_self(),&region,&length,
                VM_REGION_BASIC_INFO_64,(vm_region_info_t)&info,&count,&object);
            if (object!=MACH_PORT_NULL) mach_port_deallocate(mach_task_self(),object);
            if (result==KERN_SUCCESS && region<attempted+size && region+length>low_next)
                low_next=align_up(region+length,PAGE);
        }
#endif
        if (p!=MAP_FAILED) break;
    }
    write_unlock();
    return p==MAP_FAILED ? NULL : p;
#endif
}
/* ---- GPU library interface (gpu/shim/bbgpu.cpp) ---- */
/* Optimizations switched off at run time (diagnostics): the number in the file named by
 * BB_TOGGLE_FILE, re-read every 250 ms. Bits: 1 region cache, 2 fetch shader cache,
 * 4 page tracking early exit, 8 pending-op poll limit, 16 threaded Vulkan recording,
 * 32 texture descriptor cache, 64 lock-free upload check, 128 image lookup cache,
 * 256 buffer uploads on the recording thread, 512 barrier tracker insert memo,
 * 1024 texture binding memo, 2048 page-granular read tracking for barriers,
 * 4096 resource sharps read by the draw-preparation workers,
 * 8192 prepared draws from the draw-preparation workers,
 * 16384 small read-only buffer copies on the recording thread,
 * 32768 hot pages (opt-in with BB_HOT_PAGES=1),
 * 65536 unprotect the 256 KiB window around a guest write fault (BB_FAULT_WINDOW KiB),
 * 131072 large guest memory copies split across copy threads (BB_COPY_THREADS),
 * 262144 with BB_ASYNC_FENCES=1: wait for guest copies at fences again,
 * 524288 small guest copies batched for the copy threads instead of the recording thread,
 * 1073741824 the lock-free UpdateImage path for clean, tracked images. */
/* Bits 32 and up: the draw pipeline and related GPU thread work (gpu/shim/bbport_toggles.h). */
uint64_t runtime_disabled_optimizations;
/* Speculative readers of guest memory (GPU draw-preparation workers) register a recovery
 * point: a fault on that thread jumps back to it instead of terminating (probe.c). */
__thread sigjmp_buf *runtime_fault_recover;
static void *toggle_watcher(void *path) {
    for (unsigned long long last=ULLONG_MAX;;) {
        FILE *f=fopen(path,"r");
        unsigned long long value=0;
        if (f) { if (fscanf(f,"%llu",&value)!=1) value=0; fclose(f); }
        if (value!=last) {
            __atomic_store_n(&runtime_disabled_optimizations,(uint64_t)value,__ATOMIC_RELEASE);
            printf("Runtime: disabled optimizations mask=%llu\n",value);
            last=value;
        }
#ifdef _WIN32
        compat_sleep_ns(250000000);
#else
        struct timespec t={0,250000000}; nanosleep(&t,NULL);
#endif
    }
    return NULL;
}
void runtime_memory_set_gpu_hooks(GpuRange map, GpuRange unmap, GpuRange invalidate) {
    const char *toggles=getenv("BB_TOGGLE_FILE");
    static pthread_t watcher;
    if (toggles && !watcher) pthread_create(&watcher,NULL,toggle_watcher,(void *)toggles);
    write_lock();
    hook_map=map; hook_unmap=unmap; hook_invalidate=invalidate;
    for (size_t i=0;i<vma_count;++i) if (vmas[i].kind!=KIND_RESERVED) queue_hook(HOOK_MAP,vmas[i].start,vmas[i].end-vmas[i].start);
    write_unlock();
    flush_hooks();
}
/* Writes through the backing view; 0 when part of the range has no backing. */
int runtime_memory_write_backing(uintptr_t address, const void *data, uint64_t size) {
    read_lock();
    int ok=1;
    for (uintptr_t at=address, end=address+size; at<end && ok;) {
        size_t i=vma_index(at);
        if (i==vma_count || vmas[i].start>at || vmas[i].kind==KIND_RESERVED) { ok=0; break; }
        uint64_t n=(vmas[i].end<end ? vmas[i].end : end)-at;
        memcpy(backing_base+vmas[i].phys+(at-vmas[i].start),(const unsigned char *)data+(at-address),n);
        at+=n;
    }
    read_unlock();
    return ok;
}
/* Reads through the backing view: the host's copies of guest memory (uploads), which the GPU's
 * read protection (BB_READBACKS=2: GPU-written pages) must not stop. A fault there would ask the
 * GPU thread for a readback while it waits for these very copies (a hang at start). Gaps read
 * as zeros. */
void runtime_memory_read_backing(uintptr_t address, void *data, uint64_t size) {
    unsigned char *out=data;
    read_lock();
    for (uintptr_t at=address, end=address+size; at<end;) {
        const size_t i=vma_index(at);
        uint64_t n;
        if (i<vma_count && vmas[i].start<=at && vmas[i].kind!=KIND_RESERVED) {
            n=(vmas[i].end<end ? vmas[i].end : end)-at;
            memcpy(out+(at-address),backing_base+vmas[i].phys+(at-vmas[i].start),n);
        } else {
            const uintptr_t next=i==vma_count ? end : vmas[i].start>at ? vmas[i].start : vmas[i].end;
            n=(next<end ? next : end)-at;
            memset(out+(at-address),0,n);
        }
        at+=n;
    }
    read_unlock();
}
/* Per-thread cache of recently found regions for the GPU's per-draw queries:
 * entries hold for as long as the table generation they were read at. */
typedef struct { uint64_t generation; uintptr_t start, end; int kind; } CachedRegion;
static __thread CachedRegion region_cache[8];
static __thread unsigned region_cache_next;
static const CachedRegion *cached_region(uintptr_t address) {
    uint64_t generation=__atomic_load_n(&table_generation,__ATOMIC_ACQUIRE);
    if (generation & 1) return NULL;
    for (unsigned i=0;i<8;++i) {
        const CachedRegion *c=&region_cache[i];
        if (c->generation==generation && c->end && c->start<=address && address<c->end) return c;
    }
    return NULL;
}
/* Called under the read lock after a lookup of address. */
static void cache_region(size_t i) {
    uint64_t generation=__atomic_load_n(&table_generation,__ATOMIC_ACQUIRE);
    if (i==vma_count || (generation & 1)) return;
    region_cache[region_cache_next++ % 8]=(CachedRegion){generation,vmas[i].start,vmas[i].end,vmas[i].kind};
}
/* Mapped length from address, up to size, across adjacent mappings. */
uint64_t runtime_memory_clamp(uintptr_t address, uint64_t size) {
    const CachedRegion *c=(runtime_disabled_optimizations & 1) ? NULL : cached_region(address);
    if (c && c->kind!=KIND_RESERVED && size<=c->end-address) return size;
    read_lock();
    { size_t i=vma_index(address); if (i<vma_count && vmas[i].start<=address) cache_region(i); }
    uint64_t length=0;
    for (size_t i=vma_index(address); i<vma_count && length<size; ++i) {
        if (vmas[i].start>address+length || vmas[i].kind==KIND_RESERVED) break;
        length=vmas[i].end-address;
    }
    read_unlock();
    return length<size ? length : size;
}
/* Region around address: [start,end) mapped or a gap; 0 past the last mapping. */
int runtime_memory_region(uintptr_t address, uintptr_t *start, uintptr_t *end, int *mapped) {
    const CachedRegion *c=(runtime_disabled_optimizations & 1) ? NULL : cached_region(address);
    if (c) { *start=c->start; *end=c->end; *mapped=c->kind!=KIND_RESERVED; return 1; }
    read_lock();
    size_t i=vma_index(address);
    int found=i<vma_count;
    if (found && vmas[i].start<=address) {
        cache_region(i);
        *start=vmas[i].start; *end=vmas[i].end; *mapped=vmas[i].kind!=KIND_RESERVED;
    } else if (found) {
        *start=i ? vmas[i-1].end : 0; *end=vmas[i].start; *mapped=0;
    }
    read_unlock();
    return found;
}
/* GPU page tracking: restrict host access, never beyond the guest's own protection. */
void runtime_memory_gpu_protect(uintptr_t address, uint64_t size, int read, int write) {
    read_lock();
    for (size_t i=vma_index(address); i<vma_count && vmas[i].start<address+size; ++i) {
        if (vmas[i].kind==KIND_RESERVED) continue;
        uintptr_t a=vmas[i].start>address ? vmas[i].start : address;
        uintptr_t b=vmas[i].end<address+size ? vmas[i].end : address+size;
        int guest=host_prot(vmas[i].prot);
        int want=(read ? PROT_READ : 0)|(write ? PROT_WRITE|PROT_READ : 0)|(guest & PROT_EXEC);
        host_protect(a,b-a,guest & want);
    }
    read_unlock();
}
/* Host view of a guest range, for the GPU backend: the range must be mapped. */
int runtime_memory_is_mapped(uintptr_t address, uint64_t size) {
    const CachedRegion *c=(runtime_disabled_optimizations & 1) ? NULL : cached_region(address);
    if (c && c->kind!=KIND_RESERVED && size<=c->end-address) return 1;
    read_lock();
    int result=covered(address,address+size,0);
    read_unlock();
    return result;
}
void runtime_memory_report(void) {
    printf("Runtime: direct memory allocations=%zu, maps=%zu, live=%" PRIu64 ", budget=%" PRIu64 "\n",
           allocations, maps, live_bytes, POOL_SIZE);
    printf("Runtime: flexible maps=%zu, in use=%" PRIu64 ", protects=%zu, queries=%zu, regions=%zu\n",
           flexible_maps, flexible_bytes, protects, queries, vma_count);
}

