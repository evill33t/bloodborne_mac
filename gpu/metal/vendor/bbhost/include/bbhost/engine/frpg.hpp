// FRPG-layer classes: FrpgNetMan prefix, connection steps, SOS sign records, session type table.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Most types are markers for runtime-class metadata. Prefix structs are narrow
// layout claims only for fields whose offsets have been reduced.
// The submodules are frpg/bonfire_db.hpp and frpg/connection_debug.hpp.

struct SosSignManPrefix;  // sprj/sos_sign_man.hpp (which includes this header)


inline constexpr Rva FRPG_NET_MAN_SINGLETON_PTR{0x553b120};
inline constexpr Rva SPRJ_NETWORK_CLIENT_MAN_SINGLETON_PTR{0x5540288};

inline constexpr std::size_t FRPG_NET_MAN_FLAG_08_OFFSET = 0x08;
inline constexpr std::size_t FRPG_NET_MAN_NETWORK_JOB_TICK_ENABLED_OFFSET = 0x0b;
inline constexpr std::size_t FRPG_NET_MAN_REGION_OFFSET = 0x9e8;
inline constexpr std::size_t FRPG_NET_MAN_NAT_TYPE_OFFSET = 0x9f0;
inline constexpr std::size_t FRPG_NET_MAN_REQUEST_GATE_9F6_OFFSET = 0x9f6;
inline constexpr std::size_t FRPG_NET_MAN_REQUEST_GATE_9F8_OFFSET = 0x9f8;
inline constexpr std::size_t FRPG_NET_MAN_REQUEST_GATE_A48_OFFSET = 0xa48;
inline constexpr std::size_t FRPG_NET_MAN_REQUEST_GATE_A50_OFFSET = 0xa50;
inline constexpr std::size_t FRPG_NET_MAN_LOCAL_NP_ID_OFFSET = 0xa68;
inline constexpr std::size_t FRPG_NET_MAN_CURRENT_AREA_ID_OFFSET = 0xa78;
inline constexpr std::size_t FRPG_NET_MAN_CURRENT_AREA_REGION_ID_OFFSET = 0xa80;
inline constexpr std::size_t FRPG_NET_MAN_CURRENT_CHANNEL_ID_OFFSET = 0xa88;
inline constexpr std::size_t FRPG_NET_MAN_CHARA_ID_SOURCE_OFFSET = 0xa90;
inline constexpr std::size_t FRPG_NET_MAN_NETWORK_STATE_OFFSET = 0xa98;
inline constexpr std::size_t FRPG_NET_MAN_CURRENT_MAP_ID_OFFSET = 0xaa8;
inline constexpr std::size_t FRPG_NET_MAN_POSITION_OFFSET = 0xaac;
inline constexpr std::size_t FRPG_NET_MAN_CURRENT_BLOCK_ID_OFFSET = 0xab8;
inline constexpr std::size_t FRPG_NET_MAN_COVENANT_FLAGS_OFFSET = 0xacc;
inline constexpr std::size_t FRPG_NET_MAN_NETWORK_FLAGS_B14_OFFSET = 0xb14;
inline constexpr std::size_t FRPG_NET_MAN_SOS_SIGN_MAN_OFFSET = 0xc50;
inline constexpr std::size_t FRPG_NET_MAN_PREFIX_SIZE = 0xc58;

inline constexpr Rva FRPG_NET_CONNECT_MAN_STEP_CONSTRUCTOR{0x1479db0};
inline constexpr Rva FRPG_NET_CONNECT_MAN_STEP_INIT{0x147b350};
inline constexpr Rva FRPG_NET_CONNECT_MAN_STEP_UPDATE{0x147b4f0};
inline constexpr Rva FRPG_NET_CONNECT_MAN_STEP_FINISH{0x147ca20};
inline constexpr Rva FRPG_NET_CONNECT_MAN_STEP_QUEUE_PEER_BY_NPID{0x147af20};
// FrpgNetSysStep ownership edge: allocates the manager and stores its task
// holder at owner offset +0x30.
inline constexpr Rva FRPG_NET_SYS_STEP_CREATE_CONNECT_MANAGER{0x1493260};
inline constexpr std::size_t FRPG_NET_SYS_STEP_CONNECT_MANAGER_HOLDER_OFFSET = 0x30;
inline constexpr std::uint32_t FRPG_NET_CONNECT_MAN_STEP_REGISTERED_TYPE = 0x1a;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_SIZE = 0xd0;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_STATE_OFFSET = 0x0c;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_SHUTDOWN_REQUESTED_OFFSET = 0x28;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_ACCEPTING_REQUESTS_OFFSET = 0x29;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_ALL_HOLDER_LIST_OFFSET = 0x30;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_ACTIVE_HOLDER_LIST_OFFSET = 0x50;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_FREE_HOLDER_LIST_OFFSET = 0x70;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_CONNECTION_START_BUDGET_OFFSET = 0x90;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_BUDGET_REFILL_TIMER_OFFSET = 0x94;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_LOAD_FACTOR_OFFSET = 0x98;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_LOAD_FACTOR_TIMER_OFFSET = 0x9c;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_TARGET_POOL_COUNT_OFFSET = 0xa0;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_BASELINE_ACTIVE_TARGET_OFFSET = 0xa4;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_EXPANSION_ALLOWANCE_OFFSET = 0xa8;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_EXCESS_SELECTION_TIMER_OFFSET = 0xac;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_RAW_B0_OFFSET = 0xb0;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_RAW_B4_OFFSET = 0xb4;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_RAW_B8_OFFSET = 0xb8;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_RAW_BC_OFFSET = 0xbc;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_RAW_C0_OFFSET = 0xc0;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_FLAG_C4_OFFSET = 0xc4;
inline constexpr std::size_t FRPG_NET_CONNECT_MAN_STEP_DEBUG_PTR_OFFSET = 0xc8;
inline constexpr std::uint32_t FRPG_NET_CONNECT_MAN_STEP_INITIAL_TARGET_POOL_COUNT = 0x20;
inline constexpr std::uint32_t FRPG_NET_CONNECT_MAN_STEP_INITIAL_BASELINE_ACTIVE_TARGET = 0x14;
inline constexpr std::uint32_t FRPG_NET_CONNECT_MAN_STEP_INITIAL_EXPANSION_ALLOWANCE = 6;

inline constexpr std::size_t FRPG_NET_CONNECT_HOLDER_LIST_SIZE = 0x20;
inline constexpr std::size_t FRPG_NET_CONNECT_HOLDER_LIST_SENTINEL_OFFSET = 0x08;
inline constexpr std::size_t FRPG_NET_CONNECT_HOLDER_LIST_COUNT_OFFSET = 0x10;
inline constexpr std::size_t FRPG_NET_CONNECT_HOLDER_LIST_ALLOCATOR_OFFSET = 0x18;

inline constexpr std::size_t FRPG_NET_CONNECT_STEP_SIZE = 0xf8;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_STATE_OFFSET = 0x0c;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_CANCEL_OFFSET = 0x28;
// Historical name retained for compatibility. The constructor stores the
// parent FrpgNetConnectManStep here, not a retained summon result.
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_RETAINED_SUMMON_RESULT_OWNER_OFFSET = 0x30;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_LOCKED_CONNECTION_SLOT_OFFSET = 0x38;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_CALLBACK_STATE_5C_OFFSET = 0x5c;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_CALLBACK_STATE_60_OFFSET = 0x60;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_CONNECTION_NAME_OFFSET = 0x64;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_NPID_BYTE_LENGTH_OFFSET = 0x74;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_NPID_BYTES_OFFSET = 0x76;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_ASYNC_HANDLE_OFFSET = 0xc8;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_FAILURE_OR_CANCEL_OFFSET = 0xd0;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_TELEMETRY_PTR_OFFSET = 0xd8;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_TIMER_OFFSET = 0xe0;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_DEBUG_FLAG_OFFSET = 0xe4;
inline constexpr std::size_t FRPG_NET_CONNECT_STEP_TAIL_STATE_OFFSET = 0xe8;
inline constexpr std::uint32_t FRPG_NET_CONNECT_STEP_BEGIN_PEER_CONNECT_STATE = 8;
inline constexpr std::uint32_t FRPG_NET_CONNECT_STEP_WAIT_PEER_CONNECT_STATE = 9;
inline constexpr std::uint32_t FRPG_NET_CONNECT_STEP_FAILURE_CLEANUP_STATE = 14;
inline constexpr std::uint32_t FRPG_NET_CONNECT_STEP_CANCEL_CLEANUP_STATE = 15;
// Historical name: -1 completes the holder. Reusable initialization is state
// zero; see connection_debug::INITIALIZE_STATE and the dispatcher at 8014808c0.
inline constexpr std::int32_t FRPG_NET_CONNECT_STEP_IDLE_STATE = -1;
inline constexpr Rva FRPG_NET_CONNECT_STEP_BEGIN_PEER_CONNECT{0x1481620};
inline constexpr Rva FRPG_NET_CONNECT_STEP_WAIT_PEER_CONNECT{0x1481770};
inline constexpr Rva FRPG_NET_CONNECT_STEP_FAILURE_CLEANUP{0x1481c20};
inline constexpr Rva FRPG_NET_CONNECT_STEP_CANCEL_CLEANUP{0x1481cc0};
inline constexpr Rva FRPG_NET_CONNECT_STEP_MARK_FAILED{0x1482c70};

inline constexpr std::size_t SOS_SIGN_MAN_NATIVE_CANDIDATE_LIST_OFFSET_frpg = 0x10; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_ACTIVE_SIGN_LIST_OFFSET_frpg = 0x30; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_CONNECT_MANAGER_OFFSET_frpg = 0x48; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_GET_SOS_STEP_OFFSET_frpg = 0x50; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_SUMMON_TASK_OFFSET_frpg = 0x58; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_TARGET_USER_ID_OFFSET = 0x120;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_TARGET_CHARA_ID_OFFSET = 0x128;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_DESCRIPTOR_ID_OFFSET = 0x130;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_RESPONSE_PAYLOAD_OFFSET = 0x138;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_RESPONSE_SIZE_OFFSET = 0x140;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_RESULT_CODE_OFFSET = 0x144;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_TASK_COMPLETE_OFFSET = 0x148;
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_CREATE_LIST_OFFSET_frpg = 0x158; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_CLEAR_LIST_OFFSET_frpg = 0x178; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_RESPONSE_LIST_OFFSET_frpg = 0x1a8; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_DEFERRED_REQUEST_RESULT_QUEUE_OFFSET_frpg = 0x1c0; // renamed: sprj/sos_sign_man defines the same name
inline constexpr std::size_t SOS_SIGN_MAN_PREFIX_SIZE = 0x238;
inline constexpr std::size_t SOS_SIGN_MAN_LIST_HEADER_SIZE = 0x18;

// Populates the session-info object at SprjSessionManager+0x38 under the
// lock at SprjSessionManager+0x18 via a vtable +0x28 call, then validates
// it with SESSION_INFO_HAS_ROOM_KEY.
inline constexpr Rva SESSION_INFO_POPULATE{0x0c8f500};
// Returns true when the session-info blob is present, i.e. both
// SESSION_INFO_ROOM_KEY_BLOB_OFFSET and
// SESSION_INFO_ROOM_KEY_LEN_OFFSET are non-zero. Gates the handoff send.
inline constexpr Rva SESSION_INFO_HAS_ROOM_KEY{0x0c90d80};
// Writes the online-id wide string, a credential blob, then a u16 length
// followed by the room-key blob. Produces the type-0x02 payload.
inline constexpr Rva SESSION_INFO_SERIALIZE_WITH_ROOM_KEY{0x0c90b00};
// Exact mirror of SESSION_INFO_SERIALIZE_WITH_ROOM_KEY; the guest-side
// reader that repopulates the blob before JoinRoomWithContextLock.
inline constexpr Rva SESSION_INFO_DESERIALIZE_WITH_ROOM_KEY{0x0c90dd0};
// Guarded blob copy: yields data only when the requested length exactly
// matches SESSION_INFO_ROOM_KEY_LEN_OFFSET. Called with 0x10 by
// MatchingManager::JoinRoomUsingStoredRoomId.
inline constexpr Rva SESSION_INFO_COPY_ROOM_KEY{0x0c90a90};
// Deep copy propagating the room-key blob pointer and length.
inline constexpr Rva SESSION_INFO_DEEP_COPY{0x0c90270};

// Room-key blob pointer on the session-info object.
inline constexpr std::size_t SESSION_INFO_ROOM_KEY_BLOB_OFFSET = 0x68;
// u16 length of the room-key blob. Observed as 0x10 for a Matching2 room id.
inline constexpr std::size_t SESSION_INFO_ROOM_KEY_LEN_OFFSET = 0x70;
// Online-id wide string written first by the serializer.
inline constexpr std::size_t SESSION_INFO_ONLINE_ID_OFFSET = 0x28;
// Credential blob written between the online id and the room-key blob.
inline constexpr std::size_t SESSION_INFO_CREDENTIAL_BLOB_OFFSET = 0x60;
// Session-info object owned by SprjSessionManager, serialized by the handoff.
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_INFO_OFFSET = 0x38;
// Lock guarding SPRJ_SESSION_MANAGER_SESSION_INFO_OFFSET.
inline constexpr std::size_t SPRJ_SESSION_MANAGER_SESSION_INFO_LOCK_OFFSET = 0x18;

inline constexpr Rva SOS_SIGN_MAN_PENDING_CREATE_CLEAR_MOVE_HELPER{0x14b9d40};
inline constexpr Rva SOS_SIGN_MAN_PENDING_CREATE_CLEAR_MOVE_REMOVE_HELPER{0x14b9e50};
inline constexpr Rva SOS_SIGN_MAN_PENDING_CREATE_CLEAR_SPECIFIC_HELPER{0x14bb3d0};
inline constexpr Rva SOS_SIGN_MAN_LIST_WRAPPER_PUSH_PAIR_HELPER{0x14bf4a0};
inline constexpr Rva SOS_SIGN_PENDING_RESPONSE_DESCRIPTOR_ID_LOOKUP_HELPER{0x14baec0};
// Drains queued API results and dispatches each result through its owner's
// callback. This helper is shared by multiple FrpgNetMan request managers;
// it is not owned specifically by SosSignMan.
inline constexpr Rva FRPG_NET_REQUEST_MANAGER_DRAIN_QUEUED_RESULTS_HELPER{0x1484b50};
inline constexpr Rva SOS_SIGN_MAN_TICK_SCHEDULED_WORK_HELPER{0x1872360};
inline constexpr Rva SOS_SIGN_MAN_FILTER_REQUEST_ENTRY_HELPER{0x1874710};
inline constexpr Rva SOS_SIGN_MAN_INSERT_OR_UPDATE_REQUEST_ENTRY_HELPER{0x1875320};
inline constexpr Rva SOS_SIGN_MAN_MAYBE_SCHEDULE_REQUEST_ENTRY_HELPER{0x1876060};
inline constexpr Rva SOS_SIGN_MAN_BUILD_SCHEDULED_WORK_ENTRY_HELPER{0x1877460};
inline constexpr Rva SOS_SIGN_MAN_BUILD_REQUEST_ENTRY_FROM_CHR_HELPER{0x1878d90};
inline constexpr Rva SOS_SIGN_SCHEDULED_WORK_RESOLVE_WORLD_CHR_HELPER{0x191a440};
inline constexpr Rva SOS_SIGN_SCHEDULED_WORK_CREATE_MULTI_PLAYER_TASK_HELPER{0x1e54c30};
inline constexpr Rva SOS_SIGN_SCHEDULED_WORK_SEND_MESSAGE_HELPER{0x17894e0};
inline constexpr Rva SOS_SIGN_SELECTION_STATE_UPDATE_HELPER{0x130c590};
inline constexpr Rva LUA_EVENT_DISPATCH_BY_NAME_HELPER{0x1339870};
inline constexpr Rva SPRJ_EVENT_AREA_STATE_REQUEST_SOS_ENTRY_HELPER{0x13deaf0};
inline constexpr Rva SPRJ_EVENT_AREA_STATE_CLEAR_SOS_ENTRIES_HELPER{0x13dec10};

inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_SIZE = 0x68;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_DESCRIPTOR_ID_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_ELAPSED_OFFSET = 0x04;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_SIGN_TYPE_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_TIMEOUT_GATE_OFFSET = 0x0c;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_NAME_UTF16_OFFSET = 0x10;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_WORLD_CHR_LOOKUP_KEY_OFFSET = 0x34;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_REQUEST_FIELD_8C_OFFSET = 0x38;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_REQUEST_FIELD_90_OFFSET = 0x3c;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_POSITION_OFFSET = 0x40;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_YAW_OFFSET = 0x4c;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_RESOURCE_HANDLE_A_OFFSET = 0x58;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_RESOURCE_HANDLE_B_OFFSET = 0x60;
inline constexpr std::int32_t SOS_SIGN_SCHEDULED_WORK_INVALID_WORLD_CHR_LOOKUP_KEY = -1;

inline constexpr std::size_t SOS_SIGN_REQUEST_DESCRIPTOR_WORLD_CHR_LOOKUP_KEY_OFFSET = 0x88;
inline constexpr std::size_t SOS_SIGN_REQUEST_DESCRIPTOR_SCHEDULED_FIELD_8C_OFFSET = 0x8c;
inline constexpr std::size_t SOS_SIGN_REQUEST_DESCRIPTOR_SCHEDULED_FIELD_90_OFFSET = 0x90;

inline constexpr std::uint32_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_TYPE = 0x32;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_PAYLOAD_SIZE = 0x10;
inline constexpr std::uint8_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_MARKER = 1;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_RESOLVED_WORLD_CHR_FIELD_08_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_REQUEST_FIELD_8C_OFFSET = 0x04;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_REQUEST_FIELD_90_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_SUMMON_TYPE_OFFSET = 0x0c;
inline constexpr std::size_t SOS_SIGN_SCHEDULED_WORK_MESSAGE_MARKER_OFFSET = 0x0d;

inline constexpr Rva SESSION_TYPE_DESC_TABLE{0x553d750};
inline constexpr std::size_t SESSION_TYPE_DESC_COUNT = 0x22;
inline constexpr std::size_t SESSION_TYPE_DESC_SIZE = 0x80;
inline constexpr std::size_t SESSION_TYPE_DESC_SESSION_TYPE_ID_OFFSET = 0x00;
inline constexpr std::size_t SESSION_TYPE_DESC_CAPABILITY_MASK_OFFSET = 0x04;
inline constexpr std::size_t SESSION_TYPE_DESC_FLAGS_OFFSET = 0x08;
inline constexpr std::size_t SESSION_TYPE_DESC_SUMMONPARAM_TYPE_OFFSET = 0x0c;
inline constexpr std::size_t SESSION_TYPE_DESC_TEAM_TYPE_OFFSET = 0x10;
inline constexpr std::size_t SESSION_TYPE_DESC_SUMMON_TYPE_OFFSET = 0x14;
inline constexpr std::size_t SESSION_TYPE_DESC_FIELD_15_OFFSET = 0x15;
inline constexpr std::size_t SESSION_TYPE_DESC_FLAGS_16_OFFSET = 0x16;
inline constexpr std::size_t SESSION_TYPE_DESC_TIMEOUT_SECONDS_OFFSET = 0x18;
inline constexpr std::size_t SESSION_TYPE_DESC_ENTERLEAVE_TITLE_MESSAGE_ID_OFFSET = 0x28;
inline constexpr std::size_t SESSION_TYPE_DESC_ENTERLEAVE_DISPLAY_MESSAGE_ID_OFFSET = 0x2c;
inline constexpr std::size_t SESSION_TYPE_DESC_ENTERLEAVE_NAME_MESSAGE_ID_OFFSET = 0x34;
inline constexpr std::size_t SESSION_TYPE_DESC_LABEL_OFFSET = 0x78;

inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_SIZE = 0xf8;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_SIGN_TYPE_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_UNIQUE_ID_LOW_OFFSET = 0x0c;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_PAYLOAD_CANDIDATE_OFFSET = 0x18;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_OBJECT_REF_OFFSET = 0x20;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_AREA_ID_OFFSET = 0x84;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_POSITION_OFFSET = 0x88;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_REQUEST_TAIL_OFFSET = 0xa8;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_TIMER_OFFSET = 0xe8;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_GENERATION_OFFSET = 0xec;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_AVAILABLE_COUNT_OFFSET = 0xed;
inline constexpr std::size_t SOS_SIGN_ACTIVE_SIGN_QUEUED_COUNT_OFFSET = 0xee;

inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_ENTRY_SIZE = 0x28;
inline constexpr std::size_t SOS_SIGN_REQUEST_DESCRIPTOR_PREFIX_SIZE = 0x04;
inline constexpr std::size_t SOS_SIGN_REQUEST_DESCRIPTOR_ID_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_SIGN_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_DESCRIPTOR_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_STATE_OFFSET = 0x10;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_GENERATION_OFFSET = 0x11;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_CLEANUP_HANDLE_OFFSET = 0x18;
inline constexpr std::size_t SOS_SIGN_PENDING_RESPONSE_DEBUG_HANDLE_OFFSET = 0x20;

inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_ENTRY_SIZE = 0x30;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_DESCRIPTOR_ID_OFFSET = 0x04;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_PAYLOAD_SIZE_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_PAYLOAD_OFFSET = 0x10;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_RETRY_COUNT_OFFSET = 0x18;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_RETRY_LIMIT_OFFSET = 0x1a;
inline constexpr std::size_t SOS_SIGN_REQUEST_RESULT_TIMER_OFFSET = 0x1c;

inline constexpr std::size_t SOS_SIGN_CANDIDATE_SIZE = 0x78;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_VTABLE_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_ID_OFFSET = 0x04;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_OWNER_OR_PAYLOAD_SIZE_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_OBJECT_REF_OFFSET = 0x20;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_LIST_OFFSET = 0x30;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_COUNT_OFFSET = 0x38;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_ALLOCATOR_OFFSET = 0x40;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_REMAINING_OFFSET = 0x70;

inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_WRAPPER_SIZE = 0x10;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_WRAPPER_VTABLE_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_WRAPPER_BUFFER_OFFSET = 0x08;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_BUFFER_HEADER_SIZE = 0x3c;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_KIND_OFFSET = 0x00;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_FLAG_OFFSET = 0x01;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_HEADER_SIZE_OFFSET = 0x02;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_UNIQUE_ID_LOW_OFFSET = 0x04;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_DESCRIPTOR_ID_OFFSET = 0x0c;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_AREA_REGION_ID_OFFSET = 0x10;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_BLOCK_ID_OFFSET = 0x1c;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_POSITION_X_OFFSET = 0x20;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_MAP_ID_OFFSET = 0x24;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_PAYLOAD_SIZE_OFFSET = 0x34;
inline constexpr std::size_t SOS_SIGN_CANDIDATE_RESULT_PAYLOAD_OFFSET = 0x38;



// Renamed from OBSERVED_CLASSES / ObservedStringEncoding / OBSERVED_DEFERRED_STRINGS:
// fd4, frpg, cs and sprj each define one.
inline constexpr const char* OBSERVED_CLASSES_frpg[] = {
    "FrpgMenuDialog",        "FrpgMenuDlgBloodMsg", "FrpgMenuDlgEquip", "FrpgMenuDlgInventory",
    "FrpgMenuDlgRepository", "FrpgMenuDlgStatus",   "FrpgMenuDlgSystem", "FrpgNetConnectManStep",
    "FrpgNetConnectStep",    "FrpgNetLobbyStep",    "FrpgNetMan",        "FrpgNetSysStep",
    "FrpgOrthoCam",          "FrpgPhysIns",         "FrpgSaveLoadMan",
};

enum class ObservedStringEncoding_frpg {
    Ascii,
    Utf16,
};

struct ObservedFrpgString {
    const char* value;
    Rva address;
    ObservedStringEncoding_frpg encoding;
};

inline constexpr ObservedFrpgString OBSERVED_DEFERRED_STRINGS_frpg[] = {
    {"FrpgMenuDialog", Rva{0x49310ce}, ObservedStringEncoding_frpg::Ascii},
    {"FrpgMenuDialog", Rva{0x4976a7c}, ObservedStringEncoding_frpg::Utf16},
    {"FrpgNetMan", Rva{0x493ad45}, ObservedStringEncoding_frpg::Ascii},
    {"FrpgNetMan", Rva{0x49c0588}, ObservedStringEncoding_frpg::Utf16},
    {"FrpgSaveLoadMan", Rva{0x49313b1}, ObservedStringEncoding_frpg::Ascii},
};

// Runtime-class markers (no layout claimed).
#define BB_FRPG_MARKER(Name, Class)                                    \
    struct Name {                                                      \
        static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = Class; \
    }
BB_FRPG_MARKER(FrpgOrthoCam, FRPG_ORTHO_CAM_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgBloodMsg, FRPG_MENU_DLG_BLOOD_MSG_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgEquip, FRPG_MENU_DLG_EQUIP_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgInventory, FRPG_MENU_DLG_INVENTORY_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgRepository, FRPG_MENU_DLG_REPOSITORY_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgStatus, FRPG_MENU_DLG_STATUS_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgMenuDlgSystem, FRPG_MENU_DLG_SYSTEM_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgNetSysStep, FRPG_NET_SYS_STEP_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgNetLobbyStep, FRPG_NET_LOBBY_STEP_RUNTIME_CLASS);
BB_FRPG_MARKER(FrpgPhysIns, FRPG_PHYS_INS_RUNTIME_CLASS);
#undef BB_FRPG_MARKER

// Holder-list wrapper embedded three times in FrpgNetConnectManStep. The
// leading eight bytes are opaque; only the constructor and update accesses to
// sentinel, count and allocator are claimed.
struct FrpgNetConnectHolderList {
    Unknown<0x08> _unk000;
    void* sentinel;
    std::size_t count;
    void* allocator;
};

// Conservatively reduced layout of the FrpgNetConnectManStep allocation.
// FrpgNetConnectManStep_Ctor constructs this 0xd0 object. The manager owns a
// fixed holder pool, moves holders between free and active lists, and queues
// or refreshes one child connection workflow by native NPID. Published through
// both FrpgNetSysStep and SosSignMan; not a summon-result object.
struct FrpgNetConnectManStep {
    Unknown<0x0c> _unk000;
    std::uint32_t step_state_raw;
    Unknown<0x18> _unk010;
    std::uint8_t shutdown_requested;
    std::uint8_t accepting_requests;
    Unknown<0x06> _unk02a;
    FrpgNetConnectHolderList all_holder_list;
    FrpgNetConnectHolderList active_holder_list;
    FrpgNetConnectHolderList free_holder_list;
    // Consumed when a newly queued NPID arms a free child workflow and
    // replenished by the update timer.
    std::uint32_t connection_start_budget;
    float budget_refill_timer;
    // Elapsed-time interpolation between the start/end budget refill intervals.
    // Not measured CPU or network load (native update 80147b4f0).
    float load_factor;
    // Remaining interpolation time in seconds, initialized to 600.
    float load_factor_timer;
    std::uint32_t target_pool_count;
    std::uint32_t baseline_active_target;
    std::uint32_t expansion_allowance;
    float excess_selection_timer;
    // Debug display: active count, after native classification.
    std::uint32_t raw_b0;
    // Debug display: connected active count; includes protected session peers.
    std::uint32_t raw_b4;
    // Debug display: active "ambassador" count (native menu terminology).
    std::uint32_t raw_b8;
    // Desired maps still lacking a representative, not total requested maps.
    std::uint32_t raw_bc;
    // Node-selection skip threshold, calculated from connection coverage.
    std::uint32_t raw_c0;
    // Enables the native connection-diagram renderer, not packet capture.
    std::uint8_t flag_c4_raw;
    Unknown<0x03> _unk0c5;
    void* debug_ptr;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = FRPG_NET_CONNECT_MAN_STEP_RUNTIME_CLASS;
};

// Conservatively reduced layout of the FrpgNetConnectStep allocation. States
// 8-15 reduced through the callback table at RVA 0x5551008: 8 starts peer
// connection, 9 waits for the locked connection slot, 14 failure cleanup, 15
// cancellation cleanup, -1 completes the holder. State zero initializes a
// reusable step and clears the latched failure byte once the slot is idle.
struct FrpgNetConnectStep {
    Unknown<0x0c> _unk000;
    std::uint32_t step_state_raw;
    Unknown<0x18> _unk010;
    std::uint8_t cancel_raw;
    Unknown<0x07> _unk029;
    // Historical field name. Actually the parent FrpgNetConnectManStep:
    // 801480300 stores it and uses its +0xc8 debug-menu node.
    void* retained_summon_result_owner;
    void* locked_connection_slot;
    Unknown<0x1c> _unk040;
    std::uint32_t callback_state_5c_raw;
    std::uint32_t route_type;  // connection_debug::ConnectionRoute
    std::uint8_t connection_name_bytes[0x10];
    std::uint16_t npid_byte_length;
    std::uint8_t npid_bytes[0x52];
    void* peer_connect_callback_owner;
    std::uint8_t peer_connect_failed;
    Unknown<0x07> _unk0d1;
    void* telemetry_ptr;
    float timer;
    // Session-retention protection, displayed as a star in the debug menu.
    // 801480d10 renews it for retained native session references.
    std::uint8_t debug_flag_raw;
    Unknown<0x03> _unk0e5;
    // Raw IEEE-754 bits of the protection timer in seconds (initial renewal 30).
    std::uint32_t tail_state_raw;
    Unknown<0x0c> _unk0ec;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = FRPG_NET_CONNECT_STEP_RUNTIME_CLASS;
};

template <typename T>
struct SosSignManList {
    T* sentinel;
    std::size_t count;
    void* allocator;

    std::size_t size() const { return count; }
    bool empty() const { return count == 0; }
};

template <typename T>
struct SosSignManOwnedList {
    void* owner_state;
    T* sentinel;
    std::size_t count;
    void* allocator;
};

struct SosSignActiveSignEntry {
    Unknown<0x08> _unk000;
    std::uint8_t sign_type;
    Unknown<0x03> _unk009;
    std::uint32_t unique_id_low;
    Unknown<0x08> _unk010;
    void* payload_candidate;
    std::uint8_t object_ref[0x10];
    Unknown<0x54> _unk030;
    std::uint32_t area_id;
    float position[4];
    Unknown<0x10> _unk098;
    std::uint64_t request_tail[0x08];
    float sign_timer;
    std::uint8_t generation;
    std::uint8_t available_count;
    std::uint8_t queued_count;
    Unknown<0x09> _unk0ef;
};

struct SosSignNativeCandidateEntry {
    std::uint32_t vftable_low;
    std::uint32_t id;
    void* owner_or_payload_size;
    Unknown<0x10> _unk010;
    void* object_ref;
    Unknown<0x08> _unk028;
    void* result_list;
    std::size_t result_count;
    void* result_allocator;
    Unknown<0x08> _unk048;
    void* consumer_result_list;
    std::size_t consumer_result_count;
    void* consumer_result_allocator;
    Unknown<0x08> _unk068;
    std::int32_t remaining;
    Unknown<0x04> _unk074;
};

using SosSignPendingCreateEntry = SosSignActiveSignEntry;
using SosSignPendingClearEntry = SosSignActiveSignEntry;

// Confirmed identity prefix shared by synthetic and pending-response request
// descriptors. RVA 0x14baec0 looks up pending response state by this +0x00 ID.
// Live testing confirmed that a fresh synthetic descriptor must carry the
// pending descriptor's ID before RVA 0x1876060 can attach the room-result
// payload to the pending sign and continue signaling/room transfer.
struct SosSignRequestDescriptorPrefix {
    std::int32_t descriptor_id;

    constexpr bool has_same_identity(const SosSignRequestDescriptorPrefix& other) const {
        return descriptor_id == other.descriptor_id;
    }
};

struct SosSignPendingResponseEntry {
    SosSignActiveSignEntry* sign;
    SosSignRequestDescriptorPrefix* descriptor;
    std::uint8_t state;
    std::uint8_t generation;
    Unknown<0x06> _unk012;
    void* cleanup_handle;
    void* debug_handle;
};

struct SosSignRequestResultEntry {
    std::uint32_t status;
    std::int32_t descriptor_id;
    std::uint32_t payload_size;
    Unknown<0x04> _unk00c;
    std::uint8_t* payload;
    // Remaining attempts to queue the peer through FrpgNetConnectManStep by
    // NPID. Native construction initializes this to one.
    std::uint16_t peer_route_attempts_remaining;
    // Remaining three-second processing cycles. Native construction
    // initializes this to five and retires the entry at zero.
    std::uint16_t retry_cycles_remaining;
    float retry_timer_seconds;
    Unknown<0x10> _unk020;
};

struct SosSignCandidateResultBufferHeader;

struct SosSignCandidateResultWrapper {
    void* vftable;
    SosSignCandidateResultBufferHeader* buffer;
};

struct SosSignCandidateResultBufferHeader {
    std::uint8_t kind;
    std::uint8_t flag;
    std::uint16_t header_size;
    std::uint8_t unique_id_low[0x08];
    std::uint32_t descriptor_id;
    std::uint32_t area_region_id;
    Unknown<0x08> _unk014;
    std::uint32_t block_id;
    float position_x;
    std::uint32_t map_id;
    Unknown<0x0c> _unk028;
    std::uint32_t payload_size;
    std::uint32_t payload_prefix;
};

// Compatibility mirror of the event-side scheduled-work record. It belongs to
// SprjEventSosSelectionState, not the native SosSignMan at FrpgNetMan + 0xc50.
// New code should use SprjEventSosScheduledWorkEntry (sprj).
struct SosSignScheduledWorkEntry {
    std::int32_t descriptor_id;
    float elapsed;
    std::uint8_t sign_type;
    Unknown<0x03> _unk009;
    std::uint32_t timeout_gate;
    std::uint8_t name_utf16[0x20];
    Unknown<0x04> _unk030;
    std::int32_t world_chr_lookup_key_raw;
    std::int32_t request_field_8c_raw;
    std::int32_t request_field_90_raw;
    float position[3];
    float yaw;
    std::uint64_t _unk050;
    std::uint64_t resource_handle_a;
    std::uint64_t resource_handle_b;

    // Whether RVA 0x1872360 may enter the world-character task/send branch.
    bool has_world_chr_lookup_key() const {
        return world_chr_lookup_key_raw != SOS_SIGN_SCHEDULED_WORK_INVALID_WORLD_CHR_LOOKUP_KEY;
    }
};

// Direct payload sent as WorldSessionObjectMan message type 0x32.
// RVA 0x1872360 resolves the scheduled +0x34 key through RVA 0x191a440, copies
// the resolved WorldChr entry's raw +0x08 dword and the scheduled +0x38/+0x3c
// dwords, then sends this 16-byte payload to each non-local session peer
// through RVA 0x17894e0. The trailing two bytes remain unknown.
struct SosSignScheduledWorkMessagePayload {
    std::uint32_t resolved_world_chr_field_08_raw;
    std::int32_t request_field_8c_raw;
    std::int32_t request_field_90_raw;
    std::uint8_t summon_type;
    std::uint8_t marker;
    Unknown<0x02> _unk0e;
};

// Entry in the static session descriptor table at RVA 0x553d750.
// SprjEventSosScheduledWorkEntry + 0x08 indexes this table. The event-side
// scheduled-work paths use it to gate capability checks and choose the
// summon-parameter payload emitted to the session request path.
struct SessionTypeDesc {
    std::uint32_t session_type_id;
    std::uint32_t capability_mask;
    std::uint8_t flags;
    Unknown<0x03> _unk009;
    std::int32_t summonparam_type;
    std::uint32_t team_type;
    std::uint8_t summon_type;
    std::uint8_t field_15;
    std::uint8_t flags_16;
    std::uint8_t _unk017;
    float timeout_seconds;
    Unknown<0x0c> _unk01c;
    std::uint32_t enterleave_title_message_id;
    std::uint32_t enterleave_display_message_id;
    Unknown<0x04> _unk030;
    std::uint32_t enterleave_name_message_id;
    Unknown<0x40> _unk038;
    const std::uint8_t* label;
};

// Prefix reaching the SosSignMan pointer inside FrpgNetMan.
struct FrpgNetManPrefix {
    Unknown<0x08> _unk000;
    std::uint8_t flag_08;
    Unknown<0x02> _unk009;
    std::uint8_t network_job_tick_enabled;
    Unknown<0x9dc> _unk00c;
    std::uint32_t region;
    Unknown<0x04> _unk9ec;
    std::uint32_t nat_type;
    Unknown<0x02> _unk9f4;
    std::uint8_t request_gate_9f6;
    std::uint8_t _unk9f7;
    std::uint8_t request_gate_9f8;
    Unknown<0x4f> _unk9f9;
    std::uint8_t request_gate_a48;
    Unknown<0x07> _unka49;
    std::uint8_t request_gate_a50;
    Unknown<0x17> _unka51;
    std::uint8_t local_np_id[0x10];
    std::int32_t current_area_id;
    std::uint32_t _unka7c;
    std::int32_t current_area_region_id;
    std::uint32_t _unka84;
    std::uint64_t current_channel_id;
    void* chara_id_source;
    std::uint32_t network_state;
    Unknown<0x0c> _unka9c;
    std::uint32_t current_map_id;
    float position_x;
    float position_y;
    float position_z;
    std::uint32_t current_block_id;
    Unknown<0x10> _unkabc;
    std::uint32_t covenant_flags;
    Unknown<0x44> _unkad0;
    std::uint8_t network_flags_b14;
    Unknown<0x13b> _unkb15;
    SosSignManPrefix* sos_sign_man;
};

namespace detail::frpg_layout {
BB_SIZE(FrpgNetConnectStep, FRPG_NET_CONNECT_STEP_SIZE);
BB_OFFSET(FrpgNetConnectStep, step_state_raw, FRPG_NET_CONNECT_STEP_STATE_OFFSET);
BB_OFFSET(FrpgNetConnectStep, cancel_raw, FRPG_NET_CONNECT_STEP_CANCEL_OFFSET);
BB_OFFSET(FrpgNetConnectStep, retained_summon_result_owner, FRPG_NET_CONNECT_STEP_RETAINED_SUMMON_RESULT_OWNER_OFFSET);
BB_OFFSET(FrpgNetConnectStep, locked_connection_slot, FRPG_NET_CONNECT_STEP_LOCKED_CONNECTION_SLOT_OFFSET);
BB_OFFSET(FrpgNetConnectStep, callback_state_5c_raw, FRPG_NET_CONNECT_STEP_CALLBACK_STATE_5C_OFFSET);
BB_OFFSET(FrpgNetConnectStep, route_type, FRPG_NET_CONNECT_STEP_CALLBACK_STATE_60_OFFSET);
BB_OFFSET(FrpgNetConnectStep, connection_name_bytes, FRPG_NET_CONNECT_STEP_CONNECTION_NAME_OFFSET);
BB_OFFSET(FrpgNetConnectStep, npid_byte_length, FRPG_NET_CONNECT_STEP_NPID_BYTE_LENGTH_OFFSET);
BB_OFFSET(FrpgNetConnectStep, npid_bytes, FRPG_NET_CONNECT_STEP_NPID_BYTES_OFFSET);
BB_OFFSET(FrpgNetConnectStep, peer_connect_callback_owner, FRPG_NET_CONNECT_STEP_ASYNC_HANDLE_OFFSET);
BB_OFFSET(FrpgNetConnectStep, peer_connect_failed, FRPG_NET_CONNECT_STEP_FAILURE_OR_CANCEL_OFFSET);
BB_OFFSET(FrpgNetConnectStep, telemetry_ptr, FRPG_NET_CONNECT_STEP_TELEMETRY_PTR_OFFSET);
BB_OFFSET(FrpgNetConnectStep, timer, FRPG_NET_CONNECT_STEP_TIMER_OFFSET);
BB_OFFSET(FrpgNetConnectStep, debug_flag_raw, FRPG_NET_CONNECT_STEP_DEBUG_FLAG_OFFSET);
BB_OFFSET(FrpgNetConnectStep, tail_state_raw, FRPG_NET_CONNECT_STEP_TAIL_STATE_OFFSET);

BB_SIZE(FrpgNetConnectHolderList, FRPG_NET_CONNECT_HOLDER_LIST_SIZE);
BB_OFFSET(FrpgNetConnectHolderList, sentinel, FRPG_NET_CONNECT_HOLDER_LIST_SENTINEL_OFFSET);
BB_OFFSET(FrpgNetConnectHolderList, count, FRPG_NET_CONNECT_HOLDER_LIST_COUNT_OFFSET);
BB_OFFSET(FrpgNetConnectHolderList, allocator, FRPG_NET_CONNECT_HOLDER_LIST_ALLOCATOR_OFFSET);
BB_SIZE(FrpgNetConnectManStep, FRPG_NET_CONNECT_MAN_STEP_SIZE);
BB_OFFSET(FrpgNetConnectManStep, step_state_raw, FRPG_NET_CONNECT_MAN_STEP_STATE_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, shutdown_requested, FRPG_NET_CONNECT_MAN_STEP_SHUTDOWN_REQUESTED_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, accepting_requests, FRPG_NET_CONNECT_MAN_STEP_ACCEPTING_REQUESTS_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, all_holder_list, FRPG_NET_CONNECT_MAN_STEP_ALL_HOLDER_LIST_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, active_holder_list, FRPG_NET_CONNECT_MAN_STEP_ACTIVE_HOLDER_LIST_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, free_holder_list, FRPG_NET_CONNECT_MAN_STEP_FREE_HOLDER_LIST_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, connection_start_budget, FRPG_NET_CONNECT_MAN_STEP_CONNECTION_START_BUDGET_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, budget_refill_timer, FRPG_NET_CONNECT_MAN_STEP_BUDGET_REFILL_TIMER_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, load_factor, FRPG_NET_CONNECT_MAN_STEP_LOAD_FACTOR_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, load_factor_timer, FRPG_NET_CONNECT_MAN_STEP_LOAD_FACTOR_TIMER_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, target_pool_count, FRPG_NET_CONNECT_MAN_STEP_TARGET_POOL_COUNT_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, baseline_active_target, FRPG_NET_CONNECT_MAN_STEP_BASELINE_ACTIVE_TARGET_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, expansion_allowance, FRPG_NET_CONNECT_MAN_STEP_EXPANSION_ALLOWANCE_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, excess_selection_timer, FRPG_NET_CONNECT_MAN_STEP_EXCESS_SELECTION_TIMER_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, raw_b0, FRPG_NET_CONNECT_MAN_STEP_RAW_B0_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, raw_b4, FRPG_NET_CONNECT_MAN_STEP_RAW_B4_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, raw_b8, FRPG_NET_CONNECT_MAN_STEP_RAW_B8_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, raw_bc, FRPG_NET_CONNECT_MAN_STEP_RAW_BC_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, raw_c0, FRPG_NET_CONNECT_MAN_STEP_RAW_C0_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, flag_c4_raw, FRPG_NET_CONNECT_MAN_STEP_FLAG_C4_OFFSET);
BB_OFFSET(FrpgNetConnectManStep, debug_ptr, FRPG_NET_CONNECT_MAN_STEP_DEBUG_PTR_OFFSET);

BB_SIZE(FrpgNetManPrefix, FRPG_NET_MAN_PREFIX_SIZE);
BB_OFFSET(FrpgNetManPrefix, flag_08, FRPG_NET_MAN_FLAG_08_OFFSET);
BB_OFFSET(FrpgNetManPrefix, network_job_tick_enabled, FRPG_NET_MAN_NETWORK_JOB_TICK_ENABLED_OFFSET);
BB_OFFSET(FrpgNetManPrefix, region, FRPG_NET_MAN_REGION_OFFSET);
BB_OFFSET(FrpgNetManPrefix, nat_type, FRPG_NET_MAN_NAT_TYPE_OFFSET);
BB_OFFSET(FrpgNetManPrefix, request_gate_9f6, FRPG_NET_MAN_REQUEST_GATE_9F6_OFFSET);
BB_OFFSET(FrpgNetManPrefix, request_gate_9f8, FRPG_NET_MAN_REQUEST_GATE_9F8_OFFSET);
BB_OFFSET(FrpgNetManPrefix, request_gate_a48, FRPG_NET_MAN_REQUEST_GATE_A48_OFFSET);
BB_OFFSET(FrpgNetManPrefix, request_gate_a50, FRPG_NET_MAN_REQUEST_GATE_A50_OFFSET);
BB_OFFSET(FrpgNetManPrefix, local_np_id, FRPG_NET_MAN_LOCAL_NP_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, current_area_id, FRPG_NET_MAN_CURRENT_AREA_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, current_area_region_id, FRPG_NET_MAN_CURRENT_AREA_REGION_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, current_channel_id, FRPG_NET_MAN_CURRENT_CHANNEL_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, chara_id_source, FRPG_NET_MAN_CHARA_ID_SOURCE_OFFSET);
BB_OFFSET(FrpgNetManPrefix, network_state, FRPG_NET_MAN_NETWORK_STATE_OFFSET);
BB_OFFSET(FrpgNetManPrefix, current_map_id, FRPG_NET_MAN_CURRENT_MAP_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, position_x, FRPG_NET_MAN_POSITION_OFFSET);
BB_OFFSET(FrpgNetManPrefix, current_block_id, FRPG_NET_MAN_CURRENT_BLOCK_ID_OFFSET);
BB_OFFSET(FrpgNetManPrefix, covenant_flags, FRPG_NET_MAN_COVENANT_FLAGS_OFFSET);
BB_OFFSET(FrpgNetManPrefix, network_flags_b14, FRPG_NET_MAN_NETWORK_FLAGS_B14_OFFSET);
BB_OFFSET(FrpgNetManPrefix, sos_sign_man, FRPG_NET_MAN_SOS_SIGN_MAN_OFFSET);

// SosSignManPrefix's own size/offset checks live in sprj/sos_sign_man.hpp
// (it embeds these lists, so this header cannot include it).
using PendingResponseList = SosSignManList<SosSignPendingResponseEntry>;
BB_SIZE(PendingResponseList, SOS_SIGN_MAN_LIST_HEADER_SIZE);

BB_SIZE(SosSignScheduledWorkEntry, SOS_SIGN_SCHEDULED_WORK_SIZE);
BB_SIZE(SosSignActiveSignEntry, SOS_SIGN_ACTIVE_SIGN_SIZE);
BB_SIZE(SosSignPendingResponseEntry, SOS_SIGN_PENDING_RESPONSE_ENTRY_SIZE);
BB_SIZE(SosSignRequestResultEntry, SOS_SIGN_REQUEST_RESULT_ENTRY_SIZE);
BB_SIZE(SosSignNativeCandidateEntry, SOS_SIGN_CANDIDATE_SIZE);
BB_SIZE(SosSignCandidateResultWrapper, SOS_SIGN_CANDIDATE_RESULT_WRAPPER_SIZE);
BB_SIZE(SosSignCandidateResultBufferHeader, SOS_SIGN_CANDIDATE_RESULT_BUFFER_HEADER_SIZE);
BB_SIZE(SessionTypeDesc, SESSION_TYPE_DESC_SIZE);
BB_SIZE(SosSignRequestDescriptorPrefix, SOS_SIGN_REQUEST_DESCRIPTOR_PREFIX_SIZE);
BB_OFFSET(SosSignRequestDescriptorPrefix, descriptor_id, SOS_SIGN_REQUEST_DESCRIPTOR_ID_OFFSET);

BB_OFFSET(SosSignScheduledWorkEntry, descriptor_id, SOS_SIGN_SCHEDULED_WORK_DESCRIPTOR_ID_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, elapsed, SOS_SIGN_SCHEDULED_WORK_ELAPSED_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, sign_type, SOS_SIGN_SCHEDULED_WORK_SIGN_TYPE_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, timeout_gate, SOS_SIGN_SCHEDULED_WORK_TIMEOUT_GATE_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, name_utf16, SOS_SIGN_SCHEDULED_WORK_NAME_UTF16_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, world_chr_lookup_key_raw, SOS_SIGN_SCHEDULED_WORK_WORLD_CHR_LOOKUP_KEY_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, request_field_8c_raw, SOS_SIGN_SCHEDULED_WORK_REQUEST_FIELD_8C_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, request_field_90_raw, SOS_SIGN_SCHEDULED_WORK_REQUEST_FIELD_90_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, position, SOS_SIGN_SCHEDULED_WORK_POSITION_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, yaw, SOS_SIGN_SCHEDULED_WORK_YAW_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, resource_handle_a, SOS_SIGN_SCHEDULED_WORK_RESOURCE_HANDLE_A_OFFSET);
BB_OFFSET(SosSignScheduledWorkEntry, resource_handle_b, SOS_SIGN_SCHEDULED_WORK_RESOURCE_HANDLE_B_OFFSET);

BB_SIZE(SosSignScheduledWorkMessagePayload, SOS_SIGN_SCHEDULED_WORK_MESSAGE_PAYLOAD_SIZE);
BB_OFFSET(SosSignScheduledWorkMessagePayload, resolved_world_chr_field_08_raw,
          SOS_SIGN_SCHEDULED_WORK_MESSAGE_RESOLVED_WORLD_CHR_FIELD_08_OFFSET);
BB_OFFSET(SosSignScheduledWorkMessagePayload, request_field_8c_raw, SOS_SIGN_SCHEDULED_WORK_MESSAGE_REQUEST_FIELD_8C_OFFSET);
BB_OFFSET(SosSignScheduledWorkMessagePayload, request_field_90_raw, SOS_SIGN_SCHEDULED_WORK_MESSAGE_REQUEST_FIELD_90_OFFSET);
BB_OFFSET(SosSignScheduledWorkMessagePayload, summon_type, SOS_SIGN_SCHEDULED_WORK_MESSAGE_SUMMON_TYPE_OFFSET);
BB_OFFSET(SosSignScheduledWorkMessagePayload, marker, SOS_SIGN_SCHEDULED_WORK_MESSAGE_MARKER_OFFSET);

BB_OFFSET(SessionTypeDesc, session_type_id, SESSION_TYPE_DESC_SESSION_TYPE_ID_OFFSET);
BB_OFFSET(SessionTypeDesc, capability_mask, SESSION_TYPE_DESC_CAPABILITY_MASK_OFFSET);
BB_OFFSET(SessionTypeDesc, flags, SESSION_TYPE_DESC_FLAGS_OFFSET);
BB_OFFSET(SessionTypeDesc, summonparam_type, SESSION_TYPE_DESC_SUMMONPARAM_TYPE_OFFSET);
BB_OFFSET(SessionTypeDesc, team_type, SESSION_TYPE_DESC_TEAM_TYPE_OFFSET);
BB_OFFSET(SessionTypeDesc, summon_type, SESSION_TYPE_DESC_SUMMON_TYPE_OFFSET);
BB_OFFSET(SessionTypeDesc, field_15, SESSION_TYPE_DESC_FIELD_15_OFFSET);
BB_OFFSET(SessionTypeDesc, flags_16, SESSION_TYPE_DESC_FLAGS_16_OFFSET);
BB_OFFSET(SessionTypeDesc, timeout_seconds, SESSION_TYPE_DESC_TIMEOUT_SECONDS_OFFSET);
BB_OFFSET(SessionTypeDesc, enterleave_title_message_id, SESSION_TYPE_DESC_ENTERLEAVE_TITLE_MESSAGE_ID_OFFSET);
BB_OFFSET(SessionTypeDesc, enterleave_display_message_id, SESSION_TYPE_DESC_ENTERLEAVE_DISPLAY_MESSAGE_ID_OFFSET);
BB_OFFSET(SessionTypeDesc, enterleave_name_message_id, SESSION_TYPE_DESC_ENTERLEAVE_NAME_MESSAGE_ID_OFFSET);
BB_OFFSET(SessionTypeDesc, label, SESSION_TYPE_DESC_LABEL_OFFSET);

BB_OFFSET(SosSignActiveSignEntry, sign_type, SOS_SIGN_ACTIVE_SIGN_SIGN_TYPE_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, unique_id_low, SOS_SIGN_ACTIVE_SIGN_UNIQUE_ID_LOW_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, payload_candidate, SOS_SIGN_ACTIVE_SIGN_PAYLOAD_CANDIDATE_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, object_ref, SOS_SIGN_ACTIVE_SIGN_OBJECT_REF_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, area_id, SOS_SIGN_ACTIVE_SIGN_AREA_ID_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, position, SOS_SIGN_ACTIVE_SIGN_POSITION_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, request_tail, SOS_SIGN_ACTIVE_SIGN_REQUEST_TAIL_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, sign_timer, SOS_SIGN_ACTIVE_SIGN_TIMER_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, generation, SOS_SIGN_ACTIVE_SIGN_GENERATION_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, available_count, SOS_SIGN_ACTIVE_SIGN_AVAILABLE_COUNT_OFFSET);
BB_OFFSET(SosSignActiveSignEntry, queued_count, SOS_SIGN_ACTIVE_SIGN_QUEUED_COUNT_OFFSET);

BB_OFFSET(SosSignPendingResponseEntry, sign, SOS_SIGN_PENDING_RESPONSE_SIGN_OFFSET);
BB_OFFSET(SosSignPendingResponseEntry, descriptor, SOS_SIGN_PENDING_RESPONSE_DESCRIPTOR_OFFSET);
BB_OFFSET(SosSignPendingResponseEntry, state, SOS_SIGN_PENDING_RESPONSE_STATE_OFFSET);
BB_OFFSET(SosSignPendingResponseEntry, generation, SOS_SIGN_PENDING_RESPONSE_GENERATION_OFFSET);
BB_OFFSET(SosSignPendingResponseEntry, cleanup_handle, SOS_SIGN_PENDING_RESPONSE_CLEANUP_HANDLE_OFFSET);
BB_OFFSET(SosSignPendingResponseEntry, debug_handle, SOS_SIGN_PENDING_RESPONSE_DEBUG_HANDLE_OFFSET);

BB_OFFSET(SosSignRequestResultEntry, descriptor_id, SOS_SIGN_REQUEST_RESULT_DESCRIPTOR_ID_OFFSET);
BB_OFFSET(SosSignRequestResultEntry, payload_size, SOS_SIGN_REQUEST_RESULT_PAYLOAD_SIZE_OFFSET);
BB_OFFSET(SosSignRequestResultEntry, payload, SOS_SIGN_REQUEST_RESULT_PAYLOAD_OFFSET);
BB_OFFSET(SosSignRequestResultEntry, peer_route_attempts_remaining, SOS_SIGN_REQUEST_RESULT_RETRY_COUNT_OFFSET);
BB_OFFSET(SosSignRequestResultEntry, retry_cycles_remaining, SOS_SIGN_REQUEST_RESULT_RETRY_LIMIT_OFFSET);
BB_OFFSET(SosSignRequestResultEntry, retry_timer_seconds, SOS_SIGN_REQUEST_RESULT_TIMER_OFFSET);

BB_OFFSET(SosSignNativeCandidateEntry, vftable_low, SOS_SIGN_CANDIDATE_VTABLE_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, id, SOS_SIGN_CANDIDATE_ID_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, owner_or_payload_size, SOS_SIGN_CANDIDATE_OWNER_OR_PAYLOAD_SIZE_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, object_ref, SOS_SIGN_CANDIDATE_OBJECT_REF_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, result_list, SOS_SIGN_CANDIDATE_RESULT_LIST_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, result_count, SOS_SIGN_CANDIDATE_RESULT_COUNT_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, result_allocator, SOS_SIGN_CANDIDATE_RESULT_ALLOCATOR_OFFSET);
BB_OFFSET(SosSignNativeCandidateEntry, remaining, SOS_SIGN_CANDIDATE_REMAINING_OFFSET);

BB_OFFSET(SosSignCandidateResultWrapper, vftable, SOS_SIGN_CANDIDATE_RESULT_WRAPPER_VTABLE_OFFSET);
BB_OFFSET(SosSignCandidateResultWrapper, buffer, SOS_SIGN_CANDIDATE_RESULT_WRAPPER_BUFFER_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, kind, SOS_SIGN_CANDIDATE_RESULT_KIND_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, flag, SOS_SIGN_CANDIDATE_RESULT_FLAG_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, header_size, SOS_SIGN_CANDIDATE_RESULT_HEADER_SIZE_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, unique_id_low, SOS_SIGN_CANDIDATE_RESULT_UNIQUE_ID_LOW_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, descriptor_id, SOS_SIGN_CANDIDATE_RESULT_DESCRIPTOR_ID_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, area_region_id, SOS_SIGN_CANDIDATE_RESULT_AREA_REGION_ID_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, block_id, SOS_SIGN_CANDIDATE_RESULT_BLOCK_ID_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, position_x, SOS_SIGN_CANDIDATE_RESULT_POSITION_X_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, map_id, SOS_SIGN_CANDIDATE_RESULT_MAP_ID_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, payload_size, SOS_SIGN_CANDIDATE_RESULT_PAYLOAD_SIZE_OFFSET);
BB_OFFSET(SosSignCandidateResultBufferHeader, payload_prefix, SOS_SIGN_CANDIDATE_RESULT_PAYLOAD_OFFSET);
}  // namespace detail::frpg_layout

}  // namespace bb
