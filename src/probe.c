/* macOS loader for PS4 guest images, using Rosetta or the arm64 bbcpu translator. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#ifndef BB_CPU_TRANSLATOR
#include <cpuid.h>
#else
#include "cpu/bbcpu.h"
#endif
#include "runtime.h"
#include "runtime_modules.h"
#include "runtime_trophy.h"
#include "gpu/bbgpu.h"
#if (!defined(__x86_64__) && !defined(BB_CPU_TRANSLATOR)) || !defined(__GNUC__)
#error The Rosetta runtime requires x86-64 Clang; use BB_CPU_TRANSLATOR for arm64.
#endif
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#ifdef BB_CPU_TRANSLATOR
#define UC_RIP(uc) ((uc)->uc_mcontext->__ss.__pc)
#define UC_RBP(uc) ((uc)->uc_mcontext->__ss.__fp)
#define UC_RDI(uc) ((uc)->uc_mcontext->__ss.__x[0])
#else
#define UC_RIP(uc) ((uc)->uc_mcontext->__ss.__rip)
#define UC_RBP(uc) ((uc)->uc_mcontext->__ss.__rbp)
#define UC_RDI(uc) ((uc)->uc_mcontext->__ss.__rdi)
#endif

typedef struct { uint64_t address, size, flags; } Segment;
typedef struct { uint64_t target, kind, value, addend; } Reloc;
static char (*names)[128];
static uint64_t import_count;
static unsigned char *image;
static size_t page_size;
typedef struct { uint64_t base, size, init, tls_address, tls_memsz, tls_filesz, tls_module; } LinkedModule;
static LinkedModule modules[16];
static uint64_t module_count;
static int entered_game;
static int gpu_enabled;

static __attribute__((noreturn)) void fail(const char *message) { fprintf(stderr, "ERROR: %s\n", message); exit(1); }
static uint64_t read64(FILE *f) {
    unsigned char b[8];
    if (fread(b, 1, 8, f) != 8) fail("truncated boot file");
    uint64_t n = 0;
    for (int i = 7; i >= 0; --i) n = (n << 8) | b[i];
    return n;
}
static size_t round_page(size_t size) { return (size + page_size - 1) & ~(page_size - 1); }
static void *allocate(size_t size) {
    void *low=runtime_low_map(size,PROT_READ|PROT_WRITE);
    if (low) return low;
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) fail("mmap failed");
    return p;
}
static void protect(void *p, size_t size, unsigned flags) {
    int mode = ((flags & 4) ? PROT_READ : 0) | ((flags & 2) ? PROT_WRITE : 0) | ((flags & 1) ? PROT_EXEC : 0);
#ifdef BB_CPU_TRANSLATOR
    // The arm64 JIT executes a separate MAP_JIT allocation. Guest x86 bytes
    // need read access for decoding, never executable host mappings.
    mode &= ~PROT_EXEC;
    if (flags&1) mode |= PROT_READ;
    uintptr_t start=(uintptr_t)p&~(page_size-1);
    size=round_page((uintptr_t)p-start+size);p=(void *)start;
#endif
    if (mprotect(p, size, mode)) fail("mprotect failed");
}
static ABI __attribute__((noreturn)) void unresolved(uint32_t id, uintptr_t argument) {
    if (id >= import_count) fail("bad import trap index");
    printf("STOP: first unsupported PS4 import: %s (index %u)\n", names[id], id);
    printf("API: %s\n", runtime_import_name(names[id]));
    printf("Caller return offset: 0x%" PRIxPTR "; first argument: 0x%" PRIxPTR "\n",
           (uintptr_t)__builtin_return_address(0) - (uintptr_t)image, argument);
    uintptr_t caller=(uintptr_t)__builtin_return_address(0)-(uintptr_t)image;
    for (uint64_t m=0;m<module_count;++m)
        if (caller>=modules[m].base && caller-modules[m].base<modules[m].size)
            printf("Caller in linked module %" PRIu64 " (%s): +0x%" PRIxPTR "\n",m,m==0 ? "libc.prx" : "system module",(uintptr_t)(caller-modules[m].base));
    runtime_report();
    puts(entered_game ? "Original guest entry instructions executed; game initialization is incomplete." :
                        "Native libc initialization is incomplete; game entry has not run.");
    fflush(NULL);
    _exit(20); /* no destructors: GPU, audio and guest threads are still running */
}
#ifndef BB_CPU_TRANSLATOR
/* enter_on_stack(entry, arg0, arg1, stack_top): call entry(arg0,arg1) on a new stack. */
void enter_on_stack(void *entry, void *arg0, void *arg1, void *top);
__asm__(".text\n.globl _enter_on_stack\n_enter_on_stack:\n"
        " push %rbp\n mov %rsp,%rbp\n and $-16,%rcx\n mov %rcx,%rsp\n"
        " mov %rdi,%rax\n mov %rsi,%rdi\n mov %rdx,%rsi\n call *%rax\n"
        " mov %rbp,%rsp\n pop %rbp\n ret\n");
#endif
static ABI void guest_exit(void) { puts("Runtime: process finalizer callback reached"); }
/* Rosetta must expose the instruction set used by the PS4's AMD Jaguar CPU.
 * The native arm64 runtime delegates those instructions to bbcpu instead. */
static void check_cpu(void) {
#ifdef BB_CPU_TRANSLATOR
    puts("CPU: native arm64 host with bbcpu x86-64 translation");
#else
    if (getenv("BB_SKIP_CPU_CHECK")) return;
    unsigned a=0, b=0, c=0, d=0, basic=0, leaf7=0, extended=0;
    if (__get_cpuid(1,&a,&b,&c,&d)) basic=c;
    if (__get_cpuid_max(0,NULL)>=7) { __cpuid_count(7,0,a,b,c,d); leaf7=b; }
    if (__get_cpuid(0x80000001,&a,&b,&c,&d)) extended=c;
    int avx=0;
    if (((basic>>27)&1) && ((basic>>28)&1)) { /* OSXSAVE and AVX: the OS also has to save YMM state */
        unsigned low, high;
        __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        avx=(low&6)==6;
    }
    const struct { int present; const char *name; } features[]={
        {avx,"AVX"}, {(leaf7>>3)&1,"BMI1"}, {(basic>>22)&1,"MOVBE"}, {(extended>>5)&1,"LZCNT"},
        {(basic>>23)&1,"POPCNT"}, {(basic>>20)&1,"SSE4.2"},
    };
    char missing[64]="";
    for (size_t i=0;i<sizeof(features)/sizeof(*features);++i) {
        if (features[i].present) continue;
        if (*missing) strcat(missing,", ");
        strcat(missing,features[i].name);
    }
    if (!*missing) return;
    unsigned words[12]={0};
    char brand[49]="";
    if (__get_cpuid_max(0x80000000,NULL)>=0x80000004)
        for (unsigned i=0;i<3;++i) __get_cpuid(0x80000002+i,&words[i*4],&words[i*4+1],&words[i*4+2],&words[i*4+3]);
    memcpy(brand,words,48);
    for (size_t n=strlen(brand); n && brand[n-1]==' '; --n) brand[n-1]=0;
    const char *name=brand;
    while (*name==' ') ++name;
    if (!*name) name="unknown processor";
    fprintf(stderr,"STOP: Rosetta CPU %s is missing %s (enable ROSETTA_ADVERTISE_AVX=1 or use BB_MAC_CPU=bbcpu)\n",name,missing);
    exit(24);
#endif
}
/* Guest code reads its TCB with `mov rax, gs:[0]` (link_*.py rewrote fs:[0]); on Windows
 * the displacement becomes the TEB TLS slot that holds the guest TCB (runtime_thread.c). */
static uint64_t patch_tcb_loads(const Segment *segments, uint64_t count) {
    static const unsigned char load[]={0x65,0x48,0x8b,0x04,0x25,0,0,0,0};
    uint32_t displacement=0; /* Darwin guest TLS uses FS; GS belongs to libpthread. */
    uint64_t patched=0;
    for (uint64_t s=0;s<count;++s) {
        if (!(segments[s].flags&1) || segments[s].size<sizeof(load)) continue;
        unsigned char *at=image+segments[s].address, *end=at+segments[s].size-sizeof(load);
        for (; at<=end; ++at)
            if (at[0]==0x65 && !memcmp(at,load,sizeof(load))) {
                at[0]=0x64;
                memcpy(at+5,&displacement,4); ++patched; at+=sizeof(load)-1;
            }
    }
    return patched;
}
static void fault(int sig, siginfo_t *info, void *context) {
#ifdef BB_CPU_TRANSLATOR
    if (sig==SIGBUS && bbcpu_handle_alignment_fault(context)) return;
#endif
    /* GPU page tracking (write-protected guest pages) is resolved first. */
    if (gpu_enabled && sig == SIGSEGV && bbgpu_handle_fault(context, info->si_addr)) return;
    /* A speculative guest memory read (runtime_memory.c) failed: resume its recovery point. */
    if ((sig == SIGSEGV || sig == SIGBUS) && runtime_fault_recover) {
        sigjmp_buf *recover = runtime_fault_recover;
        runtime_fault_recover = NULL;
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        pthread_sigmask(SIG_UNBLOCK, &unblock, NULL);
        siglongjmp(*recover, 1);
    }
    /* The process is terminating: dladdr/snprintf are acceptable here. */
    ucontext_t *uc = context;
    uintptr_t rip = (uintptr_t)UC_RIP(uc);
    char line[512];
    Dl_info where;
    if (rip - (uintptr_t)image < 0x10000000)
        snprintf(line, sizeof(line), "Guest fault (signal %d) at guest offset 0x%lx, address %p\n",
                 sig, (unsigned long)(rip - (uintptr_t)image), info->si_addr);
    else if (dladdr((void *)rip, &where) && where.dli_fname)
        snprintf(line, sizeof(line), "Host fault (signal %d) in %s+0x%lx (%s), address %p\n", sig, where.dli_fname,
                 (unsigned long)(rip - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?", info->si_addr);
    else
        snprintf(line, sizeof(line), "Fault (signal %d) at RIP %p, address %p\n", sig, (void *)rip, info->si_addr);
    { ssize_t written_=write(2, line, strlen(line)); (void)written_; }
#if defined(__APPLE__) && !defined(BB_CPU_TRANSLATOR)
    const __typeof__(uc->uc_mcontext->__ss) *state=&uc->uc_mcontext->__ss;
    snprintf(line,sizeof(line),
        "  rax=%016llx rbx=%016llx rcx=%016llx rdx=%016llx\n"
        "  rsi=%016llx rdi=%016llx rbp=%016llx rsp=%016llx\n"
        "  r8=%016llx r9=%016llx r10=%016llx r11=%016llx\n"
        "  r12=%016llx r13=%016llx r14=%016llx r15=%016llx\n",
        state->__rax,state->__rbx,state->__rcx,state->__rdx,
        state->__rsi,state->__rdi,state->__rbp,state->__rsp,
        state->__r8,state->__r9,state->__r10,state->__r11,
        state->__r12,state->__r13,state->__r14,state->__r15);
    { ssize_t written_=write(2,line,strlen(line)); (void)written_; }
#endif
#ifdef BB_CPU_TRANSLATOR
    if (bbcpu_describe_fault(context,line,sizeof(line))) {
        ssize_t written_=write(2,line,strlen(line));(void)written_;
    }
#endif
    if (gpu_enabled) bbgpu_dump_guest_writes(context);
    /* Host call chain (frames with unwind info; guest frames end it). */
    void *frames[32];
    int depth = backtrace(frames, 32);
    for (int i = 2; i < depth; ++i) {
        if (dladdr(frames[i], &where) && where.dli_fname)
            snprintf(line, sizeof(line), "  #%d %s+0x%lx (%s)\n", i, where.dli_fname,
                     (unsigned long)((uintptr_t)frames[i] - (uintptr_t)where.dli_fbase), where.dli_sname ? where.dli_sname : "?");
        else
            snprintf(line, sizeof(line), "  #%d %p\n", i, frames[i]);
        ssize_t written_=write(2, line, strlen(line)); (void)written_;
    }
    _exit(128 + sig);
}
/* Watchdog: dump RIP and the rbp frame chain of every thread (guest offsets
 * when inside the image). Reads use process_vm_readv so bad frames cannot fault. */
static uintptr_t exe_base;
static void write_hex(char *out, uint64_t v) {
    const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 16; ++i) out[i] = digits[(v >> (60 - i * 4)) & 15];
}
static void dump_frames(ucontext_t *uc) {
    char line[] = "  tid=0000000000000000 rip=0000000000000000 image-relative=0000000000000000 host-relative=0000000000000000\n";
    uintptr_t rip=(uintptr_t)UC_RIP(uc), rbp=(uintptr_t)UC_RBP(uc);
    uint64_t tid=0; pthread_threadid_np(NULL,&tid);
    /* First argument register: the lock address when a thread waits on a futex. */
    char arg[]="  tid=0000000000000000 rdi=0000000000000000\n";
    write_hex(arg+6,tid); write_hex(arg+27,(uint64_t)UC_RDI(uc));
    { ssize_t written_=write(2,arg,sizeof(arg)-1); (void)written_; }
    for (int depth=0; depth<24; ++depth) {
        write_hex(line+6,tid); write_hex(line+27,rip); write_hex(line+59,rip-(uintptr_t)image); write_hex(line+90,rip-exe_base);
        { ssize_t written_=write(2,line,sizeof(line)-1); (void)written_; }
        uintptr_t frame[2];
        if (!rbp || !bb_macos_read_memory(rbp,frame,sizeof(frame))) break;
        if (frame[0]<=rbp) break;
        rbp=frame[0]; rip=frame[1];
    }
}
static void thread_dump(int sig, siginfo_t *info, void *context) { (void)sig; (void)info; dump_frames(context); }
static void watchdog(int sig, siginfo_t *info, void *context) {
    (void)info;
    const char head[]="STOP: watchdog timeout; thread stacks:\n";
    { ssize_t written_=write(2,head,sizeof(head)-1); (void)written_; }
    dump_frames(context);
    usleep(100000);
    _exit(128 + sig);
}
/* param.sfo lookup: string or integer value of key, 0 when absent. */
static int sfo_value(const char *path, const char *key, char *text, size_t text_size, uint32_t *number) {
    FILE *f=fopen(path,"rb");
    if (!f) return 0;
    unsigned char data[65536];
    size_t n=fread(data,1,sizeof(data),f); fclose(f);
    if (n<20 || memcmp(data,"\0PSF",4)) return 0;
    uint32_t keys, values, count;
    memcpy(&keys,data+8,4); memcpy(&values,data+12,4); memcpy(&count,data+16,4);
    for (uint32_t i=0;i<count && 20+i*16+16<=n;++i) {
        const unsigned char *e=data+20+i*16;
        uint16_t key_offset, format; uint32_t length, offset;
        memcpy(&key_offset,e,2); memcpy(&format,e+2,2); memcpy(&length,e+4,4); memcpy(&offset,e+12,4);
        if (keys+key_offset>=n || values+offset+length>n || strcmp((const char *)data+keys+key_offset,key)) continue;
        if (format==0x0404 && number && length>=4) { memcpy(number,data+values+offset,4); return 1; }
        if (text && text_size) {
            size_t copy=length<text_size-1 ? length : text_size-1;
            memcpy(text,data+values+offset,copy); text[copy]=0;
        }
        return 1;
    }
    return 0;
}
static int mapped(Segment *segments, uint64_t count, uint64_t address, uint64_t bytes) {
    for (uint64_t i = 0; i < count; ++i)
        if (address >= segments[i].address && bytes <= segments[i].size &&
            address - segments[i].address <= segments[i].size - bytes) return 1;
    return 0;
}
/* BBPATCH2 (patches.py): the patches' image base, then byte writes at image offsets, applied
 * after relocation. A write may replace a whole base-relative pointer slot (60/90 FPS++ swap
 * function pointers): the patch holds the address at the patches' base, rebased here. */
static void apply_patches(const char *path, Segment *segments, uint64_t ns, const Reloc *relocs, uint64_t nr) {
    FILE *f=fopen(path,"rb");
    char magic[8];
    if (!f || fread(magic,1,8,f)!=8 || memcmp(magic,"BBPATCH2",8)) fail("invalid patch file");
    uint64_t base=read64(f), count=read64(f), bytes=0, rebased=0;
    unsigned char data[4096];
    for (uint64_t i=0;i<count;++i) {
        uint64_t offset=read64(f), length=read64(f);
        if (!length || length>sizeof(data) || !mapped(segments,ns,offset,length) || fread(data,1,length,f)!=length)
            fail("bad patch entry");
        uint64_t slots[sizeof(data)/8]; size_t nslots=0;
        for (uint64_t r=0;r<nr;++r) {
            if (!(relocs[r].target<offset+length && offset<relocs[r].target+8)) continue;
            uint64_t target=relocs[r].target, value;
            if (relocs[r].kind || target<offset || target+8>offset+length) fail("patch overlaps a relocation");
            memcpy(&value,data+(target-offset),8);
            if (value<base || !mapped(segments,ns,value-base,1)) fail("patch writes a pointer outside the image");
            slots[nslots++]=target;
        }
        memcpy(image+offset,data,length);
        for (size_t s=0;s<nslots;++s) {
            uint64_t value;
            memcpy(&value,image+slots[s],8);
            value=(uint64_t)(uintptr_t)image+(value-base);
            memcpy(image+slots[s],&value,8);
        }
        rebased+=nslots;
        bytes+=length;
    }
    if (fgetc(f)!=EOF) fail("trailing data in patch file");
    fclose(f);
    printf("Patches: %" PRIu64 " writes, %" PRIu64 " bytes applied, %" PRIu64 " pointers rebased\n",count,bytes,rebased);
}
/* Restarts the game through run.sh (the settings menu: a new render resolution is a patch
 * applied at start). Descriptors are closed first so the old GPU device and its memory are
 * released before the new process opens its own. */
void runtime_restart(void) {
    fflush(NULL);
    puts("Runtime: restarting through run.sh");
    for (int fd=3,limit=getdtablesize();fd<limit;++fd) close(fd);
    execlp("bash", "bash", "run.sh", (char *)NULL);
    perror("runtime_restart: exec");
    _exit(1);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    check_cpu();
    int cpu_only = 0, strict_imports = 0;
    unsigned timeout_seconds = 10;
    const char *content_profile=NULL, *app0=NULL, *user_dir=NULL, *patch_file=NULL;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--cpu-only")) cpu_only = 1;
        else if (!strcmp(argv[i], "--strict-imports")) strict_imports = 1;
        else if (!strcmp(argv[i], "--content-profile") && i+1<argc) content_profile=argv[++i];
        else if (!strcmp(argv[i], "--app0") && i+1<argc) app0=argv[++i];
        else if (!strcmp(argv[i], "--user") && i+1<argc) user_dir=argv[++i];
        else if (!strcmp(argv[i], "--patches") && i+1<argc) patch_file=argv[++i];
        else if (!strcmp(argv[i], "--timeout") && i+1<argc) timeout_seconds=(unsigned)strtoul(argv[++i],NULL,10);
        else { fprintf(stderr, "Unknown option: %s\n", argv[i]); return 1; }
    }
    if (argc < 2) {
        fprintf(stderr, "Usage: %s boot.bin [--cpu-only] [--strict-imports] [--content-profile file] [--app0 dir] [--user dir] [--patches file] [--timeout seconds]\n", argv[0]);
        return 1;
    }
    if (content_profile) {
        FILE *profile=fopen(content_profile,"rb");
        unsigned char data[28];
        if (!profile) fail("cannot open content profile");
        if (fread(data,1,sizeof(data),profile)!=sizeof(data) || fgetc(profile)!=EOF || memcmp(data,"BBCONT01",8)) fail("invalid content profile");
        fclose(profile);
        uint32_t values[5];
        for (unsigned i=0;i<5;++i) {
            unsigned char *p=data+8+i*4;
            values[i]=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
        runtime_content_configure(values);
    }
    if (app0) {
        runtime_file_configure(app0, user_dir ? user_dir : "user");
        runtime_trophy_configure(user_dir ? user_dir : "user");
        char sfo[4096], id[16]="";
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0);
        if (sfo_value(sfo,"INSTALL_DIR_SAVEDATA",id,sizeof(id),NULL) || sfo_value(sfo,"TITLE_ID",id,sizeof(id),NULL))
            runtime_savedata_configure(id);
        char serial[16]="", title[128]="", version[16]="";
        sfo_value(sfo,"TITLE_ID",serial,sizeof(serial),NULL);
        sfo_value(sfo,"TITLE",title,sizeof(title),NULL);
        sfo_value(sfo,"APP_VER",version,sizeof(version),NULL);
        bbnet_configure(app0,serial,title,version);
    }
    bbgpu_register_kernel();
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    struct sigaction sa = {0}; sa.sa_sigaction = fault; sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGILL, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    Dl_info self_info;
    if (dladdr((void *)main,&self_info)) exe_base=(uintptr_t)self_info.dli_fbase;
    struct sigaction dump = {0}; dump.sa_sigaction = thread_dump; dump.sa_flags = SA_SIGINFO|SA_RESTART;
    sigemptyset(&dump.sa_mask); sigaction(SIGUSR2, &dump, NULL);
    struct sigaction alarm_action = {0}; alarm_action.sa_sigaction = watchdog; alarm_action.sa_flags = SA_SIGINFO;
    sigemptyset(&alarm_action.sa_mask); sigaction(SIGALRM, &alarm_action, NULL);
    alarm(timeout_seconds); /* 0 disables the watchdog */
    FILE *f = fopen(argv[1], "rb");
    if (!f) fail("cannot open boot file; run prepare.py first");
    char magic[8];
    if (fread(magic, 1, 8, f) != 8 || (memcmp(magic, "BBPROBE1", 8) && memcmp(magic, "BBPROBE2", 8) && memcmp(magic,"BBPROBE3",8) && memcmp(magic,"BBPROBE4",8) && memcmp(magic,"BBPROBE5",8) && memcmp(magic,"BBPROBE6",8))) fail("bad boot file signature");
    uint64_t size = read64(f), entry = read64(f), ns = read64(f), nr = read64(f);
    import_count = read64(f);
    uint64_t capabilities = memcmp(magic, "BBPROBE1", 8) ? read64(f) : 0;
    if (capabilities & ~UINT64_C(1)) fail("unknown runtime capabilities");
    runtime_start(strict_imports ? 0 : capabilities);
    if (!size || size > 512*1024*1024 || entry >= size || !ns || ns > 64 || nr > 1000000 || import_count > 100000)
        fail("boot file limits exceeded");
    int described=!memcmp(magic,"BBPROBE6",8);
    int multi=!memcmp(magic,"BBPROBE5",8) || described;
    int linked=!memcmp(magic,"BBPROBE3",8) || !memcmp(magic,"BBPROBE4",8) || multi;
    int native_libc=linked && !strict_imports && (capabilities&1);
    uint64_t main_tls[4]={0};
    RuntimeModuleInfo descriptions[RUNTIME_MODULE_LIMIT];
    uint64_t description_count=0;
    uint64_t nb=0,procparam=0;
    uint64_t *bindings=calloc(import_count ? import_count : 1,sizeof(*bindings));
    uint64_t *binding_kinds=calloc(import_count ? import_count : 1,sizeof(*binding_kinds));
    if (!bindings || !binding_kinds) fail("allocation failed");
    if (multi) {
        /* BBPROBE5: procparam, eboot TLS, module table, bindings (link_modules.py). */
        procparam=read64(f);
        for (int i=0;i<4;++i) main_tls[i]=read64(f);
        module_count=read64(f);
        if (!module_count || module_count>sizeof(modules)/sizeof(*modules)) fail("invalid module count");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            x->base=read64(f); x->size=read64(f); x->init=read64(f); x->tls_address=read64(f);
            x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=read64(f);
        }
        nb=read64(f);
    } else if (linked) {
        LinkedModule *x=&modules[0];
        module_count=1;
        x->base=read64(f); x->size=read64(f); x->init=read64(f);
        x->tls_address=read64(f); x->tls_memsz=read64(f); x->tls_filesz=read64(f); x->tls_module=2; nb=read64(f);
        procparam=read64(f);
        if (!memcmp(magic,"BBPROBE4",8)) for (int i=0;i<4;++i) main_tls[i]=read64(f);
    }
    if (linked) {
        if (main_tls[1]>main_tls[2] || main_tls[2]>1024*1024 || main_tls[0]>size ||
            main_tls[1]>size-main_tls[0] || (main_tls[3] & (main_tls[3]-1)) || main_tls[3]>4096)
            fail("invalid eboot TLS metadata");
        for (uint64_t m=0;m<module_count;++m) {
            LinkedModule *x=&modules[m];
            if (x->base>=size || !x->size || x->size>size-x->base || x->init<x->base || x->init-x->base>=x->size ||
                (x->tls_module && (x->tls_address>=size || x->tls_memsz>size-x->tls_address || x->tls_memsz>1024*1024 ||
                                   x->tls_filesz>x->tls_memsz || x->tls_module<2 || x->tls_module>7)))
                fail("invalid linked module metadata");
        }
        if (nb>import_count) fail("invalid binding count");
        for (uint64_t i=0;i<nb;++i) {
            uint64_t index=read64(f),address=read64(f),kind=read64(f);
            int inside=0;
            for (uint64_t m=0;m<module_count;++m)
                if (address>=modules[m].base && address-modules[m].base<modules[m].size) inside=1;
            if (index>=import_count || !inside || (kind!=1 && kind!=2) || bindings[index]) fail("invalid native binding");
            bindings[index]=address; binding_kinds[index]=kind;
        }
    }
    if(described) {
        description_count=read64(f);
        if(description_count!=module_count+1 || description_count>RUNTIME_MODULE_LIMIT)
            fail("invalid module-info count");
        if(fread(descriptions,sizeof(*descriptions),description_count,f)!=description_count)
            fail("truncated module-info table");
    }
    Segment *segments = calloc(ns, sizeof(*segments));
    Reloc *relocs = calloc(nr ? nr : 1, sizeof(*relocs));
    names = calloc(import_count ? import_count : 1, sizeof(*names));
    if (!segments || !relocs || !names) fail("allocation failed");
    for (uint64_t i = 0; i < ns; ++i) {
        segments[i].address = read64(f);
        segments[i].size = read64(f);
        segments[i].flags = read64(f);
        if (segments[i].address > size || segments[i].size > size - segments[i].address ||
            segments[i].address % 4096 || segments[i].flags > 7) fail("bad segment");
    }
    if (fread(names, 128, import_count, f) != import_count) fail("truncated import names");
    for (uint64_t i = 0; i < import_count; ++i)
        if (!memchr(names[i], 0, 128)) fail("unterminated import name");
    for (uint64_t i = 0; i < nr; ++i) {
        relocs[i].target = read64(f);
        relocs[i].kind = read64(f);
        relocs[i].value = read64(f);
        relocs[i].addend = read64(f);
        if (!mapped(segments, ns, relocs[i].target, 8) || relocs[i].kind > 2 ||
            (relocs[i].kind && relocs[i].value >= import_count) ||
            (relocs[i].kind != 2 && relocs[i].addend) || relocs[i].addend >= page_size) fail("bad relocation");
    }
    for (uint64_t m=0;m<module_count;++m)
        if (!mapped(segments,ns,modules[m].init,1) || (modules[m].tls_module && !mapped(segments,ns,modules[m].tls_address,modules[m].tls_memsz)))
            fail("unmapped module metadata");
    if (module_count && !mapped(segments,ns,procparam,64)) fail("unmapped procparam");
    for(uint64_t n=0;n<description_count;++n) {
        RuntimeModuleInfo *d=&descriptions[n];
        const uint64_t begin=n ? modules[n-1].base : 0;
        const uint64_t extent=n ? modules[n-1].size : modules[0].base;
        if(d->size!=sizeof(*d) || d->id!=(int32_t)n || !memchr(d->name,0,sizeof(d->name)) ||
           !d->segment_count || d->segment_count>4 || d->reserved1 || d->reserved2)
            fail("invalid module-info metadata");
        for(uint32_t k=0;k<d->segment_count;++k) {
            const RuntimeModuleSegment *s=&d->segments[k];
            if(s->address<begin || s->address-begin>=extent || s->size>extent-(s->address-begin))
                fail("module-info segment outside its image");
            int exact=0;
            for(uint64_t i=0;i<ns;++i)
                if(s->address==segments[i].address && s->size==segments[i].size &&
                   s->prot==(int32_t)segments[i].flags)exact=1;
            if(!exact)fail("module-info segment does not match a load segment");
        }
        uint64_t actual=0;
        for(uint64_t i=0;i<ns;++i)if(segments[i].address>=begin && segments[i].address-begin<extent) {
            ++actual;
            int present=0;
            for(uint32_t k=0;k<d->segment_count;++k)
                if(d->segments[k].address==segments[i].address && d->segments[k].size==segments[i].size &&
                   d->segments[k].prot==(int32_t)segments[i].flags)present=1;
            if(!present)fail("incomplete module-info segment table");
        }
        if(actual!=d->segment_count)fail("incomplete module-info segment table");
        const uint64_t tls_address=n ? modules[n-1].tls_address : main_tls[0];
        const uint64_t tls_filesz=n ? modules[n-1].tls_filesz : main_tls[1];
        const uint64_t tls_memsz=n ? modules[n-1].tls_memsz : main_tls[2];
        const uint64_t tls_index=n ? modules[n-1].tls_module : (main_tls[2] ? 1 : 0);
        if(d->tls_init_addr!=tls_address || d->tls_init_size!=tls_filesz ||
           d->tls_size!=tls_memsz || d->tls_index!=tls_index)
            fail("module-info TLS metadata mismatch");
        /* Each nonempty unwind range must remain within this image's actual
         * segments, rather than merely inside the aggregate boot allocation. */
        const uint64_t addresses[]={d->eh_frame_hdr_addr,d->eh_frame_addr,d->init_proc_addr,d->fini_proc_addr};
        const uint64_t lengths[]={d->eh_frame_hdr_size,d->eh_frame_size,!!d->init_proc_addr,!!d->fini_proc_addr};
        for(unsigned field=0;field<4;++field)if(lengths[field]) {
            int inside=0;
            for(uint32_t k=0;k<d->segment_count;++k) {
                const RuntimeModuleSegment *s=&d->segments[k];
                if(addresses[field]>=s->address && lengths[field]<=s->size &&
                   addresses[field]-s->address<=s->size-lengths[field])inside=1;
            }
            if(!inside)fail("unmapped module-info unwind/init range");
        }
    }
    for (uint64_t i=0;i<import_count;++i) {
        if (!bindings[i]) continue;
        if (!mapped(segments,ns,bindings[i],1)) fail("unmapped native export");
        if (binding_kinds[i]==1) {
            int executable=0;
            for (uint64_t s=0;s<ns;++s)
                if ((segments[s].flags&1) && bindings[i]>=segments[s].address &&
                    bindings[i]-segments[s].address<segments[s].size) executable=1;
            if (!executable) fail("native function is not executable");
        }
    }
    /* Contiguous, nonexecuted storage for guarded engine timing slots and translated text. */
    image = allocate(round_page(size)+16*1024*1024);
#ifdef BB_CPU_TRANSLATOR
    extern uint64_t bb_image_base;
    bb_image_base=(uintptr_t)image;
#endif
    if (fread(image, 1, size, f) != size || fgetc(f) != EOF) fail("incorrect memory image size");
    fclose(f);
    for(uint64_t n=0;n<description_count;++n) {
        RuntimeModuleInfo *d=&descriptions[n];
        for(uint32_t k=0;k<d->segment_count;++k)d->segments[k].address+=(uintptr_t)image;
        if(d->tls_size)d->tls_init_addr+=(uintptr_t)image;
        if(d->init_proc_addr)d->init_proc_addr+=(uintptr_t)image;
        if(d->fini_proc_addr)d->fini_proc_addr+=(uintptr_t)image;
        if(d->eh_frame_hdr_size)d->eh_frame_hdr_addr+=(uintptr_t)image;
        if(d->eh_frame_size)d->eh_frame_addr+=(uintptr_t)image;
    }
    if(description_count && runtime_modules_install(descriptions,description_count))
        fail("invalid loaded module-info metadata");
    if(description_count)printf("Loaded %" PRIu64 " module-info records with bounded unwind metadata\n",description_count);
    if (!cpu_only) {
        char title[128]="Bloodborne", serial[16]="UNKNOWN", sfo[4096];
        uint32_t attributes=0;
        snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",app0 ? app0 : ".");
        sfo_value(sfo,"TITLE",title,sizeof(title),NULL);
        sfo_value(sfo,"TITLE_ID",serial,sizeof(serial),NULL);
        sfo_value(sfo,"ATTRIBUTE",NULL,0,&attributes);
        uint64_t sdk=0;
        if (procparam) memcpy(&sdk,image+procparam+16,8); /* procparam: size, magic, count, sdk_version */
        BbGpuConfig gpu={title,serial,user_dir ? user_dir : "user",(uint32_t)sdk,attributes,1920,1080};
        gpu_enabled=1; /* page tracking starts while the rasterizer registers guest memory */
        if (bbgpu_init(&gpu)) fail("GPU initialization failed");
        printf("GPU: window and presenter ready; SDK 0x%08x, %u HLE symbols\n",(unsigned)sdk,bbgpu_symbol_count());
    }
    unsigned char *traps = allocate(round_page((import_count + 1) * 32));
#ifdef BB_CHECKPOINT_RUNTIME
    unsigned char *checkpoint_imports=NULL;
    uintptr_t *checkpoint_bound=NULL;
    if (bb_checkpoint_runtime_enabled()) {
        checkpoint_imports=allocate(round_page((import_count+1)*32));
        checkpoint_bound=calloc(import_count?import_count:1,sizeof(*checkpoint_bound));
        if (!checkpoint_bound) fail("cannot allocate checkpoint bindings");
    }
#endif
    unsigned char *data_traps = allocate((import_count + 1) * page_size);
    protect(data_traps, (import_count + 1) * page_size, 0);
    for (uint64_t i = 0; i < import_count; ++i) {
        unsigned char *t = traps + i * 32;
        /* SysV: mov edi, index; movabs rax, handler; jmp rax. No fake return values. */
        uint32_t index = (uint32_t)i;
        uintptr_t handler = (uintptr_t)unresolved;
        t[0] = 0x48; t[1] = 0x89; t[2] = 0xfe; /* mov rsi,rdi: preserve arg0 */
        t[3] = 0xbf; memcpy(t + 4, &index, 4);
        t[8] = 0x48; t[9] = 0xb8; memcpy(t + 10, &handler, 8);
        t[18] = 0xff; t[19] = 0xe0;
    }
    for (uint64_t i = 0; i < nr; ++i) {
        uintptr_t value = relocs[i].kind == 2 ? (uintptr_t)(data_traps + page_size * relocs[i].value + relocs[i].addend)
                        : relocs[i].kind == 1 ? (uintptr_t)(traps + 32 * relocs[i].value)
                        : (uintptr_t)image + relocs[i].value;
        if (relocs[i].kind) {
            uintptr_t resolved = runtime_resolve(names[relocs[i].value], relocs[i].kind == 2);
            if (resolved) {
                value = resolved + relocs[i].addend;
#ifdef BB_CHECKPOINT_RUNTIME
                if (checkpoint_imports && relocs[i].kind==1) {
                    uint64_t index=relocs[i].value;
                    if (relocs[i].addend) fail("checkpoint import has an unsupported addend");
                    if (!checkpoint_bound[index]) {
                        /* Adapters opt in individually once an interrupted
                         * operation can preserve its logical wait state. */
                        checkpoint_bound[index]=bb_checkpoint_runtime_import(checkpoint_imports+index*32,value,index+1,
                            bb_checkpoint_runtime_replayable_import(names[index]));
                        if (!checkpoint_bound[index]) fail("cannot bind checkpoint import");
                        bb_checkpoint_runtime_import_label(index+1,runtime_import_name(names[index]));
                    }
                    value=checkpoint_bound[index];
                }
#endif
            }
            else if (native_libc && bindings[relocs[i].value]) {
                uint64_t address=bindings[relocs[i].value];
                if (binding_kinds[relocs[i].value]!=relocs[i].kind || !mapped(segments,ns,address,relocs[i].addend+1)) fail("native export kind/range mismatch");
                value=(uintptr_t)image+address+relocs[i].addend;
            }
        }
        memcpy(image + relocs[i].target, &value, 8);
    }
    /* The Hunter's Dream backend reads the game's code before the patches change it. */
    bbnet_set_image(image, size);
    if (patch_file) apply_patches(patch_file, segments, ns, relocs, nr);
    printf("Guest TCB loads restored to Darwin FS segments: %" PRIu64 "\n",patch_tcb_loads(segments,ns));
    protect(traps, round_page((import_count + 1) * 32), 5);
#ifdef BB_CHECKPOINT_RUNTIME
    if (checkpoint_imports) {
        if (!bb_checkpoint_runtime_import(checkpoint_imports+import_count*32,(uintptr_t)guest_exit,import_count+1,0))
            fail("cannot bind checkpoint finalizer");
        protect(checkpoint_imports,round_page((import_count+1)*32),5);
    }
#endif
    protect(image, round_page(size), 0);
    int executable_entry = 0;
    for (uint64_t i = 0; i < ns; ++i) {
#ifndef BB_CPU_TRANSLATOR
        protect(image + segments[i].address, round_page(segments[i].size), (unsigned)segments[i].flags);
#else
        // Several 4 KiB ELF segments may share one 16 KiB host page. Union
        // their permissions instead of letting the last segment revoke them.
        uintptr_t begin=segments[i].address&~(page_size-1);
        size_t end=round_page(segments[i].address+segments[i].size);
        for (uintptr_t page=begin;page<end;page+=page_size) {
            unsigned flags=0;
            for (uint64_t j=0;j<ns;++j)
                if (segments[j].address<page+page_size && page<segments[j].address+segments[j].size)
                    flags|=(unsigned)segments[j].flags;
            protect(image+page,page_size,flags);
        }
        if (segments[i].flags&1) bbcpu_add_guest_code((uintptr_t)(image+segments[i].address),segments[i].size);
#endif
#ifdef BB_CHECKPOINT_RUNTIME
        if (checkpoint_imports && (segments[i].flags&1) &&
            !bb_checkpoint_runtime_code((uintptr_t)(image+segments[i].address),segments[i].size))
            fail("invalid checkpoint executable range");
#endif
        if ((segments[i].flags & 1) && entry >= segments[i].address && entry - segments[i].address < segments[i].size)
            executable_entry = 1;
    }
#ifdef BB_CPU_TRANSLATOR
    bbcpu_add_guest_code((uintptr_t)traps,round_page((import_count+1)*32));
    bbcpu_set_tls_displacement(0);
#endif
    if (!cpu_only) {
        extern void bbmac_features_segment(uint64_t,uint64_t,uint32_t);
        extern void bbmac_features_relative(uint64_t,uint64_t);
        extern void bbmac_features_install(void);
        for (uint64_t i=0;i<ns;++i) bbmac_features_segment(segments[i].address,segments[i].size,(uint32_t)segments[i].flags);
        for (uint64_t i=0;i<nr;++i) if (!relocs[i].kind) bbmac_features_relative(relocs[i].target,relocs[i].value);
        bbmac_features_install();
    }
    if (!executable_entry) fail("entry is not executable");
    printf("Mapped %" PRIu64 " bytes, %" PRIu64 " segments; applied %" PRIu64 " relocations\n", size, ns, nr);
    if (native_libc) {
        for (uint64_t m=0;m<module_count;++m) {
            int init_executable=0;
            for (uint64_t i=0;i<ns;++i)
                if ((segments[i].flags&1) && modules[m].init>=segments[i].address && modules[m].init-segments[i].address<segments[i].size) init_executable=1;
            if (!init_executable) fail("module init is not executable");
            if (modules[m].tls_module)
                runtime_set_module_tls(modules[m].tls_module,image+modules[m].tls_address,modules[m].tls_filesz,modules[m].tls_memsz);
        }
        runtime_set_main_tls(image+main_tls[0],main_tls[1],main_tls[2],main_tls[3]);
        runtime_thread_attach_main();
        runtime_set_procparam(image+procparam);
        /* Dependencies start in link order (libc first), as the PS4 dynamic linker does. */
        for (uint64_t m=0;m<module_count;++m) {
            printf("Starting linked module %" PRIu64 " at image offset 0x%" PRIx64 "; native bindings=%" PRIu64 "\n",m,modules[m].init,nb);
#ifndef BB_CPU_TRANSLATOR
            typedef int (ABI *ModuleInit)(uint64_t,void *,void *);
#endif
            int result;
#ifdef BB_CHECKPOINT_RUNTIME
            if (checkpoint_imports) result=(int)runtime_thread_run_guest(image+modules[m].init,NULL,NULL,8*1024*1024);
            else
#endif
            {
#ifdef BB_CPU_TRANSLATOR
                const uint64_t args[3]={0};
                result=(int)runtime_guest_call(image+modules[m].init,3,args);
#else
                result=((ModuleInit)(image+modules[m].init))(0,NULL,NULL);
#endif
            }
            printf("Module %" PRIu64 " initializer returned %d\n",m,result);
            if (result) fail("module initializer failed");
        }
    }
    bbnet_initialize(); /* the online session starts with the game */
    printf("Entering original x86-64 code at guest offset 0x%" PRIx64 "\n", entry);
    entered_game=1;
    struct { uint64_t argc; const char *argv[2]; } params = {1, {"/app0/eboot.bin", NULL}};
#ifdef BB_CHECKPOINT_RUNTIME
    if (checkpoint_imports) {
        /* argc, argv and its strings must not point into the native loader
         * stack or read-only host image when a guest continuation is saved. */
        unsigned char *args=runtime_low_map(16384,PROT_READ|PROT_WRITE);
        if (!args) fail("cannot allocate checkpoint entry arguments");
        memcpy(args,&params,sizeof(params));
        strcpy((char *)args+128,"/app0/eboot.bin");
        ((uint64_t *)args)[1]=(uintptr_t)(args+128);
        runtime_thread_run_guest(image+entry,args,checkpoint_imports+import_count*32,8*1024*1024);
        fail("entry unexpectedly returned");
    }
#endif
    /* The guest main thread runs on a stack below 1 TiB like PS4 stacks. */
    enum { MAIN_STACK=8*1024*1024 };
    unsigned char *stack=runtime_low_map(MAIN_STACK,PROT_READ|PROT_WRITE);
    if (!stack) fail("cannot allocate guest main stack");
#ifdef BB_CPU_TRANSLATOR
    const uint64_t entry_args[2]={(uintptr_t)&params,(uintptr_t)guest_exit};
    bbcpu_call_on_stack((uintptr_t)(image+entry),2,entry_args,stack,MAIN_STACK-64);
#else
    enter_on_stack(image+entry,&params,(void *)guest_exit,stack+MAIN_STACK-64);
#endif
    fail("entry unexpectedly returned");
}
