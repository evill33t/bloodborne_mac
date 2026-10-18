// Native event-debug storage (SprjDbgEvent) and its borrowed Lua/menu links.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct SprjLuaEventConditionList;
struct SprjLuaEventContext;

// Native SprjDbgEvent, allocated 0xf8 aligned to eight while constructing
// SprjEventState; no instance vtable. Construction zeros +0x00..+0xa0.
// LUA_BIND_FN borrows the Lua manager's two children here in reversed order;
// LUA_RESET_FN destroys them, then DETACH_FN clears these borrows and resets or
// removes selected debug menus. Singleton destruction frees the allocation
// directly without running detach. The outer debug roots' population is not
// recovered; a zero root makes its menu-builder path a no-op. Menu pointers are
// native references. Unknown bytes include uninitialized gaps.
struct SprjDbgEvent {
    SprjLuaEventContext* event_dispatcher;
    SprjLuaEventConditionList* conditions;
    std::uint8_t _unk10[0x10];
    // DETACH_FN calls this menu's virtual +0x88 when nonnull.
    void* menu_20;
    std::uint8_t _unk28[8];
    // Parent for Distance, SimpleTalk and reaction/range editor rows.
    void* condition_editor_root;
    // Overwritten with each attached row, or null on failure. A missing parent
    // leaves the previous value and returns null.
    void* last_condition_editor;
    std::uint8_t _unk40[8];
    void* condition_list_menu;
    void* message_map_menu;
    void* timer_menu;
    // DETACH_FN calls virtual +0x88 when nonnull.
    void* menu_60;
    // Parent of the ConditionList, MsgMapList and Timer menus.
    void* list_menu_root;
    std::uint8_t _unk70[0x10];
    // DETACH_FN calls virtual +0x88 when nonnull.
    void* menu_80;
    std::uint8_t _unk88[0x10];
    // EventFlagMan's constructor attaches its flag-editor rows here.
    void* event_flag_menu_root;
    std::uint8_t _unk_a0[4];
    // Initialized -1; another value requests a WorldRes-backed debug marker.
    std::int32_t map_marker_id;
    // Nonzero enables condition geometry, including the low-ID predicate.
    std::uint8_t draw_all_conditions;
    std::uint8_t _unk_a9[0x0f];
    // Positive entity ID used to resolve character, object and region lines.
    std::int32_t draw_target_id;
    std::uint8_t _unk_bc[0x0c];
    // Initialized -1; nonnegative selects an object's dummy polygon.
    std::int32_t draw_dummy_poly_id;
    // Initialized -1; display checks it against the condition-list count.
    std::int32_t condition_cursor;
    // Initialized -1; display checks it against the dispatcher message count.
    std::int32_t message_cursor;
    // Expected boolean. The attach-step consumer suppresses Lua update only for
    // exactly 1; the main frame branch treats any nonzero value as set.
    std::uint8_t suppress_event_update;
    std::uint8_t draw_target_lines;
    std::uint8_t draw_dummy_poly;
    std::uint8_t _unk_d7;
    // The debug integer editor constrains this to 0..3; initialized zero.
    std::int32_t message_option;
    std::uint8_t _unk_dc[0x1c];

    static constexpr std::size_t SIZE = 0xf8;
    static constexpr Rva SINGLETON_PTR = SPRJ_DBG_EVENT_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4931740};
    // Initializes EventState and allocates/initializes this singleton if absent.
    static constexpr Rva EVENT_STATE_CONSTRUCTOR_FN{0x13999f0};
    static constexpr Rva FREE_SINGLETON_FN{0x1399b50};
    static constexpr Rva STARTUP_OWNER_CONSTRUCTOR_FN{0x156c6b0};
    static constexpr Rva STARTUP_OWNER_DESTRUCTOR_FN{0x156ca40};
    static constexpr Rva LUA_BIND_FN{0x13129c0};
    static constexpr Rva LUA_RESET_FN{0x1312900};
    static constexpr Rva DETACH_FN{0x1396500};
    static constexpr Rva BUILD_LIST_MENUS_FN{0x13965c0};
    static constexpr Rva LIST_MENU_ACTION_FN{0x1397150};
    static constexpr Rva LIST_MENU_DISPLAY_FN{0x1397390};
    // Allocates a 0x70 Distance row and six 0x30 value editors (all value and
    // condition pointers borrowed); returns the row stored at +0x38.
    static constexpr Rva ADD_DISTANCE_EDITOR_FN{0x13979e0};
    static constexpr Rva ADD_SIMPLE_TALK_EDITOR_FN{0x1398000};
    static constexpr Rva ADD_REACTION_EDITOR_FN{0x1398620};
    static constexpr Rva DRAW_FN{0x1398840};
    static constexpr Rva DRAW_MAP_MARKER_FN{0x133b190};
    static constexpr Rva LOW_ID_DRAW_PREDICATE_FN{0x13964d0};
    static constexpr Rva UPDATE_SUPPRESSION_GETTER_FN{0x13964f0};
    static constexpr Rva EVENT_ATTACH_STEP_FN{0x1393fd0};
    static constexpr Rva MAIN_FRAME_CONSUMER_FN{0x193ac10};
    static constexpr Rva EVENT_FLAG_MENU_CONSUMER_FN{0x13b5910};

    // The exact signed native predicate; negative IDs are not rejected here.
    constexpr bool draws_low_id_condition(std::int32_t event_id) const {
        return event_id <= 15999 && draw_all_conditions != 0;
    }
    // Gate at EVENT_ATTACH_STEP_FN only, assuming the Lua manager exists: the
    // byte XOR 1 suppresses exactly the value 1.
    constexpr bool attach_step_allows_lua_update() const { return suppress_event_update != 1; }
};

namespace detail::dbg_event_layout {
BB_SIZE(SprjDbgEvent, SprjDbgEvent::SIZE);
static_assert(alignof(SprjDbgEvent) == 8, "alignof(SprjDbgEvent)");
BB_OFFSET(SprjDbgEvent, event_dispatcher, 0);
BB_OFFSET(SprjDbgEvent, conditions, 8);
BB_OFFSET(SprjDbgEvent, menu_20, 0x20);
BB_OFFSET(SprjDbgEvent, condition_editor_root, 0x30);
BB_OFFSET(SprjDbgEvent, last_condition_editor, 0x38);
BB_OFFSET(SprjDbgEvent, condition_list_menu, 0x48);
BB_OFFSET(SprjDbgEvent, message_map_menu, 0x50);
BB_OFFSET(SprjDbgEvent, timer_menu, 0x58);
BB_OFFSET(SprjDbgEvent, menu_60, 0x60);
BB_OFFSET(SprjDbgEvent, list_menu_root, 0x68);
BB_OFFSET(SprjDbgEvent, menu_80, 0x80);
BB_OFFSET(SprjDbgEvent, event_flag_menu_root, 0x98);
BB_OFFSET(SprjDbgEvent, map_marker_id, 0xa4);
BB_OFFSET(SprjDbgEvent, draw_all_conditions, 0xa8);
BB_OFFSET(SprjDbgEvent, draw_target_id, 0xb8);
BB_OFFSET(SprjDbgEvent, draw_dummy_poly_id, 0xc8);
BB_OFFSET(SprjDbgEvent, condition_cursor, 0xcc);
BB_OFFSET(SprjDbgEvent, message_cursor, 0xd0);
BB_OFFSET(SprjDbgEvent, suppress_event_update, 0xd4);
BB_OFFSET(SprjDbgEvent, draw_target_lines, 0xd5);
BB_OFFSET(SprjDbgEvent, draw_dummy_poly, 0xd6);
BB_OFFSET(SprjDbgEvent, message_option, 0xd8);
}  // namespace detail::dbg_event_layout

}  // namespace bb
