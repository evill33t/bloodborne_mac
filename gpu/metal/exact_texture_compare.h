// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <list>
#include <memory>
#include <unordered_map>
#if defined(__x86_64__)
#include <emmintrin.h>
#elif defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace Gpu::Metal::ExactTextureCompare {
using u64=std::uint64_t;
inline constexpr u64 kMinimumSourceBytes=64ull<<10;
inline constexpr u64 kMaximumSourceBytes=16ull<<20;
inline constexpr u64 kMaximumBudgetBytes=2048ull<<20;
inline constexpr std::size_t kMaximumEntries=4096;

// Unset, zero, malformed and out-of-range settings leave the feature OFF.
inline u64 ParseBudget(const char* value) noexcept {
    if(!value || !*value)return 0;
    u64 mib=0;
    for(const auto* p=value;*p;++p) {
        if(*p<'0' || *p>'9')return 0;
        const auto digit=u64(*p-'0');
        if(mib>(2048-digit)/10)return 0;
        mib=mib*10+digit;
    }
    return mib && mib<=2048 ? mib<<20 : 0;
}
inline u64 ConfiguredBudget() noexcept {
    static const u64 bytes=ParseBudget(std::getenv("BB_METAL_EXACT_TEXTURE_COMPARE_MB"));
    return bytes;
}
inline bool Retainable(u64 bytes,u64 budget) noexcept {
    return bytes>=kMinimumSourceBytes && bytes<=kMaximumSourceBytes && bytes<=budget;
}
inline u64 NextGeneration(std::atomic<u64>& counter) noexcept {
    auto old=counter.load(std::memory_order_relaxed);
    while(old!=std::numeric_limits<u64>::max())
        if(counter.compare_exchange_weak(old,old+1,std::memory_order_relaxed))return old+1;
    return 0; // Saturation disables retention; generations are never recycled.
}
inline void Add(u64& value,u64 amount=1) noexcept {
    value=amount>std::numeric_limits<u64>::max()-value ? std::numeric_limits<u64>::max() : value+amount;
}

// Exact equality: every byte participates on the unchanged path. Vector loads
// remain inside the supplied extent; the final 0..63 bytes use bounded memcmp.
inline bool EqualBytes(const void* left,const void* right,std::size_t bytes) noexcept {
    if(!bytes)return true;
    if(!left || !right)return false;
    const auto* a=static_cast<const unsigned char*>(left);
    const auto* b=static_cast<const unsigned char*>(right);
    std::size_t off=0;
    for(;bytes-off>=64;off+=64) {
#if defined(__x86_64__)
        auto different=_mm_setzero_si128();
        for(unsigned k=0;k<4;++k)
            different=_mm_or_si128(different,_mm_xor_si128(
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(a+off+16*k)),
                _mm_loadu_si128(reinterpret_cast<const __m128i*>(b+off+16*k))));
        if(_mm_movemask_epi8(_mm_cmpeq_epi8(different,_mm_setzero_si128()))!=0xffff)return false;
#elif defined(__aarch64__) && defined(__ARM_NEON)
        auto different=vdupq_n_u8(0);
        for(unsigned k=0;k<4;++k)
            different=vorrq_u8(different,veorq_u8(vld1q_u8(a+off+16*k),vld1q_u8(b+off+16*k)));
        if(vmaxvq_u8(different))return false;
#else
        if(std::memcmp(a+off,b+off,64))return false;
#endif
    }
    return std::memcmp(a+off,b+off,bytes-off)==0;
}

// Trusted-producer contract: owner pins ONE dedicated allocation; data[0,bytes)
// is the exact immutable CPU source consumed by the successful upload. No arena
// append/recycle or writable handle is published. allocation_bytes charges the
// entire backing allocation, not just this source. Generation is process-unique.
class Snapshot final {
public:
    static std::shared_ptr<const Snapshot> Create(std::shared_ptr<const void> owner,
            const void* data,u64 bytes,u64 allocation_bytes,u64 generation) {
        if(!owner || !data || !bytes || !generation || bytes>allocation_bytes ||
           bytes>std::numeric_limits<std::size_t>::max() ||
           bytes>std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(data))return {};
        return std::shared_ptr<const Snapshot>(new Snapshot(std::move(owner),data,bytes,allocation_bytes,generation));
    }
    const void* Data()const noexcept{return data_;}
    u64 Bytes()const noexcept{return bytes_;}
    u64 AllocationBytes()const noexcept{return allocation_bytes_;}
    u64 Generation()const noexcept{return generation_;}
private:
    Snapshot(std::shared_ptr<const void> owner,const void* data,u64 bytes,u64 allocated,u64 generation)
        :owner_(std::move(owner)),data_(data),bytes_(bytes),allocation_bytes_(allocated),generation_(generation){}
    const std::shared_ptr<const void> owner_;
    const void* const data_;
    const u64 bytes_,allocation_bytes_,generation_;
};
using Handle=std::shared_ptr<const Snapshot>;

struct Identity {
    u64 descriptor_key=0,address=0,bytes=0,upload_version=0,mapping_generation=0;
    bool operator==(const Identity&)const=default;
    bool Valid()const noexcept {
        return address && bytes && upload_version && mapping_generation && !(mapping_generation&1) &&
            bytes<=std::numeric_limits<u64>::max()-address;
    }
};
enum class Result {Missing,Equal,Different};
struct Stats {
    u64 stores=0,invalid=0,budget_rejects=0,allocation_failures=0,evictions=0;
    u64 equal=0,different=0,missing=0,identity_rejects=0,compared_source_bytes=0;
};

// Renderer-owner-thread only. This cache grants NO guest read permission. The
// caller must retain all current mapping/CP/callback/GPU-writer readiness guards
// before Compare. Missing always means use the original full CRC comparison.
class Cache final {
    struct Entry {Identity identity;Handle snapshot;};
    using Lru=std::list<Entry>;
    struct Allocation {Handle snapshot;std::size_t references=0;};
public:
    explicit Cache(u64 budget=0):budget_(std::min(budget,kMaximumBudgetBytes)){}
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    u64 Budget()const noexcept{return budget_;}
    u64 RetainedBytes()const noexcept{return retained_;}
    std::size_t Entries()const noexcept{return entries_.size();}
    const Stats& Counters()const noexcept{return stats_;}
    void Erase(u64 upload_version) noexcept {
        const auto found=entries_.find(upload_version);
        if(found!=entries_.end())Remove(found->second,false);
    }
    void Clear() noexcept {entries_.clear();lru_.clear();allocations_.clear();retained_=0;}
    bool Store(const Identity& id,Handle snapshot) noexcept {
        if(!id.Valid() || !snapshot || id.bytes!=snapshot->Bytes() || !Retainable(id.bytes,budget_)) {
            Add(stats_.invalid);return false;
        }
        // Producer generations uniquely identify allocations, not aliases of a
        // reused address. A conflicting wrapper must never borrow a charge.
        const auto prior=allocations_.find(snapshot->Generation());
        if(prior!=allocations_.end() && prior->second.snapshot!=snapshot) {
            Add(stats_.invalid);return false;
        }
        const auto charge=snapshot->AllocationBytes();
        if(charge>budget_) {Add(stats_.budget_rejects);return false;}
        Erase(id.upload_version);
        auto existing=allocations_.find(snapshot->Generation());
        const bool new_allocation=existing==allocations_.end();
        const auto added=new_allocation ? charge : 0;
        while(!lru_.empty() && (added>budget_-retained_ || entries_.size()>=kMaximumEntries))
            Remove(std::prev(lru_.end()),true);
        if(added>budget_-retained_) {Add(stats_.budget_rejects);return false;}
        bool allocation_inserted=false,entry_inserted=false;
        try {
            const auto allocation=allocations_.try_emplace(snapshot->Generation(),Allocation{snapshot,0});
            allocation_inserted=allocation.second;
            const auto entry=entries_.try_emplace(id.upload_version,lru_.end());
            entry_inserted=entry.second;
            lru_.push_front({id,snapshot});entry.first->second=lru_.begin();
            ++allocation.first->second.references;
            if(allocation_inserted)retained_+=charge;
        } catch(...) {
            if(entry_inserted)entries_.erase(id.upload_version);
            if(allocation_inserted)allocations_.erase(snapshot->Generation());
            Add(stats_.allocation_failures);return false;
        }
        Add(stats_.stores);return true;
    }
    Result Compare(const Identity& id,const void* source) noexcept {
        return Compare(id,source,0,id.bytes);
    }
    Result Compare(const Identity& id,const void* source,u64 offset,u64 bytes) noexcept {
        const auto found=entries_.find(id.upload_version);
        if(found==entries_.end()) {Add(stats_.missing);return Result::Missing;}
        const auto entry=found->second;
        if(!id.Valid() || entry->identity!=id || !source || !bytes || offset>id.bytes || bytes>id.bytes-offset ||
           id.bytes>std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(source)) {
            Remove(entry,false);Add(stats_.identity_rejects);return Result::Missing;
        }
        Add(stats_.compared_source_bytes,bytes);
        if(!EqualBytes(static_cast<const unsigned char*>(source)+offset,
                static_cast<const unsigned char*>(entry->snapshot->Data())+offset,std::size_t(bytes))) {
            Remove(entry,false);Add(stats_.different);return Result::Different;
        }
        lru_.splice(lru_.begin(),lru_,entry);Add(stats_.equal);return Result::Equal;
    }
private:
    void Remove(Lru::iterator entry,bool eviction) noexcept {
        const auto allocation=allocations_.find(entry->snapshot->Generation());
        if(allocation!=allocations_.end() && --allocation->second.references==0) {
            retained_-=allocation->second.snapshot->AllocationBytes();allocations_.erase(allocation);
        }
        entries_.erase(entry->identity.upload_version);lru_.erase(entry);
        if(eviction)Add(stats_.evictions);
    }
    u64 budget_=0,retained_=0;
    Stats stats_;
    Lru lru_;
    std::unordered_map<u64,Lru::iterator> entries_;
    std::unordered_map<u64,Allocation> allocations_;
};
} // namespace Gpu::Metal::ExactTextureCompare
