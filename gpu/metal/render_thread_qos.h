// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdlib>
#include <cstdio>
#include <cstring>
#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#endif

namespace Gpu::Metal::RenderThreadQos {
inline bool Configured(const char* value) noexcept {
    return value && !std::strcmp(value,"1");
}
// Dedicated frame production/presentation only, never guest workers or shader
// precompilation. A QoS hint leaves core selection to macOS; no affinity, real
// time policy, process-wide priority, or persistent power setting is changed.
inline int Apply(const char* thread) noexcept {
#if defined(__APPLE__)
    if(!Configured(std::getenv("BB_METAL_RENDER_THREAD_QOS")))return 0;
    const int result=pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE,0);
    if(Configured(std::getenv("BB_METAL_RENDER_THREAD_QOS_LOG"))) {
        qos_class_t actual;int relative=0;
        const int query=pthread_get_qos_class_np(pthread_self(),&actual,&relative);
        std::fprintf(stderr,"[render QoS] %s: set=%d query=%d class=%u relative=%d\n",
            thread,result,query,unsigned(query ? QOS_CLASS_UNSPECIFIED : actual),relative);
    }
    return result;
#else
    (void)thread;return 0;
#endif
}
}
