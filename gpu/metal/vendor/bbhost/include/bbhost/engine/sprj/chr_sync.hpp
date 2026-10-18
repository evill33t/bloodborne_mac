// Character sync controllers: the polymorphic ChrIns+0x60 base and the
// local-player frame-snapshot sender.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Constructor-proven size of the local-player character sync controller.
inline constexpr std::size_t LOCAL_PLAYER_CHR_SYNC_SIZE = 0x1a0;
// Constructor-proven size of the player frame snapshot owned at +0xc0.
inline constexpr std::size_t PLAYER_FRAME_SNAPSHOT_SIZE = 0x1d8;
inline constexpr std::size_t LOCAL_PLAYER_CHR_SYNC_SNAPSHOT_OFFSET = 0xc0;
inline constexpr std::size_t LOCAL_PLAYER_CHR_SYNC_FRAME_SKIP_COUNTDOWN_OFFSET = 0xc8;
// Native fallback for ChrSync.FrameSkipCount in 1.09. Despite the name, the
// positive value is the interval between sends: 5 sends once every five
// logical character updates.
inline constexpr std::int32_t PLAYER_FRAME_SYNC_DEFAULT_INTERVAL_UPDATES = 5;
// Minimum value: a frame snapshot on every logical update.
inline constexpr std::int32_t PLAYER_FRAME_SYNC_EVERY_UPDATE_INTERVAL = 1;
// WorldSessionObjectMan message type used for player frame snapshots.
inline constexpr std::uint32_t PLAYER_FRAME_SYNC_MESSAGE_TYPE = 0x06;
inline constexpr const char* PLAYER_FRAME_SYNC_CONFIG_KEY = "ChrSync.FrameSkipCount";

struct LocalPlayerChrSync;

// Polymorphic character-sync base stored at ChrIns+0x60. The factory has
// several selector-specific implementations, so a generic ChrIns cannot assume
// this is the local-player type.
struct ChrSync {
    void* vftable;

    // Casts to the type-1 local-player implementation. The caller must have
    // established (construction path or vtable) that this instance was made by
    // LocalPlayerChrSync_Construct.
    inline LocalPlayerChrSync* as_local_player_unchecked();
    inline const LocalPlayerChrSync* as_local_player_unchecked() const;
};

// Snapshot state constructed by RVA 0x1a71fb0 and serialized by RVA
// 0x1a72220. The footprint is proven; the variable-section storage is opaque.
struct PlayerFrameSnapshot {
    Unknown<PLAYER_FRAME_SNAPSHOT_SIZE> _data;
};

// Local-player character sync controller selected by sync-factory type 1.
// ChrIns+0x60 points here for the local player. Its update decrements
// frame_skip_countdown, captures transform/animation state when the old value
// is below two, broadcasts message 0x06, and reloads the countdown from
// ChrSync.FrameSkipCount.
struct LocalPlayerChrSync {
    ChrSync super_chr_sync;
    Unknown<0xb8> _unk008;
    PlayerFrameSnapshot* frame_snapshot;
    std::int32_t frame_skip_countdown;
    Unknown<0xd4> _unk0cc;

    // Requests one snapshot on the next logical update. One-shot: the native
    // update reloads the persistent interval right after sending.
    void request_frame_on_next_update() { frame_skip_countdown = PLAYER_FRAME_SYNC_EVERY_UPDATE_INTERVAL; }

    // Send rate for a positive native interval; 0 when either input is not
    // positive.
    static constexpr float send_rate_hz(float logical_update_hz, std::int32_t interval_updates) {
        return (logical_update_hz <= 0.0f || interval_updates <= 0)
                   ? 0.0f
                   : logical_update_hz / static_cast<float>(interval_updates);
    }
};

inline LocalPlayerChrSync* ChrSync::as_local_player_unchecked() {
    return reinterpret_cast<LocalPlayerChrSync*>(this);
}
inline const LocalPlayerChrSync* ChrSync::as_local_player_unchecked() const {
    return reinterpret_cast<const LocalPlayerChrSync*>(this);
}

namespace detail::chr_sync_layout {
BB_SIZE(LocalPlayerChrSync, LOCAL_PLAYER_CHR_SYNC_SIZE);
BB_SIZE(PlayerFrameSnapshot, PLAYER_FRAME_SNAPSHOT_SIZE);
BB_OFFSET(LocalPlayerChrSync, super_chr_sync, 0x00);
BB_OFFSET(LocalPlayerChrSync, frame_snapshot, LOCAL_PLAYER_CHR_SYNC_SNAPSHOT_OFFSET);
BB_OFFSET(LocalPlayerChrSync, frame_skip_countdown, LOCAL_PLAYER_CHR_SYNC_FRAME_SKIP_COUNTDOWN_OFFSET);
}  // namespace detail::chr_sync_layout

}  // namespace bb
