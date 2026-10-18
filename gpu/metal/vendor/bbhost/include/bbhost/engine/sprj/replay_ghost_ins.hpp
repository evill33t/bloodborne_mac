// ReplayGhostIns: the player-derived replay actor used by CSDisplayGhost and
// other replay modes.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/chr_ins.hpp"

namespace bb {

// Reflected/allocated 0x580-byte ReplayGhostIns, aligned to 16. Registration
// links PlayerIns; three constructors call its constructor before installing
// the replay vtable.
//
// The first replay field is +0x568, inside the PlayerIns view's opaque tail:
// embedding the full 0x570-byte PlayerIns would misplace it, so this view
// exposes ChrIns and leaves the player storage between opaque.
//
// CSDisplayGhost's factory creates ChrType 11 in WorldChrMan's +0x48 set, with
// model c0000, null replay_data and fade_update_enabled=1. Other native
// constructors borrow/alias the replay object owned at ChrSync +0xf0.
// Destruction delegates to PlayerIns and decrements the replay counter only
// for ChrType 10.
struct alignas(16) ReplayGhostIns {
    static constexpr std::size_t SIZE = 0x580;
    static constexpr RuntimeClassSymbol RUNTIME_CLASS = REPLAY_GHOST_INS_RUNTIME_CLASS;
    static constexpr Rva INSTANCE_VTABLE{0x538fa80};
    static constexpr Rva METADATA_VTABLE{0x5390170};
    static constexpr Rva SIZE_FN{0x1909140};
    static constexpr Rva CONSTRUCT_FROM_RECORD_ID_FN{0x1908040};
    static constexpr Rva CONSTRUCT_FROM_RECORD_FN{0x1908120};
    static constexpr Rva CONSTRUCT_WITHOUT_RECORD_FN{0x1908280};
    static constexpr Rva CREATE_DISPLAY_GHOST_FN{0x18db7d0};
    static constexpr Rva INSTALL_REPLAY_MODULES_FN{0x1906ed0};
    static constexpr Rva DESTRUCTOR_FN{0x1908360};
    static constexpr Rva DELETING_DESTRUCTOR_FN{0x1908320};
    static constexpr Rva APPLY_REPLAY_EQUIPMENT_FN{0x1908390};
    static constexpr Rva COMPLETION_PREDICATE_FN{0x1908460};
    static constexpr Rva UPDATE_FADE_FN{0x19084b0};
    static constexpr Rva FADE_PREDICATE_FN{0x19086d0};
    static constexpr Rva APPLY_PACKED_COLOR_FN{0x1908710};

    ChrIns super_chr_ins;
    Unknown<0x1b0> _player_storage_3b8;
    // Alias of ChrSync +0xf0 for recorded replays; null for CSDisplayGhost.
    // Native type and layout not established.
    void* replay_data;
    // Getter/setter copy this byte without validation. Constructors use 1 or
    // replay_data +0xc; CSDisplayGhost copies appearance +0x38.
    std::uint8_t presentation_kind_raw;
    // Completion predicate ORs this with the ChrSync mode-3/+0x10c test.
    std::uint8_t completion_override;
    // Gates the separate per-update fade logic at UPDATE_FADE_FN.
    std::uint8_t fade_update_enabled;
    std::uint8_t _unknown_573;
    // Zero moves fade toward +1, nonzero toward -1, in fixed 0.1 steps
    // (separate from CSDisplayGhost's frame-delta presentation fade).
    std::int32_t fade_direction_raw;
    // Signed fade value, initially +1. Debug creation can replace it by -2.
    float fade;
    Unknown<4> _unknown_57c;

    // Exact local predicate at FADE_PREDICATE_FN, not a complete lifetime or
    // deletion test. Mode zero (x86 SETC) accepts unordered (NaN); the nonzero
    // mode (SETNC, reversed operands) rejects it.
    bool native_fade_predicate() const {
        if (super_chr_ins.chr_type == 11) return false;
        if (fade_direction_raw != 0) return fade <= 0.0f;
        return fade < 0.0f || fade != fade;
    }
};

namespace detail::replay_ghost_ins_layout {
BB_SIZE(ReplayGhostIns, 0x580);
BB_SIZE(ReplayGhostIns, ReplayGhostIns::SIZE);
static_assert(alignof(ReplayGhostIns) == 16, "alignof(ReplayGhostIns)");
BB_OFFSET(ReplayGhostIns, super_chr_ins, 0);
BB_OFFSET(ReplayGhostIns, _player_storage_3b8, 0x3b8);
BB_OFFSET(ReplayGhostIns, replay_data, 0x568);
BB_OFFSET(ReplayGhostIns, presentation_kind_raw, 0x570);
BB_OFFSET(ReplayGhostIns, completion_override, 0x571);
BB_OFFSET(ReplayGhostIns, fade_update_enabled, 0x572);
BB_OFFSET(ReplayGhostIns, fade_direction_raw, 0x574);
BB_OFFSET(ReplayGhostIns, fade, 0x578);
}  // namespace detail::replay_ghost_ins_layout

}  // namespace bb
