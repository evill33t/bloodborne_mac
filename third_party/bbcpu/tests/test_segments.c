/* Guest context saves: segment reads must not stop the native CPU fallback. */
#include "../src/cpu/bbcpu.h"
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
uint64_t bb_image_base;
void *runtime_low_map(size_t size,int prot) {
    void *p=mmap(NULL,size,prot,MAP_PRIVATE|MAP_ANON,-1,0);
    return p==MAP_FAILED ? NULL : p;
}
static uint8_t *code;
static size_t used;
static uint64_t run(const uint8_t *bytes,size_t count,uint64_t argument) {
    const size_t at=used;assert(at+count<16384);memcpy(code+at,bytes,count);used=(at+count+15)&~15u;
    return bbcpu_call((uintptr_t)(code+at),1,&argument);
}
static void *other_thread(void *unused) {
    (void)unused;
    /* mov eax,0; mov ds,ax; mov ax,ds; ret */
    const uint8_t change[]={0x31,0xc0,0x8e,0xd8,0x66,0x8c,0xd8,0xc3};
    assert(run(change,sizeof(change),0)==0);return NULL;
}
int main(void) {
    code=runtime_low_map(16384,PROT_READ|PROT_WRITE);assert(code);
    bbcpu_add_guest_code((uintptr_t)code,16384);
    /* Exact six-byte instruction from the failed gameplay context save. */
    uint8_t memory[256];memset(memory,0xa5,sizeof(memory));
    const uint8_t crash[]={0x8c,0x8f,0xe8,0,0,0,0xb8,0x78,0x56,0x34,0x12,0xc3};
    assert(run(crash,sizeof(crash),(uintptr_t)memory)==0x12345678);
    assert(memory[0xe8]==0x43 && memory[0xe9]==0);
    for(unsigned k=0;k<sizeof(memory);++k)if(k!=0xe8 && k!=0xe9)assert(memory[k]==0xa5);
    const uint16_t expected[]={0x3b,0x43,0x3b,0x3b,0,0}; /* ES CS SS DS FS GS */
    for(unsigned segment=0;segment<6;++segment) {
        uint8_t store[]={0x8c,(uint8_t)(segment<<3),0xc3};
        memset(memory,0xa5,sizeof(memory));run(store,sizeof(store),(uintptr_t)memory);
        uint16_t value;memcpy(&value,memory,2);assert(value==expected[segment]);
        for(unsigned k=2;k<sizeof(memory);++k)assert(memory[k]==0xa5);
        /* A 16-bit destination preserves its high bits; 32/64 zero-extend. */
        uint8_t word[]={0x48,0xb8,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,
                        0x66,0x8c,(uint8_t)(0xc0|(segment<<3)),0xc3};
        assert(run(word,sizeof(word),0)==(UINT64_C(0x1122334455660000)|expected[segment]));
        uint8_t dword[]={0x48,0xc7,0xc0,0xff,0xff,0xff,0xff,0x8c,(uint8_t)(0xc0|(segment<<3)),0xc3};
        assert(run(dword,sizeof(dword),0)==expected[segment]);
        uint8_t qword[]={0x48,0xc7,0xc0,0xff,0xff,0xff,0xff,0x48,0x8c,(uint8_t)(0xc0|(segment<<3)),0xc3};
        assert(run(qword,sizeof(qword),0)==expected[segment]);
    }
    /* stc; mov eax,cs; pushfq; pop rax; and eax,1; ret: MOV preserves CF. */
    const uint8_t flags[]={0xf9,0x8c,0xc8,0x9c,0x58,0x83,0xe0,1,0xc3};
    assert(run(flags,sizeof(flags),0)==1);
    pthread_t thread;assert(!pthread_create(&thread,NULL,other_thread,NULL));assert(!pthread_join(thread,NULL));
    const uint8_t ds[]={0x31,0xc0,0x66,0x8c,0xd8,0xc3};
    assert(run(ds,sizeof(ds),0)==0x3b); /* selector state is per guest thread */
    uint64_t tls=UINT64_C(0x12345678abcdef01);bbcpu_set_thread_pointer((uintptr_t)&tls);
    /* A context selector restore leaves the runtime's explicit FS base intact. */
    const uint8_t fs[]={0x31,0xc0,0x8e,0xe0,0x64,0x48,0x8b,0x04,0x25,0,0,0,0,0xc3};
    assert(run(fs,sizeof(fs),0)==tls);
    puts("PASS: exact context-save MOV, six segment selectors, widths, flags, thread isolation and TLS");
    return 0;
}
