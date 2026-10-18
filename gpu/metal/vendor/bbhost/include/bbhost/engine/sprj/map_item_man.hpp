// MapItemMan native class slice: layout and addresses only (no item
// eligibility, replication or award policy).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva MAP_ITEM_MAN_SINGLETON{0x553d6e0};
inline constexpr std::size_t MAP_ITEM_MAN_MINIMUM_INITIALIZED_SIZE = 0xd1;

inline constexpr Rva MAP_ITEM_MAN_INITIALIZE_FN{0x17d2960};
inline constexpr Rva MAP_ITEM_MAN_PROCESS_NETWORK_PACKETS_FN{0x17d2f30};
inline constexpr Rva MAP_ITEM_MAN_INSERT_TREASURE_ITEM_FN{0x17d4fb0};
inline constexpr Rva MAP_ITEM_MAN_INSERT_CORPSE_ITEM_FN{0x17d5320};
inline constexpr Rva MAP_ITEM_MAN_CLEAR_REPLICATED_TREASURE_ITEMS_FN{0x17d5b40};
inline constexpr Rva MAP_ITEM_MAN_CLEAR_REPLICATED_CORPSE_ITEMS_FN{0x17d5c40};
inline constexpr Rva MAP_ITEM_MAN_REMOVE_ENTRY_LOCAL_FN{0x17d6780};
inline constexpr Rva MAP_ITEM_MAN_AWARD_ITEM_BATCH_FN{0x17d89f0};
inline constexpr Rva MAP_ITEM_MAN_FIND_TREASURE_ITEM_CANDIDATE_FN{0x17da500};
inline constexpr Rva MAP_ITEM_MAN_GENERATE_TREASURE_ITEMS_FOR_MAP_OBJECT_FN{0x17dc300};
inline constexpr Rva MAP_ITEM_MAN_GENERATE_CORPSE_ITEMS_FOR_CHARACTER_FN{0x17dd0b0};
inline constexpr Rva MAP_ITEM_MAN_AWARD_ITEM_LOT_WITH_CLIENT_POLICY_FN{0x17ddc50};

struct MapItemManListStorage {
    void* begin;
    void* end;
    void* capacity;
};

struct MapItemMan {
    void* vtable;
    std::uint64_t unknown_08;
    std::uint32_t unknown_10;
    Unknown<0x3c> unknown_14;
    MapItemManListStorage entry_list_50;
    void* allocator_68;
    Unknown<0x08> unknown_70;
    MapItemManListStorage entry_list_78;
    void* allocator_90;
    std::uint8_t flag_98;
    Unknown<0x37> unknown_99;
    std::uint8_t flag_d0;
};

// Ghidra recovered this native signature. Other MapItemMan methods remain
// address-only until their arguments are independently proven.
using AwardItemLotWithClientPolicy = void (BB_GAME_ABI*)(MapItemMan* map_item_man, std::int32_t item_lot_id,
                                                          std::uint8_t host_only);

namespace detail::map_item_man_layout {
BB_SIZE(MapItemManListStorage, 0x18);
BB_OFFSET(MapItemMan, vtable, 0x00);
BB_OFFSET(MapItemMan, unknown_08, 0x08);
BB_OFFSET(MapItemMan, unknown_10, 0x10);
BB_OFFSET(MapItemMan, entry_list_50, 0x50);
BB_OFFSET(MapItemMan, allocator_68, 0x68);
BB_OFFSET(MapItemMan, entry_list_78, 0x78);
BB_OFFSET(MapItemMan, allocator_90, 0x90);
BB_OFFSET(MapItemMan, flag_98, 0x98);
BB_OFFSET(MapItemMan, flag_d0, 0xd0);
static_assert(sizeof(MapItemMan) >= MAP_ITEM_MAN_MINIMUM_INITIALIZED_SIZE, "sizeof(MapItemMan)");
}  // namespace detail::map_item_man_layout

}  // namespace bb
