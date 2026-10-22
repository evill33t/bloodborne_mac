/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BB_COMPAT_MACOS_H
#define BB_COMPAT_MACOS_H
#ifdef __APPLE__
#include <pthread.h>
#include <stdint.h>
#include <time.h>
#include <sys/resource.h>
#ifndef PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP PTHREAD_RECURSIVE_MUTEX_INITIALIZER
#endif
#ifdef __cplusplus
extern "C" {
#endif
void bb_macos_set_guest_tls(void *tcb);
int bb_macos_mutex_timedlock(pthread_mutex_t *mutex,const struct timespec *deadline);
int bb_macos_rwlock_timedrdlock(pthread_rwlock_t *rwlock,const struct timespec *deadline);
int bb_macos_rwlock_timedwrlock(pthread_rwlock_t *rwlock,const struct timespec *deadline);
int bb_macos_thread_rusage(struct rusage *usage);
int bb_macos_read_memory(uintptr_t source,void *destination,size_t size);
#ifdef __cplusplus
}
#endif
#endif
#endif
