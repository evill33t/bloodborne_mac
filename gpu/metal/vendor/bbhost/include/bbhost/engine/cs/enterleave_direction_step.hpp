// Bloodborne 1.09 CSEnterleaveDirectionStep layout and runtime metadata.
//
// Constructors confirm the ObjectRef and ChrHandle target variants, the
// 0x138-byte allocation, session-type storage at +0xfc, and the presentation
// timers and control flags used by the registered callbacks.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_SIZE = 0x138;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_BASE_SIZE = 0xd0;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_DIRECTION_OFFSET = 0xd0;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_OBJECT_REF_OFFSET = 0xd8;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_TARGET_CHR_HANDLE_OFFSET = 0xf8;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_SESSION_TYPE_OFFSET = 0xfc;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_EXECUTE_REQUESTED_OFFSET = 0xfd;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_WORLD_TRANSITION_OFFSET = 0xfe;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_ABORT_REQUESTED_OFFSET = 0xff;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_RELEASE_REQUESTED_OFFSET = 0x100;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_PHASE_TIMER_OFFSET = 0x110;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_PRIMARY_AUDIO_OFFSET = 0x118;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_SECONDARY_AUDIO_OFFSET = 0x120;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_OFFSET = 0x128;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_BLEND_OFFSET = 0x130;
inline constexpr std::size_t CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_STATE_OFFSET = 0x134;

// Registers the runtime class, inheritance information, and 17-step table.
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_REGISTER_FN{0x196c760};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_CONSTRUCT_BASE_FN{0x196f070};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_CONSTRUCT_OBJECT_REF_FN{0x196a970};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_CONSTRUCT_CHR_HANDLE_FN{0x196aac0};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_FIND_OR_CREATE_OBJECT_REF_FN{0x19700a0};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_FIND_OR_CREATE_CHR_HANDLE_FN{0x1970310};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_SET_STEP_FN{0x196f1c0};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_ADVANCE_STEP_FN{0x196f360};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_VTABLE{0x5331470};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_SIZE_GETTER_FN{0x196e700};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_DESTRUCTOR_FN{0x196ac20};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_TICK_FN{0x196adc0};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_DISPATCH_FN{0x196de70};
inline constexpr Rva CS_ENTERLEAVE_DIRECTION_STEP_IS_FINISHED_FN{0x196e0c0};

// Direction selector consumed by STEP_Init. The native callback names use C2H
// and H2C, but the selector helpers prove the values distinguish
// arrival/departure and whether the target resolves to WorldChrMan's local
// main player.
enum class CSEnterleaveDirection : std::uint32_t {
    EnterForSelf = 0,
    EnterForOthers = 1,
    LeaveForSelf = 2,
    LeaveForOthers = 3,
};

// Native callback indices in the registered step table.
enum class CSEnterleaveDirectionStepIndex : std::int32_t {
    Init = 0,
    C2hInitForSelf = 1,
    C2hInitForSelfPrologue = 2,
    C2hWaitForSelfPrologue = 3,
    C2hInitForSelfLoading = 4,
    C2hWaitForSelfLoading = 5,
    C2hInitForSelfEpilogue = 6,
    C2hWaitForSelfEpilogue = 7,
    C2hInitForOthers = 8,
    C2hInitForOthersPrologue = 9,
    C2hWaitForOthersPrologue = 10,
    C2hInitForOthersEpilogue = 11,
    C2hWaitForOthersEpilogue = 12,
    H2cInit = 13,
    H2cInitForEffect = 14,  // "STEP_H2C_Init_forEffect"
    H2cWaitForEffect = 15,
    Finish = 16,
};

// Locked ObjectRef wrapper used when a target is supplied by session identity.
struct CSEnterleaveObjectRef {
    void* object;
    void* mutex_vtable;
    Unknown<0x10> _mutex_storage;
};

// Presentation task for a player's arrival or departure.
//
// The common 0xd0 SprjStepTask prefix and concrete tail are proven by both
// native constructors and the registered callbacks. This task resolves an
// existing actor, then sequences its messages, audio, effects, and
// presentation state; it does not create the session member or actor. The
// director schedules each child independently in GameFlowStep (7).
//
// The native tick clears execute_requested before dispatch unless
// world_transition is set, then clears both bytes after dispatch. Finish
// (index 16) writes effect_state=0x0101 and requests -1 only if execution is
// no longer requested or release_requested is set. Current -1, not callback
// index 16, is the native completion predicate.
//
// Destruction stops/releases both audio handles, flags/releases the effect,
// resets the ObjectRef, and destroys the task/debug prefix. Borrowed pointers
// must not outlive native retirement.
struct CSEnterleaveDirectionStep {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_ENTERLEAVE_DIRECTION_STEP_RUNTIME_CLASS;
    // STEP_TEMPLATE: 17 steps
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_ENTERLEAVE_DIRECTION_STEP_TEMPLATE;
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::GameFlowStep;

    SprjStepTaskD0 step_task;
    // Constructors copy the raw u32 without validation. Use direction().
    std::uint32_t direction_raw;
    std::uint32_t _pad_d4;
    CSEnterleaveObjectRef target_object_ref;
    std::uint32_t target_chr_handle;
    std::uint8_t session_type;
    std::uint8_t execute_requested;
    std::uint8_t world_transition;
    std::uint8_t abort_requested;
    std::uint8_t release_requested;
    Unknown<0x07> _pad_101;
    // FD4Time vtable (RVA 0x52efd40). Kept flat to preserve phase_timer.
    const void* phase_timer_vftable;
    float phase_timer;
    std::uint32_t _pad_114;
    void* primary_audio;
    void* secondary_audio;
    void* effect;
    float effect_blend;
    std::uint16_t effect_state;
    std::uint16_t _pad_136;

    bool direction_known() const { return direction_raw <= 3; }
    CSEnterleaveDirection direction() const { return static_cast<CSEnterleaveDirection>(direction_raw); }
    static bool step_known(std::int32_t raw) { return raw >= 0 && raw <= 16; }
    CSEnterleaveDirectionStepIndex current_step() const {
        return static_cast<CSEnterleaveDirectionStepIndex>(step_task.current_step);
    }
    CSEnterleaveDirectionStepIndex requested_step() const {
        return static_cast<CSEnterleaveDirectionStepIndex>(step_task.requested_step);
    }
};

namespace detail::enterleave_direction_step_layout {
using T = CSEnterleaveDirectionStep;
BB_SIZE(CSEnterleaveObjectRef, 0x20);
static_assert(alignof(T) == 8, "alignof(CSEnterleaveDirectionStep)");
BB_SIZE(T, CS_ENTERLEAVE_DIRECTION_STEP_SIZE);
BB_SIZE(SprjStepTaskD0, CS_ENTERLEAVE_DIRECTION_STEP_BASE_SIZE);
BB_OFFSET(T, step_task, 0);
BB_OFFSET(T, phase_timer_vftable, 0x108);
BB_OFFSET(T, direction_raw, CS_ENTERLEAVE_DIRECTION_STEP_DIRECTION_OFFSET);
BB_OFFSET(T, target_object_ref, CS_ENTERLEAVE_DIRECTION_STEP_OBJECT_REF_OFFSET);
BB_OFFSET(T, target_chr_handle, CS_ENTERLEAVE_DIRECTION_STEP_TARGET_CHR_HANDLE_OFFSET);
BB_OFFSET(T, session_type, CS_ENTERLEAVE_DIRECTION_STEP_SESSION_TYPE_OFFSET);
BB_OFFSET(T, execute_requested, CS_ENTERLEAVE_DIRECTION_STEP_EXECUTE_REQUESTED_OFFSET);
BB_OFFSET(T, world_transition, CS_ENTERLEAVE_DIRECTION_STEP_WORLD_TRANSITION_OFFSET);
BB_OFFSET(T, abort_requested, CS_ENTERLEAVE_DIRECTION_STEP_ABORT_REQUESTED_OFFSET);
BB_OFFSET(T, release_requested, CS_ENTERLEAVE_DIRECTION_STEP_RELEASE_REQUESTED_OFFSET);
BB_OFFSET(T, phase_timer, CS_ENTERLEAVE_DIRECTION_STEP_PHASE_TIMER_OFFSET);
BB_OFFSET(T, primary_audio, CS_ENTERLEAVE_DIRECTION_STEP_PRIMARY_AUDIO_OFFSET);
BB_OFFSET(T, secondary_audio, CS_ENTERLEAVE_DIRECTION_STEP_SECONDARY_AUDIO_OFFSET);
BB_OFFSET(T, effect, CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_OFFSET);
BB_OFFSET(T, effect_blend, CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_BLEND_OFFSET);
BB_OFFSET(T, effect_state, CS_ENTERLEAVE_DIRECTION_STEP_EFFECT_STATE_OFFSET);
}  // namespace detail::enterleave_direction_step_layout

}  // namespace bb
