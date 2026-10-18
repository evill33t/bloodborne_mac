// What every decomp source shares (docs/decomp.md): the integer names, the
// game's memory by address, and the game's functions and data by their
// Binary Ninja address. Everything here is usable from a leaf - it calls
// nothing of the host's - so a leaf and the hosted code around it read the
// game the same way.
#pragma once

#include "decomp/decomp.h"

#include <cstdint>
#include <cstring>

namespace decomp {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using ull = unsigned long long;  // for printf's %llx

// A Binary Ninja address as the guest sees it (the image may be slid).
inline u64 game_address(u64 bn) { return decomp_leaf_guest(bn); }

// The game's function at a Binary Ninja address, as F - a GUEST_ABI
// function pointer type with the game's signature.
template <class F>
inline F game_function(u64 bn) {
    return reinterpret_cast<F>(static_cast<std::uintptr_t>(game_address(bn)));
}

// A value of the game's memory at an address, or at an offset from a
// pointer - copied, so an address need not be aligned for T.
template <class T>
inline T load(u64 at) {
    T v;
    std::memcpy(&v, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(at)), sizeof v);
    return v;
}
template <class T>
inline T load(const void* base, u64 offset) {
    T v;
    std::memcpy(&v, static_cast<const u8*>(base) + offset, sizeof v);
    return v;
}
template <class T>
inline void store(u64 at, T v) {
    std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(at)), &v, sizeof v);
}

// The game's memory at an address as an object to read and write in place.
template <class T>
inline T& ref(u64 at) {
    return *reinterpret_cast<T*>(static_cast<std::uintptr_t>(at));
}

}  // namespace decomp
