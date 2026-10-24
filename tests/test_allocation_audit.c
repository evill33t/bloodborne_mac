/* Owned sparse-memory diagnostic fixture. No original procedure body is
 * reproduced or executed; small production observation guards select layouts. */
#define _DARWIN_C_SOURCE
#define BB_CPU_TRANSLATOR 1
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { uint64_t address,size,flags; } Segment;
typedef struct { uint64_t base,size; } LinkedModule;
static unsigned char *image;
static LinkedModule modules[1];
static uint64_t module_count=1,import_count;
static char names[1][128];
static int32_t guest_errno;
static int32_t *runtime_errno(void) { return &guest_errno; }
static int bbcpu_is_guest(uint64_t address) {
    return address>=(uintptr_t)image && address-(uintptr_t)image<0x6000000;
}
static void bbcpu_set_probe(uint64_t address,void (*callback)(const uint64_t[16],uint64_t,uint32_t),uint32_t tag) {
    (void)address;(void)callback;(void)tag;
}

typedef struct { uint64_t address;size_t bytes;unsigned char value[512];unsigned reads; } Cell;
static Cell cells[40];
static unsigned cell_count,read_count;
static uint64_t fail_address,change_address;
static unsigned change_at;
static uint64_t change_value;
static Cell *cell(uint64_t address,size_t bytes) {
    assert(cell_count<sizeof(cells)/sizeof(cells[0]) && bytes<=sizeof(cells[0].value));
    Cell *c=&cells[cell_count++];memset(c,0,sizeof(*c));c->address=address;c->bytes=bytes;return c;
}
static Cell *word(uint64_t address,uint64_t value) {
    Cell *c=cell(address,8);memcpy(c->value,&value,8);return c;
}
static int bb_macos_read_memory(uintptr_t address,void *out,size_t bytes) {
    ++read_count;
    errno=EACCES; /* The observer must restore errors changed by checked reads. */
    if(address==fail_address)return 0;
    for(unsigned n=0;n<cell_count;++n) {
        Cell *c=&cells[n];
        if(address>=c->address && bytes<=c->bytes && address-c->address<=c->bytes-bytes) {
            ++c->reads;
            if(address==change_address && c->reads==change_at)memcpy(c->value,&change_value,8);
            memcpy(out,c->value+address-c->address,bytes);return 1;
        }
    }
    return 0;
}
#include "../src/throw_audit.inc"

static Segment segments[4];
static uint64_t callbacks[3];
static const uint64_t base=UINT64_C(0x7000000000),object_rva=0x56a20c8,libc_rva=0x56e0000;
static const uint64_t offsets[]={0x28,0x30,0x38,0x40,0x48,0x460,0x468,0x470,0x478};
static void reset(void) {
    image=(unsigned char *)(uintptr_t)base;module_count=1;import_count=0;
    modules[0]=(LinkedModule){libc_rva,0xe0000};allocation_audit_libc=libc_rva;
    segments[0]=(Segment){0,0x4000000,5};
    segments[1]=(Segment){0x5000000,0x6e0000,6};
    segments[2]=(Segment){libc_rva,0x80000,5};
    segments[3]=(Segment){libc_rva+0xb0000,0x40000,6};
    throw_audit_segments=segments;throw_audit_segment_count=4;
    cell_count=read_count=0;fail_address=change_address=UINT64_MAX;change_at=0;
    setenv("BB_MAC_EBOOT_SHA256","cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86",1);
    const unsigned char core[]={0x53,0x31,0xc0,0x48,0x39,0x77,0x18,0x0f,0x82,0x47,0x01,0,0};
    const unsigned char capacity[]={0x48,0xc7,0x83,0x78,0x04,0,0,0,0,0x40,0x07};
    memcpy(cell(base+0x2085730,sizeof(core))->value,core,sizeof(core));
    memcpy(cell(base+0x2081c3a,sizeof(capacity))->value,capacity,sizeof(capacity));
    word(base+0x56a20c0,base+object_rva);
    for(unsigned n=0;n<9;++n)word(base+object_rva+offsets[n],100+n);
    callbacks[0]=base+0x20809f0;callbacks[1]=base+0x2080cd0;callbacks[2]=base+libc_rva+0x5fe80;
    word(base+libc_rva+0xba190,callbacks[0]);word(base+libc_rva+0xba1a0,callbacks[1]);
    word(base+libc_rva+0xbe978,callbacks[2]);
}
static char *snapshot(void) {
    FILE *capture=tmpfile();assert(capture);int original=dup(STDERR_FILENO);assert(original>=0);
    fflush(stderr);assert(dup2(fileno(capture),STDERR_FILENO)>=0);
    allocation_audit_heap_snapshot(17,callbacks,7);
    fflush(stderr);assert(dup2(original,STDERR_FILENO)>=0);close(original);
    assert(!fseek(capture,0,SEEK_END));long bytes=ftell(capture);assert(bytes>=0);
    assert(!fseek(capture,0,SEEK_SET));char *text=calloc((size_t)bytes+1,1);assert(text);
    assert(fread(text,1,(size_t)bytes,capture)==(size_t)bytes);fclose(capture);return text;
}
static void expect_snapshot(const char *expected) {
    char *text=snapshot();assert(strstr(text,expected));free(text);
}
static void heap_tests(void) {
    reset();char *text=snapshot();
    assert(strstr(text,"state=sampled unsynchronized=1 sample_comparison=equal"));
    assert(strstr(text,"sample0_free_bytes=103") && strstr(text,"sample1_free_bytes=103"));
    assert(strstr(text,"sample1_allocation_counter=104") && strstr(text,"sample1_capacity_bytes=108"));
    free(text);
    reset();unsetenv("BB_MAC_EBOOT_SHA256");expect_snapshot("reason=image-identity");assert(!read_count);
    reset();setenv("BB_MAC_EBOOT_SHA256","other-image",1);expect_snapshot("reason=image-identity");assert(!read_count);
    reset();callbacks[0]++;expect_snapshot("reason=callback-selection");assert(!read_count);
    reset();segments[1].flags=4;expect_snapshot("reason=image-layout");assert(!read_count);
    reset();image=(unsigned char *)(uintptr_t)(UINT64_MAX-0x100);expect_snapshot("reason=image-layout");assert(!read_count);
    reset();cells[0].value[0]^=1;expect_snapshot("reason=instruction-layout");
    reset();cells[2].value[0]^=1;expect_snapshot("reason=object-selection");assert(!cells[3].reads);
    reset();fail_address=base+object_rva+0x40;expect_snapshot("reason=scalar-read");
    reset();change_address=base+object_rva+0x40;change_at=2;change_value=700;
    text=snapshot();assert(strstr(text,"sample_comparison=different-racy"));
    assert(strstr(text,"sample0_free_bytes=103") && strstr(text,"sample1_free_bytes=700"));free(text);
    reset();change_address=base+0x56a20c0;change_at=2;change_value=base+object_rva+8;
    expect_snapshot("reason=selection-recheck");
    reset();change_address=base+libc_rva+0xba190;change_at=1;change_value=base+123;
    expect_snapshot("reason=selection-recheck");
}
static void add_call(uint64_t rva,uint64_t target) {
    Cell *c=cell(base+rva,5);c->value[0]=0xe8;int64_t relative=(int64_t)target-(int64_t)(rva+5);
    assert(relative>=INT32_MIN && relative<=INT32_MAX);int32_t rel=(int32_t)relative;memcpy(c->value+1,&rel,4);
}
static void frame_tests(void) {
    static const uint64_t returns[]={0x5febe,0x5a7d9,0x5a940,0x62cac,0x62d40,0x70cc6,0x70d6a};
    static const uint64_t starts[]={0x5fe80,0x5a6f0,0x5a800,0x62c20,0x62c20,0x70c20,0x70c20};
    static const uint64_t slots[]={32,64,240,64,64,64,64};
    const unsigned char small[]={0x55,0x48,0x89,0xe5,0x53,0x50,0x48,0x85,0xff,0xbb,1,0,0,0,0x48,0x0f,0x45,0xdf};
    const unsigned char saved[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x50};
    const unsigned char large[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x48,0x81,0xec,0xc0,0,0,0};
    for(unsigned n=0;n<7;++n) {
        reset();const unsigned char *prefix=n==0 ? small : n==2 ? large : saved;
        const size_t bytes=n==0 ? sizeof(small) : n==2 ? sizeof(large) : sizeof(saved);
        Cell *p=cell(base+libc_rva+starts[n],bytes);memcpy(p->value,prefix,bytes);
        add_call(libc_rva+returns[n]-5,libc_rva+0x5ebd0);
        const uint64_t stack=UINT64_C(0x9000000000),origin=base+0x1234;
        word(stack+slots[n],origin);uint64_t regs[16]={0};regs[4]=stack;regs[5]=stack+slots[n]-8;
        AllocationAuditOrigin result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"checked-fixed-rsp-slot") && result.readable);
        assert(result.caller==origin && result.caller_rva==0x1234 && result.slot_offset==slots[n]);
        const uint64_t old_size=segments[2].size;segments[2].size=starts[n]+bytes;
        result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"layout-unavailable"));segments[2].size=old_size;
        ++regs[5];result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"frame-unavailable") && !result.readable);--regs[5];
        fail_address=stack+slots[n];result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"slot-unreadable") && !result.readable);fail_address=UINT64_MAX;
        regs[4]=UINT64_MAX-4;result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"frame-unavailable"));regs[4]=stack;
        p->value[0]^=1;result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"layout-unavailable"));p->value[0]^=1;
        cells[cell_count-2].value[0]^=1;result=allocation_audit_helper_origin(regs,base+libc_rva+returns[n]);
        assert(!strcmp(result.state,"layout-unavailable"));
    }
    reset();uint64_t regs[16]={0};AllocationAuditOrigin result=allocation_audit_helper_origin(regs,base+libc_rva+123);
    assert(!strcmp(result.state,"unclassified") && !read_count);
    /* The full observer, including new unavailable/sampling branches, restores
     * host and guest errno. Successful allocation execution remains covered by
     * the owned translator loader fixture. */
    setenv("BB_MAC_EBOOT_SHA256","other-image",1);errno=EDOM;guest_errno=91;
    allocation_failure_probe(regs,base+libc_rva+0x5ebd0,0);
    assert(errno==EDOM && guest_errno==91);
    setenv("BB_RUNTIME_EXIT_AUDIT","0",1);install_throw_audit(NULL,0,NULL,NULL);
}
static void owner_and_stack_tests(void) {
    reset();
    const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x58};
    Cell *p=cell(base+0x2a3bab0,sizeof(prefix));memcpy(p->value,prefix,sizeof(prefix));
    add_call(0x2a3bbaa,0x2bbe9a8);
    const uint64_t stack=UINT64_C(0x9000000000);uint64_t regs[16]={0};regs[4]=stack;regs[5]=stack+16;
    Cell *saved=word(stack+168,base+0xabc);
    AllocationAuditOrigin result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"checked-fixed-rsp-slot") && result.caller_rva==0xabc && result.slot_offset==168);
    unsigned reads=read_count;
    result=allocation_audit_vector_owner(regs,base+0x2a3bbb0,1,callbacks,7);
    assert(!strcmp(result.state,"unclassified") && read_count==reads);
    result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,0,callbacks,7);
    assert(!strcmp(result.state,"unclassified") && read_count==reads);
    setenv("BB_MAC_EBOOT_SHA256","other",1);
    result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"layout-unavailable") && saved->reads==1);
    setenv("BB_MAC_EBOOT_SHA256",allocation_audit_eboot_hash,1);
    p->value[0]^=1;result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"layout-unavailable"));p->value[0]^=1;
    ++regs[5];result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"frame-unavailable"));--regs[5];
    fail_address=stack+168;result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"slot-unreadable"));fail_address=UINT64_MAX;
    ++callbacks[2];result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"layout-unavailable"));--callbacks[2];
    cells[cell_count-2].value[0]^=1;result=allocation_audit_vector_owner(regs,base+0x2a3bbaf,1,callbacks,7);
    assert(!strcmp(result.state,"layout-unavailable"));
    reset();Cell *block=cell(stack,512);
    for(unsigned n=0;n<64;++n){uint64_t value=n+1000;memcpy(block->value+8*n,&value,8);}
    assert(allocation_audit_stack_words(21,stack));assert(block->reads==1);
    assert(!allocation_audit_stack_words(22,stack+1));assert(block->reads==1);
    assert(!allocation_audit_stack_words(23,0));
    reads=read_count;assert(!allocation_audit_stack_words(24,UINT64_MAX-500));assert(read_count==reads);
    reset();Cell *whole=cell(stack,512);uint64_t immediate=base+0x111;memcpy(whole->value,&immediate,8);
    memset(regs,0,sizeof(regs));regs[4]=stack;errno=EDOM;guest_errno=91;
    allocation_failure_probe(regs,base+libc_rva+0x5ebd0,0);
    assert(errno==EDOM && guest_errno==91);
}
int main(void) {
    heap_tests();frame_tests();owner_and_stack_tests();
    puts("allocation audit sparse scalar/frame checks passed");return 0;
}
