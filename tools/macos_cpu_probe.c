/* SPDX-License-Identifier: GPL-2.0-or-later
 * This Mach-O helper intentionally targets x86-64. It verifies the CPU path
 * needed by the original game on Apple Silicon, independently of the renderer. */
#if !defined(__APPLE__) || !defined(__x86_64__)
#error Build this probe for macOS x86-64.
#endif
#include <cpuid.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <unistd.h>

int main(void) {
    int translated=0;
    size_t translated_size=sizeof(translated);
    int detected=!sysctlbyname("sysctl.proc_translated",&translated,&translated_size,NULL,0);
    printf("CPU process: x86_64; Rosetta: %s\n",detected ? (translated ? "yes" : "no") : "unknown");
    unsigned a=0,b=0,c=0,d=0,basic=0,leaf7=0,extended=0;
    if (__get_cpuid(1,&a,&b,&c,&d)) basic=c;
    if (__get_cpuid_max(0,NULL)>=7) { __cpuid_count(7,0,a,b,c,d); leaf7=b; }
    if (__get_cpuid(0x80000001,&a,&b,&c,&d)) extended=c;
    unsigned xstate=0;
    if (((basic>>27)&1) && ((basic>>28)&1)) {
        unsigned high;
        __asm__ volatile("xgetbv" : "=a"(xstate),"=d"(high) : "c"(0));
    }
    const struct { const char *name; int present; } features[]={
        {"AVX",(xstate&6)==6}, {"BMI1",(leaf7>>3)&1}, {"MOVBE",(basic>>22)&1},
        {"LZCNT",(extended>>5)&1}, {"POPCNT",(basic>>23)&1}, {"SSE4.2",(basic>>20)&1}
    };
    int missing=0;
    for (size_t i=0;i<sizeof(features)/sizeof(*features);++i) {
        printf("%s: %s\n",features[i].name,features[i].present ? "available" : "missing");
        missing|=!features[i].present;
    }
    if (missing) {
        fputs("STOP: this process cannot run the unmodified game instruction set.\n",stderr);
        return 24;
    }
    /* A dynamically loaded SysV guest function: load 8 floats, add, store.
     * Actual execution checks AVX translation rather than relying on CPUID. */
    const unsigned char code[]={0xc5,0xfc,0x10,0x07,0xc5,0xfc,0x58,0x06,
        0xc5,0xfc,0x11,0x02,0xc5,0xf8,0x77,0xc3};
    const size_t page=(size_t)sysconf(_SC_PAGESIZE);
    void *mapping=mmap(NULL,page,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if (mapping==MAP_FAILED) { perror("guest fixture mmap"); return 1; }
    memcpy(mapping,code,sizeof(code));
    if (mprotect(mapping,page,PROT_READ|PROT_EXEC)) {
        perror("guest fixture mprotect"); munmap(mapping,page); return 1;
    }
    float lhs[8],rhs[8],result[8]={0};
    for (int i=0;i<8;++i) { lhs[i]=(float)i; rhs[i]=(float)(10-i); }
    ((void (*)(const float*,const float*,float*))mapping)(lhs,rhs,result);
    munmap(mapping,page);
    for (int i=0;i<8;++i) if (result[i]!=10.f) {
        fputs("FAIL: dynamically loaded AVX fixture\n",stderr); return 1;
    }
    puts("PASS: dynamically loaded x86-64 SysV code and AVX execution");
    return 0;
}
