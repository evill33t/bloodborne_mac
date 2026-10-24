// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/shader/msl.h"
#include "gpu/metal/wind_consumed_contract.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
namespace WindConsumedFixture {
namespace Shader=Gpu::Shader;
using namespace Shader::Gcn;
inline void Require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
inline Shader::Program SyntheticShape() {
    // Deliberately assigned identity tests rejection/plumbing, not original
    // program identity. --capture validates actual retained code+fetch bytes.
    Shader::Program p;p.stage=Shader::Stage::Vertex;
    p.hash=Gpu::Metal::WindConsumed::ShaderHash;p.num_user_sgprs=16;p.vgpr_comp_cnt=3;
    p.fetch_ud=0;p.fetch_bytes=72;p.fetch_address=0x1000;p.param_mask=15;
    Inst prefix;prefix.enc=Enc::SOP1;prefix.op=u16(Sop1::S_MOV_B32);prefix.size=2;
    prefix.dst=Operand::VccHi;prefix.src[0]=Operand::Literal;prefix.literal=0x2be;p.code.push_back(prefix);
    for(unsigned k=0;k<5;++k) {
        Inst i;i.pc=Shader::kFetchPcBase+4*k;i.enc=Enc::SMRD;i.op=u16(Smrd::S_LOAD_DWORDX4);
        i.dst=16+4*k;i.sbase=2;i.imm=true;i.smrd_offset=4*k;p.code.push_back(i);
    }
    Inst wait;wait.pc=Shader::kFetchPcBase+0x14;wait.enc=Enc::SOPP;wait.op=u16(Sopp::S_WAITCNT);wait.simm16=127;p.code.push_back(wait);
    constexpr Mubuf ops[]{Mubuf::BUFFER_LOAD_FORMAT_XYZ,Mubuf::BUFFER_LOAD_FORMAT_XYZW,
        Mubuf::BUFFER_LOAD_FORMAT_XYZW,Mubuf::BUFFER_LOAD_FORMAT_XY,Mubuf::BUFFER_LOAD_FORMAT_XYZ};
    for(unsigned k=0;k<5;++k) {
        Inst i;i.pc=Shader::kFetchPcBase+0x18+8*k;i.enc=Enc::MUBUF;i.size=2;i.op=u16(ops[k]);
        i.vdata=4+4*k;i.vaddr=0;i.srsrc=16+4*k;i.soffset=Operand::Zero;i.idxen=true;p.code.push_back(i);
    }
    wait.pc=Shader::kFetchPcBase+0x40;wait.simm16=0;p.code.push_back(wait);
    std::map<unsigned,Inst> required;
    for(const unsigned pc:{0x000cu,0x0010u,0x001cu,0x14b4u,0x14e8u,0x14ecu,0x14f8u,0x1500u,
            0x00b4u,0x00bcu,0x00c4u,0x00f8u,0x1534u,0x1578u,0x1580u,0x1588u,0x1590u,0x1598u,0x15a0u}) {
        Inst i;i.pc=pc;i.enc=Enc::SOPP;i.op=u16(Sopp::S_NOP);
        switch(pc) {
        case 0x000c:i.enc=Enc::SMRD;i.op=u16(Smrd::S_LOAD_DWORDX8);i.dst=16;i.sbase=6;i.imm=true;i.smrd_offset=0;break;
        case 0x0010:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORD);i.dst=0;i.sbase=12;i.imm=true;i.smrd_offset=20;break;
        case 0x001c:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORDX4);i.dst=12;i.sbase=20;i.imm=true;i.smrd_offset=32;break;
        case 0x14b4:i.enc=Enc::SMRD;i.op=u16(Smrd::S_LOAD_DWORDX4);i.dst=0;i.sbase=6;i.imm=true;i.smrd_offset=8;break;
        case 0x14e8:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORD);i.dst=4;i.sbase=0;i.imm=true;i.smrd_offset=191;break;
        case 0x14ec:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORDX16);i.dst=8;i.sbase=0;i.imm=true;i.smrd_offset=200;break;
        case 0x14f8:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORD);i.dst=5;i.sbase=0;i.imm=true;i.smrd_offset=187;break;
        case 0x1500:i.enc=Enc::SMRD;i.op=u16(Smrd::S_BUFFER_LOAD_DWORD);i.dst=0;i.sbase=0;i.imm=true;i.smrd_offset=183;break;
        case 0x15a0:i.op=u16(Sopp::S_ENDPGM);break;
        }
        if(pc==0x00b4 || pc==0x00bc || pc==0x00c4) {
            const auto row=(pc-0x00b4)/8;i.enc=Enc::MTBUF;i.size=2;i.op=u16(Mtbuf::TBUFFER_LOAD_FORMAT_XYZW);
            i.vdata=23+row*4;i.vaddr=row;i.srsrc=16;i.soffset=Operand::Zero;i.idxen=true;i.dfmt=14;i.nfmt=7;
        }
        if(pc==0x00f8 || pc==0x1534) {
            i.enc=Enc::VOP2;i.op=u16(Vop::V_MAC_F32);i.dst=pc==0x00f8 ? 34 : 26;
            i.src[0]=Operand::Vgpr0+(pc==0x00f8 ? 31 : 3);i.src[1]=Operand::Vgpr0+(pc==0x00f8 ? 4 : 21);
        }
        if(pc>=0x1578 && pc<=0x1598) {
            i.enc=Enc::EXP;i.size=2;i.target=pc==0x1578 ? ExpTarget::Pos0 : ExpTarget::Param0+(pc-0x1580)/8;
            i.en=15;i.done=pc==0x1578;
            for(unsigned k=0;k<4;++k)i.vsrc[k]=5+k;
        }
        required.emplace(pc,i);
    }
    // Maintain legal instruction extents and complete fetch/main PC segments.
    // Literal MOVs occupy two dwords, allowing the synthetic context to match
    // the original decoded length without embedding any original shader bytes.
    std::size_t maximum=p.code.size();
    for(unsigned pc=12;pc<=0x15a0;) {++maximum;pc+=required.contains(pc) ? required.at(pc).size*4 : 4;}
    Require(maximum>=1105,"Synthetic fixed context is too short");auto pairs=maximum-1105;
    for(unsigned pc=12;pc<=0x15a0;) {
        Inst i;i.pc=pc;i.enc=Enc::SOPP;i.op=u16(Sopp::S_NOP);
        if(required.contains(pc))i=required.at(pc);
        else if(pairs && pc+4<0x15a0 && !required.contains(pc+4)) {
            i.enc=Enc::VOP1;i.op=u16(Vop::V_MOV_B32);i.size=2;i.dst=35;i.src[0]=Operand::Literal;--pairs;
        }
        p.code.push_back(i);pc+=i.size*4;
    }
    Require(!pairs && p.code.size()==1105,"Synthetic fixed context instruction layout failed");
    return p;
}
inline std::vector<u32> Read(const std::filesystem::path& path) {
    const auto bytes=std::filesystem::file_size(path);
    Require(bytes>=16 && bytes<=65536 && !(bytes&3),"Invalid retained shader extent");
    std::vector<u32> result(bytes/4+64,0xbf810000u);std::ifstream stream(path,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(result.data()),bytes)),"Cannot read retained shader");return result;
}
inline std::unique_ptr<Shader::Program> Original(const std::filesystem::path& path) {
    auto code=Read(path),fetch=Read(path.string()+".fetch.bin");std::array<u32,16> ud{};
    const auto address=std::uintptr_t(fetch.data());ud[0]=u32(address);ud[1]=u32(address>>32);
    Shader::BuildInput input{.stage=Shader::Stage::Vertex,.user_data=ud.data(),.num_user_sgprs=16,
        .vgpr_comp_cnt=3,.code=code.data()};
    auto program=Shader::BuildProgram(input);
    Require(program && program->hash==Gpu::Metal::WindConsumed::ShaderHash,"Original Dream VS/fetch hash mismatch");
    return program;
}
}
