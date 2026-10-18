// WorldSessionObjectMan: application-layer peer packet queues and session membership events.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/session_manager.hpp"
#include "bbhost/engine/sprj/world_session_packet.hpp"

namespace bb {

// WorldSessionObjectMan bridges SprjSessionManager and the actor task layer.
// Event 0x0e rejects the local ObjectRef, creates or finds one
// CSMultiPlayerInsTask by semantic ObjectRef identity, selects its native mode
// from active-route ownership, and records the member slot. Event 0x0f removes
// the corresponding packet/member records and begins task retirement. Typed
// WorldSession packets are queued per peer here; Matching2 room policy and
// actual remote actor construction remain owned by adjacent managers.

// Native session-event dispatcher (WorldSessionObjectMan_DispatchSessionEvent).
// Event 0x0e creates the member/GameData inputs consumed later by
// CSMultiPlayerInsTask::STEP_Create; event 0x0f removes them when a peer leaves.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_DISPATCH_SESSION_EVENT_FN{0x178b5e0};
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_CONSTRUCT_FN{0x17875d0};
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_CLEAR_PACKET_RECORDS_FN{0x1788b30};
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_POP_PEER_MESSAGE_FN{0x1789200};
inline constexpr Rva WORLD_SESSION_PEER_POP_QUEUED_MESSAGE_FN{0x17910f0};
inline constexpr Rva WORLD_SESSION_PEER_CONSTRUCT_FN{0x17906e0};
inline constexpr Rva WORLD_SESSION_PEER_DESTRUCT_FN{0x17909f0};
// Parses a framed message (mode 0), or appends bytes to the incremental
// header/body parser (mode 1), then queues completed permitted payloads.
inline constexpr Rva WORLD_SESSION_PEER_RECEIVE_BYTES_FN{0x1790c20};
// Session-manager consumer callback. Finds the peer and increments received
// bytes, but ignores the inner parser's return value. A true callback result
// is not proof that a gameplay payload was queued or consumed.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_RECEIVE_CALLBACK_FN{0x178d510};
// Returns queued count; writes the native debug byte total (payload size +4
// per entry), not the exact transport wire-byte total, to its out argument.
inline constexpr Rva WORLD_SESSION_PEER_QUEUE_STATISTICS_FN{0x1791240};
// The locked send primitive uses one shared header+payload staging buffer.
inline constexpr Rva WORLD_SESSION_SEND_STAGING_BUFFER{0x55640f0};
inline constexpr std::size_t WORLD_SESSION_SEND_STAGING_SIZE = 0x6400;
// Failed sends with types >= 7 request irregular leave when the combined
// queued-descriptor count is >= 1001 BEFORE adding another entry. Not bytes,
// elapsed time, or the number of peers; not a strict 1000-packet capacity.
inline constexpr std::uint32_t WORLD_SESSION_SEND_BACKLOG_REJECT_THRESHOLD = 1001;
// Native MaxRegularPacketNumPerPacketType variable. Initial image value: 20.
// Only the complete-frame receive branch enforces this bound in the trace.
inline constexpr Rva WORLD_SESSION_REGULAR_PACKET_QUEUE_LIMIT{0x51282cc};
// Per-frame application packet pump. It resolves each route through the member
// list, applies peer payloads to the member's selected GameData row, and
// advances the corresponding actor task's readiness flags.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_UPDATE_PACKET_EXCHANGE_FN{0x1783160};
// Consumes packet 0x0b, writes the peer row's chr_init_param, marks slot state
// bit 0x10, and refreshes an already-created matching remote actor.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_RECEIVE_PEER_CHR_INIT_PARAM_FN{0x1782f30};
// Emits the local player's native bootstrap/snapshot sequence.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_SEND_LOCAL_BOOTSTRAP_FN{0x1781ac0};
// Performs the native world-session reset immediately or defers it while a
// transition snapshot is active. The stock cleanup accepts only GameData slot
// indices 0..3; slot expansion must replace that dependency together with the
// allocator/assignment path.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_RESET_OR_DEFER_FN{0x178a900};
// Sets WorldSessionObjectMan+0x84 to the transition-snapshot phase.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_SET_TRANSITION_SNAPSHOT_PHASE_FN{0x178d9a0};

inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_SIZE = 0x148;

inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_MEMBER_LIMIT_OFFSET = 0x0c;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_OFFSET = 0x10;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_LIST_OFFSET = 0x28;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_LIST_COUNT_OFFSET = 0x30;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_LIST_OFFSET = 0x48;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_LIST_COUNT_OFFSET = 0x50;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_MEMBER_LIST_OFFSET = 0x68;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_MEMBER_LIST_COUNT_OFFSET = 0x70;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_STATE_OFFSET = 0x84;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_TX_BYTES_CURRENT_OFFSET = 0x88;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RX_BYTES_CURRENT_OFFSET = 0x8c;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_TX_BYTES_LAST_SAMPLE_OFFSET = 0x90;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RX_BYTES_LAST_SAMPLE_OFFSET = 0x94;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_TRAFFIC_SAMPLE_TIMER_OFFSET = 0x98;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_SELECTED_SLOT_INDEX_OFFSET = 0x9c;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_SELECTED_CHR_INIT_PARAM_OFFSET = 0xa0;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_QUEUED_PACKET_BYTES_OFFSET = 0x88;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_P2P_SEND_DATA_SIZE_OFFSET = 0x90;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_P2P_RECV_DATA_SIZE_OFFSET = 0x94;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_NAMES_OFFSET = 0xa8;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_HELPER_OFFSET = 0xb0;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_MEMBER_ADD_TIMER_OFFSET = 0xb8;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C1_OFFSET = 0xc1;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C2_OFFSET = 0xc2;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C3_OFFSET = 0xc3;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RELOAD_REQUESTED_FLAG_OFFSET = 0xc4;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_LIST_OFFSET = 0xd0;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_LIST_COUNT_OFFSET = 0xd8;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_JOIN_FLAG_E8_OFFSET = 0xe8;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_JOIN_FLAG_E9_OFFSET = 0xe9;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_WAIT_MAP_RELOAD_TIMER_OFFSET = 0xec;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_SESSION_CHR_INIT_PARAM_OFFSET = 0xf0;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DISCONNECT_TIMER_OFFSET = 0xf4;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_CREATE_RETRY_COUNT_OFFSET = 0x110;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_JOIN_RETRY_COUNT_OFFSET = 0x114;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_CREATE_GUARD_OFFSET = 0x120;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_JOIN_GUARD_OFFSET = 0x121;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_DEBUG_MENU_NODE_OFFSET = 0x128;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_PACKET_SEND_LOCK_OFFSET = 0x130;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_PACKET_SEND_LOCK_READY_OFFSET = 0x140;

inline constexpr Rva WORLD_SESSION_OBJECT_MAN_SET_SYNC_READY_AND_BROADCAST_FN{0x178d9c0};
inline constexpr Rva WORLD_TRANSITION_FINALIZE_LOCAL_SESSION_PLAYER_RECORD_FN{0x15c9850};

// Native bootstrap method name; it emits the local player's bootstrap/snapshot
// sequence and is not a packet-format replacement.
inline constexpr const char* WORLD_SESSION_OBJECT_MAN_SEND_LOCAL_BOOTSTRAP_GHIDRA_NAME =
    "WorldSessionObjectMan_SendLocalBootstrap";

inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_NODE_SIZE = 0x18;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_NODE_SIZE = 0x30;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_MEMBER_NODE_SIZE = 0x38;
inline constexpr std::size_t WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_NODE_SIZE = 0x18;
inline constexpr std::size_t WORLD_SESSION_OBJECT_PACKET_RECORD_SIZE = 0x88;
inline constexpr std::size_t WORLD_SESSION_PACKET_TYPE_COUNT = 0x44;
inline constexpr std::size_t WORLD_SESSION_OBJECT_PACKET_RECORD_OBJECT_REF_OFFSET = 0x08;
inline constexpr std::size_t WORLD_SESSION_OBJECT_PACKET_RECORD_QUEUE_TABLE_OFFSET = 0x28;
inline constexpr std::size_t WORLD_SESSION_OBJECT_PACKET_RECORD_DEBUG_MENU_HANDLE_OFFSET = 0x80;
inline constexpr std::size_t WORLD_SESSION_PACKET_TYPE_QUEUE_SIZE = 0x20;
inline constexpr std::size_t WORLD_SESSION_PACKET_PAYLOAD_SIZE = 0x10;
inline constexpr std::size_t WORLD_SESSION_PACKET_PAYLOAD_BYTE_SIZE_OFFSET = 0x00;
inline constexpr std::size_t WORLD_SESSION_PACKET_PAYLOAD_POINTER_OFFSET = 0x08;
// Stock number of remote GameData player slots accepted by reset and member
// cleanup. Not the total party size: the local player is separate.
inline constexpr std::size_t WORLD_SESSION_STOCK_REMOTE_PLAYER_SLOT_COUNT = 4;

enum class WorldSessionObjectState : std::int32_t {
    Idle = 0,
    JoinedNeedsSync = 1,
    EventFlagsSynced = 2,
    TransitionSnapshotPrepared = 3,
    HostSyncReady = 4,
};

template <typename T>
struct WorldSessionObjectList {
    T* sentinel;
    std::size_t count;
    void* allocator;
};

struct WorldSessionObjectPacketRecord;

// Owns the separately allocated per-peer packet record. Clearing the manager
// destroys that record before freeing this list node.
struct WorldSessionObjectPacketNode {
    WorldSessionObjectPacketNode* next;
    WorldSessionObjectPacketNode* previous;
    WorldSessionObjectPacketRecord* record;
};

struct WorldSessionActivePeerObjectNode {
    WorldSessionActivePeerObjectNode* next;
    WorldSessionActivePeerObjectNode* previous;
    SprjSessionObjectRef peer_object_ref;
};

using WorldSessionReceivePacketNode = Unknown<0x18>;

struct WorldSessionPacketPayload {
    // Native pop compares this as a signed length against caller capacity.
    std::int32_t byte_size;
    Unknown<0x04> _pad04;
    std::uint8_t* payload;
};

struct WorldSessionPacketPayloadNode {
    WorldSessionPacketPayloadNode* next;
    WorldSessionPacketPayloadNode* previous;
    WorldSessionPacketPayload* payload;
};

// Allocated list object, not an inline WorldSessionObjectList header: the
// leading qword precedes the sentinel. Empty sentinel payload is invalid.
struct WorldSessionPacketTypeQueue {
    Unknown<0x08> _unk00;
    WorldSessionPacketPayloadNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Vector at peer-record +0x28; its begin pointer is at record +0x30. Entries
// point to separately allocated queue objects, unlike the inline list array
// in SprjSessionManager's 58-type receiver.
struct WorldSessionPacketTypeQueueTable {
    Unknown<0x08> _unk00;
    WorldSessionPacketTypeQueue** begin;
    WorldSessionPacketTypeQueue** end;
    WorldSessionPacketTypeQueue** capacity;
    void* allocator;

    std::size_t size() const { return begin ? static_cast<std::size_t>(end - begin) : 0; }
    // The queue for a message type, or null.
    WorldSessionPacketTypeQueue* queue(std::size_t type) const { return type < size() ? begin[type] : nullptr; }
};

// The same locked 0x20-byte ObjectRef used by native session membership.
using WorldSessionPacketObjectRef = SprjSessionObjectRef;

// Per-peer object packet record allocated at 0x88 bytes by the case 0x0e
// session-member join path. RVA 0x1789200 matches this record by the ObjectRef
// at +0x08. RVA 0x17910f0 indexes the queue table by message type, then pops a
// WorldSessionPacketPayload. Oversized entries are skipped until a payload
// fits; the pop helper does not implement strict front-only FIFO delivery.
// Constructor initializes 68 type queues plus the incremental frame parser.
struct WorldSessionObjectPacketRecord {
    void* vftable;
    WorldSessionPacketObjectRef peer_object_ref;
    WorldSessionPacketTypeQueueTable queue_table;
    WorldSessionPacketHeader incoming_header;
    // Initially points at incoming_header; advances during partial headers.
    std::uint8_t* header_write_cursor;
    std::int32_t header_bytes_remaining;
    std::int32_t incoming_message_type;
    // Non-null while a mode-1 body is incomplete. Enqueue transfers ownership
    // to a list node, then clears this field. The inspected destructor does
    // not free this in-progress allocation. Neither inspected full-reset nor
    // event-0x0f caller drains it first; runtime reachability remains unproven.
    WorldSessionPacketPayload* incoming_payload;
    std::uint8_t* payload_write_cursor;
    std::int32_t payload_bytes_remaining;
    Unknown<0x04> _unk7c;
    void* debug_menu_handle;
};

// Inline packet-send lock/subobject used by RVA 0x17894e0. The lock
// enter/leave calls go through vfuncs from the subobject at +0x130.
// Constructor xrefs also expose the readiness byte at parent offset +0x140.
struct WorldSessionPacketSendLock {
    Unknown<0x10> _unk00;
    std::uint8_t ready;
    Unknown<0x07> _unk11;
};

struct WorldSessionObjectMan {
    void* vftable;
    std::uint8_t flag_08;
    std::uint8_t flag_09;
    Unknown<0x02> _pad0a;
    // Total multiplayer member limit shown by the debug menu as
    // `Member Limit <%d>`. The stock value is 5.
    std::int32_t member_limit;
    void* packet_storage;
    std::uint32_t network_mode_gate;
    Unknown<0x0c> _unk1c;
    WorldSessionObjectList<WorldSessionObjectPacketNode> object_packet_list;
    Unknown<0x08> _unk40;
    WorldSessionObjectList<WorldSessionActivePeerObjectNode> active_peer_object_list;
    Unknown<0x08> _unk60;
    WorldSessionObjectList<SprjSessionMemberListEntry> member_list;
    std::uint8_t member_list_dirty;
    Unknown<0x03> _pad81;
    std::int32_t state_raw;  // WorldSessionObjectState
    std::int32_t tx_bytes_current;
    std::uint32_t rx_bytes_current;
    std::uint32_t tx_bytes_last_sample;
    std::uint32_t rx_bytes_last_sample;
    float traffic_sample_timer;
    std::int32_t selected_slot_index;
    std::int32_t selected_chr_init_param;
    Unknown<0x04> _unk_a4;
    void* packet_storage_names;
    void* packet_storage_helper;
    float member_add_timer;
    Unknown<0x05> _unk_bc;
    std::uint8_t debug_network_event_latch_c1;
    std::uint8_t debug_network_event_latch_c2;
    std::uint8_t debug_network_event_latch_c3;
    std::uint8_t reload_requested;
    std::uint8_t reload_blocked;
    Unknown<0x0a> _unk_c6;
    WorldSessionObjectList<WorldSessionReceivePacketNode> receive_packet_list;
    std::uint8_t join_flag_e8;
    std::uint8_t join_flag_e9;
    Unknown<0x02> _pad_ea;
    float wait_map_reload_timer;
    std::uint32_t session_chr_init_param;
    float disconnect_timer;
    std::uint32_t packet_info_index;
    std::uint8_t accept_force_join;
    Unknown<0x03> _pad_fd;
    Unknown<0x10> _timer_debug_node;
    std::uint32_t create_retry_count;
    std::uint32_t join_retry_count;
    std::uint8_t debug_join_guard;
    Unknown<0x03> _pad119;
    std::uint32_t debug_signin_user_id;
    std::uint8_t debug_create_guard;
    std::uint8_t debug_join_guard_121;
    Unknown<0x06> _pad122;
    void* packet_storage_debug_menu;
    WorldSessionPacketSendLock packet_send_lock;

    WorldSessionObjectState state() const { return static_cast<WorldSessionObjectState>(state_raw); }
    std::size_t object_packet_count() const { return object_packet_list.count; }
    std::size_t active_peer_object_count() const { return active_peer_object_list.count; }
    std::size_t member_count() const { return member_list.count; }
    std::size_t receive_packet_count() const { return receive_packet_list.count; }
};

namespace detail::world_session_object_man_layout {
using ListProbe = WorldSessionObjectList<std::uintptr_t>;
BB_SIZE(WorldSessionObjectMan, WORLD_SESSION_OBJECT_MAN_SIZE);
BB_SIZE(ListProbe, 0x18);
BB_OFFSET(WorldSessionObjectMan, member_limit, WORLD_SESSION_OBJECT_MAN_MEMBER_LIMIT_OFFSET);
BB_OFFSET(WorldSessionObjectMan, packet_storage, WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, object_packet_list, WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_LIST_OFFSET);
static_assert(offsetof(ListProbe, count) + offsetof(WorldSessionObjectMan, object_packet_list) ==
                  WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_LIST_COUNT_OFFSET,
              "object packet list count");
static_assert(offsetof(ListProbe, count) + offsetof(WorldSessionObjectMan, active_peer_object_list) ==
                  WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_LIST_COUNT_OFFSET,
              "active peer object list count");
static_assert(offsetof(ListProbe, count) + offsetof(WorldSessionObjectMan, member_list) ==
                  WORLD_SESSION_OBJECT_MAN_MEMBER_LIST_COUNT_OFFSET,
              "member list count");
static_assert(offsetof(ListProbe, count) + offsetof(WorldSessionObjectMan, receive_packet_list) ==
                  WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_LIST_COUNT_OFFSET,
              "receive packet list count");
BB_OFFSET(WorldSessionObjectMan, active_peer_object_list, WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_LIST_OFFSET);
BB_OFFSET(WorldSessionObjectMan, member_list, WORLD_SESSION_OBJECT_MAN_MEMBER_LIST_OFFSET);
BB_OFFSET(WorldSessionObjectMan, state_raw, WORLD_SESSION_OBJECT_MAN_STATE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, tx_bytes_current, WORLD_SESSION_OBJECT_MAN_QUEUED_PACKET_BYTES_OFFSET);
BB_OFFSET(WorldSessionObjectMan, tx_bytes_current, WORLD_SESSION_OBJECT_MAN_TX_BYTES_CURRENT_OFFSET);
BB_OFFSET(WorldSessionObjectMan, rx_bytes_current, WORLD_SESSION_OBJECT_MAN_RX_BYTES_CURRENT_OFFSET);
BB_OFFSET(WorldSessionObjectMan, tx_bytes_last_sample, WORLD_SESSION_OBJECT_MAN_P2P_SEND_DATA_SIZE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, tx_bytes_last_sample, WORLD_SESSION_OBJECT_MAN_TX_BYTES_LAST_SAMPLE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, rx_bytes_last_sample, WORLD_SESSION_OBJECT_MAN_P2P_RECV_DATA_SIZE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, rx_bytes_last_sample, WORLD_SESSION_OBJECT_MAN_RX_BYTES_LAST_SAMPLE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, traffic_sample_timer, WORLD_SESSION_OBJECT_MAN_TRAFFIC_SAMPLE_TIMER_OFFSET);
BB_OFFSET(WorldSessionObjectMan, selected_slot_index, WORLD_SESSION_OBJECT_MAN_SELECTED_SLOT_INDEX_OFFSET);
BB_OFFSET(WorldSessionObjectMan, selected_chr_init_param, WORLD_SESSION_OBJECT_MAN_SELECTED_CHR_INIT_PARAM_OFFSET);
BB_OFFSET(WorldSessionObjectMan, packet_storage_names, WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_NAMES_OFFSET);
BB_OFFSET(WorldSessionObjectMan, packet_storage_helper, WORLD_SESSION_OBJECT_MAN_PACKET_STORAGE_HELPER_OFFSET);
BB_OFFSET(WorldSessionObjectMan, member_add_timer, WORLD_SESSION_OBJECT_MAN_MEMBER_ADD_TIMER_OFFSET);
BB_OFFSET(WorldSessionObjectMan, debug_network_event_latch_c1,
          WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C1_OFFSET);
BB_OFFSET(WorldSessionObjectMan, debug_network_event_latch_c2,
          WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C2_OFFSET);
BB_OFFSET(WorldSessionObjectMan, debug_network_event_latch_c3,
          WORLD_SESSION_OBJECT_MAN_DEBUG_NETWORK_EVENT_LATCH_C3_OFFSET);
BB_OFFSET(WorldSessionObjectMan, reload_requested, WORLD_SESSION_OBJECT_MAN_RELOAD_REQUESTED_FLAG_OFFSET);
BB_OFFSET(WorldSessionObjectMan, receive_packet_list, WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_LIST_OFFSET);
BB_OFFSET(WorldSessionObjectMan, join_flag_e8, WORLD_SESSION_OBJECT_MAN_JOIN_FLAG_E8_OFFSET);
BB_OFFSET(WorldSessionObjectMan, join_flag_e9, WORLD_SESSION_OBJECT_MAN_JOIN_FLAG_E9_OFFSET);
BB_OFFSET(WorldSessionObjectMan, wait_map_reload_timer, WORLD_SESSION_OBJECT_MAN_WAIT_MAP_RELOAD_TIMER_OFFSET);
BB_OFFSET(WorldSessionObjectMan, session_chr_init_param, WORLD_SESSION_OBJECT_MAN_SESSION_CHR_INIT_PARAM_OFFSET);
BB_OFFSET(WorldSessionObjectMan, disconnect_timer, WORLD_SESSION_OBJECT_MAN_DISCONNECT_TIMER_OFFSET);
BB_OFFSET(WorldSessionObjectMan, create_retry_count, WORLD_SESSION_OBJECT_MAN_CREATE_RETRY_COUNT_OFFSET);
BB_OFFSET(WorldSessionObjectMan, join_retry_count, WORLD_SESSION_OBJECT_MAN_JOIN_RETRY_COUNT_OFFSET);
BB_OFFSET(WorldSessionObjectMan, debug_create_guard, WORLD_SESSION_OBJECT_MAN_DEBUG_CREATE_GUARD_OFFSET);
// The DEBUG_JOIN_GUARD offset (0x121) names the byte the struct calls
// debug_join_guard_121; its debug_join_guard member sits at 0x118.
BB_OFFSET(WorldSessionObjectMan, debug_join_guard_121, WORLD_SESSION_OBJECT_MAN_DEBUG_JOIN_GUARD_OFFSET);
BB_OFFSET(WorldSessionObjectMan, packet_storage_debug_menu, WORLD_SESSION_OBJECT_MAN_DEBUG_MENU_NODE_OFFSET);
BB_OFFSET(WorldSessionObjectMan, packet_send_lock, WORLD_SESSION_OBJECT_MAN_PACKET_SEND_LOCK_OFFSET);
static_assert(offsetof(WorldSessionObjectMan, packet_send_lock) + offsetof(WorldSessionPacketSendLock, ready) ==
                  WORLD_SESSION_OBJECT_MAN_PACKET_SEND_LOCK_READY_OFFSET,
              "packet send lock ready byte");
BB_SIZE(WorldSessionPacketSendLock, 0x18);
BB_SIZE(WorldSessionObjectPacketNode, WORLD_SESSION_OBJECT_MAN_OBJECT_PACKET_NODE_SIZE);
BB_SIZE(WorldSessionActivePeerObjectNode, WORLD_SESSION_OBJECT_MAN_ACTIVE_PEER_OBJECT_NODE_SIZE);
BB_SIZE(WorldSessionReceivePacketNode, WORLD_SESSION_OBJECT_MAN_RECEIVE_PACKET_NODE_SIZE);
BB_SIZE(WorldSessionObjectPacketRecord, WORLD_SESSION_OBJECT_PACKET_RECORD_SIZE);
BB_OFFSET(WorldSessionObjectPacketRecord, peer_object_ref, WORLD_SESSION_OBJECT_PACKET_RECORD_OBJECT_REF_OFFSET);
BB_OFFSET(WorldSessionObjectPacketRecord, queue_table, WORLD_SESSION_OBJECT_PACKET_RECORD_QUEUE_TABLE_OFFSET);
BB_OFFSET(WorldSessionObjectPacketRecord, debug_menu_handle,
          WORLD_SESSION_OBJECT_PACKET_RECORD_DEBUG_MENU_HANDLE_OFFSET);
BB_SIZE(WorldSessionPacketTypeQueue, WORLD_SESSION_PACKET_TYPE_QUEUE_SIZE);
BB_SIZE(WorldSessionPacketPayload, WORLD_SESSION_PACKET_PAYLOAD_SIZE);
BB_OFFSET(WorldSessionPacketPayload, byte_size, WORLD_SESSION_PACKET_PAYLOAD_BYTE_SIZE_OFFSET);
BB_OFFSET(WorldSessionPacketPayload, payload, WORLD_SESSION_PACKET_PAYLOAD_POINTER_OFFSET);
}  // namespace detail::world_session_object_man_layout

}  // namespace bb
