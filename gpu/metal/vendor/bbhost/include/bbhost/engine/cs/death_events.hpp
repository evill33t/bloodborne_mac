// Native-compiled global death-event callback boundaries.
//
// Callback entry points from Bloodborne's compiled global-event script, not
// ordinary CS class methods. Only behavior proven by the 1.09 decompilation is
// described; event-context argument types remain opaque.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Parent local-death callback: emits the death presentation work and schedules
// either the local death continuation or SoloPlayDeath_2.
inline constexpr Rva GLOBAL_EVENT_SOLO_PLAY_DEATH_FN{0x1381b60};
// Final local-death callback: clears GameStateMan transition world
// character/object sections, sets the respawn destination, and calls the
// native SessionWorldTransition path.
inline constexpr Rva GLOBAL_EVENT_SOLO_PLAY_DEATH_2_FN{0x1381de0};
// Summoned-player death wait callback: emits the native death Lua call and
// schedules PartyGhostDeath_1; it does not perform the final transition.
inline constexpr Rva GLOBAL_EVENT_PARTY_GHOST_DEATH_WAIT_FN{0x1382070};
// Final summoned-player death callback: clears transition sections, performs
// conditional death bookkeeping, requests recovery, and calls the native
// SessionWorldTransition path when the boss-clear branch permits it.
inline constexpr Rva GLOBAL_EVENT_PARTY_GHOST_DEATH_2_FN{0x13824c0};

// Lua UpDateBloodMark wrapper. Calls BLOOD_MARK_COMMIT_FN with capture=1.
inline constexpr Rva LUA_UPDATE_BLOOD_MARK_FN{0x13308b0};
// ABI: (SprjEventMan+0x80 manager, local PlayerIns, u8 capture). Nonzero
// capture charges carried and pending echoes, stores GameDataMan+0x30, then
// rebuilds the runtime marker; zero reuses the saved record.
// GameStateMan+0x1770 gates capture. +0x1592 selects actor history when
// nonzero, or the +0x1500/+0x1510/+0x14f0 saved transform/map when zero.
// PartyGhostDeath_2 calls this only for ChrType 2/12 and summon type 2/3;
// friendly ChrType 1 is excluded. Script bytes +0xc and +0x14 skip that call.
inline constexpr Rva BLOOD_MARK_COMMIT_FN{0x13d2910};
// Seven-byte CMP of GameStateMan+0x1592. DL still holds capture; the following
// JE selects the saved pre-summon transform instead of PlayerIns history.
inline constexpr Rva BLOOD_MARK_POSITION_SOURCE_COMPARE{0x13d2972};
inline constexpr Rva BLOOD_MARK_REBUILD_RUNTIME_FN{0x1868a50};
// Lua ParamInitialize, called by InGameStart (RVA 0x13803f0). Reads local
// PlayerGameData+0xa4: type 0 restores the saved mark with capture=0; type 8
// does so only when GameStateMan's +0x16f8 object has +0x18 <= 1. Other types
// (including friendly type 1) clear GameDataMan+0x28 and remove the pickup.
inline constexpr Rva LUA_PARAM_INITIALIZE_BLOOD_MARK_FN{0x1334060};
// BloodStainMan debug-label callback. DropSoulPos prints the saved XYZ only
// when GameDataMan+0x28 or runtime BloodStainMan+0xc0 is nonzero.
inline constexpr Rva BLOOD_STAIN_DEBUG_LABEL_FN{0x186c3e0};
// Reads GameDataMan+0xa8; no native session-role or ChrType test.
inline constexpr Rva LUA_IS_DEATH_PENALTY_SKIP_FN{0x13389b0};
inline constexpr Rva LUA_ON_SELF_BLOOD_MARK_FN{0x1330970};
inline constexpr Rva SELF_BLOOD_MARK_CONDITION_EVALUATE_FN{0x130a8d0};
// Adds the saved mark's echoes to local PlayerGameData (clamped to 999999999),
// sets the mark's echo amount to -1, notifies Lua and requests profile saving.
inline constexpr Rva SELF_BLOOD_MARK_RECOVER_FN{0x130ab80};

}  // namespace bb
