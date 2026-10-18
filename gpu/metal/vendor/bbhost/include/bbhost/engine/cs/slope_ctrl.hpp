// CSSlopeCtrl: native slope response driven by a borrowed CSHitFloorInfo record.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSHitFloorInfo;

inline constexpr std::size_t CS_SLOPE_CTRL_SIZE = 0x40;

// Slope response state embedded at SprjChrPhysicsModule + 0x190.
//
// Constructor RVA 0x1a469c0, size method RVA 0x1a47b00, and the physics
// constructor establish this layout. RVA 0x1a46a20 derives an angle from the
// floor normal; RVA 0x1a46bc0 accumulates/clamps a projected slope response
// or damps it when inactive.
struct CSSlopeCtrl {
    const void* vftable;
    Unknown<0x08> _unk08;
    float slope_response[4];
    // Borrowed pointer to the owner's floor record at +0x70.
    CSHitFloorInfo* floor_info;
    // Cleared when either floor gate byte is nonzero.
    std::uint8_t active;
    // Initialized to one; gates the angle threshold when not forced.
    std::uint8_t threshold_enabled;
    Unknown<0x02> _unk2a;
    // acos(clamped_dot(up, normal)) * 180/pi at RVA 0x1a46a20.
    float angle_degrees;
    // Constructor copies a native vector here; subsequent meaning is unproven.
    float vector_30[4];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_SLOPE_CTRL_RUNTIME_CLASS;
};

namespace detail::slope_ctrl_layout {
BB_SIZE(CSSlopeCtrl, CS_SLOPE_CTRL_SIZE);
BB_OFFSET(CSSlopeCtrl, slope_response, 0x10);
BB_OFFSET(CSSlopeCtrl, floor_info, 0x20);
BB_OFFSET(CSSlopeCtrl, active, 0x28);
BB_OFFSET(CSSlopeCtrl, threshold_enabled, 0x29);
BB_OFFSET(CSSlopeCtrl, angle_degrees, 0x2c);
BB_OFFSET(CSSlopeCtrl, vector_30, 0x30);
}  // namespace detail::slope_ctrl_layout

}  // namespace bb
