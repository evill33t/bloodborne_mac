// Network/session script state: world-transition state, its nested session
// presentation state, the tutorial SOS flag and the action-event/display
// message anchors the script helpers use.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"

namespace bb {

inline constexpr Rva ACTION_EVENT_DISPATCH{0x1317110};
inline constexpr Rva ACTION_EVENT_MESSAGE_ID_TABLE{0x4731fe0};
inline constexpr Rva ACTION_EVENT_LANE_TABLE{0x4731ff8};
inline constexpr Rva MESSAGE_UI_SINGLETON_PTR{0x5562878};
inline constexpr Rva DISPLAY_MESSAGE_DISPATCH{0x1bffe80};
inline constexpr Rva DISPLAY_MESSAGE_PENDING{0x55813d8};
inline constexpr Rva DISPLAY_MESSAGE_TEXT_OBJECT{0x55813e0};
inline constexpr Rva DISPLAY_MESSAGE_LOCATION{0x5581420};
inline constexpr std::uint32_t DISPLAY_MESSAGE_EMEVD_BANK = 2007;
inline constexpr std::uint32_t DISPLAY_MESSAGE_EMEVD_INSTRUCTION = 4;
inline constexpr std::uint32_t DISPLAY_MESSAGE_KNOWN_NATIVE_TEST_ID = 109000;
inline constexpr std::uint32_t ACTION_EVENT_INFO_PRIMARY_CATEGORY = 0x65;
inline constexpr std::uint32_t ACTION_EVENT_INFO_FALLBACK_CATEGORY = 0x1e;
inline constexpr std::uint32_t ACTION_EVENT_KNOWN_NATIVE_TEST_ID = 0x226cd;
inline constexpr std::uint32_t ACTION_EVENT_LANES_BY_SUMMON_TYPE[4] = {0, 1, 1, 0};

// Lua WarpNextStage binding.
inline constexpr Rva WARP_NEXT_STAGE_FN{0x132e010};
// Lua WarpNextStage_Bonfire binding.
inline constexpr Rva WARP_NEXT_STAGE_BONFIRE_FN{0x132e050};
// Lua WarpNextStageKick binding.
inline constexpr Rva WARP_NEXT_STAGE_KICK_FN{0x1332b80};
// Shared transition boundary reached by all three next-stage warp bindings.
inline constexpr Rva SESSION_WORLD_TRANSITION_FN{0x13cde30};
// Removes a native multiplayer presentation member from transition state.
inline constexpr Rva WORLD_TRANSITION_REMOVE_PRESENTATION_MEMBER_FN{0x15bc080};
// Adds or refreshes a native multiplayer presentation member.
inline constexpr Rva WORLD_TRANSITION_ADD_PRESENTATION_MEMBER_FN{0x15bc590};
inline constexpr Rva WORLD_TRANSITION_REMOVE_INVADERS_FOR_BOSS_ENTRY_FN{0x15bbc00};
inline constexpr Rva WORLD_TRANSITION_RECOUNT_PRESENTATION_MEMBERS_FN{0x15bce10};
// Inserts a character handle into the four-slot set, then recounts occupied slots.
inline constexpr Rva WORLD_TRANSITION_TRACK_CHR_HANDLE_FN{0x15bcd30};
// Removes a character handle from the four-slot set and recounts occupied slots.
inline constexpr Rva WORLD_TRANSITION_UNTRACK_CHR_HANDLE_FN{0x15bcf20};
// Legacy name retained for compatibility; the native argument is a ChrHandle.
inline constexpr Rva WORLD_TRANSITION_TRACK_CHR_TYPE_FN = WORLD_TRANSITION_TRACK_CHR_HANDLE_FN;
// Legacy name retained for compatibility; the native argument is a ChrHandle.
inline constexpr Rva WORLD_TRANSITION_UNTRACK_CHR_TYPE_FN = WORLD_TRANSITION_UNTRACK_CHR_HANDLE_FN;
inline constexpr Rva WORLD_TRANSITION_CLEAR_PRESENTATION_MEMBERS_FN{0x15bcfc0};
// Removes defeated AI presentation records before the transition continues.
inline constexpr Rva WORLD_TRANSITION_RESET_TRACKED_ENTITIES_FN{0x15bed10};
// Serializes world-character, object, and decal state for SummonedMapReload.
inline constexpr Rva SUMMONED_MAP_RELOAD_SERIALIZE_WORLD_STATE_FN{0x131e5c0};
// Constructs the native MoveMapStep task and selects its normal or
// transition-snapshot path from WorldSessionObjectMan+0x84. Both paths can
// ultimately build a descriptor and call MoveMapController_RequestMove;
// phase 3 changes the target and session lifecycle gates rather than skipping
// the move request itself.
inline constexpr Rva MOVE_MAP_STEP_CTOR_FN{0x1937570};
// Native setter for the active phase-3 session target map at +0x14c4.
inline constexpr Rva SET_SESSION_TARGET_MAP_FN{0x156ce80};
inline constexpr Rva SET_SESSION_TARGET_ROUTE_A_FN{0x156ce90};
inline constexpr Rva SET_SESSION_TARGET_ROUTE_B_FN{0x156ceb0};

inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_REDRAW_OFFSET = 0x10c;
inline constexpr std::size_t MESSAGE_UI_TALK_REQUEST_OFFSET = 0x58;
inline constexpr std::size_t MESSAGE_UI_TALK_MODE_OFFSET = 0x814;
inline constexpr std::size_t MESSAGE_UI_TALK_MESSAGE_ID_OFFSET = 0x818;
inline constexpr std::size_t MESSAGE_UI_BRIEFING_REQUEST_OFFSET = 0x114;
inline constexpr std::size_t MESSAGE_UI_BRIEFING_MESSAGE_ID_OFFSET = 0x16e8;
inline constexpr std::size_t MESSAGE_UI_BRIEFING_MODE_OFFSET = 0x16ec;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_LANE_BASE_OFFSET = 0x1638;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_LANE_STRIDE = 0x10;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_RING_INDEX_OFFSET = 0x08;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_READY_OFFSET = 0x0c;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_ENTRY_SIZE = 0x38;
inline constexpr std::size_t MESSAGE_UI_ACTION_EVENT_ENTRY_COUNT = 3;

inline constexpr std::size_t WORLD_TRANSITION_STATE_TRANSITION_REQUESTED_OFFSET = 0x08;
inline constexpr std::size_t WORLD_TRANSITION_STATE_PENDING_STAGE_MAP_OFFSET = 0x0c;
inline constexpr std::size_t WORLD_TRANSITION_STATE_PENDING_TRANSITION_ID_OFFSET = 0x10;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SUMMONED_POS_OFFSET = 0x20;
inline constexpr std::size_t WORLD_TRANSITION_STATE_ACTIVE_ROUTE_OFFSET = 0x14a0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_ACTIVE_MAP_PRIMARY_OFFSET = 0x14c0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_TARGET_MAP_OFFSET = 0x14c4;
inline constexpr std::size_t WORLD_TRANSITION_STATE_ACTIVE_ROUTE_SECONDARY_OFFSET = 0x14d0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SOS_SIGN_WARP_OFFSET = 0x1520;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SUMMON_RELOAD_TRANSFORM_ACTIVE_OFFSET = 0x1521;
inline constexpr std::size_t WORLD_TRANSITION_STATE_TUTORIAL_SUMMONED_POS_OFFSET = 0x1546;
inline constexpr std::size_t WORLD_TRANSITION_STATE_ACTION_EVENT_SELECTOR_OFFSET = 0x1592;
// Compatibility alias for the second confirmed use of +0x1592.
// SessionWorldTransition begins session leave only while this byte is zero.
inline constexpr std::size_t WORLD_TRANSITION_STATE_BEGIN_LEAVE_GATE_OFFSET = WORLD_TRANSITION_STATE_ACTION_EVENT_SELECTOR_OFFSET;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SOURCE_ROUTE_OFFSET = 0x1620;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SOURCE_KEY_OFFSET = 0x1640;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SOURCE_MAP_OFFSET = 0x1644;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SOURCE_READY_OFFSET = 0x1648;
inline constexpr std::size_t WORLD_TRANSITION_STATE_WORLD_CHR_SNAPSHOT_OFFSET = 0x1690;
inline constexpr std::size_t WORLD_TRANSITION_STATE_WORLD_OBJECT_SNAPSHOT_OFFSET = 0x16b0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_WORLD_DECAL_SNAPSHOT_OFFSET = 0x16d0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_FORCE_SUMMON_COUNT_OFFSET = 0x16f0;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SESSION_TYPE_OFFSET = 0x16f4;
inline constexpr std::size_t WORLD_TRANSITION_STATE_SESSION_STATE_OFFSET = 0x16f8;
inline constexpr std::size_t WORLD_TRANSITION_STATE_PREFIX_SIZE = 0x1700;
inline constexpr std::size_t WORLD_TRANSITION_SERIALIZED_BUFFER_SIZE = 0x10;

inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_TEAM_1_COUNT_OFFSET = 0x08;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_TEAM_2_COUNT_OFFSET = 0x0c;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_TEAM_12_COUNT_OFFSET = 0x10;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_PRESENTATION_COUNT_OFFSET = 0x14;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_CONNECTING_COUNT_OFFSET = 0x18;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_PRESENTATIONS_OFFSET = 0x1c;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_PRESENTATION_COUNT = 5;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_PRESENTATION_SIZE = 0x14;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_TYPE_LOCAL = 0;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_TYPE_NET = 1;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_TYPE_AI = 2;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_IDLE = 0;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_WAITING_FOR_ACTOR = 1;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_INITIALIZING = 2;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_ACTIVE = 3;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_DEFEATED_PENDING = 4;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_DEFEATED_FINALIZING = 5;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_EVENT_PENDING = 6;
inline constexpr std::int32_t MULTIPLAYER_PRESENTATION_STATE_LOCAL_PLAYER_DEFEATED = 7;
inline constexpr std::uint32_t MULTIPLAYER_ACTION_EVENT_MESSAGE_IDS[2][3] = {{0x222fe, 0x222ff, 0x22300}, {0x2230b, 0x2230c, 0x2230d}};
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_PREFIX_SIZE = 0x9c;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_PENDING_EVENT_TYPE_OFFSET = 0x94;
inline constexpr std::size_t WORLD_TRANSITION_SESSION_STATE_ACTIVE_CONNECT_ID_OFFSET = 0x98;

inline constexpr std::size_t TUTORIAL_SOS_STATE_SHOW_SOS_MESSAGE_OFFSET = 0xb4;
inline constexpr std::size_t TUTORIAL_SOS_STATE_PREFIX_SIZE = 0xb8;

// Packed map value written by WarpNextStage at WorldTransitionState+0x0c:
// area, block, region, index from the most significant byte down.
struct PackedStageMap {
    std::uint32_t value;

    static constexpr PackedStageMap make(std::uint8_t area, std::uint8_t block, std::uint8_t region, std::uint8_t index) {
        return {(static_cast<std::uint32_t>(area) << 24) | (static_cast<std::uint32_t>(block) << 16) |
                (static_cast<std::uint32_t>(region) << 8) | index};
    }
    constexpr std::uint8_t area() const { return static_cast<std::uint8_t>(value >> 24); }
    constexpr std::uint8_t block() const { return static_cast<std::uint8_t>(value >> 16); }
    constexpr std::uint8_t region() const { return static_cast<std::uint8_t>(value >> 8); }
    constexpr std::uint8_t index() const { return static_cast<std::uint8_t>(value); }
    constexpr bool operator==(PackedStageMap o) const { return value == o.value; }
};

// Owned serialized transition buffer stored in WorldTransitionState.
struct WorldTransitionSerializedBuffer {
    std::int32_t byte_size;
    Unknown<0x04> _pad04;
    std::uint8_t* data;

    std::size_t len() const { return byte_size > 0 ? static_cast<std::size_t>(byte_size) : 0; }
    bool is_empty() const { return byte_size <= 0 || data == nullptr; }
};

struct MultiplayerPresentationRecord {
    std::uint32_t chr_handle;
    // Debug label table: 0 = Local, 1 = Net, 2 = AI.
    std::int32_t member_type;
    // Native presentation lifecycle state processed by RVA 0x15bad90.
    std::int32_t lifecycle_state;
    std::int32_t init_event_flag;
    std::int32_t end_event_flag;

    bool is_network_member() const { return member_type == MULTIPLAYER_PRESENTATION_TYPE_NET; }
    bool is_active() const { return lifecycle_state == MULTIPLAYER_PRESENTATION_STATE_ACTIVE; }
    bool is_defeated() const {
        return lifecycle_state == MULTIPLAYER_PRESENTATION_STATE_DEFEATED_PENDING ||
               lifecycle_state == MULTIPLAYER_PRESENTATION_STATE_DEFEATED_FINALIZING;
    }
};

// Nested transition/session state reached through GameStateMan + 0x16f8. The
// complete 0x9c slice is typed.
struct WorldTransitionSessionState {
    Unknown<0x08> _unk00;
    // Returned by GetWhiteGhostCount (RVA 0x1337970) and read directly by
    // Lua_MultiDoping (RVA 0x138bc70); native presentation bookkeeping, not
    // persistent co-op membership across actor/world reconstruction.
    std::uint32_t team_1_count;
    std::uint32_t team_2_count;
    std::uint32_t team_12_count;
    std::uint32_t presentation_count;
    // Native pending/connecting presentation count at +0x18.
    std::uint32_t connecting_count;
    MultiplayerPresentationRecord presentations[WORLD_TRANSITION_SESSION_PRESENTATION_COUNT];
    // Character identities inserted by scheduled NPC work and AI presentation
    // setup, removed by NPC Return. Empty slots hold ChrHandle::NONE.
    ChrHandle tracked_chr_handles[4];
    std::uint32_t tracked_chr_handle_count;
    // Cleared by ClearPresentationMembers unless the retained transition
    // condition preserves it.
    std::int32_t pending_event_type;
    // Native active connection identifier; -1 is the inactive sentinel.
    std::int32_t active_connect_id;

    bool has_active_connection() const { return active_connect_id != -1; }
};

// Prefix of the world/session transition state pointed to by RVA 0x5556678.
// Not a full type claim: the offsets touched by the 1.09 network/session
// script helpers (SetSummonedPos, SetSosSignWarp, IsForceSummoned,
// GetWhiteGhostCount, ...).
struct WorldTransitionState {
    Unknown<0x08> _unk0000;
    std::uint8_t transition_requested;
    Unknown<0x03> _unk0009;
    PackedStageMap pending_stage_map;
    std::int32_t pending_transition_id;
    Unknown<0x0c> _unk0014;
    std::uint8_t summoned_pos_requested;
    Unknown<0x147f> _unk0021;
    // Native route/transform words copied from source_route by the guest
    // CSMultiPlayerInsTask::STEP_StartMultiPlayWait path.
    std::uint64_t active_route[4];
    PackedStageMap active_map_primary;
    // Session destination consumed by the phase-3 MoveMapController path.
    PackedStageMap target_map;
    Unknown<0x08> _unk14c8;
    std::uint64_t active_route_secondary[4];
    Unknown<0x30> _unk14f0;
    std::uint8_t sos_sign_warp_requested;
    // Selects the summon target transform at +0x14d0/+0x14e0. The native join
    // event sets it and the guest multiplayer task clears it on finish.
    std::uint8_t summon_reload_transform_active;
    Unknown<0x24> _unk1522;
    std::uint8_t tutorial_summoned_pos_requested;
    Unknown<0x4b> _unk1547;
    // ActionEvent row selector and transition-path gate. MoveMapStep_Ctor
    // writes zero when WorldSessionObjectMan+0x84 is phase 3 and one for the
    // ordinary path; SessionWorldTransition begins session leave only when
    // this byte is zero. Both uses confirmed; shared meaning not yet named.
    std::uint8_t action_event_selector;
    Unknown<0x8d> _unk1593;
    // Native source route/transform data copied into both active blocks for a
    // guest multiplayer transition.
    std::uint64_t source_route[4];
    // Identity checked by the native conditional destination-update helper;
    // higher-level type not confirmed.
    std::int32_t source_key;
    PackedStageMap source_map;
    std::uint8_t source_ready;
    Unknown<0x47> _unk1649;
    WorldTransitionSerializedBuffer world_chr_snapshot;
    Unknown<0x10> _unk16a0;
    WorldTransitionSerializedBuffer world_object_snapshot;
    Unknown<0x10> _unk16c0;
    WorldTransitionSerializedBuffer world_decal_snapshot;
    Unknown<0x10> _unk16e0;
    std::int32_t force_summon_count;
    std::uint8_t session_type;
    Unknown<0x03> _unk16f5;
    WorldTransitionSessionState* session_state;

    // The action-event message ID for summonparam_type -1/-2/-3; false for
    // any other type.
    bool multiplayer_action_event_message_id(std::int32_t summonparam_type, std::uint32_t* out) const {
        const unsigned row = static_cast<unsigned>(action_event_selector ^ 1);
        int column;
        switch (summonparam_type) {
            case -1: column = 0; break;
            case -2: column = 1; break;
            case -3: column = 2; break;
            default: return false;
        }
        if (row >= 2) return false;
        *out = MULTIPLAYER_ACTION_EVENT_MESSAGE_IDS[row][column];
        return true;
    }
    // (Named apart from their fields because C++ cannot share a name between a
    // member and a member function.)
    bool is_transition_requested() const { return transition_requested != 0; }
    bool is_summoned_pos_requested() const { return summoned_pos_requested != 0; }
    bool is_sos_sign_warp_requested() const { return sos_sign_warp_requested != 0; }
    bool is_tutorial_summoned_pos_requested() const { return tutorial_summoned_pos_requested != 0; }
    bool force_summoned() const { return force_summon_count > 0; }
    // Mirrors the branch in SessionWorldTransition.
    bool skips_begin_leave_session() const { return action_event_selector != 0; }
};

// Prefix of RVA 0x5562878, used by IsShowSosMsg_Tutorial.
struct TutorialSosState {
    Unknown<0xb4> _unk00;
    std::int32_t show_sos_message;

    bool should_show_sos_message() const { return show_sos_message != 0; }
};

namespace detail::network_script_state_layout {
using W = WorldTransitionState;
BB_SIZE(W, WORLD_TRANSITION_STATE_PREFIX_SIZE);
BB_OFFSET(W, transition_requested, WORLD_TRANSITION_STATE_TRANSITION_REQUESTED_OFFSET);
BB_OFFSET(W, pending_stage_map, WORLD_TRANSITION_STATE_PENDING_STAGE_MAP_OFFSET);
BB_OFFSET(W, pending_transition_id, WORLD_TRANSITION_STATE_PENDING_TRANSITION_ID_OFFSET);
BB_OFFSET(W, summoned_pos_requested, WORLD_TRANSITION_STATE_SUMMONED_POS_OFFSET);
BB_OFFSET(W, active_route, WORLD_TRANSITION_STATE_ACTIVE_ROUTE_OFFSET);
BB_OFFSET(W, active_map_primary, WORLD_TRANSITION_STATE_ACTIVE_MAP_PRIMARY_OFFSET);
BB_OFFSET(W, target_map, WORLD_TRANSITION_STATE_TARGET_MAP_OFFSET);
BB_OFFSET(W, active_route_secondary, WORLD_TRANSITION_STATE_ACTIVE_ROUTE_SECONDARY_OFFSET);
BB_OFFSET(W, sos_sign_warp_requested, WORLD_TRANSITION_STATE_SOS_SIGN_WARP_OFFSET);
BB_OFFSET(W, summon_reload_transform_active, WORLD_TRANSITION_STATE_SUMMON_RELOAD_TRANSFORM_ACTIVE_OFFSET);
BB_OFFSET(W, tutorial_summoned_pos_requested, WORLD_TRANSITION_STATE_TUTORIAL_SUMMONED_POS_OFFSET);
BB_OFFSET(W, action_event_selector, WORLD_TRANSITION_STATE_ACTION_EVENT_SELECTOR_OFFSET);
BB_OFFSET(W, action_event_selector, WORLD_TRANSITION_STATE_BEGIN_LEAVE_GATE_OFFSET);
BB_OFFSET(W, source_route, WORLD_TRANSITION_STATE_SOURCE_ROUTE_OFFSET);
BB_OFFSET(W, source_key, WORLD_TRANSITION_STATE_SOURCE_KEY_OFFSET);
BB_OFFSET(W, source_map, WORLD_TRANSITION_STATE_SOURCE_MAP_OFFSET);
BB_OFFSET(W, source_ready, WORLD_TRANSITION_STATE_SOURCE_READY_OFFSET);
BB_OFFSET(W, world_chr_snapshot, WORLD_TRANSITION_STATE_WORLD_CHR_SNAPSHOT_OFFSET);
BB_OFFSET(W, world_object_snapshot, WORLD_TRANSITION_STATE_WORLD_OBJECT_SNAPSHOT_OFFSET);
BB_OFFSET(W, world_decal_snapshot, WORLD_TRANSITION_STATE_WORLD_DECAL_SNAPSHOT_OFFSET);
BB_OFFSET(W, force_summon_count, WORLD_TRANSITION_STATE_FORCE_SUMMON_COUNT_OFFSET);
BB_OFFSET(W, session_type, WORLD_TRANSITION_STATE_SESSION_TYPE_OFFSET);
BB_OFFSET(W, session_state, WORLD_TRANSITION_STATE_SESSION_STATE_OFFSET);
BB_SIZE(WorldTransitionSerializedBuffer, WORLD_TRANSITION_SERIALIZED_BUFFER_SIZE);
BB_OFFSET(WorldTransitionSerializedBuffer, byte_size, 0x00);
BB_OFFSET(WorldTransitionSerializedBuffer, data, 0x08);
using S = WorldTransitionSessionState;
BB_SIZE(S, WORLD_TRANSITION_SESSION_STATE_PREFIX_SIZE);
BB_OFFSET(S, team_1_count, WORLD_TRANSITION_SESSION_STATE_TEAM_1_COUNT_OFFSET);
BB_OFFSET(S, team_2_count, WORLD_TRANSITION_SESSION_STATE_TEAM_2_COUNT_OFFSET);
BB_OFFSET(S, team_12_count, WORLD_TRANSITION_SESSION_STATE_TEAM_12_COUNT_OFFSET);
BB_OFFSET(S, presentation_count, WORLD_TRANSITION_SESSION_STATE_PRESENTATION_COUNT_OFFSET);
BB_OFFSET(S, connecting_count, WORLD_TRANSITION_SESSION_STATE_CONNECTING_COUNT_OFFSET);
BB_OFFSET(S, presentations, WORLD_TRANSITION_SESSION_STATE_PRESENTATIONS_OFFSET);
BB_OFFSET(S, tracked_chr_handles, 0x80);
BB_OFFSET(S, tracked_chr_handle_count, 0x90);
BB_OFFSET(S, pending_event_type, WORLD_TRANSITION_SESSION_STATE_PENDING_EVENT_TYPE_OFFSET);
BB_OFFSET(S, active_connect_id, WORLD_TRANSITION_SESSION_STATE_ACTIVE_CONNECT_ID_OFFSET);
BB_SIZE(MultiplayerPresentationRecord, WORLD_TRANSITION_SESSION_PRESENTATION_SIZE);
BB_OFFSET(MultiplayerPresentationRecord, chr_handle, 0x00);
BB_OFFSET(MultiplayerPresentationRecord, member_type, 0x04);
BB_OFFSET(MultiplayerPresentationRecord, lifecycle_state, 0x08);
BB_OFFSET(MultiplayerPresentationRecord, init_event_flag, 0x0c);
BB_OFFSET(MultiplayerPresentationRecord, end_event_flag, 0x10);
BB_SIZE(TutorialSosState, TUTORIAL_SOS_STATE_PREFIX_SIZE);
BB_OFFSET(TutorialSosState, show_sos_message, TUTORIAL_SOS_STATE_SHOW_SOS_MESSAGE_OFFSET);
static_assert(PackedStageMap::make(21, 4, 0, 2).value == 0x15040002, "PackedStageMap byte order");
static_assert(WORLD_TRANSITION_TRACK_CHR_TYPE_FN == WORLD_TRANSITION_TRACK_CHR_HANDLE_FN, "legacy alias");
static_assert(WORLD_TRANSITION_UNTRACK_CHR_TYPE_FN == WORLD_TRANSITION_UNTRACK_CHR_HANDLE_FN, "legacy alias");
}  // namespace detail::network_script_state_layout

}  // namespace bb
