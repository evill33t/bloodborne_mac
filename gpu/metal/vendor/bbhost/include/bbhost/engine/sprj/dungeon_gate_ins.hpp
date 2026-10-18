// Dungeon-gate (chalice altar) layout, menu requests, and ritual-mode routing.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/dungeon_ritual_helper.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Row allocated with stride 0x150, alignment eight. The search updater retains
// the channel record and creates a retained label; teardown atomically
// unreferences both through their object +8 counts. The record-derived cache
// is opaque.
struct DungeonGateSearchResult {
    // Request start index plus this row's index, written by RVA 0x1a9e130.
    std::int32_t result_index;
    std::uint32_t _unk04;
    void* channel_record;
    std::uint8_t _record_cache10[0x138];
    void* retained_label;
};

// 0x30-byte search owner at gate +0x4d8 and +0x528. Update RVA 0x1a9e130 polls
// retained_request +0xb0 (1 = pending), builds rows from its record array, then
// unreferences and clears the request. Clear/destruction unreferences rows and
// frees vector storage through its allocator. The request's class is unknown.
struct DungeonGateSearchResults {
    void* retained_request;
    std::uint64_t _unk08;
    DLVector<DungeonGateSearchResult> rows;
};

// Named callback indices from registration RVA 0x1abf510. Finish requests -1;
// -1 itself is a terminal sentinel, not a named callback.
enum class SprjDungeonGateStep : std::int32_t {
    Init = 0,
    InitForChannelLoad = 1,
    UpdateForChannelLoad = 2,
    FinishForChannelLoad = 3,
    ModeSwitch = 4,
    InitForDispError = 5,
    UpdateForDispError = 6,
    FinishForDispError = 7,
    InitForNoHolygrail = 8,
    UpdateForNoHolygrail = 9,
    FinishForNoHolygrail = 10,
    InitForLocalRitual = 11,
    UpdateForLocalRitual = 12,
    FinishForLocalRitual = 13,
    InitForServerRitual = 14,
    UpdateForServerRitual = 15,
    FinishForServerRitual = 16,
    InitForLocalDungeon = 17,
    UpdateForLocalDungeon = 18,
    FinishForLocalDungeon = 19,
    InitForServerDungeon = 20,
    UpdateForServerDungeon = 21,
    FinishForServerDungeon = 22,
    Finish = 23,
};

// Whether `raw` names a callback (0..23); -1 and anything else do not.
constexpr bool sprj_dungeon_gate_step_is_valid(std::int32_t raw) { return raw >= 0 && raw <= 23; }

// The Init step ModeSwitch selects for a ritual status.
constexpr SprjDungeonGateStep sprj_dungeon_gate_step_for_ritual_status(DungeonRitualStatus status) {
    switch (status) {
        case DungeonRitualStatus::Suspended: return SprjDungeonGateStep::InitForChannelLoad;
        case DungeonRitualStatus::Empty: return SprjDungeonGateStep::InitForNoHolygrail;
        case DungeonRitualStatus::LocalIncomplete: return SprjDungeonGateStep::InitForLocalRitual;
        case DungeonRitualStatus::ServerIncomplete: return SprjDungeonGateStep::InitForServerRitual;
        case DungeonRitualStatus::LocalReady: return SprjDungeonGateStep::InitForLocalDungeon;
        case DungeonRitualStatus::ServerReady: return SprjDungeonGateStep::InitForServerDungeon;
        case DungeonRitualStatus::Error: return SprjDungeonGateStep::InitForDispError;
    }
    return SprjDungeonGateStep::InitForDispError;
}

// Reflected 0x6a8-byte local step, aligned to eight. Allocation at RVA
// 0x1ac3aa0 creates seven separately allocated gates for banks 0..6. Owns the
// inline ritual helper, conditional callback task, menu registrations and two
// channel-search result sets. Construction binds the task owner to this
// address, so a live instance must not move.
//
// Menu callbacks can accept a helper request before asynchronous work
// finishes; the outer step retires its mode's menus and returns to ModeSwitch,
// which drives an outstanding helper command before selecting the next mode.
struct SprjDungeonGateIns {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    std::uint8_t _unk59[7];
    void* allocator;
    std::uint8_t debug_flags[2];
    std::uint8_t _unk6a[6];
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    // Only refreshed when step debugging is enabled; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    std::uint8_t _unkc1[3];
    std::int32_t debug_step;
    // Constructor arms group 5 for banks below seven. The callback dispatches
    // this gate and sets value_18 back to one, retaining the next update.
    SprjCallbackTask38 update_task;
    std::uint32_t altar_bank;
    std::uint32_t _unk104;
    CSDungeonRitualHelper ritual_helper;
    // Menu request result used to leave the current mode. Alone it does not
    // establish helper completion, publication or a successful warp.
    std::uint8_t mode_change_requested;
    std::uint8_t _unk379[7];
    void* gate_menu;
    void* action_menu;
    void* suspend_resume_menu;
    void* setup_menu;
    DungeonRitualSetup ritual_setup;
    void* release_dungeon_menu;
    void* play_ritual_menu;
    void* conditional_search_menu;
    // Ten forSearchResultIdxN registrations, paired with owned menu entries.
    void* search_result_menus[10];
    void* search_result_entries[10];
    DungeonGateSearchResults conditional_search;
    // Exposed as a 0..90 value by the conditional-search menu.
    std::uint32_t search_value508;
    std::uint32_t search_value50c;
    std::int32_t search_value510;
    std::int32_t search_value514;
    std::uint8_t search_flag518;
    std::uint8_t _unk519[3];
    std::uint32_t search_value51c;
    void* keyword_search_menu;
    DungeonGateSearchResults keyword_search;
    // Constructed by RVA 0x29033c0; used by the keyword text-entry path.
    Unknown<0xd0> _text_entry558;
    std::uint32_t text_entry_state_raw;
    std::uint32_t text_entry_result_raw;
    Unknown<0x38> _text_entry_string630;
    // Eight editable UTF-16 characters plus the constructor-zeroed terminator.
    // Update copies at most eight characters from the text-entry result.
    std::uint16_t search_keyword[9];
    std::uint8_t _unk67a[6];
    void* random_search_menu;
    std::uint32_t random_search_value688;
    std::uint32_t _unk68c;
    void* share_level_menu;
    void* error_menu;
    // Error-mode Init copies ritual_helper.error_raw here; initially -1.
    std::int32_t displayed_error_raw;
    std::uint32_t _unk6a4;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_DUNGEON_GATE_INS_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_DUNGEON_GATE_INS_TEMPLATE;
    static constexpr std::size_t SIZE = 0x6a8;
    static constexpr Rva VTABLE{0x5336570};
    static constexpr Rva CONSTRUCTOR_FN{0x1ab4370};
    static constexpr Rva DESTRUCTOR_FN{0x1ab4db0};
    static constexpr Rva SIZE_GETTER_FN{0x1ac23c0};
    static constexpr Rva ALLOCATE_BANKS_FN{0x1ac3aa0};
    static constexpr Rva TASK_VTABLE{0x5399d00};
    static constexpr Rva TASK_CALLBACK_FN{0x1ab4d60};
    // Literal group passed by the constructor; its setter forwards unchanged.
    static constexpr std::uint32_t CONSTRUCTOR_TASK_GROUP = 5;
    static constexpr Rva DISPATCH_FN{0x1ac1ac0};
    static constexpr Rva DISPATCH_BODY_FN{0x1ac1780};
    static constexpr Rva ADVANCE_FN{0x1ac3000};
    static constexpr Rva SEARCH_UPDATE_FN{0x1a9e130};
    static constexpr Rva SEARCH_CLEAR_FN{0x1a9dd80};
    static constexpr Rva SEARCH_DESTRUCTOR_FN{0x1a9dc20};
    static constexpr Rva CONDITIONAL_SEARCH_CLEANUP_FN{0x1ab6f90};
    static constexpr Rva SETUP_MENU_CALLBACK_FN{0x1ab9270};
    static constexpr Rva PLAY_RITUAL_CALLBACK_FN{0x1abb290};
    static constexpr Rva SUSPEND_RESUME_CALLBACK_FN{0x1ab9020};

    // The current callback index; false when it is -1 or out of range.
    bool step(SprjDungeonGateStep* out) const {
        if (!sprj_dungeon_gate_step_is_valid(current_step)) return false;
        *out = static_cast<SprjDungeonGateStep>(current_step);
        return true;
    }
    // Snapshot of ModeSwitch's routing decision without running the helper.
    // ModeSwitch first dispatches a nonzero current command, so a later
    // snapshot can differ. False while a command is current or for an unknown
    // status. Does not test whether the outer step is ModeSwitch.
    bool mode_switch_target(SprjDungeonGateStep* out) const {
        if (ritual_helper.current_command_raw != 0) return false;
        if (ritual_helper.status_raw > 6) return false;
        *out = sprj_dungeon_gate_step_for_ritual_status(static_cast<DungeonRitualStatus>(ritual_helper.status_raw));
        return true;
    }
    bool is_finished() const { return current_step == -1; }
};

namespace detail::dungeon_gate_ins_layout {
BB_SIZE(SprjDungeonGateIns, SprjDungeonGateIns::SIZE);
static_assert(alignof(SprjDungeonGateIns) == 8, "alignof(SprjDungeonGateIns)");
BB_OFFSET(SprjDungeonGateIns, callback_table, 0x8);
BB_OFFSET(SprjDungeonGateIns, current_step, 0x50);
BB_OFFSET(SprjDungeonGateIns, requested_step, 0x54);
BB_OFFSET(SprjDungeonGateIns, continue_this_update, 0x58);
BB_OFFSET(SprjDungeonGateIns, allocator, 0x60);
BB_OFFSET(SprjDungeonGateIns, debug_flags, 0x68);
BB_OFFSET(SprjDungeonGateIns, debug_menu, 0x70);
BB_OFFSET(SprjDungeonGateIns, execution_counts, 0xb0);
BB_OFFSET(SprjDungeonGateIns, execution_label, 0xb8);
BB_OFFSET(SprjDungeonGateIns, debug_step_requested, 0xc0);
BB_OFFSET(SprjDungeonGateIns, debug_step, 0xc4);
BB_OFFSET(SprjDungeonGateIns, update_task, 0xc8);
BB_OFFSET(SprjDungeonGateIns, altar_bank, 0x100);
BB_OFFSET(SprjDungeonGateIns, ritual_helper, 0x108);
BB_OFFSET(SprjDungeonGateIns, mode_change_requested, 0x378);
BB_OFFSET(SprjDungeonGateIns, gate_menu, 0x380);
BB_OFFSET(SprjDungeonGateIns, action_menu, 0x388);
BB_OFFSET(SprjDungeonGateIns, suspend_resume_menu, 0x390);
BB_OFFSET(SprjDungeonGateIns, setup_menu, 0x398);
BB_OFFSET(SprjDungeonGateIns, ritual_setup, 0x3a0);
BB_OFFSET(SprjDungeonGateIns, release_dungeon_menu, 0x420);
BB_OFFSET(SprjDungeonGateIns, play_ritual_menu, 0x428);
BB_OFFSET(SprjDungeonGateIns, conditional_search_menu, 0x430);
BB_OFFSET(SprjDungeonGateIns, search_result_menus, 0x438);
BB_OFFSET(SprjDungeonGateIns, search_result_entries, 0x488);
BB_OFFSET(SprjDungeonGateIns, conditional_search, 0x4d8);
BB_OFFSET(SprjDungeonGateIns, search_value508, 0x508);
BB_OFFSET(SprjDungeonGateIns, search_value50c, 0x50c);
BB_OFFSET(SprjDungeonGateIns, search_value510, 0x510);
BB_OFFSET(SprjDungeonGateIns, search_value514, 0x514);
BB_OFFSET(SprjDungeonGateIns, search_flag518, 0x518);
BB_OFFSET(SprjDungeonGateIns, search_value51c, 0x51c);
BB_OFFSET(SprjDungeonGateIns, keyword_search_menu, 0x520);
BB_OFFSET(SprjDungeonGateIns, keyword_search, 0x528);
BB_OFFSET(SprjDungeonGateIns, _text_entry558, 0x558);
BB_OFFSET(SprjDungeonGateIns, text_entry_state_raw, 0x628);
BB_OFFSET(SprjDungeonGateIns, text_entry_result_raw, 0x62c);
BB_OFFSET(SprjDungeonGateIns, _text_entry_string630, 0x630);
BB_OFFSET(SprjDungeonGateIns, search_keyword, 0x668);
BB_OFFSET(SprjDungeonGateIns, random_search_menu, 0x680);
BB_OFFSET(SprjDungeonGateIns, random_search_value688, 0x688);
BB_OFFSET(SprjDungeonGateIns, share_level_menu, 0x690);
BB_OFFSET(SprjDungeonGateIns, error_menu, 0x698);
BB_OFFSET(SprjDungeonGateIns, displayed_error_raw, 0x6a0);
BB_SIZE(DungeonGateSearchResults, 0x30);
static_assert(alignof(DungeonGateSearchResults) == 8, "alignof(DungeonGateSearchResults)");
BB_OFFSET(DungeonGateSearchResults, rows, 0x10);
BB_SIZE(DungeonGateSearchResult, 0x150);
static_assert(alignof(DungeonGateSearchResult) == 8, "alignof(DungeonGateSearchResult)");
BB_OFFSET(DungeonGateSearchResult, result_index, 0);
BB_OFFSET(DungeonGateSearchResult, channel_record, 8);
BB_OFFSET(DungeonGateSearchResult, retained_label, 0x148);

// The registered callback for each step index.
constexpr std::uint64_t kGateCallbacks[24] = {
    0x1ab5370, 0x1ab5390, 0x1ab5450, 0x1ab5470, 0x1ab5490, 0x1ab55b0, 0x1ab56a0, 0x1ab56d0,
    0x1ab5750, 0x1ab6bf0, 0x1ab6c90, 0x1ab71c0, 0x1ab7570, 0x1ab75a0, 0x1ab7740, 0x1ab7880,
    0x1ab78b0, 0x1ab7a50, 0x1ab7b90, 0x1ab7bc0, 0x1ab7d00, 0x1ab7e40, 0x1ab7e70, 0x1ab7fb0,
};
constexpr bool gate_callbacks_match() {
    for (std::size_t i = 0; i < 24; ++i)
        if (SprjDungeonGateIns::STEP_TEMPLATE.steps[i].callback.rva != kGateCallbacks[i]) return false;
    return true;
}
static_assert(SprjDungeonGateIns::STEP_TEMPLATE.steps_count == 24, "SprjDungeonGateIns step count");
static_assert(gate_callbacks_match(), "SprjDungeonGateIns step callbacks");
}  // namespace detail::dungeon_gate_ins_layout

}  // namespace bb
