// SprjNetworkClientMan: the game's application-service (HTTP) request
// manager. Owns the request client, server-side user/session identifiers,
// request-result containers and queued service notices; not Matching2 room
// membership, WorldSession player slots or remote actor tasks.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_SINGLETON{0x5540288};
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_SIZE = 0x490;

inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_CTOR_FN{0x1e7c940};
inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_DTOR_FN{0x1e7e890};
inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_UPDATE_REQUESTS_FN{0x1e7df20};
inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_DISPATCH_QUEUED_NOTICES_FN{0x1e7e4a0};
inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_BUILD_AND_QUEUE_REQUEST_FN{0x1e8a880};

inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_REQUEST_APPLICATION_OFFSET = 0x08;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_REQUEST_CONTEXT_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_CONNECTION_OFFSET = 0x18;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_LOCAL_USER_ID_OFFSET = 0x60;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_SESSION_ID_OFFSET = 0x78;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_REQUEST_COOLDOWNS_OFFSET = 0xd0;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_NOTICE_HANDLER_ROOT_OFFSET = 0x3b8;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_REQUEST_UPDATE_TASK_OFFSET = 0x3f0;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_NOTICE_DISPATCH_TASK_OFFSET = 0x420;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_PENDING_NOTICES_OFFSET = 0x460;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_MAN_NOTICE_DISPATCH_ACTIVE_OFFSET = 0x480;

inline constexpr std::size_t SPRJ_NETWORK_CLIENT_REQUEST_COOLDOWN_COUNT = 11;
inline constexpr std::size_t SPRJ_NETWORK_CLIENT_REQUEST_COOLDOWN_STRIDE = 0x10;
inline constexpr std::int64_t SPRJ_NETWORK_CLIENT_UNSET_USER_ID = INT64_MIN;
inline constexpr SprjTaskGroupIndex SPRJ_NETWORK_CLIENT_REQUEST_TASK_GROUP = SprjTaskGroupIndex::NetworkClient;

// Native wide-character small string used for the session ID. Capacity 7
// keeps the text in the inline 16 bytes; larger makes the first pointer-sized
// field refer to allocator-owned storage.
struct SprjNetworkWideSmallString {
    Unknown<0x10> inline_or_heap_storage;
    std::size_t length;
    std::size_t capacity;
    void* allocator;
};

struct SprjNetworkRequestCooldown {
    float remaining_seconds;
    Unknown<0x0c> _unk04;
};

// Eight-byte notice records drained by the secondary callback task.
struct SprjNetworkQueuedNotice {
    std::uint32_t notice_type;
    std::uint32_t notice_id;
};

// Verified native SprjNetworkClientMan layout. The opaque middle region holds
// endpoint-specific request/result containers (ownership proven by
// construction/destruction, names withheld until each handler is traced). The
// two task objects and the queued-notice vector are exact.
struct SprjNetworkClientMan {
    void* vftable;
    // Native request application updated by the NetworkClient task.
    void* request_application;
    // Adapter/context passed while constructing application requests.
    void* request_context;
    // Active service connection. Request builders require its status at
    // connection + 0x08 to be zero.
    void* connection;
    Unknown<0x40> _unk020;
    // Server-side user ID; INT64_MIN is the native uninitialized sentinel.
    std::int64_t local_user_id;
    Unknown<0x10> _unk068;
    SprjNetworkWideSmallString session_id;
    Unknown<0x30> _unk0a0;
    // Endpoint cooldowns decremented by the request-update task; endpoint
    // mapping not yet proven.
    SprjNetworkRequestCooldown request_cooldowns[SPRJ_NETWORK_CLIENT_REQUEST_COOLDOWN_COUNT];
    Unknown<0x230> _unk180;
    Unknown<0x08> _notice_handler_tree_prefix;
    void* notice_handler_tree_root;
    Unknown<0x30> _unk3c0;
    // Registered in native task group 0x32 (NetworkClient).
    SprjCallbackTask request_update_task;
    // Drains queued notices. Construction initializes this callback separately
    // from the always-registered request-update task.
    SprjCallbackTask notice_dispatch_task;
    Unknown<0x10> _unk450;
    SprjNetworkQueuedNotice* pending_notices_begin;
    SprjNetworkQueuedNotice* pending_notices_end;
    SprjNetworkQueuedNotice* pending_notices_capacity;
    void* pending_notices_allocator;
    std::uint8_t notice_dispatch_active;
    Unknown<0x0f> _unk481;

    bool has_local_user_id() const { return local_user_id != SPRJ_NETWORK_CLIENT_UNSET_USER_ID; }
    bool has_session_id() const { return session_id.length != 0; }
    std::size_t pending_notice_count() const {
        if (!pending_notices_begin || !pending_notices_end || pending_notices_end < pending_notices_begin) return 0;
        return static_cast<std::size_t>(pending_notices_end - pending_notices_begin);
    }
};

namespace detail::network_client_man_layout {
using M = SprjNetworkClientMan;
BB_SIZE(M, SPRJ_NETWORK_CLIENT_MAN_SIZE);
BB_SIZE(SprjNetworkRequestCooldown, SPRJ_NETWORK_CLIENT_REQUEST_COOLDOWN_STRIDE);
BB_OFFSET(M, request_application, SPRJ_NETWORK_CLIENT_MAN_REQUEST_APPLICATION_OFFSET);
BB_OFFSET(M, request_context, SPRJ_NETWORK_CLIENT_MAN_REQUEST_CONTEXT_OFFSET);
BB_OFFSET(M, connection, SPRJ_NETWORK_CLIENT_MAN_CONNECTION_OFFSET);
BB_OFFSET(M, local_user_id, SPRJ_NETWORK_CLIENT_MAN_LOCAL_USER_ID_OFFSET);
BB_OFFSET(M, session_id, SPRJ_NETWORK_CLIENT_MAN_SESSION_ID_OFFSET);
BB_OFFSET(M, request_cooldowns, SPRJ_NETWORK_CLIENT_MAN_REQUEST_COOLDOWNS_OFFSET);
BB_OFFSET(M, notice_handler_tree_root, SPRJ_NETWORK_CLIENT_MAN_NOTICE_HANDLER_ROOT_OFFSET);
BB_OFFSET(M, request_update_task, SPRJ_NETWORK_CLIENT_MAN_REQUEST_UPDATE_TASK_OFFSET);
BB_OFFSET(M, notice_dispatch_task, SPRJ_NETWORK_CLIENT_MAN_NOTICE_DISPATCH_TASK_OFFSET);
BB_OFFSET(M, pending_notices_begin, SPRJ_NETWORK_CLIENT_MAN_PENDING_NOTICES_OFFSET);
BB_OFFSET(M, notice_dispatch_active, SPRJ_NETWORK_CLIENT_MAN_NOTICE_DISPATCH_ACTIVE_OFFSET);
}  // namespace detail::network_client_man_layout

}  // namespace bb
