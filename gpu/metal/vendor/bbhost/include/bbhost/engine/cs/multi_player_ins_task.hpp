// CSMultiPlayerInsTask: the native multiplayer actor lifecycle, its local-step
// layout, and its deletion timer.
//
// The eboot confirms that STEP_DeleteWait conditionally removes the matching
// WorldChr actor only for mode 1 when lifecycle bit 0x10 is set. Mode 0 can
// request leave presentation in STEP_Delete, but skips that delayed-removal
// branch. Mode 2 skips both.
//
// Identity enters this layer as a native ObjectRef from WorldSessionObjectMan.
// CSMultiPlayMan owns the task pointer; this task owns the ordered native
// actor lifecycle. It does not own Matching2 room membership, NPID records,
// or WorldSession packet routing.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_CTOR_FN{0x1e4f9f0};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_INIT_FN{0x1e4fcd0};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_START_MULTI_PLAY_NOTIFY_WAIT_FN{0x1e4fcf0};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_START_MULTI_PLAY_WAIT_FN{0x1e4fd50};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_FIRST_SYNC_COMPLETE_WAIT_FN{0x1e4ff70};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_CREATE_REMOTE_WORLD_CHR_FN{0x1e50090};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_CREATE_WAIT_FN{0x1e505e0};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_UPDATE_FN{0x1e50830};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_WAIT_EXIT_MULTI_PLAY_FN{0x1e50860};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_DELETE_FN{0x1e50880};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_DELETE_WAIT_FN{0x1e50a70};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_STEP_FINISH_FN{0x1e50d70};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_VTABLE{0x534f650};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_SIZE_GETTER_FN{0x1e538e0};
// Virtual +8, thunk to destructor body RVA 0x1e54340.
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_DESTRUCTOR_FN{0x1e52510};
// Virtual +0xd0, thunk to DISPATCH_FN.
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_EXECUTE_FN{0x1e53320};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_DISPATCH_FN{0x1e52fe0};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_IS_FINISHED_FN{0x1e52550};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_ADVANCE_STEP_FN{0x1e54250};
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_REQUEST_CONTINUATION_FN{0x1e52660};
// Registration initializes this mutable float global to 20.0; construction copies it.
inline constexpr Rva CS_MULTI_PLAYER_INS_TASK_DELETE_TIME_SETTING{0x558ef48};

inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_SIZE = 0x100;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_OBJECT_REF_OFFSET = 0xc8;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_MODE_OFFSET = 0xe8;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_ENTERLEAVE_MODE_OFFSET = 0xec;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_LIFECYCLE_FLAGS_OFFSET = 0xed;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_CALLBACK_TABLE_OFFSET = 0x08;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_OFFSET = 0xf0;
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_VALUE_OFFSET = 0xf8;
// Legacy misnomer: this offset holds FD4Time, not the callback table at +8.
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_STEP_TABLE_OFFSET = CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_OFFSET;
// Legacy misnomer: this offset holds a float countdown, not an integer table value.
inline constexpr std::size_t CS_MULTI_PLAYER_INS_TASK_STEP_TABLE_VALUE_OFFSET =
    CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_VALUE_OFFSET;

inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_EXIT_REQUESTED = 0x02;
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_MEMBER_READY = 0x04;
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_FIRST_SYNC_READY = 0x08;
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_REMOVE_WORLD_CHR = 0x10;
// Mode 0 StartMultiPlayWait skips copying the source map/transform into the
// active transition fields when this bit is set (RVA 0x1e4fd50).
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_SKIP_MODE0_TRANSITION_COPY = 0x20;
// Compatibility spelling for SKIP_MODE0_TRANSITION_COPY.
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_ENTERLEAVE_FLAG_20 =
    CS_MULTI_PLAYER_INS_TASK_SKIP_MODE0_TRANSITION_COPY;
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_CREATE_REQUESTED = 0x01;
// Bits with confirmed task-lifecycle meaning in Bloodborne 1.09.
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_DEFINED_FLAGS_MASK = 0x3f;
// High bits preserved, rather than initialized, by the native constructor. No
// step gives them gameplay meaning; allocator reuse can make otherwise
// equivalent fresh tasks report values such as 0x05 and 0x85, so do not
// classify lifecycle state from these bits.
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_CTOR_PRESERVED_FLAGS_MASK = 0xc0;
inline constexpr std::uint8_t CS_MULTI_PLAYER_INS_TASK_CREATE_READY = CS_MULTI_PLAYER_INS_TASK_CREATE_REQUESTED |
                                                                      CS_MULTI_PLAYER_INS_TASK_MEMBER_READY |
                                                                      CS_MULTI_PLAYER_INS_TASK_FIRST_SYNC_READY;

// Native step indices registered by CSMultiPlayerInsTask. Only Create
// materializes the remote WorldChr. A task retained in Update across a
// world-generation change does not revisit that operation.
enum class CSMultiPlayerInsTaskStep : std::int32_t {
    Init = 0,
    StartMultiPlayNotifyWait = 1,
    StartMultiPlayWait = 2,
    FirstSyncCompleteWait = 3,
    Create = 4,
    CreateWait = 5,
    Update = 6,
    WaitExitMultiPlay = 7,
    Delete = 8,
    DeleteWait = 9,
    Finish = 10,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSMultiPlayerInsTaskStep& out) {
    if (value < 0 || value > 10) return false;
    out = static_cast<CSMultiPlayerInsTaskStep>(value);
    return true;
}

// Locked ObjectRef wrapper copied into a multiplayer task. The wrapper is
// reconstructed through the native copy constructor; its inline lock bytes
// must not be copied as an opaque snapshot.
struct CSMultiPlayerTaskObjectRef {
    void* object;
    Unknown<0x18> _lock;
};

// Bloodborne 1.09 multiplayer-character task.
//
// Reflected size 0x100, allocated aligned to eight. The 0xc8 SprjStepLocal
// prefix is established by constructor and dispatcher accesses. state and
// local_state are stored as u32; they are the current and
// requested signed step indices, all bits set meaning -1. The dispatcher
// commits requested before/after callbacks and permits up to 128 callbacks per
// update through the continuation byte.
//
// CSMultiPlayMan owns and ticks this object through virtual +0xd0, then
// destroys/frees it when virtual +0x20 reports current step -1. Destruction
// resets the retained ObjectRef and tears down local-step/debug storage; it
// does not run DeleteWait or remove an actor.
//
// Reaching Update proves that native create steps advanced. Missing peer or
// GameData input can still permit advancement, so this does not prove an
// actor was created or completed draw-model, collision, ChrSet, or map binding.
struct CSMultiPlayerInsTask {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    // Current signed step, stored as u32 for compatibility. Use current_step().
    std::uint32_t state;
    // Requested signed step, stored as u32 for compatibility. Use requested_step().
    std::uint32_t local_state;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    // Refreshed only when debugging is enabled; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    CSMultiPlayerTaskObjectRef peer_object_ref;
    // Raw constructor input. Known ensure paths supply 0, 1, 2, or 3. Modes
    // 1/2 wait for first sync and can create a remote actor; mode 1 alone
    // permits DeleteWait's delayed actor removal.
    std::uint32_t mode;
    // Session-type byte forwarded to enter/leave presentation helpers.
    std::uint8_t enterleave_mode;
    std::uint8_t lifecycle_flags;
    Unknown<0x02> _tail;
    // FD4Time vtable at +0xf0, float at +0xf8. Constructor copies the 20.0
    // default; STEP_Delete subtracts context FD4Time +8 only while an eligible
    // leave-presentation task has not set its +0x135 byte.
    FD4Time delete_timer;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_MULTI_PLAYER_INS_TASK_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_MULTI_PLAYER_INS_TASK_TEMPLATE;
    static constexpr float DEFAULT_DELETE_TIME = 20.0f;

    // True and `out` set when the committed step names a registered step.
    bool current_step(CSMultiPlayerInsTaskStep& out) const {
        return from_raw(static_cast<std::int32_t>(state), out);
    }
    // True and `out` set when the requested step names a registered step.
    bool requested_step(CSMultiPlayerInsTaskStep& out) const {
        return from_raw(static_cast<std::int32_t>(local_state), out);
    }

    // Native completion tests the committed current word, not the request.
    constexpr bool is_finished() const { return state == UINT32_MAX; }

    // Mode/flag gate for DeleteWait's delayed actor-removal branch. Entering
    // that branch still requires resolving a matching actor in WorldChrMan.
    // STEP_Delete has a different, broader gate for leave presentation.
    constexpr bool uses_delayed_actor_removal() const {
        return mode == 1 && (lifecycle_flags & CS_MULTI_PLAYER_INS_TASK_REMOVE_WORLD_CHR) != 0;
    }

    // STEP_Delete's timer comparison after subtraction, excluding its mode,
    // actor, session, and presentation gates. VUCOMISS zero,value / JC waits
    // for positive values and NaN. Expiry writes +0 and advances.
    static bool delete_timer_waits(float remaining) { return remaining > 0.0f || remaining != remaining; }
};

namespace detail::multi_player_ins_task_layout {
using T = CSMultiPlayerInsTask;
BB_SIZE(CSMultiPlayerTaskObjectRef, 0x20);
BB_SIZE(T, CS_MULTI_PLAYER_INS_TASK_SIZE);
static_assert(alignof(T) == 8, "alignof(CSMultiPlayerInsTask)");
BB_OFFSET(T, peer_object_ref, CS_MULTI_PLAYER_INS_TASK_OBJECT_REF_OFFSET);
BB_OFFSET(T, mode, CS_MULTI_PLAYER_INS_TASK_MODE_OFFSET);
BB_OFFSET(T, enterleave_mode, CS_MULTI_PLAYER_INS_TASK_ENTERLEAVE_MODE_OFFSET);
BB_OFFSET(T, lifecycle_flags, CS_MULTI_PLAYER_INS_TASK_LIFECYCLE_FLAGS_OFFSET);
BB_OFFSET(T, callback_table, CS_MULTI_PLAYER_INS_TASK_CALLBACK_TABLE_OFFSET);
BB_OFFSET(T, delete_timer, CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_OFFSET);
BB_OFFSET(T, delete_timer.time, CS_MULTI_PLAYER_INS_TASK_DELETE_TIMER_VALUE_OFFSET);
static_assert(CS_MULTI_PLAYER_INS_TASK_CREATE_READY == 0x0d, "CREATE_READY");
static_assert((CS_MULTI_PLAYER_INS_TASK_DEFINED_FLAGS_MASK | CS_MULTI_PLAYER_INS_TASK_CTOR_PRESERVED_FLAGS_MASK) == 0xff,
              "flag masks");
BB_OFFSET(T, vftable, 0);
BB_OFFSET(T, _condition_dispatcher10, 0x10);
BB_OFFSET(T, state, 0x50);
BB_OFFSET(T, local_state, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _debug_string78, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
}  // namespace detail::multi_player_ins_task_layout

}  // namespace bb
