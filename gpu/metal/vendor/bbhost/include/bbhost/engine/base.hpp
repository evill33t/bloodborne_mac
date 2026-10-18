// Bloodborne 1.09 engine layouts for plugins: the shared pieces every
// engine header uses. C++17, header-only, no host dependency.
//
// The game runs natively in bbhost's process, so a pointer read out of a game
// object is an ordinary pointer: these structs are laid over the game's own
// memory (`auto* w = reinterpret_cast<bb::WorldChrMan*>(...)`) and read or
// written directly - from the game's main thread (BbHostApi::on_frame), the
// only place its objects hold still.
//
// Addresses are image-relative (Rva): the eboot's virtual offset from its
// base, independent of where it loads. `.bn()` is the Binary Ninja address of
// this repository's notes and the plugin API (base 0x400000);
// BbHostApi::guest_addr(rva.bn()) is the runtime address.
//
// Field names, comments and evidence come from independent reverse-engineering
// of the 1.09 eboot; every size and offset it established is a static_assert
// here, so a layout that does not compile to the game's numbers fails the
// build.
#pragma once

#include <cstddef>
#include <cstdint>

namespace bb {

// Binary Ninja's base for the 1.09 eboot (guest VA = ELF VA + 0x400000).
inline constexpr std::uint64_t kImageBase = 0x400000;

// An image-relative virtual offset.
struct Rva {
    std::uint64_t rva;
    constexpr std::uint64_t bn() const { return kImageBase + rva; }
    constexpr bool operator==(Rva o) const { return rva == o.rva; }
};

// The last byte of the observed image (an inventory bound, not a promise
// that every offset is mapped).
inline constexpr Rva MIN_ADDRESS{0};
inline constexpr Rva MAX_ADDRESS{0x56d55e7};

// The calling convention of the game's own functions (the PS4's SysV ABI):
// on Windows a function pointer to game code must say so. A plugin does not
// call game code through these pointers directly - the host must switch to
// the guest's thread state first (BbHostApi::call_guest); the typedefs are
// what the functions take.
#if defined(_WIN32)
#define BB_GAME_ABI __attribute__((sysv_abi))
#else
#define BB_GAME_ABI
#endif

// Bytes whose meaning is not established. Kept as arrays so the struct's
// size and offsets match the game's.
template <std::size_t N>
struct Unknown {
    std::uint8_t bytes[N];
};

// The VC2012-era vector the engine's containers embed: three pointers and an
// allocator, 0x20 bytes.
template <typename T>
struct DLVector {
    T* first;
    T* last;
    T* end;
    void* allocator;

    std::size_t size() const { return first ? static_cast<std::size_t>(last - first) : 0; }
    std::size_t capacity() const { return first ? static_cast<std::size_t>(end - first) : 0; }
    bool empty() const { return size() == 0; }
    T* begin() const { return first; }
    T* stop() const { return first ? last : first; }
    T& operator[](std::size_t i) const { return first[i]; }
};
static_assert(sizeof(DLVector<int>) == 0x20);

// A typed view of `offset` bytes into an object, for layouts known only by
// offset (the *_OFFSET constants).
template <typename T>
inline T& at(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset);
}
template <typename T>
inline const T& at(const void* base, std::size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const std::uint8_t*>(base) + offset);
}

}  // namespace bb

// Checks a member's offset and a type's size at compile time.
#define BB_OFFSET(type, member, off) static_assert(offsetof(type, member) == (off), #type "::" #member)
#define BB_SIZE(type, n) static_assert(sizeof(type) == (n), "sizeof(" #type ")")
