// Damage modules installed at character-module-container +0x98.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_module.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Eight-byte record. Constructor sets id=-1 and flag=0; cleanup passes each ID
// to RVA 0x18665a0. Registry identity and flag semantics are unclassified.
struct ChrDamageRecord {
    std::int32_t id;
    std::uint8_t flag;
    std::uint8_t _unk05[3];
};

// Common damage fields through +0xa8, without the standalone base's tail
// padding. A descriptive view, not a reflected class: the player reuses bytes
// +0xa8..+0xb0, so embedding the full base would put every derived field eight
// bytes late.
struct ChrDamageModuleFields {
    SprjChrModuleBase super_chr_module;
    // Constructor clears bit zero without initializing the other bits.
    std::uint8_t flags;
    std::uint8_t _unk11[7];
    // Explicit cleanup frees this through its owning heap and clears it.
    void* owned_buffer;
    ChrDamageRecord records[8];
    std::uint32_t value_60_bits;
    // Initialized to the bits of -1.0f.
    std::uint32_t value_64_bits;
    std::uint32_t value_68_bits;
    // Initialized to the bits of -1.0f.
    std::uint32_t value_6c_bits;
    std::uint64_t value_70_bits;
    std::uint64_t value_78_bits;
    std::uint8_t flag_80;
    std::uint8_t _unk81[3];
    std::uint32_t value_84_bits;
    std::uint8_t _unk88[8];
    std::uint64_t values_90_bits[3];
};

// Reflected base, 0xb0 bytes, allocated aligned to 16. Container +0x98 points
// to this base subobject. Event handling uses its owner and virtual methods; no
// callable ABI is inferred from decompiler signatures.
struct alignas(16) SprjChrDamageModule {
    ChrDamageModuleFields fields;
    std::uint8_t _tail_padding[8];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_DAMAGE_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0xb0;
    static constexpr Rva INSTANCE_VTABLE{0x5397930};
    static constexpr Rva SIZE_FN{0x1a313d0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a29210};
    // The base virtual destructor is empty; explicit cleanup is separate.
    static constexpr Rva DESTRUCTOR_FN{0x1a29300};
    static constexpr Rva CLEAR_RESOURCES_FN{0x1a29310};
    static constexpr Rva APPLY_DAMAGE_EVENT_FN = SPRJ_CHR_DAMAGE_MODULE_APPLY_DAMAGE_EVENT_FN;
    static constexpr Rva RECEIVE_DAMAGE_EVENTS_FN = SPRJ_CHR_DAMAGE_MODULE_RECEIVE_DAMAGE_EVENTS_FN;
};

// Enemy allocation adds no storage to the damage base.
struct SprjEnemyDamageModule {
    SprjChrDamageModule super_damage_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_DAMAGE_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0xb0;
    static constexpr Rva INSTANCE_VTABLE{0x5397aa0};
    static constexpr Rva SIZE_FN{0x1a32410};
    // Virtual +0x20 obtains a coefficient from NPC parameter byte +0x12b.
    static constexpr Rva COEFFICIENT_FN{0x1a316c0};
};

// Reflected player module, 0x320 bytes, alignment 16. Native inheritance links
// SprjChrDamageModule, but reuses its tail padding. The 0x260-byte inline
// state is initialized by RVA 0x1862860; its identity and fields are
// unclassified.
struct alignas(16) SprjPlayerDamageModule {
    ChrDamageModuleFields fields;
    // Virtual +0x18 increments this byte unless already 0xff.
    std::uint8_t counter_a8;
    std::uint8_t _unk_a9[3];
    // Initialized to -1.0f bits; virtual +0x18 copies a native global here.
    std::uint32_t value_ac_bits;
    Unknown<0x260> inline_state;
    // Initialized to 20.0f bits.
    std::uint32_t value_310_bits;
    // Initialized to 0.1f bits.
    std::uint32_t value_314_bits;
    std::uint8_t _unk318[8];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_DAMAGE_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x320;
    static constexpr Rva INSTANCE_VTABLE{0x5397c10};
    static constexpr Rva SIZE_FN{0x1a35600};
    static constexpr Rva CONSTRUCTOR_FN{0x1a327e0};
    static constexpr Rva ALLOCATE_FN{0x1a5b740};
    static constexpr Rva UPDATE_COUNTER_FN{0x1a354a0};
    // Virtual +0x20 combines equipment-parameter bytes +0xcf with weights.
    static constexpr Rva COEFFICIENT_FN{0x1a33b90};
    static constexpr Rva INLINE_STATE_CONSTRUCTOR_FN{0x1862860};
};

namespace detail::chr_damage_module_layout {
BB_SIZE(ChrDamageModuleFields, 0xa8);
BB_SIZE(ChrDamageRecord, 8);
BB_OFFSET(ChrDamageRecord, flag, 4);
BB_OFFSET(ChrDamageModuleFields, flags, 0x10);
BB_OFFSET(ChrDamageModuleFields, owned_buffer, 0x18);
BB_OFFSET(ChrDamageModuleFields, records, 0x20);
BB_OFFSET(ChrDamageModuleFields, value_60_bits, 0x60);
BB_OFFSET(ChrDamageModuleFields, value_64_bits, 0x64);
BB_OFFSET(ChrDamageModuleFields, value_68_bits, 0x68);
BB_OFFSET(ChrDamageModuleFields, value_6c_bits, 0x6c);
BB_OFFSET(ChrDamageModuleFields, value_70_bits, 0x70);
BB_OFFSET(ChrDamageModuleFields, value_78_bits, 0x78);
BB_OFFSET(ChrDamageModuleFields, flag_80, 0x80);
BB_OFFSET(ChrDamageModuleFields, value_84_bits, 0x84);
BB_OFFSET(ChrDamageModuleFields, values_90_bits, 0x90);
BB_SIZE(SprjChrDamageModule, SprjChrDamageModule::SIZE);
BB_SIZE(SprjEnemyDamageModule, SprjEnemyDamageModule::SIZE);
BB_SIZE(SprjPlayerDamageModule, SprjPlayerDamageModule::SIZE);
static_assert(alignof(SprjChrDamageModule) == 16, "alignof(SprjChrDamageModule)");
static_assert(alignof(SprjEnemyDamageModule) == 16, "alignof(SprjEnemyDamageModule)");
static_assert(alignof(SprjPlayerDamageModule) == 16, "alignof(SprjPlayerDamageModule)");
BB_OFFSET(SprjEnemyDamageModule, super_damage_module, 0);
BB_OFFSET(SprjPlayerDamageModule, fields, 0);
BB_OFFSET(SprjPlayerDamageModule, counter_a8, 0xa8);
BB_OFFSET(SprjPlayerDamageModule, value_ac_bits, 0xac);
BB_OFFSET(SprjPlayerDamageModule, inline_state, 0xb0);
BB_OFFSET(SprjPlayerDamageModule, value_310_bits, 0x310);
BB_OFFSET(SprjPlayerDamageModule, value_314_bits, 0x314);
}  // namespace detail::chr_damage_module_layout

}  // namespace bb
