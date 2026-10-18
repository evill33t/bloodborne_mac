// SprjFD4ModelItem: the model item CSModelIns owns and registers with a
// display entity.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct LocationUpdateNode;
struct ModelItemBoundsState;
struct SprjFD4ModelDispEntity;

// SprjFD4ModelItem, identified by registration/removal of this pointer under
// its native debug name. CSModelIns allocates 0x540 bytes aligned to 16 and
// owns the result at +0x10.
//
// Attachment installs this item as a display client and its bounds state as a
// source of the display's bounds aggregator; the display pointer is a
// back-reference, not separately retained. Destruction detaches the client,
// clears render resources, disables/unlinks location states, releases them
// with atomic counting and releases the model resource non-atomically. Native
// callbacks/back-pointers require a live item to stay put.
struct alignas(16) SprjFD4ModelItem {
    static constexpr std::size_t SIZE = 0x540;
    static constexpr Rva INSTANCE_VTABLE{0x53a2ac0};
    static constexpr Rva DEBUG_NAME{0x499ed3c};
    static constexpr Rva CONSTRUCTOR_FN{0x1d6e380};
    static constexpr Rva DESTRUCTOR_FN{0x1d6e8d0};
    static constexpr Rva SET_DISPLAY_ENTITY_FN{0x1d6f0f0};
    static constexpr Rva BIND_TRANSFORM_SOURCE_FN{0x1d70940};
    static constexpr Rva CLEAR_RENDER_RESOURCES_FN{0x1d6ef00};
    static constexpr Rva BIND_DEBUG_CONTEXT_FN{0x1d70e30};

    const void* vftable;
    Unknown<0x50>* owned_controller;
    // Inline polymorphic dispatch state initialized through its virtual +0x10
    // with allocator, this item and the owning CSModelIns.
    Unknown<0x30> _dispatch_state;
    void* allocator;
    void* callback_state;
    // Constructor retains at pointee +8 using a non-atomic signed count.
    void* retained_resource;
    Unknown<8> _unk58;
    Unknown<0x48> _state60;
    Unknown<0x50> _statea8;
    std::uint64_t values_f8_bits[5];
    // Initial bits are 1.0, 1.0, 0, 0; exact rendering meaning unresolved.
    std::uint32_t values_120_bits[4];
    std::uint8_t flags_130[4];
    Unknown<4> _unk134;
    std::uint64_t value_138_bits;
    Unknown<0xb0> _render_state_140;
    Unknown<0x50> _render_state_1f0;
    void* render_allocator;
    Unknown<8> _unk248;
    // Constructor clears this exact range; substructure not classified.
    Unknown<0x1c1> _render_state_250;
    Unknown<7> _unk411;
    std::uint64_t value_418_bits;
    std::uint8_t flags_420[3];
    Unknown<5> _unk423;
    std::uint64_t values_428_bits[2];
    Unknown<8> _unk438;
    SprjFD4ModelDispEntity* display_entity;
    std::uint64_t value_448_bits;
    std::uint64_t value_450_bits;
    std::uint8_t flags_458;
    Unknown<7> _unk459;
    // 0x68-byte location object; constructor RVA 0x1d2d370.
    Unknown<0x68>* location_state;
    // Optional location object; its complete extent is unresolved.
    LocationUpdateNode* optional_location_state;
    // 0x60-byte location object; constructor RVA 0x1d394a0.
    Unknown<0x60>* resource_location_state;
    ModelItemBoundsState* bounds_state;
    void* render_registration;
    // Released with atomic reference counting at pointee +8.
    void* retained_render_resource;
    void* state_490;
    std::uint64_t value_498_bits;
    // Native release treats all-one bits as the invalid value.
    std::uint64_t resource_handle;
    void* render_resource;
    std::uint64_t value_4b0_bits;
    void* render_context;
    std::uint32_t values_4c0_bits[2][4];
    std::uint32_t values_4e0_bits[4];
    void* debug_context;
    std::uint8_t flags_4f8;
    Unknown<7> _unk4f9;
    // Native string storage: buffer +0x508, length +0x518, capacity +0x520,
    // allocator +0x528. Capacity >7 selects separately allocated storage.
    Unknown<0x38> _name_storage;
    Unknown<8> _unk538;
};

namespace detail::model_item_layout {
using I = SprjFD4ModelItem;
BB_SIZE(I, SprjFD4ModelItem::SIZE);
static_assert(alignof(I) == 16, "alignof(SprjFD4ModelItem)");
BB_OFFSET(I, owned_controller, 8);
BB_OFFSET(I, _dispatch_state, 0x10);
BB_OFFSET(I, allocator, 0x40);
BB_OFFSET(I, callback_state, 0x48);
BB_OFFSET(I, retained_resource, 0x50);
BB_OFFSET(I, _state60, 0x60);
BB_OFFSET(I, _statea8, 0xa8);
BB_OFFSET(I, values_f8_bits, 0xf8);
BB_OFFSET(I, values_120_bits, 0x120);
BB_OFFSET(I, flags_130, 0x130);
BB_OFFSET(I, _render_state_140, 0x140);
BB_OFFSET(I, _render_state_1f0, 0x1f0);
BB_OFFSET(I, render_allocator, 0x240);
BB_OFFSET(I, _render_state_250, 0x250);
BB_OFFSET(I, value_418_bits, 0x418);
BB_OFFSET(I, flags_420, 0x420);
BB_OFFSET(I, values_428_bits, 0x428);
BB_OFFSET(I, display_entity, 0x440);
BB_OFFSET(I, flags_458, 0x458);
BB_OFFSET(I, location_state, 0x460);
BB_OFFSET(I, optional_location_state, 0x468);
BB_OFFSET(I, resource_location_state, 0x470);
BB_OFFSET(I, bounds_state, 0x478);
BB_OFFSET(I, render_registration, 0x480);
BB_OFFSET(I, retained_render_resource, 0x488);
BB_OFFSET(I, resource_handle, 0x4a0);
BB_OFFSET(I, render_resource, 0x4a8);
BB_OFFSET(I, render_context, 0x4b8);
BB_OFFSET(I, values_4c0_bits, 0x4c0);
BB_OFFSET(I, values_4e0_bits, 0x4e0);
BB_OFFSET(I, debug_context, 0x4f0);
BB_OFFSET(I, flags_4f8, 0x4f8);
BB_OFFSET(I, _name_storage, 0x500);
}  // namespace detail::model_item_layout

}  // namespace bb
