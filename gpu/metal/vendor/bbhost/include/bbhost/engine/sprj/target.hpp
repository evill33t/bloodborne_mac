// Native target accessor hierarchy and its character, sound, and position views.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"
#include "bbhost/engine/sprj/chr_set.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct ChrIns;

inline constexpr std::size_t SPRJ_FIXED_POS_TARGET_SIZE = 0x40;
inline constexpr std::size_t SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_SIZE = 0x18;
inline constexpr std::size_t SPRJ_SOUND_TARGET_SIZE = 0x30;

// Polymorphic root, reflected size 0x08 at RVA 0x12a3bd0. The virtual
// interface is larger than the object; its signatures remain opaque.
struct SprjTargetAccessorBase {
    const void* vftable;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_TARGET_ACCESSOR_BASE_RUNTIME_CLASS;
};

// Default target-accessor implementation; adds no data to the root header.
// Registration RVA 0x12a0960 proves the base link; size RVA 0x12a38b0 is 8.
struct SprjNullTargetAccessor {
    SprjTargetAccessorBase base;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_NULL_TARGET_ACCESSOR_RUNTIME_CLASS;
};

// Character-accessor base, derived from the null accessor with size 0x08
// (reflection RVA 0x12a3590). Concrete subclasses supply the character lookup.
struct SprjChrInsTargetAccessorBase {
    SprjNullTargetAccessor base;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_CHR_INS_TARGET_ACCESSOR_BASE_RUNTIME_CLASS;
};

// Character target backed by a packed handle and a borrowed set-entry cache.
// Constructor RVA 0x12a0090 initializes the handle and a null cache. Binding
// RVA 0x12a00b0 resolves the selector through WorldChrMan + 0x850, bounds
// checks the set index, and caches entries + index * 0x38. The cache points to
// a ChrSetEntry, not the actor, and retains neither. ChrHandle NONE clears it.
// Native set replacement or actor retirement can invalidate this cache.
struct SprjChrInsHandleTargetAccessor {
    SprjChrInsTargetAccessorBase base;
    ChrHandle handle;
    std::uint32_t _pad0c;
    ChrSetEntry<ChrIns>* cached_entry;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_RUNTIME_CLASS;
};

// Sound-derived target with an inline position and validity byte.
// Constructor RVA 0x12a0500 uses aligned SIMD stores at +0x10, clears
// validity, and sets value_24 to -1. Reset RVA 0x12a0530 resets position and
// validity but leaves that integer intact. Consumers at RVAs 0x12a0570 and
// 0x12a05d0 require validity and compare squared XYZ distance; the meaning of
// the integer at +0x24 remains unproven.
struct alignas(16) SprjSoundTarget {
    SprjNullTargetAccessor base;
    Unknown<8> _unk08;
    float position[4];
    std::uint8_t valid;
    Unknown<3> _pad21;
    std::int32_t value_24;
    Unknown<8> _pad28;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_SOUND_TARGET_RUNTIME_CLASS;
};

// A target with an inline position, derived from SprjNullTargetAccessor.
// Registration RVA 0x12a0960 links the null accessor to SprjTargetAccessorBase
// and this class to the null accessor. Reflection size RVA 0x12a2c30 returns
// 0x40; vtable RVA 0x531b7f0 slot +0x80 returns position with an aligned SIMD
// load at RVA 0x12a28a0.
//
// Native targeting owners construct these inline: RVA 0x1227e00 embeds one at
// +0x40, and RVA 0x128e1b0 embeds them at +0x7e0, +0x840, and +0x910. RVA
// 0x12a28c0 resets the two vectors and scalar without replacing the vtable.
// The second vector and scalar's gameplay meanings remain unknown.
struct alignas(16) SprjFixedPosTarget {
    const void* vftable;
    Unknown<0x08> _unk08;
    float position[4];
    float vector_20[4];
    // Read as a float by virtual slot +0x98 at RVA 0x12a28b0.
    float scalar_30;
    Unknown<0x0c> _unk34;

    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_FIXED_POS_TARGET_RUNTIME_CLASS;
};

namespace detail::target_layout {
BB_SIZE(SprjTargetAccessorBase, 0x08);
BB_SIZE(SprjNullTargetAccessor, 0x08);
BB_SIZE(SprjChrInsTargetAccessorBase, 0x08);
BB_SIZE(SprjChrInsHandleTargetAccessor, SPRJ_CHR_INS_HANDLE_TARGET_ACCESSOR_SIZE);
BB_OFFSET(SprjChrInsHandleTargetAccessor, handle, 0x08);
BB_OFFSET(SprjChrInsHandleTargetAccessor, cached_entry, 0x10);
BB_SIZE(SprjSoundTarget, SPRJ_SOUND_TARGET_SIZE);
static_assert(alignof(SprjSoundTarget) == 16, "alignof(SprjSoundTarget)");
BB_OFFSET(SprjSoundTarget, position, 0x10);
BB_OFFSET(SprjSoundTarget, valid, 0x20);
BB_OFFSET(SprjSoundTarget, value_24, 0x24);
BB_SIZE(SprjFixedPosTarget, SPRJ_FIXED_POS_TARGET_SIZE);
static_assert(alignof(SprjFixedPosTarget) == 16, "alignof(SprjFixedPosTarget)");
BB_OFFSET(SprjFixedPosTarget, vftable, 0x00);
BB_OFFSET(SprjFixedPosTarget, position, 0x10);
BB_OFFSET(SprjFixedPosTarget, vector_20, 0x20);
BB_OFFSET(SprjFixedPosTarget, scalar_30, 0x30);
}  // namespace detail::target_layout

}  // namespace bb
