// SPDX-License-Identifier: GPL-2.0-or-later
#include "src/checkpoint/archive.h"
#include "src/checkpoint/cpu_x86_64.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <tuple>
#include <array>
#include <unistd.h>

namespace {
using Bytes=std::vector<std::uint8_t>;
Bytes Read(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);return Bytes(std::istreambuf_iterator<char>(stream),{});
}
void Write(const std::filesystem::path& path,const Bytes& bytes) {
    std::ofstream stream(path,std::ios::binary);stream.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());assert(stream);
}
void Put(Bytes& bytes,std::size_t at,std::uint64_t value,unsigned count=8) {
    for(unsigned k=0;k<count;++k)bytes.at(at+k)=std::uint8_t(value>>(k*8));
}
std::uint64_t Hash(const Bytes& bytes,std::size_t start,std::size_t size) {
    std::uint64_t h=0xcbf29ce484222325ull;
    for(std::size_t k=start;k<start+size;++k)h=(h^bytes[k])*0x100000001b3ull;
    return h;
}
void Checksums(Bytes& bytes) {
    Put(bytes,32,Hash(bytes,176,48*2));Put(bytes,40,0);Put(bytes,40,Hash(bytes,0,176));
}
}
int main() {
    using namespace Bb::Checkpoint;
    char directory[]="/tmp/bb-checkpoint-archive-XXXXXX";assert(mkdtemp(directory));
    const auto path=std::filesystem::path(directory)/"fixture.bbstate";
    const auto damaged=std::filesystem::path(directory)/"damaged.bbstate";
    Archive archive;archive.identity="archive-fixture-v1";
    archive.sections.push_back({SectionKind::Fixture,1,7,{1,2,3,4,5,6}});
    archive.sections.push_back({SectionKind::MemoryBytes,1,0,{0,0,0,255,64,0,7,0}});
    std::string error;assert(Save(path,archive,error));
    Archive loaded;assert(Load(path,archive.identity,loaded,error) && loaded==archive);
    VerifiedArchive verified;assert(OpenVerified(path,archive.identity,verified,error));
    assert(verified.Sections().size()==archive.sections.size());
    std::array<std::uint8_t,6> small{};assert(verified.Read(0,0,small,error));
    assert(std::equal(small.begin(),small.end(),archive.sections[0].bytes.begin()));
    const auto original=Read(path);
    assert(!Load(path,"wrong-build",loaded,error) && loaded==archive);
    assert(!Load(path,"",loaded,error) && loaded==archive);
    auto invalid=archive;invalid.sections.push_back(invalid.sections.front());assert(!Save(path,invalid,error));
    invalid=archive;invalid.sections[0].version=0;assert(!Save(path,invalid,error));
    invalid=archive;invalid.sections[0].kind=static_cast<SectionKind>(99);assert(!Save(path,invalid,error));
    invalid=archive;invalid.identity.clear();assert(!Save(path,invalid,error));
    assert(!Save(path,archive,error,Limits{256,64,4}));
    assert(Read(path)==original); // Failed writes do not replace the old file.
    const auto reject=[&](const Bytes& bytes) {
        Write(damaged,bytes);assert(!Load(damaged,archive.identity,loaded,error) && loaded==archive);
        assert(!OpenVerified(damaged,archive.identity,verified,error) && verified.Sections().size()==2);
        assert(verified.Read(0,0,small,error));
    };
    for(std::size_t length=0;length<original.size();++length)reject(Bytes(original.begin(),original.begin()+length));
    for(std::size_t at:{std::size_t(0),std::size_t(24),std::size_t(176),original.size()-1}) {
        auto bad=original;bad[at]^=1;reject(bad);
    }
    // Structural validation must still reject a forged file with internally
    // consistent header/table checksums, before exposing any section payload.
    for(const auto& [at,value,count]:{
        std::tuple<std::size_t,std::uint64_t,unsigned>{8,2,4}, // version
        {16,0,4}, {16,65537,4}, {20,129,4},                 // counts/identity
        {176,99,4}, {180,0,4},                              // kind/version
        {192,0,8}, {200,UINT64_MAX,8}, {216,1,8},           // offset/size/padding
        {224,static_cast<std::uint32_t>(SectionKind::Fixture),4},
    }) {
        auto bad=original;Put(bad,at,value,count);
        if(at==224)Put(bad,232,7); // duplicate kind/id
        Checksums(bad);reject(bad);
    }
    auto appended=original;appended.push_back(0);Put(appended,24,appended.size());Checksums(appended);reject(appended);
    assert(!Load(path,archive.identity,loaded,error,Limits{256,64,4}) && loaded==archive);
    // Do not follow a symlink when reading an executable checkpoint.
    const auto link=std::filesystem::path(directory)/"link.bbstate";
    std::filesystem::create_symlink(path,link);assert(!Load(link,archive.identity,loaded,error) && loaded==archive);
    assert(!OpenVerified(link,archive.identity,verified,error));
    const auto stream_path=std::filesystem::path(directory)/"stream.bbstate";
    std::size_t calls=0,max_chunk=0;
    const auto pattern=[](std::uint64_t at){return std::uint8_t((at*31+(at>>17))&255);};
    StreamSection source{SectionKind::MemoryBytes,1,0x8000000000,5*1024*1024+37,
        [&](std::uint64_t at,std::span<std::uint8_t> bytes,std::string&){
            ++calls;max_chunk=std::max(max_chunk,bytes.size());
            for(auto& byte:bytes)byte=pattern(at++);return true;
        }};
    assert(SaveStream(stream_path,"stream-v1",std::span(&source,1),error));
    assert(calls==6 && max_chunk==(1<<20));
    VerifiedArchive streamed;assert(OpenVerified(stream_path,"stream-v1",streamed,error));
    std::array<std::uint8_t,2048> chunk;
    for(std::uint64_t offset:{0ull,1048557ull,5*1024*1024+37-2048ull}) {
        assert(streamed.Read(0,offset,chunk,error));
        for(auto byte:chunk)assert(byte==pattern(offset++));
    }
    assert(!streamed.Read(0,source.size-1,chunk,error));
    auto failing=source;failing.read=[](std::uint64_t at,std::span<std::uint8_t> bytes,std::string& error) {
        if(at){error="Injected stopped-memory provider failure.";return false;}
        std::fill(bytes.begin(),bytes.end(),0);return true;
    };
    assert(!SaveStream(stream_path,"stream-v1",std::span(&failing,1),error));
    assert(streamed.Read(0,source.size-chunk.size(),chunk,error));
    // Large metadata uses 64-bit sizes without allocating the section. The
    // provider deliberately fails before any payload can be published.
    failing.size=(1ull<<32)+123;bool reached=false;
    failing.read=[&](std::uint64_t at,std::span<std::uint8_t> bytes,std::string&){reached=true;assert(at==0 && bytes.size()==(1<<20));return false;};
    assert(!SaveStream(stream_path,"stream-v1",std::span(&failing,1),error,Limits{8ull<<30,8ull<<30,64}) && reached);
    assert(streamed.Read(0,0,chunk,error));
    // Detect later edits to an already verified inode before exposing bytes.
    {std::fstream edited(stream_path,std::ios::in|std::ios::out|std::ios::binary);edited.seekp(-1,std::ios::end);edited.put('X');assert(edited);}
    assert(!streamed.Read(0,0,chunk,error));
    assert(!OpenVerified(stream_path,"stream-v1",streamed,error));
    BbCheckpointCpu cpu{};cpu.features=1;cpu.rflags=0x202;cpu.rip=0x8001000010;cpu.gpr[BB_CK_RSP]=0x8002001000;
    assert(bb_checkpoint_cpu_valid(&cpu));cpu.fx[27]=0x80;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.fx[27]=0;
    cpu.rflags|=0x100;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.rflags=0x202;
    cpu.rflags|=0x400;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.rflags=0x202; // DF violates the import ABI
    cpu.rflags|=0x40000;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.rflags=0x202; // AC can fault native helpers
    cpu.gpr[BB_CK_RSP]++;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.gpr[BB_CK_RSP]--;
    cpu.reserved[0]=1;assert(!bb_checkpoint_cpu_valid(&cpu));cpu.reserved[0]=0;
    cpu.rip=UINT64_MAX;assert(!bb_checkpoint_cpu_valid(&cpu));
    std::filesystem::remove_all(directory);
    puts("Checkpoint archive: exact round trip, atomic write failures, bounded parsing, CPU validation and corrupt-file preservation passed");
}
