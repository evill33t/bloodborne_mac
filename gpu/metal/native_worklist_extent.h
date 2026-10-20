// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/metal/native_writer_proof.h"
#include <span>
#include <tuple>

namespace Gpu::Metal::NativeWorklist {
using namespace Shader::Gcn;
inline constexpr u64 kHash=0x7cf42796921ac73eull;
inline constexpr std::size_t kJobs=2048;
struct Job {u32 destination,source,last_lane;};
static_assert(sizeof(Job)==12);
struct Indices {u32 first,last;bool operator==(const Indices&) const=default;}; // inclusive
struct Plan {
    bool accepted=false;
    const char* reason="unproved";
    std::vector<Indices> indices;
    explicit operator bool() const {return accepted;}
};
inline bool Layout(u32 table,u32 destination) {
    // The translated buffer ABI uses low12 base bits and bits16..29 stride.
    // Retain only the exact captured flag patterns, or the zero-flag form
    // used by the public fixture. Other currently ignored hardware flags
    // remain on the complete-descriptor fallback.
    const u32 input=table&0xfffff000u,output=destination&0xfffff000u;
    return (input==0x000c0000u && output==0x00040000u) ||
        (input==0x000cb000u && (output==0x00048000u || output==0x0004b000u));
}

// Semantic instructions, not embedded retail bytes. Every decoded field is
// compared as well as the complete shader hash, stage and system-input ABI.
inline std::array<Inst,15> Instructions() {
    std::array<Inst,15> c{};
    const auto instruction=[&](unsigned at,u32 pc,Enc enc,u16 op,u8 size=1)->Inst& {
        auto& i=c[at];i.pc=pc;i.enc=enc;i.op=op;i.size=size;return i;
    };
    auto* i=&instruction(0,0,Enc::SOP1,u16(Sop1::S_MOV_B32),2);
    i->dst=Operand::VccHi;i->src[0]=Operand::Literal;i->literal=12;
    i=&instruction(1,8,Enc::VOP1,u16(Vop::V_MOV_B32));i->dst=1;i->src[0]=16;
    i=&instruction(2,12,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_FORMAT_XYZ),2);
    i->srsrc=8;i->idxen=true;i->soffset=Operand::Zero;i->vaddr=1;i->vdata=1;
    i=&instruction(3,20,Enc::SOPP,u16(Sopp::S_WAITCNT));i->simm16=0xf70;
    i=&instruction(4,24,Enc::VOPC,0xc6);i->sdst=Operand::VccLo;
    i->src[0]=Operand::Vgpr0+3;i->src[1]=Operand::Vgpr0;
    i=&instruction(5,28,Enc::SOP1,u16(Sop1::S_AND_SAVEEXEC_B64));i->src[0]=Operand::VccLo;
    i=&instruction(6,32,Enc::VOP2,u16(Vop::V_ADD_I32));i->dst=2;i->sdst=Operand::VccLo;
    i->src[0]=Operand::Vgpr0;i->src[1]=Operand::Vgpr0+2;
    i=&instruction(7,36,Enc::SOPP,u16(Sopp::S_CBRANCH_EXECZ));i->simm16=8;
    i=&instruction(8,40,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_FORMAT_X),2);
    i->srsrc=4;i->idxen=true;i->soffset=Operand::Zero;i->vaddr=2;i->vdata=2;
    i=&instruction(9,48,Enc::VOP2,u16(Vop::V_ADD_I32));i->sdst=Operand::VccLo;
    i->src[0]=Operand::Vgpr0;i->src[1]=Operand::Vgpr0+1;
    i=&instruction(10,52,Enc::VOPC,0xc4);i->sdst=Operand::VccLo;
    i->src[0]=14;i->src[1]=Operand::Vgpr0;
    i=&instruction(11,56,Enc::SOP1,u16(Sop1::S_AND_SAVEEXEC_B64));i->src[0]=Operand::VccLo;
    i=&instruction(12,60,Enc::SOPP,u16(Sopp::S_WAITCNT));i->simm16=0xf70;
    i=&instruction(13,64,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_FORMAT_X),2);
    i->srsrc=12;i->idxen=true;i->soffset=Operand::Zero;i->vdata=2;
    instruction(14,72,Enc::SOPP,u16(Sopp::S_ENDPGM));return c;
}
inline bool Same(const Inst& a,const Inst& b) {
    const auto fields=[](const Inst& i) {
        return std::tie(i.pc,i.enc,i.size,i.op,i.dst,i.sdst,i.src[0],i.src[1],i.src[2],i.literal,i.simm16,
            i.abs,i.neg,i.omod,i.clamp,i.vop3,i.sbase,i.imm,i.smrd_offset,i.attr,i.chan,
            i.offset,i.offen,i.idxen,i.glc,i.slc,i.addr64,i.lds,i.dfmt,i.nfmt,i.vaddr,i.vdata,
            i.srsrc,i.ssamp,i.soffset,i.dmask,i.unorm,i.da,i.r128,i.tfe,i.lwe,i.offset0,i.offset1,
            i.gds,i.data0,i.data1,i.en,i.target,i.compr,i.done,i.vm,i.vsrc[0],i.vsrc[1],i.vsrc[2],i.vsrc[3]);
    };
    return fields(a)==fields(b);
}
inline bool Matches(const Shader::Program& p) {
    if(p.hash!=kHash || p.stage!=Shader::Stage::Compute || p.num_user_sgprs!=16 || p.vgpr_comp_cnt ||
        p.compute_rsrc2!=0xa0 || !p.has_branches || !p.writes_exec || p.fetch_address ||
        p.fetch_ud>=0 || p.fetch_bytes || !p.images.empty() || !p.samplers.empty() ||
        !p.unsupported.empty() || p.labels!=std::vector<u32>{72} ||
        p.code.size()!=15 || p.buffer_writes.size()!=1)return false;
    for(u32 word=0;word<4;++word)
        if(p.buffer_writes[0][word]!=Shader::SgprSource{Shader::SgprSource::UserData,u8(12+word),0})return false;
    const auto expected=Instructions();
    for(std::size_t k=0;k<expected.size();++k)if(!Same(p.code[k],expected[k]))return false;
    return true;
}
inline Plan Bound(std::span<const Job> jobs,u32 records) {
    Plan p;
    if(jobs.empty() || jobs.size()>kJobs){p.reason="worklist-job-bound";return p;}
    for(const auto& job:jobs) {
        // CMP_GE_U32 admits lane<=last_lane, including lane0 for limit0.
        // Addition happens in uint before the descriptor's idx<records test.
        const u64 last=u64(job.destination)+std::min(job.last_lane,63u);
        if(last>UINT32_MAX){p.reason="worklist-index-overflow";return p;}
        if(job.destination>=records)continue;
        p.indices.push_back({job.destination,std::min(u32(last),records-1)});
    }
    std::sort(p.indices.begin(),p.indices.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::size_t count=0;
    for(const auto& range:p.indices) {
        if(count && u64(range.first)<=u64(p.indices[count-1].last)+1)
            p.indices[count-1].last=std::max(p.indices[count-1].last,range.last);
        else p.indices[count++]=range;
    }
    p.indices.resize(count);p.accepted=true;p.reason="proven-worklist";return p;
}
} // namespace Gpu::Metal::NativeWorklist
