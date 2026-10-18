#include "engine/menu_memory.h"

#include "core/elf.h"
#include "core/memory.h"
#include "engine/addr.h"
#include "log.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint64_t kMib = 1024 * 1024;

// `mov edx, imm32` (ba imm32) before each call that creates a Scaleform heap
// in 0x2359d50: the large-block heap, then the page heap.
constexpr std::uint64_t kBlockHeapSize = 0x235a465;
constexpr std::uint64_t kPageHeapSize = 0x235a479;
constexpr std::uint32_t kBlockHeapStock = 0x1200000, kPageHeapStock = 0x2200000;
constexpr std::uint32_t kBlockHeapMore = 32 * kMib, kPageHeapMore = 64 * kMib;

// SprjMemory's size table, [mode][16] qwords; MENU is entry 9 of mode 2.
constexpr std::uint64_t kMenuSize = 0x4b36d40 + 2 * 0x80 + 9 * 8;
constexpr std::uint64_t kMenuStock = 0x5200000;

bool patch_mov_edx(ElfImage* image, std::uint64_t bn, std::uint32_t stock, std::uint32_t want) {
    const std::uint64_t at = image->mem.slide + (bn - kPreferredGuestSlide);
    auto* p = static_cast<std::uint8_t*>(guest_ptr(image->mem, at));
    std::uint32_t imm = 0;
    std::memcpy(&imm, p + 1, 4);
    if (p[0] != 0xba || imm != stock) {
        host_log("menu memory: refused, 0x%llx is not mov edx, 0x%x", static_cast<unsigned long long>(bn), stock);
        return false;
    }
    const std::uint64_t lo = at & ~0xfffull, hi = (at + 5 + 0xfff) & ~0xfffull;
    if (!guest_protect_rwx(&image->mem, lo, hi - lo)) return false;
    std::memcpy(p + 1, &want, 4);
    guest_protect_rx(&image->mem, lo, hi - lo);
    return true;
}

}  // namespace

void menu_memory_install(ElfImage* image) {
    const std::uint64_t menu_at = image->mem.slide + (kMenuSize - kPreferredGuestSlide);
    auto* menu = static_cast<std::uint64_t*>(guest_ptr(image->mem, menu_at));
    if (*menu != kMenuStock) {
        host_log("menu memory: refused, MENU's size at 0x%llx is 0x%llx, not 0x%llx", static_cast<unsigned long long>(kMenuSize),
                 static_cast<unsigned long long>(*menu), static_cast<unsigned long long>(kMenuStock));
        return;
    }
    if (!guest_protect_rw(&image->mem, menu_at & ~0xfffull, 0x1000)) {
        host_log("menu memory: cannot unprotect MENU's size");
        return;
    }
    // MENU grows only by what the two heaps actually get, so a refused site
    // leaves its share as it was.
    const bool block = patch_mov_edx(image, kBlockHeapSize, kBlockHeapStock, kBlockHeapStock + kBlockHeapMore);
    const bool page = patch_mov_edx(image, kPageHeapSize, kPageHeapStock, kPageHeapStock + kPageHeapMore);
    *menu = kMenuStock + (block ? kBlockHeapMore : 0) + (page ? kPageHeapMore : 0);
    host_log("menu memory: Scaleform's heaps %llu and %llu MiB, MENU %llu MiB (were 18, 34 and 82)",
             static_cast<unsigned long long>((kBlockHeapStock + (block ? kBlockHeapMore : 0)) / kMib),
             static_cast<unsigned long long>((kPageHeapStock + (page ? kPageHeapMore : 0)) / kMib),
             static_cast<unsigned long long>(*menu / kMib));
}
