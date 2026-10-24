// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Exact retail shadow HTILE clear, preserving the original compute execution.
// This is NOT a general HTILE decoder. A full native depth clear is equivalent
// only after proving that the original kernel will replace EVERY word. Its
// low-nibble-13 preservation branch must never be silently discarded.
#include "gpu/shader/program.h"
#include "src/runtime_gpu_memory.h"
#include <array>
#include <bit>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Gpu::Metal::ShadowHtileClear {
using Word=std::uint32_t;
using Address=std::uint64_t;
inline constexpr Address ShaderHash=0x595f38bd3818a17bull;
inline constexpr Word Width=4096,Height=4096,Words=Width*Height/64;
inline constexpr Address MetadataBytes=Address(Words)*4;
// Live original CS14 probes in two frames: count=0x40000, value=0xfffffff0.
// DB_DEPTH_CLEAR is 1.0. Do not broaden to a guessed min/max encoding.
inline constexpr Word ClearWord=0xfffffff0u,DepthClearWord=0x3f800000u;
inline constexpr Address DepthBytes=Address(Width)*Height*4;
struct MappingSpan {
    Address address=0,bytes=0,physical=0,identity=0;
    bool operator==(const MappingSpan&) const=default;
};
inline void AppendMappingSpan(std::vector<MappingSpan>& spans,MappingSpan next) {
    if(!spans.empty()) {
        auto& last=spans.back();
        if(last.address+last.bytes==next.address && last.physical+last.bytes==next.physical && last.identity==next.identity) {
            last.bytes+=next.bytes;return;
        }
    }
    spans.push_back(next);
}

inline auto Fields(const Shader::Gcn::Inst& i) {
    // Compare decoded values rather than padding in the C++ instruction struct.
    return std::array{Address(i.pc),Address(i.enc),Address(i.size),Address(i.op),Address(i.dst),Address(i.sdst),
        Address(i.src[0]),Address(i.src[1]),Address(i.src[2]),Address(i.literal),Address(std::uint16_t(i.simm16)),
        Address(i.abs),Address(i.neg),Address(i.omod),Address(i.clamp),Address(i.vop3),Address(i.sbase),Address(i.imm),
        Address(i.smrd_offset),Address(i.attr),Address(i.chan),Address(i.offset),Address(i.offen),Address(i.idxen),
        Address(i.glc),Address(i.slc),Address(i.addr64),Address(i.lds),Address(i.dfmt),Address(i.nfmt),Address(i.vaddr),
        Address(i.vdata),Address(i.srsrc),Address(i.ssamp),Address(i.soffset),Address(i.dmask),Address(i.unorm),
        Address(i.da),Address(i.r128),Address(i.tfe),Address(i.lwe),Address(i.offset0),Address(i.offset1),Address(i.gds),
        Address(i.data0),Address(i.data1),Address(i.en),Address(i.target),Address(i.compr),Address(i.done),Address(i.vm),
        Address(i.vsrc[0]),Address(i.vsrc[1]),Address(i.vsrc[2]),Address(i.vsrc[3])};
}
inline std::array<Shader::Gcn::Inst,19> OriginalShape() {
    using namespace Shader::Gcn;
    std::array<Inst,19> a{};
    const auto set=[&](unsigned n,Word pc,Enc encoding,Word opcode) -> Inst& {
        auto& i=a[n];i.pc=pc;i.enc=encoding;i.op=opcode;return i;
    };
    auto& id=set(0,0,Enc::SOP1,Word(Sop1::S_MOV_B32));
    id.dst=Operand::VccHi;id.src[0]=Operand::Literal;id.literal=13;id.size=2;
    auto& group=set(1,8,Enc::SOP2,Word(Sop2::S_LSHL_B32));
    group.dst=0;group.src[0]=12;group.src[1]=Operand::Zero+6;
    auto& tid=set(2,12,Enc::VOP2,Word(Vop::V_ADD_I32));
    tid.dst=0;tid.src[0]=0;tid.src[1]=Operand::Vgpr0;tid.sdst=Operand::VccLo;
    auto& uniform=set(3,16,Enc::SMRD,Word(Smrd::S_BUFFER_LOAD_DWORDX2));
    uniform.dst=0;uniform.sbase=8;uniform.imm=true;
    set(4,20,Enc::SOPP,Word(Sopp::S_WAITCNT)).simm16=0x7f;
    const auto compare=[&](unsigned n,Word pc,Word opcode,Word first,Word second) {
        auto& i=set(n,pc,Enc::VOPC,opcode);i.sdst=Operand::VccLo;i.src[0]=first;i.src[1]=second;
    };
    compare(5,24,196,0,Operand::Vgpr0); // unsigned count > global thread index
    const auto save=[&](unsigned n,Word pc,Word dst) {
        auto& i=set(n,pc,Enc::SOP1,Word(Sop1::S_AND_SAVEEXEC_B64));i.dst=dst;i.src[0]=Operand::VccLo;
    };
    save(6,28,2);
    set(7,32,Enc::SOPP,Word(Sopp::S_CBRANCH_EXECZ)).simm16=12; // -> END at 0x54
    const auto memory=[&](unsigned n,Word pc,Word opcode) {
        auto& i=set(n,pc,Enc::MUBUF,opcode);i.size=2;i.idxen=true;i.vaddr=0;i.vdata=1;
        i.srsrc=4;i.soffset=Operand::Zero;
    };
    memory(8,36,Word(Mubuf::BUFFER_LOAD_FORMAT_X));
    set(9,44,Enc::SOPP,Word(Sopp::S_WAITCNT)).simm16=0xf70;
    auto& low=set(10,48,Enc::VOP2,Word(Vop::V_AND_B32));
    low.dst=1;low.src[0]=Operand::Zero+15;low.src[1]=Operand::Vgpr0+1;low.sdst=Operand::VccLo;
    compare(11,52,133,Operand::Zero+13,Operand::Vgpr0+1); // preserve low nibble 13
    save(12,56,2);
    compare(13,60,196,6,Operand::Vgpr0); // descriptor bounds
    set(14,64,Enc::SOPP,Word(Sopp::S_CBRANCH_EXECZ)).simm16=4; // -> END
    auto& value=set(15,68,Enc::VOP1,Word(Vop::V_MOV_B32));value.dst=1;value.src[0]=1;
    save(16,72,0);
    memory(17,76,Word(Mubuf::BUFFER_STORE_FORMAT_X));
    set(18,84,Enc::SOPP,Word(Sopp::S_ENDPGM));
    return a;
}
inline bool ProgramEligible(const Shader::Program& p) {
    if(p.stage!=Shader::Stage::Compute || p.hash!=ShaderHash || p.num_user_sgprs!=12 ||
       p.vgpr_comp_cnt || p.compute_rsrc2!=0x98 || p.fetch_bytes || p.fetch_address ||
       !p.images.empty() || !p.unsupported.empty() || !p.has_branches || !p.writes_exec || p.code.size()!=19)return false;
    const auto expected=OriginalShape();
    for(unsigned n=0;n<expected.size();++n)if(Fields(p.code[n])!=Fields(expected[n]))return false;
    return true;
}
inline Address Base(std::span<const Word,4> descriptor) {
    return Address(descriptor[0])|(Address(descriptor[1]&0xffu)<<32);
}
struct Surface {
    Address metadata=0,target=0,mapping_generation=0;
    Word width=0,height=0,pitch=0,padded_height=0,levels=0,layers=0,matching_targets=0;
    bool initialized=false,depth32=false,htile=false;
    Word depth_clear_word=0;
};
struct CpuReadProof {
    // The renderer must establish readability, current CP read predicate,
    // disjoint queued/active callback effects and no pending GPU writer using
    // CpuResourceRead (or an equally strong immutable snapshot proof). No wait.
    Address address=0,bytes=0,generation_before=0,generation_after=0;
    bool ready=false;
};
struct GpuReadProof {
    // The complete input is mapped in the actual consumer's ordered queue.
    // CP stores, queued/active callback writes, unknown GPU writers and map
    // changes are excluded, and its physical range is leased through completion.
    Address address=0,bytes=0,generation_before=0,generation_after=0;
    bool ready=false;
};
struct Plan {Address metadata,target,generation;Word depth_clear_word;};
enum class Reject {None,Program,Surface,Dispatch,MetadataDescriptor,UniformDescriptor,Readiness,Uniform,PreservedTiles,InputAlias};
struct Decision {std::optional<Plan> plan;Reject reason=Reject::None;};
inline Decision RecognizeShape(const Shader::Program& program,std::span<const Word,16> user,
    std::array<Word,3> groups,std::array<Word,3> threads,bool indirect,const Surface& surface) {
    const auto no=[](Reject why) {return Decision{{},why};};
    if(!ProgramEligible(program))return no(Reject::Program);
    if(!surface.metadata || !surface.target || !surface.mapping_generation || !surface.initialized ||
       !surface.depth32 || !surface.htile || surface.width!=Width || surface.height!=Height ||
       surface.pitch!=Width || surface.padded_height!=Height || surface.levels!=1 || surface.layers!=1 ||
       surface.matching_targets!=1 || surface.depth_clear_word!=DepthClearWord)return no(Reject::Surface);
    if(indirect || groups!=std::array<Word,3>{4096,1,1} || threads!=std::array<Word,3>{64,1,1})return no(Reject::Dispatch);
    const auto target=user.subspan<4,4>(),control=user.subspan<8,4>();
    // Exact observed plain R32_UINT metadata layout. No address swizzling,
    // ADD_TID or descriptor variants are inferred from a matching address.
    if((target[0]&3) || (target[1]&0xffffff00u)!=0x00043000u || target[2]!=Words ||
       target[3]!=0x00024004u || Base(target)!=surface.metadata ||
       Base(target)>((1ull<<40)-MetadataBytes))return no(Reject::MetadataDescriptor);
    if((control[0]&15) || (control[1]&0xffffff00u)!=0x00108000u || control[2]!=1 ||
       control[3]!=0x00077facu || !Base(control) || Base(control)>((1ull<<40)-8))return no(Reject::UniformDescriptor);
    if(Base(control)<surface.metadata+MetadataBytes && surface.metadata<Base(control)+8)return no(Reject::InputAlias);
    return {{Plan{surface.metadata,surface.target,surface.mapping_generation,DepthClearWord}},Reject::None};
}
inline Decision Recognize(const Shader::Program& program,std::span<const Word,16> user,
    std::array<Word,3> groups,std::array<Word,3> threads,bool indirect,const Surface& surface,
    const CpuReadProof& uniform_proof,std::array<Word,2> uniform,
    const CpuReadProof& metadata_proof,std::span<const Word> metadata) {
    auto shape=RecognizeShape(program,user,groups,threads,indirect,surface);
    if(!shape.plan)return shape;
    const auto no=[](Reject why) {return Decision{{},why};};
    const auto control=user.subspan<8,4>();
    const auto safe=[&](const CpuReadProof& proof,Address address,Address bytes) {
        return proof.ready && proof.address==address && proof.bytes==bytes &&
            proof.generation_before==surface.mapping_generation && proof.generation_after==surface.mapping_generation;
    };
    if(!safe(uniform_proof,Base(control),8) || !safe(metadata_proof,surface.metadata,MetadataBytes) ||
       metadata.size()!=Words)return no(Reject::Readiness);
    if(uniform!=std::array<Word,2>{Words,ClearWord})return no(Reject::Uniform);
    for(Word value:metadata)if((value&15u)==13u)return no(Reject::PreservedTiles);
    return shape;
}
// This approves a GPU CONTENT TEST, never an unconditional depth clear. The
// GPU must inspect the pre-RMW inputs and suppress the draw on either invalid
// uniform or any preserved-13 word. No host copy or completed-writer wait.
inline Decision RecognizeGpu(const Shader::Program& program,std::span<const Word,16> user,
    std::array<Word,3> groups,std::array<Word,3> threads,bool indirect,const Surface& surface,
    const GpuReadProof& uniform,const GpuReadProof& metadata) {
    auto shape=RecognizeShape(program,user,groups,threads,indirect,surface);
    if(!shape.plan)return shape;
    const auto safe=[&](const GpuReadProof& proof,Address address,Address bytes) {
        return proof.ready && proof.address==address && proof.bytes==bytes &&
            proof.generation_before==surface.mapping_generation && proof.generation_after==surface.mapping_generation;
    };
    if(!safe(uniform,Base(user.subspan<8,4>()),8) || !safe(metadata,surface.metadata,MetadataBytes))
        return {{},Reject::Readiness};
    return shape;
}
inline constexpr const char* DecisionMsl=R"MSL(
#include <metal_stdlib>
using namespace metal;
kernel void main0(device const uint* uniform [[buffer(0)]],
    device const uint* metadata [[buffer(1)]],device atomic_uint* args [[buffer(2)]],
    uint tid [[thread_position_in_grid]]) {
    if(tid>=262144u)return;
    if((metadata[tid]&15u)==13u || (tid==0u &&
        (uniform[0]!=262144u || uniform[1]!=0xfffffff0u)))
        atomic_store_explicit(args,0u,memory_order_relaxed);
}
)MSL";
inline constexpr const char* DepthMsl=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct DepthVertex {float4 position [[position]];};
vertex DepthVertex main0(uint tid [[vertex_id]]) {
    const float2 positions[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};
    return {float4(positions[tid],1.0f,1.0f)};
}
)MSL";
} // namespace Gpu::Metal::ShadowHtileClear
