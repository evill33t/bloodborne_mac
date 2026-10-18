// CSNetworkFlowStep: native network-flow monitoring embedded in TitleStep.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Only the four named registration entries are classified. Raw -1 is the
// terminal state; broader bounds in generic debug code are not extra steps.
enum class CSNetworkFlowStepIndex : std::int32_t {
    Init = 0,
    OfflineMode = 1,
    OnlineMode = 2,
    Finish = 3,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSNetworkFlowStepIndex& out) {
    if (value < 0 || value > 3) return false;
    out = static_cast<CSNetworkFlowStepIndex>(value);
    return true;
}

// Descriptive name for the owned 0x28-byte message-ID history wrapper.
// Constructor allocates it with alignment eight. The first word is unproven;
// begin/end/capacity/allocator at +8/+0x10/+0x18/+0x20 form a DLVector<i32>.
// Native processing appends previously unseen IDs from FrpgNetMan's tree.
// Destruction frees the buffer through its allocator, then the wrapper through
// its owning heap. This is not an additional reflected runtime class.
struct CSNetworkNoticeHistory {
    Unknown<8> _unk00;
    DLVector<std::int32_t> ids;

    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva PROCESS_NOTICES_FN{0x1e56190};
};

// Reflected 0x110-byte SprjStepLocal subclass embedded at TitleStep +0xb0.
// Constructor RVA 0x1959fb0 calls its constructor at that address; reflected
// size getter RVA 0x1e5a360 independently returns 0x110.
//
// Start resets the notice history, requests Init, clears online_update_enabled,
// and registers the inline task in TaskLineIdx_NetworkFlowStep (12). TitleStep
// also dispatches this local step directly and rearms the task. The task's
// owner points back into this object, so a live native instance must not move.
//
// OnlineMode monitors native network state and dispatches Lua/UI
// notifications; it does not create multiplayer actors. Its byte gate is only
// one of several external conditions. Finish requests -1 without unregistering
// the task. Destruction frees the history and unregisters the callback before
// base teardown.
struct CSNetworkFlowStep {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    // Refreshed only while native debug flags permit it; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    // Owned 0x28-byte wrapper; its vector contains message IDs, not tasks.
    CSNetworkNoticeHistory* notice_history;
    // Conditional task; +0x18 is rearmed to 1 after native dispatch.
    SprjCallbackTask38 update_task;
    // Written by TitleStep from its active field-transition child, or cleared
    // during other title phases. Nonzero permits OnlineMode's outer gate.
    std::uint8_t online_update_enabled;
    // Last observed normalized (FrpgNetMan +0xa05 & 2) >> 1. Constructor
    // clears it; Start does not. Only transitions to set invoke the notice helper.
    std::uint8_t previous_network_status_bit_1;
    Unknown<6> _unk10a;

    static constexpr std::size_t SIZE = 0x110;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_NETWORK_FLOW_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_NETWORK_FLOW_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x534f840};
    static constexpr Rva CONSTRUCTOR_FN{0x1e56c30};
    static constexpr Rva DESTRUCTOR_FN{0x1e56f10};
    static constexpr Rva SIZE_GETTER_FN{0x1e5a360};
    static constexpr Rva START_FN{0x1e57040};
    static constexpr Rva DISPATCH_FN{0x1e599c0};
    static constexpr Rva IS_FINISHED_FN{0x1e58f40};
    static constexpr Rva SET_STEP_FN{0x1e58f50};
    static constexpr Rva ADVANCE_STEP_FN{0x1e5ad80};
    static constexpr Rva REQUEST_CONTINUATION_FN{0x1e59050};
    static constexpr Rva INIT_FN{0x1e57140};
    static constexpr Rva OFFLINE_MODE_FN{0x1e57170};
    static constexpr Rva ONLINE_MODE_FN{0x1e57210};
    static constexpr Rva FINISH_FN{0x1e57860};
    static constexpr Rva TASK_VTABLE{0x53a8440};
    static constexpr Rva TASK_CALLBACK_FN{0x1e56ec0};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::TaskLineIdxNetworkFlowStep;
    static constexpr std::size_t TITLE_STEP_OFFSET = 0xb0;
    static constexpr Rva TITLE_STEP_CONSTRUCTOR_FN{0x1959fb0};
    static constexpr Rva TITLE_STEP_START_FLOW_FN{0x195b480};
    static constexpr Rva TITLE_STEP_UPDATE_FLOW_FN{0x195b6a0};

    // True and `out` set when current_step names a registered step.
    bool step(CSNetworkFlowStepIndex& out) const { return from_raw(current_step, out); }
    // Native predicate checks committed state, not a pending Finish request.
    bool is_finished() const { return current_step == -1; }
    // Reads only this object's gate, without evaluating external native state.
    bool online_updates_enabled() const { return online_update_enabled != 0; }
};

namespace detail::network_flow_step_layout {
using T = CSNetworkFlowStep;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSNetworkFlowStep)");
BB_OFFSET(T, callback_table, 0x08);
BB_OFFSET(T, _condition_dispatcher10, 0x10);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, requested_step, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _debug_string78, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
BB_OFFSET(T, notice_history, 0xc8);
BB_OFFSET(T, update_task, 0xd0);
BB_OFFSET(T, online_update_enabled, 0x108);
BB_OFFSET(T, previous_network_status_bit_1, 0x109);
static_assert(T::TITLE_STEP_OFFSET + sizeof(T) == 0x1c0, "TitleStep embedding");
static_assert(static_cast<std::uint32_t>(T::TASK_GROUP) == 12, "CSNetworkFlowStep::TASK_GROUP");
BB_SIZE(CSNetworkNoticeHistory, CSNetworkNoticeHistory::SIZE);
static_assert(alignof(CSNetworkNoticeHistory) == 8, "alignof(CSNetworkNoticeHistory)");
BB_OFFSET(CSNetworkNoticeHistory, ids, 0x08);
BB_SIZE(SprjCallbackTask38, 0x38);
BB_OFFSET(T, update_task.registration, 0xe0);
BB_OFFSET(T, update_task.value_18, 0xe8);
BB_OFFSET(T, update_task.owner, 0xf0);
BB_OFFSET(T, update_task.callback, 0xf8);
BB_OFFSET(T, update_task.this_adjustment, 0x100);
}  // namespace detail::network_flow_step_layout

}  // namespace bb
