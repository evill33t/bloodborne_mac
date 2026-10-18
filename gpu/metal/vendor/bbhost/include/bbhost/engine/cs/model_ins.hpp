// CSModelIns: model instance and the shared transition storage it embeds.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct SprjFD4ModelItem;
struct SprjFD4ModelDispEntity;
struct ModelTransformState;

// Descriptive 0x20-byte transition record. Endpoint words preserve raw bits;
// their material/rendering meaning has not been recovered. Native update
// subtracts task delta / duration from positive remaining_weight, provided
// duration is positive. At completion it copies primary -> secondary and
// clears remaining_weight and duration. Constructors initialize both pairs
// from one native global and zero the two floats.
struct ModelTransitionRecord {
    const void* vftable;
    std::uint32_t primary_pair_bits[2];
    std::uint32_t secondary_pair_bits[2];
    float remaining_weight;
    float duration;

    static constexpr Rva INSTANCE_VTABLE{0x534cf50};
};

// Descriptive shared 0xa8-byte transition set; native class name unresolved.
// Found at CSModelIns +0x28 and SprjAsmModel +0xc8 with identical vtables.
struct ModelTransitionSet {
    const void* vftable;
    ModelTransitionRecord records[5];

    static constexpr Rva INSTANCE_VTABLE{0x534d230};
    static constexpr Rva UPDATE_FN{0x1d6d880};
};

// Native CSModelIns, identified by constructor/debug registration rather than
// DLRuntimeClass reflection. The menu renderer allocates 0x120 bytes aligned
// to eight and stores this object at +0x670. The instance owns a 0x540-byte
// model child, retains a 0x250-byte display entity and a separate 0xa0-byte
// counted state object.
//
// Task owner points back to this object, so a live instance must not move.
// Cleanup releases cloth and the model child, unregisters the task, and
// releases the two counted objects using their different native protocols.
struct CSModelIns {
    const void* vftable;
    // Bit zero enables transition advancement in the task callback.
    std::uint8_t flags;
    Unknown<7> _unk09;
    SprjFD4ModelItem* owned_model;
    // Native debug label SprjFD4ModelDispEntity; non-atomic count at +0x08.
    SprjFD4ModelDispEntity* display_entity;
    // Uses atomic reference counting at pointee +0x08.
    ModelTransformState* retained_state;
    ModelTransitionSet transitions;
    SprjCallbackTask38 callback_task;
    // Native cleanup routes through SprjCloth, then clears this pointer.
    void* cloth;
    std::uint8_t flag_110;
    Unknown<7> _unk111;
    void* debug_context;

    static constexpr std::size_t SIZE = 0x120;
    static constexpr Rva INSTANCE_VTABLE{0x539cf90};
    static constexpr Rva DEBUG_NAME{0x4999f04};
    static constexpr Rva CONSTRUCTOR_FN{0x1ca16c0};
    static constexpr Rva DESTRUCTOR_FN{0x1ca2230};
    static constexpr Rva UPDATE_FN{0x1ca1cb0};
    static constexpr Rva BIND_DEBUG_CONTEXT_FN{0x1ca1f00};
    static constexpr Rva CLEAR_CLOTH_FN{0x1ca2680};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::DrawParamUpdate;
};

namespace detail::model_ins_layout {
BB_SIZE(CSModelIns, CSModelIns::SIZE);
static_assert(alignof(CSModelIns) == 8, "alignof(CSModelIns)");
BB_OFFSET(CSModelIns, flags, 8);
BB_OFFSET(CSModelIns, owned_model, 0x10);
BB_OFFSET(CSModelIns, display_entity, 0x18);
BB_OFFSET(CSModelIns, retained_state, 0x20);
BB_OFFSET(CSModelIns, transitions, 0x28);
BB_OFFSET(CSModelIns, callback_task, 0xd0);
BB_OFFSET(CSModelIns, callback_task.owner, 0xf0);
BB_OFFSET(CSModelIns, cloth, 0x108);
BB_OFFSET(CSModelIns, flag_110, 0x110);
BB_OFFSET(CSModelIns, debug_context, 0x118);
static_assert(static_cast<std::uint32_t>(CSModelIns::TASK_GROUP) == 0x31, "CSModelIns::TASK_GROUP");
BB_SIZE(ModelTransitionSet, 0xa8);
BB_OFFSET(ModelTransitionSet, records, 8);
BB_SIZE(ModelTransitionRecord, 0x20);
BB_OFFSET(ModelTransitionRecord, primary_pair_bits, 8);
BB_OFFSET(ModelTransitionRecord, secondary_pair_bits, 0x10);
BB_OFFSET(ModelTransitionRecord, remaining_weight, 0x18);
BB_OFFSET(ModelTransitionRecord, duration, 0x1c);
// The shared update reads record 4 remaining_weight at CSModelIns +0xc8.
BB_OFFSET(CSModelIns, transitions.records[4].remaining_weight, 0xc8);
}  // namespace detail::model_ins_layout

}  // namespace bb
