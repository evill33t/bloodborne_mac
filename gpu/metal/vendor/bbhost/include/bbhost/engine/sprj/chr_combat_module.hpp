// Actor-specific combat modules (magic, hit stop, knockback, SFX) installed by
// the NPC and player module factories. Sizes, inheritance shapes and fields
// come from constructors and consumers; unnamed words keep bytes unguessed.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"
#include "bbhost/engine/sprj/chr_module.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Magic module at container +0x60. The constructor sets both signed words to
// -1. Player virtual +0x20 resolves magic_id through the magic parameter table
// and may substitute an effect-dependent alternative ID.
struct SprjChrMagicModule {
    SprjChrModuleBase super_chr_module;
    std::int32_t value_10;
    std::int32_t magic_id;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_MAGIC_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x5398450};
    static constexpr Rva SIZE_FN{0x1a411b0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a40070};
    static constexpr Rva CHECK_RESOURCE_COUNT_FN{0x1a40140};
    static constexpr Rva CHANGE_RESOURCE_COUNT_FN{0x1a400b0};
};

// Enemy allocation adds no fields to the magic base.
struct SprjEnemyMagicModule {
    SprjChrMagicModule super_magic_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_MAGIC_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x5335510};
    static constexpr Rva SIZE_FN{0x1a41cd0};
};

// Player allocation adds one signed word, initialized to -1.
struct SprjPlayerMagicModule {
    SprjChrMagicModule super_magic_module;
    std::int32_t value_18;
    std::uint8_t _unk1c[4];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_MAGIC_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x5335580};
    static constexpr Rva SIZE_FN{0x1a433d0};
    static constexpr Rva RESOLVE_MAGIC_ID_FN{0x1a428c0};
};

// Hit-stop timing at container +0x90. Update stores the input delta at +0x1c
// and the adjusted delta at +0x18. Phases 1/2/3 decelerate, hold at zero and
// recover; phase 0 passes the input through. WorldChrManDbg may override the
// durations, so the local recovery value is not universal.
struct SprjChrHitStopModule {
    SprjChrModuleBase super_chr_module;
    float remaining_time;
    float recovery_duration;
    float adjusted_delta;
    float input_delta;
    std::uint8_t phase;
    std::uint8_t _unk21[7];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_HIT_STOP_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva INSTANCE_VTABLE{0x5397f30};
    static constexpr Rva SIZE_FN{0x1a38ce0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a383e0};
    static constexpr Rva UPDATE_FN{0x1a38430};
    static constexpr Rva BEGIN_FN{0x1a386d0};
};

struct SprjEnemyHitStopModule {
    SprjChrHitStopModule super_hit_stop_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_HIT_STOP_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva INSTANCE_VTABLE{0x53352a0};
    static constexpr Rva SIZE_FN{0x1a39810};
};

struct SprjPlayerHitStopModule {
    SprjChrHitStopModule super_hit_stop_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_HIT_STOP_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva INSTANCE_VTABLE{0x53352d0};
    static constexpr Rva SIZE_FN{0x1a3a340};
};

// Knockback at container +0xa8, allocated 0x60 aligned to 16. Direction setup
// normalizes the horizontal input into +0x20. The frame consumer decrements
// remaining_time by hit-stop input_delta (not adjusted_delta). It can resolve
// counterpart through WorldChrMan and forward residual knockback to that
// actor's module under the two control bytes. The last three initialized
// words and twelve tail bytes are unclassified.
struct alignas(16) SprjChrKnockBackModule {
    SprjChrModuleBase super_chr_module;
    float magnitude;
    float remaining_time;
    float current_speed;
    float decay_duration;
    float direction[4];
    float pending_speed;
    ChrHandle counterpart;
    float transfer_distance;
    float transfer_duration;
    float speed_multiplier;
    std::uint8_t pending;
    std::uint8_t defer_update;
    std::uint8_t _unk46[2];
    std::uint32_t value_48_bits;
    std::uint32_t value_4c_bits;
    std::uint32_t value_50_bits;
    std::uint8_t _unk54[0x0c];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_KNOCK_BACK_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x60;
    static constexpr Rva INSTANCE_VTABLE{0x53980e0};
    static constexpr Rva SIZE_FN{0x1a3b560};
    static constexpr Rva CONSTRUCTOR_FN{0x1a3a710};
    static constexpr Rva SET_DIRECTION_FN{0x1a3a7b0};
    static constexpr Rva REQUEST_FN{0x1a3a880};
    static constexpr Rva UPDATE_FN{0x1a3a9d0};
};

// Enemy coefficient lookup reads NPC parameter byte +0x11e.
struct SprjEnemyKnockBackModule {
    SprjChrKnockBackModule super_knock_back_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_KNOCK_BACK_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x60;
    static constexpr Rva INSTANCE_VTABLE{0x5335300};
    static constexpr Rva SIZE_FN{0x1a3c160};
};

// Player coefficient lookup resolves an owner-selected parameter and reads its
// unsigned word at +0xa6. Registration explicitly links the knockback base.
struct SprjPlayerKnockBackModule {
    SprjChrKnockBackModule super_knock_back_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_KNOCK_BACK_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x60;
    static constexpr Rva INSTANCE_VTABLE{0x5335340};
    static constexpr Rva SIZE_FN{0x1a3cd30};
};

// SFX module at container +0xb0. Both selectors start at -1; flags bit zero
// starts set. Refresh derives +0x10 from actor/effect parameters and +0x14 from
// the magic module's selected parameter. The selector consumer chooses by actor
// action/controller flags and returns selector + category*100 only when both
// signed inputs are nonnegative.
struct SprjChrSfxModule {
    SprjChrModuleBase super_chr_module;
    std::int32_t action_sfx_selector;
    std::int32_t magic_sfx_selector;
    std::uint8_t flags;
    std::uint8_t _unk19[7];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_SFX_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x5398a50};
    static constexpr Rva SIZE_FN{0x1a532d0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a509f0};
    static constexpr Rva SELECT_SFX_ID_FN{0x1a50a40};
    static constexpr Rva REFRESH_SELECTORS_FN{0x1a50ad0};
};

// Enemy virtual +0x18 returns NPC parameter byte +0x139.
struct SprjEnemySfxModule {
    SprjChrSfxModule super_sfx_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_SFX_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x5335780};
    static constexpr Rva SIZE_FN{0x1a53e20};
};

// Player virtual +0x18 uses the shared zero-return implementation.
struct SprjPlayerSfxModule {
    SprjChrSfxModule super_sfx_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_SFX_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x53357b0};
    static constexpr Rva SIZE_FN{0x1a54940};
};

namespace detail::chr_combat_module_layout {
BB_SIZE(SprjChrMagicModule, SprjChrMagicModule::SIZE);
BB_SIZE(SprjEnemyMagicModule, SprjEnemyMagicModule::SIZE);
BB_SIZE(SprjPlayerMagicModule, SprjPlayerMagicModule::SIZE);
BB_OFFSET(SprjChrMagicModule, magic_id, 0x14);
BB_OFFSET(SprjPlayerMagicModule, value_18, 0x18);
BB_SIZE(SprjChrSfxModule, SprjChrSfxModule::SIZE);
BB_SIZE(SprjEnemySfxModule, SprjEnemySfxModule::SIZE);
BB_SIZE(SprjPlayerSfxModule, SprjPlayerSfxModule::SIZE);
BB_OFFSET(SprjChrSfxModule, action_sfx_selector, 0x10);
BB_OFFSET(SprjChrSfxModule, magic_sfx_selector, 0x14);
BB_OFFSET(SprjChrSfxModule, flags, 0x18);
BB_SIZE(SprjChrHitStopModule, SprjChrHitStopModule::SIZE);
BB_SIZE(SprjEnemyHitStopModule, SprjEnemyHitStopModule::SIZE);
BB_SIZE(SprjPlayerHitStopModule, SprjPlayerHitStopModule::SIZE);
BB_OFFSET(SprjChrHitStopModule, remaining_time, 0x10);
BB_OFFSET(SprjChrHitStopModule, recovery_duration, 0x14);
BB_OFFSET(SprjChrHitStopModule, adjusted_delta, 0x18);
BB_OFFSET(SprjChrHitStopModule, input_delta, 0x1c);
BB_OFFSET(SprjChrHitStopModule, phase, 0x20);
BB_SIZE(SprjChrKnockBackModule, SprjChrKnockBackModule::SIZE);
BB_SIZE(SprjEnemyKnockBackModule, SprjEnemyKnockBackModule::SIZE);
BB_SIZE(SprjPlayerKnockBackModule, SprjPlayerKnockBackModule::SIZE);
static_assert(alignof(SprjChrKnockBackModule) == 16, "alignof(SprjChrKnockBackModule)");
static_assert(alignof(SprjEnemyKnockBackModule) == 16, "alignof(SprjEnemyKnockBackModule)");
static_assert(alignof(SprjPlayerKnockBackModule) == 16, "alignof(SprjPlayerKnockBackModule)");
BB_OFFSET(SprjChrKnockBackModule, magnitude, 0x10);
BB_OFFSET(SprjChrKnockBackModule, remaining_time, 0x14);
BB_OFFSET(SprjChrKnockBackModule, current_speed, 0x18);
BB_OFFSET(SprjChrKnockBackModule, decay_duration, 0x1c);
BB_OFFSET(SprjChrKnockBackModule, direction, 0x20);
BB_OFFSET(SprjChrKnockBackModule, pending_speed, 0x30);
BB_OFFSET(SprjChrKnockBackModule, counterpart, 0x34);
BB_OFFSET(SprjChrKnockBackModule, transfer_distance, 0x38);
BB_OFFSET(SprjChrKnockBackModule, transfer_duration, 0x3c);
BB_OFFSET(SprjChrKnockBackModule, speed_multiplier, 0x40);
BB_OFFSET(SprjChrKnockBackModule, pending, 0x44);
BB_OFFSET(SprjChrKnockBackModule, defer_update, 0x45);
BB_OFFSET(SprjChrKnockBackModule, value_48_bits, 0x48);
BB_OFFSET(SprjChrKnockBackModule, _unk54, 0x54);
}  // namespace detail::chr_combat_module_layout

}  // namespace bb
