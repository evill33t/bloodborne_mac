// World AI owner (SprjWorldAiManager), its inline area/block records, and native container storage.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct WorldAreaInfo;
struct WorldBlockInfo;
struct WorldInfo;

// Native 0x30-byte tree node. Sentinel links point to itself, and color and
// sentinel bytes are both initialized to 1. Sentinel payload is not an entry.
struct WorldAiTreeNode {
    WorldAiTreeNode* left;
    WorldAiTreeNode* parent;
    WorldAiTreeNode* right;
    std::uint8_t color;
    std::uint8_t is_sentinel;
    Unknown<6> _unk1a;
    // Key representation has not been established for each owning tree.
    std::uint64_t key_bits;
    void* payload;
};

// Observed tree header shared by the manager and its block records. Payload
// meaning depends on the owning tree.
struct WorldAiTree {
    Unknown<8> _unk00;
    WorldAiTreeNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Native 0x28-byte vector storage including its leading unknown word. Pointer
// bounds are raw: only the manager's file list has a proven element
// interpretation (pointer-sized file handles).
struct WorldAiVectorStorage {
    Unknown<8> _unk00;
    void* first;
    void* last;
    void* capacity_end;
    void* allocator;
};

struct WorldAiBlock;

// Descriptive 0x28-byte inline area record. Constructor stores WorldAreaInfo,
// copies its block count, and points into the manager's block pool using the
// area's first block index. No independent native class name is established.
struct WorldAiArea {
    const void* vftable;
    WorldAreaInfo* info;
    std::uint32_t block_count;
    Unknown<4> _unk14;
    WorldAiBlock* blocks;
    // Constructor default 5; full state enumeration is unknown.
    std::uint32_t state;
    std::uint8_t flags[2];
    Unknown<2> _unk26;

    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva INSTANCE_VTABLE{0x531c840};
};

// Descriptive 0xd8-byte inline block record, constructed by RVA 0x12e21c0.
// The two tree headers own separately allocated 0x30-byte sentinels.
// Unresolved state, payload, and buffer semantics remain raw.
struct WorldAiBlock {
    const void* vftable;
    WorldBlockInfo* info;
    WorldAiArea* area;
    std::uint32_t state;
    std::uint8_t flags[3];
    std::uint8_t _unk1f;
    void* pointer_20;
    WorldAiTree tree_28;
    WorldAiTree tree_48;
    std::uint64_t value_68_bits;
    std::uint64_t values_70_bits[4];
    WorldAiVectorStorage buffer;
    std::uint64_t values_b8_bits[3];
    std::uint32_t value_d0;
    // Constructor default -1.
    std::int32_t value_d4;

    static constexpr std::size_t SIZE = 0xd8;
    static constexpr Rva INSTANCE_VTABLE{0x531c860};
    static constexpr Rva CONSTRUCTOR_FN{0x12e21c0};
};

// Native singleton allocated as 0x36e0 bytes aligned to 16 by RVA 0x19ba850.
// Singleton assertions identify SprjWorldAiManager; no reflected runtime class
// is claimed. Constructor RVA 0x12db150 binds the WorldInfo prefix of the
// loading WorldRes and builds fixed pools of 20 areas and 60 blocks. Only
// area_count/block_count entries receive native constructors.
//
// The area/block pointers refer to this object's inline pools, and blocks
// point back into the area pool. A live instance must not move. Destructor
// releases file handles, tree payloads, Lua bindings, initialized pool
// entries, Lua owners, debug registrations, synchronization, and storage.
//
// CSChrThread queues TICK_LUA_FN through a member fragment. It subtracts 1/30
// from lua_owner +0xc8 and, when due, resets to 0.5 and calls the Lua
// maintenance helper at RVA 0x210d390. That narrow callback does not establish
// the semantics of the manager's complete AI update path.
struct alignas(16) SprjWorldAiManager {
    const void* vftable;
    WorldInfo* world_info;
    std::uint32_t area_count;
    Unknown<4> _unk14;
    WorldAiArea* areas;
    std::uint32_t block_count;
    Unknown<4> _unk24;
    WorldAiBlock* blocks;
    WorldAiArea area_pool[20];  // only area_count entries are constructed
    WorldAiBlock block_pool[60];  // only block_count entries are constructed
    // Constructor writes all ones; interpretation remains open.
    std::uint64_t value_35f0_bits;
    // Owned 0xd0 allocation, initialized with the name AiLua.
    Unknown<0xd0>* lua_owner;
    // Owned 0x28 allocation, initially configured with 0x1fd buckets.
    Unknown<0x28>* lua_bindings;
    // Released through lua_bindings during native teardown.
    void* binding_3608;
    void* binding_3610;
    // Teardown invokes each non-null payload's virtual destructor and frees it.
    WorldAiTree objects;
    // Teardown frees payloads containing two string storage ranges.
    WorldAiTree strings;
    // Released through the SprjFile interface; zero means absent.
    std::uintptr_t file_handle;
    // Contains pointer-sized SprjFile handles, released one by one by cleanup.
    WorldAiVectorStorage files;
    // Native 0x18-byte pthread-mutex wrapper; internal layout remains opaque.
    Unknown<0x18> _mutex3688;
    void* pointer_36a0;
    void* pointer_36a8;
    std::uint8_t flags_36b0[3];
    Unknown<5> _unk36b3;
    void* pointers_36b8[3];
    std::uint32_t value_36d0;
    // Debug menu Distance Draw on chr, initialized to 20.0.
    float debug_draw_distance;
    // Debug menu Only Enable Draw on chr, initialized to zero.
    std::uint8_t debug_draw_enabled_only;
    Unknown<7> _unk36d9;

    static constexpr std::size_t SIZE = 0x36e0;
    static constexpr std::size_t AREA_CAPACITY = 20;
    static constexpr std::size_t BLOCK_CAPACITY = 60;
    static constexpr Rva SINGLETON_PTR = SPRJ_WORLD_AI_MANAGER_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4935687};
    static constexpr Rva INSTANCE_VTABLE{0x531c7c0};
    static constexpr Rva CONSTRUCTOR_FN{0x12db150};
    static constexpr Rva DESTRUCTOR_FN{0x12dbc90};
    static constexpr Rva RELEASE_FILES_FN{0x12dc2e0};
    static constexpr Rva TICK_LUA_FN{0x12dc830};
};

namespace detail::world_ai_layout {
BB_SIZE(SprjWorldAiManager, SprjWorldAiManager::SIZE);
static_assert(alignof(SprjWorldAiManager) == 16, "alignof(SprjWorldAiManager)");
BB_OFFSET(SprjWorldAiManager, world_info, 0x08);
BB_OFFSET(SprjWorldAiManager, area_count, 0x10);
BB_OFFSET(SprjWorldAiManager, areas, 0x18);
BB_OFFSET(SprjWorldAiManager, block_count, 0x20);
BB_OFFSET(SprjWorldAiManager, blocks, 0x28);
BB_OFFSET(SprjWorldAiManager, area_pool, 0x30);
BB_OFFSET(SprjWorldAiManager, block_pool, 0x350);
BB_OFFSET(SprjWorldAiManager, value_35f0_bits, 0x35f0);
BB_OFFSET(SprjWorldAiManager, lua_owner, 0x35f8);
BB_OFFSET(SprjWorldAiManager, lua_bindings, 0x3600);
BB_OFFSET(SprjWorldAiManager, objects, 0x3618);
BB_OFFSET(SprjWorldAiManager, strings, 0x3638);
BB_OFFSET(SprjWorldAiManager, file_handle, 0x3658);
BB_OFFSET(SprjWorldAiManager, files, 0x3660);
BB_OFFSET(SprjWorldAiManager, _mutex3688, 0x3688);
BB_OFFSET(SprjWorldAiManager, pointer_36a0, 0x36a0);
BB_OFFSET(SprjWorldAiManager, flags_36b0, 0x36b0);
BB_OFFSET(SprjWorldAiManager, pointers_36b8, 0x36b8);
BB_OFFSET(SprjWorldAiManager, value_36d0, 0x36d0);
BB_OFFSET(SprjWorldAiManager, debug_draw_distance, 0x36d4);
BB_OFFSET(SprjWorldAiManager, debug_draw_enabled_only, 0x36d8);
BB_SIZE(WorldAiArea, WorldAiArea::SIZE);
static_assert(alignof(WorldAiArea) == 8, "alignof(WorldAiArea)");
BB_OFFSET(WorldAiArea, info, 0x08);
BB_OFFSET(WorldAiArea, block_count, 0x10);
BB_OFFSET(WorldAiArea, blocks, 0x18);
BB_OFFSET(WorldAiArea, state, 0x20);
BB_OFFSET(WorldAiArea, flags, 0x24);
BB_SIZE(WorldAiBlock, WorldAiBlock::SIZE);
static_assert(alignof(WorldAiBlock) == 8, "alignof(WorldAiBlock)");
BB_OFFSET(WorldAiBlock, info, 0x08);
BB_OFFSET(WorldAiBlock, area, 0x10);
BB_OFFSET(WorldAiBlock, state, 0x18);
BB_OFFSET(WorldAiBlock, flags, 0x1c);
BB_OFFSET(WorldAiBlock, tree_28, 0x28);
BB_OFFSET(WorldAiBlock, tree_48, 0x48);
BB_OFFSET(WorldAiBlock, value_68_bits, 0x68);
BB_OFFSET(WorldAiBlock, buffer, 0x90);
BB_OFFSET(WorldAiBlock, values_b8_bits, 0xb8);
BB_OFFSET(WorldAiBlock, value_d4, 0xd4);
BB_SIZE(WorldAiTree, 0x20);
BB_OFFSET(WorldAiTree, sentinel, 0x08);
BB_OFFSET(WorldAiTree, count, 0x10);
BB_OFFSET(WorldAiTree, allocator, 0x18);
BB_SIZE(WorldAiTreeNode, 0x30);
BB_OFFSET(WorldAiTreeNode, color, 0x18);
BB_OFFSET(WorldAiTreeNode, is_sentinel, 0x19);
BB_OFFSET(WorldAiTreeNode, payload, 0x28);
BB_SIZE(WorldAiVectorStorage, 0x28);
BB_OFFSET(WorldAiVectorStorage, first, 0x08);
BB_OFFSET(WorldAiVectorStorage, last, 0x10);
BB_OFFSET(WorldAiVectorStorage, capacity_end, 0x18);
BB_OFFSET(WorldAiVectorStorage, allocator, 0x20);
}  // namespace detail::world_ai_layout

}  // namespace bb
