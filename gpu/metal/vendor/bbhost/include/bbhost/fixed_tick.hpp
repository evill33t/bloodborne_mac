// SPDX-License-Identifier: GPL-3.0-or-later
// Mac adaptation: bundled plugins used 60-frame timers. Keep their clock at
// 60 Hz while leaving the public on_frame API at once per rendered frame.
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>

namespace bb {
class FixedTicks {
    std::chrono::steady_clock::time_point previous=std::chrono::steady_clock::now();
    double remainder=0;
public:
    int advance(double seconds) {
        if (!std::isfinite(seconds) || seconds<=0) return 0;
        // Match the engine's maximum 1/30 second simulation step after stalls.
        remainder+=std::min(seconds,1.0/30.0)*60.0;
        const auto ticks=int(remainder+1e-9);
        remainder=std::max(0.0,remainder-ticks);
        return ticks;
    }
    int update() {
        const auto now=std::chrono::steady_clock::now();
        const auto elapsed=std::chrono::duration<double>(now-previous).count();
        previous=now; return advance(elapsed);
    }
};
}
