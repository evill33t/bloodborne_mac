// CSPS4TrophyStep: the native local step for PS4 trophy initialization and
// asynchronous readiness.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSTrophyImp;

enum class CSPS4TrophyStepIndex : std::int32_t {
    Init = 0,
    CreateContext = 1,
    CreateHandle = 2,
    StartAsyncUpdateThread = 3,
    RequestRegisterContext = 4,
    WaitRegisterContext = 5,
    RequestCreateDebugInfo = 6,
    WaitCreateDebugInfo = 7,
    WaitUnlockRequest = 8,
    Finish = 9,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSPS4TrophyStepIndex& out) {
    if (value < 0 || value > 9) return false;
    out = static_cast<CSPS4TrophyStepIndex>(value);
    return true;
}

// Exact 0xd0-byte SprjStepLocal subclass, allocated aligned to eight by
// CSTrophyImp::START_FN. The implementation's SystemStep callback calls
// virtual +0xd0 directly; this object has no separate task registration.
//
// Creates context/handle without checking their return codes, then starts a
// DLThread and requests context registration and optional debug enumeration.
// WaitRegisterContext/WaitCreateDebugInfo observe mutex-protected completion
// bytes, which do not encode API success. WaitUnlockRequest sets owner.ready
// and does not advance. Finish is a no-op, unlike prefetch's terminating step.
// Destruction cleans up local-step internals without freeing the owner.
struct CSPS4TrophyStep {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    // Virtual +0x38 sets this byte; dispatcher limits same-update loops to 128.
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    // Debug permission/enabled bytes consumed by the native local dispatcher.
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    // Native string storage +0x80, length +0x90, capacity +0x98.
    Unknown<0x38> _string_storage;
    // Optional 11-word debug array allocated by RVA 0x2021ed0.
    std::int32_t* execution_counts;
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    CSTrophyImp* owner;

    static constexpr std::size_t SIZE = 0xd0;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_PS4_TROPHY_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_PS4_TROPHY_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x535a280};
    static constexpr Rva CONSTRUCTOR_FN{0x2020200};
    static constexpr Rva DESTRUCTOR_FN{0x2020440};
    // Virtual +0xd0 thunk to dispatcher RVA 0x2022830.
    static constexpr Rva EXECUTE_FN{0x2022b70};
    static constexpr Rva INIT_FN{0x20204c0};
    static constexpr Rva CREATE_CONTEXT_FN{0x20204f0};
    static constexpr Rva CREATE_HANDLE_FN{0x2020560};
    static constexpr Rva START_ASYNC_THREAD_FN{0x2020590};
    static constexpr Rva REQUEST_REGISTER_CONTEXT_FN{0x2020620};
    static constexpr Rva WAIT_REGISTER_CONTEXT_FN{0x2020630};
    static constexpr Rva REQUEST_CREATE_DEBUG_INFO_FN{0x2020690};
    static constexpr Rva WAIT_CREATE_DEBUG_INFO_FN{0x20206a0};
    static constexpr Rva WAIT_UNLOCK_REQUEST_FN{0x20206b0};
    static constexpr Rva FINISH_FN{0x20206c0};
};

namespace detail::ps4_trophy_step_layout {
using T = CSPS4TrophyStep;
BB_SIZE(T, 0xd0);
static_assert(alignof(T) == 8, "alignof(CSPS4TrophyStep)");
BB_OFFSET(T, callback_table, 8);
BB_OFFSET(T, _dispatcher, 0x10);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, requested_step, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _string_storage, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
BB_OFFSET(T, owner, 0xc8);
}  // namespace detail::ps4_trophy_step_layout

}  // namespace bb
