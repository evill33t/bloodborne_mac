// SprjEventFlagMan's flag store as our source (docs/decomp.md): the four
// functions every event script, Lua binding, talk script and online update
// reads and writes the world's state through - IsEventFlag (0x17cfc00),
// SetEventFlag (0x17cfcc0), GetEventFlagValue (0x17cfd80),
// SetEventFlagValue (0x17d0060), named by the game's Lua bindings - in the
// game's place as leaves (decomp/decomp.h).
//
// The store: the manager (singleton slot 0x593b100) splits flag ids into
// blocks of `block size` (+0x1c) flags; a block lives in an ordered map at
// +0x38 (std::map nodes: left +0, right +0x10, is-nil +0x19, block number
// +0x20, storage kind +0x28, storage +0x30). Kind 2 stores a pointer; kind
// 1 an index into a pool (+0x28) of `stride` (+0x20) bytes each, the offset a
// 32-bit product. A flag is a bit of its block, the first flag of each byte
// its most significant bit.
//
// A value (GetEventFlagValue/SetEventFlagValue) is `count` consecutive flags
// with the last one as bit 0. A range that reaches the end of its block goes
// on at the start of the next: the value's low bits in the first block's
// tail, the rest (the value shifted down by that many) at the next block's
// head. Shift counts and sums are 32-bit x86 arithmetic, as the game's code
// gets them. Where the game would read or write through a block that does not
// exist (a range running into a missing block), ours reads zeros and writes
// nothing; where it would divide by a zero block size, ours returns.
#pragma once

#include <cstddef>
#include <cstdint>

namespace sprj_event_flag {

constexpr std::uint64_t kBlockSize = 0x1c;   // u32: flags a block
constexpr std::uint64_t kPoolStride = 0x20;  // u32: bytes a pooled block
constexpr std::uint64_t kPool = 0x28;        // u64
constexpr std::uint64_t kMap = 0x38;         // the map's head node

inline std::uint8_t mask_of(std::uint32_t bit) { return static_cast<std::uint8_t>(0x80u >> (bit & 7)); }

// The block's storage, or 0 when the map has no such block (or a storage
// kind the game does not read). `load(at, &v)` reads a u8/u32/u64 of the
// game's memory and says whether it could.
template <class Load>
std::uint64_t block_data(std::uint64_t man, std::uint32_t block, const Load& load) {
    std::uint64_t head = 0, node = 0;
    if (!load(man + kMap, &head) || !load(head + 8, &node)) return 0;
    // lower_bound over the tree, as the game walks it; a red-black tree of any
    // size the game keeps is far shallower than the bound.
    std::uint64_t found = head;
    for (int depth = 0; depth < 64; ++depth) {
        std::uint8_t nil = 1;
        std::uint32_t key = 0;
        if (!load(node + 0x19, &nil) || nil || !load(node + 0x20, &key)) break;
        if (key < block) {
            if (!load(node + 0x10, &node)) return 0;
        } else {
            found = node;
            if (!load(node, &node)) return 0;
        }
    }
    std::uint32_t key = 0, kind = 0;
    if (found == head || !load(found + 0x20, &key) || key > block || !load(found + 0x28, &kind)) return 0;
    if (kind == 2) {
        std::uint64_t data = 0;
        return load(found + 0x30, &data) ? data : 0;
    }
    if (kind == 1) {
        std::uint32_t stride = 0, index = 0;
        std::uint64_t pool = 0;
        if (!load(man + kPoolStride, &stride) || !load(found + 0x30, &index) || !load(man + kPool, &pool)) return 0;
        return pool + static_cast<std::uint32_t>(stride * index);
    }
    return 0;
}

// Where flag `id` lives: its byte and mask, false when its block does not.
template <class Load>
bool locate(std::uint64_t man, std::uint32_t id, const Load& load, std::uint64_t* byte, std::uint8_t* mask) {
    std::uint32_t size = 0;
    if (!load(man + kBlockSize, &size) || !size) return false;
    const std::uint32_t block = id / size, bit = id - block * size;
    const std::uint64_t data = block_data(man, block, load);
    if (!data) return false;
    *byte = data + (bit >> 3);
    *mask = mask_of(bit);
    return true;
}

}  // namespace sprj_event_flag

// The game's own GetEventFlagValue while ours has its place (the
// trampoline), else nullptr: its entry is then still the game's.
void* sprj_event_flag_game_get_value();

// Flag changes, for mods: every flag whose bit a setter flips - the game's
// setters while ours run them (or while a compare run watches them), and our
// own writer - kept in a ring while watching is on and taken on the main
// thread. A save's flags loading are not changes.
struct EventFlagChange {
    std::uint32_t id;
    std::uint32_t value;
};
void event_flag_watch(bool on);
bool event_flag_watching();
// Leaf-safe: no lock, no thread-local.
void event_flag_changed(std::uint32_t id, bool value);
// Up to n changes since the last call, oldest first; `lost` gets the running
// count of changes the ring had to drop.
std::size_t event_flag_take_changes(EventFlagChange* out, std::size_t n, std::uint64_t* lost);
