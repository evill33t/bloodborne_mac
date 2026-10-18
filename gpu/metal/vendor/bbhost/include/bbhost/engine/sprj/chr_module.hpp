// Character modules: the 64-slot module container reached from ChrIns+0x3b0,
// the shared SprjChrModuleBase prefix, and the per-module layouts (data,
// action, behavior, physics, ...). Combat modules are in chr_combat_module.hpp,
// damage modules in chr_damage_module.hpp.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/hit_floor_info.hpp"
#include "bbhost/engine/cs/slope_ctrl.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct ChrIns;
struct SprjChrDamageModule;
struct SprjChrHitStopModule;
struct SprjChrKnockBackModule;
struct SprjChrMagicModule;
struct SprjChrSfxModule;

inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_PREFIX_SIZE = 0x90;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SIZE = 0x208;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT_COUNT = 64;
inline constexpr Rva CHR_INS_CONSTRUCT_FN{0x18b9ae0};
inline constexpr Rva CHR_INS_MODULE_CONTAINER_DESTRUCTOR_FN{0x1a74cb0};
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_BEHAVIOR_SCRIPT_OFFSET = 0x10;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_TIME_ACT_OFFSET = 0x18;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_RESIST_OFFSET = 0x28;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_BEHAVIOR_OFFSET = 0x30;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_BEHAVIOR_SYNC_OFFSET = 0x38;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_AI_OFFSET = 0x40;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SUPER_ARMOR_OFFSET = 0x48;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_TALK_OFFSET = 0x50;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_EVENT_OFFSET = 0x58;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_ACTION_REQUEST_OFFSET = 0x80;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_THROW_OFFSET = 0x88;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT08_OFFSET = 0x08;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_DATA_OFFSET = 0x20;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT30_OFFSET = 0x30;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT50_OFFSET = 0x50;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT58_OFFSET = 0x58;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT68_OFFSET = 0x68;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_PHYSICS_OFFSET = 0x68;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_FALL_OFFSET = 0x70;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_LADDER_OFFSET = 0x78;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_MATERIAL_OFFSET = 0xa0;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_MAGIC_OFFSET = 0x60;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_HIT_STOP_OFFSET = 0x90;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_DAMAGE_OFFSET = 0x98;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_KNOCK_BACK_OFFSET = 0xa8;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SFX_OFFSET = 0xb0;
// Installs enemy-specific modules, destroying and freeing prior slot contents.
inline constexpr Rva NPC_INS_INSTALL_MODULES_FN{0x18e7e80};
// Installs player-specific modules, destroying and freeing prior slot contents.
inline constexpr Rva PLAYER_INS_INSTALL_MODULES_FN{0x1906ed0};
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT78_OFFSET = 0x78;
inline constexpr std::size_t CHR_INS_MODULE_CONTAINER_SLOT88_OFFSET = 0x88;
inline constexpr std::size_t CHR_MODULE_BASE_SIZE = 0x10;
inline constexpr std::size_t CHR_PHYSICS_MODULE_SIZE = 0x3d0;
inline constexpr std::size_t CHR_ACTION_FLAG_MODULE_SIZE = 0x838;
inline constexpr std::size_t CHR_ACTION_FLAG_MODULE_ACTION_ID_OFFSET = 0x1b4;
inline constexpr std::size_t CHR_ACTION_FLAG_MODULE_ACTION_SUBSTATE_OFFSET = 0x1b8;
inline constexpr std::size_t CHR_DATA_MODULE_PREFIX_SIZE = 0x208;
inline constexpr std::size_t CHR_DATA_MODULE_OWNER_OFFSET = 0x08;
inline constexpr std::size_t CHR_DATA_MODULE_MAP_CONTEXT_OFFSET = 0x28;
inline constexpr std::size_t CHR_DATA_MODULE_WORLD_BLOCK_INDEX_OFFSET = 0x30;
inline constexpr std::size_t CHR_DATA_MODULE_RELATED_CHR_OFFSET = 0xf0;
inline constexpr std::size_t CHR_DATA_MODULE_HP_OFFSET = 0xf8;
inline constexpr std::size_t CHR_DATA_MODULE_MAX_HP_OFFSET = 0xfc;
inline constexpr std::size_t CHR_DATA_MODULE_BASE_HP_OFFSET = 0x100;
// Baseline used to add positive max-HP adjustments to current HP; distinct
// from max HP when an effect opts out of current-HP adjustment.
inline constexpr std::size_t CHR_DATA_MODULE_HP_ADJUSTMENT_BASE_OFFSET = 0x104;
// Takes the HP block at data module +0xf0 (not the actor or module).
// Recalculates max HP from base HP and SpEffects, clamps current HP, and adds
// positive baseline changes unless the effect opts out.
inline constexpr Rva CHR_HP_BLOCK_RECALCULATE_FN{0x1a0d7d0};
// Live stat update, including the HP-block recalculation for living actors.
inline constexpr Rva CHR_DATA_MODULE_REFRESH_EFFECT_STATS_FN{0x1a572e0};
// NPC initialization fills HP and other gauges/baselines; not a refresh call
// for an already injured actor.
inline constexpr Rva CHR_DATA_MODULE_INITIALIZE_NPC_STATS_FN{0x1a56c00};
inline constexpr std::size_t CHR_DATA_MODULE_UNCONFIRMED_HP_LIKE_128_OFFSET = 0x128;
inline constexpr std::size_t CHR_DATA_MODULE_UNCONFIRMED_HP_LIKE_12C_OFFSET = 0x12c;
inline constexpr std::size_t CHR_DATA_MODULE_NAME_OFFSET = 0x158;
inline constexpr std::size_t CHR_DATA_MODULE_NAME_SIZE = 0x20;
inline constexpr std::size_t CHR_DATA_MODULE_FLAGS_OFFSET = 0x200;
inline constexpr std::size_t CHR_DATA_MODULE_MAP_CONTEXT_PREFIX_SIZE = 0x344;
inline constexpr std::size_t CHR_DATA_MODULE_MAP_CONTEXT_INDEX_BASE_OFFSET = 0x29c;
inline constexpr std::size_t CHR_DATA_MODULE_MAP_CONTEXT_BLOCK_ID_OFFSET = 0x340;

// Applies a local or received damage event. The attacker actor is passed
// explicitly: the stable lethal-attribution boundary.
inline constexpr Rva SPRJ_CHR_DAMAGE_MODULE_APPLY_DAMAGE_EVENT_FN{0x1a29e40};
// Consumes packet 0x14 per peer route and resolves the sending ObjectRef to the
// attacker actor before dispatching the damage event.
inline constexpr Rva SPRJ_CHR_DAMAGE_MODULE_RECEIVE_DAMAGE_EVENTS_FN{0x1a341b0};

struct SprjChrActionFlagModule;
struct SprjChrBehaviorScriptModule;
struct SprjChrTimeActModule;
struct SprjChrDataModule;
struct SprjChrResistModule;
struct SprjChrBehaviorModule;
struct SprjChrBehaviorSyncModule;
struct SprjChrAiModule;
struct SprjChrSuperArmorModule;
struct SprjChrTalkModule;
struct SprjChrEventModule;
struct SprjChrPhysicsModule;
struct SprjChrFallModule;
struct SprjChrLadderModule;
struct SprjChrActionRequestModule;
struct SprjChrThrowModule;
struct SprjChrMaterialModule;
using ChrDataModule = SprjChrDataModule;

// Owning module pointer table at ChrIns+0x3b0, 0x208 bytes. ChrIns_Construct
// (RVA 0x18b9ae0) allocates a vtable plus 64 pointers, clears them and installs
// the modules below. Destructor RVA 0x1a74cb0 visits all 64 slots, destroys
// each module, frees it through its heap and clears the slot.
// CHR_INS_MODULE_CONTAINER_PREFIX_SIZE is the older captured extent.
struct ChrInsModuleContainer {
    const void* vftable;
    SprjChrActionFlagModule* action_flag;
    SprjChrBehaviorScriptModule* behavior_script;
    SprjChrTimeActModule* time_act;
    SprjChrDataModule* data;
    SprjChrResistModule* resist;
    SprjChrBehaviorModule* behavior;
    SprjChrBehaviorSyncModule* behavior_sync;
    SprjChrAiModule* ai;
    SprjChrSuperArmorModule* super_armor;
    SprjChrTalkModule* talk;
    SprjChrEventModule* event;
    SprjChrMagicModule* magic;
    SprjChrPhysicsModule* physics;
    SprjChrFallModule* fall;
    SprjChrLadderModule* ladder;
    SprjChrActionRequestModule* action_request;
    SprjChrThrowModule* throw_;  // `throw` is a C++ keyword
    SprjChrHitStopModule* hit_stop;
    SprjChrDamageModule* damage;
    SprjChrMaterialModule* material;
    SprjChrKnockBackModule* knock_back;
    SprjChrSfxModule* sfx;
    void* unclassified_slots[42];  // modules not classified yet

    // The data module at 0x20, or null.
    ChrDataModule* data_module() const { return data; }
};

// Shared prefix of character modules: DS3's ChrModuleBase vtable/owner shape,
// under Bloodborne's runtime-class name SprjChrModuleBase.
struct SprjChrModuleBase {
    void* vftable;
    ChrIns* owner;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_MODULE_BASE_RUNTIME_CLASS;
};
using ChrModuleBase = SprjChrModuleBase;

// Slot-0x08 character action module. action_id/action_substate are named
// from live death transitions: idle actors use [99999, 2]; a native guest death
// entered [2, 3] while an incompletely activated replacement entered [2, 0].
struct SprjChrActionFlagModule {
    SprjChrModuleBase super_chr_module;
    Unknown<0x1a4> _unk10;
    std::int32_t action_id;
    std::int32_t action_substate;
    Unknown<0x67c> _unk1bc;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_ACTION_FLAG_MODULE_RUNTIME_CLASS;
};

// Action-request masks and held-time state at container +0x80. Constructor
// RVA 0x1a0f7d0 and size RVA 0x1a10d90 establish 0xd0 bytes. Frame start
// 0x1a0f8e0 copies current to previous, clears current/delivered and resets
// value_98/flags_9c. Update 0x1a0f910 computes rising/falling masks,
// accumulates 16 held times, processes buffer masks and snapshots five masks
// at +0xa8. Action-bit meanings are unclassified.
struct SprjChrActionRequestModule {
    SprjChrModuleBase super_chr_module;
    std::uint64_t current_requests;
    std::uint64_t previous_requests;
    std::uint64_t rising_requests;
    std::uint64_t falling_requests;
    std::uint64_t delivered_requests;
    std::uint64_t buffered_requests;
    std::uint64_t buffer_arm_mask;
    std::uint64_t buffer_delivery_mask;
    // Release edges keep the last duration for that frame; the next inactive
    // frame clears it. Only the first 16 request bits have counters.
    float held_times[16];
    // Accumulates frame delta while flags_9c & 1; otherwise zero.
    float flagged_time;
    std::int32_t value_94;
    std::int32_t value_98;
    std::uint32_t flags_9c;
    std::uint32_t flags_a0;
    std::uint32_t _pad_a4;
    // Rising, delivered, buffered, arm and delivery masks before update clears.
    std::uint64_t mask_snapshot[5];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_ACTION_REQUEST_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0xd0;
    static constexpr Rva INSTANCE_VTABLE{0x5334350};
    static constexpr Rva SIZE_FN{0x1a10d90};
    static constexpr Rva CONSTRUCTOR_FN{0x1a0f7d0};
    static constexpr Rva BEGIN_FRAME_FN{0x1a0f8e0};
    static constexpr Rva UPDATE_FN{0x1a0f910};
};

// Behavior owner at container +0x30, allocated 0x390 aligned to 16.
// ChrIns_Construct initializes the opaque inline state at +0x60 through RVA
// 0x1ddf290. Consumer RVA 0x1a1df10 uses the behavior object at +0x10 to
// update SYS_IgnoreAllScript and SYS_IgnoreAutoAnimeEndEvent. Script dispatch
// at 0x1a1d2d0 gates on byte +0x384.
struct alignas(16) SprjChrBehaviorModule {
    SprjChrModuleBase super_chr_module;
    void* behavior_object;
    std::uint8_t _unk18[0x48];
    std::uint8_t _inline_behavior_state[0x2a8];
    std::uint8_t _unk308[0x7c];
    std::uint8_t script_dispatch_enabled;
    std::uint8_t _unk385[0x0b];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_BEHAVIOR_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x390;
    static constexpr Rva INSTANCE_VTABLE{0x5335030};
    static constexpr Rva SIZE_FN{0x1a1cd50};
};

// Descriptive view of a separately allocated script context (not an RTTI name).
struct SprjChrBehaviorScriptContext {
    ChrIns* owner;
};

// Behavior-script bridge at container +0x10, 0x20 bytes. Constructor RVA
// 0x1a1d150 owns two actor-pointer contexts; dispatch RVA 0x1a1d2d0 forwards
// through the first after checking the actor's behavior module. The second
// context's role is unknown.
struct SprjChrBehaviorScriptModule {
    SprjChrModuleBase super_chr_module;
    SprjChrBehaviorScriptContext* script_context;
    SprjChrBehaviorScriptContext* context_18;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_BEHAVIOR_SCRIPT_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x53350f0};
    static constexpr Rva SIZE_FN{0x1a1dce0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a1d150};
    static constexpr Rva DISPATCH_FN{0x1a1d2d0};
};

// Behavior synchronization at container +0x38, 0x20 bytes. Update RVA
// 0x1a1df10 stores its input at +0x10 and computes script-ignore gates from
// the actor's controller, set entry and action flags. The final bytes start as
// [1, 1, 0, 1]; their roles are unclassified.
struct SprjChrBehaviorSyncModule {
    SprjChrModuleBase super_chr_module;
    std::uint64_t update_value;
    std::uint8_t flags_18[4];
    std::uint8_t _unk1c[4];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_BEHAVIOR_SYNC_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva INSTANCE_VTABLE{0x5335120};
    static constexpr Rva SIZE_FN{0x1a20450};
    static constexpr Rva UPDATE_SCRIPT_GATES_FN{0x1a1df10};
};

// Resistance at container +0x28, 0x58 bytes. Consumer RVA 0x1a5c240 gets five
// limits through owner virtual +0x1e0 and clamps five signed integer gauges.
struct SprjChrResistModule {
    SprjChrModuleBase super_chr_module;
    std::int32_t current[5];
    std::int32_t limits[5];
    std::uint8_t _unk38[0x18];
    std::uint8_t flag_50;
    std::uint8_t _unk51[7];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_RESIST_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x58;
    static constexpr Rva INSTANCE_VTABLE{0x5335900};
    static constexpr Rva SIZE_FN{0x1a5d460};
    static constexpr Rva LOAD_GAUGES_FN{0x1a5c240};
};

// AI module at container +0x40: allocation and reflected size 0x10, no data
// beyond the shared header.
struct SprjChrAiModule {
    SprjChrModuleBase super_chr_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_AI_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x10;
    static constexpr Rva INSTANCE_VTABLE{0x53357e0};
    static constexpr Rva SIZE_FN{0x1a55ec0};
};

// Talk bridge at container +0x50, 0x18 bytes. Consumer RVA 0x1a5e5f0 routes an
// actor/target update through SprjWorldTalkMan. +0x10/+0x11 meanings unknown.
struct SprjChrTalkModule {
    SprjChrModuleBase super_chr_module;
    std::uint8_t flags_10[2];
    std::uint8_t _unk12[6];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_TALK_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53359c0};
    static constexpr Rva SIZE_FN{0x1a5f020};
    static constexpr Rva UPDATE_TARGET_FN{0x1a5e5f0};
};

// Event module at container +0x58, 0x30 bytes. Constructor RVA 0x1a59020
// initializes two eight-byte records and a sentinel, then allocates a separate
// 0x60 state block aligned to 16. Set/reset 0x1a59190/0x1a591c0 mirror value_20
// to the actor's event state and gate on behavior-module byte +0x384; reset
// also requests Idle_wild.
struct SprjChrEventModule {
    SprjChrModuleBase super_chr_module;
    std::uint8_t record_10[8];
    std::uint8_t record_18[8];
    std::int32_t value_20;
    std::uint8_t _unk24[4];
    Unknown<0x60>* state;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_EVENT_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva INSTANCE_VTABLE{0x53358b0};
    static constexpr Rva SIZE_FN{0x1a59f40};
    static constexpr Rva CONSTRUCTOR_FN{0x1a59020};
    static constexpr Rva SET_EVENT_VALUE_FN{0x1a59190};
    static constexpr Rva RESET_EVENT_VALUE_FN{0x1a591c0};
};

// Fall-module base at container +0x70, 0x10 bytes. Constructor RVA 0x1a359d0
// stores only vtable and owner. Enemy and player calculate fall damage
// differently.
struct SprjChrFallModule {
    SprjChrModuleBase super_chr_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_FALL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x10;
    static constexpr Rva INSTANCE_VTABLE{0x5397d80};
    static constexpr Rva SIZE_FN{0x1a36890};
    static constexpr std::size_t NATIVE_SIZE = SIZE;
    static constexpr Rva CONSTRUCTOR_FN{0x1a359d0};
};

// Enemy fall calculation reads the NPC parameter through owner virtual +0x168,
// including its reduction byte at parameter +0x120.
struct SprjEnemyFallModule {
    SprjChrFallModule super_fall_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_FALL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x10;
    static constexpr Rva INSTANCE_VTABLE{0x5335240};
    static constexpr Rva SIZE_FN{0x1a37430};
    static constexpr Rva CONSTRUCTOR_FN{0x1a36a80};
    static constexpr Rva CALCULATE_FALL_DAMAGE_FN{0x1a36ab0};
};

// Player fall calculation includes effect multipliers at effect parameter
// +0xdc and owner data reached through virtual +0x1c8.
struct SprjPlayerFallModule {
    SprjChrFallModule super_fall_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_FALL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x10;
    static constexpr Rva INSTANCE_VTABLE{0x5335270};
    static constexpr Rva SIZE_FN{0x1a38010};
    static constexpr Rva CALCULATE_FALL_DAMAGE_FN{0x1a37650};
};

// Ladder-module base at container +0x78, 0x18 bytes. Constructor RVA 0x1a3d100
// clears byte +0x10 (meaning unclassified). Shared predicates inspect the
// owner's controller and event-module state.
struct SprjChrLadderModule {
    SprjChrModuleBase super_chr_module;
    std::uint8_t flag_10;
    std::uint8_t _unk11[7];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_LADDER_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53982a0};
    static constexpr Rva SIZE_FN{0x1a3e450};
    static constexpr std::size_t NATIVE_SIZE = SIZE;
    static constexpr Rva CONSTRUCTOR_FN{0x1a3d100};
    static constexpr Rva QUERY_LADDER_STATE_FN{0x1a3d140};
};

// Enemy implementation; adds no fields.
struct SprjEnemyLadderModule {
    SprjChrLadderModule super_ladder_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_LADDER_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53354b0};
    static constexpr Rva SIZE_FN{0x1a3f180};
};

// Player implementation; adds no fields.
struct SprjPlayerLadderModule {
    SprjChrLadderModule super_ladder_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_LADDER_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53354e0};
    static constexpr Rva SIZE_FN{0x1a3fca0};
};

// Material-module base at container +0xa0, 0x18 bytes. Constructor RVA
// 0x1a437a0 clears the eight bytes after the owner. 0x1a437e0 stores an input
// modulo 100 at +0x10 when physics permits; 0x1a43940 uses it to pick a
// material parameter; 0x1a43820 writes the next two values (actor-flag
// override 0x33). Categories unclassified.
struct SprjChrMaterialModule {
    SprjChrModuleBase super_chr_module;
    std::uint16_t surface_material;
    std::uint16_t material_12;
    std::uint16_t material_14;
    std::uint8_t _unk16[2];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_MATERIAL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53986a0};
    static constexpr Rva SIZE_FN{0x1a44340};
    static constexpr std::size_t NATIVE_SIZE = SIZE;
    static constexpr Rva CONSTRUCTOR_FN{0x1a437a0};
    static constexpr Rva UPDATE_SURFACE_MATERIAL_FN{0x1a437e0};
    static constexpr Rva UPDATE_MATERIAL_PAIR_FN{0x1a43820};
};

// Material-pair lookup reads NPC parameter bytes +0x134/+0x135.
struct SprjEnemyMaterialModule {
    SprjChrMaterialModule super_material_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ENEMY_MATERIAL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x53355d0};
    static constexpr Rva SIZE_FN{0x1a44ee0};
    static constexpr Rva QUERY_MATERIAL_PAIR_FN{0x1a44570};
};

// Material-pair lookup uses an ID at +0x84 of owner virtual +0x2e8's result,
// resolves a parameter and reads its bytes +0xd6/+0xd7.
struct SprjPlayerMaterialModule {
    SprjChrMaterialModule super_material_module;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_PLAYER_MATERIAL_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva INSTANCE_VTABLE{0x5335600};
    static constexpr Rva SIZE_FN{0x1a45aa0};
    static constexpr Rva QUERY_MATERIAL_PAIR_FN{0x1a45110};
};

// Character physics owner with two inline floor records and slope state.
// Constructor RVA 0x1a48fe0, allocator callsite RVA 0x18ba9ae and size method
// RVA 0x1a50410 establish 0x3d0 bytes. The constructor binds
// slope_ctrl.floor_info to this object's floor_info, so instances must stay at
// their allocated addresses. The tail is opaque.
struct alignas(16) SprjChrPhysicsModule {
    SprjChrModuleBase super_chr_module;
    std::uint8_t _unk10[0x60];
    CSHitFloorInfo floor_info;
    // Second contact record; how it differs from the first is not established.
    CSHitFloorInfo secondary_floor_info;
    CSSlopeCtrl slope_ctrl;
    std::uint8_t _unk1d0[0x200];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_PHYSICS_MODULE_RUNTIME_CLASS;
};

// Super-armor state at container +0x48, 0x28 bytes. Constructor RVA 0x1a5d860
// clears the floats and break bytes. Update 0x1a5d8b0 gets the owner's limit,
// applies effect modifiers, decrements recovery time and restores current.
// Damage consumer 0x1a5d9f0 subtracts damage, charges overflow against
// overflow_reserve and sets the break byte on exhaustion (or an explicit
// damage-event flag).
struct SprjChrSuperArmorModule {
    SprjChrModuleBase super_chr_module;
    float current;
    float previous_limit;
    // Secondary reserve charged when damage exceeds current.
    float overflow_reserve;
    float recovery_time;
    std::uint8_t broken;
    std::uint8_t previously_broken;
    // Constructor clears bit zero, preserving the other bits.
    std::uint8_t flags_22;
    std::uint8_t _unk23[5];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_SUPER_ARMOR_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva INSTANCE_VTABLE{0x5335990};
    static constexpr Rva SIZE_FN{0x1a5e370};
    static constexpr Rva CONSTRUCTOR_FN{0x1a5d860};
    static constexpr Rva UPDATE_FN{0x1a5d8b0};
    static constexpr Rva APPLY_DAMAGE_FN{0x1a5d9f0};
};

// Minimum view of a throw-manager state record (not a full allocation or
// reflected class). Consumers 0x1a60390/0x1a604b0 establish the actor pointer,
// phase byte and packed counterpart handle.
struct SprjChrThrowStatePrefix {
    std::uint8_t _unk00[8];
    ChrIns* owner;
    std::uint8_t _unk10[0x10];
    std::uint8_t phase;
    std::uint8_t _unk21[3];
    ChrHandle counterpart;
};

// Throw module at container +0x88, allocated 0x70 aligned to 16. Constructor
// RVA 0x1a5fd20 initializes three eight-byte records and the 16-byte region at
// +0x30, then clears most state bytes at +0x40. Reset 0x1a60110 restores
// controller state and clears the throw phase. Paired-phase update 0x1a604b0
// resolves the counterpart's ChrHandle through WorldChrMan before writing
// phases 5/6 under a native manager lock.
struct alignas(16) SprjChrThrowModule {
    SprjChrModuleBase super_chr_module;
    std::uint8_t records[3][8];
    SprjChrThrowStatePrefix* throw_state;
    std::uint8_t value_30[16];
    std::uint8_t _unk40[0x30];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_THROW_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x70;
    static constexpr Rva INSTANCE_VTABLE{0x5335a20};
    static constexpr Rva SIZE_FN{0x1a62240};
    static constexpr Rva CONSTRUCTOR_FN{0x1a5fd20};
    static constexpr Rva RESET_FN{0x1a60110};
    static constexpr Rva UPDATE_PAIRED_PHASES_FN{0x1a604b0};
};

// Descriptive name for the actor-bound TimeAct dispatch context.
struct SprjChrTimeActContext {
    const void* vftable;
    std::uint64_t value_08;
    ChrIns* owner;
};

// Ring record, initialized to [-1, 0, 0, 1.0]. The first value selects a
// resource entry; the argument types are unanalysed, so raw bits are kept.
struct SprjChrTimeActRecord {
    std::int32_t id;
    std::uint32_t argument_bits[3];
};

// TimeAct dispatch at container +0x18, 0xd8 bytes. Constructor RVA 0x1a27510
// allocates an actor-bound 0x18 dispatch context and initializes ten 0x10
// records. Consumer 0x1a27810 walks records between the cursors modulo ten,
// looks their IDs up in the resource at +0x10, and dispatches through the
// context when SprjHkBehManager permits.
struct SprjChrTimeActModule {
    SprjChrModuleBase super_chr_module;
    void* resource;
    SprjChrTimeActContext* dispatch_context;
    SprjChrTimeActRecord records[10];
    std::int32_t end_cursor;
    std::int32_t begin_cursor;
    std::uint8_t _unkc8[8];
    std::uint8_t flag_d0;
    std::uint8_t _unkd1[7];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_TIME_ACT_MODULE_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0xd8;
    static constexpr Rva INSTANCE_VTABLE{0x5335190};
    static constexpr Rva SIZE_FN{0x1a28c30};
    static constexpr Rva CONSTRUCTOR_FN{0x1a27510};
    static constexpr Rva DISPATCH_PENDING_FN{0x1a27810};
};

// Small-string-shaped character ID storage at data module +0x158 (UTF-16).
struct ChrDataModuleName {
    std::uint16_t inline_or_heap[8];
    std::size_t length;
    std::size_t capacity;

    // The UTF-16 characters (`*count` of them, no terminator), or null for
    // impossible small-string metadata or a null heap pointer. An empty name
    // returns a non-null pointer with count 0.
    const std::uint16_t* as_utf16(std::size_t* count) const {
        *count = 0;
        if (length == 0) return inline_or_heap;
        if (capacity <= 7) {
            if (length > 7) return nullptr;
            *count = length;
            return inline_or_heap;
        }
        const std::uint16_t* ptr;
        __builtin_memcpy(&ptr, inline_or_heap, sizeof(ptr));
        if (!ptr) return nullptr;
        *count = length;
        return ptr;
    }
};

// Prefix of the context object used to derive world/block handles.
struct ChrDataModuleMapContext {
    Unknown<0x29c> _unk00;
    std::int32_t world_block_index_base;
    Unknown<0xa0> _unk2a0;
    std::uint32_t block_id;
};

// Prefix of the character data module used by world/block lookup paths.
struct SprjChrDataModule {
    SprjChrModuleBase super_chr_module;
    Unknown<0x18> _unk10;
    ChrDataModuleMapContext* map_context;
    std::int32_t world_block_index;
    Unknown<0xbc> _unk34;
    ChrIns* related_chr;
    std::int32_t hp;
    std::int32_t max_hp;
    Unknown<0x28> _unk100;
    std::int32_t unconfirmed_hp_like_128;
    std::int32_t unconfirmed_hp_like_12c;
    Unknown<0x28> _unk130;
    ChrDataModuleName name;
    Unknown<0x88> _unk178;
    std::uint16_t flags;
    Unknown<0x06> _unk202;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CHR_DATA_MODULE_RUNTIME_CLASS;

    ChrIns* owner() const { return super_chr_module.owner; }
    // The character ID (of the form c1234) as UTF-16; see ChrDataModuleName.
    const std::uint16_t* id(std::size_t* count) const { return name.as_utf16(count); }
    // hp / max_hp; false (out untouched) when max_hp is not positive.
    bool hp_ratio(float* out) const {
        if (max_hp <= 0) return false;
        *out = static_cast<float>(hp) / static_cast<float>(max_hp);
        return true;
    }
    // Whether the observed script helper treats this character as dead.
    bool is_dead() const { return hp < 1; }
    void kill() { hp = 0; }
    // The pair at +0x128/+0x12c. Live testing showed it does not track current
    // HP when the player takes damage in multiplayer: diagnostic only.
    bool has_descending_unconfirmed_hp_like_pair() const {
        return unconfirmed_hp_like_12c < unconfirmed_hp_like_128 && (flags & 0x0100) != 0;
    }
    void clamp_unconfirmed_hp_like_128_to_12c() {
        unconfirmed_hp_like_128 = unconfirmed_hp_like_12c > 0 ? unconfirmed_hp_like_12c : 0;
    }
    // world_block_index minus the context's base index; false without a
    // context or on signed overflow.
    bool world_block_index_delta(std::int32_t* out) const {
        if (!map_context) return false;
        return !__builtin_sub_overflow(world_block_index, map_context->world_block_index_base, out);
    }
};

namespace detail::chr_module_layout {
BB_SIZE(SprjChrFallModule, SprjChrFallModule::SIZE);
BB_SIZE(SprjEnemyFallModule, SprjEnemyFallModule::SIZE);
BB_SIZE(SprjPlayerFallModule, SprjPlayerFallModule::SIZE);
BB_SIZE(SprjChrLadderModule, SprjChrLadderModule::SIZE);
BB_SIZE(SprjEnemyLadderModule, SprjEnemyLadderModule::SIZE);
BB_SIZE(SprjPlayerLadderModule, SprjPlayerLadderModule::SIZE);
BB_SIZE(SprjChrMaterialModule, SprjChrMaterialModule::SIZE);
BB_SIZE(SprjEnemyMaterialModule, SprjEnemyMaterialModule::SIZE);
BB_SIZE(SprjPlayerMaterialModule, SprjPlayerMaterialModule::SIZE);
BB_OFFSET(SprjEnemyFallModule, super_fall_module, 0);
BB_OFFSET(SprjPlayerFallModule, super_fall_module, 0);
BB_OFFSET(SprjEnemyLadderModule, super_ladder_module, 0);
BB_OFFSET(SprjPlayerLadderModule, super_ladder_module, 0);
BB_OFFSET(SprjEnemyMaterialModule, super_material_module, 0);
BB_OFFSET(SprjPlayerMaterialModule, super_material_module, 0);
BB_OFFSET(SprjChrModuleBase, owner, CHR_DATA_MODULE_OWNER_OFFSET);
BB_OFFSET(SprjChrLadderModule, flag_10, 0x10);
BB_OFFSET(SprjChrMaterialModule, surface_material, 0x10);
BB_OFFSET(SprjChrMaterialModule, material_12, 0x12);
BB_OFFSET(SprjChrMaterialModule, material_14, 0x14);
static_assert(alignof(SprjPlayerMaterialModule) == 8, "alignof(SprjPlayerMaterialModule)");

BB_SIZE(ChrInsModuleContainer, CHR_INS_MODULE_CONTAINER_SIZE);
BB_SIZE(ChrInsModuleContainer, 8 + CHR_INS_MODULE_CONTAINER_SLOT_COUNT * 8);
BB_OFFSET(ChrInsModuleContainer, action_flag, CHR_INS_MODULE_CONTAINER_SLOT08_OFFSET);
BB_OFFSET(ChrInsModuleContainer, behavior_script, CHR_INS_MODULE_CONTAINER_BEHAVIOR_SCRIPT_OFFSET);
BB_OFFSET(ChrInsModuleContainer, time_act, CHR_INS_MODULE_CONTAINER_TIME_ACT_OFFSET);
BB_OFFSET(ChrInsModuleContainer, data, CHR_INS_MODULE_CONTAINER_DATA_OFFSET);
BB_OFFSET(ChrInsModuleContainer, resist, CHR_INS_MODULE_CONTAINER_RESIST_OFFSET);
BB_OFFSET(ChrInsModuleContainer, behavior, CHR_INS_MODULE_CONTAINER_BEHAVIOR_OFFSET);
BB_OFFSET(ChrInsModuleContainer, behavior, CHR_INS_MODULE_CONTAINER_SLOT30_OFFSET);
BB_OFFSET(ChrInsModuleContainer, behavior_sync, CHR_INS_MODULE_CONTAINER_BEHAVIOR_SYNC_OFFSET);
BB_OFFSET(ChrInsModuleContainer, ai, CHR_INS_MODULE_CONTAINER_AI_OFFSET);
BB_OFFSET(ChrInsModuleContainer, super_armor, CHR_INS_MODULE_CONTAINER_SUPER_ARMOR_OFFSET);
BB_OFFSET(ChrInsModuleContainer, talk, CHR_INS_MODULE_CONTAINER_TALK_OFFSET);
BB_OFFSET(ChrInsModuleContainer, talk, CHR_INS_MODULE_CONTAINER_SLOT50_OFFSET);
BB_OFFSET(ChrInsModuleContainer, event, CHR_INS_MODULE_CONTAINER_EVENT_OFFSET);
BB_OFFSET(ChrInsModuleContainer, event, CHR_INS_MODULE_CONTAINER_SLOT58_OFFSET);
BB_OFFSET(ChrInsModuleContainer, magic, CHR_INS_MODULE_CONTAINER_MAGIC_OFFSET);
BB_OFFSET(ChrInsModuleContainer, physics, CHR_INS_MODULE_CONTAINER_PHYSICS_OFFSET);
BB_OFFSET(ChrInsModuleContainer, physics, CHR_INS_MODULE_CONTAINER_SLOT68_OFFSET);
BB_OFFSET(ChrInsModuleContainer, fall, CHR_INS_MODULE_CONTAINER_FALL_OFFSET);
BB_OFFSET(ChrInsModuleContainer, ladder, CHR_INS_MODULE_CONTAINER_LADDER_OFFSET);
BB_OFFSET(ChrInsModuleContainer, ladder, CHR_INS_MODULE_CONTAINER_SLOT78_OFFSET);
BB_OFFSET(ChrInsModuleContainer, action_request, CHR_INS_MODULE_CONTAINER_ACTION_REQUEST_OFFSET);
BB_OFFSET(ChrInsModuleContainer, throw_, CHR_INS_MODULE_CONTAINER_THROW_OFFSET);
BB_OFFSET(ChrInsModuleContainer, throw_, CHR_INS_MODULE_CONTAINER_SLOT88_OFFSET);
BB_OFFSET(ChrInsModuleContainer, hit_stop, CHR_INS_MODULE_CONTAINER_PREFIX_SIZE);
BB_OFFSET(ChrInsModuleContainer, hit_stop, CHR_INS_MODULE_CONTAINER_HIT_STOP_OFFSET);
BB_OFFSET(ChrInsModuleContainer, damage, CHR_INS_MODULE_CONTAINER_DAMAGE_OFFSET);
BB_OFFSET(ChrInsModuleContainer, material, CHR_INS_MODULE_CONTAINER_MATERIAL_OFFSET);
BB_OFFSET(ChrInsModuleContainer, knock_back, CHR_INS_MODULE_CONTAINER_KNOCK_BACK_OFFSET);
BB_OFFSET(ChrInsModuleContainer, sfx, CHR_INS_MODULE_CONTAINER_SFX_OFFSET);
BB_OFFSET(ChrInsModuleContainer, unclassified_slots, 0xb8);

BB_SIZE(SprjChrActionRequestModule, SprjChrActionRequestModule::SIZE);
BB_OFFSET(SprjChrActionRequestModule, current_requests, 0x10);
BB_OFFSET(SprjChrActionRequestModule, previous_requests, 0x18);
BB_OFFSET(SprjChrActionRequestModule, rising_requests, 0x20);
BB_OFFSET(SprjChrActionRequestModule, falling_requests, 0x28);
BB_OFFSET(SprjChrActionRequestModule, delivered_requests, 0x30);
BB_OFFSET(SprjChrActionRequestModule, buffered_requests, 0x38);
BB_OFFSET(SprjChrActionRequestModule, buffer_arm_mask, 0x40);
BB_OFFSET(SprjChrActionRequestModule, buffer_delivery_mask, 0x48);
BB_OFFSET(SprjChrActionRequestModule, held_times, 0x50);
BB_OFFSET(SprjChrActionRequestModule, flagged_time, 0x90);
BB_OFFSET(SprjChrActionRequestModule, value_94, 0x94);
BB_OFFSET(SprjChrActionRequestModule, value_98, 0x98);
BB_OFFSET(SprjChrActionRequestModule, flags_9c, 0x9c);
BB_OFFSET(SprjChrActionRequestModule, flags_a0, 0xa0);
BB_OFFSET(SprjChrActionRequestModule, mask_snapshot, 0xa8);

BB_SIZE(SprjChrSuperArmorModule, SprjChrSuperArmorModule::SIZE);
BB_OFFSET(SprjChrSuperArmorModule, current, 0x10);
BB_OFFSET(SprjChrSuperArmorModule, previous_limit, 0x14);
BB_OFFSET(SprjChrSuperArmorModule, overflow_reserve, 0x18);
BB_OFFSET(SprjChrSuperArmorModule, recovery_time, 0x1c);
BB_OFFSET(SprjChrSuperArmorModule, broken, 0x20);
BB_OFFSET(SprjChrSuperArmorModule, previously_broken, 0x21);
BB_OFFSET(SprjChrSuperArmorModule, flags_22, 0x22);
BB_SIZE(SprjChrResistModule, SprjChrResistModule::SIZE);
BB_OFFSET(SprjChrResistModule, current, 0x10);
BB_OFFSET(SprjChrResistModule, limits, 0x24);
BB_OFFSET(SprjChrResistModule, flag_50, 0x50);

BB_SIZE(SprjChrBehaviorModule, SprjChrBehaviorModule::SIZE);
static_assert(alignof(SprjChrBehaviorModule) == 16, "alignof(SprjChrBehaviorModule)");
BB_OFFSET(SprjChrBehaviorModule, behavior_object, 0x10);
BB_OFFSET(SprjChrBehaviorModule, _inline_behavior_state, 0x60);
BB_OFFSET(SprjChrBehaviorModule, script_dispatch_enabled, 0x384);
BB_SIZE(SprjChrBehaviorScriptModule, SprjChrBehaviorScriptModule::SIZE);
BB_OFFSET(SprjChrBehaviorScriptModule, script_context, 0x10);
BB_OFFSET(SprjChrBehaviorScriptModule, context_18, 0x18);
BB_SIZE(SprjChrBehaviorScriptContext, 8);
BB_SIZE(SprjChrBehaviorSyncModule, SprjChrBehaviorSyncModule::SIZE);
BB_OFFSET(SprjChrBehaviorSyncModule, update_value, 0x10);
BB_OFFSET(SprjChrBehaviorSyncModule, flags_18, 0x18);
BB_SIZE(SprjChrAiModule, SprjChrAiModule::SIZE);

BB_SIZE(SprjChrTimeActModule, SprjChrTimeActModule::SIZE);
BB_OFFSET(SprjChrTimeActModule, resource, 0x10);
BB_OFFSET(SprjChrTimeActModule, dispatch_context, 0x18);
BB_OFFSET(SprjChrTimeActModule, records, 0x20);
BB_OFFSET(SprjChrTimeActModule, end_cursor, 0xc0);
BB_OFFSET(SprjChrTimeActModule, begin_cursor, 0xc4);
BB_OFFSET(SprjChrTimeActModule, flag_d0, 0xd0);
BB_SIZE(SprjChrTimeActRecord, 0x10);
BB_SIZE(SprjChrTimeActContext, 0x18);
BB_OFFSET(SprjChrTimeActContext, owner, 0x10);
BB_SIZE(SprjChrEventModule, SprjChrEventModule::SIZE);
BB_OFFSET(SprjChrEventModule, record_10, 0x10);
BB_OFFSET(SprjChrEventModule, record_18, 0x18);
BB_OFFSET(SprjChrEventModule, value_20, 0x20);
BB_OFFSET(SprjChrEventModule, state, 0x28);
BB_SIZE(SprjChrTalkModule, SprjChrTalkModule::SIZE);
BB_OFFSET(SprjChrTalkModule, flags_10, 0x10);
BB_SIZE(SprjChrThrowModule, SprjChrThrowModule::SIZE);
static_assert(alignof(SprjChrThrowModule) == 16, "alignof(SprjChrThrowModule)");
BB_OFFSET(SprjChrThrowModule, records, 0x10);
BB_OFFSET(SprjChrThrowModule, throw_state, 0x28);
BB_OFFSET(SprjChrThrowModule, value_30, 0x30);
BB_SIZE(SprjChrThrowStatePrefix, 0x28);
BB_OFFSET(SprjChrThrowStatePrefix, owner, 0x08);
BB_OFFSET(SprjChrThrowStatePrefix, phase, 0x20);
BB_OFFSET(SprjChrThrowStatePrefix, counterpart, 0x24);

BB_SIZE(SprjChrPhysicsModule, CHR_PHYSICS_MODULE_SIZE);
static_assert(alignof(SprjChrPhysicsModule) == 16, "alignof(SprjChrPhysicsModule)");
BB_OFFSET(SprjChrPhysicsModule, super_chr_module, 0x00);
BB_OFFSET(SprjChrPhysicsModule, floor_info, 0x70);
BB_OFFSET(SprjChrPhysicsModule, secondary_floor_info, 0x100);
BB_OFFSET(SprjChrPhysicsModule, slope_ctrl, 0x190);
static_assert(offsetof(SprjChrPhysicsModule, slope_ctrl) + offsetof(CSSlopeCtrl, floor_info) == 0x1b0,
              "SprjChrPhysicsModule::slope_ctrl.floor_info");
BB_OFFSET(SprjChrPhysicsModule, _unk1d0, 0x1d0);

BB_SIZE(SprjChrModuleBase, CHR_MODULE_BASE_SIZE);
BB_SIZE(ChrDataModule, CHR_DATA_MODULE_PREFIX_SIZE);
BB_SIZE(ChrDataModuleName, CHR_DATA_MODULE_NAME_SIZE);
BB_SIZE(ChrDataModuleMapContext, CHR_DATA_MODULE_MAP_CONTEXT_PREFIX_SIZE);
BB_SIZE(SprjChrActionFlagModule, CHR_ACTION_FLAG_MODULE_SIZE);
BB_OFFSET(SprjChrActionFlagModule, action_id, CHR_ACTION_FLAG_MODULE_ACTION_ID_OFFSET);
BB_OFFSET(SprjChrActionFlagModule, action_substate, CHR_ACTION_FLAG_MODULE_ACTION_SUBSTATE_OFFSET);
BB_OFFSET(ChrDataModule, map_context, CHR_DATA_MODULE_MAP_CONTEXT_OFFSET);
BB_OFFSET(ChrDataModule, world_block_index, CHR_DATA_MODULE_WORLD_BLOCK_INDEX_OFFSET);
BB_OFFSET(ChrDataModule, related_chr, CHR_DATA_MODULE_RELATED_CHR_OFFSET);
BB_OFFSET(ChrDataModule, hp, CHR_DATA_MODULE_HP_OFFSET);
BB_OFFSET(ChrDataModule, max_hp, CHR_DATA_MODULE_MAX_HP_OFFSET);
BB_OFFSET(ChrDataModule, unconfirmed_hp_like_128, CHR_DATA_MODULE_UNCONFIRMED_HP_LIKE_128_OFFSET);
BB_OFFSET(ChrDataModule, unconfirmed_hp_like_12c, CHR_DATA_MODULE_UNCONFIRMED_HP_LIKE_12C_OFFSET);
BB_OFFSET(ChrDataModule, name, CHR_DATA_MODULE_NAME_OFFSET);
BB_OFFSET(ChrDataModule, flags, CHR_DATA_MODULE_FLAGS_OFFSET);
BB_OFFSET(ChrDataModuleName, length, 0x10);
BB_OFFSET(ChrDataModuleName, capacity, 0x18);
BB_OFFSET(ChrDataModuleMapContext, world_block_index_base, CHR_DATA_MODULE_MAP_CONTEXT_INDEX_BASE_OFFSET);
BB_OFFSET(ChrDataModuleMapContext, block_id, CHR_DATA_MODULE_MAP_CONTEXT_BLOCK_ID_OFFSET);
}  // namespace detail::chr_module_layout

}  // namespace bb
