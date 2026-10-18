// SosSignMan: the FrpgNetMan-owned (+0xc50) native prefix and its summon request boundaries.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/request_summon_step.hpp"
#include "bbhost/engine/frpg.hpp"

namespace bb {

// Layout model only: no list mutation or summon policy. Event-side request
// scheduling and room handoff belong to SprjEventSosSelectionState, not here.

struct CSRequestGetSosStep;
// FrpgNetConnectManStep, SosSignManList/SosSignManOwnedList and the Sos* entry
// types come from frpg.hpp.

struct SosSignManPrefix {
    Unknown<0x10> unknown_000;
    SosSignManList<SosSignNativeCandidateEntry> native_candidate_list;
    Unknown<0x08> unknown_028;
    SosSignManList<SosSignActiveSignEntry> active_sign_list;
    // Registered FrpgNetConnectManStep used to queue or refresh signaling work
    // by NPID. The connect manager is owned by FrpgNetSysStep.
    FrpgNetConnectManStep* connect_manager;
    // Owned, separately allocated CSRequestGetSosStep.
    CSRequestGetSosStep* request_get_sos_step;
    // Embedded request task. Its request/result tail occupies manager offsets
    // +0x120..+0x148; those are not independent SosSignMan fields.
    CSRequestSummonStep request_summon_task;
    Unknown<0x08> unknown_150;
    SosSignManList<SosSignPendingCreateEntry> pending_create_list;
    // Owner word for the list wrapper whose header begins at +0x178.
    void* pending_clear_owner_state;
    SosSignManList<SosSignPendingClearEntry> pending_clear_list;
    Unknown<0x04> unknown_190;
    // Raw request-job values copied into newly built active-sign records.
    std::uint32_t request_job_field_194;
    std::uint32_t request_job_field_198;
    Unknown<0x0c> unknown_19c;
    SosSignManList<SosSignPendingResponseEntry> pending_response_list;
    SosSignManOwnedList<SosSignRequestResultEntry> deferred_request_result_queue;
    std::uint8_t update_flags_1e0;
    Unknown<0x07> unknown_1e1;
    // Raw context supplied when a sign is inserted into the active list.
    void* active_sign_context_1e8;
    Unknown<0x08> unknown_1f0;
    // Cached native search/multiplayer eligibility derived from
    // FrpgNetMan + 0xa98 and bit zero at FrpgNetMan + 0xb14.
    std::uint8_t search_eligibility_cache;
    Unknown<0x3f> unknown_1f9;
};

struct SosSignApiResultBodyPrefix;

// Prefix of a queued application result consumed by the SosSignMan owner
// callback. The body is owned by the generic request-manager queue until the
// drain helper dispatches it.
struct SosSignQueuedApiResultPrefix {
    Unknown<0x08> _unknown_000;
    SosSignApiResultBodyPrefix* body;
};

// Kind and identity prefix shared by the result bodies observed in the
// SosSignMan dispatcher.
struct SosSignApiResultBodyPrefix {
    std::uint8_t _unknown_000;
    // 0 generic result, 1 pending-create response, 2 active-sign update.
    std::uint8_t kind;
    Unknown<0x02> _unknown_002;
    // Kind-specific identity words matched against a sign/request record.
    std::uint32_t identity_word_04;
    std::uint32_t identity_word_08;
    // Kind-specific result value.
    std::uint32_t result_word_0c;
};

// Request-owner prefix passed to the result dispatcher. The ObjectRef at +0x20
// is used for active-sign matching and NPID signaling.
struct SosSignApiRequestOwnerPrefix {
    Unknown<0x20> _unknown_000;
    std::uint8_t object_ref[0x10];
};

inline constexpr Rva SOS_SIGN_MAN_QUEUE_OR_STAGE_SUMMON_REQUEST{0x14baec0};
inline constexpr Rva SOS_SIGN_MAN_DISPATCH_QUEUED_API_RESULT{0x14ba340};
inline constexpr Rva SOS_SIGN_MAN_UPDATE_AND_QUEUE_SUMMON_API_JOBS{0x14b5e10};
inline constexpr Rva SOS_SIGN_MAN_CONSTRUCTOR{0x14b4af0};
inline constexpr Rva SOS_SIGN_MAN_DESTRUCTOR{0x14b53f0};
inline constexpr Rva SOS_SIGN_MAN_QUEUE_PENDING_SIGN{0x14b8d10};
inline constexpr Rva SOS_SIGN_MAN_MOVE_PENDING_CREATE_TO_CLEAR_LIST{0x14b9d40};
inline constexpr Rva SOS_SIGN_MAN_MOVE_PENDING_CREATE_TO_CLEAR_LIST_REMOVE{0x14b9e50};
inline constexpr Rva SOS_SIGN_MAN_CLEAR_SPECIFIC_PENDING_CREATE{0x14bb3d0};
inline constexpr Rva SOS_SIGN_MAN_OWNED_LIST_PUSH_BACK{0x14bf1d0};

inline constexpr std::size_t SOS_SIGN_MAN_FRPG_NET_MAN_OFFSET = 0xc50;
// The nine list/task offsets below are also defined (same values) in
// frpg.hpp, which carries its copies with a `_frpg` suffix.
inline constexpr std::size_t SOS_SIGN_MAN_NATIVE_CANDIDATE_LIST_OFFSET = 0x10;
inline constexpr std::size_t SOS_SIGN_MAN_ACTIVE_SIGN_LIST_OFFSET = 0x30;
inline constexpr std::size_t SOS_SIGN_MAN_CONNECT_MANAGER_OFFSET = 0x48;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_GET_SOS_STEP_OFFSET = 0x50;
inline constexpr std::size_t SOS_SIGN_MAN_REQUEST_SUMMON_TASK_OFFSET = 0x58;
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_CREATE_LIST_OFFSET = 0x158;
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_CLEAR_LIST_OFFSET = 0x178;
inline constexpr std::size_t SOS_SIGN_MAN_PENDING_RESPONSE_LIST_OFFSET = 0x1a8;
inline constexpr std::size_t SOS_SIGN_MAN_DEFERRED_REQUEST_RESULT_QUEUE_OFFSET = 0x1c0;
inline constexpr std::size_t SOS_SIGN_MAN_UPDATE_FLAGS_OFFSET = 0x1e0;
inline constexpr std::size_t SOS_SIGN_MAN_ACTIVE_SIGN_CONTEXT_OFFSET = 0x1e8;
inline constexpr std::size_t SOS_SIGN_MAN_SEARCH_ELIGIBILITY_CACHE_OFFSET = 0x1f8;

// Merges or appends a native sign descriptor to the active-sign container and
// returns the canonical sign object retained by SosSignMan. When an equivalent
// sign already exists, native code merges into it, destroys the supplied
// object, and returns the existing one: stop using the supplied pointer.
using SosSignManQueuePendingSignAbi = void* (BB_GAME_ABI*)(SosSignManPrefix*, void*);
// Queues or immediately stages a summon request result. Immediate staging
// requires an exact descriptor_id match in pending_response_list. When
// FrpgNetMan + 0x0b disables immediate processing, native code owns a copied
// payload in deferred_request_result_queue instead.
using SosSignManQueueOrStageSummonRequestAbi = void (BB_GAME_ABI*)(std::int32_t descriptor_id,
                                                                 std::uint32_t payload_size,
                                                                 const std::uint8_t* payload);
using SosSignManDispatchQueuedApiResultAbi = SosSignQueuedApiResultPrefix* (BB_GAME_ABI*)(
    SosSignManPrefix*, SosSignApiRequestOwnerPrefix*, SosSignQueuedApiResultPrefix*);
using SosSignManOwnedListPushBackAbi = void (BB_GAME_ABI*)(SosSignManOwnedList<SosSignRequestResultEntry>*,
                                                         SosSignRequestResultEntry**);

namespace detail::sos_sign_man_layout {
BB_SIZE(SosSignManList<SosSignActiveSignEntry>, 0x18);
BB_SIZE(SosSignManList<SosSignPendingResponseEntry>, 0x18);
BB_SIZE(SosSignManOwnedList<SosSignRequestResultEntry>, 0x20);
BB_OFFSET(SosSignManPrefix, native_candidate_list, 0x10);
BB_OFFSET(SosSignManPrefix, active_sign_list, 0x30);
BB_OFFSET(SosSignManPrefix, connect_manager, 0x48);
BB_OFFSET(SosSignManPrefix, request_get_sos_step, 0x50);
BB_OFFSET(SosSignManPrefix, request_summon_task, 0x58);
BB_OFFSET(SosSignManPrefix, pending_create_list, 0x158);
BB_OFFSET(SosSignManPrefix, pending_clear_list, 0x178);
BB_OFFSET(SosSignManPrefix, pending_response_list, 0x1a8);
BB_OFFSET(SosSignManPrefix, deferred_request_result_queue, 0x1c0);
BB_OFFSET(SosSignManPrefix, update_flags_1e0, 0x1e0);
BB_OFFSET(SosSignManPrefix, active_sign_context_1e8, 0x1e8);
BB_OFFSET(SosSignManPrefix, search_eligibility_cache, 0x1f8);
BB_SIZE(SosSignManPrefix, 0x238);
BB_OFFSET(SosSignManPrefix, native_candidate_list, SOS_SIGN_MAN_NATIVE_CANDIDATE_LIST_OFFSET);
BB_OFFSET(SosSignManPrefix, active_sign_list, SOS_SIGN_MAN_ACTIVE_SIGN_LIST_OFFSET);
BB_OFFSET(SosSignManPrefix, connect_manager, SOS_SIGN_MAN_CONNECT_MANAGER_OFFSET);
BB_OFFSET(SosSignManPrefix, request_get_sos_step, SOS_SIGN_MAN_REQUEST_GET_SOS_STEP_OFFSET);
BB_OFFSET(SosSignManPrefix, request_summon_task, SOS_SIGN_MAN_REQUEST_SUMMON_TASK_OFFSET);
BB_OFFSET(SosSignManPrefix, pending_create_list, SOS_SIGN_MAN_PENDING_CREATE_LIST_OFFSET);
BB_OFFSET(SosSignManPrefix, pending_clear_list, SOS_SIGN_MAN_PENDING_CLEAR_LIST_OFFSET);
BB_OFFSET(SosSignManPrefix, pending_response_list, SOS_SIGN_MAN_PENDING_RESPONSE_LIST_OFFSET);
BB_OFFSET(SosSignManPrefix, deferred_request_result_queue,
          SOS_SIGN_MAN_DEFERRED_REQUEST_RESULT_QUEUE_OFFSET);
BB_OFFSET(SosSignManPrefix, update_flags_1e0, SOS_SIGN_MAN_UPDATE_FLAGS_OFFSET);
BB_OFFSET(SosSignManPrefix, active_sign_context_1e8, SOS_SIGN_MAN_ACTIVE_SIGN_CONTEXT_OFFSET);
BB_OFFSET(SosSignManPrefix, search_eligibility_cache, SOS_SIGN_MAN_SEARCH_ELIGIBILITY_CACHE_OFFSET);
BB_OFFSET(SosSignQueuedApiResultPrefix, body, 0x08);
BB_SIZE(SosSignQueuedApiResultPrefix, 0x10);
BB_OFFSET(SosSignApiResultBodyPrefix, kind, 0x01);
BB_SIZE(SosSignApiResultBodyPrefix, 0x10);
BB_OFFSET(SosSignApiRequestOwnerPrefix, object_ref, 0x20);
BB_SIZE(SosSignApiRequestOwnerPrefix, 0x30);
}  // namespace detail::sos_sign_man_layout

}  // namespace bb
