// SprjTask scheduler singletons, task-group timeline, step-task prefixes and callback tasks.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct FD4TaskDataPrefix;

// Number of task-group slots accepted by Bloodborne 1.09's registration path.
// The task registration helper at RVA 0x2050490 rejects indices >= 0x4c
// before consulting the SprjTaskGroup singleton.
inline constexpr std::size_t SPRJ_TASK_GROUP_COUNT = 0x4c;
inline constexpr std::size_t SPRJ_TASK_IMP_SIZE = 0x10;
inline constexpr std::size_t SPRJ_TASK_GROUP_SIZE = 0x268;
inline constexpr std::size_t SPRJ_TIME_LINE_TASK_GROUP_INS_PREFIX_SIZE = 0x60;
inline constexpr std::size_t SPRJ_TASK_REGISTRATION_SIZE = 0x20;
inline constexpr std::size_t SPRJ_CALLBACK_TASK_SIZE = 0x30;
inline constexpr std::size_t SPRJ_CALLBACK_TASK_VTABLE_SIZE = 0x38;

// Bloodborne 1.09 timeline indices recovered from SprjTaskGroup_BuildTimeline
// (RVA 0x2051a90).
enum class SprjTaskGroupIndex : std::uint32_t {
    FrameBegin = 0,
    SystemStep = 1,
    FileStep = 2,
    ResStep = 3,
    PadStep = 4,
    ObjResUpdate = 5,
    DungeonGateUpdate = 6,
    GameFlowStep = 7,
    MenuMan = 8,
    GameMan = 9,
    TaskLineIdxSys = 10,
    TaskLineIdxTest = 11,
    TaskLineIdxNetworkFlowStep = 12,
    TaskLineIdxInGameInGameStep = 13,
    TaskLineIdxInGameInGameStayStep = 14,
    TaskLineIdxInGameRemoStep = 15,
    TaskLineIdxInGameRemoManStep = 16,
    RemoStep = 17,
    TaskLineIdxInGameMoveMapStep = 18,
    TaskLineIdxInGameFieldAreaStep = 19,
    TaskLineIdxInGameTestNetStep = 20,
    TaskLineIdxInGameInGameMenuStep = 21,
    TaskLineIdxInGameTitleMenuStep = 22,
    TaskLineIdxInGameCommonMenuStep = 23,
    TaskLineIdxFrpgNetSys = 24,
    TaskLineIdxFrpgNetLobby = 25,
    TaskLineIdxFrpgNetConnectMan = 26,
    TaskLineIdxFrpgNetConnect = 27,
    TaskLineIdxFrpgNetOther = 28,
    SfxMan = 29,
    FaceGenMan = 30,
    FrpgNetMan = 31,
    LuaConsoleServer = 32,
    RmiMan = 33,
    ResMan = 34,
    SfxDebugger = 35,
    RemoteMan = 36,
    DarkSight = 37,
    InGameDebugViewer = 38,
    ScaleformStep = 39,
    LocationStep = 40,
    DamageStep = 41,
    HavokWorldUpdatePre = 42,
    HavokWorldUpdatePost = 43,
    HavokClothUpdatePreAddRemoveRigidBody = 44,
    HavokClothUpdatePreClothModelIns = 45,
    HavokClothUpdatePreClothModelInsThread = 46,
    HavokClothUpdatePreClothManager = 47,
    CameraStep = 48,
    DrawParamUpdate = 49,
    NetworkClient = 50,
    GetNpAuthCode = 51,
    SoundStep = 52,
    HavokClothUpdatePostClothManager = 53,
    HavokClothUpdatePostClothModelIns = 54,
    HavokClothUpdatePostClothModelInsThread = 55,
    HavokClothUpdatePostUpdateVertex = 56,
    HavokClothVertexUpdateFinishWait = 57,
    GraphicsStep = 58,
    DrawSafeArea = 59,
    DebugDrawMemoryBar = 60,
    DbgMenuStep = 61,
    DbgRemoteStep = 62,
    PlaylogSystemStep = 63,
    ReviewMan = 64,
    ReportSystemStep = 65,
    DbgDispStep = 66,
    CsEzWorkDeleteList = 67,
    DrawStep = 68,
    DrawBegin = 69,
    GameSceneDraw = 70,
    AdhocDraw = 71,
    DrawEnd = 72,
    Flip = 73,
    DelayDeleteStep = 74,
    FrameEnd = 75,
};

inline constexpr const char* SPRJ_TASK_GROUP_NAMES[SPRJ_TASK_GROUP_COUNT] = {
    "FrameBegin",
    "SystemStep",
    "FileStep",
    "ResStep",
    "PadStep",
    "ObjResUpdate",
    "DungeonGateUpdate",
    "GameFlowStep",
    "MenuMan",
    "GameMan",
    "TaskLineIdx_Sys",
    "TaskLineIdx_Test",
    "TaskLineIdx_NetworkFlowStep",
    "TaskLineIdx_InGame_InGameStep",
    "TaskLineIdx_InGame_InGameStayStep",
    "TaskLineIdx_InGame_RemoStep",
    "TaskLineIdx_InGame_RemoManStep",
    "RemoStep",
    "TaskLineIdx_InGame_MoveMapStep",
    "TaskLineIdx_InGame_FieldAreaStep",
    "TaskLineIdx_InGame_TestNetStep",
    "TaskLineIdx_InGame_InGameMenuStep",
    "TaskLineIdx_InGame_TitleMenuStep",
    "TaskLineIdx_InGame_CommonMenuStep",
    "TaskLineIdx_FrpgNet_Sys",
    "TaskLineIdx_FrpgNet_Lobby",
    "TaskLineIdx_FrpgNet_ConnectMan",
    "TaskLineIdx_FrpgNet_Connect",
    "TaskLineIdx_FrpgNet_Other",
    "SfxMan",
    "FaceGenMan",
    "FrpgNetMan",
    "LuaConsoleServer",
    "RmiMan",
    "ResMan",
    "SfxDebugger",
    "REMOTEMAN",
    "DarkSight",
    "InGameDebugViewer",
    "ScaleformStep",
    "LocationStep",
    "DamageStep",
    "HavokWorldUpdate_Pre",
    "HavokWorldUpdate_Post",
    "HavokClothUpdate_Pre_AddRemoveRigidBody",
    "HavokClothUpdate_Pre_ClothModelIns",
    "HavokClothUpdate_Pre_ClothModelIns_Thread",
    "HavokClothUpdate_Pre_ClothManager",
    "CameraStep",
    "DrawParamUpdate",
    "NetworkClient",
    "GetNPAuthCode",
    "SoundStep",
    "HavokClothUpdate_Post_ClothManager",
    "HavokClothUpdate_Post_ClothModelIns",
    "HavokClothUpdate_Post_ClothModelIns_Thread",
    "HavokClothUpdate_Post_UpdateVertex",
    "HavokClothVertexUpdateFinishWait",
    "GraphicsStep",
    "DrawSafeArea",
    "DebugDrawMemoryBar",
    "DbgMenuStep",
    "DbgRemoteStep",
    "PlaylogSystemStep",
    "ReviewMan",
    "ReportSystemStep",
    "DbgDispStep",
    "CSEzWork_DeleteList",
    "DrawStep",
    "DrawBegin",
    "GameSceneDraw",
    "AdhocDraw",
    "DrawEnd",
    "Flip",
    "DelayDeleteStep",
    "FrameEnd",
};

// The native name of a timeline index (nullptr past the table).
constexpr const char* native_name(SprjTaskGroupIndex group) {
    return static_cast<std::size_t>(group) < SPRJ_TASK_GROUP_COUNT
               ? SPRJ_TASK_GROUP_NAMES[static_cast<std::size_t>(group)]
               : nullptr;
}

// Groups 0x2d and 0x36 receive a native special-registration flag.
constexpr bool uses_special_registration(SprjTaskGroupIndex group) {
    return group == SprjTaskGroupIndex::HavokClothUpdatePreClothModelIns ||
           group == SprjTaskGroupIndex::HavokClothUpdatePostClothModelIns;
}

// Bloodborne task object. Its concrete body is still opaque.
struct SprjTask;

// Singleton named SprjTask.
struct SprjTaskImp {
    void* vftable;
    SprjTask* inner;

    static constexpr Rva SINGLETON_PTR = SPRJ_TASK_SINGLETON_PTR;
    static constexpr Rva REGISTER_TASK_FN = SPRJ_TASK_REGISTER_FN;
    static constexpr Rva SUBMIT_REGISTRATION_FN = SPRJ_TASK_SUBMIT_REGISTRATION_FN;
    static constexpr Rva UNREGISTER_TASK_FN = SPRJ_TASK_UNREGISTER_FN;
    static constexpr Rva BUILD_GROUPS_FN = SPRJ_TASK_GROUPS_CONSTRUCT_FN;
};

// Task-line group entry referenced by SprjTaskGroup. The name prefix mirrors
// DS3/Sekiro-era SprjTimeLineTaskGroupIns, while the field at +0x5c is copied
// into FD4 task-registration data by RVA 0x2051300.
struct SprjTimeLineTaskGroupIns {
    void* vftable;
    FD4BasicHashString name;
    Unknown<0x14> _unk48;
    std::uint32_t registration_flags;
};

// Singleton named SprjTaskGroup. Bloodborne 1.09 reads task-group pointers
// from SprjTaskGroup + 0x08 + index * 0x08 after validating index < 0x4c.
struct SprjTaskGroup {
    void* vftable;
    SprjTimeLineTaskGroupIns* task_groups[SPRJ_TASK_GROUP_COUNT];

    // The group at the exact index used by the registration helper, or null.
    SprjTimeLineTaskGroupIns* task_group(std::size_t index) const {
        return index < SPRJ_TASK_GROUP_COUNT ? task_groups[index] : nullptr;
    }
    SprjTimeLineTaskGroupIns* task_group(SprjTaskGroupIndex index) const {
        return task_group(static_cast<std::size_t>(index));
    }
    // fn(SprjTimeLineTaskGroupIns&) for every present group.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t i = 0; i < SPRJ_TASK_GROUP_COUNT; ++i) {
            if (SprjTimeLineTaskGroupIns* g = task_groups[i]) fn(*g);
        }
    }
};

// Descriptive 0xc8 SprjStepLocal prefix verified in the SOS search and summon
// request steps. Constructors at RVAs 0x1e5f6f0 and 0x14bef90 and dispatchers
// 0x1e62760 and 0x14bd790 establish these offsets independently of the larger
// SprjStepTask prefix below.
//
// Dispatch commits requested_step into current_step before/after a callback;
// current -1 is terminal. Continuation permits another callback in the same
// update, bounded at 128 iterations. Callback rows contain encoded member
// functions, this-adjustments, and labels. Debug fields are conditional and
// may be stale. This prefix owns native dispatcher/debug storage, with no
// inline registration task.
struct SprjStepLocalC8 {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;

    constexpr bool is_finished() const { return current_step == -1; }
};

// Descriptive view of the 0xd0 SprjStepTask prefix verified in the enter/leave
// director and its child tasks. Not SprjStepLocal's 0xc8 prefix. Constructors
// initialize current/requested to zero and debug step to -1; the registration
// holder lives outside this prefix.
//
// Dispatch copies requested into current before and after a callback. Current
// -1 is the native completion predicate; a pending request of -1 alone has not
// yet passed through dispatch. The continuation byte permits another callback
// in the same update, with a native limit of 128 iterations. Debug
// labels/counts are conditional diagnostics and can be stale.
//
// Constructor/dispatch evidence: RVAs 0x196f070, 0x196f9f0, 0x196de70, and
// 0x1971f30. Native allocations own the dispatcher and debug storage.
struct SprjStepTaskD0 {
    const void* vftable;
    // Initialized to zero by task-base constructor RVA 0xfe5c10. Further
    // meaning has not been established for this view.
    std::uint32_t task_state_raw;
    Unknown<4> _unk0c;
    const void* callback_table;
    Unknown<0x40> _dispatcher18;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk61;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk72;
    void* debug_menu;
    Unknown<0x38> _debug_string80;
    std::int32_t* execution_counts;
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc9;
    std::int32_t debug_step;

    constexpr bool is_finished() const { return current_step == -1; }
};

// Descriptive 0x18 holder used by the enter/leave director and child nodes.
// Its pointer is the task itself. Registering replaces any previous task by
// unregistering it, then submits the new task to SprjTask. Release delegates
// to FD4TaskManager_UnregisterTask and clears the pointer and state byte. This
// does not establish when the manager destroys the task allocation.
template <typename T>
struct SprjTaskHolder {
    const void* vftable;
    T* task;
    // Copied by node insertion; cleared by register/release. Other meanings
    // have not been established.
    std::uint8_t state_raw;
    Unknown<7> _pad11;

    static constexpr Rva REGISTER_FN{0x204f940};
    static constexpr Rva RELEASE_FN{0x204fc30};
    static constexpr Rva DESTRUCTOR_FN{0x204fb50};
};

// Seven-slot vtable used by the observed 0x30 and 0x38 callback tasks.
struct SprjCallbackTaskVTable {
    const void* get_runtime_class;
    const void* destructor;
    const void* deleting_destructor;
    const void* execute_dispatch;
    const void* invoke_callback;
    const void* set_group;
    const void* unregister;
};

struct SprjTaskRegistration;

// Native-compatible callback task used throughout Bloodborne. The task owns a
// native registration handle at +0x10. Execution reaches vtable slot +0x18;
// the standard dispatcher then invokes callback with owner and the native
// task-data pointer. Native task destructors unregister registration before
// releasing the owner, so injected tasks must preserve that lifetime order.
struct SprjCallbackTask {
    const SprjCallbackTaskVTable* vftable;
    std::uint64_t _unk08;
    SprjTaskRegistration* registration;
    void* owner;
    const void* callback;
    std::intptr_t this_adjustment;

    static constexpr Rva REGISTER_FN = SPRJ_TASK_REGISTER_FN;
    static constexpr Rva SET_GROUP_FN = SPRJ_TASK_SET_GROUP_FN;
    static constexpr Rva UNREGISTER_FN = SPRJ_TASK_BASE_UNREGISTER_FN;
    static constexpr Rva EXECUTE_DISPATCH_FN = SPRJ_CALLBACK_TASK_EXECUTE_DISPATCH_FN;
    static constexpr Rva INVOKE_MEMBER_FN = SPRJ_CALLBACK_TASK_INVOKE_MEMBER_FN;
};

// Larger callback-task variant embedded by CSClothThread and CSEzWork. The
// descriptive name does not claim a distinct reflected native class. The
// constructor at RVA 0x1e39110 initializes the extra word at +0x18 to zero.
// CSMovie/CSMovieIns, CSNetworkFlowStep, SprjDungeonGateIns, and SprjDarkSight
// use it as a rearm flag with conditional dispatcher RVA 0x159da40: zero
// unregisters; nonzero clears the word and invokes the member callback, which
// can set it again to retain the next update. Group setter 0x159da80 arms
// before registering or reusing the same group. This policy is specific to
// those vtable slots, not every 0x38 task. The member invoker at RVA 0x1e39d20
// reads owner/callback/adjustment at +0x20/+0x28/+0x30, unlike the 0x30
// variant. The low bit of callback may encode a virtual member-function
// offset; treating it unconditionally as a callable pointer is incorrect.
struct SprjCallbackTask38 {
    const SprjCallbackTaskVTable* vftable;
    std::uint64_t _unk08;
    SprjTaskRegistration* registration;
    std::uint32_t value_18;
    std::uint32_t _pad1c;
    void* owner;
    const void* callback;
    std::intptr_t this_adjustment;

    static constexpr Rva CONDITIONAL_EXECUTE_FN{0x159da40};
    static constexpr Rva ARM_AND_SET_GROUP_FN{0x159da80};
};

// Temporary registration node allocated by the task registration helper at
// RVA 0x2050490.
struct SprjTaskRegistration {
    void* vftable;
    std::uint64_t _unk08;
    void* task;
    std::uint32_t group_index;
    std::uint32_t _pad1c;

    static constexpr Rva EXECUTE_FN = SPRJ_TASK_REGISTRATION_EXECUTE_FN;
};

// The singleton pointers that must all be live before task registration.
struct SprjTaskRuntimeSingletons {
    static constexpr Rva SPRJ_TASK = SPRJ_TASK_SINGLETON_PTR;
    static constexpr Rva SPRJ_TASK_GROUP = SPRJ_TASK_GROUP_SINGLETON_PTR;
    static constexpr Rva FD4_TASK_MANAGER = FD4_TASK_MANAGER_SINGLETON_PTR;
};

// Shape of the callback reached after native registration dispatch.
using SprjTaskCallback = void (BB_GAME_ABI*)(void*, const FD4TaskDataPrefix*);

namespace detail::task_layout {
using Holder = SprjTaskHolder<SprjStepTaskD0>;
BB_SIZE(SprjStepLocalC8, 0xc8);
static_assert(alignof(SprjStepLocalC8) == 8, "alignof(SprjStepLocalC8)");
BB_OFFSET(SprjStepLocalC8, callback_table, 0x08);
BB_OFFSET(SprjStepLocalC8, _condition_dispatcher10, 0x10);
BB_OFFSET(SprjStepLocalC8, current_step, 0x50);
BB_OFFSET(SprjStepLocalC8, requested_step, 0x54);
BB_OFFSET(SprjStepLocalC8, continue_this_update, 0x58);
BB_OFFSET(SprjStepLocalC8, allocator, 0x60);
BB_OFFSET(SprjStepLocalC8, debug_flags, 0x68);
BB_OFFSET(SprjStepLocalC8, debug_menu, 0x70);
BB_OFFSET(SprjStepLocalC8, _debug_string78, 0x78);
BB_OFFSET(SprjStepLocalC8, execution_counts, 0xb0);
BB_OFFSET(SprjStepLocalC8, execution_label, 0xb8);
BB_OFFSET(SprjStepLocalC8, debug_step_requested, 0xc0);
BB_OFFSET(SprjStepLocalC8, debug_step, 0xc4);
BB_SIZE(SprjStepTaskD0, 0xd0);
static_assert(alignof(SprjStepTaskD0) == 8, "alignof(SprjStepTaskD0)");
BB_OFFSET(SprjStepTaskD0, task_state_raw, 0x08);
BB_OFFSET(SprjStepTaskD0, callback_table, 0x10);
BB_OFFSET(SprjStepTaskD0, _dispatcher18, 0x18);
BB_OFFSET(SprjStepTaskD0, current_step, 0x58);
BB_OFFSET(SprjStepTaskD0, requested_step, 0x5c);
BB_OFFSET(SprjStepTaskD0, continue_this_update, 0x60);
BB_OFFSET(SprjStepTaskD0, allocator, 0x68);
BB_OFFSET(SprjStepTaskD0, debug_flags, 0x70);
BB_OFFSET(SprjStepTaskD0, debug_menu, 0x78);
BB_OFFSET(SprjStepTaskD0, _debug_string80, 0x80);
BB_OFFSET(SprjStepTaskD0, execution_counts, 0xb8);
BB_OFFSET(SprjStepTaskD0, execution_label, 0xc0);
BB_OFFSET(SprjStepTaskD0, debug_step_requested, 0xc8);
BB_OFFSET(SprjStepTaskD0, debug_step, 0xcc);
BB_SIZE(Holder, 0x18);
static_assert(alignof(Holder) == 8, "alignof(SprjTaskHolder)");
BB_OFFSET(Holder, task, 0x08);
BB_OFFSET(Holder, state_raw, 0x10);
BB_SIZE(SprjTaskImp, SPRJ_TASK_IMP_SIZE);
BB_SIZE(SprjTaskGroup, SPRJ_TASK_GROUP_SIZE);
static_assert(SPRJ_TASK_GROUP_COUNT == 0x4c, "SPRJ_TASK_GROUP_COUNT");
BB_OFFSET(SprjTaskImp, inner, 0x08);
BB_OFFSET(SprjTaskGroup, task_groups, 0x08);
BB_OFFSET(SprjTimeLineTaskGroupIns, name, 0x08);
BB_OFFSET(SprjTimeLineTaskGroupIns, registration_flags, 0x5c);
BB_SIZE(SprjTimeLineTaskGroupIns, SPRJ_TIME_LINE_TASK_GROUP_INS_PREFIX_SIZE);
BB_SIZE(SprjTaskRegistration, SPRJ_TASK_REGISTRATION_SIZE);
BB_SIZE(SprjCallbackTask, SPRJ_CALLBACK_TASK_SIZE);
BB_SIZE(SprjCallbackTask38, 0x38);
BB_OFFSET(SprjCallbackTask38, registration, 0x10);
BB_OFFSET(SprjCallbackTask38, value_18, 0x18);
BB_OFFSET(SprjCallbackTask38, owner, 0x20);
BB_OFFSET(SprjCallbackTask38, callback, 0x28);
BB_OFFSET(SprjCallbackTask38, this_adjustment, 0x30);
BB_SIZE(SprjCallbackTaskVTable, SPRJ_CALLBACK_TASK_VTABLE_SIZE);
BB_OFFSET(SprjTaskRegistration, task, 0x10);
BB_OFFSET(SprjTaskRegistration, group_index, 0x18);
BB_OFFSET(SprjCallbackTask, registration, 0x10);
BB_OFFSET(SprjCallbackTask, owner, 0x18);
BB_OFFSET(SprjCallbackTask, callback, 0x20);
BB_OFFSET(SprjCallbackTask, this_adjustment, 0x28);
BB_OFFSET(SprjCallbackTaskVTable, execute_dispatch, 0x18);
BB_OFFSET(SprjCallbackTaskVTable, invoke_callback, 0x20);
BB_OFFSET(SprjCallbackTaskVTable, set_group, 0x28);
BB_OFFSET(SprjCallbackTaskVTable, unregister, 0x30);
static_assert(static_cast<std::size_t>(SprjTaskGroupIndex::FrameEnd) + 1 == SPRJ_TASK_GROUP_COUNT,
              "SprjTaskGroupIndex covers the table");
}  // namespace detail::task_layout

}  // namespace bb
