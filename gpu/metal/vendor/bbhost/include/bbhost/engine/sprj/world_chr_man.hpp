// WorldChrMan: the world-character singleton, its player/ghost sets, handle lookup and update lists.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/chr_thread.hpp"
#include "bbhost/engine/cs/cloth_thread.hpp"
#include "bbhost/engine/sprj/chr_set.hpp"
#include "bbhost/engine/sprj/world_info.hpp"

namespace bb {

struct ChrIns;
struct NetWorldChrSync;
struct PlayerIns;

// Initializes the embedded area/block owners and allocates NetWorldChrSync,
// CSChrThread, and CSClothThread. Does not itself populate player actors.
inline constexpr Rva WORLD_CHR_MAN_CONSTRUCTOR_FN{0x1913a40};
// Releases owned managers/threads and embedded sets; not a peer-leave API.
inline constexpr Rva WORLD_CHR_MAN_DESTRUCTOR_FN{0x19145d0};
// Initializes the five-entry player set and fifteen-entry ghost set, then
// registers both after the WorldInfoOwner block selectors.
inline constexpr Rva WORLD_CHR_MAN_PREPARE_PLAYER_SETS_FN{0x1914a40};
// Registers a ChrSet by set selector; a null set unregisters it and compacts
// the active list. These selectors are not network member IDs or NPID slots.
inline constexpr Rva WORLD_CHR_MAN_REGISTER_CHR_SET_FN{0x1914c70};
// Releases block/player/ghost sets and the retained render reference. Distinct
// from destroying WorldChrMan or leaving a network room.
inline constexpr Rva WORLD_CHR_MAN_RELEASE_CHARACTER_SETS_FN{0x1914f40};
// Builds transient per-set work entries and executes their actor update
// phases. Work entries remain allocated until the finalization boundary.
inline constexpr Rva WORLD_CHR_MAN_BUILD_CHARACTER_UPDATE_LISTS_FN{0x19173b0};
// Invokes actor virtual +0x120 on the full-update list, frees each temporary
// per-set allocation, and clears all three head/tail pairs.
inline constexpr Rva WORLD_CHR_MAN_FINALIZE_CHARACTER_UPDATE_LISTS_FN{0x19190a0};

// Allocates and installs the local PlayerIns when WorldChrMan+0x60 is empty.
// The post-load caller reuses the existing pointer when it is present.
inline constexpr Rva WORLD_CHR_MAN_CREATE_LOCAL_PLAYER_FN{0x18db3c0};
// Selects a free player ChrSet slot beginning at index one and invokes the
// native remote-player allocator.
inline constexpr Rva WORLD_CHR_MAN_CREATE_REMOTE_PLAYER_FN{0x18db500};
// Allocates a 0x570 remote PlayerIns, resolves its GameData record, and
// initializes it through the shared native constructor.
inline constexpr Rva WORLD_CHR_MAN_ALLOCATE_REMOTE_PLAYER_INS_FN{0x18d8100};
// Shared local/remote PlayerIns constructor and GameData/ObjectRef binder.
inline constexpr Rva PLAYER_INS_CONSTRUCT_FROM_GAME_DATA_AND_PEER_FN{0x18f1700};
// Allocates and initializes WorldChrMan.player_chr_set.
inline constexpr Rva WORLD_CHR_MAN_INIT_PLAYER_CHR_SET_FN{0x18da8d0};
// Serializes the native world-character section of a transition snapshot.
inline constexpr Rva WORLD_CHR_MAN_SERIALIZE_TRANSITION_SNAPSHOT_FN{0x191bd10};
// Native transition reader paired with the serializer.
inline constexpr Rva WORLD_CHR_MAN_CONSUME_TRANSITION_SNAPSHOT_FN{0x191bdd0};
// Detaches from player/ghost ChrSet, marks ChrIns+0x1e2 bit 7, and enqueues
// delayed deletion. Return does not prove the actor allocation was freed.
// Does not clear session membership or mod-owned references.
inline constexpr Rva WORLD_CHR_MAN_REMOVE_CHR_DELAYED_FN{0x19186e0};
// Detaches the indexed ChrSet entry and returns its actor without freeing
// it. It may destroy the actor's auxiliary GameData record first.
inline constexpr Rva WORLD_CHR_MAN_DETACH_CHR_SET_ENTRY_FN{0x18db230};
// Local collision rebind boundary.
inline constexpr Rva WORLD_CHR_MAN_REBIND_MAIN_PLAYER_MAP_COLLISION_FN{0x19ec030};

inline constexpr std::size_t WORLD_CHR_MAN_SIZE = 0x61f0;
// Vanilla 1.09 capacity: local player plus four remote actors.
inline constexpr std::size_t WORLD_CHR_MAN_NATIVE_PLAYER_CHR_SET_CAPACITY = 5;
inline constexpr std::size_t WORLD_CHR_MAN_NATIVE_PLAYER_CHR_SET_ALLOCATION_SIZE = 0x118;
// Main-player pointer installed by local creation; remote actors are bound
// through free entries in player_chr_set beginning at index one.
inline constexpr std::size_t WORLD_CHR_MAN_MAIN_PLAYER_OFFSET = 0x60;
inline constexpr std::size_t WORLD_CHR_MAN_REMOTE_PLAYER_FIRST_ENTRY_INDEX = 1;
inline constexpr std::size_t WORLD_CHR_MAN_ENTITY_LOOKUP_COUNT = 0x3c;
inline constexpr std::size_t WORLD_CHR_MAN_UNKNOWN_HANDLE_ENTRY_COUNT = 0x3e;
inline constexpr std::size_t WORLD_CHR_MAN_HANDLE_CHR_SET_OFFSET = 0x850;
inline constexpr std::size_t WORLD_CHR_MAN_HANDLE_CHR_SET_COUNT = 0x3e;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_AREA_CHR_OFFSET = 0xa70;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_AREA_CHR_COUNT = 0x14;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_BLOCK_CHR_OFFSET = 0xcf0;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_BLOCK_CHR_COUNT = 0x3c;
inline constexpr std::size_t WORLD_CHR_MAN_ENTITY_LOOKUP_SIZE = 0x100;
inline constexpr std::size_t WORLD_CHR_MAN_ENTITY_MAPPING_SIZE = 0x08;
inline constexpr std::size_t WORLD_CHR_MAN_UNKNOWN_HANDLE_ENTRY_SIZE = 0x18;
inline constexpr std::size_t WORLD_CHR_MAN_CHR_THREAD_SIZE = 0x28;
inline constexpr std::size_t WORLD_CHR_MAN_CLOTH_THREAD_SIZE = 0x88;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_AREA_CHR_SIZE = 0x20;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_BLOCK_CHR_SIZE = 0x160;
inline constexpr std::size_t WORLD_CHR_MAN_WORLD_BLOCK_CHR_SET_OFFSET = 0xb8;
inline constexpr std::size_t WORLD_CHR_MAN_CHARACTER_UPDATE_ENTRY_SIZE = 0x18;
inline constexpr std::size_t WORLD_CHR_MAN_CHARACTER_UPDATE_ARRAY_COUNT = 0x3e;
inline constexpr std::size_t WORLD_CHR_MAN_CHARACTER_UPDATE_ARRAYS_OFFSET = 0x5fd0;
inline constexpr std::size_t WORLD_CHR_MAN_FULL_UPDATE_LIST_OFFSET = 0x61c0;
inline constexpr std::size_t WORLD_CHR_MAN_SKIPPED_UPDATE_LIST_OFFSET = 0x61d0;
inline constexpr std::size_t WORLD_CHR_MAN_OTHER_UPDATE_LIST_OFFSET = 0x61e0;

struct WorldChrManEntityMapping {
    std::int32_t entity_id;
    // Packed ChrHandle value; negative is the game's no-character sentinel.
    std::int32_t handle;

    bool has_handle() const { return handle >= 0; }
};

// Prefix view of a WorldBlockChr's entity mapping fields; not a separately
// allocated 0x100-byte object. Registration at RVA 0x1914c70 stores block
// pointers.
struct WorldChrManEntityLookup {
    Unknown<0xf0> _unk00;
    std::int32_t mapping_count;
    std::uint32_t _padf4;
    WorldChrManEntityMapping* mappings;

    // The sorted entity-to-handle mappings (count, element).
    std::size_t mappings_count() const {
        return (mappings && mapping_count > 0) ? static_cast<std::size_t>(mapping_count) : 0;
    }
    // Binary search for entity_id: the packed handle, or -1 when the entity is
    // absent or mapped to the negative sentinel.
    std::int32_t find_handle(std::int32_t entity_id) const {
        std::size_t lo = 0, hi = mappings_count();
        while (lo < hi) {
            const std::size_t mid = lo + (hi - lo) / 2;
            const std::int32_t key = mappings[mid].entity_id;
            if (key == entity_id) return mappings[mid].handle >= 0 ? mappings[mid].handle : -1;
            if (key < entity_id) lo = mid + 1; else hi = mid;
        }
        return -1;
    }
};

struct WorldChrManUnknownHandleEntry {
    // Registered ChrSet pointer. Legacy field/type names are retained;
    // registration at RVA 0x1914c70 establishes the more specific meaning.
    std::uintptr_t ptr;
    // ChrSet selector, not a packed ChrHandle or multiplayer member ID.
    std::int32_t handle;
    std::uint32_t _pad0c;
    // WorldBlockChr pointer for block sets; null for player and ghost sets.
    std::uintptr_t extra;
};

// Temporary actor work record allocated by RVA 0x19173b0, stride 0x18. The
// actor is borrowed, not retained or owned. Both this entry and its next
// pointer become invalid when RVA 0x19190a0 frees the backing array.
struct WorldChrManCharacterUpdateEntry {
    ChrIns* actor;
    // Frame delta, multiplied by the actor's positive update interval only
    // when that actor is selected for this pass's full-update list.
    float delta_time;
    std::uint32_t _pad0c;
    WorldChrManCharacterUpdateEntry* next;
};

// Borrowed links into the per-set work arrays, not session membership.
struct WorldChrManCharacterUpdateList {
    WorldChrManCharacterUpdateEntry* head;
    WorldChrManCharacterUpdateEntry* tail;
};

// Legacy name for the now-identified native character-work owner.
using WorldChrManChrThread = CSChrThread;
// Legacy name for the now-identified native cloth-work owner.
using WorldChrManClothThread = CSClothThread;

// Singleton named WorldChrMan. The native creation, retirement, transition
// snapshot, and collision-rebind boundaries are recorded above; this is an
// observation contract, not authorization for replacement behaviour.
//
// The early fields differ slightly from the DS3 layout: the player ChrSet
// starts at 0x30, and the main player pointer is at 0x60. The handle lookup
// table at 0x850 is used by Bloodborne's packed character handles.
struct WorldChrMan {
    void* vftable;
    WorldInfoOwner* world_info_owner;
    std::uint32_t world_area_chr_len;
    std::uint32_t _pad14;
    WorldAreaChr* world_area_chr_ptr;
    std::uint32_t world_block_chr_count;
    std::uint32_t _pad24;
    WorldBlockChr* world_block_chr_ptr;
    ChrSet<PlayerIns> player_chr_set;
    ChrSet<ChrIns> ghost_chr_set;
    PlayerIns* main_player;
    Unknown<0x28> _unk68;
    std::uint32_t entity_lookup_count;
    std::uint32_t _pad94;
    WorldChrManEntityLookup* entity_lookups[WORLD_CHR_MAN_ENTITY_LOOKUP_COUNT];
    // Number of compact, sorted registrations at +0x280. Set registration and
    // frame-array allocation/freeing use this same count.
    std::int32_t active_chr_set_count;
    std::uint32_t _pad27c;
    WorldChrManUnknownHandleEntry unknown_handle_entries[WORLD_CHR_MAN_UNKNOWN_HANDLE_ENTRY_COUNT];
    ChrSet<ChrIns>* handle_chr_sets[WORLD_CHR_MAN_HANDLE_CHR_SET_COUNT];
    NetWorldChrSync* net_world_chr_sync;
    std::uint8_t* _unk_a48;
    std::uint8_t* _unk_a50;
    WorldChrManChrThread* chr_thread;
    WorldChrManClothThread* cloth_thread;
    std::uint32_t update_counter;
    std::uint32_t net_world_chr_sync_refresh_state;
    WorldAreaChr world_area_chr[WORLD_CHR_MAN_WORLD_AREA_CHR_COUNT];
    WorldBlockChr world_block_chr[WORLD_CHR_MAN_WORLD_BLOCK_CHR_COUNT];
    Unknown<0x60> _unk5f70;
    // One native-owned allocation per active set, with capacity * 0x18 bytes.
    // Indexed by compact registration position, not ChrSet selector. Entries
    // borrow actors; the finalization boundary frees only the arrays.
    WorldChrManCharacterUpdateEntry* character_update_arrays[WORLD_CHR_MAN_CHARACTER_UPDATE_ARRAY_COUNT];
    // Eligible actors selected by their per-actor update interval this pass.
    WorldChrManCharacterUpdateList full_update_list;
    // Eligible actors whose interval does not select this pass. Native code
    // still runs reduced callbacks; these actors are not wholly frozen.
    WorldChrManCharacterUpdateList skipped_update_list;
    // Actors outside full-update eligibility. This alone does not establish
    // death, missing resources, visibility, or a failed native task.
    WorldChrManCharacterUpdateList other_update_list;

    // The entity lookup table at index, or null.
    WorldChrManEntityLookup* entity_lookup(std::size_t index) const {
        return index < WORLD_CHR_MAN_ENTITY_LOOKUP_COUNT ? entity_lookups[index] : nullptr;
    }

    // Defined WorldAreaChr / WorldBlockChr entries (count, element).
    std::size_t area_chr_count() const { return world_area_chr_ptr ? world_area_chr_len : 0; }
    WorldAreaChr* area_chr_at(std::size_t i) const { return i < area_chr_count() ? &world_area_chr_ptr[i] : nullptr; }
    std::size_t block_chr_count() const { return world_block_chr_ptr ? world_block_chr_count : 0; }
    WorldBlockChr* block_chr_at(std::size_t i) const {
        return i < block_chr_count() ? &world_block_chr_ptr[i] : nullptr;
    }

    // Number of character handle selectors accepted by the observed 1.09
    // lookup path (WorldInfoOwner block count + 2), or 0 without an owner.
    std::size_t chr_handle_selector_limit() const {
        if (!world_info_owner) return 0;
        const std::int64_t limit = static_cast<std::int64_t>(world_info_owner->world_block_chr_count) + 2;
        return limit > 0 ? static_cast<std::size_t>(limit) : 0;
    }

    // The ChrSet selected by a packed character handle (selector in bits
    // 14..19, entry index in bits 0..13; 0xffffffff is no character), or null.
    ChrSet<ChrIns>* chr_set_by_handle(std::uint32_t raw_handle) const {
        if (raw_handle == 0xffffffffu) return nullptr;
        const std::size_t selector = (raw_handle >> 14) & 0x3f;
        if (selector >= chr_handle_selector_limit() || selector >= WORLD_CHR_MAN_HANDLE_CHR_SET_COUNT) return nullptr;
        return handle_chr_sets[selector];
    }

    // Resolves a packed character handle to its character, or null.
    ChrIns* chr_ins_by_handle(std::uint32_t raw_handle) const {
        ChrSet<ChrIns>* set = chr_set_by_handle(raw_handle);
        return set ? set->chr(raw_handle & 0x3fff) : nullptr;
    }
};

namespace detail::world_chr_man_layout {
struct Probe;
static_assert(WORLD_CHR_MAN_NATIVE_PLAYER_CHR_SET_ALLOCATION_SIZE ==
                  WORLD_CHR_MAN_NATIVE_PLAYER_CHR_SET_CAPACITY * sizeof(ChrSetEntry<Probe>),
              "player ChrSet allocation");
static_assert(sizeof(ChrSetEntry<Probe>) == CHR_SET_ENTRY_SIZE, "ChrSetEntry stride");
BB_SIZE(WorldChrMan, WORLD_CHR_MAN_SIZE);
BB_SIZE(WorldChrManEntityLookup, WORLD_CHR_MAN_ENTITY_LOOKUP_SIZE);
BB_SIZE(WorldChrManEntityMapping, WORLD_CHR_MAN_ENTITY_MAPPING_SIZE);
BB_SIZE(WorldChrManUnknownHandleEntry, WORLD_CHR_MAN_UNKNOWN_HANDLE_ENTRY_SIZE);
BB_SIZE(WorldChrManCharacterUpdateEntry, WORLD_CHR_MAN_CHARACTER_UPDATE_ENTRY_SIZE);
BB_SIZE(WorldChrManChrThread, WORLD_CHR_MAN_CHR_THREAD_SIZE);
BB_SIZE(WorldChrManClothThread, WORLD_CHR_MAN_CLOTH_THREAD_SIZE);
BB_SIZE(WorldAreaChr, WORLD_CHR_MAN_WORLD_AREA_CHR_SIZE);
BB_SIZE(WorldBlockChr, WORLD_CHR_MAN_WORLD_BLOCK_CHR_SIZE);
BB_OFFSET(WorldBlockChr, chr_set, WORLD_CHR_MAN_WORLD_BLOCK_CHR_SET_OFFSET);
BB_OFFSET(WorldChrManEntityLookup, mapping_count, 0xf0);
BB_OFFSET(WorldChrManEntityLookup, mappings, 0xf8);
BB_OFFSET(WorldChrManChrThread, chr_thread, 0x08);
BB_OFFSET(WorldChrManChrThread, back_initialize_thread, 0x18);
BB_OFFSET(WorldChrManClothThread, cloth_thread, 0x08);
BB_OFFSET(WorldChrMan, world_info_owner, 0x08);
BB_OFFSET(WorldChrMan, world_area_chr_len, 0x10);
BB_OFFSET(WorldChrMan, world_area_chr_ptr, 0x18);
BB_OFFSET(WorldChrMan, world_block_chr_count, 0x20);
BB_OFFSET(WorldChrMan, world_block_chr_ptr, 0x28);
BB_OFFSET(WorldChrMan, player_chr_set, 0x30);
BB_OFFSET(WorldChrMan, ghost_chr_set, 0x48);
BB_OFFSET(WorldChrMan, main_player, WORLD_CHR_MAN_MAIN_PLAYER_OFFSET);
BB_OFFSET(WorldChrMan, entity_lookup_count, 0x90);
BB_OFFSET(WorldChrMan, entity_lookups, 0x98);
BB_OFFSET(WorldChrMan, unknown_handle_entries, 0x280);
BB_OFFSET(WorldChrMan, handle_chr_sets, WORLD_CHR_MAN_HANDLE_CHR_SET_OFFSET);
BB_OFFSET(WorldChrMan, net_world_chr_sync, 0xa40);
BB_OFFSET(WorldChrMan, _unk_a48, 0xa48);
BB_OFFSET(WorldChrMan, _unk_a50, 0xa50);
BB_OFFSET(WorldChrMan, chr_thread, 0xa58);
BB_OFFSET(WorldChrMan, cloth_thread, 0xa60);
BB_OFFSET(WorldChrMan, update_counter, 0xa68);
BB_OFFSET(WorldChrMan, net_world_chr_sync_refresh_state, 0xa6c);
BB_OFFSET(WorldChrMan, world_area_chr, WORLD_CHR_MAN_WORLD_AREA_CHR_OFFSET);
BB_OFFSET(WorldChrMan, world_block_chr, WORLD_CHR_MAN_WORLD_BLOCK_CHR_OFFSET);
BB_OFFSET(WorldChrMan, _unk5f70, 0x5f70);
static_assert(offsetof(WorldChrMan, _unk5f70) + 0x250 == 0x61c0, "WorldChrMan update lists");
BB_OFFSET(WorldChrMan, character_update_arrays, WORLD_CHR_MAN_CHARACTER_UPDATE_ARRAYS_OFFSET);
BB_OFFSET(WorldChrMan, full_update_list, WORLD_CHR_MAN_FULL_UPDATE_LIST_OFFSET);
BB_OFFSET(WorldChrMan, skipped_update_list, WORLD_CHR_MAN_SKIPPED_UPDATE_LIST_OFFSET);
BB_OFFSET(WorldChrMan, other_update_list, WORLD_CHR_MAN_OTHER_UPDATE_LIST_OFFSET);
}  // namespace detail::world_chr_man_layout

}  // namespace bb
