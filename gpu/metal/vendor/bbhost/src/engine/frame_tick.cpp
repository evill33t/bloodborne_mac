#include "guest_abi.h"
#include "engine/frame_tick.h"

#include "core/elf.h"
#include "core/memory.h"
#include "core/thunk.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "log.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace {

struct TickSite {
    std::uint64_t at;     // runtime address of the entry
    void* trampoline;     // the moved prologue, then a jump back past it
    std::atomic<std::uint64_t> calls{0}, runs{0};
};
std::vector<TickSite*> g_sites;

// This frame's ticks, the same for every site and every call in the frame;
// the remainder carried to the next.
std::atomic<int> g_ticks{1};
double g_carry = 0.0;

void* exec_page() {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void* p = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

// The hooked entry: saved[0..5] are r9 r8 rcx rdx rsi rdi as the call
// brought them, saved[-1] what a skipped call returns, and xmm0-7 lie 136
// bytes below (core/thunk.cpp's prologue stub).
GUEST_ABI std::int64_t tick_hook(std::uint64_t id, const std::uint64_t* saved) {
    if (id == 0 || id > g_sites.size()) return 0;
    TickSite* s = g_sites[id - 1];
    s->calls.fetch_add(1, std::memory_order_relaxed);
    const int n = g_ticks.load(std::memory_order_relaxed);
    if (n <= 0) return 1;  // no tick this frame: the call is skipped (it returns 0)
    if (n > 1) {
        GuestCallRegs r{};
        r.fn = s->trampoline;
        r.ints[0] = saved[5];  // rdi
        r.ints[1] = saved[4];  // rsi
        r.ints[2] = saved[3];  // rdx
        r.ints[3] = saved[2];  // rcx
        r.ints[4] = saved[1];  // r8
        r.ints[5] = saved[0];  // r9
        const auto* xmm = reinterpret_cast<const std::uint8_t*>(saved) - 136;
        for (int k = 0; k < 8; ++k) std::memcpy(&r.xmm[k], xmm + 16 * k, 8);
        for (int k = 1; k < n; ++k) {
            GuestCallRegs once = r;  // the callee may clobber what it was given
            hle_call_guest_regs(&once);
            s->runs.fetch_add(1, std::memory_order_relaxed);
        }
    }
    s->runs.fetch_add(1, std::memory_order_relaxed);
    return 0;  // the last tick through the hook's own path
}

}  // namespace

void frame_tick_frame(float step) {
    if (g_sites.empty() || !(step > 0.0f)) return;
    g_carry += static_cast<double>(step) * 30.0;
    const int n = static_cast<int>(g_carry + 1e-6);  // a 1/30 s step is one tick even after float rounding
    g_carry -= n;
    g_ticks.store(n, std::memory_order_relaxed);
}

void frame_tick_install(ElfImage* image) {
    const char* e = std::getenv("BBHOST_FIXED_TICK");
    if (!e || !*e) return;
    const char* q = e;
    while (*q) {
        char* end = nullptr;
        const unsigned long long bn = std::strtoull(q, &end, 16);
        if (end == q || *end != ':') break;
        const unsigned long n = std::strtoul(end + 1, &end, 10);
        q = *end ? end + 1 : end;
        if (bn < kPreferredGuestSlide || n < 14 || n > 48) continue;
        const std::uint64_t at = image->mem.slide + (bn - kPreferredGuestSlide);
        std::uint8_t have[48];
        std::memcpy(have, guest_ptr(image->mem, at), n);
        auto* page = static_cast<std::uint8_t*>(exec_page());
        if (!page) continue;
        // The trampoline: the moved instructions, then jmp [rip+0] to the rest.
        std::memcpy(page, have, n);
        page[n] = 0xff;
        page[n + 1] = 0x25;
        std::memset(page + n + 2, 0, 4);
        const std::uint64_t back = at + n;
        std::memcpy(page + n + 6, &back, 8);
        auto* site = new TickSite;
        site->at = at;
        site->trampoline = page;
        g_sites.push_back(site);
        if (!engine_prologue_hook(image, at, have, n, reinterpret_cast<void*>(&tick_hook), g_sites.size())) {
            g_sites.pop_back();
            delete site;
            host_log("frame tick: 0x%llx refused (no room for its hook)", bn);
            continue;
        }
        host_log("frame tick: 0x%llx runs as a 30 Hz tick (BBHOST_FIXED_TICK, a test switch)", bn);
    }
}
