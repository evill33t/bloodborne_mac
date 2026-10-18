// SprjFD4ModelDispEntity: the display entity CSModelIns retains and rendering
// clients consume.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct ModelDisplayBoundsState;
struct CSLodRecord;
struct ModelDisplayClientNode;

// Endpoint/weight view (descriptive). Unlike ModelTransitionRecord it has no
// vtable or duration, and its extent is 0x14, not 0x18: for transition_108
// the next rendering field begins immediately at +0x11c.
struct ModelDisplayTransition {
    std::uint32_t primary_pair_bits[2];
    std::uint32_t secondary_pair_bits[2];
    float remaining_weight;
};

// Native list (descriptive). The destructor frees nodes and sentinel; it does
// not destroy their client payloads.
struct ModelDisplayClientList {
    Unknown<8> _unk00;
    ModelDisplayClientNode* sentinel;
    std::size_t count;
    void* allocator;
};

struct ModelDisplayClientNode {
    ModelDisplayClientNode* next;
    ModelDisplayClientNode* previous;
    void* client;
};

// Native SprjFD4ModelDispEntity, identified by debug registration/removal of
// this pointer. CSModelIns allocates 0x250 bytes aligned to 16. Its own
// reference count is non-atomic; the retained state at +0x200 counts
// atomically.
//
// Render dispatch gates on flags +0x210 and three 128-bit masks, then visits
// the client list and calls each non-null client's virtual +0x60 with this
// entity. Destruction returns the LOD record to its pool (or frees a fallback
// allocation), releases retained state, frees list nodes/sentinel, removes
// debug registration and detaches from its render owner.
struct alignas(16) SprjFD4ModelDispEntity {
    static constexpr std::size_t SIZE = 0x250;
    static constexpr Rva INSTANCE_VTABLE{0x53a0820};
    static constexpr Rva SECONDARY_VTABLE{0x53a0888};
    static constexpr Rva DEBUG_NAME{0x499dd44};
    static constexpr Rva CONSTRUCTOR_FN{0x1d20b40};
    static constexpr Rva DESTRUCTOR_FN{0x1d21010};
    static constexpr Rva DISPATCH_CLIENTS_FN{0x1d213b0};
    static constexpr Rva TEST_MASKS_FLAGS_01_FN{0x1d21c80};
    static constexpr Rva TEST_MASKS_FLAGS_03_FN{0x1d21b70};
    static constexpr Rva GET_RENDER_MASKS_FN{0x1d21d90};

    const void* vftable;
    std::int32_t reference_count;
    Unknown<4> _unk0c;
    const void* secondary_vftable;
    void* render_owner;
    // Native setters notify render_owner when these words change.
    std::uint32_t owner_flags[2];
    std::uint64_t value_28_bits;
    std::uint64_t value_30_bits;
    Unknown<8> _unk38;
    // Written by ModelDisplayBoundsState after combining client bounds.
    float minimum[4];
    float maximum[4];
    std::uint64_t value_60_bits;
    // Optional recipient of minimum/maximum at pointee +0x20/+0x30.
    void* bounds_target;
    // Optional mask source: mask A at pointee +8, mask B at +0x18.
    void* render_context;
    std::uint32_t configuration;
    std::int8_t lod_category;
    Unknown<3> _unk7d;
    ModelDisplayClientList clients;
    Unknown<0x68> _render_state_a0;
    // Receives CSModelIns transition record 4.
    ModelDisplayTransition transition_108;
    Unknown<0x8c> _render_state_11c;
    // transition_1a8/1c0/1d8 receive CSModelIns transition records 0, 1, 2.
    ModelDisplayTransition transition_1a8;
    Unknown<4> _unk1bc;
    ModelDisplayTransition transition_1c0;
    Unknown<4> _unk1d4;
    ModelDisplayTransition transition_1d8;
    std::uint32_t value_1ec_bits;
    std::uint32_t values_1f0_bits[2];
    std::uint32_t value_1f8_bits;
    Unknown<4> _unk1fc;
    // Bounds aggregator, allocated 0x90 with a back-pointer to this entity.
    ModelDisplayBoundsState* retained_state;
    // CSLod record, 0x90 stride, from a pool or the heap.
    CSLodRecord* lod_record;
    std::uint8_t render_flags;
    Unknown<3> _unk211;
    // Compared with the render manager's independent category mask.
    std::uint32_t category_mask[4];
    // Compared with render_context masks, or render-manager defaults.
    std::uint32_t render_masks[2][4];
    Unknown<4> _unk244;
    std::uint64_t value_248_bits;
};

namespace detail::model_disp_entity_layout {
using E = SprjFD4ModelDispEntity;
BB_SIZE(E, SprjFD4ModelDispEntity::SIZE);
static_assert(alignof(E) == 16, "alignof(SprjFD4ModelDispEntity)");
BB_OFFSET(E, reference_count, 8);
BB_OFFSET(E, secondary_vftable, 0x10);
BB_OFFSET(E, render_owner, 0x18);
BB_OFFSET(E, owner_flags, 0x20);
BB_OFFSET(E, minimum, 0x40);
BB_OFFSET(E, maximum, 0x50);
BB_OFFSET(E, bounds_target, 0x68);
BB_OFFSET(E, render_context, 0x70);
BB_OFFSET(E, configuration, 0x78);
BB_OFFSET(E, lod_category, 0x7c);
BB_OFFSET(E, clients, 0x80);
BB_OFFSET(E, transition_108, 0x108);
BB_OFFSET(E, _render_state_11c, 0x11c);
BB_OFFSET(E, transition_1a8, 0x1a8);
BB_OFFSET(E, transition_1c0, 0x1c0);
BB_OFFSET(E, transition_1d8, 0x1d8);
BB_OFFSET(E, value_1ec_bits, 0x1ec);
BB_OFFSET(E, retained_state, 0x200);
BB_OFFSET(E, lod_record, 0x208);
BB_OFFSET(E, render_flags, 0x210);
BB_OFFSET(E, category_mask, 0x214);
BB_OFFSET(E, render_masks, 0x224);
BB_OFFSET(E, value_248_bits, 0x248);
BB_SIZE(ModelDisplayTransition, 0x14);
static_assert(alignof(ModelDisplayTransition) == 4, "alignof(ModelDisplayTransition)");
BB_OFFSET(ModelDisplayTransition, secondary_pair_bits, 8);
BB_OFFSET(ModelDisplayTransition, remaining_weight, 0x10);
BB_SIZE(ModelDisplayClientList, 0x20);
BB_OFFSET(ModelDisplayClientList, sentinel, 8);
BB_OFFSET(ModelDisplayClientList, count, 0x10);
BB_OFFSET(ModelDisplayClientList, allocator, 0x18);
BB_SIZE(ModelDisplayClientNode, 0x18);
BB_OFFSET(ModelDisplayClientNode, previous, 8);
BB_OFFSET(ModelDisplayClientNode, client, 0x10);
}  // namespace detail::model_disp_entity_layout

}  // namespace bb
