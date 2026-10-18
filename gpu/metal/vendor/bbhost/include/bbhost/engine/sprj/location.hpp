// Location-update singleton, its segmented queues, and reflected owner step.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/cs/ez_work.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct LocationUpdateNode;
struct LocationQueueBlockHeader;

// Descriptive 0x50-byte queue descriptor, without a vtable. Each block has a
// 0x10-byte header followed by slots_per_block pointers. Push retains the
// queued object atomically; pop clears the slot and transfers that reference
// to the caller. Clear releases queued references.
//
// End cursors point at the last slot, not one past it. Exhausting a pooled
// block caches it in free_blocks; exhausting a heap fallback frees it.
// Equality of read_cursor and write_cursor is the native empty check.
struct LocationUpdateQueue {
    static constexpr std::size_t SIZE = 0x50;
    static constexpr Rva PUSH_FN{0x1d3d740};
    static constexpr Rva POP_FN{0x1d3d610};
    static constexpr Rva CLEAR_FN{0x1d3d410};

    void* allocator;
    LocationUpdateNode** read_cursor;
    LocationUpdateNode** read_last;
    LocationUpdateNode** write_cursor;
    LocationUpdateNode** write_last;
    LocationQueueBlockHeader* active_head;
    LocationQueueBlockHeader* active_tail;
    // Base of one allocation containing pooled_block_count consecutive blocks.
    LocationQueueBlockHeader* block_pool;
    LocationQueueBlockHeader* free_blocks;
    std::uint32_t slots_per_block;
    std::uint32_t pooled_block_count;

    bool empty() const { return read_cursor == write_cursor; }
};

// Only the fixed header is modeled: pointer slots immediately follow it.
// Pooled blocks have pooled=1; zeroed heap fallback headers have pooled=0.
// The next link is reused for active and cached-free block chains.
struct LocationQueueBlockHeader {
    std::uint8_t pooled;
    Unknown<7> _unk01;
    LocationQueueBlockHeader* next;
};

// Native 0x38-byte singleton, allocated aligned to eight by STEP_Init.
// Identity comes from singleton assertions. Reflection belongs to the owner
// step below, not to this manager.
//
// Dirty propagation acquires the inline mutex before enqueueing. Update drains
// dirty objects, runs enabled/non-recursive update virtuals, clears dirty
// state, and releases queue references. It then drains render work through
// virtual +0x48, or clears that queue when RendMan is unavailable. Native
// update itself has no observed mutex acquisition.
struct SprjFD4Location {
    static constexpr std::size_t SIZE = 0x38;
    static constexpr Rva SINGLETON_PTR = SPRJ_FD4_LOCATION_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493aafb};
    static constexpr Rva INSTANCE_VTABLE{0x534c960};
    static constexpr Rva CONSTRUCTOR_FN{0x1d3d9f0};
    static constexpr Rva DESTRUCTOR_FN{0x1d3dc80};
    static constexpr Rva UPDATE_FN{0x1d3de40};
    static constexpr Rva QUEUE_RENDER_FN{0x1d3dfd0};
    static constexpr std::size_t DIRTY_SLOTS_PER_BLOCK = 0x200;
    static constexpr std::size_t DIRTY_POOLED_BLOCKS = 0x18;
    static constexpr std::size_t DIRTY_POOL_BYTES = 0x18180;
    static constexpr std::size_t RENDER_SLOTS_PER_BLOCK = 0x80;
    static constexpr std::size_t RENDER_POOLED_BLOCKS = 8;
    static constexpr std::size_t RENDER_POOL_BYTES = 0x2080;

    const void* vftable;
    LocationUpdateQueue* dirty_queue;
    CSPlainAdaptiveMutexImp mutex;
    LocationUpdateQueue* render_queue;
};

// Reflected SprjFD4LocationStep, allocated as 0xd0 bytes aligned to eight.
// Constructor installs the callback table, and the task owner registers it on
// LocationStep (0x28). Init creates the singleton and requests step 1; Update
// drains it; Finish destroys/frees it and writes requested_step=-1. The inline
// dispatcher and string internals remain opaque.
struct SprjFD4LocationStep {
    static constexpr std::size_t SIZE = 0xd0;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FD4_LOCATION_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_FD4_LOCATION_STEP_TEMPLATE;
    static constexpr Rva INSTANCE_VTABLE{0x534c980};
    static constexpr Rva CONSTRUCTOR_FN{0x1d3e7b0};
    static constexpr Rva DESTRUCTOR_FN{0x1d3e9f0};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::LocationStep;

    const void* vftable;
    Unknown<8> _unk08;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t flag_60;
    Unknown<7> _unk61;
    void* allocator;
    // Destructor tests byte 0 before calling the allocator-cleanup helper.
    std::uint8_t allocator_flags[2];
    Unknown<6> _unk72;
    std::uint64_t value_78_bits;
    // Buffer +0x88, length +0x98, capacity +0xa0, allocator +0xa8.
    Unknown<0x38> _string_storage;
    std::uint64_t value_b8_bits;
    // Constructor points this at native wide label "NotExecuting".
    const std::uint16_t* execution_label;
    std::uint8_t flag_c8;
    Unknown<3> _unkc9;
    std::int32_t value_cc;
};

namespace detail::location_layout {
BB_SIZE(SprjFD4Location, SprjFD4Location::SIZE);
static_assert(alignof(SprjFD4Location) == 8, "alignof(SprjFD4Location)");
BB_OFFSET(SprjFD4Location, dirty_queue, 8);
BB_OFFSET(SprjFD4Location, mutex, 0x10);
BB_OFFSET(SprjFD4Location, render_queue, 0x30);
static_assert(offsetof(SprjFD4Location, mutex) + offsetof(CSPlainAdaptiveMutexImp, semaphore) == 0x20,
              "SprjFD4Location::mutex.semaphore");

BB_SIZE(LocationUpdateQueue, LocationUpdateQueue::SIZE);
static_assert(alignof(LocationUpdateQueue) == 8, "alignof(LocationUpdateQueue)");
BB_OFFSET(LocationUpdateQueue, read_cursor, 8);
BB_OFFSET(LocationUpdateQueue, read_last, 0x10);
BB_OFFSET(LocationUpdateQueue, write_cursor, 0x18);
BB_OFFSET(LocationUpdateQueue, write_last, 0x20);
BB_OFFSET(LocationUpdateQueue, active_head, 0x28);
BB_OFFSET(LocationUpdateQueue, active_tail, 0x30);
BB_OFFSET(LocationUpdateQueue, block_pool, 0x38);
BB_OFFSET(LocationUpdateQueue, free_blocks, 0x40);
BB_OFFSET(LocationUpdateQueue, slots_per_block, 0x48);
BB_OFFSET(LocationUpdateQueue, pooled_block_count, 0x4c);
BB_SIZE(LocationQueueBlockHeader, 0x10);
BB_OFFSET(LocationQueueBlockHeader, next, 8);
static_assert(sizeof(LocationQueueBlockHeader) + SprjFD4Location::DIRTY_SLOTS_PER_BLOCK * sizeof(void*) == 0x1010,
              "dirty block stride");
static_assert(0x1010 * SprjFD4Location::DIRTY_POOLED_BLOCKS == SprjFD4Location::DIRTY_POOL_BYTES, "dirty pool bytes");
static_assert(sizeof(LocationQueueBlockHeader) + SprjFD4Location::RENDER_SLOTS_PER_BLOCK * sizeof(void*) == 0x410,
              "render block stride");
static_assert(0x410 * SprjFD4Location::RENDER_POOLED_BLOCKS == SprjFD4Location::RENDER_POOL_BYTES,
              "render pool bytes");

BB_SIZE(SprjFD4LocationStep, SprjFD4LocationStep::SIZE);
static_assert(alignof(SprjFD4LocationStep) == 8, "alignof(SprjFD4LocationStep)");
BB_OFFSET(SprjFD4LocationStep, callback_table, 0x10);
BB_OFFSET(SprjFD4LocationStep, _dispatcher, 0x18);
BB_OFFSET(SprjFD4LocationStep, current_step, 0x58);
BB_OFFSET(SprjFD4LocationStep, requested_step, 0x5c);
BB_OFFSET(SprjFD4LocationStep, flag_60, 0x60);
BB_OFFSET(SprjFD4LocationStep, allocator, 0x68);
BB_OFFSET(SprjFD4LocationStep, allocator_flags, 0x70);
BB_OFFSET(SprjFD4LocationStep, _string_storage, 0x80);
BB_OFFSET(SprjFD4LocationStep, value_b8_bits, 0xb8);
BB_OFFSET(SprjFD4LocationStep, execution_label, 0xc0);
BB_OFFSET(SprjFD4LocationStep, flag_c8, 0xc8);
BB_OFFSET(SprjFD4LocationStep, value_cc, 0xcc);
static_assert(static_cast<std::uint32_t>(SprjFD4LocationStep::TASK_GROUP) == 0x28, "TASK_GROUP");
}  // namespace detail::location_layout

}  // namespace bb
