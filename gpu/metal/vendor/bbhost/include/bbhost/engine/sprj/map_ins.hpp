// Map instances, map-geometry blocks and the map collision manager.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t MAP_INS_SIZE = 0x248;
inline constexpr std::size_t MAP_INS_UPDATE_HELPER_OFFSET = 0x00;
inline constexpr std::size_t MAP_INS_BACKREAD_STATE_OFFSET = 0x221;
inline constexpr std::size_t MAP_INS_DRAW_FLAGS_OFFSET = 0x230;
inline constexpr std::uint8_t MAP_INS_DRAW_DIRTY_BIT = 1 << 0;
inline constexpr std::uint8_t MAP_INS_DRAW_ENABLE_BIT = 1 << 1;

inline constexpr std::size_t MAP_INS_PART_RECORD_SIZE = 0x48;
inline constexpr std::size_t MAP_INS_PART_RECORD_MAP_INS_OFFSET = 0x40;
inline constexpr std::size_t MAP_INS_BLOCK_SIZE = 0x198;
inline constexpr std::size_t MAP_INS_BLOCK_PART_COUNT_OFFSET = 0x150;
inline constexpr std::size_t MAP_INS_BLOCK_PARTS_OFFSET = 0x158;

inline constexpr std::size_t MAP_COLLISION_ENTRY_SIZE = 0x138;
inline constexpr std::size_t MAP_COLLISION_ENTRY_HANDLE_OFFSET = 0x08;
inline constexpr std::size_t MAP_COLLISION_ENTRY_RESOURCE_OWNER_OFFSET = 0x18;
inline constexpr std::size_t MAP_COLLISION_ENTRY_BACKING_SOURCE_OFFSET = 0x20;
inline constexpr std::size_t MAP_COLLISION_ENTRY_DISP_GROUP_OFFSET = 0x54;
inline constexpr std::size_t MAP_COLLISION_ENTRY_LOAD_GROUP_OFFSET = 0x74;
inline constexpr std::size_t MAP_COLLISION_ENTRY_DRAW_GROUP_OFFSET = 0x94;
inline constexpr std::size_t MAP_COLLISION_ENTRY_SOURCE_FLAGS_OFFSET = 0xc0;
// Required by ChrIns_SetMapCollisionEntry before it accepts a non-null
// candidate. The wider source-flag semantics are not yet named.
inline constexpr std::uint8_t MAP_COLLISION_ENTRY_BINDABLE_BIT = 1 << 0;
inline constexpr std::size_t MAP_COLLISION_ENTRY_FLAGS_OFFSET = 0x118;
inline constexpr std::uint8_t MAP_COLLISION_ENTRY_COLLISION_ENABLE_BIT = 1 << 1;
inline constexpr std::size_t MAP_COLLISION_ENTRY_STREAMING_PHASE_OFFSET = 0x119;
inline constexpr std::size_t MAP_COLLISION_ENTRY_DESIRED_BACKREAD_STATE_OFFSET = 0x11a;
inline constexpr std::size_t MAP_COLLISION_ENTRY_STREAMING_CONTROL_FLAGS_OFFSET = 0x130;
inline constexpr std::uint32_t MAP_COLLISION_ENTRY_INDEX_MASK = 0x000fffff;
inline constexpr std::uint32_t MAP_COLLISION_ENTRY_GROUP_KEY_SHIFT = 20;
inline constexpr std::uint32_t MAP_COLLISION_ENTRY_GROUP_KEY_MASK = 0xff;

inline constexpr std::size_t MAP_COLLISION_GROUP_SIZE = 0xb8;
inline constexpr std::size_t MAP_COLLISION_GROUP_RESOURCE_OWNER_OFFSET = 0x08;
inline constexpr std::size_t MAP_COLLISION_GROUP_DISP_GROUP_OFFSET = 0x18;
inline constexpr std::size_t MAP_COLLISION_GROUP_NAVI_GROUP_OFFSET = 0x38;
inline constexpr std::size_t MAP_COLLISION_GROUP_BACK_GROUP_OFFSET = 0x58;
inline constexpr std::size_t MAP_COLLISION_GROUP_ENTRY_COUNT_OFFSET = 0x78;
inline constexpr std::size_t MAP_COLLISION_GROUP_ENTRIES_OFFSET = 0x80;
inline constexpr std::size_t MAP_COLLISION_GROUP_SOURCE_INDEX_COUNT_OFFSET = 0x88;
inline constexpr std::size_t MAP_COLLISION_GROUP_SOURCE_INDEX_OFFSET = 0x90;

inline constexpr std::size_t MAP_COLLISION_SOURCE_LOOKUP_SIZE = 0x20;
inline constexpr std::size_t MAP_COLLISION_MANAGER_SIZE = 0x2e20;
inline constexpr std::size_t MAP_COLLISION_MANAGER_LOOKUP_COUNT_OFFSET = 0x14;
inline constexpr std::size_t MAP_COLLISION_MANAGER_LOOKUPS_OFFSET = 0x18;
inline constexpr std::size_t MAP_COLLISION_MANAGER_GROUP_COUNT_OFFSET = 0x20;
inline constexpr std::size_t MAP_COLLISION_MANAGER_GROUPS_OFFSET = 0x28;
inline constexpr std::size_t MAP_COLLISION_MANAGER_INLINE_LOOKUPS_OFFSET = 0x30;
inline constexpr std::size_t MAP_COLLISION_MANAGER_LOOKUP_CAPACITY = 20;
inline constexpr std::size_t MAP_COLLISION_MANAGER_INLINE_GROUPS_OFFSET = 0x2b0;
inline constexpr std::size_t MAP_COLLISION_MANAGER_GROUP_CAPACITY = 60;

// Event-script touched prefix for map instances.
struct MapIns {
    void* update_helper;
    Unknown<0x219> _unk008;
    // Native backread transition state written by RVA 0x1587ed0. Observed
    // values 0, 2 and 4; their names remain open.
    std::uint8_t backread_state;
    Unknown<0x0e> _unk222;
    std::uint8_t draw_flags;
    Unknown<0x17> _unk231;

    bool draw_update_requested_for_event() const { return (draw_flags & MAP_INS_DRAW_DIRTY_BIT) != 0; }
    bool draw_enabled_for_event() const { return (draw_flags & MAP_INS_DRAW_ENABLE_BIT) != 0; }
};

// One mask-to-map-instance binding used by a MapInsBlock.
struct MapInsPartRecord {
    // Primary and secondary 256-bit section-membership masks.
    std::uint8_t section_masks[0x40];
    MapIns* map_ins;
};

// Per-world-block map-geometry state advanced by RVA 0x15934d0.
struct MapInsBlock {
    Unknown<0x150> _unk000;
    std::uint32_t part_count;
    std::uint32_t _pad154;
    MapInsPartRecord* parts;
    Unknown<0x38> _unk160;
};

// Process-local collision entry owned by one MapCollisionGroup. ChrIns stores
// raw pointers to it: not a stable network identity, and invalid once its
// owning group is rebuilt.
struct MapCollisionEntry {
    void* vftable;
    std::uint32_t encoded_handle;
    std::uint32_t _pad00c;
    void* _source_wrapper_vftable;
    void* resource_owner;
    void* backing_source;
    Unknown<0x2c> _unk028;
    // Renderer display groups contributed while this collision is active.
    std::uint8_t disp_group[0x20];
    // Source MSB LoadGroups associated with this collision.
    std::uint8_t load_group[0x20];
    // Source MSB DrawGroups associated with this collision and its map parts.
    std::uint8_t draw_group[0x20];
    Unknown<0x0c> _unk0b4;
    // Flags owned by the embedded collision-source wrapper.
    std::uint8_t source_flags;
    Unknown<0x57> _unk0c1;
    std::uint8_t collision_flags;
    // Internal 0..=7 phase consumed by the collision streaming state machine.
    std::uint8_t streaming_phase;
    // Desired state from LoadGroup/DrawGroup-derived runtime masks. Confirmed
    // values 0 (retire), 2 (active), 4 (alternate active route).
    std::uint8_t desired_backread_state;
    Unknown<0x15> _unk11b;
    // Control bits that can suppress or force parts of collision/render
    // activation; individual bits only partially identified.
    std::uint8_t streaming_control_flags;
    Unknown<0x07> _unk131;

    // Per-group entry index in the low 20 bits of the handle.
    std::uint32_t entry_index() const { return encoded_handle & MAP_COLLISION_ENTRY_INDEX_MASK; }
    // Group/source key in bits 20..27 of the handle.
    std::uint8_t group_key() const {
        return static_cast<std::uint8_t>((encoded_handle >> MAP_COLLISION_ENTRY_GROUP_KEY_SHIFT) &
                                         MAP_COLLISION_ENTRY_GROUP_KEY_MASK);
    }
    bool collision_enabled_for_event() const { return (collision_flags & MAP_COLLISION_ENTRY_COLLISION_ENABLE_BIT) != 0; }
    // Whether the canonical character-binding setter will accept this entry.
    bool is_bindable() const { return (source_flags & MAP_COLLISION_ENTRY_BINDABLE_BIT) != 0; }
};

// One collision group in the manager's fixed 60-record outer pool. entries is
// allocated from the active map resource; rebuilding a group destroys the old
// 0x138-byte entries before replacing this pointer and count.
struct MapCollisionGroup {
    void* vftable;
    void* resource_owner;
    Unknown<0x08> _unk010;
    // Aggregated renderer display-group mask shown by WORLD HIT MAN.
    std::uint8_t disp_group[0x20];
    // Raw primary WorldBackRead mask distributed by RVA 0x19e8190.
    std::uint8_t navi_group[0x20];
    // OR of primary and secondary WorldBackRead masks.
    std::uint8_t back_group[0x20];
    std::uint32_t entry_count;
    std::uint32_t _pad07c;
    MapCollisionEntry* entries;
    std::uint32_t source_index_count;
    std::uint32_t _pad08c;
    void* source_index;
    Unknown<0x20> _unk098;

    // Entries currently allocated for this group.
    std::size_t entry_len() const { return entries ? entry_count : 0; }
    MapCollisionEntry* entry(std::size_t i) const { return i < entry_len() ? &entries[i] : nullptr; }
};

// Opaque map/source lookup record embedded in MapCollisionManager.
using MapCollisionSourceLookup = Unknown<MAP_COLLISION_SOURCE_LOOKUP_SIZE>;

// Collision owner at RVA 0x553e850. Embeds capacity for 20 source lookups and
// 60 collision groups: outer-record capacities, not a cap on collision
// geometry (each group's entries are allocated independently).
struct MapCollisionManager {
    void* vftable;
    void* world_res_context;
    std::uint32_t _unk010;
    std::uint32_t lookup_count;
    MapCollisionSourceLookup* lookups;
    std::uint32_t collision_group_count;
    std::uint32_t _pad024;
    MapCollisionGroup* collision_groups;
    MapCollisionSourceLookup inline_lookups[MAP_COLLISION_MANAGER_LOOKUP_CAPACITY];
    MapCollisionGroup inline_collision_groups[MAP_COLLISION_MANAGER_GROUP_CAPACITY];
    Unknown<0x50> _tail2dd0;

    // Initialized source lookups, capped to the embedded pool.
    std::size_t lookup_len() const {
        if (!lookups) return 0;
        return lookup_count < MAP_COLLISION_MANAGER_LOOKUP_CAPACITY ? lookup_count : MAP_COLLISION_MANAGER_LOOKUP_CAPACITY;
    }
    MapCollisionSourceLookup* lookup(std::size_t i) const { return i < lookup_len() ? &lookups[i] : nullptr; }
    // Initialized collision groups, capped to the embedded pool.
    std::size_t collision_group_len() const {
        if (!collision_groups) return 0;
        return collision_group_count < MAP_COLLISION_MANAGER_GROUP_CAPACITY ? collision_group_count
                                                                           : MAP_COLLISION_MANAGER_GROUP_CAPACITY;
    }
    MapCollisionGroup* collision_group(std::size_t i) const {
        return i < collision_group_len() ? &collision_groups[i] : nullptr;
    }
};

namespace detail::map_ins_layout {
BB_SIZE(MapIns, MAP_INS_SIZE);
BB_OFFSET(MapIns, update_helper, MAP_INS_UPDATE_HELPER_OFFSET);
BB_OFFSET(MapIns, backread_state, MAP_INS_BACKREAD_STATE_OFFSET);
BB_OFFSET(MapIns, draw_flags, MAP_INS_DRAW_FLAGS_OFFSET);
BB_SIZE(MapInsPartRecord, MAP_INS_PART_RECORD_SIZE);
BB_OFFSET(MapInsPartRecord, map_ins, MAP_INS_PART_RECORD_MAP_INS_OFFSET);
BB_SIZE(MapInsBlock, MAP_INS_BLOCK_SIZE);
BB_OFFSET(MapInsBlock, part_count, MAP_INS_BLOCK_PART_COUNT_OFFSET);
BB_OFFSET(MapInsBlock, parts, MAP_INS_BLOCK_PARTS_OFFSET);
BB_SIZE(MapCollisionEntry, MAP_COLLISION_ENTRY_SIZE);
BB_OFFSET(MapCollisionEntry, encoded_handle, MAP_COLLISION_ENTRY_HANDLE_OFFSET);
BB_OFFSET(MapCollisionEntry, resource_owner, MAP_COLLISION_ENTRY_RESOURCE_OWNER_OFFSET);
BB_OFFSET(MapCollisionEntry, backing_source, MAP_COLLISION_ENTRY_BACKING_SOURCE_OFFSET);
BB_OFFSET(MapCollisionEntry, disp_group, MAP_COLLISION_ENTRY_DISP_GROUP_OFFSET);
BB_OFFSET(MapCollisionEntry, load_group, MAP_COLLISION_ENTRY_LOAD_GROUP_OFFSET);
BB_OFFSET(MapCollisionEntry, draw_group, MAP_COLLISION_ENTRY_DRAW_GROUP_OFFSET);
BB_OFFSET(MapCollisionEntry, source_flags, MAP_COLLISION_ENTRY_SOURCE_FLAGS_OFFSET);
BB_OFFSET(MapCollisionEntry, collision_flags, MAP_COLLISION_ENTRY_FLAGS_OFFSET);
BB_OFFSET(MapCollisionEntry, streaming_phase, MAP_COLLISION_ENTRY_STREAMING_PHASE_OFFSET);
BB_OFFSET(MapCollisionEntry, desired_backread_state, MAP_COLLISION_ENTRY_DESIRED_BACKREAD_STATE_OFFSET);
BB_OFFSET(MapCollisionEntry, streaming_control_flags, MAP_COLLISION_ENTRY_STREAMING_CONTROL_FLAGS_OFFSET);
BB_SIZE(MapCollisionGroup, MAP_COLLISION_GROUP_SIZE);
BB_OFFSET(MapCollisionGroup, resource_owner, MAP_COLLISION_GROUP_RESOURCE_OWNER_OFFSET);
BB_OFFSET(MapCollisionGroup, disp_group, MAP_COLLISION_GROUP_DISP_GROUP_OFFSET);
BB_OFFSET(MapCollisionGroup, navi_group, MAP_COLLISION_GROUP_NAVI_GROUP_OFFSET);
BB_OFFSET(MapCollisionGroup, back_group, MAP_COLLISION_GROUP_BACK_GROUP_OFFSET);
BB_OFFSET(MapCollisionGroup, entry_count, MAP_COLLISION_GROUP_ENTRY_COUNT_OFFSET);
BB_OFFSET(MapCollisionGroup, entries, MAP_COLLISION_GROUP_ENTRIES_OFFSET);
BB_OFFSET(MapCollisionGroup, source_index_count, MAP_COLLISION_GROUP_SOURCE_INDEX_COUNT_OFFSET);
BB_OFFSET(MapCollisionGroup, source_index, MAP_COLLISION_GROUP_SOURCE_INDEX_OFFSET);
BB_SIZE(MapCollisionSourceLookup, MAP_COLLISION_SOURCE_LOOKUP_SIZE);
BB_SIZE(MapCollisionManager, MAP_COLLISION_MANAGER_SIZE);
BB_OFFSET(MapCollisionManager, lookup_count, MAP_COLLISION_MANAGER_LOOKUP_COUNT_OFFSET);
BB_OFFSET(MapCollisionManager, lookups, MAP_COLLISION_MANAGER_LOOKUPS_OFFSET);
BB_OFFSET(MapCollisionManager, collision_group_count, MAP_COLLISION_MANAGER_GROUP_COUNT_OFFSET);
BB_OFFSET(MapCollisionManager, collision_groups, MAP_COLLISION_MANAGER_GROUPS_OFFSET);
BB_OFFSET(MapCollisionManager, inline_lookups, MAP_COLLISION_MANAGER_INLINE_LOOKUPS_OFFSET);
BB_OFFSET(MapCollisionManager, inline_collision_groups, MAP_COLLISION_MANAGER_INLINE_GROUPS_OFFSET);
}  // namespace detail::map_ins_layout

}  // namespace bb
