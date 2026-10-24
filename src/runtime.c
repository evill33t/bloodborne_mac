/* Narrow, explicit PS4 libc contracts. No automatic success stubs. */
#define _CRT_RAND_S
#include "runtime.h"
#include "runtime_modules.h"
#ifdef BB_CPU_TRANSLATOR
#include "cpu/bbcpu.h"
#include <sys/mman.h>
uint64_t bb_image_base; /* bbcpu fault diagnostics and optional record offsets */
#endif
#include "gpu/bbgpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>
#include <pthread.h>
#ifdef BB_CHECKPOINT_RUNTIME
#include <sys/mman.h>
#include <errno.h>
#endif

typedef struct {
    union { GuestCallback plain; void (ABI *with_arg)(void *); } callback;
    void *argument, *dso;
    int with_arg, active;
} ExitHandler;
static ExitHandler *handlers;
static size_t handler_count, handler_capacity, calls_init, calls_atexit, calls_cxa;
static uint64_t capabilities;
static _Atomic size_t guards_acquired, guards_released;
static uint64_t stack_canary;
#ifdef BB_CHECKPOINT_RUNTIME
typedef struct {uint64_t canary;int32_t need_libc_internal;uint32_t reserved;} CheckpointGlobals;
static CheckpointGlobals *checkpoint_globals;
static pthread_mutex_t checkpoint_globals_lock=PTHREAD_MUTEX_INITIALIZER;
static CheckpointGlobals *guest_globals(void) {
    pthread_mutex_lock(&checkpoint_globals_lock);
    if (!checkpoint_globals) {
        checkpoint_globals=runtime_low_map(16384,PROT_READ|PROT_WRITE);
        if (!checkpoint_globals) {pthread_mutex_unlock(&checkpoint_globals_lock);fputs("Cannot allocate stable guest import data\n",stderr);abort();}
        *checkpoint_globals=(CheckpointGlobals){.canary=stack_canary,.need_libc_internal=1};
    }
    CheckpointGlobals *result=checkpoint_globals;
    pthread_mutex_unlock(&checkpoint_globals_lock);return result;
}
#endif
static _Atomic size_t memory_calls;
/* Dynamic TLS of linked modules: module IDs 2..7 (1 is the eboot's static TLS). */
#define TLS_MODULES 8
static struct { const void *data; uint64_t filesz, memsz; } tls_modules[TLS_MODULES];
static _Thread_local unsigned char *tls_blocks[TLS_MODULES];
static void *process_param;
void runtime_set_procparam(void *param) { process_param=param; }
static ABI void *guest_procparam(void) { return process_param; }
static void **application_heap_api;
static ABI void guest_set_heap_api(void **api) {
    application_heap_api=api;
    puts("Runtime: application heap API registered");
}
void **runtime_application_heap_api(void) { return application_heap_api; }
void runtime_set_module_tls(uint64_t module, const void *data, uint64_t filesz, uint64_t memsz) {
    if (module<2 || module>=TLS_MODULES) { fputs("STOP: unsupported TLS module id\n",stderr); exit(21); }
    tls_modules[module].data=data; tls_modules[module].filesz=filesz; tls_modules[module].memsz=memsz;
}
void runtime_set_libc_tls(const void *data, uint64_t filesz, uint64_t memsz) { runtime_set_module_tls(2,data,filesz,memsz); }
static ABI void *guest_tls_get_addr(const uint64_t *index) {
    if (!index || index[0]<2 || index[0]>=TLS_MODULES || !tls_modules[index[0]].data || index[1]>=tls_modules[index[0]].memsz) {
        fputs("STOP: unsupported TLS module/offset\n",stderr); exit(21);
    }
    unsigned char **block=&tls_blocks[index[0]];
#ifdef BB_CHECKPOINT_RUNTIME
    BbCheckpointThreadLocal *local=NULL;
    if (bb_checkpoint_runtime_enabled()) {
        local=runtime_thread_checkpoint_local();
        tls_blocks[index[0]]=(unsigned char *)(uintptr_t)local->module_tls[index[0]];
    }
#endif
    if (!*block) {
#ifdef BB_CPU_TRANSLATOR
        *block=runtime_low_map(tls_modules[index[0]].memsz,PROT_READ|PROT_WRITE);
#else
#ifdef BB_CHECKPOINT_RUNTIME
        if (local) *block=runtime_low_map(tls_modules[index[0]].memsz,PROT_READ|PROT_WRITE);
        else
#endif
        *block=calloc(1,tls_modules[index[0]].memsz);
#endif
        if (!*block) { fputs("Cannot allocate module TLS\n",stderr); exit(1); }
        memcpy(*block,tls_modules[index[0]].data,tls_modules[index[0]].filesz);
#ifdef BB_CHECKPOINT_RUNTIME
        if (local) {
            local->module_tls[index[0]]=(uintptr_t)*block;
            local->module_tls_size[index[0]]=tls_modules[index[0]].memsz;
        }
#endif
    }
    return *block+index[1];
}
#ifdef BB_CHECKPOINT_RUNTIME
void runtime_checkpoint_bind_module_tls(const BbCheckpointThreadLocal *local) {
    for (unsigned i=0;i<TLS_MODULES;++i) tls_blocks[i]=(unsigned char *)(uintptr_t)local->module_tls[i];
}
uint64_t runtime_checkpoint_module_tls_size(unsigned module) {return module<TLS_MODULES?tls_modules[module].memsz:0;}
#endif

void runtime_start(uint64_t flags) {
    capabilities = flags;
    runtime_modules_install(NULL,0);
    if (!(flags & 1)) return;
#ifdef _WIN32
    unsigned int halves[2];
    if (rand_s(&halves[0]) || rand_s(&halves[1])) { fputs("Cannot initialize stack canary\n", stderr); exit(1); }
    memcpy(&stack_canary, halves, sizeof(stack_canary));
#else
    FILE *random = fopen("/dev/urandom", "rb");
    if (!random || fread(&stack_canary, sizeof(stack_canary), 1, random) != 1) {
        fputs("Cannot initialize stack canary\n", stderr); exit(1);
    }
    fclose(random);
#endif
    stack_canary &= ~UINT64_C(255);
}
void runtime_report(void) {
    runtime_modules_report();
    printf("Runtime: _init_env=%zu, atexit=%zu, __cxa_atexit=%zu, registered handlers=%zu\n",
           calls_init, calls_atexit, calls_cxa, handler_count);
    printf("Runtime: static guards acquired=%zu, released=%zu\n", (size_t)guards_acquired, (size_t)guards_released);
    runtime_thread_report();
    runtime_mutex_report();
    runtime_rwlock_report();
    runtime_sema_report();
    runtime_content_report();
    runtime_memory_report();
    runtime_ajm_report();
    runtime_audio_report();
    runtime_pad_report();
    runtime_savedata_report();
    runtime_file_report();
    printf("Runtime: memory operations=%zu\n", (size_t)memory_calls);
}
static ABI void init_env(void) {
    /* prepare.py verified the supplied libc export consists of exactly C3. */
    ++calls_init;
    puts("Runtime: _init_env returned (verified libc implementation: RET)");
}
static pthread_mutex_t handler_lock = PTHREAD_MUTEX_INITIALIZER;
static int register_handler(ExitHandler value) {
    pthread_mutex_lock(&handler_lock);
    if (handler_count == handler_capacity) {
        size_t capacity = handler_capacity ? handler_capacity * 2 : 64;
        ExitHandler *next = capacity > 1024 * 1024 ? NULL : realloc(handlers, capacity * sizeof(*handlers));
        if (!next) { pthread_mutex_unlock(&handler_lock); return -1; }
        handlers = next; handler_capacity = capacity;
    }
    value.active = 1; handlers[handler_count++] = value;
    pthread_mutex_unlock(&handler_lock);
    return 0;
}
uint64_t runtime_guest_call(const void *fn,int count,const uint64_t *args) {
    if (!fn || count<0 || count>6 || (count && !args)) abort();
#ifdef BB_CPU_TRANSLATOR
    if (bbcpu_is_guest((uintptr_t)fn)) return bbcpu_call((uintptr_t)fn,count,args);
#endif
    uint64_t a[6]={0};
    if (count) memcpy(a,args,(size_t)count*sizeof(*a));
    return ((uint64_t (ABI *)(uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t))fn)
        (a[0],a[1],a[2],a[3],a[4],a[5]);
}
static ABI int guest_atexit(GuestCallback callback) {
    if (!callback) return -1;
    ++calls_atexit;
    return register_handler((ExitHandler){.callback.plain=callback});
}
static ABI int guest_cxa_atexit(void (ABI *callback)(void *), void *argument, void *dso) {
    if (!callback) return -1;
    ++calls_cxa;
    return register_handler((ExitHandler){.callback.with_arg=callback, .argument=argument, .dso=dso, .with_arg=1});
}
void runtime_finalize(void *dso) {
    /* Mark before invoking: repeated or recursive finalization cannot run twice.
       Restart at the end to include handlers registered by a destructor. */
    for (;;) {
        pthread_mutex_lock(&handler_lock);
        size_t i = handler_count;
        while (i && (!handlers[i-1].active || (dso && handlers[i-1].dso != dso))) --i;
        if (!i) { pthread_mutex_unlock(&handler_lock); return; }
        ExitHandler handler = handlers[i-1]; handlers[i-1].active = 0;
        pthread_mutex_unlock(&handler_lock);
        uint64_t argument=(uintptr_t)handler.argument;
        if (handler.with_arg) runtime_guest_call((const void *)handler.callback.with_arg,1,&argument);
        else runtime_guest_call((const void *)handler.callback.plain,0,NULL);
    }
}
static ABI void guest_finalize(void *dso) { runtime_finalize(dso); }
static _Atomic uint32_t next_guard_owner=1;
static _Thread_local uint32_t guard_owner;
static ABI int guard_acquire(uint64_t *guard) {
    _Atomic uint64_t *state = (_Atomic uint64_t *)guard;
    /* Dump libc uses the low byte for completion and the dword at +4 as lock.
       The lock word holds a per-thread owner tag so recursion is detectable. */
    uint32_t *owner=&guard_owner;
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) owner=&runtime_thread_checkpoint_local()->guard_owner;
#endif
    if (!*owner) *owner = atomic_fetch_add(&next_guard_owner, 1);
    for (;;) {
        uint64_t expected = 0;
        if (atomic_load_explicit(state, memory_order_acquire) & 1) return 0;
        if (atomic_compare_exchange_strong_explicit(state, &expected, (uint64_t)*owner << 32,
                                                    memory_order_acq_rel, memory_order_acquire)) break;
        if (expected & 1) return 0;
        if ((uint32_t)(expected >> 32) == *owner) {
            fputs("STOP: recursive/concurrent static initialization is not supported yet\n", stderr);
            exit(21);
        }
        sched_yield(); /* another thread is running the initializer */
    }
    atomic_fetch_add(&guards_acquired, 1);
    return 1;
}
static ABI void guard_release(uint64_t *guard) {
    atomic_store_explicit((_Atomic uint64_t *)guard, 1, memory_order_release);
    atomic_fetch_add(&guards_released, 1);
}
static ABI void guard_abort(uint64_t *guard) {
    atomic_fetch_and_explicit((_Atomic uint64_t *)guard, UINT64_C(0xffffffff), memory_order_release);
}
static ABI __attribute__((noreturn)) void stack_fail(void) {
    fputs("STOP: guest stack protector detected corruption\n", stderr);
    exit(22);
}
static ABI void *guest_memset(void *dst, int value, size_t size) {
    atomic_fetch_add_explicit(&memory_calls,1,memory_order_relaxed); return memset(dst, value, size);
}
static ABI void *guest_memcpy(void *dst, const void *src, size_t size) {
    atomic_fetch_add_explicit(&memory_calls,1,memory_order_relaxed); return memcpy(dst, src, size);
}
static ABI void *guest_memmove(void *dst, const void *src, size_t size) {
    atomic_fetch_add_explicit(&memory_calls,1,memory_order_relaxed); return memmove(dst, src, size);
}
static ABI int guest_memcmp(const void *a, const void *b, size_t size) {
    atomic_fetch_add_explicit(&memory_calls,1,memory_order_relaxed); return memcmp(a, b, size);
}
static ABI size_t guest_strlen(const char *text) { return strlen(text); }
static ABI __attribute__((noreturn)) void guest_libc_exit(int status) {
    printf("Runtime: guest requested exit(%d)\n", status);
    runtime_finalize(NULL);
    runtime_report();
    exit(status);
}
uintptr_t runtime_resolve(const char *name, int is_data) {
    if (!(capabilities & 1)) return 0;
    if (is_data) {
        static int32_t need_libc_internal = 1; /* SDK marker variable referenced by Fios2 */
#ifdef BB_CHECKPOINT_RUNTIME
        if (bb_checkpoint_runtime_enabled()) {
            if (!strcmp(name,"f7uOxY9mM1U#p#J")) return (uintptr_t)&guest_globals()->canary;
            if (!strcmp(name,"ZT4ODD2Ts9o#libSceLibcInternal")) return (uintptr_t)&guest_globals()->need_libc_internal;
        }
#endif
        if (!strcmp(name, "f7uOxY9mM1U#p#J")) return (uintptr_t)&stack_canary;
        if (!strcmp(name, "ZT4ODD2Ts9o#libSceLibcInternal")) return (uintptr_t)&need_libc_internal;
        return 0;
    }
    /* Exact scoped imports for CUSA03173; the suffix identifies library/module. */
    if (!strcmp(name, "bzQExy189ZI#q#q")) return (uintptr_t)init_env;
    if (!strcmp(name, "8G2LB+A3rzg#q#q")) return (uintptr_t)guest_atexit;
    if (!strcmp(name, "tsvEmnenz48#q#q")) return (uintptr_t)guest_cxa_atexit;
    if (!strcmp(name, "uMei1W9uyNo#q#q")) return (uintptr_t)guest_libc_exit;
    /* Not imported by the current eboot, exposed for ABI tests/future use. */
    if (!strcmp(name, "H2e8t5ScQGc#q#q")) return (uintptr_t)guest_finalize;
    if (!strcmp(name, "3GPpjQdAMTw#q#q")) return (uintptr_t)guard_acquire;
    if (!strcmp(name, "9rAeANT2tyE#q#q")) return (uintptr_t)guard_release;
    if (!strcmp(name, "2emaaluWzUw#q#q")) return (uintptr_t)guard_abort;
    if (!strcmp(name, "Ou3iL1abvng#p#J")) return (uintptr_t)stack_fail;
    if (!strcmp(name, "8zTFvBIAIN8#q#q")) return (uintptr_t)guest_memset;
    if (!strcmp(name, "Q3VBxCXhUHs#q#q")) return (uintptr_t)guest_memcpy;
    if (!strcmp(name, "+P6FRGH4LfA#q#q")) return (uintptr_t)guest_memmove;
    if (!strcmp(name, "DfivPArhucg#q#q")) return (uintptr_t)guest_memcmp;
    if (!strcmp(name, "j4ViWNHEgww#q#q")) return (uintptr_t)guest_strlen;
    if (!strcmp(name, "vNe1w4diLCs#p#J")) return (uintptr_t)guest_tls_get_addr;
    if (!strcmp(name, "959qrazPIrg#p#J")) return (uintptr_t)guest_procparam;
    if (!strcmp(name, "p5EcQeEeJAE#p#J")) return (uintptr_t)guest_set_heap_api;
    uintptr_t mutex = runtime_mutex_resolve(name);
    if (mutex) return mutex;
    uintptr_t thread = runtime_thread_resolve(name);
    if (thread) return thread;
    uintptr_t content=runtime_content_resolve(name);
    if (content) return content;
    uintptr_t timer = runtime_time_resolve(name);
    if (timer) return timer;
    uintptr_t sema = runtime_sema_resolve(name);
    if (sema) return sema;
    uintptr_t rwlock = runtime_rwlock_resolve(name);
    if (rwlock) return rwlock;
    uintptr_t memory = runtime_memory_resolve(name);
    if (memory) return memory;
    uintptr_t kernel = runtime_kernel_resolve(name);
    if (kernel) return kernel;
    uintptr_t services = runtime_services_resolve(name);
    if (services) return services;
    uintptr_t ajm = runtime_ajm_resolve(name);
    if (ajm) return ajm;
    uintptr_t audio = runtime_audio_resolve(name);
    if (audio) return audio;
    uintptr_t pad = runtime_pad_resolve(name);
    if (pad) return pad;
    uintptr_t rtc = runtime_rtc_resolve(name);
    if (rtc) return rtc;
    uintptr_t save = runtime_savedata_resolve(name);
    if (save) return save;
    uintptr_t file = runtime_file_resolve(name);
    if (file) return file;
    return bbgpu_resolve(name);
}
static const struct { const char *nid, *symbol; } import_names[]={
#include "import_names.inc"
};
/* Symbol name for a scoped NID, or NULL when this eboot/libc never imports it. */
const char *runtime_symbol(const char *nid) {
    for (size_t i=0;i<sizeof(import_names)/sizeof(*import_names);++i)
        if (!strcmp(nid,import_names[i].nid)) return import_names[i].symbol;
    return NULL;
}
/* Same symbol name means same contract whether imported from libkernel or
 * libScePosix, so newer modules resolve by name through runtime_symbol. */
uintptr_t runtime_lookup(const RuntimeExport *table,size_t count,const char *nid) {
    const char *symbol=runtime_symbol(nid);
    if (!symbol) return 0;
    for (size_t i=0;i<count;++i) if (!strcmp(symbol,table[i].name)) return (uintptr_t)table[i].function;
    return 0;
}
const char *runtime_import_name(const char *name) {
    const char *symbol=runtime_symbol(name);
    return symbol ? symbol : "name not resolved; see analysis.json import_name_hints";
}

#ifdef BB_CHECKPOINT_RUNTIME
_Static_assert(sizeof(BbCheckpointProcessRecord)==72,"checkpoint process record size");
_Static_assert(sizeof(BbCheckpointExitRecord)==32,"checkpoint exit record size");
int runtime_checkpoint_process_bind_globals(uint64_t address) {
    if (!bb_checkpoint_runtime_enabled() || address<UINT64_C(0x7000000000) ||
        address>=UINT64_C(0x8000000000) || address%16384) return EINVAL;
    pthread_mutex_lock(&checkpoint_globals_lock);
    int result=0;
    if (checkpoint_globals) result=(uintptr_t)checkpoint_globals==address?0:EBUSY;
    else {
        checkpoint_globals=runtime_low_map_at(address,16384,PROT_READ|PROT_WRITE);
        if (!checkpoint_globals) result=ENOMEM;
        else *checkpoint_globals=(CheckpointGlobals){.canary=stack_canary,.need_libc_internal=1};
    }
    pthread_mutex_unlock(&checkpoint_globals_lock);return result;
}
int runtime_checkpoint_process_export(BbCheckpointProcessRecord *record,BbCheckpointExitRecord *entries,size_t capacity) {
    if (!record || !bb_checkpoint_runtime_enabled()) return EINVAL;
    pthread_mutex_lock(&handler_lock);
    if (handler_count>1024*1024) {pthread_mutex_unlock(&handler_lock);return EOVERFLOW;}
    BbCheckpointProcessRecord saved={.version=1,.capabilities=capabilities,.globals=(uintptr_t)checkpoint_globals,
        .canary=checkpoint_globals?checkpoint_globals->canary:stack_canary,.process_param=(uintptr_t)process_param,
        .heap_api=(uintptr_t)application_heap_api,.need_libc_internal=checkpoint_globals?checkpoint_globals->need_libc_internal:1,
        .handlers=(uint32_t)handler_count};
    if (saved.process_param>=UINT64_C(1)<<40 || saved.heap_api>=UINT64_C(1)<<40) {pthread_mutex_unlock(&handler_lock);return EINVAL;}
    if (entries && capacity<handler_count) {pthread_mutex_unlock(&handler_lock);return ENOSPC;}
    if (entries) for (size_t i=0;i<handler_count;++i) {
        const ExitHandler *h=handlers+i;BbCheckpointExitRecord e={0};
        if (h->active) {
            e.callback=h->with_arg?(uintptr_t)h->callback.with_arg:(uintptr_t)h->callback.plain;
            e.argument=(uintptr_t)h->argument;e.dso=(uintptr_t)h->dso;e.flags=2u|(h->with_arg?1u:0u);
            if (!e.callback || e.callback>=UINT64_C(1)<<40 || e.dso>=UINT64_C(1)<<40) {pthread_mutex_unlock(&handler_lock);return EINVAL;}
        }
        entries[i]=e;
    }
    *record=saved;pthread_mutex_unlock(&handler_lock);return 0;
}
int runtime_checkpoint_process_restore(const BbCheckpointProcessRecord *saved,const BbCheckpointExitRecord *entries) {
    if (!saved || !bb_checkpoint_runtime_enabled() || saved->version!=1 || saved->reserved ||
        saved->reserved2[0] || saved->reserved2[1] || saved->capabilities!=capabilities || saved->handlers>1024*1024 ||
        (saved->handlers && !entries) || saved->process_param>=UINT64_C(1)<<40 || saved->heap_api>=UINT64_C(1)<<40 ||
        (saved->globals && (saved->globals<UINT64_C(0x7000000000) || saved->globals>=UINT64_C(0x8000000000) || saved->globals%16384))) return EINVAL;
    for (size_t i=0;i<saved->handlers;++i) {
        const BbCheckpointExitRecord *e=entries+i;
        if (e->reserved || e->flags&~3u || (!(e->flags&2) && (e->callback || e->argument || e->dso || e->flags)) ||
            ((e->flags&2) && (!e->callback || e->callback>=UINT64_C(1)<<40 || e->dso>=UINT64_C(1)<<40))) return EINVAL;
    }
    ExitHandler *rebuilt=saved->handlers?calloc(saved->handlers,sizeof(*rebuilt)):NULL;
    if (saved->handlers && !rebuilt) return ENOMEM;
    for (size_t i=0;i<saved->handlers;++i) {
        const BbCheckpointExitRecord *e=entries+i;
        if (e->flags&1) rebuilt[i].callback.with_arg=(void (ABI *)(void *))(uintptr_t)e->callback;
        else rebuilt[i].callback.plain=(GuestCallback)(uintptr_t)e->callback;
        rebuilt[i].argument=(void *)(uintptr_t)e->argument;rebuilt[i].dso=(void *)(uintptr_t)e->dso;
        rebuilt[i].with_arg=(e->flags&1)!=0;rebuilt[i].active=(e->flags&2)!=0;
    }
    pthread_mutex_lock(&handler_lock);
    if (handler_count || application_heap_api) {pthread_mutex_unlock(&handler_lock);free(rebuilt);return EBUSY;}
    if (saved->globals) {
        int error=runtime_checkpoint_process_bind_globals(saved->globals);
        if (error) {pthread_mutex_unlock(&handler_lock);free(rebuilt);return error;}
    } else if (checkpoint_globals) {pthread_mutex_unlock(&handler_lock);free(rebuilt);return EINVAL;}
    free(handlers);handlers=rebuilt;handler_count=handler_capacity=saved->handlers;
    process_param=(void *)(uintptr_t)saved->process_param;application_heap_api=(void **)(uintptr_t)saved->heap_api;
    stack_canary=saved->canary;
    if (checkpoint_globals) {checkpoint_globals->canary=saved->canary;checkpoint_globals->need_libc_internal=saved->need_libc_internal;}
    pthread_mutex_unlock(&handler_lock);return 0;
}
#endif
