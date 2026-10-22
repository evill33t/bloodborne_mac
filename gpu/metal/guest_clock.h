// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#ifdef BB_CPU_TRANSLATOR
#include "cpu/bbcpu.h"
#elif defined(__x86_64__)
#include <x86intrin.h>
#else
#include <chrono>
#endif
namespace MacGuestClock {
inline std::uint64_t ReadTsc() {
#ifdef BB_CPU_TRANSLATOR
    return bbcpu_tsc(); // same 1.6 GHz clock as guest RDTSC and libkernel
#elif defined(__x86_64__)
    return __rdtsc();
#else
    const auto ns=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    return ns/5*8+(ns%5)*8/5;
#endif
}
}
