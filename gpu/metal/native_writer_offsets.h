// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gpu/metal/native_writer_proof.h"
#include <array>
#include <functional>
#include <unordered_set>

namespace Gpu::Metal::NativeWriter {
// A maximum for each complete uint byte offset (soffset + immediate + voff).
// UINT32_MAX means unproved. No resource values are baked into this code proof.
struct OffsetProof {
    bool accepted=false;
    const char* reason="unproved";
    std::vector<u32> maximum;
    std::size_t queries=0,visits=0;
    explicit operator bool() const {return accepted;}
};

namespace Detail {
inline bool VectorWrites(const Shader::Gcn::Inst& i,std::bitset<256>& written) {
    using namespace Shader::Gcn;
    const char* name=OpName(i.enc,i.op);
    if(!name)return false;
    const auto write=[&](u32 first,u32 count) {
        if(first>=256 || count>256-first)return false;
        for(u32 k=0;k<count;++k)written.set(first+k);
        return true;
    };
    switch(i.enc) {
    case Enc::VOP1:case Enc::VOP2:case Enc::VOP3:case Enc::VOPC:
        if(i.op<256)return true;
        switch(static_cast<Vop>(i.op)) {
        case Vop::V_MOVRELD_B32:case Vop::V_MOVRELS_B32:case Vop::V_MOVRELSD_B32:return false;
        case Vop::V_READLANE_B32:case Vop::V_READFIRSTLANE_B32:
        case Vop::V_NOP:case Vop::V_CLREXCP:return true;
        default:
            // Overkill a second word for 64-bit conversions; never underkill
            // a 64-bit value result. Rare packed multiword ops stay outside.
            if(i.op==u16(Vop::V_QSAD_PK_U16_U8) || i.op==u16(Vop::V_MQSAD_PK_U16_U8) ||
                i.op==u16(Vop::V_MQSAD_U32_U8))return false;
            return write(i.dst,(std::strstr(name,"64") ? 2 : 1));
        }
    case Enc::VINTRP:return write(i.dst,1);
    case Enc::DS:
        switch(static_cast<Ds>(i.op)) {
        case Ds::DS_READ2_B32:case Ds::DS_READ2ST64_B32:case Ds::DS_READ_B64:return write(i.dst,2);
        case Ds::DS_READ_B96:return write(i.dst,3);
        case Ds::DS_READ_B128:case Ds::DS_READ2_B64:return write(i.dst,4);
        case Ds::DS_ADD_RTN_U32:case Ds::DS_SWIZZLE_B32:case Ds::DS_READ_B32:
        case Ds::DS_READ_I8:case Ds::DS_READ_U8:case Ds::DS_READ_I16:case Ds::DS_READ_U16:
        case Ds::DS_APPEND:case Ds::DS_CONSUME:return write(i.dst,1);
        case Ds::DS_ADD_U32:case Ds::DS_SUB_U32:case Ds::DS_MIN_I32:case Ds::DS_MAX_I32:
        case Ds::DS_MIN_U32:case Ds::DS_MAX_U32:case Ds::DS_AND_B32:case Ds::DS_OR_B32:case Ds::DS_XOR_B32:
        case Ds::DS_WRITE_B32:case Ds::DS_WRITE2_B32:case Ds::DS_WRITE2ST64_B32:
        case Ds::DS_WRITE_B8:case Ds::DS_WRITE_B16:case Ds::DS_WRITE_B64:
        case Ds::DS_WRITE2_B64:case Ds::DS_WRITE_B96:case Ds::DS_WRITE_B128:return true;
        default:return false;
        }
    case Enc::MTBUF:return i.op<=3 ? write(i.vdata,i.op+1) : true;
    case Enc::MUBUF:
        if(i.op<=3)return write(i.vdata,i.op+1);
        if(i.op>=8 && i.op<=u16(Mubuf::BUFFER_LOAD_DWORD))return write(i.vdata,1);
        if(i.op==u16(Mubuf::BUFFER_LOAD_DWORDX2))return write(i.vdata,2);
        if(i.op==u16(Mubuf::BUFFER_LOAD_DWORDX3))return write(i.vdata,3);
        if(i.op==u16(Mubuf::BUFFER_LOAD_DWORDX4))return write(i.vdata,4);
        if(i.op>=u16(Mubuf::BUFFER_ATOMIC_SWAP) && i.op<=u16(Mubuf::BUFFER_ATOMIC_DEC))
            return !i.glc || write(i.vdata,1);
        return IsBufferWriter(i) || i.op==u16(Mubuf::BUFFER_WBINVL1) || i.op==u16(Mubuf::BUFFER_WBINVL1_SC);
    case Enc::MIMG:
        return i.op==u16(Mimg::IMAGE_STORE) || i.op==u16(Mimg::IMAGE_STORE_MIP) || write(i.vdata,4);
    case Enc::SOP1:case Enc::SOP2:case Enc::SOPK:case Enc::SOPC:case Enc::SOPP:case Enc::SMRD:
    case Enc::EXP:return true;
    default:return false;
    }
}
inline bool ChangesExec(const Shader::Gcn::Inst& i,const std::bitset<128>& writes) {
    using namespace Shader::Gcn;
    return writes.test(Operand::ExecLo) || writes.test(Operand::ExecHi) ||
        (i.enc==Enc::SOP1 && i.op>=u16(Sop1::S_AND_SAVEEXEC_B64) && i.op<=u16(Sop1::S_XNOR_SAVEEXEC_B64)) ||
        ((i.enc==Enc::VOPC || i.enc==Enc::VOP3) && i.op<256 && (i.op&0x10));
}
inline bool LiteralValue(const Shader::Gcn::Inst& i,u16 operand,u32& value) {
    using namespace Shader::Gcn;
    if(operand>=Operand::Zero && operand<=Operand::IntPosMax){value=operand-Operand::Zero;return true;}
    if(operand>Operand::IntPosMax && operand<=Operand::IntNegMax){value=u32(-s32(operand-Operand::IntPosMax));return true;}
    if(operand==Operand::Literal){value=i.literal;return true;}
    return false;
}
}

// Only ordinary Compute initialization is supported. The caller supplies the
// exact dispatch threadgroup dimensions, not group counts or guessed limits.
// This helper is additional to Prove(): descriptor reaching definitions and
// runtime descriptor-read/alias/lifetime checks remain independently required.
inline OffsetProof ProveOffsets(const Shader::Program& program,std::array<u32,3> threads,
    Limits requested={}) {
    using namespace Shader;using namespace Shader::Gcn;using namespace Detail;
    OffsetProof result;
    const auto fail=[&](const char* reason){result.reason=reason;return result;};
    const Limits hard;
    const auto instruction_limit=std::min(requested.instructions,hard.instructions);
    const auto query_limit=std::min(requested.queries,hard.queries);
    const auto visit_limit=std::min(requested.visits,hard.visits);
    const auto n=program.code.size();
    if(program.stage!=Stage::Compute || program.vgpr_comp_cnt>2 ||
        !threads[0] || !threads[1] || !threads[2] ||
        threads[0]>65535 || threads[1]>65535 || threads[2]>65535)return fail("offset-context");
    if(!n || n>instruction_limit || program.fetch_address || program.fetch_ud>=0 || program.fetch_bytes ||
        !program.unsupported.empty() || program.code.back().enc!=Enc::SOPP ||
        program.code.back().op!=u16(Sopp::S_ENDPGM))return fail("offset-program");
    std::vector<std::bitset<256>> writes(n);
    std::vector<u8> changes_exec(n),reachable(n);
    std::vector<std::vector<u32>> predecessors(n),successors(n);
    std::unordered_map<u32,u32> by_pc;u64 next_pc=0;
    for(u32 k=0;k<n;++k) {
        const auto& i=program.code[k];std::bitset<128> scalar;
        if(i.pc!=next_pc || !i.size || i.size>4 || i.pc>=kFetchPcBase ||
            !ScalarWrites(i,scalar) || !VectorWrites(i,writes[k]))return fail("offset-instruction");
        next_pc+=u64(i.size)*4;
        if(next_pc>65536 || !by_pc.emplace(i.pc,k).second)return fail("offset-code-bound");
        changes_exec[k]=ChangesExec(i,scalar);
    }
    for(u32 k=0;k<n;++k) {
        const auto& i=program.code[k];
        if(IsBranch(i)) {
            const s64 target=s64(i.pc)+4+s64(i.simm16)*4;
            if(target<0 || target>UINT32_MAX || !by_pc.count(u32(target)))return fail("offset-branch");
            successors[k].push_back(by_pc.at(u32(target)));
        }
        const bool terminal=i.enc==Enc::SOPP && (i.op==u16(Sopp::S_ENDPGM) || i.op==u16(Sopp::S_BRANCH));
        if(!terminal){if(k+1>=n)return fail("offset-termination");successors[k].push_back(k+1);}
        for(const auto next:successors[k])predecessors[next].push_back(k);
    }
    std::vector<u32> pending{0};reachable[0]=1;
    while(!pending.empty()) {
        const auto at=pending.back();pending.pop_back();
        for(const auto next:successors[at])if(!reachable[next]){reachable[next]=1;pending.push_back(next);}
    }
    std::unordered_map<u64,u32> cache;std::unordered_set<u64> evaluating;
    bool exhausted=false;u32 depth=0;
    std::function<u32(u32,u16)> bound;
    const auto entry=[&](u16 reg){return reg<3 && reg<=program.vgpr_comp_cnt ? threads[reg]-1 : 0u;};
    const auto expression=[&](u32 at,u16 reg) ->u32 {
        const auto& i=program.code[at];
        if(i.dst!=reg || (i.enc!=Enc::VOP1 && i.enc!=Enc::VOP2 && i.enc!=Enc::VOP3) ||
            i.abs || i.neg || i.omod || i.clamp)return UINT32_MAX;
        const auto operand=[&](unsigned which) {
            u32 value=0;
            if(LiteralValue(i,i.src[which],value))return value;
            return i.src[which]>=Operand::Vgpr0 ? bound(at,i.src[which]-Operand::Vgpr0) : UINT32_MAX;
        };
        switch(static_cast<Vop>(i.op)) {
        case Vop::V_MOV_B32:return operand(0);
        case Vop::V_AND_B32:return std::min(operand(0),operand(1));
        case Vop::V_LSHL_B32:case Vop::V_LSHLREV_B32: {
            const bool reverse=i.op==u16(Vop::V_LSHLREV_B32);u32 shift=0;
            if(!LiteralValue(i,i.src[reverse ? 0 : 1],shift))return UINT32_MAX;
            shift&=31;const u32 value=operand(reverse ? 1 : 0);
            return value<=(UINT32_MAX>>shift) ? value<<shift : UINT32_MAX;
        }
        case Vop::V_ADD_I32: {
            const u64 sum=u64(operand(0))+operand(1);
            return sum<=UINT32_MAX ? u32(sum) : UINT32_MAX;
        }
        default:return UINT32_MAX;
        }
    };
    bound=[&](u32 before,u16 reg)->u32 {
        if(reg>=256 || before>=n || !reachable[before])return UINT32_MAX;
        const u64 key=(u64(before)<<8)|reg;
        if(const auto found=cache.find(key);found!=cache.end())return found->second;
        if(++result.queries>query_limit || depth>=64 || !evaluating.insert(key).second) {
            exhausted=true;return UINT32_MAX;
        }
        ++depth;u32 maximum=0;
        // A query assumes this lane is active at 'before'. Each reverse path
        // tracks whether EXEC changed. A definition before any such change
        // strongly replaces the value; after a change its inactive old value
        // also reaches the query, so continue through its predecessors.
        std::vector<u8> seen(n*2);std::vector<std::pair<u32,bool>> work;
        const auto push=[&](u32 at,bool changed) {
            const auto slot=at*2+unsigned(changed);
            if(reachable[at] && !seen[slot]){seen[slot]=1;work.emplace_back(at,changed);}
        };
        if(before==0)maximum=entry(reg);
        for(const auto previous:predecessors[before])push(previous,false);
        while(!work.empty() && maximum!=UINT32_MAX && !exhausted) {
            if(++result.visits>visit_limit){exhausted=true;break;}
            const auto [at,prior_changed]=work.back();work.pop_back();
            const bool changed=prior_changed || changes_exec[at];
            if(writes[at].test(reg)) {
                maximum=std::max(maximum,expression(at,reg));
                if(!changed)continue;
            }
            if(at==0)maximum=std::max(maximum,entry(reg));
            for(const auto previous:predecessors[at])push(previous,changed);
        }
        --depth;evaluating.erase(key);
        if(exhausted)return UINT32_MAX;
        cache.emplace(key,maximum);return maximum;
    };
    std::size_t stores=0;
    for(u32 k=0;k<n;++k)if(IsBufferWriter(program.code[k])) {
        const auto& i=program.code[k];
        if(++stores>std::min(requested.stores,hard.stores))return fail("offset-store-bound");
        u32 maximum=UINT32_MAX;
        if(reachable[k] && !i.addr64 && i.soffset==Operand::Zero) {
            const u16 reg=u16(i.vaddr)+(i.idxen ? 1 : 0);
            const u64 value=u64(i.offen ? bound(k,reg) : 0u)+i.offset;
            if(value<=UINT32_MAX)maximum=u32(value);
        }
        result.maximum.push_back(maximum);
    }
    if(exhausted)return fail("offset-query-cycle-or-bound");
    if(stores!=program.buffer_writes.size())return fail("offset-reflection");
    result.accepted=true;result.reason="proven-offsets";return result;
}
} // namespace Gpu::Metal::NativeWriter
