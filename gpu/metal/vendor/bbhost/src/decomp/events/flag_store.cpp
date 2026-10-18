#include "decomp/events/flag_store.h"

#include "core/thunk.h"
#include "decomp/decomp.h"
#include "decomp/guest.h"
#include "log.h"

#include <atomic>

using namespace decomp;

namespace sprj_event_flag {
namespace {

// The game's memory, read as the game reads it: plainly.
struct Direct {
    template <typename T>
    bool operator()(std::uint64_t at, T* out) const {
        *out = *reinterpret_cast<const T*>(static_cast<std::uintptr_t>(at));
        return true;
    }
};

inline std::uint32_t bit_at(std::uint64_t data, std::uint32_t f) {
    return (*reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(data + (f >> 3))) & mask_of(f)) ? 1u : 0u;
}

// GetEventFlagValue's read: `count` flags from `first`, the last as bit 0.
std::uint32_t read_value(std::uint64_t man, std::uint32_t first, std::uint32_t count) {
    const Direct load;
    std::uint32_t size = 0;
    if (!load(man + kBlockSize, &size) || !size) return 0;
    const std::uint32_t block = first / size, bit = first - block * size;
    if (bit + count < size) {
        const std::uint64_t data = block_data(man, block, load);
        if (!data || !count) return 0;
        std::uint32_t v = 0;
        for (std::uint32_t i = 0; i != count; ++i) {
            if (bit_at(data, bit + count - 1 - i)) v += 1u << (i & 31);
        }
        return v;
    }
    // Into the next block: this block's tail is the low part, the next one's
    // head the high part above it.
    const std::uint64_t d0 = block_data(man, block, load), d1 = block_data(man, block + 1, load);
    const std::uint32_t n1 = size - bit;
    std::uint32_t lo = 0, hi = 0;
    for (std::uint32_t i = 0; i != n1; ++i) {
        if (d0 && bit_at(d0, size - 1 - i)) lo += 1u << (i & 31);
    }
    if (n1 == count) return lo;
    const std::uint32_t n2 = count - n1;
    for (std::uint32_t i = 0; i != n2; ++i) {
        if (d1 && bit_at(d1, n2 - 1 - i)) hi += 1u << (i & 31);
    }
    return (hi << (n1 & 31)) | lo;
}

// SetEventFlagValue's writes, in its order: fn(byte, mask, flag id, bit).
template <class Fn>
void walk_write(std::uint64_t man, std::uint32_t first, std::uint32_t count, std::uint32_t value, Fn&& fn) {
    const Direct load;
    std::uint32_t size = 0;
    if (!load(man + kBlockSize, &size) || !size) return;
    const std::uint32_t block = first / size, bit = first - block * size;
    if (bit + count < size) {
        const std::uint64_t data = block_data(man, block, load);
        if (!data || !count) return;
        for (std::uint32_t i = 0; i != count; ++i) {
            const std::uint32_t f = bit + count - 1 - i;
            fn(data + (f >> 3), mask_of(f), block * size + f, (value >> (i & 31)) & 1u);
        }
        return;
    }
    const std::uint64_t d0 = block_data(man, block, load), d1 = block_data(man, block + 1, load);
    const std::uint32_t n1 = size - bit;
    for (std::uint32_t i = 0; i != n1; ++i) {
        const std::uint32_t f = size - 1 - i;
        if (d0) fn(d0 + (f >> 3), mask_of(f), block * size + f, (value >> (i & 31)) & 1u);
    }
    if (n1 == count) return;
    const std::uint32_t rest = value >> (n1 & 31), n2 = count - n1;
    for (std::uint32_t i = 0; i != n2; ++i) {
        const std::uint32_t f = n2 - 1 - i;
        if (d1) fn(d1 + (f >> 3), mask_of(f), (block + 1) * size + f, (rest >> (i & 31)) & 1u);
    }
}

// The game's read-modify-write of a flag's byte, and the change noted.
inline void put(std::uint64_t at, std::uint8_t mask, std::uint32_t id, bool on) {
    auto* byte = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(at));
    const std::uint8_t was = *byte, now = on ? static_cast<std::uint8_t>(was | mask) : static_cast<std::uint8_t>(was & ~mask);
    *byte = now;
    if (now != was) event_flag_changed(id, on);
}

}  // namespace
}  // namespace sprj_event_flag

using namespace sprj_event_flag;

// ---- The four, in the game's place --------------------------------------

// 0x17cfc00: one flag, and whether its block exists (a 32-bit int).
DECOMP_LEAF std::uint64_t IsEventFlag(std::uint64_t man, std::uint32_t id, std::int32_t* exists) {
    std::uint64_t byte = 0;
    std::uint8_t mask = 0;
    if (!locate(man, id, Direct{}, &byte, &mask)) {
        *exists = 0;
        return 0;
    }
    *exists = 1;
    return (*reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(byte)) & mask) ? 1 : 0;
}

// 0x17cfcc0: sets the flag when `on` (all 32 bits) is nonzero, else clears it.
DECOMP_LEAF void SetEventFlag(std::uint64_t man, std::uint32_t id, std::uint32_t on) {
    std::uint64_t byte = 0;
    std::uint8_t mask = 0;
    if (locate(man, id, Direct{}, &byte, &mask)) put(byte, mask, id, on != 0);
}

// 0x17cfd80
DECOMP_LEAF std::uint64_t GetEventFlagValue(std::uint64_t man, std::uint32_t first, std::uint32_t count) {
    return read_value(man, first, count);
}

// 0x17d0060
DECOMP_LEAF void SetEventFlagValue(std::uint64_t man, std::uint32_t first, std::uint32_t count, std::uint32_t value) {
    walk_write(man, first, count, value,
               [](std::uint64_t at, std::uint8_t mask, std::uint32_t id, std::uint32_t on) { put(at, mask, id, on != 0); });
}

// ---- Compare runs: the game's version does the work, ours must agree -----

namespace {

void* g_game_is = nullptr;
void* g_game_set = nullptr;
void* g_game_get_value = nullptr;
void* g_game_set_value = nullptr;
DecompCompare g_cmp_is, g_cmp_set, g_cmp_get_value, g_cmp_set_value;

void count(DecompCompare& c, bool same, const char* fn, std::uint32_t id, std::uint32_t n, ull theirs, ull ours) {
    c.calls.fetch_add(1, std::memory_order_relaxed);
    if (same) return;
    if (c.differ.fetch_add(1, std::memory_order_relaxed) < 8) {
        host_log("decomp: %s(%u, %u) differs: the game's %llu, ours %llu", fn, id, n, theirs, ours);
    }
}

GUEST_ABI std::uint64_t compare_is(std::uint64_t man, std::uint32_t id, std::int32_t* exists) {
    std::int32_t ours_exists = 0;
    const std::uint64_t ours = IsEventFlag(man, id, &ours_exists);
    const auto theirs = hle_call_guest<std::uint64_t>(g_game_is, man, id, exists);
    count(g_cmp_is, theirs == ours && *exists == ours_exists, "IsEventFlag", id, 1, theirs, ours);
    return theirs;
}

GUEST_ABI std::uint64_t compare_get_value(std::uint64_t man, std::uint32_t first, std::uint32_t n) {
    const std::uint64_t ours = GetEventFlagValue(man, first, n);
    const auto theirs = hle_call_guest<std::uint64_t>(g_game_get_value, man, first, n);
    count(g_cmp_get_value, theirs == ours, "GetEventFlagValue", first, n, theirs, ours);
    return theirs;
}

// A setter's compare: ours says which bits the call writes and to what; the
// game's then runs, and each bit must read back as ours said.
struct Write {
    std::uint64_t at;
    std::uint8_t mask;
    std::uint8_t was;
    std::uint32_t id, on;
};
constexpr int kMaxWrites = 64;

GUEST_ABI void compare_set(std::uint64_t man, std::uint32_t id, std::uint32_t on) {
    std::uint64_t at = 0;
    std::uint8_t mask = 0;
    const bool found = locate(man, id, Direct{}, &at, &mask);
    const std::uint8_t was = found ? *reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(at)) : 0;
    hle_call_guest<std::int64_t>(g_game_set, man, id, on);
    if (!found) {
        count(g_cmp_set, true, "SetEventFlag", id, 1, 0, 0);
        return;
    }
    const std::uint8_t now = *reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(at));
    count(g_cmp_set, ((now & mask) != 0) == (on != 0), "SetEventFlag", id, 1, (now & mask) != 0, on != 0);
    if ((was & mask) != (now & mask)) event_flag_changed(id, (now & mask) != 0);
}

GUEST_ABI void compare_set_value(std::uint64_t man, std::uint32_t first, std::uint32_t n, std::uint32_t value) {
    Write writes[kMaxWrites];
    int k = 0;
    bool fits = true;
    walk_write(man, first, n, value, [&](std::uint64_t at, std::uint8_t mask, std::uint32_t id, std::uint32_t on) {
        if (k == kMaxWrites) {
            fits = false;
            return;
        }
        writes[k++] = {at, mask, *reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(at)), id, on};
    });
    hle_call_guest<std::int64_t>(g_game_set_value, man, first, n, value);
    bool same = true;
    for (int i = 0; i < k; ++i) {
        const std::uint8_t now = *reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(writes[i].at));
        same = same && ((now & writes[i].mask) != 0) == (writes[i].on != 0);
        if ((writes[i].was & writes[i].mask) != (now & writes[i].mask)) event_flag_changed(writes[i].id, (now & writes[i].mask) != 0);
    }
    if (fits) count(g_cmp_set_value, same, "SetEventFlagValue", first, n, value, value);
}

// The entries' first whole instructions (14 or more bytes), as the eboot has them.
constexpr std::uint8_t kIsEntry[] = {0x53, 0x49, 0x89, 0xd0, 0x8b, 0x4f, 0x1c, 0x31,
                                     0xd2, 0x89, 0xf0, 0xf7, 0xf1, 0x41, 0x89, 0xc2};
constexpr std::uint8_t kSetEntry[] = {0x41, 0x89, 0xd0, 0x8b, 0x4f, 0x1c, 0x31, 0xd2,
                                      0x89, 0xf0, 0xf7, 0xf1, 0x41, 0x89, 0xc1};
constexpr std::uint8_t kGetValueEntry[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x53, 0x41, 0x89,
                                           0xd0, 0x44, 0x8b, 0x4f, 0x1c, 0x31, 0xd2};
constexpr std::uint8_t kSetValueEntry[] = {0x55, 0x41, 0x57, 0x41, 0x56, 0x53, 0x41, 0x89,
                                           0xc8, 0x41, 0x89, 0xd1, 0x44, 0x8b, 0x57, 0x1c};

// ---- The change ring: many writers, the main thread reads ---------------

struct Slot {
    std::atomic<std::uint32_t> seq{0};
    std::atomic<std::uint32_t> id{0}, value{0};
};
// The title flips ~4,700 flags in one frame (a range the game sets up), so
// the ring holds a few such bursts between two frames: 768 KiB.
constexpr std::uint32_t kRing = 1u << 16;
Slot g_ring[kRing];
std::atomic<std::uint32_t> g_head{0};
std::uint32_t g_tail = 0;
std::atomic<std::uint64_t> g_lost{0};
std::atomic<bool> g_watch{false};

}  // namespace

void decomp_flag_store_add() {
    const char* area = "events";
    decomp_add({"IsEventFlag", area, 0x17cfc00, kIsEntry, sizeof(kIsEntry), reinterpret_cast<void*>(&IsEventFlag),
                DecompKind::Leaf, &g_game_is, reinterpret_cast<void*>(&compare_is), &g_cmp_is});
    decomp_add({"SetEventFlag", area, 0x17cfcc0, kSetEntry, sizeof(kSetEntry), reinterpret_cast<void*>(&SetEventFlag),
                DecompKind::Leaf, &g_game_set, reinterpret_cast<void*>(&compare_set), &g_cmp_set});
    decomp_add({"GetEventFlagValue", area, 0x17cfd80, kGetValueEntry, sizeof(kGetValueEntry),
                reinterpret_cast<void*>(&GetEventFlagValue), DecompKind::Leaf, &g_game_get_value,
                reinterpret_cast<void*>(&compare_get_value), &g_cmp_get_value});
    decomp_add({"SetEventFlagValue", area, 0x17d0060, kSetValueEntry, sizeof(kSetValueEntry),
                reinterpret_cast<void*>(&SetEventFlagValue), DecompKind::Leaf, &g_game_set_value,
                reinterpret_cast<void*>(&compare_set_value), &g_cmp_set_value});
}

void* sprj_event_flag_game_get_value() { return g_game_get_value; }

void event_flag_watch(bool on) { g_watch.store(on, std::memory_order_relaxed); }
bool event_flag_watching() { return g_watch.load(std::memory_order_relaxed); }

void event_flag_changed(std::uint32_t id, bool value) {
    if (!g_watch.load(std::memory_order_relaxed)) return;
    const std::uint32_t pos = g_head.fetch_add(1, std::memory_order_relaxed);
    Slot& s = g_ring[pos % kRing];
    s.seq.store(0, std::memory_order_relaxed);  // being written
    s.id.store(id, std::memory_order_relaxed);
    s.value.store(value ? 1u : 0u, std::memory_order_relaxed);
    s.seq.store(pos + 1, std::memory_order_release);
}

std::size_t event_flag_take_changes(EventFlagChange* out, std::size_t n, std::uint64_t* lost) {
    const std::uint32_t head = g_head.load(std::memory_order_acquire);
    if (head - g_tail > kRing) {  // lapped: the oldest are gone
        g_lost.fetch_add(head - g_tail - kRing, std::memory_order_relaxed);
        g_tail = head - kRing;
    }
    std::size_t got = 0;
    while (g_tail != head && got < n) {
        Slot& s = g_ring[g_tail % kRing];
        const std::uint32_t seq = s.seq.load(std::memory_order_acquire);
        if (seq != g_tail + 1) {
            // Claimed but not written yet: the next call takes it. Written
            // over by a later lap: lost.
            if (seq == 0 || static_cast<std::int32_t>(seq - (g_tail + 1)) < 0) break;
            g_lost.fetch_add(1, std::memory_order_relaxed);
            ++g_tail;
            continue;
        }
        const EventFlagChange c{s.id.load(std::memory_order_relaxed), s.value.load(std::memory_order_relaxed)};
        std::atomic_thread_fence(std::memory_order_acquire);
        if (s.seq.load(std::memory_order_relaxed) != g_tail + 1) {
            g_lost.fetch_add(1, std::memory_order_relaxed);
            ++g_tail;
            continue;
        }
        out[got++] = c;
        ++g_tail;
    }
    if (lost) *lost = g_lost.load(std::memory_order_relaxed);
    return got;
}
