/* Optional native-translator observation. Included by the loader after Segment
 * and LinkedModule are defined. The guest function and return slot stay intact. */
#ifdef BB_CPU_TRANSLATOR
#include <stdatomic.h>
#include <errno.h>

static const Segment *throw_audit_segments;
static uint64_t throw_audit_segment_count;
static _Atomic unsigned throw_audit_events;

static int throw_audit_image_read(uint64_t address,void *out,size_t bytes) {
    const uint64_t base=(uintptr_t)image;
    if(address<base)return 0;
    const uint64_t offset=address-base;
    for(uint64_t n=0;n<throw_audit_segment_count;++n) {
        const Segment *s=&throw_audit_segments[n];
        if((s->flags&4) && offset>=s->address && bytes<=s->size &&
           offset-s->address<=s->size-bytes)
            return bb_macos_read_memory((uintptr_t)address,out,bytes);
    }
    return 0;
}

static void throw_audit_probe(const uint64_t regs[16],uint64_t address,uint32_t tag) {
    (void)tag;
    const int saved_errno=errno;
    const int32_t saved_guest_errno=*runtime_errno();
    const unsigned sequence=atomic_fetch_add_explicit(&throw_audit_events,1,memory_order_relaxed);
    if(sequence>=64) {
        if(sequence==64)fputs("Runtime throw audit: event limit reached (64); further events omitted\n",stderr);
        *runtime_errno()=saved_guest_errno;errno=saved_errno;
        return;
    }
    /* Public probe register order: rax,rcx,rdx,rbx,rsp,rbp,rsi,rdi,r8..r15.
     * Inspect only the immediate checked return slot, never walk a stack. */
    uint64_t caller=0,caller_rva=UINT64_MAX;
    const int caller_readable=bb_macos_read_memory((uintptr_t)regs[4],&caller,sizeof(caller));
    if(caller_readable && caller>=(uintptr_t)image && bbcpu_is_guest(caller))
        caller_rva=caller-(uintptr_t)image;
    uint64_t typeinfo[2]={0};
    char escaped[96*4+1]={0};
    const char *type_state="unavailable";
    if(throw_audit_image_read(regs[6],typeinfo,sizeof(typeinfo))) {
        size_t written=0;
        unsigned n;
        for(n=0;n<96;++n) {
            unsigned char c;
            if(typeinfo[1]>UINT64_MAX-n || !throw_audit_image_read(typeinfo[1]+n,&c,1))break;
            if(!c) {type_state="available";break;}
            if(c>=32 && c<=126 && c!='"' && c!='\\')escaped[written++]=(char)c;
            else {
                static const char hex[]="0123456789abcdef";
                escaped[written++]='\\';escaped[written++]='x';
                escaped[written++]=hex[c>>4];escaped[written++]=hex[c&15];
            }
        }
        if(n==96)type_state="truncated";
    }
    if(!strcmp(type_state,"unavailable"))strcpy(escaped,"<unavailable>");
    char thread[64]={0};
    pthread_getname_np(pthread_self(),thread,sizeof(thread));
    if(!thread[0])strcpy(thread,"unnamed");
    fprintf(stderr,"Runtime throw audit: sequence=%u thread=%s pthread=%#" PRIxPTR
        " function_rva=%#" PRIx64 " object=%#" PRIx64 " typeinfo=%#" PRIx64
        " destructor=%#" PRIx64 " caller_readable=%d caller=%#" PRIx64
        " caller_rva=%#" PRIx64 " type_state=%s type_name=\"%s\"\n",
        sequence,thread,(uintptr_t)pthread_self(),address-(uintptr_t)image,regs[7],regs[6],regs[2],
        caller_readable,caller,caller_rva,type_state,escaped);
    *runtime_errno()=saved_guest_errno;errno=saved_errno;
}

static void install_throw_audit(const Segment *segments,uint64_t count,
                              const uint64_t *bindings,const uint64_t *kinds) {
    const char *enabled=getenv("BB_RUNTIME_EXIT_AUDIT");
    if(!enabled || strcmp(enabled,"1"))return;
    uint64_t target=0;
    for(uint64_t n=0;n<import_count;++n)if(!strcmp(names[n],"vkuuLfhnSZI#q#q") && bindings[n] && kinds[n]==1) {
        if(target && target!=bindings[n]) {
            fputs("Runtime throw audit: refused ambiguous bound __cxa_throw targets\n",stderr);return;
        }
        target=bindings[n];
    }
    if(!target) {fputs("Runtime throw audit: no bound __cxa_throw target\n",stderr);return;}
    int in_module=0,executable=0;
    enum { FUNCTION_BYTES=54 };
    for(uint64_t n=0;n<module_count;++n)
        if(target>=modules[n].base && FUNCTION_BYTES<=modules[n].size &&
           target-modules[n].base<=modules[n].size-FUNCTION_BYTES)in_module=1;
    for(uint64_t n=0;n<count;++n)
        if((segments[n].flags&1) && target>=segments[n].address && FUNCTION_BYTES<=segments[n].size &&
           target-segments[n].address<=segments[n].size-FUNCTION_BYTES)executable=1;
    /* The observed original entry saves RBP and writes exception-header count1.
     * This narrow signature is an observation guard, not a full binary identity. */
    static const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x48,0xc7,0x47,0x80,1,0,0,0};
    unsigned char actual[sizeof(prefix)];
    if(!in_module || !executable ||
       !bb_macos_read_memory((uintptr_t)image+target,actual,sizeof(actual)) || memcmp(actual,prefix,sizeof(prefix))) {
        fputs("Runtime throw audit: refused __cxa_throw extent/prologue mismatch\n",stderr);return;
    }
    throw_audit_segments=segments;throw_audit_segment_count=count;
    bbcpu_set_probe((uintptr_t)image+target,throw_audit_probe,0);
    fprintf(stderr,"Runtime throw audit: installed observation at image RVA %#" PRIx64
        " (64 events; guest instructions unchanged)\n",target);
}
#endif
