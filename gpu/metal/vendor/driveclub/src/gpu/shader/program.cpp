#include "gpu/shader/program.h"

#include <algorithm>
#include <cstring>

#include "common/log.h"
#include "core/memory.h"

namespace Gpu::Shader {

using namespace Gcn;

namespace {

bool Readable(u64 addr, u64 size) {
    Core::Memory::Vma vma;
    return addr && Core::Memory::Query(addr, false, &vma) && addr + size <= vma.start + vma.size;
}

u64 HashCode(const std::vector<Inst>& insts, const u32* base, u64 seed) {
    // FNV-1a over the instruction words.
    u64 h = seed ^ 0xcbf29ce484222325ull;
    for (const Inst& i : insts) {
        for (u32 k = 0; k < i.size; ++k) {
            h ^= base[i.pc / 4 + k];
            h *= 0x100000001b3ull;
        }
    }
    return h;
}

bool IsSgpr(u16 code) {
    return code <= Operand::SgprMax;
}

// Tracks where each scalar register's value came from.
class Provenance {
public:
    explicit Provenance(u32 num_user_sgprs) {
        for (u32 i = 0; i < num_user_sgprs && i < state.size(); ++i) {
            state[i] = {SgprSource::UserData, static_cast<u8>(i), 0};
        }
    }

    const SgprSource& Get(u32 reg) const {
        static const SgprSource unknown{};
        return reg < state.size() ? state[reg] : unknown;
    }

    void Clobber(u32 reg, u32 count = 1) {
        for (u32 k = 0; k < count; ++k) {
            if (reg + k < state.size()) {
                state[reg + k] = {};
            }
        }
    }

    void Step(const Inst& i) {
        switch (i.enc) {
        case Enc::SMRD: {
            const auto op = static_cast<Smrd>(i.op);
            u32 count = 0;
            bool buffer = false;
            switch (op) {
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
            default: return;
            }
            const SgprSource& lo = Get(i.sbase);
            const SgprSource& hi = Get(i.sbase + 1u);
            const bool base_is_ud = lo.kind == SgprSource::UserData &&
                                    hi.kind == SgprSource::UserData && hi.ud == lo.ud + 1;
            for (u32 k = 0; k < count; ++k) {
                SgprSource s{};
                if (base_is_ud && i.imm) {
                    s.kind = buffer ? SgprSource::BufferLoad : SgprSource::PointerLoad;
                    s.ud = lo.ud;
                    s.offset = i.smrd_offset * 4 + k * 4;
                }
                if (i.dst + k < state.size()) {
                    state[i.dst + k] = s;
                }
            }
            return;
        }
        case Enc::SOP1: {
            const auto op = static_cast<Sop1>(i.op);
            if (op == Sop1::S_MOV_B32 && IsSgpr(i.src[0])) {
                Set(i.dst, Get(i.src[0]));
                return;
            }
            if (op == Sop1::S_MOV_B64 && IsSgpr(i.src[0])) {
                const SgprSource a = Get(i.src[0]), b = Get(i.src[0] + 1u);
                Set(i.dst, a);
                Set(i.dst + 1u, b);
                return;
            }
            Clobber(i.dst, Width(i));
            return;
        }
        case Enc::SOP2:
            Clobber(i.dst, Width(i));
            return;
        case Enc::SOPK:
            if (i.op >= static_cast<u16>(Sopk::S_CMPK_EQ_I32) && i.op <= static_cast<u16>(Sopk::S_CMPK_LE_U32)) {
                return; // compares write SCC only
            }
            Clobber(i.dst, 1);
            return;
        case Enc::VOPC:
        case Enc::VOP2:
        case Enc::VOP1:
        case Enc::VOP3:
            if (i.op < 256 || i.vop3) {
                if (i.sdst <= Operand::SgprMax) {
                    Clobber(i.sdst, 2);
                }
            }
            if (static_cast<Vop>(i.op) == Vop::V_READFIRSTLANE_B32 ||
                static_cast<Vop>(i.op) == Vop::V_READLANE_B32) {
                Clobber(i.dst);
            }
            return;
        default:
            return;
        }
    }

private:
    // Scalar registers an SALU instruction writes: two for the 64-bit forms.
    static u32 Width(const Inst& i) {
        const char* n = OpName(i.enc, i.op);
        return n && std::strstr(n, "64") ? 2 : 1;
    }

    void Set(u32 reg, const SgprSource& s) {
        if (reg < state.size()) {
            state[reg] = s;
        }
    }

    std::array<SgprSource, 104> state{};
};

bool WritesExec(const Inst& i) {
    auto is_exec = [](u16 code) { return code == Operand::ExecLo || code == Operand::ExecHi; };
    switch (i.enc) {
    case Enc::SOP2:
    case Enc::SOPK:
        return is_exec(i.dst);
    case Enc::SOP1: {
        const auto op = static_cast<Sop1>(i.op);
        if (op >= Sop1::S_AND_SAVEEXEC_B64 && op <= Sop1::S_XNOR_SAVEEXEC_B64) {
            return true;
        }
        return is_exec(i.dst);
    }
    case Enc::VOPC:
    case Enc::VOP3:
        if (i.op < 256) {
            const u16 base = i.op < 0x80 ? (i.op & 0x70) : (i.op & 0xF0);
            // V_CMPX_* families: 0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0.
            return (base & 0x10) != 0 || is_exec(i.sdst);
        }
        return i.vop3 && is_exec(i.sdst);
    default:
        return false;
    }
}

bool IsImageSample(u16 op) {
    return op >= static_cast<u16>(Mimg::IMAGE_SAMPLE) && op <= static_cast<u16>(Mimg::IMAGE_GET_LOD);
}

bool IsImageCompare(u16 op) {
    switch (static_cast<Mimg>(op)) {
    case Mimg::IMAGE_SAMPLE_C: case Mimg::IMAGE_SAMPLE_C_CL: case Mimg::IMAGE_SAMPLE_C_D:
    case Mimg::IMAGE_SAMPLE_C_D_CL: case Mimg::IMAGE_SAMPLE_C_L: case Mimg::IMAGE_SAMPLE_C_B:
    case Mimg::IMAGE_SAMPLE_C_B_CL: case Mimg::IMAGE_SAMPLE_C_LZ: case Mimg::IMAGE_SAMPLE_C_O:
    case Mimg::IMAGE_SAMPLE_C_CL_O: case Mimg::IMAGE_SAMPLE_C_D_O: case Mimg::IMAGE_SAMPLE_C_D_CL_O:
    case Mimg::IMAGE_SAMPLE_C_L_O: case Mimg::IMAGE_SAMPLE_C_B_O: case Mimg::IMAGE_SAMPLE_C_B_CL_O:
    case Mimg::IMAGE_SAMPLE_C_LZ_O: case Mimg::IMAGE_GATHER4_C: case Mimg::IMAGE_GATHER4_C_CL:
    case Mimg::IMAGE_GATHER4_C_L: case Mimg::IMAGE_GATHER4_C_B: case Mimg::IMAGE_GATHER4_C_B_CL:
    case Mimg::IMAGE_GATHER4_C_LZ: case Mimg::IMAGE_GATHER4_C_LZ_O:
        return true;
    default:
        return false;
    }
}

// Replaces S_SWAPPC_B64 (call into the fetch subroutine) with the subroutine's
// code. The callee address comes from user data, so it is known per draw.
bool InlineFetch(Program& p, const BuildInput& in) {
    for (size_t idx = 0; idx < p.code.size(); ++idx) {
        const Inst& call = p.code[idx];
        if (call.enc != Enc::SOP1 || static_cast<Sop1>(call.op) != Sop1::S_SWAPPC_B64) {
            continue;
        }
        Provenance prov(in.num_user_sgprs);
        for (size_t k = 0; k < idx; ++k) {
            prov.Step(p.code[k]);
        }
        const SgprSource lo = prov.Get(call.src[0]);
        const SgprSource hi = prov.Get(call.src[0] + 1u);
        if (lo.kind != SgprSource::UserData || hi.kind != SgprSource::UserData) {
            DC_WARN("shader %#llx: fetch subroutine address is not in user data", p.address);
            return false;
        }
        const u64 target = u64(in.user_data[lo.ud]) | (u64(in.user_data[hi.ud]) << 32);
        if (!Readable(target, 16)) {
            DC_WARN("shader %#llx: fetch subroutine %#llx is not mapped", p.address, target);
            return false;
        }
        const u32* fcode = reinterpret_cast<const u32*>(target);
        auto fetch = DecodeProgram(fcode, 4096, true);
        p.fetch_bytes = fetch.empty() ? 0 : fetch.back().pc + fetch.back().size * 4;
        if (!fetch.empty() && fetch.back().enc == Enc::SOP1 &&
            static_cast<Sop1>(fetch.back().op) == Sop1::S_SETPC_B64) {
            fetch.pop_back();
        }
        p.hash = HashCode(fetch, fcode, p.hash);
        for (Inst& f : fetch) {
            f.pc += kFetchPcBase;
        }
        p.fetch_address = target;
        p.fetch_ud = lo.ud;
        p.code.erase(p.code.begin() + static_cast<long>(idx));
        p.code.insert(p.code.begin() + static_cast<long>(idx), fetch.begin(), fetch.end());
        return true;
    }
    return true;
}

} // namespace

static void FindBufferCopy(Program& p) {
    if (p.stage!=Stage::Compute || p.code.size()>12) return;
    const Inst *load=nullptr,*store=nullptr;
    bool grouped=false,add=false,compare=false,mask=false;
    for (const Inst& i:p.code) {
        if (i.enc==Enc::MUBUF) {
            if (!i.idxen || i.offen || i.addr64 || i.offset || i.lds || i.vaddr!=0 || i.vdata!=1) return;
            if (i.op==u16(Mubuf::BUFFER_LOAD_FORMAT_X) && !load && !store) load=&i;
            else if (i.op==u16(Mubuf::BUFFER_STORE_FORMAT_X) && load && !store) store=&i;
            else return;
        } else if (i.enc==Enc::SOP1 && i.op==u16(Sop1::S_MOV_B32) && i.dst==Operand::VccHi) {
            // Compiler metadata prefix.
        } else if (i.enc==Enc::SOP2 && i.op==u16(Sop2::S_LSHL_B32) && !load &&
                   i.dst==0 && i.src[0]==p.num_user_sgprs && i.src[1]==Operand::Zero+6) {
            grouped=true;
        } else if (i.enc==Enc::VOP2 && i.op==u16(Vop::V_ADD_I32) && !load &&
                   i.dst==0 && i.src[0]==0 && i.src[1]==Operand::Vgpr0) {
            add=true;
        } else if (i.enc==Enc::VOPC && i.op==u16(Vop::V_CMP_U32)+4 && load && !store &&
                   i.src[1]==Operand::Vgpr0) {
            compare=true;
        } else if (i.enc==Enc::SOP1 && i.op==u16(Sop1::S_AND_SAVEEXEC_B64) &&
                   i.dst==0 && i.src[0]==Operand::VccLo && compare && !store) {
            mask=true;
        } else if (i.enc==Enc::SOPP && (i.op==u16(Sopp::S_WAITCNT) || i.op==u16(Sopp::S_ENDPGM))) {
        } else return;
    }
    if (!load || !store || grouped!=add || compare!=mask ||
        (grouped && !(p.compute_rsrc2 & (1u<<7))) ||
        load->srsrc<4 || store->srsrc<4 || load->srsrc+4>p.num_user_sgprs ||
        store->srsrc+4>p.num_user_sgprs) return;
    if (compare) for (const Inst& i:p.code)
        if (i.enc==Enc::VOPC && i.src[0]!=store->srsrc+2) return;
    p.copy_source_ud=load->srsrc;
    p.copy_destination_ud=store->srsrc;
    p.copy_uses_group_id=grouped;
}

// Gnm's typed vector-fill kernels cover either 64 or 128 consecutive records
// per group. Validate their complete address/value calculation before treating
// a dispatch as a full-surface clear; other compute shaders run normally.
static void FindBufferFill(Program& p) {
    if (p.stage != Stage::Compute || !(p.compute_rsrc2 & (1u<<7))) return;
    std::vector<const Inst*> code;
    for (const auto& i : p.code) {
        if (i.enc==Enc::SOP1 && i.op==u16(Sop1::S_MOV_B32) && i.dst==Operand::VccHi) continue;
        if (i.enc==Enc::SOPP && (i.op==u16(Sopp::S_WAITCNT) || i.op==u16(Sopp::S_ENDPGM))) continue;
        if (i.abs || i.neg || i.omod || i.clamp) return;
        code.push_back(&i);
    }
    if (code.size()!=8 && code.size()!=20) return;
    const bool twice=code.size()==20;
    auto alu=[&](size_t at, Enc enc, u16 op, u16 dst, u16 a, u16 b=0) {
        const auto& i=*code[at];
        return i.enc==enc && i.op==op && i.dst==dst && i.src[0]==a &&
            (enc==Enc::VOP1 || i.src[1]==b);
    };
    if (!alu(0,Enc::SOP2,u16(Sop2::S_LSHL_B32),0,p.num_user_sgprs,Operand::Zero+6) ||
        !alu(1,Enc::VOP2,u16(Vop::V_ADD_I32),0,0,Operand::Vgpr0)) return;
    const auto& load=*code[2];
    if (load.enc!=Enc::SMRD || load.op!=u16(Smrd::S_BUFFER_LOAD_DWORDX4) ||
        load.dst!=0 || !load.imm || load.smrd_offset || load.sbase<4 ||
        load.sbase+4>p.num_user_sgprs) return;
    if (twice && (
        !alu(3,Enc::VOP2,u16(Vop::V_LSHRREV_B32),1,Operand::Zero+6,Operand::Vgpr0) ||
        !alu(4,Enc::VOP2,u16(Vop::V_SUB_I32),2,Operand::Zero,Operand::Vgpr0+1) ||
        !alu(5,Enc::VOP2,u16(Vop::V_LSHLREV_B32),2,Operand::Zero+6,Operand::Vgpr0+2) ||
        !alu(6,Enc::VOP2,u16(Vop::V_ADD_I32),0,Operand::Vgpr0,Operand::Vgpr0+2) ||
        !alu(7,Enc::VOP2,u16(Vop::V_LSHLREV_B32),1,Operand::Zero+7,Operand::Vgpr0+1) ||
        !alu(8,Enc::VOP2,u16(Vop::V_ADD_I32),0,Operand::Vgpr0,Operand::Vgpr0+1))) return;
    const u32 start=twice ? 9 : 3;
    for (u16 k=0;k<4;++k)
        if (!alu(start+k,Enc::VOP1,u16(Vop::V_MOV_B32),k+1,k)) return;
    if (twice) {
        if (!alu(13,Enc::VOP2,u16(Vop::V_ADD_I32),5,Operand::Zero+64,Operand::Vgpr0)) return;
        for (u16 k=0;k<4;++k)
            if (!alu(14+k,Enc::VOP1,u16(Vop::V_MOV_B32),k+6,k)) return;
    }
    const u32 store_at=twice ? 18 : 7;
    const u32 destination=code[store_at]->srsrc;
    if (destination+4>p.num_user_sgprs || destination<4 || destination==load.sbase) return;
    for (u32 k=0;k<(twice ? 2u : 1u);++k) {
        const auto& i=*code[store_at+k];
        if (i.enc!=Enc::MUBUF || i.op!=u16(Mubuf::BUFFER_STORE_FORMAT_XYZW) ||
            i.srsrc!=destination || !i.idxen || i.offen || i.addr64 || i.lds ||
            i.offset || i.soffset!=Operand::Zero || i.vaddr!=(k ? 5 : 0) ||
            i.vdata!=(k ? 6 : 1)) return;
    }
    // Loading the fill value into s0..s3 must not overwrite the destination V#.
    p.fill_destination_ud=destination;
    p.fill_value_ud=load.sbase;
    p.fill_elements_per_group=twice ? 128 : 64;
}

u32 Program::ImageSlot(u32 pc) const {
    for (const auto& [ipc, slot] : image_slot_by_pc) {
        if (ipc == pc) {
            return slot;
        }
    }
    return 0;
}

std::unique_ptr<Program> BuildProgram(const BuildInput& in) {
    if (!in.code && !Readable(in.address, 64)) {
        return nullptr;
    }
    auto p = std::make_unique<Program>();
    p->stage = in.stage;
    p->address = in.address;
    p->num_user_sgprs = in.num_user_sgprs;
    p->vgpr_comp_cnt = in.vgpr_comp_cnt;
    p->compute_rsrc2 = in.compute_rsrc2;

    const u32* code = in.code ? in.code : reinterpret_cast<const u32*>(in.address);
    p->code = DecodeProgram(code);
    if (p->code.empty()) {
        return nullptr;
    }
    p->hash = HashCode(p->code, code, static_cast<u64>(in.stage));
    if (!InlineFetch(*p, in)) {
        return nullptr;
    }
    // The same instruction bytes can use different system inputs and descriptor
    // provenance depending on the stage's resource registers.
    for (u32 value : {in.num_user_sgprs,in.vgpr_comp_cnt,in.compute_rsrc2}) {
        p->hash ^= value;
        p->hash *= 0x100000001b3ull;
    }

    Provenance prov(in.num_user_sgprs);
    for (const Inst& i : p->code) {
        if (IsBranch(i)) {
            p->has_branches = true;
            p->labels.push_back(BranchTarget(i));
        }
        p->writes_exec |= WritesExec(i);

        switch (i.enc) {
        case Enc::SMRD:
            if (i.op>=u16(Smrd::S_BUFFER_LOAD_DWORD) && i.op<=u16(Smrd::S_BUFFER_LOAD_DWORDX16)) {
                std::array<SgprSource,4> descriptor;
                for (unsigned k=0;k<4;++k) descriptor[k]=prov.Get(i.sbase+k);
                if (std::find(p->buffers.begin(),p->buffers.end(),descriptor)==p->buffers.end()) p->buffers.push_back(descriptor);
                BufferReadResource read;
                read.descriptor=descriptor;
                read.pc=i.pc; read.encoding=i.enc; read.op=i.op;
                read.immediate_offset=i.imm ? i.smrd_offset*4 : 0;
                read.scalar_offset=i.imm ? 0 : static_cast<u16>(i.smrd_offset);
                read.dynamic_scalar_offset=!i.imm;
                p->buffer_reads.push_back(read);
            }
            break;
        case Enc::MUBUF:
        case Enc::MTBUF: {
            const bool typed=i.enc==Enc::MTBUF;
            const bool atomic=!typed && i.op>=u16(Mubuf::BUFFER_ATOMIC_SWAP) &&
                              i.op<=u16(Mubuf::BUFFER_ATOMIC_DEC);
            const bool load=i.op<=3 || (!typed && i.op>=u16(Mubuf::BUFFER_LOAD_UBYTE) &&
                                       i.op<=u16(Mubuf::BUFFER_LOAD_DWORDX3));
            const bool store=(i.op>=4 && i.op<=7) || (!typed &&
                (i.op==u16(Mubuf::BUFFER_STORE_BYTE) || i.op==u16(Mubuf::BUFFER_STORE_SHORT) ||
                 (i.op>=u16(Mubuf::BUFFER_STORE_DWORD) && i.op<=u16(Mubuf::BUFFER_STORE_DWORDX3))));
            if (store || atomic) {
                std::array<SgprSource,4> descriptor;
                for (unsigned k=0;k<4;++k) descriptor[k]=prov.Get(i.srsrc+k);
                p->buffer_writes.push_back(descriptor);
            }
            if (!load && !atomic) break;
            BufferReadResource read;
            for (unsigned k=0;k<4;++k) read.descriptor[k]=prov.Get(i.srsrc+k);
            read.pc=i.pc; read.encoding=i.enc; read.op=i.op;
            read.immediate_offset=i.offset; read.scalar_offset=i.soffset;
            read.dynamic_scalar_offset=!(i.soffset>=Operand::Zero && i.soffset<=Operand::IntNegMax);
            read.vector_address=i.vaddr; read.index_enabled=i.idxen;
            read.offset_enabled=i.offen; read.address64=i.addr64;
            read.writes=atomic;
            p->buffer_reads.push_back(read);
            break;
        }
        case Enc::EXP:
            if (i.target >= ExpTarget::Param0) {
                p->param_mask |= 1u << (i.target - ExpTarget::Param0);
            } else if (i.target == ExpTarget::Pos0) {
                p->exports_position = true;
            } else if (i.target < 8) {
                p->mrt_mask |= static_cast<u8>(1u << i.target);
            } else if (i.target == ExpTarget::MrtZ) {
                p->exports_depth = true;
            }
            break;
        case Enc::VINTRP:
            p->attr_mask |= 1u << i.attr;
            if (static_cast<Vintrp>(i.op) == Vintrp::V_INTERP_MOV_F32) {
                p->attr_flat_mask |= 1u << i.attr;
            }
            break;
        case Enc::DS:
            if (static_cast<Ds>(i.op) == Ds::DS_SWIZZLE_B32) {
                p->uses_quad_ops = true;
            }
            break;
        case Enc::MIMG: {
            ImageResource r;
            for (u32 k = 0; k < 8; ++k) {
                r.tsharp[k] = prov.Get(i.srsrc + k);
            }
            r.has_sampler = IsImageSample(i.op);
            if (r.has_sampler) {
                for (u32 k = 0; k < 4; ++k) {
                    r.ssharp[k] = prov.Get(i.ssamp + k);
                }
                u32 s_idx = 0;
                for (; s_idx < p->samplers.size(); ++s_idx) {
                    if (p->samplers[s_idx] == r.ssharp) {
                        break;
                    }
                }
                if (s_idx == p->samplers.size()) {
                    p->samplers.push_back(r.ssharp);
                }
                r.sampler_index = s_idx;
            }
            r.storage = i.op >= static_cast<u16>(Mimg::IMAGE_STORE) &&
                        i.op < static_cast<u16>(Mimg::IMAGE_SAMPLE) &&
                        static_cast<Mimg>(i.op) != Mimg::IMAGE_GET_RESINFO;
            r.reads = i.op < static_cast<u16>(Mimg::IMAGE_STORE) ||
                      (i.op > static_cast<u16>(Mimg::IMAGE_GET_RESINFO) && i.op < static_cast<u16>(Mimg::IMAGE_SAMPLE));
            r.compare = IsImageCompare(i.op);
            u32 slot = 0;
            for (; slot < p->images.size(); ++slot) {
                const ImageResource& e = p->images[slot];
                if (e.tsharp == r.tsharp && e.ssharp == r.ssharp) {
                    break;
                }
            }
            if (slot == p->images.size()) {
                p->images.push_back(r);
            } else {
                p->images[slot].storage |= r.storage;
                p->images[slot].reads |= r.reads;
                p->images[slot].compare |= r.compare;
                p->images[slot].has_sampler |= r.has_sampler;
                if (r.has_sampler) {
                    p->images[slot].sampler_index = r.sampler_index;
                }
            }
            p->image_slot_by_pc.emplace_back(i.pc, slot);
            break;
        }
        default:
            break;
        }
        prov.Step(i);
    }
    for (auto& read:p->buffer_reads)
        read.descriptor_written=std::find(p->buffer_writes.begin(),p->buffer_writes.end(),read.descriptor)!=p->buffer_writes.end();
    std::sort(p->labels.begin(), p->labels.end());
    p->labels.erase(std::unique(p->labels.begin(), p->labels.end()), p->labels.end());
    FindBufferCopy(*p);
    FindBufferFill(*p);
    return p;
}

bool MayWriteBuffer(const Program& program,const u32* ud,u64 base,u64 bytes) {
    constexpr u64 limit=1ull<<40;
    if (!ud || !bytes || base>=limit || bytes>limit-base) return true;
    for (const auto& writer:program.buffer_writes) {
        std::array<u32,4> descriptor;
        for (unsigned k=0;k<4;++k) {
            const auto& source=writer[k];
            if (source.kind==SgprSource::Unknown || source.ud>=16 ||
                (source.kind!=SgprSource::UserData && source.ud>=15)) return true;
            if (source.kind!=SgprSource::UserData) {
                const u64 pointer=source.kind==SgprSource::PointerLoad
                    ? (u64(ud[source.ud])|(u64(ud[source.ud+1])<<32))&0xffffffffffffull
                    : u64(ud[source.ud])|(u64(ud[source.ud+1]&0xfff)<<32);
                const u64 address=pointer+source.offset;
                if (address<pointer || address>~0ull-4 || !Readable(address,4)) return true;
            }
            descriptor[k]=ResolveSgpr(source,ud);
        }
        const u64 start=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&(limit-1);
        const u64 count=u64(descriptor[2])*std::max(1u,(descriptor[1]>>16)&0x3fff);
        if (!count || count>limit-start) return true;
        if (start<base+bytes && base<start+count) return true;
    }
    return false;
}

u32 ResolveSgpr(const SgprSource& src, const u32* ud) {
    switch (src.kind) {
    case SgprSource::UserData:
        return ud[src.ud];
    case SgprSource::PointerLoad: {
        const u64 base = (u64(ud[src.ud]) | (u64(ud[src.ud + 1]) << 32)) & 0xFFFFFFFFFFFFull;
        const u64 addr = base + src.offset;
        if (!Readable(addr, 4)) {
            return 0;
        }
        u32 v;
        std::memcpy(&v, reinterpret_cast<const void*>(addr), 4);
        return v;
    }
    case SgprSource::BufferLoad: {
        const u64 base = u64(ud[src.ud]) | (u64(ud[src.ud + 1] & 0xFFF) << 32);
        const u64 addr = base + src.offset;
        if (!Readable(addr, 4)) {
            return 0;
        }
        u32 v;
        std::memcpy(&v, reinterpret_cast<const void*>(addr), 4);
        return v;
    }
    default:
        return 0;
    }
}

} // namespace Gpu::Shader
