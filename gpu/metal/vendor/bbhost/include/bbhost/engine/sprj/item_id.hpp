// Item IDs: an EQUIP_PARAM_* category in the high nibble, the param row below.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t ITEM_ID_SIZE = 0x04;
inline constexpr std::size_t OPTIONAL_ITEM_ID_SIZE = 0x04;

// Why an item ID or category is invalid.
enum class ItemIdError : std::uint8_t {
    None = 0,
    InvalidCategory,  // the category high bits are not a known equipment table
    InvalidParamId,   // the parameter ID does not fit in the low 28 bits
};

// Item categories backed by Bloodborne EQUIP_PARAM_* tables.
enum class ItemCategory : std::uint8_t {
    Weapon = 0,
    Protector = 1,
    Accessory = 2,
    Goods = 4,
};

// False for a raw value that names no category.
inline constexpr bool item_category_from_raw(std::uint8_t value, ItemCategory* out) {
    switch (value) {
    case 0:
    case 1:
    case 2:
    case 4: *out = static_cast<ItemCategory>(value); return true;
    default: return false;
    }
}

// Like ItemCategory, but shifted into the high nibble of an item ID.
enum class ItemCategoryHigh : std::uint32_t {
    Weapon = 0,
    Protector = 0x10000000,
    Accessory = 0x20000000,
    Goods = 0x40000000,
};

inline constexpr ItemCategoryHigh to_high(ItemCategory c) {
    return static_cast<ItemCategoryHigh>(std::uint32_t(c) << 28);
}
inline constexpr ItemCategory to_low(ItemCategoryHigh c) {
    return static_cast<ItemCategory>(std::uint32_t(c) >> 28);
}

// An item ID that may be invalid or absent.
struct OptionalItemId {
    std::uint32_t value;

    // Value used by the game to represent no item.
    static const OptionalItemId NONE;

    // Normalizes an unknown category to NONE.
    static constexpr OptionalItemId from_raw(std::uint32_t raw) {
        OptionalItemId id{raw};
        return id.is_valid() ? id : OptionalItemId{0xffffffffu};
    }

    // Whether this contains a known item category.
    constexpr bool is_valid() const {
        ItemCategory c{};
        return item_category_from_raw(category_raw(), &c);
    }
    // Raw low 28-bit parameter ID.
    constexpr std::uint32_t param_id_raw() const { return value & 0x0fffffff; }
    // Raw high-nibble category.
    constexpr std::uint8_t category_raw() const { return static_cast<std::uint8_t>(value >> 28); }
    // Parameter ID, if this item ID is valid.
    constexpr bool param_id(std::uint32_t* out) const {
        if (!is_valid()) return false;
        *out = param_id_raw();
        return true;
    }
    // Category, if this item ID is valid.
    constexpr bool category(ItemCategory* out) const { return item_category_from_raw(category_raw(), out); }
    // The underlying raw value.
    constexpr std::uint32_t into_inner() const { return value; }
    constexpr bool operator==(OptionalItemId o) const { return value == o.value; }
    constexpr bool operator!=(OptionalItemId o) const { return value != o.value; }
};
inline constexpr OptionalItemId OptionalItemId::NONE{0xffffffffu};

// An item ID with a known Bloodborne equipment category.
struct ItemId {
    OptionalItemId id;

    // Creates an item ID from an equipment category and param row ID
    // (InvalidParamId when param_id does not fit in 28 bits).
    static constexpr ItemIdError make(ItemCategory category, std::uint32_t param_id, ItemId* out) {
        if (param_id > 0x0fffffff) return ItemIdError::InvalidParamId;
        *out = new_unchecked(category, param_id);
        return ItemIdError::None;
    }
    // Creates an item ID without checking that param_id fits in 28 bits.
    static constexpr ItemId new_unchecked(ItemCategory category, std::uint32_t param_id) {
        return ItemId{OptionalItemId{(std::uint32_t(category) << 28) | param_id}};
    }
    // An ItemId from a raw ID, or why it is not one.
    static constexpr ItemIdError try_from(OptionalItemId raw, ItemId* out) {
        if (!raw.is_valid()) return ItemIdError::InvalidCategory;
        *out = ItemId{raw};
        return ItemIdError::None;
    }

    // This ID's equipment category (valid by construction).
    constexpr ItemCategory category() const { return static_cast<ItemCategory>(id.category_raw()); }
    // This ID's parameter row ID.
    constexpr std::uint32_t param_id() const { return id.param_id_raw(); }
    // The underlying raw value.
    constexpr std::uint32_t into_inner() const { return id.into_inner(); }
    constexpr bool operator==(ItemId o) const { return id == o.id; }
    constexpr bool operator!=(ItemId o) const { return id != o.id; }
};

namespace detail::item_id_layout {
BB_SIZE(OptionalItemId, OPTIONAL_ITEM_ID_SIZE);
BB_SIZE(ItemId, ITEM_ID_SIZE);
static_assert(ItemId::new_unchecked(ItemCategory::Goods, 123).into_inner() == 0x4000007b, "ItemId packing");
static_assert(OptionalItemId::from_raw(0x30000001) == OptionalItemId::NONE, "invalid category normalizes");
}  // namespace detail::item_id_layout

}  // namespace bb
