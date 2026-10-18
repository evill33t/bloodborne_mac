// CSPlatformNetworkMan: platform authentication, PS Plus requests, and
// deferred request deletion.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSEzWork;
struct CSEzWorkCompletion;
struct CSPlatformNetworkRequest;

inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_SIZE = 0x1b0;

inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_PRIMARY_VECTOR_OFFSET = 0x18;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_FIRST_CALLBACK_OFFSET = 0x38;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_SECOND_CALLBACK_OFFSET = 0x68;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_PENDING_LIST_OFFSET = 0xd0;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_STATUS_A_OFFSET = 0xf8;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_STATUS_B_OFFSET = 0xfc;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_ENABLED_OFFSET = 0x100;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_AUTH_TIMEOUT_OFFSET = 0x108;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_AUTH_REQUEST_ID_OFFSET = 0x118;
// Legacy offset name. These bytes contain FD4Time followed by an auth request
// ID, not a step object; CSPSPlusCheckStep is embedded in another owner.
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_STEP_OBJECT_OFFSET = CS_PLATFORM_NETWORK_MAN_AUTH_TIMEOUT_OFFSET;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_PAYLOAD_OFFSET = 0x11c;
inline constexpr std::size_t CS_PLATFORM_NETWORK_MAN_SENTINEL_OFFSET = 0x1a4;

// Both callbacks use the native 0x30-byte Sprj task representation.
using CSPlatformNetworkCallback = SprjCallbackTask;

// Historical name for a 0x28-byte wrapper around a retained-request vector.
// The vector's begin/end/capacity/allocator lie at owner +0xd8/+0xe0/+0xe8/+0xf0.
// Native insertion/removal adjusts request reference counts atomically.
struct CSPlatformNetworkPendingList {
    void* _unk00;
    DLVector<CSPlatformNetworkRequest*> items;
};

// Native 0x1b0-byte singleton. Startup allocation requests alignment 16; the
// observed field layout itself requires only pointer alignment.
//
// Two GetNpAuthCode tasks target this stable native address. Construction
// registers maintenance; accepted authentication requests register polling.
// Destruction unregisters both tasks, releases the callback and both vectors,
// destroys the deletion executor, and unloads platform module 0x9d.
struct CSPlatformNetworkMan {
    void* vftable;
    // Lazily created EzWork_NpAuthDelayDelete executor; destroyed when its
    // completion vector drains, and during owner destruction.
    CSEzWork* auth_delete_work;
    Unknown<8> _unk10;
    // Retained completions for auth-delete fragments, not Sprj step pointers.
    DLVector<CSEzWorkCompletion*> primary_vector;
    // Invokes AUTH_POLL_FN; registered while an auth request is active.
    CSPlatformNetworkCallback first_callback;
    // Invokes MAINTENANCE_FN; registered by construction.
    CSPlatformNetworkCallback second_callback;
    Unknown<8> _unk98;
    // Small-buffer storage for the native auth completion callback.
    Unknown<0x20> auth_callback_storage;
    // Null, a pointer into +0xa0, or a separately allocated native callback.
    // Its virtual +0x10 receives code/issuer/status; +0x20 destroys it.
    void* auth_callback;
    Unknown<8> _unkc8;
    CSPlatformNetworkPendingList pending_list;
    // PS Plus recheck flag. Event 1 sets this; the local step consumes it.
    std::uint32_t status_a;
    // Signed system-event holdoff countdown. Maintenance maps n > 1 to n-1,
    // otherwise to 0. Observed system event 0x10000000 sets it to 5.
    std::int32_t status_b;
    // Gates the observed Plus-feature notification path.
    std::uint8_t enabled;
    // Bit 0 requests a Plus-feature notification; maintenance clears it even
    // if disabled or initial-user lookup fails. Other bits remain unclassified.
    std::uint8_t notification_flags;
    Unknown<0x06> _pad102;
    FD4Time auth_timeout;
    // sceNpAuth request identifier, distinct from CSPlatformNetworkRequest IDs.
    std::int32_t auth_request_id;
    // Native authorization-code output buffer, populated by the platform API.
    Unknown<0x88> payload;
    // Authorization-code issuer ID; construction initializes it to -1.
    std::int32_t sentinel;
    Unknown<0x08> _unk1a8;

    static constexpr std::size_t SIZE = CS_PLATFORM_NETWORK_MAN_SIZE;
    static constexpr Rva SINGLETON_PTR = CS_PLATFORM_NETWORK_MAN_SINGLETON_PTR;
    static constexpr Rva VTABLE{0x5350360};
    static constexpr Rva TASK_VTABLE{0x53a8f90};
    static constexpr Rva CONSTRUCTOR_FN{0x1e75060};
    static constexpr Rva DESTRUCTOR_FN{0x1e75ae0};
    static constexpr Rva START_AUTH_FN{0x1e75d10};
    static constexpr Rva AUTH_POLL_FN{0x1e75280};
    static constexpr Rva MAINTENANCE_FN{0x1e757d0};
    static constexpr Rva REQUEST_PLUS_CHECK_FN{0x1e76660};
    static constexpr Rva POLL_PLUS_CHECK_FN{0x1e76780};
    static constexpr Rva PLUS_EVENT_CALLBACK_FN{0x1e76a30};
    static constexpr Rva RECEIVE_SYSTEM_EVENTS_FN{0x1e76a50};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::GetNpAuthCode;
    static constexpr std::uint8_t PLUS_NOTIFY_PENDING_BIT = 1;
    static constexpr std::uint32_t SYSTEM_EVENT_ID = 0x10000000;
    static constexpr std::int32_t SYSTEM_EVENT_HOLDOFF_UPDATES = 5;

    // Snapshot of the enabled byte; does not establish sign-in/entitlement.
    bool is_enabled() const { return enabled != 0; }
    // Request acceptance checks callback occupancy only. Native platform
    // submission can still fail after this predicate succeeds.
    bool can_start_auth() const { return auth_callback == nullptr; }
    bool plus_recheck_pending() const { return status_a != 0; }
    bool plus_notification_pending() const { return (notification_flags & PLUS_NOTIFY_PENDING_BIT) != 0; }
    // Pure maintenance countdown rule; negative native values clear to zero.
    static constexpr std::int32_t next_system_event_holdoff(std::int32_t value) { return value > 1 ? value - 1 : 0; }
};

// Descriptive 0x48-byte allocation returned by factory RVA 0x1e74f00. This is
// a sceNp async request, separate from the owner's sceNpAuth ID. Both the PS
// Plus step and the pending vector retain it. Its native destructor calls
// sceNpDeleteRequest; reference counts are atomic in native code.
struct CSPlatformNetworkRequest {
    const void* vftable;
    std::int32_t reference_count;
    std::int32_t request_id;
    // Maintenance subtracts frame delta and aborts expired requests without
    // removing them. Polling performs removal on either completion or error.
    FD4Time timeout;
    // Factory leaves this unwritten. Poll return 0 copies the platform result;
    // negative poll returns finish/remove without writing it. Initialization
    // must be established separately before inspecting this field.
    std::int32_t result_code;
    // Output passed to sceNpCheckPlus, also not initialized by the factory.
    std::uint8_t entitlement;
    Unknown<0x23> _unk25;

    static constexpr std::size_t SIZE = 0x48;
    static constexpr Rva VTABLE{0x5350330};
    static constexpr Rva FACTORY_FN{0x1e74f00};
    static constexpr Rva DESTRUCTOR_FN{0x1e76d50};
    static constexpr Rva TIMEOUT_DEFAULT_PTR{0x55902a0};
    // Static initializer RVA 0x1e76cc0 stores float bits 0x41a00000. Requests
    // copy the mutable global, so this is an initial default, not a clamp.
    static constexpr float INITIAL_TIMEOUT_SECONDS = 20.0f;
    static constexpr Rva APPEND_PENDING_FN{0x1e770c0};
    static constexpr Rva ERASE_PENDING_FN{0x1e771b0};
};

// Distinguishes the poll API return from the request's result_code output.
enum class CSPlatformRequestPollOutcome {
    // Positive poll return: keep the request pending, no result copy.
    Pending,
    // Zero poll return: copy result_code, then erase from the pending vector.
    Completed,
    // Negative poll return: erase without writing result_code.
    Failed,
};

inline constexpr CSPlatformRequestPollOutcome from_poll_return(std::int32_t value) {
    return value > 0    ? CSPlatformRequestPollOutcome::Pending
           : value == 0 ? CSPlatformRequestPollOutcome::Completed
                        : CSPlatformRequestPollOutcome::Failed;
}

// Descriptive 0x18-byte work fragment allocated by AUTH_POLL_FN. The native
// executor owns it; the owner retains its completion separately. Execute calls
// sceNpAuthDeleteRequest. Destruction decrements the completion pending count,
// signals its event on the last fragment, and releases the reference.
struct CSPlatformAuthDeleteFragment {
    const void* vftable;
    CSEzWorkCompletion* completion;
    // Observed 0: executor destroys the fragment after execution.
    std::uint32_t cleanup_mode;
    std::int32_t auth_request_id;

    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva VTABLE{0x5350380};
    static constexpr Rva EXECUTE_FN{0x1e76f40};
    static constexpr Rva DESTRUCTOR_FN{0x1e76e40};
};

namespace detail::platform_network_layout {
using T = CSPlatformNetworkMan;
BB_SIZE(T, CS_PLATFORM_NETWORK_MAN_SIZE);
static_assert(alignof(T) == 8, "alignof(CSPlatformNetworkMan)");
BB_OFFSET(T, auth_delete_work, 0x08);
BB_OFFSET(T, primary_vector, CS_PLATFORM_NETWORK_MAN_PRIMARY_VECTOR_OFFSET);
BB_OFFSET(T, first_callback, CS_PLATFORM_NETWORK_MAN_FIRST_CALLBACK_OFFSET);
BB_OFFSET(T, second_callback, CS_PLATFORM_NETWORK_MAN_SECOND_CALLBACK_OFFSET);
BB_OFFSET(T, first_callback.owner, 0x50);
BB_OFFSET(T, first_callback.callback, 0x58);
BB_OFFSET(T, second_callback.owner, 0x80);
BB_OFFSET(T, second_callback.callback, 0x88);
BB_OFFSET(T, auth_callback_storage, 0xa0);
BB_OFFSET(T, auth_callback, 0xc0);
BB_OFFSET(T, pending_list, CS_PLATFORM_NETWORK_MAN_PENDING_LIST_OFFSET);
BB_OFFSET(T, status_a, CS_PLATFORM_NETWORK_MAN_STATUS_A_OFFSET);
BB_OFFSET(T, status_b, CS_PLATFORM_NETWORK_MAN_STATUS_B_OFFSET);
BB_OFFSET(T, enabled, CS_PLATFORM_NETWORK_MAN_ENABLED_OFFSET);
BB_OFFSET(T, notification_flags, 0x101);
BB_OFFSET(T, auth_timeout, CS_PLATFORM_NETWORK_MAN_AUTH_TIMEOUT_OFFSET);
BB_OFFSET(T, auth_timeout.time, 0x110);
BB_OFFSET(T, auth_request_id, CS_PLATFORM_NETWORK_MAN_AUTH_REQUEST_ID_OFFSET);
BB_OFFSET(T, payload, CS_PLATFORM_NETWORK_MAN_PAYLOAD_OFFSET);
BB_OFFSET(T, sentinel, CS_PLATFORM_NETWORK_MAN_SENTINEL_OFFSET);
BB_SIZE(CSPlatformNetworkCallback, 0x30);
BB_SIZE(CSPlatformNetworkPendingList, 0x28);
BB_OFFSET(T, pending_list.items, 0xd8);
BB_SIZE(CSPlatformNetworkRequest, CSPlatformNetworkRequest::SIZE);
static_assert(alignof(CSPlatformNetworkRequest) == 8, "alignof(CSPlatformNetworkRequest)");
BB_OFFSET(CSPlatformNetworkRequest, reference_count, 0x08);
BB_OFFSET(CSPlatformNetworkRequest, request_id, 0x0c);
BB_OFFSET(CSPlatformNetworkRequest, timeout, 0x10);
BB_OFFSET(CSPlatformNetworkRequest, timeout.time, 0x18);
BB_OFFSET(CSPlatformNetworkRequest, result_code, 0x20);
BB_OFFSET(CSPlatformNetworkRequest, entitlement, 0x24);
BB_SIZE(CSPlatformAuthDeleteFragment, CSPlatformAuthDeleteFragment::SIZE);
static_assert(alignof(CSPlatformAuthDeleteFragment) == 8, "alignof(CSPlatformAuthDeleteFragment)");
BB_OFFSET(CSPlatformAuthDeleteFragment, completion, 0x08);
BB_OFFSET(CSPlatformAuthDeleteFragment, cleanup_mode, 0x10);
BB_OFFSET(CSPlatformAuthDeleteFragment, auth_request_id, 0x14);
}  // namespace detail::platform_network_layout

}  // namespace bb
