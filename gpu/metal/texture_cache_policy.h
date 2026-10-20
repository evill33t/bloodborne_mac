// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Gpu::Metal {
struct CachedTexture {
    std::uint64_t key,bytes;
    double last_used;
    std::uint64_t version=0;
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
struct TextureRetirementTicket {
    std::uint64_t key,version;
    double last_used;
};
// Lookup returns the current cache entry, never a retained Metal resource.
// Inspect at most `limit` records, including stale tickets, so reactivation
// cannot turn a bounded retirement step into an unbounded cache walk.
template<class Lookup>
inline std::vector<std::uint64_t> TextureRetirementBatch(
        std::span<const TextureRetirementTicket> tickets,std::size_t& cursor,
        Lookup lookup,std::uint64_t budget,long double remaining,double now,
        std::size_t limit=32) {
    std::vector<std::uint64_t> result;
    if(cursor>=tickets.size())return result;
    const auto end=cursor+std::min(limit,tickets.size()-cursor);
    for(;cursor<end;++cursor) {
        const auto& ticket=tickets[cursor];
        const auto live=lookup(ticket.key);
        if(!live || live->key!=ticket.key || live->version!=ticket.version ||
           live->last_used!=ticket.last_used)continue;
        const double idle=now-live->last_used;
        if(idle>=20 || (remaining>budget && idle>=2)) {
            result.push_back(ticket.key);remaining-=live->bytes;
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
