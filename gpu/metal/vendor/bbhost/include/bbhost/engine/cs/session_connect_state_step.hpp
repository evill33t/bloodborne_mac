// CSSessionConnectStateStep: the native asynchronous leave step owned by
// SprjSessionManager.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Named local-step values, distinct from SprjSessionManager's role values.
// Finished is the committed terminal state, not the STEP_Finish callback.
enum class CSSessionConnectStep : std::int32_t {
    Finished = -1,
    Init = 0,
    Update = 1,
    LeaveSession = 2,
    LeaveSessionWait = 3,
    Finish = 4,
};

// True and `out` set when `value` names a step (including Finished = -1).
inline constexpr bool from_raw(std::int32_t value, CSSessionConnectStep& out) {
    if (value < -1 || value > 4) return false;
    out = static_cast<CSSessionConnectStep>(value);
    return true;
}

// Exact 0xe0-byte SprjStepLocal subclass. BeginLeaveSession allocates it with
// alignment eight and stores it at SprjSessionManager +0x280. Reflected size
// getter RVA 0x1ed58c0 independently confirms its size.
//
// The manager drives virtual +0xd0 directly; there is no inline Sprj task.
// BeginLeaveSession sets request bit 0 only on a newly allocated step.
// ResetToIdle sets bit 1 without freeing the object. Normal manager update
// commits the step transitions and, after current_step reaches -1, invokes
// virtual +0x08 destruction, frees through the owning heap, and clears +0x280.
// A reset/finish request therefore does not establish allocation retirement.
//
// Native cleanup releases local-step conditions, debug registration/menu, and
// string storage; counter reclamation remains unverified.
struct CSSessionConnectStateStep {
    const void* vtable;
    const void* step_table;
    Unknown<0x40> _dispatcher10;
    std::int32_t current_step;
    std::int32_t next_step;
    // When set, the dispatcher may execute another step in the same update.
    std::uint8_t immediate_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    // Optional six-word array allocated by RVA 0x1ed45b0; includes the
    // unnamed table slot, not six named callbacks.
    std::int32_t* execution_counts;
    // Only refreshed when native step debugging is enabled; may be stale.
    const std::uint16_t* debug_step_name;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    // Start of the inline 0x10-byte FD4Time. The float remains exposed at its
    // established field name/offset for existing SPRJ consumers.
    const void* leave_timer_vtable;
    // STEP_LeaveSession initializes 10.0; Wait subtracts frame-context +8.
    // Exactly zero does not expire: the native comparison is strictly < 0.
    float leave_wait_remaining;
    Unknown<4> _unkd4;
    // Bit 0 requests leave; bit 1 lets LeaveSessionWait select Finish. Each
    // consuming callback clears only its own bit. Other bits are opaque.
    std::uint8_t request_flags;
    Unknown<7> _unkd9;

    static constexpr std::size_t SIZE = 0xe0;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_SESSION_CONNECT_STATE_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_SESSION_CONNECT_STATE_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x5350f30};
    static constexpr Rva LOCAL_BASE_CONSTRUCTOR_FN{0x1ed62b0};
    static constexpr Rva DESTRUCTOR_FN{0x1ed4370};
    static constexpr Rva SIZE_GETTER_FN{0x1ed58c0};
    // Virtual +0xd0 thunk to DISPATCH_FN.
    static constexpr Rva EXECUTE_FN{0x1ed5240};
    static constexpr Rva DISPATCH_FN{0x1ed4f00};
    static constexpr Rva IS_FINISHED_FN{0x1ed4480};
    static constexpr Rva INIT_FN{0x1ece570};
    static constexpr Rva POLL_REQUEST_FN{0x1ece590};
    static constexpr Rva LEAVE_FN{0x1ece5d0};
    static constexpr Rva LEAVE_WAIT_FN{0x1ece5e0};
    static constexpr Rva FINISH_FN{0x1ece630};
    static constexpr Rva DEBUG_INITIALIZE_FN{0x1ed45b0};
    static constexpr Rva DEBUG_CLEANUP_FN{0x1ed6fd0};
    static constexpr std::uint8_t LEAVE_REQUEST_BIT = 1;
    static constexpr std::uint8_t LEAVE_COMPLETION_BIT = 2;
    static constexpr float INITIAL_LEAVE_WAIT = 10.0f;

    // True and `out` set when current_step names a step.
    bool step(CSSessionConnectStep& out) const { return from_raw(current_step, out); }
    // Matches native virtual +0x20; a next_step of -1 alone is insufficient.
    bool is_finished() const { return current_step == static_cast<std::int32_t>(CSSessionConnectStep::Finished); }
    bool leave_requested() const { return (request_flags & LEAVE_REQUEST_BIT) != 0; }
    bool leave_completion_requested() const { return (request_flags & LEAVE_COMPLETION_BIT) != 0; }
    // Pure view of the Wait callback's branch after subtracting frame delta.
    // Does not check the current step, write the timer, consume bit 1, request
    // Finish, or dispatch. NaN does not expire unless bit 1 is present.
    bool leave_wait_would_finish(float delta) const {
        return leave_completion_requested() || leave_wait_remaining - delta < 0.0f;
    }
};

namespace detail::session_connect_state_step_layout {
using T = CSSessionConnectStateStep;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSSessionConnectStateStep)");
BB_OFFSET(T, step_table, 0x08);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, next_step, 0x54);
BB_OFFSET(T, immediate_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _debug_string78, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, debug_step_name, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
BB_OFFSET(T, leave_timer_vtable, 0xc8);
BB_OFFSET(T, leave_wait_remaining, 0xd0);
BB_OFFSET(T, request_flags, 0xd8);
}  // namespace detail::session_connect_state_step_layout

}  // namespace bb
