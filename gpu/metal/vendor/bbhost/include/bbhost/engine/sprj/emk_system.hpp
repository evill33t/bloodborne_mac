// EMEVD runtime ownership, event instances, and the reflected update task.
//
// SprjEmkSystem and SprjEmkSystemUpdateTask are established native names.
// SprjEmkEventIns is named by native debug keys. Other supporting names
// describe recovered layouts, not additional native classes. Native code owns
// these allocations. Locations are RVAs for Bloodborne 1.09.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/emk_condition.hpp"

namespace bb {

struct SprjEvdRuntimeData;
struct SprjEmkEventCore;
struct SprjEmkRestartRecord;
struct SprjEmkInstructionDispatcher;

// 0x20-byte list header. The 0x18-byte sentinel links to itself when empty;
// its payload is not initialized. Real nodes own separately allocated records.
struct SprjEmkRestartNode;
struct SprjEmkRestartQueue {
    std::uint64_t _unk00;
    SprjEmkRestartNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

struct SprjEmkRestartNode {
    SprjEmkRestartNode* next;
    SprjEmkRestartNode* previous;
    SprjEmkRestartRecord* record;
};

// Full 0x28-byte record, allocated aligned to eight by event end/restart.
// Owns a 16-byte-aligned copy of event arguments, but borrows runtime_data.
// Draining reconstructs a descriptor by event ID and then frees the record,
// argument copy, and list node. Slot is sign-extended from the event's i16.
struct SprjEmkRestartRecord {
    std::int32_t event_id;
    std::int32_t slot;
    std::uint32_t map_id;
    Unknown<4> _unk0c;
    std::uint8_t* arguments;
    std::uint32_t argument_size;
    Unknown<4> _unk1c;
    SprjEvdRuntimeData* runtime_data;
};

// 0x30-byte red-black node, aligned to eight. Sentinel links initially point
// to itself and color/is_nil are 1; its key/value remain uninitialized.
struct SprjEmkScriptNode {
    SprjEmkScriptNode* left;
    SprjEmkScriptNode* parent;
    SprjEmkScriptNode* right;
    std::uint8_t color;
    std::uint8_t is_nil;
    Unknown<6> _unk1a;
    std::uint32_t map_id;
    Unknown<4> _unk24;
    SprjEvdRuntimeData* runtime_data;
};

// 0x20-byte tree header with unsigned map keys and borrowed EVD data.
struct SprjEmkScriptMap {
    std::uint64_t _unk00;
    SprjEmkScriptNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// Non-polymorphic 0x78-byte owner, allocated aligned to eight by WorldRes
// creation and the update task's Initialize callback. Distinct from
// SprjEmkResMan, which owns event-file residency.
//
// Update receives peer messages before merging pending into active, updates
// active objects, then destroys objects whose retirement bit is set. Normal
// insertion compares descending signed event ID and signed slot. The special
// completion event is appended at the pending tail; do not assume every
// pending chain is sorted. The merge reuses a forward cursor in active.
//
// Native destruction clears the script tree, restart records, active events,
// dispatcher, and sentinels. It does not walk or clear pending. Map unloading
// explicitly removes matching objects from both chains. Borrowed script data
// is neither retained nor released by these paths.
struct SprjEmkSystem {
    static constexpr std::size_t SIZE = 0x78;
    static constexpr Rva SINGLETON_PTR = SPRJ_EMK_SYSTEM_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493117d};
    static constexpr Rva NAME_STRING_UTF16{0x495116e};
    static constexpr Rva CONSTRUCTOR_FN{0x12ef860};
    static constexpr Rva DESTRUCTOR_FN{0x12efa70};
    static constexpr Rva UPDATE_FN{0x12f0f90};
    // Null cap/data is ignored. Inserts a borrowed cap.data under the unsigned
    // map ID if absent. A duplicate preserves the old tree value but still
    // schedules from the newly supplied data. Event 50 receives an immediate
    // update; event 0 and a completion object enter pending.
    static constexpr Rva LOAD_SCRIPT_FN{0x12eff60};
    // Queues event 1 if found, then removes all matching-map objects from
    // active and pending, including that newly queued object. Also removes
    // matching restart records, calls EventMan cleanup, and erases the tree
    // node. There is no intervening event update in this native function.
    static constexpr Rva UNLOAD_SCRIPT_FN{0x12f03d0};
    // Deduplicates (event ID, slot) across both chains only if both inputs
    // are nonnegative; map ID is not part of the key. Input slot is compared
    // as i32 but stored truncated to i16. Immediate update does not dequeue.
    static constexpr Rva CREATE_EVENT_FN{0x12efd00};
    static constexpr Rva ENQUEUE_RESTART_FN{0x12f1930};
    // These container helpers receive the header at owner +0x40.
    static constexpr Rva INSERT_SCRIPT_NODE_FN{0x12f2150};
    static constexpr Rva CLEAR_SCRIPT_NODES_FN{0x12f1c10};
    static constexpr Rva LOAD_COMMON_FN{0x12f08c0};
    static constexpr Rva UNLOAD_COMMON_FN{0x12f0990};
    static constexpr Rva LOAD_AREA_FN{0x12f0a60};
    static constexpr Rva UNLOAD_AREA_FN{0x12f0bd0};
    static constexpr Rva LOAD_BLOCK_FN{0x12f0d40};
    static constexpr Rva UNLOAD_BLOCK_FN{0x12f0e60};
    static constexpr Rva WORLD_CREATE_FN{0x1565da0};
    static constexpr Rva WORLD_DESTROY_FN{0x1565e50};
    static constexpr std::uint32_t COMMON_MAP_ID = 0xffffffffu;
    // Looked up once per registered script during the WorldChrMan state-3
    // reset branch, after restart-queue processing and active restarts.
    static constexpr std::int32_t RESET_EVENT_ID = 1000000000;

    SprjEmkEventCore* active;
    SprjEmkEventCore* pending;
    SprjEmkRestartQueue restart_queue;
    // Post-incremented at event creation; only its low 15 bits are packed into
    // event flags. Not a globally unique event identity.
    std::uint32_t next_sequence;
    Unknown<4> _unk34;
    SprjEmkInstructionDispatcher* dispatcher;
    SprjEmkScriptMap scripts;
    // Signed counter. Packet 0x31 receive runs only when this is <= 0; event
    // merging/updating still runs while positive. Completion destruction
    // decrements it. Load increments even if completion allocation fails.
    std::int32_t pending_completion_count;
    Unknown<4> _unk64;
    // Constructor clears this word; its interpretation remains unresolved.
    std::uint64_t value_68_bits;
    // Gates condition-holder debug-menu removal; initially zero.
    std::uint8_t condition_debug_enabled;
    Unknown<7> _unk71;
};

// Full 0x78-byte dispatcher, allocated aligned to eight. Construction clears
// all fifteen words, then fills fourteen slots with separate one-byte native
// allocations whose values are not initialized here. Destruction frees slots
// 12 down to 0, then 13. The final word's purpose remains unresolved.
struct SprjEmkInstructionDispatcher {
    static constexpr std::size_t SIZE = 0x78;
    static constexpr Rva CONSTRUCTOR_FN{0x17b8f20};
    static constexpr Rva DESTRUCTOR_FN{0x17b9180};
    static constexpr Rva DISPATCH_FN{0x17b93a0};

    std::uint8_t* handlers[14];
    std::uint64_t value_70_bits;
};

// Common virtual slots through +0x50. The interpreter additionally has a slot
// at +0x58; this is deliberately only the shared vtable prefix.
struct SprjEmkEventVTablePrefix {
    const void* destroy;
    const void* delete_;  // `delete` is a C++ keyword
    const void* update;
    const void* restart;
    const void* slot_20;
    const void* receive_network_callback;
    const void* end_or_restart;
    const void* behavior_code;
    const void* behavior_digit;
    const void* slot_48;
    const void* slot_50;
};

// Common 0x78-byte prefix, not an independently allocated base-class size.
// Concrete events are aligned to 16 and embed this as a member so their flag
// at +0x78 is not displaced by base tail padding.
struct SprjEmkEventCore {
    static constexpr std::size_t PREFIX_SIZE = 0x78;
    static constexpr Rva BASE_VTABLE{0x5366880};
    static constexpr Rva NETWORK_SOURCE_VTABLE{0x5320920};
    static constexpr Rva RECEIVE_NETWORK_CALLBACK_FN{0x12ee420};
    static constexpr Rva CLEAR_CONDITIONS_FN{0x12ebff0};
    static constexpr std::uint16_t RETIREMENT_BIT = 1;

    const SprjEmkEventVTablePrefix* vtable;
    std::uint64_t _unk08;
    // Initially zero; state points into this storage until separately backed.
    std::uint32_t inline_state[4];
    // Native self-reference initially points at inline_state. Native cleanup
    // frees a non-null pointer only if different from that inline address.
    std::uint32_t* state;
    std::int32_t event_id;
    std::int16_t slot;
    // Bit 0 requests retirement; bits 1..15 hold the truncated sequence.
    std::uint16_t flags;
    // Verified holder view, including the instruction-progress words at event
    // +0x48/+0x4c. Its three group pointers initially start null.
    SprjEmkConditionHolder conditions;
    std::uint64_t value_50_bits;
    // Embedded polymorphic scalar at +0x58, not FD4Time or an execution budget.
    const void* network_source_vtable;
    // Initially 10000. Callback 1 writes matched player's +0x3b8 plus 10001;
    // callback 2 copies its first payload word. Full identifier semantics open.
    std::int32_t network_source;
    Unknown<4> _unk64;
    std::uint32_t map_id;
    Unknown<4> _unk6c;
    SprjEmkEventCore* next;

    bool is_retired() const { return (flags & RETIREMENT_BIT) != 0; }
    std::uint16_t sequence() const { return static_cast<std::uint16_t>(flags >> 1); }
};

struct SprjEmkEventRecord;

// Borrowed constructor input. Both pointers refer to the EVD resource's
// runtime state; constructing an event does not increment its reference count.
struct SprjEmkEventDescriptor {
    SprjEvdRuntimeData* runtime_data;
    const SprjEmkEventRecord* event;
};

// 0x30-byte EMEVD event-table stride established by native lookups. Offsets
// are relative to the corresponding file section, not executable RVAs or
// native pointers. Only observed low 32-bit count/ID fields are named.
struct SprjEmkEventRecord {
    std::int32_t event_id;
    Unknown<4> _unk04;
    std::uint32_t instruction_count;
    Unknown<4> _unk0c;
    std::int64_t instructions_offset;
    std::uint32_t argument_replacement_count;
    Unknown<4> _unk1c;
    std::int64_t argument_replacements_offset;
    // Virtual +0x38 returns this word; +0x40 extracts a requested decimal
    // digit. The manager uses digit zero values 1 (restart) and 2 (remove).
    std::uint32_t behavior_code;
    Unknown<4> _unk2c;
};

// 0x20-byte instruction-table stride. Argument data offset is relative to the
// EMEVD argument-data section. The final word is not classified here.
struct SprjEmkInstructionRecord {
    std::int32_t bank;
    std::int32_t instruction_id;
    std::uint32_t argument_size;
    Unknown<4> _unk0c;
    std::int64_t arguments_offset;
    std::uint64_t _unk18;
};

// Full 0xe0-byte event interpreter, named SprjEmkEventIns by its native debug
// registration/removal keys and allocated aligned to 16. Resource and table
// pointers remain borrowed; arguments and instruction_arguments are owned
// native buffers. Do not copy: core.state is a native self-reference.
struct alignas(16) SprjEmkEventIns {
    static constexpr std::size_t SIZE = 0xe0;
    static constexpr Rva DEBUG_NAME_UTF16{0x4946d22};
    static constexpr Rva ATTACH_DEBUG_FN{0x12ed380};
    static constexpr Rva REMOVE_DEBUG_FN{0x12ed940};
    static constexpr Rva VTABLE{0x5366810};
    static constexpr Rva CONSTRUCTOR_FN{0x12ec990};
    static constexpr Rva DESTRUCTOR_FN{0x12ecd00};
    static constexpr Rva DELETE_FN{0x12ecc40};
    static constexpr Rva UPDATE_FN{0x12ecdc0};
    static constexpr Rva RESTART_FN{0x12ecfe0};
    static constexpr Rva END_OR_RESTART_FN{0x12ed100};
    // Receives self +0x80, not the outer event pointer.
    static constexpr Rva ADVANCE_FN{0x12ef2e0};
    // Receives self +0x80; frees instruction_arguments and arguments only.
    static constexpr Rva DESTROY_INTERPRETER_STATE_FN{0x12ef150};
    static constexpr Rva BEHAVIOR_CODE_FN{0x12ede40};
    static constexpr Rva BEHAVIOR_DIGIT_FN{0x12ede50};

    SprjEmkEventCore core;
    std::uint8_t flag_78;
    Unknown<7> _unk79;
    SprjEvdRuntimeData* runtime_data;
    const SprjEmkEventRecord* event;
    std::uint32_t argument_size;
    Unknown<4> _unk94;
    std::uint8_t* arguments;
    std::int32_t instruction_index;
    Unknown<4> _unka4;
    // Set alongside instruction by ADVANCE_FN; may remain non-null when
    // instruction is null at end of event.
    SprjEvdRuntimeData* instruction_source;
    const SprjEmkInstructionRecord* instruction;
    // Copied instruction argument bytes with event-argument replacements. If
    // absent, dispatch reads argument bytes from the borrowed EMEVD image.
    std::uint8_t* instruction_arguments;
    // Added to instruction_index by advance, then reset to one. Zero can
    // retain the current instruction. End/restart writes larger signed deltas.
    std::int32_t instruction_delta;
    // Returned to update by advance, then reset to one; zero ends this tick.
    std::uint8_t continue_update;
    Unknown<3> _unkc5;
    // Transient suppression of callback-1 broadcast, cleared by update when
    // consumed. Not durable evidence that the whole event is remote.
    std::uint8_t suppress_callback_once;
    Unknown<7> _unkc9;
    std::uint64_t value_d0_bits;
    Unknown<8> _unkd8;
};

// Earlier descriptive spelling for the same interpreter layout.
using SprjEmkEventInstance = SprjEmkEventIns;

// Full 0x80-byte completion object, allocated aligned to 16 after event 0.
// First update decrements delay_ticks from one and returns. The next update
// activates the map's WorldObjAct block (when available), commits session
// success when the map matches the current load context, then ends the event.
// Destruction decrements the owner's signed pending_completion_count.
struct alignas(16) SprjEmkCompletionEvent {
    static constexpr std::size_t SIZE = 0x80;
    static constexpr std::int32_t EVENT_ID = 1000000001;
    static constexpr std::int16_t SLOT = -1;
    static constexpr Rva VTABLE{0x53668f0};
    static constexpr Rva DESTRUCTOR_FN{0x12eec20};
    static constexpr Rva DELETE_FN{0x12eeb30};
    static constexpr Rva UPDATE_FN{0x12eed10};
    static constexpr Rva END_FN{0x12ef090};

    SprjEmkEventCore core;
    std::uint8_t flag_78;
    Unknown<3> _unk79;
    std::int32_t delay_ticks;
};

// Full reflected 0xd0-byte extent, with callback-proven fields. An independent
// allocation alignment and concrete constructor/instance vtable have not been
// located. Opaque task bytes are not inferred from other SprjStepTask
// instantiations.
//
// Initialize ensures the singleton exists and increments requested_step.
// Update runs the owner, queries virtual +0x78 for SprjStepAttachFinishRequest,
// then sets flag_60 and increments requested_step on that request. Finalize
// destroys/frees the owner, clears its singleton, and requests step -1.
struct SprjEmkSystemUpdateTask {
    static constexpr std::size_t SIZE = 0xd0;
    static constexpr Rva NAME_STRING{0x492bcb9};
    static constexpr Rva NAME_STRING_UTF16{0x4946d78};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_EMK_SYSTEM_UPDATE_TASK_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_EMK_SYSTEM_UPDATE_TASK_TEMPLATE;
    // Vtable of the metadata object, not an instance vtable.
    static constexpr Rva RUNTIME_CLASS_VTABLE{0x5366960};
    // Metadata vtable +0x50 returns 0xd0. The zero-returning +0x40 slot is a
    // different method and must not be interpreted as the instance size.
    static constexpr Rva SIZE_FN{0x12f3ec0};
    static constexpr Rva INITIALIZE_FN{0x12f24b0};
    static constexpr Rva UPDATE_FN{0x12f2510};
    static constexpr Rva FINALIZE_FN{0x12f2590};
    static constexpr Rva NEXT_STEP_FN{0x12f4830};

    const void* vtable;
    Unknown<0x50> _unk08;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t flag_60;
    Unknown<0x6f> _unk61;
};

namespace detail::emk_system_layout {
BB_SIZE(SprjEmkSystem, 0x78);
static_assert(alignof(SprjEmkSystem) == 8, "alignof(SprjEmkSystem)");
BB_OFFSET(SprjEmkSystem, active, 0);
BB_OFFSET(SprjEmkSystem, pending, 8);
BB_OFFSET(SprjEmkSystem, restart_queue, 0x10);
BB_OFFSET(SprjEmkSystem, next_sequence, 0x30);
BB_OFFSET(SprjEmkSystem, dispatcher, 0x38);
BB_OFFSET(SprjEmkSystem, scripts, 0x40);
BB_OFFSET(SprjEmkSystem, pending_completion_count, 0x60);
BB_OFFSET(SprjEmkSystem, value_68_bits, 0x68);
BB_OFFSET(SprjEmkSystem, condition_debug_enabled, 0x70);
BB_SIZE(SprjEmkInstructionDispatcher, 0x78);
BB_SIZE(SprjEmkInstructionDispatcher, SprjEmkInstructionDispatcher::SIZE);
static_assert(alignof(SprjEmkInstructionDispatcher) == 8, "alignof(SprjEmkInstructionDispatcher)");
BB_OFFSET(SprjEmkInstructionDispatcher, value_70_bits, 0x70);

BB_SIZE(SprjEmkRestartQueue, 0x20);
BB_OFFSET(SprjEmkRestartQueue, sentinel, 8);
BB_OFFSET(SprjEmkRestartQueue, count, 0x10);
BB_OFFSET(SprjEmkRestartQueue, allocator, 0x18);
BB_SIZE(SprjEmkRestartNode, 0x18);
BB_OFFSET(SprjEmkRestartNode, record, 0x10);
BB_SIZE(SprjEmkRestartRecord, 0x28);
static_assert(alignof(SprjEmkRestartRecord) == 8, "alignof(SprjEmkRestartRecord)");
BB_OFFSET(SprjEmkRestartRecord, slot, 4);
BB_OFFSET(SprjEmkRestartRecord, map_id, 8);
BB_OFFSET(SprjEmkRestartRecord, arguments, 0x10);
BB_OFFSET(SprjEmkRestartRecord, argument_size, 0x18);
BB_OFFSET(SprjEmkRestartRecord, runtime_data, 0x20);

BB_SIZE(SprjEmkScriptMap, 0x20);
BB_OFFSET(SprjEmkScriptMap, sentinel, 8);
BB_OFFSET(SprjEmkScriptMap, count, 0x10);
BB_OFFSET(SprjEmkScriptMap, allocator, 0x18);
BB_SIZE(SprjEmkScriptNode, 0x30);
static_assert(alignof(SprjEmkScriptNode) == 8, "alignof(SprjEmkScriptNode)");
BB_OFFSET(SprjEmkScriptNode, parent, 8);
BB_OFFSET(SprjEmkScriptNode, right, 0x10);
BB_OFFSET(SprjEmkScriptNode, color, 0x18);
BB_OFFSET(SprjEmkScriptNode, is_nil, 0x19);
BB_OFFSET(SprjEmkScriptNode, map_id, 0x20);
BB_OFFSET(SprjEmkScriptNode, runtime_data, 0x28);

BB_SIZE(SprjEmkEventCore, 0x78);
BB_SIZE(SprjEmkEventCore, SprjEmkEventCore::PREFIX_SIZE);
static_assert(alignof(SprjEmkEventCore) == 8, "alignof(SprjEmkEventCore)");
BB_OFFSET(SprjEmkEventCore, inline_state, 0x10);
BB_OFFSET(SprjEmkEventCore, state, 0x20);
BB_OFFSET(SprjEmkEventCore, event_id, 0x28);
BB_OFFSET(SprjEmkEventCore, slot, 0x2c);
BB_OFFSET(SprjEmkEventCore, flags, 0x2e);
BB_OFFSET(SprjEmkEventCore, conditions, 0x30);
static_assert(offsetof(SprjEmkEventCore, conditions) + offsetof(SprjEmkConditionHolder, current_instruction) == 0x48,
              "SprjEmkEventCore::conditions.current_instruction");
static_assert(offsetof(SprjEmkEventCore, conditions) + offsetof(SprjEmkConditionHolder, remote_instruction) == 0x4c,
              "SprjEmkEventCore::conditions.remote_instruction");
BB_OFFSET(SprjEmkEventCore, value_50_bits, 0x50);
BB_OFFSET(SprjEmkEventCore, network_source_vtable, 0x58);
BB_OFFSET(SprjEmkEventCore, network_source, 0x60);
BB_OFFSET(SprjEmkEventCore, map_id, 0x68);
BB_OFFSET(SprjEmkEventCore, next, 0x70);
BB_SIZE(SprjEmkEventVTablePrefix, 0x58);
BB_OFFSET(SprjEmkEventVTablePrefix, update, 0x10);
BB_OFFSET(SprjEmkEventVTablePrefix, receive_network_callback, 0x28);
BB_OFFSET(SprjEmkEventVTablePrefix, end_or_restart, 0x30);
BB_OFFSET(SprjEmkEventVTablePrefix, behavior_digit, 0x40);

BB_SIZE(SprjEmkEventInstance, 0xe0);
BB_SIZE(SprjEmkEventIns, SprjEmkEventIns::SIZE);
static_assert(alignof(SprjEmkEventInstance) == 16, "alignof(SprjEmkEventInstance)");
BB_OFFSET(SprjEmkEventInstance, flag_78, 0x78);
BB_OFFSET(SprjEmkEventInstance, runtime_data, 0x80);
BB_OFFSET(SprjEmkEventInstance, event, 0x88);
BB_OFFSET(SprjEmkEventInstance, argument_size, 0x90);
BB_OFFSET(SprjEmkEventInstance, arguments, 0x98);
BB_OFFSET(SprjEmkEventInstance, instruction_index, 0xa0);
BB_OFFSET(SprjEmkEventInstance, instruction_source, 0xa8);
BB_OFFSET(SprjEmkEventInstance, instruction, 0xb0);
BB_OFFSET(SprjEmkEventInstance, instruction_arguments, 0xb8);
BB_OFFSET(SprjEmkEventInstance, instruction_delta, 0xc0);
BB_OFFSET(SprjEmkEventInstance, continue_update, 0xc4);
BB_OFFSET(SprjEmkEventInstance, suppress_callback_once, 0xc8);
BB_OFFSET(SprjEmkEventInstance, value_d0_bits, 0xd0);
BB_SIZE(SprjEmkCompletionEvent, 0x80);
BB_SIZE(SprjEmkCompletionEvent, SprjEmkCompletionEvent::SIZE);
static_assert(alignof(SprjEmkCompletionEvent) == 16, "alignof(SprjEmkCompletionEvent)");
BB_OFFSET(SprjEmkCompletionEvent, flag_78, 0x78);
BB_OFFSET(SprjEmkCompletionEvent, delay_ticks, 0x7c);

BB_SIZE(SprjEmkEventDescriptor, 0x10);
BB_OFFSET(SprjEmkEventDescriptor, event, 8);
BB_SIZE(SprjEmkEventRecord, 0x30);
BB_OFFSET(SprjEmkEventRecord, instruction_count, 8);
BB_OFFSET(SprjEmkEventRecord, instructions_offset, 0x10);
BB_OFFSET(SprjEmkEventRecord, argument_replacement_count, 0x18);
BB_OFFSET(SprjEmkEventRecord, argument_replacements_offset, 0x20);
BB_OFFSET(SprjEmkEventRecord, behavior_code, 0x28);
BB_SIZE(SprjEmkInstructionRecord, 0x20);
BB_OFFSET(SprjEmkInstructionRecord, instruction_id, 4);
BB_OFFSET(SprjEmkInstructionRecord, argument_size, 8);
BB_OFFSET(SprjEmkInstructionRecord, arguments_offset, 0x10);

BB_SIZE(SprjEmkSystemUpdateTask, 0xd0);
BB_SIZE(SprjEmkSystemUpdateTask, SprjEmkSystemUpdateTask::SIZE);
BB_OFFSET(SprjEmkSystemUpdateTask, current_step, 0x58);
BB_OFFSET(SprjEmkSystemUpdateTask, requested_step, 0x5c);
BB_OFFSET(SprjEmkSystemUpdateTask, flag_60, 0x60);
BB_SIZE(SprjEmkSystem, SprjEmkSystem::SIZE);
static_assert(SprjEmkSystem::SINGLETON_PTR.bn() == 0x0593b0c0, "SINGLETON_PTR");
}  // namespace detail::emk_system_layout

}  // namespace bb
