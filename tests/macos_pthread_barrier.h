/* SPDX-License-Identifier: GPL-2.0-or-later
 * Test-harness barrier: Darwin provides no POSIX pthread_barrier_t. */
#pragma once
#include <pthread.h>
#include <errno.h>
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    unsigned count,waiting,generation;
} BbTestBarrier;
static inline int bb_test_barrier_init(BbTestBarrier *barrier,const void *attr,unsigned count) {
    (void)attr;
    if (!count) return EINVAL;
    int e=pthread_mutex_init(&barrier->mutex,NULL);
    if (e) return e;
    e=pthread_cond_init(&barrier->changed,NULL);
    if (e) { pthread_mutex_destroy(&barrier->mutex); return e; }
    barrier->count=count; barrier->waiting=barrier->generation=0;
    return 0;
}
static inline int bb_test_barrier_wait(BbTestBarrier *barrier) {
    pthread_mutex_lock(&barrier->mutex);
    unsigned generation=barrier->generation;
    if (++barrier->waiting==barrier->count) {
        barrier->waiting=0; ++barrier->generation;
        pthread_cond_broadcast(&barrier->changed);
        pthread_mutex_unlock(&barrier->mutex);
        return 1;
    }
    while (generation==barrier->generation) pthread_cond_wait(&barrier->changed,&barrier->mutex);
    pthread_mutex_unlock(&barrier->mutex);
    return 0;
}
static inline int bb_test_barrier_destroy(BbTestBarrier *barrier) {
    int e=pthread_cond_destroy(&barrier->changed);
    return e ? e : pthread_mutex_destroy(&barrier->mutex);
}
#define pthread_barrier_t BbTestBarrier
#define pthread_barrier_init bb_test_barrier_init
#define pthread_barrier_wait bb_test_barrier_wait
#define pthread_barrier_destroy bb_test_barrier_destroy
#define PTHREAD_BARRIER_SERIAL_THREAD 1
