// Native tendency (white/black) singleton, its event-flag values, and script/debug consumers.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Selector names follow the debug labels for white and black tendency, at RVAs
// 0x494f484 and 0x494f496. No world/player distinction is established.
enum class SprjTendencyKind : std::uint8_t {
    White = 0,
    Black = 1,
};

// Comparison codes used by the native method and event-condition consumer.
enum class SprjTendencyComparison : std::uint32_t {
    Equal = 0,
    NotEqual = 1,
    Greater = 2,
    Less = 3,
    GreaterOrEqual = 4,
    LessOrEqual = 5,
};

// Evaluates an already-read flag value, with the native operand truncation to
// its low byte. Neither reads event flags nor substitutes the manager's cache.
// Invalid comparison codes return false.
constexpr bool matches(SprjTendencyComparison comparison, std::uint32_t flag_value, std::uint32_t operand) {
    operand &= 0xff;
    switch (comparison) {
        case SprjTendencyComparison::Equal: return flag_value == operand;
        case SprjTendencyComparison::NotEqual: return flag_value != operand;
        case SprjTendencyComparison::Greater: return flag_value > operand;
        case SprjTendencyComparison::Less: return flag_value < operand;
        case SprjTendencyComparison::GreaterOrEqual: return flag_value >= operand;
        case SprjTendencyComparison::LessOrEqual: return flag_value <= operand;
    }
    return false;
}

// Native SprjTendencyMan, allocated as 0x10 bytes aligned to eight by RVA
// 0x156c6b0 and published at SINGLETON_PTR. Identity is established by
// singleton assertions, allocation, construction, and consumers. No vtable,
// and no reflected runtime-class registration is claimed.
//
// Construction initializes both cache bytes and cleanup_object to zero. The
// separate RESET_VALUES_FN writes 50 to both seven-bit event-flag ranges and
// both cache bytes. World-loading/transition paths invoke that reset.
//
// Comparisons and event deltas read flags directly, so these cache bytes need
// not reflect unrelated flag writes. The debug editor instead edits a cache
// byte and writes it back to flags; its display callback reads flags directly.
//
// Native destruction cleans up the optional +0x08 object and removes the
// Tendency debug root. Startup-owner destruction then frees the singleton and
// clears its pointer slot.
struct SprjTendencyMan {
    std::uint8_t white_cached;
    std::uint8_t black_cached;
    Unknown<6> _unk02;
    // Initialized null. If nonnull, destruction passes it to RVA 0x1001720
    // before clearing it. That helper calls object->owner(+0x08)->vtable+0x80.
    // Its concrete type and population path are unproven. The constructor's
    // debug-root result is not stored here.
    void* cleanup_object;

    static constexpr std::size_t SIZE = 0x10;
    static constexpr Rva SINGLETON_PTR = SPRJ_TENDENCY_MAN_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x49318bd};
    static constexpr Rva CONSTRUCTOR_FN{0x139cba0};
    static constexpr Rva DESTRUCTOR_FN{0x139cc60};
    static constexpr Rva STARTUP_OWNER_CONSTRUCTOR_FN{0x156c6b0};
    static constexpr Rva STARTUP_OWNER_DESTRUCTOR_FN{0x156ca40};
    // Reads the selected seven-bit flag value and compares against the
    // operand's low byte using SprjTendencyComparison. Invalid codes: false.
    static constexpr Rva COMPARE_FN{0x139ccc0};
    // Reads flags, adds the delta modulo 256, caps at 100, writes flags, and
    // refreshes the cache only for selectors zero or one. See event_delta_value.
    static constexpr Rva APPLY_EVENT_DELTA_FN{0x139cda0};
    static constexpr Rva RESET_VALUES_FN{0x139ce90};
    static constexpr Rva DEBUG_ROOT_NAME_FN{0x139cf30};
    // Edits the selected cache in response to debug input, then writes flags.
    // Has its own lower/upper clamp and optional endpoint wrap, unlike deltas.
    static constexpr Rva DEBUG_EDIT_FN{0x139cf40};
    static constexpr Rva DEBUG_DISPLAY_FN{0x139d140};
    // Allocates a menu row and two 0x28-byte bound callbacks per selector. The
    // callbacks store a borrowed manager pointer and a raw selector, and bind
    // DEBUG_EDIT_FN / DEBUG_DISPLAY_FN to menu event slots four/five.
    static constexpr Rva ADD_DEBUG_ROW_FN{0x139d550};
    static constexpr Rva WHITE_DEBUG_FORMAT{0x494f484};
    static constexpr Rva BLACK_DEBUG_FORMAT{0x494f496};
    // Event-condition record: selector +0x28, signed comparison byte +0x29,
    // operand +0x2a. Reads flags and replaces only result bit zero at +0x10.
    static constexpr Rva EVENT_CONDITION_UPDATE_FN{0x17b8330};
    // Bank 2003 instruction 29 supplies selector byte zero and signed delta
    // byte one from its argument data to APPLY_EVENT_DELTA_FN.
    static constexpr Rva EMEVD_BANK_2003_DISPATCH_FN{0x17c0420};
    static constexpr std::uint32_t EMEVD_DELTA_INSTRUCTION = 29;
    static constexpr Rva WORLD_LOAD_RESET_CALLER_FN{0x1938de0};
    static constexpr std::uint32_t FLAG_WIDTH = 7;
    static constexpr std::uint32_t WHITE_FLAG_START = 500;
    static constexpr std::uint32_t BLACK_FLAG_START = 550;
    static constexpr std::uint8_t RESET_VALUE = 50;
    static constexpr std::uint8_t MAX_UPDATED_VALUE = 100;

    // Native compare/delta paths select black for every nonzero selector,
    // including invalid ones. Invalid selectors do not update either cache in
    // APPLY_EVENT_DELTA_FN; debug edit/display accept only zero or one.
    static constexpr std::uint32_t native_flag_start(std::uint32_t raw_selector) {
        return raw_selector == 0 ? WHITE_FLAG_START : BLACK_FLAG_START;
    }

    // Pure arithmetic from APPLY_EVENT_DELTA_FN, given an already-read flag
    // value. EMEVD supplies a signed byte, but the native addition keeps only
    // its low byte and compares the wrapped result unsigned against 100, so
    // 0 + (-1) becomes 100, not zero. Not debug-edit logic.
    static constexpr std::uint8_t event_delta_value(std::uint8_t flag_value, std::int8_t delta) {
        const std::uint8_t wrapped =
            static_cast<std::uint8_t>(flag_value + static_cast<std::uint8_t>(delta));
        return wrapped > MAX_UPDATED_VALUE ? MAX_UPDATED_VALUE : wrapped;
    }
};

// First event flag of the selected tendency's seven-bit range.
constexpr std::uint32_t flag_start(SprjTendencyKind kind) {
    return SprjTendencyMan::native_flag_start(static_cast<std::uint32_t>(kind));
}

namespace detail::tendency_man_layout {
BB_SIZE(SprjTendencyMan, 0x10);
BB_SIZE(SprjTendencyMan, SprjTendencyMan::SIZE);
static_assert(alignof(SprjTendencyMan) == 8, "alignof(SprjTendencyMan)");
BB_OFFSET(SprjTendencyMan, white_cached, 0);
BB_OFFSET(SprjTendencyMan, black_cached, 1);
BB_OFFSET(SprjTendencyMan, cleanup_object, 8);
static_assert(flag_start(SprjTendencyKind::White) == 500 && flag_start(SprjTendencyKind::Black) == 550,
              "tendency flag ranges");
static_assert(SprjTendencyMan::event_delta_value(0, -1) == 100 && SprjTendencyMan::event_delta_value(50, -50) == 0,
              "tendency delta wraps before the clamp");
}  // namespace detail::tendency_man_layout

}  // namespace bb
