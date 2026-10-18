// VideoOut registration contract (display buffers, gamma ramp, present), distinct from GX targets.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// No host presentation or native calls here.

// Alternate initializer with fixed 1920x1080 registration arguments.
inline constexpr Rva INITIALIZE_ORBIS_WRAPPER{0x11e4620};
// Creates/registers display buffers using caller dimensions and buffer count.
// Distinct from the fixed-size wrapper above.
inline constexpr Rva INITIALIZE_DISPLAY_BUFFERS{0x26d5e90};
inline constexpr Rva DISPLAY_REGISTER_RETURN{0x26d65f8};
// Copies 1,025 interleaved RGB entries and uploads each channel separately.
inline constexpr Rva SET_GAMMA_RAMP{0x26d6b20};
inline constexpr Rva GET_GAMMA_RAMP{0x26d6d80};
// Selects the display buffer, emits the gamma pass and submits its flip.
inline constexpr Rva PRESENT{0x26d6e50};
// PS4 rectangle-list draw with source image and RGB-table texture views.
inline constexpr Rva DRAW_DISPLAY_GAMMA{0x26b6cb0};

inline constexpr std::size_t GAMMA_RAMP_ENTRY_COUNT = 0x401;

// Native CPU gamma ramp at GXVideoOut +0x3b8; uploaded as three array slices.
struct GammaRamp {
    float rgb[GAMMA_RAMP_ENTRY_COUNT][3];
};

// Native 40-byte sceVideoOutSetBufferAttribute/RegisterBuffers description.
// Raw pixel format/tiling values are not DXGI or GNM descriptor enums.
struct VideoOutBufferAttribute {
    std::uint32_t pixel_format_raw;
    std::int32_t tiling_mode_raw;
    std::int32_t aspect_ratio_raw;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t pitch_in_pixels;
    std::uint32_t option_raw;
    std::uint32_t reserved0;
    std::uint64_t reserved1;
};

namespace detail::video_out_layout {
BB_SIZE(GammaRamp, 0x300c);
BB_SIZE(VideoOutBufferAttribute, 0x28);
BB_OFFSET(VideoOutBufferAttribute, width, 0x0c);
BB_OFFSET(VideoOutBufferAttribute, pitch_in_pixels, 0x14);
BB_OFFSET(VideoOutBufferAttribute, reserved1, 0x20);
}  // namespace detail::video_out_layout

}  // namespace bb
