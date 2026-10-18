// CSPrefetchForSlowStorageStep: the native PlayGo slow-storage prefetch step,
// its retained file caps, and its callbacks.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct FD4FileCap;

enum class CSPrefetchForSlowStorageStepIndex : std::int32_t {
    Init = 0,
    InitCache = 1,
    WaitCache = 2,
    Finish = 3,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSPrefetchForSlowStorageStepIndex& out) {
    if (value < 0 || value > 3) return false;
    out = static_cast<CSPrefetchForSlowStorageStepIndex>(value);
    return true;
}

struct CSStoragePrefetchNode {
    CSStoragePrefetchNode* next;
    CSStoragePrefetchNode* previous;
    FD4FileCap* file_cap;
};

// Descriptive native list owner. Constructor allocates a self-linked 0x18-byte
// sentinel; its payload must not be treated as a cap.
struct CSStoragePrefetchList {
    Unknown<8> _unk00;
    CSStoragePrefetchNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Reflected 0xf0-byte SprjStepLocal subclass allocated aligned to eight by
// CSPlaygo::UPDATE_FN while its sub_state is SlowStorage (1). The owner calls
// virtual +0xd0 directly; this step has no independent task registration.
//
// InitCache requests 38 map-model files and 26 object bundles through the file
// service's PREFETCH:-keyed factory, retaining one cap in each list node.
// WaitCache accepts null caps and requires state +0x78 == 4 for all non-null
// caps before releasing them, draining the list, setting prefetched, and
// advancing. Finish writes requested_step = -1. Native callback names retain
// the binary's "Chache" spelling in STEP_TEMPLATE.
//
// Destructor frees list nodes/sentinel and step internals, but does not
// release caps still in the list.
struct CSPrefetchForSlowStorageStep {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    // Set by virtual +0x38 after advancement to request another callback in
    // this same update. The dispatcher clears it and bounds retries to 128.
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t allocator_flags[2];
    Unknown<6> _unk6a;
    std::uint64_t value_70_bits;
    // Native string storage at +0x80, length +0x90, capacity +0x98.
    Unknown<0x38> _string_storage;
    std::uint64_t value_b0_bits;
    const std::uint16_t* execution_label;
    std::uint8_t flag_c0;
    Unknown<3> _unkc1;
    std::int32_t value_c4;
    CSStoragePrefetchList pending_files;
    std::uint8_t prefetched;
    Unknown<7> _unke9;

    static constexpr std::size_t SIZE = 0xf0;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_PREFETCH_FOR_SLOW_STORAGE_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_PREFETCH_FOR_SLOW_STORAGE_STEP_TEMPLATE;
    static constexpr Rva VTABLE{0x5359240};
    static constexpr Rva CONSTRUCTOR_FN{0x1fdf5d0};
    static constexpr Rva DESTRUCTOR_FN{0x1fdf860};
    // Virtual +0xd0 thunk to the native local-step dispatcher.
    static constexpr Rva EXECUTE_FN{0x1fe22b0};
    static constexpr Rva ENQUEUE_FILE_FN{0x1fdf960};
    static constexpr Rva INIT_FN{0x1fdfaa0};
    static constexpr Rva INIT_CACHE_FN{0x1fdfac0};
    static constexpr Rva WAIT_CACHE_FN{0x1fdfd20};
    static constexpr Rva FINISH_FN{0x1fdfe60};
    static constexpr Rva MAP_MODEL_IDS{0x47371d0};
    static constexpr std::size_t MAP_MODEL_COUNT = 38;
    static constexpr Rva OBJECT_IDS{0x4737270};
    static constexpr std::size_t OBJECT_COUNT = 26;
};

namespace detail::prefetch_for_slow_storage_step_layout {
using T = CSPrefetchForSlowStorageStep;
BB_SIZE(T, 0xf0);
static_assert(alignof(T) == 8, "alignof(CSPrefetchForSlowStorageStep)");
BB_OFFSET(T, callback_table, 8);
BB_OFFSET(T, _dispatcher, 0x10);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, requested_step, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, allocator_flags, 0x68);
BB_OFFSET(T, value_70_bits, 0x70);
BB_OFFSET(T, _string_storage, 0x78);
BB_OFFSET(T, value_b0_bits, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, flag_c0, 0xc0);
BB_OFFSET(T, value_c4, 0xc4);
BB_OFFSET(T, pending_files, 0xc8);
BB_OFFSET(T, prefetched, 0xe8);
BB_SIZE(CSStoragePrefetchList, 0x20);
static_assert(alignof(CSStoragePrefetchList) == 8, "alignof(CSStoragePrefetchList)");
BB_OFFSET(CSStoragePrefetchList, sentinel, 8);
BB_OFFSET(CSStoragePrefetchList, count, 0x10);
BB_OFFSET(CSStoragePrefetchList, allocator, 0x18);
BB_SIZE(CSStoragePrefetchNode, 0x18);
static_assert(alignof(CSStoragePrefetchNode) == 8, "alignof(CSStoragePrefetchNode)");
BB_OFFSET(CSStoragePrefetchNode, previous, 8);
BB_OFFSET(CSStoragePrefetchNode, file_cap, 0x10);
static_assert(T::MAP_MODEL_COUNT + T::OBJECT_COUNT == 64, "prefetch counts");
}  // namespace detail::prefetch_for_slow_storage_step_layout

}  // namespace bb
