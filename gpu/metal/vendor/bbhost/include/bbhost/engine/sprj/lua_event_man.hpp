// SprjLuaEventMan, its event context, condition list, and Lua_MultiDoping anchors.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/lua_event_message.hpp"

namespace bb {

struct SprjLuaEventConditionNode;

// Compiled Lua_MultiDoping event callback, not a general all-NPC iterator.
// Reads GameStateMan+0x16f8 -> +0x08 and applies 7500/7501 to the supplied
// entity for counts 1/2. There is no branch for three or more helpers.
inline constexpr Rva LUA_MULTI_DOPING_HANDLER_FN{0x138bc70};
// Lua accessor for the native presentation state's team-1 count, not a
// persistent co-op membership count.
inline constexpr Rva LUA_GET_WHITE_GHOST_COUNT_FN{0x1337970};
// Recalculates registered event-body HP records (indices 1 through 40), using
// their base HP and the resolved actor's current SpEffect multiplier. Does not
// apply a scaling effect to every enemy.
inline constexpr Rva MULTI_DOPING_ALL_EVENT_BODY_FN{0x1333a60};
// Compiled ForceUpdate callback scheduled by Lua_MultiDoping. Refreshes
// event-body HP, then marks the requested actor for next-frame update.
inline constexpr Rva LUA_MULTI_DOPING_FORCE_UPDATE_FN{0x138bd90};
// Calculates max-HP scaling from the actor's SpEffect container. Also includes
// native NG and matching corrections; not simply a single param-row lookup.
inline constexpr Rva SPEFFECT_MAX_HP_MULTIPLIER_FN{0x17e46e0};
inline constexpr std::uint32_t MULTI_DOPING_ONE_HELPER_EFFECT_ID = 7500;
inline constexpr std::uint32_t MULTI_DOPING_TWO_HELPERS_EFFECT_ID = 7501;
inline constexpr std::uint32_t MULTI_DOPING_REFRESH_KEY = 4070;
inline constexpr float MULTI_DOPING_REFRESH_DELAY_SECONDS = 0.1f;

// Historical verified-prefix boundary; use SPRJ_LUA_EVENT_MAN_SIZE for the allocation.
inline constexpr std::size_t SPRJ_LUA_EVENT_MAN_PREFIX_SIZE = 0x10;
inline constexpr std::size_t SPRJ_LUA_EVENT_MAN_SIZE = 0x30;
inline constexpr std::size_t SPRJ_LUA_EVENT_MAN_EVENT_DISPATCHER_OFFSET = 0x08;
// Historical verified-prefix boundary; the full captured context is now 0xb8.
inline constexpr std::size_t SPRJ_LUA_EVENT_CONTEXT_PREFIX_SIZE = 0x8e;
inline constexpr std::size_t SPRJ_LUA_EVENT_CONTEXT_SIZE = 0xb8;
inline constexpr std::size_t SPRJ_LUA_EVENT_CONTEXT_NET_MESSAGE_OFFSET = 0x8a;
inline constexpr std::size_t SPRJ_LUA_EVENT_CONTEXT_LEAVE_EVENT_LATCH_OFFSET = 0x8d;

// Full 0xb8-byte runtime context owned at SprjLuaEventMan+0x08, allocated with
// eight-byte alignment by RVA 0x13129c0. SprjLuaEventContext is a descriptive
// name. The constructor resolves a registry entry using the native string
// SprjLuaEventScriptImitation; that shared lookup key alone does not establish
// a one-to-one reflected C++ class identity for this context.
//
// NotNetMessage_begin/end write +0x8a. The presentation-member leave handler
// at RVA 0x1316640 sets +0x8d; RVA 0x1339b50 publishes that byte to the
// event-action and action-button managers.
//
// Native destruction empties the opaque vector, destroys the embedded message
// map, then frees vector storage. The Lua manager separately frees this object.
struct SprjLuaEventContext {
    static constexpr std::size_t SIZE = SPRJ_LUA_EVENT_CONTEXT_SIZE;
    static constexpr Rva VTABLE{0x531d780};
    static constexpr Rva CONSTRUCTOR_FN{0x1339530};
    static constexpr Rva DESTRUCTOR_FN{0x1339740};
    static constexpr Rva SCRIPT_TYPE_NAME{0x492f2a3};
    static constexpr Rva SECOND_TIMER_INITIAL_VALUE{0x5546ea0};
    static constexpr Rva DISPATCH_NAMED_FN{0x1339870};
    // Drains peer packets 0x0e/0x0f, updates the flag-clear timer, then
    // publishes leave_event_latched to EventActMan +0x58 and ActionButtonMan +0x52.
    static constexpr Rva UPDATE_NETWORK_FN{0x1339b50};
    // Dispatches message registrations, then offers the key to condition
    // virtual +0x18 until one condition handles it.
    static constexpr Rva DISPATCH_AND_NOTIFY_CONDITIONS_FN{0x133a150};
    // Dispatches first, then optionally broadcasts a 0x14-byte packet 0x0e
    // containing the four-word key and local-player model ID.
    static constexpr Rva DISPATCH_AND_SEND_FN{0x133a1f0};
    static constexpr Rva REMOVE_REGISTRATION_FN{0x133a360};
    static constexpr Rva SWEEP_REGISTRATIONS_FN{0x133a3d0};
    // Adds a timed condition and a message-15/16 registration. Condition
    // allocation/insertion and registration allocation are separate
    // operations; a false return does not prove no condition was inserted.
    static constexpr Rva SCHEDULE_TIMED_CALLBACK_FN{0x132b1c0};

    const void* vtable;
    // Script registry entry, followed through its parent at +0x08 by name lookup.
    void* script_type;
    std::uint64_t _unk10;
    // Constructor/destructor establish this vector header and allocator;
    // element type, stride, and population remain unproven.
    void* vector_begin;
    void* vector_end;
    void* vector_capacity;
    void* vector_allocator;
    SprjLuaEventMessageMap messages;
    Unknown<2> _unk88;
    // Copied to newly registered callbacks' send_after_match. Initialized one.
    std::uint8_t net_message_enabled;
    // Initialized one and overwritten by each matching registration's IsSend
    // byte. DISPATCH_AND_SEND_FN consults this even if dispatch returned 0/-1;
    // no-match dispatch leaves the preceding value intact.
    std::uint8_t send_after_dispatch;
    // Registration producers copy this to discard_consumed; initialized zero.
    std::uint8_t discard_consumed_registrations;
    std::uint8_t leave_event_latched;
    Unknown<2> _unk8e;
    // Initialized -1. UPDATE_NETWORK_FN eventually clears this event flag and
    // resets the ID to -1 when flag_clear_timer is no longer positive.
    std::int32_t pending_clear_flag_id;
    Unknown<4> _unk94;
    // Initialized -1.0. A positive value is decremented without clearing the
    // flag that tick, even when it crosses zero. Clearing happens on a later
    // entry with a nonpositive/NaN timer and pending ID != -1.
    FD4Time flag_clear_timer;
    // Initialized from RVA 0x5546ea0; additional consumers remain unclassified.
    FD4Time timer_a8;

    // Whether UPDATE_NETWORK_FN takes its clear/reset branch on entry. Says
    // nothing about whether the flag's native backing storage exists.
    bool flag_clear_is_due() const { return !(flag_clear_timer.time > 0.0f) && pending_clear_flag_id != -1; }
};

// Descriptive name for the 0x30-byte condition owner allocated by the Lua
// manager. No native class-name string or reflected registration is claimed.
// Construction inserts four initial conditions (type codes 4, 17, 0, 1).
// Destruction destroys/frees each condition, then the list nodes and sentinel.
// SprjDbgEvent borrows this owner; it does not own its condition objects.
struct SprjLuaEventConditionList {
    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva VTABLE{0x531d760};
    static constexpr Rva CONSTRUCTOR_FN{0x13224f0};
    static constexpr Rva DESTRUCTOR_FN{0x1322840};
    // Receives the embedded list at owner +0x08, not the outer owner pointer.
    static constexpr Rva INSERT_FN{0x1327a30};

    const void* vtable;
    std::uint64_t _unk08;
    SprjLuaEventConditionNode* sentinel;
    std::uint64_t count;
    void* allocator;
    // Initialized zero; broader semantics are unclassified.
    std::uint8_t state_28;
    Unknown<7> _unk29;
};

// Descriptive list node. The sentinel has no initialized condition payload.
struct SprjLuaEventConditionNode {
    SprjLuaEventConditionNode* next;
    SprjLuaEventConditionNode* previous;
    void* condition;
};

// Full non-polymorphic 0x30-byte Bloodborne Lua manager, aligned to eight.
//
// RVA 0x13129c0 allocates the condition owner at +0x00 and the dispatcher at
// +0x08, then lends both to SprjDbgEvent in reverse order. The first pointer
// is not a vtable. WorldRes loader RVA 0x154bfb0 proves the outer allocation;
// unload RVA 0x1547b40 resets its children/binding, frees it, and clears the
// global. event_dispatcher may be null.
struct SprjLuaEventMan {
    static constexpr std::size_t SIZE = SPRJ_LUA_EVENT_MAN_SIZE;
    static constexpr Rva SINGLETON_PTR = SPRJ_LUA_EVENT_MAN_SINGLETON_PTR;
    static constexpr Rva CONSTRUCTOR_FN{0x13128c0};
    static constexpr Rva WORLD_LOAD_CREATE_FN{0x154bfb0};
    static constexpr Rva WORLD_UNLOAD_FN{0x1547b40};
    // Allocates 0x30 / 0xb8 children, records the current block in EventState,
    // and publishes borrowed child pointers to SprjDbgEvent.
    static constexpr Rva INITIALIZE_CHILDREN_FN{0x13129c0};
    // Destroys/frees both children before detaching SprjDbgEvent. Also
    // releases the manager's script-binding storage at +0x10.
    static constexpr Rva RESET_CHILDREN_FN{0x1312900};
    static constexpr Rva SCRIPT_INITIALIZE_FN{0x1312b90};
    static constexpr Rva UPDATE_FN{0x1312ff0};

    SprjLuaEventConditionList* conditions;
    SprjLuaEventContext* event_dispatcher;
    // Three native words initialized zero. SCRIPT_INITIALIZE_FN obtains the
    // first two from registry creation and uses them to invoke g_Initialize.
    // Teardown tests word one, then follows word zero's virtual +0x48 with this
    // storage and the allocator. The final word's meaning remains opaque.
    std::uint64_t script_binding[3];
    // Initially zero; UPDATE_FN decrements a positive timer. Crossing <=0
    // activates the selected lamp in the same tick, unlike the context timer.
    float lamp_activation_delay;
    // Initially -1; read when a positive lamp_activation_delay expires.
    std::int32_t pending_lamp_id;
};

namespace detail::lua_event_man_layout {
BB_SIZE(SprjLuaEventMan, 0x30);
BB_SIZE(SprjLuaEventMan, SprjLuaEventMan::SIZE);
static_assert(alignof(SprjLuaEventMan) == 8, "alignof(SprjLuaEventMan)");
BB_OFFSET(SprjLuaEventMan, script_binding, SPRJ_LUA_EVENT_MAN_PREFIX_SIZE);
BB_OFFSET(SprjLuaEventMan, conditions, 0);
BB_OFFSET(SprjLuaEventMan, event_dispatcher, SPRJ_LUA_EVENT_MAN_EVENT_DISPATCHER_OFFSET);
BB_OFFSET(SprjLuaEventMan, lamp_activation_delay, 0x28);
BB_OFFSET(SprjLuaEventMan, pending_lamp_id, 0x2c);
BB_SIZE(SprjLuaEventContext, 0xb8);
BB_SIZE(SprjLuaEventContext, SprjLuaEventContext::SIZE);
static_assert(alignof(SprjLuaEventContext) == 8, "alignof(SprjLuaEventContext)");
BB_OFFSET(SprjLuaEventContext, vtable, 0);
BB_OFFSET(SprjLuaEventContext, script_type, 8);
BB_OFFSET(SprjLuaEventContext, vector_begin, 0x18);
BB_OFFSET(SprjLuaEventContext, vector_end, 0x20);
BB_OFFSET(SprjLuaEventContext, vector_capacity, 0x28);
BB_OFFSET(SprjLuaEventContext, vector_allocator, 0x30);
BB_OFFSET(SprjLuaEventContext, messages, 0x38);
BB_OFFSET(SprjLuaEventContext, net_message_enabled, SPRJ_LUA_EVENT_CONTEXT_NET_MESSAGE_OFFSET);
BB_OFFSET(SprjLuaEventContext, leave_event_latched, SPRJ_LUA_EVENT_CONTEXT_LEAVE_EVENT_LATCH_OFFSET);
BB_OFFSET(SprjLuaEventContext, send_after_dispatch, 0x8b);
BB_OFFSET(SprjLuaEventContext, discard_consumed_registrations, 0x8c);
static_assert(offsetof(SprjLuaEventContext, leave_event_latched) + 1 == SPRJ_LUA_EVENT_CONTEXT_PREFIX_SIZE,
              "SprjLuaEventContext verified prefix");
BB_OFFSET(SprjLuaEventContext, pending_clear_flag_id, 0x90);
BB_OFFSET(SprjLuaEventContext, flag_clear_timer, 0x98);
BB_OFFSET(SprjLuaEventContext, timer_a8, 0xa8);
BB_SIZE(SprjLuaEventConditionList, 0x30);
BB_SIZE(SprjLuaEventConditionList, SprjLuaEventConditionList::SIZE);
static_assert(alignof(SprjLuaEventConditionList) == 8, "alignof(SprjLuaEventConditionList)");
BB_OFFSET(SprjLuaEventConditionList, vtable, 0);
BB_OFFSET(SprjLuaEventConditionList, sentinel, 0x10);
BB_OFFSET(SprjLuaEventConditionList, count, 0x18);
BB_OFFSET(SprjLuaEventConditionList, allocator, 0x20);
BB_OFFSET(SprjLuaEventConditionList, state_28, 0x28);
BB_SIZE(SprjLuaEventConditionNode, 0x18);
static_assert(alignof(SprjLuaEventConditionNode) == 8, "alignof(SprjLuaEventConditionNode)");
BB_OFFSET(SprjLuaEventConditionNode, next, 0);
BB_OFFSET(SprjLuaEventConditionNode, previous, 8);
BB_OFFSET(SprjLuaEventConditionNode, condition, 0x10);
}  // namespace detail::lua_event_man_layout

}  // namespace bb
