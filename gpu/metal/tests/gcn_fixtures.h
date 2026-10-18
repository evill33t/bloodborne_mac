// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gpu/shader/gcn.h"
#include <vector>

namespace TestGcn {
// Synthetic SDK-style typed fills, generated without game shader binaries.
inline std::vector<u32> UniformFill(bool twice) {
    using namespace Gpu::Shader::Gcn;
    const auto v1=[](Vop op,u32 dst,u32 src) {
        return 0x7e000000u|(dst<<17)|((u32(op)-384)<<9)|src;
    };
    const auto v2=[](Vop op,u32 dst,u32 a,u32 b) {
        return ((u32(op)-256)<<25)|(dst<<17)|(b<<9)|a;
    };
    std::vector<u32> words{
        0x80000000u|(u32(Sop2::S_LSHL_B32)<<23)|((Operand::Zero+6)<<8)|12,
        v2(Vop::V_ADD_I32,0,0,0),
        0xc0000000u|(u32(Smrd::S_BUFFER_LOAD_DWORDX4)<<22)|(4u<<9)|(1u<<8)};
    if (twice) {
        words.push_back(v2(Vop::V_LSHRREV_B32,1,Operand::Zero+6,0));
        words.push_back(v2(Vop::V_SUB_I32,2,Operand::Zero,1));
        words.push_back(v2(Vop::V_LSHLREV_B32,2,Operand::Zero+6,2));
        words.push_back(v2(Vop::V_ADD_I32,0,Operand::Vgpr0,2));
        words.push_back(v2(Vop::V_LSHLREV_B32,1,Operand::Zero+7,1));
        words.push_back(v2(Vop::V_ADD_I32,0,Operand::Vgpr0,1));
    }
    for (u32 k=0;k<4;++k) words.push_back(v1(Vop::V_MOV_B32,k+1,k));
    if (twice) {
        words.push_back(v2(Vop::V_ADD_I32,5,Operand::Zero+64,0));
        for (u32 k=0;k<4;++k) words.push_back(v1(Vop::V_MOV_B32,k+6,k));
    }
    words.push_back(0xe0002000u|(u32(Mubuf::BUFFER_STORE_FORMAT_XYZW)<<18));
    words.push_back(0x80010100);
    if (twice) {
        words.push_back(0xe0002000u|(u32(Mubuf::BUFFER_STORE_FORMAT_XYZW)<<18));
        words.push_back(0x80010605);
    }
    words.push_back(0xbf810000);
    return words;
}
}
