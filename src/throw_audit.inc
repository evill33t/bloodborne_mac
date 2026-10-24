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
static const char allocation_audit_eboot_hash[]="cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86";

static int allocation_audit_code(const Segment *,uint64_t,uint64_t,size_t);
static int allocation_audit_call(uint64_t,uint64_t);

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

static int allocation_audit_image_range(uint64_t offset,size_t bytes,uint64_t flags) {
    for(uint64_t n=0;n<throw_audit_segment_count;++n) {
        const Segment *s=&throw_audit_segments[n];
        if((s->flags&flags)==flags && offset>=s->address && bytes<=s->size &&
           offset-s->address<=s->size-bytes)return 1;
    }
    return 0;
}

#include "retirement_audit.inc"

static int allocation_audit_cleanup_slot(uint64_t slot,uint64_t *value,uint64_t *rva) {
    const uint64_t base=(uintptr_t)image;
    if(!module_count || allocation_audit_libc!=modules[0].base || slot>modules[0].size ||
       8>modules[0].size-slot || base>UINT64_MAX-allocation_audit_libc ||
       base+allocation_audit_libc>UINT64_MAX-slot-7 ||
       !throw_audit_image_read(base+allocation_audit_libc+slot,value,8))return 0;
    if(*value>=base && bbcpu_is_guest(*value))*rva=*value-base;
    return 1;
}

/* Only the original default standard-allocator BSS object is described here.
 * Read fixed scalars, never bin heads, arena contents, or an alternate object.
 * Two equal observations are still unsynchronized, not an atomic snapshot. */
static void allocation_audit_heap_snapshot(unsigned sequence,const uint64_t callbacks[3],
                                          unsigned callback_readable) {
    static const unsigned char core_guard[]={0x53,0x31,0xc0,0x48,0x39,0x77,0x18,0x0f,0x82,0x47,0x01,0,0};
    static const unsigned char capacity_guard[]={0x48,0xc7,0x83,0x78,0x04,0,0,0,0,0x40,0x07};
    static const uint64_t fields[]={0x28,0x30,0x38,0x40,0x48,0x460,0x468,0x470,0x478};
    const uint64_t base=(uintptr_t)image,global_rva=0x56a20c0,object_rva=0x56a20c8;
    const char *reason="image-identity";
    const char *hash=getenv("BB_MAC_EBOOT_SHA256");
    uint64_t object=0,after=0,samples[2][9]={{0}},current_callbacks[2]={0};
    unsigned char actual[sizeof(core_guard)];
    /* The trusted launcher supplies the prepared original ELF identity. This
     * does not recompute the hash of the relocated in-memory image. */
    if(!hash || strcmp(hash,allocation_audit_eboot_hash))goto unavailable;
    reason="image-layout";
    if(base>UINT64_MAX-object_rva-0x480 || !module_count ||
       object_rva+0x480>modules[0].base ||
       !allocation_audit_image_range(global_rva,8,6) ||
       !allocation_audit_image_range(object_rva,0x480,6))goto unavailable;
    reason="callback-selection";
    if((callback_readable&3)!=3 || callbacks[0]!=base+0x20809f0 ||
       callbacks[1]!=base+0x2080cd0)goto unavailable;
    reason="instruction-layout";
    if(!allocation_audit_image_range(0x2085730,sizeof(core_guard),5) ||
       !allocation_audit_image_range(0x2081c3a,sizeof(capacity_guard),5) ||
       !throw_audit_image_read(base+0x2085730,actual,sizeof(core_guard)) ||
       memcmp(actual,core_guard,sizeof(core_guard)) ||
       !throw_audit_image_read(base+0x2081c3a,actual,sizeof(capacity_guard)) ||
       memcmp(actual,capacity_guard,sizeof(capacity_guard)))goto unavailable;
    reason="object-selection";
    if(!throw_audit_image_read(base+global_rva,&object,8) || object!=base+object_rva)goto unavailable;
    reason="scalar-read";
    for(unsigned sample=0;sample<2;++sample)
        for(unsigned n=0;n<9;++n)
            if(!throw_audit_image_read(object+fields[n],&samples[sample][n],8))goto unavailable;
    reason="selection-recheck";
    if(!throw_audit_image_read(base+global_rva,&after,8) ||
       !throw_audit_image_read(base+allocation_audit_libc+0xba190,&current_callbacks[0],8) ||
       !throw_audit_image_read(base+allocation_audit_libc+0xba1a0,&current_callbacks[1],8) ||
       after!=object || current_callbacks[0]!=callbacks[0] || current_callbacks[1]!=callbacks[1])goto unavailable;
    flockfile(stderr);
    fprintf(stderr,"Runtime allocation heap audit: sequence=%u state=sampled unsynchronized=1"
        " sample_comparison=%s object=%#" PRIx64 " heap=%#" PRIx64,
        sequence,memcmp(samples[0],samples[1],sizeof(samples[0])) ? "different-racy" : "equal",
        object,object+0x28);
    for(unsigned sample=0;sample<2;++sample)
        fprintf(stderr," sample%u_bitmap=%#" PRIx64 " sample%u_arena_start=%#" PRIx64
            " sample%u_arena_end=%#" PRIx64 " sample%u_free_bytes=%" PRIu64
            " sample%u_allocation_counter=%" PRIu64 " sample%u_initial_span=%" PRIu64
            " sample%u_mapped_base=%#" PRIx64 " sample%u_physical_offset=%#" PRIx64
            " sample%u_capacity_bytes=%" PRIu64,
            sample,samples[sample][0],sample,samples[sample][1],sample,samples[sample][2],
            sample,samples[sample][3],sample,samples[sample][4],sample,samples[sample][5],
            sample,samples[sample][6],sample,samples[sample][7],sample,samples[sample][8]);
    fputc('\n',stderr);
    funlockfile(stderr);
    return;
unavailable:
    fprintf(stderr,"Runtime allocation heap audit: sequence=%u state=unavailable reason=%s unsynchronized=1\n",sequence,reason);
}

typedef struct {
    const char *state;
    uint64_t function_rva,slot_offset,caller,caller_rva;
    int readable;
} AllocationAuditOrigin;

static AllocationAuditOrigin allocation_audit_helper_origin(const uint64_t regs[16],uint64_t immediate) {
    static const unsigned char small_prefix[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x85,0xff,
        0xbb,1,0,0,0,0x48,0x0f,0x45,0xdf};
    static const unsigned char saved_prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x50};
    static const unsigned char large_prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x54,0x53,0x48,0x81,0xec,0xc0,0,0,0};
    static const struct {
        uint64_t return_rva,start,bytes,slot_offset;
        const unsigned char *prefix;size_t prefix_bytes;
    } layouts[]={
        {0x5febe,0x5fe80,69,32,small_prefix,sizeof(small_prefix)},
        {0x5a7d9,0x5a6f0,267,64,saved_prefix,sizeof(saved_prefix)},
        {0x5a940,0x5a800,379,240,large_prefix,sizeof(large_prefix)},
        {0x62cac,0x62c20,377,64,saved_prefix,sizeof(saved_prefix)},
        {0x62d40,0x62c20,377,64,saved_prefix,sizeof(saved_prefix)},
        {0x70cc6,0x70c20,421,64,saved_prefix,sizeof(saved_prefix)},
        {0x70d6a,0x70c20,421,64,saved_prefix,sizeof(saved_prefix)},
    };
    AllocationAuditOrigin result={.state="unclassified",.caller_rva=UINT64_MAX};
    const uint64_t base=(uintptr_t)image;
    if(base>UINT64_MAX-allocation_audit_libc || immediate<base+allocation_audit_libc)return result;
    const uint64_t relative=immediate-base-allocation_audit_libc;
    for(unsigned n=0;n<sizeof(layouts)/sizeof(layouts[0]);++n)if(relative==layouts[n].return_rva) {
        unsigned char actual[sizeof(small_prefix)];
        result.state="layout-unavailable";
        result.function_rva=allocation_audit_libc+layouts[n].start;
        result.slot_offset=layouts[n].slot_offset;
        if(!allocation_audit_code(throw_audit_segments,throw_audit_segment_count,result.function_rva,layouts[n].bytes) ||
           !throw_audit_image_read(base+result.function_rva,actual,layouts[n].prefix_bytes) ||
           memcmp(actual,layouts[n].prefix,layouts[n].prefix_bytes) ||
           !allocation_audit_call(allocation_audit_libc+relative-5,allocation_audit_libc+0x5ebd0))return result;
        /* These complete original procedures preserve this entry frame until
         * the exact helper CALL. Read its one fixed saved return, not [RBP]. */
        result.state="frame-unavailable";
        if(regs[4]>UINT64_MAX-result.slot_offset || regs[5]!=regs[4]+result.slot_offset-8)return result;
        result.readable=bb_macos_read_memory((uintptr_t)(regs[4]+result.slot_offset),&result.caller,8);
        result.state=result.readable ? "checked-fixed-rsp-slot" : "slot-unreadable";
        if(result.readable && result.caller>=base && bbcpu_is_guest(result.caller))result.caller_rva=result.caller-base;
        return result;
    }
    return result;
}

static AllocationAuditOrigin allocation_audit_vector_owner(const uint64_t regs[16],uint64_t immediate,
                                                          unsigned tag,const uint64_t callbacks[3],unsigned mask) {
    AllocationAuditOrigin result={.state="unclassified",.caller_rva=UINT64_MAX};
    const uint64_t base=(uintptr_t)image;
    const char *hash=getenv("BB_MAC_EBOOT_SHA256");
    if(tag!=1 || base>UINT64_MAX-0x2a3bfc9 || immediate!=base+0x2a3bbaf)return result;
    result.state="layout-unavailable";result.function_rva=0x2a3bab0;result.slot_offset=168;
    static const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58};
    unsigned char actual[sizeof(prefix)];
    if(!hash || strcmp(hash,allocation_audit_eboot_hash) || !(mask&4) ||
       callbacks[2]!=base+allocation_audit_libc+0x5fe80 ||
       !allocation_audit_image_range(0x2a3bab0,1305,5) ||
       !throw_audit_image_read(base+0x2a3bab0,actual,sizeof(actual)) || memcmp(actual,prefix,sizeof(prefix)) ||
       !allocation_audit_call(0x2a3bbaa,0x2bbe9a8))return result;
    result.state="frame-unavailable";
    /* Game frame: push RBP + five saved registers + 88 local bytes =136.
     * Its CALL and the validated default-new 24-byte frame add32, so its
     * saved return is exactly168 bytes above this failure-label RSP. */
    if(regs[4]>UINT64_MAX-168 || regs[5]!=regs[4]+16)return result;
    result.readable=bb_macos_read_memory((uintptr_t)(regs[4]+168),&result.caller,8);
    result.state=result.readable ? "checked-fixed-rsp-slot" : "slot-unreadable";
    if(result.readable && result.caller>=base && bbcpu_is_guest(result.caller))result.caller_rva=result.caller-base;
    return result;
}

static int allocation_audit_stack_words(unsigned sequence,uint64_t rsp) {
    uint64_t words[64];
    const int readable=rsp<=UINT64_MAX-(sizeof(words)-1) &&
        bb_macos_read_memory((uintptr_t)rsp,words,sizeof(words));
    flockfile(stderr);
    fprintf(stderr,"Runtime allocation stack audit: sequence=%u uninterpreted=1 rsp=%#" PRIx64
        " bytes=%zu readable=%d",sequence,rsp,sizeof(words),readable);
    if(readable)for(unsigned n=0;n<64;++n)fprintf(stderr," word%u=%#" PRIx64,n,words[n]);
    fputc('\n',stderr);funlockfile(stderr);return readable;
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
    AllocationAuditOrigin origin={.state="not-helper-site",.caller_rva=UINT64_MAX};
    if(!tag && readable)origin=allocation_audit_helper_origin(regs,caller);
    uint64_t callbacks[3]={0},callback_rvas[3]={UINT64_MAX,UINT64_MAX,UINT64_MAX};
    static const uint64_t slots[]={0xba190,0xba1a0,0xbe978};
    unsigned callback_readable=0;
    for(unsigned n=0;n<3;++n) {
        if(slots[n]<=modules[0].size && 8<=modules[0].size-slots[n] &&
           throw_audit_image_read((uintptr_t)image+allocation_audit_libc+slots[n],&callbacks[n],8)) {
            callback_readable|=1u<<n;
            if(callbacks[n]>=(uintptr_t)image && bbcpu_is_guest(callbacks[n]))
                callback_rvas[n]=callbacks[n]-(uintptr_t)image;
        }
    }
    /* Preserve the existing malloc/calloc/new array and readable-bit contract.
     * The two cleanup selectors have their own separately labeled mask. */
    uint64_t cleanup[2]={0},cleanup_rvas[2]={UINT64_MAX,UINT64_MAX};
    const unsigned cleanup_readable=
        (unsigned)allocation_audit_cleanup_slot(0xba198,&cleanup[0],&cleanup_rvas[0]) |
        ((unsigned)allocation_audit_cleanup_slot(0xbe998,&cleanup[1],&cleanup_rvas[1])<<1);
    AllocationAuditOrigin owner={.state="not-default-new-site",.caller_rva=UINT64_MAX};
    if(tag && readable)owner=allocation_audit_vector_owner(regs,caller,tag,callbacks,callback_readable);
    char request[32]="unavailable",thread[64]={0};
    /* Only this proved default-new block retains the normalized request in
     * RBX. The helper's RDI=8 would describe the exception payload, not it. */
    if(tag)snprintf(request,sizeof(request),"%" PRIu64,regs[3]);
    pthread_getname_np(pthread_self(),thread,sizeof(thread));
    if(!thread[0])strcpy(thread,"unnamed");
    fprintf(stderr,"Runtime allocation audit: sequence=%u site=%s thread=%s pthread=%#" PRIxPTR
        " function_rva=%#" PRIx64 " normalized_request_bytes=%s caller_readable=%d"
        " caller_slot_offset=%" PRIu64 " caller=%#" PRIx64 " caller_rva=%#" PRIx64
        " rax=%#" PRIx64 " rsp=%#" PRIx64 " rbp=%#" PRIx64
        " rdi=%#" PRIx64 " rsi=%#" PRIx64 " rdx=%#" PRIx64 " rcx=%#" PRIx64
        " r8=%#" PRIx64 " r9=%#" PRIx64 " rbx=%#" PRIx64 " r12=%#" PRIx64
        " r13=%#" PRIx64 " r14=%#" PRIx64 " r15=%#" PRIx64
        " callbacks_readable=%#x malloc=%#" PRIx64 " malloc_rva=%#" PRIx64
        " calloc=%#" PRIx64 " calloc_rva=%#" PRIx64 " new=%#" PRIx64 " new_rva=%#" PRIx64
        " cleanup_callbacks_readable=%#x free=%#" PRIx64 " free_rva=%#" PRIx64
        " delete=%#" PRIx64 " delete_rva=%#" PRIx64
        " origin_state=%s origin_function_rva=%#" PRIx64 " origin_slot_base=rsp"
        " origin_slot_offset=%" PRIu64 " origin_readable=%d origin=%#" PRIx64 " origin_rva=%#" PRIx64
        " owner_state=%s owner_function_rva=%#" PRIx64 " owner_slot_base=rsp"
        " owner_slot_offset=%" PRIu64 " owner_readable=%d owner=%#" PRIx64 " owner_rva=%#" PRIx64 "\n",
        sequence,tag ? "default-new-failure" : "bad-alloc-helper",thread,(uintptr_t)pthread_self(),
        address-(uintptr_t)image,request,readable,displacement,caller,caller_rva,
        regs[0],regs[4],regs[5],regs[7],regs[6],regs[2],regs[1],regs[8],regs[9],regs[3],regs[12],regs[13],regs[14],regs[15],
        callback_readable,callbacks[0],callback_rvas[0],callbacks[1],callback_rvas[1],callbacks[2],callback_rvas[2],
        cleanup_readable,cleanup[0],cleanup_rvas[0],cleanup[1],cleanup_rvas[1],
        origin.state,origin.function_rva,origin.slot_offset,origin.readable,origin.caller,origin.caller_rva,
        owner.state,owner.function_rva,owner.slot_offset,owner.readable,owner.caller,owner.caller_rva);
    allocation_audit_heap_snapshot(sequence,callbacks,callback_readable);
    retirement_audit_snapshot(sequence);
    if(!tag)allocation_audit_stack_words(sequence,regs[4]);
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
    install_retirement_audit();
}
#endif
