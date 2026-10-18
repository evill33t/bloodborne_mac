// SprjEventMan and SprjEventActMan prefixes: the offsets 1.09 xrefs use.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct SprjEventSosSelectionState;

inline constexpr std::size_t SPRJ_EVENT_MAN_PREFIX_SIZE = 0x88;
inline constexpr std::size_t SPRJ_EVENT_MAN_STATE_FLAGS_SIZE = 0x18;
inline constexpr std::size_t SPRJ_EVENT_MAN_CURRENT_AREA_STATE_PREFIX_SIZE = 0x50;

inline constexpr std::size_t SPRJ_EVENT_MAN_STATE_FLAGS_OFFSET = 0x08;
inline constexpr std::size_t SPRJ_EVENT_MAN_RESET_TARGET_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_EVENT_MAN_AREA_STATE_OFFSET = 0x60;
inline constexpr std::size_t SPRJ_EVENT_MAN_RUNTIME_STATE_OFFSET = 0x80;

inline constexpr std::size_t SPRJ_EVENT_MAN_STATE_FLAGS_MAP_ID_OFFSET = 0x08;
inline constexpr std::size_t SPRJ_EVENT_MAN_STATE_FLAGS_EVENT_ACTIVE_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_EVENT_MAN_STATE_FLAGS_EVENT_PENDING_OFFSET = 0x11;
inline constexpr std::size_t SPRJ_EVENT_MAN_AREA_STATE_CURRENT_AREA_STATE_OFFSET = 0x30;
inline constexpr std::size_t SPRJ_EVENT_MAN_AREA_STATE_QUEUED_MAP_ID_OFFSET = 0x40;
inline constexpr std::size_t SPRJ_EVENT_MAN_AREA_STATE_TEMP_SUMMON_PARAM_OFFSET =
    SPRJ_EVENT_MAN_AREA_STATE_QUEUED_MAP_ID_OFFSET;
inline constexpr std::size_t SPRJ_EVENT_MAN_CURRENT_AREA_STATE_SOS_SELECTION_OFFSET = 0x38;

inline constexpr std::size_t SPRJ_EVENT_ACT_MAN_PREFIX_SIZE = 0x60;
inline constexpr std::size_t SPRJ_EVENT_ACT_MAN_EVENT_PAD_ENABLED_OFFSET = 0x28;
inline constexpr std::size_t SPRJ_EVENT_ACT_MAN_LEAVE_EVENT_LATCH_OFFSET = 0x58;

struct SprjEventManStateFlags {
    Unknown<0x08> _unk00;
    std::int32_t map_id;
    Unknown<0x04> _unk0c;
    std::uint8_t event_active;
    std::uint8_t event_pending;
    Unknown<0x06> _unk12;
};

struct SprjEventManCurrentAreaState {
    void* vftable;
    Unknown<0x30> _unk08;
    // Separately allocated 0x240-byte summon-selection object constructed by
    // RVA 0x1946ce0. Its fields are not inline in this owner.
    SprjEventSosSelectionState* sos_selection_state;
    Unknown<0x10> _unk40;
};

struct SprjEventManAreaState {
    Unknown<0x30> _unk00;
    SprjEventManCurrentAreaState* current_area_state;
    Unknown<0x08> _unk38;
    std::int32_t queued_map_id;

    std::int32_t temp_summon_param() const { return queued_map_id; }
};

struct SprjEventManRuntimeState {
    Unknown<0x08> _unk00;
    void* runtime;  // of a type not identified yet
};

// Prefix of Bloodborne's SprjEventMan. The full allocation size is not
// recovered yet; this covers only offsets used by 1.09 xrefs. The pointers
// are null-guarded by every native consumer.
struct SprjEventMan {
    void* vftable;
    SprjEventManStateFlags* state_flags;
    void* reset_target;  // of a type not identified yet
    Unknown<0x48> _unk18;
    SprjEventManAreaState* area_state;
    Unknown<0x18> _unk68;
    SprjEventManRuntimeState* runtime_state;
};

// Prefix of Bloodborne's SprjEventActMan. SetEnableEventPad proves the byte at
// +0x28. The network event-state publisher at RVA 0x1339b50 mirrors the Lua
// event-context leave latch into +0x58 and into SprjActionButtonMan+0x52.
struct SprjEventActMan {
    void* vftable;
    Unknown<0x20> _unk08;
    std::uint8_t event_pad_enabled;
    Unknown<0x2f> _unk29;
    std::uint8_t leave_event_latched;
    Unknown<0x07> _unk59;
};

namespace detail::event_man_layout {
BB_SIZE(SprjEventMan, SPRJ_EVENT_MAN_PREFIX_SIZE);
BB_OFFSET(SprjEventMan, state_flags, SPRJ_EVENT_MAN_STATE_FLAGS_OFFSET);
BB_OFFSET(SprjEventMan, reset_target, SPRJ_EVENT_MAN_RESET_TARGET_OFFSET);
BB_OFFSET(SprjEventMan, area_state, SPRJ_EVENT_MAN_AREA_STATE_OFFSET);
BB_OFFSET(SprjEventMan, runtime_state, SPRJ_EVENT_MAN_RUNTIME_STATE_OFFSET);
BB_SIZE(SprjEventManStateFlags, SPRJ_EVENT_MAN_STATE_FLAGS_SIZE);
BB_OFFSET(SprjEventManStateFlags, map_id, SPRJ_EVENT_MAN_STATE_FLAGS_MAP_ID_OFFSET);
BB_OFFSET(SprjEventManStateFlags, event_active, SPRJ_EVENT_MAN_STATE_FLAGS_EVENT_ACTIVE_OFFSET);
BB_OFFSET(SprjEventManStateFlags, event_pending, SPRJ_EVENT_MAN_STATE_FLAGS_EVENT_PENDING_OFFSET);
BB_OFFSET(SprjEventManAreaState, current_area_state, SPRJ_EVENT_MAN_AREA_STATE_CURRENT_AREA_STATE_OFFSET);
BB_OFFSET(SprjEventManAreaState, queued_map_id, SPRJ_EVENT_MAN_AREA_STATE_QUEUED_MAP_ID_OFFSET);
BB_OFFSET(SprjEventManAreaState, queued_map_id, SPRJ_EVENT_MAN_AREA_STATE_TEMP_SUMMON_PARAM_OFFSET);
BB_SIZE(SprjEventManCurrentAreaState, SPRJ_EVENT_MAN_CURRENT_AREA_STATE_PREFIX_SIZE);
BB_OFFSET(SprjEventManCurrentAreaState, sos_selection_state, SPRJ_EVENT_MAN_CURRENT_AREA_STATE_SOS_SELECTION_OFFSET);
BB_SIZE(SprjEventActMan, SPRJ_EVENT_ACT_MAN_PREFIX_SIZE);
BB_OFFSET(SprjEventActMan, event_pad_enabled, SPRJ_EVENT_ACT_MAN_EVENT_PAD_ENABLED_OFFSET);
BB_OFFSET(SprjEventActMan, leave_event_latched, SPRJ_EVENT_ACT_MAN_LEAVE_EVENT_LATCH_OFFSET);
}  // namespace detail::event_man_layout

}  // namespace bb
