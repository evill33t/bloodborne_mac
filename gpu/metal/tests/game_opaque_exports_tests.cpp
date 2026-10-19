// SPDX-License-Identifier: GPL-2.0-or-later
// CFG regressions for removing only provably unreachable translator kills.
// Optional retail shader captures are read from argv; no game bytes are embedded.
#include "gpu/shader/msl.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() { std::abort(); }

namespace {
namespace Shader = Gpu::Shader;
using namespace Shader::Gcn;
using Shader::Program;

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

Inst Move(u16 destination,u16 source) {
    Inst i; i.enc=Enc::SOP1; i.op=static_cast<u16>(Sop1::S_MOV_B64);
    i.dst=destination; i.src[0]=source; return i;
}
Inst Export() {
    Inst i; i.enc=Enc::EXP; i.target=ExpTarget::Mrt0; i.en=15; return i;
}
Inst End() {
    Inst i; i.enc=Enc::SOPP; i.op=static_cast<u16>(Sopp::S_ENDPGM); return i;
}
Inst Branch(Sopp op,s16 relative) {
    Inst i; i.enc=Enc::SOPP; i.op=static_cast<u16>(op); i.simm16=relative; return i;
}
Inst SaveExec(u16 destination) {
    Inst i; i.enc=Enc::SOP1; i.op=static_cast<u16>(Sop1::S_AND_SAVEEXEC_B64);
    i.dst=destination; i.src[0]=Operand::VccLo; return i;
}
Inst Compare(u16 destination=Operand::VccLo,bool cmpx=false) {
    Inst i; i.enc=Enc::VOPC; i.op=cmpx ? 0x12 : 2; i.sdst=destination;
    i.src[0]=Operand::Vgpr0; i.src[1]=Operand::Zero; return i;
}
Program Make(std::initializer_list<Inst> instructions) {
    Program p; p.stage=Shader::Stage::Fragment; p.code=instructions;
    p.writes_exec=true; p.mrt_mask=1;
    for (size_t k=0;k<p.code.size();++k) {
        auto& i=p.code[k]; i.pc=static_cast<u32>(k*4); i.size=1;
        if (IsBranch(i)) { p.has_branches=true; p.labels.push_back(BranchTarget(i)); }
    }
    std::sort(p.labels.begin(),p.labels.end());
    p.labels.erase(std::unique(p.labels.begin(),p.labels.end()),p.labels.end());
    return p;
}

std::string RemoveArtificialKills(std::string text) {
    for (const auto* kill : {"if (exec_lo == 0u) discard_fragment();",
                            "if (!dc_exported) discard_fragment();"}) {
        for (size_t pos;(pos=text.find(kill))!=std::string::npos;)
            text.erase(pos,std::char_traits<char>::length(kill));
    }
    // The emitter also omits the otherwise-empty source line. Ignore whitespace
    // to verify that no arithmetic, memory access, branch or output was changed.
    text.erase(std::remove_if(text.begin(),text.end(),[](unsigned char c) {
        return c==' ' || c=='\t' || c=='\n' || c=='\r';
    }),text.end());
    return text;
}

void Check(const Program& p,bool expected,const char* label) {
    Require(Shader::FragmentExportsOpaque(p)==expected,std::string(label)+": incorrect proof result");
    Shader::MslOptions original; original.col_format=4;
    auto optimized=original; optimized.opaque_exports=true;
    std::vector<std::string> unsupported;
    const auto before=Shader::GenerateMsl(p,original,&unsupported);
    const auto after=Shader::GenerateMsl(p,optimized,&unsupported);
    if (expected) {
        Require(after.find("if (exec_lo == 0u) discard_fragment();")==std::string::npos,
                std::string(label)+": redundant EXEC kill retained");
        Require(after.find("if (!dc_exported) discard_fragment();")==std::string::npos,
                std::string(label)+": redundant termination kill retained");
        Require(RemoveArtificialKills(before)==RemoveArtificialKills(after),
                std::string(label)+": optimization changed an instruction/output");
        // Depth bounds has a real renderer-generated kill independent of EXEC.
        optimized.depth_bounds=true;
        const auto bounded=Shader::GenerateMsl(p,optimized,&unsupported);
        Require(bounded.find("if (z_ < bmin || z_ > bmax) discard_fragment();")!=std::string::npos,
                std::string(label)+": depth-bounds kill was lost");
        Require(after.find("early_fragment_tests")==std::string::npos,
                std::string(label)+": forced early depth/stencil tests");
    } else {
        Require(before==after,std::string(label)+": unproven shader was changed");
    }
}

void SyntheticCases() {
    Check(Make({Export(),End()}),true,"unconditional export");
    Check(Make({Move(64,Operand::ExecLo),Compare(),SaveExec(0),
                Branch(Sopp::S_CBRANCH_EXECZ,1),Move(Operand::ExecLo,Operand::Zero),
                Move(Operand::ExecLo,0),Export(),Move(Operand::ExecLo,64),Export(),End()}),
          true,"forward branch restores original EXEC");
    Check(Make({Compare(),SaveExec(0),Move(Operand::ExecLo,0),Export(),End()}),
          true,"dynamic condition restored before export");
    Check(Make({Branch(Sopp::S_CBRANCH_SCC0,2),Export(),End(),Export(),End()}),
          true,"both exits export");
    Check(Make({Branch(Sopp::S_BRANCH,1),Move(Operand::ExecLo,Operand::Zero),Export(),End()}),
          true,"unreachable kill does not affect proof");
    Check(Make({Export()}),true,"export before fallthrough exit");
    Check(Make({End()}),false,"no export");
    Check(Make({Move(Operand::ExecLo,Operand::Zero),Export(),End()}),false,"explicit kill");
    Check(Make({Compare(),SaveExec(0),Export(),End()}),false,"dynamic EXEC export");
    Check(Make({Branch(Sopp::S_CBRANCH_SCC0,1),Export(),End()}),false,"branch skips export");
    Check(Make({Export(),Branch(Sopp::S_BRANCH,-2)}),false,"back edge");
    Check(Make({Branch(Sopp::S_BRANCH,9),Export(),End()}),false,"invalid target");
    Check(Make({Compare(Operand::VccLo,true),Export(),End()}),false,"CMPX can kill");
    Check(Make({Move(Operand::ExecLo,Operand::Zero),Export(),
                Move(Operand::ExecLo,Operand::IntPosMax+1),Export(),End()}),
          false,"earlier kill is not canceled by a later export");
    Check(Make({Move(64,Operand::ExecLo),Compare(64),Move(Operand::ExecLo,64),Export(),End()}),
          false,"compare overwrites saved EXEC");
    Inst load; load.enc=Enc::SMRD; load.op=static_cast<u16>(Smrd::S_LOAD_DWORDX2); load.dst=64;
    Check(Make({Move(64,Operand::ExecLo),load,Move(Operand::ExecLo,64),Export(),End()}),
          false,"scalar memory overwrites saved EXEC");
    Inst carry; carry.enc=Enc::VOP3; carry.op=static_cast<u16>(Vop::V_ADD_I32); carry.sdst=64;
    Check(Make({Move(64,Operand::ExecLo),carry,Move(Operand::ExecLo,64),Export(),End()}),
          false,"vector carry overwrites saved EXEC");
    Inst read; read.enc=Enc::VOP1; read.op=static_cast<u16>(Vop::V_READFIRSTLANE_B32); read.dst=65;
    Check(Make({Move(64,Operand::ExecLo),read,Move(Operand::ExecLo,64),Export(),End()}),
          false,"lane read overwrites high saved EXEC");
    Check(Make({Move(64,Operand::ExecLo),Branch(Sopp::S_CBRANCH_SCC0,1),Move(64,Operand::Zero),
                Move(Operand::ExecLo,64),Export(),End()}),false,"merge loses full constant");
    auto vertex=Make({Export(),End()}); vertex.stage=Shader::Stage::Vertex;
    Require(!Shader::FragmentExportsOpaque(vertex),"non-fragment proof accepted");
    auto unknown=Make({Export(),End()}); unknown.code.front().enc=Enc::Unknown;
    Require(!Shader::FragmentExportsOpaque(unknown),"unknown instruction accepted");
    auto partial=Make({Move(Operand::ExecLo,Operand::Literal),Export(),End()});
    partial.code.front().literal=~0u;
    Check(partial,false,"full low word with zero high EXEC is not full");
    // Exhaust both arms of a merge with aliases of the saved EXEC low/high word.
    // A write on either arm must prevent proof, regardless of branch direction.
    for (u16 reg=64;reg<=65;++reg) {
        auto alias=Make({Move(64,Operand::ExecLo),Branch(Sopp::S_CBRANCH_SCC0,1),
                         Move(reg,Operand::Zero),Move(Operand::ExecLo,64),Export(),End()});
        Check(alias,false,"saved EXEC word alias across branch");
    }
}

std::unique_ptr<Program> Capture(const std::filesystem::path& directory,u64 hash) {
    char suffix[32]; std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path file;
    for (const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("ps_") && name.ends_with(suffix)) {
            file=entry.path(); break;
        }
    }
    Require(!file.empty(),std::string("Missing original fragment capture ")+suffix);
    const auto bytes=std::filesystem::file_size(file);
    Require(bytes>=8 && bytes<=65536 && !(bytes&3),"Invalid original fragment capture length");
    std::vector<u32> code(bytes/4+64,0xbf810000u);
    std::ifstream stream(file,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(code.data()),bytes)),"Cannot read fragment capture");
    std::array<u32,16> user{};
    // Bind exact original bytes and metadata, rather than accepting a different
    // shader under a convenient filename. Captures may vary in SGPR metadata.
    for (u32 sgprs=0;sgprs<=16;++sgprs) for (u32 components=0;components<4;++components) {
        Shader::BuildInput input{.stage=Shader::Stage::Fragment,.address=0,.user_data=user.data(),
            .num_user_sgprs=sgprs,.vgpr_comp_cnt=components,.code=code.data()};
        auto candidate=Shader::BuildProgram(input);
        if (candidate && candidate->hash==hash) return candidate;
    }
    throw std::runtime_error("Original fragment hash/metadata mismatch");
}
} // namespace

int main(int argc,char** argv) {
    try {
        SyntheticCases();
        std::puts("Opaque-export CFG positive/negative regressions passed.");
        if (argc==2) {
            Check(*Capture(argv[1],0x68fcb859536adcd4ull),true,"original G-buffer 68fc");
            Check(*Capture(argv[1],0xaff190d9945a53e0ull),true,"original G-buffer aff1");
            Check(*Capture(argv[1],0x50d8922180bb4fc9ull),false,"original masked G-buffer 50d8");
            std::puts("Original captured opaque/masked fragment proofs passed.");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"opaque-export fixture: %s\n",error.what()); return 1;
    }
}
