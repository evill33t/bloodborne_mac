// CSMultiPlayMan: the native owner of multiplayer actor tasks.
//
// It stores primary CSMultiPlayerInsTask pointers and secondary NPC-player
// task pointers in two independent vectors. Session membership is not owned
// here. WorldSessionObjectMan event 0x0e ensures one primary task for a peer
// ObjectRef; this manager then advances readiness, create, exit, and erase
// state for that task. The task itself materializes or retires the remote
// WorldChr actor.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct CSMultiPlayerInsTask;
struct CSMultiNPCPlayerInsTask;

inline constexpr std::size_t CS_MULTI_PLAY_MAN_SIZE = 0x58;

inline constexpr std::size_t CS_MULTI_PLAY_MAN_PRIMARY_VECTOR_OFFSET = 0x10;
inline constexpr std::size_t CS_MULTI_PLAY_MAN_PRIMARY_ALLOCATOR_OFFSET = 0x28;
inline constexpr std::size_t CS_MULTI_PLAY_MAN_SECONDARY_VECTOR_OFFSET = 0x38;
inline constexpr std::size_t CS_MULTI_PLAY_MAN_SECONDARY_ALLOCATOR_OFFSET = 0x50;
inline constexpr Rva CS_MULTI_PLAY_MAN_SINGLETON_POINTER{0x5540230};

// Constructor verified from RVA 0x1e54640.
inline constexpr Rva CS_MULTI_PLAY_MAN_CONSTRUCT_FN{0x1e54640};
// Each ensure function performs the same semantic ObjectRef lookup. If a task
// already exists it is left unchanged; otherwise a 0x100-byte task is
// allocated with the mode encoded by the function name.
inline constexpr Rva CS_MULTI_PLAY_MAN_ENSURE_MODE_3_TASK_FN{0x1e548f0};
inline constexpr Rva CS_MULTI_PLAY_MAN_ENSURE_MODE_0_TASK_FN{0x1e549c0};
inline constexpr Rva CS_MULTI_PLAY_MAN_ENSURE_MODE_2_TASK_FN{0x1e54a90};
inline constexpr Rva CS_MULTI_PLAY_MAN_ENSURE_MODE_1_TASK_FN{0x1e54b60};
// Ensures one 0x130-byte mode-1 NPC task per ChrHandle. Copies the creation
// input only for a new task; an existing matching task is left unchanged.
inline constexpr Rva CS_MULTI_PLAY_MAN_ENSURE_NPC_PLAYER_TASK_FN{0x1e54c30};
// Requests return for a matching NPC handle; repeated requests preserve the
// first selected return-direction bit rather than refreshing that policy.
inline constexpr Rva CS_MULTI_PLAY_MAN_REQUEST_NPC_PLAYER_RETURN_FN{0x1e55050};
// Requests retirement across both the primary ObjectRef and secondary NPC vectors.
inline constexpr Rva CS_MULTI_PLAY_MAN_REQUEST_ALL_TASK_RETURNS_FN{0x1e54fd0};
// Requires every primary index >5 and every NPC index >2. Empty vectors pass.
inline constexpr Rva CS_MULTI_PLAY_MAN_ALL_TASKS_PAST_SUMMON_WAIT_FN{0x1e55320};
// Tests primary index >6/exit-requested or NPC index >4/return-requested.
inline constexpr Rva CS_MULTI_PLAY_MAN_ANY_TASK_RETURNING_FN{0x1e553a0};
// Public jump thunk for the ObjectRef task-lifecycle update.
inline constexpr Rva CS_MULTI_PLAY_MAN_UPDATE_TASK_LIFECYCLE_FOR_OBJECT_REF_FN{0x1e54da0};
// Implementation body reached by the public thunk at RVA 0x1e54da0.
inline constexpr Rva CS_MULTI_PLAY_MAN_UPDATE_TASK_LIFECYCLE_FOR_OBJECT_REF_IMPL_FN{0x1e54db0};
// Compatibility alias for callers that used the public entry point.
inline constexpr Rva CS_MULTI_PLAY_MAN_UPDATE_TASK_FOR_OBJECT_REF_FN =
    CS_MULTI_PLAY_MAN_UPDATE_TASK_LIFECYCLE_FOR_OBJECT_REF_FN;
inline constexpr Rva CS_MULTI_PLAY_MAN_HAS_TRANSITION_READY_ACTIVE_ROUTE_TASK_FN{0x1e55230};
inline constexpr Rva CS_MULTI_PLAY_MAN_PRIMARY_TASKS_PAST_FIRST_SYNC_FN{0x1e55560};
inline constexpr Rva CS_MULTI_PLAY_MAN_SET_TASK_CREATE_REQUESTED_FN{0x1e555b0};
inline constexpr Rva CS_MULTI_PLAY_MAN_SET_TASK_ENTERLEAVE_READINESS_FN{0x1e55610};
inline constexpr Rva CS_MULTI_PLAY_MAN_TICK_AND_ERASE_FINISHED_TASKS_FN{0x1e556f0};

struct CSMultiPlayMan {
    void* vftable;
    Unknown<0x08> _unk08;
    // Native primary CSMultiPlayerInsTask* vector header at +0x10.
    DLVector<CSMultiPlayerInsTask*> primary_vector;
    Unknown<0x08> _unk30;
    // Native secondary CSMultiNPCPlayerInsTask* vector header at +0x38.
    DLVector<CSMultiNPCPlayerInsTask*> secondary_vector;
};

namespace detail::multi_play_layout {
BB_SIZE(CSMultiPlayMan, CS_MULTI_PLAY_MAN_SIZE);
BB_OFFSET(CSMultiPlayMan, primary_vector, CS_MULTI_PLAY_MAN_PRIMARY_VECTOR_OFFSET);
BB_OFFSET(CSMultiPlayMan, primary_vector.allocator, CS_MULTI_PLAY_MAN_PRIMARY_ALLOCATOR_OFFSET);
BB_OFFSET(CSMultiPlayMan, secondary_vector, CS_MULTI_PLAY_MAN_SECONDARY_VECTOR_OFFSET);
BB_OFFSET(CSMultiPlayMan, secondary_vector.allocator, CS_MULTI_PLAY_MAN_SECONDARY_ALLOCATOR_OFFSET);
}  // namespace detail::multi_play_layout

}  // namespace bb
