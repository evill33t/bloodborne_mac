// CSRequestSummonStep: runtime metadata and the request/result tail of the
// summon request step.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_SIZE = 0xf8;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_BASE_SIZE = 0xc8;

enum class CSRequestSummonStepIndex : std::int32_t {
    Init = 0,
    Update = 1,
    RequestSummonCheckWait = 2,
    RequestSummonCheckComplete = 3,
    Finish = 4,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSRequestSummonStepIndex& out) {
    if (value < 0 || value > 4) return false;
    out = static_cast<CSRequestSummonStepIndex>(value);
    return true;
}

inline constexpr std::uint32_t CS_REQUEST_SUMMON_SPECIAL_SUCCESS_RESULT = 0x10301;

// Request task embedded at SosSignMan + 0x58.
//
// CSRequestSummonStep_Ctor initializes the common step-task prefix through
// +0xc7. SosSignMan_Ctor initializes the request/result tail below. The step
// submits target_user_id and target_chara_id through SprjNetworkClientMan
// operation 0x1c; its completion callback writes the result and completion
// fields only. The buffer at +0xe0 is staged by the owner before submission,
// not allocated by the network callback.
//
// Update advances to Wait after calling the request builder even if that
// builder rejects submission. Wait polls complete; it has no local timeout.
// CheckComplete optionally copies the staged buffer into an owned result queue
// entry, frees/resets the original, and requests Update for reuse. The
// completion byte therefore does not mean terminal task completion.
// Destruction frees a remaining staged buffer and local-step/debug storage.
// Callback adapters borrow this task; no cancellation guarantee.
struct CSRequestSummonStep {
    SprjStepLocalC8 local_step;
    std::uint32_t target_user_id;
    std::uint32_t _pad_cc;
    std::uint64_t target_chara_id;
    std::int32_t descriptor_id;
    std::uint32_t _pad_dc;
    // Legacy field name for the owned, staged payload copied by SosSignMan. A
    // non-null value gates submission; CheckComplete/destruction release it.
    void* response_payload;
    // Byte length of the staged payload, not a length written by the callback.
    std::uint32_t response_payload_size;
    std::uint32_t result_code;
    std::uint8_t complete;
    Unknown<0x07> _pad_f1;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_REQUEST_SUMMON_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_REQUEST_SUMMON_STEP_TEMPLATE;
    static constexpr std::uint64_t EMPTY_TARGET_CHARA_ID = 0x8000000000000000ull;

    // True and `out` set when the committed step names a registered step.
    bool current_step(CSRequestSummonStepIndex& out) const { return from_raw(local_step.current_step, out); }
    // True and `out` set when the requested step names a registered step.
    bool requested_step(CSRequestSummonStepIndex& out) const { return from_raw(local_step.requested_step, out); }

    // Exact result-code branch in CheckComplete, not a general success test.
    // The special 0x10301 result skips creation of the deferred queue entry.
    static constexpr bool completion_queues_staged_payload(std::uint32_t result) {
        return result != CS_REQUEST_SUMMON_SPECIAL_SUCCESS_RESULT && (result >> 16) == 0;
    }
};

// Descriptive callable adapter cloned into the network request callback store.
// Clone helper RVA 0x14be000 allocates 0x18 bytes if no inline destination is
// supplied, and copies the borrowed task pointer at +8. Completion RVA
// 0x14be080 copies response +0x0c into result_code and sets complete=1. The
// final eight bytes remain unclassified.
struct CSRequestSummonCompletion {
    const void* vftable;
    CSRequestSummonStep* task;
    Unknown<8> _unk10;
};

inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_EMBEDDED_IN_SOS_SIGN_MAN_OFFSET = 0x58;
// Non-null gate consumed by STEP_Update; the request builder itself receives
// only the target user/chara fields and its completion callback.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_REQUEST_GATE_OFFSET = 0xe0;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_RESPONSE_PAYLOAD_OFFSET = 0xe0;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_CURRENT_STEP_OFFSET = 0x50;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_REQUESTED_STEP_OFFSET = 0x54;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_CONTINUATION_OFFSET = 0x58;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_ALLOCATOR_OFFSET = 0x60;
// Compatibility spelling for CURRENT_STEP_OFFSET.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_FIELD_50_OFFSET = 0x50;
// Compatibility spelling for REQUESTED_STEP_OFFSET.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_FIELD_54_OFFSET = 0x54;
// Legacy misnomer: this is continuation, not the completion flag at +0xf0.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_CHECK_WAIT_LATCH_OFFSET = 0x58;
// Compatibility spelling for ALLOCATOR_OFFSET.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_FIELD_60_OFFSET = 0x60;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_TARGET_USER_ID_OFFSET = 0xc8;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_TARGET_CHARA_ID_OFFSET = 0xd0;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_DESCRIPTOR_ID_OFFSET = 0xd8;
// Legacy misnomer: +0xd8 holds descriptor_id; payload byte length is +0xe8.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_REQUEST_SIZE_OFFSET = 0xd8;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_RESPONSE_SIZE_OFFSET = 0xe8;
// Legacy misnomer: the owner writes this staged length, not the callback.
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_CALLBACK_PAYLOAD_LEN_OFFSET = 0xe8;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_RESULT_CODE_OFFSET = 0xec;
inline constexpr std::size_t CS_REQUEST_SUMMON_STEP_COMPLETE_FLAG_OFFSET = 0xf0;
inline constexpr std::size_t CS_REQUEST_SUMMON_RESPONSE_RESULT_CODE_OFFSET = 0x0c;

inline constexpr Rva CS_REQUEST_SUMMON_STEP_CONSTRUCTOR_FN{0x14bef90};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_CALLBACK_VTABLE{0x5324bd0};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_CALLBACK_COMPLETE_FN{0x14be080};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_VTABLE{0x53249e0};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_SIZE_GETTER_FN{0x14be620};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_DESTRUCTOR_FN{0x14bcc80};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_EXECUTE_FN{0x14bdad0};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_DISPATCH_FN{0x14bd790};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_IS_FINISHED_FN{0x14bcdf0};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_SUBMIT_FN{0x1e98650};
inline constexpr Rva CS_REQUEST_SUMMON_STEP_CALLBACK_CLONE_FN{0x14be000};

namespace detail::request_summon_step_layout {
using T = CSRequestSummonStep;
BB_SIZE(T, CS_REQUEST_SUMMON_STEP_SIZE);
BB_SIZE(SprjStepLocalC8, CS_REQUEST_SUMMON_STEP_BASE_SIZE);
static_assert(alignof(T) == 8, "alignof(CSRequestSummonStep)");
BB_OFFSET(T, local_step, 0);
BB_OFFSET(T, local_step.current_step, CS_REQUEST_SUMMON_STEP_CURRENT_STEP_OFFSET);
BB_OFFSET(T, local_step.requested_step, CS_REQUEST_SUMMON_STEP_REQUESTED_STEP_OFFSET);
BB_OFFSET(T, local_step.continue_this_update, CS_REQUEST_SUMMON_STEP_CONTINUATION_OFFSET);
BB_OFFSET(T, local_step.allocator, CS_REQUEST_SUMMON_STEP_ALLOCATOR_OFFSET);
BB_OFFSET(T, target_user_id, CS_REQUEST_SUMMON_STEP_TARGET_USER_ID_OFFSET);
BB_OFFSET(T, target_chara_id, CS_REQUEST_SUMMON_STEP_TARGET_CHARA_ID_OFFSET);
BB_OFFSET(T, descriptor_id, CS_REQUEST_SUMMON_STEP_DESCRIPTOR_ID_OFFSET);
BB_OFFSET(T, response_payload, CS_REQUEST_SUMMON_STEP_RESPONSE_PAYLOAD_OFFSET);
BB_OFFSET(T, response_payload_size, CS_REQUEST_SUMMON_STEP_RESPONSE_SIZE_OFFSET);
BB_OFFSET(T, result_code, CS_REQUEST_SUMMON_STEP_RESULT_CODE_OFFSET);
BB_OFFSET(T, complete, CS_REQUEST_SUMMON_STEP_COMPLETE_FLAG_OFFSET);
BB_SIZE(CSRequestSummonCompletion, 0x18);
static_assert(alignof(CSRequestSummonCompletion) == 8, "alignof(CSRequestSummonCompletion)");
BB_OFFSET(CSRequestSummonCompletion, task, 8);
static_assert(T::completion_queues_staged_payload(0xffff) && !T::completion_queues_staged_payload(0x10301) &&
                  !T::completion_queues_staged_payload(0x10000),
              "CheckComplete result gate");
}  // namespace detail::request_summon_step_layout

}  // namespace bb
