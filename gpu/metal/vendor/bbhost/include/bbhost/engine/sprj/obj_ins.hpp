// ObjIns: the event-script touched prefix of object instances.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t OBJ_INS_SIZE = 0x2e0;
inline constexpr std::size_t OBJ_INS_UPDATE_HELPER_OFFSET = 0x10;
inline constexpr std::size_t OBJ_INS_DEACTIVATE_GATE_FLAGS_OFFSET = 0x1c8;
inline constexpr std::size_t OBJ_INS_EVENT_STATE_FLAGS_OFFSET = 0x1fc;

inline constexpr std::uint8_t OBJ_INS_DEACTIVATE_WRITE_GATE_BIT = 1 << 2;
inline constexpr std::uint32_t OBJ_INS_DRAW_DIRTY_BIT = 1u << 13;
inline constexpr std::uint32_t OBJ_INS_DRAW_ENABLE_BIT = 1u << 14;
inline constexpr std::uint32_t OBJ_INS_COLLISION_ENABLE_BIT = 1u << 18;
inline constexpr std::uint32_t OBJ_INS_DEACTIVATED_FOR_EVENT_BIT = 1u << 21;

// Event-script touched prefix for object instances.
struct ObjIns {
    void* vftable;
    Unknown<0x08> _unk08;
    void* update_helper;
    Unknown<0x1b0> _unk18;
    std::uint8_t deactivate_gate_flags;
    Unknown<0x33> _unk1c9;
    std::uint32_t event_state_flags;
    Unknown<0xe0> _unk200;

    bool draw_update_requested_for_event() const { return (event_state_flags & OBJ_INS_DRAW_DIRTY_BIT) != 0; }
    bool draw_enabled_for_event() const { return (event_state_flags & OBJ_INS_DRAW_ENABLE_BIT) != 0; }
    bool collision_enabled_for_event() const { return (event_state_flags & OBJ_INS_COLLISION_ENABLE_BIT) != 0; }
    bool deactivated_for_event() const { return (event_state_flags & OBJ_INS_DEACTIVATED_FOR_EVENT_BIT) != 0; }
    bool allows_event_deactivate_write() const { return (deactivate_gate_flags & OBJ_INS_DEACTIVATE_WRITE_GATE_BIT) != 0; }
};

namespace detail::obj_ins_layout {
BB_SIZE(ObjIns, OBJ_INS_SIZE);
BB_OFFSET(ObjIns, update_helper, OBJ_INS_UPDATE_HELPER_OFFSET);
BB_OFFSET(ObjIns, deactivate_gate_flags, OBJ_INS_DEACTIVATE_GATE_FLAGS_OFFSET);
BB_OFFSET(ObjIns, event_state_flags, OBJ_INS_EVENT_STATE_FLAGS_OFFSET);
}  // namespace detail::obj_ins_layout

}  // namespace bb
