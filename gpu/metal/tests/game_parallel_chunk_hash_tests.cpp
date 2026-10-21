// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/parallel_chunk_hash.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
using namespace Gpu::Metal::ParallelChunkHash;
int main() {
    assert(!ConfiguredWorkers(nullptr) && !ConfiguredWorkers("") && !ConfiguredWorkers("1"));
    assert(!ConfiguredWorkers("-2") && !ConfiguredWorkers("2x") && !ConfiguredWorkers(" 4"));
    assert(ConfiguredWorkers("2")==2 && ConfiguredWorkers("4")==4);
    for(std::size_t size:{1u,31u,32u,33u,65535u,65536u,65537u,262144u,1048576u,1048613u,5595136u}) {
        std::vector<unsigned char> data(size+8);
        for(std::size_t i=0;i<data.size();++i)data[i]=(i*13+i/17)&255;
        for(unsigned alignment:{0u,1u,7u}) {
            auto* source=data.data()+alignment;const auto expected=Gpu::ContentHash::Chunks(source,size);
            for(unsigned workers:{2u,4u}) {
                const auto same=Compare(source,size,expected,workers,1);
                assert(same.valid && !same.changed && same.bytes==size && same.chunks==expected.size());
                for(auto at:{std::size_t(0),size/2,size-1}) {
                    source[at]^=0x40;const auto edit=Compare(source,size,expected,workers,1);
                    assert(edit.valid && edit.changed && edit.bytes && edit.bytes<=size);
                    if(at==0 && size>65536)assert(!edit.parallel && edit.workers==1 && edit.bytes==65536 && edit.chunks==1);
                    source[at]^=0x40;
                }
                if(size>65536)for(auto at:{std::size_t(65535),std::size_t(65536)}) {
                    source[at]^=1;assert(Compare(source,size,expected,workers,1).changed);source[at]^=1;
                }
                const auto serial=Compare(source,size,expected,workers,std::numeric_limits<std::size_t>::max());
                assert(serial.valid && !serial.parallel && serial.workers==1 && !serial.changed && serial.bytes==size);
            }
            auto bad=expected;bad.pop_back();assert(!Compare(source,size,bad,4).valid);
            assert(!Compare(source,size,expected,3).valid);
        }
    }
    assert(!Compare(nullptr,1,{},4).valid && !Compare(nullptr,0,{},4).valid);
    const std::array<std::uint64_t,1> one{};
    assert(!Compare(reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max()-3),8,one,4).valid);
    assert(Histogram::ConfiguredEnabled("1") && !Histogram::ConfiguredEnabled("0") &&
        !Histogram::ConfiguredEnabled("1x") && !Histogram::ConfiguredEnabled(nullptr));
    Histogram histogram;
    for(auto size:{1ull,65536ull,65537ull,262144ull,262145ull,1048576ull,1048577ull,
            4194304ull,4194305ull,16777216ull,16777217ull})
        histogram.Note(size,true,size>=1048576,true,false,37);
    std::uint64_t checks=0,bytes=0;for(const auto& bin:histogram.bins) {checks+=bin.checks;bytes+=bin.bytes;}
    assert(checks==11 && bytes==11*37);
    std::uint64_t saturated=std::numeric_limits<std::uint64_t>::max()-2;
    Histogram::Add(saturated,3);assert(saturated==std::numeric_limits<std::uint64_t>::max());
    // A source ending exactly at inaccessible memory proves no lookahead/tail
    // read escapes the caller's full declared span. Misaligned short tail too.
    const auto page=std::size_t(sysconf(_SC_PAGESIZE));const std::size_t size=(1u<<20)+37;
    const auto mapped=(size+page-1)/page*page;
    auto* allocation=static_cast<unsigned char*>(mmap(nullptr,mapped+page,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANON,-1,0));assert(allocation!=MAP_FAILED);
    assert(!mprotect(allocation+mapped,page,PROT_NONE));auto* source=allocation+mapped-size;
    for(std::size_t i=0;i<size;++i)source[i]=(i*3)&255;
    const auto expected=Gpu::ContentHash::Chunks(source,size);
    assert(Compare(source,size,expected,4,1).valid && !Compare(source,size,expected,4,1).changed);
    assert(!munmap(allocation,mapped+page));
    std::puts("Parallel chunk hash: exact CRC chunks, full/tail/unaligned scans, first/middle/last edits, disabled/malformed configuration and inaccessible source boundary passed.");
}
