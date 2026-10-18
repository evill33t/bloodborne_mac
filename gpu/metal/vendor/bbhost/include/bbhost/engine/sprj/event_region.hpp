// Event-region ownership and the five runtime shapes created from map data.
//
// Only SprjEventRegionMan has an established native name. Supporting type
// names are descriptive. Pointers and allocations remain owned by native code.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct SprjEventRegionNode;
struct SprjEventRegionCore;

// Full 0x28-byte singleton, allocated with eight-byte alignment. Owns a tree
// of signed region IDs and shape pointers. The first container word is opaque.
// Clear destroys shapes and nodes but retains the sentinel; destruction also
// frees the sentinel. Neither operation frees the borrowed source map data.
struct SprjEventRegionMan {
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva SINGLETON_PTR = SPRJ_EVENT_REGION_MAN_SINGLETON_PTR;
    static constexpr Rva NAME{0x492f7ea};
    static constexpr Rva VTABLE{0x5320b20};
    static constexpr Rva CONSTRUCTOR_FN{0x13c8140};
    static constexpr Rva DESTRUCTOR_FN{0x13c82f0};
    static constexpr Rva DELETE_FN{0x13c8250};
    static constexpr Rva CLEAR_FN{0x13c8390};
    // Rejects null source/ID pointer, existing ID, unsupported shape, and
    // failed shape allocation. Shape 0 returns true without creating an entry.
    // For shapes 1..=5, the virtual initializer's return is not checked before
    // insertion. Tree-node allocation failure follows the native transaction
    // abort path, rather than returning false.
    static constexpr Rva REGISTER_FN{0x13c84b0};
    static constexpr Rva REMOVE_FN{0x13c8a30};
    static constexpr Rva FIND_FN{0x13c8b70};
    // Singleton lookup that returns null when the manager or ID is absent.
    static constexpr Rva FIND_GLOBAL_FN{0x13c9c70};
    // Receives the embedded tree at manager +8, not the manager pointer.
    static constexpr Rva INSERT_NODE_FN{0x13c9440};
    // Walks resource +0x80's pointer table, skipping its final slot, null
    // records, missing ID pointers, and ID -1. Stops on first failed register;
    // previously inserted entries remain. The table index is passed to init.
    static constexpr Rva REGISTER_MAP_FN{0x13c9ce0};
    // Walks the same source table and removes by ID; no source-owner check.
    static constexpr Rva UNREGISTER_MAP_FN{0x13c9db0};
    static constexpr Rva WORLD_CREATE_FN{0x1565da0};
    static constexpr Rva WORLD_DESTROY_FN{0x1565e50};
    static constexpr Rva EMK_LOAD_FN{0x12f0d40};
    static constexpr Rva EMK_UNLOAD_FN{0x12f0e60};

    const void* vtable;
    std::uint64_t _unk08;
    SprjEventRegionNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// 0x30-byte red-black tree node. Sentinel key and payload are uninitialized.
// The sentinel's left/parent/right links initially point to itself; its color
// and is_nil bytes are both 1. Ordinary nodes start with both bytes zero.
struct SprjEventRegionNode {
    SprjEventRegionNode* left;
    SprjEventRegionNode* parent;
    SprjEventRegionNode* right;
    std::uint8_t color;
    std::uint8_t is_nil;
    Unknown<6> _unk1a;
    std::int32_t region_id;
    Unknown<4> _unk24;
    SprjEventRegionCore* region;
};

// Consumer-proven 0x50-byte prefix of a borrowed parsed map-region record.
// Not a claim about the complete resource record size or alignment.
struct SprjEventRegionSourcePrefix {
    Unknown<0x10> _unk00;
    std::uint32_t shape;
    float position[3];
    float rotation[3];
    Unknown<0x14> _unk2c;
    // One to three floats, interpreted according to shape; remains borrowed.
    const float* shape_data;
    // Only this record's leading signed ID has been established here.
    const std::int32_t* id_data;
};

// Five observed virtual slots. Slot +8 is a no-op for these five variants.
// Slot zero initializes from a source; it is not a destructor.
struct SprjEventRegionVTable {
    const void* initialize;
    const void* no_op;
    const void* contains_point;
    const void* position;
    const void* rotation;
};

// Embedded native wide string. Capacity <=7 selects inline UTF-16 storage;
// larger capacity selects the pointer in storage. Native cleanup frees that
// buffer through allocator. The neighboring +0x68 byte is outside the string.
struct SprjEventRegionWideString {
    std::uint64_t _unk00;
    std::uint8_t storage[16];
    std::uint64_t length;
    std::uint64_t capacity;
    void* allocator;
};

// Common prefix through +0x70, not the size of an independently allocated
// base class. Each concrete object also has control bytes at +0x70/+0x71 and
// shape data starting at +0x74. All observed shapes are allocated aligned to 16.
struct alignas(16) SprjEventRegionCore {
    static constexpr std::size_t PREFIX_SIZE = 0x70;
    static constexpr Rva BASE_VTABLE{0x5367bb0};
    static constexpr Rva INITIALIZE_COMMON_FN{0x13c6ab0};
    static constexpr Rva WIDE_STRING_CONSTRUCTOR_FN{0x27c0e00};

    const SprjEventRegionVTable* vtable;
    const SprjEventRegionSourcePrefix* source;
    // Initialized from the source rotation by RVA 0x13c6ab0.
    float direction[4];
    // Initialized zero; further semantics are unclassified.
    std::uint32_t state_20;
    // Source pointer-table index supplied by REGISTER_MAP_FN.
    std::int32_t source_index;
    // Truncated source rotation[1] * 57.29578; initially zero.
    std::int32_t yaw_degrees;
    Unknown<4> _unk2c;
    // Initialized null; later role remains unclassified.
    void* auxiliary;
    SprjEventRegionWideString wide_string;
    // Initialized one, separate from wide-string ownership.
    std::uint8_t state_68;
    Unknown<7> _unk69;
};

// Common bytes immediately after the 0x70 prefix. Both state bytes start
// zero; their semantics remain open. Shape parameters reuse the following
// +0x74 storage, so embedding a padded, larger base would shift native fields.
struct SprjEventRegionControl {
    std::uint8_t state_70;
    std::uint8_t state_71;
    Unknown<2> _unk72;
};

// Shape 1: horizontal circle, ignoring height. Runtime position comes from
// virtual +0x18 (source +0x14), rather than the cached direction vector.
struct alignas(16) SprjEventRegionCircle {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::uint32_t SOURCE_SHAPE = 1;
    static constexpr Rva VTABLE{0x5320a60};
    static constexpr Rva INITIALIZE_FN{0x13c7b50};
    static constexpr Rva CONTAINS_FN{0x13c7ba0};

    SprjEventRegionCore core;
    SprjEventRegionControl control;
    float radius;
    Unknown<8> _unk78;

    // Pure final native comparison after subtracting the borrowed position.
    // Radius is squared without clamping; the circumference is excluded and Y
    // is ignored.
    bool contains_relative_point(const float p[3]) const {
        return (p[0] * p[0] + 0.0f) + p[2] * p[2] < radius * radius;
    }
};

// Shape 2: sphere with a strict radius boundary.
struct alignas(16) SprjEventRegionSphere {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::uint32_t SOURCE_SHAPE = 2;
    static constexpr Rva VTABLE{0x5320ae0};
    static constexpr Rva INITIALIZE_FN{0x13c7e60};
    static constexpr Rva CONTAINS_FN{0x13c7eb0};

    SprjEventRegionCore core;
    SprjEventRegionControl control;
    float radius;
    Unknown<8> _unk78;

    // Pure comparison on a position-relative snapshot.
    bool contains_relative_point(const float p[3]) const {
        return (p[0] * p[0] + p[1] * p[1]) + p[2] * p[2] < radius * radius;
    }
};

// Shape 3: vertical cylinder. Radius is at +0x74 and height at +0x78.
// Bottom/top planes are included; the curved surface is excluded. The
// assembly's height gates accept unordered comparisons: a NaN Y passes both
// gates, and a NaN height bypasses only the upper-height gate.
struct alignas(16) SprjEventRegionCylinder {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::uint32_t SOURCE_SHAPE = 3;
    static constexpr Rva VTABLE{0x5320aa0};
    static constexpr Rva INITIALIZE_FN{0x13c7cc0};
    static constexpr Rva CONTAINS_FN{0x13c7d20};

    SprjEventRegionCore core;
    SprjEventRegionControl control;
    float radius;
    float height;
    Unknown<4> _unk7c;

    // Pure final query after subtracting the native position; no rotation.
    bool contains_relative_point(const float p[3]) const {
        return !(0.0f > p[1]) && !(p[1] > height) && (p[0] * p[0] + 0.0f) + p[2] * p[2] < radius * radius;
    }
};

// Shape 4: horizontal rectangle. Initialize stores half the source's first two
// dimensions. Native query subtracts position and applies inverse source yaw
// before testing X/Z; Y is ignored and all rectangle edges are excluded.
struct alignas(16) SprjEventRegionRectangle {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::uint32_t SOURCE_SHAPE = 4;
    static constexpr Rva VTABLE{0x53209e0};
    static constexpr Rva INITIALIZE_FN{0x13c75f0};
    static constexpr Rva CONTAINS_FN{0x13c7640};

    SprjEventRegionCore core;
    SprjEventRegionControl control;
    float half_width;
    float half_depth;
    // Initialized zero; no established consumer here.
    std::uint32_t state_7c;

    // Final bounds test only. Caller must first perform the native translation
    // and inverse-yaw transform.
    bool contains_local_point(const float p[3]) const {
        return -half_width < p[0] && p[0] < half_width && -half_depth < p[2] && p[2] < half_depth;
    }
};

// Shape 5: box. Bounds are XYZ and start at +0x78/+0x84, without SIMD
// alignment. For source dimensions [width, depth, height], min = [-width/2,
// height*0, -depth/2] and max = [width/2, height, depth/2]. The native
// height*0 is not a constant zero (nonfinite inputs can differ).
struct alignas(16) SprjEventRegionBox {
    static constexpr std::size_t SIZE = 0x90;
    static constexpr std::uint32_t SOURCE_SHAPE = 5;
    static constexpr Rva VTABLE{0x5320a20};
    static constexpr Rva INITIALIZE_FN{0x13c7840};
    static constexpr Rva CONTAINS_FN{0x13c78f0};

    SprjEventRegionCore core;
    SprjEventRegionControl control;
    // Initialized zero; not used by the observed initializer/query.
    std::uint32_t state_74;
    float bounds_min[3];
    float bounds_max[3];

    // Final bounds test after native translation/inverse yaw, excluding all
    // faces. Assembly confirms ordered comparisons: NaN on any axis fails.
    bool contains_local_point(const float p[3]) const {
        return bounds_min[0] < p[0] && bounds_min[1] < p[1] && bounds_min[2] < p[2] && p[0] < bounds_max[0] &&
               p[1] < bounds_max[1] && p[2] < bounds_max[2];
    }
};

namespace detail::event_region_layout {
BB_SIZE(SprjEventRegionMan, 0x28);
BB_SIZE(SprjEventRegionMan, SprjEventRegionMan::SIZE);
static_assert(alignof(SprjEventRegionMan) == 8, "alignof(SprjEventRegionMan)");
BB_OFFSET(SprjEventRegionMan, sentinel, 0x10);
BB_OFFSET(SprjEventRegionMan, count, 0x18);
BB_OFFSET(SprjEventRegionMan, allocator, 0x20);
BB_SIZE(SprjEventRegionNode, 0x30);
static_assert(alignof(SprjEventRegionNode) == 8, "alignof(SprjEventRegionNode)");
BB_OFFSET(SprjEventRegionNode, left, 0);
BB_OFFSET(SprjEventRegionNode, parent, 8);
BB_OFFSET(SprjEventRegionNode, right, 0x10);
BB_OFFSET(SprjEventRegionNode, color, 0x18);
BB_OFFSET(SprjEventRegionNode, is_nil, 0x19);
BB_OFFSET(SprjEventRegionNode, region_id, 0x20);
BB_OFFSET(SprjEventRegionNode, region, 0x28);

BB_SIZE(SprjEventRegionSourcePrefix, 0x50);
BB_OFFSET(SprjEventRegionSourcePrefix, shape, 0x10);
BB_OFFSET(SprjEventRegionSourcePrefix, position, 0x14);
BB_OFFSET(SprjEventRegionSourcePrefix, rotation, 0x20);
BB_OFFSET(SprjEventRegionSourcePrefix, shape_data, 0x40);
BB_OFFSET(SprjEventRegionSourcePrefix, id_data, 0x48);
BB_SIZE(SprjEventRegionVTable, 0x28);
BB_OFFSET(SprjEventRegionVTable, contains_point, 0x10);
BB_OFFSET(SprjEventRegionVTable, position, 0x18);
BB_OFFSET(SprjEventRegionVTable, rotation, 0x20);
BB_SIZE(SprjEventRegionWideString, 0x30);
BB_OFFSET(SprjEventRegionWideString, storage, 8);
BB_OFFSET(SprjEventRegionWideString, length, 0x18);
BB_OFFSET(SprjEventRegionWideString, capacity, 0x20);
BB_OFFSET(SprjEventRegionWideString, allocator, 0x28);
BB_SIZE(SprjEventRegionCore, 0x70);
BB_SIZE(SprjEventRegionCore, SprjEventRegionCore::PREFIX_SIZE);
static_assert(alignof(SprjEventRegionCore) == 16, "alignof(SprjEventRegionCore)");
BB_OFFSET(SprjEventRegionCore, source, 8);
BB_OFFSET(SprjEventRegionCore, direction, 0x10);
BB_OFFSET(SprjEventRegionCore, state_20, 0x20);
BB_OFFSET(SprjEventRegionCore, source_index, 0x24);
BB_OFFSET(SprjEventRegionCore, yaw_degrees, 0x28);
BB_OFFSET(SprjEventRegionCore, auxiliary, 0x30);
BB_OFFSET(SprjEventRegionCore, wide_string, 0x38);
BB_OFFSET(SprjEventRegionCore, state_68, 0x68);
BB_SIZE(SprjEventRegionControl, 4);
BB_OFFSET(SprjEventRegionControl, state_71, 1);

#define BB_EVENT_REGION_COMMON(T, n)                   \
    BB_SIZE(T, n);                                     \
    BB_SIZE(T, T::SIZE);                               \
    static_assert(alignof(T) == 16, "alignof(" #T ")"); \
    BB_OFFSET(T, core, 0);                             \
    BB_OFFSET(T, control, 0x70)
BB_EVENT_REGION_COMMON(SprjEventRegionCircle, 0x80);
BB_EVENT_REGION_COMMON(SprjEventRegionSphere, 0x80);
BB_EVENT_REGION_COMMON(SprjEventRegionCylinder, 0x80);
BB_EVENT_REGION_COMMON(SprjEventRegionRectangle, 0x80);
BB_EVENT_REGION_COMMON(SprjEventRegionBox, 0x90);
#undef BB_EVENT_REGION_COMMON
BB_OFFSET(SprjEventRegionCircle, radius, 0x74);
BB_OFFSET(SprjEventRegionSphere, radius, 0x74);
BB_OFFSET(SprjEventRegionCylinder, radius, 0x74);
BB_OFFSET(SprjEventRegionCylinder, height, 0x78);
BB_OFFSET(SprjEventRegionRectangle, half_width, 0x74);
BB_OFFSET(SprjEventRegionRectangle, half_depth, 0x78);
BB_OFFSET(SprjEventRegionRectangle, state_7c, 0x7c);
BB_OFFSET(SprjEventRegionBox, state_74, 0x74);
BB_OFFSET(SprjEventRegionBox, bounds_min, 0x78);
BB_OFFSET(SprjEventRegionBox, bounds_max, 0x84);
}  // namespace detail::event_region_layout

}  // namespace bb
