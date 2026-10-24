// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/render_thread_qos.h"
#include <cassert>
#include <thread>

int main() {
    using namespace Gpu::Metal::RenderThreadQos;
    assert(!Configured(nullptr) && !Configured("") && !Configured("0") &&
        !Configured("2") && !Configured("1x") && Configured("1"));
#if defined(__APPLE__)
    for(const char* option:{"0","1","bad"}) {
        assert(setenv("BB_METAL_RENDER_THREAD_QOS",option,1)==0);
        std::thread worker([&] {
            qos_class_t before,after;int relative_before=0,relative_after=0;
            assert(pthread_get_qos_class_np(pthread_self(),&before,&relative_before)==0);
            assert(Apply("fixture")==0);
            assert(pthread_get_qos_class_np(pthread_self(),&after,&relative_after)==0);
            if(Configured(option))assert(after==QOS_CLASS_USER_INTERACTIVE && relative_after==0);
            else assert(after==before && relative_after==relative_before);
        });
        worker.join();
    }
    unsetenv("BB_METAL_RENDER_THREAD_QOS");
#endif
    std::puts("Render QoS: strict admission, owned-thread policy and unchanged controls passed");
}
