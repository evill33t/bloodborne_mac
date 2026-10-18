// SprjWorldObjActMan: per-block ObjAct entries and the packet 0x2e replication payload.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_SIZE = 0x4010;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_AREA_COUNT_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_AREA_ENTRIES_OFFSET = 0x18;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_BLOCK_COUNT_OFFSET = 0x20;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_BLOCK_ENTRIES_OFFSET = 0x28;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_INLINE_AREA_ENTRIES_OFFSET = 0x30;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_INLINE_BLOCK_ENTRIES_OFFSET = 0xcb0;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_MAN_DEBUG_MENU_NODE_OFFSET = 0x3ff8;

inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_SIZE = 0xa0;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_CAPACITY = 0x14;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_SIZE = 0xd8;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_CAPACITY = 0x3c;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_ENTRIES_OFFSET = 0x90;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_ENTRY_COUNT_OFFSET = 0x98;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_TARGET_NETWORK_STATE_OFFSET = 0xc0;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_FLAG_C4_OFFSET = 0xc4;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_FLAG_C5_OFFSET = 0xc5;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_SELECTED_ENTRY_OFFSET = 0xc8;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_VALUE_CC_OFFSET = 0xcc;
inline constexpr std::size_t SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_VALUE_D0_OFFSET = 0xd0;

inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_SIZE = 0x140;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_LIST_ID_OFFSET = 0x08;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_KICK_EVENT_ID_OFFSET = 0x0c;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_PENDING_LIST_OFFSET = 0x20;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_PENDING_COUNT_OFFSET = 0x28;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_PENDING_ALLOCATOR_OFFSET = 0x30;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_RESET_COUNTER_OFFSET = 0x50;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_OBJECT_HANDLE_OFFSET = 0x58;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_NETWORK_STATE_OFFSET = 0x5c;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_STATE_GENERATION_OFFSET = 0x68;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_KICK_FLAG_OFFSET = 0x78;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_CHANGE_FLAG_OFFSET = 0x79;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_SEND_NET_FLAG_OFFSET = 0x7a;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_SECONDARY_ID_OFFSET = 0x80;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_BACKING_OBJECT_OFFSET = 0x88;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_ACTION_OBJECT_OFFSET = 0x90;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_SEND_PENDING_LATCH_OFFSET = 0x99;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_RELATION_OBJECT_OFFSET = 0xa8;
inline constexpr std::size_t SPRJ_OBJ_ACT_ENTRY_FILTER_STATE_OFFSET = 0xb4;

inline constexpr std::uint32_t SPRJ_OBJ_ACT_REPLICATION_PACKET_TYPE = 0x2e;
inline constexpr std::size_t SPRJ_OBJ_ACT_REPLICATION_PAYLOAD_SIZE = 0x06;
inline constexpr std::uint8_t SPRJ_OBJ_ACT_REPLICATION_STATE = 4;

enum class SprjObjActNetworkState : std::int32_t {
    Idle = 0,
    ForceReplicate = 4,
};

struct SprjObjActPendingList {
    void* sentinel;
    std::size_t count;
    void* allocator;
};

// Payload sent as WorldSessionObjectMan packet type 0x2e. RVA 0x13afc30, RVA
// 0x13b03f0, and RVA 0x13b0bb0 build this on the stack, then pass it to RVA
// 0x17894e0 with length 6.
struct SprjObjActReplicationPayload {
    std::uint8_t block_index_packed;
    std::uint8_t _reserved01;
    std::int16_t entry_index;
    std::uint8_t network_state;
    std::uint8_t _reserved05;

    std::size_t block_index() const { return block_index_packed & 0x3f; }
    bool has_entry_index() const { return entry_index >= 0; }
    bool is_force_replicate() const { return network_state == SPRJ_OBJ_ACT_REPLICATION_STATE; }
};

// One 0x140 ObjAct entry owned by a SprjWorldObjActBlock.
struct SprjObjActEntry {
    std::int32_t block_index;
    Unknown<0x04> _unk04;
    std::uint32_t list_id;
    std::int32_t kick_event_id;
    std::uint8_t pending_list_dirty;
    Unknown<0x0f> _unk11;
    SprjObjActPendingList pending_list;
    Unknown<0x18> _unk38;
    std::uint32_t reset_counter;
    Unknown<0x04> _unk54;
    std::uint32_t object_handle;
    std::int32_t network_state_raw;  // SprjObjActNetworkState
    Unknown<0x08> _unk60;
    std::uint32_t state_generation;
    Unknown<0x0c> _unk6c;
    std::uint8_t kick_requested;
    std::uint8_t change_requested;
    std::uint8_t send_network_update;
    Unknown<0x05> _unk7b;
    std::int32_t secondary_id;
    Unknown<0x04> _unk84;
    void* backing_object;
    void* action_object;
    std::uint8_t _unk98;
    std::uint8_t send_pending_latch;
    Unknown<0x0e> _unk9a;
    void* relation_object;
    Unknown<0x04> _unkb0;
    std::int32_t filter_state;
    Unknown<0x88> _unkb8;

    SprjObjActNetworkState network_state() const { return static_cast<SprjObjActNetworkState>(network_state_raw); }
    bool wants_network_send() const { return send_network_update != 0 || send_pending_latch != 0; }
    bool is_force_replicate_pending() const {
        return network_state_raw == static_cast<std::int32_t>(SprjObjActNetworkState::ForceReplicate);
    }
};

// 0xd8 row indexed from SprjWorldObjActMan + 0x28.
struct SprjWorldObjActBlock {
    void* source_descriptor;
    Unknown<SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_SIZE>* area_entry;
    std::uint8_t loaded;
    Unknown<0x07> _unk11;
    Unknown<0x78> _debug_node_list;
    SprjObjActEntry* entries;
    std::uint32_t entry_count;
    Unknown<0x04> _unk9c;
    std::uint64_t selected_entry_key;
    std::uint32_t debug_entry_count;
    Unknown<0x04> _unkac;
    void* debug_node_b0;
    void* debug_node_b8;
    std::uint32_t target_network_state;
    std::uint8_t debug_flag_c4;
    std::uint8_t debug_flag_c5;
    Unknown<0x02> _unkc6;
    std::uint32_t debug_selected_entry;
    std::uint32_t debug_value_cc;
    std::uint32_t debug_value_d0;
    Unknown<0x04> _unkd4;

    std::size_t entries_count() const { return entries ? entry_count : 0; }
    // The entry a packet's signed index names, or null when out of range.
    SprjObjActEntry* entry_from_packet_index(std::int16_t index) const {
        if (index < 0 || static_cast<std::uint32_t>(index) >= entry_count || !entries) return nullptr;
        return &entries[index];
    }
};

// Singleton allocated at RVA 0x553b0f0 by RVA 0x154b7e0, which allocates
// 0x4010 bytes and constructs this through RVA 0x13b1970. Inline area rows at
// +0x30 and inline block rows at +0xcb0; only the block rows are currently
// tied to the verified ObjAct packet-send path.
struct SprjWorldObjActMan {
    void* vftable;
    void* world_res;
    std::int32_t area_count;
    Unknown<0x04> _pad14;
    Unknown<SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_SIZE>* area_entries;
    std::int32_t block_count;
    Unknown<0x04> _pad24;
    SprjWorldObjActBlock* block_entries;
    Unknown<SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_SIZE * SPRJ_WORLD_OBJ_ACT_AREA_ENTRY_CAPACITY> _inline_area_entries;
    Unknown<SPRJ_WORLD_OBJ_ACT_BLOCK_SIZE * SPRJ_WORLD_OBJ_ACT_BLOCK_CAPACITY> _inline_block_entries;
    Unknown<0x78> _debug_menu_list;
    std::uint64_t _debug_value_3fc8;
    Unknown<0x10> _debug_range_3fd0;
    Unknown<0x10> _debug_range_3fe0;
    std::uint8_t debug_toggle_3ff0;
    Unknown<0x07> _pad3ff1;
    void* debug_menu_node;
    std::uint8_t debug_toggle_4000;
    std::uint8_t debug_byte_4001;
    Unknown<0x02> _pad4002;
    std::int32_t debug_value_4004;
    std::int32_t debug_value_4008;
    std::uint8_t debug_toggle_400c;
    Unknown<0x03> _pad400d;

    std::size_t blocks_count() const {
        return (block_entries && block_count > 0) ? static_cast<std::size_t>(block_count) : 0;
    }
    SprjWorldObjActBlock* block_at(std::size_t i) const { return i < blocks_count() ? &block_entries[i] : nullptr; }
    // The block a replication payload names, or null when out of range.
    SprjWorldObjActBlock* block_from_packet(const SprjObjActReplicationPayload& payload) const {
        return block_at(payload.block_index());
    }
};

namespace detail::world_obj_act_layout {
BB_SIZE(SprjWorldObjActMan, SPRJ_WORLD_OBJ_ACT_MAN_SIZE);
BB_OFFSET(SprjWorldObjActMan, area_count, SPRJ_WORLD_OBJ_ACT_MAN_AREA_COUNT_OFFSET);
BB_OFFSET(SprjWorldObjActMan, area_entries, SPRJ_WORLD_OBJ_ACT_MAN_AREA_ENTRIES_OFFSET);
BB_OFFSET(SprjWorldObjActMan, block_count, SPRJ_WORLD_OBJ_ACT_MAN_BLOCK_COUNT_OFFSET);
BB_OFFSET(SprjWorldObjActMan, block_entries, SPRJ_WORLD_OBJ_ACT_MAN_BLOCK_ENTRIES_OFFSET);
BB_OFFSET(SprjWorldObjActMan, _inline_area_entries, SPRJ_WORLD_OBJ_ACT_MAN_INLINE_AREA_ENTRIES_OFFSET);
BB_OFFSET(SprjWorldObjActMan, _inline_block_entries, SPRJ_WORLD_OBJ_ACT_MAN_INLINE_BLOCK_ENTRIES_OFFSET);
BB_OFFSET(SprjWorldObjActMan, debug_menu_node, SPRJ_WORLD_OBJ_ACT_MAN_DEBUG_MENU_NODE_OFFSET);
BB_SIZE(SprjWorldObjActBlock, SPRJ_WORLD_OBJ_ACT_BLOCK_SIZE);
BB_OFFSET(SprjWorldObjActBlock, entries, SPRJ_WORLD_OBJ_ACT_BLOCK_ENTRIES_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, entry_count, SPRJ_WORLD_OBJ_ACT_BLOCK_ENTRY_COUNT_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, target_network_state, SPRJ_WORLD_OBJ_ACT_BLOCK_TARGET_NETWORK_STATE_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, debug_flag_c4, SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_FLAG_C4_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, debug_flag_c5, SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_FLAG_C5_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, debug_selected_entry, SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_SELECTED_ENTRY_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, debug_value_cc, SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_VALUE_CC_OFFSET);
BB_OFFSET(SprjWorldObjActBlock, debug_value_d0, SPRJ_WORLD_OBJ_ACT_BLOCK_DEBUG_VALUE_D0_OFFSET);
BB_SIZE(SprjObjActEntry, SPRJ_OBJ_ACT_ENTRY_SIZE);
BB_OFFSET(SprjObjActEntry, list_id, SPRJ_OBJ_ACT_ENTRY_LIST_ID_OFFSET);
BB_OFFSET(SprjObjActEntry, kick_event_id, SPRJ_OBJ_ACT_ENTRY_KICK_EVENT_ID_OFFSET);
BB_OFFSET(SprjObjActEntry, pending_list, SPRJ_OBJ_ACT_ENTRY_PENDING_LIST_OFFSET);
static_assert(offsetof(SprjObjActPendingList, count) + offsetof(SprjObjActEntry, pending_list) ==
                  SPRJ_OBJ_ACT_ENTRY_PENDING_COUNT_OFFSET,
              "SprjObjActEntry pending count");
static_assert(offsetof(SprjObjActPendingList, allocator) + offsetof(SprjObjActEntry, pending_list) ==
                  SPRJ_OBJ_ACT_ENTRY_PENDING_ALLOCATOR_OFFSET,
              "SprjObjActEntry pending allocator");
BB_OFFSET(SprjObjActEntry, reset_counter, SPRJ_OBJ_ACT_ENTRY_RESET_COUNTER_OFFSET);
BB_OFFSET(SprjObjActEntry, object_handle, SPRJ_OBJ_ACT_ENTRY_OBJECT_HANDLE_OFFSET);
BB_OFFSET(SprjObjActEntry, network_state_raw, SPRJ_OBJ_ACT_ENTRY_NETWORK_STATE_OFFSET);
BB_OFFSET(SprjObjActEntry, state_generation, SPRJ_OBJ_ACT_ENTRY_STATE_GENERATION_OFFSET);
BB_OFFSET(SprjObjActEntry, kick_requested, SPRJ_OBJ_ACT_ENTRY_KICK_FLAG_OFFSET);
BB_OFFSET(SprjObjActEntry, change_requested, SPRJ_OBJ_ACT_ENTRY_CHANGE_FLAG_OFFSET);
BB_OFFSET(SprjObjActEntry, send_network_update, SPRJ_OBJ_ACT_ENTRY_SEND_NET_FLAG_OFFSET);
BB_OFFSET(SprjObjActEntry, secondary_id, SPRJ_OBJ_ACT_ENTRY_SECONDARY_ID_OFFSET);
BB_OFFSET(SprjObjActEntry, backing_object, SPRJ_OBJ_ACT_ENTRY_BACKING_OBJECT_OFFSET);
BB_OFFSET(SprjObjActEntry, action_object, SPRJ_OBJ_ACT_ENTRY_ACTION_OBJECT_OFFSET);
BB_OFFSET(SprjObjActEntry, send_pending_latch, SPRJ_OBJ_ACT_ENTRY_SEND_PENDING_LATCH_OFFSET);
BB_OFFSET(SprjObjActEntry, relation_object, SPRJ_OBJ_ACT_ENTRY_RELATION_OBJECT_OFFSET);
BB_OFFSET(SprjObjActEntry, filter_state, SPRJ_OBJ_ACT_ENTRY_FILTER_STATE_OFFSET);
BB_SIZE(SprjObjActReplicationPayload, SPRJ_OBJ_ACT_REPLICATION_PAYLOAD_SIZE);
BB_OFFSET(SprjObjActReplicationPayload, block_index_packed, 0);
BB_OFFSET(SprjObjActReplicationPayload, entry_index, 2);
BB_OFFSET(SprjObjActReplicationPayload, network_state, 4);
static_assert(static_cast<std::int32_t>(SprjObjActNetworkState::ForceReplicate) == SPRJ_OBJ_ACT_REPLICATION_STATE,
              "replication state");
}  // namespace detail::world_obj_act_layout

}  // namespace bb
