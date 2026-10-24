/* Guest threads on host pthreads. Each guest thread owns a FreeBSD-style TCB
 * (variant II: static TLS below the TCB). The loader rewrites the eboot's
 * `mov rax, fs:[0]` into `mov rax, gs:[0]`, so GS base = guest TCB while glibc
 * keeps FS. Priorities/affinity are recorded, not enforced by a PS4 scheduler.
 * Windows: GS is the TEB and cannot be moved. The guest TCB lives in a TLS slot of the TEB
 * instead, and the loader rewrites the load to `mov rax, gs:[0x1480+8*slot]` (TlsSlots),
 * the same 9-byte instruction with another displacement. */
#define _GNU_SOURCE
#include "runtime.h"
#ifdef BB_CPU_TRANSLATOR
#include "cpu/bbcpu.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <setjmp.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#define ERR(n) ((int32_t)(UINT32_C(0x80020000)|(n)))
#define ATTR_MAGIC UINT32_C(0x41545452)
#define STACK_MARGIN (256*1024)
#define MIN_STACK (64*1024)
#define DEFAULT_STACK (1024*1024)
#define DEFAULT_PRIO 700

typedef void *(ABI *GuestEntry)(void *);
typedef struct ThreadAttr {
    uint32_t magic;
    int detached, policy, prio, inherit;
    uint64_t stack, guard, affinity;
    struct ThreadAttr *next;
} ThreadAttr;
typedef struct GuestThread {
    /* Guest-visible TCB is allocated separately; this is the ScePthread handle. */
    uint64_t *tcb;
    unsigned char *tls_block;
    pthread_t host;
    GuestEntry entry;
    void *argument, *result;
    ThreadAttr attr;
    char name[32];
    int detached, finished, joined, host_owned;
#ifdef BB_CPU_TRANSLATOR
    void *translated_stack;
    size_t translated_stack_size;
#endif
#ifdef BB_CHECKPOINT_RUNTIME
    void *guest_stack;
    size_t guest_stack_size;
    size_t tls_size;
    BbCheckpointThreadLocal *local;
    BbCheckpointCpu resume_cpu;
    int restored,native_started;
    uint64_t module_mappings[8],module_sizes[8];
#endif
    jmp_buf exit_jump;
    struct GuestThread *next;
} GuestThread;
#ifdef BB_CHECKPOINT_RUNTIME
_Static_assert(sizeof(GuestThread)<=16384,"checkpoint thread identity occupies one owned page");
#endif

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static ThreadAttr *attributes;
static GuestThread *threads;
static _Thread_local GuestThread *current;
static _Thread_local int32_t guest_errno;
static const unsigned char *tls_template;
static uint64_t tls_filesz, tls_memsz, tls_align=16;
static size_t created, joined_count, exited;
#ifdef BB_CHECKPOINT_RUNTIME
static pthread_cond_t restore_changed=PTHREAD_COND_INITIALIZER;
static pthread_cond_t thread_changed=PTHREAD_COND_INITIALIZER;
static int restore_ready;
#endif

void runtime_set_main_tls(const void *data,uint64_t filesz,uint64_t memsz,uint64_t align) {
    tls_template=data; tls_filesz=filesz; tls_memsz=memsz; tls_align=align ? align : 16;
}
static uint64_t tls_offset(void) { return (tls_memsz+tls_align-1)&~(tls_align-1); }
static void set_gs(void *base) { bb_macos_set_guest_tls(base); }
#define tls_block_alloc(size) runtime_low_map(size,PROT_READ|PROT_WRITE)
/* Build TCB/static TLS for the calling host thread and point GS at it. */
static void attach(GuestThread *t) {
    uint64_t offset=tls_offset();
    size_t total=offset+256;
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) total=offset+512+sizeof(BbCheckpointThreadLocal);
#endif
    unsigned char *block=tls_block_alloc((total+63)&~(size_t)63);
    if (!block) { fputs("Cannot allocate guest TLS\n",stderr); exit(1); }
    memset(block,0,total);
    if (tls_filesz) memcpy(block,tls_template,tls_filesz);
    uint64_t *tcb=(uint64_t *)(block+offset);
    static uint64_t dtv[3];
    tcb[0]=(uint64_t)(uintptr_t)tcb;         /* tcb_self */
    tcb[1]=(uint64_t)(uintptr_t)dtv;         /* tcb_dtv (static module only) */
    tcb[2]=(uint64_t)(uintptr_t)t;           /* tcb_thread */
    t->tls_block=block; t->tcb=tcb;
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) {
        t->tls_size=(total+16383)&~(size_t)16383;
        tcb[1]=(uintptr_t)(block+offset+256);
        t->local=(BbCheckpointThreadLocal *)(block+offset+512);
        t->local->guard_owner=(uint32_t)(((uintptr_t)t-UINT64_C(0x7000000000))/16384+1);
    }
#endif
    set_gs(tcb);
    current=t;
}
static GuestThread *new_thread(void) {
    /* Bloodborne packs the thread identity into 40-bit lock-owner fields.
     * Darwin's malloc may return a higher address even in a non-PIE process. */
    GuestThread *t=runtime_low_map(sizeof(*t),PROT_READ|PROT_WRITE);
    if (!t) return NULL;
    t->attr=(ThreadAttr){.magic=ATTR_MAGIC,.policy=1,.prio=DEFAULT_PRIO,.stack=DEFAULT_STACK,.affinity=0x7f};
    return t;
}
static void publish(GuestThread *t) {
    pthread_mutex_lock(&lock); t->next=threads; threads=t; pthread_mutex_unlock(&lock);
}
/* Host threads that call into guest code without being created by the guest
 * (the main thread, test threads) get a record on first use. */
GuestThread *runtime_thread_current(void) {
    if (current) return current;
    GuestThread *t=new_thread();
    if (!t) { fputs("Cannot allocate guest thread\n",stderr); exit(1); }
    t->host=pthread_self(); t->host_owned=1;
    snprintf(t->name,sizeof(t->name),"host");
    attach(t); publish(t);
    return t;
}
/* Host threads that call guest code (HLE decoders invoking guest callbacks) need
 * a TCB/TLS like guest threads; the record lives until process exit. */
void runtime_thread_attach_host(const char *name) {
    GuestThread *t=runtime_thread_current();
    snprintf(t->name,sizeof(t->name),"%s",name ? name : "host");
}
void runtime_thread_attach_main(void) {
    GuestThread *t=runtime_thread_current();
    snprintf(t->name,sizeof(t->name),"main");
}
static GuestThread *find_thread(void *handle) {
    GuestThread *found=NULL;
    pthread_mutex_lock(&lock);
    for (GuestThread *t=threads;t;t=t->next) if (t==handle) { found=t; break; }
    pthread_mutex_unlock(&lock);
    return found;
}
static ThreadAttr *find_attr(ThreadAttr **slot) {
    if (!slot || !*slot) return NULL;
    ThreadAttr *found=NULL;
    pthread_mutex_lock(&lock);
    for (ThreadAttr *a=attributes;a;a=a->next) if (a==*slot && a->magic==ATTR_MAGIC) { found=a; break; }
    pthread_mutex_unlock(&lock);
    return found;
}

static ABI void *thread_self(void) { return runtime_thread_current(); }
int32_t *runtime_errno(void) {
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) return &runtime_thread_checkpoint_local()->error;
#endif
    return &guest_errno;
}
static ABI int32_t *guest_error(void) { return runtime_errno(); }

static ABI int32_t attr_init(ThreadAttr **out) {
    if (!out) return ERR(22);
    ThreadAttr *a;
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) a=runtime_low_map(sizeof(*a),PROT_READ|PROT_WRITE);
    else
#endif
        a=calloc(1,sizeof(*a));
    if (!a) return ERR(12);
    *a=(ThreadAttr){.magic=ATTR_MAGIC,.policy=1,.prio=DEFAULT_PRIO,.inherit=4,.stack=DEFAULT_STACK,.guard=4096,.affinity=0x7f};
    pthread_mutex_lock(&lock); a->next=attributes; attributes=a; pthread_mutex_unlock(&lock);
    *out=a; return 0;
}
static ABI int32_t attr_destroy(ThreadAttr **slot) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    pthread_mutex_lock(&lock);
    ThreadAttr **link=&attributes;
    while (*link!=a) link=&(*link)->next;
    *link=a->next; a->magic=0;
    pthread_mutex_unlock(&lock);
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) munmap(a,16384);
    else
#endif
        free(a);
    *slot=NULL; return 0;
}
static ABI int32_t attr_get(void *thread,ThreadAttr **out) {
    ThreadAttr *a=find_attr(out);
    if (!a || !thread) return ERR(22);
    GuestThread *t=find_thread(thread);
    if (!t) return ERR(3);
    ThreadAttr *next=a->next;
    *a=t->attr; a->magic=ATTR_MAGIC; a->next=next;
    a->detached=t->detached;
    return 0;
}
static ABI int32_t attr_set_stack(ThreadAttr **slot,uint64_t size) {
    ThreadAttr *a=find_attr(slot);
    if (!a || size<16384) return ERR(22);
    a->stack=size; return 0;
}
static ABI int32_t attr_get_stack(ThreadAttr **slot,uint64_t *size) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !size) return ERR(22);
    *size=a->stack; return 0;
}
static ABI int32_t attr_set_detach(ThreadAttr **slot,int state) {
    ThreadAttr *a=find_attr(slot);
    if (!a || (state!=0 && state!=1)) return ERR(22);
    a->detached=state; return 0;
}
static ABI int32_t attr_get_detach(ThreadAttr **slot,int *state) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !state) return ERR(22);
    *state=a->detached; return 0;
}
static ABI int32_t attr_set_policy(ThreadAttr **slot,int policy) {
    ThreadAttr *a=find_attr(slot);
    if (!a || policy<1 || policy>3) return ERR(22);
    a->policy=policy; return 0;
}
static ABI int32_t attr_set_inherit(ThreadAttr **slot,int inherit) {
    ThreadAttr *a=find_attr(slot);
    if (!a || (inherit!=0 && inherit!=4)) return ERR(22);
    a->inherit=inherit; return 0;
}
static ABI int32_t attr_set_param(ThreadAttr **slot,const int *param) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !param) return ERR(22);
    a->prio=*param; return 0;
}
static ABI int32_t attr_get_param(ThreadAttr **slot,int *param) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !param) return ERR(22);
    *param=a->prio; return 0;
}
static ABI int32_t attr_set_affinity(ThreadAttr **slot,uint64_t mask) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    a->affinity=mask; return 0;
}
static ABI int32_t attr_affinity(ThreadAttr **slot,uint64_t *mask) {
    ThreadAttr *a=find_attr(slot);
    if (!a || !mask) return ERR(22);
    *mask=a->affinity; return 0;
}
static ABI int32_t attr_set_guard(ThreadAttr **slot,uint64_t size) {
    ThreadAttr *a=find_attr(slot);
    if (!a) return ERR(22);
    a->guard=size; return 0;
}

/* Linux thread names hold 15 characters; longer ones would be rejected. */
static void set_host_name(const char *name) {
    char host[16]={0};
    memcpy(host,name,strnlen(name,sizeof(host)-1));
    pthread_setname_np(host);
}
#define exit_setjmp(t) setjmp((t)->exit_jump)
#define exit_longjmp(t) longjmp((t)->exit_jump,1)
static void *host_start(void *p) {
    GuestThread *t=p;
#ifdef BB_CHECKPOINT_RUNTIME
    if (t->restored) {
        pthread_mutex_lock(&lock);
        while (!restore_ready) pthread_cond_wait(&restore_changed,&lock);
        pthread_mutex_unlock(&lock);
        current=t;set_gs(t->tcb);runtime_checkpoint_bind_module_tls(t->local);
    } else
#endif
        attach(t);
    set_host_name(t->name);
    if (!exit_setjmp(t)) {
#ifdef BB_CHECKPOINT_RUNTIME
        if (bb_checkpoint_runtime_enabled())
            t->result=(void *)(uintptr_t)(t->restored ?
                bb_checkpoint_runtime_run((uintptr_t)t,(void *)t->entry,t->argument,NULL,t->guest_stack,t->guest_stack_size,&t->resume_cpu) :
                runtime_thread_run_guest((void *)t->entry,t->argument,NULL,t->attr.stack+STACK_MARGIN));
        else
#endif
        {
#ifdef BB_CPU_TRANSLATOR
            uint64_t argument=(uintptr_t)t->argument;
            bbcpu_set_thread_label(t->name);
            t->result=(void *)(uintptr_t)bbcpu_call_on_stack((uintptr_t)t->entry,1,&argument,
                t->translated_stack,t->translated_stack_size);
#else
            t->result=t->entry(t->argument);
#endif
        }
    }
    runtime_thread_keys_cleanup();
    pthread_mutex_lock(&lock); t->finished=1; ++exited;
#ifdef BB_CHECKPOINT_RUNTIME
    pthread_cond_broadcast(&thread_changed);
#endif
    pthread_mutex_unlock(&lock);
    return t->result;
}
static int32_t create(GuestThread **out,ThreadAttr **attr_slot,GuestEntry entry,void *argument,const char *name) {
    if (!out || !entry) return ERR(22);
    ThreadAttr *a=NULL;
    if (attr_slot && !(a=find_attr(attr_slot))) return ERR(22);
    GuestThread *t=new_thread();
    if (!t) return ERR(12);
    if (a) { t->attr=*a; t->attr.next=NULL; }
    t->entry=entry; t->argument=argument; t->detached=t->attr.detached;
    snprintf(t->name,sizeof(t->name),"%s",name ? name : "guest");
    pthread_attr_t host;
    pthread_attr_init(&host);
    uint64_t stack=t->attr.stack<MIN_STACK ? MIN_STACK : t->attr.stack;
    /* Stacks below 1 TiB as on PS4; guest code may pack stack addresses. */
    size_t stack_bytes=(size_t)stack+STACK_MARGIN;
#ifdef BB_CPU_TRANSLATOR
    t->translated_stack=runtime_low_map(stack_bytes,PROT_READ|PROT_WRITE);
    if (!t->translated_stack) { pthread_attr_destroy(&host); return ERR(12); }
    t->translated_stack_size=stack_bytes;
    pthread_attr_setstacksize(&host,stack_bytes);
#else
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) pthread_attr_setstacksize(&host,stack_bytes);
    else
#endif
    {
        void *stack_memory=runtime_low_map(stack_bytes,PROT_READ|PROT_WRITE);
        if (stack_memory) pthread_attr_setstack(&host,stack_memory,stack_bytes);
        else pthread_attr_setstacksize(&host,stack_bytes);
    }
#endif
    publish(t);
    /* Publish the handle before the thread can run and inspect itself. */
    *out=t;
    int e=pthread_create(&t->host,&host,host_start,t);
    pthread_attr_destroy(&host);
    if (e) { fprintf(stderr,"STOP: host pthread_create failed: %d\n",e); exit(21); }
#ifdef BB_CHECKPOINT_RUNTIME
    t->native_started=1;
#endif
    if (t->detached) pthread_detach(t->host);
    pthread_mutex_lock(&lock); ++created; pthread_mutex_unlock(&lock);
    printf("Runtime: guest thread '%s' created (stack=%llu, prio=%d)\n",t->name,(unsigned long long)stack,t->attr.prio);
    return 0;
}
static ABI int32_t thread_create(GuestThread **out,ThreadAttr **attr,GuestEntry entry,void *argument,const char *name) {
    return create(out,attr,entry,argument,name);
}
static ABI int32_t thread_join(GuestThread *t,void **result) {
    if (!find_thread(t) || t->host_owned) return ERR(3);
    if (t==current) return ERR(11);
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_enabled()) {
        pthread_mutex_lock(&lock);
        if (t->detached || t->joined) {pthread_mutex_unlock(&lock);return ERR(22);}
        t->joined=1;
        while (!t->finished) {
            if (bb_checkpoint_runtime_requested() && bb_checkpoint_runtime_can_replay()) {
                t->joined=0;pthread_mutex_unlock(&lock);
                if (!bb_checkpoint_runtime_replay()) abort();
                return 0;
            }
            struct timespec deadline;clock_gettime(CLOCK_REALTIME,&deadline);
            deadline.tv_nsec+=2000000;
            if (deadline.tv_nsec>=1000000000) {++deadline.tv_sec;deadline.tv_nsec-=1000000000;}
            int e=pthread_cond_timedwait(&thread_changed,&lock,&deadline);
            if (e && e!=ETIMEDOUT) {pthread_mutex_unlock(&lock);abort();}
        }
        pthread_mutex_unlock(&lock);
        if (t->native_started && pthread_join(t->host,NULL)) abort();
        if (result) *result=t->result;
        pthread_mutex_lock(&lock);++joined_count;pthread_mutex_unlock(&lock);return 0;
    }
#endif
    if (t->detached || t->joined) return ERR(22);
    t->joined=1;
    void *value=NULL;
    int e=0;
#ifdef BB_CHECKPOINT_RUNTIME
    if (!t->restored || t->native_started)
#endif
        e=pthread_join(t->host,&value);
    if (e) { fprintf(stderr,"STOP: host pthread_join failed: %d\n",e); exit(21); }
    if (result) *result=t->result;
    pthread_mutex_lock(&lock); ++joined_count; pthread_mutex_unlock(&lock);
    return 0;
}
static ABI int32_t thread_detach(GuestThread *t) {
    if (!find_thread(t)) return ERR(3);
    if (t->detached) return ERR(22);
    t->detached=1;
    if (!t->host_owned
#ifdef BB_CHECKPOINT_RUNTIME
        && (!t->restored || t->native_started)
#endif
    ) pthread_detach(t->host);
    return 0;
}
static ABI __attribute__((noreturn)) void thread_exit(void *value) {
    GuestThread *t=runtime_thread_current();
    if (t->host_owned) { fputs("STOP: pthread_exit on host-owned/main thread\n",stderr); exit(21); }
    t->result=value;
#ifdef BB_CHECKPOINT_RUNTIME
    if (bb_checkpoint_runtime_exit(value)) __builtin_unreachable();
#endif
    /* No host unwinder: guest frames have no registered FDEs. */
    exit_longjmp(t);
}
/* pthread_exit for the network libraries' threads (gpu/bbnet/bbnet_glue.cpp). */
__attribute__((noreturn)) void runtime_thread_exit(void *value) { thread_exit(value); }
static ABI int32_t thread_yield(void) { sched_yield(); return 0; }
static ABI int32_t thread_get_prio(GuestThread *t,int *prio) {
    if (!find_thread(t)) return ERR(3);
    if (!prio) return ERR(22);
    *prio=t->attr.prio; return 0;
}
static ABI int32_t thread_set_prio(GuestThread *t,int prio) {
    if (!find_thread(t)) return ERR(3);
    t->attr.prio=prio; return 0;
}
static ABI int32_t thread_set_affinity(GuestThread *t,uint64_t mask) {
    if (!find_thread(t)) return ERR(3);
    t->attr.affinity=mask; return 0;
}
static ABI int32_t thread_get_affinity(GuestThread *t,uint64_t *mask) {
    if (!find_thread(t)) return ERR(3);
    if (!mask) return ERR(22);
    *mask=t->attr.affinity; return 0;
}
static ABI int32_t thread_rename(GuestThread *t,const char *name) {
    if (!find_thread(t)) return ERR(3);
    if (!name) return ERR(22);
    snprintf(t->name,sizeof(t->name),"%s",name);
    if (t==current) set_host_name(t->name);
    return 0;
}
static ABI int32_t thread_equal(GuestThread *a,GuestThread *b) { return a==b; }

/* POSIX (libScePosix) variants return positive errno values. */
static int32_t posix(int32_t r) { return r ? (int32_t)((uint32_t)r&0xffff) : 0; }
static ABI int32_t posix_attr_init(ThreadAttr **a) { return posix(attr_init(a)); }
static ABI int32_t posix_attr_destroy(ThreadAttr **a) { return posix(attr_destroy(a)); }
static ABI int32_t posix_attr_set_detach(ThreadAttr **a,int s) { return posix(attr_set_detach(a,s)); }
static ABI int32_t posix_attr_set_stack(ThreadAttr **a,uint64_t s) { return posix(attr_set_stack(a,s)); }
static ABI int32_t posix_attr_set_param(ThreadAttr **a,const int *p) { return posix(attr_set_param(a,p)); }
static ABI int32_t posix_create(GuestThread **t,ThreadAttr **a,GuestEntry e,void *arg) { return posix(create(t,a,e,arg,"posix")); }
static ABI int32_t posix_create_name(GuestThread **t,ThreadAttr **a,GuestEntry e,void *arg,const char *name) { return posix(create(t,a,e,arg,name)); }
static ABI int32_t posix_join(GuestThread *t,void **r) { return posix(thread_join(t,r)); }

/* Co-op: guest threads started by host libraries (NP matching/signaling threads call the
 * game's callbacks). Both return 0 or a positive errno value. */
int32_t runtime_thread_spawn(void **out,void *(ABI *entry)(void *),void *argument,uint64_t stack,const char *name) {
    ThreadAttr *attr=NULL;
    int32_t r=attr_init(&attr);
    if (!r && stack) r=attr_set_stack(&attr,stack);
    if (!r) r=create((GuestThread **)out,&attr,entry,argument,name);
    if (attr) attr_destroy(&attr);
    return posix(r);
}
int32_t runtime_thread_join_spawned(void *thread,void **result) {
    return posix(thread_join(thread,result));
}

uintptr_t runtime_thread_resolve(const char *name) {
    static const struct { const char *nid; void *fn; } table[]={
        {"aI+OeCz8xrQ#p#J",thread_self}, {"EotR8a3ASf4#I#J",thread_self},
        {"9BcDykPmo1I#p#J",guest_error},
        {"nsYoNRywwNg#p#J",attr_init}, {"62KCwEMmzcM#p#J",attr_destroy},
        {"x1X76arYMxU#p#J",attr_get}, {"8+s5BzZjxSg#p#J",attr_affinity},
        {"UTXzJbWhhTE#p#J",attr_set_stack}, {"-Wreprtu0Qs#p#J",attr_set_detach},
        {"4+h9EzwKF4I#p#J",attr_set_policy}, {"DzES9hQF4f4#p#J",attr_set_param},
        {"3qxgM4ezETA#p#J",attr_set_affinity}, {"eXbUSpEaTsA#p#J",attr_set_inherit},
        {"JaRMy+QcpeU#p#J",attr_get_detach}, {"-fA+7ZlGDQs#p#J",attr_get_stack},
        {"FXPWHNk8Of0#p#J",attr_get_param}, {"El+cQ20DynU#p#J",attr_set_guard},
        {"6UgtwV+0zb4#p#J",thread_create}, {"onNY9Byn-W8#p#J",thread_join},
        {"4qGrR6eoP9Y#p#J",thread_detach}, {"3kg7rT0NQIs#p#J",thread_exit},
        {"T72hz6ffq08#p#J",thread_yield}, {"1tKyG7RlMJo#p#J",thread_get_prio},
        {"W0Hpm2X0uPE#p#J",thread_set_prio}, {"bt3CTBKmGyI#p#J",thread_set_affinity}, {"rcrVFJsQWRY#p#J",thread_get_affinity},
        {"GBUY7ywdULE#p#J",thread_rename}, {"3PtV6p3QNX4#p#J",thread_equal},
        {"wtkt-teR1so#I#J",posix_attr_init}, {"zHchY8ft5pk#I#J",posix_attr_destroy},
        {"E+tyo3lp5Lw#I#J",posix_attr_set_detach}, {"2Q0z6rnBrTE#I#J",posix_attr_set_stack},
        {"euKRgm0Vn2M#I#J",posix_attr_set_param}, {"OxhIB8LB-PQ#I#J",posix_create},
        {"Jmi+9w9u0E4#I#J",posix_create_name}, {"h9CcP3J0oVM#I#J",posix_join},
        {"FJrT5LuUBAU#I#J",thread_exit},
    };
    for (size_t i=0;i<sizeof(table)/sizeof(*table);++i)
        if (!strcmp(name,table[i].nid)) return (uintptr_t)table[i].fn;
    return 0;
}
void runtime_thread_report(void) {
    pthread_mutex_lock(&lock);
    printf("Runtime: guest threads created=%zu, exited=%zu, joined=%zu\n",created,exited,joined_count);
    pthread_mutex_unlock(&lock);
}

#ifdef BB_CHECKPOINT_RUNTIME
/* The native bootstrap and exit recovery point stay on the native pthread
 * stack. The guest stack contains only guest frames and a rebound root stub. */
BbCheckpointThreadLocal *runtime_thread_checkpoint_local(void) {return runtime_thread_current()->local;}
uint64_t runtime_thread_run_guest(void *entry,void *arg0,void *arg1,size_t stack_size) {
    GuestThread *t=runtime_thread_current();
    if (stack_size<MIN_STACK) stack_size=MIN_STACK;
    if (!t->guest_stack) {
        t->guest_stack=runtime_low_map(stack_size,PROT_READ|PROT_WRITE);
        t->guest_stack_size=stack_size;
    }
    if (!t->guest_stack || stack_size>t->guest_stack_size) {
        fputs("STOP: cannot establish checkpoint guest stack\n",stderr); abort();
    }
    return bb_checkpoint_runtime_run((uintptr_t)t,entry,arg0,arg1,t->guest_stack,t->guest_stack_size,NULL);
}

int runtime_thread_checkpoint_export(BbCheckpointThreadRecord *records,size_t capacity,size_t *count) {
    if (!count || !bb_checkpoint_runtime_enabled()) return EINVAL;
    pthread_mutex_lock(&lock);
    size_t n=0;for (GuestThread *t=threads;t;t=t->next) ++n;
    *count=n;
    if (!records) {pthread_mutex_unlock(&lock);return 0;}
    if (capacity<n) {pthread_mutex_unlock(&lock);return ENOSPC;}
    n=0;
    for (GuestThread *t=threads;t;t=t->next) {
        if (!t->tls_block || !t->local || (!t->finished && !t->guest_stack)) {pthread_mutex_unlock(&lock);return EBUSY;}
        BbCheckpointThreadRecord r={0};
        r.id=(uintptr_t)t;r.stack=(uintptr_t)t->guest_stack;r.stack_size=t->guest_stack_size;
        r.tls=(uintptr_t)t->tls_block;r.tls_size=t->tls_size;r.tcb=(uintptr_t)t->tcb;
        r.entry=(uintptr_t)t->entry;r.argument=(uintptr_t)t->argument;r.result=(uintptr_t)t->result;
        r.attr_stack=t->attr.stack;r.attr_guard=t->attr.guard;r.attr_affinity=t->attr.affinity;
        r.flags=(t->detached?1u:0u)|(t->finished?2u:0u)|(t->joined?4u:0u)|(t->host_owned?8u:0u);
        r.policy=t->attr.policy;r.priority=t->attr.prio;r.inherit=t->attr.inherit;
        memcpy(r.name,t->name,sizeof(r.name));records[n++]=r;
        memcpy(records[n-1].module_tls,t->local->module_tls,sizeof(r.module_tls));
        memcpy(records[n-1].module_tls_size,t->local->module_tls_size,sizeof(r.module_tls_size));
    }
    pthread_mutex_unlock(&lock);return 0;
}
int runtime_thread_checkpoint_attr_export(BbCheckpointThreadAttrRecord *records,size_t capacity,size_t *count) {
    if (!count || !bb_checkpoint_runtime_enabled()) return EINVAL;
    pthread_mutex_lock(&lock);size_t n=0;for (ThreadAttr *a=attributes;a;a=a->next) ++n;*count=n;
    if (records && capacity<n) {pthread_mutex_unlock(&lock);return ENOSPC;}
    n=0;
    if (records) for (ThreadAttr *a=attributes;a;a=a->next)
        records[n++]=(BbCheckpointThreadAttrRecord){.id=(uintptr_t)a,.stack=a->stack,.guard=a->guard,.affinity=a->affinity,
            .detached=a->detached,.policy=a->policy,.priority=a->prio,.inherit=a->inherit};
    pthread_mutex_unlock(&lock);return 0;
}
static int checkpoint_low(uint64_t address,uint64_t size) {
    return size && !(address%16384) && address>=UINT64_C(0x7000000000) &&
        address<UINT64_C(0x8000000000) && size<=UINT64_C(0x8000000000)-address;
}
int runtime_thread_checkpoint_attr_restore(const BbCheckpointThreadAttrRecord *records,size_t count) {
    if ((!records && count) || count>4096 || !bb_checkpoint_runtime_enabled()) return EINVAL;
    for (size_t i=0;i<count;++i) {
        const BbCheckpointThreadAttrRecord *r=records+i;
        if (!checkpoint_low(r->id,16384) || r->stack<16384 || r->stack>128*1024*1024 ||
            r->detached<0 || r->detached>1 || r->policy<1 || r->policy>3 || r->inherit<0 || r->inherit>4 ||
            r->reserved[0] || r->reserved[1]) return EINVAL;
        for (size_t j=0;j<i;++j) if (records[j].id==r->id) return EINVAL;
    }
    pthread_mutex_lock(&lock);
    if (attributes || current || restore_ready) {pthread_mutex_unlock(&lock);return EBUSY;}
    for (GuestThread *t=threads;t;t=t->next) if (!t->restored || t->native_started) {pthread_mutex_unlock(&lock);return EBUSY;}
    ThreadAttr *built=NULL;
    for (size_t i=0;i<count;++i) {
        const BbCheckpointThreadAttrRecord *r=records+i;
        ThreadAttr *a=runtime_low_map_at(r->id,sizeof(*a),PROT_READ|PROT_WRITE);
        if (!a) {pthread_mutex_unlock(&lock);return ENOMEM;}
        *a=(ThreadAttr){.magic=ATTR_MAGIC,.stack=r->stack,.guard=r->guard,.affinity=r->affinity,
            .detached=r->detached,.policy=r->policy,.prio=r->priority,.inherit=r->inherit,.next=built};built=a;
    }
    attributes=built;pthread_mutex_unlock(&lock);return 0;
}
int runtime_thread_checkpoint_restore(const BbCheckpointThreadRecord *records,size_t count) {
    if (!records || !count || count>4096 || !bb_checkpoint_runtime_enabled()) return EINVAL;
    /* Validate all ranges and pairwise overlaps before creating any mapping. */
    for (size_t i=0;i<count;++i) {
        const BbCheckpointThreadRecord *r=records+i;
        if (!checkpoint_low(r->id,16384) || !checkpoint_low(r->tls,r->tls_size) ||
            r->tls_size!=((tls_offset()+512+sizeof(BbCheckpointThreadLocal)+16383)&~UINT64_C(16383)) ||
            r->tcb!=r->tls+tls_offset() || r->flags&~15u || r->reserved[0] || r->reserved[1] ||
            !memchr(r->name,0,sizeof(r->name)) || r->attr_stack>128*1024*1024 || r->attr_stack<16384 ||
            r->policy<1 || r->policy>3 || r->inherit<0 || r->inherit>4 ||
            (!(r->flags&2) && (!checkpoint_low(r->stack,r->stack_size) || r->stack_size<MIN_STACK || r->stack_size>128*1024*1024))) return EINVAL;
        const uint64_t starts[3]={r->id,r->tls,r->stack},sizes[3]={16384,r->tls_size,(r->stack_size+16383)&~UINT64_C(16383)};
        if (r->stack_size && (!checkpoint_low(r->stack,r->stack_size) || r->stack_size>128*1024*1024)) return EINVAL;
        for (size_t j=0;j<=i;++j) {
            const BbCheckpointThreadRecord *q=records+j;
            const uint64_t other[3]={q->id,q->tls,q->stack},lengths[3]={16384,q->tls_size,(q->stack_size+16383)&~UINT64_C(16383)};
            for (unsigned a=0;a<3;++a) for (unsigned b=0;b<3;++b)
                if ((j<i || a<b) && sizes[a] && lengths[b] && starts[a]<other[b]+lengths[b] && other[b]<starts[a]+sizes[a]) return EINVAL;
        }
        for (unsigned m=0;m<8;++m) {
            uint64_t address=r->module_tls[m],size=r->module_tls_size[m];
            if (!address && !size) continue;
            if (m<2 || !checkpoint_low(address,size) || size>1024*1024 || size!=runtime_checkpoint_module_tls_size(m)) return EINVAL;
            size=(size+16383)&~UINT64_C(16383);
            for (size_t j=0;j<count;++j) {
                const BbCheckpointThreadRecord *q=records+j;
                const uint64_t base[3]={q->id,q->tls,q->stack},length[3]={16384,q->tls_size,(q->stack_size+16383)&~UINT64_C(16383)};
                for (unsigned b=0;b<3;++b)
                    if (length[b] && base[b]<address+size && address<base[b]+length[b]) return EINVAL;
                for (unsigned k=2;k<8;++k) {
                    if (j==i && k==m) continue;
                    uint64_t other=q->module_tls[k],bytes=q->module_tls_size[k];
                    if (!other && !bytes) continue;
                    if (!checkpoint_low(other,bytes) || bytes>1024*1024) return EINVAL;
                    bytes=(bytes+16383)&~UINT64_C(16383);
                    if (other<address+size && address<other+bytes) return EINVAL;
                }
            }
        }
    }
    pthread_mutex_lock(&lock);
    if (threads || attributes || current) {pthread_mutex_unlock(&lock);return EBUSY;}
    GuestThread *built=NULL;
    for (size_t i=0;i<count;++i) {
        const BbCheckpointThreadRecord *r=records+i;
        GuestThread *t=runtime_low_map_at(r->id,sizeof(*t),PROT_READ|PROT_WRITE);
        void *tls=runtime_low_map_at(r->tls,r->tls_size,PROT_READ|PROT_WRITE);
        void *stack=r->stack_size?runtime_low_map_at(r->stack,r->stack_size,PROT_READ|PROT_WRITE):NULL;
        if (!t || !tls || (r->stack_size && !stack)) {
            /* The caller must discard this fresh restore process. Never
             * publish partially rebuilt threads or activate guest execution. */
            pthread_mutex_unlock(&lock);return ENOMEM;
        }
        t->tcb=(uint64_t *)(uintptr_t)r->tcb;t->tls_block=tls;t->tls_size=r->tls_size;
        t->local=(BbCheckpointThreadLocal *)((char *)t->tcb+512);
        t->guest_stack=stack;t->guest_stack_size=r->stack_size;
        t->entry=(GuestEntry)(uintptr_t)r->entry;t->argument=(void *)(uintptr_t)r->argument;t->result=(void *)(uintptr_t)r->result;
        t->attr=(ThreadAttr){.magic=ATTR_MAGIC,.policy=r->policy,.prio=r->priority,.inherit=r->inherit,
            .stack=r->attr_stack,.guard=r->attr_guard,.affinity=r->attr_affinity};
        t->detached=(r->flags&1)!=0;t->finished=(r->flags&2)!=0;t->joined=(r->flags&4)!=0;t->host_owned=(r->flags&8)!=0;
        t->restored=1;memcpy(t->name,r->name,sizeof(t->name));t->next=built;built=t;
        for (unsigned m=2;m<8;++m) if (r->module_tls[m]) {
            if (!runtime_low_map_at(r->module_tls[m],r->module_tls_size[m],PROT_READ|PROT_WRITE)) {pthread_mutex_unlock(&lock);return ENOMEM;}
            t->module_mappings[m]=r->module_tls[m];t->module_sizes[m]=r->module_tls_size[m];
        }
    }
    threads=built;restore_ready=0;pthread_mutex_unlock(&lock);return 0;
}
int runtime_thread_checkpoint_start(const BbCheckpointRuntimeThread *contexts,size_t count) {
    if (!contexts || !count || count>4096) return EINVAL;
    pthread_mutex_lock(&lock);
    if (restore_ready) {pthread_mutex_unlock(&lock);return EBUSY;}
    size_t live=0;
    for (GuestThread *t=threads;t;t=t->next) {
        if (!t->restored || t->native_started) {pthread_mutex_unlock(&lock);return EBUSY;}
        if (t->finished) continue;
        ++live;const BbCheckpointRuntimeThread *found=NULL;
        for (size_t i=0;i<count;++i) if (contexts[i].id==(uintptr_t)t) {if(found){pthread_mutex_unlock(&lock);return EINVAL;}found=contexts+i;}
        if (!found || !bb_checkpoint_runtime_valid_continuation(&found->cpu,found->stack,found->stack_size) ||
            found->stack!=(uintptr_t)t->guest_stack || found->stack_size!=t->guest_stack_size ||
            t->tcb[0]!=(uintptr_t)t->tcb || t->tcb[1]!=(uintptr_t)t->tcb+256 || t->tcb[2]!=(uintptr_t)t) {
            pthread_mutex_unlock(&lock);return EINVAL;
        }
        if (t->local->guard_owner!=(uint32_t)(((uintptr_t)t-UINT64_C(0x7000000000))/16384+1)) {pthread_mutex_unlock(&lock);return EINVAL;}
        t->resume_cpu=found->cpu;
        if (memcmp(t->local->module_tls,t->module_mappings,sizeof(t->module_mappings)) ||
            memcmp(t->local->module_tls_size,t->module_sizes,sizeof(t->module_sizes))) {pthread_mutex_unlock(&lock);return EINVAL;}
    }
    if (live!=count) {pthread_mutex_unlock(&lock);return EINVAL;}
    for (GuestThread *t=threads;t;t=t->next) if (!t->finished) {
        int e=pthread_create(&t->host,NULL,host_start,t);
        if (e) {pthread_mutex_unlock(&lock);return e;}
        t->native_started=1;
        if (t->detached) pthread_detach(t->host);
    }
    pthread_mutex_unlock(&lock);return 0;
}
void runtime_thread_checkpoint_activate(void) {
    pthread_mutex_lock(&lock);restore_ready=1;pthread_cond_broadcast(&restore_changed);pthread_mutex_unlock(&lock);
}
#endif

