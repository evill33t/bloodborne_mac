// NetWorldChrSync: per-world character synchronization (WorldChrMan + 0xa40)
// and the WorldSessionObjectMan message records it exchanges.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct ChrIns;
template <typename T>
struct ChrSet;
struct WorldInfoOwner;

inline constexpr std::size_t NET_WORLD_CHR_SYNC_SIZE = 0x26d8;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_BLOCK_COUNT = 0x3e;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_BLOCK_SIZE = 0x48;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_SECONDARY_BLOCK_SIZE = 0x10;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_HOST_STATE_SIZE = 0x18;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_RUNTIME_STATE_SIZE = 0x60;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_SCALAR_STATE_SIZE = 0x04;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_NETWORK_UPDATE_SIZE = 0x14;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_HOST_VECTOR_PACKET_SIZE = 0x0c;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_SCALAR_PACKET_SIZE = 0x08;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_NETWORK_PACKET_SIZE = 0x18;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_RUNTIME_PACKET_SIZE = 0x64;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_SCALAR_BATCH_CAPACITY = 10;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_HOST_VECTOR_BATCH_CAPACITY = 8;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_TRANSFORM_BATCH_CAPACITY = 64;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_NETWORK_BATCH_CAPACITY = 64;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_SCALAR_MESSAGE_TYPE = 0x05;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_HOST_VECTOR_MESSAGE_TYPE = 0x04;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_AUTHORITY_CANDIDATE_MESSAGE_TYPE = 0x16;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_AUTHORITY_ASSIGNMENT_MESSAGE_TYPE = 0x17;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_PEER_HANDLE_MESSAGE_TYPE = NET_WORLD_CHR_SYNC_AUTHORITY_CANDIDATE_MESSAGE_TYPE;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_TRANSFORM_MESSAGE_TYPE = 0x34;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_TARGETED_RUNTIME_MESSAGE_TYPE = 0x29;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_CONTROL_MESSAGE_TYPE = 0x36;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_RUNTIME_MESSAGE_TYPE = 0x3d;
inline constexpr std::uint32_t NET_WORLD_CHR_SYNC_NETWORK_MESSAGE_TYPE = 0x41;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_SCALAR_MAX_PAYLOAD = 0x50;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_HOST_VECTOR_MAX_PAYLOAD = 0x60;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_PEER_HANDLE_MAX_PAYLOAD = 0x1000;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_TRANSFORM_PAYLOAD_SIZE = 0x64;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_NETWORK_MAX_PAYLOAD = 0x600;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_RUNTIME_MAX_PAYLOAD = 0x1900;
inline constexpr float NET_WORLD_CHR_SYNC_AUTHORITY_ARBITRATION_INTERVAL_SECONDS = 3.0f;
inline constexpr float NET_WORLD_CHR_SYNC_AUTHORITY_CANDIDATE_INTERVAL_SECONDS = 5.0f;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_AUTHORITY_PEER_LIST_SIZE = 0x18;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_AUTHORITY_PEER_RECORD_SIZE = 0x28;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_AUTHORITY_PEER_NODE_SIZE = 0x18;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_AUTHORITY_CANDIDATE_NODE_SIZE = 0x18;
// Constructor defaults confirmed in NetWorldChrSync_Constructor.
inline constexpr std::size_t NET_WORLD_CHR_SYNC_DEFAULT_SCALAR_BATCH_LIMIT = 8;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_DEFAULT_PEER_BATCH_LIMIT = 5;
inline constexpr std::size_t NET_WORLD_CHR_SYNC_DEFAULT_TRANSFORM_BATCH_LIMIT = 10;
inline constexpr float NET_WORLD_CHR_SYNC_DEFAULT_SCALAR_SEND_INTERVAL_SECONDS = 0.25f;
inline constexpr float NET_WORLD_CHR_SYNC_DEFAULT_NETWORK_SEND_INTERVAL_SECONDS = 0.5f;

// Compact handle used by the scalar, transform and network-update lanes:
// 5-bit block at bits 11..15, 11-bit entry. Distinct from ChrHandle, whose
// peer-list form uses a 14-bit entry and 6-bit selector.
struct NetWorldChrSyncHandle {
    std::uint32_t value;

    static const NetWorldChrSyncHandle NONE;

    static constexpr NetWorldChrSyncHandle from_raw(std::uint32_t raw) { return {raw}; }
    static constexpr NetWorldChrSyncHandle make(std::uint32_t block, std::uint32_t entry) {
        return {((block & 0x1f) << 11) | (entry & 0x7ff)};
    }
    constexpr std::uint32_t into_inner() const { return value; }
    constexpr bool is_empty() const { return value == 0xffffffffu; }
    constexpr std::size_t block() const { return (value >> 11) & 0x1f; }
    constexpr std::size_t entry() const { return value & 0x7ff; }
    constexpr bool operator==(NetWorldChrSyncHandle o) const { return value == o.value; }
};
inline constexpr NetWorldChrSyncHandle NetWorldChrSyncHandle::NONE{0xffffffffu};

// Ranked authority candidate sent in message 0x16: compact world-character
// handle in the low 16 bits, clamped authority score in the high 16.
struct NetWorldChrSyncAuthorityCandidate {
    std::uint32_t value;

    static constexpr NetWorldChrSyncAuthorityCandidate from_raw(std::uint32_t raw) { return {raw}; }
    static constexpr NetWorldChrSyncAuthorityCandidate make(NetWorldChrSyncHandle handle, std::uint16_t score) {
        return {(static_cast<std::uint32_t>(score) << 16) | (handle.into_inner() & 0xffff)};
    }
    constexpr std::uint32_t into_inner() const { return value; }
    constexpr NetWorldChrSyncHandle handle() const { return NetWorldChrSyncHandle::from_raw(value & 0xffff); }
    constexpr std::uint16_t score() const { return static_cast<std::uint16_t>(value >> 16); }
};

// Host-side state, one 0x18-byte record per synchronized character.
struct NetWorldChrSyncHostState {
    // Copied from character state +0x1e0.
    float vector_00[3];
    // Copied from character state +0x1d0.
    float vector_0c[3];
};

// Partially typed eight-byte lane within a runtime-state record.
struct NetWorldChrSyncRuntimeReference {
    std::int32_t reference;
    std::uint32_t value;
};

// Per-character working state reset by NetWorldChrSyncBlock::Reset.
struct NetWorldChrSyncRuntimeState {
    std::uint64_t value_00;
    std::uint32_t value_08;
    // Seven reference/value pairs initialized with reference -1.
    NetWorldChrSyncRuntimeReference references[7];
    // Eighth reference, followed immediately by the three tail values.
    std::int32_t reference_44;
    std::uint64_t value_48;
    std::uint64_t value_50;
    std::uint64_t value_58;
};

// Two scalar values tracked and sent independently from the 0x14 network update.
struct NetWorldChrSyncScalarState {
    std::uint16_t value;
    std::uint16_t state;
};

// Dirty/update bits stored per synchronized character.
struct NetWorldChrSyncDirtyFlags {
    std::uint16_t value;

    static constexpr std::uint16_t RECEIVED_SCALAR_UPDATE = 0x10;
    static constexpr std::uint16_t LOCAL_SCALAR_UPDATE = 0x20;
    static constexpr std::uint16_t RECEIVED_NETWORK_UPDATE = 0x40;

    constexpr bool contains(std::uint16_t flag) const { return (value & flag) != 0; }
};

// The 0x14-byte per-character payload carried by message type 0x41.
struct NetWorldChrSyncNetworkUpdate {
    std::uint8_t bytes_00[0x10];
    std::int16_t value_10;
    std::uint16_t state_12;
};

// One map/block lane within NetWorldChrSync.
struct NetWorldChrSyncBlock {
    Unknown<0x08> _unk00;
    ChrSet<ChrIns>* chr_set;
    std::int32_t entry_count;
    std::uint32_t _pad14;
    std::int16_t* active_entries;
    NetWorldChrSyncHostState* host_states;
    NetWorldChrSyncRuntimeState* runtime_states;
    NetWorldChrSyncScalarState* scalar_states;
    NetWorldChrSyncDirtyFlags* dirty_flags;
    NetWorldChrSyncNetworkUpdate* network_updates;

    // Per-entry arrays (dirty_flags, network_updates, ...) hold entry_len().
    std::size_t entry_len() const { return entry_count > 0 ? static_cast<std::size_t>(entry_count) : 0; }
    // The signed active-entry value selected by bits 0..10 of raw; false if
    // out of range or negative.
    bool active_entry_by_raw(std::uint32_t raw, std::int16_t* out) const {
        const std::size_t index = raw & 0x7ff;
        if (!active_entries || index >= entry_len()) return false;
        const std::int16_t v = active_entries[index];
        if (v < 0) return false;
        *out = v;
        return true;
    }
    bool lookup_raw(std::uint32_t raw, std::int16_t* out) const { return active_entry_by_raw(raw, out); }
};

// Secondary 0x10-byte lookup entries beginning at NetWorldChrSync+0x208.
struct NetWorldChrSyncSecondaryBlock {
    NetWorldChrSyncBlock* block;
    std::uintptr_t _unk08;
};

struct NetWorldChrSyncAuthorityPeerNode;
struct NetWorldChrSyncAuthorityCandidateNode;

// Intrusive-list header at NetWorldChrSync+0x5f0.
struct NetWorldChrSyncAuthorityPeerList {
    NetWorldChrSyncAuthorityPeerNode* sentinel;
    std::size_t len;
    std::uintptr_t allocator;
};

// Host-side candidate state for one session member.
struct NetWorldChrSyncAuthorityPeerRecord {
    std::uint32_t peer_chr_handle;
    std::uint32_t _pad04;
    std::uint64_t _unk08;
    NetWorldChrSyncAuthorityCandidateNode* candidates_sentinel;
    std::size_t candidates_len;
    std::uintptr_t candidates_allocator;
};

// List node pointing at one peer's authority-candidate record.
struct NetWorldChrSyncAuthorityPeerNode {
    NetWorldChrSyncAuthorityPeerNode* next;
    NetWorldChrSyncAuthorityPeerNode* previous;
    NetWorldChrSyncAuthorityPeerRecord* record;
};

// Intrusive node holding one packed (score, world-character handle).
struct NetWorldChrSyncAuthorityCandidateNode {
    NetWorldChrSyncAuthorityCandidateNode* next;
    NetWorldChrSyncAuthorityCandidateNode* previous;
    NetWorldChrSyncAuthorityCandidate candidate;
    std::uint32_t _pad14;
};

// Four-pointer header of the class's game-allocator backed ordered
// containers; a list or tree root depending on the call site.
struct NetWorldChrSyncContainer {
    std::uintptr_t _owner;
    std::uintptr_t head;
    std::size_t len;
    std::uintptr_t _allocator;
};

// Eight-byte scalar-state record carried by the smaller update path.
struct NetWorldChrSyncScalarPacket {
    NetWorldChrSyncHandle packed_handle;
    NetWorldChrSyncScalarState state;
};

// One 12-byte record from message 0x04. The encoded tail is consumed by the
// native transform decoder before updating the block's two host vectors.
struct NetWorldChrSyncHostVectorPacket {
    NetWorldChrSyncHandle packed_handle;
    std::uint8_t encoded_state[0x08];
};

// One full per-character runtime-state record from message 0x3d.
struct NetWorldChrSyncRuntimePacket {
    NetWorldChrSyncHandle packed_handle;
    // Byte-exact copy of one block runtime_states entry; a byte array because
    // the wire record has no alignment padding between handle and state.
    std::uint8_t state_bytes[NET_WORLD_CHR_SYNC_RUNTIME_STATE_SIZE];
};

// Targeted message 0x29: one encoded host-vector delta, the complete runtime
// state and the compact handle at the tail.
struct NetWorldChrSyncTargetedRuntimePacket {
    std::uint8_t encoded_host_vector[0x08];
    std::uint8_t state_bytes[NET_WORLD_CHR_SYNC_RUNTIME_STATE_SIZE];
    NetWorldChrSyncHandle packed_handle;
};

// Eight-byte message 0x36. The low two bits select one of three native
// control/timing subtypes; the remaining bits are subtype-specific.
struct NetWorldChrSyncControlPacket {
    std::uint32_t packed_control;
    std::uint32_t value;

    constexpr std::uint32_t subtype() const { return packed_control & 0x03; }
};

// 0x18-byte record carried by WorldSessionObjectMan message type 0x41.
struct NetWorldChrSyncNetworkPacket {
    NetWorldChrSyncHandle packed_handle;
    NetWorldChrSyncNetworkUpdate update;
};

// Message type 0x34: one packed world-character handle and three 32-byte
// transform/state blocks, copied to the target ChrIns at +0x218, +0x238 and
// +0x258.
struct NetWorldChrSyncTransformPacket {
    NetWorldChrSyncHandle packed_handle;
    std::uint8_t state_blocks[3][0x20];
};

// Per-world character synchronization owned by WorldChrMan + 0xa40 (the game
// names it NetWorldChrSync). Its update resolves packed non-player
// world-character handles through blocks and exchanges enemy/NPC state through
// WorldSessionObjectMan messages. The constructor at RVA 0x18ec020 gets the
// active block count from its world-info owner at +0x18; the arrays here are
// native capacities, not necessarily the active count for the current map.
struct NetWorldChrSync {
    WorldInfoOwner* world_info_owner;
    std::int32_t block_count;
    std::uint32_t _pad0c;
    NetWorldChrSyncBlock* blocks[NET_WORLD_CHR_SYNC_BLOCK_COUNT];
    std::int32_t secondary_block_count;
    std::uint32_t _pad204;
    NetWorldChrSyncSecondaryBlock secondary_blocks[NET_WORLD_CHR_SYNC_BLOCK_COUNT];
    Unknown<0x08> _unk5e8;
    // Host-side records holding each peer's ranked authority candidates.
    NetWorldChrSyncAuthorityPeerList authority_peers;
    std::uintptr_t _queued_handles_owner;
    // Packed character handles awaiting peer synchronization.
    std::uint32_t* queued_handles_begin;
    std::uint32_t* queued_handles_end;
    std::uint32_t* queued_handles_capacity;
    std::uintptr_t _queued_handles_allocator;
    float authority_arbitration_interval;
    float authority_arbitration_timer;
    float authority_candidate_interval;
    float authority_candidate_timer;
    // Block selector used by the first incremental synchronization pass.
    std::int32_t primary_batch_block;
    std::uint32_t primary_batch_cursor;
    bool primary_batch_active;
    Unknown<3> _pad649;
    float scalar_send_interval;
    float scalar_send_timer;
    std::uint32_t scalar_batch_limit;
    bool scalar_batch_active;
    Unknown<3> _pad659;
    // Block selector used by the second incremental synchronization pass.
    std::int32_t secondary_batch_block;
    std::uint32_t secondary_batch_cursor;
    bool secondary_batch_active;
    Unknown<3> _pad665;
    float network_send_interval;
    float network_send_timer;
    std::uint32_t peer_batch_limit;
    // Current block key for the transform/state pass.
    std::int32_t transform_batch_block;
    std::uint32_t transform_batch_cursor;
    std::uint32_t transform_batch_limit;
    Unknown<0x08> _unk680;
    // Ordered host-side lookup containers used while building batches.
    NetWorldChrSyncContainer primary_lookup;
    NetWorldChrSyncContainer secondary_lookup;
    // Scratch payload for message type 0x04 (eight 12-byte records).
    NetWorldChrSyncHostVectorPacket host_vector_packet_scratch[NET_WORLD_CHR_SYNC_HOST_VECTOR_BATCH_CAPACITY];
    // Scratch payload for message type 0x05 (ten 8-byte records).
    NetWorldChrSyncScalarPacket scalar_packet_scratch[NET_WORLD_CHR_SYNC_SCALAR_BATCH_CAPACITY];
    // Per-character 100-byte runtime-state records used by message 0x3d.
    NetWorldChrSyncRuntimePacket runtime_packet_scratch[NET_WORLD_CHR_SYNC_TRANSFORM_BATCH_CAPACITY];
    NetWorldChrSyncContainer transform_queue;
    // Scratch payload for message type 0x41 (64 24-byte records).
    NetWorldChrSyncNetworkPacket network_packet_scratch[NET_WORLD_CHR_SYNC_NETWORK_BATCH_CAPACITY];
    NetWorldChrSyncContainer network_queue;
    bool dispatch_enabled;
    Unknown<7> _pad26b9;
    Unknown<0x18> _iterator_state;

    // The block selected by bits 11..15 of raw, or null.
    NetWorldChrSyncBlock* block_by_raw(std::uint32_t raw) const {
        if (static_cast<std::int32_t>(raw) < 0 || block_count <= 0) return nullptr;
        const std::size_t selector = (raw >> 11) & 0x1f;
        if (selector >= static_cast<std::size_t>(block_count) || selector >= NET_WORLD_CHR_SYNC_BLOCK_COUNT)
            return nullptr;
        return blocks[selector];
    }
    // The signed active-entry value selected by the packed handle.
    bool active_entry_by_raw(std::uint32_t raw, std::int16_t* out) const {
        const NetWorldChrSyncBlock* b = block_by_raw(raw);
        return b && b->active_entry_by_raw(raw, out);
    }
    // Packed handles queued for the peer-list synchronization path.
    std::size_t queued_handle_count() const {
        if (!queued_handles_begin || !queued_handles_end || queued_handles_end < queued_handles_begin) return 0;
        return static_cast<std::size_t>(queued_handles_end - queued_handles_begin);
    }
    // Compatibility names from the first partial model of this object.
    NetWorldChrSyncBlock* bucket_by_raw(std::uint32_t raw) const { return block_by_raw(raw); }
    bool lookup_raw(std::uint32_t raw, std::int16_t* out) const { return active_entry_by_raw(raw, out); }
};

// Compatibility aliases for the initial, partially identified model.
inline constexpr std::size_t WORLD_CHR_MAN_BLOCK_LOOKUP_COUNT = NET_WORLD_CHR_SYNC_BLOCK_COUNT;
inline constexpr std::size_t WORLD_CHR_MAN_BLOCK_LOOKUP_SIZE = NET_WORLD_CHR_SYNC_SIZE;
inline constexpr std::size_t WORLD_CHR_MAN_BLOCK_LOOKUP_BUCKET_SIZE = NET_WORLD_CHR_SYNC_BLOCK_SIZE;
using WorldChrManBlockLookup = NetWorldChrSync;
using WorldChrManBlockLookupBucket = NetWorldChrSyncBlock;

namespace detail::net_world_chr_sync_layout {
using S = NetWorldChrSync;
using B = NetWorldChrSyncBlock;
BB_SIZE(S, NET_WORLD_CHR_SYNC_SIZE);
BB_SIZE(B, NET_WORLD_CHR_SYNC_BLOCK_SIZE);
BB_SIZE(NetWorldChrSyncSecondaryBlock, NET_WORLD_CHR_SYNC_SECONDARY_BLOCK_SIZE);
BB_OFFSET(S, block_count, 0x08);
BB_OFFSET(S, blocks, 0x10);
BB_OFFSET(S, secondary_block_count, 0x200);
BB_OFFSET(S, secondary_blocks, 0x208);
BB_OFFSET(S, authority_peers, 0x5f0);
BB_OFFSET(S, queued_handles_begin, 0x610);
BB_OFFSET(S, queued_handles_end, 0x618);
BB_OFFSET(S, queued_handles_capacity, 0x620);
BB_OFFSET(S, authority_arbitration_interval, 0x630);
BB_OFFSET(S, authority_candidate_timer, 0x63c);
BB_OFFSET(S, primary_batch_block, 0x640);
BB_OFFSET(S, primary_lookup, 0x688);
BB_OFFSET(S, secondary_lookup, 0x6a8);
BB_OFFSET(S, host_vector_packet_scratch, 0x6c8);
BB_OFFSET(S, scalar_packet_scratch, 0x728);
BB_OFFSET(S, runtime_packet_scratch, 0x778);
BB_OFFSET(S, transform_queue, 0x2078);
BB_OFFSET(S, network_packet_scratch, 0x2098);
BB_OFFSET(S, network_queue, 0x2698);
BB_OFFSET(S, dispatch_enabled, 0x26b8);
BB_OFFSET(B, chr_set, 0x08);
BB_OFFSET(B, entry_count, 0x10);
BB_OFFSET(B, active_entries, 0x18);
BB_OFFSET(B, host_states, 0x20);
BB_OFFSET(B, runtime_states, 0x28);
BB_OFFSET(B, scalar_states, 0x30);
BB_OFFSET(B, dirty_flags, 0x38);
BB_OFFSET(B, network_updates, 0x40);
BB_SIZE(NetWorldChrSyncHostState, NET_WORLD_CHR_SYNC_HOST_STATE_SIZE);
BB_SIZE(NetWorldChrSyncRuntimeState, NET_WORLD_CHR_SYNC_RUNTIME_STATE_SIZE);
BB_SIZE(NetWorldChrSyncRuntimeReference, 0x08);
BB_SIZE(NetWorldChrSyncScalarState, NET_WORLD_CHR_SYNC_SCALAR_STATE_SIZE);
BB_SIZE(NetWorldChrSyncNetworkUpdate, NET_WORLD_CHR_SYNC_NETWORK_UPDATE_SIZE);
BB_SIZE(NetWorldChrSyncScalarPacket, NET_WORLD_CHR_SYNC_SCALAR_PACKET_SIZE);
BB_SIZE(NetWorldChrSyncHostVectorPacket, NET_WORLD_CHR_SYNC_HOST_VECTOR_PACKET_SIZE);
BB_SIZE(NetWorldChrSyncRuntimePacket, NET_WORLD_CHR_SYNC_RUNTIME_PACKET_SIZE);
BB_SIZE(NetWorldChrSyncTargetedRuntimePacket, 0x6c);
BB_SIZE(NetWorldChrSyncControlPacket, 0x08);
BB_SIZE(NetWorldChrSyncNetworkPacket, NET_WORLD_CHR_SYNC_NETWORK_PACKET_SIZE);
BB_SIZE(NetWorldChrSyncTransformPacket, 0x64);
BB_SIZE(NetWorldChrSyncTransformPacket, NET_WORLD_CHR_SYNC_TRANSFORM_PAYLOAD_SIZE);
BB_SIZE(NetWorldChrSyncContainer, 0x20);
BB_SIZE(NetWorldChrSyncAuthorityPeerList, NET_WORLD_CHR_SYNC_AUTHORITY_PEER_LIST_SIZE);
BB_SIZE(NetWorldChrSyncAuthorityPeerRecord, NET_WORLD_CHR_SYNC_AUTHORITY_PEER_RECORD_SIZE);
BB_SIZE(NetWorldChrSyncAuthorityPeerNode, NET_WORLD_CHR_SYNC_AUTHORITY_PEER_NODE_SIZE);
BB_SIZE(NetWorldChrSyncAuthorityCandidateNode, NET_WORLD_CHR_SYNC_AUTHORITY_CANDIDATE_NODE_SIZE);
BB_OFFSET(NetWorldChrSyncAuthorityPeerRecord, candidates_sentinel, 0x10);
BB_OFFSET(NetWorldChrSyncAuthorityPeerRecord, candidates_len, 0x18);
BB_OFFSET(NetWorldChrSyncRuntimeState, references, 0x0c);
BB_OFFSET(NetWorldChrSyncRuntimeState, reference_44, 0x44);
BB_OFFSET(NetWorldChrSyncRuntimeState, value_48, 0x48);
static_assert(NET_WORLD_CHR_SYNC_HOST_VECTOR_MAX_PAYLOAD ==
              NET_WORLD_CHR_SYNC_HOST_VECTOR_BATCH_CAPACITY * sizeof(NetWorldChrSyncHostVectorPacket), "host vector payload");
static_assert(NET_WORLD_CHR_SYNC_SCALAR_MAX_PAYLOAD ==
              NET_WORLD_CHR_SYNC_SCALAR_BATCH_CAPACITY * sizeof(NetWorldChrSyncScalarPacket), "scalar payload");
static_assert(NET_WORLD_CHR_SYNC_NETWORK_MAX_PAYLOAD ==
              NET_WORLD_CHR_SYNC_NETWORK_BATCH_CAPACITY * sizeof(NetWorldChrSyncNetworkPacket), "network payload");
static_assert(NET_WORLD_CHR_SYNC_RUNTIME_MAX_PAYLOAD ==
              NET_WORLD_CHR_SYNC_TRANSFORM_BATCH_CAPACITY * sizeof(NetWorldChrSyncRuntimePacket), "runtime payload");
static_assert(NetWorldChrSyncHandle::make(0x12, 0x456).into_inner() == 0x9456, "handle packing");
static_assert(NetWorldChrSyncAuthorityCandidate::make(NetWorldChrSyncHandle::make(0x12, 0x456), 0x4321).into_inner() ==
              0x43219456u, "candidate packing");
}  // namespace detail::net_world_chr_sync_layout

}  // namespace bb
