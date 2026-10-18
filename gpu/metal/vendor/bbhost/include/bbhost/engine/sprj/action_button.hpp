// Action-button system runtime metadata.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_ACTION_BUTTON_MAN_SIZE = 0x60;
inline constexpr std::size_t SPRJ_ACTION_BUTTON_MAN_LEAVE_EVENT_LATCH_OFFSET = 0x52;

// Observed SprjActionButtonMan singleton. The init path at RVA 0x176acd0
// allocates 0x60 bytes and zeros these fields inline. Use sites prove a list
// head at +0x08 and flag reads at +0x28/+0x2b. The network event-state
// publisher at RVA 0x1339b50 mirrors the Lua event-context leave latch into +0x52.
struct SprjActionButtonMan {
    void* vftable;
    Unknown<1>* action_button_list_head;
    Unknown<0x18> _unk10;
    std::uint8_t flags_28[4];
    std::uint32_t _unk2c;
    void* _node30_vftable;
    std::uint32_t _node38_count;
    std::uint32_t _pad3c;
    void* _node40_vftable;
    std::uint32_t _node48_count;
    std::uint32_t _unk4c;
    std::uint16_t _unk50;
    std::uint8_t leave_event_latched;
    std::uint8_t _unk53;
    std::uint16_t _unk54;
    std::uint8_t flag_56;
    std::uint8_t _pad57;
    std::uint64_t _unk58;

    static constexpr Rva SINGLETON_PTR = SPRJ_ACTION_BUTTON_MAN_SINGLETON_PTR;

    std::uint8_t flag_28() const { return flags_28[0]; }
    std::uint8_t flag_2b() const { return flags_28[3]; }
};

namespace detail::action_button_layout {
static_assert(SprjActionButtonMan::SINGLETON_PTR.rva == 0x5540048, "SprjActionButtonMan::SINGLETON_PTR");
BB_SIZE(SprjActionButtonMan, SPRJ_ACTION_BUTTON_MAN_SIZE);
BB_OFFSET(SprjActionButtonMan, action_button_list_head, 0x08);
BB_OFFSET(SprjActionButtonMan, flags_28, 0x28);
BB_OFFSET(SprjActionButtonMan, leave_event_latched, SPRJ_ACTION_BUTTON_MAN_LEAVE_EVENT_LATCH_OFFSET);
BB_OFFSET(SprjActionButtonMan, flag_56, 0x56);
}  // namespace detail::action_button_layout

}  // namespace bb
