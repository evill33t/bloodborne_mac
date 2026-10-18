// SprjScript/Lua event bridge: script-exposed class objects and
// SprjScriptCallParam.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva SPRJ_LUA_EVENT_PROXY_CLASS_OBJECT_PTR{0x554a040};
inline constexpr Rva SPRJ_SCRIPT_CALL_PARAM_CLASS_OBJECT_PTR{0x5549fd0};

inline constexpr std::size_t SPRJ_SCRIPT_CLASS_OBJECT_PREFIX_SIZE = 0x50;
inline constexpr std::size_t SPRJ_SCRIPT_CLASS_OBJECT_NAME_OFFSET = 0x40;
inline constexpr std::size_t SPRJ_SCRIPT_CLASS_OBJECT_WIDE_NAME_OFFSET = 0x48;

inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_PREFIX_SIZE = 0x20;
inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_IS_NET_MESSAGE_OFFSET = 0x08;
inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_PLAY_ID_OFFSET = 0x0c;
inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_PARAM1_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_PARAM2_OFFSET = 0x14;
inline constexpr std::size_t SPRJ_SCRIPT_CALL_PARAM_PARAM3_OFFSET = 0x18;

// Prefix of a script-exposed class object used by the SprjScript/Lua event
// bridge. RVA 0x137d550 returns the SprjLuaEventProxy class object at RVA
// 0x554a040; RVA 0x133b4e0 uses its vfunc +0x58 to register the 524 script
// call handlers. RVA 0x5549fd0 is the same shape for SprjScriptCallParam.
struct SprjScriptClassObject {
    void* vftable;
    Unknown<0x38> _unk08;
    const char* name;
    const char16_t* wide_name;
};

// Prefix of SprjScriptCallParam, registered by RVA 0x133b4e0.
struct SprjScriptCallParam {
    void* vftable;
    std::uint8_t is_net_message;
    Unknown<0x03> _pad09;
    std::int32_t play_id;
    std::int32_t param1;
    std::int32_t param2;
    std::int32_t param3;
    Unknown<0x04> _pad1c;

    // Nonzero counts as true.
    bool net_message() const { return is_net_message != 0; }
};

namespace detail::script_call_layout {
BB_SIZE(SprjScriptClassObject, SPRJ_SCRIPT_CLASS_OBJECT_PREFIX_SIZE);
BB_OFFSET(SprjScriptClassObject, name, SPRJ_SCRIPT_CLASS_OBJECT_NAME_OFFSET);
BB_OFFSET(SprjScriptClassObject, wide_name, SPRJ_SCRIPT_CLASS_OBJECT_WIDE_NAME_OFFSET);
BB_SIZE(SprjScriptCallParam, SPRJ_SCRIPT_CALL_PARAM_PREFIX_SIZE);
BB_OFFSET(SprjScriptCallParam, is_net_message, SPRJ_SCRIPT_CALL_PARAM_IS_NET_MESSAGE_OFFSET);
BB_OFFSET(SprjScriptCallParam, play_id, SPRJ_SCRIPT_CALL_PARAM_PLAY_ID_OFFSET);
BB_OFFSET(SprjScriptCallParam, param1, SPRJ_SCRIPT_CALL_PARAM_PARAM1_OFFSET);
BB_OFFSET(SprjScriptCallParam, param2, SPRJ_SCRIPT_CALL_PARAM_PARAM2_OFFSET);
BB_OFFSET(SprjScriptCallParam, param3, SPRJ_SCRIPT_CALL_PARAM_PARAM3_OFFSET);
}  // namespace detail::script_call_layout

}  // namespace bb
