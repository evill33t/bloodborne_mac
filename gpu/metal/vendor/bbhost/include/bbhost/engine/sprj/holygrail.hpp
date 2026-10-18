// Native altar-gate owner, gate recreation, and selected-helper access.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/dungeon_gate_ins.hpp"
#include "bbhost/engine/cs/dungeon_ritual_helper.hpp"

namespace bb {

// Native SprjHolygrail singleton, allocated as 0x20 bytes aligned to eight by
// RVA 0x1927cf0. Its identity follows singleton assertions, allocation,
// construction, and consumers; no reflected runtime-class object is claimed.
// The first word is a count, not a vtable pointer.
//
// Constructor RVA 0x1ac36b0 leaves an empty gate array, enables resume
// uploads, and creates the HOLYGRAIL debug root. Startup then publishes the
// singleton before constructing seven gates, so each gate can attach to that
// root.
//
// The pointer array and its gates are native-owned allocations. Gate
// recreation destroys every old gate and replaces the array, invalidating all
// borrowed gate/helper pointers. The native destructor destroys the gates
// before removing the root.
struct SprjHolygrail {
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva SINGLETON_PTR = SPRJ_HOLYGRAIL_SINGLETON_PTR;
    static constexpr Rva CONSTRUCTOR_FN{0x1ac36b0};
    static constexpr Rva DESTRUCTOR_FN{0x1ac3960};
    static constexpr Rva ALLOCATE_BANKS_FN = SprjDungeonGateIns::ALLOCATE_BANKS_FN;
    // Destroys/frees each gate and its pointer array, then leaves count/pointer
    // zero. The debug root and resume-upload byte remain intact.
    static constexpr Rva RELEASE_BANKS_FN{0x1ac3bb0};
    // Destroy/reallocate all seven banks, without replacing this owner/root.
    // Reached by game-state reload and the HOLYGRAIL debug action.
    static constexpr Rva RECREATE_BANKS_FN{0x1ac3ca0};
    // Reads the seven-bit selector at flag 0x233c, chooses its highest set bit,
    // and returns gates[bank] +0x108. Native code checks neither a zero
    // selector nor gate_count/null slots; see cs dungeon_altar_bank_from_selector.
    static constexpr Rva GET_SELECTED_HELPER_FN{0x1ac3e70};
    // Uses bank zero directly, independently of the event-flag selector. Can
    // cancel an existing ritual, request QuickJoin, dispatch, and recover/cancel
    // on failure. The success branch checks helper status ServerReady (5),
    // without independently certifying idle state or Open privacy.
    static constexpr Rva QUICK_JOIN_BANK_ZERO_FN{0x1ac3ef0};
    static constexpr Rva DEBUG_RECREATE_CALLBACK_FN{0x1ac4220};
    static constexpr Rva GET_DEBUG_ROOT_FN{0x1ac4360};
    static constexpr Rva STARTUP_OWNER_CONSTRUCTOR_FN{0x1927cf0};
    static constexpr Rva STARTUP_OWNER_DESTRUCTOR_FN{0x1928360};
    static constexpr Rva RESUME_UPLOAD_STEP_FN{0x1aa7430};

    // Zero after construction/release, seven after normal gate allocation.
    std::uint32_t gate_count;
    std::uint32_t _unk04;
    // Owned array of gate_count pointers. Allocation can leave null gate slots.
    SprjDungeonGateIns** gates;
    // Constructor writes one; game-flow routines write zero or one. The
    // helper's ExecUploadForResume (RVA 0x1aa7430) returns without advancing
    // while zero. A nonzero byte permits that step to proceed, subject to its
    // other checks; it does not imply network readiness or authorize every
    // upload operation.
    std::uint8_t resume_upload_permitted;
    Unknown<7> _unk11;
    void* debug_root;
};

namespace detail::holygrail_layout {
BB_SIZE(SprjHolygrail, 0x20);
BB_SIZE(SprjHolygrail, SprjHolygrail::SIZE);
static_assert(alignof(SprjHolygrail) == 8, "alignof(SprjHolygrail)");
BB_OFFSET(SprjHolygrail, gate_count, 0);
BB_OFFSET(SprjHolygrail, gates, 8);
BB_OFFSET(SprjHolygrail, resume_upload_permitted, 0x10);
BB_OFFSET(SprjHolygrail, debug_root, 0x18);
BB_OFFSET(SprjHolygrail, gates, SPRJ_HOLYGRAIL_OWNER_ARRAY_OFFSET);
BB_OFFSET(SprjDungeonGateIns, ritual_helper, DUNGEON_ALTAR_OWNER_HELPER_OFFSET);
BB_OFFSET(SprjDungeonGateIns, ritual_setup, DUNGEON_ALTAR_OWNER_DEBUG_CONFIG_OFFSET);
static_assert(offsetof(SprjDungeonGateIns, ritual_helper) + offsetof(CSDungeonRitualHelper, status_raw) == 0x364,
              "SprjDungeonGateIns::ritual_helper.status_raw");
static_assert(offsetof(SprjDungeonGateIns, ritual_helper) + offsetof(CSDungeonRitualHelper, current_command_raw) ==
                  0x36c,
              "SprjDungeonGateIns::ritual_helper.current_command_raw");
}  // namespace detail::holygrail_layout

}  // namespace bb
