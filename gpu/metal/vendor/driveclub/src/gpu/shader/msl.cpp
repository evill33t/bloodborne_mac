#include "gpu/shader/msl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <set>
#include <unordered_map>

#include "gpu/shader/msl_prelude.h"
#include "gpu/shader/msl_readonly_prelude.h"
#include "gpu/metal/native_tessellation.h"
#include "gpu/metal/static_smrd.h"
#include "gpu/metal/native_vertex_fetch.h"

namespace Gpu::Shader {

using namespace Gcn;

namespace {

std::string Hex(u32 v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%xu", v);
    return b;
}

std::string Hex64(u64 v) {
    char b[32];
    std::snprintf(b, sizeof(b), "0x%llxul", static_cast<unsigned long long>(v));
    return b;
}

std::string N(u32 v) {
    return std::to_string(v);
}

u32 FloatBits(float f) {
    u32 u;
    std::memcpy(&u, &f, 4);
    return u;
}

using OpaqueWord = std::optional<u32>;
using OpaquePair = std::array<OpaqueWord, 2>;

struct OpaqueState {
    std::array<OpaqueWord, 128> scalar{};
    bool exported = false;
};

u16 OpaqueHi(u16 code) {
    return code == Operand::VccLo ? Operand::VccHi :
           code == Operand::ExecLo ? Operand::ExecHi : static_cast<u16>(code + 1);
}

OpaqueWord OpaqueRead(const OpaqueState& state, u16 code, u32 literal) {
    if (code < state.scalar.size()) return state.scalar[code];
    if (code >= Operand::Zero && code <= Operand::IntPosMax) return code - Operand::Zero;
    if (code > Operand::IntPosMax && code <= Operand::IntNegMax)
        return u32(-s32(code - Operand::IntPosMax));
    if (code >= Operand::FloatHalf && code <= Operand::FloatNegFour) {
        static const float values[] = {0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 4.0f, -4.0f};
        return FloatBits(values[code - Operand::FloatHalf]);
    }
    if (code == Operand::Literal) return literal;
    return {}; // VGPRs and predicates are dynamic.
}

OpaquePair OpaqueRead64(const OpaqueState& state, u16 code, u32 literal) {
    if (code <= Operand::SgprMax || code == Operand::VccLo || code == Operand::ExecLo)
        return {OpaqueRead(state,code,literal),OpaqueRead(state,OpaqueHi(code),literal)};
    const auto lo = OpaqueRead(state,code,literal);
    // Match Src64: literal dwords zero extend, inline integers sign extend.
    return {lo,lo ? OpaqueWord(code == Operand::Literal ? 0u : u32(s32(*lo) >> 31)) : OpaqueWord{}};
}

bool OpaqueWrite(OpaqueState& state, u16 code, OpaqueWord word) {
    if (code >= state.scalar.size()) return false;
    state.scalar[code] = word;
    return true;
}

bool OpaqueWrite64(OpaqueState& state, u16 code, const OpaquePair& pair) {
    return OpaqueWrite(state,code,pair[0]) && OpaqueWrite(state,OpaqueHi(code),pair[1]);
}

OpaqueWord OpaqueBitwise(u16 op, OpaqueWord a, OpaqueWord b) {
    if (!a || !b) return {};
    switch (op) {
    case 0: return *a & *b;
    case 1: return *a | *b;
    case 2: return *a ^ *b;
    case 3: return *a & ~*b;
    case 4: return *a | ~*b;
    case 5: return ~(*a & *b);
    case 6: return ~(*a | *b);
    case 7: return ~(*a ^ *b);
    default: return {};
    }
}

// Track only scalar constants needed to prove EXEC restores. All other writes
// lose information. Keep the scalar write cases aligned with EmitSop*/EmitVop;
// in particular vector compares, carries, and lane reads can clobber a saved
// EXEC pair even though most vector instructions only write VGPRs.
bool OpaqueStep(OpaqueState& state, const Inst& i) {
    if (!OpName(i.enc,i.op)) return false;
    switch (i.enc) {
    case Enc::EXP:
        if (state.scalar[Operand::ExecLo] != OpaqueWord(~0u) ||
            state.scalar[Operand::ExecHi] != OpaqueWord(~0u)) return false;
        state.exported = true;
        return true;
    case Enc::SOP1: {
        const auto a = OpaqueRead(state,i.src[0],i.literal);
        const auto pair = OpaqueRead64(state,i.src[0],i.literal);
        const auto op = static_cast<Sop1>(i.op);
        if (op >= Sop1::S_AND_SAVEEXEC_B64 && op <= Sop1::S_XNOR_SAVEEXEC_B64) {
            const auto old = OpaqueRead64(state,Operand::ExecLo,0);
            const auto kind = i.op - static_cast<u16>(Sop1::S_AND_SAVEEXEC_B64);
            const OpaquePair next = {OpaqueBitwise(kind,pair[0],old[0]),
                                     OpaqueBitwise(kind,pair[1],old[1])};
            return OpaqueWrite64(state,i.dst,old) && OpaqueWrite64(state,Operand::ExecLo,next);
        }
        switch (op) {
        case Sop1::S_MOV_B32: case Sop1::S_MOV_FED_B32: case Sop1::S_WQM_B32:
            return OpaqueWrite(state,i.dst,a);
        case Sop1::S_MOV_B64: case Sop1::S_WQM_B64:
            return OpaqueWrite64(state,i.dst,pair);
        case Sop1::S_NOT_B32:
            return OpaqueWrite(state,i.dst,a ? OpaqueWord(~*a) : OpaqueWord{});
        case Sop1::S_NOT_B64:
            return OpaqueWrite64(state,i.dst,{pair[0] ? OpaqueWord(~*pair[0]) : OpaqueWord{},
                                              pair[1] ? OpaqueWord(~*pair[1]) : OpaqueWord{}});
        case Sop1::S_CMOV_B32:
            return OpaqueWrite(state,i.dst,a == OpaqueRead(state,i.dst,0) ? a : OpaqueWord{});
        case Sop1::S_CMOV_B64: {
            const auto old = OpaqueRead64(state,i.dst,0);
            return OpaqueWrite64(state,i.dst,{old[0] == pair[0] ? pair[0] : OpaqueWord{},
                                              old[1] == pair[1] ? pair[1] : OpaqueWord{}});
        }
        case Sop1::S_SETPC_B64: case Sop1::S_SWAPPC_B64: case Sop1::S_RFE_B64:
        case Sop1::S_MOVRELS_B32: case Sop1::S_MOVRELS_B64:
        case Sop1::S_MOVRELD_B32: case Sop1::S_MOVRELD_B64: case Sop1::S_CBRANCH_JOIN:
            return false;
        default:
            return OpaqueWrite64(state,i.dst,{}); // May conservatively kill an adjacent constant.
        }
    }
    case Enc::SOP2: {
        if (i.op == static_cast<u16>(Sop2::S_CBRANCH_G_FORK)) return false;
        if (i.op >= static_cast<u16>(Sop2::S_AND_B32) && i.op <= static_cast<u16>(Sop2::S_XNOR_B64)) {
            const auto kind = (i.op - static_cast<u16>(Sop2::S_AND_B32)) / 2;
            if (i.op & 1u) {
                const auto a = OpaqueRead64(state,i.src[0],i.literal);
                const auto b = OpaqueRead64(state,i.src[1],i.literal);
                return OpaqueWrite64(state,i.dst,{OpaqueBitwise(kind,a[0],b[0]),OpaqueBitwise(kind,a[1],b[1])});
            }
            return OpaqueWrite(state,i.dst,OpaqueBitwise(kind,OpaqueRead(state,i.src[0],i.literal),
                                                                OpaqueRead(state,i.src[1],i.literal)));
        }
        return OpaqueWrite64(state,i.dst,{});
    }
    case Enc::SOPK:
        if (i.op == static_cast<u16>(Sopk::S_CBRANCH_I_FORK)) return false;
        if ((i.op >= static_cast<u16>(Sopk::S_CMPK_EQ_I32) && i.op <= static_cast<u16>(Sopk::S_CMPK_LE_U32)) ||
            i.op == static_cast<u16>(Sopk::S_SETREG_B32) || i.op == static_cast<u16>(Sopk::S_SETREG_IMM32_B32)) return true;
        return OpaqueWrite(state,i.dst,i.op == static_cast<u16>(Sopk::S_MOVK_I32) ?
                           OpaqueWord(u32(s32(i.simm16))) : OpaqueWord{});
    case Enc::SOPC:
        return true; // SCC is not used to prune either branch.
    case Enc::SOPP:
        return IsBranch(i) || i.op == static_cast<u16>(Sopp::S_ENDPGM) ||
               i.op == static_cast<u16>(Sopp::S_NOP) || i.op == static_cast<u16>(Sopp::S_WAITCNT) ||
               i.op == static_cast<u16>(Sopp::S_BARRIER);
    case Enc::SMRD: {
        if (i.op == static_cast<u16>(Smrd::S_DCACHE_INV) || i.op == static_cast<u16>(Smrd::S_DCACHE_INV_VOL)) return true;
        u32 count = 0;
        if (i.op <= 4) count = 1u << i.op;
        else if (i.op >= 8 && i.op <= 12) count = 1u << (i.op - 8);
        else if (i.op == static_cast<u16>(Smrd::S_MEMTIME)) count = 2;
        else return false;
        for (u32 k = 0; k < count; ++k)
            if (!OpaqueWrite(state,static_cast<u16>(i.dst+k),{})) return false;
        return true;
    }
    case Enc::VOPC: case Enc::VOP3:
        if (i.op < 256) {
            if (!OpaqueWrite64(state,i.sdst,{})) return false;
            if ((i.op & 0x10u) && !OpaqueWrite64(state,Operand::ExecLo,{})) return false;
            return true;
        }
        [[fallthrough]];
    case Enc::VOP1: case Enc::VOP2:
        switch (static_cast<Vop>(i.op)) {
        case Vop::V_READLANE_B32: case Vop::V_READFIRSTLANE_B32:
            return OpaqueWrite(state,i.dst,{});
        case Vop::V_ADD_I32: case Vop::V_SUB_I32: case Vop::V_SUBREV_I32:
        case Vop::V_ADDC_U32: case Vop::V_SUBB_U32: case Vop::V_SUBBREV_U32:
        case Vop::V_DIV_SCALE_F32: case Vop::V_DIV_SCALE_F64:
        case Vop::V_MAD_U64_U32: case Vop::V_MAD_I64_I32:
            return OpaqueWrite64(state,i.sdst,{});
        default: return true;
        }
    case Enc::VINTRP: case Enc::DS: case Enc::MUBUF: case Enc::MTBUF: case Enc::MIMG:
        return true; // These instructions do not write scalar registers or EXEC.
    default: return false;
    }
}

bool ProveOpaqueExports(const Program& p) {
    if (p.stage != Stage::Fragment || p.code.empty() || !p.unsupported.empty() || p.code.front().pc != 0) return false;
    std::unordered_map<u32,size_t> indices;
    for (size_t k = 0; k < p.code.size(); ++k) {
        if (!indices.emplace(p.code[k].pc,k).second || p.code[k].size == 0 ||
            (k && p.code[k].pc != p.code[k-1].pc + 4u*p.code[k-1].size)) return false;
    }
    const auto end_pc = p.code.back().pc + 4u*p.code.back().size;
    indices.emplace(end_pc,p.code.size());
    std::vector<std::optional<OpaqueState>> incoming(p.code.size()+1);
    OpaqueState initial;
    initial.scalar[Operand::ExecLo] = initial.scalar[Operand::ExecHi] = ~0u;
    initial.scalar[Operand::VccLo] = initial.scalar[Operand::VccHi] = 0u;
    incoming[0] = initial;
    auto merge = [&](size_t at, const OpaqueState& state) {
        if (!incoming[at]) { incoming[at] = state; return; }
        auto& old = *incoming[at];
        old.exported &= state.exported;
        for (size_t k = 0; k < old.scalar.size(); ++k)
            if (old.scalar[k] != state.scalar[k]) old.scalar[k].reset();
    };
    bool exited = false;
    for (size_t k = 0; k < p.code.size(); ++k) {
        if (!incoming[k]) continue;
        auto state = *incoming[k];
        const auto& i = p.code[k];
        if (!OpaqueStep(state,i)) return false;
        if (i.enc == Enc::SOPP && i.op == static_cast<u16>(Sopp::S_ENDPGM)) {
            if (!state.exported) return false;
            exited = true;
            continue;
        }
        if (IsBranch(i)) {
            const auto found = indices.find(BranchTarget(i));
            // Acyclic CFG only. Loops/dynamic jumps use the original kills.
            if (found == indices.end() || found->second <= k) return false;
            merge(found->second,state);
            if (i.op == static_cast<u16>(Sopp::S_BRANCH)) continue;
        }
        merge(k+1,state); // Both arms of a dynamic conditional remain reachable.
    }
    if (incoming.back()) {
        if (!incoming.back()->exported) return false;
        exited = true;
    }
    return exited;
}

bool StaticSmrdClobbers(u8& intact,const Inst& i) {
    const char* name=OpName(i.enc,i.op);
    if(!name) return false;
    auto write=[&](u16 destination,u32 words) {
        constexpr u16 roots[]={2,3,12,13};
        if(destination>=128 || words>128u-destination) return false;
        for(u32 k=0;k<4;++k)
            if(roots[k]>=destination && u32(roots[k]-destination)<words) intact&=u8(~(1u<<k));
        return true;
    };
    const u32 width=std::strstr(name,"64") ? 2 : 1;
    switch(i.enc) {
    case Enc::SOP1:
        switch(static_cast<Sop1>(i.op)) {
        case Sop1::S_SETPC_B64:case Sop1::S_SWAPPC_B64:case Sop1::S_RFE_B64:
        case Sop1::S_MOVRELS_B32:case Sop1::S_MOVRELS_B64:
        case Sop1::S_MOVRELD_B32:case Sop1::S_MOVRELD_B64:case Sop1::S_CBRANCH_JOIN:
            return false;
        default:return write(i.dst,width);
        }
    case Enc::SOP2:
        return i.op!=u16(Sop2::S_CBRANCH_G_FORK) && write(i.dst,width);
    case Enc::SOPK:
        if(i.op==u16(Sopk::S_CBRANCH_I_FORK)) return false;
        if((i.op>=u16(Sopk::S_CMPK_EQ_I32) && i.op<=u16(Sopk::S_CMPK_LE_U32)) ||
           i.op==u16(Sopk::S_SETREG_B32) || i.op==u16(Sopk::S_SETREG_IMM32_B32)) return true;
        return write(i.dst,1);
    case Enc::SMRD:
        if(i.op<=4) return write(i.dst,1u<<i.op);
        if(i.op>=8 && i.op<=12) return write(i.dst,1u<<(i.op-8));
        if(i.op==u16(Smrd::S_MEMTIME)) return write(i.dst,2);
        return i.op==u16(Smrd::S_DCACHE_INV) || i.op==u16(Smrd::S_DCACHE_INV_VOL);
    case Enc::VOPC:case Enc::VOP3:
        if(i.op<256) return write(i.sdst,2);
        [[fallthrough]];
    case Enc::VOP1:case Enc::VOP2:
        switch(static_cast<Vop>(i.op)) {
        case Vop::V_READLANE_B32:case Vop::V_READFIRSTLANE_B32:return write(i.dst,1);
        case Vop::V_ADD_I32:case Vop::V_SUB_I32:case Vop::V_SUBREV_I32:
        case Vop::V_ADDC_U32:case Vop::V_SUBB_U32:case Vop::V_SUBBREV_U32:
        case Vop::V_DIV_SCALE_F32:case Vop::V_DIV_SCALE_F64:
        case Vop::V_MAD_U64_U32:case Vop::V_MAD_I64_I32:return write(i.sdst,2);
        default:return true;
        }
    case Enc::SOPP:
        return IsBranch(i) || i.op==u16(Sopp::S_ENDPGM) || i.op==u16(Sopp::S_NOP) ||
            i.op==u16(Sopp::S_WAITCNT) || i.op==u16(Sopp::S_BARRIER);
    case Enc::SOPC:case Enc::VINTRP:case Enc::DS:case Enc::MUBUF:
    case Enc::MTBUF:case Enc::MIMG:case Enc::EXP:return true;
    default:return false;
    }
}

bool ProveStaticSmrd(const Program& p) {
    const auto layout=Gpu::Metal::StaticSmrd::Layout(p.hash);
    if(p.stage!=Stage::Fragment || p.num_user_sgprs<14 || layout.empty() ||
       p.code.empty() || p.code.front().pc!=0 || !p.unsupported.empty()) return false;
    std::unordered_map<u32,size_t> indices;
    size_t selected=0;
    for(size_t k=0;k<p.code.size();++k) {
        const auto& i=p.code[k];
        if(!i.size || !indices.emplace(i.pc,k).second ||
           (k && i.pc!=p.code[k-1].pc+4u*p.code[k-1].size)) return false;
        if(i.enc!=Enc::SMRD || i.op>4) continue;
        if(selected==layout.size()) return false;
        const auto& load=layout[selected++];
        if(i.pc!=load.pc || i.sbase!=load.root || !i.imm ||
           i.smrd_offset!=load.offset/4 || (1u<<i.op)!=load.words ||
           i.dst!=load.destination || i.size!=1) return false;
    }
    if(selected!=layout.size()) return false;
    indices.emplace(p.code.back().pc+4u*p.code.back().size,p.code.size());
    std::vector<std::optional<u8>> incoming(p.code.size()+1);
    incoming[0]=15;
    auto merge=[&](size_t at,u8 intact) {
        if(incoming[at]) *incoming[at]&=intact;else incoming[at]=intact;
    };
    u32 visited=0;
    for(size_t k=0;k<p.code.size();++k) {
        if(!incoming[k]) continue;
        u8 intact=*incoming[k];
        const auto& i=p.code[k];
        if(i.enc==Enc::SMRD && i.op<=4) {
            const auto mask=i.sbase==2 ? 3u : 12u;
            if((intact&mask)!=mask) return false;
            ++visited;
        }
        if(!StaticSmrdClobbers(intact,i)) return false;
        if(i.enc==Enc::SOPP && i.op==u16(Sopp::S_ENDPGM)) continue;
        if(IsBranch(i)) {
            const auto target=indices.find(BranchTarget(i));
            if(target==indices.end() || target->second<=k) return false;
            merge(target->second,intact);
            if(i.op==u16(Sopp::S_BRANCH)) continue;
        }
        merge(k+1,intact);
    }
    return visited==layout.size();
}

// Nonzero tokens identify a value common to all lanes, rather than merely
// marking it uniform on one CFG arm. A join keeps a token only when both arms
// carry the same value identity. This deliberately does not reason about
// equality of different arithmetic expressions or mutable memory contents.
using SimdSmrdState = std::array<u64,128>;
constexpr u64 SimdConstant(u32 value) { return (1ull<<32)|value; }
u64 SimdRead(const SimdSmrdState& state,u16 code,u32 literal) {
    if(code<state.size()) return state[code];
    const auto constant=OpaqueRead(OpaqueState{},code,literal);
    return constant ? SimdConstant(*constant) : 0;
}
bool SimdWrite(SimdSmrdState& state,u16 code,u32 words,u64 token=0) {
    if(code>=state.size() || words>state.size()-code) return false;
    for(u32 k=0;k<words;++k) state[code+k]=token ? token+k : 0;
    return true;
}
bool SimdSmrdStep(SimdSmrdState& state,const Inst& i,size_t index,u32& loads) {
    const char* name=OpName(i.enc,i.op);
    if(!name) return false;
    const u32 width=std::strstr(name,"64") ? 2 : 1;
    switch(i.enc) {
    case Enc::SMRD: {
        const u32 words=i.op<=4 ? 1u<<i.op :
            i.op>=8 && i.op<=12 ? 1u<<(i.op-8) : 0;
        if(!words || i.sbase>=127 || !state[i.sbase] || !state[i.sbase+1] ||
           (!i.imm && !SimdRead(state,u16(i.smrd_offset),0))) return false;
        ++loads;
        // No reachable instruction can write guest memory in this proof, so
        // identical scalar addresses read the same GPU-time bytes. Different
        // load sites intentionally receive different identities.
        return SimdWrite(state,i.dst,words,(3ull<<32)|((index+1)<<8));
    }
    case Enc::SOP1: {
        const auto op=static_cast<Sop1>(i.op);
        if(op==Sop1::S_MOV_B32 || op==Sop1::S_MOV_FED_B32 || op==Sop1::S_WQM_B32)
            return SimdWrite(state,i.dst,1,SimdRead(state,i.src[0],i.literal));
        if(op==Sop1::S_MOV_B64 || op==Sop1::S_WQM_B64) {
            const u64 lo=SimdRead(state,i.src[0],i.literal);
            const u64 hi=i.src[0]<=Operand::SgprMax || i.src[0]==Operand::VccLo || i.src[0]==Operand::ExecLo ?
                SimdRead(state,OpaqueHi(i.src[0]),i.literal) :
                OpaqueRead64(OpaqueState{},i.src[0],i.literal)[1] ?
                    SimdConstant(*OpaqueRead64(OpaqueState{},i.src[0],i.literal)[1]) : 0;
            return SimdWrite(state,i.dst,1,lo) && SimdWrite(state,OpaqueHi(i.dst),1,hi);
        }
        if(op>=Sop1::S_AND_SAVEEXEC_B64 && op<=Sop1::S_XNOR_SAVEEXEC_B64) {
            const u64 lo=state[Operand::ExecLo],hi=state[Operand::ExecHi];
            return SimdWrite(state,i.dst,1,lo) && SimdWrite(state,OpaqueHi(i.dst),1,hi) &&
                SimdWrite(state,Operand::ExecLo,2);
        }
        switch(op) {
        case Sop1::S_SETPC_B64:case Sop1::S_SWAPPC_B64:case Sop1::S_RFE_B64:
        case Sop1::S_MOVRELS_B32:case Sop1::S_MOVRELS_B64:
        case Sop1::S_MOVRELD_B32:case Sop1::S_MOVRELD_B64:case Sop1::S_CBRANCH_JOIN:return false;
        default:return SimdWrite(state,i.dst,width);
        }
    }
    case Enc::SOP2:
        return i.op!=u16(Sop2::S_CBRANCH_G_FORK) && SimdWrite(state,i.dst,width);
    case Enc::SOPK:
        if(i.op==u16(Sopk::S_CBRANCH_I_FORK)) return false;
        if((i.op>=u16(Sopk::S_CMPK_EQ_I32) && i.op<=u16(Sopk::S_CMPK_LE_U32)) ||
           i.op==u16(Sopk::S_SETREG_B32) || i.op==u16(Sopk::S_SETREG_IMM32_B32)) return true;
        return SimdWrite(state,i.dst,1,i.op==u16(Sopk::S_MOVK_I32) ? SimdConstant(u32(s32(i.simm16))) : 0);
    case Enc::VOPC:case Enc::VOP3:
        if(i.op<256) return SimdWrite(state,i.sdst,2) &&
            (!(i.op&0x10u) || SimdWrite(state,Operand::ExecLo,2));
        [[fallthrough]];
    case Enc::VOP1:case Enc::VOP2:
        switch(static_cast<Vop>(i.op)) {
        case Vop::V_READLANE_B32:case Vop::V_READFIRSTLANE_B32:return SimdWrite(state,i.dst,1);
        case Vop::V_ADD_I32:case Vop::V_SUB_I32:case Vop::V_SUBREV_I32:
        case Vop::V_ADDC_U32:case Vop::V_SUBB_U32:case Vop::V_SUBBREV_U32:
        case Vop::V_DIV_SCALE_F32:case Vop::V_DIV_SCALE_F64:
        case Vop::V_MAD_U64_U32:case Vop::V_MAD_I64_I32:return SimdWrite(state,i.sdst,2);
        default:return true;
        }
    case Enc::SOPP:
        return IsBranch(i) || i.op==u16(Sopp::S_ENDPGM) || i.op==u16(Sopp::S_NOP) ||
            i.op==u16(Sopp::S_WAITCNT);
    case Enc::MIMG:
        return !std::strstr(name,"STORE") && !std::strstr(name,"ATOMIC");
    case Enc::MTBUF:
        // Typed buffer loads only write VGPRs. Their vector addresses may vary
        // without affecting the scalar-load address proof. Stores remain
        // ineligible because they could mutate a later scalar payload.
        return i.op<=u16(Mtbuf::TBUFFER_LOAD_FORMAT_XYZW);
    case Enc::SOPC:case Enc::VINTRP:case Enc::EXP:return true;
    // Other buffer/DS operations, atomics and unhandled operations are ineligible.
    default:return false;
    }
}
bool ProveSimdSmrd(const Program& p,std::string* failure=nullptr) {
    if(failure) failure->clear();
    if(!ProveStaticSmrd(p)) {
        if(failure) *failure="Original hash/layout/CFG proof failed";
        return false;
    }
    std::unordered_map<u32,size_t> indices;
    for(size_t k=0;k<p.code.size();++k) indices.emplace(p.code[k].pc,k);
    indices.emplace(p.code.back().pc+4u*p.code.back().size,p.code.size());
    SimdSmrdState initial;initial.fill(SimdConstant(0));
    for(u32 k=0;k<p.num_user_sgprs && k<16;++k) initial[k]=(2ull<<32)|k;
    initial[Operand::ExecLo]=initial[Operand::ExecHi]=SimdConstant(~0u);
    std::vector<std::optional<SimdSmrdState>> incoming(p.code.size()+1);incoming[0]=initial;
    auto merge=[&](size_t at,const SimdSmrdState& state) {
        if(!incoming[at]) {incoming[at]=state;return;}
        for(size_t k=0;k<state.size();++k) if((*incoming[at])[k]!=state[k]) (*incoming[at])[k]=0;
    };
    u32 loads=0;
    for(size_t k=0;k<p.code.size();++k) {
        if(!incoming[k]) continue;
        auto state=*incoming[k];const auto& i=p.code[k];
        if(!SimdSmrdStep(state,i,k,loads)) {
            if(failure) {
                *failure="pc="+N(i.pc)+" "+Disassemble(i);
                if(i.enc==Enc::SMRD) {
                    *failure+=" base s"+N(i.sbase)+" token="+std::to_string(i.sbase<128 ? state[i.sbase] : 0)+
                        "/"+std::to_string(i.sbase<127 ? state[i.sbase+1] : 0);
                    if(!i.imm) *failure+=" offset operand="+N(i.smrd_offset)+
                        " token="+std::to_string(SimdRead(state,u16(i.smrd_offset),0));
                } else *failure+=" (unhandled operation or scalar destination)";
            }
            return false;
        }
        if(i.enc==Enc::SOPP && i.op==u16(Sopp::S_ENDPGM)) continue;
        if(IsBranch(i)) {
            const auto target=indices.find(BranchTarget(i));
            if(target==indices.end() || target->second<=k) return false;
            merge(target->second,state);
            if(i.op==u16(Sopp::S_BRANCH)) continue;
        }
        merge(k+1,state);
    }
    return loads>0;
}

bool ProveNativeVertexFetch(const Program& p,std::string* failure=nullptr) {
    if(failure) failure->clear();
    const auto* layout=Gpu::Metal::NativeVertexFetch::Layout(p.hash);
    const auto reject=[&](const char* reason) {if(failure) *failure=reason;return false;};
    if(!layout || p.stage!=Stage::Vertex || p.num_user_sgprs!=16 || p.vgpr_comp_cnt!=3 ||
       p.compute_rsrc2 || p.fetch_ud!=0 || p.fetch_bytes!=layout->fetch_bytes ||
       p.has_branches || p.writes_exec || !p.labels.empty() || !p.unsupported.empty() || !p.images.empty())
        return reject("Original vertex hash/stage/metadata mismatch");
    const u32 n=layout->attributes;
    if(p.code.size()<2*n+4) return reject("Truncated original fetch cluster");
    const auto& prefix=p.code[0];
    if(prefix.pc || prefix.enc!=Enc::SOP1 || prefix.op!=u16(Sop1::S_MOV_B32) ||
       prefix.dst!=Operand::VccHi || prefix.src[0]!=Operand::Literal || prefix.size!=2 ||
       prefix.literal!=layout->prefix_literal) return reject("Fetch prefix can change vertex inputs");
    for(u32 k=0;k<n;++k) {
        const auto& i=p.code[1+k];
        if(i.pc!=kFetchPcBase+4*k || i.enc!=Enc::SMRD || i.op!=u16(Smrd::S_LOAD_DWORDX4) ||
           i.dst!=16+4*k || i.sbase!=2 || !i.imm || i.smrd_offset!=4*k || i.size!=1)
            return reject("Original descriptor load cluster changed");
    }
    const auto wait=[&](const Inst& i,u32 pc) {
        return i.pc==pc && i.enc==Enc::SOPP && i.op==u16(Sopp::S_WAITCNT) && i.size==1;
    };
    if(!wait(p.code[1+n],kFetchPcBase+4*n)) return reject("Descriptor/fetch boundary changed");
    for(u32 k=0;k<n;++k) {
        const auto& i=p.code[2+n+k];
        const u16 op=k==0 ? u16(Mubuf::BUFFER_LOAD_FORMAT_XYZ) : k==5 ?
            u16(Mubuf::BUFFER_LOAD_FORMAT_XY) : u16(Mubuf::BUFFER_LOAD_FORMAT_XYZW);
        if(i.pc!=layout->first_pc+8*k || i.enc!=Enc::MUBUF || i.op!=op || i.size!=2 ||
           i.vaddr || i.vdata!=4+4*k || i.srsrc!=16+4*k || i.soffset!=Operand::Zero ||
           i.offset || !i.idxen || i.offen || i.addr64 || i.glc || i.slc || i.tfe || i.lds)
            return reject("Original typed fetch cluster changed");
    }
    if(!wait(p.code[2+2*n],layout->last_pc+8)) return reject("Original fetch exit changed");
    // No second fetch range or hidden branch may enter the fused cluster.
    for(size_t k=3+2*n;k<p.code.size();++k)
        if(p.code[k].pc>=kFetchPcBase || IsBranch(p.code[k]) ||
           (p.code[k].enc==Enc::SOP1 && (p.code[k].op==u16(Sop1::S_SWAPPC_B64) ||
            p.code[k].op==u16(Sop1::S_SETPC_B64)))) return reject("Unexpected additional fetch/control flow");
    return true;
}

bool ProveReadOnlyGuestCode(const Program& p,std::string* failure=nullptr) {
    auto reject=[&](const char* why) {if(failure)*failure=why;return false;};
    if((p.stage!=Stage::Vertex && p.stage!=Stage::Fragment) || p.code.empty() ||
       p.code.size()>32768 || !p.unsupported.empty()) return reject("Not an ordinary supported VS/PS");
    const bool fetched=p.fetch_address || p.fetch_bytes || p.fetch_ud>=0;
    std::size_t fetch_end=1;
    if(fetched) {
        if(p.stage!=Stage::Vertex || !p.fetch_address || p.fetch_ud<0 || p.fetch_ud>14 ||
           p.num_user_sgprs>16 || u32(p.fetch_ud+1)>=p.num_user_sgprs ||
           p.fetch_bytes<8 || p.fetch_bytes>16384 || p.code.size()<4)
            return reject("Incomplete inline-fetch metadata");
        // The decoder replaced the single one-dword call at main PC8 with a
        // branchless fetch body and removed its final one-dword return. Admit
        // this exact two-segment layout, not arbitrary gaps or indirect calls.
        const auto& prefix=p.code.front();
        if(prefix.pc!=0 || prefix.size!=2 || prefix.enc!=Enc::SOP1 ||
           prefix.op!=u16(Sop1::S_MOV_B32) || prefix.dst!=Operand::VccHi ||
           prefix.src[0]!=Operand::Literal || p.code[1].pc!=kFetchPcBase)
            return reject("Unexpected inline-fetch call prefix");
        u32 next_fetch=kFetchPcBase;
        for(;fetch_end<p.code.size() && p.code[fetch_end].pc>=kFetchPcBase;++fetch_end) {
            const auto& i=p.code[fetch_end];
            if(i.pc!=next_fetch || !i.size || i.size>2) return reject("Discontinuous inline fetch");
            next_fetch+=4u*i.size;
            const bool descriptor=i.enc==Enc::SMRD && i.op==u16(Smrd::S_LOAD_DWORDX4);
            const bool load=i.enc==Enc::MUBUF && i.op<=u16(Mubuf::BUFFER_LOAD_FORMAT_XYZW) && !i.lds && !i.tfe;
            const bool wait=i.enc==Enc::SOPP && i.op==u16(Sopp::S_WAITCNT);
            if(!descriptor && !load && !wait) return reject("Inline fetch is not a branchless read-only fetch");
        }
        if(fetch_end==p.code.size() || p.code[fetch_end].pc!=12 ||
           p.fetch_bytes!=next_fetch-kFetchPcBase+4u)
            return reject("Inline-fetch return or resumed main layout mismatch");
        for(u32 label:p.labels) if(label>=kFetchPcBase) return reject("Branch label enters inline fetch");
    }
    if(!p.buffer_writes.empty()) return reject("Reflected buffer writer");
    for(const auto& image:p.images) if(image.storage) return reject("Storage image");
    std::set<u32> pcs;
    bool branches=false;
    u32 next=0;
    for(std::size_t k=0;k<p.code.size();++k) {
        const auto& i=p.code[k];
        if(fetched && k==1) next=kFetchPcBase;
        if(fetched && k==fetch_end) next=12;
        const bool in_fetch=fetched && k>=1 && k<fetch_end;
        if(i.pc!=next || !i.size || i.size>2 || (!in_fetch && i.pc>=kFetchPcBase) ||
           !OpName(i.enc,i.op) || pcs.contains(i.pc))
            return reject("Incomplete or unclassified instruction stream");
        next=i.pc+4u*i.size;pcs.insert(i.pc);
        // This is independent of reflection: even a missing/stale writer
        // descriptor must never turn a buffer store/atomic into a const input.
        switch(i.enc) {
        case Enc::MUBUF:
            if(i.op>u16(Mubuf::BUFFER_LOAD_FORMAT_XYZW) &&
               !(i.op>=u16(Mubuf::BUFFER_LOAD_UBYTE) && i.op<=u16(Mubuf::BUFFER_LOAD_DWORDX3)))
                return reject("Buffer store/atomic or unknown buffer opcode");
            break;
        case Enc::MTBUF:
            if(i.op>u16(Mtbuf::TBUFFER_LOAD_FORMAT_XYZW)) return reject("Typed buffer writer");
            break;
        case Enc::MIMG:
            switch(static_cast<Mimg>(i.op)) {
            case Mimg::IMAGE_LOAD:case Mimg::IMAGE_LOAD_MIP:case Mimg::IMAGE_GET_RESINFO:
            case Mimg::IMAGE_SAMPLE:case Mimg::IMAGE_SAMPLE_CL:case Mimg::IMAGE_SAMPLE_D:
            case Mimg::IMAGE_SAMPLE_D_CL:case Mimg::IMAGE_SAMPLE_L:case Mimg::IMAGE_SAMPLE_B:
            case Mimg::IMAGE_SAMPLE_B_CL:case Mimg::IMAGE_SAMPLE_LZ:case Mimg::IMAGE_SAMPLE_C:
            case Mimg::IMAGE_SAMPLE_C_CL:case Mimg::IMAGE_SAMPLE_C_D:case Mimg::IMAGE_SAMPLE_C_D_CL:
            case Mimg::IMAGE_SAMPLE_C_L:case Mimg::IMAGE_SAMPLE_C_B:case Mimg::IMAGE_SAMPLE_C_B_CL:
            case Mimg::IMAGE_SAMPLE_C_LZ:case Mimg::IMAGE_GATHER4:case Mimg::IMAGE_GATHER4_C:
            case Mimg::IMAGE_GET_LOD:break;
            default:return reject("Image store/atomic or unknown image opcode");
            }
            break;
        case Enc::DS:
            if(i.op!=u16(Ds::DS_SWIZZLE_B32)) return reject("LDS/GDS instruction");
            break;
        case Enc::SOP1:
            switch(static_cast<Sop1>(i.op)) {
            case Sop1::S_SETPC_B64:case Sop1::S_SWAPPC_B64:case Sop1::S_RFE_B64:
            case Sop1::S_MOVRELS_B32:case Sop1::S_MOVRELS_B64:
            case Sop1::S_MOVRELD_B32:case Sop1::S_MOVRELD_B64:case Sop1::S_CBRANCH_JOIN:
                return reject("Indirect scalar control/register access");
            default:break;
            }
            break;
        case Enc::SOP2:
            if(i.op==u16(Sop2::S_CBRANCH_G_FORK)) return reject("Indirect fork");
            break;
        case Enc::SOPK:
            if(i.op==u16(Sopk::S_CBRANCH_I_FORK)) return reject("Indirect fork");
            break;
        case Enc::SOPP:
            branches|=IsBranch(i);
            if(!IsBranch(i) && i.op!=u16(Sopp::S_ENDPGM) && i.op!=u16(Sopp::S_NOP) &&
               i.op!=u16(Sopp::S_WAITCNT)) return reject("Unexpected scalar control");
            break;
        case Enc::SMRD:case Enc::SOPC:case Enc::VOP1:case Enc::VOP2:
        case Enc::VOP3:case Enc::VOPC:case Enc::VINTRP:case Enc::EXP:break;
        default:return reject("Unknown instruction encoding");
        }
    }
    const auto& last=p.code.back();
    if(last.enc!=Enc::SOPP || last.op!=u16(Sopp::S_ENDPGM) || branches!=p.has_branches)
        return reject("Missing termination or branch metadata mismatch");
    for(const auto& i:p.code) if(IsBranch(i) && (!pcs.contains(BranchTarget(i)) || BranchTarget(i)>=kFetchPcBase))
        return reject("Invalid branch target or branch into inline fetch");
    return true;
}

class Emitter {
public:
    Emitter(const Program& p, const MslOptions& o) : p(p), o(o),
        opaque_exports(o.opaque_exports && ProveOpaqueExports(p)),
        static_smrd(o.static_smrd && !o.direct_smrd && !o.simd_smrd && ProveStaticSmrd(p)),
        direct_smrd(o.direct_smrd && !o.static_smrd && !o.simd_smrd && ProveStaticSmrd(p)),
        simd_smrd(o.simd_smrd && !o.static_smrd && !o.direct_smrd && ProveSimdSmrd(p)),
        native_vertex_fetch(o.native_vertex_fetch && o.tess_stage==TessStage::None && !o.native_tess_layout &&
            !o.rect_list && !o.points && !o.debug_capture && ProveNativeVertexFetch(p)),
        readonly_guest_memory(o.readonly_guest_memory && o.tess_stage==TessStage::None &&
            !o.native_tess_layout && (!o.native_vertex_fetch || native_vertex_fetch) && !o.rect_list && !o.points &&
            !o.debug_capture && o.probe_pc==~0u && !o.static_smrd && !o.direct_smrd &&
            !o.simd_smrd && ProveReadOnlyGuestCode(p)) {}

    std::string Run();
    std::set<std::string> unsupported;

private:
    // ---- output ----
    void Line(const std::string& s) { body += "    " + s + "\n"; }
    std::string GuestPointer() const {return readonly_guest_memory ? "device const uint*" : "device uint*";}
    void Unsupported(const Inst& i) {
        const char* n = OpName(i.enc, i.op);
        unsupported.insert(n ? n : "op" + N(i.op));
        Line("/* unsupported: " + Disassemble(i) + " */");
    }

    // ---- registers ----
    std::string SName(u16 code) {
        if (code <= Operand::SgprMax) {
            max_sgpr = std::max<u32>(max_sgpr, code + 1u);
            return "s" + N(code);
        }
        switch (code) {
        case Operand::VccLo: return "vcc_lo";
        case Operand::VccHi: return "vcc_hi";
        case Operand::M0: return "m0r";
        case Operand::ExecLo: return "exec_lo";
        case Operand::ExecHi: return "exec_hi";
        default: return "dc_null";
        }
    }
    static u16 Hi(u16 code) {
        if (code == Operand::VccLo) return Operand::VccHi;
        if (code == Operand::ExecLo) return Operand::ExecHi;
        return static_cast<u16>(code + 1);
    }
    std::string VName(u32 n) {
        max_vgpr = std::max(max_vgpr, n + 1u);
        return "v" + N(n);
    }

    std::string Src(u16 code, u32 lit) {
        if (code <= Operand::SgprMax || code == Operand::VccLo || code == Operand::VccHi ||
            code == Operand::M0 || code == Operand::ExecLo || code == Operand::ExecHi) {
            return SName(code);
        }
        if (code >= Operand::Vgpr0) {
            return VName(code - Operand::Vgpr0);
        }
        if (code >= Operand::Zero && code <= Operand::IntPosMax) {
            return N(code - Operand::Zero) + "u";
        }
        if (code > Operand::IntPosMax && code <= Operand::IntNegMax) {
            return Hex(static_cast<u32>(-static_cast<s32>(code - Operand::IntPosMax)));
        }
        if (code >= Operand::FloatHalf && code <= Operand::FloatNegFour) {
            static const float f[] = {0.5f, -0.5f, 1.0f, -1.0f, 2.0f, -2.0f, 4.0f, -4.0f};
            return Hex(FloatBits(f[code - Operand::FloatHalf]));
        }
        switch (code) {
        case Operand::Literal: return Hex(lit);
        case Operand::Vccz: return "(vcc_lo == 0u ? 1u : 0u)";
        case Operand::Execz: return "(exec_lo == 0u ? 1u : 0u)";
        case Operand::Scc: return "(scc ? 1u : 0u)";
        default: return "0u";
        }
    }
    std::string U(const Inst& i, int k) { return Src(i.src[k], i.literal); }
    std::string I(const Inst& i, int k) { return "int(" + U(i, k) + ")"; }
    std::string ModifiedFloatBits(const Inst& i, int k) {
        // V_CNDMASK_B32's VOP3 data operands accept float source modifiers.
        // Apply them to the sign bit so selects preserve NaN payloads,
        // denormals and signed zero without passing through float arithmetic.
        std::string e = U(i, k);
        if (i.abs & (1 << k)) e = "(" + e + " & 0x7fffffffu)";
        if (i.neg & (1 << k)) e = "(" + e + " ^ 0x80000000u)";
        return e;
    }
    std::string F(const Inst& i, int k) {
        std::string e = "as_type<float>(" + U(i, k) + ")";
        if (i.abs & (1 << k)) e = "abs(" + e + ")";
        if (i.neg & (1 << k)) e = "(-" + e + ")";
        return e;
    }
    std::string Src64(u16 code, u32 lit) {
        if (code <= Operand::SgprMax || code == Operand::VccLo || code == Operand::ExecLo) {
            return "(ulong(" + SName(code) + ") | (ulong(" + SName(Hi(code)) + ") << 32))";
        }
        if (code >= Operand::Vgpr0) {
            const u32 v = code - Operand::Vgpr0;
            return "(ulong(" + VName(v) + ") | (ulong(" + VName(v + 1) + ") << 32))";
        }
        if (code == Operand::Literal) {
            return "ulong(" + Hex(lit) + ")";
        }
        return "ulong(long(int(" + Src(code, lit) + ")))";
    }
    std::string U64(const Inst& i, int k) { return Src64(i.src[k], i.literal); }
    // A lane mask read as this lane's bit.
    std::string Mask(u16 code) {
        if (code == Operand::Zero) return "false";
        return "(" + Src(code, 0) + " != 0u)";
    }

    void SetS(u16 code, const std::string& e) { Line(SName(code) + " = " + e + ";"); }
    void SetS64(u16 code, const std::string& e) {
        Line("{ ulong t_ = " + e + "; " + SName(code) + " = uint(t_); " + SName(Hi(code)) +
             " = uint(t_ >> 32); }");
    }
    void SetV(u32 v, const std::string& e) {
        if (p.writes_exec) {
            Line("if (exec_lo != 0u) " + VName(v) + " = " + e + ";");
        } else {
            Line(VName(v) + " = " + e + ";");
        }
    }
    std::string FOut(const Inst& i, std::string e) {
        if (i.omod == 1) e = "(" + e + ") * 2.0f";
        else if (i.omod == 2) e = "(" + e + ") * 4.0f";
        else if (i.omod == 3) e = "(" + e + ") * 0.5f";
        if (i.clamp) e = "dc_sat(" + e + ")";
        return "as_type<uint>(" + e + ")";
    }
    void SetVF(const Inst& i, const std::string& fe) { SetV(i.dst, FOut(i, fe)); }
    void SetMask(u16 code, const std::string& cond) {
        Line("{ uint m_ = ((" + cond + ") && exec_lo != 0u) ? 0xffffffffu : 0u; " + SName(code) +
             " = m_; " + SName(Hi(code)) + " = m_; }");
    }

    u64 PcAddress(u32 pc) const {
        if (pc >= kFetchPcBase) {
            return p.fetch_address + (pc - kFetchPcBase);
        }
        return p.address + pc;
    }

    // ---- instruction emitters ----
    void Emit(const Inst& i);
    void EmitSop2(const Inst& i);
    void EmitSopk(const Inst& i);
    void EmitSop1(const Inst& i);
    void EmitSopc(const Inst& i);
    void EmitSopp(const Inst& i);
    void EmitSmrd(const Inst& i);
    void EmitVopc(const Inst& i);
    void EmitVop(const Inst& i);
    void EmitVintrp(const Inst& i);
    void EmitDs(const Inst& i);
    void EmitBuffer(const Inst& i);
    void EmitImage(const Inst& i);
    void EmitExp(const Inst& i);
    // The CB stores R11G11B10 with negatives and NaN as 0 and overflow as the largest
    // finite value; Metal leaves those undefined (they read back as NaN/Inf).
    void ClampUfloat(u32 target, const std::string& dst) {
        if (o.mrt_ufloat & (1u << target)) {
            Line(dst + " = dc_ufloat(" + dst + ");");
        }
    }

    // Register probe at color exports that are not finite.
    void ProbeNanExport(const std::string& dst) {
        if (o.probe_pc == kProbeExportNan) {
            Line("if (any(!isfinite(" + dst + "))) {");
            body += "    /*DC_PROBE*/\n";
            Line("}");
        }
    }
    void Jump(u32 target) {
        if (!structured) {
            Line("{ pc_ = " + Hex(target) + "; continue; }");
            return;
        }
        const u32 t = index_of.at(target);
        Line(t <= cur ? "{ tgt_ = " + N(t) + "u; continue; }" : "tgt_ = " + N(t) + "u;");
    }
    void End() {
        if (p.stage == Stage::Fragment && !opaque_exports) {
            Line("if (!dc_exported) discard_fragment();");
        }
        Line(ComputeLike() ? "return;" : "return out;");
    }

    bool TessPrep() const { return o.tess_stage == TessStage::Local || o.tess_stage == TessStage::Hull; }
    bool ComputeLike() const { return p.stage == Stage::Compute || TessPrep(); }

    bool Structure();
    void EmitStructured();
    void EmitSwitch();

    std::string AttrExpr(u32 attr, u32 chan);
    std::string TexArgs() const;     // call arguments for dc_run
    std::string TexParams(bool entry) const;

    const Program& p;
    const MslOptions& o;
    const bool opaque_exports;
    const bool static_smrd;
    const bool direct_smrd;
    const bool simd_smrd;
    const bool native_vertex_fetch;
    const bool readonly_guest_memory;
    std::string body;
    u32 max_sgpr = 0;
    u32 max_vgpr = 0;
    bool uses_movrel = false;
    std::set<u32> ps_params; // VS params the PS reads
    std::unordered_map<u32,u32> ps_interpolation; // VS param -> enabled barycentric kind

    // Structured control flow (see Structure()).
    bool structured = false;
    u32 cur = 0;                              // index of the instruction being emitted
    std::unordered_map<u32, u32> index_of;    // pc -> instruction index
    std::unordered_map<u32, u32> loop_end;    // loop header index -> index of its last back edge
};

void Emitter::Emit(const Inst& i) {
    const auto* fetch=native_vertex_fetch ? Gpu::Metal::NativeVertexFetch::Layout(p.hash) : nullptr;
    if(fetch && i.pc==fetch->first_pc) {
        std::string descriptors;
        for(u32 k=0;k<6;++k) {
            descriptors+=", uint4(";
            for(u32 j=0;j<4;++j) {
                if(j) descriptors+=", ";
                descriptors+=k<fetch->attributes ? SName(u16(16+4*k+j)) : "0u";
            }
            descriptors+=")";
        }
        Line("{ DcVertexFetch dc_vf_; if (dc_native_vertex_fetch(dc_vf_, mem, v0, "+N(fetch->stride)+"u"+descriptors+")) {");
        for(u32 k=0;k<fetch->attributes;++k) {
            const u32 components=k==0 ? 3 : k==5 ? 2 : 4;
            for(u32 j=0;j<components;++j) SetV(4+4*k+j,"dc_vf_.a"+N(k)+"["+N(j)+"]");
        }
        Line("} else {");
    }
    if (i.pc == o.probe_pc && p.stage != Stage::Vertex) {
        body += "    /*DC_PROBE*/\n";
    }
    switch (i.enc) {
    case Enc::SOP2: EmitSop2(i); break;
    case Enc::SOPK: EmitSopk(i); break;
    case Enc::SOP1: EmitSop1(i); break;
    case Enc::SOPC: EmitSopc(i); break;
    case Enc::SOPP: EmitSopp(i); break;
    case Enc::SMRD: EmitSmrd(i); break;
    case Enc::VOPC: EmitVopc(i); break;
    case Enc::VOP1:
    case Enc::VOP2:
        EmitVop(i);
        break;
    case Enc::VOP3:
        if (i.op < 256) {
            EmitVopc(i);
        } else {
            EmitVop(i);
        }
        break;
    case Enc::VINTRP: EmitVintrp(i); break;
    case Enc::DS: EmitDs(i); break;
    case Enc::MUBUF:
    case Enc::MTBUF:
        EmitBuffer(i);
        break;
    case Enc::MIMG: EmitImage(i); break;
    case Enc::EXP: EmitExp(i); break;
    default: Unsupported(i); break;
    }
    if(fetch && i.pc==fetch->last_pc) Line("} }");
}

void Emitter::EmitSop2(const Inst& i) {
    const std::string d = SName(i.dst);
    const std::string a = U(i, 0), b = U(i, 1);
    auto r32 = [&](const std::string& e) { Line("{ uint r_ = " + e + "; " + d + " = r_; scc = r_ != 0u; }"); };
    auto r64 = [&](const std::string& e) {
        Line("{ ulong r_ = " + e + "; " + d + " = uint(r_); " + SName(Hi(i.dst)) +
             " = uint(r_ >> 32); scc = r_ != 0ul; }");
    };
    const std::string a64 = U64(i, 0), b64 = U64(i, 1);
    switch (static_cast<Sop2>(i.op)) {
    case Sop2::S_ADD_U32:
        Line("{ ulong r_ = ulong(" + a + ") + ulong(" + b + "); " + d + " = uint(r_); scc = (r_ >> 32) != 0ul; }");
        break;
    case Sop2::S_SUB_U32:
        Line("{ uint x_ = " + a + ", y_ = " + b + "; " + d + " = x_ - y_; scc = y_ > x_; }");
        break;
    case Sop2::S_ADD_I32:
        Line("{ int x_ = int(" + a + "), y_ = int(" + b + "); int r_ = int(uint(x_) + uint(y_)); " + d +
             " = uint(r_); scc = ((x_ >= 0) == (y_ >= 0)) && ((r_ >= 0) != (x_ >= 0)); }");
        break;
    case Sop2::S_SUB_I32:
        Line("{ int x_ = int(" + a + "), y_ = int(" + b + "); int r_ = int(uint(x_) - uint(y_)); " + d +
             " = uint(r_); scc = ((x_ >= 0) != (y_ >= 0)) && ((r_ >= 0) != (x_ >= 0)); }");
        break;
    case Sop2::S_ADDC_U32:
        Line("{ ulong r_ = ulong(" + a + ") + ulong(" + b + ") + (scc ? 1ul : 0ul); " + d +
             " = uint(r_); scc = (r_ >> 32) != 0ul; }");
        break;
    case Sop2::S_SUBB_U32:
        Line("{ ulong x_ = ulong(" + a + "), y_ = ulong(" + b + ") + (scc ? 1ul : 0ul); " + d +
             " = uint(x_ - y_); scc = y_ > x_; }");
        break;
    case Sop2::S_MIN_I32:
        Line("{ int x_ = int(" + a + "), y_ = int(" + b + "); scc = x_ < y_; " + d + " = uint(scc ? x_ : y_); }");
        break;
    case Sop2::S_MIN_U32:
        Line("{ uint x_ = " + a + ", y_ = " + b + "; scc = x_ < y_; " + d + " = scc ? x_ : y_; }");
        break;
    case Sop2::S_MAX_I32:
        Line("{ int x_ = int(" + a + "), y_ = int(" + b + "); scc = x_ > y_; " + d + " = uint(scc ? x_ : y_); }");
        break;
    case Sop2::S_MAX_U32:
        Line("{ uint x_ = " + a + ", y_ = " + b + "; scc = x_ > y_; " + d + " = scc ? x_ : y_; }");
        break;
    case Sop2::S_CSELECT_B32: SetS(i.dst, "scc ? " + a + " : " + b); break;
    case Sop2::S_CSELECT_B64: SetS64(i.dst, "scc ? " + a64 + " : " + b64); break;
    case Sop2::S_AND_B32: r32(a + " & " + b); break;
    case Sop2::S_AND_B64: r64(a64 + " & " + b64); break;
    case Sop2::S_OR_B32: r32(a + " | " + b); break;
    case Sop2::S_OR_B64: r64(a64 + " | " + b64); break;
    case Sop2::S_XOR_B32: r32(a + " ^ " + b); break;
    case Sop2::S_XOR_B64: r64(a64 + " ^ " + b64); break;
    case Sop2::S_ANDN2_B32: r32(a + " & ~" + b); break;
    case Sop2::S_ANDN2_B64: r64(a64 + " & ~" + b64); break;
    case Sop2::S_ORN2_B32: r32(a + " | ~" + b); break;
    case Sop2::S_ORN2_B64: r64(a64 + " | ~" + b64); break;
    case Sop2::S_NAND_B32: r32("~(" + a + " & " + b + ")"); break;
    case Sop2::S_NAND_B64: r64("~(" + a64 + " & " + b64 + ")"); break;
    case Sop2::S_NOR_B32: r32("~(" + a + " | " + b + ")"); break;
    case Sop2::S_NOR_B64: r64("~(" + a64 + " | " + b64 + ")"); break;
    case Sop2::S_XNOR_B32: r32("~(" + a + " ^ " + b + ")"); break;
    case Sop2::S_XNOR_B64: r64("~(" + a64 + " ^ " + b64 + ")"); break;
    case Sop2::S_LSHL_B32: r32(a + " << (" + b + " & 31u)"); break;
    case Sop2::S_LSHL_B64: r64(a64 + " << (" + b + " & 63u)"); break;
    case Sop2::S_LSHR_B32: r32(a + " >> (" + b + " & 31u)"); break;
    case Sop2::S_LSHR_B64: r64(a64 + " >> (" + b + " & 63u)"); break;
    case Sop2::S_ASHR_I32: r32("uint(int(" + a + ") >> (" + b + " & 31u))"); break;
    case Sop2::S_ASHR_I64: r64("ulong(long(" + a64 + ") >> (" + b + " & 63u))"); break;
    case Sop2::S_BFM_B32: SetS(i.dst, "((1u << (" + a + " & 31u)) - 1u) << (" + b + " & 31u)"); break;
    case Sop2::S_BFM_B64: SetS64(i.dst, "((1ul << (" + a + " & 63u)) - 1ul) << (" + b + " & 63u)"); break;
    case Sop2::S_MUL_I32: SetS(i.dst, "uint(int(" + a + ") * int(" + b + "))"); break;
    case Sop2::S_BFE_U32: r32("dc_sbfe_u(" + a + ", " + b + " & 31u, (" + b + " >> 16) & 0x7fu)"); break;
    case Sop2::S_BFE_I32: r32("dc_sbfe_i(" + a + ", " + b + " & 31u, (" + b + " >> 16) & 0x7fu)"); break;
    case Sop2::S_ABSDIFF_I32: r32("uint(abs(int(" + a + ") - int(" + b + ")))"); break;
    default: Unsupported(i); break;
    }
}

void Emitter::EmitSopk(const Inst& i) {
    const std::string d = SName(i.dst);
    const std::string sx = Hex(static_cast<u32>(static_cast<s32>(i.simm16)));
    const std::string zx = Hex(static_cast<u16>(i.simm16));
    static const char* cmp[] = {"==", "!=", ">", ">=", "<", "<="};
    const u16 op = i.op;
    if (op >= static_cast<u16>(Sopk::S_CMPK_EQ_I32) && op <= static_cast<u16>(Sopk::S_CMPK_LE_I32)) {
        Line("scc = int(" + d + ") " + cmp[op - 3] + " int(" + sx + ");");
        return;
    }
    if (op >= static_cast<u16>(Sopk::S_CMPK_EQ_U32) && op <= static_cast<u16>(Sopk::S_CMPK_LE_U32)) {
        Line("scc = " + d + " " + cmp[op - 9] + " " + zx + ";");
        return;
    }
    switch (static_cast<Sopk>(op)) {
    case Sopk::S_MOVK_I32: SetS(i.dst, sx); break;
    case Sopk::S_CMOVK_I32: Line("if (scc) " + d + " = " + sx + ";"); break;
    case Sopk::S_ADDK_I32:
        Line("{ int x_ = int(" + d + "), y_ = int(" + sx + "); int r_ = int(uint(x_) + uint(y_)); " + d +
             " = uint(r_); scc = ((x_ >= 0) == (y_ >= 0)) && ((r_ >= 0) != (x_ >= 0)); }");
        break;
    case Sopk::S_MULK_I32: SetS(i.dst, "uint(int(" + d + ") * int(" + sx + "))"); break;
    case Sopk::S_GETREG_B32: SetS(i.dst, "0u"); break;
    case Sopk::S_SETREG_B32:
    case Sopk::S_SETREG_IMM32_B32:
        break;
    default: Unsupported(i); break;
    }
}

void Emitter::EmitSop1(const Inst& i) {
    const std::string d = SName(i.dst);
    const std::string a = U(i, 0);
    const std::string a64 = U64(i, 0);
    auto r32 = [&](const std::string& e) { Line("{ uint r_ = " + e + "; " + d + " = r_; scc = r_ != 0u; }"); };
    auto r64 = [&](const std::string& e) {
        Line("{ ulong r_ = " + e + "; " + d + " = uint(r_); " + SName(Hi(i.dst)) +
             " = uint(r_ >> 32); scc = r_ != 0ul; }");
    };
    auto saveexec = [&](const std::string& op) {
        Line("{ ulong s_ = " + a64 + "; ulong e_ = ulong(exec_lo) | (ulong(exec_hi) << 32); " + d +
             " = uint(e_); " + SName(Hi(i.dst)) + " = uint(e_ >> 32); e_ = " + op +
             "; exec_lo = uint(e_); exec_hi = uint(e_ >> 32); scc = e_ != 0ul; }");
    };
    switch (static_cast<Sop1>(i.op)) {
    case Sop1::S_MOV_B32:
    case Sop1::S_MOV_FED_B32:
        SetS(i.dst, a);
        break;
    case Sop1::S_MOV_B64: SetS64(i.dst, a64); break;
    case Sop1::S_CMOV_B32: Line("if (scc) " + d + " = " + a + ";"); break;
    case Sop1::S_CMOV_B64:
        Line("if (scc) { ulong t_ = " + a64 + "; " + d + " = uint(t_); " + SName(Hi(i.dst)) + " = uint(t_ >> 32); }");
        break;
    case Sop1::S_NOT_B32: r32("~" + a); break;
    case Sop1::S_NOT_B64: r64("~" + a64); break;
    // Per-lane masks: whole-quad mode keeps this lane's bit.
    case Sop1::S_WQM_B32: r32(a); break;
    case Sop1::S_WQM_B64: r64(a64); break;
    case Sop1::S_BREV_B32: SetS(i.dst, "reverse_bits(" + a + ")"); break;
    case Sop1::S_BCNT0_I32_B32: r32("32u - popcount(" + a + ")"); break;
    case Sop1::S_BCNT1_I32_B32: r32("popcount(" + a + ")"); break;
    case Sop1::S_BCNT1_I32_B64: r32("uint(popcount(" + a64 + "))"); break;
    case Sop1::S_FF0_I32_B32: SetS(i.dst, "dc_ffbl(~" + a + ")"); break;
    case Sop1::S_FF1_I32_B32: SetS(i.dst, "dc_ffbl(" + a + ")"); break;
    case Sop1::S_FLBIT_I32_B32: SetS(i.dst, "dc_ffbh_u(" + a + ")"); break;
    case Sop1::S_FLBIT_I32: SetS(i.dst, "dc_ffbh_i(" + a + ")"); break;
    case Sop1::S_SEXT_I32_I8: SetS(i.dst, "uint(int(" + a + " << 24) >> 24)"); break;
    case Sop1::S_SEXT_I32_I16: SetS(i.dst, "uint(int(" + a + " << 16) >> 16)"); break;
    case Sop1::S_BITSET0_B32: Line(d + " &= ~(1u << (" + a + " & 31u));"); break;
    case Sop1::S_BITSET1_B32: Line(d + " |= 1u << (" + a + " & 31u);"); break;
    case Sop1::S_GETPC_B64: SetS64(i.dst, Hex64(PcAddress(i.pc) + 4)); break;
    case Sop1::S_AND_SAVEEXEC_B64: saveexec("s_ & e_"); break;
    case Sop1::S_OR_SAVEEXEC_B64: saveexec("s_ | e_"); break;
    case Sop1::S_XOR_SAVEEXEC_B64: saveexec("s_ ^ e_"); break;
    case Sop1::S_ANDN2_SAVEEXEC_B64: saveexec("s_ & ~e_"); break;
    case Sop1::S_ORN2_SAVEEXEC_B64: saveexec("s_ | ~e_"); break;
    case Sop1::S_NAND_SAVEEXEC_B64: saveexec("~(s_ & e_)"); break;
    case Sop1::S_NOR_SAVEEXEC_B64: saveexec("~(s_ | e_)"); break;
    case Sop1::S_XNOR_SAVEEXEC_B64: saveexec("~(s_ ^ e_)"); break;
    case Sop1::S_ABS_I32: r32("uint(abs(int(" + a + ")))"); break;
    default: Unsupported(i); break;
    }
}

void Emitter::EmitSopc(const Inst& i) {
    const std::string a = U(i, 0), b = U(i, 1);
    static const char* cmp[] = {"==", "!=", ">", ">=", "<", "<="};
    if (i.op <= static_cast<u16>(Sopc::S_CMP_LE_I32)) {
        Line("scc = int(" + a + ") " + cmp[i.op] + " int(" + b + ");");
        return;
    }
    if (i.op <= static_cast<u16>(Sopc::S_CMP_LE_U32)) {
        Line("scc = " + a + " " + cmp[i.op - 6] + " " + b + ";");
        return;
    }
    switch (static_cast<Sopc>(i.op)) {
    case Sopc::S_BITCMP0_B32: Line("scc = ((" + a + " >> (" + b + " & 31u)) & 1u) == 0u;"); break;
    case Sopc::S_BITCMP1_B32: Line("scc = ((" + a + " >> (" + b + " & 31u)) & 1u) != 0u;"); break;
    case Sopc::S_BITCMP0_B64: Line("scc = ((" + U64(i, 0) + " >> (" + b + " & 63u)) & 1ul) == 0ul;"); break;
    case Sopc::S_BITCMP1_B64: Line("scc = ((" + U64(i, 0) + " >> (" + b + " & 63u)) & 1ul) != 0ul;"); break;
    case Sopc::S_SETVSKIP: break;
    default: Unsupported(i); break;
    }
}

void Emitter::EmitSopp(const Inst& i) {
    switch (static_cast<Sopp>(i.op)) {
    case Sopp::S_ENDPGM: End(); break;
    case Sopp::S_BRANCH: Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_SCC0: Line("if (!scc)"); Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_SCC1: Line("if (scc)"); Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_VCCZ: Line("if (vcc_lo == 0u)"); Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_VCCNZ: Line("if (vcc_lo != 0u)"); Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_EXECZ: Line("if (exec_lo == 0u)"); Jump(BranchTarget(i)); break;
    case Sopp::S_CBRANCH_EXECNZ: Line("if (exec_lo != 0u)"); Jump(BranchTarget(i)); break;
    case Sopp::S_BARRIER:
        if (p.stage == Stage::Compute) {
            Line("threadgroup_barrier(mem_flags::mem_threadgroup | mem_flags::mem_device);");
        }
        break;
    default: break; // waitcnt, nop, sendmsg, priorities: nothing to do
    }
}

void Emitter::EmitSmrd(const Inst& i) {
    u32 count = 0;
    bool buffer = false;
    switch (static_cast<Smrd>(i.op)) {
    case Smrd::S_LOAD_DWORD: count = 1; break;
    case Smrd::S_LOAD_DWORDX2: count = 2; break;
    case Smrd::S_LOAD_DWORDX4: count = 4; break;
    case Smrd::S_LOAD_DWORDX8: count = 8; break;
    case Smrd::S_LOAD_DWORDX16: count = 16; break;
    case Smrd::S_BUFFER_LOAD_DWORD: count = 1; buffer = true; break;
    case Smrd::S_BUFFER_LOAD_DWORDX2: count = 2; buffer = true; break;
    case Smrd::S_BUFFER_LOAD_DWORDX4: count = 4; buffer = true; break;
    case Smrd::S_BUFFER_LOAD_DWORDX8: count = 8; buffer = true; break;
    case Smrd::S_BUFFER_LOAD_DWORDX16: count = 16; buffer = true; break;
    case Smrd::S_MEMTIME: SetS64(i.dst, "0ul"); return;
    case Smrd::S_DCACHE_INV:
    case Smrd::S_DCACHE_INV_VOL:
        return;
    default: Unsupported(i); return;
    }
    if(static_smrd && !buffer) {
        u32 packed=0;
        for(const auto& load:Gpu::Metal::StaticSmrd::Layout(p.hash)) {
            if(load.pc==i.pc) {
                for(u32 k=0;k<count;++k)
                    SetS(static_cast<u16>(i.dst+k),"dc_smrd["+N(packed+k)+"u]");
                return;
            }
            packed+=load.words;
        }
    }
    const std::string b0 = SName(i.sbase), b1 = SName(static_cast<u16>(i.sbase + 1));
    const std::string base = buffer ? "dc_vbase(" + b0 + ", " + b1 + ")"
                                    : "((ulong(" + b0 + ") | (ulong(" + b1 + ") << 32)) & 0xfffffffffffful)";
    const std::string off = i.imm ? Hex(i.smrd_offset * 4) : SName(static_cast<u16>(i.smrd_offset));
    if(direct_smrd && !buffer) for(const auto& load:Gpu::Metal::StaticSmrd::Layout(p.hash)) {
        if(load.pc!=i.pc) continue;
        const u32 slot=load.root==2 ? 0 : 1;
        std::string s="{ ulong root_ = dc_smrd_roots["+N(slot)+"u]; device uint* p_; ";
        s+="if (root_ != ~0ul) p_ = ((root_ & 1ul) != 0ul ? gm1 : gm0) + (root_ >> 1) + "+N(load.offset/4)+"u; ";
        s+="else p_ = dc_ptr_n(mem, ("+base+" + ulong("+off+")) & ~3ul, "+N(count*4)+"u);";
        for(u32 k=0;k<count;++k)
            s+=" "+SName(static_cast<u16>(i.dst+k))+" = p_ ? p_["+N(k)+"] : 0u;";
        Line(s+" }");return;
    }
    // Scalar loads ignore the low address bits: one translation for all dwords.
    std::string s = "{ " + GuestPointer() + " p_ = dc_ptr_n(mem, (" + base + " + ulong(" + off + ")) & ~3ul, " +
                    N(count * 4) + "u);";
    for (u32 k = 0; k < count; ++k) {
        s += " " + SName(static_cast<u16>(i.dst + k)) + " = p_ ? p_[" + N(k) + "] : 0u;";
    }
    s += " }";
    if(simd_smrd) {
        // Helpers must retain the scalar values used by their texture/ALU
        // derivatives, irrespective of their participation in SIMD election.
        Line("if (simd_is_helper_thread()) " + s + " else {");
        Line("if (simd_is_first()) " + s);
        for(u32 k=0;k<count;++k) {
            const auto reg=SName(static_cast<u16>(i.dst+k));
            SetS(static_cast<u16>(i.dst+k),"simd_broadcast_first("+reg+")");
        }
        Line("}");
    } else Line(s);
}

void Emitter::EmitVopc(const Inst& i) {
    const u16 op = i.op;
    bool cmpx = (op & 0x10) != 0;
    std::string cond;
    if (op < 0x80) {
        if (op & 0x20) { // f64
            Unsupported(i);
            cond = "false";
        } else {
            const std::string a = F(i, 0), b = F(i, 1);
            switch (op & 15) {
            case 0: cond = "false"; break;
            case 1: cond = a + " < " + b; break;
            case 2: cond = a + " == " + b; break;
            case 3: cond = a + " <= " + b; break;
            case 4: cond = a + " > " + b; break;
            case 5: cond = "(" + a + " < " + b + " || " + a + " > " + b + ")"; break;
            case 6: cond = a + " >= " + b; break;
            case 7: cond = "(!isnan(" + a + ") && !isnan(" + b + "))"; break;
            case 8: cond = "(isnan(" + a + ") || isnan(" + b + "))"; break;
            case 9: cond = "!(" + a + " >= " + b + ")"; break;
            case 10: cond = "!(" + a + " < " + b + " || " + a + " > " + b + ")"; break;
            case 11: cond = "!(" + a + " > " + b + ")"; break;
            case 12: cond = "!(" + a + " <= " + b + ")"; break;
            case 13: cond = "!(" + a + " == " + b + ")"; break;
            case 14: cond = "!(" + a + " < " + b + ")"; break;
            default: cond = "true"; break;
            }
        }
    } else if ((op & 0xF) == 8) { // class
        if (op & 0x20) {
            Unsupported(i);
            cond = "false";
        } else {
            cond = "dc_class(" + F(i, 0) + ", " + U(i, 1) + ")";
        }
    } else {
        std::string a, b;
        switch ((op >> 5) & 3) {
        case 0: a = I(i, 0); b = I(i, 1); break;
        case 1: a = "long(" + U64(i, 0) + ")"; b = "long(" + U64(i, 1) + ")"; break;
        case 2: a = U(i, 0); b = U(i, 1); break;
        default: a = U64(i, 0); b = U64(i, 1); break;
        }
        static const char* ops[] = {"", "<", "==", "<=", ">", "!=", ">=", ""};
        const u32 c = op & 7;
        cond = c == 0 ? "false" : c == 7 ? "true" : a + " " + ops[c] + " " + b;
    }
    SetMask(i.sdst, cond);
    if (cmpx) {
        Line("exec_lo = " + SName(i.sdst) + "; exec_hi = " + SName(Hi(i.sdst)) + ";");
    }
}

void Emitter::EmitVop(const Inst& i) {
    const std::string d = VName(i.dst);
    auto f = [&](int k) { return F(i, k); };
    auto u = [&](int k) { return U(i, k); };
    auto s = [&](int k) { return I(i, k); };
    auto carry_add = [&](const std::string& a, const std::string& b, const std::string& c) {
        Line("{ ulong r_ = ulong(" + a + ") + ulong(" + b + ")" + c + "; uint c_ = uint(r_ >> 32);");
        SetV(i.dst, "uint(r_)");
        SetMask(i.sdst, "c_ != 0u");
        Line("}");
    };
    auto borrow_sub = [&](const std::string& a, const std::string& b, const std::string& c) {
        Line("{ ulong x_ = ulong(" + a + "), y_ = ulong(" + b + ")" + c + ";");
        SetV(i.dst, "uint(x_ - y_)");
        SetMask(i.sdst, "y_ > x_");
        Line("}");
    };
    const std::string cin = " + (" + Mask(i.src[2]) + " ? 1ul : 0ul)";
    switch (static_cast<Vop>(i.op)) {
    // ---- VOP2 ----
    case Vop::V_CNDMASK_B32:
        SetV(i.dst, "(" + Mask(i.src[2]) + " ? " + ModifiedFloatBits(i, 1) + " : " + ModifiedFloatBits(i, 0) + ")");
        break;
    case Vop::V_READLANE_B32: SetS(i.dst, u(0)); break;
    case Vop::V_WRITELANE_B32: SetV(i.dst, u(0)); break;
    case Vop::V_ADD_F32: SetVF(i, f(0) + " + " + f(1)); break;
    case Vop::V_SUB_F32: SetVF(i, f(0) + " - " + f(1)); break;
    case Vop::V_SUBREV_F32: SetVF(i, f(1) + " - " + f(0)); break;
    case Vop::V_MAC_LEGACY_F32: SetVF(i, "dc_mul_legacy(" + f(0) + ", " + f(1) + ") + as_type<float>(" + d + ")"); break;
    case Vop::V_MUL_LEGACY_F32: SetVF(i, "dc_mul_legacy(" + f(0) + ", " + f(1) + ")"); break;
    case Vop::V_MUL_F32: SetVF(i, f(0) + " * " + f(1)); break;
    case Vop::V_MUL_I32_I24: SetV(i.dst, "uint(((" + s(0) + " << 8) >> 8) * ((" + s(1) + " << 8) >> 8))"); break;
    case Vop::V_MUL_HI_I32_I24:
        SetV(i.dst, "uint((long((" + s(0) + " << 8) >> 8) * long((" + s(1) + " << 8) >> 8)) >> 32)");
        break;
    case Vop::V_MUL_U32_U24: SetV(i.dst, "(" + u(0) + " & 0xffffffu) * (" + u(1) + " & 0xffffffu)"); break;
    case Vop::V_MUL_HI_U32_U24:
        SetV(i.dst, "uint((ulong(" + u(0) + " & 0xffffffu) * ulong(" + u(1) + " & 0xffffffu)) >> 32)");
        break;
    case Vop::V_MIN_LEGACY_F32:
    case Vop::V_MIN_F32: SetVF(i, "fmin(" + f(0) + ", " + f(1) + ")"); break;
    case Vop::V_MAX_LEGACY_F32:
    case Vop::V_MAX_F32: SetVF(i, "fmax(" + f(0) + ", " + f(1) + ")"); break;
    case Vop::V_MIN_I32: SetV(i.dst, "uint(min(" + s(0) + ", " + s(1) + "))"); break;
    case Vop::V_MAX_I32: SetV(i.dst, "uint(max(" + s(0) + ", " + s(1) + "))"); break;
    case Vop::V_MIN_U32: SetV(i.dst, "min(" + u(0) + ", " + u(1) + ")"); break;
    case Vop::V_MAX_U32: SetV(i.dst, "max(" + u(0) + ", " + u(1) + ")"); break;
    case Vop::V_LSHR_B32: SetV(i.dst, u(0) + " >> (" + u(1) + " & 31u)"); break;
    case Vop::V_LSHRREV_B32: SetV(i.dst, u(1) + " >> (" + u(0) + " & 31u)"); break;
    case Vop::V_ASHR_I32: SetV(i.dst, "uint(" + s(0) + " >> (" + u(1) + " & 31u))"); break;
    case Vop::V_ASHRREV_I32: SetV(i.dst, "uint(" + s(1) + " >> (" + u(0) + " & 31u))"); break;
    case Vop::V_LSHL_B32: SetV(i.dst, u(0) + " << (" + u(1) + " & 31u)"); break;
    case Vop::V_LSHLREV_B32: SetV(i.dst, u(1) + " << (" + u(0) + " & 31u)"); break;
    case Vop::V_AND_B32: SetV(i.dst, u(0) + " & " + u(1)); break;
    case Vop::V_OR_B32: SetV(i.dst, u(0) + " | " + u(1)); break;
    case Vop::V_XOR_B32: SetV(i.dst, u(0) + " ^ " + u(1)); break;
    case Vop::V_BFM_B32: SetV(i.dst, "((1u << (" + u(0) + " & 31u)) - 1u) << (" + u(1) + " & 31u)"); break;
    case Vop::V_MAC_F32: SetVF(i, f(0) + " * " + f(1) + " + as_type<float>(" + d + ")"); break;
    case Vop::V_MADMK_F32: SetVF(i, f(0) + " * as_type<float>(" + Hex(i.literal) + ") + " + f(1)); break;
    case Vop::V_MADAK_F32: SetVF(i, f(0) + " * " + f(1) + " + as_type<float>(" + Hex(i.literal) + ")"); break;
    case Vop::V_BCNT_U32_B32: SetV(i.dst, "popcount(" + u(0) + ") + " + u(1)); break;
    // Lane counting needs the real wave; per-lane execution sees itself as lane 0.
    // Counting active lanes below this one. Masks are per-lane here, so
    // assume a full mask (the common "lane id" idiom); compute knows its lane.
    // Active lanes below this one. Each lane holds its own bit of the mask,
    // so a SIMD-group prefix sum counts them (the SIMD group stands in for
    // the wave); the low half already counts every lane, the high half adds 0.
    case Vop::V_MBCNT_LO_U32_B32:
        SetV(i.dst, p.stage == Stage::Compute
                        ? "simd_prefix_exclusive_sum(" + Mask(i.src[0]) + " ? 1u : 0u) + " + u(1)
                        : u(1));
        break;
    case Vop::V_MBCNT_HI_U32_B32:
        SetV(i.dst, u(1));
        break;
    case Vop::V_ADD_I32: carry_add(u(0), u(1), ""); break;
    case Vop::V_SUB_I32: borrow_sub(u(0), u(1), ""); break;
    case Vop::V_SUBREV_I32: borrow_sub(u(1), u(0), ""); break;
    case Vop::V_ADDC_U32: carry_add(u(0), u(1), cin); break;
    case Vop::V_SUBB_U32: borrow_sub(u(0), u(1), cin); break;
    case Vop::V_SUBBREV_U32: borrow_sub(u(1), u(0), cin); break;
    case Vop::V_LDEXP_F32: SetVF(i, "ldexp(" + f(0) + ", " + s(1) + ")"); break;
    case Vop::V_CVT_PKNORM_I16_F32: SetV(i.dst, "pack_float_to_snorm2x16(float2(" + f(0) + ", " + f(1) + "))"); break;
    case Vop::V_CVT_PKNORM_U16_F32: SetV(i.dst, "pack_float_to_unorm2x16(float2(" + f(0) + ", " + f(1) + "))"); break;
    case Vop::V_CVT_PKRTZ_F16_F32: SetV(i.dst, "dc_f16_rtz(" + f(0) + ") | (dc_f16_rtz(" + f(1) + ") << 16)"); break;
    case Vop::V_CVT_PK_U16_U32: SetV(i.dst, "min(" + u(0) + ", 0xffffu) | (min(" + u(1) + ", 0xffffu) << 16)"); break;
    case Vop::V_CVT_PK_I16_I32:
        SetV(i.dst, "(uint(clamp(" + s(0) + ", -32768, 32767)) & 0xffffu) | (uint(clamp(" + s(1) + ", -32768, 32767)) << 16)");
        break;
    // ---- VOP3 only ----
    case Vop::V_MAD_LEGACY_F32: SetVF(i, "dc_mul_legacy(" + f(0) + ", " + f(1) + ") + " + f(2)); break;
    case Vop::V_MAD_F32: SetVF(i, f(0) + " * " + f(1) + " + " + f(2)); break;
    case Vop::V_FMA_F32: SetVF(i, "fma(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_MAD_I32_I24:
        SetV(i.dst, "uint(((" + s(0) + " << 8) >> 8) * ((" + s(1) + " << 8) >> 8) + " + s(2) + ")");
        break;
    case Vop::V_MAD_U32_U24: SetV(i.dst, "(" + u(0) + " & 0xffffffu) * (" + u(1) + " & 0xffffffu) + " + u(2)); break;
    case Vop::V_CUBEID_F32: SetVF(i, "dc_cubeid(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_CUBESC_F32: SetVF(i, "dc_cubesc(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_CUBETC_F32: SetVF(i, "dc_cubetc(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_CUBEMA_F32: SetVF(i, "dc_cubema(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_BFE_U32: SetV(i.dst, "dc_bfe_u(" + u(0) + ", " + u(1) + ", " + u(2) + ")"); break;
    case Vop::V_BFE_I32: SetV(i.dst, "dc_bfe_i(" + u(0) + ", " + u(1) + ", " + u(2) + ")"); break;
    case Vop::V_BFI_B32: SetV(i.dst, "(" + u(0) + " & " + u(1) + ") | (~" + u(0) + " & " + u(2) + ")"); break;
    case Vop::V_ALIGNBIT_B32:
        SetV(i.dst, "uint(((ulong(" + u(0) + ") << 32) | ulong(" + u(1) + ")) >> (" + u(2) + " & 31u))");
        break;
    case Vop::V_ALIGNBYTE_B32:
        SetV(i.dst, "uint(((ulong(" + u(0) + ") << 32) | ulong(" + u(1) + ")) >> ((" + u(2) + " & 3u) * 8u))");
        break;
    case Vop::V_MIN3_F32: SetVF(i, "fmin(fmin(" + f(0) + ", " + f(1) + "), " + f(2) + ")"); break;
    case Vop::V_MAX3_F32: SetVF(i, "fmax(fmax(" + f(0) + ", " + f(1) + "), " + f(2) + ")"); break;
    case Vop::V_MED3_F32: SetVF(i, "dc_med3(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_MIN3_I32: SetV(i.dst, "uint(min(min(" + s(0) + ", " + s(1) + "), " + s(2) + "))"); break;
    case Vop::V_MAX3_I32: SetV(i.dst, "uint(max(max(" + s(0) + ", " + s(1) + "), " + s(2) + "))"); break;
    case Vop::V_MED3_I32: SetV(i.dst, "uint(dc_med3i(" + s(0) + ", " + s(1) + ", " + s(2) + "))"); break;
    case Vop::V_MIN3_U32: SetV(i.dst, "min(min(" + u(0) + ", " + u(1) + "), " + u(2) + ")"); break;
    case Vop::V_MAX3_U32: SetV(i.dst, "max(max(" + u(0) + ", " + u(1) + "), " + u(2) + ")"); break;
    case Vop::V_MED3_U32: SetV(i.dst, "dc_med3u(" + u(0) + ", " + u(1) + ", " + u(2) + ")"); break;
    case Vop::V_SAD_U32: SetV(i.dst, "absdiff(" + u(0) + ", " + u(1) + ") + " + u(2)); break;
    case Vop::V_CVT_PK_U8_F32:
        Line("{ uint sh_ = (" + u(1) + " & 3u) * 8u;");
        SetV(i.dst, "(" + u(2) + " & ~(0xffu << sh_)) | ((uint(clamp(rint(" + f(0) + "), 0.0f, 255.0f)) & 0xffu) << sh_)");
        Line("}");
        break;
    // Division helpers: we skip the scaling, so the sequence reduces to a plain divide.
    case Vop::V_DIV_SCALE_F32:
        SetV(i.dst, "as_type<uint>(" + f(0) + ")");
        SetMask(i.sdst, "false");
        break;
    case Vop::V_DIV_FMAS_F32: SetVF(i, "fma(" + f(0) + ", " + f(1) + ", " + f(2) + ")"); break;
    case Vop::V_DIV_FIXUP_F32: SetVF(i, f(0)); break;
    case Vop::V_LSHL_B64:
    case Vop::V_LSHR_B64:
    case Vop::V_ASHR_I64: {
        const std::string a = U64(i, 0);
        std::string e;
        if (static_cast<Vop>(i.op) == Vop::V_LSHL_B64) e = a + " << (" + u(1) + " & 63u)";
        else if (static_cast<Vop>(i.op) == Vop::V_LSHR_B64) e = a + " >> (" + u(1) + " & 63u)";
        else e = "ulong(long(" + a + ") >> (" + u(1) + " & 63u))";
        Line("{ ulong r_ = " + e + ";");
        SetV(i.dst, "uint(r_)");
        SetV(i.dst + 1u, "uint(r_ >> 32)");
        Line("}");
        break;
    }
    case Vop::V_MUL_LO_U32:
    case Vop::V_MUL_LO_I32:
        SetV(i.dst, u(0) + " * " + u(1));
        break;
    case Vop::V_MUL_HI_U32: SetV(i.dst, "mulhi(" + u(0) + ", " + u(1) + ")"); break;
    case Vop::V_MUL_HI_I32: SetV(i.dst, "uint(mulhi(" + s(0) + ", " + s(1) + "))"); break;
    case Vop::V_MAD_U64_U32:
        Line("{ ulong r_ = ulong(" + u(0) + ") * ulong(" + u(1) + ") + " + U64(i, 2) + ";");
        SetV(i.dst, "uint(r_)");
        SetV(i.dst + 1u, "uint(r_ >> 32)");
        SetMask(i.sdst, "false");
        Line("}");
        break;
    // ---- VOP1 ----
    case Vop::V_NOP:
    case Vop::V_CLREXCP:
        break;
    // Relative VGPR addressing (vN + M0): a switch over every register the shader has,
    // filled in once the register count is known (see Run()).
    case Vop::V_MOVRELS_B32:
    case Vop::V_MOVRELSD_B32: {
        const u32 sbase = i.src[0] >= Operand::Vgpr0 ? i.src[0] - Operand::Vgpr0 : 0;
        Line("{ uint t_ = 0u; switch (" + N(sbase) + "u + m0r) { /*DC_REL_READ*/ default: break; }");
        if (static_cast<Vop>(i.op) == Vop::V_MOVRELS_B32) {
            SetV(i.dst, "t_");
        } else {
            Line("if (exec_lo != 0u) switch (" + N(i.dst) + "u + m0r) { /*DC_REL_WRITE*/ default: break; }");
        }
        Line("}");
        uses_movrel = true;
        break;
    }
    case Vop::V_MOVRELD_B32:
        Line("{ uint t_ = " + U(i, 0) + "; if (exec_lo != 0u) switch (" + N(i.dst) + "u + m0r) { /*DC_REL_WRITE*/ default: break; } }");
        uses_movrel = true;
        break;
    case Vop::V_MOV_B32:
    case Vop::V_MOV_FED_B32:
        SetV(i.dst, u(0));
        break;
    case Vop::V_READFIRSTLANE_B32:
        SetS(i.dst, p.stage == Stage::Compute ? "simd_broadcast_first(" + u(0) + ")" : u(0));
        break;
    case Vop::V_CVT_F32_I32: SetVF(i, "float(" + s(0) + ")"); break;
    case Vop::V_CVT_F32_U32: SetVF(i, "float(" + u(0) + ")"); break;
    case Vop::V_CVT_U32_F32: SetV(i.dst, "dc_cvt_u32(" + f(0) + ")"); break;
    case Vop::V_CVT_I32_F32: SetV(i.dst, "dc_cvt_i32(" + f(0) + ")"); break;
    case Vop::V_CVT_F16_F32: SetV(i.dst, "dc_f16(" + f(0) + ")"); break;
    case Vop::V_CVT_F32_F16: SetVF(i, "dc_f32(" + u(0) + ")"); break;
    case Vop::V_CVT_RPI_I32_F32: SetV(i.dst, "dc_cvt_i32(floor(" + f(0) + " + 0.5f))"); break;
    case Vop::V_CVT_FLR_I32_F32: SetV(i.dst, "dc_cvt_i32(floor(" + f(0) + "))"); break;
    case Vop::V_CVT_OFF_F32_I4: SetVF(i, "float(int(" + u(0) + " << 28) >> 28) / 16.0f"); break;
    case Vop::V_CVT_F32_UBYTE0: SetVF(i, "float(" + u(0) + " & 0xffu)"); break;
    case Vop::V_CVT_F32_UBYTE1: SetVF(i, "float((" + u(0) + " >> 8) & 0xffu)"); break;
    case Vop::V_CVT_F32_UBYTE2: SetVF(i, "float((" + u(0) + " >> 16) & 0xffu)"); break;
    case Vop::V_CVT_F32_UBYTE3: SetVF(i, "float(" + u(0) + " >> 24)"); break;
    case Vop::V_FRACT_F32: SetVF(i, "fract(" + f(0) + ")"); break;
    case Vop::V_TRUNC_F32: SetVF(i, "trunc(" + f(0) + ")"); break;
    case Vop::V_CEIL_F32: SetVF(i, "ceil(" + f(0) + ")"); break;
    case Vop::V_RNDNE_F32: SetVF(i, "rint(" + f(0) + ")"); break;
    case Vop::V_FLOOR_F32: SetVF(i, "floor(" + f(0) + ")"); break;
    case Vop::V_EXP_F32:
    case Vop::V_EXP_LEGACY_F32:
        SetVF(i, "exp2(" + f(0) + ")");
        break;
    case Vop::V_LOG_F32:
    case Vop::V_LOG_LEGACY_F32:
        SetVF(i, "log2(" + f(0) + ")");
        break;
    case Vop::V_LOG_CLAMP_F32: SetVF(i, "fmax(log2(" + f(0) + "), -FLT_MAX)"); break;
    case Vop::V_RCP_F32:
    case Vop::V_RCP_IFLAG_F32:
    case Vop::V_RCP_LEGACY_F32:
        SetVF(i, "1.0f / " + f(0));
        break;
    case Vop::V_RCP_CLAMP_F32: SetVF(i, "clamp(1.0f / " + f(0) + ", -FLT_MAX, FLT_MAX)"); break;
    case Vop::V_RSQ_F32:
    case Vop::V_RSQ_LEGACY_F32:
        SetVF(i, "rsqrt(" + f(0) + ")");
        break;
    case Vop::V_RSQ_CLAMP_F32: SetVF(i, "clamp(rsqrt(" + f(0) + "), -FLT_MAX, FLT_MAX)"); break;
    case Vop::V_SQRT_F32: SetVF(i, "sqrt(" + f(0) + ")"); break;
    case Vop::V_SIN_F32: SetVF(i, "sin(" + f(0) + " * 6.28318530718f)"); break;
    case Vop::V_COS_F32: SetVF(i, "cos(" + f(0) + " * 6.28318530718f)"); break;
    case Vop::V_NOT_B32: SetV(i.dst, "~" + u(0)); break;
    case Vop::V_BFREV_B32: SetV(i.dst, "reverse_bits(" + u(0) + ")"); break;
    case Vop::V_FFBH_U32: SetV(i.dst, "dc_ffbh_u(" + u(0) + ")"); break;
    case Vop::V_FFBL_B32: SetV(i.dst, "dc_ffbl(" + u(0) + ")"); break;
    case Vop::V_FFBH_I32: SetV(i.dst, "dc_ffbh_i(" + u(0) + ")"); break;
    case Vop::V_FREXP_EXP_I32_F32:
        Line("{ int e_; frexp(" + f(0) + ", e_);");
        SetV(i.dst, "uint(e_)");
        Line("}");
        break;
    case Vop::V_FREXP_MANT_F32:
        Line("{ int e_; float m_ = frexp(" + f(0) + ", e_);");
        SetVF(i, "m_");
        Line("}");
        break;
    default: Unsupported(i); break;
    }
}

std::string Emitter::AttrExpr(u32 attr, u32 chan) {
    const u32 cntl = o.ps_input_cntl[attr & 31];
    const u32 off = cntl & 0x3F;
    if ((off & 0x20) || !((o.vs_param_mask >> off) & 1)) {
        static const char* defaults[4][4] = {
            {"0.0f", "0.0f", "0.0f", "0.0f"},
            {"0.0f", "0.0f", "0.0f", "1.0f"},
            {"1.0f", "1.0f", "1.0f", "0.0f"},
            {"1.0f", "1.0f", "1.0f", "1.0f"},
        };
        return defaults[(cntl >> 8) & 3][chan & 3];
    }
    ps_params.insert(off);
    return "pin.p" + N(off) + "[" + N(chan) + "]";
}

void Emitter::EmitVintrp(const Inst& i) {
    if (p.stage != Stage::Fragment) {
        Unsupported(i);
        return;
    }
    if (static_cast<Vintrp>(i.op) == Vintrp::V_INTERP_MOV_F32 && i.src[0] - Operand::Vgpr0 != 2) {
        SetV(i.dst, "0u"); // P10 / P20: vertex deltas
        return;
    }
    if (static_cast<Vintrp>(i.op)==Vintrp::V_INTERP_P2_F32) {
        // P2's J input identifies the interpolation pair in SPI_PS_INPUT_ADDR.
        // In particular, linear interpolation cannot use Metal's default
        // perspective correction for projected screen/atlas coordinates.
        const u32 source=i.src[0]-Operand::Vgpr0;
        u32 reg=0;
        for (u32 bit=0;bit<7;++bit) {
            if (!(o.ps_input_addr&(1u<<bit))) continue;
            const u32 width=bit==3 ? 3 : 2;
            if (source>=reg && source<reg+width) {
                const u32 param=o.ps_input_cntl[i.attr&31]&0x3f;
                const auto [it,added]=ps_interpolation.try_emplace(param,bit);
                if (!added && it->second!=bit) {
                    // One Metal float4 cannot carry different interpolation
                    // qualifiers per component. Preserve the previous smooth
                    // behavior for that uncommon layout and report it.
                    it->second=1;
                    Unsupported(i);
                }
                break;
            }
            reg+=width;
        }
    }
    SetV(i.dst, "as_type<uint>(" + AttrExpr(i.attr, i.chan) + ")");
}

void Emitter::EmitDs(const Inst& i) {
    if (static_cast<Ds>(i.op) == Ds::DS_SWIZZLE_B32 && p.stage == Stage::Fragment) {
        const u32 pattern = (u32(i.offset1) << 8) | i.offset0;
        if (pattern & 0x8000) { // quad permute: 2 bits of source lane per quad lane
            SetV(i.dst, "quad_shuffle(" + VName(i.vaddr) + ", (" + Hex(pattern & 0xFF) +
                            " >> (dc_qlane * 2u)) & 3u)");
            return;
        }
    }
    if (p.stage != Stage::Compute && o.tess_stage == TessStage::None) {
        Unsupported(i);
        return;
    }
    // LDS: `lds` is the threadgroup memory as dwords; addresses are bytes.
    const std::string a = VName(i.vaddr);
    const u32 off16 = (u32(i.offset1) << 8) | i.offset0;
    const std::string guard = p.writes_exec ? "if (exec_lo != 0u) " : "";
    auto at = [&](u32 byte_off) {
        if (o.native_tess_layout) return "dc_native_tess_load(lds, " + a + " + " + N(byte_off) + "u)";
        return "lds[(" + a + " + " + N(byte_off) + "u) >> 2]";
    };
    auto atomic = [&](const char* fn, bool rtn, bool sign) {
        const std::string space = o.tess_stage == TessStage::None ? "threadgroup" : "device";
        const std::string ty = space + (sign ? " atomic_int*" : " atomic_uint*");
        const std::string val = sign ? "int(" + VName(i.data0) + ")" : VName(i.data0);
        const std::string call = std::string(fn) + "((" + ty + ")&" + at(off16) + ", " + val + ", memory_order_relaxed)";
        if (rtn) {
            SetV(i.dst, "uint(" + call + ")");
        } else {
            Line(guard + "(void)" + call + ";");
        }
    };
    switch (static_cast<Ds>(i.op)) {
    case Ds::DS_WRITE_B32: Line(guard + at(off16) + " = " + VName(i.data0) + ";"); break;
    case Ds::DS_WRITE2_B32:
    case Ds::DS_WRITE2ST64_B32: {
        const u32 scale = static_cast<Ds>(i.op) == Ds::DS_WRITE2ST64_B32 ? 256 : 4;
        Line(guard + "{ " + at(i.offset0 * scale) + " = " + VName(i.data0) + "; " + at(i.offset1 * scale) + " = " +
             VName(i.data1) + "; }");
        break;
    }
    case Ds::DS_WRITE_B64:
        Line(guard + "{ " + at(off16) + " = " + VName(i.data0) + "; " + at(off16 + 4) + " = " + VName(i.data0 + 1u) + "; }");
        break;
    case Ds::DS_WRITE2_B64:
        Line(guard + "{ " + at(i.offset0 * 8) + " = " + VName(i.data0) + "; " + at(i.offset0 * 8 + 4) + " = " +
             VName(i.data0 + 1u) + "; " + at(i.offset1 * 8) + " = " + VName(i.data1) + "; " + at(i.offset1 * 8 + 4) +
             " = " + VName(i.data1 + 1u) + "; }");
        break;
    case Ds::DS_WRITE_B96:
    case Ds::DS_WRITE_B128: {
        const u32 n = static_cast<Ds>(i.op) == Ds::DS_WRITE_B96 ? 3 : 4;
        std::string w = guard + "{ ";
        for (u32 k = 0; k < n; ++k) {
            w += at(off16 + 4 * k) + " = " + VName(i.data0 + k) + "; ";
        }
        Line(w + "}");
        break;
    }
    case Ds::DS_WRITE_B8:
        Line(guard + "((" + (o.tess_stage == TessStage::None ? std::string("threadgroup") : "device") +
             " uchar*)lds)[" + a + " + " + N(off16) + "u] = uchar(" + VName(i.data0) + ");");
        break;
    case Ds::DS_WRITE_B16:
        Line(guard + "((" + (o.tess_stage == TessStage::None ? std::string("threadgroup") : "device") +
             " ushort*)lds)[(" + a + " + " + N(off16) + "u) >> 1] = ushort(" + VName(i.data0) + ");");
        break;
    case Ds::DS_READ_B32: SetV(i.dst, at(off16)); break;
    case Ds::DS_READ2_B32:
    case Ds::DS_READ2ST64_B32: {
        const u32 scale = static_cast<Ds>(i.op) == Ds::DS_READ2ST64_B32 ? 256 : 4;
        Line("{ uint r0_ = " + at(i.offset0 * scale) + ", r1_ = " + at(i.offset1 * scale) + ";");
        SetV(i.dst, "r0_");
        SetV(i.dst + 1u, "r1_");
        Line("}");
        break;
    }
    case Ds::DS_READ_B64:
    case Ds::DS_READ_B96:
    case Ds::DS_READ_B128: {
        const u32 n = static_cast<Ds>(i.op) == Ds::DS_READ_B64 ? 2 : static_cast<Ds>(i.op) == Ds::DS_READ_B96 ? 3 : 4;
        std::string r = "{ uint r_[4] = {";
        for (u32 k = 0; k < n; ++k) {
            r += (k ? ", " : "") + at(off16 + 4 * k);
        }
        Line(r + "};");
        for (u32 k = 0; k < n; ++k) {
            SetV(i.dst + k, "r_[" + N(k) + "]");
        }
        Line("}");
        break;
    }
    case Ds::DS_READ2_B64:
        Line("{ uint r_[4] = {" + at(i.offset0 * 8) + ", " + at(i.offset0 * 8 + 4) + ", " + at(i.offset1 * 8) + ", " +
             at(i.offset1 * 8 + 4) + "};");
        for (u32 k = 0; k < 4; ++k) {
            SetV(i.dst + k, "r_[" + N(k) + "]");
        }
        Line("}");
        break;
    case Ds::DS_READ_U8: SetV(i.dst, "uint(((threadgroup uchar*)lds)[" + a + " + " + N(off16) + "u])"); break;
    case Ds::DS_READ_I8: SetV(i.dst, "uint(int(((threadgroup char*)lds)[" + a + " + " + N(off16) + "u]))"); break;
    case Ds::DS_READ_U16:
        SetV(i.dst, "uint(((threadgroup ushort*)lds)[(" + a + " + " + N(off16) + "u) >> 1])");
        break;
    case Ds::DS_READ_I16:
        SetV(i.dst, "uint(int(((threadgroup short*)lds)[(" + a + " + " + N(off16) + "u) >> 1]))");
        break;
    case Ds::DS_APPEND:
    case Ds::DS_CONSUME: {
        // One atomic per wave (SIMD group): returns the wave's base slot;
        // lanes add their mbcnt to it.
        const bool append = static_cast<Ds>(i.op) == Ds::DS_APPEND;
        // M0's high half selects the GDS allocation. Its low half describes
        // LDS size and must not redirect global work-list counters.
        const std::string idx = "(((m0r >> 16) + " + N(off16) + "u) >> 2)";
        const std::string target = i.gds ? "(device atomic_uint*)&gds[" + idx + "]"
                                         : "(threadgroup atomic_uint*)&lds[" + idx + "]";
        Line(std::string("{ uint act_ = ") + (p.writes_exec ? "exec_lo != 0u ? 1u : 0u" : "1u") +
             "; uint cnt_ = simd_sum(act_); uint base_ = 0u;");
        Line("  if (simd_is_first()) base_ = atomic_fetch_" + std::string(append ? "add" : "sub") + "_explicit(" +
             target + ", cnt_, memory_order_relaxed);");
        Line("  base_ = simd_broadcast_first(base_);");
        // Both operations return the pre-reservation counter. Consume shaders
        // subtract the masked lane index and one themselves; subtracting cnt_
        // here a second time skips live entries and can underflow the list.
        SetV(i.dst, "base_");
        Line("}");
        break;
    }
    case Ds::DS_ADD_U32: atomic("atomic_fetch_add_explicit", false, false); break;
    case Ds::DS_ADD_RTN_U32: atomic("atomic_fetch_add_explicit", true, false); break;
    case Ds::DS_SUB_U32: atomic("atomic_fetch_sub_explicit", false, false); break;
    case Ds::DS_MIN_I32: atomic("atomic_fetch_min_explicit", false, true); break;
    case Ds::DS_MAX_I32: atomic("atomic_fetch_max_explicit", false, true); break;
    case Ds::DS_MIN_U32: atomic("atomic_fetch_min_explicit", false, false); break;
    case Ds::DS_MAX_U32: atomic("atomic_fetch_max_explicit", false, false); break;
    case Ds::DS_AND_B32: atomic("atomic_fetch_and_explicit", false, false); break;
    case Ds::DS_OR_B32: atomic("atomic_fetch_or_explicit", false, false); break;
    case Ds::DS_XOR_B32: atomic("atomic_fetch_xor_explicit", false, false); break;
    default: Unsupported(i); break;
    }
}

void Emitter::EmitBuffer(const Inst& i) {
    // The appended hull SGPR is the hardware tessellation-factor buffer base.
    // Its stores become the half factors consumed by Metal's tessellator.
    if (o.tess_stage == TessStage::Hull && i.enc == Enc::MUBUF &&
        i.soffset == p.num_user_sgprs && !i.idxen && !i.addr64 &&
        static_cast<Mubuf>(i.op) == Mubuf::BUFFER_STORE_DWORD) {
        const std::string off = (i.offen ? VName(i.vaddr) : "0u") + " + " + N(i.offset) + "u";
        Line("{ uint a_ = " + off + "; if (a_ < 24u && (a_ & 3u) == 0u && exec_lo != 0u) "
             "dc_tf[a_ >> 2] = half(as_type<float>(" + VName(i.vdata) + ")); }");
        return;
    }
    const u16 r = i.srsrc;
    const std::string r0 = SName(r), r1 = SName(r + 1u), r2 = SName(r + 2u), r3 = SName(r + 3u);
    std::string idx = "0u", voff = "0u";
    if (i.idxen) {
        idx = VName(i.vaddr);
        if (i.offen) {
            voff = VName(i.vaddr + 1u);
        }
    } else if (i.offen) {
        voff = VName(i.vaddr);
    }
    Line("{ ulong base_ = dc_vbase(" + r0 + ", " + r1 + "); uint stride_ = dc_vstride(" + r1 + ");");
    Line("  uint idx_ = " + idx + "; uint off_ = " + Src(i.soffset, 0) + " + " + N(i.offset) + "u + " + voff + ";");
    Line(std::string("  bool oob_ = ") + (i.idxen ? "idx_ >= " + r2 : "(stride_ == 0u && off_ >= " + r2 + ")") + ";");
    Line("  ulong a_ = base_ + ulong(idx_) * ulong(stride_) + ulong(off_);");

    auto store_guard = [&]() { return std::string(p.writes_exec ? "if (!oob_ && exec_lo != 0u) " : "if (!oob_) "); };
    auto load_fmt = [&](u32 n, const std::string& fetch) {
        Line("  uint4 f_ = oob_ ? uint4(0u) : " + fetch + ";");
        for (u32 k = 0; k < n; ++k) {
            SetV(i.vdata + k, "f_[" + N(k) + "]");
        }
    };
    if (i.enc == Enc::MTBUF) {
        const u32 op = i.op;
        const std::string fetch = "dc_load_fmt(mem, a_, " + N(i.dfmt) + "u, " + N(i.nfmt) + "u)";
        if (op <= static_cast<u32>(Mtbuf::TBUFFER_LOAD_FORMAT_XYZW)) {
            load_fmt(op + 1, fetch);
        } else if (i.dfmt == 4 || i.dfmt == 11 || i.dfmt == 13 || i.dfmt == 14) { // 32-bit stores
            const u32 n = op - static_cast<u32>(Mtbuf::TBUFFER_STORE_FORMAT_X) + 1;
            for (u32 k = 0; k < n; ++k) {
                Line("  " + store_guard() + "dc_st(mem, a_ + " + N(4 * k) + "ul, " + VName(i.vdata + k) + ");");
            }
        } else {
            Unsupported(i);
        }
        Line("}");
        return;
    }

    const std::string vfmt = "dc_swizzle(dc_load_fmt(mem, a_, (" + r3 + " >> 15) & 15u, (" + r3 +
                             " >> 12) & 7u), " + r3 + ")";
    auto atomic = [&](const char* fn, bool sign) {
        const std::string ty = sign ? "atomic_int" : "atomic_uint";
        const std::string val = sign ? "int(" + VName(i.vdata) + ")" : VName(i.vdata);
        Line("  device uint* p_ = oob_ ? nullptr : dc_ptr(mem, a_);");
        Line("  uint r_ = 0u; " + std::string(p.writes_exec ? "if (p_ && exec_lo != 0u) " : "if (p_) ") +
             "r_ = uint(" + fn + "((device " + ty + "*)p_, " + val + ", memory_order_relaxed));");
        if (i.glc) {
            SetV(i.vdata, "r_");
        }
    };
    switch (static_cast<Mubuf>(i.op)) {
    case Mubuf::BUFFER_LOAD_FORMAT_X: load_fmt(1, vfmt); break;
    case Mubuf::BUFFER_LOAD_FORMAT_XY: load_fmt(2, vfmt); break;
    case Mubuf::BUFFER_LOAD_FORMAT_XYZ: load_fmt(3, vfmt); break;
    case Mubuf::BUFFER_LOAD_FORMAT_XYZW: load_fmt(4, vfmt); break;
    case Mubuf::BUFFER_LOAD_UBYTE: SetV(i.vdata, "oob_ ? 0u : (dc_ld(mem, a_) & 0xffu)"); break;
    case Mubuf::BUFFER_LOAD_SBYTE: SetV(i.vdata, "oob_ ? 0u : uint(int(dc_ld(mem, a_) << 24) >> 24)"); break;
    case Mubuf::BUFFER_LOAD_USHORT: SetV(i.vdata, "oob_ ? 0u : (dc_ld(mem, a_) & 0xffffu)"); break;
    case Mubuf::BUFFER_LOAD_SSHORT: SetV(i.vdata, "oob_ ? 0u : uint(int(dc_ld(mem, a_) << 16) >> 16)"); break;
    case Mubuf::BUFFER_LOAD_DWORD:
    case Mubuf::BUFFER_LOAD_DWORDX2:
    case Mubuf::BUFFER_LOAD_DWORDX3:
    case Mubuf::BUFFER_LOAD_DWORDX4: {
        const u32 n = i.op == static_cast<u16>(Mubuf::BUFFER_LOAD_DWORD) ? 1
                    : i.op == static_cast<u16>(Mubuf::BUFFER_LOAD_DWORDX2) ? 2
                    : i.op == static_cast<u16>(Mubuf::BUFFER_LOAD_DWORDX3) ? 3 : 4;
        if (n == 1) {
            SetV(i.vdata, "oob_ ? 0u : dc_ld(mem, a_)");
            break;
        }
        Line("{ " + GuestPointer() + " p_ = oob_ || (a_ & 3ul) ? nullptr : dc_ptr_n(mem, a_, " + N(4 * n) + "u);");
        for (u32 k = 0; k < n; ++k) {
            SetV(i.vdata + k, "p_ ? p_[" + N(k) + "] : (oob_ ? 0u : dc_ld(mem, a_ + " + N(4 * k) + "ul))");
        }
        Line("}");
        break;
    }
    case Mubuf::BUFFER_STORE_FORMAT_X:
    case Mubuf::BUFFER_STORE_FORMAT_XY:
    case Mubuf::BUFFER_STORE_FORMAT_XYZ:
    case Mubuf::BUFFER_STORE_FORMAT_XYZW: {
        const u32 n = i.op - static_cast<u32>(Mubuf::BUFFER_STORE_FORMAT_X) + 1;
        std::string v = "uint4(";
        for (u32 k = 0; k < 4; ++k) {
            v += (k ? ", " : "") + (k < n ? VName(i.vdata + k) : std::string("0u"));
        }
        Line("  " + store_guard() + "dc_store_fmt(mem, a_, " + v + "), (" + r3 + " >> 15) & 15u, (" + r3 +
             " >> 12) & 7u);");
        break;
    }
    case Mubuf::BUFFER_STORE_BYTE: Line("  " + store_guard() + "dc_st_part(mem, a_, " + VName(i.vdata) + ", 1u);"); break;
    case Mubuf::BUFFER_STORE_SHORT: Line("  " + store_guard() + "dc_st_part(mem, a_, " + VName(i.vdata) + ", 2u);"); break;
    case Mubuf::BUFFER_STORE_DWORD:
    case Mubuf::BUFFER_STORE_DWORDX2:
    case Mubuf::BUFFER_STORE_DWORDX3:
    case Mubuf::BUFFER_STORE_DWORDX4: {
        const u32 n = i.op == static_cast<u16>(Mubuf::BUFFER_STORE_DWORD) ? 1
                    : i.op == static_cast<u16>(Mubuf::BUFFER_STORE_DWORDX2) ? 2
                    : i.op == static_cast<u16>(Mubuf::BUFFER_STORE_DWORDX3) ? 3 : 4;
        for (u32 k = 0; k < n; ++k) {
            Line("  " + store_guard() + "dc_st(mem, a_ + " + N(4 * k) + "ul, " + VName(i.vdata + k) + ");");
        }
        break;
    }
    case Mubuf::BUFFER_ATOMIC_ADD: atomic("atomic_fetch_add_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_SUB: atomic("atomic_fetch_sub_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_SMIN: atomic("atomic_fetch_min_explicit", true); break;
    case Mubuf::BUFFER_ATOMIC_UMIN: atomic("atomic_fetch_min_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_SMAX: atomic("atomic_fetch_max_explicit", true); break;
    case Mubuf::BUFFER_ATOMIC_UMAX: atomic("atomic_fetch_max_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_AND: atomic("atomic_fetch_and_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_OR: atomic("atomic_fetch_or_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_XOR: atomic("atomic_fetch_xor_explicit", false); break;
    case Mubuf::BUFFER_ATOMIC_SWAP: atomic("atomic_exchange_explicit", false); break;
    case Mubuf::BUFFER_WBINVL1:
    case Mubuf::BUFFER_WBINVL1_SC:
        break;
    default: Unsupported(i); break;
    }
    Line("}");
}

// DC_NAN_PROBE: float outputs that are NaN turn magenta, infinite ones yellow.
static bool NanProbe() {
    static const bool on = std::getenv("DC_NAN_PROBE") != nullptr;
    return on;
}

// Depth-compare result (1 or 0) for S# DEPTH_COMPARE_FUNC `func`: ref <op> texel.
static std::string Compare(u8 func, const std::string& ref, const std::string& texel) {
    static const char* ops[8] = {nullptr, "<", "==", "<=", ">", "!=", ">=", nullptr};
    if (func == 0) return "0.0f";
    if (func == 7) return "1.0f";
    return "(" + ref + " " + ops[func & 7] + " " + texel + " ? 1.0f : 0.0f)";
}

void Emitter::EmitImage(const Inst& i) {
    const u32 slot = p.ImageSlot(i.pc);
    const TextureType tt = slot < o.textures.size() ? o.textures[slot] : TextureType{};
    const u32 s_idx = slot < p.images.size() && p.images[slot].has_sampler ? p.images[slot].sampler_index : 0;
    const std::string t = "t" + N(slot), smp = "smp" + N(std::min<u32>(s_idx, 15u));
    const auto op = static_cast<Mimg>(i.op);
    const bool frag = p.stage == Stage::Fragment;
    const char* vec = tt.sample == TexSample::Float ? "float4" : tt.sample == TexSample::Uint ? "uint4" : "int4";
    auto write_dmask = [&](const std::string& value) {
        u32 n = 0;
        for (u32 k = 0; k < 4; ++k) {
            if (i.dmask & (1u << k)) {
                SetV(i.vdata + n++, "as_type<uint>(" + value + "[" + N(k) + "])");
            }
        }
    };
    auto vf = [&](u32 reg) { return "as_type<float>(" + VName(reg) + ")"; };

    if (op == Mimg::IMAGE_GET_RESINFO) {
        const std::string mip = VName(i.vaddr);
        std::string depth = "1u";
        if (tt.dim == TexDim::Tex3D) depth = t + ".get_depth(" + mip + ")";
        else if (tt.dim == TexDim::Tex2DArray) depth = t + ".get_array_size()";
        Line("{ uint4 r_ = uint4(" + t + ".get_width(" + mip + "), " + t + ".get_height(" + mip + "), " + depth +
             ", " + t + ".get_num_mip_levels());");
        write_dmask("r_");
        Line("}");
        return;
    }
    const bool storage = slot < p.images.size() && p.images[slot].storage;
    auto int_coord = [&](u32 a) {
        if (tt.dim == TexDim::Tex2D) return "uint2(" + VName(a) + ", " + VName(a + 1) + ")";
        if (tt.dim == TexDim::Tex2DArray) return "uint2(" + VName(a) + ", " + VName(a + 1) + "), " + VName(a + 2);
        return "uint3(" + VName(a) + ", " + VName(a + 1) + ", " + VName(a + 2) + ")";
    };
    if (op == Mimg::IMAGE_STORE || op == Mimg::IMAGE_STORE_MIP) {
        // Data registers hold the components selected by dmask, in order.
        std::string comps[4] = {"0u", "0u", "0u", "0u"};
        u32 n = 0;
        for (u32 k = 0; k < 4; ++k) {
            if (i.dmask & (1u << k)) {
                comps[k] = VName(i.vdata + n++);
            }
        }
        std::string value;
        if (tt.sample == TexSample::Float) {
            value = "float4(as_type<float>(" + comps[0] + "), as_type<float>(" + comps[1] + "), as_type<float>(" +
                    comps[2] + "), as_type<float>(" + comps[3] + "))";
            if (NanProbe()) value = "dc_probe(" + value + ")";
            if (tt.ufloat) value = "dc_ufloat(" + value + ")";
        } else {
            value = std::string(vec) + "(uint4(" + comps[0] + ", " + comps[1] + ", " + comps[2] + ", " + comps[3] + "))";
        }
        Line((p.writes_exec ? "if (exec_lo != 0u) " : "") + t + ".write(" + value + ", " + int_coord(i.vaddr) + ");");
        return;
    }
    if (op == Mimg::IMAGE_LOAD || op == Mimg::IMAGE_LOAD_MIP) {
        const u32 a = i.vaddr;
        std::string lod = op == Mimg::IMAGE_LOAD_MIP ? VName(a + (tt.dim == TexDim::Tex2D ? 2u : 3u)) : "0u";
        std::string coord = int_coord(a) + (storage ? "" : ", " + lod);
        Line(std::string("{ ") + vec + " r_ = " + vec + "(" + t + ".read(" + coord + "));");
        write_dmask("r_");
        Line("}");
        return;
    }
    const bool gather = i.op >= static_cast<u16>(Mimg::IMAGE_GATHER4) && i.op < static_cast<u16>(Mimg::IMAGE_GET_LOD);
    const bool sample = i.op >= static_cast<u16>(Mimg::IMAGE_SAMPLE) && i.op < static_cast<u16>(Mimg::IMAGE_GATHER4);
    if (!gather && !sample && op != Mimg::IMAGE_GET_LOD) {
        Unsupported(i);
        return;
    }
    // Address layout: [offset] [bias] [compare] [derivatives] coords [lod | clamp]
    const u32 v = gather ? i.op - 64u : i.op - 32u;
    const bool has_o = v >= 16;
    const u32 kind = v & 15;
    const bool c = kind >= 8 && op != Mimg::IMAGE_GET_LOD;
    const u32 k = kind & 7;
    const bool d = sample && (k == 2 || k == 3);
    const bool l = k == 4;
    const bool b = k == 5 || k == 6;
    const bool lz = k == 7;
    u32 cur = i.vaddr;
    std::string offset, bias, ref, grad;
    if (has_o) offset = VName(cur++);
    if (b) bias = vf(cur++);
    if (c) ref = vf(cur++);
    const bool three = tt.dim != TexDim::Tex2D;
    if (d) {
        if (tt.dim == TexDim::Tex3D) {
            grad = "gradient3d(float3(" + vf(cur) + ", " + vf(cur + 1) + ", " + vf(cur + 2) + "), float3(" +
                   vf(cur + 3) + ", " + vf(cur + 4) + ", " + vf(cur + 5) + "))";
            cur += 6;
        } else {
            grad = "gradient2d(float2(" + vf(cur) + ", " + vf(cur + 1) + "), float2(" + vf(cur + 2) + ", " +
                   vf(cur + 3) + "))";
            cur += 4;
        }
    }
    const std::string s = vf(cur), tc = vf(cur + 1);
    const std::string r = three ? vf(cur + 2) : "";
    cur += three ? 3 : 2;
    const std::string lod = l ? vf(cur) : "";

    std::string coord;
    std::string layer = r;
    if (tt.dim == TexDim::Tex3D) {
        coord = "float3(" + s + ", " + tc + ", " + r + ")";
    } else if (tt.cube) {
        // Cube math leaves face coordinates in [1, 2]; the face (or 8 * slice + face
        // for cube arrays) indexes the faces stored as array layers.
        coord = "float2(" + s + " - 1.0f, " + tc + " - 1.0f)";
        layer = "(" + r + " - 2.0f * floor(" + r + " * 0.125f))";
    } else {
        coord = "float2(" + s + ", " + tc + ")";
    }
    Line("{");
    if (!offset.empty() && tt.dim != TexDim::Tex3D) {
        Line("  float2 o_ = float2(int(" + offset + " << 26) >> 26, int(" + offset + " << 18) >> 26) / float2(" + t +
             ".get_width(), " + t + ".get_height());");
        coord = "(" + coord + " + o_)";
    }
    std::string args = smp + ", " + coord;
    if (tt.dim == TexDim::Tex2DArray) {
        args += ", uint(max(rint(" + layer + "), 0.0f))";
    }
    if (op == Mimg::IMAGE_GET_LOD) {
        if (!frag) {
            Unsupported(i);
            Line("}");
            return;
        }
        Line("  float4 r_ = float4(" + t + ".calculate_clamped_lod(" + smp + ", " + coord + "), " + t +
             ".calculate_unclamped_lod(" + smp + ", " + coord + "), 0.0f, 0.0f);");
        write_dmask("r_");
        Line("}");
        return;
    }
    if (gather) {
        u32 comp = 0;
        while (comp < 3 && !(i.dmask & (1u << comp))) {
            ++comp;
        }
        static const char* comps[] = {"component::x", "component::y", "component::z", "component::w"};
        const std::string offs = tt.dim == TexDim::Tex3D ? "" : ", int2(0)";
        if (c && tt.native_depth) {
            Line("  float4 r_ = " + t + ".gather_compare(" + args + ", " + ref + offs + ");");
        } else {
            Line(std::string("  ") + vec + " r_ = " + t + ".gather(" + args + offs +
                 (tt.native_depth ? "" : ", " + std::string(comps[comp])) + ");");
        }
        if (c && !tt.native_depth) {
            Line("  r_ = " + std::string(vec) + "(" + Compare(tt.compare, ref, "r_.x") + ", " +
                 Compare(tt.compare, ref, "r_.y") + ", " + Compare(tt.compare, ref, "r_.z") + ", " +
                 Compare(tt.compare, ref, "r_.w") + ");");
        }
        for (u32 n = 0; n < 4; ++n) {
            SetV(i.vdata + n, "as_type<uint>(r_[" + N(n) + "])");
        }
        Line("}");
        return;
    }
    if (c && tt.native_depth) args += ", " + ref;
    if (l) args += ", level(" + lod + ")";
    else if (lz || !frag) args += ", level(0.0f)";
    else if (b) args += ", bias(" + bias + ")";
    else if (d) args += ", " + grad;
    Line(std::string("  ") + vec + " r_ = " + vec + "(" + t +
         (c && tt.native_depth ? ".sample_compare(" : ".sample(") + args + "));");
    if (c && !tt.native_depth) {
        Line("  r_ = " + std::string(vec) + "(" + Compare(tt.compare, ref, "r_.x") + ");");
    }
    write_dmask("r_");
    Line("}");
}

void Emitter::EmitExp(const Inst& i) {
    auto comp = [&](u32 k, const char* fallback) {
        return (i.en & (1u << k)) ? "as_type<float>(" + VName(i.vsrc[k]) + ")" : std::string(fallback);
    };
    if (p.stage == Stage::Vertex) {
        std::string value;
        if (i.compr) {
            value = "float4(float2(as_type<half2>(" + VName(i.vsrc[0]) + ")), float2(as_type<half2>(" +
                    VName(i.vsrc[1]) + ")))";
        } else {
            value = "float4(" + comp(0, "0.0f") + ", " + comp(1, "0.0f") + ", " + comp(2, "0.0f") + ", " +
                    comp(3, "1.0f") + ")";
        }
        if (i.target == ExpTarget::Pos0) {
            Line("out.pos = " + value + ";");
        } else if (i.target == ExpTarget::Pos0 + 1 && o.points && o.vtx_point_size) {
            Line("out.psize = " + value + ".x;");
        } else if (i.target >= ExpTarget::Param0) {
            Line("out.p" + N(i.target - ExpTarget::Param0) + " = " + value + ";");
        }
        return;
    }
    if (p.stage != Stage::Fragment) {
        return;
    }
    if (p.writes_exec && !opaque_exports) {
        Line("if (exec_lo == 0u) discard_fragment();");
    }
    Line("dc_exported = true;");
    if (i.target == ExpTarget::MrtZ) {
        if ((i.en & 1) && o.depth_target) {
            Line("out.depth = " + comp(0, "0.0f") + ";");
        }
        return;
    }
    if (i.target >= 8) {
        return;
    }
    const u32 fmt = (o.col_format >> (4 * i.target)) & 0xF;
    if (fmt == 0) {
        return;
    }
    const OutType type = o.mrt_type[i.target];
    const std::string dst = "out.c" + N(i.target);
    // DC_PS_REG=<ps address hex>:<vgpr>: that shader exports vN..vN+2 instead (debugging)
    if (static const char* reg = std::getenv("DC_PS_REG"); reg && type == OutType::Float && i.target == 0) {
        char* e = nullptr;
        const u64 at = std::strtoull(reg, &e, 16);
        if (at == p.address && *e == ':') {
            const u32 n = std::strtoul(e + 1, nullptr, 10);
            Line(dst + " = float4(as_type<float>(" + VName(n) + "), as_type<float>(" + VName(n + 1) +
                 "), as_type<float>(" + VName(n + 2) + "), 1.0f);");
            return;
        }
    }
    if (i.compr) {
        const std::string a = VName(i.vsrc[0]), b = VName(i.vsrc[1]);
        std::string value;
        switch (fmt) {
        case 5: value = "float4(unpack_unorm2x16_to_float(" + a + "), unpack_unorm2x16_to_float(" + b + "))"; break;
        case 6: value = "float4(unpack_snorm2x16_to_float(" + a + "), unpack_snorm2x16_to_float(" + b + "))"; break;
        case 7: value = "uint4(" + a + " & 0xffffu, " + a + " >> 16, " + b + " & 0xffffu, " + b + " >> 16)"; break;
        case 8:
            value = "int4(int(" + a + " << 16) >> 16, int(" + a + ") >> 16, int(" + b + " << 16) >> 16, int(" + b + ") >> 16)";
            break;
        default:
            value = "float4(float2(as_type<half2>(" + a + ")), float2(as_type<half2>(" + b + ")))";
            break;
        }
        if (type == OutType::Float) {
            Line(dst + (NanProbe() ? " = dc_probe(float4(" + value + "));" : " = float4(" + value + ");"));
            ClampUfloat(i.target, dst);
            ProbeNanExport(dst);
        }
        else if (type == OutType::Uint) Line(dst + " = uint4(" + value + ");");
        else Line(dst + " = int4(" + value + ");");
        return;
    }
    if (type == OutType::Float) {
        const std::string v4 = "float4(" + comp(0, "0.0f") + ", " + comp(1, "0.0f") + ", " + comp(2, "0.0f") + ", " +
                               comp(3, "1.0f") + ")";
        Line(dst + " = " + (NanProbe() ? "dc_probe(" + v4 + ")" : v4) + ";");
        ClampUfloat(i.target, dst);
        ProbeNanExport(dst);
    } else {
        const char* t = type == OutType::Uint ? "uint4" : "int4";
        auto raw = [&](u32 k) { return (i.en & (1u << k)) ? VName(i.vsrc[k]) : std::string("0u"); };
        Line(dst + " = " + t + "(" + std::string(t) + "(" + raw(0) + ", " + raw(1) + ", " + raw(2) + ", " + raw(3) + "));");
    }
}

std::string Emitter::TexParams(bool entry) const {
    std::string s;
    for (size_t k = 0; k < p.images.size(); ++k) {
        const TextureType tt = k < o.textures.size() ? o.textures[k] : TextureType{};
        const char* comp = tt.sample == TexSample::Float ? "float" : tt.sample == TexSample::Uint ? "uint" : "int";
        const char* kind = tt.native_depth ? (tt.dim == TexDim::Tex2D ? "depth2d" : "depth2d_array")
            : tt.dim == TexDim::Tex2D ? "texture2d" : tt.dim == TexDim::Tex2DArray ? "texture2d_array" : "texture3d";
        // Images the shader writes are bound write-only, or read_write when it also
        // loads them (Metal allows read_write only for a few formats).
        const std::string access = !p.images[k].storage ? ""
                                 : p.images[k].reads ? ", access::read_write" : ", access::write";
        s += std::string(", ") + kind + "<" + comp + access + "> t" + N(k);
        if (entry) s += " [[texture(" + N(k) + ")]]";
    }
    const u32 num_samplers = std::min<u32>(static_cast<u32>(p.samplers.size()), 16u);
    for (u32 u = 0; u < num_samplers; ++u) {
        s += ", sampler smp" + N(u);
        if (entry) s += " [[sampler(" + N(u) + ")]]";
    }
    return s;
}

std::string Emitter::TexArgs() const {
    std::string s;
    for (size_t k = 0; k < p.images.size(); ++k) {
        s += ", t" + N(k);
    }
    const u32 num_samplers = std::min<u32>(static_cast<u32>(p.samplers.size()), 16u);
    for (u32 u = 0; u < num_samplers; ++u) {
        s += ", smp" + N(u);
    }
    return s;
}

// Branchy code can always run as a `switch (pc_)` dispatch loop, but GPU
// compilers take seconds on that shape. GCN code from the shader compiler is
// structured, so instead each basic block becomes `if (tgt_ <= index) {..}`:
// a forward branch sets tgt_ to its target and the blocks in between are
// skipped; a backward branch becomes `continue` of a `while (true)` around
// its loop. That needs the loops to nest and every back edge to target its
// innermost loop; otherwise the dispatch loop is used.
bool Emitter::Structure() {
    if (!p.has_branches) {
        return false;
    }
    for (u32 k = 0; k < p.code.size(); ++k) {
        index_of[p.code[k].pc] = k;
    }
    const Inst& last = p.code.back();
    index_of.emplace(last.pc + last.size * 4u, static_cast<u32>(p.code.size()));
    for (u32 k = 0; k < p.code.size(); ++k) {
        const Inst& i = p.code[k];
        if (!IsBranch(i)) {
            continue;
        }
        const auto it = index_of.find(BranchTarget(i));
        if (it == index_of.end()) {
            return false;
        }
        if (it->second <= k) {
            u32& end = loop_end[it->second];
            end = std::max(end, k);
        }
    }
    // Loops [h, e] must nest, and a back edge must target its innermost loop.
    for (const auto& [h1, e1] : loop_end) {
        for (const auto& [h2, e2] : loop_end) {
            if (h1 < h2 && h2 <= e1 && e1 < e2) {
                return false;
            }
        }
    }
    for (u32 k = 0; k < p.code.size(); ++k) {
        const Inst& i = p.code[k];
        if (!IsBranch(i)) {
            continue;
        }
        const u32 h = index_of.at(BranchTarget(i));
        if (h > k) {
            continue;
        }
        for (const auto& [h2, e2] : loop_end) {
            if (h < h2 && h2 <= k && k <= e2) {
                return false; // continue would hit the inner loop
            }
        }
    }
    return true;
}

void Emitter::EmitStructured() {
    std::set<u32> starts; // first instruction of every basic block
    for (u32 k = 0; k < p.code.size(); ++k) {
        const Inst& i = p.code[k];
        if (IsBranch(i)) {
            starts.insert(index_of.at(BranchTarget(i)));
            starts.insert(k + 1);
        }
    }
    bool open = false;
    auto close = [&] {
        if (open) {
            body += "    }\n";
            open = false;
        }
    };
    for (cur = 0; cur < p.code.size(); ++cur) {
        if (loop_end.count(cur)) {
            close();
            body += "    while (true) {\n";
        }
        if (cur == 0 || starts.count(cur)) {
            close();
            body += "    if (tgt_ <= " + N(cur) + "u) {\n";
            open = true;
        }
        Emit(p.code[cur]);
        for (const auto& [h, e] : loop_end) {
            if (e == cur) {
                close();
                body += "    break;\n    }\n";
            }
        }
    }
    close();
    End();
}

void Emitter::EmitSwitch() {
    size_t label = 0;
    for (const Inst& i : p.code) {
        while (p.has_branches && label < p.labels.size() && p.labels[label] <= i.pc) {
            if (p.labels[label] == i.pc && i.pc != 0) {
                body += "    case " + Hex(i.pc) + ":\n";
            }
            ++label;
        }
        Emit(i);
    }
    End();
}

std::string Emitter::Run() {
    // Body first: it records which registers and inputs are used.
    structured = Structure();
    if (structured) {
        EmitStructured();
    } else {
        EmitSwitch();
    }

    if (uses_movrel) {
        std::string rd, wr;
        for (u32 k = 0; k < max_vgpr; ++k) {
            rd += "case " + N(k) + "u: t_ = v" + N(k) + "; break; ";
            wr += "case " + N(k) + "u: v" + N(k) + " = t_; break; ";
        }
        for (size_t at; (at = body.find("/*DC_REL_READ*/")) != std::string::npos;) {
            body.replace(at, std::strlen("/*DC_REL_READ*/"), rd);
        }
        for (size_t at; (at = body.find("/*DC_REL_WRITE*/")) != std::string::npos;) {
            body.replace(at, std::strlen("/*DC_REL_WRITE*/"), wr);
        }
    }

    std::string decl;
    auto dl = [&](const std::string& s) { decl += "    " + s + "\n"; };
    const u32 nud = std::min<u32>(p.num_user_sgprs, 16);
    std::vector<std::string> sinit(std::max<u32>(max_sgpr, nud + 4), "0u");
    for (u32 k = 0; k < nud; ++k) {
        sinit[k] = "ud[" + N(k) + "]";
    }
    if (p.stage == Stage::Compute) {
        // COMPUTE_PGM_RSRC2: TGID_X/Y/Z_EN and TG_SIZE_EN append system SGPRs.
        u32 k = nud;
        static const char* ids[3] = {"dc_gid.x", "dc_gid.y", "dc_gid.z"};
        for (u32 c = 0; c < 3; ++c) {
            if (p.compute_rsrc2 & (1u << (7 + c))) {
                sinit[k++] = ids[c];
            }
        }
        if (p.compute_rsrc2 & (1u << 10)) {
            sinit[k++] = "0u"; // TG_SIZE: wave index within the group
        }
    }
    for (u32 k = 0; k < std::max(max_sgpr, nud); ++k) {
        dl("uint s" + N(k) + " = " + sinit[k] + ";");
    }
    dl("uint vcc_lo = 0u, vcc_hi = 0u, exec_lo = 0xffffffffu, exec_hi = 0xffffffffu, m0r = 0u, dc_null = 0u;");
    dl("bool scc = false;");
    std::vector<std::string> vinit(max_vgpr, "0u");
    if (o.tess_stage == TessStage::Local) {
        if (!vinit.empty()) vinit[0] = "dc_tp.base_vertex + dc_vertex";
        // One control point per patch: the relative patch/vertex ID is zero
        // within its isolated LDS region. No instancing in this path yet.
    } else if (o.tess_stage == TessStage::Hull) {
        if (!vinit.empty()) vinit[0] = "dc_patch";
    } else if (o.tess_stage == TessStage::Domain) {
        if (!vinit.empty()) vinit[0] = "as_type<uint>(dc_uv.x)";
        if (vinit.size() > 1) vinit[1] = "as_type<uint>(dc_uv.y)";
        if (vinit.size() > 3) vinit[3] = "dc_patch";
    } else if (p.stage == Stage::Vertex) {
        if (!vinit.empty()) vinit[0] = "vid";
        for (u32 k = 1; k <= 3 && k < vinit.size(); ++k) {
            if (k <= p.vgpr_comp_cnt) vinit[k] = "iid";
        }
    } else if (p.stage == Stage::Compute) {
        static const char* ids[3] = {"dc_tid.x", "dc_tid.y", "dc_tid.z"};
        for (u32 k = 0; k < 3 && k < vinit.size(); ++k) {
            if (k <= p.vgpr_comp_cnt) vinit[k] = ids[k];
        }
    } else if (p.stage == Stage::Fragment) {
        // System inputs follow the enabled barycentrics in SPI_PS_INPUT_ADDR order.
        static const u32 widths[16] = {2, 2, 2, 3, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        u32 reg = 0;
        for (u32 bit = 0; bit < 16; ++bit) {
            if (!(o.ps_input_addr & (1u << bit))) continue;
            std::string val;
            switch (bit) {
            case 8: val = "as_type<uint>(pin.pos.x)"; break;
            case 9: val = "as_type<uint>(pin.pos.y)"; break;
            case 10: val = "as_type<uint>(pin.pos.z)"; break;
            case 11: val = "as_type<uint>(1.0f / pin.pos.w)"; break; // GCN gives clip W, Metal 1/W
            case 12: val = "as_type<uint>(pin.ff ? 1.0f : -1.0f)"; break;
            default: break;
            }
            if (!val.empty() && reg < vinit.size()) vinit[reg] = val;
            reg += widths[bit];
        }
    }
    for (u32 k = 0; k < max_vgpr; ++k) {
        dl("uint v" + N(k) + " = " + vinit[k] + ";");
    }

    // Register probe: the marker left in front of the probed instruction.
    const bool probe = o.probe_pc != ~0u && p.stage != Stage::Vertex && body.find("/*DC_PROBE*/") != std::string::npos;
    if (probe) {
        // x == ~0: the first invocation to get there (any pixel / lane).
        // x == ~0: the first invocation to get there (any pixel / lane); x == ~1: the
        // first one whose v[probe_y] is NaN or infinite.
        const bool any = o.probe_x == ~0u || o.probe_x == ~1u;
        const std::string first = "atomic_exchange_explicit((device atomic_uint*)&dbg[7], 1u, memory_order_relaxed) == 0u";
        std::string dump = o.probe_x == ~1u ? "    if (!isfinite(as_type<float>(v" + N(o.probe_y) + ")) && " + first + ") {"
                         : any ? "    if (" + first + ") {"
                         : p.stage == Stage::Fragment
                             ? "    if (uint(pin.pos.x) == " + N(o.probe_x) + "u && uint(pin.pos.y) == " + N(o.probe_y) + "u) {"
                             : "    if (dc_gtid.x == " + N(o.probe_x) + "u && dc_gtid.y == " + N(o.probe_y) + "u) {";
        dump += " dbg[0] = 1u; dbg[1] = exec_lo; dbg[2] = vcc_lo; dbg[3] = m0r;";
        dump += p.stage == Stage::Fragment ? " dbg[4] = uint(pin.pos.x); dbg[5] = uint(pin.pos.y);"
                                           : " dbg[4] = dc_gtid.x; dbg[5] = dc_gtid.y;";
        for (u32 k = 0; k < std::min<u32>(max_vgpr, 96); ++k) {
            dump += " dbg[" + N(8 + k) + "] = v" + N(k) + ";";
        }
        for (u32 k = 0; k < std::min<u32>(std::max(max_sgpr, nud), 104); ++k) {
            dump += " dbg[" + N(104 + k) + "] = s" + N(k) + ";";
        }
        dump += " }\n";
        for (size_t at; (at = body.find("    /*DC_PROBE*/\n")) != std::string::npos;) {
            body.replace(at, std::strlen("    /*DC_PROBE*/\n"), dump);
        }
    }
    const std::string probe_param = probe ? ", device uint* dbg [[buffer(" + N(kProbeBinding) + ")]]" : "";

    std::string wrapped;
    if (structured) {
        wrapped = "    uint tgt_ = 0u;\n" + body;
    } else if (p.has_branches) {
        wrapped = "    uint pc_ = 0u;\n    while (true) {\n    switch (pc_) {\n    case 0u:\n" + body +
                  "    default:\n        " + (p.stage == Stage::Fragment && !opaque_exports ? "if (!dc_exported) discard_fragment(); " : "") +
                  (ComputeLike() ? "return;" : "return out;") + "\n    }\n    }\n";
    } else {
        wrapped = body;
    }

    std::string src = readonly_guest_memory ? kMslReadOnlyPrelude : kMslPrelude;
    if (o.native_tess_layout) src += Gpu::Metal::NativeTessellationMsl();
    if(native_vertex_fetch) src += Gpu::Metal::NativeVertexFetch::Msl(readonly_guest_memory);
    src += "\n";
    const std::string mem_params =
        "constant uint* ud [[buffer(0)]], " + GuestPointer() + " gm0 [[buffer(1)]], " + GuestPointer() + " gm1 [[buffer(2)]], " +
        "device const DcMap& gmap [[buffer(3)]]";
    if (TessPrep()) {
        src += "kernel void main0(uint dc_patch [[thread_position_in_grid]], " + mem_params +
            ", device uint* dc_lds [[buffer(8)]], device half* dc_factors [[buffer(9)]], "
            "constant DcTess& dc_tp [[buffer(10)]]" + TexParams(true) + ") {\n";
        src += "    if (dc_patch >= dc_tp.patch_count) return;\n"
            "    device uint* lds = dc_lds + dc_patch * " + N(kTessLdsWords) + "u;\n"
            "    device half* dc_tf = dc_factors + dc_patch * 6u;\n"
            "    DcMem mem = {gm0, gm1, &gmap};\n";
        if (o.tess_stage == TessStage::Local) {
            src += "    uint dc_vertex = dc_patch;\n"
                "    if (dc_tp.index_size != 0u) {\n"
                "        ulong at_ = dc_tp.index_base + ulong(dc_patch) * dc_tp.index_size;\n"
                "        dc_vertex = dc_ld(mem, at_);\n"
                "        if (dc_tp.index_size == 2u) dc_vertex &= 0xffffu;\n"
                "    }\n";
        }
        src += decl + wrapped + "}\n";
    } else if (p.stage == Stage::Vertex) {
        src += "struct VsOut {\n    float4 pos [[position, invariant]];\n";
        if (o.points) {
            src += "    float psize [[point_size]];\n";
        }
        for (u32 k = 0; k < 32; ++k) {
            if (p.param_mask & (1u << k)) {
                src += "    float4 p" + N(k) + " [[user(p" + N(k) + ")]];\n";
            }
        }
        src += "};\n\n";
        const bool domain = o.tess_stage == TessStage::Domain;
        const std::string tess_params = !domain ? "" : o.native_tess_layout
            ? ", float2 dc_uv, uint dc_patch, thread const DcNativeTess& lds"
            : ", float2 dc_uv, uint dc_patch, device uint* lds";
        src += "static VsOut dc_run(uint vid, uint iid, constant uint* ud, thread const DcMem& mem" + tess_params + TexParams(false) +
               ") {\n    VsOut out = {};\n" + (o.points ? "    out.psize = 1.0f;\n" : "") + decl + wrapped + "}\n\n";
        const std::string args = "ud, mem" + TexArgs();
        src += domain ? "[[patch(quad, 1)]] vertex VsOut main0(uint dc_patch [[patch_id]], float2 dc_uv [[position_in_patch]], "
            : "vertex VsOut main0(uint vid [[vertex_id]], uint iid [[instance_id]], ";
        src += mem_params +
               ", constant DcDraw& draw [[buffer(4)]]" + (o.debug_capture ? ", device float4* dbg [[buffer(6)]]" : "") +
               (domain ? (o.native_tess_layout ? ", constant uint* dc_lud [[buffer(11)]], constant uint* dc_hud [[buffer(12)]], constant DcTess& dc_tp [[buffer(10)]]"
                                              : ", device uint* dc_lds [[buffer(8)]]") : "") +
               TexParams(true) + ") {\n";
        src += "    DcMem mem = {gm0, gm1, &gmap};\n    VsOut o;\n";
        if (domain) {
            if (o.native_tess_layout) {
                src += "    DcNativeTess dc_local;\n    dc_native_tess_init(dc_local, mem, dc_lud, dc_hud, dc_tp, dc_patch, " +
                    N(o.native_tess_layout) + "u, " + N(o.native_tess_factor) + "u);\n";
                src += "    o = dc_run(0u, 0u, ud, mem, dc_uv, dc_patch, dc_local" + TexArgs() + ");\n";
            } else src += "    o = dc_run(0u, 0u, ud, mem, dc_uv, dc_patch, dc_lds + dc_patch * " +
                N(kTessLdsWords) + "u" + TexArgs() + ");\n";
        } else if (o.rect_list) {
            // As shadPS4's rect list TCS: the vertex whose x matches one neighbour and y
            // the other is the right-angle corner; the 4th vertex is opposite it. Triangles:
            // the given one, then the two others with the 4th, wound the same way (culling).
            src += "    uint r_ = vid / 6u, c_ = vid % 6u;\n";
            src += "    if (c_ < 3u) {\n        o = dc_run(r_ * 3u + c_, iid, " + args + ");\n    } else {\n";
            src += "        VsOut v_[3] = {dc_run(r_ * 3u, iid, " + args + "), dc_run(r_ * 3u + 1u, iid, " + args +
                   "), dc_run(r_ * 3u + 2u, iid, " + args + ")};\n";
            src += "        bool2 e_[3];\n";
            src += "        for (uint i = 0u; i < 3u; ++i) e_[i] = v_[i].pos.xy == v_[(i + 1u) % 3u].pos.xy;\n";
            src += "        float b_[3]; uint ci_ = 0u;\n";
            src += "        for (uint i = 0u; i < 3u; ++i) {\n";
            src += "            bool edge = (e_[i].x && e_[(i + 2u) % 3u].y) || (e_[i].y && e_[(i + 2u) % 3u].x);\n";
            src += "            b_[i] = edge ? -1.0f : 1.0f; if (edge) ci_ = i;\n        }\n";
            src += "        if (min(b_[0], min(b_[1], b_[2])) > 0.0f) b_[0] = -1.0f; // no right angle: v0\n";
            src += "        if (c_ < 5u) {\n            o = v_[(ci_ + 5u - c_) % 3u];\n        } else {\n";
            src += "            o.pos = v_[0].pos * b_[0] + v_[1].pos * b_[1] + v_[2].pos * b_[2];\n";
            for (u32 k = 0; k < 32; ++k) {
                if (p.param_mask & (1u << k)) {
                    src += "            o.p" + N(k) + " = v_[0].p" + N(k) + " * b_[0] + v_[1].p" + N(k) + " * b_[1] + v_[2].p" +
                           N(k) + " * b_[2];\n";
                }
            }
            src += "        }\n    }\n";
        } else {
            src += "    o = dc_run(vid, iid, " + args + ");\n";
        }
        if (o.debug_capture) {
            src += "    if (vid < 32u) { dbg[vid * 2u] = o.pos; }\n";
        }
        src += "    o.pos = dc_viewport(o.pos, draw);\n";
        if (o.debug_capture) {
            src += "    if (vid < 32u) { dbg[vid * 2u + 1u] = o.pos; }\n";
        }
        src += "    return o;\n}\n";
    } else if (p.stage == Stage::Fragment) {
        src += "struct PsIn {\n    float4 pos [[position]];\n    bool ff [[front_facing]];\n";
        for (u32 k : ps_params) {
            bool flat = false;
            for (u32 a = 0; a < 32; ++a) {
                const u32 cntl = o.ps_input_cntl[a];
                if ((cntl & 0x3F) == k && (p.attr_mask & (1u << a)) &&
                    ((cntl & (1u << 10)) || (p.attr_flat_mask & (1u << a)))) {
                    flat = true;
                }
            }
            std::string qualifier=flat ? ", flat" : "";
            if (!flat) if (auto it=ps_interpolation.find(k);it!=ps_interpolation.end()) {
                static constexpr const char* modes[]{", sample_perspective",", center_perspective",
                    ", centroid_perspective",", center_perspective",", sample_no_perspective",
                    ", center_no_perspective",", centroid_no_perspective"};
                qualifier=modes[it->second];
            }
            src += "    float4 p" + N(k) + " [[user(p" + N(k) + ")" + qualifier + "]];\n";
        }
        src += "};\nstruct PsOut {\n";
        for (u32 k = 0; k < 8; ++k) {
            if ((p.mrt_mask & (1u << k)) && ((o.col_format >> (4 * k)) & 0xF)) {
                const char* t = o.mrt_type[k] == OutType::Float ? "float4" : o.mrt_type[k] == OutType::Uint ? "uint4" : "int4";
                src += std::string("    ") + t + " c" + N(k) + " [[color(" + N(k) + ")]];\n";
            }
        }
        if (p.exports_depth && o.depth_target) {
            src += "    float depth [[depth(any)]];\n";
        }
        src += "};\n\n";
        const std::string bounds_param =
            o.depth_bounds ? ", texture2d<float> dc_zb [[texture(" + N(kDepthBoundsTexture) + ")]], constant float2& dc_zbr [[buffer(" +
                                 N(kDepthBoundsBuffer) + ")]]"
                           : "";
        const std::string smrd_param=static_smrd ? ", constant uint* dc_smrd [[buffer("+N(Binding::StaticSmrd)+")]]" :
            direct_smrd ? ", constant ulong* dc_smrd_roots [[buffer("+N(Binding::StaticSmrd)+")]]" : "";
        src += "fragment PsOut main0(PsIn pin [[stage_in]], " + mem_params + probe_param + bounds_param + smrd_param + TexParams(true) +
               ") {\n";
        if (o.depth_bounds) {
            src += "    { float z_ = dc_zb.read(uint2(pin.pos.xy)).x; float bmin = min(dc_zbr.x, dc_zbr.y); float bmax = max(dc_zbr.x, dc_zbr.y); if (z_ < bmin || z_ > bmax) discard_fragment(); }\n";
        }
        src += "    DcMem mem = {gm0, gm1, &gmap};\n    PsOut out = {};\n    bool dc_exported = false;\n";
        src += "    uint dc_qlane = (uint(pin.pos.x) & 1u) | ((uint(pin.pos.y) & 1u) << 1);\n";
        src += "    (void)mem; (void)dc_qlane;\n";
        src += decl + wrapped + "}\n";
    } else {
        src += "kernel void main0(uint3 dc_tid [[thread_position_in_threadgroup]], "
               "uint3 dc_gid [[threadgroup_position_in_grid]], uint dc_lidx [[thread_index_in_threadgroup]], "
               "uint3 dc_gtid [[thread_position_in_grid]], " +
               mem_params + ", device uint* gds [[buffer(5)]], threadgroup uint* lds [[threadgroup(0)]]" + probe_param + TexParams(true) +
               ") {\n";
        src += "    DcMem mem = {gm0, gm1, &gmap};\n    uint dc_lane = dc_lidx & 63u;\n";
        src += "    (void)mem; (void)dc_lane; (void)lds; (void)gds;\n";
        src += decl + wrapped + "}\n";
    }
    return src;
}

} // namespace

bool FragmentExportsOpaque(const Program& p) {
    return ProveOpaqueExports(p);
}

bool FragmentStaticSmrdEligible(const Program& p) {
    return ProveStaticSmrd(p);
}

bool FragmentSimdSmrdEligible(const Program& p,std::string* failure) {
    return ProveSimdSmrd(p,failure);
}

bool VertexNativeFetchEligible(const Program& p,std::string* failure) {
    return ProveNativeVertexFetch(p,failure);
}

bool GuestMemoryReadOnlyEligible(const Program& p,std::string* failure) {
    if(!ProveReadOnlyGuestCode(p,failure)) return false;
    // BuildProgram reflects resources; translation support is reported by the
    // emitter. Check the original generator once before caching eligibility.
    const MslOptions options;
    Emitter original(p,options);
    original.Run();
    if(!original.unsupported.empty()) {
        if(failure)*failure="Unsupported translated instruction";
        return false;
    }
    return true;
}

std::string GenerateMsl(const Program& p, const MslOptions& o, std::vector<std::string>* unsupported) {
    Emitter e(p, o);
    std::string src = e.Run();
    if(o.readonly_guest_memory && !e.unsupported.empty()) {
        auto fallback=o;fallback.readonly_guest_memory=false;
        return GenerateMsl(p,fallback,unsupported);
    }
    if (unsupported) {
        unsupported->assign(e.unsupported.begin(), e.unsupported.end());
    }
    return src;
}

} // namespace Gpu::Shader
