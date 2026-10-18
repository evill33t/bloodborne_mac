// SprjSessionManager: native room membership and route ownership. Owns the
// Matching2 session role, room/session handles, peer/member records, deferred
// outbound packets and native leave state - not remote PlayerIns construction:
// accepted membership events are consumed by WorldSessionObjectMan, which
// bridges them into CSMultiPlayMan tasks.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/session_connect_state_step.hpp"
#include "bbhost/engine/sprj/network_client_man.hpp"

namespace bb {

struct MatchingSessionPrefix;

inline constexpr Rva SPRJ_SESSION_MANAGER_CONSTRUCT_FN{0x1ece640};
inline constexpr Rva SPRJ_SESSION_MANAGER_INITIALIZE_FN{0x1eceed0};
inline constexpr Rva SPRJ_SESSION_MANAGER_DESTRUCT_FN{0x1ecf1c0};
// Matching event callback: copies result arrays for events 0x28/0x29 and
// queues lifecycle events for the game-thread session-manager update.
inline constexpr Rva SPRJ_SESSION_MANAGER_MATCHING_EVENT_CALLBACK_FN{0x1ed64f0};
// Appends an owning event copy to a native list. The caller owns locking.
inline constexpr Rva SPRJ_SESSION_EVENT_LIST_APPEND_FN{0x272f840};
inline constexpr Rva SPRJ_SESSION_EVENT_CONSTRUCT_WITH_OBJECT_REF_FN{0x0c88fb0};
// Retains ObjectRefs and copies owned containers; not a bytewise copy.
inline constexpr Rva SPRJ_SESSION_EVENT_COPY_CONSTRUCT_FN{0x0c8a340};
// Releases event contents, but does not free the containing event/node.
inline constexpr Rva SPRJ_SESSION_EVENT_DESTRUCT_FN{0x0c8ae10};
inline constexpr std::size_t SPRJ_SESSION_EVENT_SIZE = 0x198;
inline constexpr std::size_t SPRJ_SESSION_EVENT_NODE_SIZE = 0x1a8;
// Installs two matching-session channel descriptors using the receive callback below.
inline constexpr Rva SPRJ_SESSION_MANAGER_CONFIGURE_RECEIVE_CHANNELS_FN{0x1ed00d0};
// Copies borrowed incoming bytes into the owning +0x290 buffer under +0x2b8's mutex.
// Native argument order: peer ObjectRef, message type, bytes, size, manager context.
inline constexpr Rva SPRJ_SESSION_MANAGER_RECEIVE_PAYLOAD_CALLBACK_FN{0x1ed6780};
// Protected append catches the native container-allocation longjmp and returns false.
// On failure the producer remains responsible for freeing its payload bytes.
inline constexpr Rva SPRJ_SESSION_RECEIVE_PAYLOAD_TRY_APPEND_FN{0x1ed6c30};
inline constexpr std::int32_t SPRJ_SESSION_RECEIVE_PAYLOAD_RETRY_COUNT = 0x708;
inline constexpr Rva SPRJ_SESSION_PEER_REGISTRY_ADD_FN{0x1ecc970};
inline constexpr Rva SPRJ_SESSION_PEER_REGISTRY_REMOVE_FN{0x1ecce30};
inline constexpr Rva SPRJ_SESSION_PEER_REGISTRY_CLEAR_FN{0x1ecd0d0};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVERS_ADD_FN{0x1ecc130};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVERS_REMOVE_FN{0x1ecc2d0};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVERS_CLEAR_FN{0x1ecc3e0};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVERS_READ_FN{0x1ecc450};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVER_CONSTRUCT_FN{0x1ecd180};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVER_DESTRUCT_FN{0x1ecd4a0};
inline constexpr Rva SPRJ_SESSION_PEER_RECEIVER_READ_FN{0x1ecd750};
// Constructor-proven queue count, not a party-size limit. It does not prove
// which of these queues a gameplay packet ID is received on.
inline constexpr std::size_t SPRJ_SESSION_PEER_RECEIVER_QUEUE_COUNT = 0x3a;

// Starts the asynchronous native leave path. Roles 0/2/5 return false without
// changing state. Other values write role 7 and call the guarded matching
// wrappers. Success allocates a leave task at +0x280 when absent and sets its
// request bit 0. An existing task returns true without rearming that bit or
// resetting its timer. Matching failure invokes ResetToIdle and returns false.
// A true return therefore does not establish completed disconnection.
inline constexpr Rva SPRJ_SESSION_MANAGER_BEGIN_LEAVE_SESSION_FN{0x1ed07a0};
// Native session-manager update. It refreshes route ownership, dispatches
// queued Matching2 events, advances the asynchronous leave task, and drains
// received payloads.
inline constexpr Rva SPRJ_SESSION_MANAGER_UPDATE_AND_DISPATCH_SESSION_EVENTS_FN{0x1ed0b20};
// Native idle reset: clears the active route, peer containers and +0x138 list, requests
// completion of an installed leave task, and returns the role to Idle.
// The task remains installed until the normal update observes step -1 and
// destroys it. Role Idle alone therefore does not prove task retirement.
// This is not a universal drain: the +0x158 list and event/payload queues remain.
inline constexpr Rva SPRJ_SESSION_MANAGER_RESET_TO_IDLE_AND_CLEAR_SESSION_STATE_FN{0x1ed0910};
// Refreshes the active route and route buffer for host/client roles, or clears
// both when the matching session is unavailable.
inline constexpr Rva SPRJ_SESSION_MANAGER_REFRESH_OR_CLEAR_ACTIVE_ROUTE_FN{0x1ed2120};
// Finds or creates a peer's deferred outbound queue and takes ownership of
// the caller-allocated framed bytes. This void method does not confirm delivery.
inline constexpr Rva SPRJ_SESSION_MANAGER_QUEUE_PEER_SEND_FN{0x1ed2310};
// Removes matching deferred-send records by ObjectRef and frees their queued
// payloads, descriptors, nodes and retained reference. Historical name retained.
inline constexpr Rva SPRJ_SESSION_MANAGER_REMOVE_ROUTE_UPDATE_FOR_PEER_FN{0x1ed2650};
// Clears both the per-peer deferred sends and the secondary descriptor list.
// List sentinels survive. WorldSessionObjectMan reset invokes this after
// clearing its receive records; SprjSessionManager ResetToIdle does not.
inline constexpr Rva SPRJ_SESSION_MANAGER_CLEAR_DEFERRED_SENDS_FN{0x1ed2710};
inline constexpr Rva SPRJ_SESSION_PEER_DEFERRED_SENDS_DESTRUCT_FN{0x1ed6ca0};

// Returns whether the locked ObjectRef currently contains a non-null object.
inline constexpr Rva OBJECT_REF_IS_VALID_FN{0x0ca1400};
// Copy-constructs a locked ObjectRef wrapper.
inline constexpr Rva OBJECT_REF_COPY_CONSTRUCT_FN{0x0ca1500};
// Releases and clears a locked ObjectRef wrapper.
inline constexpr Rva OBJECT_REF_RESET_FN{0x0ca1570};
// Compares two locked ObjectRef wrappers by their native target identity.
inline constexpr Rva OBJECT_REF_MATCHES_TARGET_FN{0x0ca1c70};

inline constexpr std::size_t SPRJ_SESSION_MANAGER_SIZE = 0x308;
inline constexpr std::size_t CS_SESSION_CONNECT_STATE_STEP_SIZE = CSSessionConnectStateStep::SIZE;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_TABLE = CSSessionConnectStateStep::STEP_TEMPLATE.table;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_UPDATE_FN = CSSessionConnectStateStep::DISPATCH_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_IS_FINISHED_FN = CSSessionConnectStateStep::IS_FINISHED_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_INIT_FN = CSSessionConnectStateStep::INIT_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_POLL_REQUEST_FN = CSSessionConnectStateStep::POLL_REQUEST_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_LEAVE_FN = CSSessionConnectStateStep::LEAVE_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_LEAVE_WAIT_FN = CSSessionConnectStateStep::LEAVE_WAIT_FN;
inline constexpr Rva CS_SESSION_CONNECT_STATE_STEP_FINISH_FN = CSSessionConnectStateStep::FINISH_FN;

inline constexpr std::size_t SPRJ_SESSION_MANAGER_LUA_EVENT_CALLBACK_VTABLE_OFFSET = 0x10;
// Guarded matching-session handle passed as the first argument to
// RVA 0x0c8f500(SprjSessionManager + 0x18, SprjSessionManager + 0x38, 1).
inline constexpr std::size_t SPRJ_SESSION_MANAGER_MATCHING_SESSION_HANDLE_OFFSET = 0x18;
// Join-room result buffer refreshed/read by RVA 0x0c8f500.
inline constexpr std::size_t SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_OFFSET = 0x38;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SOURCE_ROUTE_CONFIG_OFFSET = SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_OFFSET;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ACTIVE_ROUTE_OFFSET = 0xd0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ROUTE_CONFIG_BUFFER_OFFSET = 0xf0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_CLOSING_OFFSET = 0x118;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_SUB_STATE_OFFSET = 0x120;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ROLE_OFFSET = 0x124;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SUMMON_PENDING_LIST_OFFSET = 0x138;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SUMMON_PENDING_COUNT_OFFSET = 0x140;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SUMMON_PENDING_ALLOCATOR_OFFSET = 0x148;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_OFFSET = 0x158;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_COUNT_OFFSET = 0x160;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_ALLOCATOR_OFFSET = 0x168;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_PEER_CONTAINER_170_OFFSET = 0x170;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_PEER_CONTAINER_178_OFFSET = 0x178;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ROUTE_UPDATE_LIST_OFFSET = 0x188;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ROUTE_UPDATE_COUNT_OFFSET = 0x190;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ROUTE_UPDATE_ALLOCATOR_OFFSET = 0x198;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_EVENT_LIST_OFFSET = 0x1c8;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_EVENT_COUNT_OFFSET = 0x1d0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_EVENT_ALLOCATOR_OFFSET = 0x1d8;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_PEER_LIST_OFFSET = 0x220;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_MEMBER_LIST_OFFSET = 0x248;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_MATCHING_SERVICE_STATUS_OFFSET = 0x270;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_READY_FLAG_OFFSET = 0x278;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_MEMBER_COUNT_MET_FLAG_OFFSET = 0x279;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_LEAVE_TASK_OFFSET = 0x280;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_RECEIVE_PAYLOAD_OFFSET = 0x290;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_PAYLOAD_MUTEX_OFFSET = 0x2b8;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_PAYLOAD_PTHREAD_MUTEX_OFFSET = 0x2c0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_BANDWIDTH_THRESHOLD_OFFSET = 0x2d0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_ACCUMULATED_BANDWIDTH_OFFSET = 0x2e0;
inline constexpr std::size_t SPRJ_SESSION_MANAGER_CURRENT_BANDWIDTH_COUNTER_OFFSET = 0x2f8;

inline constexpr std::size_t SPRJ_SESSION_ROUTE_CONFIG_SIZE = 0x20;
inline constexpr std::size_t SPRJ_SESSION_MATCHING_SESSION_HANDLE_SIZE = 0x20;
inline constexpr std::size_t SPRJ_SESSION_JOIN_ROOM_RESULT_SIZE = 0x98;
inline constexpr std::size_t SPRJ_SESSION_OBJECT_REF_SIZE = 0x20;
inline constexpr std::size_t SPRJ_SESSION_MEMBER_LIST_ENTRY_SIZE = 0x38;
inline constexpr std::size_t SPRJ_SESSION_MEMBER_LIST_ENTRY_OBJECT_REF_OFFSET = 0x10;
inline constexpr std::size_t SPRJ_SESSION_MEMBER_LIST_ENTRY_SLOT_OFFSET = 0x30;

enum class SprjSessionRole : std::int32_t {
    Idle = 0,
    TryingToHost = 1,
    FailedToHost = 2,
    Host = 3,
    TryingToJoin = 4,
    JoinFailed = 5,
    Client = 6,
    Leaving = 7,
};

// Matching-service availability result cached at SprjSessionManager+0x270.
// Values 1..4 are mapped from their exact Orbis NP errors by RVA 0x0cc0340;
// 0, 5 and 6 follow that function's control flow.
enum class SprjMatchingServiceStatus : std::int32_t {
    Available = 0,
    NotYetInService = 1,
    ServiceEnded = 2,
    Maintenance = 3,
    AgeRestricted = 4,
    Unavailable = 5,
    WaitingForReadiness = 6,
};

// Entry walked by the session send/receive paths: several iterate
// SprjSessionManager + 0xf8..+0x100 with a 0x20 stride. Internals unnamed.
struct SprjSessionRouteConfig {
    Unknown<SPRJ_SESSION_ROUTE_CONFIG_SIZE> _unk00;
};

// Guarded pointer stored at SprjSessionManager + 0x18. The wrappers in RVA
// range 0x0c8f000..0x0c90000 lock the inline object at +0x08, check the
// nullable session pointer at +0x00, then dispatch to the matching session.
struct SprjSessionMatchingSessionHandle {
    MatchingSessionPrefix* session;
    Unknown<0x18> lock;
};

// Session buffer shape observed at SprjSessionManager + 0xf0. The send paths
// use begin and end; the surrounding slots stay opaque.
template <typename T>
struct SprjSessionBuffer {
    void* _unk00;
    T* begin;
    T* end;
    T* capacity;
    void* allocator;

    std::size_t len() const { return begin ? static_cast<std::size_t>(end - begin) : 0; }
    bool is_empty() const { return len() == 0; }
    T& operator[](std::size_t i) const { return begin[i]; }
};

// Contiguous 0x10-byte result element copied on matching event 0x28. Native
// copy constructor RVA 0x0c8e4c0 installs the vtable and copies two dwords
// (meanings not established). Not a WorldSessionObjectMan member node.
struct SprjSessionPeerListEntry {
    const void* vtable;
    std::uint32_t value_08;
    std::uint32_t value_0c;
};

// Contiguous 0x0c-byte result element copied on matching event 0x29. RVA
// 0x1499e60 and RVA 0x0c91ac0 establish the stride and three dword copies.
struct SprjSessionMemberResultEntry {
    std::uint32_t values[3];
};

// Shared/ObjectRef-shaped peer handle used by session member nodes.
struct SprjSessionObjectRef {
    void* object;
    void* mutex_vtable;
    Unknown<0x10> _mutex_storage;
};

// Deferred callback payload at SprjSessionManager+0x290, stride 0x38. The
// manager copies the queue under its payload lock, then invokes the callback
// outside it. Accepted/exhausted entries free their payload; rejected entries
// with retries remaining are requeued and keep it.
struct SprjSessionReceivePayloadEntry {
    SprjSessionObjectRef peer_object_ref;
    std::uint32_t message_type;
    Unknown<0x04> _unk24;
    std::uint8_t* payload;
    std::uint32_t payload_size;
    // Starts at 1,800 in the registered callback; decremented after a rejected
    // consumer callback. Not seconds or packet ACK retries.
    std::int32_t retries_remaining;
};

// Slot metadata attached to a session member. RVA 0x178b5e0 case 0xe writes
// it as one qword at SprjSessionMemberListEntry + 0x30: low dword the player
// slot selected from GameDataMan + 0x18, high dword a generated member id.
// RVA 0x1e50090 consumes only index.
struct SprjSessionMemberSlot {
    static constexpr std::int32_t UNASSIGNED = -1;
    // Stock remote GameData slot count; the local player is not in this range.
    static constexpr std::int32_t MAX_PLAYER_SLOTS = 4;

    std::int32_t index;
    std::uint32_t generated_id;

    bool is_assigned() const { return index >= 0 && index < MAX_PLAYER_SLOTS; }
    bool as_index(std::size_t* out) const {
        if (!is_assigned()) return false;
        *out = static_cast<std::size_t>(index);
        return true;
    }
};

// Linked member node used by WorldSessionObjectMan + 0x68. Not the element
// type of SprjSessionManager+0x248 (that buffer holds 0x0c-byte matching
// results, not these 0x38-byte nodes).
struct SprjSessionMemberListEntry {
    SprjSessionMemberListEntry* next;
    SprjSessionMemberListEntry* previous;
    SprjSessionObjectRef peer_object_ref;
    SprjSessionMemberSlot slot;
};

// Container layout of the session-owned peer and packet lists. The leading
// qword is not initialized by the observed list constructors. Native code owns
// nodes and sentinels.
template <typename T>
struct SprjSessionList {
    Unknown<0x08> _unk00;
    T* sentinel;
    std::size_t count;
    void* allocator;
};

// Node in manager +0x138: native construction allocates 0x30 bytes, align 8.
// The sentinel has this size but its ObjectRef is not constructed. Event 0x0f
// can compact retained references into earlier nodes before erasing the tail:
// a node address is not a stable peer identity.
struct SprjSessionPeerReferenceNode {
    SprjSessionPeerReferenceNode* next;
    SprjSessionPeerReferenceNode* previous;
    SprjSessionObjectRef peer_object_ref;
};

// Separate manager +0x158 list. Constructor/destructor establish the links,
// retained reference and 0x38 allocation; producers and trailing data are not
// established. Not a live actor/member list.
struct SprjSessionReferenceNode158 {
    SprjSessionReferenceNode158* next;
    SprjSessionReferenceNode158* previous;
    SprjSessionObjectRef peer_object_ref;
    Unknown<0x08> _unk30;
};

// Owned NexusRevolution session event - not an on-wire packet or Matching2
// callback payload. Constructor/copy/destructor and native getters prove these
// boundaries. Do not memcpy or free this native owner from an inspection tool.
struct SprjSessionEvent {
    std::uint32_t event_type;
    // Native getter at RVA 0x0c8b120; interpretation depends on event_type.
    std::uint8_t result;
    Unknown<0x03> _pad05;
    std::uint32_t unknown_08;
    Unknown<0x04> _pad0c;
    std::uint64_t unknown_10;
    std::uint32_t unknown_18;
    Unknown<0x04> _pad1c;
    SprjSessionObjectRef peer_object_ref;
    SprjSessionBuffer<SprjSessionObjectRef> peer_references;
    // Getter RVA 0x0c8b170; each owned element occupies 0x98 bytes.
    SprjSessionBuffer<Unknown<0x98>> session_results;
    SprjSessionBuffer<Unknown<0x28>> factory_results;
    // Event 0x28 copies this into SprjSessionManager+0x220.
    SprjSessionBuffer<SprjSessionPeerListEntry> peer_results;
    // Event 0x29 copies this into SprjSessionManager+0x248.
    SprjSessionBuffer<SprjSessionMemberResultEntry> member_results;
    // Optional owned 0x88-byte object, deep-copied by the copy constructor.
    Unknown<0x88>* owned_payload;
    SprjSessionBuffer<Unknown<0x38>> unknown_results;
    Unknown<0x20> _unk138;
    Unknown<0x08> _string_prefix;
    // Getter returns self+0x158 (prefix included). Event 0x3a passes its
    // inline/heap UTF-16 data and result to the context callback immediately.
    SprjNetworkWideSmallString text;
    std::uint8_t unknown_188;
    Unknown<0x07> _pad189;
    // Shallow-copied, not freed by the event destructor. Getter RVA 0x0c8b2f0.
    void* callback_context;
};

// The list sentinel at manager+0x1c8 owns 0x1a8-byte nodes. Only next/previous
// are initialized in the sentinel; its event storage is not a live event.
struct SprjSessionEventNode {
    SprjSessionEventNode* next;
    SprjSessionEventNode* previous;
    SprjSessionEvent event;
};

// Inline PS4 DLLightMutex proved by constructor and unlock. The pthread value
// is an opaque handle, not an inline host pthread_mutex_t.
struct SprjSessionEventMutex {
    const void* vtable;
    void* pthread_handle;
    std::uint8_t initialized;
    Unknown<0x07> _pad11;
};

// Owned descriptor allocated by QueuePeerSend, size 0x18. Bytes already
// include the WorldSessionPacketHeader. The queue stores the caller's buffer
// without copying; successful retry or queue teardown frees it.
struct SprjSessionDeferredSend {
    const void* vtable;
    std::uint32_t byte_size;
    Unknown<0x04> _unk0c;
    std::uint8_t* bytes;
};

struct SprjSessionDeferredSendNode {
    SprjSessionDeferredSendNode* next;
    SprjSessionDeferredSendNode* previous;
    SprjSessionDeferredSend* packet;
};

// Per-peer deferred-send owner, size 0x40. Native update retries every queued
// buffer with send mode 1: success frees buffer, descriptor and node; failure
// keeps them. The empty peer owner remains until explicit removal.
struct SprjSessionPeerDeferredSends {
    SprjSessionObjectRef peer_object_ref;
    SprjSessionList<SprjSessionDeferredSendNode> packets;
};

// The list at manager +0x188 owns these 0x18-byte nodes and their separate
// 0x40-byte peer records. Its count is peer queue owners, not packets.
struct SprjSessionPeerDeferredSendsNode {
    SprjSessionPeerDeferredSendsNode* next;
    SprjSessionPeerDeferredSendsNode* previous;
    SprjSessionPeerDeferredSends* peer;
};

// 0x20-byte nested node; payload semantics not established.
struct SprjSessionPeerValueNode {
    SprjSessionPeerValueNode* next;
    SprjSessionPeerValueNode* previous;
    std::uint64_t value[2];
};

// 0x50-byte node matched by ObjectRef, never by list position.
struct SprjSessionPeerRegistryNode {
    SprjSessionPeerRegistryNode* next;
    SprjSessionPeerRegistryNode* previous;
    SprjSessionObjectRef peer_object_ref;
    SprjSessionList<SprjSessionPeerValueNode> values;
};

// Allocation at SprjSessionManager+0x170, size 0x40 (descriptive names).
// Initialize allocates both list sentinels; event 0x0e adds a peer and 0x0f
// removes it. ClearPeers empties only peers; destruction also clears/frees
// values and both lists.
struct SprjSessionPeerRegistry {
    SprjSessionList<SprjSessionPeerRegistryNode> peers;
    SprjSessionList<SprjSessionPeerValueNode> values;
};

// Heap-owned payload descriptor referenced by the per-type receiver lists.
struct SprjSessionQueuedPayload {
    std::int32_t size;
    Unknown<0x04> _unk04;
    std::uint8_t* data;
};

struct SprjSessionPayloadNode {
    SprjSessionPayloadNode* next;
    SprjSessionPayloadNode* previous;
    SprjSessionQueuedPayload* payload;
};

// Separately allocated 0x88-byte object owned by the vector at manager+0x178.
// Removal destroys it and fills its vector position with the last pointer, so
// vector indices cannot identify persistent peers.
//
// Read (RVA 0x1ecd750) selects one of 58 queues, copies a fitting payload and
// frees its bytes, descriptor and node. Zero means nothing delivered (blocked
// type, empty queue, invalid type, no fitting payload). The read wrapper has
// no direct code callers in this binary; active gameplay use is not shown.
struct SprjSessionPeerReceiver {
    const void* vtable;
    SprjSessionObjectRef peer_object_ref;
    // Optional callback; virtual +8 can prevent reading the requested type.
    void* read_filter;
    SprjSessionBuffer<SprjSessionList<SprjSessionPayloadNode>> queues;
    Unknown<0x08> _unk58;
    // Constructor points this at self+0x58; purpose not established.
    void* storage_pointer;
    std::uint32_t storage_capacity;
    Unknown<0x1c> _unk6c;
};

struct SprjSessionManager {
    void* vftable;
    std::uint64_t _unk08;
    // Historical name: points to the consumer object, not directly to its
    // vtable. Virtual +0 consumes events; virtual +8 consumes payloads.
    void* lua_event_callback_vtable;
    SprjSessionMatchingSessionHandle matching_session_handle;
    Unknown<SPRJ_SESSION_JOIN_ROOM_RESULT_SIZE> join_room_result;
    Unknown<0x20> active_route;
    SprjSessionBuffer<SprjSessionRouteConfig> route_config_buffer;
    std::uint8_t session_closing;
    Unknown<0x07> _pad119;
    std::int32_t session_sub_state;
    std::int32_t role_raw;
    Unknown<0x10> _unk128;
    // Historical name. Peer-leave filtering and idle-reset clear are proven;
    // its producer and "summon pending" reading are not.
    SprjSessionPeerReferenceNode* summon_pending_list;
    std::size_t summon_pending_count;
    void* summon_pending_allocator;
    Unknown<0x08> _unk150;
    // Retained-reference list not cleared by the inspected idle reset.
    SprjSessionReferenceNode158* reference_list_158;
    std::size_t reference_list_158_count;
    void* reference_list_158_allocator;
    // Owned 0x40-byte list container; distinct from actor membership.
    SprjSessionPeerRegistry* peer_container_170;
    // Owned 0x28-byte vector of owned 0x88-byte receiver pointers.
    SprjSessionBuffer<SprjSessionPeerReceiver*>* peer_container_178;
    Unknown<0x08> _unk180;
    // Per-peer deferred outbound queue sentinel (historical names kept); not
    // merely routing metadata or a receive queue.
    SprjSessionPeerDeferredSendsNode* route_update_list;
    std::uint64_t route_update_count;
    void* route_update_allocator;
    // Constructor/clear prove a second owned packet-descriptor list; producer
    // and destination scope not established.
    SprjSessionList<SprjSessionDeferredSendNode> secondary_packet_list;
    Unknown<0x08> _unk1c0;
    // Owned event queue consumed by UpdateAndDispatchSessionEvents. A false
    // consumer callback keeps the node; ResetToIdle does not clear it.
    SprjSessionEventNode* session_event_list;
    std::uint64_t session_event_count;
    void* session_event_allocator;
    SprjSessionEventMutex session_event_mutex;
    Unknown<0x28> _unk1f8;
    SprjSessionBuffer<SprjSessionPeerListEntry> peer_list;
    SprjSessionBuffer<SprjSessionMemberResultEntry> member_list;
    std::int32_t matching_service_status_raw;
    std::uint32_t _unk274;
    std::uint8_t ready;
    std::uint8_t member_count_met;
    Unknown<0x06> _pad27a;
    // Owned asynchronous native leave task. Cleared by the session update
    // after current_step reaches -1, not merely when role becomes Idle.
    CSSessionConnectStateStep* leave_task;
    Unknown<0x08> _unk288;
    SprjSessionBuffer<SprjSessionReceivePayloadEntry> receive_payload_buffer;
    void* payload_mutex;
    void* payload_pthread_mutex;
    Unknown<0x08> _unk2c8;
    // Historical name: compared against bytes rejected/requeued by the
    // consumer in this update, not wire bytes per second.
    std::uint32_t bandwidth_threshold;
    Unknown<0x0c> _unk2d4;
    // Historical name: accumulated update-context delta while the
    // rejected-byte total exceeds the threshold; resets to zero otherwise.
    float accumulated_bandwidth;
    Unknown<0x14> _unk2e4;
    // Historical name: sum of payload sizes requeued in the latest update.
    std::uint32_t current_bandwidth_counter;
    Unknown<0x0c> _unk2fc;

    // False when role_raw is outside the known values.
    bool role(SprjSessionRole* out) const {
        if (role_raw < 0 || role_raw > 7) return false;
        *out = static_cast<SprjSessionRole>(role_raw);
        return true;
    }
    bool matching_service_status(SprjMatchingServiceStatus* out) const {
        if (matching_service_status_raw < 0 || matching_service_status_raw > 6) return false;
        *out = static_cast<SprjMatchingServiceStatus>(matching_service_status_raw);
        return true;
    }
    bool is_session_closing() const { return session_closing != 0; }
    bool is_ready() const { return ready != 0; }
    bool is_member_count_met() const { return member_count_met != 0; }
    std::size_t route_config_count() const { return route_config_buffer.len(); }
    SprjSessionRouteConfig* route_configs() const { return route_config_buffer.begin; }
    MatchingSessionPrefix* matching_session() const { return matching_session_handle.session; }
    const Unknown<SPRJ_SESSION_JOIN_ROOM_RESULT_SIZE>* join_room_result_ptr() const { return &join_room_result; }
};

namespace detail::session_manager_layout {
using M = SprjSessionManager;
BB_SIZE(SprjSessionPeerReferenceNode, 0x30);
BB_SIZE(SprjSessionReferenceNode158, 0x38);
BB_OFFSET(SprjSessionPeerReferenceNode, peer_object_ref, 0x10);
BB_OFFSET(SprjSessionReferenceNode158, peer_object_ref, 0x10);
BB_SIZE(SprjSessionEvent, SPRJ_SESSION_EVENT_SIZE);
BB_SIZE(SprjSessionEventNode, SPRJ_SESSION_EVENT_NODE_SIZE);
BB_SIZE(SprjSessionEventMutex, 0x18);
BB_OFFSET(SprjSessionEvent, peer_object_ref, 0x20);
BB_OFFSET(SprjSessionEvent, peer_results, 0xb8);
BB_OFFSET(SprjSessionEvent, member_results, 0xe0);
BB_OFFSET(SprjSessionEvent, text, 0x160);
BB_OFFSET(SprjSessionEvent, callback_context, 0x190);
// Sizes of the entry and node types.
BB_SIZE(SprjSessionReceivePayloadEntry, 0x38);
BB_SIZE(SprjSessionPeerListEntry, 0x10);
BB_SIZE(SprjSessionMemberResultEntry, 0x0c);
BB_SIZE(SprjSessionDeferredSend, 0x18);
BB_SIZE(SprjSessionPeerDeferredSends, 0x40);
BB_SIZE(SprjSessionPeerDeferredSendsNode, 0x18);
BB_SIZE(SprjSessionPeerValueNode, 0x20);
BB_SIZE(SprjSessionPeerRegistryNode, 0x50);
BB_SIZE(SprjSessionPeerRegistry, 0x40);
BB_SIZE(SprjSessionPeerReceiver, 0x88);
BB_SIZE(SprjSessionBuffer<SprjSessionPeerReceiver*>, 0x28);
BB_SIZE(M, SPRJ_SESSION_MANAGER_SIZE);
BB_OFFSET(M, session_event_mutex, 0x1e0);
BB_OFFSET(M, summon_pending_list, 0x138);
BB_OFFSET(M, reference_list_158, 0x158);
BB_OFFSET(M, reference_list_158_count, 0x160);
BB_OFFSET(M, reference_list_158_allocator, 0x168);
BB_SIZE(SprjSessionMatchingSessionHandle, SPRJ_SESSION_MATCHING_SESSION_HANDLE_SIZE);
BB_OFFSET(M, lua_event_callback_vtable, SPRJ_SESSION_MANAGER_LUA_EVENT_CALLBACK_VTABLE_OFFSET);
BB_OFFSET(M, matching_session_handle, SPRJ_SESSION_MANAGER_MATCHING_SESSION_HANDLE_OFFSET);
BB_OFFSET(M, join_room_result, SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_OFFSET);
BB_OFFSET(M, join_room_result, SPRJ_SESSION_MANAGER_SOURCE_ROUTE_CONFIG_OFFSET);
BB_OFFSET(M, active_route, SPRJ_SESSION_MANAGER_ACTIVE_ROUTE_OFFSET);
BB_OFFSET(M, route_config_buffer, SPRJ_SESSION_MANAGER_ROUTE_CONFIG_BUFFER_OFFSET);
BB_OFFSET(M, session_closing, SPRJ_SESSION_MANAGER_SESSION_CLOSING_OFFSET);
BB_OFFSET(M, session_sub_state, SPRJ_SESSION_MANAGER_SESSION_SUB_STATE_OFFSET);
BB_OFFSET(M, role_raw, SPRJ_SESSION_MANAGER_ROLE_OFFSET);
BB_OFFSET(M, summon_pending_list, SPRJ_SESSION_MANAGER_SUMMON_PENDING_LIST_OFFSET);
BB_OFFSET(M, summon_pending_count, SPRJ_SESSION_MANAGER_SUMMON_PENDING_COUNT_OFFSET);
BB_OFFSET(M, summon_pending_allocator, SPRJ_SESSION_MANAGER_SUMMON_PENDING_ALLOCATOR_OFFSET);
BB_OFFSET(M, reference_list_158, SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_OFFSET);
BB_OFFSET(M, reference_list_158_count, SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_COUNT_OFFSET);
BB_OFFSET(M, reference_list_158_allocator, SPRJ_SESSION_MANAGER_REFERENCE_LIST_158_ALLOCATOR_OFFSET);
BB_OFFSET(M, peer_container_170, SPRJ_SESSION_MANAGER_PEER_CONTAINER_170_OFFSET);
BB_OFFSET(M, peer_container_178, SPRJ_SESSION_MANAGER_PEER_CONTAINER_178_OFFSET);
BB_OFFSET(M, route_update_list, SPRJ_SESSION_MANAGER_ROUTE_UPDATE_LIST_OFFSET);
BB_OFFSET(M, route_update_count, SPRJ_SESSION_MANAGER_ROUTE_UPDATE_COUNT_OFFSET);
BB_OFFSET(M, route_update_allocator, SPRJ_SESSION_MANAGER_ROUTE_UPDATE_ALLOCATOR_OFFSET);
BB_OFFSET(M, session_event_list, SPRJ_SESSION_MANAGER_SESSION_EVENT_LIST_OFFSET);
BB_OFFSET(M, session_event_count, SPRJ_SESSION_MANAGER_SESSION_EVENT_COUNT_OFFSET);
BB_OFFSET(M, session_event_allocator, SPRJ_SESSION_MANAGER_SESSION_EVENT_ALLOCATOR_OFFSET);
BB_OFFSET(M, peer_list, SPRJ_SESSION_MANAGER_PEER_LIST_OFFSET);
BB_OFFSET(M, member_list, SPRJ_SESSION_MANAGER_MEMBER_LIST_OFFSET);
BB_OFFSET(M, matching_service_status_raw, SPRJ_SESSION_MANAGER_MATCHING_SERVICE_STATUS_OFFSET);
BB_OFFSET(M, ready, SPRJ_SESSION_MANAGER_READY_FLAG_OFFSET);
BB_OFFSET(M, member_count_met, SPRJ_SESSION_MANAGER_MEMBER_COUNT_MET_FLAG_OFFSET);
BB_OFFSET(M, leave_task, SPRJ_SESSION_MANAGER_LEAVE_TASK_OFFSET);
BB_OFFSET(M, receive_payload_buffer, SPRJ_SESSION_MANAGER_RECEIVE_PAYLOAD_OFFSET);
BB_OFFSET(M, payload_mutex, SPRJ_SESSION_MANAGER_PAYLOAD_MUTEX_OFFSET);
BB_OFFSET(M, payload_pthread_mutex, SPRJ_SESSION_MANAGER_PAYLOAD_PTHREAD_MUTEX_OFFSET);
BB_OFFSET(M, bandwidth_threshold, SPRJ_SESSION_MANAGER_BANDWIDTH_THRESHOLD_OFFSET);
BB_OFFSET(M, accumulated_bandwidth, SPRJ_SESSION_MANAGER_ACCUMULATED_BANDWIDTH_OFFSET);
BB_OFFSET(M, current_bandwidth_counter, SPRJ_SESSION_MANAGER_CURRENT_BANDWIDTH_COUNTER_OFFSET);
using Buf = SprjSessionBuffer<std::size_t>;
BB_SIZE(Buf, 0x28);
BB_OFFSET(Buf, begin, 0x08);
BB_OFFSET(Buf, end, 0x10);
BB_SIZE(SprjSessionRouteConfig, SPRJ_SESSION_ROUTE_CONFIG_SIZE);
BB_SIZE(SprjSessionObjectRef, SPRJ_SESSION_OBJECT_REF_SIZE);
BB_SIZE(SprjSessionMemberListEntry, SPRJ_SESSION_MEMBER_LIST_ENTRY_SIZE);
BB_OFFSET(SprjSessionMemberListEntry, peer_object_ref, SPRJ_SESSION_MEMBER_LIST_ENTRY_OBJECT_REF_OFFSET);
BB_OFFSET(SprjSessionMemberListEntry, slot, SPRJ_SESSION_MEMBER_LIST_ENTRY_SLOT_OFFSET);
BB_OFFSET(SprjSessionMemberSlot, index, 0x00);
BB_OFFSET(SprjSessionMemberSlot, generated_id, 0x04);
}  // namespace detail::session_manager_layout

}  // namespace bb
