/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the production Darwin guest-memory implementation, without a game
 * image or GPU stubs. Only a few physical pages of the sparse pool are touched. */
#include "../src/runtime_memory.c"

int32_t *runtime_errno(void) {
    static _Thread_local int32_t guest_errno;
    return &guest_errno;
}
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *name) {
    for (size_t i=0;i<count;++i) if (!strcmp(table[i].name,name)) return (uintptr_t)table[i].function;
    return 0;
}
static void check(int condition,const char *message) {
    if (!condition) { fprintf(stderr,"FAIL: %s\n",message); exit(1); }
}
typedef void *(ABI *GuestMmap)(void *,uint64_t,int,int,int,int64_t);
static void fixed_flexible_budget(GuestMmap guest_mmap) {
    const uint64_t mib=UINT64_C(1024)*1024,chunk=32*mib;
    unsigned char *full=guest_mmap(NULL,FLEXIBLE_SIZE,3,0x1002,-1,0);
    check(full!=(void *)UINTPTR_MAX && flexible_bytes==FLEXIBLE_SIZE,"fill complete flexible quota");
    int64_t physical=-1;
    check(!direct_allocate(0,POOL_SIZE,PAGE,0,3,&physical),"allocate independent direct neighbor");
    void *neighbor=full+FLEXIBLE_SIZE;
    check(!direct_map(&neighbor,PAGE,3,MAP_FIXED_FLAG|MAP_NO_OVERWRITE,physical,0),"map independent direct neighbor");
    *(uint64_t *)neighbor=UINT64_C(0x1122334455667788);
    unsigned char *replace=full+chunk;
    replace[-1]=0x4c;replace[chunk]=0x6d;
    replace[0]=0x93;replace[chunk/2]=0x83;replace[chunk-1]=0x73;
    BbGpuLabel old={0},fresh={0},prefix={0};uint64_t observed=0;
    check(runtime_gpu_label_capture((uintptr_t)replace,4,&old) && runtime_gpu_label_capture((uintptr_t)full,4,&prefix),"capture original replacement identities");
    check(guest_mmap(replace,chunk,3,0x1012,-1,0)==replace,"replace32MiB at full quota using net flexible charge");
    check(flexible_bytes==FLEXIBLE_SIZE && replace[-1]==0x4c && replace[chunk]==0x6d,"replacement preserves accounting and outside bytes");
    for(uint64_t n=0;n<chunk;++n)check(!replace[n],"entire replacement is zero initialized");
    check(!runtime_gpu_label_load(&old,&observed) && runtime_gpu_label_capture((uintptr_t)replace,4,&fresh) && fresh.identity!=old.identity,
        "replacement retires old VMA identity");
    check(runtime_gpu_label_load(&prefix,&observed),"replacement preserves prefix VMA identity");
    replace[0]=0xa7;
    check(guest_mmap(replace,chunk,3,0x1092,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==12,
        "full-budget no-overwrite still rejects replacement");
    check(replace[0]==0xa7 && runtime_gpu_label_load(&fresh,&observed) && flexible_bytes==FLEXIBLE_SIZE,
        "failed no-overwrite preserves bytes identity and quota");
    check(guest_mmap(NULL,PAGE,3,0x1002,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==12,"full quota rejects real new growth");
    check(guest_mmap(neighbor,PAGE,3,0x1012,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==12 &&
        *(uint64_t *)neighbor==UINT64_C(0x1122334455667788),"direct overlap provides no flexible quota credit");
    unsigned char *partial=full+FLEXIBLE_SIZE-chunk/2;
    partial[0]=0x42;
    check(guest_mmap(partial,chunk,3,0x1012,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==12 && partial[0]==0x42 &&
        *(uint64_t *)neighbor==UINT64_C(0x1122334455667788),"partial overlap cannot admit over-budget growth or alter direct neighbor");
    check(!release_flexible(full+FLEXIBLE_SIZE-chunk/2,chunk/2) && flexible_bytes==FLEXIBLE_SIZE-chunk/2,"free16MiB from full pool");
    partial=full+FLEXIBLE_SIZE-chunk;
    partial[-1]=0x58;partial[0]=0xb3;partial[chunk/2-1]=0xc4;
    check(guest_mmap(partial,chunk,3,0x1012,-1,0)==partial && flexible_bytes==FLEXIBLE_SIZE,
        "partial16MiB overlap plus16MiB growth uses exact clipped credit");
    for(uint64_t n=0;n<chunk;++n)check(!partial[n],"part-overlap replacement is entirely zero initialized");
    check(partial[-1]==0x58 && *(uint64_t *)neighbor==UINT64_C(0x1122334455667788),"part-overlap preserves both neighboring mappings");
    check(!release_flexible(full,FLEXIBLE_SIZE) && flexible_bytes==0,"release full-budget replacement fixture");
    check(!direct_release((uint64_t)physical,PAGE),"release independent direct neighbor");
    puts("PASS: full448MiB flexible quota,32MiB fixed replacement, exact partial credit, true growth/no-overwrite rejection, zeroing and VMA identity");
}
static int audit_failures(void) {
    FILE *capture=tmpfile();check(capture!=NULL,"create owned audit capture");
    const int saved_stderr=dup(STDERR_FILENO);check(saved_stderr>=0 && dup2(fileno(capture),STDERR_FILENO)>=0,"capture audit stderr");
    setenv("BB_RUNTIME_EXIT_AUDIT","0",1);
    errno=EDOM;*runtime_errno()=77;
    check(direct_allocate(-1,0,PAGE,0,0,NULL)==INVALID && errno==EDOM && *runtime_errno()==77,
        "disabled failure observation preserves errno");
    check(!atomic_load_explicit(&memory_failure_rows,memory_order_relaxed),"audit is opt in");
    setenv("BB_RUNTIME_EXIT_AUDIT","1",1);
    char directory[]="/private/tmp/bb-memory-audit-XXXXXX";check(mkdtemp(directory)!=NULL,"create owned host-error fixture directory");
    char missing[PATH_MAX];snprintf(missing,sizeof(missing),"%s/missing",directory);
    const char *tmp=getenv("TMPDIR");char *saved_tmp=tmp ? strdup(tmp) : NULL;
    setenv("TMPDIR",missing,1);
    int64_t physical=-1;
    check(direct_allocate(0,POOL_SIZE,PAGE,0,0,&physical)==NO_MEMORY && errno==ENOENT && *runtime_errno()==77,
        "captured actual host pool failure preserves guest errno");
    if(saved_tmp){setenv("TMPDIR",saved_tmp,1);free(saved_tmp);}else unsetenv("TMPDIR");
    check(!rmdir(directory),"remove owned host-error fixture directory");
    const unsigned before=atomic_load_explicit(&memory_failure_rows,memory_order_relaxed);
    void *valid=NULL;check(!map_flexible(&valid,PAGE,3,0) && !release_flexible(valid,PAGE),"successful audit-mode mapping");
    check(atomic_load_explicit(&memory_failure_rows,memory_order_relaxed)==before,"success emits no failure row");
    void *address=NULL;
    errno=EDOM;*runtime_errno()=77;
    check(map_anonymous(&address,PAGE,3,0x1003,-1,0)==INVALID && errno==EDOM && *runtime_errno()==77,
        "early mmap validation audit preserves both errnos");
    BatchEntry entry={.length=FLEXIBLE_SIZE+PAGE,.prot=3,.operation=3};int processed=-1;
    check(batch_map2(&entry,1,&processed,0)==NO_MEMORY && processed==0,"batch flexible quota failure observed");
    check(direct_allocate(0,POOL_SIZE,POOL_SIZE+PAGE,0,0,&physical)==NO_SPACE,"direct range failure observed");
    for(unsigned n=0;n<100;++n)check(direct_allocate(-1,0,PAGE,0,0,NULL)==INVALID,"bounded repeated audit failure");
    fflush(stderr);check(dup2(saved_stderr,STDERR_FILENO)>=0,"restore stderr");close(saved_stderr);
    check(!fseek(capture,0,SEEK_END),"size audit capture");const long length=ftell(capture);check(length>0 && length<65536,"bounded audit capture size");
    rewind(capture);char *text=malloc((size_t)length+1);check(text && fread(text,1,(size_t)length,capture)==(size_t)length,"read audit capture");
    text[length]=0;fclose(capture);unsigned rows=0;
    for(char *at=text;(at=strstr(at,"Runtime memory audit:"));++at)++rows;
    check(rows==64 && atomic_load_explicit(&memory_failure_rows,memory_order_relaxed)==64,"at most64 failure audit rows");
    check(strstr(text,"operation=direct_allocate") && strstr(text,"reason=host-pool") && strstr(text,"host_errno_checked=1 host_errno=2"),
        "actual host errno recorded only for a checked failing operation");
    check(strstr(text,"operation=map_anonymous") && strstr(text,"operation=map_flexible") && strstr(text,"reason=flexible-quota"),"early validation and batch failures retained");
    check(strstr(text,"guest_caller=unavailable") && strstr(text,"host_errno_checked=0 host_errno=0"),"unavailable caller and unchecked host errno explicit");
    free(text);puts("PASS: opt-in failure-only VM audit, batch/early mmap/direct failures, checked host errno, preserved errnos and64-row cap");return 0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--audit"))return audit_failures();
    if(argc!=1)return 2;
    runtime_memory_reserve();
    int64_t physical=-1;
    check(!direct_allocate(0,POOL_SIZE,PAGE,0,3,&physical),"allocate physical page");
    void *a=NULL,*b=NULL;
    check(!direct_map(&a,PAGE,3,0,physical,0),"first alias");
    check(!direct_map(&b,PAGE,3,0,physical,0),"second alias");
    check(a!=b,"distinct virtual aliases");
    memset(a,0x6a,PAGE);
    check(((unsigned char *)b)[PAGE-1]==0x6a,"aliases share storage");
    const uint64_t payload=UINT64_C(0x1234567887654321);
    check(runtime_memory_write_backing((uintptr_t)a,&payload,sizeof(payload)),"GPU backing write");
    check(*(uint64_t *)b==payload,"GPU writes are visible through aliases");
    check(!direct_release((uint64_t)physical,PAGE),"physical release");
    check(!runtime_memory_is_mapped((uintptr_t)a,PAGE),"released mapping forgotten");
    check(!direct_allocate(physical,physical+PAGE,PAGE,0,3,&physical),"physical page reuse");
    a=NULL;
    check(!direct_map(&a,PAGE,3,0,physical,0),"reused page mapping");
    for (size_t i=0;i<PAGE;++i) check(!((unsigned char *)a)[i],"released physical memory is zero on reuse");
    check(!direct_release((uint64_t)physical,PAGE),"release reused page");

    void *flex=NULL;
    check(!map_flexible(&flex,PAGE,3,0),"flexible allocation");
    memset(flex,0x77,PAGE);
    check(!release_flexible(flex,PAGE),"flexible release");
    flex=NULL;
    check(!map_flexible(&flex,PAGE,3,0),"flexible reuse");
    for (size_t i=0;i<PAGE;++i) check(!((unsigned char *)flex)[i],"flexible memory is zero on reuse");
    check(!release_flexible(flex,PAGE),"release reused flexible page");

    GuestMmap guest_mmap=(GuestMmap)runtime_memory_resolve("BPE9s9vQQXo#p#J");
    check(guest_mmap!=NULL,"resolve libc mmap import from the recorded game stop");
    void *anonymous=guest_mmap(NULL,PAGE+1,3,0x1002,-1,0);
    check(anonymous!=(void *)UINTPTR_MAX,"libc anonymous heap mapping");
    check(runtime_memory_is_mapped((uintptr_t)anonymous,PAGE*2),"mmap rounds to guest pages");
    for (size_t i=0;i<PAGE*2;++i) check(!((unsigned char *)anonymous)[i],"mmap is zero initialized");
    memset(anonymous,0x36,PAGE*2);
    check(guest_mmap(anonymous,PAGE,3,0x1092,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==12,
        "mmap no-overwrite reports guest ENOMEM");
    check(((unsigned char *)anonymous)[0]==0x36,"failed mmap preserves existing bytes");
    check(guest_mmap(NULL,UINT64_MAX,3,0x1002,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==22,
        "mmap rejects overflowing lengths");
    check(guest_mmap(NULL,PAGE,3,0x1003,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==22,
        "mmap rejects conflicting sharing modes");
    check(guest_mmap(NULL,PAGE,3,2,4,0)==(void *)UINTPTR_MAX && *runtime_errno()==19,
        "unsupported file mmap returns a guest error");
    uint64_t host_value=payload;
    check(guest_mmap(&host_value,PAGE,3,0x1012,-1,0)==(void *)UINTPTR_MAX && *runtime_errno()==22,
        "fixed mmap cannot replace host memory");
    check(host_value==payload,"rejected fixed mmap preserves host memory");
    check(posix_munmap(anonymous,PAGE+1)==0,"POSIX munmap releases rounded anonymous allocation");
    check(!runtime_memory_is_mapped((uintptr_t)anonymous,PAGE),"anonymous mapping removed from GPU queries");
    check(posix_munmap((void *)((uintptr_t)anonymous+1),PAGE)==-1 && *runtime_errno()==22,
        "POSIX munmap reports guest errno");
    check(flexible_bytes==0,"anonymous mmap allocations released");

    /* A checkpoint's VMA table must retain the correct physical offset after
     * splitting a flexible allocation. Releasing its middle page must not
     * discard or reuse the still-live first/last pages. */
    unsigned char *split=NULL;
    check(!map_flexible((void **)&split,PAGE*3,3,0),"split flexible allocation");
    memset(split,0x11,PAGE);memset(split+PAGE,0x22,PAGE);memset(split+PAGE*2,0x33,PAGE);
    check(!kernel_mprotect(split+PAGE,PAGE,1),"split flexible protection");
    check(!release_flexible(split+PAGE,PAGE),"release middle flexible page");
    check(split[0]==0x11 && split[PAGE*2]==0x33,"partial flexible release preserves live backing pages");
    unsigned char *replacement=NULL;
    check(!map_flexible((void **)&replacement,PAGE,3,0),"reuse freed flexible page");
    memset(replacement,0x77,PAGE);
    check(split[0]==0x11 && split[PAGE*2]==0x33,"flexible reuse cannot alias live neighbors");
    check(!release_flexible(replacement,PAGE) && !release_flexible(split,PAGE) &&
        !release_flexible(split+PAGE*2,PAGE),"release split flexible fixture");
    check(flexible_bytes==0,"split flexible allocation accounting");
    fixed_flexible_budget(guest_mmap);

    void *occupied=runtime_low_map(PAGE,PROT_READ|PROT_WRITE);
    check(occupied!=NULL,"initial low allocation");
    *(uint64_t *)occupied=payload;
    low_next=(uintptr_t)occupied; /* simulate a host allocation racing the search */
    void *low=runtime_low_map(PAGE,PROT_READ|PROT_WRITE);
    check(low && low!=occupied && (uintptr_t)low<USER_MIN,"low pointer allocation skips collision");
    check(*(uint64_t *)occupied==payload,"low allocation preserves existing host memory");
    check(!munmap(occupied,PAGE) && !munmap(low,PAGE),"fixture cleanup");
    puts("PASS: Darwin physical aliases, GPU backing writes, anonymous mmap, partial flexible release, zero-on-reuse and safe low mappings");
    return 0;
}
