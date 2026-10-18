// Bloodborne 1.09 EnterleaveDirectorImp layout and runtime metadata.
//
// The binary spelling is EnterleaveDirectorImp; EnterLeaveDirectorImpl is only
// the Remodel file grouping. STEP_Update owns child removal from the intrusive
// list and does not itself own session membership. The director retains active
// CSEnterleaveDirectionStep presentation children and removes them after the
// native task reports completion. Room membership, signaling, world loading,
// and remote-character allocation are owned by other systems.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct CSEnterleaveDirectionStep;

inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_SIZE = 0x108;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_BASE_SIZE = 0xd0;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_LIST_OWNER_OFFSET = 0xd0;
// Corrected meaning of the member previously called list owner.
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_TASK_HOLDER_OFFSET = 0xd0;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_CHILD_LIST_OFFSET = 0xe8;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_CHILD_SENTINEL_OFFSET = 0xf0;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_CHILD_COUNT_OFFSET = 0xf8;
inline constexpr std::size_t ENTERLEAVE_DIRECTOR_IMP_CHILD_ALLOCATOR_OFFSET = 0x100;

inline constexpr Rva ENTERLEAVE_DIRECTOR_SINGLETON_PTR{0x553e8b0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_CONSTRUCTOR_FN{0x196f9f0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_VTABLE{0x53315f0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_SIZE_GETTER_FN{0x19727f0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_DESTRUCTOR_FN{0x196fdb0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_DISPATCH_FN{0x1971f30};
inline constexpr Rva ENTERLEAVE_DIRECTOR_IMP_IS_FINISHED_FN{0x1972180};
inline constexpr Rva ENTERLEAVE_DIRECTOR_CHILD_APPEND_FN{0x1973210};
inline constexpr Rva ENTERLEAVE_DIRECTOR_STARTUP_OWNER_FN{0x156c6b0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_CREATE_ENTER_BY_CHR_HANDLE_FN{0x19701f0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_FIND_OR_CREATE_BY_OBJECT_REF_FN{0x19700a0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_FIND_OR_CREATE_BY_CHR_HANDLE_FN{0x1970310};
inline constexpr Rva ENTERLEAVE_DIRECTOR_CREATE_ENTER_FOR_SELF_BY_OBJECT_REF_FN{0x1970440};
inline constexpr Rva ENTERLEAVE_DIRECTOR_CREATE_ENTER_FOR_OTHERS_BY_OBJECT_REF_FN{0x1970470};
inline constexpr Rva ENTERLEAVE_DIRECTOR_CREATE_LEAVE_BY_CHR_HANDLE_FN{0x19704b0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_MARK_CHILDREN_FOR_WORLD_TRANSITION_FN{0x1970660};
inline constexpr Rva ENTERLEAVE_DIRECTOR_STEP_INIT_FN{0x19706d0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_STEP_UPDATE_FN{0x19707d0};
inline constexpr Rva ENTERLEAVE_DIRECTOR_STEP_FINISH_FN{0x1970880};
inline constexpr Rva ENTERLEAVE_DIRECTOR_REGISTER_RUNTIME_CLASS_AND_STEPS_FN{0x19709f0};

// Director step indices from the registered three-entry step table.
enum class EnterleaveDirectorStepIndex : std::int32_t {
    Init = 0,
    Update = 1,  // "STEP_Update"
    Finish = 2,
};

// 0x28-byte intrusive-list node used for one child task. The flattened payload
// at +0x10 is a SprjTaskHolder; task stays at +0x18. The sentinel initializes
// only its links; its payload must not be read as a live task holder.
struct EnterleaveDirectorChildNode {
    EnterleaveDirectorChildNode* next;
    EnterleaveDirectorChildNode* previous;
    void* _payload_vtable;
    CSEnterleaveDirectionStep* task;
    std::uint8_t payload_state_raw;
    Unknown<7> _pad21;
};

// Game-allocator-backed child list beginning at director offset 0xe8.
struct EnterleaveDirectorChildList {
    std::uintptr_t _list_storage;
    EnterleaveDirectorChildNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Singleton task that retains active arrival/departure presentation children.
// Startup RVA 0x156c6b0 allocates 0x108 aligned to eight, publishes the
// singleton, and registers self through task_holder in GameFlowStep (7). Both
// find/create paths schedule children in the same group. Update only checks
// completion and removes nodes; it does not dispatch child callbacks.
//
// Update releases a finished/null child's holder before unlinking, destroying
// its payload, and freeing the node. Destruction unregisters all child holders
// and the self holder, frees the list/sentinel, and tears down the base. A live
// object contains a self pointer and must not be moved or copied.
struct EnterleaveDirectorImp {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = ENTERLEAVE_DIRECTOR_IMP_RUNTIME_CLASS;
    // STEP_TEMPLATE: 3 steps
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = ENTERLEAVE_DIRECTOR_IMP_TEMPLATE;
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::GameFlowStep;

    SprjStepTaskD0 step_task;
    SprjTaskHolder<EnterleaveDirectorImp> task_holder;
    EnterleaveDirectorChildList children;

    static bool step_known(std::int32_t raw) { return raw >= 0 && raw <= 2; }
    EnterleaveDirectorStepIndex current_step() const {
        return static_cast<EnterleaveDirectorStepIndex>(step_task.current_step);
    }
    EnterleaveDirectorStepIndex requested_step() const {
        return static_cast<EnterleaveDirectorStepIndex>(step_task.requested_step);
    }
};

namespace detail::enterleave_director_layout {
using T = EnterleaveDirectorImp;
BB_SIZE(EnterleaveDirectorChildNode, 0x28);
static_assert(alignof(T) == 8, "alignof(EnterleaveDirectorImp)");
BB_OFFSET(T, step_task, 0);
BB_OFFSET(EnterleaveDirectorChildNode, _payload_vtable, 0x10);
BB_OFFSET(EnterleaveDirectorChildNode, task, 0x18);
BB_OFFSET(EnterleaveDirectorChildNode, payload_state_raw, 0x20);
BB_SIZE(EnterleaveDirectorChildList, 0x20);
BB_SIZE(T, ENTERLEAVE_DIRECTOR_IMP_SIZE);
BB_SIZE(SprjStepTaskD0, ENTERLEAVE_DIRECTOR_IMP_BASE_SIZE);
BB_OFFSET(T, task_holder, ENTERLEAVE_DIRECTOR_IMP_LIST_OWNER_OFFSET);
BB_OFFSET(T, task_holder, ENTERLEAVE_DIRECTOR_IMP_TASK_HOLDER_OFFSET);
BB_OFFSET(T, children, ENTERLEAVE_DIRECTOR_IMP_CHILD_LIST_OFFSET);
BB_OFFSET(EnterleaveDirectorChildList, sentinel, 0x08);
BB_OFFSET(EnterleaveDirectorChildList, count, 0x10);
BB_OFFSET(EnterleaveDirectorChildList, allocator, 0x18);
static_assert(offsetof(T, children) + offsetof(EnterleaveDirectorChildList, sentinel) ==
                  ENTERLEAVE_DIRECTOR_IMP_CHILD_SENTINEL_OFFSET,
              "children.sentinel");
static_assert(offsetof(T, children) + offsetof(EnterleaveDirectorChildList, count) ==
                  ENTERLEAVE_DIRECTOR_IMP_CHILD_COUNT_OFFSET,
              "children.count");
static_assert(offsetof(T, children) + offsetof(EnterleaveDirectorChildList, allocator) ==
                  ENTERLEAVE_DIRECTOR_IMP_CHILD_ALLOCATOR_OFFSET,
              "children.allocator");
}  // namespace detail::enterleave_director_layout

}  // namespace bb
