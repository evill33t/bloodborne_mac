// CSPSPlusCheckStep: the native PS Plus entitlement step and its observed
// state transitions.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSPlatformNetworkRequest;

// Named callbacks in registration order. Native step fields remain integers
// because terminal and unnamed table values are not enumerators.
enum class CSPSPlusCheckStepIndex : std::int32_t {
    Init = 0,
    RegisterCallback = 1,
    WaitRecheckEvent = 2,
    RequestCheck = 3,
    WaitCheckResult = 4,
    UnregisterCallback = 5,
    Finish = 6,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSPSPlusCheckStepIndex& out) {
    if (value < 0 || value > 6) return false;
    out = static_cast<CSPSPlusCheckStepIndex>(value);
    return true;
}

// Exact 0xd8-byte SprjStepLocal subclass, confirmed by reflected size getter
// RVA 0x1e7b0b0. Constructor RVA 0x1927cf0 embeds it at owner +0x13ae0. That
// field-transition owner's complete class identity remains unproven.
//
// WaitRecheckEvent pumps platform callbacks and consumes the manager's +0xf8
// flag. RequestCheck retains an async request; WaitCheckResult releases it on
// completion. UnregisterCallback releases any remaining request, unregisters
// the platform callback, and restarts at Init. Finish requests -1.
//
// Native destruction also unregisters the platform callback unconditionally
// and removes both debug menus.
struct CSPSPlusCheckStep {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    // Optional eight-word debug counter array, allocated by RVA 0x1e79fe0; no
    // safe unsynchronized traversal.
    std::int32_t* execution_counts;
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    // Retained independently of the platform manager's pending vector.
    CSPlatformNetworkRequest* request;
    // PSPLUS menu under PRIMAL_SYSTEM, separate from the base step menu.
    void* ps_plus_debug_menu;

    static constexpr std::size_t SIZE = 0xd8;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_PS_PLUS_CHECK_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_PS_PLUS_CHECK_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x53503b0};
    static constexpr Rva CONSTRUCTOR_FN{0x1e779c0};
    static constexpr Rva DESTRUCTOR_FN{0x1e77dd0};
    // Virtual +0xd0 thunk to the shared local-step dispatcher.
    static constexpr Rva EXECUTE_FN{0x1e7aa20};
    static constexpr Rva SIZE_GETTER_FN{0x1e7b0b0};
    static constexpr Rva INIT_FN{0x1e77f40};
    static constexpr Rva REGISTER_CALLBACK_FN{0x1e78010};
    static constexpr Rva WAIT_RECHECK_EVENT_FN{0x1e780e0};
    static constexpr Rva REQUEST_CHECK_FN{0x1e78220};
    static constexpr Rva WAIT_CHECK_RESULT_FN{0x1e783c0};
    static constexpr Rva UNREGISTER_CALLBACK_FN{0x1e78550};
    static constexpr Rva FINISH_FN{0x1e785f0};
    static constexpr Rva DEBUG_RECHECK_FN{0x1e78600};
    static constexpr std::size_t FIELD_TRANSITION_OWNER_OFFSET = 0x13ae0;
    static constexpr std::size_t FRPG_ENTITLEMENT_LOSS_FLAG_OFFSET = 0xa06;

    // Pure predicate from the callbacks. Arguments are captured bytes from
    // FrpgNetMan +0x08/+0x9f6/+0x9f8/+0xa50, plus client singleton presence.
    // Native code asserts FrpgNetMan exists when the client is present.
    // Failure waits in Init, restarts Init during registration, and routes the
    // recheck/request/result states to UnregisterCallback.
    static constexpr bool network_gate(bool network_client_present, std::uint8_t network_enabled,
                                       std::uint8_t gate_9f6, std::uint8_t gate_9f8, std::uint8_t gate_a50) {
        return network_client_present && network_enabled != 0 && gate_9f6 == 0 && gate_9f8 == 0 && gate_a50 == 0;
    }

    // Branch in WaitCheckResult after native polling reports completion.
    // Selecting UnregisterCallback here also sets FrpgNetMan +0xa06 to 1. A
    // negative request result returns to WaitRecheckEvent. Inputs must be
    // initialized observations: the factory does not initialize either output,
    // and a negative poll return does not write result_code.
    static constexpr CSPSPlusCheckStepIndex completed_check_step(std::int32_t result_code, std::uint8_t entitlement) {
        return (result_code < 0 || entitlement != 0) ? CSPSPlusCheckStepIndex::WaitRecheckEvent
                                                     : CSPSPlusCheckStepIndex::UnregisterCallback;
    }
};

namespace detail::ps_plus_check_step_layout {
using T = CSPSPlusCheckStep;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSPSPlusCheckStep)");
BB_OFFSET(T, callback_table, 0x08);
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
BB_OFFSET(T, request, 0xc8);
BB_OFFSET(T, ps_plus_debug_menu, 0xd0);
static_assert(T::FIELD_TRANSITION_OWNER_OFFSET + T::SIZE == 0x13bb8, "field-transition embedding");
}  // namespace detail::ps_plus_check_step_layout

}  // namespace bb
