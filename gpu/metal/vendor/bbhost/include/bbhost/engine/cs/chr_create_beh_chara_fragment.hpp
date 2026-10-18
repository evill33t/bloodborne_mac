// Behavior-character creation work queued by the character thread owner.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSEzWorkCompletion;

inline constexpr std::size_t CS_CHR_CREATE_BEH_CHARA_FRAGMENT_SIZE = 0x18;
inline constexpr Rva CS_CHR_CREATE_BEH_CHARA_FRAGMENT_VTABLE{0x5335ab0};
inline constexpr Rva CS_CHR_CREATE_BEH_CHARA_FRAGMENT_SUBMIT_FN{0x1a657a0};
inline constexpr Rva CS_CHR_CREATE_BEH_CHARA_FRAGMENT_EXECUTE_FN{0x1a641c0};
inline constexpr Rva CS_CHR_CREATE_BEH_CHARA_FRAGMENT_SIZE_FN{0x1a64870};

// A 0x18-byte work fragment submitted to CSChrThread.back_initialize_thread.
//
// Submission at RVA 0x1a657a0 retains the completion object, increments its
// pending count, writes the request ID, and queues the fragment. Vtable slot
// +0x18 resolves the class metadata; slot +0x10 executes RVA 0x1a641c0, which
// passes request_id to RVA 0x1dddf50 (the behavior-creation request ring). It
// is not a packed character handle. The work-state word starts at zero;
// executor RVA 0x2032370 uses it for post-execution cleanup: 0 destroys/frees
// immediately, 1 queues deferred deletion, 2 skips fragment deletion.
struct CSChrCreateBehCharaFragment {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CHR_CREATE_BEH_CHARA_FRAGMENT_RUNTIME_CLASS;
    const void* vftable;
    CSEzWorkCompletion* completion;
    std::uint32_t work_state;
    std::uint32_t request_id;
};

namespace detail::chr_create_beh_chara_fragment_layout {
BB_SIZE(CSChrCreateBehCharaFragment, CS_CHR_CREATE_BEH_CHARA_FRAGMENT_SIZE);
BB_OFFSET(CSChrCreateBehCharaFragment, completion, 0x08);
BB_OFFSET(CSChrCreateBehCharaFragment, work_state, 0x10);
BB_OFFSET(CSChrCreateBehCharaFragment, request_id, 0x14);
}  // namespace detail::chr_create_beh_chara_fragment_layout

}  // namespace bb
