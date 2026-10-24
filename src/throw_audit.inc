/* Optional native-translator observation. Included by the loader after Segment
 * and LinkedModule are defined. The guest function and return slot stay intact. */
#ifdef BB_CPU_TRANSLATOR
#include <stdatomic.h>
#include <errno.h>

static const Segment *throw_audit_segments;
static uint64_t throw_audit_segment_count;
static _Atomic unsigned throw_audit_events;
static _Atomic unsigned allocation_audit_events;
static uint64_t allocation_audit_libc;

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

static void allocation_failure_probe(const uint64_t regs[16],uint64_t address,uint32_t tag) {
    const int saved_errno=errno;
    const int32_t saved_guest_errno=*runtime_errno();
    const unsigned sequence=atomic_fetch_add_explicit(&allocation_audit_events,1,memory_order_relaxed);
    if(sequence>=64) {
        if(sequence==64)fputs("Runtime allocation audit: event limit reached (64); further events omitted\n",stderr);
        *runtime_errno()=saved_guest_errno;errno=saved_errno;return;
    }
    uint64_t caller=0,caller_rva=UINT64_MAX;
    /* At the helper entry use the immediate return. At the validated default
     * new failure label, push rbp/push rbx/push rax put its original return at
     * RSP+24. This is one fixed slot, never an RBP chain or arbitrary stackwalk. */
    const uint64_t displacement=tag ? 24 : 0;
    const int readable=regs[4]<=UINT64_MAX-displacement &&
        bb_macos_read_memory((uintptr_t)(regs[4]+displacement),&caller,sizeof(caller));
    if(readable && caller>=(uintptr_t)image && bbcpu_is_guest(caller))caller_rva=caller-(uintptr_t)image;
    uint64_t callbacks[3]={0},callback_rvas[3]={UINT64_MAX,UINT64_MAX,UINT64_MAX};
    static const uint64_t slots[]={0xba190,0xba1a0,0xbe978};
    unsigned callback_readable=0;
    for(unsigned n=0;n<3;++n) {
        if(throw_audit_image_read((uintptr_t)image+allocation_audit_libc+slots[n],&callbacks[n],8)) {
            callback_readable|=1u<<n;
            if(callbacks[n]>=(uintptr_t)image && bbcpu_is_guest(callbacks[n]))
                callback_rvas[n]=callbacks[n]-(uintptr_t)image;
        }
    }
    char request[32]="unavailable",thread[64]={0};
    /* Only this proved default-new block retains the normalized request in
     * RBX. The helper's RDI=8 would describe the exception payload, not it. */
    if(tag)snprintf(request,sizeof(request),"%" PRIu64,regs[3]);
    pthread_getname_np(pthread_self(),thread,sizeof(thread));
    if(!thread[0])strcpy(thread,"unnamed");
    fprintf(stderr,"Runtime allocation audit: sequence=%u site=%s thread=%s pthread=%#" PRIxPTR
        " function_rva=%#" PRIx64 " normalized_request_bytes=%s caller_readable=%d"
        " caller_slot_offset=%" PRIu64 " caller=%#" PRIx64 " caller_rva=%#" PRIx64
        " rdi=%#" PRIx64 " rsi=%#" PRIx64 " rdx=%#" PRIx64 " rcx=%#" PRIx64
        " r8=%#" PRIx64 " r9=%#" PRIx64 " rbx=%#" PRIx64 " r12=%#" PRIx64
        " r13=%#" PRIx64 " r14=%#" PRIx64 " r15=%#" PRIx64
        " callbacks_readable=%#x malloc=%#" PRIx64 " malloc_rva=%#" PRIx64
        " calloc=%#" PRIx64 " calloc_rva=%#" PRIx64 " new=%#" PRIx64 " new_rva=%#" PRIx64 "\n",
        sequence,tag ? "default-new-failure" : "bad-alloc-helper",thread,(uintptr_t)pthread_self(),
        address-(uintptr_t)image,request,readable,displacement,caller,caller_rva,
        regs[7],regs[6],regs[2],regs[1],regs[8],regs[9],regs[3],regs[12],regs[13],regs[14],regs[15],
        callback_readable,callbacks[0],callback_rvas[0],callbacks[1],callback_rvas[1],callbacks[2],callback_rvas[2]);
    *runtime_errno()=saved_guest_errno;errno=saved_errno;
}

static int allocation_audit_code(const Segment *segments,uint64_t count,uint64_t address,size_t bytes) {
    if(address<modules[0].base || bytes>modules[0].size || address-modules[0].base>modules[0].size-bytes)return 0;
    for(uint64_t n=0;n<count;++n)
        if((segments[n].flags&1) && address>=segments[n].address && bytes<=segments[n].size &&
           address-segments[n].address<=segments[n].size-bytes)return 1;
    return 0;
}
static int allocation_audit_call(uint64_t address,uint64_t target) {
    unsigned char bytes[5];int32_t relative;
    if(!bb_macos_read_memory((uintptr_t)image+address,bytes,sizeof(bytes)) || bytes[0]!=0xe8)return 0;
    memcpy(&relative,bytes+1,sizeof(relative));
    return address+5+(int64_t)relative==target;
}
static void install_allocation_audit(const Segment *segments,uint64_t count,uint64_t throw_target) {
    /* These failure labels belong to the original libc layout, rather than an
     * arbitrary function sharing the public throw entry signature. */
    if(throw_target-modules[0].base!=0x60510)return;
    const uint64_t helper=modules[0].base+0x5ebd0,normal_new=modules[0].base+0x5fe80;
    static const unsigned char helper_prefix[]={0x55,0x48,0x89,0xe5,0xbf,8,0,0,0};
    static const unsigned char new_prefix[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x85,0xff,
        0xbb,1,0,0,0,0x48,0x0f,0x45,0xdf};
    unsigned char bytes[sizeof(new_prefix)];
    if(!allocation_audit_code(segments,count,helper,50) ||
       !bb_macos_read_memory((uintptr_t)image+helper,bytes,sizeof(helper_prefix)) ||
       memcmp(bytes,helper_prefix,sizeof(helper_prefix)) || !allocation_audit_call(helper+45,throw_target)) {
        fputs("Runtime allocation audit: refused bad_alloc helper extent/signature/call mismatch\n",stderr);return;
    }
    allocation_audit_libc=modules[0].base;
    bbcpu_set_probe((uintptr_t)image+helper,allocation_failure_probe,0);
    fprintf(stderr,"Runtime allocation audit: installed bad_alloc helper at image RVA %#" PRIx64 "\n",helper);
    if(!allocation_audit_code(segments,count,normal_new,69) ||
       !bb_macos_read_memory((uintptr_t)image+normal_new,bytes,sizeof(new_prefix)) ||
       memcmp(bytes,new_prefix,sizeof(new_prefix)) || !allocation_audit_call(normal_new+57,helper)) {
        fputs("Runtime allocation audit: refused default-new extent/prologue/call mismatch\n",stderr);return;
    }
    bbcpu_set_probe((uintptr_t)image+normal_new+57,allocation_failure_probe,1);
    fprintf(stderr,"Runtime allocation audit: installed default-new failure label at image RVA %#" PRIx64 "\n",normal_new+57);
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
    /* The loader's first linked module is libc. Do not observe a lookalike
     * scoped binding into a later system module, even with matching bytes. */
    if(module_count && target>=modules[0].base && FUNCTION_BYTES<=modules[0].size &&
       target-modules[0].base<=modules[0].size-FUNCTION_BYTES)in_module=1;
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
    install_allocation_audit(segments,count,target);
}
#endif
