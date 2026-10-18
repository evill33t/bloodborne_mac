// Model-location update graph (descriptive names for verified roles, not
// recovered reflection names).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct SprjFD4ModelDispEntity;
struct SprjFD4ModelItem;
struct LocationUpdateNode;

// Native 0x20-byte intrusive link. A source object's embedded root has
// source_root == self; a detached dependency has a null source_root. A linked
// dependency points to its source's root, whose owner identifies the source.
// Retention applies to the nonempty dependency ring, not one reference per edge.
struct LocationDependencyLink {
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva ATTACH_FN{0x1d3c120};
    static constexpr Rva DETACH_FN{0x1d3c000};

    // Object that must update when its dependency changes.
    LocationUpdateNode* owner;
    LocationDependencyLink* source_root;
    LocationDependencyLink* next;
    LocationDependencyLink* previous;
};

// Common 0x38-byte prefix of location update objects. The reference count and
// dirty word take part in native atomic operations. Native invalidation
// queues the object through SprjFD4Location and marks dependent owners dirty.
// Objects and their embedded links must not move.
struct LocationUpdateNode {
    static constexpr std::size_t PREFIX_SIZE = 0x38;
    static constexpr Rva BASE_VTABLE{0x53a1950};
    static constexpr Rva PROPAGATE_DIRTY_FN{0x1d3c500};

    const void* vftable;
    std::int32_t reference_count;
    // Bit 0 enables updates; bit 1 guards against recursive processing.
    std::uint8_t flags;
    Unknown<3> _unk0d;
    // Changed 0 -> 1 by compare/exchange; cleared after update dispatch.
    std::int32_t dirty;
    Unknown<4> _unk14;
    LocationDependencyLink dependents;
};

struct ModelBoundsDependencyNode;

struct ModelBoundsDependencyList {
    Unknown<8> _unk00;
    ModelBoundsDependencyNode* sentinel;
    std::size_t count;
    void* allocator;
};

struct ModelBoundsDependencyNode {
    ModelBoundsDependencyNode* next;
    ModelBoundsDependencyNode* previous;
    // Owned 0x20-byte allocation; unlink before native destruction/free.
    LocationDependencyLink* dependency;
};

// 0x90-byte bounds aggregator at display entity +0x200 (descriptive). Native
// construction allocates aligned to 16. Dependencies are separately allocated
// links, stored as payloads of the sentinel-based list. Update runs dirty
// enabled sources, combines their bounds, then writes the result into the
// display entity and its CSLod record.
struct alignas(16) ModelDisplayBoundsState {
    static constexpr std::size_t SIZE = 0x90;
    static constexpr Rva INSTANCE_VTABLE{0x53a1710};
    static constexpr Rva CONSTRUCTOR_FN{0x1d39d00};
    static constexpr Rva DESTRUCTOR_FN{0x1d39e90};
    static constexpr Rva ADD_SOURCE_FN{0x1d39fc0};
    static constexpr Rva REMOVE_SOURCE_FN{0x1d3a150};
    static constexpr Rva UPDATE_SOURCES_FN{0x1d3a1f0};
    static constexpr Rva UPDATE_BOUNDS_FN{0x1d3a280};

    LocationUpdateNode update;
    ModelBoundsDependencyList dependencies;
    Unknown<8> _unk58;
    float minimum[4];
    float maximum[4];
    SprjFD4ModelDispEntity* display_entity;
    Unknown<8> _unk88;
};

// 0x170-byte bounds producer owned at SprjFD4ModelItem +0x478 (descriptive).
// Source links feed native transform data into the bounds calculation. The
// display aggregator reads minimum/maximum at +0x80/+0x90.
struct alignas(16) ModelItemBoundsState {
    static constexpr std::size_t SIZE = 0x170;
    static constexpr Rva INSTANCE_VTABLE{0x53a1500};
    static constexpr Rva CONSTRUCTOR_FN{0x1d37040};
    static constexpr Rva DESTRUCTOR_FN{0x1d37240};
    static constexpr Rva UPDATE_BOUNDS_FN{0x1d37400};

    LocationUpdateNode update;
    LocationDependencyLink primary_source;
    LocationDependencyLink secondary_source;
    std::uint8_t use_secondary_source;
    Unknown<7> _unk79;
    float minimum[4];
    float maximum[4];
    // Native helper initialized at +0xa0 and destroyed by RVA 0x21c7ff0.
    Unknown<0xa8> _bounds_work;
    std::uint64_t value_148_bits;
    std::uint64_t value_150_bits;
    SprjFD4ModelItem* model_item;
    // Borrowed from model_item's retained resource +0x308.
    const void* resource_bounds;
    Unknown<8> _unk168;
};

// 0xa0-byte transform source at CSModelIns +0x20 (descriptive), constructed
// inline in the instance constructor with 16-byte alignment. Update requests a
// matrix from source virtual +0x28; copy virtual +0x28 returns this matrix only
// for index zero. Native class name unresolved.
struct alignas(16) ModelTransformState {
    static constexpr std::size_t SIZE = 0xa0;
    static constexpr Rva INSTANCE_VTABLE{0x53a0f10};
    static constexpr Rva UPDATE_TRANSFORM_FN{0x1d2dd80};
    static constexpr Rva COPY_TRANSFORM_FN{0x1d2ddb0};

    LocationUpdateNode update;
    LocationDependencyLink source;
    std::uint32_t value_58;
    Unknown<4> _unk5c;
    float transform[4][4];
};

namespace detail::model_location_state_layout {
BB_SIZE(LocationUpdateNode, LocationUpdateNode::PREFIX_SIZE);
static_assert(alignof(LocationUpdateNode) == 8, "alignof(LocationUpdateNode)");
BB_OFFSET(LocationUpdateNode, reference_count, 8);
BB_OFFSET(LocationUpdateNode, flags, 0xc);
BB_OFFSET(LocationUpdateNode, dirty, 0x10);
BB_OFFSET(LocationUpdateNode, dependents, 0x18);
BB_SIZE(LocationDependencyLink, LocationDependencyLink::SIZE);
BB_OFFSET(LocationDependencyLink, source_root, 8);
BB_OFFSET(LocationDependencyLink, next, 0x10);
BB_OFFSET(LocationDependencyLink, previous, 0x18);
BB_SIZE(ModelDisplayBoundsState, ModelDisplayBoundsState::SIZE);
static_assert(alignof(ModelDisplayBoundsState) == 16, "alignof(ModelDisplayBoundsState)");
BB_OFFSET(ModelDisplayBoundsState, dependencies, 0x38);
BB_OFFSET(ModelDisplayBoundsState, minimum, 0x60);
BB_OFFSET(ModelDisplayBoundsState, maximum, 0x70);
BB_OFFSET(ModelDisplayBoundsState, display_entity, 0x80);
BB_SIZE(ModelBoundsDependencyList, 0x20);
BB_OFFSET(ModelBoundsDependencyList, sentinel, 8);
BB_OFFSET(ModelBoundsDependencyList, count, 0x10);
BB_OFFSET(ModelBoundsDependencyList, allocator, 0x18);
BB_SIZE(ModelBoundsDependencyNode, 0x18);
BB_OFFSET(ModelBoundsDependencyNode, dependency, 0x10);
BB_SIZE(ModelItemBoundsState, ModelItemBoundsState::SIZE);
static_assert(alignof(ModelItemBoundsState) == 16, "alignof(ModelItemBoundsState)");
BB_OFFSET(ModelItemBoundsState, primary_source, 0x38);
BB_OFFSET(ModelItemBoundsState, secondary_source, 0x58);
BB_OFFSET(ModelItemBoundsState, use_secondary_source, 0x78);
BB_OFFSET(ModelItemBoundsState, minimum, 0x80);
BB_OFFSET(ModelItemBoundsState, maximum, 0x90);
BB_OFFSET(ModelItemBoundsState, _bounds_work, 0xa0);
BB_OFFSET(ModelItemBoundsState, value_148_bits, 0x148);
BB_OFFSET(ModelItemBoundsState, model_item, 0x158);
BB_OFFSET(ModelItemBoundsState, resource_bounds, 0x160);
BB_SIZE(ModelTransformState, ModelTransformState::SIZE);
static_assert(alignof(ModelTransformState) == 16, "alignof(ModelTransformState)");
BB_OFFSET(ModelTransformState, source, 0x38);
BB_OFFSET(ModelTransformState, value_58, 0x58);
BB_OFFSET(ModelTransformState, transform, 0x60);
}  // namespace detail::model_location_state_layout

}  // namespace bb
