#include "engine/image_text.h"

#include "core/elf.h"
#include "core/memory.h"
#include "log.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

constexpr std::uint32_t kExec = 1, kWrite = 2;

const ElfSegment* segment_of(const ElfImage& image, std::uint64_t va, std::uint64_t n) {
    for (const ElfSegment& s : image.segments) {
        if (va >= s.va && va + n <= s.va + s.memsz) return &s;
    }
    return nullptr;
}

struct Site {
    std::uint64_t va;  // the lea's first byte (ELF address)
    std::int32_t disp;
};

}  // namespace

ImageTextResult image_replace_text(ElfImage* image, const ImageText* items, std::size_t count) {
    ImageTextResult out;
    GuestMemory& mem = image->mem;
    // Each text into the tail, and where each string's references go now.
    std::unordered_map<std::uint64_t, std::uint64_t> to;
    for (std::size_t i = 0; i < count; ++i) {
        const ImageText& t = items[i];
        const ElfSegment* seg = segment_of(*image, t.at, 2);
        const std::size_t bytes = (t.text.size() + 1) * 2;
        if (!seg || (seg->flags & kWrite) || (t.at & 1) || t.text.empty() || to.count(t.at) ||
            image->tail_used + bytes > image->tail_size) {
            ++out.rejected;
            continue;
        }
        const std::uint64_t va = image->tail_va + image->tail_used;
        auto* dst = static_cast<std::uint8_t*>(guest_ptr(mem, mem.slide + va));
        std::memcpy(dst, t.text.data(), bytes - 2);
        dst[bytes - 2] = dst[bytes - 1] = 0;
        image->tail_used += (bytes + 7) & ~std::size_t(7);
        to[t.at] = va;
    }
    if (to.empty()) return out;

    // The code: lea r64, [rip + disp32] (REX.W 8D, mod 00, rm 101) whose
    // target is one of the strings.
    std::unordered_map<std::uint64_t, std::size_t> refs;
    std::vector<Site> sites;
    for (const ElfSegment& s : image->segments) {
        if (!(s.flags & kExec)) continue;
        const auto* p = static_cast<const std::uint8_t*>(guest_ptr(mem, mem.slide + s.va));
        for (std::uint64_t i = 0; i + 7 <= s.filesz; ++i) {
            if ((p[i] != 0x48 && p[i] != 0x4c) || p[i + 1] != 0x8d || (p[i + 2] & 0xc7) != 0x05) continue;
            std::int32_t disp;
            std::memcpy(&disp, p + i + 3, 4);
            const std::uint64_t next = s.va + i + 7;
            auto it = to.find(next + static_cast<std::uint64_t>(static_cast<std::int64_t>(disp)));
            if (it == to.end()) continue;
            const std::int64_t moved = static_cast<std::int64_t>(it->second) - static_cast<std::int64_t>(next);
            if (moved < INT32_MIN || moved > INT32_MAX) continue;
            sites.push_back({s.va + i, static_cast<std::int32_t>(moved)});
            ++refs[it->first];
        }
    }
    // Written a page at a time, each page writable only while it is.
    for (std::size_t i = 0; i < sites.size();) {
        const std::uint64_t page = (mem.slide + sites[i].va) & ~0xfffull;
        std::size_t j = i;
        while (j < sites.size() && ((mem.slide + sites[j].va + 6) & ~0xfffull) <= page + 0x1000) ++j;
        const std::uint64_t end = (mem.slide + sites[j - 1].va + 7 + 0xfff) & ~0xfffull;
        if (!guest_protect_rwx(&mem, page, end - page)) {
            host_log("image-text: cannot write the code page 0x%llx", static_cast<unsigned long long>(page - mem.slide));
            i = j;
            continue;
        }
        for (std::size_t k = i; k < j; ++k) {
            std::memcpy(guest_ptr(mem, mem.slide + sites[k].va + 3), &sites[k].disp, 4);
            ++out.leas;
        }
        guest_protect_rx(&mem, page, end - page);
        i = j;
    }

    // The data: the pointers the loader relocated to one of the strings.
    for (const auto& [slot, value] : image->relative) {
        auto it = to.find(value);
        if (it == to.end()) continue;
        const ElfSegment* seg = segment_of(*image, slot, 8);
        if (!seg) continue;
        const std::uint64_t at = mem.slide + slot;
        const std::uint64_t lo = at & ~0xfffull, hi = (at + 8 + 0xfff) & ~0xfffull;
        const bool locked = !(seg->flags & kWrite);
        if (locked && !guest_protect_rwx(&mem, lo, hi - lo)) continue;
        const std::uint64_t ptr = mem.slide + it->second;
        std::memcpy(guest_ptr(mem, at), &ptr, 8);
        if (locked) {
            if (seg->flags & kExec) guest_protect_rx(&mem, lo, hi - lo);
            else guest_protect_rw(&mem, lo, hi - lo);
        }
        ++out.pointers;
        ++refs[it->first];
    }
    out.strings = refs.size();
    out.unreferenced = to.size() - refs.size();
    return out;
}
