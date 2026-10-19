// SPDX-License-Identifier: GPL-2.0-or-later
// CPU-only resource-provenance fixtures. These synthetic instructions never
// execute on a GPU and contain no game shader bytes.
#include "gpu/shader/program.h"
#include "core/memory.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <vector>

namespace {
struct ReadableRange {u64 address,size;};
std::vector<ReadableRange> readable_ranges;
}

// Link this fixture with program.cpp and gcn_decode.cpp, without the runtime's
// memory manager. Only explicitly registered fixture tables are readable.
namespace Core::Memory {
u64 RuntimeDirectSize() {return 0;}
bool Query(VAddr address,bool find_next,Vma* out) {
    assert(!find_next);
    for (const auto& range:readable_ranges) {
        if (address>=range.address && address-range.address<range.size) {
            if (out) *out={range.address,range.size,kProtCpuRead,VmaType::Flexible,0,"fixture"};
            return true;
        }
    }
    return false;
}
}

namespace {
using namespace Gpu::Shader;
using namespace Gpu::Shader::Gcn;
constexpr u32 End=0xbf810000;

// GCN memory encodings: resource registers are encoded in groups of four.
void Buffer(std::vector<u32>& code,Enc encoding,u16 op,u32 resource,
            u32 offset=0,bool index=false,bool vector_offset=false,u16 scalar=Operand::Zero) {
    assert(resource%4==0 && resource<104 && offset<4096);
    const bool typed=encoding==Enc::MTBUF;
    assert(typed || encoding==Enc::MUBUF);
    code.push_back((typed ? 0xe8000000u : 0xe0000000u) |
        (u32(op)<<(typed ? 16 : 18)) | (index ? 1u<<13 : 0) |
        (vector_offset ? 1u<<12 : 0) | offset |
        (typed ? (4u<<19)|(4u<<23) : 0));
    code.push_back((u32(scalar)<<24)|((resource/4)<<16)|(3u<<8)|2u);
}
u32 Scalar(Smrd op,u32 destination,u32 resource,u32 offset,bool immediate=true) {
    assert(resource%2==0 && resource<104 && offset<256);
    return 0xc0000000u|(u32(op)<<22)|(destination<<15)|((resource/2)<<9)|
        (immediate ? 1u<<8 : 0)|offset;
}
std::unique_ptr<Program> Build(std::vector<u32> code,u32 user_registers=12) {
    code.push_back(End);
    const std::array<u32,16> ud{};
    BuildInput input{.stage=Stage::Compute,.user_data=ud.data(),.num_user_sgprs=user_registers,.code=code.data()};
    auto program=BuildProgram(input);
    assert(program && program->code.back().op==u16(Sopp::S_ENDPGM));
    return program;
}
void DirectDescriptor(const BufferReadResource& read,unsigned first) {
    for (unsigned k=0;k<4;++k) {
        const auto& source=read.descriptor[k];
        assert(source.kind==SgprSource::UserData && source.ud==first+k && source.offset==0);
    }
}
void DirectReads() {
    std::vector<u32> code;
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),4,28,true,true,6);
    Buffer(code,Enc::MTBUF,u16(Mtbuf::TBUFFER_LOAD_FORMAT_XY),8,12);
    const auto program=Build(code);
    assert(program->buffer_reads.size()==2 && program->buffers.empty());
    const auto& untyped=program->buffer_reads[0];
    DirectDescriptor(untyped,4);
    assert(untyped.pc==0 && untyped.encoding==Enc::MUBUF && untyped.op==u16(Mubuf::BUFFER_LOAD_DWORD));
    assert(untyped.immediate_offset==28 && untyped.scalar_offset==6 && untyped.dynamic_scalar_offset);
    assert(untyped.index_enabled && untyped.offset_enabled && untyped.vector_address==2);
    assert(!untyped.address64 && !untyped.writes && !untyped.descriptor_written);
    assert(untyped.HasReadOnlyDescriptor());
    const auto& typed=program->buffer_reads[1];
    DirectDescriptor(typed,8);
    assert(typed.pc==8 && typed.encoding==Enc::MTBUF && typed.op==u16(Mtbuf::TBUFFER_LOAD_FORMAT_XY));
    assert(typed.immediate_offset==12 && !typed.dynamic_scalar_offset && !typed.index_enabled && !typed.offset_enabled);
    // Special scalar registers are dynamic too; they are not inline constants.
    code.clear();Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),4,0,false,false,Operand::M0);
    assert(Build(code)->buffer_reads[0].dynamic_scalar_offset);
}
void ScalarReadsPreserveCameraList() {
    const auto program=Build({Scalar(Smrd::S_BUFFER_LOAD_DWORDX4,20,4,3),
        Scalar(Smrd::S_BUFFER_LOAD_DWORD,24,4,6,false)});
    assert(program->buffer_reads.size()==2 && program->buffers.size()==1);
    DirectDescriptor(program->buffer_reads[0],4);
    assert(program->buffers[0]==program->buffer_reads[0].descriptor);
    assert(program->buffer_reads[0].encoding==Enc::SMRD && program->buffer_reads[0].immediate_offset==12);
    assert(!program->buffer_reads[0].dynamic_scalar_offset);
    assert(program->buffer_reads[1].dynamic_scalar_offset && program->buffer_reads[1].scalar_offset==6);
}
void PointerTableDescriptor() {
    // Load four descriptor words from [s0:s1 + 16] into s20..s23. The later
    // vector load must retain the table provenance instead of treating s20 as UD.
    std::vector<u32> code{Scalar(Smrd::S_LOAD_DWORDX4,20,0,4)};
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),20,8,true);
    const auto program=Build(code,2);
    assert(program->buffer_reads.size()==1 && program->buffers.empty());
    auto& read=program->buffer_reads[0];
    assert(read.HasReadOnlyDescriptor());
    assert(read.pc==4);
    std::array<u32,8> table{11,22,33,44,0xabc00000,0x00040080,128,0x24004};
    const auto pointer=reinterpret_cast<std::uintptr_t>(table.data());
    readable_ranges={{pointer,sizeof(table)}};
    std::array<u32,16> ud{};ud[0]=u32(pointer);ud[1]=u32(u64(pointer)>>32);
    for (unsigned k=0;k<4;++k) {
        const auto& source=read.descriptor[k];
        assert(source.kind==SgprSource::PointerLoad && source.ud==0 && source.offset==16+k*4);
        assert(ResolveSgpr(source,ud.data())==table[4+k]);
    }
    table[4]=0xdef00000;
    assert(ResolveSgpr(read.descriptor[0],ud.data())==0xdef00000); // resolve current binding, not stale bytes
    readable_ranges.clear();
    assert(ResolveSgpr(read.descriptor[0],ud.data())==0); // unmapped table is refused
}
void WritesAndUnknowns() {
    std::vector<u32> code;
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_DWORD),4);
    Buffer(code,Enc::MTBUF,u16(Mtbuf::TBUFFER_STORE_FORMAT_X),8);
    assert(Build(code)->buffer_reads.empty());

    code.clear();
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),4);
    Buffer(code,Enc::MTBUF,u16(Mtbuf::TBUFFER_LOAD_FORMAT_X),8);
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_DWORD),4);
    const auto rmw=Build(code);
    assert(rmw->buffer_reads.size()==2);
    assert(!rmw->buffer_reads[0].writes && rmw->buffer_reads[0].descriptor_written);
    assert(!rmw->buffer_reads[1].descriptor_written); // a store to another descriptor must not taint it
    assert(!rmw->buffer_reads[0].HasReadOnlyDescriptor());
    assert(rmw->buffer_reads[1].HasReadOnlyDescriptor());
    code.clear();Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_ATOMIC_ADD),4);
    const auto atomic=Build(code);
    assert(atomic->buffer_reads.size()==1 && atomic->buffer_reads[0].writes && atomic->buffer_reads[0].descriptor_written);
    assert(!atomic->buffer_reads[0].HasReadOnlyDescriptor());

    code.clear();Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),20);
    const auto unknown=Build(code,8);
    for (const auto& source:unknown->buffer_reads[0].descriptor) assert(source.kind==SgprSource::Unknown);
    assert(!unknown->buffer_reads[0].HasReadOnlyDescriptor());
    // Scalar arithmetic invalidates only the modified descriptor word. It must
    // not retain a guessed UD origin or discard the other three valid words.
    code={0x80000000u|(5u<<16)|(u32(Operand::Zero)<<8)|5u}; // s_add_u32 s5,s5,0
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),4);
    const auto modified=Build(code,8);
    const auto& descriptor=modified->buffer_reads[0].descriptor;
    assert(descriptor[1].kind==SgprSource::Unknown);
    assert(!modified->buffer_reads[0].HasReadOnlyDescriptor());
    for (unsigned k:{0u,2u,3u}) assert(descriptor[k].kind==SgprSource::UserData && descriptor[k].ud==4+k);
}
void RuntimeWriteAliases() {
    std::vector<u32> code;
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_LOAD_DWORD),4);
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_DWORD),8);
    const auto program=Build(code);
    assert(program->buffer_reads[0].HasReadOnlyDescriptor());
    assert(program->buffer_writes.size()==1);
    std::array<u32,16> ud{};
    ud[4]=0x80000;ud[5]=4u<<16;ud[6]=64;
    ud[8]=0x80020;ud[9]=4u<<16;ud[10]=4;
    assert(MayWriteBuffer(*program,ud.data(),0x80000,256)); // different descriptors, same allocation
    ud[8]=0x80100;
    assert(!MayWriteBuffer(*program,ud.data(),0x80000,256)); // adjacent is not overlapping
    ud[8]=0x7fff8;
    assert(MayWriteBuffer(*program,ud.data(),0x80000,256)); // overlap begins before read base
    ud[10]=0;
    assert(MayWriteBuffer(*program,ud.data(),0x80000,256)); // unknown writer range
    code.clear();Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_DWORD),20);
    assert(MayWriteBuffer(*Build(code,8),ud.data(),0x80000,256));
    // A separately loaded table descriptor can alias the direct read descriptor.
    code={Scalar(Smrd::S_LOAD_DWORDX4,20,0,4)};
    Buffer(code,Enc::MUBUF,u16(Mubuf::BUFFER_STORE_DWORD),20);
    const auto table_writer=Build(code,2);
    std::array<u32,8> table{0,0,0,0,0x80080,4u<<16,8,0x24004};
    const u64 pointer=reinterpret_cast<std::uintptr_t>(table.data());
    ud[0]=u32(pointer);ud[1]=u32(pointer>>32);
    readable_ranges={{pointer,sizeof(table)}};
    assert(MayWriteBuffer(*table_writer,ud.data(),0x80000,256));
    table[4]=0x90000;
    assert(!MayWriteBuffer(*table_writer,ud.data(),0x80000,256));
    readable_ranges.clear();
    assert(MayWriteBuffer(*table_writer,ud.data(),0x80000,256)); // unreadable writer is refused
}
}

int main() {
    DirectReads();ScalarReadsPreserveCameraList();PointerTableDescriptor();WritesAndUnknowns();RuntimeWriteAliases();
    std::puts("HTILE resource provenance: direct/scalar/table reads, write-only exclusion, RMW and unknown origins passed");
}
