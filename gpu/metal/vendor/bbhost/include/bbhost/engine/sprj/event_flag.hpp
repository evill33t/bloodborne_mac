// Event flags: SprjEventFlagMan's FD4 virtual-memory bit store and SprjEventState.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t EVENT_FLAG_SIZE = 0x04;
inline constexpr std::size_t SPRJ_EVENT_FLAG_MAN_VERIFIED_PREFIX_SIZE = 0x84;
inline constexpr std::size_t FD4_VIRTUAL_MEMORY_FLAG_PREFIX_SIZE = 0x40;
inline constexpr std::size_t FD4_VIRTUAL_MEMORY_FLAG_BLOCK_NODE_SIZE = 0x38;
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SINGLETON{0x553b100};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SET_LOAD_MODE{0x13bb590};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_READ_RANGE{0x13cfd80};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_CLEAR_BIT_RANGE{0x13d0430};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_READ_BIT{0x13cfc00};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_WRITE_BIT{0x13cfcc0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_WRITE_RANGE{0x13d0060};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_RESOLVE_GROUP_KEY{0x13bc710};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_SERIALIZE_SHARED_SNAPSHOT{0x13be3c0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_APPEND_FLAG_GROUP{0x13beac0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_APPLY_SNAPSHOT{0x13beca0};
inline constexpr Rva SPRJ_EVENT_FLAG_MAN_BROADCAST_SET_FLAG{0x132aad0};
inline constexpr Rva SPRJ_EVENT_STATE_SINGLETON{0x553b0d8};
inline constexpr Rva SPRJ_EVENT_STATE_SERIALIZE{0x1399de0};
inline constexpr std::uint32_t EVENT_FLAG_BITS_PER_LOGICAL_BLOCK = 1000;
inline constexpr std::uint32_t MAP_RUNTIME_EVENT_FLAG_ZONE = 4;

// A map's 1,000-bit zone-four logical event-flag block, or false when the map
// has none. Packed map IDs use the area in bits 24..31 and block in bits
// 16..23. For example, M24_00 (0x18000000) owns logical block 12404, which
// holds transient boss state such as 12404800 and 12404802, but also
// post-cutscene presentation flags such as 12404223: this bank must not be
// cleared wholesale when resetting a retained boss map.
inline constexpr bool map_runtime_event_flag_block(std::uint32_t map_id, std::uint32_t* block_out) {
    std::uint32_t area = (map_id >> 24) & 0xff;
    std::uint32_t block = (map_id >> 16) & 0xff;
    if (!(area < 90 && block < 10)) return false;
    *block_out = 10000 + area * 100 + block * 10 + MAP_RUNTIME_EVENT_FLAG_ZONE;
    return true;
}

// Why a raw ID is not a valid EventFlag.
enum class EventFlagError : std::uint8_t {
    None = 0,
    TooHigh,      // above the maximum value 99999999
    InvalidArea,  // area number must be less than 90
};

// A handle pointing to a one-bit event flag in the game's event storage.
struct EventFlag {
    std::uint32_t value;

    // The raw decimal event-flag ID.
    constexpr std::uint32_t id() const { return value; }
    // The index of this event's world/region group.
    constexpr std::uint8_t region() const { return static_cast<std::uint8_t>((value / 10000000) % 10); }
    // The world area component.
    constexpr std::uint8_t area() const { return static_cast<std::uint8_t>((value / 100000) % 100); }
    // The world block group component.
    constexpr std::uint8_t group() const { return static_cast<std::uint8_t>((value / 10000) % 10); }
    // The zone component.
    constexpr std::uint8_t zone() const { return static_cast<std::uint8_t>((value / 1000) % 10); }
    // The 32-bit word component used by DS3/Sekiro-style event storage.
    constexpr std::uint8_t word() const { return static_cast<std::uint8_t>((value % 1000) / 32); }
    // The bit within a 32-bit word, matching DS3/Sekiro bindings.
    constexpr std::uint8_t bit() const { return static_cast<std::uint8_t>(31 - ((value % 1000) % 32)); }

    // None when `raw` is a valid flag.
    static constexpr EventFlagError validate(std::uint32_t raw) {
        if (raw > 99999999) return EventFlagError::TooHigh;
        if (EventFlag{raw}.area() >= 90) return EventFlagError::InvalidArea;
        return EventFlagError::None;
    }
    static constexpr bool try_from(std::uint32_t raw, EventFlag* out) {
        if (validate(raw) != EventFlagError::None) return false;
        *out = EventFlag{raw};
        return true;
    }
};

// Node in the block lookup tree used by FD4VirtualMemoryFlag.
struct FD4VirtualMemoryFlagBlockNode {
    FD4VirtualMemoryFlagBlockNode* left;
    FD4VirtualMemoryFlagBlockNode* root_or_parent;
    FD4VirtualMemoryFlagBlockNode* right;
    std::uint8_t _unk18;
    bool is_sentinel;
    std::uint8_t _pad1a[6];
    std::uint32_t block_index;
    std::uint32_t _pad24;
    std::int32_t storage_kind;
    std::uint32_t _pad2c;
    std::uintptr_t storage_index_or_ptr;

    // The block's bytes: kind 1 indexes block_storage by stride, kind 2 holds a
    // direct pointer. Null for any other kind or overflow.
    std::uint8_t* data(std::uint8_t* block_storage, std::uint32_t block_stride) const {
        switch (storage_kind) {
        case 1: {
            std::uint64_t offset = std::uint64_t(block_stride) * std::uint32_t(storage_index_or_ptr);
            if (offset > 0xffffffffu) return nullptr;
            return block_storage + offset;
        }
        case 2: return reinterpret_cast<std::uint8_t*>(storage_index_or_ptr);
        default: return nullptr;
        }
    }
};

// Bloodborne's FD4 virtual-memory flag store prefix. The 1.09 accessors at RVA
// 0x13cfc00, 0x13cfcc0, 0x13cfd80 and 0x13d0060 all use this prefix: +0x1c
// selects the number of bits per block, +0x20/+0x28 resolve indirect block
// storage, and +0x38 points to the block tree.
struct FD4VirtualMemoryFlag {
    Unknown<0x1c> _unk00;
    std::uint32_t bits_per_block;
    std::uint32_t block_stride;
    std::uint32_t _pad24;
    std::uint8_t* block_storage;
    std::uint64_t _unk30;
    FD4VirtualMemoryFlagBlockNode* block_tree;

    // Reads one bit using Bloodborne's observed byte/bit orientation. False
    // when the bit's block is not mapped.
    bool get_bit(std::uint32_t bit, bool* out) const {
        std::uint32_t bit_in_block;
        std::uint8_t* data = block_data(bit, &bit_in_block);
        if (!data) return false;
        *out = (data[bit_in_block >> 3] & bit_mask(bit_in_block)) != 0;
        return true;
    }

    // Writes one bit using Bloodborne's observed byte/bit orientation.
    bool set_bit(std::uint32_t bit, bool state) {
        std::uint32_t bit_in_block;
        std::uint8_t* data = block_data(bit, &bit_in_block);
        if (!data) return false;
        std::uint8_t& byte = data[bit_in_block >> 3];
        std::uint8_t mask = bit_mask(bit_in_block);
        byte = state ? std::uint8_t(byte | mask) : std::uint8_t(byte & ~mask);
        return true;
    }

    // Reads up to 32 bits with the orientation of GetEventFlagValue (RVA 0x13cfd80).
    bool get_bits(std::uint32_t bit, std::uint32_t width, std::uint32_t* out) const {
        if (width > 32) return false;
        std::uint32_t value = 0;
        for (std::uint32_t index = 0; index < width; ++index) {
            std::uint32_t add = width - 1 - index;
            if (bit > 0xffffffffu - add) return false;
            bool b;
            if (!get_bit(bit + add, &b)) return false;
            if (b) value |= 1u << index;
        }
        *out = value;
        return true;
    }

    // Writes up to 32 bits with the orientation of SetEventFlagValue (RVA 0x13d0060).
    bool set_bits(std::uint32_t bit, std::uint32_t width, std::uint32_t value) {
        if (width > 32) return false;
        for (std::uint32_t index = 0; index < width; ++index) {
            std::uint32_t add = width - 1 - index;
            if (bit > 0xffffffffu - add) return false;
            if (!set_bit(bit + add, ((value >> index) & 1) != 0)) return false;
        }
        return true;
    }

    std::uint8_t* block_data(std::uint32_t bit, std::uint32_t* bit_in_block) const {
        if (bits_per_block == 0) return nullptr;
        std::uint32_t block_index = bit / bits_per_block;
        *bit_in_block = bit - block_index * bits_per_block;
        const FD4VirtualMemoryFlagBlockNode* node = block_node(block_index);
        return node ? node->data(block_storage, block_stride) : nullptr;
    }

    const FD4VirtualMemoryFlagBlockNode* block_node(std::uint32_t block_index) const {
        if (!block_tree) return nullptr;
        const FD4VirtualMemoryFlagBlockNode* candidate = nullptr;
        const FD4VirtualMemoryFlagBlockNode* cursor = block_tree->root_or_parent;
        while (cursor && !cursor->is_sentinel) {
            if (block_index <= cursor->block_index) {
                candidate = cursor;
                cursor = cursor->left;
            } else {
                cursor = cursor->right;
            }
        }
        return candidate && candidate->block_index <= block_index ? candidate : nullptr;
    }

    static std::uint8_t bit_mask(std::uint32_t bit_in_block) {
        return std::uint8_t(1u << (~std::uint8_t(bit_in_block) & 7));
    }
};

// Singleton named SprjEventFlagMan.
struct SprjEventFlagMan {
    FD4VirtualMemoryFlag flags;
    Unknown<0x40> _unknown_40;
    std::int32_t load_mode;

    // Reads a single event flag from the FD4 virtual-memory bit store.
    bool get_flag(EventFlag flag, bool* out) const { return flags.get_bit(flag.id(), out); }
    // Writes a single event flag.
    bool set_flag(EventFlag flag, bool state) { return flags.set_bit(flag.id(), state); }
    // Reads a multi-bit event flag value.
    bool get_flag_value(EventFlag flag, std::uint32_t width, std::uint32_t* out) const {
        return flags.get_bits(flag.id(), width, out);
    }
    // Writes a multi-bit event flag value.
    bool set_flag_value(EventFlag flag, std::uint32_t width, std::uint32_t value) {
        return flags.set_bits(flag.id(), width, value);
    }
};

// Non-polymorphic event-state singleton, allocated as 0x28 bytes aligned to
// eight by RVA 0x156c6b0. Its first eight bytes are a zero state code and an
// initially -1 map identifier (not a vtable).
//
// SERIALIZE_FN sets serialization_started before attempting any stream write.
// It writes the event-flag backing storage, then exactly one byte from +0x26.
// It does not serialize this object wholesale or persist alive_motion.
// DESERIALIZE_FN resets state_code, map_id, serialization_started, and
// reset_on_load_27 before loading flags and the +0x26 byte. A failed operation
// can leave partial changes.
//
// Construction also creates SprjDbgEvent if absent. Native destruction frees
// that singleton before the startup owner frees this object.
struct alignas(8) SprjEventState {
    static constexpr std::size_t SIZE = 0x28;
    static constexpr Rva SINGLETON_PTR = SPRJ_EVENT_STATE_SINGLETON;
    static constexpr Rva NAME_STRING{0x493b1c2};
    static constexpr Rva CONSTRUCTOR_FN{0x13999f0};
    static constexpr Rva DESTRUCTOR_FN{0x1399b50};
    static constexpr Rva STARTUP_OWNER_CONSTRUCTOR_FN{0x156c6b0};
    static constexpr Rva STARTUP_OWNER_DESTRUCTOR_FN{0x156ca40};
    static constexpr Rva SET_STATE_FN{0x1399da0};
    static constexpr Rva TAKE_STATE_FN{0x1399dc0};
    static constexpr Rva SET_MAP_ID_FN{0x1399dd0};
    static constexpr Rva SERIALIZE_FN = SPRJ_EVENT_STATE_SERIALIZE;
    static constexpr Rva DESERIALIZE_FN{0x1399e90};
    static constexpr Rva SET_ALIVE_MOTION_FN{0x131c6d0};
    static constexpr Rva GET_ALIVE_MOTION_FN{0x131c6f0};
    static constexpr Rva SET_REVIVE_WAIT_FN{0x131c710};
    static constexpr Rva GET_REVIVE_WAIT_FN{0x131c730};
    // Lua wrappers additionally require the Lua manager to exist. Registration
    // at RVA 0x1340f1d..0x1341024 binds the four names below to these wrappers.
    static constexpr Rva LUA_SET_ALIVE_MOTION_FN{0x1333d00};
    static constexpr Rva LUA_IS_ALIVE_MOTION_FN{0x1333d30};
    static constexpr Rva LUA_SET_REVIVE_WAIT_FN{0x1333d60};
    static constexpr Rva LUA_IS_REVIVE_WAIT_FN{0x1333d90};
    static constexpr Rva SET_ALIVE_MOTION_NAME{0x492d2a5};
    static constexpr Rva IS_ALIVE_MOTION_NAME{0x492d2b4};
    static constexpr Rva SET_REVIVE_WAIT_NAME{0x492d2c2};
    static constexpr Rva IS_REVIVE_WAIT_NAME{0x492d2d0};

    // SET_STATE_FN accepts 0, 1, 2; every other unsigned input writes zero and
    // returns false. TAKE_STATE_FN returns the raw word and clears it.
    std::uint32_t state_code;
    // Filled from WorldResLoadContext's selected WorldBlockInfo +0x08.
    // Initialized -1 and restored to -1 before deserialization.
    std::int32_t map_id;
    // Constructor stores zero here; meaning remains unclassified.
    std::uint32_t word_08;
    Unknown<4> _unk0c;
    // Constructor zeros +0x10..+0x28; the interior fields remain unclassified.
    Unknown<0x14> _unk10;
    // SetAliveMotion / IsAliveMotion; reads treat any nonzero as true.
    std::uint8_t alive_motion;
    // Set before serialization starts, not a success/completion indication.
    std::uint8_t serialization_started;
    // SetReviveWait / IsReviveWait. Saved as one byte after event-flag storage.
    std::uint8_t revive_wait;
    std::uint8_t reset_on_load_27;
};

namespace detail::event_flag_layout {
BB_SIZE(EventFlag, EVENT_FLAG_SIZE);
static_assert(offsetof(SprjEventFlagMan, load_mode) + sizeof(std::int32_t) == SPRJ_EVENT_FLAG_MAN_VERIFIED_PREFIX_SIZE,
              "SprjEventFlagMan verified prefix");
BB_SIZE(SprjEventFlagMan, 0x88);
BB_SIZE(FD4VirtualMemoryFlag, FD4_VIRTUAL_MEMORY_FLAG_PREFIX_SIZE);
BB_SIZE(FD4VirtualMemoryFlagBlockNode, FD4_VIRTUAL_MEMORY_FLAG_BLOCK_NODE_SIZE);
BB_OFFSET(SprjEventFlagMan, flags, 0x00);
BB_OFFSET(SprjEventFlagMan, load_mode, 0x80);
BB_OFFSET(FD4VirtualMemoryFlag, bits_per_block, 0x1c);
BB_OFFSET(FD4VirtualMemoryFlag, block_stride, 0x20);
BB_OFFSET(FD4VirtualMemoryFlag, block_storage, 0x28);
BB_OFFSET(FD4VirtualMemoryFlag, block_tree, 0x38);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, left, 0x00);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, root_or_parent, 0x08);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, right, 0x10);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, is_sentinel, 0x19);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, block_index, 0x20);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, storage_kind, 0x28);
BB_OFFSET(FD4VirtualMemoryFlagBlockNode, storage_index_or_ptr, 0x30);
BB_SIZE(SprjEventState, 0x28);
BB_SIZE(SprjEventState, SprjEventState::SIZE);
static_assert(alignof(SprjEventState) == 8, "alignof(SprjEventState)");
BB_OFFSET(SprjEventState, state_code, 0);
BB_OFFSET(SprjEventState, map_id, 4);
BB_OFFSET(SprjEventState, word_08, 8);
BB_OFFSET(SprjEventState, alive_motion, 0x24);
BB_OFFSET(SprjEventState, serialization_started, 0x25);
BB_OFFSET(SprjEventState, revive_wait, 0x26);
BB_OFFSET(SprjEventState, reset_on_load_27, 0x27);
static_assert(EventFlag{12345678}.area() == 23 && EventFlag{12345678}.bit() == 25, "EventFlag components");
}  // namespace detail::event_flag_layout

}  // namespace bb
