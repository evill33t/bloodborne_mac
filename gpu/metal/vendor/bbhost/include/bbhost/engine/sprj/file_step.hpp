// Native file-service lifecycle step and its allocation-limiter context.
//
// SprjFileStep is reflected. The limiter, record, and list names below are
// descriptive views recovered from their constructors and callback consumers.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct SprjFileRepository;
struct SprjFileAllocationNode;
struct SprjFileAllocationRecord;

enum class SprjFileStepIndex : std::int32_t {
    Init = 0,
    Update = 1,
    Finish = 2,
};

// Native fields can also contain -1 (finish requests termination): false for
// any value outside the three steps.
inline constexpr bool sprj_file_step_index_from_raw(std::int32_t value, SprjFileStepIndex* out) {
    if (value < 0 || value > 2) return false;
    *out = static_cast<SprjFileStepIndex>(value);
    return true;
}

// Descriptive list owner. Sentinel payload is not a record; count is the
// number of real nodes. The leading eight bytes remain uncharacterized.
struct SprjFileAllocationList {
    Unknown<8> _unk00;
    SprjFileAllocationNode* sentinel;
    std::size_t count;
    void* allocator;
};

struct SprjFileAllocationNode {
    SprjFileAllocationNode* next;
    SprjFileAllocationNode* previous;
    SprjFileAllocationRecord* record;
};

// Separately allocated 0x18-byte record. Identity is allocator virtual +0x10
// (displayed as heapId) plus byte_size, not an allocation pointer. Multiple
// identical records are possible; release selects the first matching record.
struct SprjFileAllocationRecord {
    std::int32_t heap_id;
    Unknown<4> _unk04;
    std::uint64_t byte_size;
    void* debug_node;
};

// Descriptive allocation-limiter context, allocated as 0x80 bytes aligned to
// eight by SprjFileStep::INIT_FN. Native debug caption is "ALLOC LIMITER"; no
// reflected class name is claimed. Constructor enables the limit and creates
// two independent lists with 0x18-byte sentinels.
//
// Both loader callbacks lock mutex. Admission checks cached totals, records a
// non-null allocator's heap ID/size, and recomputes totals from active.
// Release moves the first matching (heap ID, size) record to retired, removes
// its old list node, and recomputes totals. Update frees retired records and
// their debug attachments/nodes. Destruction drains both lists, removes the
// debug root, frees sentinels, then destroys the native mutex.
struct SprjFileAllocationLimiter {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::uint64_t LIMIT_BYTES = 0x02000000;
    static constexpr Rva VTABLE{0x53213a0};
    static constexpr Rva CONSTRUCTOR_FN{0x142ec90};
    static constexpr Rva DESTRUCTOR_FN{0x142eea0};
    static constexpr Rva ADMIT_ALLOCATION_FN{0x142fe70};
    static constexpr Rva RETIRE_ALLOCATION_FN{0x142f4f0};
    static constexpr Rva CLEAN_RETIRED_FN{0x142f260};
    static constexpr Rva BUILD_DEBUG_MENU_FN{0x142f640};

    const void* vftable;
    // Native 0x18-byte DLLightMutex; pthread handle at +0x10, flag at +0x18.
    Unknown<0x18> _mutex;
    std::uint8_t enabled;
    Unknown<7> _unk21;
    std::uint64_t tracked_bytes;
    std::size_t tracked_count;
    SprjFileAllocationList active;
    SprjFileAllocationList retired;
    void* debug_root;

    // Read-only snapshot of the native admission predicate, not a reservation
    // or synchronized native call. An empty active list permits the first
    // allocation regardless of size. Addition preserves native 64-bit wrap.
    bool allows_additional_bytes(std::uint64_t requested_bytes) const {
        return enabled == 0 || tracked_count == 0 || tracked_bytes + requested_bytes <= LIMIT_BYTES;
    }
};

// Reflected 0x118-byte task, allocated aligned to eight by RVA 0x201b0e0 and
// registered on FileStep (2). Init creates the limiter, separate SPRJ
// repository, and global file service, registers frame_begin_task on group
// zero, increments requested_step, and sets flag_60.
//
// Update cleans retired limiter records, updates the repository, applies its
// worker mode request, and sets CSPlaygo.flags[0] when any stream is nonempty.
// Finish unregisters frame_begin_task, clears the FD4 service alias, destroys
// the SPRJ service, repository, then limiter, and requests step -1. The C++
// destructor separately frees buffer_allocator and task/base state; it is not
// a replacement for running Finish.
struct SprjFileStep {
    static constexpr std::size_t SIZE = 0x118;
    static constexpr Rva NAME_STRING{0x492fbf0};
    static constexpr Rva NAME_STRING_UTF16{0x495209a};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FILE_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_FILE_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x5321220};
    static constexpr Rva CONSTRUCTOR_FN{0x142b950};
    static constexpr Rva DESTRUCTOR_FN{0x142bbe0};
    static constexpr Rva INIT_FN{0x142bcf0};
    static constexpr Rva UPDATE_FN{0x142c100};
    static constexpr Rva FINISH_FN{0x142c210};
    static constexpr Rva FRAME_BEGIN_FN{0x142bbb0};
    static constexpr Rva FRAME_BEGIN_TASK_VTABLE{0x536b240};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::FileStep;
    static constexpr SprjTaskGroupIndex FRAME_BEGIN_TASK_GROUP = SprjTaskGroupIndex::FrameBegin;

    const void* vftable;
    Unknown<8> _unk08;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t flag_60;
    Unknown<7> _unk61;
    void* allocator;
    // Destructor tests byte zero before native allocator cleanup.
    std::uint8_t allocator_flags[2];
    Unknown<6> _unk72;
    std::uint64_t value_78_bits;
    // Native UTF-16 string: storage +0x88, length +0x98, capacity +0xa0.
    Unknown<0x38> _string_storage;
    std::uint64_t value_b8_bits;
    // Constructor points at the native wide label "NotExecuting".
    const std::uint16_t* execution_label;
    std::uint8_t flag_c8;
    Unknown<3> _unkc9;
    std::int32_t value_cc;
    // Owned 0x18-byte allocator adapter; passed as repository seed +0x20.
    void* buffer_allocator;
    // Owned 0x80-byte context passed as repository seed +0x28.
    SprjFileAllocationLimiter* allocation_limiter;
    // Owned here; the file service borrows this repository.
    SprjFileRepository* repository;
    SprjCallbackTask frame_begin_task;
};

namespace detail::file_step_layout {
BB_SIZE(SprjFileStep, 0x118);
BB_SIZE(SprjFileStep, SprjFileStep::SIZE);
static_assert(alignof(SprjFileStep) == 8, "alignof(SprjFileStep)");
BB_OFFSET(SprjFileStep, callback_table, 0x10);
BB_OFFSET(SprjFileStep, _dispatcher, 0x18);
BB_OFFSET(SprjFileStep, current_step, 0x58);
BB_OFFSET(SprjFileStep, requested_step, 0x5c);
BB_OFFSET(SprjFileStep, flag_60, 0x60);
BB_OFFSET(SprjFileStep, allocator, 0x68);
BB_OFFSET(SprjFileStep, allocator_flags, 0x70);
BB_OFFSET(SprjFileStep, value_78_bits, 0x78);
BB_OFFSET(SprjFileStep, _string_storage, 0x80);
BB_OFFSET(SprjFileStep, value_b8_bits, 0xb8);
BB_OFFSET(SprjFileStep, execution_label, 0xc0);
BB_OFFSET(SprjFileStep, flag_c8, 0xc8);
BB_OFFSET(SprjFileStep, value_cc, 0xcc);
BB_OFFSET(SprjFileStep, buffer_allocator, 0xd0);
BB_OFFSET(SprjFileStep, allocation_limiter, 0xd8);
BB_OFFSET(SprjFileStep, repository, 0xe0);
BB_OFFSET(SprjFileStep, frame_begin_task, 0xe8);
static_assert(offsetof(SprjFileStep, frame_begin_task) + offsetof(SprjCallbackTask, registration) == 0xf8,
              "SprjFileStep::frame_begin_task.registration");
static_assert(offsetof(SprjFileStep, frame_begin_task) + offsetof(SprjCallbackTask, owner) == 0x100,
              "SprjFileStep::frame_begin_task.owner");
static_assert(offsetof(SprjFileStep, frame_begin_task) + offsetof(SprjCallbackTask, callback) == 0x108,
              "SprjFileStep::frame_begin_task.callback");
static_assert(offsetof(SprjFileStep, frame_begin_task) + offsetof(SprjCallbackTask, this_adjustment) == 0x110,
              "SprjFileStep::frame_begin_task.this_adjustment");
static_assert(static_cast<std::uint32_t>(SprjFileStep::TASK_GROUP) == 2, "TASK_GROUP");
static_assert(static_cast<std::uint32_t>(SprjFileStep::FRAME_BEGIN_TASK_GROUP) == 0, "FRAME_BEGIN_TASK_GROUP");

BB_SIZE(SprjFileAllocationLimiter, 0x80);
BB_SIZE(SprjFileAllocationLimiter, SprjFileAllocationLimiter::SIZE);
static_assert(alignof(SprjFileAllocationLimiter) == 8, "alignof(SprjFileAllocationLimiter)");
BB_OFFSET(SprjFileAllocationLimiter, _mutex, 8);
BB_OFFSET(SprjFileAllocationLimiter, enabled, 0x20);
BB_OFFSET(SprjFileAllocationLimiter, tracked_bytes, 0x28);
BB_OFFSET(SprjFileAllocationLimiter, tracked_count, 0x30);
BB_OFFSET(SprjFileAllocationLimiter, active, 0x38);
BB_OFFSET(SprjFileAllocationLimiter, retired, 0x58);
BB_OFFSET(SprjFileAllocationLimiter, debug_root, 0x78);
BB_SIZE(SprjFileAllocationList, 0x20);
static_assert(alignof(SprjFileAllocationList) == 8, "alignof(SprjFileAllocationList)");
BB_OFFSET(SprjFileAllocationList, sentinel, 8);
BB_OFFSET(SprjFileAllocationList, count, 0x10);
BB_OFFSET(SprjFileAllocationList, allocator, 0x18);
BB_SIZE(SprjFileAllocationNode, 0x18);
static_assert(alignof(SprjFileAllocationNode) == 8, "alignof(SprjFileAllocationNode)");
BB_OFFSET(SprjFileAllocationNode, previous, 8);
BB_OFFSET(SprjFileAllocationNode, record, 0x10);
BB_SIZE(SprjFileAllocationRecord, 0x18);
static_assert(alignof(SprjFileAllocationRecord) == 8, "alignof(SprjFileAllocationRecord)");
BB_OFFSET(SprjFileAllocationRecord, heap_id, 0);
BB_OFFSET(SprjFileAllocationRecord, byte_size, 8);
BB_OFFSET(SprjFileAllocationRecord, debug_node, 0x10);
}  // namespace detail::file_step_layout

}  // namespace bb
