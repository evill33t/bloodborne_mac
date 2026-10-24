/* Read-only completion selection for the exact supported original initializer.
 * Included by throw_audit.inc; original instructions/guest data stay intact. */
static _Atomic uint64_t retirement_audit_context;
static _Atomic unsigned retirement_audit_installed;
static _Atomic unsigned retirement_audit_init_seen;
static _Atomic unsigned retirement_audit_init_selected;

static int retirement_audit_enabled(void) {
    const char *enabled=getenv("BB_RUNTIME_EXIT_AUDIT");
    return enabled && !strcmp(enabled,"1");
}
static const char *retirement_audit_layout(void) {
    const char *hash=getenv("BB_MAC_EBOOT_SHA256");
    const uint64_t base=(uintptr_t)image;
    static const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x48,0x89,0xfb};
    static const unsigned char publish[]={0x48,0x8b,0x83,0x30,0x0c,0,0,
        0x48,0x8d,0x0d,0x68,0x2b,0x2e,0x04,0x48,0x89,0x01,0x4c,0x89,0xf7};
    static const unsigned char call_return[]={0xe8,0x3d,0x3b,0x02,0,
        0x48,0x8b,0x03,0x48,0x89,0xdf};
    unsigned char actual[sizeof(prefix)];
    if(!hash || strcmp(hash,allocation_audit_eboot_hash))return "image-identity";
    if(base>UINT64_MAX-0x54b9eb8 || !module_count || modules[0].base<0x54b9eb8 ||
       !allocation_audit_image_range(0x11d7200,437,5) ||
       !allocation_audit_image_range(0x54b9eb0,8,6))return "image-layout";
    if(!throw_audit_image_read(base+0x11d7200,actual,sizeof(prefix)) || memcmp(actual,prefix,sizeof(prefix)) ||
       !throw_audit_image_read(base+0x11d733a,actual,sizeof(publish)) || memcmp(actual,publish,sizeof(publish)) ||
       !throw_audit_image_read(base+0x11d734e,actual,sizeof(call_return)) || memcmp(actual,call_return,sizeof(call_return)))
        return "instruction-layout";
    return NULL;
}
/* Only fixed original fields of a recognized initializer instance are read.
 * The completion label is a separately allocated scalar, not an owner pointer. */
static int retirement_audit_selection(uint64_t context,uint64_t *label) {
    uint32_t selector;
    uint64_t global_label=0,context_label=0,global_after=0;
    if(!context || (context&7) || context>UINT64_MAX-0x3520 ||
       !bb_macos_read_memory((uintptr_t)(context+0x351c),&selector,4) || selector ||
       !bb_macos_read_memory((uintptr_t)(context+0xc30),&context_label,8) ||
       !throw_audit_image_read((uintptr_t)image+0x54b9eb0,&global_label,8) ||
       !global_label || (global_label&7) || global_label>UINT64_MAX-7 || context_label!=global_label ||
       !throw_audit_image_read((uintptr_t)image+0x54b9eb0,&global_after,8) || global_after!=global_label)return 0;
    *label=global_label;return 1;
}
static void retirement_audit_init_probe(const uint64_t regs[16],uint64_t address,uint32_t tag) {
    (void)tag;
    const int saved_errno=errno;
    const int32_t saved_guest_errno=*runtime_errno();
    uint64_t label=0;
    if(!retirement_audit_enabled() || !atomic_load_explicit(&retirement_audit_installed,memory_order_acquire) ||
       address!=(uintptr_t)image+0x11d7353 || retirement_audit_layout())goto done;
    atomic_fetch_add_explicit(&retirement_audit_init_seen,1,memory_order_relaxed);
    /* Public register order puts the initializer's saved original RDI in RBX. */
    if(!retirement_audit_selection(regs[3],&label))goto done;
    atomic_store_explicit(&retirement_audit_context,regs[3],memory_order_release);
    const unsigned selected=atomic_fetch_add_explicit(&retirement_audit_init_selected,1,memory_order_relaxed);
    if(selected<64)fprintf(stderr,"Runtime retirement audit: selected context=%#" PRIx64
        " completion_label=%#" PRIx64 " init_rva=0x11d7353 unsynchronized=1\n",regs[3],label);
    else if(selected==64)fputs("Runtime retirement audit: initialization log limit reached (64)\n",stderr);
 done:
    *runtime_errno()=saved_guest_errno;errno=saved_errno;
}
static void install_retirement_audit(void) {
    if(!retirement_audit_enabled())return;
    const char *reason=retirement_audit_layout();
    if(reason) {
        fprintf(stderr,"Runtime retirement audit: refused initialization observation reason=%s\n",reason);return;
    }
    /* This verified CALL return starts a decoded block on both selector paths.
     * The earlier publish fallthrough is inside the selected path's JIT block. */
    bbcpu_set_probe((uintptr_t)image+0x11d7353,retirement_audit_init_probe,0);
    atomic_store_explicit(&retirement_audit_installed,1,memory_order_release);
    fputs("Runtime retirement audit: installed read-only initialization observation at image RVA 0x11d7353\n",stderr);
}
static void retirement_audit_snapshot(unsigned sequence) {
    if(!retirement_audit_enabled())return;
    const char *reason=retirement_audit_layout();
    uint64_t context=0,label[2]={0},submitted[2]={0},completed[2]={0},after=0;
    if(reason)goto unavailable;
    reason="initialization-not-observed";
    if(!atomic_load_explicit(&retirement_audit_installed,memory_order_acquire) ||
       !(context=atomic_load_explicit(&retirement_audit_context,memory_order_acquire)))goto unavailable;
    for(unsigned sample=0;sample<2;++sample) {
        reason="context-selection";
        if(!retirement_audit_selection(context,&label[sample]))goto unavailable;
        reason="counter-read";
        if(!bb_macos_read_memory((uintptr_t)(context+0x3400),&submitted[sample],8) ||
           !bb_macos_read_memory((uintptr_t)label[sample],&completed[sample],8))goto unavailable;
    }
    reason="selection-recheck";
    if(label[0]!=label[1] || context!=atomic_load_explicit(&retirement_audit_context,memory_order_acquire) ||
       !retirement_audit_selection(context,&after) || after!=label[0])goto unavailable;
    fprintf(stderr,"Runtime retirement audit: sequence=%u state=sampled unsynchronized=1"
        " sample_comparison=%s context=%#" PRIx64 " completion_label=%#" PRIx64
        " sample0_submitted=%" PRIu64 " sample0_completed=%" PRIu64
        " sample1_submitted=%" PRIu64 " sample1_completed=%" PRIu64
        " init_seen=%u init_selected=%u\n",
        sequence,submitted[0]!=submitted[1] || completed[0]!=completed[1] ? "different-racy" : "equal",
        context,label[0],submitted[0],completed[0],submitted[1],completed[1],
        atomic_load_explicit(&retirement_audit_init_seen,memory_order_relaxed),
        atomic_load_explicit(&retirement_audit_init_selected,memory_order_relaxed));
    return;
 unavailable:
    fprintf(stderr,"Runtime retirement audit: sequence=%u state=unavailable reason=%s unsynchronized=1"
        " init_seen=%u init_selected=%u\n",sequence,reason,
        atomic_load_explicit(&retirement_audit_init_seen,memory_order_relaxed),
        atomic_load_explicit(&retirement_audit_init_selected,memory_order_relaxed));
}
