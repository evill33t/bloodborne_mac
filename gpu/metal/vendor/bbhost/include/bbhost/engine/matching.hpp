// Matching root, NpMatching2 context and matching-session prefixes (Matching2 room/join state).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t MATCHING_ROOT_PREFIX_SIZE = 0xb0;
inline constexpr std::size_t MATCHING_ROOT_MATCHING_MANAGER_INTERFACE_VTABLE_OFFSET = 0x78;
inline constexpr std::size_t MATCHING_ROOT_STATE_OFFSET = 0x80;
inline constexpr std::size_t MATCHING_ROOT_ACTIVE_SESSION_CONTAINER_OFFSET = 0x88;
inline constexpr std::size_t MATCHING_ROOT_OWNED_SUBOBJECT_OFFSET = 0x90;
inline constexpr std::size_t MATCHING_ROOT_OPTIONAL_NP_SUBSYSTEM_OFFSET = 0x98;
inline constexpr std::size_t MATCHING_ROOT_MATCHING_ENABLED_OFFSET = 0xa0;
inline constexpr std::size_t MATCHING_ROOT_NP_MATCHING2_CONTEXT_OFFSET = 0xa8;

inline constexpr std::size_t NP_MATCHING2_CONTEXT_PREFIX_SIZE = 0x70;
inline constexpr std::size_t NP_MATCHING2_CONTEXT_LOCK_OBJECT_OFFSET = 0x08;
inline constexpr std::size_t NP_MATCHING2_CONTEXT_ID_OFFSET = 0x68;

inline constexpr std::size_t MATCHING_SESSION_PREFIX_SIZE = 0x460;
inline constexpr std::size_t MATCHING_SESSION_STATE_OFFSET = 0x3c;
inline constexpr std::size_t MATCHING_SESSION_SEARCH_ROOM_STATE_OFFSET = 0x5c;
inline constexpr std::size_t MATCHING_SESSION_DEFERRED_CALLBACK_OBJECT_OFFSET = 0x68;
inline constexpr std::size_t MATCHING_SESSION_ACTIVE_ROOM_ENTRIES_BEGIN_OFFSET = 0x80;
inline constexpr std::size_t MATCHING_SESSION_ACTIVE_ROOM_ENTRIES_END_OFFSET = 0x88;
inline constexpr std::size_t MATCHING_SESSION_CONTAINER_OFFSET = 0xf0;
inline constexpr std::size_t MATCHING_SESSION_CONTAINER_BEGIN_OFFSET = 0xf8;
inline constexpr std::size_t MATCHING_SESSION_CONTAINER_END_OFFSET = 0x100;
inline constexpr std::size_t MATCHING_SESSION_TRANSACTION_LIST_OFFSET = 0x170;
inline constexpr std::size_t MATCHING_SESSION_TRANSACTION_ALLOCATOR_OFFSET = 0x188;
inline constexpr std::size_t MATCHING_SESSION_REQUEST_DESCRIPTOR_BEGIN_OFFSET = 0x2d8;
inline constexpr std::size_t MATCHING_SESSION_REQUEST_DESCRIPTOR_END_OFFSET = 0x2e0;
inline constexpr std::size_t MATCHING_SESSION_REQUEST_DESCRIPTOR_STRIDE = 0xd8;
inline constexpr std::size_t MATCHING_SESSION_INTERNAL_LOCK_OFFSET = 0x3d0;
inline constexpr std::size_t MATCHING_SESSION_ROOM_STORAGE_OFFSET = 0x400;
inline constexpr std::size_t MATCHING_SESSION_JOIN_TICKET_SIZE = 0x10;
inline constexpr std::size_t MATCHING_SESSION_EVENT_ARG_OFFSET = 0x408;
inline constexpr std::size_t MATCHING_SESSION_EVENT_CODE_OFFSET = 0x40c;
inline constexpr std::size_t MATCHING_SESSION_EVENT_PAYLOAD_OFFSET = 0x418;
inline constexpr std::size_t MATCHING_SESSION_REQUEST_ID_OFFSET = 0x430;
inline constexpr std::size_t MATCHING_SESSION_EVENT_STATUS_A_OFFSET = 0x444;
inline constexpr std::size_t MATCHING_SESSION_EVENT_STATUS_B_OFFSET = 0x445;
inline constexpr std::size_t MATCHING_SESSION_PENDING_CALLBACK_OWNER_OFFSET = 0x458;

struct NpMatching2ContextPrefix;

struct MatchingRootPrefix {
    Unknown<0x78> _unk00;
    // Secondary vtable for the matching-manager facade embedded in
    // "NexusRevolution Main"; global storage +0x78 is this field, not a
    // singleton pointer slot.
    const void* matching_manager_interface_vtable;
    std::uint32_t state;
    Unknown<0x04> _unk84;
    void* active_session_container;
    void* owned_subobject;
    void* optional_np_subsystem;
    bool matching_enabled;
    Unknown<0x07> _unk_a1;
    NpMatching2ContextPrefix* np_matching2_context;
};

struct NpMatching2ContextPrefix {
    Unknown<0x08> _unk00;
    void* lock_object;
    Unknown<0x58> _unk10;
    std::int32_t context_id;
    Unknown<0x04> _unk6c;
};

struct MatchingSessionPrefix {
    Unknown<0x3c> _unk000;
    std::uint32_t state;
    Unknown<0x1c> _unk040;
    std::uint32_t search_room_state;
    Unknown<0x08> _unk060;
    void* deferred_callback_object;
    Unknown<0x10> _unk070;
    void* active_room_entries_begin;
    void* active_room_entries_end;
    Unknown<0x60> _unk090;
    void* container;
    void* container_begin;
    void* container_end;
    Unknown<0x68> _unk108;
    void* transaction_list;
    void* transaction_list_end;
    Unknown<0x08> _unk180;
    void* transaction_allocator;
    Unknown<0x148> _unk190;
    void* request_descriptor_begin;  // stride MATCHING_SESSION_REQUEST_DESCRIPTOR_STRIDE
    void* request_descriptor_end;
    Unknown<0xe8> _unk2e8;
    Unknown<0x18> internal_lock;
    Unknown<0x18> _unk3e8;
    std::uint8_t room_storage_prefix[0x08];
    std::uint16_t event_arg;
    Unknown<0x02> _pad40a;
    std::uint16_t event_code;
    Unknown<0x0a> _pad40e;
    void* event_payload;
    Unknown<0x10> _unk420;
    std::uint32_t request_id;
    Unknown<0x10> _unk434;
    std::uint8_t event_status_a;
    std::uint8_t event_status_b;
    Unknown<0x12> _unk446;
    void* pending_callback_owner;
};

using MatchingManagerPrefix = MatchingSessionPrefix;

namespace detail::matching_layout {
BB_SIZE(MatchingRootPrefix, MATCHING_ROOT_PREFIX_SIZE);
BB_OFFSET(MatchingRootPrefix, matching_manager_interface_vtable, MATCHING_ROOT_MATCHING_MANAGER_INTERFACE_VTABLE_OFFSET);
BB_OFFSET(MatchingRootPrefix, state, MATCHING_ROOT_STATE_OFFSET);
BB_OFFSET(MatchingRootPrefix, active_session_container, MATCHING_ROOT_ACTIVE_SESSION_CONTAINER_OFFSET);
BB_OFFSET(MatchingRootPrefix, owned_subobject, MATCHING_ROOT_OWNED_SUBOBJECT_OFFSET);
BB_OFFSET(MatchingRootPrefix, optional_np_subsystem, MATCHING_ROOT_OPTIONAL_NP_SUBSYSTEM_OFFSET);
BB_OFFSET(MatchingRootPrefix, matching_enabled, MATCHING_ROOT_MATCHING_ENABLED_OFFSET);
BB_OFFSET(MatchingRootPrefix, np_matching2_context, MATCHING_ROOT_NP_MATCHING2_CONTEXT_OFFSET);

BB_SIZE(NpMatching2ContextPrefix, NP_MATCHING2_CONTEXT_PREFIX_SIZE);
BB_OFFSET(NpMatching2ContextPrefix, lock_object, NP_MATCHING2_CONTEXT_LOCK_OBJECT_OFFSET);
BB_OFFSET(NpMatching2ContextPrefix, context_id, NP_MATCHING2_CONTEXT_ID_OFFSET);

BB_SIZE(MatchingSessionPrefix, MATCHING_SESSION_PREFIX_SIZE);
BB_OFFSET(MatchingSessionPrefix, state, MATCHING_SESSION_STATE_OFFSET);
BB_OFFSET(MatchingSessionPrefix, search_room_state, MATCHING_SESSION_SEARCH_ROOM_STATE_OFFSET);
BB_OFFSET(MatchingSessionPrefix, deferred_callback_object, MATCHING_SESSION_DEFERRED_CALLBACK_OBJECT_OFFSET);
BB_OFFSET(MatchingSessionPrefix, active_room_entries_begin, MATCHING_SESSION_ACTIVE_ROOM_ENTRIES_BEGIN_OFFSET);
BB_OFFSET(MatchingSessionPrefix, active_room_entries_end, MATCHING_SESSION_ACTIVE_ROOM_ENTRIES_END_OFFSET);
BB_OFFSET(MatchingSessionPrefix, container, MATCHING_SESSION_CONTAINER_OFFSET);
BB_OFFSET(MatchingSessionPrefix, container_begin, MATCHING_SESSION_CONTAINER_BEGIN_OFFSET);
BB_OFFSET(MatchingSessionPrefix, container_end, MATCHING_SESSION_CONTAINER_END_OFFSET);
BB_OFFSET(MatchingSessionPrefix, transaction_list, MATCHING_SESSION_TRANSACTION_LIST_OFFSET);
BB_OFFSET(MatchingSessionPrefix, transaction_allocator, MATCHING_SESSION_TRANSACTION_ALLOCATOR_OFFSET);
BB_OFFSET(MatchingSessionPrefix, request_descriptor_begin, MATCHING_SESSION_REQUEST_DESCRIPTOR_BEGIN_OFFSET);
BB_OFFSET(MatchingSessionPrefix, request_descriptor_end, MATCHING_SESSION_REQUEST_DESCRIPTOR_END_OFFSET);
BB_OFFSET(MatchingSessionPrefix, internal_lock, MATCHING_SESSION_INTERNAL_LOCK_OFFSET);
BB_OFFSET(MatchingSessionPrefix, room_storage_prefix, MATCHING_SESSION_ROOM_STORAGE_OFFSET);
BB_OFFSET(MatchingSessionPrefix, event_arg, MATCHING_SESSION_EVENT_ARG_OFFSET);
BB_OFFSET(MatchingSessionPrefix, event_code, MATCHING_SESSION_EVENT_CODE_OFFSET);
BB_OFFSET(MatchingSessionPrefix, event_payload, MATCHING_SESSION_EVENT_PAYLOAD_OFFSET);
BB_OFFSET(MatchingSessionPrefix, request_id, MATCHING_SESSION_REQUEST_ID_OFFSET);
BB_OFFSET(MatchingSessionPrefix, event_status_a, MATCHING_SESSION_EVENT_STATUS_A_OFFSET);
BB_OFFSET(MatchingSessionPrefix, event_status_b, MATCHING_SESSION_EVENT_STATUS_B_OFFSET);
BB_OFFSET(MatchingSessionPrefix, pending_callback_owner, MATCHING_SESSION_PENDING_CALLBACK_OWNER_OFFSET);
}  // namespace detail::matching_layout

}  // namespace bb
