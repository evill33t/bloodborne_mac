// Lamp (bonfire) DB owned by FrpgNetMan+0xc70: rows, list nodes and the functions that touch them.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// These are
// layout/address declarations, not callable signatures. Several apparent
// queries reorder rows, allocate, or consume notifications; they must not be
// used as read-only inspector getters.
//
// The module-level constants (SIZE, CONSTRUCTOR, ...) are kept in
// bb::bonfire_db; the structs are in bb.
namespace bonfire_db {

inline constexpr std::size_t OWNER_OFFSET = 0xc70;
inline constexpr std::size_t SIZE = 0x238;
inline constexpr std::size_t ITEM_SIZE = 0x58;
inline constexpr Rva CONSTRUCTOR{0x146b7c0};
inline constexpr Rva CREATE_ITEM{0x146c3a0};
inline constexpr Rva ACTIVATE_LAMP{0x146c5a0};
inline constexpr Rva REGISTER_EVENT_FLAGS{0x146da20};
inline constexpr Rva ADVERTISE_WARP_POINTS{0x146be20};
inline constexpr Rva CLEAR_ROWS{0x146d2b0};
inline constexpr Rva SERIALIZE_ROWS{0x146d3e0};
inline constexpr Rva DESERIALIZE_ROWS{0x146d5f0};
// Copies row status and clears pending notification bytes +0x1c..+0x1f.
// Also allocates a result vector and can reorder the native row list.
inline constexpr Rva CONSUME_ITEM_STATUS{0x146dc40};
// Creates a row if absent and can reorder the list: not a read-only getter.
inline constexpr Rva GET_OR_CREATE_SPOT_POINTS{0x146e140};
// Returns cached +0x48, but can reorder the native list.
inline constexpr Rva FIND_CACHED_VALID{0x146e6e0};
inline constexpr Rva ITEM_CONSTRUCTOR{0x1471a20};
inline constexpr Rva ITEM_IS_LIT{0x1471cf0};
inline constexpr Rva ITEM_SET_LIT{0x1471d50};
// Reads base+11, with Game.Autotest.AutoExecChair as an override.
inline constexpr Rva ITEM_IS_SELECTED{0x1471dc0};
inline constexpr Rva ITEM_SET_SELECTED{0x1471e40};
// Writes a ten-bit range. This is not the cached debug LV byte at +0x49.
inline constexpr Rva ITEM_SET_LEVEL_BITS{0x1471ed0};
inline constexpr Rva ITEM_DEBUG_FORMAT{0x1472620};
inline constexpr Rva DEBUG_CAP_INPUT{0x146ed70};
inline constexpr Rva CLAMP_ALL_SPOT_POINTS{0x146e060};
inline constexpr Rva MAX_BASE_SPOT_POINTS{0x5127aa4};
inline constexpr Rva MAX_ADDITIONAL_SPOT_POINTS{0x5127aa8};
inline constexpr Rva MAX_TOTAL_SPOT_POINTS{0x5127aac};

inline constexpr std::uint32_t LEVEL_BIT_COUNT = 10;
inline constexpr std::uint32_t LIT_FLAG_DELTA = 10;
inline constexpr std::uint32_t SELECTED_FLAG_DELTA = 11;
inline constexpr std::int32_t INVALID_FLAG_BASE = -1;

// Gameplay callbacks that connect lamp activation to the return-point write.
// The immediate callback excludes native role 6 from its anchor write; its
// subsequent DB activation is not excluded by that same role check. It also
// requires GameStateMan+0x1592 != 0 and nested session state +0x98 == -1. The
// first condition conflicts with a guest that keeps that byte zero to apply a
// host's snapshot (game_state_man.hpp);
// native activation and return-point registration must not be conflated.
inline constexpr Rva LUA_EVENT_ACTIVATE_LAMP{0x131e4a0};
inline constexpr Rva LAMP_ROLE6_RETURN_POINT_BRANCH{0x131e50a};
inline constexpr Rva LAMP_WORLD_SELECTOR_RETURN_POINT_BRANCH{0x131e525};
inline constexpr Rva LUA_EVENT_UPDATE_DELAYED_LAMP{0x1312ff0};
inline constexpr std::size_t GAME_STATE_PACKED_RETURN_POINT_OFFSET = 0x1528;
inline constexpr std::int32_t LAMP_TO_RETURN_POINT_ENTITY_DELTA = 1000;

}  // namespace bonfire_db

struct FrpgNetBonfireDbItem;

// List node owns a pointer to a separately allocated 0x58-byte DB item.
struct BonfireDbNode {
    BonfireDbNode* next;
    BonfireDbNode* previous;
    FrpgNetBonfireDbItem* item;
};

// Layout name for the owner at FrpgNetMan+0xc70, not a new RTTI assertion.
// Job internals remain opaque; do not clone this owning allocation.
struct BonfireDb {
    std::uint8_t request_manager[0x28];
    void* list_header;
    BonfireDbNode* sentinel;
    std::size_t row_count;
    void* row_allocator;
    std::uint8_t jobs[0x1e0];
    std::uint8_t flags_228;
    std::uint8_t unknown_229[7];
    void* debug_node;
};

// Native name appears in FD4 registration/destruction. All offsets are from
// the row constructor, debug formatter, serializer and activation consumer.
struct FrpgNetBonfireDbItem {
    void* vtable;                             // +0x00
    std::int32_t entity_id;                   // +0x08
    std::int16_t base_spot_points;            // +0x0c
    std::int16_t additional_spot_points;      // +0x0e
    std::uint8_t parent;                      // +0x10
    std::uint8_t registered;                  // +0x11
    std::uint8_t unknown_012[2];
    std::int32_t event_flag_base;             // +0x14
    std::int32_t scale_raw;                   // +0x18
    std::uint8_t first_activation_pending;    // +0x1c
    std::uint8_t repeat_activation_pending;   // +0x1d
    std::uint8_t notification_1e;
    std::uint8_t notification_1f;
    void* entries_header;                     // +0x20
    void* entries_begin;                      // +0x28, stride 0x20
    void* entries_end;                        // +0x30
    void* entries_capacity;                   // +0x38
    void* entries_allocator;                  // +0x40
    // Serialized/debug value. Do not substitute for the live base+10 flag.
    std::uint8_t cached_valid;                // +0x48
    std::int8_t cached_level;                 // +0x49, formatter sign-extends
    std::uint8_t unknown_04a[6];
    void* debug_node;                         // +0x50
};

namespace detail::bonfire_db_layout {
BB_SIZE(BonfireDb, bonfire_db::SIZE);
BB_SIZE(FrpgNetBonfireDbItem, bonfire_db::ITEM_SIZE);
BB_OFFSET(FrpgNetBonfireDbItem, entity_id, 0x08);
BB_OFFSET(FrpgNetBonfireDbItem, base_spot_points, 0x0c);
BB_OFFSET(FrpgNetBonfireDbItem, additional_spot_points, 0x0e);
BB_OFFSET(FrpgNetBonfireDbItem, parent, 0x10);
BB_OFFSET(FrpgNetBonfireDbItem, registered, 0x11);
BB_OFFSET(FrpgNetBonfireDbItem, event_flag_base, 0x14);
BB_OFFSET(FrpgNetBonfireDbItem, scale_raw, 0x18);
BB_OFFSET(FrpgNetBonfireDbItem, first_activation_pending, 0x1c);
BB_OFFSET(FrpgNetBonfireDbItem, repeat_activation_pending, 0x1d);
BB_OFFSET(FrpgNetBonfireDbItem, entries_header, 0x20);
BB_OFFSET(FrpgNetBonfireDbItem, entries_begin, 0x28);
BB_OFFSET(FrpgNetBonfireDbItem, entries_end, 0x30);
BB_OFFSET(FrpgNetBonfireDbItem, entries_capacity, 0x38);
BB_OFFSET(FrpgNetBonfireDbItem, entries_allocator, 0x40);
BB_OFFSET(FrpgNetBonfireDbItem, cached_valid, 0x48);
BB_OFFSET(FrpgNetBonfireDbItem, cached_level, 0x49);
BB_OFFSET(FrpgNetBonfireDbItem, debug_node, 0x50);
}  // namespace detail::bonfire_db_layout

}  // namespace bb
