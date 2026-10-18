// CSNowLoadingHelper: loading requests, frame latching, and the shuffled item
// descriptions. CSNowLoadingHelper is the asserted singleton;
// CSNowLoadingRandXorshift is a descriptive name for its allocated generator
// (separate RTTI is unproven).
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CS_NOW_LOADING_HELPER_SIZE = 0x58;
inline constexpr std::size_t CS_NOW_LOADING_RAND_XORSHIFT_SIZE = 0x18;
inline constexpr std::size_t CS_NOW_LOADING_ITEM_TABLE_COUNT = 80;
// Historical name: this table holds item IDs, not MENU_LOAD row IDs.
inline constexpr std::size_t CS_NOW_LOADING_HELPER_MENU_LOAD_TABLE_COUNT = CS_NOW_LOADING_ITEM_TABLE_COUNT;

inline constexpr std::size_t CS_NOW_LOADING_HELPER_RAND_XORSHIFT_OFFSET = 0x08;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_UPDATE_TASK_OFFSET = 0x10;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_MENU_LOAD_ENTRIES_OFFSET = 0x40;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_MENU_LOAD_ENTRY_COUNT_OFFSET = 0x48;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_MENU_LOAD_COUNTER_OFFSET = 0x4c;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_IS_LOADING_OFFSET = 0x50;
inline constexpr std::size_t CS_NOW_LOADING_HELPER_PREVIOUS_LOADING_OFFSET = 0x51;

// Initial item IDs written to RVA 0x5578170 by 80 consecutive MOV instructions
// at RVAs 0x19c8740..0x19c8a60 (exclusive). The constructor shuffles this
// shared storage in place, so its live order differs. Integer IDs, not RVAs.
inline constexpr std::int32_t CS_NOW_LOADING_INITIAL_ITEM_IDS[CS_NOW_LOADING_ITEM_TABLE_COUNT] = {
    0x40000064, 0x4000006f, 0x40000078, 0x400000c8, 0x400000cd, 0x400000e1, 0x400002be, 0x400002bf,
    0x40000384, 0x400003e8, 0x4000044c, 0x4000044d, 0x40000456, 0x40000460, 0x400004b0, 0x400004ba,
    0x400004c4, 0x400004ce, 0x400004d8, 0x400004ec, 0x40000514, 0x4000051e, 0x40000532, 0x40000582,
    0x40000583, 0x40000584, 0x400005dc, 0x400005e6, 0x400007e4, 0x400007ee, 0x40000820, 0x40000bb8,
    0x40000bcc, 0x40000fab, 0x40000fad, 0x40001006, 0x40001007, 0x40001008, 0x40001009, 0x4000100e,
    0x40001010, 0x40001012, 0x400010cc, 0x40001327, 0x40001329, 0x400017d4, 0x400017dd, 0x4000184b,
    0x40001b58, 0x40001b64, 0x40001b6c, 0x40001b8a, 0x40001bd0, 0x40001bda, 0x40001be4, 0x40001bee,
    0x003d0900, 0x004c4b40, 0x005b8d80, 0x006acfc0, 0x007a1200, 0x007b98a0, 0x00989680, 0x00d59f80,
    0x0112a880, 0x0121eac0, 0x0132b3a0, 0x014fb180, 0x10002af8, 0x1000c738, 0x1000ee48, 0x10011170,
    0x10013880, 0x1001b198, 0x1001b580, 0x1002c308, 0x10038a40, 0x100445c0, 0x1004bed8, 0x1004e5e8,
};

// Four-word xorshift128 state allocated with size 0x18 and alignment eight.
// The 32-bit generator rotates x/y/z/w and returns the new w. The 64-bit
// generator advances twice, placing the first output in the high word.
struct CSNowLoadingRandXorshift {
    const void* vftable;
    std::uint32_t state[4];

    static constexpr std::size_t SIZE = CS_NOW_LOADING_RAND_XORSHIFT_SIZE;
    static constexpr Rva VTABLE{0x5321db0};
    static constexpr Rva BASE_VTABLE{0x5321d80};
    static constexpr Rva DESTRUCTOR_FN{0x1460da0};
    static constexpr Rva NEXT_U32_FN{0x1460d10};
    static constexpr Rva NEXT_U64_FN{0x1460d40};
    // Integer LCG values read from RVAs 0x486d380/0x486d388 during seeding.
    static constexpr std::uint64_t SEED_MULTIPLIER = 0x5deece66d;
    static constexpr std::uint64_t SEED_INCREMENT = 11;

    // Pure constructor seeding from a caller-supplied word. Native construction
    // passes the low 32 bits of seconds*1_000_000 + microseconds. Four wrapping
    // 64-bit LCG steps contribute their upper halves; there is no 48-bit mask.
    static constexpr void state_from_seed(std::uint32_t seed, std::uint32_t out[4]) {
        std::uint64_t value = seed;
        for (int i = 0; i < 4; ++i) {
            value = value * SEED_MULTIPLIER + SEED_INCREMENT;
            out[i] = static_cast<std::uint32_t>(value >> 32);
        }
    }

    // Advances only caller-owned state, without calling native code or reading
    // the helper singleton. Zero state remains zero, as in the native routine.
    static constexpr std::uint32_t next_u32_from_state(std::uint32_t s[4]) {
        std::uint32_t x = s[0];
        std::uint32_t w = s[3];
        std::uint32_t t = x ^ (x << 11);
        std::uint32_t next = w ^ (w >> 19) ^ t ^ (t >> 8);
        s[0] = s[1];
        s[1] = s[2];
        s[2] = w;
        s[3] = next;
        return next;
    }

    // Advances caller-owned state twice, preserving the native word ordering.
    static constexpr std::uint64_t next_u64_from_state(std::uint32_t s[4]) {
        std::uint64_t high = next_u32_from_state(s);
        std::uint64_t low = next_u32_from_state(s);
        return (high << 32) | low;
    }
};

// Exact 0x58-byte owner. Construction shuffles the shared 80-item table once,
// then registers update_task in FrameBegin. Native destruction unregisters the
// task and destroys/frees the generator; the shared table is not freed.
struct CSNowLoadingHelper {
    void* vftable;
    CSNowLoadingRandXorshift* rand_xorshift;
    SprjCallbackTask update_task;
    // Historical name for the borrowed, globally shared item-ID table.
    std::int32_t* menu_load_entries;
    std::uint32_t menu_load_entry_count;
    // Index of the next item; native selection increments modulo the count.
    std::uint32_t menu_load_counter;
    // Request accumulated during this frame. FrameBegin copies this byte to
    // previous_loading and clears it. It is not the consumer's latched value.
    std::uint8_t is_loading;
    // Previous frame's request, read by native loading/work consumers.
    std::uint8_t previous_loading;
    Unknown<0x06> _pad52;

    static constexpr std::size_t SIZE = CS_NOW_LOADING_HELPER_SIZE;
    static constexpr Rva SINGLETON_PTR = CS_NOW_LOADING_HELPER_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4935b4b};
    static constexpr Rva VTABLE{0x53333e0};
    static constexpr Rva TASK_VTABLE{0x5395b70};
    static constexpr Rva CONSTRUCTOR_FN{0x19c8280};
    static constexpr Rva DESTRUCTOR_FN{0x19c84f0};
    static constexpr Rva FRAME_BEGIN_FN{0x19c8450};
    static constexpr Rva REQUEST_LOADING_FN{0x19c85a0};
    // Native output-parameter getter: table[counter], followed by
    // counter = (counter + 1) % count. It does not reshuffle or validate bounds.
    static constexpr Rva NEXT_ITEM_FN{0x19c8580};
    static constexpr Rva SHARED_ITEM_TABLE{0x5578170};
    static constexpr SprjTaskGroupIndex UPDATE_TASK_GROUP = SprjTaskGroupIndex::FrameBegin;

    // Not named is_loading(): C++ cannot share the field's name. Current
    // frame's request byte (+0x50).
    bool loading_requested() const { return is_loading != 0; }
    // Latched previous-frame value (+0x51) used by native work consumers.
    bool loading_last_frame() const { return previous_loading != 0; }
    // The menu's load entries: the shared item-ID table as
    // pointer + count, empty when the pointer is null. Not synchronized
    // against other native readers/writers of the global table.
    std::int32_t* entries() const { return menu_load_entries; }
    std::size_t entry_count() const { return menu_load_entries ? menu_load_entry_count : 0; }
};

namespace detail::now_loading_layout {
using T = CSNowLoadingHelper;
BB_SIZE(T, CS_NOW_LOADING_HELPER_SIZE);
static_assert(alignof(T) == 8, "alignof(CSNowLoadingHelper)");
BB_OFFSET(T, rand_xorshift, CS_NOW_LOADING_HELPER_RAND_XORSHIFT_OFFSET);
BB_OFFSET(T, update_task, CS_NOW_LOADING_HELPER_UPDATE_TASK_OFFSET);
BB_OFFSET(T, menu_load_entries, CS_NOW_LOADING_HELPER_MENU_LOAD_ENTRIES_OFFSET);
BB_OFFSET(T, menu_load_entry_count, CS_NOW_LOADING_HELPER_MENU_LOAD_ENTRY_COUNT_OFFSET);
BB_OFFSET(T, menu_load_counter, CS_NOW_LOADING_HELPER_MENU_LOAD_COUNTER_OFFSET);
BB_OFFSET(T, is_loading, CS_NOW_LOADING_HELPER_IS_LOADING_OFFSET);
BB_OFFSET(T, previous_loading, CS_NOW_LOADING_HELPER_PREVIOUS_LOADING_OFFSET);
BB_OFFSET(T, _pad52, 0x52);
BB_OFFSET(T, update_task.registration, 0x20);
BB_OFFSET(T, update_task.owner, 0x28);
BB_OFFSET(T, update_task.callback, 0x30);
BB_OFFSET(T, update_task.this_adjustment, 0x38);
BB_SIZE(CSNowLoadingRandXorshift, CS_NOW_LOADING_RAND_XORSHIFT_SIZE);
static_assert(alignof(CSNowLoadingRandXorshift) == 8, "alignof(CSNowLoadingRandXorshift)");
BB_OFFSET(CSNowLoadingRandXorshift, state, 8);
static_assert(static_cast<std::uint32_t>(T::UPDATE_TASK_GROUP) == 0, "CSNowLoadingHelper::UPDATE_TASK_GROUP");
// FNV-1a of the little-endian immediates of all 80 native MOV instructions:
// checks the captured order and every item ID.
constexpr std::uint64_t item_table_fnv1a() {
    std::uint64_t hash = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < CS_NOW_LOADING_ITEM_TABLE_COUNT; ++i) {
        std::uint32_t id = static_cast<std::uint32_t>(CS_NOW_LOADING_INITIAL_ITEM_IDS[i]);
        for (int b = 0; b < 4; ++b) hash = (hash ^ ((id >> (8 * b)) & 0xff)) * 0x100000001b3ull;
    }
    return hash;
}
static_assert(item_table_fnv1a() == 0xc3e25054fdbabe64ull, "CS_NOW_LOADING_INITIAL_ITEM_IDS checksum");
constexpr bool seed_one_matches() {
    std::uint32_t s[4] = {};
    CSNowLoadingRandXorshift::state_from_seed(1, s);
    return s[0] == 0x00000005 && s[1] == 0x7760bb61 && s[2] == 0x41e8df41 && s[3] == 0xdc806023;
}
static_assert(seed_one_matches(), "CSNowLoadingRandXorshift::state_from_seed");
static_assert(CS_NOW_LOADING_INITIAL_ITEM_IDS[0] == 0x40000064, "item table");
static_assert(CS_NOW_LOADING_INITIAL_ITEM_IDS[79] == 0x1004e5e8, "item table");
}  // namespace detail::now_loading_layout

}  // namespace bb
