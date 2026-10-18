// SprjRapidReentryHelper: retains file and mobank resources while the game
// tears down and rapidly re-enters world data.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_RAPID_REENTRY_HELPER_SIZE = 0x58;
inline constexpr std::size_t SPRJ_RAPID_REENTRY_RESOURCE_SET_SIZE = 0x20;
inline constexpr std::size_t SPRJ_RAPID_REENTRY_RESOURCE_NODE_SIZE = 0x28;

// Opaque SprjFile resource tracked across a rapid map re-entry.
struct SprjFile;
// Opaque SprjFD4MobankResCap resource tracked across a rapid map re-entry.
struct SprjFD4MobankResCap;

// Observed rapid-reentry mode at SprjRapidReentryHelper + 0x0c. The transition
// setup at RVA 0x1942190 writes only these three values; the numeric names
// avoid claiming more than the xrefs prove.
enum class RapidReentryMode : std::int32_t {
    Mode0 = 0,
    Mode1 = 1,
    Mode2 = 2,
};

// Classification derived from event flag 9800 during resource capture.
enum class RapidReentryEventFlag9800Class : std::uint32_t {
    Default = 0,
    Value4 = 1,
    Value6 = 2,
    Value7 = 3,
};

// MSVC 2012 ordered-set node. The sentinel has is_nil == true and its
// resource storage is not initialized: use resource_key().
template <typename T>
struct SprjRapidReentryResourceNode {
    SprjRapidReentryResourceNode* left;
    SprjRapidReentryResourceNode* parent;
    SprjRapidReentryResourceNode* right;
    std::uint8_t color;
    bool is_nil;
    Unknown<6> _pad1a;
    T* resource;

    // The pointer key of a non-sentinel node, else null.
    T* resource_key() const { return is_nil ? nullptr : resource; }
};

// MSVC 2012 std::set<T*> layout used for retained resources.
template <typename T>
struct SprjRapidReentryResourceSet {
    using Node = SprjRapidReentryResourceNode<T>;

    // Storage occupied by the VC2012 comparator/base subobject.
    std::uintptr_t _comparator_storage;
    Node* head;
    std::size_t len;
    void* allocator;

    bool empty() const { return len == 0; }

    // fn(T*) for each resource key in address order (at most len of them).
    template <typename Fn>
    void for_each(Fn&& fn) const {
        if (!head) return;
        Node* cur = head->left;
        if (!cur || cur->is_nil) return;
        for (std::size_t remaining = len; remaining && cur; --remaining) {
            T* r = cur->resource_key();
            if (!r) return;
            fn(r);
            cur = successor(cur);
        }
    }

    // Membership using the pointer-address ordering of the game's std::set
    // insertion routine at RVA 0x19cfcb0.
    bool contains(const T* resource) const {
        if (!head || !resource) return false;
        Node* cursor = head->parent;
        const auto target = reinterpret_cast<std::uintptr_t>(resource);
        while (cursor) {
            T* key = cursor->resource_key();
            if (!key) return false;
            const auto candidate = reinterpret_cast<std::uintptr_t>(key);
            if (target == candidate) return true;
            Node* next = target < candidate ? cursor->left : cursor->right;
            if (!next || next->is_nil) return false;
            cursor = next;
        }
        return false;
    }

private:
    Node* successor(Node* current) const {
        Node* right = current->right;
        if (right && !right->is_nil) {
            for (;;) {
                Node* left = right->left;
                if (!left || left->is_nil) return right;
                right = left;
            }
        }
        Node* child = current;
        Node* parent = current->parent;
        while (parent && parent != head) {
            if (parent->left == child) return parent;
            child = parent;
            parent = parent->parent;
        }
        return nullptr;
    }
};

// Resource-retention helper used across rapid world re-entry. Constructor RVA
// 0x19ce610 initializes this exact 0x58-byte object. Capture records the
// current packed map ID and an event-flag-derived class, then retains SprjFile
// and mobank resources in two ordered sets. Reset RVA 0x19ce980 releases both
// sets and restores the scalar sentinels.
struct SprjRapidReentryHelper {
    static constexpr Rva SINGLETON_PTR = SPRJ_RAPID_REENTRY_HELPER_SINGLETON_PTR;

    const void* vftable;
    std::int32_t enabled_raw;
    std::int32_t mode_raw;
    std::uint32_t source_map_id_raw;
    std::uint32_t event_flag_9800_class_raw;
    SprjRapidReentryResourceSet<SprjFile> files;
    SprjRapidReentryResourceSet<SprjFD4MobankResCap> mobanks;

    bool is_enabled() const { return enabled_raw != 0; }
    // False when mode_raw is not one of the observed values.
    bool mode(RapidReentryMode* out) const {
        if (mode_raw < 0 || mode_raw > 2) return false;
        *out = static_cast<RapidReentryMode>(mode_raw);
        return true;
    }
    // False for the 0xffffffff sentinel.
    bool source_map_id(std::uint32_t* out) const {
        if (source_map_id_raw == 0xffffffffu) return false;
        *out = source_map_id_raw;
        return true;
    }
    // False for the 0xffffffff sentinel or an unknown class.
    bool event_flag_9800_class(RapidReentryEventFlag9800Class* out) const {
        if (event_flag_9800_class_raw > 3) return false;
        *out = static_cast<RapidReentryEventFlag9800Class>(event_flag_9800_class_raw);
        return true;
    }
};

namespace detail::rapid_reentry_helper_layout {
using Set = SprjRapidReentryResourceSet<SprjFile>;
using Node = SprjRapidReentryResourceNode<SprjFile>;
BB_SIZE(SprjRapidReentryHelper, SPRJ_RAPID_REENTRY_HELPER_SIZE);
BB_SIZE(Set, SPRJ_RAPID_REENTRY_RESOURCE_SET_SIZE);
BB_SIZE(Node, SPRJ_RAPID_REENTRY_RESOURCE_NODE_SIZE);
BB_OFFSET(SprjRapidReentryHelper, enabled_raw, 0x08);
BB_OFFSET(SprjRapidReentryHelper, mode_raw, 0x0c);
BB_OFFSET(SprjRapidReentryHelper, source_map_id_raw, 0x10);
BB_OFFSET(SprjRapidReentryHelper, event_flag_9800_class_raw, 0x14);
BB_OFFSET(SprjRapidReentryHelper, files, 0x18);
BB_OFFSET(SprjRapidReentryHelper, mobanks, 0x38);
BB_OFFSET(Set, head, 0x08);
BB_OFFSET(Set, len, 0x10);
BB_OFFSET(Set, allocator, 0x18);
BB_OFFSET(Node, color, 0x18);
BB_OFFSET(Node, is_nil, 0x19);
BB_OFFSET(Node, resource, 0x20);
}  // namespace detail::rapid_reentry_helper_layout

}  // namespace bb
