// GameStateMan known prefix (Bloodborne 1.09): a layout and symbol model only.
// Unknown ranges stay opaque until a direct native read/write proof exists.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva GAME_STATE_MAN_SINGLETON{0x5556678};
// Unconditional packed write; not a summon-specific invalidation policy.
inline constexpr Rva GAME_STATE_MAN_SET_PACKED_RETURN_POINT_FN{0x156b960};
inline constexpr Rva GAME_STATE_MAN_GET_PACKED_RETURN_POINT_FN{0x156d100};
// Resolves ReturnPointParam first, then writes the packed pair at +0x1528.
inline constexpr Rva GAME_STATE_MAN_SET_RETURN_POINT_ENTITY_FN{0x156d110};
// Resolves the same pair without writing GameStateMan. Failure preserves the
// requested entity ID and sets the animation half to 0xffffffff.
inline constexpr Rva GAME_STATE_MAN_RESOLVE_RETURN_POINT_FN{0x156d1a0};
inline constexpr Rva GAME_STATE_MAN_SET_FORCE_HUNTERS_DREAM_FN{0x156cfe0};
inline constexpr Rva GAME_STATE_MAN_GET_FORCE_HUNTERS_DREAM_FN{0x156cff0};
inline constexpr Rva GAME_STATE_MAN_SET_USE_RETURN_POINT_FN{0x156d0e0};
inline constexpr Rva GAME_STATE_MAN_GET_USE_RETURN_POINT_FN{0x156d0f0};
inline constexpr Rva GAME_STATE_MAN_SET_TRANSITION_RESPAWN_ENTITY_FN{0x156cd00};
inline constexpr Rva GAME_STATE_MAN_GET_TRANSITION_RESPAWN_ENTITY_FN{0x156ccf0};
inline constexpr Rva GAME_STATE_MAN_QUEUE_CHARACTER_SAVE_FN{0x156aa30};
inline constexpr Rva GAME_STATE_MAN_SERIALIZE_SAVE_AND_TRANSITION_SNAPSHOTS_FN{0x156aa50};
inline constexpr Rva GAME_STATE_MAN_DESERIALIZE_SAVE_AND_TRANSITION_SNAPSHOTS_FN{0x156b970};
inline constexpr Rva GAME_STATE_MAN_SET_TRANSITION_MAP_FN{0x156cce0};
inline constexpr Rva GAME_STATE_MAN_SET_SUMMON_RELOAD_TRANSFORM_ACTIVE_FN{0x156cfa0};
inline constexpr Rva GAME_STATE_MAN_IS_SUMMON_RELOAD_TRANSFORM_ACTIVE_FN{0x156cfb0};
inline constexpr Rva GAME_STATE_MAN_REQUEST_CHARACTER_SAVE_FN{0x156d570};
inline constexpr Rva GAME_STATE_MAN_REQUEST_SYSTEM_SAVE_FN{0x156d8a0};
inline constexpr Rva GAME_STATE_MAN_QUEUE_CHARACTER_AND_SYSTEM_SAVE_FN{0x156e820};
inline constexpr Rva GAME_STATE_MAN_IS_SYSTEM_SAVE_QUEUED_FN{0x156e870};
inline constexpr Rva GAME_STATE_MAN_WORLD_CHR_SERIALIZE_TRANSITION_OCCUPANCY_FN{0x190e750};

inline constexpr std::size_t GAME_STATE_MAN_KNOWN_PREFIX_SIZE = 0x1700;

struct GameStateManSerializedBuffer {
    std::int32_t byte_size;
    Unknown<0x04> _pad04;
    std::uint8_t* data;
};

using GameStateManTransitionBuffer = GameStateManSerializedBuffer;

struct GameStateManPackedStageMap {
    std::uint32_t value;
};

// ReturnPointParam row +4/+8, also consumed as one little-endian u64. Distinct
// from the transition's requested entity at GameStateMan+0x10 and the guest's
// summoned destination map at +0x14f0.
struct GameStateManReturnPoint {
    std::uint32_t entity_id;
    std::uint32_t animation_id;
};

struct GameStateMan {
    Unknown<0x0c> _unknown_0000;
    GameStateManPackedStageMap transition_map;
    // Lua_SetDefaultMapUid and the transition descriptor, not the lamp DB.
    std::uint32_t transition_respawn_entity_id;
    Unknown<0x147c> _unknown_0014;
    std::int32_t active_slot_index;
    Unknown<0x0c> _unknown_1494;
    std::uint64_t active_route[4];
    GameStateManPackedStageMap active_map_primary;
    GameStateManPackedStageMap session_target_map;
    Unknown<0x08> _unknown_14c8;
    std::uint64_t active_route_secondary[4];
    // Summon/party-ghost destination; not the persistent lamp return point.
    GameStateManPackedStageMap summon_destination_map;
    Unknown<0x2c> _unknown_14f4;
    std::uint8_t sos_sign_warp_active;
    std::uint8_t summon_reload_transform_active;
    Unknown<0x01> _unknown_1522;
    // One-shot choice consumed by BuildMoveMapTransitionDescriptor.
    std::uint8_t force_hunters_dream_respawn;
    // Lua_SetDefaultMapUid sets this when selecting the saved return point.
    std::uint8_t use_return_point;
    Unknown<0x03> _unknown_1525;
    GameStateManReturnPoint return_point;
    Unknown<0x17> _unknown_1530;
    std::uint8_t character_save_queued;
    std::uint8_t system_save_queued;
    std::uint8_t save_serialization_flavor;
    Unknown<0x0a> _unknown_154a;
    std::int32_t save_in_progress;
    std::uint8_t save_timestamp[0x10];
    Unknown<0x10> _unknown_1568;
    std::uint8_t save_enabled;
    std::uint8_t save_submitted;
    Unknown<0x0e> _unknown_157a;
    void* event_state;
    // CSNetworkFlowStep Init clears this; OfflineMode publishes 1 when its
    // native network checks pass. Lua network-error handlers also read it.
    // This observed latch is not an independent connectivity guarantee.
    std::uint8_t network_flow_online;
    // TitleStep enables this on its field-owning constructor path and clears
    // it on destruction. Gates CSNetworkFlowStep's network notifications.
    std::uint8_t title_network_events_enabled;
    // Shared world/snapshot policy byte, not just an action-state bit. Vanilla
    // immediate lamp registration requires nonzero; a guest applying a host's
    // snapshot and scalars can keep it zero and bypass the lamp's check
    // locally instead of changing this global value to light a lamp.
    std::uint8_t action_event_selector;
    Unknown<0x0d> _unknown_1593;
    // Set after OnLanCutError or OnNpServerSignOut dispatch. Clearing and other
    // consumers remain unclassified; it is not a native error enum.
    std::uint8_t network_disconnect_event_latched;
    Unknown<0x7f> _unknown_15a1;
    std::uint64_t source_route[4];
    std::int32_t source_key;
    GameStateManPackedStageMap source_map;
    std::uint8_t source_ready;
    Unknown<0x37> _unknown_1649;
    std::uint8_t serialize_guard_1680;
    std::uint8_t serialize_guard_1681;
    std::uint8_t transition_payload_state_1682;
    Unknown<0x0d> _unknown_1683;
    GameStateManSerializedBuffer world_chr_snapshot;
    Unknown<0x10> _unknown_16a0;
    GameStateManSerializedBuffer world_object_snapshot;
    Unknown<0x10> _unknown_16c0;
    GameStateManSerializedBuffer world_decal_snapshot;
    Unknown<0x10> _unknown_16e0;
    std::int32_t force_summon_count;
    std::uint8_t session_type;
    Unknown<0x03> _unknown_16f5;
    void* session_state;
};

namespace detail::game_state_man_layout {
BB_SIZE(GameStateMan, GAME_STATE_MAN_KNOWN_PREFIX_SIZE);
BB_SIZE(GameStateManSerializedBuffer, 0x10);
BB_OFFSET(GameStateMan, transition_map, 0x0c);
BB_OFFSET(GameStateMan, active_slot_index, 0x1490);
BB_OFFSET(GameStateMan, active_route, 0x14a0);
BB_OFFSET(GameStateMan, active_map_primary, 0x14c0);
BB_OFFSET(GameStateMan, session_target_map, 0x14c4);
BB_OFFSET(GameStateMan, sos_sign_warp_active, 0x1520);
BB_OFFSET(GameStateMan, summon_reload_transform_active, 0x1521);
BB_OFFSET(GameStateMan, character_save_queued, 0x1547);
BB_OFFSET(GameStateMan, system_save_queued, 0x1548);
BB_OFFSET(GameStateMan, save_in_progress, 0x1554);
BB_OFFSET(GameStateMan, save_enabled, 0x1578);
BB_OFFSET(GameStateMan, event_state, 0x1588);
BB_OFFSET(GameStateMan, network_flow_online, 0x1590);
BB_OFFSET(GameStateMan, title_network_events_enabled, 0x1591);
BB_OFFSET(GameStateMan, action_event_selector, 0x1592);
BB_OFFSET(GameStateMan, network_disconnect_event_latched, 0x15a0);
BB_OFFSET(GameStateMan, source_route, 0x1620);
BB_OFFSET(GameStateMan, source_key, 0x1640);
BB_OFFSET(GameStateMan, source_map, 0x1644);
BB_OFFSET(GameStateMan, source_ready, 0x1648);
BB_OFFSET(GameStateMan, serialize_guard_1680, 0x1680);
BB_OFFSET(GameStateMan, serialize_guard_1681, 0x1681);
BB_OFFSET(GameStateMan, transition_payload_state_1682, 0x1682);
BB_OFFSET(GameStateMan, world_chr_snapshot, 0x1690);
BB_OFFSET(GameStateMan, world_object_snapshot, 0x16b0);
BB_OFFSET(GameStateMan, world_decal_snapshot, 0x16d0);
BB_OFFSET(GameStateMan, force_summon_count, 0x16f0);
BB_OFFSET(GameStateMan, session_type, 0x16f4);
BB_OFFSET(GameStateMan, session_state, 0x16f8);
// The setter/doc comments place the packed return point at +0x1528.
BB_OFFSET(GameStateMan, return_point, 0x1528);
}  // namespace detail::game_state_man_layout

}  // namespace bb
