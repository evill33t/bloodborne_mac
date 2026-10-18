// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Gpu::Metal {
struct CachedTexture {
    std::uint64_t key,bytes;
    double last_used;
};
// Uploads are reconstructible from guest memory. Never apply this policy to
// render targets, whose newest contents may exist only on the GPU. A two-second
// grace period prevents pressure from repeatedly evicting the current scene.
inline std::vector<std::uint64_t> ExpiredTextures(std::vector<CachedTexture> entries,
        std::uint64_t budget,double now) {
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) { return a.last_used<b.last_used; });
    long double remaining=0;
    for (const auto& e:entries) remaining+=e.bytes;
    std::vector<std::uint64_t> result;
    for (const auto& e:entries) {
        const double idle=now-e.last_used;
        if (idle>=20 || (remaining>budget && idle>=2)) {
            result.push_back(e.key); remaining-=e.bytes;
        }
    }
    return result;
}
// Do not override point, unmipped, or comparison samplers (UI, data and shadows).
inline unsigned SceneAnisotropy(unsigned requested,unsigned original,unsigned min,unsigned mag,unsigned mip,unsigned compare) {
    return requested && (min&1) && (mag&1) && mip>=2 && compare==0
        ? requested : original;
}
}
