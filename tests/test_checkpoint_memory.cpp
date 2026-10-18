// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/checkpoint/archive.h"
#include "src/checkpoint/memory.h"
#include <array>
#include <bit>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>

extern "C" void bb_checkpoint_memory_fixture_setup(std::uint64_t[4]);
extern "C" void bb_checkpoint_memory_fixture_verify(const std::uint64_t[4]);
namespace {
using namespace Bb::Checkpoint;
static_assert(std::endian::native==std::endian::little);
template<class T>void Append(std::vector<std::uint8_t>& bytes,const T* data,std::size_t count) {
    if(!count)return;
    const auto* first=reinterpret_cast<const std::uint8_t*>(data);
    bytes.insert(bytes.end(),first,first+count*sizeof(T));
}
std::map<std::uint64_t,std::uint64_t> Contents(const std::vector<BbCheckpointMemoryBlock>& blocks,
                                             const std::vector<BbCheckpointMemoryVma>& vmas) {
    std::map<std::uint64_t,std::uint64_t> ranges;
    for(const auto& b:blocks)assert(ranges.emplace(b.physical,b.size).second);
    for(const auto& v:vmas)if(v.kind==BB_CK_MEMORY_FLEXIBLE)assert(ranges.emplace(v.physical,v.size).second);
    return ranges;
}
}
int main(int argc,char** argv) {
    using namespace Bb::Checkpoint;
    assert(argc==3);std::string error;
    if(!std::strcmp(argv[1],"--save")) {
        std::array<std::uint64_t,4> locations{};bb_checkpoint_memory_fixture_setup(locations.data());
        BbCheckpointMemoryLayout layout{};assert(!runtime_memory_checkpoint_export(&layout,nullptr,0,nullptr,0));
        std::vector<BbCheckpointMemoryBlock> blocks(layout.block_count);
        std::vector<BbCheckpointMemoryVma> vmas(layout.vma_count);
        assert(!runtime_memory_checkpoint_export(&layout,blocks.data(),blocks.size(),vmas.data(),vmas.size()));
        Section maps;maps.kind=SectionKind::MemoryLayout;
        Append(maps.bytes,&layout,1);Append(maps.bytes,blocks.data(),blocks.size());Append(maps.bytes,vmas.data(),vmas.size());
        Archive archive;archive.identity="checkpoint-memory-fixture-v1";archive.sections.push_back(std::move(maps));
        Section fixture;fixture.kind=SectionKind::Fixture;Append(fixture.bytes,locations.data(),locations.size());
        archive.sections.push_back(std::move(fixture));
        for(const auto& [physical,size]:Contents(blocks,vmas)) {
            Section bytes;bytes.kind=SectionKind::MemoryBytes;bytes.id=physical;bytes.bytes.resize(size);
            assert(!runtime_memory_checkpoint_read(physical,bytes.bytes.data(),bytes.bytes.size()));
            archive.sections.push_back(std::move(bytes));
        }
        assert(Save(argv[2],archive,error));
        bb_checkpoint_memory_fixture_verify(locations.data());
        puts("Checkpoint memory: production VMA/physical layout saved, including aliases and split flexible pages");return 0;
    }
    assert(!std::strcmp(argv[1],"--restore"));
    Archive archive;assert(Load(argv[2],"checkpoint-memory-fixture-v1",archive,error));
    assert(archive.sections.size()>=2 && archive.sections[0].kind==SectionKind::MemoryLayout &&
           archive.sections[0].id==0 && archive.sections[0].version==1);
    const auto& bytes=archive.sections[0].bytes;assert(bytes.size()>=sizeof(BbCheckpointMemoryLayout));
    BbCheckpointMemoryLayout layout{};std::memcpy(&layout,bytes.data(),sizeof(layout));
    assert(layout.block_count<=4096 && layout.vma_count<=65536);
    assert(bytes.size()==sizeof(layout)+layout.block_count*sizeof(BbCheckpointMemoryBlock)+layout.vma_count*sizeof(BbCheckpointMemoryVma));
    std::vector<BbCheckpointMemoryBlock> blocks(layout.block_count);
    std::vector<BbCheckpointMemoryVma> vmas(layout.vma_count);
    std::memcpy(blocks.data(),bytes.data()+sizeof(layout),blocks.size()*sizeof(blocks[0]));
    std::memcpy(vmas.data(),bytes.data()+sizeof(layout)+blocks.size()*sizeof(blocks[0]),vmas.size()*sizeof(vmas[0]));
    const auto expected=Contents(blocks,vmas);assert(archive.sections.size()==expected.size()+2);
    assert(archive.sections[1].kind==SectionKind::Fixture && archive.sections[1].id==0 && archive.sections[1].version==1 &&
           archive.sections[1].bytes.size()==sizeof(std::array<std::uint64_t,4>));
    std::array<std::uint64_t,4> locations{};std::memcpy(locations.data(),archive.sections[1].bytes.data(),sizeof(locations));
    for(std::size_t k=2;k<archive.sections.size();++k) {
        const auto& section=archive.sections[k];const auto found=expected.find(section.id);
        assert(section.kind==SectionKind::MemoryBytes && section.version==1 && found!=expected.end() && found->second==section.bytes.size());
    }
    // Invalid records must leave the fresh memory table empty.
    auto invalid=vmas;invalid[0].start=0;
    assert(runtime_memory_checkpoint_restore(&layout,blocks.data(),invalid.data())==EINVAL);
    invalid=vmas;invalid[0].size=UINT64_MAX;
    assert(runtime_memory_checkpoint_restore(&layout,blocks.data(),invalid.data())==EINVAL);
    auto invalid_blocks=blocks;invalid_blocks[0].size=UINT64_MAX;
    assert(runtime_memory_checkpoint_restore(&layout,invalid_blocks.data(),vmas.data())==EINVAL);
    BbCheckpointMemoryLayout empty{};assert(!runtime_memory_checkpoint_export(&empty,nullptr,0,nullptr,0));
    assert(!empty.block_count && !empty.vma_count);
    assert(!runtime_memory_checkpoint_restore(&layout,blocks.data(),vmas.data()));
    for(std::size_t k=2;k<archive.sections.size();++k) {
        const auto& section=archive.sections[k];
        assert(!runtime_memory_checkpoint_write(section.id,section.bytes.data(),section.bytes.size()));
    }
    assert(runtime_memory_checkpoint_restore(&layout,blocks.data(),vmas.data())==EBUSY);
    bb_checkpoint_memory_fixture_verify(locations.data());
    puts("Checkpoint memory: fresh-process physical aliases, contents, permissions and allocator bitmap restored");
}
