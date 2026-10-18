// ChrIns and PlayerIns: the character instance, its control/collision object
// (ChrCtrl), and the player position history.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_handle.hpp"
#include "bbhost/engine/sprj/chr_module.hpp"
#include "bbhost/engine/sprj/session_manager.hpp"

namespace bb {

template <typename T>
struct ChrSetEntry;
struct ChrSync;
struct GameDataManPlayerRecord;
struct MapCollisionEntry;

inline constexpr std::size_t CHR_INS_SIZE = 0x3b8;
// Map-character factory used by ChrSet initial population and regeneration.
// Builds an NpcIns or an NPC PlayerIns (sync mode 6) depending on the map
// record. Friendly NPCs also use it.
inline constexpr Rva MAP_CHR_CREATE_OR_RECONSTRUCT_FN{0x18d85b0};
inline constexpr Rva NPC_INS_CONSTRUCT_FN{0x18e4870};
inline constexpr Rva CHR_SET_POPULATE_MAP_CHARACTERS_FN{0x18da680};
inline constexpr Rva CHR_SET_REGENERATE_MAP_CHARACTERS_FN{0x18dad90};
// Common effect/stat update called from both NpcIns and PlayerIns wrappers,
// including data-module HP recalculation and event-body HP updates.
inline constexpr Rva CHR_INS_UPDATE_EFFECTS_AND_STATS_FN{0x18c9d90};
inline constexpr std::size_t CHR_INS_HANDLE_OFFSET = 0x08;
inline constexpr std::size_t CHR_INS_CHR_CTRL_OFFSET = 0x58;
inline constexpr std::size_t CHR_INS_CHR_SET_ENTRY_OFFSET = 0x18;
inline constexpr std::size_t CHR_INS_DISABLE_BACKREAD_FOR_EVENT_OFFSET = 0x28;
inline constexpr std::size_t CHR_INS_ALWAYS_ENABLE_BACKREAD_FOR_EVENT_OFFSET = 0x29;
inline constexpr std::size_t CHR_INS_CHR_SYNC_OFFSET = 0x60;
inline constexpr std::size_t CHR_INS_CHR_TYPE_OFFSET = 0x78;
inline constexpr std::size_t CHR_INS_TEAM_TYPE_OFFSET = 0x88;
// Native character update scheduling interval, not ChrSync packet cadence.
inline constexpr std::size_t CHR_INS_UPDATE_INTERVAL_OFFSET = 0xe4;
inline constexpr std::size_t CHR_INS_EVENT_DRAW_FLAGS_OFFSET = 0x1e1;
// ChrIns+0x1e2 bit 7, relative to the event_draw_flags byte array.
// WorldChrMan_RemoveChrDelayed sets it after detachment and before enqueue; not
// proof that enqueue succeeded or destruction completed.
inline constexpr std::uint64_t CHR_INS_DELAYED_DELETION_MARKED_BIT = 1ull << 15;
inline constexpr std::size_t CHR_INS_NETWORK_UPDATE_AUTHORITY_OFFSET = 0x1ec;
inline constexpr std::size_t CHR_INS_MAP_COLLISION_ENTRY_OFFSET = 0x288;
inline constexpr std::size_t CHR_INS_DRAW_GROUP_OVERRIDE_OFFSET = 0x29c;
inline constexpr std::size_t CHR_INS_MAP_COLLISION_TRANSITION_STATE_OFFSET = 0x29d;
inline constexpr std::size_t CHR_INS_CACHED_MAP_COLLISION_ENTRY_OFFSET = 0x350;
inline constexpr std::size_t CHR_INS_NETWORK_AUTHORITY_SCORE_OFFSET = 0x3a8;
inline constexpr std::size_t CHR_INS_MODULE_PTR_OFFSET = 0x3b0;
inline constexpr std::uint32_t NETWORK_UPDATE_AUTHORITY_NORMAL = 0;
inline constexpr std::uint32_t NETWORK_UPDATE_AUTHORITY_FORCED = 0x0fff;
inline constexpr std::int32_t NETWORK_AUTHORITY_STATE_TIER_STRIDE = 0x100;
inline constexpr float NETWORK_AUTHORITY_DISTANCE_RANGE = 64.0f;
inline constexpr std::int32_t NETWORK_AUTHORITY_DISTANCE_MAX_PRIORITY = 0xff;
inline constexpr std::int32_t NETWORK_AUTHORITY_SCORE_MAX = 0x7fff;
inline constexpr std::uint64_t CHR_INS_DRAW_ENABLE_BIT = 1ull << 12;
inline constexpr std::uint64_t CHR_INS_ALWAYS_DRAW_FOR_EVENT_BIT = 1ull << 20;
inline constexpr std::uint64_t CHR_INS_COMPLETELY_NO_MOVE_BIT = 1ull << 23;
// Character class used by the dormant coliseum session descriptors.
inline constexpr std::uint32_t CHR_TYPE_COLISEUM_GHOST = 13;
// Hostile combat team derived for coliseum actors by the session setup.
inline constexpr std::uint32_t TEAM_TYPE_HOSTILE = 21;
// Native Indiscriminate team used by chalice Enemy Avatar NPC rows.
inline constexpr std::uint32_t TEAM_TYPE_INDISCRIMINATE = 29;
inline constexpr std::size_t PLAYER_INS_SIZE = 0x570;
inline constexpr std::size_t PLAYER_INS_PLAYER_NUMBER_OFFSET = 0x3b8;
inline constexpr std::size_t PLAYER_INS_GAME_DATA_OFFSET = 0x3c0;
inline constexpr std::size_t PLAYER_INS_OBJECT_REF_OFFSET = 0x3d8;
inline constexpr std::size_t PLAYER_INS_POSITION_HISTORY_OFFSET = 0x400;
// Updates/allocates history only for WorldChrMan+0x60, regardless of role.
// PlayerIns+0x52c bit 0 enables sampling; death state may freeze it.
inline constexpr Rva PLAYER_INS_UPDATE_POSITION_HISTORY_FN{0x18f7fb0};
inline constexpr Rva PLAYER_POSITION_HISTORY_CONSTRUCT_FN{0x1909330};
inline constexpr Rva PLAYER_POSITION_HISTORY_SAMPLE_FN{0x1909b40};
inline constexpr Rva PLAYER_INS_GET_BLOOD_MARK_POSITION_FN{0x1904660};
inline constexpr Rva PLAYER_INS_GET_BLOOD_MARK_ROTATION_FN{0x1904690};
inline constexpr Rva PLAYER_INS_GET_BLOOD_MARK_MAP_FN{0x19046c0};
// ChrType bitset (types 1, 2 and 12) CSMultiPlayerInsTask::STEP_Create uses to
// decide whether a new remote actor starts with its event-draw bit off.
inline constexpr std::uint32_t PLAYER_INS_POST_CREATE_DRAW_DISABLED_CHR_TYPE_MASK = 0x1006;
// Virtual called by CSMultiPlayerInsTask::STEP_Create after construction to
// attach PlayerIns child modules to the selected WorldChr/map-group owner.
inline constexpr std::size_t PLAYER_INS_REFRESH_MODULE_WORLD_BINDINGS_VTABLE_OFFSET = 0xb0;
// Reads the session/summon descriptor index associated with a PlayerIns.
inline constexpr Rva PLAYER_INS_GET_SESSION_DESCRIPTOR_INDEX_FN{0x18fe420};
// Applies the local-player revive/reset state used during re-entry.
inline constexpr Rva PLAYER_INS_APPLY_REVIVE_RESET_FN{0x1900710};

inline constexpr std::size_t CHR_CTRL_PREFIX_SIZE = 0x118;
inline constexpr std::size_t CHR_CTRL_MAP_COLLISION_BINDING_CONTROL_OFFSET = 0x38;
inline constexpr std::size_t CHR_CTRL_ENABLE_LOGIC_OFFSET = 0x110;
inline constexpr std::size_t CHR_CTRL_DISABLE_MOVE_OFFSET = 0x111;
inline constexpr std::size_t CHR_CTRL_COLLISION_FLAGS_OFFSET = 0x114;
// Transition/occupancy mark set by the transition-state applicator and cleared
// by PlayerIns transition-reset finalization. Distinct from
// CHR_CTRL_COLLISION_DISABLED_BIT (bit 1).
inline constexpr std::uint16_t CHR_CTRL_TRANSITION_OCCUPANCY_BIT = 1u << 0;
inline constexpr std::uint16_t CHR_CTRL_COLLISION_DISABLED_BIT = 1u << 1;
inline constexpr std::size_t CHR_CTRL_MAP_COLLISION_BINDING_CONTROL_SIZE = 0xb0;
inline constexpr std::size_t CHR_CTRL_MAP_COLLISION_BINDING_STATE_FLAGS_OFFSET = 0x5a;
inline constexpr std::size_t CHR_CTRL_MAP_COLLISION_BINDING_MODE_OFFSET = 0x5d;
inline constexpr std::uint32_t CHR_CTRL_USE_CACHED_MAP_COLLISION_BINDING_BIT = 1u << 24;

// Prefix of the nested controller reached through ChrIns+0x58 -> ChrCtrl+0x38.
// RVA 0x18e5ef0 mirrors bit zero at +0x5d into ChrIns+0x1e1 bit 0x20, which
// makes ChrIns_UpdateMapCollisionBinding reapply ChrIns+0x350 instead of the
// live candidate at (*(ChrIns+0x3b0)+0x68)+0xa8. The higher-level state name
// and its writer are under investigation.
struct ChrCtrlMapCollisionBindingControl {
    Unknown<0x5a> _unk000;
    // Constructor-initialized packed state holding the binding-route bit; byte
    // +0x5d bit zero is this little-endian word's bit 24.
    std::uint8_t state_flags_le[4];
    Unknown<0x52> _unk05e;

    bool uses_cached_map_collision_binding() const {
        const std::uint32_t v = std::uint32_t{state_flags_le[0]} | (std::uint32_t{state_flags_le[1]} << 8) |
                                (std::uint32_t{state_flags_le[2]} << 16) | (std::uint32_t{state_flags_le[3]} << 24);
        return (v & CHR_CTRL_USE_CACHED_MAP_COLLISION_BINDING_BIT) != 0;
    }
};

// Prefix of the character control/collision object at ChrIns+0x58.
struct ChrCtrl {
    Unknown<0x38> _unk000;
    // Controller state consumed by RVA 0x18e5ef0 while choosing the live versus
    // cached map-collision binding route.
    ChrCtrlMapCollisionBindingControl* map_collision_binding_control;
    Unknown<0xd0> _unk040;
    std::uint8_t enable_logic;
    std::uint8_t disable_move;
    Unknown<0x02> _unk112;
    std::uint16_t collision_flags;
    std::uint16_t _pad116;

    // -1 without a binding control, else 0/1.
    int uses_cached_map_collision_binding() const {
        return map_collision_binding_control
                   ? (map_collision_binding_control->uses_cached_map_collision_binding() ? 1 : 0)
                   : -1;
    }
    // Bit zero of the gate written by native EnableLogic.
    bool logic_enabled() const { return (enable_logic & 1) != 0; }
    // Sets bit zero like the native EnableLogic callback at RVA 0x132e4d0.
    void set_logic_enabled(bool enabled) {
        enable_logic = static_cast<std::uint8_t>((enable_logic & ~1u) | (enabled ? 1u : 0u));
    }
    // The per-character movement gate written by DisableMove.
    bool movement_disabled() const { return disable_move != 0; }
    // Sets the same gate as the native DisableMove callback at RVA 0x132f770.
    void set_movement_disabled(bool disabled) { disable_move = disabled ? 1 : 0; }
    // SetColiEnable(false) sets bit 1 at +0x114; enabling clears it.
    bool collision_enabled_for_event() const { return (collision_flags & CHR_CTRL_COLLISION_DISABLED_BIT) == 0; }
};

// Constructor-proven footprint of the class named ChrIns.
struct ChrIns {
    void* vftable;
    ChrHandle field_ins_handle;
    Unknown<0x0c> _unk0c;
    // Owning character-set record; transition occupancy is at entry +0x20.
    ChrSetEntry<ChrIns>* chr_set_entry;
    Unknown<0x08> _unk20;
    // EMEVD SetDisableBackread_forEvent override.
    std::uint8_t disable_backread_for_event;
    // EMEVD SetAlwayEnableBackread_forEvent override.
    std::uint8_t always_enable_backread_for_event;
    Unknown<0x2e> _unk2a;
    ChrCtrl* chr_ctrl;
    ChrSync* chr_sync;
    Unknown<0x10> _unk68;
    // Multiplayer character class written from the session descriptor.
    std::uint32_t chr_type;
    Unknown<0x0c> _unk7c;
    // Combat relationship used by targeting and damage rules.
    std::uint32_t team_type;
    Unknown<0x58> _unk8c;
    // Used by WorldChrMan_BuildCharacterUpdateLists (RVA 0x19173b0). Zero
    // selects every pass; positive n selects when this actor's ChrSet slot
    // index % n equals WorldChrMan.update_counter % n, with delta * n. Negative
    // excludes the full-update path. Other gates still apply; unselected actors
    // receive reduced callbacks.
    std::int32_t update_interval;
    Unknown<0xf9> _unk0e8;
    std::uint8_t event_draw_flags[5];
    Unknown<0x06> _unk1e6;
    // Priority override written by EMEVD SetNetworkUpdateAuthority. Feeds
    // enemy/NPC authority arbitration; not the ownership mechanism for local or
    // remote player frame updates.
    std::uint32_t network_update_authority;
    Unknown<0x98> _unk1f0;
    // Process-local collision entry consumed by action/physics. Group rebuilds
    // can invalidate it; hard transitions detach it through
    // ChrIns_SetMapCollisionEntry(self, null) first.
    MapCollisionEntry* map_collision_entry;
    Unknown<0x0c> _unk290;
    // Set by EMEVD SetDrawGroup after installing an explicit entry. Nonzero
    // also stops the canonical collision setter replacing +0x288. No session or
    // NetWorldChrSync writer observed.
    std::uint8_t draw_group_overridden;
    // Collision-transition state set to 1 by map-transition preparation and
    // cleared before later collision-binding updates.
    std::uint8_t map_collision_transition_state[2];
    Unknown<0xb1> _unk29f;
    // Cached process-local entry used by one runtime rebind branch.
    MapCollisionEntry* cached_map_collision_entry;
    Unknown<0x50> _unk358;
    // Last host-comparison score computed for this world character.
    std::int32_t network_authority_score;
    Unknown<0x04> _unk3ac;
    ChrInsModuleContainer* chr_module;

    // The five-byte packed flag field written by SetDrawEnable,
    // SetAlwaysDrawForEvent and similar script helpers.
    std::uint64_t event_draw_flags_raw() const {
        std::uint64_t raw = 0;
        for (int i = 0; i < 5; ++i) raw |= std::uint64_t{event_draw_flags[i]} << (i * 8);
        return raw;
    }
    bool draw_enabled_for_event() const { return (event_draw_flags_raw() & CHR_INS_DRAW_ENABLE_BIT) != 0; }
    bool always_draw_for_event() const { return (event_draw_flags_raw() & CHR_INS_ALWAYS_DRAW_FOR_EVENT_BIT) != 0; }
    // The flag written by the native SetCompletelyNoMove callback.
    bool completely_no_move() const { return (event_draw_flags_raw() & CHR_INS_COMPLETELY_NO_MOVE_BIT) != 0; }
    // Sets the same packed bit as SetCompletelyNoMove at RVA 0x1337830.
    void set_completely_no_move(bool disabled) {
        if (disabled)
            event_draw_flags[2] |= 0x80;
        else
            event_draw_flags[2] &= 0x7f;
    }
    // -1 without a controller, else 0/1.
    int collision_enabled_for_event() const {
        return chr_ctrl ? (chr_ctrl->collision_enabled_for_event() ? 1 : 0) : -1;
    }
    // Whether EMEVD requested forced enemy/NPC network authority.
    bool network_update_authority_forced() const {
        return network_update_authority == NETWORK_UPDATE_AUTHORITY_FORCED;
    }
    // SetNetworkUpdateAuthority(..., Forced) / (..., Normal).
    void force_network_update_authority() { network_update_authority = NETWORK_UPDATE_AUTHORITY_FORCED; }
    void normalize_network_update_authority() { network_update_authority = NETWORK_UPDATE_AUTHORITY_NORMAL; }
    // Whether EMEVD installed an explicit draw-group override.
    bool draw_group_override_set() const { return draw_group_overridden != 0; }
    std::uint16_t map_collision_transition_state_u16() const {
        return static_cast<std::uint16_t>(map_collision_transition_state[0] |
                                          (map_collision_transition_state[1] << 8));
    }
    // Whether the main-player fallback will search for and install a live
    // map-collision entry: WorldChrMan_RebindMainPlayerMapCollision (RVA
    // 0x19ec030) only looks when +0x288 is null, so a stale non-null pointer
    // suppresses native post-load recovery.
    bool needs_map_collision_rebind() const { return map_collision_entry == nullptr; }
    bool backread_disabled_for_event() const { return disable_backread_for_event != 0; }
    bool backread_always_enabled_for_event() const { return always_enable_backread_for_event != 0; }
    // The character data module through the module container, or null.
    ChrDataModule* chr_data() const { return chr_module ? chr_module->data : nullptr; }
    // HP accessors through the data module; false (out untouched) without one.
    bool hp(std::int32_t* out) const {
        ChrDataModule* d = chr_data();
        if (!d) return false;
        *out = d->hp;
        return true;
    }
    bool max_hp(std::int32_t* out) const {
        ChrDataModule* d = chr_data();
        if (!d) return false;
        *out = d->max_hp;
        return true;
    }
    // Also false when max HP is not positive.
    bool hp_ratio(float* out) const {
        ChrDataModule* d = chr_data();
        return d && d->hp_ratio(out);
    }
    bool is_dead(bool* out) const {
        ChrDataModule* d = chr_data();
        if (!d) return false;
        *out = d->is_dead();
        return true;
    }
    // Sets HP to zero; false without a data module.
    bool kill() {
        ChrDataModule* d = chr_data();
        if (!d) return false;
        d->kill();
        return true;
    }
};

// History owner at PlayerIns+0x400, allocation 0xe8. Sampling keeps a delayed,
// ground-qualified position rather than the instantaneous death/fall position.
// +0x30/+0x3c/+0x48 are consumed by the blood-mark commit; the rest is opaque.
struct PlayerPositionHistory {
    Unknown<0x30> _unk000;
    float blood_mark_position[3];
    float blood_mark_rotation[3];
    std::uint32_t blood_mark_map;
    Unknown<0x9c> _unk04c;
};

// Constructor-proven footprint of the class named PlayerIns.
struct PlayerIns {
    ChrIns super_chr_ins;
    // Multiplayer player number written by
    // PlayerIns_ConstructFromGameDataAndPeer (RVA 0x18f1700).
    std::int32_t player_number;
    std::uint32_t _pad3bc;
    // GameDataMan player record selected by model id before construction.
    GameDataManPlayerRecord* game_data;
    Unknown<0x10> _unk3c8;
    // Peer identity copied by the ObjectRef constructor at RVA 0x0ca1440.
    SprjSessionObjectRef peer_object_ref;
    Unknown<0x178> _unk3f8;
};

namespace detail::chr_ins_layout {
BB_SIZE(ChrIns, CHR_INS_SIZE);
BB_SIZE(PlayerIns, PLAYER_INS_SIZE);
BB_SIZE(ChrCtrl, CHR_CTRL_PREFIX_SIZE);
BB_SIZE(ChrCtrlMapCollisionBindingControl, CHR_CTRL_MAP_COLLISION_BINDING_CONTROL_SIZE);
BB_SIZE(PlayerPositionHistory, 0xe8);
BB_OFFSET(PlayerPositionHistory, blood_mark_position, 0x30);
BB_OFFSET(PlayerPositionHistory, blood_mark_rotation, 0x3c);
BB_OFFSET(PlayerPositionHistory, blood_mark_map, 0x48);
BB_OFFSET(ChrIns, vftable, 0x00);
BB_OFFSET(ChrIns, field_ins_handle, CHR_INS_HANDLE_OFFSET);
BB_OFFSET(ChrIns, chr_set_entry, CHR_INS_CHR_SET_ENTRY_OFFSET);
BB_OFFSET(ChrIns, disable_backread_for_event, CHR_INS_DISABLE_BACKREAD_FOR_EVENT_OFFSET);
BB_OFFSET(ChrIns, always_enable_backread_for_event, CHR_INS_ALWAYS_ENABLE_BACKREAD_FOR_EVENT_OFFSET);
BB_OFFSET(ChrIns, chr_ctrl, CHR_INS_CHR_CTRL_OFFSET);
BB_OFFSET(ChrIns, chr_sync, CHR_INS_CHR_SYNC_OFFSET);
BB_OFFSET(ChrIns, chr_type, CHR_INS_CHR_TYPE_OFFSET);
BB_OFFSET(ChrIns, team_type, CHR_INS_TEAM_TYPE_OFFSET);
BB_OFFSET(ChrIns, update_interval, CHR_INS_UPDATE_INTERVAL_OFFSET);
BB_OFFSET(ChrIns, event_draw_flags, CHR_INS_EVENT_DRAW_FLAGS_OFFSET);
BB_OFFSET(ChrIns, network_update_authority, CHR_INS_NETWORK_UPDATE_AUTHORITY_OFFSET);
BB_OFFSET(ChrIns, map_collision_entry, CHR_INS_MAP_COLLISION_ENTRY_OFFSET);
BB_OFFSET(ChrIns, draw_group_overridden, CHR_INS_DRAW_GROUP_OVERRIDE_OFFSET);
BB_OFFSET(ChrIns, map_collision_transition_state, CHR_INS_MAP_COLLISION_TRANSITION_STATE_OFFSET);
BB_OFFSET(ChrIns, cached_map_collision_entry, CHR_INS_CACHED_MAP_COLLISION_ENTRY_OFFSET);
BB_OFFSET(ChrIns, network_authority_score, CHR_INS_NETWORK_AUTHORITY_SCORE_OFFSET);
BB_OFFSET(ChrIns, chr_module, CHR_INS_MODULE_PTR_OFFSET);
BB_OFFSET(ChrCtrl, map_collision_binding_control, CHR_CTRL_MAP_COLLISION_BINDING_CONTROL_OFFSET);
BB_OFFSET(ChrCtrl, enable_logic, CHR_CTRL_ENABLE_LOGIC_OFFSET);
BB_OFFSET(ChrCtrl, disable_move, CHR_CTRL_DISABLE_MOVE_OFFSET);
BB_OFFSET(ChrCtrl, collision_flags, CHR_CTRL_COLLISION_FLAGS_OFFSET);
BB_OFFSET(ChrCtrlMapCollisionBindingControl, state_flags_le, CHR_CTRL_MAP_COLLISION_BINDING_STATE_FLAGS_OFFSET);
static_assert(offsetof(ChrCtrlMapCollisionBindingControl, state_flags_le) + 3 ==
                  CHR_CTRL_MAP_COLLISION_BINDING_MODE_OFFSET,
              "ChrCtrlMapCollisionBindingControl mode byte");
BB_OFFSET(PlayerIns, player_number, PLAYER_INS_PLAYER_NUMBER_OFFSET);
BB_OFFSET(PlayerIns, game_data, PLAYER_INS_GAME_DATA_OFFSET);
BB_OFFSET(PlayerIns, peer_object_ref, PLAYER_INS_OBJECT_REF_OFFSET);
BB_OFFSET(PlayerIns, _unk3f8, 0x3f8);
}  // namespace detail::chr_ins_layout

}  // namespace bb
