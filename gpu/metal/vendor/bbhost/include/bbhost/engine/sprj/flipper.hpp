// SprjFlipper: presentation cadence (Game.FlipMode) and measured frame time.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_FLIPPER_SIZE = 0x2c8;
inline constexpr std::size_t SPRJ_FLIPPER_FRAME_HISTORY_COUNT = 32;
inline constexpr std::size_t SPRJ_FLIPPER_FPS_HISTORY_COUNT = 16;

// Native Game.FlipMode values accepted by Bloodborne 1.09. Constructor RVA
// 0x2034520 clamps both configured values to 0..5, and the update routine
// indexes the five UTF-16 names at RVA 0x535a920.
enum class SprjFlipMode : std::uint32_t {
    Fps30 = 0,
    Fps30WithoutSkip = 1,
    Fps60WithoutSkip = 2,
    Fps30WithoutTearing = 3,
    Fps30WithoutSkipTearing = 4,
};

// False for a raw value outside the five modes.
inline constexpr bool sprj_flip_mode_from_raw(std::uint32_t value, SprjFlipMode* out) {
    if (value > 4) return false;
    *out = static_cast<SprjFlipMode>(value);
    return true;
}

// One 0x10-byte wall-clock sample retained by SprjFlipper.
struct SprjFlipperFrameHistoryEntry {
    std::uint64_t elapsed_microseconds;
    bool delayed_or_behind;
    std::uint8_t _pad09[7];
};

// Bloodborne's presentation cadence and measured-frame-time owner. The names
// after measured_frame_seconds remain conservative where the update xrefs
// prove storage behavior but not the original source identifier.
struct SprjFlipper {
    static constexpr Rva SINGLETON_PTR = SPRJ_FLIPPER_SINGLETON_PTR;
    static constexpr Rva CONSTRUCTOR = SPRJ_FLIPPER_CONSTRUCTOR_FN;
    static constexpr Rva UPDATE = SPRJ_FLIPPER_UPDATE_FN;

    const void* vftable;
    std::uint32_t primary_flip_mode_raw;
    std::uint32_t secondary_flip_mode_raw;
    std::uint32_t frame_advance_count;
    bool allow_frame_skip;
    std::uint8_t _pad15[3];
    float target_frame_seconds;
    std::uint8_t _pad1c[4];
    std::uint64_t previous_frame_microseconds;
    std::uint64_t current_frame_microseconds;
    std::uint64_t phase_timestamps_microseconds[6];
    SprjFlipperFrameHistoryEntry frame_history[SPRJ_FLIPPER_FRAME_HISTORY_COUNT];
    std::uint32_t frame_history_index;
    // Wall-clock duration of the most recently completed frame.
    float measured_frame_seconds;
    std::uint32_t foreground_history_count;
    std::uint32_t background_history_count;
    bool frame_behind;
    bool reset_frame_history;
    bool force_no_sleep;
    bool force_frame_advance;
    bool request_window_60_hz;
    bool secondary_mode_active;
    bool secondary_mode_requested;
    std::uint8_t _pad277;
    float fps_frame_time_history[SPRJ_FLIPPER_FPS_HISTORY_COUNT];
    float calculated_fps;
    std::int32_t override_foreground_history_count;
    std::int32_t override_background_history_count;
    bool reset_to_30_fps_pending;
    bool use_realtime_clock;
    std::uint8_t _pad2c6[2];

    bool primary_flip_mode(SprjFlipMode* out) const { return sprj_flip_mode_from_raw(primary_flip_mode_raw, out); }
    bool secondary_flip_mode(SprjFlipMode* out) const {
        return sprj_flip_mode_from_raw(secondary_flip_mode_raw, out);
    }
};

namespace detail::flipper_layout {
BB_SIZE(SprjFlipper, SPRJ_FLIPPER_SIZE);
BB_SIZE(SprjFlipperFrameHistoryEntry, 0x10);
BB_OFFSET(SprjFlipper, primary_flip_mode_raw, 0x08);
BB_OFFSET(SprjFlipper, secondary_flip_mode_raw, 0x0c);
BB_OFFSET(SprjFlipper, frame_advance_count, 0x10);
BB_OFFSET(SprjFlipper, allow_frame_skip, 0x14);
BB_OFFSET(SprjFlipper, target_frame_seconds, 0x18);
BB_OFFSET(SprjFlipper, frame_history, 0x60);
BB_OFFSET(SprjFlipper, frame_history_index, 0x260);
BB_OFFSET(SprjFlipper, measured_frame_seconds, 0x264);
BB_OFFSET(SprjFlipper, foreground_history_count, 0x268);
BB_OFFSET(SprjFlipper, background_history_count, 0x26c);
BB_OFFSET(SprjFlipper, fps_frame_time_history, 0x278);
BB_OFFSET(SprjFlipper, calculated_fps, 0x2b8);
BB_OFFSET(SprjFlipper, reset_to_30_fps_pending, 0x2c4);
BB_OFFSET(SprjFlipper, use_realtime_clock, 0x2c5);
}  // namespace detail::flipper_layout

}  // namespace bb
