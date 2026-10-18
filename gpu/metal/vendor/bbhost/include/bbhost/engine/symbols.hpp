// Bloodborne 1.09 image-relative addresses (functions, globals, vtables, runtime-class
// and step-template evidence) anchored in eboot.bin.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Resolve every symbol against the image base: Rva::bn() is the Binary Ninja address.
//
// Names that another header also defines (same value, checked when this header was
// generated) carry a `_symbols` suffix here so both headers can share a translation unit;
// ALL_SYMBOLS lists them under their own name.

// Element count of a constexpr table.
template <typename T, std::size_t N>
constexpr std::size_t count_of(const T (&)[N]) { return N; }

struct StepCallbackSymbol;

// SosSignMan summon/request boundaries verified in Bloodborne 1.09 eboot.bin.
// These are address symbols only; callable ABI exposure belongs to the
// bounded `sprj::SosSignMan` layout module.
inline constexpr Rva SOS_SIGN_MAN_UPDATE_AND_QUEUE_SUMMON_API_JOBS_symbols{0x14b5e10};  // ALL_SYMBOLS name SOS_SIGN_MAN_UPDATE_AND_QUEUE_SUMMON_API_JOBS (also in sprj/sos_sign_man.hpp)
inline constexpr Rva SOS_SIGN_MAN_QUEUE_PENDING_SIGN_symbols{0x14b8d10};  // ALL_SYMBOLS name SOS_SIGN_MAN_QUEUE_PENDING_SIGN (also in sprj/sos_sign_man.hpp)
inline constexpr Rva SOS_SIGN_MAN_QUEUE_OR_STAGE_SUMMON_REQUEST_symbols{0x14baec0};  // ALL_SYMBOLS name SOS_SIGN_MAN_QUEUE_OR_STAGE_SUMMON_REQUEST (also in sprj/sos_sign_man.hpp)
inline constexpr Rva SOS_SIGN_MAN_CLEAR_SPECIFIC_PENDING_CREATE_symbols{0x14bb3d0};  // ALL_SYMBOLS name SOS_SIGN_MAN_CLEAR_SPECIFIC_PENDING_CREATE (also in sprj/sos_sign_man.hpp)

// These 0x187xxxx routines belong to the event-side SOS selection state. They
// were formerly attributed to the FrpgNetMan-owned SosSignMan.
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_INSERT_OR_UPDATE_REQUEST{0x1875320};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_MAYBE_SCHEDULE_REQUEST{0x1876060};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_BUILD_SCHEDULED_WORK{0x1877460};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_BUILD_REQUEST_FROM_CHR{0x1878d90};
inline constexpr Rva SPRJ_EVENT_SOS_SELECTION_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW{0x1872360};

// Compatibility aliases for downstream address tables. New code should use
// the SPRJ_EVENT_SOS_SELECTION_* names above.
inline constexpr Rva SOS_SIGN_MAN_INSERT_OR_UPDATE_REQUEST_ENTRY = SPRJ_EVENT_SOS_SELECTION_INSERT_OR_UPDATE_REQUEST;
inline constexpr Rva SOS_SIGN_MAN_MAYBE_SCHEDULE_REQUEST_ENTRY = SPRJ_EVENT_SOS_SELECTION_MAYBE_SCHEDULE_REQUEST;
inline constexpr Rva SOS_SIGN_MAN_BUILD_SCHEDULED_WORK_ENTRY = SPRJ_EVENT_SOS_SELECTION_BUILD_SCHEDULED_WORK;
inline constexpr Rva SOS_SIGN_MAN_BUILD_REQUEST_ENTRY_FROM_CHR = SPRJ_EVENT_SOS_SELECTION_BUILD_REQUEST_FROM_CHR;
inline constexpr Rva SOS_SIGN_MAN_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW = SPRJ_EVENT_SOS_SELECTION_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW;
// Runtime class registration address evidence.
struct RuntimeClassSymbol {
    const char* name;
    // Global storage that receives a pointer to `runtime_class`.
    Rva runtime_class_ptr;
    // Embedded `DLRuntimeClassImpl` object.
    Rva runtime_class;
    // Registration function that writes the class names and initializes links.
    Rva registration_function;
    // Parent class observed or strongly implied during registration.
    const char* probable_base_class;  // nullptr when there is none
};

// FD4/Sprj step-template callback metadata observed during class registration.
struct StepTemplateSymbol {
    const char* owner;
    // First table entry for the named step callbacks.
    Rva table;
    Rva registration_function;
    const StepCallbackSymbol* steps;
    std::size_t steps_count;
};

// One named step callback in a step-template table.
struct StepCallbackSymbol {
    const char* name;
    Rva callback;
    Rva name_string;
};

// Nexus/PSN matching function or handler address evidence.
struct MatchingFunctionSymbol {
    const char* name;
    Rva function;
};

// Nexus/PSN matching string anchor evidence.
struct MatchingStringSymbol {
    const char* name;
    Rva address;
};

inline constexpr Rva WORLD_CHR_MAN_SINGLETON_PTR{0x553e878};

// Global storage for the native 0x20-byte altar-gate owner.
inline constexpr Rva SPRJ_HOLYGRAIL_SINGLETON_PTR{0x553e9e8};

// Global storage for the native 0xe8-byte dark-sight volume owner.
inline constexpr Rva SPRJ_DARK_SIGHT_SINGLETON_PTR{0x553e918};

// Local native summon-area eligibility. Checks the local player's selected
// area handle (+0x278/+0x27c), its <1,000,000 bound, and the area's disable flag.
// The event-side room receiver calls this before deserializing a received room
// and clears the pending-create request even when this predicate returns false.
inline constexpr Rva WORLD_CHR_MAN_IS_SUMMON_AREA_ELIGIBLE{0x191a750};

// Indexed [map - 21][block][region]; positive entries identify disable flags.
// M24_00 region 0 maps to flag 2405. This is not a party-capacity predicate.
inline constexpr Rva SUMMON_AREA_DISABLE_FLAG_TABLE{0x47301b0};

// Global storage for the `SprjTaskImp*` singleton.
inline constexpr Rva SPRJ_TASK_SINGLETON_PTR{0x5540510};

// Global storage for the location-update manager created by SprjFD4LocationStep.
inline constexpr Rva SPRJ_FD4_LOCATION_SINGLETON_PTR{0x5540078};

// Global storage for the `SprjTaskGroup*` singleton.
inline constexpr Rva SPRJ_TASK_GROUP_SINGLETON_PTR{0x5540518};

// Global storage for the `FD4TaskManager*` used by `SprjTask`.
inline constexpr Rva FD4_TASK_MANAGER_SINGLETON_PTR{0x54b2e30};

// Allocates and registers a task in one of Bloodborne's 76 timeline groups.
inline constexpr Rva SPRJ_TASK_REGISTER_FN{0x2050490};

// Submits a prepared registration node to the selected timeline group.
inline constexpr Rva SPRJ_TASK_SUBMIT_REGISTRATION_FN{0x2051300};

// Removes a registration node from the native task manager.
inline constexpr Rva SPRJ_TASK_UNREGISTER_FN{0x2050560};

// Native callback-task method that changes its timeline group.
inline constexpr Rva SPRJ_TASK_SET_GROUP_FN{0x20501c0};

// Native callback-task method that unregisters its current handle.
inline constexpr Rva SPRJ_TASK_BASE_UNREGISTER_FN{0x2050210};

// Builds the complete 76-entry Bloodborne timeline-group table.
inline constexpr Rva SPRJ_TASK_GROUPS_CONSTRUCT_FN{0x2051a90};

// Registration-node thunk that calls task vtable slot `+0x18`.
inline constexpr Rva SPRJ_TASK_REGISTRATION_EXECUTE_FN{0x20507c0};

// Generic callback-task execute dispatcher.
inline constexpr Rva SPRJ_CALLBACK_TASK_EXECUTE_DISPATCH_FN{0x142de10};

// Generic callback-task owner/callback invocation thunk.
inline constexpr Rva SPRJ_CALLBACK_TASK_INVOKE_MEMBER_FN{0x1956290};

// FD4 manager registration boundary used by `SprjTask`.
inline constexpr Rva FD4_TASK_MANAGER_REGISTER_FN{0x0f890a0};

// FD4 manager removal boundary used by `SprjTask`.
inline constexpr Rva FD4_TASK_MANAGER_UNREGISTER_FN{0x0f89320};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_READ_BIT_FN{0x13cfc00};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_WRITE_BIT_FN{0x13cfcc0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_READ_RANGE_FN{0x13cfd80};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_WRITE_RANGE_FN{0x13d0060};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SET_LOAD_MODE_FN{0x13bb590};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_RESOLVE_GROUP_KEY_FN{0x13bc710};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SERIALIZE_SHARED_SNAPSHOT_FN{0x13be3c0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_APPEND_FLAG_GROUP_FN{0x13beac0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_APPLY_SNAPSHOT_FN{0x13beca0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG_FN_symbols{0x132aad0};  // ALL_SYMBOLS name SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG_FN (also in sprj/world_session_packet.hpp)
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_CLEAR_BIT_RANGE_FN{0x13d0430};
inline constexpr Rva WORLD_CHR_MAN_DBG_SINGLETON_PTR{0x553e880};

// Singleton pointer for the `0x2c8`-byte frame pacing and timing owner.
inline constexpr Rva SPRJ_FLIPPER_SINGLETON_PTR{0x55404f8};
inline constexpr Rva SPRJ_FLIPPER_CONSTRUCTOR_FN{0x2034520};
inline constexpr Rva SPRJ_FLIPPER_UPDATE_FN{0x2034770};
inline constexpr Rva SPRJ_FLIPPER_DEBUG_FORMAT_FN{0x2035480};
inline constexpr Rva SPRJ_FLIPPER_MODE_NAME_TABLE{0x535a920};

// Factory that selects the character-sync implementation by integer type.
inline constexpr Rva CHR_SYNC_FACTORY_FN{0x18c0280};

// Constructs the `0x1a0` type-1 sync controller used by the local player.
inline constexpr Rva LOCAL_PLAYER_CHR_SYNC_CONSTRUCTOR_FN{0x1526410};

// Captures and broadcasts a player snapshot when the `+0xc8` counter expires.
inline constexpr Rva LOCAL_PLAYER_CHR_SYNC_UPDATE_FN{0x1529470};

// Instruction `mov edx, 5` that supplies the native config fallback.
inline constexpr Rva CHR_SYNC_FRAME_SKIP_FALLBACK_INSTRUCTION{0x1529675};

// UTF-16 key `ChrSync.FrameSkipCount` read by the update routine.
inline constexpr Rva CHR_SYNC_FRAME_SKIP_CONFIG_KEY{0x497ec2e};

// Constructs the `0x1d8` player-frame snapshot state.
inline constexpr Rva PLAYER_FRAME_SNAPSHOT_CONSTRUCTOR_FN{0x1a71fb0};

// Serializes and broadcasts a snapshot as WorldSessionObjectMan type `0x06`.
inline constexpr Rva PLAYER_FRAME_SNAPSHOT_BROADCAST_FN{0x1a72220};

// Receives and parses WorldSessionObjectMan type `0x06` snapshots.
inline constexpr Rva PLAYER_FRAME_SNAPSHOT_RECEIVE_FN{0x1a6d390};

// Resets all per-character arrays in one `NetWorldChrSyncBlock`.
inline constexpr Rva NET_WORLD_CHR_SYNC_BLOCK_RESET_FN{0x18eb900};

// Initializes the `0x26d8`-byte `NetWorldChrSync` object.
inline constexpr Rva NET_WORLD_CHR_SYNC_CONSTRUCTOR_FN{0x18ec020};

// Main `NetWorldChrSync` host/client message update.
inline constexpr Rva NET_WORLD_CHR_SYNC_UPDATE_FN{0x18eceb0};

// Updates the packed-handle list exchanged with session peers.
inline constexpr Rva NET_WORLD_CHR_SYNC_PEER_HANDLE_UPDATE_FN{0x18f0780};

// Constructs the `NetWorldChrSync` UTF-16 debug-menu name. The retail menu
// registration creates an empty folder and attaches no native controls.
inline constexpr Rva NET_WORLD_CHR_SYNC_DEBUG_MENU_NAME_FN{0x18f0f50};

// Callsite that adds the empty `NetWorldChrSync` folder under `CHR INS`.
inline constexpr Rva NET_WORLD_CHR_SYNC_DEBUG_MENU_FOLDER_CALLSITE{0x15679bc};

// Computes and stores one enemy/NPC's ranked authority score.
inline constexpr Rva CHR_INS_COMPUTE_NETWORK_AUTHORITY_SCORE_FN{0x18cd180};

// Direct setter for the `ChrIns+0x1ec` authority-priority override.
inline constexpr Rva CHR_INS_SET_NETWORK_UPDATE_AUTHORITY_FN{0x18cd440};

// Debug-configurable bonus for an enemy/NPC already controlled locally.
inline constexpr Rva NETWORK_AUTHORITY_LOCAL_CONTROL_BONUS{0x556f938};

// Looks up or allocates a host-side authority record for one session peer.
inline constexpr Rva NET_WORLD_CHR_SYNC_GET_OR_CREATE_AUTHORITY_PEER_FN{0x18f0b70};

// Constructs a `0x28`-byte per-peer authority record.
inline constexpr Rva NET_WORLD_CHR_SYNC_AUTHORITY_PEER_CONSTRUCTOR_FN{0x1a6c420};

// Replaces one peer record's packed authority-candidate list.
inline constexpr Rva NET_WORLD_CHR_SYNC_SET_AUTHORITY_CANDIDATES_FN{0x1a6c590};

// Resolves duplicate enemy handles between two peer candidate records.
inline constexpr Rva NET_WORLD_CHR_SYNC_RESOLVE_AUTHORITY_CONFLICTS_FN{0x1a6c700};

// Lua helper that writes Forced (`0x0fff`) authority for a resolved entity.
inline constexpr Rva LUA_REQUEST_FORCE_UPDATE_NETWORK_FN{0x1335070};

// Lua helper that restores Normal (`0`) authority for a resolved entity.
inline constexpr Rva LUA_REQUEST_NORMAL_UPDATE_NETWORK_FN{0x13350a0};

// Constructs the fixed `0x2b18`-byte map metadata owner.
inline constexpr Rva WORLD_INFO_CONSTRUCTOR_FN{0x1562510};

// Constructs the `0x13830`-byte `WorldRes` map-resource owner.
inline constexpr Rva WORLD_RES_CONSTRUCTOR_FN{0x15632a0};

// Canonical writer for the process-local `MapCollisionEntry*` at `ChrIns+0x288`.
inline constexpr Rva CHR_INS_SET_MAP_COLLISION_ENTRY_FN{0x18bba70};

// Constructs the `ChrCtrl` object stored at `ChrIns+0x58`.
inline constexpr Rva CHR_CTRL_CONSTRUCTOR_FN{0x1511bd0};

// Constructs the `0xb0`-byte controller state stored at `ChrCtrl+0x38`.
inline constexpr Rva CHR_CTRL_NESTED_STATE_CONSTRUCTOR_FN{0x150a480};

// Mirrors nested controller `+0x5d bit 0` into the cached-collision route bit
// at `ChrIns+0x1e1 bit 0x20`.
inline constexpr Rva CHR_INS_UPDATE_COLLISION_BINDING_MODE_FROM_CONTROLLER_FN{0x18e5ef0};

// EMEVD `SetDrawGroup`; binds a collision entry and sets `ChrIns+0x29c`.
inline constexpr Rva LUA_SET_DRAW_GROUP_FN{0x13347d0};

// EMEVD `SetHitInfo`; binds a collision entry without setting `ChrIns+0x29c`.
inline constexpr Rva LUA_SET_HIT_INFO_FN{0x1334820};

// EMEVD `SetDisableBackread_forEvent`; writes `ChrIns+0x28`.
inline constexpr Rva LUA_SET_DISABLE_BACKREAD_FOR_EVENT_FN{0x1334fa0};

// EMEVD `SetAlwayEnableBackread_forEvent`; writes `ChrIns+0x29`.
inline constexpr Rva LUA_SET_ALWAYS_ENABLE_BACKREAD_FOR_EVENT_FN{0x1339150};

// Returns whether a character has an active or area-managed backread route.
inline constexpr Rva CHR_INS_HAS_ACTIVE_BACKREAD_ROUTE_FN{0x18c0240};

// Runtime connected-block path that selects a candidate collision entry.
inline constexpr Rva CHR_INS_UPDATE_MAP_COLLISION_BINDING_FN{0x18becf0};

// Resolves spawn group/index metadata into this process's collision-entry pool.
inline constexpr Rva CHR_INS_INITIALIZE_MAP_COLLISION_FROM_SPAWN_FN{0x18d8be0};

// Main-player fallback that only binds when `ChrIns+0x288` is null.
inline constexpr Rva WORLD_CHR_MAN_REBIND_MAIN_PLAYER_MAP_COLLISION_FN_symbols{0x19ec030};  // ALL_SYMBOLS name WORLD_CHR_MAN_REBIND_MAIN_PLAYER_MAP_COLLISION_FN (also in sprj/world_chr_man.hpp)

// Applies transition occupancy state to an existing character. Client role 6
// skips one occupancy-bit write performed for other roles.
inline constexpr Rva CHR_INS_APPLY_TRANSITION_OCCUPANCY_STATE_FN{0x18c49f0};

// PlayerIns virtual `+0xb0`, called immediately after remote-player creation.
// Rebinds child-module records to the global or current map-group WorldChr
// binding. It performs assignment but exposes no completion result.
inline constexpr Rva PLAYER_INS_REFRESH_MODULE_WORLD_BINDINGS_FN{0x18f4a50};

// Candidate native reinitialization routine. Its owning class and virtual
// dispatch are unresolved; it is not verified as the live PlayerIns +0x78
// method. It ends by calling the transition-reset finalizer.
inline constexpr Rva CANDIDATE_REINITIALIZE_RUNTIME_STATE_FN{0x18e7900};

// Finalizes PlayerIns transition reset: rebuilds controller state, clears
// transition occupancy bits, and invokes the actor lifecycle callbacks.
inline constexpr Rva PLAYER_INS_FINALIZE_TRANSITION_RESET_FN{0x18cc080};

// Rebuilds the nested ChrCtrl control word from the current controller flags.
inline constexpr Rva CHR_CTRL_REBUILD_CONTROL_WORD_FN{0x1516520};

// Applies the older transition-snapshot format before refreshing retained
// character occupancy state.
inline constexpr Rva WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V15091700_FN{0x190e8b0};

// Applies the newer transition-snapshot format before refreshing retained
// character occupancy state.
inline constexpr Rva WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V10080300_FN{0x190e9b0};

// Consumes a serialized transition snapshot into existing world characters.
inline constexpr Rva WORLD_CHR_MAN_CONSUME_TRANSITION_SNAPSHOT_FN_symbols{0x191bdd0};  // ALL_SYMBOLS name WORLD_CHR_MAN_CONSUME_TRANSITION_SNAPSHOT_FN (also in sprj/world_chr_man.hpp)

// Builds WorldChrMan's per-frame character update lists and applies
// `ChrSetEntry` backread/area residency filtering.
inline constexpr Rva WORLD_CHR_MAN_BUILD_CHARACTER_UPDATE_LISTS_FN_symbols{0x19173b0};  // ALL_SYMBOLS name WORLD_CHR_MAN_BUILD_CHARACTER_UPDATE_LISTS_FN (also in sprj/world_chr_man.hpp)

// Remote-player virtual `+0x128` override that selects the retained ChrSync
// route from the derived backread/area-active state.
inline constexpr Rva PLAYER_INS_SELECT_SYNC_FOR_BACKREAD_STATE_FN{0x18f8d20};

// Queues a character for removal from `WorldChrMan` without directly tearing
// down Sprj session membership.
inline constexpr Rva WORLD_CHR_MAN_REMOVE_CHR_DELAYED_FN_symbols{0x19186e0};  // ALL_SYMBOLS name WORLD_CHR_MAN_REMOVE_CHR_DELAYED_FN (also in sprj/world_chr_man.hpp)

// Map-load step that reuses `WorldChrMan+0x60` when it is non-null. The reuse
// path does not clear the retained player's collision binding at `+0x288`.
inline constexpr Rva WORLD_LOAD_STEP_REBUILD_LOCAL_PLAYER_FN{0x1939470};

// Map-load step that dispatches the retained-character transition snapshot.
inline constexpr Rva WORLD_LOAD_CONSUME_TRANSITION_SNAPSHOT_STEP_FN{0x1939970};

// Large map-load coordinator containing the role-specific leave branch.
inline constexpr Rva WORLD_LOAD_COORDINATOR_UPDATE_FN{0x193ac10};

// Detaches character collision pointers during the native hard-transition path.
inline constexpr Rva WORLD_RES_PREPARE_PLAYER_MAP_TRANSITION_FN{0x154b110};

// Load-complete collision readiness check that invokes the main-player rebind.
inline constexpr Rva WORLD_RES_CHECK_PLAYER_COLLISION_READY_FN{0x154c4d0};

// Clears tracked transition presentation entities. It does not detach the
// main player's `MapCollisionEntry` pointer.
inline constexpr Rva WORLD_TRANSITION_RESET_TRACKED_ENTITIES_FN_symbols{0x15bed10};  // ALL_SYMBOLS name WORLD_TRANSITION_RESET_TRACKED_ENTITIES_FN (also in sprj/network_script_state.hpp)

// Destroys and recreates the dynamic `0x138`-byte entries for one group.
inline constexpr Rva MAP_COLLISION_GROUP_REBUILD_ENTRIES_FN{0x18865c0};

// Indexed wrapper over the manager's `0xb8`-stride collision-group table.
inline constexpr Rva MAP_COLLISION_MANAGER_REBUILD_GROUP_BY_INDEX_FN{0x188a030};
inline constexpr Rva MAP_COLLISION_ENTRY_CONSTRUCTOR_FN{0x1882710};
inline constexpr Rva MAP_COLLISION_ENTRY_SOURCE_CONSTRUCTOR_FN{0x187f020};
inline constexpr Rva MAP_COLLISION_GROUP_FIND_ENTRIES_BY_SOURCE_ID_FN{0x18877a0};
inline constexpr Rva MAP_COLLISION_MANAGER_CONSTRUCTOR_FN{0x1889520};

// Action/physics consumer that dereferences the current collision entry.
inline constexpr Rva WORLD_CHR_ACTION_UPDATE_FN{0x18cb0e0};

// Requests the three file groups owned by one `0x420`-byte block resource.
inline constexpr Rva WORLD_BLOCK_RES_REQUEST_FILES_FN_symbols{0x15614e0};  // ALL_SYMBOLS name WORLD_BLOCK_RES_REQUEST_FILES_FN (also in sprj/world_info.hpp)

// Advances one block resource's internal load/unload state machine. The
// collision-group rebuild occurs in state 6 without clearing `ChrIns` raw
// collision pointers.
inline constexpr Rva WORLD_BLOCK_RES_ADVANCE_LOAD_STATE_FN_symbols{0x155aae0};  // ALL_SYMBOLS name WORLD_BLOCK_RES_ADVANCE_LOAD_STATE_FN (also in sprj/world_info.hpp)

// Computes one block resource's desired residency and then advances it.
inline constexpr Rva WORLD_BLOCK_RES_UPDATE_FN_symbols{0x155a170};  // ALL_SYMBOLS name WORLD_BLOCK_RES_UPDATE_FN (also in sprj/world_info.hpp)

// Updates all active area/block resource records in `WorldRes`.
inline constexpr Rva WORLD_RES_UPDATE_FN_symbols{0x1565450};  // ALL_SYMBOLS name WORLD_RES_UPDATE_FN (also in sprj/world_info.hpp)

// Requests files for every currently defined block resource.
inline constexpr Rva WORLD_RES_REQUEST_ALL_BLOCK_FILES_FN_symbols{0x1565f80};  // ALL_SYMBOLS name WORLD_RES_REQUEST_ALL_BLOCK_FILES_FN (also in sprj/world_info.hpp)

// Constructs the asynchronous `0xe0`-byte current-map load context.
inline constexpr Rva WORLD_RES_LOAD_CONTEXT_CONSTRUCTOR_FN_symbols{0x1547650};  // ALL_SYMBOLS name WORLD_RES_LOAD_CONTEXT_CONSTRUCTOR_FN (also in sprj/world_info.hpp)

// Global pointer to the current asynchronous map-load context.
inline constexpr Rva WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR_symbols{0x553b148};  // ALL_SYMBOLS name WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR (also in sprj/world_info.hpp)

// Constructs the `0xfd0`-byte distance-based character backread scheduler.
inline constexpr Rva WORLD_BACK_READ_CONSTRUCTOR_FN{0x1554f00};

// Updates the active backread scheduler from player position, navmesh and
// collision candidates.
inline constexpr Rva WORLD_BACK_READ_UPDATE_FN{0x15556b0};

// Considers one character/collision candidate for a backread profile.
inline constexpr Rva WORLD_BACK_READ_CONSIDER_CANDIDATE_FN{0x1555c00};

// Applies one profile's distance and delay state machine.
inline constexpr Rva WORLD_BACK_READ_UPDATE_PROFILE_FN{0x1556040};

// Converts distributed NaviGroup/BackGroup masks into each
// `MapCollisionEntry+0x11a` desired streaming state.
inline constexpr Rva MAP_COLLISION_GROUP_UPDATE_BACKREAD_STATE_FN{0x1886b50};

// Consumes `MapCollisionEntry+0x11a` and advances the entry's collision-file,
// WorldHit, and renderer-facing streaming state machines.
inline constexpr Rva MAP_COLLISION_ENTRY_UPDATE_STREAMING_STATE_FN{0x1882980};

// Loads the collision resource selected by one active map-collision entry and
// constructs its WorldHit collision-resource wrapper.
inline constexpr Rva MAP_COLLISION_ENTRY_LOAD_COLLISION_RESOURCE_FN{0x1884c10};

// Applies the renderer-facing state associated with an updated collision entry.
inline constexpr Rva MAP_COLLISION_ENTRY_COMMIT_RENDER_STATE_FN{0x18832e0};

// Constructs the WorldHit wrapper and its indexed collision/navmesh records.
inline constexpr Rva WORLD_HIT_COLLISION_RESOURCE_CONSTRUCTOR_FN{0x1801980};

// Activates every indexed record in a WorldHit collision resource and queues
// the resource for native add/remove processing.
inline constexpr Rva WORLD_HIT_COLLISION_RESOURCE_ACTIVATE_FN{0x1802780};

// Deactivates every indexed record in a WorldHit collision resource and queues
// the resource for native add/remove processing.
inline constexpr Rva WORLD_HIT_COLLISION_RESOURCE_DEACTIVATE_FN{0x1802860};

// Thread-safe dirty-resource queue used by WorldHit collision activation.
inline constexpr Rva WORLD_HIT_COLLISION_MANAGER_QUEUE_DIRTY_RESOURCE_FN{0x1806cb0};

// Compares requested record state with WorldHit state and emits native
// collision/navmesh add and remove lists.
inline constexpr Rva WORLD_HIT_COLLISION_RESOURCE_COLLECT_ACTIVATION_DELTAS_FN{0x18021f0};

// SprjHkAiManager update that consumes changed navmesh/collision records and
// invokes Havok's dynamic-navmesh modification pipeline.
inline constexpr Rva SPRJ_HK_AI_MANAGER_PROCESS_DYNAMIC_NAVMESH_CHANGES_FN{0x1dcdad0};

// Havok AI clearance-cache allocation reached after streamed navmesh changes.
inline constexpr Rva HKAI_NAVMESH_INSTANCE_INIT_CLEARANCE_CACHE_FN{0x08b7550};

// Resets/rebuilds affected Havok AI clearance caches after navmesh cutting.
inline constexpr Rva HKAI_DYNAMIC_NAVMESH_RESET_CLEARANCE_CACHES_FN{0x08c4110};
inline constexpr Rva MOVE_MAP_CONTROLLER_REQUEST_MOVE_FN{0x1928680};
inline constexpr Rva MOVE_MAP_CONTROLLER_PROCESS_COMPLETED_REQUEST_FN{0x19297e0};

// `WorldChrMan` update routine that owns refresh and sync dispatch.
inline constexpr Rva WORLD_CHR_MAN_UPDATE_FN{0x1915320};

// Root object whose `+0x2830` field owns the active `ChrCam` object.
inline constexpr Rva CHR_CAM_OWNER_ROOT_PTR{0x553e860};
inline constexpr Rva SPRJ_EVENT_STATE_SINGLETON_PTR{0x553b0d8};

// Global storage for the native 0xf8-byte event-debug singleton.
inline constexpr Rva SPRJ_DBG_EVENT_SINGLETON_PTR{0x553b0d0};

// Global storage for the native 0x10-byte tendency singleton.
inline constexpr Rva SPRJ_TENDENCY_MAN_SINGLETON_PTR{0x553b0e0};
inline constexpr Rva SPRJ_EVENT_ACT_MAN_SINGLETON_PTR{0x553b0f8};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SINGLETON_PTR{0x553b100};
inline constexpr Rva SPRJ_EVENT_MAN_SINGLETON_PTR{0x553b108};
inline constexpr Rva SPRJ_EVENT_REGION_MAN_SINGLETON_PTR{0x553b110};

// Global storage for the non-polymorphic 0x78-byte EMEVD runtime owner.
inline constexpr Rva SPRJ_EMK_SYSTEM_SINGLETON_PTR{0x553b0c0};
inline constexpr Rva SPRJ_LUA_EVENT_MAN_SINGLETON_PTR{0x553b0c8};
inline constexpr Rva SPRJ_TARGET_BANK_MANAGER_SINGLETON_PTR{0x553b098};
inline constexpr Rva SPRJ_WORLD_AI_MANAGER_SINGLETON_PTR{0x553b0a0};
inline constexpr Rva GAME_DATA_MAN_SINGLETON_PTR{0x553b130};
inline constexpr Rva MAP_INS_MAN_SINGLETON_PTR{0x553b158};
inline constexpr Rva OBJ_INS_MAN_SINGLETON_PTR{0x553b168};
inline constexpr Rva WORLD_SESSION_OBJECT_MAN_SINGLETON_PTR{0x553d6d0};
inline constexpr Rva MAP_COLLISION_MANAGER_SINGLETON_PTR{0x553e850};

// Compatibility name retained for the earlier partial map-collision model.
inline constexpr Rva COLLISION_MAP_MAN_SINGLETON_PTR = MAP_COLLISION_MANAGER_SINGLETON_PTR;
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_SINGLETON_PTR{0x553e8c8};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_CONSTRUCTOR_FN{0x19ce610};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_DESTRUCTOR_FN{0x19ce8b0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_DELETING_DESTRUCTOR_FN{0x19ce7e0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_RESET_FN{0x19ce980};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_SET_STATE_FN{0x19cebe0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_GET_ENABLED_FN{0x19cebf0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_GET_MODE_FN{0x19cec00};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_TRACK_FILE_FN{0x19cec10};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_TRACK_LOADED_FILE_FN{0x19cecb0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_TRACK_MOBANK_FN{0x19ced50};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_CAPTURE_RESOURCES_FN{0x19ceec0};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_RECONCILE_RESOURCES_FN{0x19cef70};
inline constexpr Rva SPRJ_RAPID_REENTRY_HELPER_PUBLISH_ENTRYFILELIST_FN{0x19cf3b0};
inline constexpr Rva SPRJ_SESSION_MANAGER_SINGLETON_PTR{0x5540290};
inline constexpr Rva WORLD_TRANSITION_STATE_PTR{0x5556678};
inline constexpr Rva TUTORIAL_SOS_STATE_PTR{0x5562878};

// Refresh/check helper for `SprjSessionManager + 0x38` join-room result.
//
// Confirmed callers pass `RVA 0x5540290 + 0x18` as the owner object and
// `RVA 0x5540290 + 0x38` as the join-room result buffer.
inline constexpr Rva SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_REFRESH_FN{0x0c8f500};
inline constexpr Rva SPRJ_SESSION_MANAGER_SOURCE_ROUTE_REFRESH_FN = SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_REFRESH_FN;
inline constexpr Rva SPRJ_BULLET_MANAGER_SINGLETON_PTR{0x553e8d0};
inline constexpr Rva SPRJ_CAMERA_SINGLETON_PTR{0x553e8f8};
inline constexpr Rva CHR_EX_FOLLOW_CAM_UPDATE_FN{0x143ac60};
inline constexpr Rva SPRJ_ACTION_BUTTON_MAN_SINGLETON_PTR{0x5540048};
inline constexpr Rva CS_MULTI_PLAY_MAN_SINGLETON_PTR{0x5540230};
inline constexpr Rva CS_PLATFORM_NETWORK_MAN_SINGLETON_PTR{0x5540238};
inline constexpr Rva CSDLC_SINGLETON_PTR{0x55403d0};
inline constexpr Rva CS_PLAYGO_SINGLETON_PTR{0x55404a0};
inline constexpr Rva CS_TROPHY_SINGLETON_PTR{0x55404e8};
inline constexpr Rva CS_NOW_LOADING_HELPER_SINGLETON_PTR{0x553e8b8};

// Global storage for the native CSLod manager; allocated by the render owner.
inline constexpr Rva CS_LOD_SINGLETON_PTR{0x5540080};

// Global storage for CSEzWorkPool, which owns four shared work executors.
inline constexpr Rva CS_EZ_WORK_POOL_SINGLETON_PTR{0x55404f0};

// Global storage for CSGparamBankImp, asserted at use sites as CSGparamBank.
inline constexpr Rva CS_GPARAM_BANK_SINGLETON_PTR{0x5540098};

// CSEntryfilelistRepositoryImp singleton, asserted as CSEntryfilelistRepository.
inline constexpr Rva CS_ENTRYFILELIST_REPOSITORY_SINGLETON_PTR{0x553e8c0};
inline constexpr Rva CS_NOW_LOADING_HELPER_MENU_LOAD_TABLE{0x5578170};
inline constexpr Rva FD4_DEBUG_MENU_MANAGER_SINGLETON_PTR{0x54b3570};
inline constexpr Rva FD4_DEBUG_MENU_REPORT_SYSTEM_SINGLETON_PTR{0x54b35f0};
inline constexpr Rva FD4_DEBUG_MENU_SHARE_STRING_MANAGER_SINGLETON_PTR{0x54b38f0};
inline constexpr Rva MATCHING_ROOT_PTR{0x56c6ba8};
inline constexpr Rva MATCHING_ROOT_STORAGE{0x56c6bb8};
inline constexpr Rva NEXUS_REVOLUTION_MAIN_MATCHING_MANAGER_INTERFACE_VTABLE{0x52c5a80};
inline constexpr Rva NEXUS_REVOLUTION_MATCHING_MANAGER_INTERFACE_BASE_VTABLE{0x52c5580};
inline constexpr Rva MATCHING_LIFECYCLE_STATE_GATE{0x546cbbc};
inline constexpr Rva MATCHING_CONTAINER_LOCK_STATE{0x546cbf0};
inline constexpr Rva MATCHING_CONTAINER_LOCK_FLAG{0x546cc09};
inline constexpr MatchingFunctionSymbol MATCHING_FUNCTIONS[] = {
    {"Matching2EventHandler", Rva{0x0cc3210}},
    {"PrepareandCreatejoinroom", Rva{0x0cc48d0}},
    {"MatchingManager::HandleJoinStoredRoomResult", Rva{0x0cc4fa0}},
    {"MatchingManager::JoinRoomUsingStoredRoomId", Rva{0x0cc5000}},
    {"MatchingSession::GetWorldInfoList", Rva{0x0cc7700}},
    {"JoinRoomWithContextLock", Rva{0x0cc7850}},
    {"VTable_BeginSearchRoomRequest", Rva{0x0cbf900}},
};

inline constexpr MatchingStringSymbol MATCHING_STRING_ANCHORS[] = {
    {"NexusRevolution2.Matching.MemberSyncCheck", Rva{0x4873458}},
    {"NexusRevolution2.Matching.MemberSyncJoinedOnly", Rva{0x4873aec}},
    {"NexusRevolution2.PS4.Matching2.ThreadAffinityMask", Rva{0x4874c2c}},
    {"SCE_NP_MATCHING2_REQUEST_EVENT anchors", Rva{0x487599e}},
    {"SCE_NP_MATCHING2_ROOM_EVENT anchors", Rva{0x487648c}},
};

inline constexpr RuntimeClassSymbol CHR_INS_RUNTIME_CLASS = {"ChrIns", Rva{0x556fb38}, Rva{0x556fb40}, Rva{0x18d6d30}, nullptr};
inline constexpr RuntimeClassSymbol PLAYER_INS_RUNTIME_CLASS = {"PlayerIns", Rva{0x5571538}, Rva{0x5571540}, Rva{0x19074b0}, "ChrIns"};
inline constexpr RuntimeClassSymbol REPLAY_GHOST_INS_RUNTIME_CLASS = {"ReplayGhostIns", Rva{0x5571600}, Rva{0x5571608}, Rva{0x19089b0}, "PlayerIns"};
inline constexpr RuntimeClassSymbol SOLO_PARAM_REPOSITORY_IMP_RUNTIME_CLASS = {"SoloParamRepositoryImp", Rva{0x55939d0}, Rva{0x55939d8}, Rva{0x1f28370}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_CHR_DATA_MODULE_RUNTIME_CLASS = {"SprjChrDataModule", Rva{0x557c4c8}, Rva{0x557c4d0}, Rva{0x1a585a0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_MODULE_BASE_RUNTIME_CLASS = {"SprjChrModuleBase", Rva{0x557c648}, Rva{0x557c650}, Rva{0x1a5a4a0}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_CHR_ACTION_FLAG_MODULE_RUNTIME_CLASS = {"SprjChrActionFlagModule", Rva{0x557a0f8}, Rva{0x557a100}, Rva{0x1a0ed60}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_ACTION_REQUEST_MODULE_RUNTIME_CLASS = {"SprjChrActionRequestModule", Rva{0x557a1b8}, Rva{0x557a1c0}, Rva{0x1a10540}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_AI_MODULE_RUNTIME_CLASS = {"SprjChrAiModule", Rva{0x557c408}, Rva{0x557c410}, Rva{0x1a55730}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_BEHAVIOR_SCRIPT_MODULE_RUNTIME_CLASS = {"SprjChrBehaviorScriptModule", Rva{0x557a3f8}, Rva{0x557a400}, Rva{0x1a1d550}, "SprjChrModuleBase"};

// The common owner/vtable header is constructor-proven; this registration
// does not explicitly populate a reflected parent link.
inline constexpr RuntimeClassSymbol SPRJ_CHR_BEHAVIOR_SYNC_MODULE_RUNTIME_CLASS = {"SprjChrBehaviorSyncModule", Rva{0x557a4b8}, Rva{0x557a4c0}, Rva{0x1a1fea0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_RESIST_MODULE_RUNTIME_CLASS = {"SprjChrResistModule", Rva{0x557c7b0}, Rva{0x557c7b8}, Rva{0x1a5cc10}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_TALK_MODULE_RUNTIME_CLASS = {"SprjChrTalkModule", Rva{0x557c928}, Rva{0x557c930}, Rva{0x1a5e890}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_BEHAVIOR_MODULE_RUNTIME_CLASS = {"SprjChrBehaviorModule", Rva{0x557a338}, Rva{0x557a340}, Rva{0x1a1c690}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_EVENT_MODULE_RUNTIME_CLASS = {"SprjChrEventModule", Rva{0x557c590}, Rva{0x557c598}, Rva{0x1a597b0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_FALL_MODULE_RUNTIME_CLASS = {"SprjChrFallModule", Rva{0x557ad48}, Rva{0x557ad50}, Rva{0x1a36470}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_LADDER_MODULE_RUNTIME_CLASS = {"SprjChrLadderModule", Rva{0x557b508}, Rva{0x557b510}, Rva{0x1a3df80}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_MATERIAL_MODULE_RUNTIME_CLASS = {"SprjChrMaterialModule", Rva{0x557ba28}, Rva{0x557ba30}, Rva{0x1a43f20}, "SprjChrModuleBase"};

// Probable base follows instance layout; registration omits its parent link.
inline constexpr RuntimeClassSymbol SPRJ_CHR_MAGIC_MODULE_RUNTIME_CLASS = {"SprjChrMagicModule", Rva{0x557b748}, Rva{0x557b750}, Rva{0x1a40db0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_MAGIC_MODULE_RUNTIME_CLASS = {"SprjEnemyMagicModule", Rva{0x557b808}, Rva{0x557b810}, Rva{0x1a41540}, "SprjChrMagicModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_MAGIC_MODULE_RUNTIME_CLASS = {"SprjPlayerMagicModule", Rva{0x557b968}, Rva{0x557b970}, Rva{0x1a42c40}, "SprjChrMagicModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_HIT_STOP_MODULE_RUNTIME_CLASS = {"SprjChrHitStopModule", Rva{0x557af88}, Rva{0x557af90}, Rva{0x1a388d0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_HIT_STOP_MODULE_RUNTIME_CLASS = {"SprjEnemyHitStopModule", Rva{0x557b048}, Rva{0x557b050}, Rva{0x1a39080}, "SprjChrHitStopModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_HIT_STOP_MODULE_RUNTIME_CLASS = {"SprjPlayerHitStopModule", Rva{0x557b108}, Rva{0x557b110}, Rva{0x1a39bb0}, "SprjChrHitStopModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_KNOCK_BACK_MODULE_RUNTIME_CLASS = {"SprjChrKnockBackModule", Rva{0x557b2d0}, Rva{0x557b2d8}, Rva{0x1a3b130}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_KNOCK_BACK_MODULE_RUNTIME_CLASS = {"SprjEnemyKnockBackModule", Rva{0x557b388}, Rva{0x557b390}, Rva{0x1a3b9d0}, "SprjChrKnockBackModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_KNOCK_BACK_MODULE_RUNTIME_CLASS = {"SprjPlayerKnockBackModule", Rva{0x557b448}, Rva{0x557b450}, Rva{0x1a3c920}, "SprjChrKnockBackModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_DAMAGE_MODULE_RUNTIME_CLASS = {"SprjChrDamageModule", Rva{0x557a858}, Rva{0x557a860}, Rva{0x1a30ed0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_DAMAGE_MODULE_RUNTIME_CLASS = {"SprjEnemyDamageModule", Rva{0x557a918}, Rva{0x557a920}, Rva{0x1a31c60}, "SprjChrDamageModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_DAMAGE_MODULE_RUNTIME_CLASS = {"SprjPlayerDamageModule", Rva{0x557ac90}, Rva{0x557ac98}, Rva{0x1a351c0}, "SprjChrDamageModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_SFX_MODULE_RUNTIME_CLASS = {"SprjChrSfxModule", Rva{0x557c1c8}, Rva{0x557c1d0}, Rva{0x1a52eb0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_SFX_MODULE_RUNTIME_CLASS = {"SprjEnemySfxModule", Rva{0x557c288}, Rva{0x557c290}, Rva{0x1a53690}, "SprjChrSfxModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_SFX_MODULE_RUNTIME_CLASS = {"SprjPlayerSfxModule", Rva{0x557c348}, Rva{0x557c350}, Rva{0x1a541b0}, "SprjChrSfxModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_PHYSICS_MODULE_RUNTIME_CLASS = {"SprjChrPhysicsModule", Rva{0x557c110}, Rva{0x557c118}, Rva{0x1a4fba0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_FALL_MODULE_RUNTIME_CLASS = {"SprjEnemyFallModule", Rva{0x557ae08}, Rva{0x557ae10}, Rva{0x1a36ca0}, "SprjChrFallModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_FALL_MODULE_RUNTIME_CLASS = {"SprjPlayerFallModule", Rva{0x557aec8}, Rva{0x557aed0}, Rva{0x1a37880}, "SprjChrFallModule"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_LADDER_MODULE_RUNTIME_CLASS = {"SprjEnemyLadderModule", Rva{0x557b5c8}, Rva{0x557b5d0}, Rva{0x1a3e9f0}, "SprjChrLadderModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_LADDER_MODULE_RUNTIME_CLASS = {"SprjPlayerLadderModule", Rva{0x557b688}, Rva{0x557b690}, Rva{0x1a3f510}, "SprjChrLadderModule"};
inline constexpr RuntimeClassSymbol SPRJ_ENEMY_MATERIAL_MODULE_RUNTIME_CLASS = {"SprjEnemyMaterialModule", Rva{0x557bae8}, Rva{0x557baf0}, Rva{0x1a44750}, "SprjChrMaterialModule"};
inline constexpr RuntimeClassSymbol SPRJ_PLAYER_MATERIAL_MODULE_RUNTIME_CLASS = {"SprjPlayerMaterialModule", Rva{0x557bba8}, Rva{0x557bbb0}, Rva{0x1a45310}, "SprjChrMaterialModule"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_SUPER_ARMOR_MODULE_RUNTIME_CLASS = {"SprjChrSuperArmorModule", Rva{0x557c868}, Rva{0x557c870}, Rva{0x1a5ddd0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_THROW_MODULE_RUNTIME_CLASS = {"SprjChrThrowModule", Rva{0x557cab0}, Rva{0x557cab8}, Rva{0x1a61ab0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_TIME_ACT_MODULE_RUNTIME_CLASS = {"SprjChrTimeActModule", Rva{0x557a5c8}, Rva{0x557a5d0}, Rva{0x1a283e0}, "SprjChrModuleBase"};
inline constexpr RuntimeClassSymbol SPRJ_BULLET_INS_RUNTIME_CLASS = {"SprjBulletIns", Rva{0x5579c28}, Rva{0x5579c30}, Rva{0x19ffa70}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_CAMERA_STEP_RUNTIME_CLASS = {"SprjCameraStep", Rva{0x5579e60}, Rva{0x5579e68}, Rva{0x1a09ee0}, "SprjStepTask< SprjCameraStep >"};
inline constexpr RuntimeClassSymbol SPRJ_DEBUG_CAM_RUNTIME_CLASS = {"SprjDebugCam", Rva{0x5586760}, Rva{0x5586768}, Rva{0x1d158e0}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_FD4_LOCATION_STEP_RUNTIME_CLASS = {"SprjFD4LocationStep", Rva{0x5587af0}, Rva{0x5587af8}, Rva{0x1d3edc0}, "SprjStepTask< SprjFD4LocationStep >"};

// Target accessor family registered together at RVA `0x12a0960`.
inline constexpr RuntimeClassSymbol SPRJ_TARGET_ACCESSOR_BASE_RUNTIME_CLASS = {"SprjTargetAccessorBase", Rva{0x55444b8}, Rva{0x55444c0}, Rva{0x12a0960}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_NULL_TARGET_ACCESSOR_RUNTIME_CLASS = {"SprjNullTargetAccessor", Rva{0x5544448}, Rva{0x5544450}, Rva{0x12a0960}, "SprjTargetAccessorBase"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_RUNTIME_CLASS = {"SprjChrInsTargetAccessorBase", Rva{0x55443d8}, Rva{0x55443e0}, Rva{0x12a0960}, "SprjNullTargetAccessor"};
inline constexpr RuntimeClassSymbol SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_RUNTIME_CLASS = {"SprjChrInsHandleTargetAccessor", Rva{0x5544368}, Rva{0x5544370}, Rva{0x12a0960}, "SprjChrInsTargetAccessorBase"};
inline constexpr RuntimeClassSymbol SPRJ_SOUND_TARGET_RUNTIME_CLASS = {"SprjSoundTarget", Rva{0x55442f8}, Rva{0x5544300}, Rva{0x12a0960}, "SprjNullTargetAccessor"};
inline constexpr Rva SPRJ_TARGET_ACCESSOR_BASE_SIZE_FN{0x12a3bd0};
inline constexpr Rva SPRJ_NULL_TARGET_ACCESSOR_SIZE_FN{0x12a38b0};
inline constexpr Rva SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_SIZE_FN{0x12a3590};
inline constexpr Rva SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_SIZE_FN{0x12a3270};
inline constexpr Rva SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_VTABLE{0x5365d40};
inline constexpr Rva SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_CONSTRUCTOR_FN{0x12a0090};
inline constexpr Rva SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_BIND_FN{0x12a00b0};
inline constexpr Rva SPRJ_SOUND_TARGET_VTABLE{0x531b6d0};
inline constexpr Rva SPRJ_SOUND_TARGET_CONSTRUCTOR_FN{0x12a0500};
inline constexpr Rva SPRJ_SOUND_TARGET_RESET_FN{0x12a0530};
inline constexpr Rva SPRJ_SOUND_TARGET_COMPARE_POSITION_FN{0x12a0570};
inline constexpr Rva SPRJ_SOUND_TARGET_IS_NEAR_POINT_FN{0x12a05d0};
inline constexpr Rva SPRJ_SOUND_TARGET_SIZE_FN{0x12a2f50};
inline constexpr RuntimeClassSymbol SPRJ_FIXED_POS_TARGET_RUNTIME_CLASS = {"SprjFixedPosTarget", Rva{0x5544288}, Rva{0x5544290}, Rva{0x12a0960}, "SprjNullTargetAccessor"};
inline constexpr Rva SPRJ_FIXED_POS_TARGET_VTABLE{0x531b7f0};

// Virtual slot +0x80 returns the aligned position vector at object +0x10.
inline constexpr Rva SPRJ_FIXED_POS_TARGET_GET_POSITION_FN{0x12a28a0};
inline constexpr Rva SPRJ_FIXED_POS_TARGET_GET_SCALAR_FN{0x12a28b0};
inline constexpr Rva SPRJ_FIXED_POS_TARGET_RESET_FN{0x12a28c0};
inline constexpr Rva SPRJ_FIXED_POS_TARGET_SIZE_FN{0x12a2c30};
inline constexpr Rva CS_HIT_FLOOR_INFO_VTABLE{0x5335630};
inline constexpr Rva CS_HIT_FLOOR_INFO_CONSTRUCTOR_FN{0x1a45c90};
inline constexpr Rva CS_HIT_FLOOR_INFO_SET_STATE_FN{0x1a45d30};
inline constexpr Rva CS_HIT_FLOOR_INFO_ALLOWS_SLOPE_FN{0x1a45d60};
inline constexpr Rva CS_HIT_FLOOR_INFO_RESOLVE_COLLISION_FN{0x1a45e60};
inline constexpr Rva CS_HIT_FLOOR_INFO_CLEAR_COLLISION_FN{0x1a46080};
inline constexpr Rva CS_HIT_FLOOR_INFO_SIZE_FN{0x1a467d0};
inline constexpr Rva CS_SLOPE_CTRL_VTABLE{0x5335650};
inline constexpr Rva CS_SLOPE_CTRL_CONSTRUCTOR_FN{0x1a469c0};
inline constexpr Rva CS_SLOPE_CTRL_UPDATE_ANGLE_FN{0x1a46a20};
inline constexpr Rva CS_SLOPE_CTRL_UPDATE_RESPONSE_FN{0x1a46bc0};
inline constexpr Rva CS_SLOPE_CTRL_CLEAR_RESPONSE_FN{0x1a46ee0};
inline constexpr Rva CS_SLOPE_CTRL_SIZE_FN{0x1a47b00};
inline constexpr Rva SPRJ_CHR_PHYSICS_MODULE_VTABLE{0x53356f0};
inline constexpr Rva SPRJ_CHR_PHYSICS_MODULE_CONSTRUCTOR_FN{0x1a48fe0};
inline constexpr Rva SPRJ_CHR_PHYSICS_MODULE_SIZE_FN{0x1a50410};
inline constexpr RuntimeClassSymbol CS_DISPLAY_GHOST_RUNTIME_CLASS = {"CSDisplayGhost", Rva{0x557d128}, Rva{0x557d130}, Rva{0x1a68a30}, "SprjStepLocal< CSDisplayGhost >"};
inline constexpr RuntimeClassSymbol CS_SESSION_CONNECT_STATE_STEP_RUNTIME_CLASS = {"CSSessionConnectStateStep", Rva{0x5590a38}, Rva{0x5590a40}, Rva{0x1ed2e80}, "SprjStepLocal< CSSessionConnectStateStep >"};
inline constexpr RuntimeClassSymbol CS_NETWORK_FLOW_STEP_RUNTIME_CLASS = {"CSNetworkFlowStep", Rva{0x558f2a0}, Rva{0x558f2a8}, Rva{0x1e579d0}, "SprjStepLocal< CSNetworkFlowStep >"};
inline constexpr RuntimeClassSymbol CS_REQUEST_GET_SOS_STEP_RUNTIME_CLASS = {"CSRequestGetSosStep", Rva{0x558f7f0}, Rva{0x558f7f8}, Rva{0x1e60580}, "SprjStepLocal< CSRequestGetSosStep >"};
inline constexpr RuntimeClassSymbol CS_REQUEST_SUMMON_STEP_RUNTIME_CLASS = {"CSRequestSummonStep", Rva{0x5551e78}, Rva{0x5551e80}, Rva{0x14bb770}, "SprjStepLocal< CSRequestSummonStep >"};
inline constexpr RuntimeClassSymbol CS_ENTERLEAVE_DIRECTION_STEP_RUNTIME_CLASS = {"CSEnterleaveDirectionStep", Rva{0x5574af0}, Rva{0x5574af8}, Rva{0x196c760}, "SprjStepTask< CSEnterleaveDirectionStep >"};
inline constexpr RuntimeClassSymbol ENTERLEAVE_DIRECTOR_IMP_RUNTIME_CLASS = {"EnterleaveDirectorImp", Rva{0x5574d58}, Rva{0x5574d60}, Rva{0x19709f0}, "SprjStepTask< EnterleaveDirectorImp >"};
inline constexpr RuntimeClassSymbol CS_CHR_THREAD_RUNTIME_CLASS = {"CSChrThread", Rva{0x557cd58}, Rva{0x557cd60}, Rva{0x1a659a0}, nullptr};
inline constexpr RuntimeClassSymbol CS_CLOTH_THREAD_RUNTIME_CLASS = {"CSClothThread", Rva{0x558e460}, Rva{0x558e468}, Rva{0x1e39940}, nullptr};
inline constexpr RuntimeClassSymbol CS_CHR_ASM_INFO_TRANSFER_RUNTIME_CLASS = {"CSChrAsmInfoTransfer", Rva{0x55832f0}, Rva{0x55832f8}, Rva{0x1c9bf60}, nullptr};
inline constexpr RuntimeClassSymbol SPRJ_ASM_MODEL_RUNTIME_CLASS = {"SprjAsmModel", Rva{0x55837a0}, Rva{0x55837a8}, Rva{0x1cb23e0}, nullptr};
inline constexpr RuntimeClassSymbol CHR_ASM_MODEL_RUNTIME_CLASS = {"ChrAsmModel", Rva{0x5583240}, Rva{0x5583248}, Rva{0x1c9b1d0}, "SprjAsmModel"};
inline constexpr RuntimeClassSymbol CS_CHR_CREATE_BEH_CHARA_FRAGMENT_RUNTIME_CLASS = {"CSChrCreateBehCharaFragment", Rva{0x557cc98}, Rva{0x557cca0}, Rva{0x1a64330}, nullptr};
inline constexpr RuntimeClassSymbol CS_CHR_UPDATE_POST_FRAGMENT_RUNTIME_CLASS = {"CSChrUpdatePostFragment", Rva{0x557ce58}, Rva{0x557ce60}, Rva{0x1a665c0}, nullptr};
inline constexpr RuntimeClassSymbol CS_CHR_UPDATE_PRE_FRAGMENT_RUNTIME_CLASS = {"CSChrUpdatePreFragment", Rva{0x557cf18}, Rva{0x557cf20}, Rva{0x1a66cd0}, nullptr};
inline constexpr RuntimeClassSymbol CS_MULTI_NPC_PLAYER_INS_TASK_RUNTIME_CLASS = {"CSMultiNPCPlayerInsTask", Rva{0x558ec18}, Rva{0x558ec20}, Rva{0x1e4bd70}, "SprjStepLocal< CSMultiNPCPlayerInsTask >"};
inline constexpr RuntimeClassSymbol CS_MULTI_PLAYER_INS_TASK_RUNTIME_CLASS = {"CSMultiPlayerInsTask", Rva{0x558ef50}, Rva{0x558ef58}, Rva{0x1e50f00}, "SprjStepLocal< CSMultiPlayerInsTask >"};
inline constexpr RuntimeClassSymbol CS_PS4_TROPHY_STEP_RUNTIME_CLASS = {"CSPS4TrophyStep", Rva{0x569fef8}, Rva{0x569ff00}, Rva{0x2020830}, "SprjStepLocal< CSPS4TrophyStep >"};
inline constexpr RuntimeClassSymbol CS_PS_PLUS_CHECK_STEP_RUNTIME_CLASS = {"CSPSPlusCheckStep", Rva{0x55903a0}, Rva{0x55903a8}, Rva{0x1e789b0}, "SprjStepLocal< CSPSPlusCheckStep >"};
inline constexpr RuntimeClassSymbol CS_PREFETCH_FOR_SLOW_STORAGE_STEP_RUNTIME_CLASS = {"CSPrefetchForSlowStorageStep", Rva{0x569e638}, Rva{0x569e640}, Rva{0x1fdffd0}, "SprjStepLocal< CSPrefetchForSlowStorageStep >"};
inline constexpr RuntimeClassSymbol CS_LAMS_CAPTURE_STEP_RUNTIME_CLASS = {"CSLamsCaptureStep", Rva{0x55987a0}, Rva{0x55987a8}, Rva{0x1fa6a30}, "SprjStepTask< CSLamsCaptureStep >"};
inline constexpr RuntimeClassSymbol CS_MOVIE_INS_RUNTIME_CLASS = {"CSMovieIns", Rva{0x569dd28}, Rva{0x569dd30}, Rva{0x1fce990}, "SprjStepLocal< CSMovieIns >"};
inline constexpr RuntimeClassSymbol CS_COLLECT_NEAR_NAVI_MESH_PARTS_RUNTIME_CLASS = {"CSCollectNearNaviMeshParts", Rva{0x558dc20}, Rva{0x558dc28}, Rva{0x1e1a5b0}, "SprjStepLocal< CSCollectNearNaviMeshParts >"};
inline constexpr RuntimeClassSymbol CS_DUNGEON_RITUAL_HELPER_RUNTIME_CLASS = {"CSDungeonRitualHelper", Rva{0x557e548}, Rva{0x557e550}, Rva{0x1aaa8c0}, "SprjStepLocal< CSDungeonRitualHelper >"};
inline constexpr RuntimeClassSymbol SPRJ_DUNGEON_GATE_INS_RUNTIME_CLASS = {"SprjDungeonGateIns", Rva{0x557ea98}, Rva{0x557eaa0}, Rva{0x1abf510}, "SprjStepLocal< SprjDungeonGateIns >"};
inline constexpr RuntimeClassSymbol SPRJ_MOVE_MAP_LIST_STEP_RUNTIME_CLASS = {"SprjMoveMapListStep", Rva{0x5577c90}, Rva{0x5577c98}, Rva{0x19c2110}, "SprjStepTask< SprjMoveMapListStep >"};
inline constexpr RuntimeClassSymbol SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_RUNTIME_CLASS = {"SprjDungeonMoveMapListStep", Rva{0x5574370}, Rva{0x5574378}, Rva{0x1963530}, "SprjStepTask< SprjDungeonMoveMapListStep >"};
inline constexpr RuntimeClassSymbol CS_MENU_ASM_MODEL_REND_RUNTIME_CLASS = {"CSMenuAsmModelRend", Rva{0x558b378}, Rva{0x558b380}, Rva{0x1dac860}, "SprjStepLocal< CSMenuAsmModelRend >"};
inline constexpr RuntimeClassSymbol CS_LAMS_CAPTURE_THREAD_RUNTIME_CLASS = {"CSLamsCaptureThread", Rva{0x55989e0}, Rva{0x55989e8}, Rva{0x1faad00}, nullptr};
inline constexpr RuntimeClassSymbol CS_HK_BEH_CHARA_CREATER_RUNTIME_CLASS = {"CSHkBehCharaCreater", Rva{0x558ca88}, Rva{0x558ca90}, Rva{0x1dde9d0}, nullptr};
inline constexpr RuntimeClassSymbol CS_GPARAM_BANK_IMP_RUNTIME_CLASS = {"CSGparamBankImp", Rva{0x5588460}, Rva{0x5588468}, Rva{0x1d4eb40}, nullptr};
inline constexpr RuntimeClassSymbol CS_ENTRYFILELIST_FILE_CAP_RUNTIME_CLASS = {"CSEntryfilelistFileCap", Rva{0x55783d0}, Rva{0x55783d8}, Rva{0x19cc150}, "FD4FileCap"};
inline constexpr RuntimeClassSymbol CS_ENTRYFILELIST_REPOSITORY_IMP_RUNTIME_CLASS = {"CSEntryfilelistRepositoryImp", Rva{0x5578470}, Rva{0x5578478}, Rva{0x19cd100}, "FD4ResRep"};
inline constexpr RuntimeClassSymbol CS_ENTRYFILELIST_RES_CAP_RUNTIME_CLASS = {"CSEntryfilelistResCap", Rva{0x5578510}, Rva{0x5578518}, Rva{0x19cdee0}, "FD4ResCap"};
inline constexpr RuntimeClassSymbol CS_ENTRYFILELISTBND_FILE_CAP_RUNTIME_CLASS = {"CSEntryfilelistbndFileCap", Rva{0x5578330}, Rva{0x5578338}, Rva{0x19cb130}, "FD4FileCap"};
inline constexpr RuntimeClassSymbol CS_HIT_FLOOR_INFO_RUNTIME_CLASS = {"CSHitFloorInfo", Rva{0x557bc60}, Rva{0x557bc68}, Rva{0x1a46270}, nullptr};
inline constexpr RuntimeClassSymbol CS_SLOPE_CTRL_RUNTIME_CLASS = {"CSSlopeCtrl", Rva{0x557bd00}, Rva{0x557bd08}, Rva{0x1a47500}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_ORTHO_CAM_RUNTIME_CLASS = {"FrpgOrthoCam", Rva{0x554ff50}, Rva{0x554ff58}, Rva{0x14476d0}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_NET_CONNECT_MAN_STEP_RUNTIME_CLASS = {"FrpgNetConnectManStep", Rva{0x5550e70}, Rva{0x5550e78}, Rva{0x147d20d}, "Step<FrpgNetConnectManStep>"};
inline constexpr RuntimeClassSymbol FRPG_NET_CONNECT_STEP_RUNTIME_CLASS = {"FrpgNetConnectStep", Rva{0x5551118}, Rva{0x5551120}, Rva{0x1483000}, "Step<FrpgNetConnectStep>"};
inline constexpr RuntimeClassSymbol FRPG_NET_SYS_STEP_RUNTIME_CLASS = {"FrpgNetSysStep", Rva{0x5551640}, Rva{0x5551648}, Rva{0x1493730}, "Step<FrpgNetSysStep>"};
inline constexpr RuntimeClassSymbol FRPG_NET_LOBBY_STEP_RUNTIME_CLASS = {"FrpgNetLobbyStep", Rva{0x5551890}, Rva{0x5551898}, Rva{0x149900d}, "Step<FrpgNetLobbyStep>"};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_BLOOD_MSG_RUNTIME_CLASS = {"FrpgMenuDlgBloodMsg", Rva{0x5558d08}, Rva{0x5558d10}, Rva{0x15cb8d0}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_EQUIP_RUNTIME_CLASS = {"FrpgMenuDlgEquip", Rva{0x555b128}, Rva{0x555b130}, Rva{0x162bf30}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_INVENTORY_RUNTIME_CLASS = {"FrpgMenuDlgInventory", Rva{0x5560210}, Rva{0x5560218}, Rva{0x16e5800}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_REPOSITORY_RUNTIME_CLASS = {"FrpgMenuDlgRepository", Rva{0x5561778}, Rva{0x5561780}, Rva{0x1723f60}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_STATUS_RUNTIME_CLASS = {"FrpgMenuDlgStatus", Rva{0x5562108}, Rva{0x5562110}, Rva{0x174cb40}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_MENU_DLG_SYSTEM_RUNTIME_CLASS = {"FrpgMenuDlgSystem", Rva{0x5562438}, Rva{0x5562440}, Rva{0x17591f0}, nullptr};
inline constexpr RuntimeClassSymbol FRPG_PHYS_INS_RUNTIME_CLASS = {"FrpgPhysIns", Rva{0x556c0e0}, Rva{0x556c0e8}, Rva{0x18008f0}, nullptr};

// Native event-file residency owner; allocated as SprjEmkResMan.
inline constexpr Rva SPRJ_EMK_RES_MAN_SINGLETON_PTR{0x55402c8};

// Native EDF repository singleton pointer slot.
inline constexpr Rva SPRJ_EDF_REPOSITORY_SINGLETON_PTR{0x55402b0};
inline constexpr RuntimeClassSymbol SPRJ_EDF_FILE_CAP_RUNTIME_CLASS = {"SprjEdfFileCap", Rva{0x554ee10}, Rva{0x554ee18}, Rva{0x141aad0}, "FD4FileCap"};
inline constexpr RuntimeClassSymbol SPRJ_EDF_RES_CAP_RUNTIME_CLASS = {"SprjEdfResCap", Rva{0x5591680}, Rva{0x5591688}, Rva{0x1ee7ab0}, "FD4ResCap"};

// Native ELD repository singleton pointer slot.
inline constexpr Rva SPRJ_ELD_REPOSITORY_SINGLETON_PTR{0x55402b8};
inline constexpr RuntimeClassSymbol SPRJ_ELD_FILE_CAP_RUNTIME_CLASS = {"SprjEldFileCap", Rva{0x554eeb0}, Rva{0x554eeb8}, Rva{0x141bbc0}, "FD4FileCap"};
inline constexpr RuntimeClassSymbol SPRJ_ELD_RES_CAP_RUNTIME_CLASS = {"SprjEldResCap", Rva{0x55917a0}, Rva{0x55917a8}, Rva{0x1ee91e0}, "FD4ResCap"};

// Native EVD repository singleton pointer slot.
inline constexpr Rva SPRJ_EVD_REPOSITORY_SINGLETON_PTR{0x55402c0};
inline constexpr RuntimeClassSymbol SPRJ_EVD_FILE_CAP_RUNTIME_CLASS = {"SprjEvdFileCap", Rva{0x554ef50}, Rva{0x554ef58}, Rva{0x141ccb0}, "FD4FileCap"};
inline constexpr RuntimeClassSymbol SPRJ_EVD_RES_CAP_RUNTIME_CLASS = {"SprjEvdResCap", Rva{0x55918c0}, Rva{0x55918c8}, Rva{0x1eeb640}, "FD4ResCap"};

// File service singleton asserted as SprjFile, constructed as SprjFileImp.
inline constexpr Rva SPRJ_FILE_SINGLETON_PTR{0x553b118};
inline constexpr RuntimeClassSymbol SPRJ_FILE_IMP_RUNTIME_CLASS = {"SprjFileImp", Rva{0x554f1d8}, Rva{0x554f1e0}, Rva{0x1426980}, "FD4FileManagerImp"};
inline constexpr RuntimeClassSymbol SPRJ_FILE_REPOSITORY_RUNTIME_CLASS = {"SprjFileRepository", Rva{0x554f290}, Rva{0x554f298}, Rva{0x1428f40}, "FD4FileRepository"};
inline constexpr RuntimeClassSymbol SPRJ_FILE_REPOSITORY_SEED_RUNTIME_CLASS = {"SprjFileRepositorySeed", Rva{0x554f330}, Rva{0x554f338}, Rva{0x142a160}, "FD4FileRepositorySeed"};
inline constexpr RuntimeClassSymbol SPRJ_FILE_SEED_RUNTIME_CLASS = {"SprjFileSeed", Rva{0x554f3d0}, Rva{0x554f3d8}, Rva{0x142ac80}, "FD4FileManagerSeed"};
inline constexpr RuntimeClassSymbol SPRJ_FILE_STEP_RUNTIME_CLASS = {"SprjFileStep", Rva{0x554f4d0}, Rva{0x554f4d8}, Rva{0x142c460}, "SprjStepTask< SprjFileStep >"};

// Template wrapper only: its size getter reports 0xc8, while the concrete
// SprjWorldBlockNvmPrepare allocation is 0xf0. The generic parent's name
// has not been established from this registration.
inline constexpr RuntimeClassSymbol SPRJ_WORLD_BLOCK_NVM_PREPARE_THIS_RUNTIME_CLASS = {"SprjWorldBlockNvmPrepare::THIS_CLASS", Rva{0x558e0c8}, Rva{0x558e0d0}, Rva{0x1e28ba0}, nullptr};

// Returned by the concrete instance's runtime-class getter; this template
// record also reports size 0xc8, not the allocation's trailing five pointers.
inline constexpr RuntimeClassSymbol SPRJ_WORLD_BLOCK_NVM_PREPARE_STEP_BASE_RUNTIME_CLASS = {"SprjWorldBlockNvmPrepare::FD4STEPTEMPLATEBASE_CHILD_CLASS", Rva{0x558e058}, Rva{0x558e060}, Rva{0x1e28ba0}, "SprjWorldBlockNvmPrepare::THIS_CLASS"};
inline constexpr RuntimeClassSymbol SPRJ_EMK_SYSTEM_UPDATE_TASK_RUNTIME_CLASS = {"SprjEmkSystemUpdateTask", Rva{0x5545c88}, Rva{0x5545c90}, Rva{0x12f2750}, "SprjStepTask< SprjEmkSystemUpdateTask >"};
inline constexpr RuntimeClassSymbol RUNTIME_CLASSES[] = {
    SPRJ_EMK_SYSTEM_UPDATE_TASK_RUNTIME_CLASS,
    SPRJ_WORLD_BLOCK_NVM_PREPARE_THIS_RUNTIME_CLASS,
    SPRJ_WORLD_BLOCK_NVM_PREPARE_STEP_BASE_RUNTIME_CLASS,
    SPRJ_FILE_STEP_RUNTIME_CLASS,
    SPRJ_FILE_IMP_RUNTIME_CLASS,
    SPRJ_FILE_REPOSITORY_RUNTIME_CLASS,
    SPRJ_FILE_REPOSITORY_SEED_RUNTIME_CLASS,
    SPRJ_FILE_SEED_RUNTIME_CLASS,
    SPRJ_EDF_FILE_CAP_RUNTIME_CLASS,
    SPRJ_EDF_RES_CAP_RUNTIME_CLASS,
    SPRJ_ELD_FILE_CAP_RUNTIME_CLASS,
    SPRJ_ELD_RES_CAP_RUNTIME_CLASS,
    SPRJ_EVD_FILE_CAP_RUNTIME_CLASS,
    SPRJ_EVD_RES_CAP_RUNTIME_CLASS,
    SPRJ_TARGET_ACCESSOR_BASE_RUNTIME_CLASS,
    SPRJ_NULL_TARGET_ACCESSOR_RUNTIME_CLASS,
    SPRJ_FIXED_POS_TARGET_RUNTIME_CLASS,
    SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_RUNTIME_CLASS,
    SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_RUNTIME_CLASS,
    SPRJ_SOUND_TARGET_RUNTIME_CLASS,
    CHR_INS_RUNTIME_CLASS,
    PLAYER_INS_RUNTIME_CLASS,
    REPLAY_GHOST_INS_RUNTIME_CLASS,
    SOLO_PARAM_REPOSITORY_IMP_RUNTIME_CLASS,
    SPRJ_CHR_DATA_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_MODULE_BASE_RUNTIME_CLASS,
    SPRJ_CHR_ACTION_FLAG_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_ACTION_REQUEST_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_AI_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_BEHAVIOR_SCRIPT_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_BEHAVIOR_SYNC_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_RESIST_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_TALK_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_BEHAVIOR_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_EVENT_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_FALL_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_LADDER_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_MATERIAL_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_MAGIC_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_MAGIC_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_MAGIC_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_HIT_STOP_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_HIT_STOP_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_HIT_STOP_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_KNOCK_BACK_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_KNOCK_BACK_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_KNOCK_BACK_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_SFX_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_DAMAGE_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_DAMAGE_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_DAMAGE_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_SFX_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_SFX_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_FALL_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_FALL_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_LADDER_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_LADDER_MODULE_RUNTIME_CLASS,
    SPRJ_ENEMY_MATERIAL_MODULE_RUNTIME_CLASS,
    SPRJ_PLAYER_MATERIAL_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_PHYSICS_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_SUPER_ARMOR_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_THROW_MODULE_RUNTIME_CLASS,
    SPRJ_CHR_TIME_ACT_MODULE_RUNTIME_CLASS,
    SPRJ_BULLET_INS_RUNTIME_CLASS,
    SPRJ_CAMERA_STEP_RUNTIME_CLASS,
    SPRJ_DEBUG_CAM_RUNTIME_CLASS,
    SPRJ_FD4_LOCATION_STEP_RUNTIME_CLASS,
    CS_DISPLAY_GHOST_RUNTIME_CLASS,
    CS_SESSION_CONNECT_STATE_STEP_RUNTIME_CLASS,
    CS_NETWORK_FLOW_STEP_RUNTIME_CLASS,
    CS_REQUEST_GET_SOS_STEP_RUNTIME_CLASS,
    CS_REQUEST_SUMMON_STEP_RUNTIME_CLASS,
    CS_ENTERLEAVE_DIRECTION_STEP_RUNTIME_CLASS,
    ENTERLEAVE_DIRECTOR_IMP_RUNTIME_CLASS,
    CS_CHR_THREAD_RUNTIME_CLASS,
    CS_CLOTH_THREAD_RUNTIME_CLASS,
    CS_CHR_ASM_INFO_TRANSFER_RUNTIME_CLASS,
    SPRJ_ASM_MODEL_RUNTIME_CLASS,
    CHR_ASM_MODEL_RUNTIME_CLASS,
    CS_CHR_CREATE_BEH_CHARA_FRAGMENT_RUNTIME_CLASS,
    CS_CHR_UPDATE_POST_FRAGMENT_RUNTIME_CLASS,
    CS_CHR_UPDATE_PRE_FRAGMENT_RUNTIME_CLASS,
    CS_MULTI_NPC_PLAYER_INS_TASK_RUNTIME_CLASS,
    CS_MULTI_PLAYER_INS_TASK_RUNTIME_CLASS,
    CS_PS4_TROPHY_STEP_RUNTIME_CLASS,
    CS_PS_PLUS_CHECK_STEP_RUNTIME_CLASS,
    CS_PREFETCH_FOR_SLOW_STORAGE_STEP_RUNTIME_CLASS,
    CS_LAMS_CAPTURE_STEP_RUNTIME_CLASS,
    CS_MOVIE_INS_RUNTIME_CLASS,
    CS_COLLECT_NEAR_NAVI_MESH_PARTS_RUNTIME_CLASS,
    CS_DUNGEON_RITUAL_HELPER_RUNTIME_CLASS,
    SPRJ_DUNGEON_GATE_INS_RUNTIME_CLASS,
    SPRJ_MOVE_MAP_LIST_STEP_RUNTIME_CLASS,
    SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_RUNTIME_CLASS,
    CS_MENU_ASM_MODEL_REND_RUNTIME_CLASS,
    CS_LAMS_CAPTURE_THREAD_RUNTIME_CLASS,
    CS_HK_BEH_CHARA_CREATER_RUNTIME_CLASS,
    CS_GPARAM_BANK_IMP_RUNTIME_CLASS,
    CS_ENTRYFILELIST_FILE_CAP_RUNTIME_CLASS,
    CS_ENTRYFILELIST_REPOSITORY_IMP_RUNTIME_CLASS,
    CS_ENTRYFILELIST_RES_CAP_RUNTIME_CLASS,
    CS_ENTRYFILELISTBND_FILE_CAP_RUNTIME_CLASS,
    CS_HIT_FLOOR_INFO_RUNTIME_CLASS,
    CS_SLOPE_CTRL_RUNTIME_CLASS,
    FRPG_ORTHO_CAM_RUNTIME_CLASS,
    FRPG_NET_CONNECT_MAN_STEP_RUNTIME_CLASS,
    FRPG_NET_CONNECT_STEP_RUNTIME_CLASS,
    FRPG_NET_SYS_STEP_RUNTIME_CLASS,
    FRPG_NET_LOBBY_STEP_RUNTIME_CLASS,
    FRPG_MENU_DLG_BLOOD_MSG_RUNTIME_CLASS,
    FRPG_MENU_DLG_EQUIP_RUNTIME_CLASS,
    FRPG_MENU_DLG_INVENTORY_RUNTIME_CLASS,
    FRPG_MENU_DLG_REPOSITORY_RUNTIME_CLASS,
    FRPG_MENU_DLG_STATUS_RUNTIME_CLASS,
    FRPG_MENU_DLG_SYSTEM_RUNTIME_CLASS,
    FRPG_PHYS_INS_RUNTIME_CLASS,
};

// Seven entries, followed immediately by the template class-pointer global.
// Repeated update entries are intentional; no terminating zero row is claimed.
inline constexpr StepCallbackSymbol SPRJ_WORLD_BLOCK_NVM_PREPARE_CALLBACKS[] = {
    {"SprjWorldBlockNvmPrepare::_STEP_None", Rva{0x1e28840}, Rva{0x49a36f2}},
    {"SprjWorldBlockNvmPrepare::_STEP_FileLoadWait", Rva{0x1e28850}, Rva{0x49a373c}},
    {"SprjWorldBlockNvmPrepare::_STEP_NvmUpdate", Rva{0x1e289b0}, Rva{0x49a3796}},
    {"SprjWorldBlockNvmPrepare::_STEP_NvmUpdate", Rva{0x1e289b0}, Rva{0x49a3796}},
    {"SprjWorldBlockNvmPrepare::_STEP_NvmUpdate", Rva{0x1e289b0}, Rva{0x49a3796}},
    {"SprjWorldBlockNvmPrepare::_STEP_NvmSetUpWait", Rva{0x1e289e0}, Rva{0x49a37ea}},
    {"SprjWorldBlockNvmPrepare::_STEP_Finish", Rva{0x1e28a30}, Rva{0x49a3844}},
};

inline constexpr StepCallbackSymbol SPRJ_FILE_STEP_CALLBACKS[] = {
    {"SprjFileStep::STEP_Init", Rva{0x142bcf0}, Rva{0x495217c}},
    {"SprjFileStep::STEP_Update", Rva{0x142c100}, Rva{0x49521ac}},
    {"SprjFileStep::STEP_Finish", Rva{0x142c210}, Rva{0x49521e0}},
};

inline constexpr StepCallbackSymbol SPRJ_FD4_LOCATION_STEP_CALLBACKS[] = {
    {"SprjFD4LocationStep::STEP_Init", Rva{0x1d3ea70}, Rva{0x499e3b2}},
    {"SprjFD4LocationStep::STEP_Update", Rva{0x1d3ebb0}, Rva{0x499e3f0}},
    {"SprjFD4LocationStep::STEP_Finish", Rva{0x1d3ec00}, Rva{0x499e432}},
};

inline constexpr StepCallbackSymbol SPRJ_CAMERA_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1a09b90}, Rva{0x4991626}},
    {"STEP_Update", Rva{0x1a09cd0}, Rva{0x499165a}},
    {"STEP_Finish", Rva{0x1a09d20}, Rva{0x4991692}},
};

inline constexpr StepCallbackSymbol CS_SESSION_CONNECT_STATE_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1ece570}, Rva{0x49acdfe}},
    {"STEP_Update", Rva{0x1ece590}, Rva{0x49ace48}},
    {"STEP_LeaveSession", Rva{0x1ece5d0}, Rva{0x49ace96}},
    {"STEP_LeaveSessionWait", Rva{0x1ece5e0}, Rva{0x49acef0}},
    {"STEP_Finish", Rva{0x1ece630}, Rva{0x49acf52}},
};

inline constexpr StepCallbackSymbol CS_NETWORK_FLOW_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1e57140}, Rva{0x49a53fa}},
    {"STEP_OfflineMode", Rva{0x1e57170}, Rva{0x49a5434}},
    {"STEP_OnlineMode", Rva{0x1e57210}, Rva{0x49a547c}},
    {"STEP_Finish", Rva{0x1e57860}, Rva{0x49a54c2}},
};

inline constexpr StepCallbackSymbol CS_REQUEST_GET_SOS_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1e5faf0}, Rva{0x49a5922}},
    {"STEP_Update_forSeamless", Rva{0x1e5fbf0}, Rva{0x49a5960}},
    {"STEP_Update_forWide", Rva{0x1e603b0}, Rva{0x49a59ba}},
    {"STEP_Finish", Rva{0x1e603c0}, Rva{0x49a5a0c}},
};

inline constexpr StepCallbackSymbol CS_REQUEST_SUMMON_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x14b47a0}, Rva{0x4960bda}},
    {"STEP_Update", Rva{0x14b47c0}, Rva{0x4960c18}},
    {"STEP_RequestSummonCheckWait", Rva{0x14b4890}, Rva{0x4960c5a}},
    {"STEP_RequestSummonCheckComplete", Rva{0x14b48c0}, Rva{0x4960cbc}},
    {"STEP_Finish", Rva{0x14b4ae0}, Rva{0x4960d26}},
};

inline constexpr StepCallbackSymbol CS_ENTERLEAVE_DIRECTION_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x196b050}, Rva{0x4989406}},
    {"STEP_C2H_Init_forSelf", Rva{0x196b0b0}, Rva{0x4989450}},
    {"STEP_C2H_Init_forSelfPrologue", Rva{0x196b0f0}, Rva{0x49894b2}},
    {"STEP_C2H_Wait_forSelfPrologue", Rva{0x196b500}, Rva{0x4989524}},
    {"STEP_C2H_Init_forSelfLoading", Rva{0x196b580}, Rva{0x4989596}},
    {"STEP_C2H_Wait_forSelfLoading", Rva{0x196b730}, Rva{0x4989606}},
    {"STEP_C2H_Init_forSelfEpilogue", Rva{0x196b770}, Rva{0x4989676}},
    {"STEP_C2H_Wait_forSelfEpilogue", Rva{0x196b7c0}, Rva{0x49896e8}},
    {"STEP_C2H_Init_forOthers", Rva{0x196b830}, Rva{0x498975a}},
    {"STEP_C2H_Init_forOthersPrologue", Rva{0x196b870}, Rva{0x49897c0}},
    {"STEP_C2H_Wait_forOthersPrologue", Rva{0x196ba20}, Rva{0x4989836}},
    {"STEP_C2H_Init_forOthersEpilogue", Rva{0x196be40}, Rva{0x49898ac}},
    {"STEP_C2H_Wait_forOthersEpilogue", Rva{0x196be90}, Rva{0x4989922}},
    {"STEP_H2C_Init", Rva{0x196bef0}, Rva{0x4989998}},
    {"STEP_H2C_Init_forEffect", Rva{0x196c030}, Rva{0x49899ea}},
    {"STEP_H2C_Wait_forEffect", Rva{0x196c2d0}, Rva{0x4989a50}},
    {"STEP_Finish", Rva{0x196c530}, Rva{0x4989ab6}},
};

inline constexpr StepCallbackSymbol ENTERLEAVE_DIRECTOR_IMP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x19706d0}, Rva{0x4989c2e}},
    {"STEP_Update", Rva{0x19707d0}, Rva{0x4989c70}},
    {"STEP_Finish", Rva{0x1970880}, Rva{0x4989cb6}},
};

inline constexpr StepCallbackSymbol CS_MULTI_NPC_PLAYER_INS_TASK_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1e4b170}, Rva{0x49a4bf8}},
    {"STEP_SummonMsgWait", Rva{0x1e4b1b0}, Rva{0x49a4c3e}},
    {"STEP_SummonWait", Rva{0x1e4b230}, Rva{0x49a4c96}},
    {"STEP_Summon", Rva{0x1e4b440}, Rva{0x49a4ce8}},
    {"STEP_Update", Rva{0x1e4b620}, Rva{0x49a4d32}},
    {"STEP_ReturnWait", Rva{0x1e4b650}, Rva{0x49a4d7c}},
    {"STEP_Return", Rva{0x1e4b830}, Rva{0x49a4dce}},
    {"STEP_Finish", Rva{0x1e4bc00}, Rva{0x49a4e18}},
};

inline constexpr StepCallbackSymbol CS_MULTI_PLAYER_INS_TASK_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1e4fcd0}, Rva{0x49a4f86}},
    {"STEP_StartMultiPlayNotifyWait", Rva{0x1e4fcf0}, Rva{0x49a4fc6}},
    {"STEP_StartMultiPlayWait", Rva{0x1e4fd50}, Rva{0x49a502e}},
    {"STEP_FirstSyncCompleteWait", Rva{0x1e4ff70}, Rva{0x49a508a}},
    {"STEP_Create", Rva{0x1e50090}, Rva{0x49a50ec}},
    {"STEP_CreateWait", Rva{0x1e505e0}, Rva{0x49a5130}},
    {"STEP_Update", Rva{0x1e50830}, Rva{0x49a517c}},
    {"STEP_WaitExitMultiPlay", Rva{0x1e50860}, Rva{0x49a51c0}},
    {"STEP_Delete", Rva{0x1e50880}, Rva{0x49a521a}},
    {"STEP_DeleteWait", Rva{0x1e50a70}, Rva{0x49a525e}},
    {"STEP_Finish", Rva{0x1e50d70}, Rva{0x49a52aa}},
};

inline constexpr StepCallbackSymbol CS_PS4_TROPHY_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x20204c0}, Rva{0x49be094}},
    {"STEP_Exec_SceNpTrophyCreateContext", Rva{0x20204f0}, Rva{0x49be0ca}},
    {"STEP_Exec_SceNpTrophyCreateHandle", Rva{0x2020560}, Rva{0x49be132}},
    {"STEP_Start_AsyncUpdateThread", Rva{0x2020590}, Rva{0x49be198}},
    {"STEP_Request_SceNpTrophyRegisterContext", Rva{0x2020620}, Rva{0x49be1f4}},
    {"STEP_Wait_SceNpTrophyRegisterContext", Rva{0x2020630}, Rva{0x49be266}},
    {"STEP_Request_CreateDebugInfo", Rva{0x2020690}, Rva{0x49be2d2}},
    {"STEP_Wait_CreateDebugInfo", Rva{0x20206a0}, Rva{0x49be32e}},
    {"STEP_Wait_UnlockRequest", Rva{0x20206b0}, Rva{0x49be384}},
    {"STEP_Finish", Rva{0x20206c0}, Rva{0x49be3d6}},
};

inline constexpr StepCallbackSymbol CS_PS_PLUS_CHECK_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1e77f40}, Rva{0x49a7c8e}},
    {"STEP_RegistCallback", Rva{0x1e78010}, Rva{0x49a7cc8}},
    {"STEP_WaitRecheckEvent", Rva{0x1e780e0}, Rva{0x49a7d16}},
    {"STEP_ReqestCheck", Rva{0x1e78220}, Rva{0x49a7d68}},
    {"STEP_WaitCheckResult", Rva{0x1e783c0}, Rva{0x49a7db0}},
    {"STEP_UnresistCallback", Rva{0x1e78550}, Rva{0x49a7e00}},
    {"STEP_Finish", Rva{0x1e785f0}, Rva{0x49a7e52}},
};

inline constexpr StepCallbackSymbol CS_PREFETCH_FOR_SLOW_STORAGE_STEP_CALLBACKS[] = {
    {"STEP_Init", Rva{0x1fdfaa0}, Rva{0x49b8b50}},
    {"STEP_Init_Chache", Rva{0x1fdfac0}, Rva{0x49b8ba0}},
    {"STEP_Wait_Chache", Rva{0x1fdfd20}, Rva{0x49b8bfe}},
    {"STEP_Finish", Rva{0x1fdfe60}, Rva{0x49b8c5c}},
};

inline constexpr StepCallbackSymbol CS_LAMS_CAPTURE_STEP_CALLBACKS[] = {
    {"_STEP_Initialize", Rva{0x1fa59a0}, Rva{0x49b6bac}},
    {"_STEP_WaitInput", Rva{0x1fa5ab0}, Rva{0x49b6bf4}},
    {"_STEP_InitializeStdin", Rva{0x1fa5ac0}, Rva{0x49b6c3a}},
    {"_STEP_WaitStdin", Rva{0x1fa5c20}, Rva{0x49b6c8c}},
    {"_STEP_InitializeMsgCapture", Rva{0x1fa5c30}, Rva{0x49b6cd2}},
    {"_STEP_SetMsg", Rva{0x1fa5d80}, Rva{0x49b6d2e}},
    {"_STEP_UpdateMsg", Rva{0x1fa61c0}, Rva{0x49b6d6e}},
    {"_STEP_InitializeTalk", Rva{0x1fa6240}, Rva{0x49b6db4}},
    {"_STEP_SetTalk", Rva{0x1fa6350}, Rva{0x49b6e04}},
    {"_STEP_UpdateTalk", Rva{0x1fa63f0}, Rva{0x49b6e46}},
    {"_STEP_Finalize", Rva{0x1fa6460}, Rva{0x49b6e8e}},
};

inline constexpr StepCallbackSymbol CS_MOVIE_INS_CALLBACKS[] = {
    {"CSMovieIns::STEP_Init", Rva{0x1fcd910}, Rva{0x49b8540}},
    {"CSMovieIns::STEP_Wait_Request", Rva{0x1fcd930}, Rva{0x49b856c}},
    {"CSMovieIns::STEP_Init_Setup", Rva{0x1fcd980}, Rva{0x49b85a8}},
    {"CSMovieIns::STEP_Wait_Setup", Rva{0x1fcdef0}, Rva{0x49b85e0}},
    {"CSMovieIns::STEP_Finish_Setup", Rva{0x1fce210}, Rva{0x49b8618}},
    {"CSMovieIns::STEP_Init_Play", Rva{0x1fd1d70}, Rva{0x49b8654}},
    {"CSMovieIns::STEP_Wait_Play", Rva{0x1fce3b0}, Rva{0x49b868a}},
    {"CSMovieIns::STEP_Finish_Play", Rva{0x1fce650}, Rva{0x49b86c0}},
    {"CSMovieIns::STEP_Finish", Rva{0x1fce820}, Rva{0x49b86fa}},
};

inline constexpr StepCallbackSymbol CS_COLLECT_NEAR_NAVI_MESH_PARTS_CALLBACKS[] = {
    {"CSCollectNearNaviMeshParts::STEP_Wait", Rva{0x1e19690}, Rva{0x49a329a}},
    {"CSCollectNearNaviMeshParts::STEP_GetPathDist", Rva{0x1e198f0}, Rva{0x49a32e6}},
    {"CSCollectNearNaviMeshParts::STEP_CollectNearby", Rva{0x1e1a170}, Rva{0x49a3340}},
    {"CSCollectNearNaviMeshParts::STEP_Result", Rva{0x1e1a1f0}, Rva{0x49a339e}},
    {"CSCollectNearNaviMeshParts::STEP_Finish", Rva{0x1e1a2d0}, Rva{0x49a33ee}},
};

inline constexpr StepCallbackSymbol CS_DUNGEON_RITUAL_HELPER_CALLBACKS[] = {
    {"CSDungeonRitualHelper::STEP_Init", Rva{0x1aa64a0}, Rva{0x49955ee}},
    {"CSDungeonRitualHelper::STEP_Wait_forRequest", Rva{0x1aa64c0}, Rva{0x4995630}},
    {"CSDungeonRitualHelper::STEP_Exec_forSuspend", Rva{0x1aa65b0}, Rva{0x4995688}},
    {"CSDungeonRitualHelper::STEP_Exec_forResume", Rva{0x1aa6860}, Rva{0x49956e0}},
    {"CSDungeonRitualHelper::STEP_Wait_forResume", Rva{0x1aa71e0}, Rva{0x4995736}},
    {"CSDungeonRitualHelper::STEP_ExecUpload_forResume", Rva{0x1aa7430}, Rva{0x499578c}},
    {"CSDungeonRitualHelper::STEP_WaitUpload_forResume", Rva{0x1aa7620}, Rva{0x49957ee}},
    {"CSDungeonRitualHelper::STEP_Finish_forResume", Rva{0x1aa76b0}, Rva{0x4995850}},
    {"CSDungeonRitualHelper::STEP_Exec_forSetupHolygrail", Rva{0x1aa79c0}, Rva{0x49958aa}},
    {"CSDungeonRitualHelper::STEP_Exec_forJoin", Rva{0x1aa7c00}, Rva{0x4995910}},
    {"CSDungeonRitualHelper::STEP_Exec_forRandomJoin", Rva{0x1aa7e40}, Rva{0x4995962}},
    {"CSDungeonRitualHelper::STEP_Wait_forRandomJoin", Rva{0x1aa7e60}, Rva{0x49959c0}},
    {"CSDungeonRitualHelper::STEP_Finish_forRandomJoin", Rva{0x1aa7f80}, Rva{0x4995a1e}},
    {"CSDungeonRitualHelper::STEP_Exec_forQuickJoin", Rva{0x1aa81d0}, Rva{0x4995a80}},
    {"CSDungeonRitualHelper::STEP_Exec_forCancel", Rva{0x1aa8410}, Rva{0x4995adc}},
    {"CSDungeonRitualHelper::STEP_Exec_forPresentOffering", Rva{0x1aa87f0}, Rva{0x4995b32}},
    {"CSDungeonRitualHelper::STEP_Wait_forPresentOffering", Rva{0x1aa8810}, Rva{0x4995b9a}},
    {"CSDungeonRitualHelper::STEP_ExecUpload_forPresentOffering", Rva{0x1aa8b20}, Rva{0x4995c02}},
    {"CSDungeonRitualHelper::STEP_WaitUpload_forPresentOffering", Rva{0x1aa8cb0}, Rva{0x4995c76}},
    {"CSDungeonRitualHelper::STEP_ExecGet_forPresentOffering", Rva{0x1aa8fc0}, Rva{0x4995cea}},
    {"CSDungeonRitualHelper::STEP_WaitGet_forPresentOffering", Rva{0x1aa91f0}, Rva{0x4995d58}},
    {"CSDungeonRitualHelper::STEP_Finish_forPresentOffering", Rva{0x1aa9500}, Rva{0x4995dc6}},
    {"CSDungeonRitualHelper::STEP_Exec_forChangeShareLevel", Rva{0x1aa9d60}, Rva{0x4995e32}},
    {"CSDungeonRitualHelper::STEP_WaitUpload_forChangeShareLevel", Rva{0x1aa9d80}, Rva{0x4995e9c}},
    {"CSDungeonRitualHelper::STEP_Wait_forChangeShareLevel", Rva{0x1aa9e10}, Rva{0x4995f12}},
    {"CSDungeonRitualHelper::STEP_ExecGet_forChangeShareLevel", Rva{0x1aa9ea0}, Rva{0x4995f7c}},
    {"CSDungeonRitualHelper::STEP_WaitGet_forChangeShareLevel", Rva{0x1aaa0d0}, Rva{0x4995fec}},
    {"CSDungeonRitualHelper::STEP_Finish_forChangeShareLevel", Rva{0x1aaa160}, Rva{0x499605c}},
    {"CSDungeonRitualHelper::STEP_Exec_forErrorRecovery", Rva{0x1aaa3a0}, Rva{0x49960ca}},
    {"CSDungeonRitualHelper::STEP_StateSwitch", Rva{0x1aaa3d0}, Rva{0x499612e}},
    {"CSDungeonRitualHelper::STEP_Finish", Rva{0x1aaa750}, Rva{0x499617e}},
};

inline constexpr StepCallbackSymbol SPRJ_DUNGEON_GATE_INS_CALLBACKS[] = {
    {"SprjDungeonGateIns::STEP_Init", Rva{0x1ab5370}, Rva{0x49975ae}},
    {"SprjDungeonGateIns::STEP_Init_forChannelLoad", Rva{0x1ab5390}, Rva{0x49975ea}},
    {"SprjDungeonGateIns::STEP_Update_forChannelLoad", Rva{0x1ab5450}, Rva{0x4997644}},
    {"SprjDungeonGateIns::STEP_Finish_forChannelLoad", Rva{0x1ab5470}, Rva{0x49976a2}},
    {"SprjDungeonGateIns::STEP_ModeSwitch", Rva{0x1ab5490}, Rva{0x4997700}},
    {"SprjDungeonGateIns::STEP_Init_forDispError", Rva{0x1ab55b0}, Rva{0x4997748}},
    {"SprjDungeonGateIns::STEP_Update_forDispError", Rva{0x1ab56a0}, Rva{0x499779e}},
    {"SprjDungeonGateIns::STEP_Finish_forDispError", Rva{0x1ab56d0}, Rva{0x49977f8}},
    {"SprjDungeonGateIns::STEP_Init_forNoHolygrail", Rva{0x1ab5750}, Rva{0x4997852}},
    {"SprjDungeonGateIns::STEP_Update_forNoHolygrail", Rva{0x1ab6bf0}, Rva{0x49978ac}},
    {"SprjDungeonGateIns::STEP_Finish_forNoHolygrail", Rva{0x1ab6c90}, Rva{0x499790a}},
    {"SprjDungeonGateIns::STEP_Init_forLocalRitual", Rva{0x1ab71c0}, Rva{0x4997968}},
    {"SprjDungeonGateIns::STEP_Update_forLocalRitual", Rva{0x1ab7570}, Rva{0x49979c2}},
    {"SprjDungeonGateIns::STEP_Finish_forLocalRitual", Rva{0x1ab75a0}, Rva{0x4997a20}},
    {"SprjDungeonGateIns::STEP_Init_forServerRitual", Rva{0x1ab7740}, Rva{0x4997a7e}},
    {"SprjDungeonGateIns::STEP_Update_forServerRitual", Rva{0x1ab7880}, Rva{0x4997ada}},
    {"SprjDungeonGateIns::STEP_Finish_forServerRitual", Rva{0x1ab78b0}, Rva{0x4997b3a}},
    {"SprjDungeonGateIns::STEP_Init_forLocalDungeon", Rva{0x1ab7a50}, Rva{0x4997b9a}},
    {"SprjDungeonGateIns::STEP_Update_forLocalDungeon", Rva{0x1ab7b90}, Rva{0x4997bf6}},
    {"SprjDungeonGateIns::STEP_Finish_forLocalDungeon", Rva{0x1ab7bc0}, Rva{0x4997c56}},
    {"SprjDungeonGateIns::STEP_Init_forServerDungeon", Rva{0x1ab7d00}, Rva{0x4997cb6}},
    {"SprjDungeonGateIns::STEP_Update_forServerDungeon", Rva{0x1ab7e40}, Rva{0x4997d14}},
    {"SprjDungeonGateIns::STEP_Finish_forServerDungeon", Rva{0x1ab7e70}, Rva{0x4997d76}},
    {"SprjDungeonGateIns::STEP_Finish", Rva{0x1ab7fb0}, Rva{0x4997dd8}},
};

inline constexpr StepCallbackSymbol SPRJ_MOVE_MAP_LIST_STEP_CALLBACKS[] = {
    {"SprjMoveMapListStep::STEP_Init", Rva{0x19c4870}, Rva{0x498f722}},
    {"SprjMoveMapListStep::STEP_Init_forData", Rva{0x19c4870}, Rva{0x498f760}},
    {"SprjMoveMapListStep::STEP_Wait_forData", Rva{0x19c1d60}, Rva{0x498f7ae}},
    {"SprjMoveMapListStep::STEP_Wait", Rva{0x19c1eb0}, Rva{0x498f7fc}},
    {"SprjMoveMapListStep::STEP_Finish", Rva{0x19c1fa0}, Rva{0x498f83a}},
};

inline constexpr StepCallbackSymbol SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_CALLBACKS[] = {
    {"SprjDungeonMoveMapListStep::STEP_Init", Rva{0x1962850}, Rva{0x4988ce8}},
    {"SprjDungeonMoveMapListStep::STEP_ListSelect", Rva{0x1962940}, Rva{0x4988d34}},
    {"SprjDungeonMoveMapListStep::STEP_MoveMapWait", Rva{0x19633a0}, Rva{0x4988d8c}},
    {"SprjDungeonMoveMapListStep::STEP_Finish", Rva{0x19633c0}, Rva{0x4988de6}},
};

inline constexpr StepCallbackSymbol CS_MENU_ASM_MODEL_REND_CALLBACKS[] = {
    {"CSMenuAsmModelRend::STEP_Init", Rva{0x1dab280}, Rva{0x49a0f22}},
    {"CSMenuAsmModelRend::STEP_Wait_Request", Rva{0x1dab2a0}, Rva{0x49a0f5e}},
    {"CSMenuAsmModelRend::STEP_Init_Setup", Rva{0x1dab2e0}, Rva{0x49a0faa}},
    {"CSMenuAsmModelRend::STEP_Wait_Setup", Rva{0x1dab410}, Rva{0x49a0ff2}},
    {"CSMenuAsmModelRend::STEP_Finish_Setup", Rva{0x1dac0c0}, Rva{0x49a103a}},
    {"CSMenuAsmModelRend::STEP_Init_Play", Rva{0x1dac170}, Rva{0x49a1086}},
    {"CSMenuAsmModelRend::STEP_Wait_Play", Rva{0x1dac2d0}, Rva{0x49a10cc}},
    {"CSMenuAsmModelRend::STEP_Finish_Play", Rva{0x1dac5c0}, Rva{0x49a1112}},
    {"CSMenuAsmModelRend::STEP_Finish", Rva{0x1dac6c0}, Rva{0x49a115c}},
};

inline constexpr StepTemplateSymbol CS_SESSION_CONNECT_STATE_STEP_TEMPLATE = {"CSSessionConnectStateStep", Rva{0x55909a8}, Rva{0x1ed2e80}, CS_SESSION_CONNECT_STATE_STEP_CALLBACKS, count_of(CS_SESSION_CONNECT_STATE_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_NETWORK_FLOW_STEP_TEMPLATE = {"CSNetworkFlowStep", Rva{0x558f228}, Rva{0x1e579d0}, CS_NETWORK_FLOW_STEP_CALLBACKS, count_of(CS_NETWORK_FLOW_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_REQUEST_GET_SOS_STEP_TEMPLATE = {"CSRequestGetSosStep", Rva{0x558f778}, Rva{0x1e60580}, CS_REQUEST_GET_SOS_STEP_CALLBACKS, count_of(CS_REQUEST_GET_SOS_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_REQUEST_SUMMON_STEP_TEMPLATE = {"CSRequestSummonStep", Rva{0x5551de8}, Rva{0x14bb770}, CS_REQUEST_SUMMON_STEP_CALLBACKS, count_of(CS_REQUEST_SUMMON_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_ENTERLEAVE_DIRECTION_STEP_TEMPLATE = {"CSEnterleaveDirectionStep", Rva{0x5574918}, Rva{0x196c760}, CS_ENTERLEAVE_DIRECTION_STEP_CALLBACKS, count_of(CS_ENTERLEAVE_DIRECTION_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol ENTERLEAVE_DIRECTOR_IMP_TEMPLATE = {"EnterleaveDirectorImp", Rva{0x5574cf8}, Rva{0x19709f0}, ENTERLEAVE_DIRECTOR_IMP_CALLBACKS, count_of(ENTERLEAVE_DIRECTOR_IMP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_MULTI_NPC_PLAYER_INS_TASK_TEMPLATE = {"CSMultiNPCPlayerInsTask", Rva{0x558eb40}, Rva{0x1e4bd70}, CS_MULTI_NPC_PLAYER_INS_TASK_CALLBACKS, count_of(CS_MULTI_NPC_PLAYER_INS_TASK_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_MULTI_PLAYER_INS_TASK_TEMPLATE = {"CSMultiPlayerInsTask", Rva{0x558ee28}, Rva{0x1e50f00}, CS_MULTI_PLAYER_INS_TASK_CALLBACKS, count_of(CS_MULTI_PLAYER_INS_TASK_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_PS4_TROPHY_STEP_TEMPLATE = {"CSPS4TrophyStep", Rva{0x569fdf0}, Rva{0x2020830}, CS_PS4_TROPHY_STEP_CALLBACKS, count_of(CS_PS4_TROPHY_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_PS_PLUS_CHECK_STEP_TEMPLATE = {"CSPSPlusCheckStep", Rva{0x55902e0}, Rva{0x1e789b0}, CS_PS_PLUS_CHECK_STEP_CALLBACKS, count_of(CS_PS_PLUS_CHECK_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_PREFETCH_FOR_SLOW_STORAGE_STEP_TEMPLATE = {"CSPrefetchForSlowStorageStep", Rva{0x569e5c0}, Rva{0x1fdffd0}, CS_PREFETCH_FOR_SLOW_STORAGE_STEP_CALLBACKS, count_of(CS_PREFETCH_FOR_SLOW_STORAGE_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_LAMS_CAPTURE_STEP_TEMPLATE = {"CSLamsCaptureStep", Rva{0x5598698}, Rva{0x1fa6a30}, CS_LAMS_CAPTURE_STEP_CALLBACKS, count_of(CS_LAMS_CAPTURE_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_MOVIE_INS_TEMPLATE = {"CSMovieIns", Rva{0x569dc38}, Rva{0x1fce990}, CS_MOVIE_INS_CALLBACKS, count_of(CS_MOVIE_INS_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_COLLECT_NEAR_NAVI_MESH_PARTS_TEMPLATE = {"CSCollectNearNaviMeshParts", Rva{0x558db90}, Rva{0x1e1a5b0}, CS_COLLECT_NEAR_NAVI_MESH_PARTS_CALLBACKS, count_of(CS_COLLECT_NEAR_NAVI_MESH_PARTS_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_DUNGEON_RITUAL_HELPER_TEMPLATE = {"CSDungeonRitualHelper", Rva{0x557e248}, Rva{0x1aaa8c0}, CS_DUNGEON_RITUAL_HELPER_CALLBACKS, count_of(CS_DUNGEON_RITUAL_HELPER_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_DUNGEON_GATE_INS_TEMPLATE = {"SprjDungeonGateIns", Rva{0x557e840}, Rva{0x1abf510}, SPRJ_DUNGEON_GATE_INS_CALLBACKS, count_of(SPRJ_DUNGEON_GATE_INS_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_MOVE_MAP_LIST_STEP_TEMPLATE = {"SprjMoveMapListStep", Rva{0x5577c00}, Rva{0x19c2110}, SPRJ_MOVE_MAP_LIST_STEP_CALLBACKS, count_of(SPRJ_MOVE_MAP_LIST_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_TEMPLATE = {"SprjDungeonMoveMapListStep", Rva{0x55742f8}, Rva{0x1963530}, SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_CALLBACKS, count_of(SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol CS_MENU_ASM_MODEL_REND_TEMPLATE = {"CSMenuAsmModelRend", Rva{0x558b288}, Rva{0x1dac860}, CS_MENU_ASM_MODEL_REND_CALLBACKS, count_of(CS_MENU_ASM_MODEL_REND_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_CAMERA_STEP_TEMPLATE = {"SprjCameraStep", Rva{0x5579e00}, Rva{0x1a09ee0}, SPRJ_CAMERA_STEP_CALLBACKS, count_of(SPRJ_CAMERA_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_FD4_LOCATION_STEP_TEMPLATE = {"SprjFD4LocationStep", Rva{0x5587a90}, Rva{0x1d3edc0}, SPRJ_FD4_LOCATION_STEP_CALLBACKS, count_of(SPRJ_FD4_LOCATION_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_FILE_STEP_TEMPLATE = {"SprjFileStep", Rva{0x554f470}, Rva{0x142c460}, SPRJ_FILE_STEP_CALLBACKS, count_of(SPRJ_FILE_STEP_CALLBACKS)};
inline constexpr StepTemplateSymbol SPRJ_WORLD_BLOCK_NVM_PREPARE_TEMPLATE = {"SprjWorldBlockNvmPrepare", Rva{0x558dfb0}, Rva{0x1e28ba0}, SPRJ_WORLD_BLOCK_NVM_PREPARE_CALLBACKS, count_of(SPRJ_WORLD_BLOCK_NVM_PREPARE_CALLBACKS)};
inline constexpr StepCallbackSymbol SPRJ_EMK_SYSTEM_UPDATE_TASK_CALLBACKS[] = {
    {"SprjEmkSystemUpdateTask::_STEP_Initialize", Rva{0x12f24b0}, Rva{0x4946eb2}},
    {"SprjEmkSystemUpdateTask::_STEP_Update", Rva{0x12f2510}, Rva{0x4946f06}},
    {"SprjEmkSystemUpdateTask::_STEP_Finalize", Rva{0x12f2590}, Rva{0x4946f52}},
};

// Three 0x18-byte callback/adjustment/name records initialized by registration.
inline constexpr StepTemplateSymbol SPRJ_EMK_SYSTEM_UPDATE_TASK_TEMPLATE = {"SprjEmkSystemUpdateTask", Rva{0x5545c40}, Rva{0x12f2750}, SPRJ_EMK_SYSTEM_UPDATE_TASK_CALLBACKS, count_of(SPRJ_EMK_SYSTEM_UPDATE_TASK_CALLBACKS)};

// Ten named callbacks populated by native registration. The final 0x18
// bytes of the cleared 0x108 table are zero, not an eleventh callback.
inline constexpr StepCallbackSymbol CS_DISPLAY_GHOST_CALLBACKS[] = {
    {"CSDisplayGhost::STEP_Init", Rva{0x1a67e70}, Rva{0x4994b6c}},
    {"CSDisplayGhost::STEP_Init_forLoadResource", Rva{0x1a67e90}, Rva{0x4994ba0}},
    {"CSDisplayGhost::STEP_Wait_forLoadResource", Rva{0x1a67f60}, Rva{0x4994bf4}},
    {"CSDisplayGhost::STEP_Init_forBloodMessageGhost", Rva{0x1a67fc0}, Rva{0x4994c48}},
    {"CSDisplayGhost::STEP_Update_forBloodMessageGhost", Rva{0x1a680c0}, Rva{0x4994ca6}},
    {"CSDisplayGhost::STEP_Wait_forFadeOut", Rva{0x1a68110}, Rva{0x4994d08}},
    {"CSDisplayGhost::STEP_Wait_forPlayRequest", Rva{0x1a681f0}, Rva{0x4994d52}},
    {"CSDisplayGhost::STEP_Init_forReleseResource", Rva{0x1a68850}, Rva{0x4994da4}},
    {"CSDisplayGhost::STEP_Wait_forReleseResource", Rva{0x1a688b0}, Rva{0x4994dfc}},
    {"CSDisplayGhost::STEP_Finish", Rva{0x1a688c0}, Rva{0x4994e54}},
};

inline constexpr StepTemplateSymbol CS_DISPLAY_GHOST_TEMPLATE = {"CSDisplayGhost", Rva{0x557d018}, Rva{0x1a68a30}, CS_DISPLAY_GHOST_CALLBACKS, count_of(CS_DISPLAY_GHOST_CALLBACKS)};
inline constexpr StepTemplateSymbol STEP_TEMPLATES[] = {
    CS_DISPLAY_GHOST_TEMPLATE,
    SPRJ_EMK_SYSTEM_UPDATE_TASK_TEMPLATE,
    SPRJ_WORLD_BLOCK_NVM_PREPARE_TEMPLATE,
    SPRJ_FILE_STEP_TEMPLATE,
    SPRJ_CAMERA_STEP_TEMPLATE,
    SPRJ_FD4_LOCATION_STEP_TEMPLATE,
    CS_SESSION_CONNECT_STATE_STEP_TEMPLATE,
    CS_NETWORK_FLOW_STEP_TEMPLATE,
    CS_REQUEST_GET_SOS_STEP_TEMPLATE,
    CS_REQUEST_SUMMON_STEP_TEMPLATE,
    CS_ENTERLEAVE_DIRECTION_STEP_TEMPLATE,
    ENTERLEAVE_DIRECTOR_IMP_TEMPLATE,
    CS_MULTI_NPC_PLAYER_INS_TASK_TEMPLATE,
    CS_MULTI_PLAYER_INS_TASK_TEMPLATE,
    CS_PS4_TROPHY_STEP_TEMPLATE,
    CS_PS_PLUS_CHECK_STEP_TEMPLATE,
    CS_PREFETCH_FOR_SLOW_STORAGE_STEP_TEMPLATE,
    CS_LAMS_CAPTURE_STEP_TEMPLATE,
    CS_MOVIE_INS_TEMPLATE,
    CS_COLLECT_NEAR_NAVI_MESH_PARTS_TEMPLATE,
    CS_DUNGEON_RITUAL_HELPER_TEMPLATE,
    SPRJ_DUNGEON_GATE_INS_TEMPLATE,
    SPRJ_MOVE_MAP_LIST_STEP_TEMPLATE,
    SPRJ_DUNGEON_MOVE_MAP_LIST_STEP_TEMPLATE,
    CS_MENU_ASM_MODEL_REND_TEMPLATE,
};

// Every top-level address constant above, by name, for run-time lookup.
struct NamedSymbol {
    const char* name;
    Rva addr;
};

inline constexpr NamedSymbol ALL_SYMBOLS[] = {
    {"SOS_SIGN_MAN_UPDATE_AND_QUEUE_SUMMON_API_JOBS", SOS_SIGN_MAN_UPDATE_AND_QUEUE_SUMMON_API_JOBS_symbols},
    {"SOS_SIGN_MAN_QUEUE_PENDING_SIGN", SOS_SIGN_MAN_QUEUE_PENDING_SIGN_symbols},
    {"SOS_SIGN_MAN_QUEUE_OR_STAGE_SUMMON_REQUEST", SOS_SIGN_MAN_QUEUE_OR_STAGE_SUMMON_REQUEST_symbols},
    {"SOS_SIGN_MAN_CLEAR_SPECIFIC_PENDING_CREATE", SOS_SIGN_MAN_CLEAR_SPECIFIC_PENDING_CREATE_symbols},
    {"SPRJ_EVENT_SOS_SELECTION_INSERT_OR_UPDATE_REQUEST", SPRJ_EVENT_SOS_SELECTION_INSERT_OR_UPDATE_REQUEST},
    {"SPRJ_EVENT_SOS_SELECTION_MAYBE_SCHEDULE_REQUEST", SPRJ_EVENT_SOS_SELECTION_MAYBE_SCHEDULE_REQUEST},
    {"SPRJ_EVENT_SOS_SELECTION_BUILD_SCHEDULED_WORK", SPRJ_EVENT_SOS_SELECTION_BUILD_SCHEDULED_WORK},
    {"SPRJ_EVENT_SOS_SELECTION_BUILD_REQUEST_FROM_CHR", SPRJ_EVENT_SOS_SELECTION_BUILD_REQUEST_FROM_CHR},
    {"SPRJ_EVENT_SOS_SELECTION_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW", SPRJ_EVENT_SOS_SELECTION_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW},
    {"SOS_SIGN_MAN_INSERT_OR_UPDATE_REQUEST_ENTRY", SOS_SIGN_MAN_INSERT_OR_UPDATE_REQUEST_ENTRY},
    {"SOS_SIGN_MAN_MAYBE_SCHEDULE_REQUEST_ENTRY", SOS_SIGN_MAN_MAYBE_SCHEDULE_REQUEST_ENTRY},
    {"SOS_SIGN_MAN_BUILD_SCHEDULED_WORK_ENTRY", SOS_SIGN_MAN_BUILD_SCHEDULED_WORK_ENTRY},
    {"SOS_SIGN_MAN_BUILD_REQUEST_ENTRY_FROM_CHR", SOS_SIGN_MAN_BUILD_REQUEST_ENTRY_FROM_CHR},
    {"SOS_SIGN_MAN_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW", SOS_SIGN_MAN_TICK_ROOM_HANDOFF_AND_SUMMON_FLOW},
    {"WORLD_CHR_MAN_SINGLETON_PTR", WORLD_CHR_MAN_SINGLETON_PTR},
    {"SPRJ_HOLYGRAIL_SINGLETON_PTR", SPRJ_HOLYGRAIL_SINGLETON_PTR},
    {"SPRJ_DARK_SIGHT_SINGLETON_PTR", SPRJ_DARK_SIGHT_SINGLETON_PTR},
    {"WORLD_CHR_MAN_IS_SUMMON_AREA_ELIGIBLE", WORLD_CHR_MAN_IS_SUMMON_AREA_ELIGIBLE},
    {"SUMMON_AREA_DISABLE_FLAG_TABLE", SUMMON_AREA_DISABLE_FLAG_TABLE},
    {"SPRJ_TASK_SINGLETON_PTR", SPRJ_TASK_SINGLETON_PTR},
    {"SPRJ_FD4_LOCATION_SINGLETON_PTR", SPRJ_FD4_LOCATION_SINGLETON_PTR},
    {"SPRJ_TASK_GROUP_SINGLETON_PTR", SPRJ_TASK_GROUP_SINGLETON_PTR},
    {"FD4_TASK_MANAGER_SINGLETON_PTR", FD4_TASK_MANAGER_SINGLETON_PTR},
    {"SPRJ_TASK_REGISTER_FN", SPRJ_TASK_REGISTER_FN},
    {"SPRJ_TASK_SUBMIT_REGISTRATION_FN", SPRJ_TASK_SUBMIT_REGISTRATION_FN},
    {"SPRJ_TASK_UNREGISTER_FN", SPRJ_TASK_UNREGISTER_FN},
    {"SPRJ_TASK_SET_GROUP_FN", SPRJ_TASK_SET_GROUP_FN},
    {"SPRJ_TASK_BASE_UNREGISTER_FN", SPRJ_TASK_BASE_UNREGISTER_FN},
    {"SPRJ_TASK_GROUPS_CONSTRUCT_FN", SPRJ_TASK_GROUPS_CONSTRUCT_FN},
    {"SPRJ_TASK_REGISTRATION_EXECUTE_FN", SPRJ_TASK_REGISTRATION_EXECUTE_FN},
    {"SPRJ_CALLBACK_TASK_EXECUTE_DISPATCH_FN", SPRJ_CALLBACK_TASK_EXECUTE_DISPATCH_FN},
    {"SPRJ_CALLBACK_TASK_INVOKE_MEMBER_FN", SPRJ_CALLBACK_TASK_INVOKE_MEMBER_FN},
    {"FD4_TASK_MANAGER_REGISTER_FN", FD4_TASK_MANAGER_REGISTER_FN},
    {"FD4_TASK_MANAGER_UNREGISTER_FN", FD4_TASK_MANAGER_UNREGISTER_FN},
    {"SPRJ_EVENT_FLAG_MAN_READ_BIT_FN", SPRJ_EVENT_FLAG_MAN_READ_BIT_FN},
    {"SPRJ_EVENT_FLAG_MAN_WRITE_BIT_FN", SPRJ_EVENT_FLAG_MAN_WRITE_BIT_FN},
    {"SPRJ_EVENT_FLAG_MAN_READ_RANGE_FN", SPRJ_EVENT_FLAG_MAN_READ_RANGE_FN},
    {"SPRJ_EVENT_FLAG_MAN_WRITE_RANGE_FN", SPRJ_EVENT_FLAG_MAN_WRITE_RANGE_FN},
    {"SPRJ_EVENT_FLAG_MAN_SET_LOAD_MODE_FN", SPRJ_EVENT_FLAG_MAN_SET_LOAD_MODE_FN},
    {"SPRJ_EVENT_FLAG_MAN_RESOLVE_GROUP_KEY_FN", SPRJ_EVENT_FLAG_MAN_RESOLVE_GROUP_KEY_FN},
    {"SPRJ_EVENT_FLAG_MAN_SERIALIZE_SHARED_SNAPSHOT_FN", SPRJ_EVENT_FLAG_MAN_SERIALIZE_SHARED_SNAPSHOT_FN},
    {"SPRJ_EVENT_FLAG_MAN_APPEND_FLAG_GROUP_FN", SPRJ_EVENT_FLAG_MAN_APPEND_FLAG_GROUP_FN},
    {"SPRJ_EVENT_FLAG_MAN_APPLY_SNAPSHOT_FN", SPRJ_EVENT_FLAG_MAN_APPLY_SNAPSHOT_FN},
    {"SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG_FN", SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG_FN_symbols},
    {"SPRJ_EVENT_FLAG_MAN_CLEAR_BIT_RANGE_FN", SPRJ_EVENT_FLAG_MAN_CLEAR_BIT_RANGE_FN},
    {"WORLD_CHR_MAN_DBG_SINGLETON_PTR", WORLD_CHR_MAN_DBG_SINGLETON_PTR},
    {"SPRJ_FLIPPER_SINGLETON_PTR", SPRJ_FLIPPER_SINGLETON_PTR},
    {"SPRJ_FLIPPER_CONSTRUCTOR_FN", SPRJ_FLIPPER_CONSTRUCTOR_FN},
    {"SPRJ_FLIPPER_UPDATE_FN", SPRJ_FLIPPER_UPDATE_FN},
    {"SPRJ_FLIPPER_DEBUG_FORMAT_FN", SPRJ_FLIPPER_DEBUG_FORMAT_FN},
    {"SPRJ_FLIPPER_MODE_NAME_TABLE", SPRJ_FLIPPER_MODE_NAME_TABLE},
    {"CHR_SYNC_FACTORY_FN", CHR_SYNC_FACTORY_FN},
    {"LOCAL_PLAYER_CHR_SYNC_CONSTRUCTOR_FN", LOCAL_PLAYER_CHR_SYNC_CONSTRUCTOR_FN},
    {"LOCAL_PLAYER_CHR_SYNC_UPDATE_FN", LOCAL_PLAYER_CHR_SYNC_UPDATE_FN},
    {"CHR_SYNC_FRAME_SKIP_FALLBACK_INSTRUCTION", CHR_SYNC_FRAME_SKIP_FALLBACK_INSTRUCTION},
    {"CHR_SYNC_FRAME_SKIP_CONFIG_KEY", CHR_SYNC_FRAME_SKIP_CONFIG_KEY},
    {"PLAYER_FRAME_SNAPSHOT_CONSTRUCTOR_FN", PLAYER_FRAME_SNAPSHOT_CONSTRUCTOR_FN},
    {"PLAYER_FRAME_SNAPSHOT_BROADCAST_FN", PLAYER_FRAME_SNAPSHOT_BROADCAST_FN},
    {"PLAYER_FRAME_SNAPSHOT_RECEIVE_FN", PLAYER_FRAME_SNAPSHOT_RECEIVE_FN},
    {"NET_WORLD_CHR_SYNC_BLOCK_RESET_FN", NET_WORLD_CHR_SYNC_BLOCK_RESET_FN},
    {"NET_WORLD_CHR_SYNC_CONSTRUCTOR_FN", NET_WORLD_CHR_SYNC_CONSTRUCTOR_FN},
    {"NET_WORLD_CHR_SYNC_UPDATE_FN", NET_WORLD_CHR_SYNC_UPDATE_FN},
    {"NET_WORLD_CHR_SYNC_PEER_HANDLE_UPDATE_FN", NET_WORLD_CHR_SYNC_PEER_HANDLE_UPDATE_FN},
    {"NET_WORLD_CHR_SYNC_DEBUG_MENU_NAME_FN", NET_WORLD_CHR_SYNC_DEBUG_MENU_NAME_FN},
    {"NET_WORLD_CHR_SYNC_DEBUG_MENU_FOLDER_CALLSITE", NET_WORLD_CHR_SYNC_DEBUG_MENU_FOLDER_CALLSITE},
    {"CHR_INS_COMPUTE_NETWORK_AUTHORITY_SCORE_FN", CHR_INS_COMPUTE_NETWORK_AUTHORITY_SCORE_FN},
    {"CHR_INS_SET_NETWORK_UPDATE_AUTHORITY_FN", CHR_INS_SET_NETWORK_UPDATE_AUTHORITY_FN},
    {"NETWORK_AUTHORITY_LOCAL_CONTROL_BONUS", NETWORK_AUTHORITY_LOCAL_CONTROL_BONUS},
    {"NET_WORLD_CHR_SYNC_GET_OR_CREATE_AUTHORITY_PEER_FN", NET_WORLD_CHR_SYNC_GET_OR_CREATE_AUTHORITY_PEER_FN},
    {"NET_WORLD_CHR_SYNC_AUTHORITY_PEER_CONSTRUCTOR_FN", NET_WORLD_CHR_SYNC_AUTHORITY_PEER_CONSTRUCTOR_FN},
    {"NET_WORLD_CHR_SYNC_SET_AUTHORITY_CANDIDATES_FN", NET_WORLD_CHR_SYNC_SET_AUTHORITY_CANDIDATES_FN},
    {"NET_WORLD_CHR_SYNC_RESOLVE_AUTHORITY_CONFLICTS_FN", NET_WORLD_CHR_SYNC_RESOLVE_AUTHORITY_CONFLICTS_FN},
    {"LUA_REQUEST_FORCE_UPDATE_NETWORK_FN", LUA_REQUEST_FORCE_UPDATE_NETWORK_FN},
    {"LUA_REQUEST_NORMAL_UPDATE_NETWORK_FN", LUA_REQUEST_NORMAL_UPDATE_NETWORK_FN},
    {"WORLD_INFO_CONSTRUCTOR_FN", WORLD_INFO_CONSTRUCTOR_FN},
    {"WORLD_RES_CONSTRUCTOR_FN", WORLD_RES_CONSTRUCTOR_FN},
    {"CHR_INS_SET_MAP_COLLISION_ENTRY_FN", CHR_INS_SET_MAP_COLLISION_ENTRY_FN},
    {"CHR_CTRL_CONSTRUCTOR_FN", CHR_CTRL_CONSTRUCTOR_FN},
    {"CHR_CTRL_NESTED_STATE_CONSTRUCTOR_FN", CHR_CTRL_NESTED_STATE_CONSTRUCTOR_FN},
    {"CHR_INS_UPDATE_COLLISION_BINDING_MODE_FROM_CONTROLLER_FN", CHR_INS_UPDATE_COLLISION_BINDING_MODE_FROM_CONTROLLER_FN},
    {"LUA_SET_DRAW_GROUP_FN", LUA_SET_DRAW_GROUP_FN},
    {"LUA_SET_HIT_INFO_FN", LUA_SET_HIT_INFO_FN},
    {"LUA_SET_DISABLE_BACKREAD_FOR_EVENT_FN", LUA_SET_DISABLE_BACKREAD_FOR_EVENT_FN},
    {"LUA_SET_ALWAYS_ENABLE_BACKREAD_FOR_EVENT_FN", LUA_SET_ALWAYS_ENABLE_BACKREAD_FOR_EVENT_FN},
    {"CHR_INS_HAS_ACTIVE_BACKREAD_ROUTE_FN", CHR_INS_HAS_ACTIVE_BACKREAD_ROUTE_FN},
    {"CHR_INS_UPDATE_MAP_COLLISION_BINDING_FN", CHR_INS_UPDATE_MAP_COLLISION_BINDING_FN},
    {"CHR_INS_INITIALIZE_MAP_COLLISION_FROM_SPAWN_FN", CHR_INS_INITIALIZE_MAP_COLLISION_FROM_SPAWN_FN},
    {"WORLD_CHR_MAN_REBIND_MAIN_PLAYER_MAP_COLLISION_FN", WORLD_CHR_MAN_REBIND_MAIN_PLAYER_MAP_COLLISION_FN_symbols},
    {"CHR_INS_APPLY_TRANSITION_OCCUPANCY_STATE_FN", CHR_INS_APPLY_TRANSITION_OCCUPANCY_STATE_FN},
    {"PLAYER_INS_REFRESH_MODULE_WORLD_BINDINGS_FN", PLAYER_INS_REFRESH_MODULE_WORLD_BINDINGS_FN},
    {"CANDIDATE_REINITIALIZE_RUNTIME_STATE_FN", CANDIDATE_REINITIALIZE_RUNTIME_STATE_FN},
    {"PLAYER_INS_FINALIZE_TRANSITION_RESET_FN", PLAYER_INS_FINALIZE_TRANSITION_RESET_FN},
    {"CHR_CTRL_REBUILD_CONTROL_WORD_FN", CHR_CTRL_REBUILD_CONTROL_WORD_FN},
    {"WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V15091700_FN", WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V15091700_FN},
    {"WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V10080300_FN", WORLD_CHR_AREA_APPLY_TRANSITION_OCCUPANCY_V10080300_FN},
    {"WORLD_CHR_MAN_CONSUME_TRANSITION_SNAPSHOT_FN", WORLD_CHR_MAN_CONSUME_TRANSITION_SNAPSHOT_FN_symbols},
    {"WORLD_CHR_MAN_BUILD_CHARACTER_UPDATE_LISTS_FN", WORLD_CHR_MAN_BUILD_CHARACTER_UPDATE_LISTS_FN_symbols},
    {"PLAYER_INS_SELECT_SYNC_FOR_BACKREAD_STATE_FN", PLAYER_INS_SELECT_SYNC_FOR_BACKREAD_STATE_FN},
    {"WORLD_CHR_MAN_REMOVE_CHR_DELAYED_FN", WORLD_CHR_MAN_REMOVE_CHR_DELAYED_FN_symbols},
    {"WORLD_LOAD_STEP_REBUILD_LOCAL_PLAYER_FN", WORLD_LOAD_STEP_REBUILD_LOCAL_PLAYER_FN},
    {"WORLD_LOAD_CONSUME_TRANSITION_SNAPSHOT_STEP_FN", WORLD_LOAD_CONSUME_TRANSITION_SNAPSHOT_STEP_FN},
    {"WORLD_LOAD_COORDINATOR_UPDATE_FN", WORLD_LOAD_COORDINATOR_UPDATE_FN},
    {"WORLD_RES_PREPARE_PLAYER_MAP_TRANSITION_FN", WORLD_RES_PREPARE_PLAYER_MAP_TRANSITION_FN},
    {"WORLD_RES_CHECK_PLAYER_COLLISION_READY_FN", WORLD_RES_CHECK_PLAYER_COLLISION_READY_FN},
    {"WORLD_TRANSITION_RESET_TRACKED_ENTITIES_FN", WORLD_TRANSITION_RESET_TRACKED_ENTITIES_FN_symbols},
    {"MAP_COLLISION_GROUP_REBUILD_ENTRIES_FN", MAP_COLLISION_GROUP_REBUILD_ENTRIES_FN},
    {"MAP_COLLISION_MANAGER_REBUILD_GROUP_BY_INDEX_FN", MAP_COLLISION_MANAGER_REBUILD_GROUP_BY_INDEX_FN},
    {"MAP_COLLISION_ENTRY_CONSTRUCTOR_FN", MAP_COLLISION_ENTRY_CONSTRUCTOR_FN},
    {"MAP_COLLISION_ENTRY_SOURCE_CONSTRUCTOR_FN", MAP_COLLISION_ENTRY_SOURCE_CONSTRUCTOR_FN},
    {"MAP_COLLISION_GROUP_FIND_ENTRIES_BY_SOURCE_ID_FN", MAP_COLLISION_GROUP_FIND_ENTRIES_BY_SOURCE_ID_FN},
    {"MAP_COLLISION_MANAGER_CONSTRUCTOR_FN", MAP_COLLISION_MANAGER_CONSTRUCTOR_FN},
    {"WORLD_CHR_ACTION_UPDATE_FN", WORLD_CHR_ACTION_UPDATE_FN},
    {"WORLD_BLOCK_RES_REQUEST_FILES_FN", WORLD_BLOCK_RES_REQUEST_FILES_FN_symbols},
    {"WORLD_BLOCK_RES_ADVANCE_LOAD_STATE_FN", WORLD_BLOCK_RES_ADVANCE_LOAD_STATE_FN_symbols},
    {"WORLD_BLOCK_RES_UPDATE_FN", WORLD_BLOCK_RES_UPDATE_FN_symbols},
    {"WORLD_RES_UPDATE_FN", WORLD_RES_UPDATE_FN_symbols},
    {"WORLD_RES_REQUEST_ALL_BLOCK_FILES_FN", WORLD_RES_REQUEST_ALL_BLOCK_FILES_FN_symbols},
    {"WORLD_RES_LOAD_CONTEXT_CONSTRUCTOR_FN", WORLD_RES_LOAD_CONTEXT_CONSTRUCTOR_FN_symbols},
    {"WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR", WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR_symbols},
    {"WORLD_BACK_READ_CONSTRUCTOR_FN", WORLD_BACK_READ_CONSTRUCTOR_FN},
    {"WORLD_BACK_READ_UPDATE_FN", WORLD_BACK_READ_UPDATE_FN},
    {"WORLD_BACK_READ_CONSIDER_CANDIDATE_FN", WORLD_BACK_READ_CONSIDER_CANDIDATE_FN},
    {"WORLD_BACK_READ_UPDATE_PROFILE_FN", WORLD_BACK_READ_UPDATE_PROFILE_FN},
    {"MAP_COLLISION_GROUP_UPDATE_BACKREAD_STATE_FN", MAP_COLLISION_GROUP_UPDATE_BACKREAD_STATE_FN},
    {"MAP_COLLISION_ENTRY_UPDATE_STREAMING_STATE_FN", MAP_COLLISION_ENTRY_UPDATE_STREAMING_STATE_FN},
    {"MAP_COLLISION_ENTRY_LOAD_COLLISION_RESOURCE_FN", MAP_COLLISION_ENTRY_LOAD_COLLISION_RESOURCE_FN},
    {"MAP_COLLISION_ENTRY_COMMIT_RENDER_STATE_FN", MAP_COLLISION_ENTRY_COMMIT_RENDER_STATE_FN},
    {"WORLD_HIT_COLLISION_RESOURCE_CONSTRUCTOR_FN", WORLD_HIT_COLLISION_RESOURCE_CONSTRUCTOR_FN},
    {"WORLD_HIT_COLLISION_RESOURCE_ACTIVATE_FN", WORLD_HIT_COLLISION_RESOURCE_ACTIVATE_FN},
    {"WORLD_HIT_COLLISION_RESOURCE_DEACTIVATE_FN", WORLD_HIT_COLLISION_RESOURCE_DEACTIVATE_FN},
    {"WORLD_HIT_COLLISION_MANAGER_QUEUE_DIRTY_RESOURCE_FN", WORLD_HIT_COLLISION_MANAGER_QUEUE_DIRTY_RESOURCE_FN},
    {"WORLD_HIT_COLLISION_RESOURCE_COLLECT_ACTIVATION_DELTAS_FN", WORLD_HIT_COLLISION_RESOURCE_COLLECT_ACTIVATION_DELTAS_FN},
    {"SPRJ_HK_AI_MANAGER_PROCESS_DYNAMIC_NAVMESH_CHANGES_FN", SPRJ_HK_AI_MANAGER_PROCESS_DYNAMIC_NAVMESH_CHANGES_FN},
    {"HKAI_NAVMESH_INSTANCE_INIT_CLEARANCE_CACHE_FN", HKAI_NAVMESH_INSTANCE_INIT_CLEARANCE_CACHE_FN},
    {"HKAI_DYNAMIC_NAVMESH_RESET_CLEARANCE_CACHES_FN", HKAI_DYNAMIC_NAVMESH_RESET_CLEARANCE_CACHES_FN},
    {"MOVE_MAP_CONTROLLER_REQUEST_MOVE_FN", MOVE_MAP_CONTROLLER_REQUEST_MOVE_FN},
    {"MOVE_MAP_CONTROLLER_PROCESS_COMPLETED_REQUEST_FN", MOVE_MAP_CONTROLLER_PROCESS_COMPLETED_REQUEST_FN},
    {"WORLD_CHR_MAN_UPDATE_FN", WORLD_CHR_MAN_UPDATE_FN},
    {"CHR_CAM_OWNER_ROOT_PTR", CHR_CAM_OWNER_ROOT_PTR},
    {"SPRJ_EVENT_STATE_SINGLETON_PTR", SPRJ_EVENT_STATE_SINGLETON_PTR},
    {"SPRJ_DBG_EVENT_SINGLETON_PTR", SPRJ_DBG_EVENT_SINGLETON_PTR},
    {"SPRJ_TENDENCY_MAN_SINGLETON_PTR", SPRJ_TENDENCY_MAN_SINGLETON_PTR},
    {"SPRJ_EVENT_ACT_MAN_SINGLETON_PTR", SPRJ_EVENT_ACT_MAN_SINGLETON_PTR},
    {"SPRJ_EVENT_FLAG_MAN_SINGLETON_PTR", SPRJ_EVENT_FLAG_MAN_SINGLETON_PTR},
    {"SPRJ_EVENT_MAN_SINGLETON_PTR", SPRJ_EVENT_MAN_SINGLETON_PTR},
    {"SPRJ_EVENT_REGION_MAN_SINGLETON_PTR", SPRJ_EVENT_REGION_MAN_SINGLETON_PTR},
    {"SPRJ_EMK_SYSTEM_SINGLETON_PTR", SPRJ_EMK_SYSTEM_SINGLETON_PTR},
    {"SPRJ_LUA_EVENT_MAN_SINGLETON_PTR", SPRJ_LUA_EVENT_MAN_SINGLETON_PTR},
    {"SPRJ_TARGET_BANK_MANAGER_SINGLETON_PTR", SPRJ_TARGET_BANK_MANAGER_SINGLETON_PTR},
    {"SPRJ_WORLD_AI_MANAGER_SINGLETON_PTR", SPRJ_WORLD_AI_MANAGER_SINGLETON_PTR},
    {"GAME_DATA_MAN_SINGLETON_PTR", GAME_DATA_MAN_SINGLETON_PTR},
    {"MAP_INS_MAN_SINGLETON_PTR", MAP_INS_MAN_SINGLETON_PTR},
    {"OBJ_INS_MAN_SINGLETON_PTR", OBJ_INS_MAN_SINGLETON_PTR},
    {"WORLD_SESSION_OBJECT_MAN_SINGLETON_PTR", WORLD_SESSION_OBJECT_MAN_SINGLETON_PTR},
    {"MAP_COLLISION_MANAGER_SINGLETON_PTR", MAP_COLLISION_MANAGER_SINGLETON_PTR},
    {"COLLISION_MAP_MAN_SINGLETON_PTR", COLLISION_MAP_MAN_SINGLETON_PTR},
    {"SPRJ_RAPID_REENTRY_HELPER_SINGLETON_PTR", SPRJ_RAPID_REENTRY_HELPER_SINGLETON_PTR},
    {"SPRJ_RAPID_REENTRY_HELPER_CONSTRUCTOR_FN", SPRJ_RAPID_REENTRY_HELPER_CONSTRUCTOR_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_DESTRUCTOR_FN", SPRJ_RAPID_REENTRY_HELPER_DESTRUCTOR_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_DELETING_DESTRUCTOR_FN", SPRJ_RAPID_REENTRY_HELPER_DELETING_DESTRUCTOR_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_RESET_FN", SPRJ_RAPID_REENTRY_HELPER_RESET_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_SET_STATE_FN", SPRJ_RAPID_REENTRY_HELPER_SET_STATE_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_GET_ENABLED_FN", SPRJ_RAPID_REENTRY_HELPER_GET_ENABLED_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_GET_MODE_FN", SPRJ_RAPID_REENTRY_HELPER_GET_MODE_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_TRACK_FILE_FN", SPRJ_RAPID_REENTRY_HELPER_TRACK_FILE_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_TRACK_LOADED_FILE_FN", SPRJ_RAPID_REENTRY_HELPER_TRACK_LOADED_FILE_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_TRACK_MOBANK_FN", SPRJ_RAPID_REENTRY_HELPER_TRACK_MOBANK_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_CAPTURE_RESOURCES_FN", SPRJ_RAPID_REENTRY_HELPER_CAPTURE_RESOURCES_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_RECONCILE_RESOURCES_FN", SPRJ_RAPID_REENTRY_HELPER_RECONCILE_RESOURCES_FN},
    {"SPRJ_RAPID_REENTRY_HELPER_PUBLISH_ENTRYFILELIST_FN", SPRJ_RAPID_REENTRY_HELPER_PUBLISH_ENTRYFILELIST_FN},
    {"SPRJ_SESSION_MANAGER_SINGLETON_PTR", SPRJ_SESSION_MANAGER_SINGLETON_PTR},
    {"WORLD_TRANSITION_STATE_PTR", WORLD_TRANSITION_STATE_PTR},
    {"TUTORIAL_SOS_STATE_PTR", TUTORIAL_SOS_STATE_PTR},
    {"SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_REFRESH_FN", SPRJ_SESSION_MANAGER_JOIN_ROOM_RESULT_REFRESH_FN},
    {"SPRJ_SESSION_MANAGER_SOURCE_ROUTE_REFRESH_FN", SPRJ_SESSION_MANAGER_SOURCE_ROUTE_REFRESH_FN},
    {"SPRJ_BULLET_MANAGER_SINGLETON_PTR", SPRJ_BULLET_MANAGER_SINGLETON_PTR},
    {"SPRJ_CAMERA_SINGLETON_PTR", SPRJ_CAMERA_SINGLETON_PTR},
    {"CHR_EX_FOLLOW_CAM_UPDATE_FN", CHR_EX_FOLLOW_CAM_UPDATE_FN},
    {"SPRJ_ACTION_BUTTON_MAN_SINGLETON_PTR", SPRJ_ACTION_BUTTON_MAN_SINGLETON_PTR},
    {"CS_MULTI_PLAY_MAN_SINGLETON_PTR", CS_MULTI_PLAY_MAN_SINGLETON_PTR},
    {"CS_PLATFORM_NETWORK_MAN_SINGLETON_PTR", CS_PLATFORM_NETWORK_MAN_SINGLETON_PTR},
    {"CSDLC_SINGLETON_PTR", CSDLC_SINGLETON_PTR},
    {"CS_PLAYGO_SINGLETON_PTR", CS_PLAYGO_SINGLETON_PTR},
    {"CS_TROPHY_SINGLETON_PTR", CS_TROPHY_SINGLETON_PTR},
    {"CS_NOW_LOADING_HELPER_SINGLETON_PTR", CS_NOW_LOADING_HELPER_SINGLETON_PTR},
    {"CS_LOD_SINGLETON_PTR", CS_LOD_SINGLETON_PTR},
    {"CS_EZ_WORK_POOL_SINGLETON_PTR", CS_EZ_WORK_POOL_SINGLETON_PTR},
    {"CS_GPARAM_BANK_SINGLETON_PTR", CS_GPARAM_BANK_SINGLETON_PTR},
    {"CS_ENTRYFILELIST_REPOSITORY_SINGLETON_PTR", CS_ENTRYFILELIST_REPOSITORY_SINGLETON_PTR},
    {"CS_NOW_LOADING_HELPER_MENU_LOAD_TABLE", CS_NOW_LOADING_HELPER_MENU_LOAD_TABLE},
    {"FD4_DEBUG_MENU_MANAGER_SINGLETON_PTR", FD4_DEBUG_MENU_MANAGER_SINGLETON_PTR},
    {"FD4_DEBUG_MENU_REPORT_SYSTEM_SINGLETON_PTR", FD4_DEBUG_MENU_REPORT_SYSTEM_SINGLETON_PTR},
    {"FD4_DEBUG_MENU_SHARE_STRING_MANAGER_SINGLETON_PTR", FD4_DEBUG_MENU_SHARE_STRING_MANAGER_SINGLETON_PTR},
    {"MATCHING_ROOT_PTR", MATCHING_ROOT_PTR},
    {"MATCHING_ROOT_STORAGE", MATCHING_ROOT_STORAGE},
    {"NEXUS_REVOLUTION_MAIN_MATCHING_MANAGER_INTERFACE_VTABLE", NEXUS_REVOLUTION_MAIN_MATCHING_MANAGER_INTERFACE_VTABLE},
    {"NEXUS_REVOLUTION_MATCHING_MANAGER_INTERFACE_BASE_VTABLE", NEXUS_REVOLUTION_MATCHING_MANAGER_INTERFACE_BASE_VTABLE},
    {"MATCHING_LIFECYCLE_STATE_GATE", MATCHING_LIFECYCLE_STATE_GATE},
    {"MATCHING_CONTAINER_LOCK_STATE", MATCHING_CONTAINER_LOCK_STATE},
    {"MATCHING_CONTAINER_LOCK_FLAG", MATCHING_CONTAINER_LOCK_FLAG},
    {"SPRJ_TARGET_ACCESSOR_BASE_SIZE_FN", SPRJ_TARGET_ACCESSOR_BASE_SIZE_FN},
    {"SPRJ_NULL_TARGET_ACCESSOR_SIZE_FN", SPRJ_NULL_TARGET_ACCESSOR_SIZE_FN},
    {"SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_SIZE_FN", SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_SIZE_FN},
    {"SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_SIZE_FN", SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_SIZE_FN},
    {"SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_VTABLE", SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_VTABLE},
    {"SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_CONSTRUCTOR_FN", SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_CONSTRUCTOR_FN},
    {"SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_BIND_FN", SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_BIND_FN},
    {"SPRJ_SOUND_TARGET_VTABLE", SPRJ_SOUND_TARGET_VTABLE},
    {"SPRJ_SOUND_TARGET_CONSTRUCTOR_FN", SPRJ_SOUND_TARGET_CONSTRUCTOR_FN},
    {"SPRJ_SOUND_TARGET_RESET_FN", SPRJ_SOUND_TARGET_RESET_FN},
    {"SPRJ_SOUND_TARGET_COMPARE_POSITION_FN", SPRJ_SOUND_TARGET_COMPARE_POSITION_FN},
    {"SPRJ_SOUND_TARGET_IS_NEAR_POINT_FN", SPRJ_SOUND_TARGET_IS_NEAR_POINT_FN},
    {"SPRJ_SOUND_TARGET_SIZE_FN", SPRJ_SOUND_TARGET_SIZE_FN},
    {"SPRJ_FIXED_POS_TARGET_VTABLE", SPRJ_FIXED_POS_TARGET_VTABLE},
    {"SPRJ_FIXED_POS_TARGET_GET_POSITION_FN", SPRJ_FIXED_POS_TARGET_GET_POSITION_FN},
    {"SPRJ_FIXED_POS_TARGET_GET_SCALAR_FN", SPRJ_FIXED_POS_TARGET_GET_SCALAR_FN},
    {"SPRJ_FIXED_POS_TARGET_RESET_FN", SPRJ_FIXED_POS_TARGET_RESET_FN},
    {"SPRJ_FIXED_POS_TARGET_SIZE_FN", SPRJ_FIXED_POS_TARGET_SIZE_FN},
    {"CS_HIT_FLOOR_INFO_VTABLE", CS_HIT_FLOOR_INFO_VTABLE},
    {"CS_HIT_FLOOR_INFO_CONSTRUCTOR_FN", CS_HIT_FLOOR_INFO_CONSTRUCTOR_FN},
    {"CS_HIT_FLOOR_INFO_SET_STATE_FN", CS_HIT_FLOOR_INFO_SET_STATE_FN},
    {"CS_HIT_FLOOR_INFO_ALLOWS_SLOPE_FN", CS_HIT_FLOOR_INFO_ALLOWS_SLOPE_FN},
    {"CS_HIT_FLOOR_INFO_RESOLVE_COLLISION_FN", CS_HIT_FLOOR_INFO_RESOLVE_COLLISION_FN},
    {"CS_HIT_FLOOR_INFO_CLEAR_COLLISION_FN", CS_HIT_FLOOR_INFO_CLEAR_COLLISION_FN},
    {"CS_HIT_FLOOR_INFO_SIZE_FN", CS_HIT_FLOOR_INFO_SIZE_FN},
    {"CS_SLOPE_CTRL_VTABLE", CS_SLOPE_CTRL_VTABLE},
    {"CS_SLOPE_CTRL_CONSTRUCTOR_FN", CS_SLOPE_CTRL_CONSTRUCTOR_FN},
    {"CS_SLOPE_CTRL_UPDATE_ANGLE_FN", CS_SLOPE_CTRL_UPDATE_ANGLE_FN},
    {"CS_SLOPE_CTRL_UPDATE_RESPONSE_FN", CS_SLOPE_CTRL_UPDATE_RESPONSE_FN},
    {"CS_SLOPE_CTRL_CLEAR_RESPONSE_FN", CS_SLOPE_CTRL_CLEAR_RESPONSE_FN},
    {"CS_SLOPE_CTRL_SIZE_FN", CS_SLOPE_CTRL_SIZE_FN},
    {"SPRJ_CHR_PHYSICS_MODULE_VTABLE", SPRJ_CHR_PHYSICS_MODULE_VTABLE},
    {"SPRJ_CHR_PHYSICS_MODULE_CONSTRUCTOR_FN", SPRJ_CHR_PHYSICS_MODULE_CONSTRUCTOR_FN},
    {"SPRJ_CHR_PHYSICS_MODULE_SIZE_FN", SPRJ_CHR_PHYSICS_MODULE_SIZE_FN},
    {"SPRJ_EMK_RES_MAN_SINGLETON_PTR", SPRJ_EMK_RES_MAN_SINGLETON_PTR},
    {"SPRJ_EDF_REPOSITORY_SINGLETON_PTR", SPRJ_EDF_REPOSITORY_SINGLETON_PTR},
    {"SPRJ_ELD_REPOSITORY_SINGLETON_PTR", SPRJ_ELD_REPOSITORY_SINGLETON_PTR},
    {"SPRJ_EVD_REPOSITORY_SINGLETON_PTR", SPRJ_EVD_REPOSITORY_SINGLETON_PTR},
    {"SPRJ_FILE_SINGLETON_PTR", SPRJ_FILE_SINGLETON_PTR},
};

// Linear search of ALL_SYMBOLS; nullptr when the name is unknown.
inline const NamedSymbol* find_symbol(const char* name) {
    for (const NamedSymbol& s : ALL_SYMBOLS) {
        const char* a = s.name;
        const char* b = name;
        while (*a && *a == *b) {
            ++a;
            ++b;
        }
        if (*a == *b) return &s;
    }
    return nullptr;
}

}  // namespace bb
