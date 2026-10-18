// Native floor-contact state embedded in SprjChrPhysicsModule.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CS_HIT_FLOOR_INFO_SIZE = 0x90;

// Floor-contact record with an optional collision object and its transform.
//
// Constructor RVA 0x1a45c90 and reflection-size method RVA 0x1a467d0 establish
// the extent; aligned matrix stores establish 16-byte alignment. The physics
// module embeds records at +0x70 and +0x100; its slope controller references
// the first. Collision pointers are borrowed native pointers.
struct alignas(16) CSHitFloorInfo {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_HIT_FLOOR_INFO_RUNTIME_CLASS;
    const void* vftable;
    // Numeric classification set by RVA 0x1a45d30; game-facing names unproven.
    std::int32_t state_code;
    // Initialized to -1.0; physical unit/meaning not established.
    float value_0c;
    // Set exactly when state_code is in 100..200.
    std::uint8_t state_in_100_range;
    // Also suppresses slope response; independently written meaning unproven.
    std::uint8_t flag_11;
    // Set exactly when state_code is in 400..600.
    std::uint8_t state_in_400_or_500_range;
    Unknown<0x0d> _unk13;
    // Contact normal used by the slope controller's dot/projection operations.
    float normal[4];
    // Signed collision sub-index, initialized/reset to -1.
    std::int32_t collision_index;
    Unknown<0x04> _unk34;
    // Native object resolved from the collision table; concrete type varies.
    void* collision_object;
    // Four native SIMD rows, reset to identity with the collision association.
    float collision_transform[4][4];
    // Populated from the resolved collision entry's flags by RVA 0x1a45e60.
    std::uint8_t collision_transform_active;
    Unknown<0x0f> _unk81;

    // Mirrors the two-byte gate at RVA 0x1a45d60, without following pointers.
    bool allows_slope_response() const { return state_in_100_range == 0 && flag_11 == 0; }
};

namespace detail::hit_floor_info_layout {
using T = CSHitFloorInfo;
BB_SIZE(T, CS_HIT_FLOOR_INFO_SIZE);
static_assert(alignof(T) == 16, "alignof(CSHitFloorInfo)");
BB_OFFSET(T, state_code, 0x08);
BB_OFFSET(T, value_0c, 0x0c);
BB_OFFSET(T, state_in_100_range, 0x10);
BB_OFFSET(T, flag_11, 0x11);
BB_OFFSET(T, state_in_400_or_500_range, 0x12);
BB_OFFSET(T, normal, 0x20);
BB_OFFSET(T, collision_index, 0x30);
BB_OFFSET(T, collision_object, 0x38);
BB_OFFSET(T, collision_transform, 0x40);
BB_OFFSET(T, collision_transform_active, 0x80);
}  // namespace detail::hit_floor_info_layout

}  // namespace bb
