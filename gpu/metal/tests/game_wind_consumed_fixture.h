// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/shader/msl.h"
#include "gpu/metal/wind_consumed_contract.h"
#include <array>
#include <filesystem>
#include <fstream>
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
    p.hash=Gpu::Metal::WindConsumed::ShaderHash;p.num_user_sgprs=16;
    for(unsigned pc=0;pc<=0x1504;) {
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
        case 0x1504:i.op=u16(Sopp::S_ENDPGM);break;
        }
        if(pc==0x00b4 || pc==0x00bc || pc==0x00c4) {
            const auto row=(pc-0x00b4)/8;i.enc=Enc::MTBUF;i.size=2;i.op=u16(Mtbuf::TBUFFER_LOAD_FORMAT_XYZW);
            i.vdata=23+row*4;i.vaddr=row;i.srsrc=16;i.soffset=Operand::Zero;i.idxen=true;i.dfmt=14;i.nfmt=7;
        }
        p.code.push_back(i);
        pc+=i.size*4;
    }
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
