// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "gpu/shader/program.h"
#include <algorithm>
#include <bitset>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace Gpu::Metal::NativeWriter {
// This is a bounded reaching-definition check, not a value optimizer. It
// validates the existing reflection against every entry-reachable path.
struct Limits {
    std::size_t instructions=8192,stores=256,queries=1024,visits=1u<<20;
};
struct Proof {
    bool accepted=false;
    const char* reason="unproved";
    std::size_t instructions=0,stores=0,queries=0,visits=0;
    explicit operator bool() const {return accepted;}
};

inline bool IsBufferWriter(const Shader::Gcn::Inst& instruction) {
    using namespace Shader::Gcn;
    const bool typed=instruction.enc==Enc::MTBUF;
    if(!typed && instruction.enc!=Enc::MUBUF)return false;
    const auto op=instruction.op;
    return (op>=4 && op<=7) || (!typed &&
        (op==u16(Mubuf::BUFFER_STORE_BYTE) || op==u16(Mubuf::BUFFER_STORE_SHORT) ||
         (op>=u16(Mubuf::BUFFER_STORE_DWORD) && op<=u16(Mubuf::BUFFER_STORE_DWORDX3)) ||
         (op>=u16(Mubuf::BUFFER_ATOMIC_SWAP) && op<=u16(Mubuf::BUFFER_ATOMIC_DEC))));
}

namespace Detail {
using namespace Shader::Gcn;
// A conservative copy of the emitter's scalar-destination contract. Reject
// indirect/relative scalar operations and unknown control flow globally.
inline bool ScalarWrites(const Inst& instruction,std::bitset<128>& written) {
    const auto& i=instruction;
    const char* name=OpName(i.enc,i.op);
    if(!name)return false;
    auto write=[&](u16 destination,u32 count) {
        if(destination>=128 || count>128u-destination)return false;
        for(u32 k=0;k<count;++k)written.set(destination+k);
        return true;
    };
    const u32 width=std::strstr(name,"64") ? 2 : 1;
    switch(i.enc) {
    case Enc::SOP1:
        switch(static_cast<Sop1>(i.op)) {
        case Sop1::S_SETPC_B64:case Sop1::S_SWAPPC_B64:case Sop1::S_RFE_B64:
        case Sop1::S_MOVRELS_B32:case Sop1::S_MOVRELS_B64:
        case Sop1::S_MOVRELD_B32:case Sop1::S_MOVRELD_B64:case Sop1::S_CBRANCH_JOIN:return false;
        default:return write(i.dst,width);
        }
    case Enc::SOP2:return i.op!=u16(Sop2::S_CBRANCH_G_FORK) && write(i.dst,width);
    case Enc::SOPK:
        if(i.op==u16(Sopk::S_CBRANCH_I_FORK))return false;
        if((i.op>=u16(Sopk::S_CMPK_EQ_I32) && i.op<=u16(Sopk::S_CMPK_LE_U32)) ||
            i.op==u16(Sopk::S_SETREG_B32) || i.op==u16(Sopk::S_SETREG_IMM32_B32))return true;
        return write(i.dst,1);
    case Enc::SMRD:
        if(i.op<=4)return write(i.dst,1u<<i.op);
        if(i.op>=8 && i.op<=12)return write(i.dst,1u<<(i.op-8));
        if(i.op==u16(Smrd::S_MEMTIME))return write(i.dst,2);
        return i.op==u16(Smrd::S_DCACHE_INV) || i.op==u16(Smrd::S_DCACHE_INV_VOL);
    case Enc::VOPC:case Enc::VOP3:
        if(i.op<256)return write(i.sdst,2);
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
    case Enc::SOPC:return i.op!=u16(Sopc::S_SETVSKIP);
    case Enc::VINTRP:case Enc::DS:case Enc::MUBUF:case Enc::MTBUF:case Enc::MIMG:case Enc::EXP:return true;
    default:return false;
    }
}
struct Definition {
    enum Kind {Entry,Instruction,Ambiguous} kind=Ambiguous;
    u32 index=0;
    bool operator==(const Definition&) const=default;
};
}

inline Proof Prove(const Shader::Program& program,Limits requested={}) {
    using namespace Shader;
    using namespace Shader::Gcn;
    using namespace Detail;
    Proof result;
    const Limits hard;
    const Limits limit{std::min(requested.instructions,hard.instructions),std::min(requested.stores,hard.stores),
        std::min(requested.queries,hard.queries),std::min(requested.visits,hard.visits)};
    const auto fail=[&](const char* reason){result.reason=reason;return result;};
    const auto n=program.code.size();result.instructions=n;
    if(!n || n>limit.instructions || program.num_user_sgprs>16)return fail("instruction-bound");
    if(!program.unsupported.empty() || program.fetch_address || program.fetch_ud>=0 || program.fetch_bytes)
        return fail("unsupported-or-fetch");
    if(program.code.back().enc!=Enc::SOPP || program.code.back().op!=u16(Sopp::S_ENDPGM))
        return fail("unterminated-program");
    std::unordered_map<u32,u32> by_pc;
    std::vector<std::bitset<128>> writes(n);
    std::vector<std::vector<u32>> successors(n),predecessors(n);
    u64 next_pc=0;
    for(u32 k=0;k<n;++k) {
        const auto& i=program.code[k];
        if(i.pc!=next_pc || !i.size || i.size>4 || i.pc>=kFetchPcBase)
            return fail("noncontiguous-instructions");
        next_pc+=u64(i.size)*4;
        if(next_pc>65536 || !by_pc.emplace(i.pc,k).second)return fail("code-byte-bound");
        if(!ScalarWrites(i,writes[k]))return fail("unknown-write-or-control");
    }
    for(u32 k=0;k<n;++k) {
        const auto& i=program.code[k];
        if(IsBranch(i)) {
            const s64 target=s64(i.pc)+4+s64(i.simm16)*4;
            if(target<0 || target>std::numeric_limits<u32>::max())return fail("branch-target");
            const auto found=by_pc.find(u32(target));
            if(found==by_pc.end())return fail("branch-target");
            successors[k].push_back(found->second);
        }
        const bool terminal=i.enc==Enc::SOPP && (i.op==u16(Sopp::S_ENDPGM) || i.op==u16(Sopp::S_BRANCH));
        if(!terminal) {
            if(k+1>=n)return fail("unterminated-program");
            successors[k].push_back(k+1);
        }
        for(const auto next:successors[k])predecessors[next].push_back(k);
    }
    std::vector<u8> reachable(n);std::vector<u32> work{0};reachable[0]=1;
    while(!work.empty()) {
        const auto at=work.back();work.pop_back();
        for(const auto next:successors[at])if(!reachable[next]){reachable[next]=1;work.push_back(next);}
    }
    std::unordered_map<u64,Definition> cache;
    bool exhausted=false;
    const auto last_definition=[&](u32 before,u16 reg) {
        const u64 key=(u64(before)<<8)|reg;
        if(const auto found=cache.find(key);found!=cache.end())return found->second;
        Definition answer;bool found_definition=false;
        const auto merge=[&](Definition candidate) {
            if(!found_definition){answer=candidate;found_definition=true;}
            else if(answer!=candidate)answer={Definition::Ambiguous,0};
        };
        if(reg>Operand::SgprMax || before>=n || !reachable[before] || ++result.queries>limit.queries) {
            exhausted=true;return answer;
        }
        std::vector<u8> visited(n);std::vector<u32> pending;
        const auto push=[&](u32 at){if(reachable[at] && !visited[at]){visited[at]=1;pending.push_back(at);}};
        if(before==0)merge({Definition::Entry,0});
        for(const auto previous:predecessors[before])push(previous);
        while(!pending.empty()) {
            if(++result.visits>limit.visits){exhausted=true;return Definition{};}
            const auto at=pending.back();pending.pop_back();
            if(writes[at].test(reg))merge({Definition::Instruction,at});
            else {
                if(at==0)merge({Definition::Entry,0});
                for(const auto previous:predecessors[at])push(previous);
            }
            if(found_definition && answer.kind==Definition::Ambiguous)break;
        }
        cache.emplace(key,answer);return answer;
    };
    std::size_t writer=0;
    for(u32 k=0;k<n;++k) {
        const auto& store=program.code[k];
        if(!IsBufferWriter(store))continue;
        if(++result.stores>limit.stores || writer>=program.buffer_writes.size())return fail("store-bound-or-reflection");
        if(!reachable[k] || store.srsrc>Operand::SgprMax-3)return fail("unreachable-or-invalid-store");
        const auto& descriptor=program.buffer_writes[writer++];
        for(u32 word=0;word<4;++word) {
            const auto& source=descriptor[word];const u16 reg=store.srsrc+word;
            const auto definition=last_definition(k,reg);
            if(exhausted)return fail("query-visit-bound");
            if(source.kind==SgprSource::UserData) {
                if(source.ud!=reg || source.ud>=program.num_user_sgprs || source.offset ||
                    definition.kind!=Definition::Entry)return fail("entry-descriptor-definition");
                continue;
            }
            // Pointer loads are the proved subset. BufferLoad requires an
            // additional full source-V# contract, so remains conservative.
            if(source.kind!=SgprSource::PointerLoad || source.ud+1>=program.num_user_sgprs ||
                definition.kind!=Definition::Instruction)return fail("descriptor-definition");
            const auto& load=program.code[definition.index];
            if(load.enc!=Enc::SMRD || load.op!=u16(Smrd::S_LOAD_DWORDX4) || !load.imm ||
                load.sbase!=source.ud || load.dst!=store.srsrc || reg<load.dst || reg-load.dst>=4 ||
                u64(load.smrd_offset)*4+u64(reg-load.dst)*4!=source.offset)
                return fail("expected-pointer-load");
            for(u16 root=source.ud;root<=source.ud+1;++root) {
                if(last_definition(definition.index,root).kind!=Definition::Entry)
                    return fail(exhausted ? "query-visit-bound" : "root-clobber");
            }
        }
    }
    if(writer!=program.buffer_writes.size())return fail("store-reflection-mismatch");
    result.accepted=true;result.reason="proven";return result;
}
} // namespace Gpu::Metal::NativeWriter
