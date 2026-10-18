// Bloodborne's application-layer networking boundaries.
//
// Stops below the transport: FSDP, UDP/RUDP selection, sockets and server
// transport policy are not represented. The verified native layers are
// 1. room membership (Matching2 creates, searches, joins, reports members),
// 2. signaling objects (per-peer signaling targets activated and refreshed),
// 3. WorldSession objects (peer/object records; typed gameplay messages routed
//    through WorldSessionObjectMan),
// 4. datagrams (the lower stack carries serialized WorldSession payloads).
// A three-client runtime capture shows A-B, A-C and B-C signaling all active
// and every client receiving the full room NPID list - evidence about the
// application layer, not about FSDP's topology.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Exact OrbisNpOnlineId storage (PS4 NP ABI): 16 data bytes, a terminator and
// three padding bytes, all part of the native 20-byte object.
struct OrbisNpOnlineId {
    std::uint8_t data[16];
    std::uint8_t terminator;
    std::uint8_t padding[3];
};

// Exact 36-byte OrbisNpId identifying a peer. `online_id` is the stable
// identity; opt/reserved are kept for size, not alternate keys. A display name,
// session slot, member index, endpoint or ObjectRef cannot substitute for it.
struct OrbisNpId {
    OrbisNpOnlineId online_id;
    std::uint8_t opt[8];
    std::uint8_t reserved[8];

    static constexpr std::size_t BYTE_LENGTH = 36;

    // The complete native identity, all 36 bytes (a layout view, no truncation).
    const std::uint8_t* as_bytes() const { return reinterpret_cast<const std::uint8_t*>(this); }
};

// Native per-peer signaling activation entry point. Ghidra signature:
// `undefined __stdcall CS_SignalingActivateNPIDTarget(long, undefined8)`; the
// second argument is an opaque address/reference object.
inline constexpr Rva CS_SIGNALING_ACTIVATE_NPID_TARGET_FN{0x0cd8820};
// Exports the NPID window from a resolved session address.
inline constexpr Rva NP_SESSION_ADDRESS_COPY_NPID_BYTES_FN{0x0ca25e0};
// Gate/helper that exports a resolved session address for signaling.
inline constexpr Rva NP_SESSION_ADDRESS_EXPORT_NPID_FOR_SIGNALING_FN{0x0ccc5a0};
// First byte copied by NpSessionAddress_CopyNpIdBytes, and the count.
inline constexpr std::size_t NP_SESSION_ADDRESS_NPID_OFFSET = 0x36;
inline constexpr std::size_t NP_SESSION_ADDRESS_NPID_SIZE = OrbisNpId::BYTE_LENGTH;

// Direct native local-identity acquisition chain (Ghidra).
inline constexpr const char* LOCAL_NPID_ACQUISITION =
    "sceUserServiceGetInitialUser -> sceNpGetNpId -> native context creation";

// Local Matching2 context initialization.
inline constexpr Rva MATCHING_CONTEXT_QUEUE_EVENT_FN{0x0cbcf70};
// sceNpGetNpId callsite inside MatchingContext::QueueContextEvent.
inline constexpr Rva MATCHING_CONTEXT_LOCAL_NPID_CALLSITE{0x0cbd072};
// Local NP signaling/title-lookup initialization.
inline constexpr Rva INITIALIZE_NP_SIGNALING_FOR_LOCAL_USER_FN{0x0cc0860};
// Context ID fields in the verified initializer object.
inline constexpr std::size_t LOCAL_SIGNALING_CONTEXT_ID_OFFSET = 0x78;
inline constexpr std::size_t LOCAL_TITLE_LOOKUP_CONTEXT_ID_OFFSET = 0x7c;

// Lazy native owner of NPID-keyed peer/address records. frpg_net_man.hpp
// defines it as PLAYER_DATA_MANAGER_SINGLETON (same value), so this copy
// carries the module suffix.
inline constexpr Rva PLAYER_DATA_MANAGER_SINGLETON_sprj{0x56c6d70};
inline constexpr Rva PLAYER_DATA_MANAGER_GET_SINGLETON_FN{0x0ca48f0};
// Lookup-or-create. Takes the polymorphic NPID target object, not a raw
// OrbisNpId. Under the mutex it allocates a temporary comparison record, scans
// the list, and on a miss allocates a second record for insertion, so even a
// hit depends on factory allocation. Equality is native semantic equality
// (see Ps4NpPlayerData), not exclusively NPID equality. No non-mutating lookup
// is known.
inline constexpr Rva PLAYER_DATA_MANAGER_GET_OR_CREATE_NPID_FN{0x0ca4570};
// Produces a record pointer plus an owning 0x20-byte shared reference. Factory
// failure panics at PlayerDataManager.cpp:90, a wrong runtime type at line 93;
// there is no recoverable null result.
inline constexpr Rva PLAYER_DATA_MANAGER_CREATE_RECORD_FN{0x0ca3eb0};
inline constexpr Rva PLAYER_DATA_MANAGER_CREATE_AND_INSERT_RECORD_FN{0x0ca4700};
inline constexpr Rva PS4_NP_PLAYER_DATA_CONSTRUCT_FN{0x0ca2290};
inline constexpr Rva PS4_NP_PLAYER_DATA_ASSIGN_FN{0x0ca2700};
inline constexpr Rva PS4_NP_PLAYER_DATA_EQUALS_FN{0x0ca29a0};
inline constexpr Rva PS4_NP_PLAYER_DATA_FACTORY_ALLOCATE_FN{0x2728f70};
inline constexpr std::size_t PS4_NP_PLAYER_DATA_SIZE = 0x60;
// Update/prune: retires records whose intrusive count fell to the manager's own.
inline constexpr Rva PLAYER_DATA_MANAGER_PRUNE_FN{0x0ca43d0};

inline constexpr std::size_t PLAYER_DATA_MANAGER_SIZE = 0x68;
inline constexpr std::size_t PLAYER_DATA_MANAGER_RECORD_LIST_SENTINEL_OFFSET = 0x08;
inline constexpr std::size_t PLAYER_DATA_MANAGER_RECORD_COUNT_OFFSET = 0x10;
inline constexpr std::size_t PLAYER_DATA_MANAGER_ALLOCATOR_OFFSET = 0x18;
inline constexpr std::size_t PLAYER_DATA_MANAGER_LOCAL_RECORD_REF_OFFSET = 0x20;
inline constexpr std::size_t PLAYER_DATA_MANAGER_ONLINE_ID_DATA_MANAGER_OFFSET = 0x40;
inline constexpr std::size_t PLAYER_DATA_MANAGER_STATE_FLAGS_OFFSET = 0x48;
inline constexpr std::size_t PLAYER_DATA_MANAGER_MUTEX_OFFSET = 0x50;

// Shared-reference reset/release helper used by signaling records.
inline constexpr Rva SHARED_OBJECT_RESET_FN{0x0ca1570};
// Checks one live MatchingObjectRef while holding its embedded lock.
inline constexpr Rva MATCHING_OBJECT_REF_IS_VALID_FN{0x0ca1400};
// Compares two live wrappers' targets by native semantic equality. Copied
// wrapper bytes, target-pointer equality and memcmp are invalid substitutes.
inline constexpr Rva MATCHING_OBJECT_REF_MATCHES_TARGET_FN{0x0ca1c70};
// Intrusive MatchingObjectRef copy constructor: locks the source, copies the
// target, increments its count, unlocks. The native-safe way to retain the
// connection-session reference at +0x18; byte-copying the wrapper is invalid.
inline constexpr Rva MATCHING_OBJECT_REF_COPY_CONSTRUCT_FN{0x0ca1500};

// Initializes one native per-peer session/signaling object. RVA 0x0cc6780
// resolves the room member's exact NPID to a PlayerData reference, allocates
// this 0xe0-byte object and calls this. The base initializer creates the
// session-event ObjectRef at +0x70; this then stores all 36 NPID bytes at the
// unaligned +0xb2. The two references are different targets; their placement in
// one peer object is the proven bridge from exact NPID to the ObjectRef later
// emitted in event 0x0e, copied into WorldSessionObjectMan+0x68 and assigned to
// PlayerIns+0x3d8.
inline constexpr Rva SESSION_SIGNALING_PEER_INITIALIZE_FN{0x0cc6df0};
inline constexpr std::size_t SESSION_SIGNALING_PEER_SIZE = 0xe0;
inline constexpr std::size_t SESSION_SIGNALING_PEER_OWNER_OFFSET = 0x68;
inline constexpr std::size_t SESSION_SIGNALING_PEER_EVENT_OBJECT_REF_OFFSET = 0x70;
inline constexpr std::size_t SESSION_SIGNALING_PEER_ROOM_MEMBER_ID_OFFSET = 0xb0;
inline constexpr std::size_t SESSION_SIGNALING_PEER_NPID_OFFSET = 0xb2;
inline constexpr std::size_t SESSION_SIGNALING_PEER_NPID_SIZE = OrbisNpId::BYTE_LENGTH;

// Layout marker for the per-peer session/signaling object. No packed owner:
// the NPID starts at the unaligned +0xb2; copy that byte window instead.
struct VerifiedSessionSignalingPeerLayout {};

// Verified slice of NpSessionAddress: reads at +0x36/+0x3e/+0x46/+0x4e/+0x56,
// 0x24 bytes in all. Everything around it (ownership, lifetime, local vs remote)
// is opaque; not a complete native struct.
#pragma pack(push, 1)
struct VerifiedNpSessionAddressNpIdWindow {
    OrbisNpId np_id;
};
#pragma pack(pop)

// Marker for the signaling connection-session object. State 0xf assigns a
// resolved MatchingObjectRef to +0x18; state 5 exports its NPID for activation;
// cleanup state 0xd resets +0x18 and deactivates signaling before returning to
// state 0. A caller needing the identity beyond cleanup must copy-construct its
// own reference before state 0xd and reset it after its own peer retirement.
struct VerifiedConnectionSessionLayout {};

inline constexpr std::size_t CONNECTION_SESSION_STATE_OFFSET = 0x0c;
inline constexpr std::size_t CONNECTION_SESSION_ADDRESS_REF_OFFSET = 0x18;
inline constexpr std::size_t CONNECTION_SESSION_ACTIVATION_FLAG_OFFSET = 0xae;
inline constexpr std::size_t CONNECTION_SESSION_CONNECTION_ID_OFFSET = 0xb0;
inline constexpr std::size_t CONNECTION_SESSION_SUBSTATE_OFFSET = 0xb4;
inline constexpr std::uint32_t CONNECTION_SESSION_SUBSTATE_INACTIVE = 0x00;
inline constexpr std::uint32_t CONNECTION_SESSION_SUBSTATE_ACTIVATE = 0x05;
inline constexpr std::uint32_t CONNECTION_SESSION_SUBSTATE_CLEANUP = 0x0d;
inline constexpr std::uint32_t CONNECTION_SESSION_SUBSTATE_ASSIGN_RECORD = 0x0f;

// Setter for a peer target given as a complete native address reference (not a
// slot or truncated display identity).
inline constexpr Rva CONNECTION_SESSION_SET_TARGET_NPID_FN{0x0cda490};
// Converts exact NPID/account lookup data into a native address reference.
inline constexpr Rva RESOLVE_LOOKUP_NPID_TO_SESSION_ADDRESS_FN{0x0ca4700};
// Compatibility name for the PlayerDataManager operation.
inline constexpr Rva MATCHING_OBJECT_RESOLVE_BY_NPID_FN = PLAYER_DATA_MANAGER_GET_OR_CREATE_NPID_FN;

// Signaling connection-session state machine; one opaque `astruct_66 *` arg.
inline constexpr Rva PROCESS_NP_SIGNALING_CONNECTION_SESSION_STATE_FN{0x0cd8bb0};
// Refreshes a signaling endpoint and queues its callback. Not proof a gameplay
// datagram was sent, nor that every room member was activated.
inline constexpr Rva SESSION_SIGNALING_CONTEXT_REFRESH_ENDPOINT_FN{0x0cc6fd0};
// Matching2 completion/event handler: void(matching_ctx, event_arg, request_id,
// event_code, event_payload, event_data). Room membership completes upstream of
// signaling activation and WorldSession object creation.
inline constexpr Rva MATCHING2_EVENT_HANDLER_FN{0x0cc3210};
// WorldSessionObjectMan's packet send: (manager, peer ObjectRef, u32 type,
// payload, u32 size). Returns 1 only for an immediate send; for types >= 7 a
// failed immediate send may hand a framed copy to SprjSessionManager while
// still returning 0. The application-layer handoff, not UDP/RUDP.
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_SEND_PACKET_FN{0x17894e0};

// The four application-layer ownership boundaries.
enum class ApplicationNetworkLayer {
    RoomMembership,
    SignalingObjects,
    WorldSessionObjects,
    Datagrams,
};

// Routing shortcuts forbidden by the hardening work: an inventory, not
// fallbacks. A caller must resolve the exact NPID and native route, or report
// and hold the operation.
enum class ForbiddenRoutingFallback {
    CachedEndpoint,
    RoomMemberEndpoint,
    MappedAddress,
    ZeroMemberId,
    TruncatedIdentity,
};

constexpr const char* description(ForbiddenRoutingFallback f) {
    switch (f) {
        case ForbiddenRoutingFallback::CachedEndpoint: return "cached endpoint";
        case ForbiddenRoutingFallback::RoomMemberEndpoint: return "room-member endpoint";
        case ForbiddenRoutingFallback::MappedAddress: return "mapped address";
        case ForbiddenRoutingFallback::ZeroMemberId: return "zero member ID";
        case ForbiddenRoutingFallback::TruncatedIdentity: return "truncated identity";
    }
    return "";
}

// Opaque context passed to ProcessNpSignalingConnectionSessionState.
struct NativeSignalingConnectionSession;
// Opaque signaling context passed to the endpoint refresh routine.
struct NativeSessionSignalingContext;

struct NativePs4OnlineIdData;

// Eight-byte intrusive owner used by the online-ID container and PlayerData.
// Unlike MatchingObjectRef it has no embedded lock. Native assign retains the
// source target before releasing the destination's; reset releases and clears.
// Never memcpy it or change the target's count directly.
struct NativeOnlineIdRef {
    NativePs4OnlineIdData* target;
};

// PS4 online-ID object constructed at RVA 0x0ccc6d0, allocation 0x40: the
// shorter name object owned by Ps4NpPlayerData+0x10, not the 36-byte NPID at
// Ps4NpPlayerData+0x36.
struct NativePs4OnlineIdData {
    const void* vtable;
    std::uint32_t intrusive_reference_count;
    std::uint32_t _reserved_00c;
    Unknown<0x18> mutex_shell;
    // Constructor clears all twenty bytes, then copies at most sixteen name
    // bytes. Equality RVA 0x0ccc9d0 compares those sixteen with strncmp after
    // separately locking/copying each object's name storage.
    OrbisNpOnlineId online_id;
    std::uint32_t _reserved_03c;
};

// PlayerDataManager+0x40 owner, allocated as 0x48 bytes by RVA 0x0ca2060.
// Pruning locks +0x30 and swap-removes entries whose target count is exactly
// one, so vector positions are not persistent peer identities.
struct NativeOnlineIdDataManager {
    const void* vtable;
    std::uint64_t _reserved_008;
    NativeOnlineIdRef* records_begin;
    NativeOnlineIdRef* records_end;
    NativeOnlineIdRef* records_capacity_end;
    void* allocator;
    Unknown<0x18> mutex_shell;
};

// Retains a raw intrusive target into a new eight-byte wrapper.
inline constexpr Rva NATIVE_INTRUSIVE_REF_CONSTRUCT_FN{0x0ca0f50};
// Retains source, releases destination, copies the pointer. No lock in the
// wrapper; the owner provides synchronization.
inline constexpr Rva NATIVE_INTRUSIVE_REF_ASSIGN_FN{0x0ca1180};
// Releases a wrapper's target through native lifetime handling and clears it.
inline constexpr Rva NATIVE_INTRUSIVE_REF_RESET_FN{0x0ca0f90};
// Atomic increment of target+8; does not clean up a subobject.
inline constexpr Rva NATIVE_INTRUSIVE_TARGET_ADD_REF_FN{0x0cba5c0};
// Atomic decrement; calls target virtual +0 when the previous count <= 1.
inline constexpr Rva NATIVE_INTRUSIVE_TARGET_RELEASE_FN{0x0cba5d0};
// Reads the count at +8; does not retain the target or lock an owner.
inline constexpr Rva NATIVE_INTRUSIVE_TARGET_REF_COUNT_FN{0x0cba600};
inline constexpr Rva ONLINE_ID_DATA_MANAGER_CONSTRUCT_FN{0x0ca3230};
inline constexpr Rva ONLINE_ID_DATA_MANAGER_DESTROY_BASE_FN{0x0ccbfc0};
// Prunes manager-only online-ID owners, calls manager virtual +0 under the
// lock, unlocks. The PS4 virtual at RVA 0x0ca3220 is empty.
inline constexpr Rva ONLINE_ID_DATA_MANAGER_PRUNE_FN{0x0ccc0c0};
inline constexpr Rva PS4_ONLINE_ID_DATA_CONSTRUCT_FN{0x0ccc6d0};
inline constexpr Rva PS4_ONLINE_ID_DATA_EQUALS_FN{0x0ccc9d0};
// Returns a retained online-ID owner while holding PlayerData's lock.
inline constexpr Rva PLAYER_DATA_COPY_ONLINE_ID_REF_FN{0x0ca3b80};
// Snapshots/assigns base flag and online-ID owner under the source lock, then
// the destination lock. Address/NPID copying is a later phase of
// Ps4NpPlayerData assignment, not one whole-record atomic lock.
inline constexpr Rva PLAYER_DATA_ASSIGN_BASE_FN{0x0ca3c20};

struct PlayerDataRecordNode;

// Native manager owning the NPID-keyed peer records used by signaling.
// GetOrCreateRecordByNpId locks mutex_shell, creates a temporary comparison
// record, scans the intrusive list and allocates another record on a miss: not
// an observation API. PruneUnreferencedRecords removes entries whose count fell
// to the manager's own reference. Retain a returned shared reference through
// the native copy constructor; never cache its raw target after releasing it.
struct NativePlayerDataManager {
    std::uint64_t _reserved_000;
    PlayerDataRecordNode* record_list_sentinel;
    std::size_t record_count;
    void* allocator;
    // Native 0x20-byte shared reference retained for the local record.
    Unknown<0x20> local_record_ref;
    NativeOnlineIdDataManager* online_id_data_manager;
    std::uint32_t state_flags;
    std::uint32_t _reserved_04c;
    // Polymorphic lock shell used by lookup/create and prune.
    Unknown<0x18> mutex_shell;
};

// Intrusive list node owned by NativePlayerDataManager. `record` aliases the
// shared reference's target (not an extra owner). Pruning checks the target's
// count at +0x08, unlinks the node, releases the reference and frees the node.
struct PlayerDataRecordNode {
    PlayerDataRecordNode* next;
    PlayerDataRecordNode* previous;
    // Polymorphic PlayerData base; PS4 NPID records use Ps4NpPlayerData.
    void* record;
    Unknown<0x20> record_ref;
};

// NexusRevolution's PS4 identity/address record (not a PlayerIns or save row).
// Constructor RVA 0x0ca2290 and factory RVA 0x2728f70 establish 0x60 bytes.
// Assignment RVA 0x0ca2700 copies base flag/online-ID owner, then address,
// port and NPID, under source/destination locks. Equality RVA 0x0ca29a0
// accepts identical objects, sceNpCmpNpId equality, OR equal nonzero address
// words plus equal ports: not an exact-NPID ownership check. Address/port byte
// order is not established.
struct Ps4NpPlayerData {
    const void* vtable;
    std::uint32_t intrusive_reference_count;
    std::uint8_t base_flag_0c;
    std::uint8_t _reserved_00d[3];
    // Distinct from both the NPID below and MatchingObjectRef.
    NativeOnlineIdRef online_id_owner;
    Unknown<0x18> mutex_shell;
    std::uint32_t address_raw;
    std::uint16_t port_raw;
    // Unaligned +0x36, hence a byte array.
    std::uint8_t npid_bytes[OrbisNpId::BYTE_LENGTH];
    std::uint8_t _reserved_05a[6];
};

// Argument shape of the six-parameter Matching2 handler (descriptive only; the
// payload cannot be decoded without the event code).
struct Matching2EventHandlerArguments {
    void* matching_context;
    std::uint32_t event_arg;
    std::int32_t request_id;
    std::uint32_t event_code;
    std::uint32_t event_payload;
    void* event_data;
};

namespace detail::application_network_layout {
static_assert(NP_SESSION_ADDRESS_NPID_OFFSET == 0x36, "NP_SESSION_ADDRESS_NPID_OFFSET");
static_assert(NP_SESSION_ADDRESS_NPID_SIZE == 0x24, "NP_SESSION_ADDRESS_NPID_SIZE");
BB_SIZE(OrbisNpOnlineId, 20);
BB_SIZE(OrbisNpId, OrbisNpId::BYTE_LENGTH);
BB_OFFSET(OrbisNpId, opt, 20);
BB_OFFSET(OrbisNpId, reserved, 28);
BB_SIZE(VerifiedNpSessionAddressNpIdWindow, 0x24);
BB_SIZE(NativePlayerDataManager, PLAYER_DATA_MANAGER_SIZE);
BB_OFFSET(NativePlayerDataManager, record_list_sentinel, PLAYER_DATA_MANAGER_RECORD_LIST_SENTINEL_OFFSET);
BB_OFFSET(NativePlayerDataManager, record_count, PLAYER_DATA_MANAGER_RECORD_COUNT_OFFSET);
BB_OFFSET(NativePlayerDataManager, allocator, PLAYER_DATA_MANAGER_ALLOCATOR_OFFSET);
BB_OFFSET(NativePlayerDataManager, local_record_ref, PLAYER_DATA_MANAGER_LOCAL_RECORD_REF_OFFSET);
BB_OFFSET(NativePlayerDataManager, online_id_data_manager, PLAYER_DATA_MANAGER_ONLINE_ID_DATA_MANAGER_OFFSET);
BB_OFFSET(NativePlayerDataManager, state_flags, PLAYER_DATA_MANAGER_STATE_FLAGS_OFFSET);
BB_OFFSET(NativePlayerDataManager, mutex_shell, PLAYER_DATA_MANAGER_MUTEX_OFFSET);
BB_SIZE(Ps4NpPlayerData, PS4_NP_PLAYER_DATA_SIZE);
BB_OFFSET(Ps4NpPlayerData, online_id_owner, 0x10);
BB_OFFSET(Ps4NpPlayerData, npid_bytes, NP_SESSION_ADDRESS_NPID_OFFSET);
BB_SIZE(NativePs4OnlineIdData, 0x40);
BB_SIZE(NativeOnlineIdDataManager, 0x48);
BB_OFFSET(NativeOnlineIdDataManager, mutex_shell, 0x30);
}  // namespace detail::application_network_layout

}  // namespace bb
