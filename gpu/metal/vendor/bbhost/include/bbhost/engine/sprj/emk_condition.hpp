// Native EMEVD condition groups, their holder, and three concrete predicates.
// Group and holder names are native debug-key spellings; concrete predicate
// and common-prefix names are descriptive. Pointers stay native-owned; the
// pure helpers neither traverse them nor call native functions.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct SprjEmkConditionGroup;

// One virtual slot is present in the observed group table.
struct SprjEmkConditionGroupVTable {
    const void* update;
};

// Seven slots observed in all three concrete condition tables (raw pointers,
// no callable ABI claimed). Holder/group cleanup calls destroy at +0 and then
// frees through the condition's owning heap.
struct SprjEmkConditionVTable {
    const void* destroy;
    const void* delete_;  // `delete` is a C++ keyword
    const void* update;
    const void* slot_18;
    const void* release_auxiliary;
    const void* attach_debug;
    const void* slot_30;
};

// 0x20-byte holder view at SprjEmkEventCore +0x30. The final two words are
// instruction progress, consumed relative to the holder by query/update. The
// subobject boundary beyond this view is unproven; event +0x50 stays separately
// unclassified.
//
// New groups are prepended to newest; oldest stays fixed until clear. Update
// starts at oldest and follows newer links; lookup/clear start at newest and
// follow older links. main_group aliases group zero (not separately owned).
// Clear destroys every condition and group and nulls all three pointers,
// leaving both progress words unchanged.
struct SprjEmkConditionHolder {
    SprjEmkConditionGroup* newest;
    SprjEmkConditionGroup* oldest;
    SprjEmkConditionGroup* main_group;
    // Initially -1; event update publishes its instruction index here.
    std::int32_t current_instruction;
    // Initially -1; network callbacks and event end update this word.
    std::int32_t remote_instruction;

    static constexpr std::size_t PREFIX_SIZE = 0x20;
    static constexpr Rva DEBUG_NAME_UTF16{0x4946c64};
    static constexpr Rva CLEAR_FN{0x12ebff0};
    // A null condition is ignored. Otherwise finds/creates the signed-byte group
    // and appends the condition. A new group is 0x30 aligned to eight; the
    // append call assumes allocation succeeded.
    static constexpr Rva ADD_CONDITION_FN{0x12ec230};
    // Updates every group's conditions oldest-first, then latches satisfied
    // nonzero group bits into event.state[0] if current > remote progress.
    // Existing bits are not cleared.
    static constexpr Rva UPDATE_FN{0x12ec380};
    // The same latching pass without updating conditions first.
    static constexpr Rva LATCH_GROUP_BITS_FN{0x12ec4a0};
    static constexpr Rva QUERY_GROUP_FN{0x12ec5a0};
    static constexpr Rva QUERY_MAIN_FN{0x12ec660};
    static constexpr Rva COUNT_SATISFIED_FN{0x12ec6f0};
    static constexpr Rva COUNT_CONDITIONS_FN{0x12ec740};
    // Whether earlier conditions permit evaluating a particular node. A missing
    // group/node is not reported as an error.
    static constexpr Rva CHECK_PRECEDING_FN{0x12ec770};

    // The native query's final progress override for an already computed group
    // result: 0/1 for false/true, or 2 for group zero once remote progress has
    // reached current. Queries no nodes.
    constexpr std::uint8_t query_code(std::int8_t group_id, bool group_result) const {
        return (group_id == 0 && current_instruction <= remote_instruction) ? 2 : (group_result ? 1 : 0);
    }
};

// Shared 0x28-byte prefix established by three distinct 0x30 allocations (not
// a claim about the full abstract base). Initialization clears result bit 0
// and override bits 0/1, keeping the other bits; group_id starts zero,
// value_14 starts -1, next/auxiliary start null.
struct SprjEmkConditionCore {
    const SprjEmkConditionVTable* vtable;
    SprjEmkConditionCore* next;
    std::uint8_t result_flags;
    std::int8_t group_id;
    std::uint8_t _unk12[2];
    std::int32_t value_14;
    // Bit 0 enables an override; bit 1 supplies the overridden result.
    std::uint8_t override_flags;
    std::uint8_t _unk19[7];
    // Some concrete destructors release this through RVA 0x1001720; type and
    // producer not established.
    void* auxiliary;

    static constexpr std::size_t PREFIX_SIZE = 0x28;
    static constexpr Rva RELEASE_AUXILIARY_FN{0x12eb960};

    constexpr bool effective_result() const { return result_from_flags(result_flags, override_flags); }
    static constexpr bool result_from_flags(std::uint8_t result_flags, std::uint8_t override_flags) {
        return (override_flags & 1) ? (override_flags & 2) != 0 : (result_flags & 1) != 0;
    }
};

// Full 0x30-byte group allocation, aligned to eight by ADD_CONDITION_FN. Its
// vtable slot zero updates conditions (not a destructor). Conditions are
// appended in insertion order. A negative ID uses OR, nonnegative uses AND; an
// empty group is satisfied either way.
struct SprjEmkConditionGroup {
    const SprjEmkConditionGroupVTable* vtable;
    std::int8_t group_id;
    std::uint8_t _unk09[7];
    SprjEmkConditionCore* conditions;
    SprjEmkConditionGroup* older;
    SprjEmkConditionGroup* newer;
    // Initially zero; no further interpretation.
    std::uint64_t value_28_bits;

    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva DEBUG_NAME_UTF16{0x4946c38};
    static constexpr Rva VTABLE{0x531c970};
    static constexpr Rva CLEAR_CONDITIONS_FN{0x12ebb50};
    // Links at the tail, nulls condition.next and copies group_id into condition
    // +0x11. Debug attachment, if enabled, precedes the ID store.
    static constexpr Rva APPEND_CONDITION_FN{0x12ebbb0};
    static constexpr Rva UPDATE_FN{0x12ebca0};
    static constexpr Rva QUERY_FN{0x12ebcf0};
    static constexpr Rva COUNT_SATISFIED_FN{0x12ebd60};
    static constexpr Rva COUNT_CONDITIONS_FN{0x12ebda0};
    static constexpr Rva CHECK_PRECEDING_FN{0x12ebdc0};

    // Pure evaluation of supplied effective results. Missing groups also yield
    // true in the native holder query.
    static constexpr bool evaluate_results(std::int8_t group_id, const bool* results, std::size_t count) {
        if (count == 0) return true;
        if (group_id < 0) {
            for (std::size_t i = 0; i < count; ++i)
                if (results[i]) return true;
            return false;
        }
        for (std::size_t i = 0; i < count; ++i)
            if (!results[i]) return false;
        return true;
    }

    // Bit latched in event.state[0]; 0 for group zero, which has none. x86
    // BT/SHL use only five shift bits, including for unusual signed-byte IDs;
    // no 1..15 range restriction is imposed.
    static constexpr std::uint32_t state_mask(std::int8_t group_id) {
        if (group_id == 0) return 0;
        const std::int32_t index = group_id > 0 ? std::int32_t{group_id} - 1 : 15 - std::int32_t{group_id};
        return 1u << (static_cast<std::uint32_t>(index) & 31u);
    }
};

// Full 0x30-byte constant-result predicate, allocated aligned to eight by bank 0
// opcode 1. Creation compares two signed i32 arguments once; update copies that
// byte into result bit 0. The destructor is a no-op.
struct SprjEmkConstantCondition {
    SprjEmkConditionCore core;
    std::uint8_t result;
    std::uint8_t _unk29[7];

    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva VTABLE{0x5386210};
    static constexpr Rva PRODUCER_FN{0x17c7160};
    static constexpr Rva UPDATE_FN{0x17a61f0};
    static constexpr Rva DESTRUCTOR_FN{0x17a6210};
    static constexpr Rva DELETE_FN{0x17a6200};

    // Selector mapping verified by SETZ/SETNZ/SETG/SETL/SETGE/SETLE in the
    // producer. Invalid selectors create no condition (returns false, out
    // untouched), although the bank handler still returns success for opcode 1.
    static constexpr bool compare_i32(std::int8_t selector, std::int32_t lhs, std::int32_t rhs, bool* out) {
        switch (selector) {
            case 0: *out = lhs == rhs; return true;
            case 1: *out = lhs != rhs; return true;
            case 2: *out = lhs > rhs; return true;
            case 3: *out = lhs < rhs; return true;
            case 4: *out = lhs >= rhs; return true;
            case 5: *out = lhs <= rhs; return true;
            default: return false;
        }
    }
};

// Full 0x30-byte predicate querying another condition group. Bank 0 opcode 0
// creates mode zero, stores a normalized desired state in +0x29 and +0x2a, sets
// first_update and sign-extends the target group's byte; it calls update once
// before appending to its owning group. Update treats native query results 1
// and 2 as true. Mode zero tests equality with desired_state; modes 1..3 use
// previous_state for transitions (their creation paths are not established).
struct SprjEmkGroupStateCondition {
    SprjEmkConditionCore core;
    std::uint8_t mode;
    std::uint8_t desired_state;
    std::uint8_t previous_state;
    std::uint8_t first_update;
    std::int32_t target_group;

    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva VTABLE{0x532ca70};
    static constexpr Rva PRODUCER_FN{0x17c7160};
    static constexpr Rva UPDATE_FN{0x17a5320};
    static constexpr Rva DESTRUCTOR_FN{0x17a54c0};
    static constexpr Rva DELETE_FN{0x17a5490};
    static constexpr Rva ATTACH_DEBUG_FN{0x17a5830};
};

// Full 0x30-byte predicate created by bank 11 opcodes 0, 1 and 2: the opcode
// becomes selector, the argument at +4 collision_id. Update requires the Lua
// manager and resolves the ID to a MapCollisionEntry via the collision
// manager's signed-ID index and decoded group/entry handle. Selector 0 scans
// character sets for matching collision pointers plus state gates; selector 1's
// helper always returns false; selector 2 checks the local player's +0x288
// collision pointer, a positive character-data word and a state bit.
struct SprjEmkCollisionCondition {
    SprjEmkConditionCore core;
    std::uint8_t selector;
    std::uint8_t _unk29[3];
    std::int32_t collision_id;

    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva VTABLE{0x532d070};
    static constexpr Rva PRODUCER_FN{0x17c2f60};
    static constexpr Rva UPDATE_FN{0x17aee40};
    static constexpr Rva DESTRUCTOR_FN{0x17af0a0};
    static constexpr Rva DELETE_FN{0x17af070};
    static constexpr Rva ATTACH_DEBUG_FN{0x17af340};
    static constexpr Rva RESOLVE_COLLISION_FN{0x13c9fb0};
    static constexpr Rva QUERY_CHARACTER_SETS_FN{0x191b490};
    static constexpr Rva QUERY_SELECTOR_ONE_FN{0x191b620};
};

namespace detail::emk_condition_layout {
// The SprjEmkEventCore offsets are asserted in emk_system.hpp.
BB_SIZE(SprjEmkConditionHolder, SprjEmkConditionHolder::PREFIX_SIZE);
static_assert(alignof(SprjEmkConditionHolder) == 8, "alignof(SprjEmkConditionHolder)");
BB_OFFSET(SprjEmkConditionHolder, newest, 0);
BB_OFFSET(SprjEmkConditionHolder, oldest, 8);
BB_OFFSET(SprjEmkConditionHolder, main_group, 0x10);
BB_OFFSET(SprjEmkConditionHolder, current_instruction, 0x18);
BB_OFFSET(SprjEmkConditionHolder, remote_instruction, 0x1c);
BB_SIZE(SprjEmkConditionGroup, SprjEmkConditionGroup::SIZE);
static_assert(alignof(SprjEmkConditionGroup) == 8, "alignof(SprjEmkConditionGroup)");
BB_OFFSET(SprjEmkConditionGroup, group_id, 8);
BB_OFFSET(SprjEmkConditionGroup, conditions, 0x10);
BB_OFFSET(SprjEmkConditionGroup, older, 0x18);
BB_OFFSET(SprjEmkConditionGroup, newer, 0x20);
BB_OFFSET(SprjEmkConditionGroup, value_28_bits, 0x28);
BB_SIZE(SprjEmkConditionGroupVTable, 8);
BB_SIZE(SprjEmkConditionCore, SprjEmkConditionCore::PREFIX_SIZE);
BB_OFFSET(SprjEmkConditionCore, next, 8);
BB_OFFSET(SprjEmkConditionCore, result_flags, 0x10);
BB_OFFSET(SprjEmkConditionCore, group_id, 0x11);
BB_OFFSET(SprjEmkConditionCore, value_14, 0x14);
BB_OFFSET(SprjEmkConditionCore, override_flags, 0x18);
BB_OFFSET(SprjEmkConditionCore, auxiliary, 0x20);
BB_SIZE(SprjEmkConditionVTable, 0x38);
BB_OFFSET(SprjEmkConditionVTable, update, 0x10);
BB_OFFSET(SprjEmkConditionVTable, attach_debug, 0x28);
BB_SIZE(SprjEmkConstantCondition, SprjEmkConstantCondition::SIZE);
static_assert(alignof(SprjEmkConstantCondition) == 8, "alignof(SprjEmkConstantCondition)");
BB_OFFSET(SprjEmkConstantCondition, result, 0x28);
BB_SIZE(SprjEmkGroupStateCondition, SprjEmkGroupStateCondition::SIZE);
static_assert(alignof(SprjEmkGroupStateCondition) == 8, "alignof(SprjEmkGroupStateCondition)");
BB_OFFSET(SprjEmkGroupStateCondition, mode, 0x28);
BB_OFFSET(SprjEmkGroupStateCondition, desired_state, 0x29);
BB_OFFSET(SprjEmkGroupStateCondition, previous_state, 0x2a);
BB_OFFSET(SprjEmkGroupStateCondition, first_update, 0x2b);
BB_OFFSET(SprjEmkGroupStateCondition, target_group, 0x2c);
BB_SIZE(SprjEmkCollisionCondition, SprjEmkCollisionCondition::SIZE);
static_assert(alignof(SprjEmkCollisionCondition) == 8, "alignof(SprjEmkCollisionCondition)");
BB_OFFSET(SprjEmkCollisionCondition, selector, 0x28);
BB_OFFSET(SprjEmkCollisionCondition, collision_id, 0x2c);
static_assert(SprjEmkConditionGroup::state_mask(-128) == 0x8000 && SprjEmkConditionGroup::state_mask(127) == 0x40000000 &&
                  SprjEmkConditionGroup::state_mask(-16) == 0x80000000u && SprjEmkConditionGroup::state_mask(17) == 0x10000,
              "SprjEmkConditionGroup::state_mask");
}  // namespace detail::emk_condition_layout

}  // namespace bb
