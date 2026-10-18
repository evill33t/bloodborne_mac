// Native LAMS message/talk capture task and its worker handoff.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct CSLamsCaptureThread;

// Named callbacks in registration order. Stored indices remain raw integers;
// -1 is terminal and is not a named callback.
enum class CSLamsCaptureStepIndex : std::int32_t {
    Initialize = 0,
    WaitInput = 1,
    InitializeStdin = 2,
    WaitStdin = 3,
    InitializeMsgCapture = 4,
    SetMsg = 5,
    UpdateMsg = 6,
    InitializeTalk = 7,
    SetTalk = 8,
    UpdateTalk = 9,
    Finalize = 10,
};

enum class CSLamsStdinMode : std::int32_t {
    Message = 0,
    Talk = 1,
};

struct CSLamsCaptureIdNode {
    CSLamsCaptureIdNode* next;
    CSLamsCaptureIdNode* previous;
    std::int32_t id;
    Unknown<4> _unk14;
};

// Descriptive native list header. The sentinel is an allocated 0x18-byte node,
// not an item; empty next/previous links point back to it. Native append/pop
// and clear own the nodes.
struct CSLamsCaptureIdList {
    Unknown<8> _unk00;
    CSLamsCaptureIdNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Descriptive 0x18-byte retained adapter allocated by SetMsg. Its native
// reference count is atomic; destruction releases presentation_task. The
// virtual update delegates to that task, converting raw returns 0/1 to 1. The
// outer update releases the adapter after a return greater than 1.
struct CSLamsMessageCapture {
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva VTABLE{0x5357e40};
    static constexpr Rva UPDATE_FN{0x1fa9400};
    static constexpr Rva DESTRUCTOR_FN{0x1fa9340};

    const void* vftable;
    std::int32_t reference_count;
    Unknown<4> _unk0c;
    void* presentation_task;

    static constexpr std::uint32_t forwarded_update_result(std::uint32_t result) { return result > 1 ? result : 1; }
};

// Descriptive inline 0x30-byte talk controller, not a reflected class claim.
// Start selects a base TalkParam row; subsequent rows use base + index + 1. It
// drives voice playback and a message ID, and publishes active to
// SprjWorldTalkMan +0x3ff8. The eight-byte audio handle remains opaque.
struct CSLamsTalkCapture {
    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva START_FN{0x1fa9ab0};
    static constexpr Rva ADVANCE_FN{0x1fa9c80};
    static constexpr Rva UPDATE_FN{0x1faa150};

    std::int32_t base_talk_id;
    // Starts at -1; incremented after the next TalkParam row is found.
    std::int32_t sequence_index;
    std::uint8_t active;
    std::uint8_t just_started;
    Unknown<2> _unk0a;
    std::int32_t message_id;
    // Byte state at +0x10..+0x14: display gate, post-capture wait, advance
    // request, follow-sequence option, and alternate-audio condition. Meanings
    // beyond the captured start/advance/update branches stay raw.
    std::uint8_t flags[5];
    Unknown<3> _unk15;
    Unknown<8> audio_handle;
    float duration;
    float elapsed;
    // Constructor clears this word; further meaning unproven.
    std::uint32_t value_28;
    Unknown<4> _unk2c;

    bool is_active() const { return active != 0; }
};

// Exact 0x160-byte SprjStepTask subclass, allocated aligned to eight by startup
// RVA 0x201b0e0 and registered in DbgDispStep (66). Its 0xd0-byte base differs
// from SprjStepLocal: table/current/next/continuation are at
// +0x10/+0x58/+0x5c/+0x60. The task registration lives in the startup owner.
//
// Debug actions start full scans or stdin capture. WaitInput/WaitStdin are
// no-ops; the stdin worker can write requested_step and capture state directly
// (no cross-thread synchronization here). Finalize clears capture
// state/queues and requests -1. Destruction also destroys the owned thread
// (waiting for work), frees list sentinels, and tears down task/debug state.
struct CSLamsCaptureStep {
    // RUNTIME_CLASS: probable base "SprjStepTask< CSLamsCaptureStep >".
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_LAMS_CAPTURE_STEP_RUNTIME_CLASS;
    // STEP_TEMPLATE: its 11 step callbacks are INITIALIZE_FN..FINALIZE_FN below, in order.
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_LAMS_CAPTURE_STEP_TEMPLATE;
    static constexpr std::size_t SIZE = 0x160;
    static constexpr Rva VTABLE{0x5357c60};
    static constexpr Rva CONSTRUCTOR_FN{0x1fa4c70};
    static constexpr Rva DESTRUCTOR_FN{0x1fa5060};
    static constexpr Rva SIZE_GETTER_FN{0x1fa89d0};
    static constexpr Rva DISPATCH_FN{0x1fa8080};
    static constexpr Rva IS_FINISHED_FN{0x1fa82d0};
    static constexpr Rva CLEAR_CAPTURE_FN{0x1fa5240};
    static constexpr Rva READ_STDIN_FN{0x1fa53b0};
    static constexpr Rva DEBUG_ACTION_FN{0x1fa6480};
    static constexpr Rva APPEND_ID_FN{0x1fa9520};
    static constexpr Rva SET_STEP_FN{0x1fa9610};
    static constexpr Rva ADVANCE_STEP_FN{0x1fa9700};
    static constexpr Rva INITIALIZE_FN{0x1fa59a0};
    static constexpr Rva WAIT_INPUT_FN{0x1fa5ab0};
    static constexpr Rva INITIALIZE_STDIN_FN{0x1fa5ac0};
    static constexpr Rva WAIT_STDIN_FN{0x1fa5c20};
    static constexpr Rva INITIALIZE_MSG_CAPTURE_FN{0x1fa5c30};
    static constexpr Rva SET_MSG_FN{0x1fa5d80};
    static constexpr Rva UPDATE_MSG_FN{0x1fa61c0};
    static constexpr Rva INITIALIZE_TALK_FN{0x1fa6240};
    static constexpr Rva SET_TALK_FN{0x1fa6350};
    static constexpr Rva UPDATE_TALK_FN{0x1fa63f0};
    static constexpr Rva FINALIZE_FN{0x1fa6460};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::DbgDispStep;
    static constexpr float DEFAULT_DISPLAY_DURATION = 4.0f;
    static constexpr std::uint32_t MESSAGE_CATEGORY = 0xcc;

    const void* vftable;
    Unknown<8> _task_state08;
    const void* callback_table;
    Unknown<0x40> _dispatcher18;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk61;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk72;
    void* debug_menu;
    Unknown<0x38> _debug_string80;
    std::int32_t* execution_counts;
    // Conditionally refreshed by the debug-enabled dispatcher; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc9;
    std::int32_t debug_step;
    // Retained wrapper around a message presentation task.
    CSLamsMessageCapture* message_capture;
    CSLamsTalkCapture talk_capture;
    CSLamsCaptureIdList message_ids;
    CSLamsCaptureIdList talk_ids;
    // Created lazily by InitializeStdin; owns an executor and completion holder.
    CSLamsCaptureThread* stdin_thread;
    // Both display-duration settings default to 4.0 in the constructor.
    float message_duration;
    float talk_duration;
    std::int32_t stdin_mode_raw;
    Unknown<4> _unk15c;

    bool step_known() const { return current_step >= 0 && current_step <= 10; }
    CSLamsCaptureStepIndex step() const { return static_cast<CSLamsCaptureStepIndex>(current_step); }
    bool stdin_mode_known() const { return stdin_mode_raw == 0 || stdin_mode_raw == 1; }
    CSLamsStdinMode stdin_mode() const { return static_cast<CSLamsStdinMode>(stdin_mode_raw); }
    // Native predicate tests committed state, not the requested index.
    bool is_finished() const { return current_step == -1; }
    // Numeric filter used for ATT_LOC: message input. Full message scans also
    // check that MsgRepository category 0xcc actually contains each ID.
    static constexpr bool accepts_stdin_message_id(std::uint32_t id) {
        return id - 4000u < 1000u || id - 401000u < 1000u;
    }
    // Native 32-bit addition before a TalkParam existence lookup. Not
    // validation: the worker proceeds only if that resulting row exists.
    static constexpr std::int32_t stdin_talk_id(std::int32_t value) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(value) + 1000000u);
    }
};

namespace detail::lams_capture_step_layout {
using T = CSLamsCaptureStep;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSLamsCaptureStep)");
BB_OFFSET(T, callback_table, 0x10);
BB_OFFSET(T, current_step, 0x58);
BB_OFFSET(T, requested_step, 0x5c);
BB_OFFSET(T, continue_this_update, 0x60);
BB_OFFSET(T, allocator, 0x68);
BB_OFFSET(T, debug_flags, 0x70);
BB_OFFSET(T, debug_menu, 0x78);
BB_OFFSET(T, _debug_string80, 0x80);
BB_OFFSET(T, execution_counts, 0xb8);
BB_OFFSET(T, execution_label, 0xc0);
BB_OFFSET(T, debug_step_requested, 0xc8);
BB_OFFSET(T, debug_step, 0xcc);
BB_OFFSET(T, message_capture, 0xd0);
BB_OFFSET(T, talk_capture, 0xd8);
BB_OFFSET(T, message_ids, 0x108);
BB_OFFSET(T, talk_ids, 0x128);
BB_OFFSET(T, stdin_thread, 0x148);
BB_OFFSET(T, message_duration, 0x150);
BB_OFFSET(T, talk_duration, 0x154);
BB_OFFSET(T, stdin_mode_raw, 0x158);
BB_SIZE(CSLamsCaptureIdList, 0x20);
BB_OFFSET(CSLamsCaptureIdList, sentinel, 0x08);
BB_OFFSET(CSLamsCaptureIdList, count, 0x10);
BB_OFFSET(CSLamsCaptureIdList, allocator, 0x18);
BB_SIZE(CSLamsCaptureIdNode, 0x18);
BB_OFFSET(CSLamsCaptureIdNode, previous, 0x08);
BB_OFFSET(CSLamsCaptureIdNode, id, 0x10);
BB_SIZE(CSLamsMessageCapture, CSLamsMessageCapture::SIZE);
BB_OFFSET(CSLamsMessageCapture, reference_count, 0x08);
BB_OFFSET(CSLamsMessageCapture, presentation_task, 0x10);
BB_SIZE(CSLamsTalkCapture, CSLamsTalkCapture::SIZE);
BB_OFFSET(CSLamsTalkCapture, sequence_index, 0x04);
BB_OFFSET(CSLamsTalkCapture, active, 0x08);
BB_OFFSET(CSLamsTalkCapture, just_started, 0x09);
BB_OFFSET(CSLamsTalkCapture, message_id, 0x0c);
BB_OFFSET(CSLamsTalkCapture, flags, 0x10);
BB_OFFSET(CSLamsTalkCapture, audio_handle, 0x18);
BB_OFFSET(CSLamsTalkCapture, duration, 0x20);
BB_OFFSET(CSLamsTalkCapture, elapsed, 0x24);
BB_OFFSET(CSLamsTalkCapture, value_28, 0x28);
static_assert(T::accepts_stdin_message_id(401999) && !T::accepts_stdin_message_id(5000), "stdin message filter");
static_assert(T::stdin_talk_id(-1) == 999999, "stdin_talk_id");
}  // namespace detail::lams_capture_step_layout

}  // namespace bb
