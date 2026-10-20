// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic CPU contracts and optional GPU comparisons against private captures.
// No retail executable or shader bytes are embedded in this fixture.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/shader/msl_prelude.h"
#include "gpu/metal/native_vertex_fetch.h"
#include "gpu/metal/direct_root_span.h"
#include "gpu/metal/direct_root_read.h"
#include "gpu/metal/static_smrd.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() {std::abort();}
namespace {
namespace Shader=Gpu::Shader;
namespace Native=Gpu::Metal::NativeVertexFetch;
using namespace Shader::Gcn;
constexpr u32 MemoryBytes=0x100000,PageBytes=16384,Vertices=16,RawWords=25;
constexpr u64 Table=0x10000,Records=0x20000,Alternative=0x28000;
void Require(bool value,const std::string& message) {if(!value) throw std::runtime_error(message);}
size_t Count(const std::string& source,const char* word) {
    size_t result=0;for(size_t at=0;(at=source.find(word,at))!=std::string::npos;at+=std::strlen(word)) ++result;
    return result;
}
Shader::Program Synthetic(u64 hash) {
    const auto* layout=Native::Layout(hash);Require(layout,"Unknown synthetic vertex hash");
    Shader::Program p;p.hash=hash;p.stage=Shader::Stage::Vertex;p.num_user_sgprs=16;p.vgpr_comp_cnt=3;
    p.fetch_address=0x1000;p.fetch_ud=0;p.fetch_bytes=layout->fetch_bytes;
    Inst prefix{};prefix.enc=Enc::SOP1;prefix.op=u16(Sop1::S_MOV_B32);prefix.dst=Operand::VccHi;
    prefix.src[0]=Operand::Literal;prefix.literal=layout->prefix_literal;prefix.size=2;
    p.code.push_back(prefix);
    for(u32 k=0;k<layout->attributes;++k) {
        Inst i{};i.pc=Shader::kFetchPcBase+4*k;i.enc=Enc::SMRD;i.op=u16(Smrd::S_LOAD_DWORDX4);
        i.dst=u16(16+4*k);i.sbase=2;i.imm=true;i.smrd_offset=4*k;p.code.push_back(i);
    }
    Inst wait{};wait.enc=Enc::SOPP;wait.op=u16(Sopp::S_WAITCNT);wait.pc=Shader::kFetchPcBase+4*layout->attributes;
    p.code.push_back(wait);
    for(u32 k=0;k<layout->attributes;++k) {
        Inst i{};i.pc=layout->first_pc+8*k;i.enc=Enc::MUBUF;i.size=2;
        i.op=k==0 ? u16(Mubuf::BUFFER_LOAD_FORMAT_XYZ) : k==5 ? u16(Mubuf::BUFFER_LOAD_FORMAT_XY) :
            u16(Mubuf::BUFFER_LOAD_FORMAT_XYZW);
        i.vdata=u8(4+4*k);i.srsrc=u8(16+4*k);i.soffset=Operand::Zero;i.idxen=true;p.code.push_back(i);
    }
    wait.pc=layout->last_pc+8;p.code.push_back(wait);
    // Expose the components outside XYZ/XY write masks. In particular v7 and
    // v26/v27 must retain their initial zero bits after a successful fusion.
    Inst position{};position.pc=12;position.enc=Enc::EXP;position.size=2;position.en=15;
    position.target=ExpTarget::Pos0;for(u32 k=0;k<4;++k) position.vsrc[k]=u8(4+k);p.code.push_back(position);
    Inst parameter=position;parameter.pc=20;parameter.target=ExpTarget::Param0;
    for(u32 k=0;k<4;++k) parameter.vsrc[k]=u8(24+k);p.code.push_back(parameter);
    p.exports_position=true;p.param_mask=1;
    Inst end{};end.pc=28;end.enc=Enc::SOPP;end.op=u16(Sopp::S_ENDPGM);p.code.push_back(end);
    return p;
}
void Proof(const Shader::Program& original) {
    std::string reason;const bool eligible=Shader::VertexNativeFetchEligible(original,&reason);
    Require(eligible,"Original vertex fetch proof failed: "+reason);
    Shader::MslOptions before_options,after_options;after_options.native_vertex_fetch=true;
    std::vector<std::string> unsupported;
    const auto before=Shader::GenerateMsl(original,before_options,&unsupported);
    Require(unsupported.empty(),"Reference vertex translation unsupported");
    const auto after=Shader::GenerateMsl(original,after_options,&unsupported);
    Require(unsupported.empty() && Count(after,"if (dc_native_vertex_fetch(")==1 &&
        Count(after,"dc_swizzle(dc_load_fmt(mem, a_")==Native::Layout(original.hash)->attributes,
        "Native fetch or original fallback cluster missing");
    const auto first=before.find("    { ulong base_ = dc_vbase(s16, s17)");
    Require(first!=std::string::npos,"Original fetch source missing");
    size_t end=first;
    for(u32 k=0;k<Native::Layout(original.hash)->attributes;++k) {
        end=before.find("    }\n",end);Require(end!=std::string::npos,"Original typed fetch block missing");end+=6;
    }
    Require(after.find(before.substr(first,end-first))!=std::string::npos &&
        after.find(before.substr(end))!=std::string::npos,"Original fallback or subsequent transform math changed");
    auto readonly_options=after_options;readonly_options.readonly_guest_memory=true;
    Require(Shader::GuestMemoryReadOnlyEligible(original,&reason),"Native program readonly proof failed: "+reason);
    const auto readonly_source=Shader::GenerateMsl(original,readonly_options,&unsupported);
    Require(unsupported.empty() && readonly_source.find("device const uint* dc_vertex_record")!=std::string::npos &&
        readonly_source.find("device const uint* p=dc_vertex_record")!=std::string::npos &&
        readonly_source.find("device uint*")==std::string::npos && Count(readonly_source,"if (dc_native_vertex_fetch(")==1,
        "Native readonly helper retained writable inputs or lost its proven fusion");
    for(u32 kind=0;kind<32;++kind) {
        auto changed=original;auto& descriptor=changed.code[1];
        auto& fetch=changed.code[2+Native::Layout(original.hash)->attributes];
        switch(kind) {
        case 0:changed.hash^=1;break;case 1:changed.stage=Shader::Stage::Fragment;break;
        case 2:--changed.num_user_sgprs;break;case 3:--changed.vgpr_comp_cnt;break;
        case 4:changed.fetch_ud=2;break;case 5:changed.fetch_bytes-=4;break;
        case 6:changed.has_branches=true;break;case 7:changed.writes_exec=true;break;
        case 8:changed.labels.push_back(fetch.pc);break;case 9:changed.unsupported.push_back("fixture");break;
        case 10:++descriptor.dst;break;case 11:descriptor.sbase=4;break;case 12:descriptor.imm=false;break;
        case 13:++descriptor.smrd_offset;break;case 14:descriptor.pc+=4;break;
        case 15:fetch.vaddr=1;break;case 16:++fetch.vdata;break;case 17:fetch.srsrc+=4;break;
        case 18:fetch.soffset=Operand::Zero+1;break;case 19:fetch.offset=4;break;
        case 20:fetch.idxen=false;break;case 21:fetch.offen=true;break;case 22:fetch.addr64=true;break;
        case 23:fetch.glc=true;break;case 24:fetch.slc=true;break;case 25:fetch.tfe=true;break;
        case 26:fetch.lds=true;break;case 27:fetch.op=u16(Mubuf::BUFFER_STORE_FORMAT_XYZ);break;
        case 28:changed.code[0].dst=2;break;case 29:changed.code.back().enc=Enc::SOP1;
            changed.code.back().op=u16(Sop1::S_SETPC_B64);break;
        case 30:changed.code[0].literal^=1;break;
        case 31:changed.code[0].literal=Native::Layout(original.hash)->attributes==5 ?
            (original.code[0].literal==0x4e ? 0x52 : 0x4e) :
            (original.code[0].literal==0x57 ? 0x5b : 0x57);break;
        }
        Require(!Shader::VertexNativeFetchEligible(changed,&reason) && !reason.empty(),"Mutated fetch proof accepted or lacked diagnostic");
        if(kind!=9) {
            const auto fallback=Shader::GenerateMsl(changed,after_options,&unsupported);
            Require(fallback.find("struct DcVertexFetch")==std::string::npos,"Rejected fetch still emitted native helper");
        }
    }
    for(u32 kind=0;kind<4;++kind) {
        auto options=after_options;
        switch(kind) {case 0:options.rect_list=true;break;case 1:options.points=true;break;
            case 2:options.debug_capture=true;break;case 3:options.tess_stage=Shader::TessStage::Domain;break;}
        Require(Shader::GenerateMsl(original,options,&unsupported).find("struct DcVertexFetch")==std::string::npos,
            "Unsupported vertex wrapper still fused fetches");
    }
    std::printf("Vertex %016llx opcode/register/PC/fallback proof passed.\n",(unsigned long long)original.hash);
}
std::unique_ptr<Shader::Program> Capture(const std::filesystem::path& directory,u64 hash) {
    char suffix[32];std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path selected;
    for(const auto& e:std::filesystem::directory_iterator(directory)) {
        const auto name=e.path().filename().string();
        if(e.is_regular_file() && name.starts_with("vs_") && name.ends_with(suffix)) {selected=e.path();break;}
    }
    Require(!selected.empty(),std::string("Missing original vertex capture ")+suffix);
    const auto read=[](const std::filesystem::path& file) {
        const auto bytes=std::filesystem::file_size(file);
        Require(bytes>=16 && bytes<=65536 && !(bytes&3),"Invalid original capture length");
        std::vector<u32> words(bytes/4+64,0xbf810000u);std::ifstream stream(file,std::ios::binary);
        Require(bool(stream.read(reinterpret_cast<char*>(words.data()),bytes)),"Cannot read original capture");return words;
    };
    auto code=read(selected),fetch=read(std::filesystem::path(selected.string()+".fetch.bin"));
    std::array<u32,16> ud{};const auto pointer=reinterpret_cast<std::uintptr_t>(fetch.data());
    ud[0]=u32(pointer);ud[1]=u32(pointer>>32);
    Shader::BuildInput input{.stage=Shader::Stage::Vertex,.address=0,.user_data=ud.data(),
        .num_user_sgprs=16,.vgpr_comp_cnt=3,.code=code.data()};
    auto p=Shader::BuildProgram(input);Require(p && p->hash==hash,"Original vertex capture hash/metadata mismatch");return p;
}
id<MTLLibrary> Compile(id<MTLDevice> device,const std::string& source) {
    auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;options.preserveInvariance=YES;
    options.languageVersion=MTLLanguageVersion3_0;NSError* error=nil;
    auto text=[[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
    auto result=[device newLibraryWithSource:text options:options error:&error];
    Require(result!=nil,std::string("Native vertex GPU compile: ")+(error ? error.localizedDescription.UTF8String : "unknown"));
    return result;
}
id<MTLComputePipelineState> Pipeline(id<MTLDevice> device,id<MTLLibrary> library,NSString* name) {
    NSError* error=nil;auto result=[device newComputePipelineStateWithFunction:[library newFunctionWithName:name] error:&error];
    Require(result!=nil,std::string("Native vertex pipeline: ")+(error ? error.localizedDescription.UTF8String : "unknown"));return result;
}
std::string VertexCompute(const Shader::Program& p,bool native,bool readonly=false,bool direct_root=false) {
    Shader::MslOptions options;options.native_vertex_fetch=native;options.readonly_guest_memory=readonly;options.direct_vertex_root=direct_root;
    std::vector<std::string> unsupported;
    auto source=Shader::GenerateMsl(p,options,&unsupported);Require(unsupported.empty(),"Captured VS unsupported");
    const auto entry=source.find("\nvertex VsOut main0(");Require(entry!=std::string::npos,"Captured VS entry missing");
    source.resize(entry);
    const std::string pointer=readonly ? "device const uint*" : "device uint*";
    source+=std::string("\nkernel void main0(constant uint* ud [[buffer(0)]],")+pointer+" gm0 [[buffer(1)]],"+pointer+" gm1 [[buffer(2)]],"
        "device const DcMap& map [[buffer(3)]],constant uint2* ids [[buffer(4)]],device uint4* result [[buffer(5)]],"+
        (direct_root ? "constant ulong* dc_vs_roots [[buffer(13)]]," : "")+
        "uint tid [[thread_position_in_grid]]) { DcMem mem={gm0,gm1,&map}; VsOut o=dc_run(ids[tid].x,ids[tid].y,ud,mem"+
        (direct_root ? ",dc_vs_roots" : "")+");";
    const u32 vectors=1+std::popcount(p.param_mask);
    source+=" uint base=tid*"+std::to_string(vectors)+"u;result[base]=as_type<uint4>(o.pos);";
    u32 at=1;for(u32 k=0;k<32;++k) if(p.param_mask&(1u<<k))
        source+="result[base+"+std::to_string(at++)+"u]=as_type<uint4>(o.p"+std::to_string(k)+");";
    return source+="}\n";
}
struct Mapping {Shader::GpuMemMap header{};std::array<s32,MemoryBytes/PageBytes> pages{};};
struct alignas(16) RawInput {u64 table;u32 vertex_index,stride;};
struct alignas(16) Mutation {u64 table,base;u32 stride,attributes,kind,seed;};
using Descriptor=std::array<u32,4>;
struct Guest {
    id<MTLBuffer> memory,map;
    Mapping mapping{};
    std::array<u32,16> ud{};
    Guest(id<MTLDevice> device) {
        memory=[device newBufferWithLength:MemoryBytes options:MTLResourceStorageModeShared];
        map=[device newBufferWithLength:sizeof(Mapping) options:MTLResourceStorageModeShared];
        Require(memory && map,"Cannot allocate synthetic guest memory");Reset();
    }
    void Reset() {
        std::memset(memory.contents,0,MemoryBytes);mapping={};mapping.header={0,mapping.pages.size(),MemoryBytes,14,0,MemoryBytes};ud={};
    }
    void Write(u64 address,const void* data,size_t bytes) {
        const auto* source=static_cast<const u8*>(data);auto* destination=static_cast<u8*>(memory.contents);
        for(size_t k=0;k<bytes;++k) {
            const auto va=address+k,page=(va-mapping.header.base)>>mapping.header.shift;
            if(page>=mapping.header.pages || page>=mapping.pages.size() || mapping.pages[page]==Shader::GpuMemMap::kUnmapped) continue;
            const u64 physical=va+u64(s64(mapping.pages[page])*PageBytes);
            if(physical<MemoryBytes) destination[physical]=source[k];
        }
    }
    template<class T> void Put(u64 address,const T& value) {Write(address,&value,sizeof(value));}
    void Pointer(u32 slot,u64 value) {ud[slot]=u32(value);ud[slot+1]=u32(value>>32);}
    Descriptor Buffer(u64 base,u32 stride,u32 records,u32 format=0x20077fac) {
        return {u32(base),u32(base>>32)|(stride<<16),records,format};
    }
    void PackedRecords(u64 base,u32 stride,u32 seed) {
        for(u32 vertex=0;vertex<128;++vertex) {
            std::array<u32,9> words{};words[0]=std::bit_cast<u32>(.125f*vertex+.25f*seed);
            words[1]=std::bit_cast<u32>(.25f+.0625f*vertex);words[2]=std::bit_cast<u32>(.5f+.03125f*seed);
            words[3]=((vertex+seed)&1u)<<24|0x00d18f21;words[4]=0x00a7e341u^(seed<<8);
            words[5]=0xff7f0100u^(vertex*0x01010101u);words[6]=0xffff8000u+vertex;words[7]=0x7fff0000u+seed;
            words[8]=0x80007fffu^(vertex<<16);Write(base+vertex*stride,words.data(),stride);
        }
    }
    void Constants() {
        Pointer(6,0x50000);Put(0x50000,Buffer(0x60000,16,6));Put(0x50010,Buffer(0x70000,16,64));
        const auto u8desc=Buffer(0x78000,16,4),u12desc=Buffer(0x7a000,16,26);
        std::copy(u8desc.begin(),u8desc.end(),ud.begin()+8);std::copy(u12desc.begin(),u12desc.end(),ud.begin()+12);
        for(u32 bone=0;bone<2;++bone) for(u32 row=0;row<3;++row) {
            std::array<float,4> matrix{};matrix[row]=1;matrix[3]=float(bone)*.125f;Put(0x60000+(bone*3+row)*16,matrix);
        }
        for(u32 row=0;row<4;++row) {std::array<float,4> matrix{};matrix[row]=1;Put(0x70320+row*16,matrix);}
        Put(0x702dc,.0625f);Put(0x702ec,.125f);Put(0x702fc,.1875f);
        const std::array<float,4> uv{1,1,.125f,.25f};Put(0x78000,uv);Put(0x78030,uv);Put(0x7a030,uv);
        Put(0x7a050,u32(0));
    }
    void UploadMap() {std::memcpy(map.contents,&mapping,sizeof(mapping));}
};

std::string RawSource() {
    return std::string(Shader::kMslPrelude)+Native::Msl()+R"MSL(
struct RawInput {ulong table;uint vertex_index;uint stride;};
struct Mutation {ulong table;ulong base;uint stride;uint attributes;uint kind;uint seed;};
kernel void raw(constant RawInput& p [[buffer(0)]],device uint* lo [[buffer(1)]],device uint* hi [[buffer(2)]],
        device const DcMap& map [[buffer(3)]],device uint* result [[buffer(5)]]) {
    DcMem mem={lo,hi,&map};uint4 d[6];uint attributes=p.stride==32u ? 5u : 6u;
    for(uint k=0;k<6u;++k) {
        device uint* descriptor=k<attributes ? dc_ptr_n(mem,((p.table&0xfffffffffffful)+ulong(k*16u))&~3ul,16u) : nullptr;
        d[k]=descriptor ? uint4(descriptor[0],descriptor[1],descriptor[2],descriptor[3]) : uint4(0u);
    }
    uint4 reference[6];
    for(uint k=0;k<6u;++k) {
        reference[k]=uint4(0u);if(k>=attributes) continue;
        ulong address=dc_vbase(d[k].x,d[k].y)+ulong(p.vertex_index)*ulong(dc_vstride(d[k].y));
        if(p.vertex_index<d[k].z) reference[k]=dc_swizzle(dc_load_fmt(mem,address,(d[k].w>>15)&15u,(d[k].w>>12)&7u),d[k].w);
    }
    DcVertexFetch fetched;bool hit=dc_native_vertex_fetch(fetched,mem,p.vertex_index,p.stride,d[0],d[1],d[2],d[3],d[4],d[5]);
    uint4 optimized[6];
    optimized[0]=hit ? fetched.a0 : reference[0];optimized[1]=hit ? fetched.a1 : reference[1];
    optimized[2]=hit ? fetched.a2 : reference[2];optimized[3]=hit ? fetched.a3 : reference[3];
    optimized[4]=hit ? fetched.a4 : reference[4];optimized[5]=hit ? fetched.a5 : reference[5];
    result[0]=0u;result[25]=uint(hit);
    for(uint k=0;k<6u;++k) for(uint component=0;component<4u;++component) {
        result[1u+k*4u+component]=reference[k][component];result[26u+k*4u+component]=optimized[k][component];
    }
}
kernel void writer(constant Mutation& p [[buffer(0)]],device uint* lo [[buffer(1)]],device uint* hi [[buffer(2)]],
        device const DcMap& map [[buffer(3)]],uint tid [[thread_position_in_grid]]) {
    DcMem mem={lo,hi,&map};
    if(p.kind==0u && tid<128u) {
        ulong base=p.base+ulong(tid)*ulong(p.stride);
        dc_st(mem,base,as_type<uint>(.125f*float(tid)+.25f*float(p.seed)));
        dc_st(mem,base+4ul,as_type<uint>(.25f+.0625f*float(tid)));
        dc_st(mem,base+8ul,as_type<uint>(.5f+.03125f*float(p.seed)));
        dc_st(mem,base+12ul,(((tid+p.seed)&1u)<<24)|0x00d18f21u);
        dc_st(mem,base+16ul,0x00a7e341u^(p.seed<<8));
        dc_st(mem,base+20ul,0xff7f0100u^(tid*0x01010101u));
        dc_st(mem,base+24ul,0xffff8000u+tid);dc_st(mem,base+28ul,0x7fff0000u+p.seed);
        if(p.stride==36u) dc_st(mem,base+32ul,0x80007fffu^(tid<<16));
    }
    if(p.kind==1u && tid<p.attributes) {
        uint offsets[6]={0u,12u,16u,20u,24u,32u};
        dc_st(mem,(p.table&~3ul)+ulong(tid*16u),uint(p.base+ulong(offsets[tid])));
    }
    if(p.kind==2u && tid==0u) dc_st(mem,(p.table&~3ul)+60ul,0x20050fadu);
    if(p.kind==3u && tid==0u) dc_st(mem,(p.table&~3ul)+8ul,3u);
}
)MSL";
}
bool Nan32(u32 word) {return (word&0x7f800000u)==0x7f800000u && (word&0x007fffffu);}
void Equal(const std::vector<u32>& reference,const std::vector<u32>& optimized,const char* label,bool floating) {
    Require(reference.size()==optimized.size(),std::string(label)+" output length differs");
    for(size_t k=0;k<reference.size();++k)
        Require(reference[k]==optimized[k] || (floating && Nan32(reference[k]) && Nan32(optimized[k])),
            std::string(label)+" differs at word "+std::to_string(k));
}
struct Result {std::vector<u32> raw,vertex;bool hit=false;};
class Harness {
public:
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
    id<MTLComputePipelineState> raw,writer;
    std::array<id<MTLComputePipelineState>,2> vertex{};
    const Shader::Program& program;
    u32 vectors;
    bool direct_root=false;
    Harness(id<MTLDevice> d,const Shader::Program& p,bool readonly=false,bool readonly_native=false,bool root=false,bool root_native=false):device(d),program(p),vectors(1+std::popcount(p.param_mask)),direct_root(root) {
        queue=[device newCommandQueue];auto library=Compile(device,RawSource());
        raw=Pipeline(device,library,@"raw");writer=Pipeline(device,library,@"writer");
        if(readonly) Require(Shader::GuestMemoryReadOnlyEligible(p),"Full vertex readonly proof failed");
        for(u32 enabled=0;enabled<2;++enabled) {
            auto compiled=Compile(device,VertexCompute(p,root ? root_native : readonly ? readonly_native : enabled,readonly && enabled,root && enabled));
            if(!readonly) {vertex[enabled]=Pipeline(device,compiled,@"main0");continue;}
            auto descriptor=[MTLComputePipelineDescriptor new];descriptor.computeFunction=[compiled newFunctionWithName:@"main0"];
            NSError* error=nil;MTLComputePipelineReflection* reflection=nil;
            vertex[enabled]=[device newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionBindingInfo reflection:&reflection error:&error];
            Require(vertex[enabled]!=nil,"Readonly full vertex pipeline failed");
            unsigned inputs=0;
            for(id<MTLBinding> binding in reflection.bindings) if(binding.type==MTLBindingTypeBuffer && (binding.index==1 || binding.index==2)) {
                Require(binding.access==(enabled ? MTLBindingAccessReadOnly : MTLBindingAccessReadWrite),
                    "Actual full VS input reflection did not change RW to RO");++inputs;
            }
            Require(inputs==2,"Actual full VS did not reflect both guest inputs");
        }
    }
    void Bind(id<MTLComputeCommandEncoder> encoder,const Guest& guest) {
        [encoder setBuffer:guest.memory offset:0 atIndex:1];
        const auto split=guest.mapping.header.split;
        [encoder setBuffer:guest.memory offset:(split<MemoryBytes ? split : 0) atIndex:2];
        [encoder setBuffer:guest.map offset:0 atIndex:3];
    }
    Result Run(Guest& guest,const RawInput& input,bool expected,const Mutation* mutation=nullptr,bool write_mask=false,bool force_sentinel=false) {
        guest.UploadMap();
        auto raw_result=[device newBufferWithLength:RawWords*2*4 options:MTLResourceStorageModeShared];
        const u64 bytes=u64(Vertices)*vectors*16;
        auto vertices=[device newBufferWithLength:bytes*2 options:MTLResourceStorageModeShared];
        const std::array<u32,Vertices> indices{0,1,2,3,7,15,31,63,64,95,126,127,128,~0u,~0u-1,17};
        std::array<std::array<u32,2>,Vertices> ids{};
        for(u32 k=0;k<Vertices;++k) ids[k]={indices[k],k%3};
        auto command=[queue commandBuffer];
        if(mutation) {
            auto enc=[command computeCommandEncoder];[enc setComputePipelineState:writer];Bind(enc,guest);
            [enc setBytes:mutation length:sizeof(*mutation) atIndex:0];
            [enc dispatchThreads:MTLSizeMake(128,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];[enc endEncoding];
        }
        auto enc=[command computeCommandEncoder];[enc setComputePipelineState:raw];Bind(enc,guest);
        [enc setBytes:&input length:sizeof(input) atIndex:0];[enc setBuffer:raw_result offset:0 atIndex:5];
        [enc dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];[enc endEncoding];
        for(u32 enabled=0;enabled<2;++enabled) {
            enc=[command computeCommandEncoder];[enc setComputePipelineState:vertex[enabled]];Bind(enc,guest);
            [enc setBytes:guest.ud.data() length:64 atIndex:0];
            if(direct_root && enabled) {
                Gpu::Metal::StaticSmrd::AddressBlock roots;
                Gpu::Metal::DirectRootSpan::Span span;
                const auto bytes=u64(Native::Layout(program.hash)->attributes)*16;
                if(!force_sentinel && Gpu::Metal::DirectRootSpan::TranslateSpan(
                    Gpu::Metal::DirectRootSpan::SmrdRoot(guest.ud[2],guest.ud[3]),bytes,
                    guest.mapping.header,guest.mapping.pages,span))roots.roots[0]=span.encoded;
                [enc setBytes:&roots length:sizeof(roots) atIndex:13];
            }
            [enc setBytes:ids.data() length:sizeof(ids) atIndex:4];[enc setBuffer:vertices offset:enabled*bytes atIndex:5];
            [enc dispatchThreads:MTLSizeMake(Vertices,1,1) threadsPerThreadgroup:MTLSizeMake(16,1,1)];[enc endEncoding];
        }
        [command commit];[command waitUntilCompleted];
        Require(command.status==MTLCommandBufferStatusCompleted,std::string("Native vertex GPU execution failed: ")+
            (command.error ? command.error.localizedDescription.UTF8String : "unknown"));
        const auto* words=static_cast<const u32*>(raw_result.contents);Result result;
        result.hit=words[RawWords]!=0;
        Require(result.hit==expected,"Raw native fetch fast-path decision differs from scenario expectation");
        result.raw={words+1,words+RawWords};
        Equal(result.raw,{words+RawWords+1,words+RawWords*2},"Raw typed register decode",false);
        words=static_cast<const u32*>(vertices.contents);const size_t count=bytes/4;
        result.vertex={words,words+count};Equal(result.vertex,{words+count,words+count*2},"Original full vertex output",true);
        if(write_mask) for(u32 k=0;k<Vertices;++k) {
            Require(words[k*vectors*4+3]==0,"XYZ fetch changed the untouched v7 component");
            Require(words[k*vectors*4+6]==0 && words[k*vectors*4+7]==0,
                "XY fetch changed untouched v26/v27 components");
        }
        return result;
    }
};
void GpuCases(id<MTLDevice> device,const Shader::Program& program,bool synthetic,bool readonly=false,bool readonly_native=false,bool direct_root=false,bool root_native=false) {
    const auto* layout=Native::Layout(program.hash);Harness harness(device,program,readonly,readonly_native,direct_root,root_native);Guest guest(device);
    u32 cases=0,fast=0,fallback=0;bool record_writer=false,table_writer=false,format_writer=false,count_writer=false;
    const auto check=[&](const RawInput& input,bool expected,const Mutation* mutation=nullptr) {
        const auto result=harness.Run(guest,input,expected,mutation,synthetic);
        ++cases;fast+=result.hit;fallback+=!result.hit;return result;
    };
    const auto prepare=[&](u64 table,u64 records) {
        guest.Pointer(2,table);guest.Constants();guest.PackedRecords(records,layout->stride,1);
        guest.PackedRecords(Alternative,layout->stride,7);
        for(u32 k=0;k<layout->attributes;++k)
            guest.Put((table&0xffffffffffffull&~3ull)+k*16,guest.Buffer(records+Native::Offsets[k],layout->stride,128,Native::Formats[k]));
    };
    for(u32 root_alignment=0;root_alignment<4;++root_alignment) {
        guest.Reset();prepare(Table+root_alignment,Records);
        RawInput input{Table+root_alignment,3,layout->stride};
        const auto before=check(input,true);
        if(root_alignment==0) {
            u32 finite=0,nonzero=0;
            for(u32 word:before.vertex) if(std::isfinite(std::bit_cast<float>(word))) {
                ++finite;nonzero+=(word&0x7fffffffu)!=0;
            }
            Require(finite>before.vertex.size()*3/4 && nonzero>before.vertex.size()/8,
                "Synthetic guest inputs did not produce meaningful finite vertex output");
        }
        Mutation mutation{Table+root_alignment,Records,layout->stride,layout->attributes,0,5};
        const auto after_record=check(input,true,&mutation);
        record_writer|=before.raw!=after_record.raw && before.vertex!=after_record.vertex;
        mutation.base=Alternative;mutation.kind=1;
        const auto after_table=check(input,true,&mutation);
        table_writer|=after_record.raw!=after_table.raw && after_record.vertex!=after_table.vertex;
        mutation.kind=2;
        const auto after_format=check(input,false,&mutation);
        format_writer|=after_table.raw!=after_format.raw;
        if(direct_root) {
            mutation.kind=3;
            const auto after_count=check(input,false,&mutation);
            count_writer|=after_format.raw!=after_count.raw && after_format.vertex!=after_count.vertex;
        }
    }
    for(u32 attribute=0;attribute<layout->attributes;++attribute) for(u32 kind=0;kind<5;++kind) {
        guest.Reset();prepare(Table,Records);
        auto descriptor=guest.Buffer(Records+Native::Offsets[attribute],layout->stride,128,Native::Formats[attribute]);
        switch(kind) {
        case 0:descriptor[3]^=1;break; // nonidentity destination swizzle
        case 1:descriptor[3]^=1u<<15;break; // different packed data format
        case 2:descriptor[1]+=1u<<16;break; // different stride
        case 3:descriptor[0]+=4;break; // noninterleaved base
        case 4:descriptor[2]=3;break; // one attribute out of bounds
        }
        guest.Put(Table+attribute*16,descriptor);check({Table,3,layout->stride},false);
    }
    for(u32 kind=0;kind<19;++kind) {
        guest.Reset();u64 records=Records,table=Table;bool expected=false;u32 vertex=3;
        switch(kind) {
        case 0:records+=1;break;case 1:records+=2;break;case 2:records+=3;break;
        case 3:records=0x23ff0;vertex=0;break; // complete record crosses a guest page
        case 4:records=0x23ff0;vertex=0;guest.mapping.pages[9]=Shader::GpuMemMap::kUnmapped;break;
        case 5:records=0x23ff0;vertex=0;guest.mapping.pages[9]=2;break;
        case 6:guest.mapping.header.split=Records+16;vertex=0;break; // physical split inside one guest page
        case 7:guest.mapping.header.limit=Records+layout->stride-1;vertex=0;break;
        case 8:guest.mapping.pages[Records/PageBytes]=-s32(Records/PageBytes);expected=true;break; // physical zero
        case 9:guest.mapping.pages[Records/PageBytes]=8;expected=true;break; // second physical buffer below
        case 10:guest.mapping.pages[Table/PageBytes]=Shader::GpuMemMap::kUnmapped;break;
        case 11:vertex=128;break;case 12:vertex=~0u;break;
        case 13:guest.mapping.header.split=Records/2;expected=true;break;
        case 14:guest.mapping.header.shift=13;break;
        case 15:guest.mapping.header.pages=Records/PageBytes;break;
        case 16:guest.mapping.pages[Records/PageBytes]=-s32(Records/PageBytes)-1;break;
        case 17:guest.mapping.header.base=Records+PageBytes;break;
        case 18:table|=0xabcdull<<48;expected=true;break;
        }
        if(kind==9) guest.mapping.header.split=Records;
        prepare(table,records);check({table,vertex,layout->stride},expected);
    }
    if(direct_root)for(unsigned kind=0;kind<6;++kind) {
        guest.Reset();u64 table=Table;bool expected=true;
        switch(kind) {
        case 0:guest.mapping.pages[Table/PageBytes]=-s32(Table/PageBytes);break; // descriptor physical zero
        case 1:guest.mapping.header.split=Table/2;break; // descriptor in second physical buffer
        case 2:table=0x13fe0;break; // complete table crosses page
        case 3:table=0x13fe0;guest.mapping.pages[5]=Shader::GpuMemMap::kUnmapped;expected=false;break;
        case 4:table=0x13fe0;guest.mapping.pages[5]=1;break; // discontiguous mapped descriptors: sentinel
        case 5:table=0x13fe0;guest.mapping.header.split=0x14000;break; // split-crossing root: sentinel
        }
        prepare(table,Records);check({table,3,layout->stride},expected);
    }
    // Unknown descriptor flags and every byte-level UNORM/signed boundary are
    // tested independently of renderer captures. The flag mutation must fail
    // closed even though the original general decoder ignores those bits.
    guest.Reset();prepare(Table,Records);auto descriptor=guest.Buffer(Records,layout->stride,128,Native::Formats[0]);
    descriptor[1]|=0x1000;guest.Put(Table,descriptor);check({Table,3,layout->stride},false);
    guest.Reset();prepare(Table,Records);
    for(u32 vertex:{0u,1u,7u,31u,63u,95u,127u}) check({Table,vertex,layout->stride},true);
    Require(record_writer && table_writer && format_writer && (!direct_root || count_writer),"Ordered GPU record/table/format/count writes were not observed");
    Require(fast>=20 && fallback>=30,"Native vertex fixtures did not exercise both guarded paths");
    if(direct_root) {
        guest.Reset();prepare(Table,Records);
        const auto reference=harness.Run(guest,{Table,3,layout->stride},true,nullptr,synthetic);
        const auto sentinel=harness.Run(guest,{Table,3,layout->stride},true,nullptr,synthetic,true);
        Equal(reference.raw,sentinel.raw,"Valid-map forced virtual descriptor fallback",false);
        Equal(reference.vertex,sentinel.vertex,"Original full VS forced virtual fallback",true);
        puts("VS address-only root: original outputs, queued payload/base/format/OOB writes and valid-map sentinel fallback covered");
    }
    std::printf("Vertex %016llx %s GPU differential passed (%u cases, %u fast, %u fallback; ordered writers observed).\n",
        (unsigned long long)program.hash,synthetic ? "synthetic write-mask/raw" : "original full-output/raw",cases,fast,fallback);
    if(readonly)std::puts(readonly_native ?
        "Readonly native-fetch/full-VS raw outputs, queued record/descriptor/format writers and actual RW/RO reflection passed." :
        "Readonly original inline-fetch/full-VS raw outputs, queued record/descriptor/format writers and actual RW/RO reflection passed.");
}
void DirectRootProof(const Shader::Program& program) {
    Shader::MslOptions off,on,fused;on.direct_vertex_root=true;fused=on;fused.native_vertex_fetch=true;
    std::vector<std::string> unsupported;
    const auto baseline=Shader::GenerateMsl(program,off,&unsupported);
    Require(unsupported.empty(),"VS root baseline unsupported");
    const auto direct=Shader::GenerateMsl(program,on,&unsupported);
    Require(unsupported.empty(),"VS root variant unsupported");
    const auto attributes=Native::Layout(program.hash)->attributes;
    Require(Count(direct,"ulong root_ = dc_vs_roots[0u]")==attributes &&
        Count(direct,"constant ulong* dc_vs_roots [[buffer(13)]]")==1 &&
        Count(direct,"else p_ = dc_ptr_n(mem,")==attributes,"VS descriptor sites/sentinel fallback differ");
    Require(direct.find("device uint* gm0 [[buffer(1)]]")!=std::string::npos &&
        direct.find("device uint* gm1 [[buffer(2)]]")!=std::string::npos,"VS root changed guest pointer qualification");
    Require(Count(Shader::GenerateMsl(program,fused,&unsupported),"if (dc_native_vertex_fetch(")==1 &&
        unsupported.empty(),"VS address root did not compose with native typed fetch");
    on.readonly_guest_memory=true;
    Require(Shader::GenerateMsl(program,on,&unsupported).find("device uint* gm0 [[buffer(1)]]")!=std::string::npos,
        "VS root accidentally promised readonly/noalias guest inputs");
    for(unsigned kind=0;kind<5;++kind) {
        auto options=on;
        if(kind==0)options.rect_list=true;
        if(kind==1)options.points=true;
        if(kind==2)options.debug_capture=true;
        if(kind==3)options.probe_pc=4;
        if(kind==4)options.tess_stage=Shader::TessStage::Domain;
        Require(Shader::GenerateMsl(program,options,&unsupported).find("dc_vs_roots")==std::string::npos,
            "Unsupported VS wrapper admitted address root");
    }
    auto changed=program;changed.code[1].smrd_offset+=1;
    Require(Shader::GenerateMsl(changed,on,&unsupported).find("dc_vs_roots")==std::string::npos,
        "Changed original descriptor PC/opcode/offset shape admitted");
    Require(baseline==Shader::GenerateMsl(program,off,&unsupported),"VS root OFF changed source");
}
void DirectRootProtectionProof() {
    namespace R=Gpu::Metal::DirectRootRead;
    namespace S=Gpu::Metal::DirectRootSpan;
    struct Guards {
        Shader::GpuMemMap map{0,32,0x40000,14,0,0x80000};
        std::array<s32,32> entries{};S::Span span;
        bool context=true,read_only=true,predicate=true,cpu=true,callback=true,lease=true,physical=true;
        u64 generation=7,current_generation=7,command=10,writer_command=9;
        bool writer_unknown=false,writer_overlap=false;
        unsigned publications=0,generation_checks=0,change_generation_at=0;
        u64 observed_address=0,observed_bytes=0;
        bool Context()const{return context;}
        bool StagesReadOnly()const{return read_only;}
        bool HasCpuPredicate()const{return predicate;}
        bool GenerationStable(){if(++generation_checks==change_generation_at)++current_generation;return generation==current_generation;}
        bool Mapping(const R::Request& request){return S::TranslateSpan(request.address,request.bytes,map,entries,span);}
        bool PhysicalMatch(const R::Request& request)const {
            return physical && S::MatchBacking(span,request.encoded,span.selector,span.offset,0x40000,generation,current_generation);
        }
        bool CpuReady(const R::Request& request){observed_address=request.address;observed_bytes=request.bytes;return cpu;}
        bool CallbacksReady()const{return callback;}
        R::Reject WritersReady()const {
            if(writer_unknown)return R::Reject::GpuUnknown;
            if(writer_overlap && writer_command>=command)return R::Reject::GpuCurrentCommand;
            return R::Reject::None;
        }
        bool PublishRead(const R::Request&){if(!lease)return false;++publications;return true;}
    };
    for(const u64 bytes:{u64(80),u64(96)}) {
        Guards guard;S::Span span;
        Require(S::TranslateSpan(Table,bytes,guard.map,guard.entries,span),"VS request translation failed");
        const R::Request request{Table,bytes,span.encoded};
        Require(R::Protect(request,guard)==R::Reject::None && guard.publications==1 &&
            guard.observed_address==Table && guard.observed_bytes==bytes,"VS exact root lease not published");
        for(unsigned kind=0;kind<14;++kind) {
            guard={};auto changed=request;auto expected=R::Reject::None;
            switch(kind) {
            case 0:changed.encoded=S::Fallback;expected=R::Reject::Untranslated;break;
            case 1:changed.bytes=0;expected=R::Reject::Layout;break;
            case 2:guard.context=false;expected=R::Reject::Context;break;
            case 3:guard.read_only=false;expected=R::Reject::StageWrites;break;
            case 4:guard.predicate=false;expected=R::Reject::MissingGuard;break;
            case 5:++guard.current_generation;expected=R::Reject::MapGeneration;break;
            case 6:guard.entries[Table/PageBytes]=Shader::GpuMemMap::kUnmapped;expected=R::Reject::Mapping;break;
            case 7:changed.encoded^=2;expected=R::Reject::PhysicalMismatch;break;
            case 8:guard.physical=false;expected=R::Reject::PhysicalMismatch;break;
            case 9:guard.cpu=false;expected=R::Reject::CpuPending;break;
            case 10:guard.callback=false;expected=R::Reject::Callback;break;
            case 11:guard.writer_unknown=true;expected=R::Reject::GpuUnknown;break;
            case 12:guard.writer_overlap=true;guard.writer_command=guard.command;expected=R::Reject::GpuCurrentCommand;break;
            case 13:guard.lease=false;expected=R::Reject::Lease;break;
            }
            Require(R::Protect(changed,guard)==expected && !guard.publications,"Rejected VS request published a lease");
        }
        for(unsigned phase:{2u,3u}) {
            guard={};guard.change_generation_at=phase;
            Require(R::Protect(request,guard)==R::Reject::MapGeneration && !guard.publications,"Late VS generation change admitted");
        }
        guard={};guard.writer_overlap=true; // known producer on prior queue command
        Require(R::Protect(request,guard)==R::Reject::None,"Ordered prior-command descriptor producer rejected");
    }
}
void DirectRootMapProof() {
    namespace R=Gpu::Metal::DirectRootSpan;
    Shader::GpuMemMap map{};map.shift=14;map.pages=8;map.limit=8*PageBytes;map.split=4*PageBytes;
    std::array<s32,8> pages{};R::Span span;
    Require(R::SmrdRoot(3,0xabcd0000u)==0,"SMRD48-bit/dword semantics changed");
    Require(R::TranslateSpan(PageBytes,96,map,pages,span) && span.physical==PageBytes && span.selector==0,
        "Complete root span failed");
    Require(R::MatchBacking(span,span.encoded,0,PageBytes,map.split,7,7),"Backing proof failed");
    Require(!R::MatchBacking(span,span.encoded^2,0,PageBytes,map.split,7,7) &&
        !R::MatchBacking(span,span.encoded,1,PageBytes,map.split,7,7) &&
        !R::MatchBacking(span,span.encoded,0,PageBytes,map.split,7,8) &&
        !R::MatchBacking(span,span.encoded,0,PageBytes,PageBytes+95,7,7),"Invalid backing/generation accepted");
    pages[1]=-1;Require(R::TranslateSpan(PageBytes,80,map,pages,span) && !span.physical && !span.encoded,
        "Physical page zero rejected");
    pages[1]=-2;Require(!R::TranslateSpan(PageBytes,80,map,pages,span),"Negative physical underflow accepted");
    pages[1]=4;Require(R::TranslateSpan(PageBytes,80,map,pages,span) && span.selector==1 && span.offset==PageBytes,
        "Second pool buffer failed");pages[1]=0;
    Require(R::TranslateSpan(PageBytes-32,96,map,pages,span),"Contiguous crossed page rejected");
    pages[1]=Shader::GpuMemMap::kUnmapped;
    Require(!R::TranslateSpan(PageBytes-32,96,map,pages,span),"Mapped endpoints/gap accepted");pages[1]=0;
    pages[2]=Shader::GpuMemMap::kUnmapped;
    Require(!R::TranslateSpan(PageBytes-8,2*PageBytes+16,map,pages,span),"Middle map hole accepted");pages[2]=0;
    Require(!R::TranslateSpan(map.split-32,96,map,pages,span),"Split-crossing root accepted");
    for(const auto bytes:std::array<u64,3>{0,3,R::MaximumBytes+4})Require(!R::TranslateSpan(0,bytes,map,pages,span),"Invalid root byte count accepted");
    Require(!R::TranslateSpan(R::AddressLimit-4,80,map,pages,span) &&
        !R::TranslateSpan(1,80,map,pages,span),"Root alignment/domain overflow accepted");
    auto invalid=map;invalid.shift=13;Require(!R::TranslateSpan(PageBytes,80,invalid,pages,span),"Invalid map shift accepted");
    invalid=map;invalid.pages=9;Require(!R::TranslateSpan(PageBytes,80,invalid,pages,span),"Short page table accepted");
    invalid=map;invalid.base=1;Require(!R::TranslateSpan(PageBytes,80,invalid,pages,span),"Unaligned map base accepted");
    puts("VS address root: complete map/physical backing/generation/sentinel metadata contracts passed (no source bytes)");
}
} // namespace

int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            for(const auto hash:Native::Hashes) {
                Require(Native::IsExtended(hash)==(hash!=Native::Hash32 && hash!=Native::Hash36),
                    "Extended vertex admission classification changed");
                Proof(Synthetic(hash));
            }
            Require(!Native::IsExtended(0) && !Native::Layout(0),"Unknown vertex identity admitted");
            if(argc>=2 && (std::string(argv[1])=="--cpu-direct-root" ||
                std::string(argv[1])=="--gpu-direct-root" || std::string(argv[1])=="--gpu-direct-root-native" ||
                std::string(argv[1])=="--gpu-direct-root-captures" || std::string(argv[1])=="--gpu-direct-root-native-captures")) {
                DirectRootMapProof();DirectRootProtectionProof();
                for(const auto hash:Native::Hashes)DirectRootProof(Synthetic(hash));
                if(std::string(argv[1])=="--cpu-direct-root"){Require(argc==2,"Unexpected CPU root args");return 0;}
                const bool native=std::string(argv[1]).find("native")!=std::string::npos;
                const bool captures=std::string(argv[1]).find("captures")!=std::string::npos;
                Require(argc==(captures ? 3 : 2),"Expected optional original capture directory only in capture modes");
                auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
                if(argc==2)for(const auto hash:Native::Hashes)GpuCases(device,Synthetic(hash),true,false,false,true,native);
                else {
                    Require(argc==3,"Expected original capture directory");
                    for(const auto hash:Native::Hashes) {
                        const auto program=Capture(argv[2],hash);DirectRootProof(*program);
                        GpuCases(device,*program,false,false,false,true,native);
                    }
                }
                return 0;
            }
            if(argc==2 && (std::string(argv[1])=="--gpu-readonly" || std::string(argv[1])=="--gpu-readonly-native")) {
                auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
                for(const auto hash:{Native::Hash32,Native::Hash36})GpuCases(device,Synthetic(hash),true,true,std::string(argv[1])=="--gpu-readonly-native");
                return 0;
            }
            if(argc==1) {
                auto device=MTLCreateSystemDefaultDevice();if(!device) return 77;
                for(const auto hash:Native::Hashes)GpuCases(device,Synthetic(hash),true);
                return 0;
            }
            Require(argc==3 && (std::string(argv[1])=="--cpu-captures" || std::string(argv[1])=="--gpu-captures" ||
                    std::string(argv[1])=="--cpu-readonly-captures" || std::string(argv[1])=="--gpu-readonly-captures" ||
                    std::string(argv[1])=="--gpu-readonly-native-captures"),
                "Use --cpu-captures/--gpu-captures/--cpu-readonly-captures/--gpu-readonly-captures DIR or --gpu-readonly/--gpu-readonly-native");
            std::vector<std::unique_ptr<Shader::Program>> programs;
            for(const auto hash:Native::Hashes) {programs.push_back(Capture(argv[2],hash));Proof(*programs.back());}
            const bool readonly=std::string(argv[1]).find("readonly")!=std::string::npos;
            if(readonly)for(const auto& p:programs)Require(Shader::GuestMemoryReadOnlyEligible(*p),"Original full VS readonly proof failed");
            if(std::string(argv[1])=="--cpu-captures" || std::string(argv[1])=="--cpu-readonly-captures") return 0;
            auto device=MTLCreateSystemDefaultDevice();if(!device) return 77;
            for(const auto& p:programs)GpuCases(device,*p,false,readonly,std::string(argv[1])=="--gpu-readonly-native-captures");
            return 0;
        } catch(const std::exception& error) {std::fprintf(stderr,"native vertex fixture: %s\n",error.what());return 1;}
    }
}
