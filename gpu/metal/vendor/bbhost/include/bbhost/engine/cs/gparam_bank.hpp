// Graphics-parameter bank, retained instances, and native refresh lifecycle.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct GparamBankIns;

// Native 0x18-byte list node; each real node retains its instance atomically.
// Replacing an entry matches parameter_id, detaches the previous instance,
// activates the new one, then releases/retains the stored reference. The same
// pointer is a no-op.
struct GparamBankInstanceNode {
    GparamBankInstanceNode* next;
    GparamBankInstanceNode* previous;
    GparamBankIns* instance;
};

// Descriptive 0x20-byte list header. The leading eight bytes are unresolved.
// Empty sentinel links point to itself; its payload is not an instance.
struct GparamBankInstanceList {
    Unknown<8> _unk00;
    GparamBankInstanceNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Concrete 0x80-byte bank allocated with alignment eight by RVA 0x205fd70. The
// reflected size method at RVA 0x1d4f1e0 agrees. Singleton assertions call this
// object CSGparamBank; no separate base layout or reflected base link is
// established by that spelling.
//
// The bank retains one GparamBankIns per parameter ID in a circular list.
// Changing a selector schedules its inline callback in ResStep (group 3). That
// callback refreshes registered instances, marks downstream rendering state
// dirty when the list is nonempty, and writes callback.value_18 = 1.
//
// Native destruction removes debug bindings, releases the retained instances,
// frees list nodes/sentinel, and unregisters the callback.
struct CSGparamBankImp {
    // RUNTIME_CLASS: no base
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_GPARAM_BANK_IMP_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::size_t SLOT_COUNT = 5;
    static constexpr Rva SINGLETON_PTR = CS_GPARAM_BANK_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4935187};
    static constexpr Rva VTABLE{0x534cdb0};
    static constexpr Rva CALLBACK_VTABLE{0x53a2300};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::ResStep;
    static constexpr Rva CONSTRUCTOR_FN{0x1d4de80};
    static constexpr Rva DESTRUCTOR_FN{0x1d4e2c0};
    static constexpr Rva CREATE_SINGLETON_FN{0x205fd70};
    static constexpr Rva DESTROY_SINGLETON_FN{0x205fde0};
    static constexpr Rva REFRESH_INSTANCES_FN{0x1d4e230};
    // Out-of-range indices and unchanged values do nothing.
    static constexpr Rva SET_SELECTOR_FN{0x1d4e440};
    // Returns -1 for an index outside the five slots.
    static constexpr Rva GET_SELECTOR_FN{0x1d4e470};
    static constexpr Rva INSERT_OR_REPLACE_INSTANCE_FN{0x1d4e490};
    // Matches the argument's ID, detaches that argument, then releases/removes
    // the matching stored entry. Do not infer an identity check on the argument.
    static constexpr Rva REMOVE_INSTANCE_FN{0x1d4e6f0};

    const void* vftable;
    // Owner is this bank; callback is REFRESH_INSTANCES_FN, adjustment zero.
    SprjCallbackTask38 refresh_task;
    // Constructor defaults [0, -1, -1, -1, -1]. Slot meanings unresolved.
    std::int32_t selectors[5];
    Unknown<4> _unk54;
    GparamBankInstanceList instances;
    // GPARAM BANK debug-menu context, initially null when debug UI is absent.
    void* debug_context;
};

// Native debug identity registered for this pointer by RVA 0x1d4f1f0.
// Allocation in RVA 0x1d01270 proves size 0x70 and alignment eight. No separate
// DLRuntimeClass registration is claimed.
//
// START_LOAD_FN obtains a SprjFile handle. TRY_REGISTER_FN returns success if
// already registered; otherwise it inserts into the bank when the handle is
// absent or its +0x78 state is 4. REFRESH_RESOURCES_FN operates only while
// registered. Category-4 instances use the bank's five selectors; other
// categories use selector zero per slot.
//
// Detach removes render bindings and clears slots/registration. Destruction
// releases the file handle, passes remaining resource pointers to repository
// release (RVA 0xfe6ae0), destroys/frees resources when that helper returns
// true, and unregisters the debug context.
struct GparamBankIns {
    static constexpr std::size_t SIZE = 0x70;
    static constexpr Rva NAME_STRING{0x499e85a};
    static constexpr Rva VTABLE{0x534ce40};
    static constexpr Rva CONSTRUCTOR_FN{0x1d4f1f0};
    static constexpr Rva DESTRUCTOR_FN{0x1d4f770};
    static constexpr Rva START_LOAD_FN{0x1d4fba0};
    static constexpr Rva TRY_REGISTER_FN{0x1d506c0};
    static constexpr Rva REFRESH_RESOURCES_FN{0x1d4fff0};
    static constexpr Rva DETACH_RESOURCES_FN{0x1d4f620};

    const void* vftable;
    // Native DLReferenceCountObject counter; atomic native updates, initial 0.
    std::int32_t reference_count;
    // Bank insertion/removal matches this ID, not pointer identity alone.
    std::uint32_t parameter_id;
    // Used with selector * 100 when composing category-4 resource IDs.
    std::uint32_t parameter_suffix;
    // Constructor stores whether the native ID category equals 4.
    std::uint8_t uses_bank_selectors;
    Unknown<3> _unk15;
    // Initially all -1. A selector can be cached even if resource lookup fails.
    std::int32_t resolved_selectors[5];
    Unknown<4> _unk2c;
    // Repository lookup results; native render bind/unbind consumes +0x70.
    // Lookup itself does not increment a reference count.
    void* resources[5];
    // Opaque native SprjFile handle/pointer, not an image-relative address.
    std::uintptr_t file_handle;
    std::uint8_t registered;
    Unknown<7> _unk61;
    void* debug_context;
};

namespace detail::gparam_bank_layout {
BB_SIZE(CSGparamBankImp, CSGparamBankImp::SIZE);
static_assert(alignof(CSGparamBankImp) == 8, "alignof(CSGparamBankImp)");
BB_OFFSET(CSGparamBankImp, vftable, 0);
BB_OFFSET(CSGparamBankImp, refresh_task, 0x08);
BB_OFFSET(CSGparamBankImp, selectors, 0x40);
BB_OFFSET(CSGparamBankImp, instances, 0x58);
BB_OFFSET(CSGparamBankImp, debug_context, 0x78);
static_assert(static_cast<std::uint32_t>(CSGparamBankImp::TASK_GROUP) == 3, "CSGparamBankImp::TASK_GROUP");
BB_SIZE(GparamBankInstanceList, 0x20);
static_assert(alignof(GparamBankInstanceList) == 8, "alignof(GparamBankInstanceList)");
BB_OFFSET(GparamBankInstanceList, sentinel, 0x08);
BB_OFFSET(GparamBankInstanceList, count, 0x10);
BB_OFFSET(GparamBankInstanceList, allocator, 0x18);
BB_SIZE(GparamBankInstanceNode, 0x18);
static_assert(alignof(GparamBankInstanceNode) == 8, "alignof(GparamBankInstanceNode)");
BB_OFFSET(GparamBankInstanceNode, next, 0x00);
BB_OFFSET(GparamBankInstanceNode, previous, 0x08);
BB_OFFSET(GparamBankInstanceNode, instance, 0x10);
BB_SIZE(GparamBankIns, GparamBankIns::SIZE);
static_assert(alignof(GparamBankIns) == 8, "alignof(GparamBankIns)");
BB_OFFSET(GparamBankIns, vftable, 0x00);
BB_OFFSET(GparamBankIns, reference_count, 0x08);
BB_OFFSET(GparamBankIns, parameter_id, 0x0c);
BB_OFFSET(GparamBankIns, parameter_suffix, 0x10);
BB_OFFSET(GparamBankIns, uses_bank_selectors, 0x14);
BB_OFFSET(GparamBankIns, resolved_selectors, 0x18);
BB_OFFSET(GparamBankIns, resources, 0x30);
BB_OFFSET(GparamBankIns, file_handle, 0x58);
BB_OFFSET(GparamBankIns, registered, 0x60);
BB_OFFSET(GparamBankIns, debug_context, 0x68);
static_assert(CSGparamBankImp::SINGLETON_PTR.bn() == 0x05940098, "CSGparamBankImp::SINGLETON_PTR");
}  // namespace detail::gparam_bank_layout

}  // namespace bb
