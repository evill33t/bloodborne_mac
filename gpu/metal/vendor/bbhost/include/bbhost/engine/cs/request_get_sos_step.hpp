// CSRequestGetSosStep: runtime metadata and native layout of the SOS
// search-request step.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_SIZE = 0x130;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_BASE_SIZE = 0xc8;

// Native callback indices; Finish requests terminal -1.
enum class CSRequestGetSosStepIndex : std::int32_t {
    Init = 0,
    UpdateForSeamless = 1,
    UpdateForWide = 2,
    Finish = 3,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSRequestGetSosStepIndex& out) {
    if (value < 0 || value > 3) return false;
    out = static_cast<CSRequestGetSosStepIndex>(value);
    return true;
}

// Search-request task allocated at SosSignMan + 0x50.
//
// Reflected extent 0x130, aligned to eight. SosSignMan directly dispatches
// this local step. Its execute override clears the descriptor vector's logical
// length and alternate_request_api after every update, retaining capacity.
// Callers must populate search inputs again for a later tick. Init resets
// retry_cursor and requests Seamless with continuation; Wide is a native
// no-op. Finish requests -1 rather than destroying this object.
//
// Construction clones the supplied completion callable into inline storage or
// a heap allocation and reserves at least five descriptor slots. Submit
// deduplicates descriptors into a temporary vector, builds from local-player
// and FrpgNetMan state, and binds a callback borrowing this task. Destruction
// releases the callable/vector and native local-step storage. It does not
// establish cancellation of outstanding callbacks.
struct CSRequestGetSosStep {
    SprjStepLocalC8 local_step;
    void* _descriptor_vector_owner;
    // Zero-extended invade_type bytes from accepted session descriptors,
    // stored as 32-bit values. AddSessionType can append duplicates.
    std::int32_t* descriptor_ids_begin;
    std::int32_t* descriptor_ids_end;
    std::int32_t* descriptor_ids_capacity;
    void* descriptor_ids_allocator;
    Unknown<0x20> _callback_inline_storage;
    void* completion_callback;
    Unknown<0x08> _unknown_118;
    // Selects the alternate native request API; reset after each update.
    // Independent of the alternate-search argument chosen by retry rotation.
    std::uint8_t alternate_request_api;
    Unknown<0x03> _unknown_121;
    // Rotates ordinary versus alternate searches against the native connection
    // limit exposed by SprjNetworkClientMan. Native comparison interprets this
    // word as signed; u32 storage is retained for compatibility.
    std::uint32_t retry_cursor;
    Unknown<0x08> _unknown_128;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_REQUEST_GET_SOS_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_REQUEST_GET_SOS_STEP_TEMPLATE;

    // True and `out` set when the committed step names a registered step.
    bool current_step(CSRequestGetSosStepIndex& out) const { return from_raw(local_step.current_step, out); }
    // True and `out` set when the requested step names a registered step.
    bool requested_step(CSRequestGetSosStepIndex& out) const { return from_raw(local_step.requested_step, out); }

    // AddSessionType's index gate, before reading the descriptor table.
    static constexpr bool accepts_session_type(std::uint32_t index) {
        return index <= 25 && (0x03e40180u & (1u << index)) != 0;
    }

    // Rotation comparison used only when FrpgNetMan +0xb14 bit 1 permits
    // alternate searches. The caller supplies the native connection limit
    // (default 5 when the network client implementation is absent).
    constexpr bool rotation_requests_alternate(std::int32_t connection_limit) const {
        return static_cast<std::int32_t>(retry_cursor) >= connection_limit;
    }
};

inline constexpr Rva CS_REQUEST_GET_SOS_STEP_CONSTRUCTOR_FN{0x1e5f6f0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_DESTRUCTOR_FN{0x1e61af0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_SUBMIT_SEARCH_REQUEST_FN{0x1e5fce0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_INVOKE_COMPLETION_CALLBACK_FN{0x1e603d0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_VTABLE{0x534fc20};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_SIZE_GETTER_FN{0x1e63190};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_EXECUTE_FN{0x1e5fac0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_DISPATCH_FN{0x1e62760};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_IS_FINISHED_FN{0x1e61ce0};
inline constexpr Rva CS_REQUEST_GET_SOS_STEP_ADD_SESSION_TYPE_FN{0x1e5f970};

inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_BEGIN_OFFSET = 0xd0;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_END_OFFSET = 0xd8;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_CAPACITY_OFFSET = 0xe0;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_ALLOCATOR_OFFSET = 0xe8;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_COMPLETION_CALLBACK_OFFSET = 0x110;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_ALTERNATE_REQUEST_API_OFFSET = 0x120;
inline constexpr std::size_t CS_REQUEST_GET_SOS_STEP_RETRY_CURSOR_OFFSET = 0x124;

namespace detail::request_get_sos_step_layout {
using T = CSRequestGetSosStep;
BB_OFFSET(T, descriptor_ids_begin, CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_BEGIN_OFFSET);
BB_OFFSET(T, descriptor_ids_end, CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_END_OFFSET);
BB_OFFSET(T, descriptor_ids_capacity, CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_CAPACITY_OFFSET);
BB_OFFSET(T, descriptor_ids_allocator, CS_REQUEST_GET_SOS_STEP_DESCRIPTOR_IDS_ALLOCATOR_OFFSET);
BB_OFFSET(T, completion_callback, CS_REQUEST_GET_SOS_STEP_COMPLETION_CALLBACK_OFFSET);
BB_OFFSET(T, alternate_request_api, CS_REQUEST_GET_SOS_STEP_ALTERNATE_REQUEST_API_OFFSET);
BB_OFFSET(T, retry_cursor, CS_REQUEST_GET_SOS_STEP_RETRY_CURSOR_OFFSET);
BB_SIZE(T, CS_REQUEST_GET_SOS_STEP_SIZE);
BB_SIZE(SprjStepLocalC8, CS_REQUEST_GET_SOS_STEP_BASE_SIZE);
static_assert(alignof(T) == 8, "alignof(CSRequestGetSosStep)");
BB_OFFSET(T, local_step, 0);
BB_OFFSET(T, _descriptor_vector_owner, 0xc8);
BB_OFFSET(T, _callback_inline_storage, 0xf0);
static_assert(T::accepts_session_type(7) && T::accepts_session_type(25) && !T::accepts_session_type(26) &&
                  !T::accepts_session_type(6),
              "AddSessionType gate");
}  // namespace detail::request_get_sos_step_layout

}  // namespace bb
