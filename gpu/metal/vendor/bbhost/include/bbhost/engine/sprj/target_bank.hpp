// Native target-character cache and AI update reservation scheduler.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct ChrIns;

// Observed 0x28-byte vector header, including an unknown leading eight bytes.
// Storage holds borrowed ChrIns pointers, with an eight-byte element stride.
// Rebuild resets last to first; remove shifts subsequent elements in place.
struct TargetBankCharacterList {
    Unknown<8> _unk00;
    ChrIns** first;
    ChrIns** last;
    ChrIns** capacity_end;
    void* allocator;

    std::size_t size() const { return first ? static_cast<std::size_t>(last - first) : 0; }
    ChrIns* at(std::size_t i) const { return i < size() ? first[i] : nullptr; }
};

// Native debug identity attached to the manager's +0x30 subobject. Spelling
// preserves Schedular from the binary. Observed inline span 0x6c; no separate
// allocation or reflected class registration is claimed.
//
// Reservations map delay * 30 plus cursor into a 100-slot ring. If the slot is
// full, native code searches forward and extends the delay. The disabled byte
// bypasses that search but still increments the selected slot count. Counts
// remain raw u8 values; native code does not saturate them.
struct SprjAiUpdateTimeSchedular {
    std::uint8_t reservations[100];
    std::int32_t cursor;
    // Constructor default 3; debug menu allows 1..=100.
    std::uint8_t max_reservations_per_slot;
    std::uint8_t disabled;
    Unknown<2> _unk6a;

    static constexpr std::size_t SIZE = 0x6c;
    static constexpr std::size_t SLOT_COUNT = 100;
    static constexpr Rva NAME_STRING{0x494546a};
    static constexpr Rva RESERVE_DELAY_FN{0x12a3be0};
};

// Native singleton allocated as 0xa0 bytes aligned to eight by the world
// creation path. Identity follows singleton assertions and the TargetBankMan
// debug menu. No instance vtable or DLRuntimeClass is established.
//
// Update clears the current reservation slot, advances its cursor modulo 100,
// and adds 1/30 to the refresh timer. Once the interval is reached, it rebuilds
// the borrowed character list from WorldChrMan's global and current-block
// sets, retaining only actors whose virtual +0x78 predicate succeeds.
// CSChrThread can queue this update through a member-callback fragment.
//
// Destruction unregisters debug bindings and frees vector storage without
// destroying the actors. Native removal compacts the list by pointer identity.
struct SprjTargetBankManager {
    TargetBankCharacterList characters;
    // Constructor initializes both timer floats to 5.0.
    float refresh_interval;
    float refresh_elapsed;
    SprjAiUpdateTimeSchedular scheduler;
    // Debug display/input consumers page through ten character IDs at a time.
    std::int32_t debug_page;

    static constexpr std::size_t SIZE = 0xa0;
    static constexpr Rva SINGLETON_PTR = SPRJ_TARGET_BANK_MANAGER_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4932fc3};
    static constexpr Rva CONSTRUCTOR_FN{0x12a3dd0};
    static constexpr Rva DESTRUCTOR_FN{0x12a4240};
    static constexpr Rva UPDATE_FN{0x12a43b0};
    static constexpr Rva REMOVE_CHARACTER_FN{0x12a45e0};
};

namespace detail::target_bank_layout {
BB_SIZE(SprjTargetBankManager, SprjTargetBankManager::SIZE);
static_assert(alignof(SprjTargetBankManager) == 8, "alignof(SprjTargetBankManager)");
BB_OFFSET(SprjTargetBankManager, characters, 0);
BB_OFFSET(SprjTargetBankManager, refresh_interval, 0x28);
BB_OFFSET(SprjTargetBankManager, refresh_elapsed, 0x2c);
BB_OFFSET(SprjTargetBankManager, scheduler, 0x30);
BB_OFFSET(SprjTargetBankManager, debug_page, 0x9c);
BB_SIZE(TargetBankCharacterList, 0x28);
BB_OFFSET(TargetBankCharacterList, first, 0x08);
BB_OFFSET(TargetBankCharacterList, last, 0x10);
BB_OFFSET(TargetBankCharacterList, capacity_end, 0x18);
BB_OFFSET(TargetBankCharacterList, allocator, 0x20);
BB_SIZE(SprjAiUpdateTimeSchedular, SprjAiUpdateTimeSchedular::SIZE);
static_assert(alignof(SprjAiUpdateTimeSchedular) == 4, "alignof(SprjAiUpdateTimeSchedular)");
BB_OFFSET(SprjAiUpdateTimeSchedular, cursor, 0x64);
BB_OFFSET(SprjAiUpdateTimeSchedular, max_reservations_per_slot, 0x68);
BB_OFFSET(SprjAiUpdateTimeSchedular, disabled, 0x69);
}  // namespace detail::target_bank_layout

}  // namespace bb
