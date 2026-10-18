// Character sets: the slot arrays WorldChrMan keeps its characters in.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t CHR_SET_SIZE = 0x18;
inline constexpr std::size_t CHR_SET_ENTRY_SIZE = 0x38;
inline constexpr std::size_t CHR_SET_CAPACITY_OFFSET = 0x00;
inline constexpr std::size_t CHR_SET_ENTRIES_OFFSET = 0x08;
inline constexpr std::size_t CHR_SET_ENTRY_CHR_OFFSET = 0x00;
inline constexpr std::size_t CHR_SET_ENTRY_TRANSITION_STATE_FLAGS_OFFSET = 0x20;
inline constexpr std::uint64_t CHR_SET_ENTRY_TRANSITION_OCCUPANCY_BIT = 1ull << 1;
inline constexpr std::size_t CHR_SET_ENTRY_BACKREAD_STATE_OFFSET = 0x14;
inline constexpr std::size_t CHR_SET_ENTRY_BACKREAD_GROUP_BINDING_OFFSET = 0x30;
inline constexpr std::uint64_t CHR_SET_ENTRY_AREA_BACKREAD_MANAGED_BIT = CHR_SET_ENTRY_TRANSITION_OCCUPANCY_BIT;
inline constexpr std::size_t CHR_BACKREAD_GROUP_BINDING_SIZE = 0x20;
inline constexpr std::uint8_t CHR_BACKREAD_GROUP_ACTIVE_BIT = 1;

// Prefix of the area/backread-group binding reached from ChrSetEntry+0x30.
struct ChrBackreadGroupBinding {
    Unknown<0x18> _unk00;
    std::uint8_t active_flags;
    Unknown<0x07> _unk19;

    bool is_active() const { return (active_flags & CHR_BACKREAD_GROUP_ACTIVE_BIT) != 0; }
};

// Character-set entry. Lookups stride these entries by 0x38. Native detach at
// RVA 0x18db230 zeros +0x00..+0x1f and +0x28..+0x37 but clears only the low
// three bits of +0x20: an empty entry does not imply every flag byte is reset.
template <typename T>
struct ChrSetEntry {
    T* chr;  // null when the slot is empty
    Unknown<0x0c> _unk08;
    // Backread state selected by WorldChrMan. The observed disabled value is
    // 4; the complete enum remains unknown.
    std::int32_t backread_state;
    Unknown<0x08> _unk18;
    // Packed state refreshed by ChrIns_ApplyTransitionOccupancyState.
    // Non-client roles write bit 1 from the transition occupancy test; client
    // role 6 skips that write. Forcing it on a remote role-6 actor broke that
    // actor's collision: the flag takes part in backread/collision scheduling.
    // It is not itself the stale-reentry condition.
    std::uint64_t transition_state_flags;
    Unknown<0x08> _unk28;
    // Area/backread-group link consulted while building character update
    // lists. A null or inactive link suppresses area-managed actors.
    ChrBackreadGroupBinding* backread_group_binding;

    bool empty() const { return chr == nullptr; }
    bool transition_occupancy_marked() const {
        return (transition_state_flags & CHR_SET_ENTRY_TRANSITION_OCCUPANCY_BIT) != 0;
    }
    // Native transition processing classified this character as managed by an
    // area/backread group.
    bool area_backread_managed() const {
        return (transition_state_flags & CHR_SET_ENTRY_AREA_BACKREAD_MANAGED_BIT) != 0;
    }
    bool backread_group_active() const { return backread_group_binding && backread_group_binding->is_active(); }
    // Mirrors the native scheduling condition for an area-managed actor.
    bool scheduled_for_current_area() const { return !area_backread_managed() || backread_group_active(); }
};

// Character set used by the early WorldChrMan prefix.
template <typename T>
struct ChrSet {
    std::uint32_t capacity;
    std::uint32_t _pad04;
    ChrSetEntry<T>* entries;
    std::uint32_t _unk10;
    std::uint32_t _pad14;

    std::size_t size() const { return entries ? capacity : 0; }
    ChrSetEntry<T>* entry(std::size_t i) const { return i < size() ? &entries[i] : nullptr; }
    T* chr(std::size_t i) const {
        ChrSetEntry<T>* e = entry(i);
        return e ? e->chr : nullptr;
    }
    // fn(T&) for every occupied slot.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t i = 0; i < size(); ++i) {
            if (T* c = entries[i].chr) fn(*c);
        }
    }
};

namespace detail::chr_set_layout {
struct Probe;
using Set = ChrSet<Probe>;
using Entry = ChrSetEntry<Probe>;
BB_SIZE(Set, CHR_SET_SIZE);
BB_SIZE(Entry, CHR_SET_ENTRY_SIZE);
BB_SIZE(ChrBackreadGroupBinding, CHR_BACKREAD_GROUP_BINDING_SIZE);
BB_OFFSET(Set, capacity, CHR_SET_CAPACITY_OFFSET);
BB_OFFSET(Set, entries, CHR_SET_ENTRIES_OFFSET);
BB_OFFSET(Entry, chr, CHR_SET_ENTRY_CHR_OFFSET);
BB_OFFSET(Entry, backread_state, CHR_SET_ENTRY_BACKREAD_STATE_OFFSET);
BB_OFFSET(Entry, transition_state_flags, CHR_SET_ENTRY_TRANSITION_STATE_FLAGS_OFFSET);
BB_OFFSET(Entry, backread_group_binding, CHR_SET_ENTRY_BACKREAD_GROUP_BINDING_OFFSET);
}  // namespace detail::chr_set_layout

}  // namespace bb
