// Bloodborne class/singleton names observed in eboot.bin strings (Sprj inventory).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Renamed from OBSERVED_CLASSES, ObservedStringEncoding and OBSERVED_DEFERRED_STRINGS:
// fd4, frpg, cs and sprj each define their own, so this header appends _sprj.

// Sorted and unique.
inline constexpr const char* OBSERVED_CLASSES_sprj[] = {
    "ChrAsmModel",
    "ChrIns",
    "NetWorldChrSync",
    "PlayerIns",
    "ReplayGhostIns",
    "SprjActionButtonMan",
    "SprjAiUpdateTimeSchedular",
    "SprjAsmModel",
    "SprjBulletIns",
    "SprjBulletManager",
    "SprjCamera",
    "SprjCameraStep",
    "SprjChrActionFlagModule",
    "SprjChrActionRequestModule",
    "SprjChrAiModule",
    "SprjChrBehaviorModule",
    "SprjChrBehaviorScriptModule",
    "SprjChrBehaviorSyncModule",
    "SprjChrDamageModule",
    "SprjChrDataModule",
    "SprjChrEventModule",
    "SprjChrFallModule",
    "SprjChrHitStopModule",
    "SprjChrInsHandleTargetAccessor",
    "SprjChrInsTargetAccessorBase",
    "SprjChrKnockBackModule",
    "SprjChrLadderModule",
    "SprjChrMagicModule",
    "SprjChrMaterialModule",
    "SprjChrModuleBase",
    "SprjChrPhysicsModule",
    "SprjChrResistModule",
    "SprjChrSfxModule",
    "SprjChrSuperArmorModule",
    "SprjChrTalkModule",
    "SprjChrThrowModule",
    "SprjChrTimeActModule",
    "SprjDarkSight",
    "SprjDbgEvent",
    "SprjDebugCam",
    "SprjEdfFileCap",
    "SprjEdfRepository",
    "SprjEdfResCap",
    "SprjEldFileCap",
    "SprjEldRepository",
    "SprjEldResCap",
    "SprjEmkConditionGroup",
    "SprjEmkConditionHolder",
    "SprjEmkEventIns",
    "SprjEmkResMan",
    "SprjEmkSystem",
    "SprjEmkSystemUpdateTask",
    "SprjEnemyDamageModule",
    "SprjEnemyFallModule",
    "SprjEnemyHitStopModule",
    "SprjEnemyKnockBackModule",
    "SprjEnemyLadderModule",
    "SprjEnemyMagicModule",
    "SprjEnemyMaterialModule",
    "SprjEnemySfxModule",
    "SprjEvdFileCap",
    "SprjEvdRepository",
    "SprjEvdResCap",
    "SprjEventFlagMan",
    "SprjEventMan",
    "SprjEventRegionMan",
    "SprjEventState",
    "SprjFD4Location",
    "SprjFD4LocationStep",
    "SprjFD4ModelDispEntity",
    "SprjFD4ModelItem",
    "SprjFileImp",
    "SprjFileRepository",
    "SprjFileRepositorySeed",
    "SprjFileSeed",
    "SprjFileStep",
    "SprjFixedPosTarget",
    "SprjLuaEventMan",
    "SprjLuaEventProxy",
    "SprjLuaEventScriptImitation",
    "SprjLuaEventUpdateTask",
    "SprjNullTargetAccessor",
    "SprjObjActUpdateTask",
    "SprjPlayerDamageModule",
    "SprjPlayerFallModule",
    "SprjPlayerHitStopModule",
    "SprjPlayerKnockBackModule",
    "SprjPlayerLadderModule",
    "SprjPlayerMagicModule",
    "SprjPlayerMaterialModule",
    "SprjPlayerSfxModule",
    "SprjRapidReentryHelper",
    "SprjScriptCallParam",
    "SprjSessionManager",
    "SprjSoundTarget",
    "SprjTargetAccessorBase",
    "SprjTargetBankManager",
    "SprjTask",
    "SprjTaskGroup",
    "SprjTendencyMan",
    "SprjWorldAiManager",
    "SprjWorldObjActMan",
    "WorldAreaInfo",
    "WorldBlockChr",
    "WorldChrMan",
    "WorldChrManDbg",
    "WorldInfo",
    "WorldRes",
};

enum class ObservedStringEncoding_sprj {
    Ascii,
    Utf16,
};

struct ObservedSprjString {
    const char* value;
    Rva address;
    ObservedStringEncoding_sprj encoding;
};

// Sorted by value, then address.
inline constexpr ObservedSprjString OBSERVED_DEFERRED_STRINGS_sprj[] = {
    {"NetWorldChrSync", Rva{0x49853ac}, ObservedStringEncoding_sprj::Utf16},
    {"SprjRapidReentryHelper", Rva{0x4938835}, ObservedStringEncoding_sprj::Ascii},
    {"SprjWorldObjActMan", Rva{0x493002d}, ObservedStringEncoding_sprj::Ascii},
    {"SprjWorldObjActMan", Rva{0x4950890}, ObservedStringEncoding_sprj::Utf16},
    {"WorldChrManDbg", Rva{0x4935c4a}, ObservedStringEncoding_sprj::Ascii},
    {"WorldRes", Rva{0x496898a}, ObservedStringEncoding_sprj::Utf16},
};

}  // namespace bb
