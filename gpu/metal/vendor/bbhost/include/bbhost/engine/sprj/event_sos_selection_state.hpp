// Event-side summon selection and room-handoff state (Bloodborne 1.09).
//
// Allocated by the event system at RVA 0x1946ce0 and distinct from the native
// SosSignMan stored at FrpgNetMan + 0xc50. The event object owns request
// indexes, scheduled work, room handoff state, and the per-team scheduling
// limits consumed by its tick.
//
// At RVA 0x187387b, the received-room path calls
// WorldChrMan_IsSummonAreaEligible (RVA 0x191a750). Only an eligible local
// player, role 0/2/5, and process_callback_payload != 0 reach deserialization.
// The pending-create record is cleared afterward even if those checks fail, so
// a received SosSignMan room payload does not imply a room join began.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_CONSTRUCT{0x1870c50};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_TICK{0x1872360};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_INSERT_OR_UPDATE_REQUEST{0x1875320};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_MAYBE_SCHEDULE_REQUEST{0x1876060};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_BUILD_SCHEDULED_WORK{0x1877460};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_RETIRE_DESCRIPTOR_STATE{0x1875740};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_UPDATE_DEBUG_UI{0x18777e0};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_STATE_BUILD_REQUEST_FROM_CHR{0x1878d90};

// Bytes requested by the event owner before calling the constructor.
inline constexpr std::size_t SPRJ_EVENT_SOS_SELECTION_STATE_ALLOCATION_SIZE = 0x240;
// End of the final proven field (+0x230 through +0x233).
inline constexpr std::size_t SPRJ_EVENT_SOS_SELECTION_STATE_PROVEN_FIELD_END = 0x234;
// The naturally aligned size of the proven field model.
inline constexpr std::size_t SPRJ_EVENT_SOS_SELECTION_STATE_PREFIX_SIZE = 0x238;
inline constexpr std::size_t SPRJ_EVENT_SOS_SELECTION_REQUEST_ENTRY_SIZE = 0xa8;
inline constexpr std::size_t SPRJ_EVENT_SOS_SELECTION_SCHEDULED_WORK_SIZE = 0x68;

// Four-word tree owner used by the three SOS-id indexes at +0x08, +0x28 and
// +0x48. The constructor allocates a 0x30-byte red-black-tree sentinel and
// stores it in sentinel.
struct SprjEventSosTreeHeader {
    void* _owner_state;
    void* sentinel;
    std::size_t count;
    void* allocator;
};

struct SprjEventSosListHeader {
    void* sentinel;
    std::size_t count;
    void* allocator;
};

// Canonical 0xa8-byte request copied into the event-side request index. Only
// fields read by the scheduler are named.
struct SprjEventSosRequestEntry {
    std::int32_t descriptor_id;
    Unknown<0x1e> _unknown_004;
    std::uint8_t session_type;
    std::uint8_t _unknown_023;
    std::uint16_t character_name_utf16[0x11];
    Unknown<0x42> _unknown_046;
    std::int32_t world_chr_lookup_key;
    std::int32_t request_field_8c;
    std::int32_t request_field_90;
    Unknown<0x04> _unknown_094;
    std::uint64_t resource_handle_a;
    std::uint64_t resource_handle_b;
};

// 0x68-byte work item shown by the game's own SosSignMan debug page as SOSID,
// character name, state, type, and timeout.
struct SprjEventSosScheduledWorkEntry {
    std::int32_t descriptor_id;
    float elapsed;
    std::uint8_t session_type;
    Unknown<0x03> _unknown_009;
    std::uint32_t state_raw;
    std::uint16_t character_name_utf16[0x10];
    Unknown<0x04> _unknown_030;
    std::int32_t world_chr_lookup_key;
    std::int32_t request_field_8c;
    std::int32_t request_field_90;
    float position[3];
    float yaw;
    std::uint64_t _unknown_050;
    std::uint64_t resource_handle_a;
    std::uint64_t resource_handle_b;
};

// Proven prefix of the event-side SOS selection object. The two secondary
// indexes are role-named rather than content-named: their exact value
// ownership remains unresolved, but the scheduler uses them to retire state
// and resources associated with a descriptor id.
struct SprjEventSosSelectionState {
    void* vftable;
    SprjEventSosTreeHeader requests_by_descriptor_id;
    SprjEventSosTreeHeader request_state_by_descriptor_id;
    SprjEventSosTreeHeader request_resources_by_descriptor_id;
    Unknown<0x08> _unknown_068;
    SprjEventSosListHeader route_request_list;
    std::int32_t room_handoff_arm;
    std::uint32_t room_handoff_payload_word;
    std::int32_t active_summon_descriptor;
    Unknown<0x10> _unknown_094;
    std::int32_t summon_in_flight_raw;
    std::int32_t room_handoff_in_flight_raw;
    Unknown<0x114> _unknown_0ac;
    // Native tick-owned work list. The tick frees completed/expired work and
    // its list node before retiring the corresponding descriptor indexes.
    SprjEventSosListHeader scheduled_work_list;
    Unknown<0x08> _unknown_1d8;
    SprjEventSosListHeader completion_list;
    std::uint8_t process_callback_payload;
    Unknown<0x03> _unknown_1f9;
    std::int32_t team_1_coop_limit;
    std::int32_t team_2_invasion_limit;
    Unknown<0x0c> _unknown_204;
    float schedule_cooldown;
    Unknown<0x04> _unknown_214;
    void* debug_menu;
    void* debug_request_menu;
    void* debug_sign_menu;
    std::int32_t room_handoff_latch_raw;
};

using SprjEventSosBuildScheduledWorkAbi = void(BB_GAME_ABI*)(SprjEventSosSelectionState*,
                                                              const SprjEventSosRequestEntry*);
using SprjEventSosSelectionStateConstructAbi = void(BB_GAME_ABI*)(SprjEventSosSelectionState*);
using SprjEventSosSelectionStateTickAbi = void(BB_GAME_ABI*)(float, SprjEventSosSelectionState*);
using SprjEventSosSelectionRetireDescriptorStateAbi = void(BB_GAME_ABI*)(SprjEventSosSelectionState*,
                                                                          std::uint32_t descriptor_id);
using SprjEventSosSelectionRequestAbi = void(BB_GAME_ABI*)(SprjEventSosSelectionState*,
                                                            const SprjEventSosRequestEntry*);
using SprjEventSosSelectionDebugUiAbi = void(BB_GAME_ABI*)(SprjEventSosSelectionState*);

namespace detail::event_sos_selection_state_layout {
BB_SIZE(SprjEventSosTreeHeader, 0x20);
BB_SIZE(SprjEventSosListHeader, 0x18);
BB_SIZE(SprjEventSosRequestEntry, 0xa8);
BB_SIZE(SprjEventSosRequestEntry, SPRJ_EVENT_SOS_SELECTION_REQUEST_ENTRY_SIZE);
BB_SIZE(SprjEventSosScheduledWorkEntry, 0x68);
BB_SIZE(SprjEventSosScheduledWorkEntry, SPRJ_EVENT_SOS_SELECTION_SCHEDULED_WORK_SIZE);
BB_OFFSET(SprjEventSosSelectionState, route_request_list, 0x70);
BB_OFFSET(SprjEventSosSelectionState, room_handoff_arm, 0x88);
BB_OFFSET(SprjEventSosSelectionState, scheduled_work_list, 0x1c0);
BB_OFFSET(SprjEventSosSelectionState, completion_list, 0x1e0);
BB_OFFSET(SprjEventSosSelectionState, team_1_coop_limit, 0x1fc);
BB_OFFSET(SprjEventSosSelectionState, schedule_cooldown, 0x210);
BB_OFFSET(SprjEventSosSelectionState, room_handoff_latch_raw, 0x230);
static_assert(offsetof(SprjEventSosSelectionState, room_handoff_latch_raw) + sizeof(std::int32_t) ==
                  SPRJ_EVENT_SOS_SELECTION_STATE_PROVEN_FIELD_END,
              "SprjEventSosSelectionState proven field end");
BB_SIZE(SprjEventSosSelectionState, 0x238);
BB_SIZE(SprjEventSosSelectionState, SPRJ_EVENT_SOS_SELECTION_STATE_PREFIX_SIZE);
}  // namespace detail::event_sos_selection_state_layout

}  // namespace bb
