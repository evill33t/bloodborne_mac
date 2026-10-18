/* SPDX-FileCopyrightText: Copyright 2025-2026 shadPS4 Emulator Project
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Darwin guest TLS follows shadPS4's src/core/tls.cpp LDT approach.
 * The remaining helpers adapt host APIs without changing guest layouts. */
#include "compat_macos.h"
#include <architecture/i386/table.h>
#include <i386/user_ldt.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>

/* The Mach-O segment reserves these low addresses. Its 32-bit segment bases
 * allow FS guest accesses while Darwin continues using GS for host pthreads. */
__asm__(".zerofill TCB_SPACE,TCB_SPACE,__tcb_space,0x3fc000");
#define LDT_BASE UINT32_C(0x4000)
#define LDT_BYTES UINT32_C(0x3fc000)
#define LDT_PAGE UINT32_C(0x1000)
#define LDT_COUNT (LDT_BYTES/LDT_PAGE)
static pthread_once_t tls_once=PTHREAD_ONCE_INIT;
static pthread_mutex_t tls_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_key_t tls_key;
static unsigned char tls_used[LDT_COUNT];
static void tls_fail(const char *operation) {
    fprintf(stderr,"STOP: Darwin guest TLS %s (errno %d)\n",operation,errno);
    exit(21);
}
static void tls_release(void *page) {
    size_t index=((uintptr_t)page-LDT_BASE)/LDT_PAGE;
    pthread_mutex_lock(&tls_lock);
    if (index<LDT_COUNT) tls_used[index]=0;
    pthread_mutex_unlock(&tls_lock);
}
static void tls_initialize(void) {
    unsigned long reserved_size=0;
    const void *reserved=getsectiondata(&_mh_execute_header,"TCB_SPACE","TCB_SPACE",&reserved_size);
    if ((uintptr_t)reserved!=LDT_BASE || reserved_size!=LDT_BYTES)
        tls_fail("requires the reserved low Mach-O segment");
    if (mmap((void *)(uintptr_t)LDT_BASE,LDT_BYTES,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==MAP_FAILED) tls_fail("mapping failed");
    if (pthread_key_create(&tls_key,tls_release)) tls_fail("key allocation failed");
}
void bb_macos_set_guest_tls(void *tcb) {
    pthread_once(&tls_once,tls_initialize);
    void *page=pthread_getspecific(tls_key);
    if (!page) {
        pthread_mutex_lock(&tls_lock);
        unsigned index=0;
        while (index<LDT_COUNT && tls_used[index]) ++index;
        if (index==LDT_COUNT) { pthread_mutex_unlock(&tls_lock); tls_fail("entries exhausted"); }
        tls_used[index]=1;
        pthread_mutex_unlock(&tls_lock);
        uintptr_t address=LDT_BASE+index*LDT_PAGE;
        union ldt_entry entry={0};
        entry.data.limit00=LDT_PAGE-1;
        entry.data.base00=(uint16_t)address;
        entry.data.base16=(uint8_t)(address>>16);
        entry.data.base24=(uint8_t)(address>>24);
        entry.data.type=DESC_DATA_WRITE;
        entry.data.dpl=3;
        entry.data.present=1;
        entry.data.stksz=DESC_DATA_32B;
        entry.data.granular=DESC_GRAN_BYTE;
        unsigned slot=index+8;
        if (i386_set_ldt((int)slot,&entry,1)!=(int)slot) tls_fail("LDT installation failed");
        const uint16_t selector=(uint16_t)((slot<<3)|7);
        __asm__ volatile("mov %0,%%fs"::"r"(selector));
        page=(void *)address;
        if (pthread_setspecific(tls_key,page)) tls_fail("thread association failed");
    }
    memcpy(page,tcb,256);
    void *observed;
    __asm__ volatile("mov %%fs:0,%0":"=r"(observed));
    if (observed!=*(void **)tcb) tls_fail("self-pointer validation failed");
}

static int pause_until(const struct timespec *deadline) {
    if (!deadline || deadline->tv_nsec<0 || deadline->tv_nsec>=1000000000) return EINVAL;
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME,&now)) return errno;
    if (now.tv_sec>deadline->tv_sec ||
        (now.tv_sec==deadline->tv_sec && now.tv_nsec>=deadline->tv_nsec)) return ETIMEDOUT;
    int64_t left=(int64_t)(deadline->tv_sec-now.tv_sec)*1000000000+deadline->tv_nsec-now.tv_nsec;
    struct timespec nap={0,left<1000000 ? (long)left : 1000000};
    while (nanosleep(&nap,&nap) && errno==EINTR) {}
    return 0;
}
int bb_macos_mutex_timedlock(pthread_mutex_t *mutex,const struct timespec *deadline) {
    int result;
    while ((result=pthread_mutex_trylock(mutex))==EBUSY) {
        result=pause_until(deadline);
        if (result) return result;
    }
    return result;
}
static int rw_timedlock(pthread_rwlock_t *rwlock,const struct timespec *deadline,int writer) {
    int result;
    while ((result=writer ? pthread_rwlock_trywrlock(rwlock) : pthread_rwlock_tryrdlock(rwlock))==EBUSY) {
        result=pause_until(deadline);
        if (result) return result;
    }
    return result;
}
int bb_macos_rwlock_timedrdlock(pthread_rwlock_t *rwlock,const struct timespec *deadline) {
    return rw_timedlock(rwlock,deadline,0);
}
int bb_macos_rwlock_timedwrlock(pthread_rwlock_t *rwlock,const struct timespec *deadline) {
    return rw_timedlock(rwlock,deadline,1);
}
int bb_macos_thread_rusage(struct rusage *usage) {
    thread_basic_info_data_t info;
    mach_msg_type_number_t count=THREAD_BASIC_INFO_COUNT;
    mach_port_t thread=mach_thread_self();
    kern_return_t result=thread_info(thread,THREAD_BASIC_INFO,(thread_info_t)&info,&count);
    mach_port_deallocate(mach_task_self(),thread);
    if (result!=KERN_SUCCESS) return -1;
    memset(usage,0,sizeof(*usage));
    usage->ru_utime=(struct timeval){info.user_time.seconds,info.user_time.microseconds};
    usage->ru_stime=(struct timeval){info.system_time.seconds,info.system_time.microseconds};
    return 0;
}
int bb_macos_read_memory(uintptr_t source,void *destination,size_t size) {
    mach_vm_size_t read=0;
    return mach_vm_read_overwrite(mach_task_self(),source,size,(mach_vm_address_t)destination,&read)
        ==KERN_SUCCESS && read==size;
}
