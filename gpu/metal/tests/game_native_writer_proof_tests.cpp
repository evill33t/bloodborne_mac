// SPDX-License-Identifier: GPL-2.0-or-later
// Public synthetic CPU fixtures; optional retail inputs stay outside source.
#include "gpu/metal/native_writer_proof.h"
#include "gpu/metal/native_writer_offsets.h"
#include "gpu/metal/native_worklist_extent.h"
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
u32 Vector(Vop op,u32 destination,u16 first,u16 second=Operand::Vgpr0) {
    return ((u32(op)-256)<<25)|(destination<<17)|((second-Operand::Vgpr0)<<9)|first;
}
u32 VectorMov(u32 destination,u16 first) {
    return 0x7e000000u|(destination<<17)|((u32(Vop::V_MOV_B32)-384)<<9)|first;
}
u32 Exec(u16 source) {
    return 0xbe800000u|(u32(Operand::ExecLo)<<16)|(u32(Sop1::S_MOV_B64)<<8)|source;
}
void Atomic(std::vector<u32>& words,u32 offset_register=2,u32 immediate=0) {
    words.push_back(0xe0003000u|(u32(Mubuf::BUFFER_ATOMIC_ADD)<<18)|immediate);
    words.push_back(0x80000400u|(1u<<16)|(offset_register-1));
}
std::unique_ptr<Program> OffsetProgram(std::vector<u32> words) {
    words.push_back(End);const std::array<u32,16> ud{};
    auto result=BuildProgram({.stage=Stage::Compute,.user_data=ud.data(),.num_user_sgprs=8,
        .vgpr_comp_cnt=2,.code=words.data(),.compute_rsrc2=0x1000});
    assert(result && !result->buffer_writes.empty());return result;
}
void OffsetSynthetic() {
    const auto check=[](const Program& program,std::array<u32,3> threads,std::vector<u32> expected,const char* label) {
        const auto proof=Writer::ProveOffsets(program,threads);
        if(!proof || proof.maximum!=expected) {
            std::fprintf(stderr,"%s accepted=%d reason=%s maximum=",label,bool(proof),proof.reason);
            for(const auto value:proof.maximum)std::fprintf(stderr,"%u,",value);
            std::fputc('\n',stderr);std::abort();
        }
    };
    const auto shift_z=Vector(Vop::V_LSHLREV_B32,2,Operand::Zero+6,Operand::Vgpr0+2);
    const auto mask_x=Vector(Vop::V_AND_B32,0,Operand::Zero+63,Operand::Vgpr0);
    const auto shift_x=Vector(Vop::V_LSHLREV_B32,2,Operand::Zero+4,Operand::Vgpr0);
    std::vector<u32> words{shift_z,Exec(Operand::Zero),mask_x,shift_x,Exec(Operand::IntNegMax-15)};
    Atomic(words);auto retained=OffsetProgram(words);
    // Bounding definitions may be skipped while inactive: the initial z<<6
    // value survives a later EXEC restore and MUST remain in the interval.
    check(*retained,{64,1,1},{1008},"inactive old zero and bounded shifted mask");
    check(*retained,{64,1,32},{1984},"inactive old z value survives reactivation");
    check(*retained,{64,1,65535},{4194176},"actual 16-bit dimension, no guessed 10-bit limit");

    words={VectorMov(27,Operand::Literal),0xfffffff0u,shift_z,Exec(Operand::Zero),mask_x,shift_x,
        Exec(Operand::IntNegMax-15),Vector(Vop::V_ADD_I32,27,Operand::Zero+4,Operand::Vgpr0+2)};
    Atomic(words,27);auto replaced=OffsetProgram(words);
    check(*replaced,{64,1,1},{1012},"same EXEC update replaces old unbounded value");
    words.insert(words.end()-2,Exec(Operand::IntNegMax-15));
    check(*OffsetProgram(words),{64,1,1},{0xfffffff0u},"any EXEC write prevents strong update");

    words={VectorMov(27,Operand::Literal),0xffffffffu,Branch(1),
        Vector(Vop::V_ADD_I32,27,Operand::Zero+4,Operand::Vgpr0+2)};
    Atomic(words,27);check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"branch can skip bounded assignment");
    words={VectorMov(2,Operand::Literal),0xffffffffu,
        Vector(Vop::V_ADD_I32,27,Operand::Zero+4,Operand::Vgpr0+2)};
    Atomic(words,27);check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"unsigned addition overflow is unproved");
    words={VectorMov(2,Operand::Literal),0xfffffff0u};Atomic(words,2,32);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"full byte offset overflow remains conservative");
    words={VectorMov(2,Operand::Literal),0xffffffffu,Exec(Operand::Zero),mask_x,shift_x,
        Exec(Operand::IntNegMax-15)};Atomic(words);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"inactive unbounded offset survives apparent mask definition");
    words={mask_x,shift_x};Atomic(words);
    words.push_back(VectorMov(2,Operand::Literal));words.push_back(0xffffffffu);
    words.push_back(Branch(-5,Sopp::S_CBRANCH_SCC0));
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"backedge brings unbounded offset to atomic");
    words={VectorMov(27,Operand::Literal),0xffffffffu,
        Vector(Vop::V_ADD_I32,27,Operand::Zero+4,Operand::Vgpr0+2),Load(126,0,4,Smrd::S_LOAD_DWORDX2)};
    Atomic(words,27);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"SMRD overlapping EXEC prevents strong update");
    words={VectorMov(27,Operand::Literal),0xffffffffu,
        Vector(Vop::V_ADD_I32,27,Operand::Zero+4,Operand::Vgpr0+2),
        0x7c000000u|(u32(Vop::V_CMPX_I32)<<17)|Operand::Zero};
    Atomic(words,27);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"implicit CMPX EXEC change retains prior value");
    words={mask_x,shift_x,0xe0002000u|(u32(Mubuf::BUFFER_LOAD_DWORDX4)<<18),
        0x80000000u|(1u<<16)};Atomic(words);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"multiword buffer load clobbers offset VGPR");
    words={mask_x,shift_x,0xf0000000u|(u32(Mimg::IMAGE_GATHER4)<<18)|(1u<<8),0};Atomic(words);
    check(*OffsetProgram(words),{64,1,1},{UINT32_MAX},"gather writes four VGPRs despite one-bit dmask");
    words={mask_x,shift_x};Atomic(words);auto context=OffsetProgram(words);
    check(*context,{64,1,1},{1008},"mask/shift offset");
    auto changed=*context;changed.code[1].neg=1;
    check(changed,{64,1,1},{UINT32_MAX},"source modifiers outside integer bound subset");
    changed=*context;changed.code[0].op=u16(Vop::V_MOVRELD_B32);
    assert(!Writer::ProveOffsets(changed,{64,1,1}));
    changed=*context;changed.stage=Stage::Vertex;assert(!Writer::ProveOffsets(changed,{64,1,1}));
    assert(!Writer::ProveOffsets(*context,{0,1,1}));assert(!Writer::ProveOffsets(*context,{64,1,65536}));
    Writer::Limits limits;limits.visits=0;assert(!Writer::ProveOffsets(*context,{64,1,1},limits));
    limits={};limits.queries=0;assert(!Writer::ProveOffsets(*context,{64,1,1},limits));
    limits={};limits.instructions=1;assert(!Writer::ProveOffsets(*context,{64,1,1},limits));
    limits={};limits.stores=0;assert(!Writer::ProveOffsets(*context,{64,1,1},limits));
    std::puts("native writer offsets: inactive retained values, unchanged EXEC, branches, overflow, context and bounds passed");
}
void WorklistSynthetic() {
    namespace W=Gpu::Metal::NativeWorklist;
    Program p;p.stage=Stage::Compute;p.hash=W::kHash;p.num_user_sgprs=16;p.compute_rsrc2=0xa0;
    p.has_branches=p.writes_exec=true;p.labels={72};
    const auto canonical=W::Instructions();p.code.assign(canonical.begin(),canonical.end());
    p.buffer_writes.resize(1);
    for(u32 k=0;k<4;++k)p.buffer_writes[0][k]={SgprSource::UserData,u8(12+k),0};
    assert(W::Matches(p));assert(Writer::Prove(p));
    auto changed=p;changed.hash^=1;assert(!W::Matches(changed));
    changed=p;changed.stage=Stage::Vertex;assert(!W::Matches(changed));
    changed=p;changed.num_user_sgprs=15;assert(!W::Matches(changed));
    changed=p;changed.compute_rsrc2^=1u<<8;assert(!W::Matches(changed));
    changed=p;changed.vgpr_comp_cnt=1;assert(!W::Matches(changed));
    changed=p;changed.writes_exec=false;assert(!W::Matches(changed));
    changed=p;changed.code.pop_back();assert(!W::Matches(changed));
    for(std::size_t k=0;k<p.code.size();++k) {
        changed=p;changed.code[k].op^=1;assert(!W::Matches(changed));
        changed=p;changed.code[k].pc+=4;assert(!W::Matches(changed));
        changed=p;changed.code[k].tfe=true;assert(!W::Matches(changed));
    }
    changed=p;changed.code[7].simm16=7;assert(!W::Matches(changed));
    changed=p;changed.code[13].srsrc=8;assert(!W::Matches(changed));
    changed=p;changed.code[2].vdata=0;assert(!W::Matches(changed));
    changed=p;changed.buffer_writes[0][0].ud=8;assert(!W::Matches(changed));
    assert(W::Layout(0x000c0080,0x00040080));
    assert(W::Layout(0x000cb080,0x00048080));
    assert(W::Layout(0x000cb080,0x0004b080));
    assert(!W::Layout(0x000cb080,0x00040080));
    assert(!W::Layout(0x000c0080,0x00048080));
    assert(!W::Layout(0x000c0080,0x0004b080));
    assert(!W::Layout(0x000cb080,0x0004f080));
    for(u32 flags:{0x1000u,0x2000u,0x4000u,0x8000u,0x40000000u,0x80000000u})
        assert(!W::Layout(0x000c0080|flags,0x00040080));
    assert(!W::Layout(0x000c0080,0x00041080));
    assert(!W::Layout(0x000c0080,0x00080080));
    const auto check=[](std::vector<W::Job> jobs,u32 records,std::vector<W::Indices> expected) {
        const auto plan=W::Bound(jobs,records);assert(plan && plan.indices==expected);
    };
    check({{100,0,0}},1000,{{100,100}}); // inclusive lane0, not an empty job
    for(u32 last:{63u,64u,UINT32_MAX})check({{100,0,last}},1000,{{100,163}});
    check({{100,0,63}},120,{{100,119}}); // actual V# idx clipping
    check({{100,0,63}},100,{});
    check({{100,0,63}},0,{});
    check({{200,0,0},{100,0,63},{164,0,36}},1000,{{100,200}}); // sorted/merged
    check({{UINT32_MAX,0,0}},UINT32_MAX,{});
    assert(!W::Bound(std::array<W::Job,1>{{{UINT32_MAX,0,1}}},1)); // overflow BEFORE clip
    assert(!W::Bound(std::array<W::Job,1>{{{UINT32_MAX-10,0,63}}},UINT32_MAX));
    assert(!W::Bound({},100));
    assert(!W::Bound(std::vector<W::Job>(W::kJobs+1),100));
    assert(W::Bound(std::vector<W::Job>(W::kJobs),100));
    std::vector<W::Job> many(W::kJobs);
    for(u32 k=0;k<many.size();++k)many[k]={1000+k,0,0};
    const auto all=W::Bound(many,10000);
    const std::vector<W::Indices> all_expected{{1000,1000+u32(W::kJobs)-1}};
    assert(all && all.indices==all_expected);
    many.back()={UINT32_MAX,0,1};assert(!W::Bound(many,1)); // final job must also be checked
    std::puts("native worklist CPU: complete shader/context, inclusive lanes, index clipping, preclip overflow, merging and2048job bound passed");
}
void OffsetCapture(const char* binary,u64 hash,u32 users,u32 rsrc2,std::array<u32,3> threads,std::size_t stores) {
    std::ifstream file(binary,std::ios::binary);if(!file)throw std::runtime_error("Cannot open private offset shader");
    const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.empty() || bytes.size()>65536 || bytes.size()%4)throw std::runtime_error("Invalid private offset code extent");
    std::vector<u32> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());
    const std::array<u32,16> ud{};
    auto program=BuildProgram({.stage=Stage::Compute,.user_data=ud.data(),.num_user_sgprs=users,
        .vgpr_comp_cnt=(rsrc2>>11)&3,.code=words.data(),.compute_rsrc2=rsrc2});
    if(!program || program->hash!=hash)throw std::runtime_error("Private offset shader identity differs");
    const auto descriptors=Writer::Prove(*program);const auto proof=Writer::ProveOffsets(*program,threads);
    std::printf("private offsets hash=%016llx descriptor_proof=%d stores=%zu accepted=%d reason=%s queries=%zu visits=%zu\n",
        hash,bool(descriptors),proof.maximum.size(),bool(proof),proof.reason,proof.queries,proof.visits);
    for(std::size_t k=0;k<proof.maximum.size();++k)std::printf("  writer=%zu maximum_byte_offset=%u\n",k,proof.maximum[k]);
    if(!descriptors || !proof || proof.maximum.size()!=stores)throw std::runtime_error("Private offset proof rejected");
    if(hash==0x26ec0808a6cd92dcull && threads==std::array<u32,3>{64,1,1}) {
        const std::array<u32,4> expected{1008,1012,1016,1020};
        if(proof.maximum.size()<expected.size() ||
            !std::equal(expected.begin(),expected.end(),proof.maximum.begin()))
            throw std::runtime_error("Private26ec atomic bounds differ from audited offsets");
    }
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
    if(program->hash==Gpu::Metal::NativeWorklist::kHash && !Gpu::Metal::NativeWorklist::Matches(*program))
        throw std::runtime_error("Private worklist full-program semantic identity differs");
    std::printf("private hash=%016llx stores=%zu queries=%zu visits=%zu accepted=%d reason=%s\n",
        program->hash,proof.stores,proof.queries,proof.visits,bool(proof),proof.reason);
    if(!proof || proof.stores!=stores)throw std::runtime_error("Private writer proof rejected or store count differed");
}
}
int main(int argc,char** argv) {
    try {
        Synthetic();OffsetSynthetic();WorklistSynthetic();
        if(argc==2 && !std::strcmp(argv[1],"--private"))return 77; // no bundled retail inputs
        if(argc==10 && !std::strcmp(argv[1],"--offset-capture")) {
            OffsetCapture(argv[2],std::stoull(argv[3],nullptr,16),u32(std::stoul(argv[4],nullptr,0)),
                u32(std::stoul(argv[5],nullptr,0)),{u32(std::stoul(argv[6],nullptr,0)),
                u32(std::stoul(argv[7],nullptr,0)),u32(std::stoul(argv[8],nullptr,0))},
                std::stoull(argv[9],nullptr,0));
        } else if(argc!=1) {
            if(argc!=7 || std::strcmp(argv[1],"--capture"))throw std::runtime_error("Usage: --capture CODE JSON USERS RSRC2 STORES");
            Capture(argv[2],argv[3],u32(std::stoul(argv[4],nullptr,0)),u32(std::stoul(argv[5],nullptr,0)),
                std::stoull(argv[6],nullptr,0));
        }
        return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"native writer proof: %s\n",error.what());return 1;}
}
