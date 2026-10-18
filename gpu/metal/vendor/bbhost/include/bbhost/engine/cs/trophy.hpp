// CSTrophy: the PS4 trophy owner, its implementation, retained unlock
// requests, and debug data. CSTrophy is the asserted singleton name;
// CSTrophyImp and the supporting record/container/message names describe
// observed objects, not independently reflected classes.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSPS4TrophyStep;

inline constexpr std::size_t CS_TROPHY_SIZE = 0x20;
inline constexpr std::size_t CS_TROPHY_IMP_SIZE = 0xc0;

inline constexpr std::size_t CS_TROPHY_IMP_OFFSET = 0x08;
inline constexpr std::size_t CS_TROPHY_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET = 0x10;
inline constexpr std::size_t CS_TROPHY_DEBUG_FLAG_OFFSET = 0x18;

// Worker message +0x50 borrows this record inside a retained list node.
// Completion is set after the unlock API returns, regardless of its result.
// The SystemStep callback removes at most one completed head node per tick.
struct CSTrophyUnlockRequest {
    std::int32_t trophy_id;
    std::uint8_t completed;
    Unknown<3> _unk05;
};

struct CSTrophyUnlockRequestNode {
    CSTrophyUnlockRequestNode* next;
    CSTrophyUnlockRequestNode* previous;
    CSTrophyUnlockRequest request;
};

// Descriptive 0x20-byte native list owner at implementation +0x78. The
// 0x18-byte sentinel is self-linked and has no valid request payload.
struct CSTrophyUnlockRequestList {
    Unknown<8> _unk00;
    CSTrophyUnlockRequestNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Native debug record populated by GetTrophyInfo and read by DEBUG_LABEL_FN.
// IDs initialize to -1 and the rest to zero. Successful unlock updates only
// unlocked (including the returned platinum ID when not -1); a completed
// request alone does not establish that this byte was set.
struct CSTrophyDebugInfo {
    std::int32_t trophy_id;
    std::uint32_t grade;
    std::uint8_t hidden;
    std::uint8_t _unk09;
    std::uint16_t name[128];
    std::uint8_t unlocked;
    std::uint8_t _unk10b;
};

// Descriptive 0x28-byte vector owner at implementation +0x98. Native elements
// have stride 0x10c, with allocation alignment four.
struct CSTrophyDebugInfoVector {
    Unknown<8> _unk00;
    CSTrophyDebugInfo* begin;
    CSTrophyDebugInfo* end;
    CSTrophyDebugInfo* capacity_end;
    void* allocator;
};

// Exact 0xc0-byte implementation allocated aligned to eight by CSTrophy.
// Constructor loads sysmodule 0xad and registers update_task on SystemStep.
// START_FN separately allocates the local initialization step. ASYNC_RUN_FN
// handles messages on the DLThread created by that step; it is virtual +0x00,
// while the non-deleting destructor is virtual +0x08.
//
// Native destruction deletes the step, debug vector, request list and mutex,
// then unregisters the task. It does not stop/free the thread or destroy the
// platform context/handle. The observed CSTrophy destructor does not delete
// this implementation.
struct CSTrophyImp {
    const void* vftable;
    SprjCallbackTask update_task;
    CSPS4TrophyStep* step;
    // Set by WaitUnlockRequest, which remains the active step afterward.
    std::uint8_t ready;
    Unknown<3> _unk41;
    std::int32_t context;
    std::int32_t handle;
    Unknown<4> _unk4c;
    // DLThread, allocation 0xc8 aligned to eight; runnable owner is this impl.
    void* async_thread;
    // Native pthread mutex (vtable, handle, initialized byte); protects shared
    // completion flags. It does not make unsynchronized list walks safe.
    Unknown<0x18> _mutex58;
    // Worker skips messages 0/1/3 when nonzero. Constructor clears it; no
    // shutdown setter has been established in the captured owner lifecycle.
    std::uint8_t suppress_async_operations;
    // Completion of the registration call, including a failed platform call.
    std::uint8_t registration_done;
    // Completion of debug enumeration, or immediate success when no debug menu.
    std::uint8_t debug_info_done;
    Unknown<5> _unk73;
    CSTrophyUnlockRequestList unlock_requests;
    CSTrophyDebugInfoVector debug_info;

    static constexpr std::size_t SIZE = CS_TROPHY_IMP_SIZE;
    static constexpr Rva VTABLE{0x535a480};
    static constexpr Rva UPDATE_TASK_VTABLE{0x53b14a0};
    static constexpr Rva ASYNC_THREAD_NAME{0x493ae64};
    static constexpr Rva CONSTRUCTOR_FN{0x2024430};
    static constexpr Rva DESTRUCTOR_FN{0x2024710};
    static constexpr Rva UPDATE_FN{0x2024650};
    static constexpr Rva START_FN{0x2024900};
    static constexpr Rva IS_READY_FN{0x2024960};
    // Requires an existing thread; rejects duplicate retained IDs, appends a
    // node, and posts an unlock message. No trophy-ID bounds check is observed.
    static constexpr Rva REQUEST_UNLOCK_FN{0x2024970};
    static constexpr Rva ASYNC_RUN_FN{0x2024b50};
    static constexpr Rva REQUEST_REGISTER_FN{0x2025470};
    static constexpr Rva REQUEST_DEBUG_INFO_FN{0x20255b0};
    static constexpr Rva WAIT_DEBUG_INFO_FN{0x20256f0};
    static constexpr Rva DEBUG_LABEL_FN{0x2025aa0};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::SystemStep;
};

// Exact 0x20-byte singleton. Debug flag +0x18 suppresses the ready predicate;
// the request wrapper itself simply forwards to the implementation.
struct CSTrophy {
    void* vftable;
    CSTrophyImp* imp;
    void* primal_system_debug_menu;
    std::uint8_t debug_flag;
    Unknown<0x07> _pad19;

    static constexpr std::size_t SIZE = CS_TROPHY_SIZE;
    static constexpr Rva SINGLETON_PTR = CS_TROPHY_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493ae80};
    static constexpr Rva VTABLE{0x535a460};
    static constexpr Rva CONSTRUCTOR_FN{0x2023cd0};
    static constexpr Rva DESTRUCTOR_FN{0x2023dd0};
    static constexpr Rva START_FN{0x2023e30};
    static constexpr Rva IS_READY_FN{0x2023e60};
    static constexpr Rva REQUEST_UNLOCK_FN{0x2023e80};
};

// Message IDs switched on by the implementation's asynchronous runnable. Stop
// is an observed consumer case; the captured owner does not send it.
enum class CSTrophyAsyncMessageKind : std::uint32_t {
    RegisterContext = 0,
    Unlock = 1,
    Stop = 2,
    CreateDebugInfo = 3,
};

// Descriptive 0x58-byte unlock message allocated by REQUEST_UNLOCK_FN. Base
// messages use the first 0x50 bytes. If flags bit 0 is clear the worker
// destroys/frees allocation; otherwise it signals completion_event.
struct CSTrophyUnlockMessage {
    const void* vftable;
    void* recipient;
    std::uint32_t flags;
    std::uint32_t kind;
    Unknown<0x20> _completion_event;
    void* allocation;
    void* next;
    void* previous;
    CSTrophyUnlockRequest* request;

    static constexpr Rva VTABLE{0x535a530};
};

namespace detail::trophy_layout {
BB_SIZE(CSTrophy, CS_TROPHY_SIZE);
static_assert(alignof(CSTrophy) == 8, "alignof(CSTrophy)");
BB_OFFSET(CSTrophy, imp, CS_TROPHY_IMP_OFFSET);
BB_OFFSET(CSTrophy, primal_system_debug_menu, CS_TROPHY_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET);
BB_OFFSET(CSTrophy, debug_flag, CS_TROPHY_DEBUG_FLAG_OFFSET);
BB_SIZE(CSTrophyImp, CS_TROPHY_IMP_SIZE);
static_assert(alignof(CSTrophyImp) == 8, "alignof(CSTrophyImp)");
BB_OFFSET(CSTrophyImp, update_task, 8);
BB_OFFSET(CSTrophyImp, update_task.registration, 0x18);
BB_OFFSET(CSTrophyImp, update_task.owner, 0x20);
BB_OFFSET(CSTrophyImp, update_task.callback, 0x28);
BB_OFFSET(CSTrophyImp, step, 0x38);
BB_OFFSET(CSTrophyImp, ready, 0x40);
BB_OFFSET(CSTrophyImp, context, 0x44);
BB_OFFSET(CSTrophyImp, handle, 0x48);
BB_OFFSET(CSTrophyImp, async_thread, 0x50);
BB_OFFSET(CSTrophyImp, _mutex58, 0x58);
BB_OFFSET(CSTrophyImp, suppress_async_operations, 0x70);
BB_OFFSET(CSTrophyImp, registration_done, 0x71);
BB_OFFSET(CSTrophyImp, debug_info_done, 0x72);
BB_OFFSET(CSTrophyImp, unlock_requests, 0x78);
BB_OFFSET(CSTrophyImp, debug_info, 0x98);
static_assert(static_cast<std::uint32_t>(CSTrophyImp::TASK_GROUP) == 1, "CSTrophyImp::TASK_GROUP");
BB_SIZE(CSTrophyUnlockRequestList, 0x20);
BB_OFFSET(CSTrophyUnlockRequestList, sentinel, 8);
BB_OFFSET(CSTrophyUnlockRequestList, count, 0x10);
BB_OFFSET(CSTrophyUnlockRequestList, allocator, 0x18);
BB_SIZE(CSTrophyUnlockRequestNode, 0x18);
static_assert(alignof(CSTrophyUnlockRequestNode) == 8, "alignof(CSTrophyUnlockRequestNode)");
BB_OFFSET(CSTrophyUnlockRequestNode, previous, 8);
BB_OFFSET(CSTrophyUnlockRequestNode, request, 0x10);
BB_SIZE(CSTrophyUnlockRequest, 8);
BB_OFFSET(CSTrophyUnlockRequestNode, request.completed, 0x14);
BB_SIZE(CSTrophyUnlockMessage, 0x58);
static_assert(alignof(CSTrophyUnlockMessage) == 8, "alignof(CSTrophyUnlockMessage)");
BB_OFFSET(CSTrophyUnlockMessage, recipient, 8);
BB_OFFSET(CSTrophyUnlockMessage, flags, 0x10);
BB_OFFSET(CSTrophyUnlockMessage, kind, 0x14);
BB_OFFSET(CSTrophyUnlockMessage, _completion_event, 0x18);
BB_OFFSET(CSTrophyUnlockMessage, allocation, 0x38);
BB_OFFSET(CSTrophyUnlockMessage, next, 0x40);
BB_OFFSET(CSTrophyUnlockMessage, previous, 0x48);
BB_OFFSET(CSTrophyUnlockMessage, request, 0x50);
BB_SIZE(CSTrophyDebugInfoVector, 0x28);
static_assert(alignof(CSTrophyDebugInfoVector) == 8, "alignof(CSTrophyDebugInfoVector)");
BB_OFFSET(CSTrophyDebugInfoVector, begin, 8);
BB_OFFSET(CSTrophyDebugInfoVector, end, 0x10);
BB_OFFSET(CSTrophyDebugInfoVector, capacity_end, 0x18);
BB_OFFSET(CSTrophyDebugInfoVector, allocator, 0x20);
BB_SIZE(CSTrophyDebugInfo, 0x10c);
static_assert(alignof(CSTrophyDebugInfo) == 4, "alignof(CSTrophyDebugInfo)");
BB_OFFSET(CSTrophyDebugInfo, grade, 4);
BB_OFFSET(CSTrophyDebugInfo, hidden, 8);
BB_OFFSET(CSTrophyDebugInfo, name, 0xa);
BB_OFFSET(CSTrophyDebugInfo, unlocked, 0x10a);
}  // namespace detail::trophy_layout

}  // namespace bb
