// SprjDungeonMoveMapListStep: the chalice-dungeon map-list selector and move
// requester.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_SIZE = 0x248;
inline constexpr std::size_t SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_MOVE_CONTROLLER_OFFSET = 0xd0;
inline constexpr std::size_t SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_SELECTOR_OFFSET = 0xd8;
inline constexpr std::size_t SPRJ_DUNGEON_MOVE_MAP_LIST_SELECTOR_SIZE = 0x170;

inline constexpr Rva SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_CONSTRUCTOR_FN{0x19625b0};
inline constexpr Rva SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_REGISTER_FN{0x1963530};
inline constexpr Rva SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_SET_STEP_FN{0x1965d40};
inline constexpr Rva SPRJ_DUNGEON_MAP_LIST_SELECTOR_CONSTRUCTOR_FN{0x19e3330};
inline constexpr Rva SPRJ_DUNGEON_MOVE_MAP_REQUEST_FN{0x1928680};

inline constexpr const char* SPRJ_DUNGEON_MOVE_MAP_LOAD_LIST = "map:/MapViewList_forDungeon.loadlistlist";

enum class SprjDungeonMoveMapListStepIndex : std::int32_t {
    Init = 0,
    ListSelect = 1,
    MoveMapWait = 2,
    Finish = 3,
};

// Whether `raw` names one of the four steps.
constexpr bool sprj_dungeon_move_map_list_step_index_is_valid(std::int32_t raw) { return raw >= 0 && raw <= 3; }

// Embedded list UI/bot selector constructed at task offset +0xd8. The two
// inline wide strings are opaque; their values are the dungeon move-map test
// caption and the dungeon map-view load-list path.
struct SprjDungeonMoveMapListSelector {
    Unknown<0xc8> _selector_base;
    Unknown<0x38> _caption;
    std::uint32_t display_mode;
    std::uint32_t entries_per_page;
    float row_spacing;
    float column_spacing;
    void* list_data;
    Unknown<0x38> _load_list_path;
    std::uint32_t cursor_index;
    std::uint32_t page_index;
    std::uint32_t selection_mode;
    std::uint8_t automated_selection;
    std::uint8_t cancel_requested;
    std::uint16_t _pad_15e;
    void* auxiliary_data;
    std::uint32_t auxiliary_state;
    std::uint32_t _pad_16c;
};

// Chalice-specific map-list selector and move requester.
struct SprjDungeonMoveMapListStep {
    Unknown<SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_MOVE_CONTROLLER_OFFSET> _step_task;
    // Shared move-map controller; STEP_MoveMapWait polls its +0x30 request latch.
    void* move_map_controller;
    SprjDungeonMoveMapListSelector selector;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_TEMPLATE;
};

namespace detail::dungeon_move_map_list_step_layout {
static_assert(SprjDungeonMoveMapListStep::STEP_TEMPLATE.steps_count == 4, "SprjDungeonMoveMapListStep step count");
BB_SIZE(SprjDungeonMoveMapListSelector, SPRJ_DUNGEON_MOVE_MAP_LIST_SELECTOR_SIZE);
BB_SIZE(SprjDungeonMoveMapListStep, SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_SIZE);
BB_OFFSET(SprjDungeonMoveMapListStep, move_map_controller, SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_MOVE_CONTROLLER_OFFSET);
BB_OFFSET(SprjDungeonMoveMapListStep, selector, SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_SELECTOR_OFFSET);
BB_OFFSET(SprjDungeonMoveMapListSelector, entries_per_page, 0x104);
BB_OFFSET(SprjDungeonMoveMapListSelector, list_data, 0x110);
BB_OFFSET(SprjDungeonMoveMapListSelector, cursor_index, 0x150);
BB_OFFSET(SprjDungeonMoveMapListSelector, page_index, 0x154);
BB_OFFSET(SprjDungeonMoveMapListSelector, automated_selection, 0x15c);
BB_OFFSET(SprjDungeonMoveMapListSelector, cancel_requested, 0x15d);
}  // namespace detail::dungeon_move_map_list_step_layout

}  // namespace bb
