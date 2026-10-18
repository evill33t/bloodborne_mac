// SPDX-License-Identifier: GPL-2.0-or-later
#include "frontend_snapshot.h"
#include <cstring>
#include <set>
namespace Gpu::Metal::FrontendSnapshot {
namespace {
using State=Liverpool::CommandProcessor::CheckpointState;
constexpr std::uint64_t Registers=Liverpool::RegFile::kSize,CeWords=0xc000/4;
bool Valid(const State& s,std::string& error) {
    if(s.version!=1 || (s.index_size!=2 && s.index_size!=4) || s.index_base>=1ull<<40 ||
       s.registers.size()!=Registers || s.ce_ram.size()!=CeWords || s.rings.size()>4096) {
        error="Invalid GPU frontend checkpoint layout.";return false;
    }
    std::set<std::uint32_t> ids;
    for(const auto& r:s.rings)if(r.index>65535 || r.indirect_base>=1ull<<40 || r.registers.size()!=Registers || !ids.insert(r.index).second) {
        error="Invalid GPU frontend checkpoint ring.";return false;
    }
    return true;
}
}
bool Encode(const State& s,std::vector<std::uint8_t>& out,std::string& error) {
    if(!Valid(s,error))return false;
    std::vector<std::uint8_t> bytes{'B','B','C','P','F','E','1',0};
    const auto put=[&](std::uint64_t v,unsigned count=4){for(unsigned k=0;k<count;++k)bytes.push_back(std::uint8_t(v>>(8*k)));};
    put(s.version);put(s.index_size);put(s.num_instances);put(s.rings.size());put(s.index_base,8);put(s.ce_counter,8);put(s.de_counter,8);
    for(auto v:s.registers)put(v);for(auto v:s.ce_ram)put(v);
    for(const auto& r:s.rings){put(r.index);put(0);put(r.indirect_base,8);for(auto v:r.registers)put(v);}
    out=std::move(bytes);error.clear();return true;
}
bool Decode(std::span<const std::uint8_t> bytes,State& out,std::string& error) {
    const auto fail=[&](const char* why){error=why;return false;};
    constexpr std::uint64_t Base=48+4*(Registers+CeWords),RingSize=16+4*Registers;
    if(bytes.size()<Base || std::memcmp(bytes.data(),"BBCPFE1",8))return fail("Invalid GPU frontend checkpoint signature.");
    std::size_t offset=8;
    const auto get=[&](unsigned count=4){std::uint64_t v=0;for(unsigned k=0;k<count;++k)v|=std::uint64_t(bytes[offset++])<<(8*k);return v;};
    State s;s.version=get();s.index_size=get();s.num_instances=get();const auto rings=get();
    if(rings>4096 || bytes.size()!=Base+RingSize*rings)return fail("Invalid GPU frontend checkpoint size.");
    s.index_base=get(8);s.ce_counter=get(8);s.de_counter=get(8);
    s.registers.resize(Registers);s.ce_ram.resize(CeWords);
    for(auto& v:s.registers)v=get();for(auto& v:s.ce_ram)v=get();
    for(std::size_t n=0;n<rings;++n) {
        Liverpool::CommandProcessor::CheckpointRing r;r.index=get();
        if(get())return fail("Invalid GPU frontend reserved field.");r.indirect_base=get(8);r.registers.resize(Registers);
        for(auto& v:r.registers)v=get();s.rings.push_back(std::move(r));
    }
    if(!Valid(s,error))return false;out=std::move(s);error.clear();return true;
}
}
