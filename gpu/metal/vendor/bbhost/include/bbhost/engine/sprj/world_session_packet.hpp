// WorldSessionObjectMan inner gameplay packet identifiers, sizes and payload layouts (Bloodborne 1.09).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// These are the inner gameplay messages carried by the per-peer world-session
// queue, distinct from NexusRevolution vport/session event types.

inline constexpr Rva WORLD_SESSION_SEND_PACKET_FN{0x17894e0};
inline constexpr Rva WORLD_SESSION_POP_PEER_MESSAGE_FN{0x1789200};
inline constexpr Rva WORLD_SESSION_POP_CURRENT_ROUTE_MESSAGE_FN{0x17893e0};
inline constexpr Rva WORLD_SESSION_POP_QUEUED_MESSAGE_FN{0x17910f0};
inline constexpr Rva WORLD_SESSION_SEND_COMPRESSED_EVENT_FLAGS_FN{0x1786050};
inline constexpr Rva WORLD_SESSION_SEND_TRANSITION_BOOTSTRAP_FN{0x1786360};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG_FN{0x132aad0};

// Eight-byte header assembled by WorldSessionObjectMan_SendPacket before the
// payload is handed to the active route.
struct WorldSessionPacketHeader {
    std::uint32_t packet_type;
    std::uint32_t payload_size;
};

// Open packet identifier: values outside the list stay legal.
enum class WorldSessionPacketType : std::uint32_t {
    NET_CHR_HOST_VECTOR_BATCH = 0x04,
    NET_CHR_SCALAR_BATCH = 0x05,
    PLAYER_FRAME_SNAPSHOT = 0x06,
    SESSION_CREATE_STATE = 0x07,
    REMOTE_PLAYER_SNAPSHOT = 0x08,
    SESSION_SYNC_READY = 0x0a,
    AREA_SESSION_ID = 0x0b,
    PEER_LOADOUT_STATE = 0x0c,
    PEER_SESSION_FLAGS = 0x0d,
    EVENT_ACTION_STATE = 0x0e,
    EVENT_FLAG_ID = 0x0f,
    COMPRESSED_EVENT_FLAGS = 0x10,
    TRANSITION_BOOTSTRAP_BUNDLE = 0x11,
    PAIRED_CHR_ANIMATION_START = 0x12,
    WORLD_CHR_OBJECT_REFS = 0x13,
    CHARACTER_DAMAGE_EVENT = 0x14,
    WORLD_CHR_UPDATE_15 = 0x15,
    NET_CHR_AUTHORITY_CANDIDATES = 0x16,
    NET_CHR_AUTHORITY_ASSIGNMENTS = 0x17,
    HOST_SESSION_METADATA = 0x19,
    PEER_SLOT_BLOCK_ID = 0x22,
    WORLD_CHR_SP_EFFECT_APPLY = 0x24,
    PLAYER_SP_EFFECT_REQUEST = 0x25,
    TYPE_26 = 0x26,
    THANKS_KICKOUT = 0x27,
    NET_CHR_TARGETED_RUNTIME_STATE = 0x29,
    OBJ_INS_ACTIVE_ENTRY_BATCH = 0x2a,
    OBJ_INS_MOTION_STATE_BATCH = 0x2b,
    MAP_ITEM_STATE_SYNC = 0x2c,
    OBJ_ACT_ACTIVATION_REQUEST = 0x2d,
    OBJECT_ACTION_STATE = 0x2e,
    OBJECT_ACTION_AUX = 0x2f,
    PAIRED_CHR_ANIMATION_PROGRESS = 0x30,
    OBJECT_NETWORK_CALLBACK = 0x31,
    SOS_SIGN_SCHEDULED_WORK = 0x32,
    PHANTOM_LEAVE = 0x33,
    NET_CHR_TRANSFORM = 0x34,
    NET_CHR_CONTROL_TIMING = 0x36,
    PAIRED_CHR_ANIMATION_END = 0x37,
    TYPE_38 = 0x38,
    INSIGHT_DELTA = 0x39,
    REMOTE_PLAYER_SP_EFFECT_MANIFEST = 0x3b,
    REMOTE_PLAYER_SP_EFFECT_BATCH = 0x3c,
    NET_CHR_RUNTIME_BATCH = 0x3d,
    BULLET_EVENT_BATCH = 0x3e,
    BULLET_FULL_SNAPSHOT = 0x3f,
    BULLET_COMPACT_STATE_BATCH = 0x40,
    NET_CHR_NETWORK_UPDATE_BATCH = 0x41,
    MEMBER_STATUS = 0x42,
    PLAYER_AUX_STATE = 0x43,
};

// Fixed size, or variable with an observed maximum (0 when none observed).
struct WorldSessionPayloadSize {
    bool variable;
    std::size_t size;  // fixed size, or the observed maximum for a variable payload
};

enum class PacketSemanticConfidence : std::uint8_t {
    High,
    Medium,
    Low,
};

struct WorldSessionPacketDescriptor {
    WorldSessionPacketType packet_type;
    WorldSessionPayloadSize payload_size;
    const char* name;
    PacketSemanticConfidence confidence;
};

// All packet IDs with a static Bloodborne 1.09 send or receive reference,
// sorted by ID.
inline constexpr WorldSessionPacketDescriptor OBSERVED_WORLD_SESSION_PACKETS[] = {
    {WorldSessionPacketType{0x04}, {true, 0x60}, "net_chr_host_vector_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x05}, {true, 0x50}, "net_chr_scalar_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x06}, {true, 0x63f8}, "player_frame_snapshot", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x07}, {false, 0x04}, "session_create_state", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x08}, {false, 0x168}, "remote_player_snapshot", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x0a}, {false, 0x01}, "session_sync_ready", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x0b}, {false, 0x04}, "area_session_id", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x0c}, {false, 0x80}, "peer_loadout_state", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x0d}, {false, 0x04}, "peer_session_flags", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x0e}, {false, 0x14}, "event_action_state", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x0f}, {false, 0x04}, "event_flag_id", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x10}, {true, 0}, "compressed_event_flags", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x11}, {true, 0}, "transition_bootstrap_bundle", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x12}, {false, 0x2c}, "paired_chr_animation_start", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x13}, {false, 0x08}, "world_chr_object_refs", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x14}, {false, 0xec}, "character_damage_event", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x15}, {false, 0x0c}, "world_chr_update_15", PacketSemanticConfidence::Low},
    {WorldSessionPacketType{0x16}, {true, 0x1000}, "net_chr_authority_candidates", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x17}, {true, 0x1000}, "net_chr_authority_assignments", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x19}, {false, 0x44}, "host_session_metadata", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x22}, {false, 0x04}, "peer_slot_block_id", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x24}, {false, 0x08}, "world_chr_sp_effect_apply", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x25}, {false, 0x04}, "player_sp_effect_request", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x26}, {false, 0x04}, "type_26", PacketSemanticConfidence::Low},
    {WorldSessionPacketType{0x27}, {false, 0x01}, "thanks_kickout", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x29}, {false, 0x6c}, "net_chr_targeted_runtime_state", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2a}, {true, 0x64}, "obj_ins_active_entry_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2b}, {true, 0x20}, "obj_ins_motion_state_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2c}, {true, 0x100}, "map_item_state_sync", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2d}, {false, 0x08}, "obj_act_activation_request", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2e}, {false, 0x06}, "object_action_state", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x2f}, {false, 0x04}, "object_action_aux", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x30}, {false, 0x04}, "paired_chr_animation_progress", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x31}, {true, 0x100}, "object_network_callback", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x32}, {false, 0x10}, "sos_sign_scheduled_work", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x33}, {false, 0x08}, "phantom_leave", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x34}, {false, 0x64}, "net_chr_transform", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x36}, {false, 0x08}, "net_chr_control_timing", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x37}, {false, 0x04}, "paired_chr_animation_end", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x38}, {false, 0x04}, "type_38", PacketSemanticConfidence::Low},
    {WorldSessionPacketType{0x39}, {false, 0x02}, "insight_delta", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x3b}, {false, 0x08}, "remote_player_sp_effect_manifest", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x3c}, {true, 0}, "remote_player_sp_effect_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x3d}, {true, 0x1900}, "net_chr_runtime_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x3e}, {true, 10'000}, "bullet_event_batch", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x3f}, {true, 0}, "bullet_full_snapshot", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x40}, {true, 10'000}, "bullet_compact_state_batch", PacketSemanticConfidence::Medium},
    {WorldSessionPacketType{0x41}, {true, 0x600}, "net_chr_network_update_batch", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x42}, {false, 0x01}, "member_status", PacketSemanticConfidence::High},
    {WorldSessionPacketType{0x43}, {false, 0x90}, "player_aux_state", PacketSemanticConfidence::Medium},
};

// The descriptor of a packet type, or null when it is not in the table.
constexpr const WorldSessionPacketDescriptor* packet_descriptor(WorldSessionPacketType packet_type) {
    for (const WorldSessionPacketDescriptor& d : OBSERVED_WORLD_SESSION_PACKETS) {
        if (d.packet_type == packet_type) return &d;
    }
    return nullptr;
}

struct WorldSessionEventFlagIdPacket {
    std::uint32_t event_flag_id;
};

// Starts a synchronized two-character animation such as a grab or other paired
// action. Character handles are the compact 16-bit WorldChr handles used by
// this packet family.
struct PairedChrAnimationStartPacket {
    float first_position[3];
    float first_yaw;
    float second_position[3];
    float second_yaw;
    std::uint16_t first_chr_handle;
    std::uint16_t second_chr_handle;
    std::int32_t animation_id;
    std::uint8_t flags;
    std::uint8_t _pad29[3];
};

// Updates one character's control byte and quantized progress during a
// paired-character animation.
struct PairedChrAnimationProgressPacket {
    std::uint16_t chr_handle;
    std::uint8_t control;
    std::uint8_t quantized_progress;
};

// Ends/unlinks the paired animation state for two characters.
struct PairedChrAnimationEndPacket {
    std::uint16_t first_chr_handle;
    std::uint16_t second_chr_handle;
};

// Shared count-prefixed header used by the ObjIns packet pair 0x2a/0x2b.
struct ObjInsNetworkBatchHeader {
    std::uint8_t flags[2];
    std::uint16_t entry_count;
    std::uint32_t block_or_group_id;
};

// One compact motion-state entry in packet 0x2b. The low 12 bits of
// packed_motion select the object entry, bits 30..12 hold a quantized motion
// value, and bit 31 is a boolean state flag.
struct ObjInsMotionStateEntry {
    std::uint32_t packed_motion;
    std::uint32_t state;
};

// Packed 0x24 world-character SpEffect application.
struct WorldChrSpEffectApplyPacket {
    std::uint64_t value;

    static constexpr std::uint64_t FIELD_MASK = 0x1ffff;

    constexpr std::uint32_t chr_selector() const { return static_cast<std::uint32_t>(value & FIELD_MASK); }
    // Signed 17-bit field at bits 17..33.
    constexpr std::int32_t sp_effect_id() const {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>((value >> 17) & FIELD_MASK) << 15) >> 15;
    }
    constexpr std::uint32_t timestamp() const { return static_cast<std::uint32_t>(value >> 34); }
};

// 0x2d request sent before ObjAct state replication. The triggering character
// uses the full packed WorldChr handle format.
struct ObjActActivationRequestPacket {
    std::uint32_t triggering_chr_handle;
    std::uint8_t block_index;
    std::uint8_t trigger_state;
    std::int16_t entry_index;
};

// Operation header shared by the compact and full MapItemMan packet forms.
// All statically observed senders write version 1.
struct MapItemStateSyncHeader {
    std::uint16_t version;
    std::uint16_t operation;

    static constexpr std::uint16_t REQUEST_SNAPSHOT = 1;
    static constexpr std::uint16_t HOST_REMOVE_ENTRY = 2;
    static constexpr std::uint16_t GUEST_ENTRY_SNAPSHOT = 3;
    static constexpr std::uint16_t HOST_ENTRY_SNAPSHOT = 4;
    static constexpr std::uint16_t GUEST_CONSUMED_ENTRY = 5;
};

// Compact 0x2c request/removal/consumption packet.
struct MapItemStateSyncCompactPacket {
    MapItemStateSyncHeader header;
    std::uint32_t entry_id;
};

// Full 0x2c map-item entry packet used by operations 3 and 4.
struct MapItemStateSyncFullPacket {
    MapItemStateSyncHeader header;
    std::uint8_t entry[0x98];
};

// Header for the 0x31 per-event-object callback lane. The payload follows
// immediately and is payload_size bytes long (8 or 12 in known senders).
// SprjEmkSystem::UPDATE_FN accepts kind zero and a declared size <= 0xf0, then
// matches active events by signed object_id/object_sub_id. The final
// generation/flags word is not consulted by that routing loop. Pending events
// are merged only after receive. The observed loop does not validate that the
// received byte count covers this header and its declared payload.
struct EventObjectNetworkCallbackHeader {
    std::uint16_t packet_kind;
    std::uint16_t callback_opcode;
    std::uint32_t payload_size;
    std::int32_t object_id;
    std::int16_t object_sub_id;
    std::uint16_t object_generation_or_flags;
};

// Native 0x100-byte receive buffer, not a claim that every sender transmits
// the whole buffer. Only payload_size bytes following the header are payload.
struct EventObjectNetworkCallbackBuffer {
    EventObjectNetworkCallbackHeader header;
    std::uint8_t payload[0xf0];
};

// 0x39 transfers an Insight change to one peer. Native receive code clamps the
// resulting value to the game's 0..=99 range.
struct InsightDeltaPacket {
    std::uint8_t operation;
    std::uint8_t amount;

    static constexpr std::uint8_t SUBTRACT = 0;
    static constexpr std::uint8_t ADD = 1;
};

// 0x3b starts a remote-player special-effect snapshot. The following 0x3c
// packets contain exactly effect_count packed entries in total.
struct RemotePlayerSpEffectManifestPacket {
    std::uint32_t effect_count;
    std::uint32_t auxiliary_state;
};

// One packed entry from the 0x3c remote-player special-effect batch. Bits
// 31..15 hold the signed SpEffect ID and bits 14..0 hold a normalized
// remaining-duration ratio.
struct PackedRemotePlayerSpEffect {
    std::uint32_t value;

    static constexpr std::uint32_t DURATION_RATIO_MASK = 0x7fff;
    static constexpr float DURATION_RATIO_DENOMINATOR = 32767.0f;

    constexpr std::int32_t sp_effect_id() const { return static_cast<std::int32_t>(value) >> 15; }
    constexpr std::uint16_t duration_ratio_bits() const {
        return static_cast<std::uint16_t>(value & DURATION_RATIO_MASK);
    }
    float remaining_duration_ratio() const {
        return static_cast<float>(duration_ratio_bits()) / DURATION_RATIO_DENOMINATOR;
    }
};

// Count-prefixed 0x3f entries are full snapshots of active SprjBulletIns
// objects. The fields inside each serialized record remain unreduced.
struct BulletFullSnapshotRecord {
    std::uint8_t state[0x100];
};

// Count-prefixed 0x40 entries update compact SprjBulletManager state.
struct BulletCompactStateRecord {
    std::uint8_t state[0x20];
};

// Fixed payload copied into the player-owned auxiliary network-state object.
// The owner uses adjacent bytes +0x94 (send dirty) and +0x95 (received).
struct PlayerAuxStatePacket {
    std::uint8_t state[0x90];
};

namespace detail::world_session_packet_layout {
BB_SIZE(WorldSessionPacketHeader, 0x08);
BB_SIZE(WorldSessionEventFlagIdPacket, 0x04);
BB_SIZE(PairedChrAnimationStartPacket, 0x2c);
BB_SIZE(PairedChrAnimationProgressPacket, 0x04);
BB_SIZE(PairedChrAnimationEndPacket, 0x04);
BB_SIZE(ObjInsNetworkBatchHeader, 0x08);
BB_SIZE(ObjInsMotionStateEntry, 0x08);
BB_SIZE(WorldChrSpEffectApplyPacket, 0x08);
BB_SIZE(ObjActActivationRequestPacket, 0x08);
BB_SIZE(MapItemStateSyncHeader, 0x04);
BB_SIZE(MapItemStateSyncCompactPacket, 0x08);
BB_SIZE(MapItemStateSyncFullPacket, 0x9c);
BB_SIZE(EventObjectNetworkCallbackHeader, 0x10);
BB_SIZE(EventObjectNetworkCallbackBuffer, 0x100);
BB_OFFSET(EventObjectNetworkCallbackBuffer, payload, 0x10);
BB_OFFSET(EventObjectNetworkCallbackHeader, payload_size, 4);
BB_OFFSET(EventObjectNetworkCallbackHeader, object_id, 8);
BB_OFFSET(EventObjectNetworkCallbackHeader, object_sub_id, 0xc);
BB_OFFSET(EventObjectNetworkCallbackHeader, object_generation_or_flags, 0xe);
BB_SIZE(InsightDeltaPacket, 0x02);
BB_SIZE(RemotePlayerSpEffectManifestPacket, 0x08);
BB_SIZE(PackedRemotePlayerSpEffect, 0x04);
BB_SIZE(BulletFullSnapshotRecord, 0x100);
BB_SIZE(BulletCompactStateRecord, 0x20);
BB_SIZE(PlayerAuxStatePacket, 0x90);

constexpr bool packets_sorted_and_unique() {
    for (std::size_t i = 1; i < sizeof(OBSERVED_WORLD_SESSION_PACKETS) / sizeof(OBSERVED_WORLD_SESSION_PACKETS[0]);
         ++i) {
        if (static_cast<std::uint32_t>(OBSERVED_WORLD_SESSION_PACKETS[i - 1].packet_type) >=
            static_cast<std::uint32_t>(OBSERVED_WORLD_SESSION_PACKETS[i].packet_type))
            return false;
    }
    return true;
}
static_assert(packets_sorted_and_unique(), "OBSERVED_WORLD_SESSION_PACKETS is sorted and unique");
}  // namespace detail::world_session_packet_layout

}  // namespace bb
