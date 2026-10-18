// Navigation preparation step owned by a world block's resource-loading state.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct FD4FileCap;
struct SprjWorldBlockNvm;
struct WorldBlockInfo;

// Seven table slots. The three update names distinguish indices; the native
// table repeats the same _STEP_NvmUpdate callback and label.
enum class SprjWorldBlockNvmPrepareStep : std::int32_t {
    None = 0,
    FileLoadWait = 1,
    NvmUpdateFirst = 2,
    NvmUpdateSecond = 3,
    NvmUpdateThird = 4,
    NvmSetUpWait = 5,
    Finish = 6,
};

// Concrete 0xf0-byte allocation aligned to eight, owned by
// WorldBlockRes.loading.navigation_prepare (owner +0x150). Its constructor
// borrows all four inputs; FileLoadWait resolves the navigation block using
// WorldBlockInfo +0x30 as an index into SprjWorldNvmManager.blocks.
//
// The instance getter returns the FD4STEPTEMPLATEBASE_CHILD_CLASS record. Both
// observed template reflection records report only the 0xc8 prefix; neither
// is evidence for the concrete allocation's size or a separately registered
// class named exactly SprjWorldBlockNvmPrepare.
//
// WorldBlockRes state 3 dispatches through virtual +0x18 with an FD4Time and
// tests virtual +0x20 for current_step == -1. State 5 requests block unload,
// invokes virtual +8, frees this task, and clears its pointer. The destructor
// alone does not request unload or release the borrowed file caps.
struct SprjWorldBlockNvmPrepare {
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
    // Conditionally refreshed by the debug dispatcher; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    WorldBlockInfo* world_block_info;
    // Initially null; borrowed from the navigation manager's inline pool.
    SprjWorldBlockNvm* navigation_block;
    // Borrowed .nvmhktbnd cap from WorldBlockRes.loading +0x28.
    FD4FileCap* nvmhktbnd_file;
    // Borrowed .nva cap from WorldBlockRes.loading +0x30.
    FD4FileCap* nva_file;
    // Borrowed loading +8 cap, copied to navigation_block +0x50. Its precise
    // derived file-cap type remains unproven.
    FD4FileCap* context_file;

    static constexpr std::size_t SIZE = 0xf0;
    static constexpr std::size_t REFLECTED_TEMPLATE_SIZE = 0xc8;
    static constexpr RuntimeClassSymbol THIS_CLASS = SPRJ_WORLD_BLOCK_NVM_PREPARE_THIS_RUNTIME_CLASS;
    static constexpr RuntimeClassSymbol STEP_BASE_CLASS = SPRJ_WORLD_BLOCK_NVM_PREPARE_STEP_BASE_RUNTIME_CLASS;
    static constexpr StepTemplateSymbol STEP_TEMPLATE = SPRJ_WORLD_BLOCK_NVM_PREPARE_TEMPLATE;
    static constexpr Rva VTABLE{0x534ed10};
    static constexpr Rva CONSTRUCTOR_FN{0x1e284e0};
    static constexpr Rva DESTRUCTOR_FN{0x1e28760};
    static constexpr Rva DELETING_DESTRUCTOR_FN{0x1e286e0};
    // Separate from destruction; WorldBlockRes retirement inlines this logic.
    static constexpr Rva REQUEST_UNLOAD_FN{0x1e287f0};
    static constexpr Rva RUNTIME_CLASS_GETTER_FN{0x1e28300};
    static constexpr Rva THIS_CLASS_SIZE_GETTER_FN{0x1e2ad20};
    static constexpr Rva STEP_BASE_SIZE_GETTER_FN{0x1e2aa00};
    // Virtual +0x18 thunk to DISPATCH_BODY_FN; commits requested to current.
    static constexpr Rva DISPATCH_FN{0x1e29680};
    static constexpr Rva DISPATCH_BODY_FN{0x1e2a110};
    static constexpr Rva IS_FINISHED_FN{0x1e29690};
    static constexpr Rva ADVANCE_FN{0x1e2ade0};
    static constexpr Rva NONE_FN{0x1e28840};
    static constexpr Rva FILE_LOAD_WAIT_FN{0x1e28850};
    static constexpr Rva NVM_UPDATE_FN{0x1e289b0};
    static constexpr Rva NVM_SETUP_WAIT_FN{0x1e289e0};
    static constexpr Rva FINISH_FN{0x1e28a30};

    // Whether current_step is one of the seven table slots.
    constexpr bool has_known_step() const { return current_step >= 0 && current_step <= 6; }
    SprjWorldBlockNvmPrepareStep step() const { return static_cast<SprjWorldBlockNvmPrepareStep>(current_step); }

    // Matches the native predicate, without asserting navigation success.
    constexpr bool is_finished() const { return current_step == -1; }

    // FileLoadWait's cap gate only. A null state pointer represents a null
    // cap; both non-null caps must have state 4. Does not check the manager or
    // world-block index.
    static constexpr bool files_allow_binding(const std::uint8_t* nvmhktbnd_state, const std::uint8_t* nva_state) {
        return nvmhktbnd_state && nva_state && *nvmhktbnd_state == 4 && *nva_state == 4;
    }

    // Signed comparison performed after block update in NvmSetUpWait. Phase 6
    // can still be waiting for its area, and failure phase 8 also passes.
    // Passing this gate is not proof that the block has reached Active.
    static constexpr bool setup_wait_can_advance(std::int32_t setup_phase) { return setup_phase > 5; }
};

namespace detail::world_block_nvm_prepare_layout {
BB_SIZE(SprjWorldBlockNvmPrepare, 0xf0);
BB_SIZE(SprjWorldBlockNvmPrepare, SprjWorldBlockNvmPrepare::SIZE);
static_assert(alignof(SprjWorldBlockNvmPrepare) == 8, "alignof(SprjWorldBlockNvmPrepare)");
BB_OFFSET(SprjWorldBlockNvmPrepare, callback_table, 0x08);
BB_OFFSET(SprjWorldBlockNvmPrepare, _condition_dispatcher10, 0x10);
BB_OFFSET(SprjWorldBlockNvmPrepare, current_step, 0x50);
BB_OFFSET(SprjWorldBlockNvmPrepare, requested_step, 0x54);
BB_OFFSET(SprjWorldBlockNvmPrepare, continue_this_update, 0x58);
BB_OFFSET(SprjWorldBlockNvmPrepare, allocator, 0x60);
BB_OFFSET(SprjWorldBlockNvmPrepare, debug_flags, 0x68);
BB_OFFSET(SprjWorldBlockNvmPrepare, debug_menu, 0x70);
BB_OFFSET(SprjWorldBlockNvmPrepare, _debug_string78, 0x78);
BB_OFFSET(SprjWorldBlockNvmPrepare, execution_counts, 0xb0);
BB_OFFSET(SprjWorldBlockNvmPrepare, execution_label, 0xb8);
BB_OFFSET(SprjWorldBlockNvmPrepare, debug_step_requested, 0xc0);
BB_OFFSET(SprjWorldBlockNvmPrepare, debug_step, 0xc4);
BB_OFFSET(SprjWorldBlockNvmPrepare, world_block_info, 0xc8);
BB_OFFSET(SprjWorldBlockNvmPrepare, navigation_block, 0xd0);
BB_OFFSET(SprjWorldBlockNvmPrepare, nvmhktbnd_file, 0xd8);
BB_OFFSET(SprjWorldBlockNvmPrepare, nva_file, 0xe0);
BB_OFFSET(SprjWorldBlockNvmPrepare, context_file, 0xe8);
static_assert(SprjWorldBlockNvmPrepare::REFLECTED_TEMPLATE_SIZE == 0xc8, "reflected template size");
static_assert(SprjWorldBlockNvmPrepare::VTABLE.rva == 0x534ed10, "SprjWorldBlockNvmPrepare vtable");
// The step table's eighth row would be reflection storage, not a terminator.
static_assert(SprjWorldBlockNvmPrepare::STEP_TEMPLATE.table.rva + 7 * 0x18 ==
                  SprjWorldBlockNvmPrepare::STEP_BASE_CLASS.runtime_class_ptr.rva,
              "seven step callbacks");
static_assert(SprjWorldBlockNvmPrepare::THIS_CLASS.runtime_class_ptr.rva == 0x558e0c8, "THIS_CLASS");
}  // namespace detail::world_block_nvm_prepare_layout

}  // namespace bb
