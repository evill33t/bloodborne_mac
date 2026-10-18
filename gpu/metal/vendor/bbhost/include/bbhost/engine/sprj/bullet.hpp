// Bullet-system runtime metadata: SprjBulletManager and its pools.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_BULLET_MANAGER_SIZE = 0x168;
inline constexpr std::size_t SPRJ_BULLET_POOL_ENTRY_SIZE = 0x890;
inline constexpr std::size_t SPRJ_BULLET_POOL_ENTRY_COUNT = 0x80;
inline constexpr std::size_t SPRJ_BULLET_SECONDARY_ENTRY_SIZE = 0x640;
inline constexpr std::size_t SPRJ_BULLET_SECONDARY_ENTRY_COUNT = 0x40;
inline constexpr std::size_t SPRJ_BULLET_GROUP_SIZE = 0x2720;
inline constexpr std::size_t SPRJ_BULLET_GROUP_COUNT = 0x04;
inline constexpr std::size_t SPRJ_BULLET_GROUP_ENTRY_SIZE = 0x270;
inline constexpr std::size_t SPRJ_BULLET_GROUP_ENTRY_COUNT = 0x10;
inline constexpr Rva SPRJ_BULLET_INS_CTOR_FN{0x19f63c0};
inline constexpr Rva SPRJ_BULLET_MANAGER_UPDATE_NETWORK_FN{0x1a011a0};
inline constexpr Rva SPRJ_BULLET_MANAGER_SEND_FULL_SNAPSHOT_FN{0x1a04190};

// Primary bullet-pool entry. The manager constructor creates 0x80 entries of
// stride 0x890 with SprjBulletIns_Ctor and threads them through +0x868. Most
// contents are opaque.
struct SprjBulletPoolEntry {
    void* vftable;
    std::uint32_t packed_id;
    std::uint32_t _unk0c;
    Unknown<0x858> _unk10;
    SprjBulletPoolEntry* next_entry;
    Unknown<0x20> _unk870;
};

// Secondary bullet-pool entry: 0x40 entries of stride 0x640 built by RVA
// 0x19ef980. Destructor evidence proves owned pointers at +0x1a0, +0x5e8, +0x628.
struct SprjBulletSecondaryEntry {
    std::uint32_t packed_id;
    Unknown<0x19c> _unk04;
    Unknown<0x10>* helper;
    Unknown<0x440> _unk1a8;
    Unknown<1>* ref_counted_link;
    Unknown<0x38> _unk5f0;
    Unknown<1>* resource;
    Unknown<0x10> _unk630;
};

// Grouped bullet helper pool. RVA 0x19f3c80 initializes sixteen subentries at
// +0x10 with stride 0x270; the manager allocates four groups.
struct SprjBulletGroup {
    std::uint32_t packed_id;
    Unknown<0x0c> _unk04;
    Unknown<SPRJ_BULLET_GROUP_ENTRY_SIZE> entries[SPRJ_BULLET_GROUP_ENTRY_COUNT];
    std::uint32_t count;
    std::uint32_t _pad2714;
    SprjBulletGroup* next_group;
};

// Vector/list prefix embedded several times in SprjBulletManager. The
// constructor sets the table slot, begin/end/capacity and allocator. Element
// type and semantics are opaque.
struct SprjBulletManagerVector {
    void* vftable;
    std::uint64_t _unk08;
    Unknown<1>* begin;
    Unknown<1>* end;
    Unknown<1>* capacity;
    Unknown<1>* allocator;
};

// Observed SprjBulletManager singleton. World startup allocates 0x168 bytes
// and calls RVA 0x1a003f0, which allocates the 0x44800-byte primary pool (0x80
// entries of 0x890). Other embedded containers stay opaque vector prefixes.
struct SprjBulletManager {
    SprjBulletPoolEntry* bullet_pool;
    std::uint64_t _unk08;
    SprjBulletPoolEntry* bullet_pool_head;
    std::uint64_t _unk18;
    SprjBulletSecondaryEntry* secondary_pool;
    std::uint64_t _unk28;
    SprjBulletSecondaryEntry* secondary_pool_head;
    std::uint64_t _unk38;
    SprjBulletGroup* group_pool;
    std::uint64_t _unk48;
    SprjBulletGroup* group_pool_head;
    Unknown<0x18> _unk58;
    SprjBulletManagerVector vector_70;
    SprjBulletManagerVector vector_a0;
    SprjBulletManagerVector vector_d0;
    SprjBulletManagerVector vector_100;
    std::uint8_t flag_130;
    Unknown<0x27> _unk131;
    std::uint8_t flag_158;
    Unknown<0x03> _unk159;
    std::uint32_t debug_float_bits_15c;
    std::uint32_t _unk160;
    std::uint32_t _unk164;

    static constexpr Rva SINGLETON_PTR = SPRJ_BULLET_MANAGER_SINGLETON_PTR;

    // Pools as pointer + count; null (count 0) before allocation.
    SprjBulletPoolEntry* bullet_pool_entries() const { return bullet_pool; }
    std::size_t bullet_pool_entry_count() const { return bullet_pool ? SPRJ_BULLET_POOL_ENTRY_COUNT : 0; }
    SprjBulletSecondaryEntry* secondary_pool_entries() const { return secondary_pool; }
    std::size_t secondary_pool_entry_count() const { return secondary_pool ? SPRJ_BULLET_SECONDARY_ENTRY_COUNT : 0; }
    SprjBulletGroup* groups() const { return group_pool; }
    std::size_t group_count() const { return group_pool ? SPRJ_BULLET_GROUP_COUNT : 0; }
};

// Marker for the observed SprjBulletIns class. Not a layout type.
struct SprjBulletIns {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_BULLET_INS_RUNTIME_CLASS;
};

namespace detail::bullet_layout {
static_assert(SprjBulletManager::SINGLETON_PTR.rva == 0x553e8d0, "SprjBulletManager::SINGLETON_PTR");
static_assert(SprjBulletIns::RUNTIME_CLASS.runtime_class_ptr.rva == 0x5579c28, "SprjBulletIns runtime class");
BB_SIZE(SprjBulletManagerVector, 0x30);
BB_SIZE(SprjBulletPoolEntry, SPRJ_BULLET_POOL_ENTRY_SIZE);
BB_SIZE(SprjBulletSecondaryEntry, SPRJ_BULLET_SECONDARY_ENTRY_SIZE);
BB_SIZE(SprjBulletGroup, SPRJ_BULLET_GROUP_SIZE);
BB_SIZE(SprjBulletManager, SPRJ_BULLET_MANAGER_SIZE);
BB_OFFSET(SprjBulletManager, bullet_pool, 0x00);
BB_OFFSET(SprjBulletManager, bullet_pool_head, 0x10);
BB_OFFSET(SprjBulletManager, secondary_pool, 0x20);
BB_OFFSET(SprjBulletManager, secondary_pool_head, 0x30);
BB_OFFSET(SprjBulletManager, group_pool, 0x40);
BB_OFFSET(SprjBulletManager, group_pool_head, 0x50);
BB_OFFSET(SprjBulletManager, vector_70, 0x70);
BB_OFFSET(SprjBulletManager, vector_a0, 0xa0);
BB_OFFSET(SprjBulletManager, vector_d0, 0xd0);
BB_OFFSET(SprjBulletManager, vector_100, 0x100);
BB_OFFSET(SprjBulletManager, flag_130, 0x130);
BB_OFFSET(SprjBulletManager, flag_158, 0x158);
BB_OFFSET(SprjBulletManager, debug_float_bits_15c, 0x15c);
static_assert(SPRJ_BULLET_POOL_ENTRY_SIZE * SPRJ_BULLET_POOL_ENTRY_COUNT == 0x44800, "bullet pool bytes");
static_assert(SPRJ_BULLET_SECONDARY_ENTRY_SIZE * SPRJ_BULLET_SECONDARY_ENTRY_COUNT == 0x19000, "secondary pool bytes");
static_assert(SPRJ_BULLET_GROUP_SIZE * SPRJ_BULLET_GROUP_COUNT == 0x9c80, "group pool bytes");
static_assert(SPRJ_BULLET_GROUP_ENTRY_SIZE * SPRJ_BULLET_GROUP_ENTRY_COUNT == 0x2700, "group entry bytes");
BB_OFFSET(SprjBulletPoolEntry, packed_id, 0x08);
BB_OFFSET(SprjBulletPoolEntry, next_entry, 0x868);
BB_OFFSET(SprjBulletSecondaryEntry, packed_id, 0x00);
BB_OFFSET(SprjBulletSecondaryEntry, helper, 0x1a0);
BB_OFFSET(SprjBulletSecondaryEntry, ref_counted_link, 0x5e8);
BB_OFFSET(SprjBulletSecondaryEntry, resource, 0x628);
BB_OFFSET(SprjBulletGroup, packed_id, 0x00);
BB_OFFSET(SprjBulletGroup, entries, 0x10);
BB_OFFSET(SprjBulletGroup, count, 0x2710);
BB_OFFSET(SprjBulletGroup, next_group, 0x2718);
}  // namespace detail::bullet_layout

}  // namespace bb
