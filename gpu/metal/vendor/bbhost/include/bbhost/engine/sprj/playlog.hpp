// Native PlayLog metadata. RVAs normalized from the Ghidra image base
// 0x400000 (rechecked 2026-10-02; earlier entries stored mapped VAs). Research
// definitions, not hooks or permission to enable uploads.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr Rva PLAYLOG_SYSTEM_SINGLETON{0x55404b0};
inline constexpr Rva PLAYLOG_LOGGER_SINGLETON{0x55404a8};
inline constexpr Rva PLAYLOG_FORMAT_NAMES{0x53594e0};
inline constexpr Rva PLAYLOG_APPLY_SERVER_CONFIGURATION{0x1fe69b0};
inline constexpr Rva PLAYLOG_CONSTRUCT_RECORD{0x1fe8200};
inline constexpr Rva PLAYLOG_SUBMIT_RECORD{0x1ff9320};
inline constexpr Rva PLAYLOG_APPEND_RECORD{0x1fe4f30};
inline constexpr Rva PLAYLOG_FLUSH_UPLOAD{0x1fe5530};
inline constexpr Rva PLAYLOG_POLL_UPLOAD_COMPLETION{0x1fe4d70};
inline constexpr Rva PLAYLOG_SYSTEM_TICK{0x1ff8640};
inline constexpr Rva PLAYLOG_SYSTEM_STEP_TICK{0x1ffb6f0};
inline constexpr Rva PLAYLOG_RECORD_MAP_CHANGE{0x1ff4c30};
inline constexpr Rva PLAYLOG_TRACK_MAP_VISIT{0x1ff8d20};
inline constexpr Rva PLAYLOG_APPEND_MAP{0x1ff7540};
inline constexpr Rva PLAYLOG_APPEND_MAP_RELATIVE_POSITION{0x1ff6970};
// Appends a transformed position from a borrowed `MapCollisionEntry*` (arg 3).
// A non-null entry is unconditionally followed through `+0x18` to the MSB
// transform at `+0x4c0`. Native code does not validate that owner's lifetime.
// Null instead resolves the current map in MsbRepository; if absent it writes
// the supplied position unchanged. Confirmed access violation at RVA 0x1ff6aba
// in the 2026-10-02 MRSGUEST host log (mapped base 0x5310000).
inline constexpr Rva PLAYLOG_APPEND_POSITION_WITH_COLLISION{0x1ff6a80};
// Uses WorldChrMan's main-player collision with a supplied position. Like the
// character wrapper above, appends a hit name afterward and returns false for
// a missing collision. Guarding only the transform helper leaves that second
// dereference of the borrowed collision owner unprotected.
inline constexpr Rva PLAYLOG_APPEND_LOCAL_POSITION{0x1ff6e30};
inline constexpr Rva PLAYLOG_APPEND_HIT_NAME{0x200b020};
inline constexpr Rva PLAYLOG_APPEND_CHARACTER_VITALS{0x1ff72c0};
inline constexpr Rva PLAYLOG_RECORD_ATTACK_DAMAGE{0x1fea250};
inline constexpr Rva PLAYLOG_RECORD_ENEMY_REGULAR{0x1ff2b30};
inline constexpr std::size_t PLAYLOG_RECORD_KIND_OFFSET = 0x90;
inline constexpr std::size_t PLAYLOG_SYSTEM_ENABLED_OFFSET = 0x10;
// Additional logger append gate. Its independent setter is not yet traced.
inline constexpr std::size_t PLAYLOG_SYSTEM_APPEND_READY_OFFSET = 0x90;
inline constexpr std::size_t PLAYLOG_SYSTEM_PLAY_NUMBER_OFFSET = 0x13c;
inline constexpr std::size_t PLAYLOG_SYSTEM_LAST_MAP_OFFSET = 0x138;
inline constexpr std::size_t PLAYLOG_LOGGER_DESTINATION_MODE_OFFSET = 0xb8;
inline constexpr std::size_t PLAYLOG_LOGGER_KIND_ENABLE_OFFSET = 0x130;
inline constexpr std::size_t PLAYLOG_LOGGER_MUTEX_OFFSET = 0x178;
inline constexpr std::size_t PLAYLOG_LOGGER_COMPRESSED_BUFFER_OFFSET = 0x70;
inline constexpr std::size_t PLAYLOG_LOGGER_ENCRYPTED_BUFFER_OFFSET = 0x78;
inline constexpr std::size_t PLAYLOG_COMPRESSED_BUFFER_CAPACITY = 0x19000;
inline constexpr std::size_t PLAYLOG_ENCRYPTED_BUFFER_CAPACITY = 0x19020;
inline constexpr std::size_t SERVER_INFO_PLAYLOG_URL_OFFSET = 0x938;
inline constexpr std::size_t SERVER_INFO_PLAYLOG_MODE_OFFSET = 0x970;
inline constexpr std::size_t SERVER_INFO_PLAYLOG_SWITCHES_OFFSET = 0x974;

// Values of the server-info PlayLog field, not logger destination modes.
enum class PlaylogServerMode : std::uint32_t {
    Disabled = 0,
    Test = 1,
    Production = 2,
};

// Index into the 69-entry native format table and logger enable-byte array.
// Server configuration reorders several enable bytes; it is not a memcpy.
enum class PlaylogRecordKind : std::uint32_t {
    SeriousPlaylogInfo = 0,
    ChangeMapInfo = 1,
    ChrDead = 2,
    ChrFallDead = 3,
    ChrActionMagic = 4,
    ChrChangeMagic = 5,
    ChrChangeWeapon = 6,
    ChrSwitchWeapon = 7,
    ChrFallDamage = 8,
    ChrAttackDamage = 9,
    ChrDestroyPart = 10,
    ChrAttack = 11,
    ChrAttackHitMap = 12,
    ChrThrow = 13,
    ChrGuard = 14,
    ChrUseItem = 15,
    ChrMake = 16,
    ChrHeal = 17,
    ChrGuardBreak = 18,
    ChrBreakDown = 19,
    PickUpItem = 20,
    SetEquipItem = 21,
    AddEquipItem = 22,
    CommonInfo = 23,
    ChrRolling = 24,
    ChrBackStep = 25,
    ChrDashJump = 26,
    ChrLadderUp = 27,
    ChrLadderDown = 28,
    ChrTalk = 29,
    ObjActEvoke = 30,
    AiRecogEnemy = 31,
    AiRecogSound = 32,
    RegularLog = 33,
    EnemyRegularLog = 34,
    EventLog = 35,
    MeasureTime = 36,
    BreakObj = 37,
    PutBloodMessageMessenger = 38,
    PutBloodStainMessenger = 39,
    CheckBloodMessageMessenger = 40,
    EvaluateBloodMessageMessenger = 41,
    CheckBloodStainMessenger = 42,
    EvaluateBloodStainMessenger = 43,
    MatchingLog = 44,
    MatchingFailureLog = 45,
    PcPrimaryParam = 46,
    PcTempParam = 47,
    PcWeaponParam = 48,
    PcArmorParam = 49,
    HostSendGetListRequest = 50,
    HostReceiveGetListResponse = 51,
    HostGuestInBlockList = 52,
    HostSendSummonRequest = 53,
    HostReceiveSummonResponse = 54,
    HostSendCreateSessionRequest = 55,
    HostReceiveCreateSessionResponse = 56,
    HostSessionTimeout = 57,
    HostSendP2pHandshakeRequest = 58,
    HostRejectedP2pHandshake = 59,
    HostNotComeInHostWorldTimeout = 60,
    HostStartMultiPlay = 61,
    GuestSendCreateRequest = 62,
    GuestReceiveCreateResponse = 63,
    GuestReceiveHandshakeRequest = 64,
    GuestSendHandshakeResponse = 65,
    GuestSendJoinSessionRequest = 66,
    GuestReceiveJoinSessionResponse = 67,
    GuestStartMultiPlay = 68,
};

inline constexpr std::size_t PLAYLOG_RECORD_KIND_COUNT = 69;

// Verified record prefix. The native report container stays opaque and is not
// POD network data: do not memcpy this object into a transport packet.
struct SprjPlaylogDataPrefix {
    std::uintptr_t vtable;
    std::uint8_t report_container[0x88];
    std::uint32_t kind;
};

// Logger prefix ending before its native mutex; allocation size is unproven.
struct SprjPlaylogLoggerPrefix {
    Unknown<0x08> unknown_00;
    std::uintptr_t request_template;
    std::uintptr_t request_manager;
    std::uint8_t buffered_stream[0x58];
    std::uintptr_t compressed_buffer;
    std::uintptr_t encrypted_buffer;
    std::uint8_t configured_url_storage[0x38];
    // 10 = test S3 bucket; 11 = production S3 bucket; 2 has a custom-URL path.
    std::uint32_t destination_mode;
    Unknown<0x74> unknown_bc;
    std::uint8_t kind_enabled[PLAYLOG_RECORD_KIND_COUNT];
    Unknown<3> padding_175;
};

namespace detail::playlog_layout {
static_assert(PLAYLOG_SYSTEM_SINGLETON.bn() == 0x059404b0, "PLAYLOG_SYSTEM_SINGLETON");
static_assert(PLAYLOG_LOGGER_SINGLETON.bn() == 0x059404a8, "PLAYLOG_LOGGER_SINGLETON");
static_assert(PLAYLOG_FORMAT_NAMES.bn() == 0x057594e0, "PLAYLOG_FORMAT_NAMES");
static_assert(PLAYLOG_APPLY_SERVER_CONFIGURATION.bn() == 0x023e69b0, "PLAYLOG_APPLY_SERVER_CONFIGURATION");
static_assert(PLAYLOG_SUBMIT_RECORD.bn() == 0x023f9320, "PLAYLOG_SUBMIT_RECORD");
static_assert(PLAYLOG_APPEND_POSITION_WITH_COLLISION.bn() == 0x023f6a80, "PLAYLOG_APPEND_POSITION_WITH_COLLISION");
static_assert(PLAYLOG_APPEND_LOCAL_POSITION.bn() == 0x023f6e30, "PLAYLOG_APPEND_LOCAL_POSITION");
static_assert(PLAYLOG_APPEND_HIT_NAME.bn() == 0x0240b020, "PLAYLOG_APPEND_HIT_NAME");
static_assert(static_cast<std::size_t>(PlaylogRecordKind::GuestStartMultiPlay) + 1 == PLAYLOG_RECORD_KIND_COUNT,
              "PlaylogRecordKind count");
BB_OFFSET(SprjPlaylogDataPrefix, kind, PLAYLOG_RECORD_KIND_OFFSET);
BB_OFFSET(SprjPlaylogLoggerPrefix, compressed_buffer, PLAYLOG_LOGGER_COMPRESSED_BUFFER_OFFSET);
BB_OFFSET(SprjPlaylogLoggerPrefix, encrypted_buffer, PLAYLOG_LOGGER_ENCRYPTED_BUFFER_OFFSET);
BB_OFFSET(SprjPlaylogLoggerPrefix, destination_mode, PLAYLOG_LOGGER_DESTINATION_MODE_OFFSET);
BB_OFFSET(SprjPlaylogLoggerPrefix, kind_enabled, PLAYLOG_LOGGER_KIND_ENABLE_OFFSET);
// The prefix ends where the logger's mutex begins.
BB_SIZE(SprjPlaylogLoggerPrefix, PLAYLOG_LOGGER_MUTEX_OFFSET);
}  // namespace detail::playlog_layout

}  // namespace bb
