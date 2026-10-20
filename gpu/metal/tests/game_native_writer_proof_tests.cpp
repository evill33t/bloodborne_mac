// SPDX-License-Identifier: GPL-2.0-or-later
// Public synthetic CPU fixtures; optional retail inputs stay outside source.
#include "gpu/metal/native_writer_proof.h"
#include "core/memory.h"
#include "gpu/metal/vendor/bbhost/third_party/nlohmann/json.hpp"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace Core::Memory {
u64 RuntimeDirectSize(){return 0;}
bool Query(VAddr,bool,Vma*){return false;}
}
namespace {
using namespace Gpu::Shader;
using namespace Gpu::Shader::Gcn;
namespace Writer=Gpu::Metal::NativeWriter;
constexpr u32 End=0xbf810000;
u32 Load(u32 destination,u32 root=0,u32 offset=4,Smrd op=Smrd::S_LOAD_DWORDX4) {
    return 0xc0000000u|(u32(op)<<22)|(destination<<15)|((root/2)<<9)|0x100u|offset;
}
u32 Branch(s16 offset,Sopp op=Sopp::S_CBRANCH_EXECZ){return 0xbf800000u|(u32(op)<<16)|u16(offset);}
void Store(std::vector<u32>& words,u32 resource=4) {
    words.push_back(0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18));
    words.push_back(0x80000100u|((resource/4)<<16));
}
std::unique_ptr<Program> Build(std::vector<u32> words,u32 users=8) {
    if(words.empty() || words.back()!=End)words.push_back(End);
    const std::array<u32,16> ud{};
    auto program=BuildProgram({.stage=Stage::Compute,.user_data=ud.data(),.num_user_sgprs=users,.code=words.data()});
    assert(program && program->buffer_writes.size()==1);return program;
}
void Expect(const Program& program,bool accepted,const char* label,Writer::Limits limits={}) {
    const auto proof=Writer::Prove(program,limits);
    if(bool(proof)!=accepted) {
        std::fprintf(stderr,"%s expected=%d actual=%d reason=%s queries=%zu visits=%zu\n",
            label,accepted,bool(proof),proof.reason,proof.queries,proof.visits);std::abort();
    }
}
void Synthetic() {
    std::vector<u32> words{Branch(3)};Store(words);words.push_back(Branch(-3,Sopp::S_CBRANCH_EXECNZ));words.push_back(End);
    const auto direct=Build(words);Expect(*direct,true,"invariant direct UD through branches/backedge");
    words={Load(4),Branch(3)};Store(words);words.push_back(Branch(-3,Sopp::S_CBRANCH_EXECNZ));words.push_back(End);
    const auto pointer=Build(words,2);Expect(*pointer,true,"dominating X4 and unchanged entry roots");
    words={Load(4)};Store(words);words.push_back(Branch(-4,Sopp::S_CBRANCH_EXECNZ));words.push_back(End);
    Expect(*Build(words,2),true,"loop over X4 with invariant non-overlapping roots");
    words={Load(0)};Store(words,0);const auto self_root=Build(words,2);
    Expect(*self_root,true,"same instruction reads entry root before overwriting it");
    words={Load(0)};Store(words,0);words.push_back(Branch(-4,Sopp::S_CBRANCH_EXECNZ));words.push_back(End);
    Expect(*Build(words,2),false,"root overwritten on loopback");

    words={Branch(1),Load(4)};Store(words);Expect(*Build(words,2),false,"branch skips expected definition");
    words={Load(4),Branch(1),Load(4,0,8)};Store(words);
    Expect(*Build(words,2),false,"alternate definition reaches store");
    words={Load(4)};Store(words);words.push_back(Load(4,0,8));
    words.push_back(Branch(-4,Sopp::S_CBRANCH_EXECNZ));words.push_back(End);
    Expect(*Build(words,2),false,"backedge brings another definition");
    const u32 memtime=Load(0,0,0,Smrd::S_MEMTIME);
    words={memtime,Load(4)};Store(words);Expect(*Build(words,2),false,"straight-line root MEMTIME clobber");
    for(const auto op:{Smrd::S_LOAD_DWORDX8,Smrd::S_LOAD_DWORDX16}) {
        words={Load(4,0,4,op)};Store(words);Expect(*Build(words,2),false,"wider SMRD footprint outside exact-X4 subset");
    }
    words={Load(4,0,4,Smrd::S_LOAD_DWORDX2)};Store(words);
    Expect(*Build(words),false,"mixed partial pointer and original UD descriptor");
    words={Load(4),Load(5,2,4,Smrd::S_LOAD_DWORD)};Store(words);
    Expect(*Build(words),false,"mixed independent SMRD definitions");
    words={Load(4,0,4,Smrd::S_BUFFER_LOAD_DWORDX4)};Store(words);
    Expect(*Build(words),false,"buffer-load source V# outside proved subset");

    // Deliberately retain the old expected entry tag: the independent scalar
    // write classifier must reject each decoded clobber, regardless of what
    // the separate linear provenance pass happens to remember.
    const std::vector<std::vector<u32>> clobbers{
        {Load(4,0,0,Smrd::S_MEMTIME)},
        {0xb0000000u|(u32(Sopk::S_GETREG_B32)<<23)|(4u<<16)},
        {0x7e000000u|(4u<<17)|((u32(Vop::V_READFIRSTLANE_B32)-384)<<9)|Operand::Vgpr0},
        {((u32(Vop::V_READLANE_B32)-256)<<25)|(4u<<17)|Operand::Vgpr0},
        {0xd0000000u|(u32(Vop::V_ADD_I32)<<17)|(3u<<8)|16u,Operand::Zero|(u32(Operand::Zero)<<9)},
        {0xbe800000u|(4u<<16)|(u32(Sop1::S_CMOV_B32)<<8)|Operand::Zero}
    };
    for(auto code:clobbers) {
        Store(code);auto program=Build(code);
        for(unsigned k=0;k<4;++k)program->buffer_writes[0][k]={SgprSource::UserData,u8(4+k),0};
        Expect(*program,false,"scalar/vector/mask destination clobber");
    }

    auto changed=*pointer;changed.code[0].op=u16(Smrd::S_LOAD_DWORDX8);
    Expect(changed,false,"mutated wider load");
    changed=*pointer;changed.buffer_writes[0][1].offset+=4;Expect(changed,false,"incorrect expected byte offset");
    changed=*direct;changed.code.pop_back();Expect(changed,false,"missing terminating END");
    changed=*direct;changed.code[0].simm16=1;Expect(changed,false,"branch into second instruction dword");
    changed=*direct;changed.code[0].simm16=-2;Expect(changed,false,"negative out-of-program branch");
    changed=*direct;changed.code[0].simm16=32767;Expect(changed,false,"forward out-of-program branch");
    changed=*direct;changed.code[0].enc=Enc::SOP1;changed.code[0].op=u16(Sop1::S_SETPC_B64);
    Expect(changed,false,"indirect target");
    changed.code[0].op=u16(Sop1::S_SWAPPC_B64);Expect(changed,false,"call target");
    changed=*direct;changed.fetch_address=0x1000;Expect(changed,false,"inlined fetch program");
    changed=*direct;changed.code[1].pc+=4;Expect(changed,false,"noncontiguous decoded prefix");
    changed=*direct;changed.code[0].enc=Enc::SOP2;changed.code[0].op=65535;
    Expect(changed,false,"unknown scalar opcode");
    Writer::Limits limits;limits.instructions=1;Expect(*pointer,false,"instruction bound",limits);
    limits={};limits.stores=0;Expect(*pointer,false,"store bound",limits);
    limits={};limits.queries=0;Expect(*pointer,false,"query bound",limits);
    limits={};limits.visits=0;Expect(*pointer,false,"walk visit bound",limits);
    std::puts("native writer CPU: dominance, roots, alternate/backedge definitions, complete CFG, clobbers and hard bounds passed");
}
void Capture(const char* binary,const char* metadata,u32 users,u32 rsrc2,std::size_t stores) {
    std::ifstream raw(binary,std::ios::binary);if(!raw)throw std::runtime_error("Cannot open private shader input");
    std::vector<char> bytes((std::istreambuf_iterator<char>(raw)),{});
    if(bytes.empty() || bytes.size()>65536 || bytes.size()%4)throw std::runtime_error("Invalid private code byte extent");
    std::vector<u32> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    std::ifstream json_file(metadata);if(!json_file)throw std::runtime_error("Cannot open private metadata");
    const auto metadata_json=nlohmann::json::parse(json_file);
    const auto values=metadata_json.at("user_data").get<std::vector<u32>>();
    if(values.size()!=16)throw std::runtime_error("Expected 16 immutable capture UD words");
    BuildInput input{.stage=Stage::Compute,.address=metadata_json.at("shader_address").get<u64>(),
        .user_data=values.data(),.num_user_sgprs=users,.vgpr_comp_cnt=(rsrc2>>11)&3,
        .code=words.data(),.compute_rsrc2=rsrc2};
    auto program=BuildProgram(input);if(!program)throw std::runtime_error("Cannot decode private shader input");
    if(program->hash!=metadata_json.at("shader_hash").get<u64>())throw std::runtime_error("Capture shader identity mismatch");
    const auto proof=Writer::Prove(*program);
    std::printf("private hash=%016llx stores=%zu queries=%zu visits=%zu accepted=%d reason=%s\n",
        program->hash,proof.stores,proof.queries,proof.visits,bool(proof),proof.reason);
    if(!proof || proof.stores!=stores)throw std::runtime_error("Private writer proof rejected or store count differed");
}
}
int main(int argc,char** argv) {
    try {
        Synthetic();
        if(argc==2 && !std::strcmp(argv[1],"--private"))return 77; // no bundled retail inputs
        if(argc!=1) {
            if(argc!=7 || std::strcmp(argv[1],"--capture"))throw std::runtime_error("Usage: --capture CODE JSON USERS RSRC2 STORES");
            Capture(argv[2],argv[3],u32(std::stoul(argv[4],nullptr,0)),u32(std::stoul(argv[5],nullptr,0)),
                std::stoull(argv[6],nullptr,0));
        }
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"native writer proof: %s\n",error.what());return 1;}
}
