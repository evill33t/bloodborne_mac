// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/material_snapshot.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <unistd.h>

namespace {
void U32(std::vector<char>& bytes,std::size_t at,std::uint32_t n) {
    for(unsigned k=0;k<4;++k)bytes.at(at+k)=char(n>>(k*8));
}
void Checksum(std::vector<char>& bytes) {
    std::uint64_t hash=0xcbf29ce484222325ull;
    for(std::size_t k=0;k<bytes.size()-8;++k)hash=(hash^std::uint8_t(bytes[k]))*0x100000001b3ull;
    for(unsigned k=0;k<8;++k)bytes[bytes.size()-8+k]=char(hash>>(k*8));
}
}

int main() {
    namespace S=Gpu::Metal::MaterialSnapshot;
    char directory[]="/tmp/bb-material-snapshot-XXXXXX";assert(mkdtemp(directory));
    const auto path=std::filesystem::path(directory)/"blood.bbmat";
    S::Snapshot snapshot; snapshot.game_hash="fixture-build-1";
    S::Material material;material.key=S::Key(17,23,41);material.shader_hash=17;
    material.colour={2,2,71,{{64,0,7,0, 0,0,0,255, 17,2,1,127, 0,0,0,255},{24,0,3,159}}};
    material.surface={2,2,70,{{7,8,0,0, 0,0,0,0, 3,4,0,127, 0,0,0,0},{4,5,0,31}}};
    material.pixel_data[2]=0x1234;material.vertex_data[15]=0xabcdef;
    material.memory.push_back({0x8001234000,{1,2,3,4,5}});
    snapshot.materials.push_back(material);
    std::string error;assert(S::Save(path,snapshot,error));
    S::Snapshot loaded;assert(S::Load(path,snapshot.game_hash,loaded,error));assert(loaded==snapshot);
    assert(S::HasStains(loaded.materials.front().colour));
    assert(!S::HasStains({1,1,70,{{0,0,0,255}}}));
    assert(S::Key(17,23,41)==material.key && S::Key(17,23,42)!=material.key && !S::Key(17,0,41));
    // Preserve the old fixture on disk, and keep current loaded state intact
    // on wrong-game, damaged, or truncated inputs.
    const auto original=loaded;
    assert(!S::Load(path,"different-game",loaded,error) && loaded==original);
    assert(!S::Load(path,"",loaded,error) && loaded==original);
    auto changed=snapshot;changed.materials[0].colour.mips[0][0]=99;
    assert(S::Save(path,changed,error));
    S::Snapshot previous;assert(S::Load(path.string()+".previous",snapshot.game_hash,previous,error));assert(previous==snapshot);
    std::ifstream input(path,std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
    const auto valid_bytes=bytes;
    const auto bad=std::filesystem::path(directory)/"bad.bbmat";
    for(std::size_t length : {std::size_t(0),std::size_t(12),bytes.size()-1}) {
        {std::ofstream out(bad,std::ios::binary);out.write(bytes.data(),length);}
        assert(!S::Load(bad,snapshot.game_hash,loaded,error) && loaded==original);
    }
    bytes[bytes.size()/2]^=1;
    {std::ofstream out(bad,std::ios::binary);out.write(bytes.data(),bytes.size());}
    assert(!S::Load(bad,snapshot.game_hash,loaded,error) && loaded==original);
    // Valid checksums must not bypass structural checks. Exercise the reader,
    // independently of the writer's validation, with impossible field values.
    const std::size_t count_at=16+snapshot.game_hash.size();
    for(const auto [at,value] : {
        std::pair<std::size_t,std::uint32_t>{8,2}, // unsupported version
        {12,129},                                // overlong game identity
        {count_at,0},                            // empty material set
        {count_at,S::MaxMaterials+1},
        {count_at+20,1025},                       // oversized texture width
        {count_at+28,73},                         // unsupported pixel format
        {count_at+32,12},                         // too many mip levels
        {count_at+36,0xffffffffu}                 // impossible mip length
    }) {
        auto malformed=valid_bytes;U32(malformed,at,value);Checksum(malformed);
        {std::ofstream out(bad,std::ios::binary);out.write(malformed.data(),malformed.size());}
        assert(!S::Load(bad,snapshot.game_hash,loaded,error) && loaded==original);
    }
    auto trailing=valid_bytes;trailing.insert(trailing.end()-8,1,'x');Checksum(trailing);
    {std::ofstream out(bad,std::ios::binary);out.write(trailing.data(),trailing.size());}
    assert(!S::Load(bad,snapshot.game_hash,loaded,error) && loaded==original);
    auto invalid=changed;invalid.materials[0].colour.mips[1].pop_back();
    assert(!S::Save(path,invalid,error));
    assert(S::Load(path,snapshot.game_hash,loaded,error) && loaded==changed);
    invalid=changed;invalid.materials.push_back(invalid.materials.front());assert(!S::Save(path,invalid,error));
    invalid=changed;invalid.materials[0].surface.format=73;assert(!S::Save(path,invalid,error));
    invalid=changed;invalid.materials[0].memory[0].address=~std::uint64_t(0);assert(!S::Save(path,invalid,error));
    invalid=changed;invalid.game_hash.clear();assert(!S::Save(path,invalid,error));
    assert(S::Load(path,snapshot.game_hash,loaded,error) && loaded==changed);
    std::filesystem::remove_all(directory);
    puts("Material snapshot: lossless RGBA/mips/memory, identity, previous copy and invalid-input preservation passed");
}
