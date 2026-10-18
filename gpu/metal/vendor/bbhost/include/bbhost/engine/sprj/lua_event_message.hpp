// Native Lua callback-registration message map (descriptive names from
// constructor/consumer evidence, not recovered class spellings).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct SprjLuaEventRegistrationNode;

// Four-word registration key and incoming-message prefix.
struct SprjLuaEventMessageKey {
    std::uint32_t message_id;
    std::int32_t parameters[3];

    // Native active-registration comparison in RVA 0x139a9d0. For the special
    // IDs the stored registration must hold zero in the omitted positions
    // (stored values are not wildcards); incoming values there are ignored.
    constexpr bool matches_active(const SprjLuaEventMessageKey& incoming) const {
        if (message_id != incoming.message_id || parameters[0] != incoming.parameters[0]) return false;
        switch (incoming.message_id) {
            case 32: case 33: case 43: case 54:
                return parameters[1] == 0 && parameters[2] == 0;
            case 12: case 57:
                return parameters[1] == incoming.parameters[1] && parameters[2] == 0;
            default:
                return parameters[1] == incoming.parameters[1] && parameters[2] == incoming.parameters[2];
        }
    }
    // The consumed-list duplicate gate always compares all four words exactly.
    constexpr bool matches_consumed(const SprjLuaEventMessageKey& incoming) const {
        return message_id == incoming.message_id && parameters[0] == incoming.parameters[0] &&
               parameters[1] == incoming.parameters[1] && parameters[2] == incoming.parameters[2];
    }
};

// Embedded 0x30-byte narrow string used for a callback name. Capacity <=15
// selects inline bytes; larger selects the pointer in storage. Length/capacity
// are byte counts. The first word is not a vtable claim. The native destructor
// frees heap storage through allocator, ignoring the registration's +0x48 byte.
struct SprjLuaEventCallbackName {
    std::uint64_t _unk00;
    std::uint8_t storage[16];
    std::uint64_t length;
    std::uint64_t capacity;
    void* allocator;
};

// Native disposition of an already-matched registration. A match does not
// guarantee a script function was found or invoked successfully.
enum class SprjLuaRegistrationDisposition {
    RemainActive,
    Free,
    RememberConsumed,
};

// Owned 0x50-byte registration allocated (8-byte aligned) by RVA 0x139a790 and
// the inlined event-registration producers.
struct SprjLuaEventRegistration {
    static constexpr std::size_t SIZE = 0x50;
    static constexpr Rva NAME_CONSTRUCTOR_FN{0x2974370};

    SprjLuaEventMessageKey key;
    // Debug display calls this IsDelete. Nonzero retires a matched record.
    std::uint8_t consume_after_match;
    // Debug display calls this IsSend. Copied to context +0x8b after matching.
    std::uint8_t send_after_match;
    // Nonzero frees a consumed registration instead of retaining its key.
    // Producers copy context +0x8c here.
    std::uint8_t discard_consumed;
    // Marked by first-parameter value; later swept from the active list.
    // Dispatch itself does not check this byte.
    std::uint8_t deletion_requested;
    Unknown<4> _unk14;
    SprjLuaEventCallbackName callback_name;
    // Initialized zero after callback-name construction; role unclassified.
    std::uint8_t state_48;
    Unknown<7> _unk49;

    // Post-match retirement branch. The numeric range applies to the incoming
    // first parameter, not the message ID.
    constexpr SprjLuaRegistrationDisposition disposition_after_match(std::int32_t incoming_parameter_1) const {
        if (consume_after_match == 0) return SprjLuaRegistrationDisposition::RemainActive;
        if (static_cast<std::uint32_t>(incoming_parameter_1) - 4000u < 96u || discard_consumed != 0)
            return SprjLuaRegistrationDisposition::Free;
        return SprjLuaRegistrationDisposition::RememberConsumed;
    }
};

// Native list header; the allocator owns nodes, each payload's owning heap is
// found separately during destruction. The first word is opaque.
struct SprjLuaEventRegistrationList {
    std::uint64_t _unk00;
    SprjLuaEventRegistrationNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// Sentinel payload is not initialized by the constructor.
struct SprjLuaEventRegistrationNode {
    SprjLuaEventRegistrationNode* next;
    SprjLuaEventRegistrationNode* previous;
    SprjLuaEventRegistration* registration;
};

// Native message-map subobject at context +0x38 (0x50 bytes). Constructor
// creates two separate 0x18 sentinels; destructor frees records, their heap
// callback names, list nodes and both sentinels.
//
// Dispatch first rejects an exact key already in consumed (returns -1).
// Otherwise it scans active registrations with matches_active, resolves each
// callback through script_type and its parent chain, and counts each match even
// if the callback is missing. Each match overwrites context.send_after_dispatch
// with that registration's byte, then applies disposition_after_match.
// RememberConsumed moves the payload to a new consumed-list node and frees the
// old active node. Active registration creation has no duplicate-key check.
struct SprjLuaEventMessageMap {
    static constexpr std::size_t SIZE = 0x50;
    static constexpr Rva VTABLE{0x5320230};
    static constexpr Rva CONSTRUCTOR_FN{0x139a300};
    static constexpr Rva DESTRUCTOR_FN{0x139a560};
    static constexpr Rva REGISTER_FN{0x139a790};
    // Removes all active records with the exact four-word key; does not compare
    // callback names or remove matching consumed records.
    static constexpr Rva REMOVE_KEY_FN{0x139a870};
    // Marks active records whose first parameter matches; returns zero.
    static constexpr Rva REQUEST_DELETE_PARAMETER_FN{0x139a9a0};
    static constexpr Rva DISPATCH_FN{0x139a9d0};
    // Destroys/frees marked active records and returns the number removed.
    static constexpr Rva SWEEP_DELETED_FN{0x139af90};
    // Returns null for a negative or out-of-range active-list index.
    static constexpr Rva GET_ACTIVE_BY_INDEX_FN{0x139b090};
    // Takes the list header at map +0x08/+0x28, not the map itself.
    static constexpr Rva APPEND_FN{0x139b2a0};

    const void* vtable;
    SprjLuaEventRegistrationList active;
    SprjLuaEventRegistrationList consumed;
    // Registry entry looked up by the SprjLuaEventScriptImitation string.
    void* script_type;
};

namespace detail::lua_event_message_layout {
BB_SIZE(SprjLuaEventMessageKey, 0x10);
BB_OFFSET(SprjLuaEventMessageKey, message_id, 0);
BB_OFFSET(SprjLuaEventMessageKey, parameters, 4);
BB_SIZE(SprjLuaEventCallbackName, 0x30);
static_assert(alignof(SprjLuaEventCallbackName) == 8, "alignof(SprjLuaEventCallbackName)");
BB_OFFSET(SprjLuaEventCallbackName, storage, 8);
BB_OFFSET(SprjLuaEventCallbackName, length, 0x18);
BB_OFFSET(SprjLuaEventCallbackName, capacity, 0x20);
BB_OFFSET(SprjLuaEventCallbackName, allocator, 0x28);
BB_SIZE(SprjLuaEventRegistration, 0x50);
BB_SIZE(SprjLuaEventRegistration, SprjLuaEventRegistration::SIZE);
static_assert(alignof(SprjLuaEventRegistration) == 8, "alignof(SprjLuaEventRegistration)");
BB_OFFSET(SprjLuaEventRegistration, key, 0);
BB_OFFSET(SprjLuaEventRegistration, consume_after_match, 0x10);
BB_OFFSET(SprjLuaEventRegistration, send_after_match, 0x11);
BB_OFFSET(SprjLuaEventRegistration, discard_consumed, 0x12);
BB_OFFSET(SprjLuaEventRegistration, deletion_requested, 0x13);
BB_OFFSET(SprjLuaEventRegistration, callback_name, 0x18);
BB_OFFSET(SprjLuaEventRegistration, state_48, 0x48);
BB_SIZE(SprjLuaEventMessageMap, 0x50);
BB_SIZE(SprjLuaEventMessageMap, SprjLuaEventMessageMap::SIZE);
static_assert(alignof(SprjLuaEventMessageMap) == 8, "alignof(SprjLuaEventMessageMap)");
BB_OFFSET(SprjLuaEventMessageMap, vtable, 0);
BB_OFFSET(SprjLuaEventMessageMap, active, 8);
BB_OFFSET(SprjLuaEventMessageMap, consumed, 0x28);
BB_OFFSET(SprjLuaEventMessageMap, script_type, 0x48);
BB_SIZE(SprjLuaEventRegistrationList, 0x20);
BB_OFFSET(SprjLuaEventRegistrationList, sentinel, 8);
BB_OFFSET(SprjLuaEventRegistrationList, count, 0x10);
BB_OFFSET(SprjLuaEventRegistrationList, allocator, 0x18);
BB_SIZE(SprjLuaEventRegistrationNode, 0x18);
BB_OFFSET(SprjLuaEventRegistrationNode, next, 0);
BB_OFFSET(SprjLuaEventRegistrationNode, previous, 8);
BB_OFFSET(SprjLuaEventRegistrationNode, registration, 0x10);
}  // namespace detail::lua_event_message_layout

}  // namespace bb
