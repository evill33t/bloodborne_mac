// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Gpu::Metal::WindConsumed {
// All reads here are from already loaded thread registers or the private
// record. The observation never adds a guest descriptor/payload load.
inline constexpr const char MslPrelude[]=R"MSL(
static inline void dc_record_wind_site(device atomic_uint* record_, uint site_, thread const uint* values_,
                                       ulong leaf_, uint vid_, uint iid_, bool mapped_) {
    uint samples_ = site_ == 0u ? 3u : site_ == 1u ? 4u : 60u;
    uint old_ = atomic_fetch_add_explicit(record_ + samples_, 1u, memory_order_relaxed);
    if (old_ == 0xffffffffu) atomic_store_explicit(record_ + 2u, 1u, memory_order_relaxed);
    if (!mapped_) atomic_fetch_add_explicit(record_ + (site_ == 0u ? 5u : site_ == 1u ? 6u : 61u), 1u, memory_order_relaxed);
    uint leaf_at_ = site_ == 0u ? 7u : site_ == 1u ? 11u : 62u;
    atomic_fetch_min_explicit(record_ + leaf_at_, uint(leaf_), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + leaf_at_ + 1u, uint(leaf_), memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + leaf_at_ + 2u, uint(leaf_ >> 32), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + leaf_at_ + 3u, uint(leaf_ >> 32), memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 15u, vid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 16u, vid_, memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 17u, iid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 18u, iid_, memory_order_relaxed);
    uint count_ = site_ == 0u ? 4u : site_ == 1u ? 16u : 3u;
    uint min_at_ = site_ == 0u ? 20u : site_ == 1u ? 28u : 66u;
    uint max_at_ = site_ == 0u ? 24u : site_ == 1u ? 44u : 69u;
    for (uint k_ = 0u; k_ < count_; ++k_) {
        atomic_fetch_min_explicit(record_ + min_at_ + k_, values_[k_], memory_order_relaxed);
        atomic_fetch_max_explicit(record_ + max_at_ + k_, values_[k_], memory_order_relaxed);
    }
}
// Select one original row0 index, then ignore legitimate data belonging to
// other indices. No spinning: a spurious initial CAS miss is counted/dropped.
// All three actual original rows must be in descriptor bounds. Mapping-null
// status is deliberately unobserved: the original typed helper returns zeros
// for a null map, and observing that separately would require more plumbing.
static inline void dc_record_wind_matrix(device atomic_uint* record_, thread const uint* values_,
                                         uint4 descriptor_, uint3 rows_, uint vid_, uint iid_) {
    uint before_ = atomic_fetch_add_explicit(record_ + 72u, 1u, memory_order_relaxed);
    if (before_ == 0xffffffffu) atomic_store_explicit(record_ + 2u, 1u, memory_order_relaxed);
    if (rows_.x > 0xfffffffdu || rows_.y != rows_.x + 1u || rows_.z != rows_.x + 2u || any(rows_ >= descriptor_.z)) {
        atomic_fetch_add_explicit(record_ + 76u, 1u, memory_order_relaxed); return;
    }
    uint expected_ = 0xffffffffu;
    bool claimed_ = atomic_compare_exchange_weak_explicit(record_ + 77u, &expected_, rows_.x,
                                                          memory_order_relaxed, memory_order_relaxed);
    uint selected_ = claimed_ ? rows_.x : expected_;
    if (selected_ == 0xffffffffu) {atomic_fetch_add_explicit(record_ + 75u, 1u, memory_order_relaxed); return;}
    if (selected_ != rows_.x) {atomic_fetch_add_explicit(record_ + 74u, 1u, memory_order_relaxed); return;}
    before_ = atomic_fetch_add_explicit(record_ + 73u, 1u, memory_order_relaxed);
    if (before_ == 0xffffffffu) atomic_store_explicit(record_ + 2u, 1u, memory_order_relaxed);
    ulong leaf_ = ulong(descriptor_.x) | (ulong(descriptor_.y & 0xfffu) << 32);
    atomic_fetch_min_explicit(record_ + 78u, uint(leaf_), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 79u, uint(leaf_), memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 80u, uint(leaf_ >> 32), memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 81u, uint(leaf_ >> 32), memory_order_relaxed);
    for(uint k_ = 0u; k_ < 4u; ++k_) {
        atomic_fetch_min_explicit(record_ + 82u + k_, descriptor_[k_], memory_order_relaxed);
        atomic_fetch_max_explicit(record_ + 86u + k_, descriptor_[k_], memory_order_relaxed);
    }
    for(uint k_ = 0u; k_ < 3u; ++k_) {
        atomic_fetch_min_explicit(record_ + 90u + k_, rows_[k_], memory_order_relaxed);
        atomic_fetch_max_explicit(record_ + 93u + k_, rows_[k_], memory_order_relaxed);
    }
    for(uint k_ = 0u; k_ < 12u; ++k_) {
        atomic_fetch_min_explicit(record_ + 96u + k_, values_[k_], memory_order_relaxed);
        atomic_fetch_max_explicit(record_ + 108u + k_, values_[k_], memory_order_relaxed);
    }
    atomic_fetch_min_explicit(record_ + 15u, vid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 16u, vid_, memory_order_relaxed);
    atomic_fetch_min_explicit(record_ + 17u, iid_, memory_order_relaxed);
    atomic_fetch_max_explicit(record_ + 18u, iid_, memory_order_relaxed);
}
)MSL";
} // namespace Gpu::Metal::WindConsumed
