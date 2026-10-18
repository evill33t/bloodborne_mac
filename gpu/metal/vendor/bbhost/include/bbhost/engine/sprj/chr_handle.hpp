// Packed character handle used to resolve ChrIns pointers through ChrSets.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t CHR_HANDLE_SIZE = 0x04;

// Packed Bloodborne character handle: ChrSet selector in bits 14..19, entry
// index in bits 0..13 (the 1.09 lookup path).
struct ChrHandle {
    std::uint32_t value;

    // Value the game uses to represent no character.
    static const ChrHandle NONE;

    static constexpr ChrHandle from_raw(std::uint32_t raw) { return ChrHandle{raw}; }
    static constexpr ChrHandle make(std::uint32_t selector, std::uint32_t index) {
        return ChrHandle{((selector & 0x3f) << 14) | (index & 0x3fff)};
    }
    constexpr std::uint32_t into_inner() const { return value; }
    constexpr bool is_empty() const { return value == 0xffffffffu; }
    constexpr std::size_t selector() const { return (value >> 14) & 0x3f; }
    constexpr std::size_t index() const { return value & 0x3fff; }
    constexpr bool operator==(ChrHandle o) const { return value == o.value; }
    constexpr bool operator!=(ChrHandle o) const { return value != o.value; }
};
inline constexpr ChrHandle ChrHandle::NONE{0xffffffffu};

namespace detail::chr_handle_layout {
BB_SIZE(ChrHandle, CHR_HANDLE_SIZE);
static_assert(ChrHandle::make(0x35, 0x2345).into_inner() == 0xd6345, "ChrHandle packing");
}  // namespace detail::chr_handle_layout

}  // namespace bb
