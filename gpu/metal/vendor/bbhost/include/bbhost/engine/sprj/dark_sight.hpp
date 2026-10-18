// Native dark-sight volumes, their map ownership, and the special-effect
// consumer that feeds managed spheres.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// The four virtual slots shared by the observed volumes (raw native addresses,
// not callable function pointers).
struct SprjDarkSightVolumeVTable {
    const void* destructor;
    const void* delete_;  // `delete` is a C++ keyword
    const void* contains_point;
    const void* debug_draw;
};

// Common 0x30-byte prefix of the observed volume allocations (descriptive name,
// not a reflected class). All volumes are native-owned.
struct alignas(16) SprjDarkSightVolume {
    const SprjDarkSightVolumeVTable* vftable;
    // Map-backed constructors store a borrowed region pointer. Managed-sphere
    // construction leaves this word uninitialized; never assume it is set
    // without establishing the concrete variant and its lifetime.
    const void* source_region;
    float bounds_min[4];
    float bounds_max[4];
};

// Complete 0x40-byte sphere shared by map shape 2 and managed dynamic volumes
// (different vtables). Updating an existing managed sphere replaces only
// center/radius, leaving its constructor-time bounds stale. Containment reads
// center/radius directly and excludes the surface.
struct alignas(16) SprjDarkSightSphere {
    SprjDarkSightVolume volume;
    float center[3];
    float radius;

    static constexpr std::size_t SIZE = 0x40;
    static constexpr Rva MAP_VTABLE{0x5335df0};
    static constexpr Rva MANAGED_VTABLE{0x5335e80};
    static constexpr Rva MAP_CONTAINS_FN{0x1a77e40};
    static constexpr Rva MANAGED_CONTAINS_FN{0x1a791d0};

    // The native arithmetic on this snapshot (no vtable call or bounds test).
    // Radius is squared as stored, negative included. NaNs fail the ordered
    // comparison; zero radius contains no point, not even the center.
    bool contains_point(const float point[3]) const {
        const float x = point[0] - center[0];
        const float y = point[1] - center[1];
        const float z = point[2] - center[2];
        return (x * x + y * y) + z * z < radius * radius;
    }
};

// Complete 0x60-byte map shape 3, conventionally a cylinder. The constructor
// stores the origin, the axis scaled by height, radius and squared radius. Its
// containment arithmetic differs from the usual cylinder formula (see
// contains_point). The last eight bytes have no established meaning.
struct alignas(16) SprjDarkSightCylinder {
    SprjDarkSightVolume volume;
    float origin[4];
    float axis_delta[4];
    float radius;
    float radius_squared;
    std::uint8_t _unk58[8];

    static constexpr std::size_t SIZE = 0x60;
    static constexpr Rva VTABLE{0x5335e20};
    static constexpr Rva CONSTRUCTOR_FN{0x1a77f20};
    static constexpr Rva CONTAINS_FN{0x1a781a0};

    // The verified native query, repeated projection sum included. With
    // d = point-origin, a = axis_delta, p = dot(d,a): tests 0 <= p <= dot(a,a),
    // then dot(d,d) - 3*p*p/dot(a,a) < radius_squared, in native operation
    // order. Uses the stored squared radius; ignores bounds and radius. Not an
    // ideal-cylinder predicate. A zero axis reaches NaN and returns false.
    bool contains_point(const float point[3]) const {
        const float x = point[0] - origin[0];
        const float y = point[1] - origin[1];
        const float z = point[2] - origin[2];
        const float ax = axis_delta[0], ay = axis_delta[1], az = axis_delta[2];
        const float projection = (x * ax + y * ay) + z * az;
        const float axis_squared = (ax * ax + ay * ay) + az * az;
        if (!(0.0f <= projection && projection <= axis_squared)) return false;
        const float projection_squared = projection * projection;
        const float repeated = (projection_squared + projection_squared) + projection_squared;
        const float adjusted_distance = (x * x + y * y) + z * z + repeated * (-1.0f / axis_squared);
        return adjusted_distance < radius_squared;
    }
};

// Complete 0x80-byte map shape 5. The query subtracts center, projects onto
// the three axes in native order, then checks inclusive +/- extents.
struct alignas(16) SprjDarkSightBox {
    SprjDarkSightVolume volume;
    float half_extents[4];
    float axes[3][4];
    float center[4];

    static constexpr std::size_t SIZE = 0x80;
    static constexpr Rva VTABLE{0x5335e50};
    static constexpr Rva CONSTRUCTOR_FN{0x1a78910};
    static constexpr Rva CONTAINS_FN{0x1a78bb0};
};

struct SprjDarkSightVolumeNode {
    SprjDarkSightVolumeNode* next;
    SprjDarkSightVolumeNode* previous;
    // Owned by the map/managed/one-shot list; borrowed in the removal queue.
    // Null after a failed managed-sphere allocation.
    SprjDarkSightVolume* volume;
};

// Native list owner. Its 0x18-byte sentinel initializes only links; never read
// the sentinel's volume field. The first word is unclassified.
struct SprjDarkSightVolumeList {
    std::uint64_t _unk00;
    SprjDarkSightVolumeNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// Full 0x48-byte tree-node allocation. The sentinel initializes only links and
// color/is_nil; its key and volume list are not payload.
struct SprjDarkSightMapNode {
    SprjDarkSightMapNode* left;
    SprjDarkSightMapNode* parent;
    SprjDarkSightMapNode* right;
    std::uint8_t color_raw;
    std::uint8_t is_nil;
    std::uint8_t _unk1a[6];
    std::uint32_t map_key;
    std::uint32_t _unk24;
    SprjDarkSightVolumeList volumes;
};

// Map-keyed tree owner. Keys are copied from the map resource's +0x340 word.
struct SprjDarkSightMapTree {
    std::uint64_t _unk00;
    SprjDarkSightMapNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// Native SprjDarkSight singleton, allocated 0xe8 aligned to eight by RVA
// 0x1927cf0; identified through singleton assertions and consumers (no
// reflected class or instance vtable). Holds map-owned volumes, managed spheres,
// deferred managed removals and active/pending one-shot lists (names from the
// native debug menu; no producer of pending_one_shot entries located).
//
// UPDATE_FN drains removals, destroys the previous active one-shot volumes,
// moves pending one-shot entries to active (rearming one more update for their
// retirement) and draws volumes when enabled. The conditional task runs in
// DarkSight (37); enabling debug drawing or first queuing a managed removal
// arms/registers it. Ordinary managed creation does not schedule it.
//
// Destruction releases all owned volumes, clears removal aliases, unregisters
// the task, removes the debug root and frees container storage. Map clear
// invalidates that map's volume pointers; a queued managed removal invalidates
// its pointer when UPDATE_FN drains it.
struct SprjDarkSight {
    SprjDarkSightMapTree maps;
    SprjDarkSightVolumeList managed;
    SprjDarkSightVolumeList pending_removals;
    SprjDarkSightVolumeList active_one_shot;
    SprjDarkSightVolumeList pending_one_shot;
    SprjCallbackTask38 update_task;
    void* debug_root;
    std::uint8_t debug_draw_enabled;
    std::uint8_t _unke1[7];

    static constexpr std::size_t SIZE = 0xe8;
    static constexpr Rva SINGLETON_PTR = SPRJ_DARK_SIGHT_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x492febd};
    static constexpr Rva CONSTRUCTOR_FN{0x1a74f40};
    static constexpr Rva DESTRUCTOR_FN{0x1a75da0};
    static constexpr Rva STARTUP_OWNER_CONSTRUCTOR_FN{0x1927cf0};
    static constexpr Rva STARTUP_OWNER_DESTRUCTOR_FN{0x1928360};
    static constexpr Rva UPDATE_FN{0x1a753f0};
    static constexpr Rva UPDATE_TASK_VTABLE{0x5399700};
    static constexpr SprjTaskGroupIndex UPDATE_GROUP = SprjTaskGroupIndex::DarkSight;
    // Creates/reuses a tree node for map +0x340 and appends region shapes
    // 2/3/5. Does not clear an existing node's volume list first.
    static constexpr Rva LOAD_MAP_VOLUMES_FN{0x1a76350};
    // Destroys/frees matching map volumes and nodes, but keeps its tree node and
    // empty sentinel until manager destruction.
    static constexpr Rva CLEAR_MAP_VOLUMES_FN{0x1a76b70};
    // Null input allocates/owns a managed sphere; existing input updates only
    // center/radius. The returned pointer is borrowed from this manager.
    static constexpr Rva CREATE_OR_UPDATE_SPHERE_FN{0x1a76c90};
    // Appends a borrowed sphere pointer to pending_removals: no deduplication or
    // immediate destruction; callers must avoid duplicate retirement.
    static constexpr Rva QUEUE_SPHERE_REMOVAL_FN{0x1a76d90};
    // Queries map lists, then managed, then active_one_shot via volume +0x10.
    // Pending one-shot volumes are not queried; pending removals stay visible
    // through managed until the update unlinks/frees them.
    static constexpr Rva CONTAINS_POINT_FN{0x1a76e10};
    static constexpr Rva DEBUG_EDIT_FN{0x1a76f90};
    static constexpr Rva DEBUG_DISPLAY_FN{0x1a76ff0};
    static constexpr Rva ADD_DEBUG_ROW_FN{0x1a775d0};
    static constexpr Rva MAP_LOAD_CALLER_FN{0x1592780};
    static constexpr Rva MAP_CLEAR_CALLER_FN{0x15924b0};
    // ChrIns_SetMapCollisionEntry copies the high nibble at actor +0x281 to the
    // low nibble when nonzero; if this manager exists and the actor's physics
    // position is contained, it clears the low nibble.
    static constexpr Rva CHR_COLLISION_BINDING_FN{0x18bba70};
    static constexpr std::size_t CHR_COLLISION_NIBBLES_OFFSET = 0x281;
};

// 0x48-byte special-effect callback allocated by RVA 0x189b3b0 (class name and
// callback-base semantics unproven; not the manager's embedded task).
// UPDATE_FN scans actor SpEffect nodes, borrowing managed spheres for active
// rows with positive antiDarkSightRadius, positioned at the row's dummy poly
// when resolvable, else the actor's physics position. Unused sphere slots are
// queued for deferred removal and the vector shortened. Destruction likewise
// queues each sphere, then frees only the vector storage.
struct SprjDarkSightEffectCallback {
    const void* vftable;
    // Constructor writes zero; callback-list lookup checks it for zero.
    std::uint32_t selector_raw;
    // Constructor 0x25; insertion sets 0x4000. Update clears 0x4000 only if it
    // finds no positive-radius row, even when all positive rows are inactive.
    std::uint16_t flags_raw;
    std::uint16_t _unk0e;
    void* next_callback;
    std::uint64_t _unk18;
    std::uint64_t _unk20;
    SprjDarkSightSphere** spheres_begin;
    SprjDarkSightSphere** spheres_end;
    SprjDarkSightSphere** spheres_capacity_end;
    void* allocator;

    static constexpr std::size_t SIZE = 0x48;
    static constexpr Rva VTABLE{0x536f580};
    static constexpr Rva CONSTRUCTOR_FN{0x14ff220};
    static constexpr Rva DESTRUCTOR_FN{0x14ff2f0};
    static constexpr Rva UPDATE_FN{0x14ff5c0};
    static constexpr Rva ENSURE_CALLBACK_FN{0x189b3b0};
    static constexpr std::size_t UPDATE_VTABLE_OFFSET = 0x20;
    static constexpr std::size_t RADIUS_PARAM_OFFSET = 0x16c;
    static constexpr std::size_t DUMMY_POLY_PARAM_OFFSET = 0x170;
    static constexpr std::uint32_t INACTIVE_EFFECT_MASK = 0x800c0003;
};

namespace detail::dark_sight_layout {
BB_SIZE(SprjDarkSight, SprjDarkSight::SIZE);
static_assert(alignof(SprjDarkSight) == 8, "alignof(SprjDarkSight)");
BB_OFFSET(SprjDarkSight, maps, 0);
BB_OFFSET(SprjDarkSight, managed, 0x20);
BB_OFFSET(SprjDarkSight, pending_removals, 0x40);
BB_OFFSET(SprjDarkSight, active_one_shot, 0x60);
BB_OFFSET(SprjDarkSight, pending_one_shot, 0x80);
BB_OFFSET(SprjDarkSight, update_task, 0xa0);
BB_OFFSET(SprjDarkSight, debug_root, 0xd8);
BB_OFFSET(SprjDarkSight, debug_draw_enabled, 0xe0);
static_assert(offsetof(SprjDarkSight, update_task) + offsetof(SprjCallbackTask38, value_18) == 0xb8,
              "SprjDarkSight::update_task.value_18");
static_assert(offsetof(SprjDarkSight, update_task) + offsetof(SprjCallbackTask38, owner) == 0xc0,
              "SprjDarkSight::update_task.owner");
static_assert(offsetof(SprjDarkSight, update_task) + offsetof(SprjCallbackTask38, callback) == 0xc8,
              "SprjDarkSight::update_task.callback");
static_assert(static_cast<std::uint32_t>(SprjDarkSight::UPDATE_GROUP) == 37, "SprjDarkSight::UPDATE_GROUP");
BB_SIZE(SprjDarkSightMapTree, 0x20);
BB_SIZE(SprjDarkSightVolumeList, 0x20);
BB_OFFSET(SprjDarkSightVolumeList, sentinel, 8);
BB_OFFSET(SprjDarkSightVolumeList, count, 0x10);
BB_OFFSET(SprjDarkSightVolumeList, allocator, 0x18);
BB_SIZE(SprjDarkSightVolumeNode, 0x18);
BB_OFFSET(SprjDarkSightVolumeNode, volume, 0x10);
BB_SIZE(SprjDarkSightMapNode, 0x48);
BB_OFFSET(SprjDarkSightMapNode, is_nil, 0x19);
BB_OFFSET(SprjDarkSightMapNode, map_key, 0x20);
BB_OFFSET(SprjDarkSightMapNode, volumes, 0x28);
static_assert(offsetof(SprjDarkSightMapNode, volumes) + offsetof(SprjDarkSightVolumeList, sentinel) == 0x30,
              "SprjDarkSightMapNode::volumes.sentinel");
BB_SIZE(SprjDarkSightVolumeVTable, 0x20);
BB_OFFSET(SprjDarkSightVolumeVTable, contains_point, 0x10);
BB_SIZE(SprjDarkSightVolume, 0x30);
BB_OFFSET(SprjDarkSightVolume, source_region, 8);
BB_OFFSET(SprjDarkSightVolume, bounds_min, 0x10);
BB_OFFSET(SprjDarkSightVolume, bounds_max, 0x20);
BB_SIZE(SprjDarkSightSphere, SprjDarkSightSphere::SIZE);
static_assert(alignof(SprjDarkSightSphere) == 16, "alignof(SprjDarkSightSphere)");
BB_OFFSET(SprjDarkSightSphere, center, 0x30);
BB_OFFSET(SprjDarkSightSphere, radius, 0x3c);
BB_SIZE(SprjDarkSightCylinder, SprjDarkSightCylinder::SIZE);
static_assert(alignof(SprjDarkSightCylinder) == 16, "alignof(SprjDarkSightCylinder)");
BB_OFFSET(SprjDarkSightCylinder, origin, 0x30);
BB_OFFSET(SprjDarkSightCylinder, axis_delta, 0x40);
BB_OFFSET(SprjDarkSightCylinder, radius, 0x50);
BB_OFFSET(SprjDarkSightCylinder, radius_squared, 0x54);
BB_SIZE(SprjDarkSightBox, SprjDarkSightBox::SIZE);
static_assert(alignof(SprjDarkSightBox) == 16, "alignof(SprjDarkSightBox)");
BB_OFFSET(SprjDarkSightBox, half_extents, 0x30);
BB_OFFSET(SprjDarkSightBox, axes, 0x40);
BB_OFFSET(SprjDarkSightBox, center, 0x70);
BB_SIZE(SprjDarkSightEffectCallback, SprjDarkSightEffectCallback::SIZE);
static_assert(alignof(SprjDarkSightEffectCallback) == 8, "alignof(SprjDarkSightEffectCallback)");
BB_OFFSET(SprjDarkSightEffectCallback, flags_raw, 0xc);
BB_OFFSET(SprjDarkSightEffectCallback, next_callback, 0x10);
BB_OFFSET(SprjDarkSightEffectCallback, spheres_begin, 0x28);
BB_OFFSET(SprjDarkSightEffectCallback, spheres_end, 0x30);
BB_OFFSET(SprjDarkSightEffectCallback, spheres_capacity_end, 0x38);
BB_OFFSET(SprjDarkSightEffectCallback, allocator, 0x40);
}  // namespace detail::dark_sight_layout

}  // namespace bb
