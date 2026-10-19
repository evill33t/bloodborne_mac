// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#if defined(__x86_64__)
#include <nmmintrin.h>
#elif defined(__aarch64__) && defined(__ARM_FEATURE_CRC32)
#include <arm_acle.h>
#endif

namespace Gpu::ContentHash {
// Texture change checks and immutable upload snapshots use the same chunks.
inline constexpr std::size_t kChunkBytes=64u<<10;
inline std::uint64_t Scalar(const void* data,std::size_t size) {
    const auto* p=static_cast<const unsigned char*>(data);
    std::uint64_t h[4]={0x9e3779b97f4a7c15ull,0xc2b2ae3d27d4eb4full,
        0x165667b19e3779f9ull,0x27d4eb2f165667c5ull};
    std::size_t off=0;
    for (;off+32<=size;off+=32) for (unsigned k=0;k<4;++k) {
        std::uint64_t v;std::memcpy(&v,p+off+k*8,8);
        h[k]=(h[k]^v)*0x100000001b3ull;h[k]^=h[k]>>29;
    }
    for (;off<size;++off) h[0]=(h[0]^p[off])*0x100000001b3ull;
    return h[0]^(h[1]*3)^(h[2]*5)^(h[3]*7);
}

#if defined(__x86_64__) || (defined(__aarch64__) && defined(__ARM_FEATURE_CRC32))
#if defined(__x86_64__)
#define DC_CRC_TARGET __attribute__((target("sse4.2")))
#define DC_CRC64(h,v) static_cast<std::uint32_t>(_mm_crc32_u64(h,v))
#define DC_CRC8(h,v) _mm_crc32_u8(h,v)
#else
#define DC_CRC_TARGET
#define DC_CRC64(h,v) __crc32cd(h,v)
#define DC_CRC8(h,v) __crc32cb(h,v)
#endif

// Read every byte. Four independent hardware CRC streams hide instruction
// latency; nonlinear mixing prevents equal edits in different lanes cancelling.
// This is a nonpersistent change detector, not a cryptographic hash.
DC_CRC_TARGET inline std::uint64_t Hardware(const void* data,std::size_t size) {
    const auto* p=static_cast<const unsigned char*>(data);
    std::uint32_t h[4]={0x9e3779b9u,0xc2b2ae3du,0x165667b1u,0x27d4eb2fu};
    std::size_t off=0;
    for (;off+32<=size;off+=32) for (unsigned k=0;k<4;++k) {
        std::uint64_t v;std::memcpy(&v,p+off+k*8,8);
        h[k]=DC_CRC64(h[k],v);
    }
    for (;off<size;++off) h[0]=DC_CRC8(h[0],p[off]);
    return (std::uint64_t(h[0])*0x9e3779b185ebca87ull)^
        std::rotl(std::uint64_t(h[1])*0xc2b2ae3d27d4eb4full,17)^
        (std::uint64_t(h[2])*0x165667b19e3779f9ull)^
        std::rotl(std::uint64_t(h[3])*0x85ebca77c2b2ae63ull,41)^size;
}
#undef DC_CRC_TARGET
#undef DC_CRC64
#undef DC_CRC8

inline bool HasHardware() {
#if defined(__x86_64__)
    static const bool available=__builtin_cpu_supports("sse4.2");
    return available;
#else
    return true;
#endif
}
#else
inline bool HasHardware() { return false; }
#endif

inline std::uint64_t Bytes(const void* data,std::size_t size) {
#if defined(__x86_64__) || (defined(__aarch64__) && defined(__ARM_FEATURE_CRC32))
    if (HasHardware()) return Hardware(data,size);
#endif
    return Scalar(data,size);
}
inline std::vector<std::uint64_t> Chunks(const void* data,std::size_t size) {
    const auto* bytes=static_cast<const unsigned char*>(data);
    std::vector<std::uint64_t> hashes;
    hashes.reserve(size/kChunkBytes+(size%kChunkBytes!=0));
    for(std::size_t offset=0;offset<size;offset+=kChunkBytes) {
        const auto remaining=size-offset;
        hashes.push_back(Bytes(bytes+offset,remaining<kChunkBytes ? remaining : kChunkBytes));
    }
    return hashes;
}
} // namespace Gpu::ContentHash
