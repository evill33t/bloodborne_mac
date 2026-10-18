// Work executors owned by CS thread classes and the shared CSEzWorkPool.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

inline constexpr std::size_t CS_EZ_WORK_SIZE = 0xe8;
inline constexpr Rva CS_EZ_WORK_CONSTRUCTOR_FN{0x20313e0};
inline constexpr Rva CS_EZ_WORK_DESTRUCTOR_FN{0x2031cf0};
inline constexpr Rva CS_EZ_WORK_VTABLE{0x535a870};
inline constexpr Rva CS_EZ_WORK_DELETE_COMPLETED_FN{0x2031be0};
// Locks the pending list, appends a fragment, and wakes the worker threads.
inline constexpr Rva CS_EZ_WORK_ENQUEUE_FN{0x20320b0};
// Sends a work message to every worker after callers have queued fragments.
inline constexpr Rva CS_EZ_WORK_WAKE_WORKERS_FN{0x2032200};
// Pops one fragment under the pending mutex, executes it, handles completion.
inline constexpr Rva CS_EZ_WORK_PROCESS_ONE_FN{0x2032370};
// Raw list append helper; callers hold the executor's pending mutex.
inline constexpr Rva CS_EZ_WORK_LIST_APPEND_FN{0x2032780};
inline constexpr Rva CS_EZ_WORK_CREATE_COMPLETION_FN{0x2032f40};

struct CSEzWorkListNode {
    CSEzWorkListNode* next;
    CSEzWorkListNode* previous;
    // A work fragment or worker according to the owning list.
    void* payload;
};

// Observed 0x20 list header. The sentinel has the same 0x18 node stride; its
// payload is not an element. Empty lists point next/previous to themselves.
struct CSEzWorkList {
    Unknown<8> _unk00;
    CSEzWorkListNode* sentinel;
    std::size_t count;
    void* allocator;
};

// PS4 adaptive mutex embedded by the work executor at +0x30 and +0x70, and by
// SprjFD4Location at +0x10. Constructor RVA 0x20313e0 writes vtable RVA
// 0x53a19b0, spin count 200000, creates a semaphore at +0x10, and clears the
// word at +0x18. Source-path assertion: CSPlainAdaptiveMutexImp_sce.cpp.
struct CSPlainAdaptiveMutexImp {
    const void* vftable;
    std::uint32_t spin_count;
    std::uint32_t _pad0c;
    void* semaphore;
    Unknown<8> _unk18;
};

// Native work executor, allocated as 0xe8 bytes with alignment 8 by
// CSChrThread, CSClothThread, and CSEzWorkPool.
//
// Identity follows the constructor's default CSEzWork name and source-path
// strings, not a recovered runtime-class registration. Three sentinel lists
// track queued fragments, fragments awaiting deletion, and workers. The
// deletion callback at +0x90 targets this object, so a live instance must stay
// at its original address. Native teardown stops and joins all workers before
// destroying remaining fragments, list storage, the callback registration, and
// both mutexes. These fields are accessed concurrently by native code.
struct CSEzWork {
    const void* vftable;
    // Constructor default is 2; character back-initialization uses 1. The
    // complete mode enumeration is not established.
    std::uint32_t mode;
    std::uint32_t _pad0c;
    CSEzWorkList pending;
    CSPlainAdaptiveMutexImp pending_mutex;
    CSEzWorkList completed;
    CSPlainAdaptiveMutexImp completed_mutex;
    SprjCallbackTask38 delete_completed_task;
    // Payloads point to separately allocated CSEzWorkWorker objects.
    CSEzWorkList workers;
};

// Descriptive constructor-options layout, built in 0x20-byte stack slots by
// CSEzWorkPool initialization; not a recovered runtime-class name. The executor
// consumes these synchronously, retaining no pointer to the options or name.
// Names are copied/formatted for each worker.
struct CSEzWorkOptions {
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x535a890};

    const void* vftable;
    // Null selects the native default name CSEzWork.
    const std::uint16_t* name;
    // Native DLThreadHandle priority in 0..=6, passed to RVA 0x263b550. Not a
    // worker count. Pool A/B/C use 6; D uses 2.
    std::uint32_t priority;
    // Copied to each worker's +0xa0 byte. All four shared pool options use 0.
    std::uint8_t initialize_havok_memory;
    Unknown<3> _unk15;
    // Selects a six-slot affinity-mask expansion. Only nonzero masks create
    // workers; the raw policy is preserved because other callers use it too.
    std::uint32_t affinity_policy;
    Unknown<4> _unk1c;
};

// Descriptive name for each 0xa8-byte worker allocation (alignment eight).
// Constructor RVA 0x2033c20 stores its executor, affinity mask, owned thread,
// and optional Havok-memory flag. No reflected worker class is claimed.
//
// Vtable slot +0x00 runs the message loop: message 0 initializes optional
// Havok memory, 1 drains the executor, and 2 finalizes memory and exits.
// Destructor slot +0x08 sends message 2, joins the thread, frees its 0xc8-byte
// object, then tears down the embedded memory storage. Native thread state
// refers back to this worker, so a live worker must not move.
struct CSEzWorkWorker {
    static constexpr std::size_t SIZE = 0xa8;
    static constexpr Rva INSTANCE_VTABLE{0x535a8e0};
    static constexpr Rva CONSTRUCTOR_FN{0x2033c20};
    static constexpr Rva DESTRUCTOR_FN{0x2033ea0};
    static constexpr Rva RUN_FN{0x2034060};

    const void* vftable;
    // Borrowed; the executor owns this worker through its worker list.
    CSEzWork* executor;
    // Forwarded to native thread creation, which applies the low six bits.
    std::uint64_t affinity_mask;
    // Separate native thread allocation, constructed by RVA 0x208afe0.
    Unknown<0xc8>* thread;
    // Initialized by RVA 0x429970 and registered as CSEzWorkHkThreadMemory
    // when enabled. The internal Havok layout is not inferred here.
    Unknown<0x80> _havok_memory20;
    std::uint8_t initialize_havok_memory;
    Unknown<7> _unk_a1;
};

struct CSEzWorkCompletion;

// Descriptive 0x30-byte member-callback fragment allocated by CSChrThread and
// CSLamsCaptureThread, among the captured consumers. Separate from the
// reflected 0x20-byte CSChrUpdatePre/PostFragment classes. Submission retains
// completion and increments its pending count, then appends under the
// executor's pending mutex. Submissions do not wake workers individually: the
// owner wakes the batch and waits separately. The target is borrowed.
struct CSEzWorkMemberFragment {
    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva CHARACTER_VTABLE{0x5335b60};
    static constexpr Rva TARGET_BANK_VTABLE{0x5335b30};
    static constexpr Rva WORLD_AI_VTABLE{0x5335b00};
    static constexpr Rva LAMS_VTABLE{0x5357e90};
    static constexpr Rva INVOKE_CHARACTER_FN{0x1a65f80};
    static constexpr Rva INVOKE_TARGET_BANK_FN{0x1a65e60};
    static constexpr Rva INVOKE_WORLD_AI_FN{0x1a65d40};
    static constexpr Rva INVOKE_LAMS_FN{0x1fab030};

    const void* vftable;
    CSEzWorkCompletion* completion;
    // All observed character-owner submissions use 0: destroy after execution.
    std::uint32_t cleanup_mode;
    Unknown<4> _unk14;
    void* target;
    // Native member-pointer encoding, not an RVA. When bit 0 is set, this value
    // minus one is a byte offset in the adjusted target's vtable; otherwise it
    // holds a native function pointer.
    std::uintptr_t member_function_bits;
    // Added to target before resolving and invoking the member function.
    std::intptr_t this_adjustment;
};

// Separately allocated eight-byte holder owned by each CS thread owner. Its
// pointer retains a completion object. Destruction waits for completion,
// decrements the native reference count, then frees the holder.
struct CSEzWorkCompletionHolder {
    CSEzWorkCompletion* completion;
};

// Descriptive name for the 0x38 completion object allocated by RVA 0x2032f40;
// no runtime-class name established. Counts are natively atomic integers.
struct CSEzWorkCompletion {
    const void* vftable;
    std::int32_t reference_count;
    std::uint32_t _pad0c;
    // SprjThreadEvent-compatible subobject; internals opaque.
    Unknown<0x20> _event10;
    std::int32_t pending_count;
    std::uint32_t _pad34;
};

namespace detail::ez_work_layout {
BB_SIZE(CSEzWork, CS_EZ_WORK_SIZE);
static_assert(alignof(CSEzWork) == 8, "alignof(CSEzWork)");
BB_OFFSET(CSEzWork, mode, 0x08);
BB_OFFSET(CSEzWork, pending, 0x10);
BB_OFFSET(CSEzWork, pending_mutex, 0x30);
BB_OFFSET(CSEzWork, completed, 0x50);
BB_OFFSET(CSEzWork, completed_mutex, 0x70);
BB_OFFSET(CSEzWork, delete_completed_task, 0x90);
BB_OFFSET(CSEzWork, workers, 0xc8);
BB_SIZE(CSEzWorkList, 0x20);
BB_OFFSET(CSEzWorkList, sentinel, 0x08);
BB_OFFSET(CSEzWorkList, count, 0x10);
BB_OFFSET(CSEzWorkList, allocator, 0x18);
BB_SIZE(CSEzWorkListNode, 0x18);
BB_OFFSET(CSEzWorkListNode, payload, 0x10);
BB_SIZE(CSPlainAdaptiveMutexImp, 0x20);
BB_OFFSET(CSPlainAdaptiveMutexImp, spin_count, 0x08);
BB_OFFSET(CSPlainAdaptiveMutexImp, semaphore, 0x10);
BB_SIZE(CSEzWorkCompletionHolder, 0x08);
BB_SIZE(CSEzWorkCompletion, 0x38);
BB_OFFSET(CSEzWorkCompletion, reference_count, 0x08);
BB_OFFSET(CSEzWorkCompletion, _event10, 0x10);
BB_OFFSET(CSEzWorkCompletion, pending_count, 0x30);
BB_SIZE(CSEzWorkOptions, CSEzWorkOptions::SIZE);
static_assert(alignof(CSEzWorkOptions) == 8, "alignof(CSEzWorkOptions)");
BB_OFFSET(CSEzWorkOptions, name, 0x08);
BB_OFFSET(CSEzWorkOptions, priority, 0x10);
BB_OFFSET(CSEzWorkOptions, initialize_havok_memory, 0x14);
BB_OFFSET(CSEzWorkOptions, affinity_policy, 0x18);
BB_SIZE(CSEzWorkWorker, CSEzWorkWorker::SIZE);
static_assert(alignof(CSEzWorkWorker) == 8, "alignof(CSEzWorkWorker)");
BB_OFFSET(CSEzWorkWorker, executor, 0x08);
BB_OFFSET(CSEzWorkWorker, affinity_mask, 0x10);
BB_OFFSET(CSEzWorkWorker, thread, 0x18);
BB_OFFSET(CSEzWorkWorker, _havok_memory20, 0x20);
BB_OFFSET(CSEzWorkWorker, initialize_havok_memory, 0xa0);
BB_SIZE(CSEzWorkMemberFragment, CSEzWorkMemberFragment::SIZE);
static_assert(alignof(CSEzWorkMemberFragment) == 8, "alignof(CSEzWorkMemberFragment)");
BB_OFFSET(CSEzWorkMemberFragment, completion, 0x08);
BB_OFFSET(CSEzWorkMemberFragment, cleanup_mode, 0x10);
BB_OFFSET(CSEzWorkMemberFragment, target, 0x18);
BB_OFFSET(CSEzWorkMemberFragment, member_function_bits, 0x20);
BB_OFFSET(CSEzWorkMemberFragment, this_adjustment, 0x28);
}  // namespace detail::ez_work_layout

}  // namespace bb
