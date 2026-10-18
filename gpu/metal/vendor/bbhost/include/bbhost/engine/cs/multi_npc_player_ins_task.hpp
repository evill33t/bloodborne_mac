// CSMultiNPCPlayerInsTask: the NPC-player summon and return workflow owned by
// CSMultiPlayMan.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Eight named callbacks in native registration order. -1 is terminal.
enum class CSMultiNPCPlayerInsTaskStep : std::int32_t {
    Init = 0,
    SummonMsgWait = 1,
    SummonWait = 2,
    Summon = 3,
    Update = 4,
    ReturnWait = 5,
    Return = 6,
    Finish = 7,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSMultiNPCPlayerInsTaskStep& out) {
    if (value < 0 || value > 7) return false;
    out = static_cast<CSMultiNPCPlayerInsTaskStep>(value);
    return true;
}

// Descriptive view of the constructor input, consumed through byte +0x28. Its
// extent includes three trailing alignment bytes; a larger native allocation
// or reflected class is not claimed. The constructor copies the fields and
// retains no pointer to this caller-owned descriptor.
struct CSMultiNPCPlayerCreateInfo {
    ChrHandle chr_handle;
    // Sign-extended by native descriptor-table readers; kept raw.
    std::int8_t session_type_raw;
    Unknown<3> _unk05;
    float position[3];
    float rotation[3];
    std::int32_t init_event_flag;
    std::int32_t end_event_flag;
    std::uint8_t flags;
    Unknown<3> _unk29;
};

// Reflected 0x130-byte SprjStepLocal subclass allocated aligned to eight by
// CSMultiPlayMan's ensure function. Identity is a ChrHandle; a repeated ensure
// leaves the existing task, descriptor, timers, and flags unchanged.
//
// The observed manager path supplies mode 1. Modes remain raw because native
// branches also distinguish 0/2 without establishing their full contracts.
// Summon resolves/configures an existing actor and applies the stored
// transform. Return resets that actor's summon state and sets end_event_flag
// when valid; deleting this task only tears down its local-step/debug state.
//
// The manager dispatches through vtable +0xd0 and destroys/frees the task after
// +0x20 reports committed step -1. There is no inline scheduling task or owned
// actor pointer.
struct CSMultiNPCPlayerInsTask {
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
    // Updated conditionally by the native debug dispatcher; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    ChrHandle chr_handle;
    std::uint32_t mode;
    std::int8_t session_type_raw;
    Unknown<3> _unkd1;
    float position[3];
    float rotation[3];
    // Carried from the descriptor; presentation-state setup also receives it.
    std::int32_t init_event_flag;
    // Return sets this flag only after successfully resolving the actor.
    std::int32_t end_event_flag;
    Unknown<4> _unkf4;
    FD4Time summon_message_timer;
    FD4Time summon_timer;
    FD4Time return_timer;
    // Native byte, including unknown/preserved bits. See the masks below.
    std::uint8_t lifecycle_flags;
    Unknown<7> _unk129;

    static constexpr std::size_t SIZE = 0x130;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_MULTI_NPC_PLAYER_INS_TASK_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_MULTI_NPC_PLAYER_INS_TASK_TEMPLATE;
    static constexpr Rva VTABLE{0x534f470};
    static constexpr Rva CONSTRUCTOR_FN{0x1e4ae90};
    static constexpr Rva DESTRUCTOR_FN{0x1e4d320};
    static constexpr Rva SIZE_GETTER_FN{0x1e4e7c0};
    static constexpr Rva DISPATCH_FN{0x1e4dec0};
    static constexpr Rva IS_FINISHED_FN{0x1e4d430};
    static constexpr Rva ADVANCE_STEP_FN{0x1e4f130};
    static constexpr Rva REQUEST_CONTINUATION_FN{0x1e4d540};
    static constexpr Rva INIT_FN{0x1e4b170};
    static constexpr Rva SUMMON_MSG_WAIT_FN{0x1e4b1b0};
    static constexpr Rva SUMMON_WAIT_FN{0x1e4b230};
    static constexpr Rva SUMMON_FN{0x1e4b440};
    static constexpr Rva UPDATE_FN{0x1e4b620};
    static constexpr Rva RETURN_WAIT_FN{0x1e4b650};
    static constexpr Rva RETURN_FN{0x1e4b830};
    static constexpr Rva FINISH_FN{0x1e4bc00};

    // Registration defaults; construction reads the corresponding globals.
    static constexpr float DEFAULT_SUMMON_MESSAGE_TIME = 10.0f;
    static constexpr float DEFAULT_SUMMON_TIME = 5.0f;
    static constexpr float DEFAULT_RETURN_TIME = 20.0f;
    // Clears the first two timers at Init and skips summon presentation paths.
    static constexpr std::uint8_t SKIP_SUMMON_PRESENTATION = 0x01;
    // Constructor sets this; Summon clears it. Required by modes 1/2 to leave
    // SummonWait, in addition to the direction and actor-readiness checks.
    static constexpr std::uint8_t SUMMON_READY = 0x02;
    static constexpr std::uint8_t RETURN_REQUESTED = 0x04;
    // Permits ReturnWait's leave-direction branch; other native gates still apply.
    static constexpr std::uint8_t RETURN_DIRECTION_ENABLED = 0x10;
    static constexpr std::uint8_t CONSTRUCTOR_PRESERVED_FLAGS = 0xe0;

    // True and `out` set when current_step names a registered step.
    bool step(CSMultiNPCPlayerInsTaskStep& out) const { return from_raw(current_step, out); }
    bool is_finished() const { return current_step == -1; }
    bool return_requested() const { return (lifecycle_flags & RETURN_REQUESTED) != 0; }

    // Exact byte composition, without native construction or flag validation.
    // Descriptor bits are ORed without masking, including its high bits.
    static constexpr std::uint8_t constructor_flags(std::uint8_t previous, std::uint8_t descriptor_flags) {
        return static_cast<std::uint8_t>((previous & CONSTRUCTOR_PRESERVED_FLAGS) | descriptor_flags | SUMMON_READY);
    }

    // Numeric countdown shared by the first two timers. Negative results clamp
    // to +0; NaN and signed zero survive the native ordered comparison.
    static float clamped_summon_countdown(float remaining, float delta) {
        float next = remaining - delta;
        return next < 0.0f ? 0.0f : next;
    }

    // SummonMsgWait's comparison after subtraction/clamp. NaN advances.
    static bool summon_message_timer_waits(float remaining) { return remaining > 0.0f; }

    // ReturnWait's subtraction branch uses VUCOMISS zero,value / JC, which
    // waits for positive values OR NaN. This predicate does not evaluate the
    // surrounding mode, actor, session, and direction gates.
    static bool return_timer_waits(float remaining) { return remaining > 0.0f || remaining != remaining; }
};

namespace detail::multi_npc_player_ins_task_layout {
using T = CSMultiNPCPlayerInsTask;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSMultiNPCPlayerInsTask)");
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
BB_OFFSET(T, chr_handle, 0xc8);
BB_OFFSET(T, mode, 0xcc);
BB_OFFSET(T, session_type_raw, 0xd0);
BB_OFFSET(T, position, 0xd4);
BB_OFFSET(T, rotation, 0xe0);
BB_OFFSET(T, init_event_flag, 0xec);
BB_OFFSET(T, end_event_flag, 0xf0);
BB_OFFSET(T, summon_message_timer, 0xf8);
BB_OFFSET(T, summon_timer, 0x108);
BB_OFFSET(T, return_timer, 0x118);
BB_OFFSET(T, lifecycle_flags, 0x128);
BB_SIZE(FD4Time, 0x10);
BB_OFFSET(FD4Time, time, 0x08);
BB_SIZE(CSMultiNPCPlayerCreateInfo, 0x2c);
static_assert(alignof(CSMultiNPCPlayerCreateInfo) == 4, "alignof(CSMultiNPCPlayerCreateInfo)");
BB_OFFSET(CSMultiNPCPlayerCreateInfo, chr_handle, 0x00);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, session_type_raw, 0x04);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, position, 0x08);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, rotation, 0x14);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, init_event_flag, 0x20);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, end_event_flag, 0x24);
BB_OFFSET(CSMultiNPCPlayerCreateInfo, flags, 0x28);
static_assert(T::constructor_flags(0xff, 0) == 0xe2, "constructor_flags");
static_assert(T::constructor_flags(0, 1) == 0x03, "constructor_flags");
static_assert(T::constructor_flags(0, 0x90) == 0x92, "constructor_flags");
}  // namespace detail::multi_npc_player_ins_task_layout

}  // namespace bb
