// Native DLC discovery, debug overrides, published flags, and map variants.
//
// CSDlc is the asserted singleton. The CSDlcBackingData name and supporting
// record names are descriptive; separate reflection is unproven.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CSDLC_SIZE = 0x80;
inline constexpr std::size_t CSDLC_BACKING_DATA_SIZE = 0x390;
inline constexpr std::size_t CSDLC_FLAG_COUNT = 100;

inline constexpr std::size_t CSDLC_BACKING_DATA_OFFSET = 0x08;
inline constexpr std::size_t CSDLC_FLAGS_OFFSET = 0x10;
inline constexpr std::size_t CSDLC_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET = 0x78;

// Eight-byte debug row at backing +0x6c + index*8. The first word initializes
// to -1; its further use is unproven. The mode initializes to Default (0).
struct CSDlcDebugOverride {
    std::int32_t value_00;
    // Kept raw so unexpected native values remain representable. Only value 2
    // forces true; all other nonzero modes force false.
    std::int32_t mode;
};

// Labels verified at RVAs 0x49b75d4, 0x49b75e8, and 0x49b75fa.
enum class CSDlcOverrideMode : std::int32_t {
    Default = 0,
    Locked = 1,
    Unlocked = 2,
};

// Exact 0x390-byte implementation allocated aligned to eight. Refresh clears
// detected_flags, calls sceAppContentGetAddcontInfoList twice (count/data), and
// sets flags from each label's final two decimal digits. It does not read the
// platform records' status fields; query failure leaves the cleared flags.
// Initialization builds 100 debug rows; no task/thread in this object. Its
// native non-deleting destructor is a no-op; the owner destructor does not free
// this allocation.
struct CSDlcBackingData {
    static constexpr std::size_t SIZE = CSDLC_BACKING_DATA_SIZE;
    static constexpr Rva VTABLE{0x5358280};
    static constexpr Rva CONSTRUCTOR_FN{0x1fb6150};
    static constexpr Rva DESTRUCTOR_FN{0x1fb6210};
    static constexpr Rva INITIALIZE_DEBUG_FN{0x1fb6220};
    static constexpr Rva EDIT_OVERRIDE_FN{0x1fb6490};
    static constexpr Rva DEBUG_LABEL_FN{0x1fb6510};
    static constexpr Rva REFRESH_FN{0x1fb6730};
    static constexpr Rva RESOLVED_FLAG_FN{0x1fb68b0};
    static constexpr std::size_t ADDCONT_INFO_STRIDE = 0x18;

    const void* vftable;
    std::uint8_t detected_flags[CSDLC_FLAG_COUNT];
    CSDlcDebugOverride overrides[CSDLC_FLAG_COUNT];
    Unknown<4> _unk38c;

    // Snapshot of the native override precedence. False on a bounds failure
    // (native returns false); no platform query or owner update occurs.
    bool resolved_flag(std::size_t index, bool& out) const {
        if (index >= CSDLC_FLAG_COUNT) return false;
        const CSDlcDebugOverride& row = overrides[index];
        out = row.mode == 0 ? detected_flags[index] != 0 : row.mode == 2;
        return true;
    }

    // Native final-two-ASCII-digits policy over a NUL-terminated label. Prefix
    // bytes and platform status are not examined.
    static bool flag_index_from_label(const char* label, std::size_t& out) {
        std::size_t n = 0;
        while (label[n] != '\0') ++n;
        if (n < 2) return false;
        const unsigned char a = static_cast<unsigned char>(label[n - 2]);
        const unsigned char b = static_cast<unsigned char>(label[n - 1]);
        if (a < '0' || a > '9' || b < '0' || b > '9') return false;
        out = static_cast<std::size_t>(a - '0') * 10 + static_cast<std::size_t>(b - '0');
        return true;
    }
};

// Native 12-byte table entry. Components compare to packed map-ID bytes
// 24..31, 16..23, and 8..15 respectively; the last byte is the variant.
struct CSDlcMapVariantKey {
    std::uint32_t components[3];
};

// Exact 15-entry table copied from RVA 0x4735e50. Integer map components are
// native data, not addresses. All four path wrappers use this same table.
inline constexpr CSDlcMapVariantKey CSDLC_MAP_VARIANT_KEYS[15] = {
    {{23, 0, 0}},   {{24, 0, 0}},   {{24, 1, 0}},   {{24, 2, 0}},   {{27, 0, 0}},
    {{28, 0, 0}},   {{32, 0, 0}},   {{29, 10, 90}}, {{29, 20, 90}}, {{29, 30, 90}},
    {{29, 40, 90}}, {{29, 42, 90}}, {{29, 50, 90}}, {{29, 52, 90}}, {{29, 53, 90}},
};

// Exact 0x80-byte owner. flags is a published snapshot, refreshed explicitly
// from the implementation and exported to event flags 6900..6999. Editing an
// override does not itself update this snapshot. Destructor removes only the
// debug menu.
struct CSDlc {
    static constexpr std::size_t SIZE = CSDLC_SIZE;
    // Ghidra/BN VA 0x059403d0.
    static constexpr Rva SINGLETON_PTR = CSDLC_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493ad2b};
    static constexpr Rva VTABLE{0x5358200};
    static constexpr Rva CONSTRUCTOR_FN{0x1fb5060};
    static constexpr Rva DESTRUCTOR_FN{0x1fb5450};
    static constexpr Rva INITIALIZE_FN{0x1fb54a0};
    static constexpr Rva REFRESH_AND_PUBLISH_FN{0x1fb54d0};
    static constexpr Rva COMBINED_FLAG_FN{0x1fb5620};
    static constexpr Rva FLAG_FN{0x1fb5660};
    static constexpr Rva MAPSTUDIO_PATH_FN{0x1fb5680};
    static constexpr Rva MAPSTUDIO_ALT_PATH_FN{0x1fb5730};
    static constexpr Rva BREAKOBJ_PATH_FN{0x1fb57e0};
    static constexpr Rva CAP_BREAKOBJ_PATH_FN{0x1fb5890};
    static constexpr Rva MAP_VARIANT_TABLE{0x4735e50};
    // CSLocalize word read only when flags[3] is zero. Its native domain is
    // 0/1; an edition/region interpretation is not established.
    static constexpr Rva LOCALIZE_MODE_GLOBAL{0x512894c};
    static constexpr std::uint32_t FIRST_EVENT_FLAG = 6900;
    static constexpr std::uint32_t COMBINED_EVENT_FLAG = 6899;

    void* vftable;
    CSDlcBackingData* backing_data;
    std::uint8_t flags[CSDLC_FLAG_COUNT];
    std::uint32_t _unk74;
    void* primal_system_debug_menu;

    // False when index is out of range.
    bool flag(std::size_t index) const { return index < CSDLC_FLAG_COUNT && flags[index] != 0; }
    // Edits only this memory view; does not publish to SprjEventFlagMan or
    // change the implementation's detected flags/override table.
    bool set_flag(std::size_t index, bool value) {
        if (index >= CSDLC_FLAG_COUNT) return false;
        flags[index] = value ? 1 : 0;
        return true;
    }
    static constexpr bool event_flag_id(std::size_t index, std::uint32_t& out) {
        if (index >= CSDLC_FLAG_COUNT) return false;
        out = FIRST_EVENT_FLAG + static_cast<std::uint32_t>(index);
        return true;
    }
    // Pure snapshot of the event-flag-6899 predicate. Returns false for the
    // native invalid-mode assertion, which is bypassed when flags[3] is set.
    bool combined_flag(std::uint32_t localize_mode, bool& out) const {
        if (flags[3] != 0) {
            out = true;
            return true;
        }
        if (localize_mode > 1) return false;
        out = localize_mode == 1;
        return true;
    }
    // Pure map-ID rewrite performed by all four native path wrappers. A
    // matching three-byte key forces the last byte to 1, replacing any prior
    // variant. The wrappers themselves do not inspect entitlement flags.
    static constexpr std::uint32_t map_resource_id(std::uint32_t map_id) {
        const std::uint32_t a = map_id >> 24, b = (map_id >> 16) & 0xff, c = (map_id >> 8) & 0xff;
        for (const CSDlcMapVariantKey& key : CSDLC_MAP_VARIANT_KEYS) {
            if (key.components[0] == a && key.components[1] == b && key.components[2] == c)
                return (map_id & 0xffffff00u) | 1;
        }
        return map_id;
    }
};

namespace detail::dlc_layout {
BB_SIZE(CSDlc, CSDLC_SIZE);
static_assert(alignof(CSDlc) == 8, "alignof(CSDlc)");
BB_OFFSET(CSDlc, backing_data, CSDLC_BACKING_DATA_OFFSET);
BB_OFFSET(CSDlc, flags, CSDLC_FLAGS_OFFSET);
BB_OFFSET(CSDlc, primal_system_debug_menu, CSDLC_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET);
BB_SIZE(CSDlcBackingData, CSDLC_BACKING_DATA_SIZE);
static_assert(alignof(CSDlcBackingData) == 8, "alignof(CSDlcBackingData)");
BB_OFFSET(CSDlcBackingData, detected_flags, 8);
BB_OFFSET(CSDlcBackingData, overrides, 0x6c);
BB_OFFSET(CSDlcBackingData, _unk38c, 0x38c);
BB_SIZE(CSDlcDebugOverride, 8);
static_assert(alignof(CSDlcDebugOverride) == 4, "alignof(CSDlcDebugOverride)");
BB_OFFSET(CSDlcDebugOverride, mode, 4);
static_assert(offsetof(CSDlcBackingData, overrides) + offsetof(CSDlcDebugOverride, mode) == 0x70, "override mode");
BB_SIZE(CSDlcMapVariantKey, 0xc);
static_assert(alignof(CSDlcMapVariantKey) == 4, "alignof(CSDlcMapVariantKey)");
static_assert(sizeof(CSDLC_MAP_VARIANT_KEYS) == 0xb4, "sizeof(CSDLC_MAP_VARIANT_KEYS)");
static_assert(CSDlc::map_resource_id(0x1d355aff) == 0x1d355a01, "map_resource_id");
static_assert(CSDlc::SINGLETON_PTR.bn() == 0x059403d0, "CSDlc::SINGLETON_PTR");
}  // namespace detail::dlc_layout

}  // namespace bb
