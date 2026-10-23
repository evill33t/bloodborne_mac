// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Gpu::Metal::WindConsumed {
// All reads here are from already loaded thread registers or the private
// record. The observation never adds a guest descriptor/payload load.
inline constexpr const char MslPrelude[]=R"MSL(
static inline void dc_record_wind_site(device atomic_uint* record_, uint site_, thread const uint* values_,
                                       ulong leaf_, uint vid_, uint iid_, bool mapped_) {
    uint samples_ = site_ == 0u ? 3u : 4u;
    uint old_ = atomic_fetch_add_explicit(record_ + samples_, 1u, memory_order_relaxed);
    if (old_ == 0xffffffffu) atomic_store_explicit(record_ + 2u, 1u, memory_order_relaxed);
    if (!mapped_) atomic_fetch_add_explicit(record_ + (site_ == 0u ? 5u : 6u), 1u, memory_order_relaxed);
    uint leaf_at_ = site_ == 0u ? 7u : 11u;
    atomic_fetch_min_explicit(record_ + leaf_at_, uint(leaf_), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + leaf_at_ + 1u, uint(leaf_), memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + leaf_at_ + 2u, uint(leaf_ >> 32), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + leaf_at_ + 3u, uint(leaf_ >> 32), memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 15u, vid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 16u, vid_, memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 17u, iid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 18u, iid_, memory_order_relaxed);
    uint count_ = site_ == 0u ? 4u : 16u;
    uint min_at_ = site_ == 0u ? 20u : 28u;
    uint max_at_ = site_ == 0u ? 24u : 44u;
    for (uint k_ = 0u; k_ < count_; ++k_) {
        atomic_fetch_min_explicit(record_ + min_at_ + k_, values_[k_], memory_order_relaxed);
        atomic_fetch_max_explicit(record_ + max_at_ + k_, values_[k_], memory_order_relaxed);
    }
}
)MSL";
} // namespace Gpu::Metal::WindConsumed
