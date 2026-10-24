/* Owned executable model of initializer register allocation and publication.
 * Only short production guard fragments match the supported binary. No full
 * original procedure is reproduced. Run once with BB_JIT=1 and once with0. */
#define _DARWIN_C_SOURCE
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "cpu/bbcpu.h"

uint64_t bb_image_base;
void *runtime_low_map(size_t size,int prot) {
    void *memory=mmap(NULL,size,prot,MAP_PRIVATE|MAP_ANON,-1,0);
    return memory==MAP_FAILED ? NULL : memory;
}

typedef struct {uint64_t address,size,flags;} Segment;
typedef struct {uint64_t base,size;} LinkedModule;
static unsigned char *image;
static LinkedModule modules[1]={{0x56e0000,0xe0000}};
static uint64_t module_count=1;
static const char allocation_audit_eboot_hash[]="cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86";
static Segment ranges[]={{0,0x4000000,5},{0x5000000,0x6e0000,6}};
static unsigned char context[0x3520] __attribute__((aligned(16)));
static uint64_t label;
static int32_t guest_errno;
static int32_t *runtime_errno(void) {return &guest_errno;}
static int contained(uint64_t address,size_t bytes,uint64_t start,uint64_t size) {
    return address>=start && bytes<=size && address-start<=size-bytes;
}
static int bb_macos_read_memory(uintptr_t address,void *out,size_t bytes) {
    if(!contained(address,bytes,(uintptr_t)image,0x5800000) &&
       !contained(address,bytes,(uintptr_t)context,sizeof(context)) &&
       !contained(address,bytes,(uintptr_t)&label,sizeof(label)))return 0;
    memcpy(out,(void *)address,bytes);return 1;
}
static int allocation_audit_image_range(uint64_t offset,size_t bytes,uint64_t flags) {
    for(unsigned n=0;n<sizeof(ranges)/sizeof(ranges[0]);++n)
        if((ranges[n].flags&flags)==flags && contained(offset,bytes,ranges[n].address,ranges[n].size))return 1;
    return 0;
}
static int throw_audit_image_read(uint64_t address,void *out,size_t bytes) {
    if(address<(uintptr_t)image || !allocation_audit_image_range(address-(uintptr_t)image,bytes,4))return 0;
    return bb_macos_read_memory(address,out,bytes);
}
#include "../src/retirement_audit.inc"

static unsigned old_site_count;
static void old_site(const uint64_t regs[16],uint64_t address,uint32_t tag) {
    (void)regs;(void)address;(void)tag;++old_site_count;
}
static void store(uint64_t offset,const unsigned char *bytes,size_t size) {memcpy(image+offset,bytes,size);}
static void rel(uint64_t offset,unsigned char opcode,uint64_t target) {
    image[offset]=opcode;int64_t delta=(int64_t)target-(int64_t)(offset+5);
    assert(delta>=INT32_MIN && delta<=INT32_MAX);int32_t displacement=(int32_t)delta;
    memcpy(image+offset+1,&displacement,4);
}
static void write64(void *target,uint64_t value) {memcpy(target,&value,8);}
static void write32(void *target,uint32_t value) {memcpy(target,&value,4);}
static uint64_t read64(const void *target) {uint64_t value;memcpy(&value,target,8);return value;}
int main(void) {
    image=mmap(NULL,0x5800000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(image!=MAP_FAILED);
    bb_image_base=(uintptr_t)image;
    const unsigned char prefix[]={0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
        0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x48,0x89,0xfb};
    store(0x11d7200,prefix,sizeof(prefix));
    /* Independently authored entry: RDI=context, RSI=label; save the label in
     * R14 and enter the selector branch. Real RBX stays callee-saved. */
    const unsigned char setup[]={0x49,0x89,0xf6};store(0x11d7214,setup,sizeof(setup));
    rel(0x11d7217,0xe9,0x11d7314);
    const unsigned char selector[]={0x83,0xbb,0x1c,0x35,0,0,0,0x75,0x2e};
    store(0x11d7314,selector,sizeof(selector));
    const unsigned char from_label[]={0x4c,0x89,0xf0};store(0x11d731d,from_label,sizeof(from_label));
    rel(0x11d7320,0xe9,0x11d732c);
    const unsigned char data[]={0x48,0x89,0x83,0x30,0x0c,0,0,0x48,0xc7,0,0,0,0,0};
    store(0x11d732c,data,sizeof(data));
    const unsigned char publish[]={0x48,0x8b,0x83,0x30,0x0c,0,0,
        0x48,0x8d,0x0d,0x68,0x2b,0x2e,0x04,0x48,0x89,0x01,0x4c,0x89,0xf7};
    store(0x11d733a,publish,sizeof(publish));rel(0x11d734e,0xe8,0x11fae90);
    const unsigned char continuation[]={0x48,0x8b,0x03,0x48,0x89,0xdf,
        0xb8,0x2a,0,0,0,0x48,0x83,0xc4,0x18,0x5b,0x41,0x5c,0x41,0x5d,0x41,0x5e,0x41,0x5f,0x5d,0xc3};
    store(0x11d7353,continuation,sizeof(continuation));
    /* Owned callee returns with RBX preserved and makes observable progress. */
    const unsigned char callee[]={0x48,0xc7,0x07,7,0,0,0,0xc3};store(0x11fae90,callee,sizeof(callee));
    bbcpu_add_guest_code((uintptr_t)image,0x4000000);
    setenv("BB_RUNTIME_EXIT_AUDIT","1",1);setenv("BB_MAC_EBOOT_SHA256",allocation_audit_eboot_hash,1);
    install_retirement_audit();assert(atomic_load(&retirement_audit_installed));
    bbcpu_set_probe((uintptr_t)image+0x11d734b,old_site,0);
    write64(context+0x3400,9);
    uint64_t args[]={(uintptr_t)context,(uintptr_t)&label};
    assert(bbcpu_call((uintptr_t)image+0x11d7200,2,args)==42);
    assert(label==7 && read64(context+0xc30)==(uintptr_t)&label && read64(image+0x54b9eb0)==(uintptr_t)&label);
    assert(atomic_load(&retirement_audit_context)==(uintptr_t)context);
    assert(atomic_load(&retirement_audit_init_seen)==1 && atomic_load(&retirement_audit_init_selected)==1);
    const char *jit=getenv("BB_JIT");if(jit && !strcmp(jit,"1"))assert(old_site_count==0);
    retirement_audit_snapshot(0);
    /* The alternate branch reaches the old site but is not a publisher. */
    atomic_store(&retirement_audit_context,0);write32(context+0x351c,1);
    write64(context+0xc30,0);write64(image+0x54b9eb0,0);old_site_count=0;
    assert(bbcpu_call((uintptr_t)image+0x11d7200,2,args)==42);
    assert(old_site_count==1 && !atomic_load(&retirement_audit_context));
    assert(atomic_load(&retirement_audit_init_seen)==2 && atomic_load(&retirement_audit_init_selected)==1);
    assert(read64(context+0xc30)==0 && read64(image+0x54b9eb0)==0);
    setenv("BB_RUNTIME_EXIT_AUDIT","0",1);write32(context+0x351c,0);
    assert(bbcpu_call((uintptr_t)image+0x11d7200,2,args)==42);
    assert(!atomic_load(&retirement_audit_context) && atomic_load(&retirement_audit_init_seen)==2);
    puts("retirement observation executed through fallthrough and CALL return");return 0;
}
