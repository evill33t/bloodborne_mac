// SPDX-License-Identifier: GPL-2.0-or-later
#include "common/content_hash.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

int main() {
    using namespace Gpu::ContentHash;
    // Every byte and bit in block boundaries, unaligned inputs and short tails
    // must invalidate a cached texture. Also cover the full 64 KiB cache chunk.
    for (std::size_t size : {0u,1u,7u,8u,15u,31u,32u,33u,63u,65u,65536u}) {
        std::vector<unsigned char> storage(size+16);
        for (std::size_t i=0;i<storage.size();++i) storage[i]=(i*13+i/17)&255;
        for (unsigned alignment : {0u,1u,7u}) {
            auto* p=storage.data()+alignment;
            const auto original=Bytes(p,size), scalar=Scalar(p,size);
            for (std::size_t i=0;i<size;++i) {
                // All bits for small inputs; each byte for full cache chunks.
                for (unsigned bit=0;bit<(size<128 ? 8u : 1u);++bit) {
                    p[i]^=1u<<bit;
                    assert(Bytes(p,size)!=original);
                    if (size<128) assert(Scalar(p,size)!=scalar);
                    p[i]^=1u<<bit;
                }
            }
            assert(Bytes(p,size)==original && Scalar(p,size)==scalar);
        }
    }
    // Equal changes in independent CRC lanes must not cancel at combination.
    std::array<unsigned char,96> lanes{};
    for (unsigned pattern=0;pattern<2;++pattern) {
        lanes.fill(pattern ? 255 : 0);
        const auto original=Bytes(lanes.data(),lanes.size());
        for (unsigned a=0;a<32;++a) for (unsigned b=a+1;b<32;++b) {
            for (unsigned bit=0;bit<8;++bit) {
                lanes[a]^=1u<<bit; lanes[b]^=1u<<bit;
                assert(Bytes(lanes.data(),lanes.size())!=original);
                lanes[a]^=1u<<bit; lanes[b]^=1u<<bit;
            }
        }
    }
    assert(Bytes(lanes.data(),31)!=Bytes(lanes.data(),32));
    std::printf("Texture hash: full coverage, unaligned/tail and paired edits passed (hardware=%d)\n",HasHardware());
}
