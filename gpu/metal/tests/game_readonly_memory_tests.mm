// SPDX-License-Identifier: GPL-2.0-or-later
// Public synthetic inputs only. Retail shader differentials are a separate
// --gpu-readonly-captures mode of game_static_smrd_tests.mm.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <CommonCrypto/CommonDigest.h>
#include "gpu/shader/msl.h"
#include "gpu/shader/msl_prelude.h"
#include "gpu/shader/msl_readonly_prelude.h"
#include "gpu/metal/native_vertex_fetch.h"
#include "gpu/metal/constant_memory_map.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() {std::abort();}
namespace {
namespace Shader=Gpu::Shader;
using namespace Shader::Gcn;
void Require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
std::string Digest(const char* text) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(text,static_cast<CC_LONG>(std::strlen(text)),digest);
    std::string result;
    for(auto b:digest) {result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}
    return result;
}
Shader::Program Synthetic(Shader::Stage stage) {
    Shader::Program p;p.stage=stage;p.num_user_sgprs=2;p.exports_position=stage==Shader::Stage::Vertex;
    p.mrt_mask=stage==Shader::Stage::Fragment ? 1 : 0;
    Inst scalar;scalar.enc=Enc::SMRD;scalar.op=u16(Smrd::S_LOAD_DWORDX4);
    scalar.sbase=0;scalar.dst=4;scalar.imm=true;
    Inst load;load.enc=Enc::MUBUF;load.op=u16(Mubuf::BUFFER_LOAD_DWORDX4);load.size=2;
    load.srsrc=4;load.vdata=0;load.soffset=Operand::Zero;load.idxen=stage==Shader::Stage::Vertex;
    Inst exp;exp.enc=Enc::EXP;exp.size=2;exp.en=15;exp.done=true;
    exp.target=stage==Shader::Stage::Vertex ? ExpTarget::Pos0 : 0;
    for(u32 k=0;k<4;++k) exp.vsrc[k]=k;
    Inst end;end.enc=Enc::SOPP;end.op=u16(Sopp::S_ENDPGM);
    p.code={scalar,load,exp,end};
    u32 pc=0;for(auto& i:p.code) {i.pc=pc;pc+=i.size*4;}
    return p;
}
Shader::MslOptions Options(Shader::Stage stage,bool readonly) {
    Shader::MslOptions o;o.readonly_guest_memory=readonly;
    if(stage==Shader::Stage::Fragment) {o.col_format=1;o.mrt_type[0]=Shader::OutType::Uint;}
    return o;
}
Shader::Program SyntheticInline() {
    auto p=Synthetic(Shader::Stage::Vertex);p.fetch_address=0x2000;p.fetch_ud=0;p.fetch_bytes=20;
    Inst prefix;prefix.enc=Enc::SOP1;prefix.op=u16(Sop1::S_MOV_B32);prefix.dst=Operand::VccHi;
    prefix.src[0]=Operand::Literal;prefix.literal=0x4e;prefix.size=2;
    auto descriptor=p.code[0];descriptor.pc=Shader::kFetchPcBase;
    auto load=p.code[1];load.pc=Shader::kFetchPcBase+4;load.op=u16(Mubuf::BUFFER_LOAD_FORMAT_XYZW);
    Inst wait;wait.enc=Enc::SOPP;wait.op=u16(Sopp::S_WAITCNT);wait.pc=Shader::kFetchPcBase+12;
    p.code={prefix,descriptor,load,wait,p.code[2],p.code[3]};return p;
}
void CpuProofs() {
    Require(std::strlen(Shader::kMslPrelude)==13143 && Digest(Shader::kMslPrelude)==
        "0150e2ed48e9a398657245c84eb896021cc58ddc68d2107b9561764ad393dcf7",
        "Default writable prelude bytes changed");
    Require(std::strlen(Gpu::Metal::NativeVertexFetch::Msl())==2909 &&
        Digest(Gpu::Metal::NativeVertexFetch::Msl())=="47738e22acc89c215547d077a00ae47cb8401d2350538859ab359d48d25fc922",
        "Default writable native-fetch helper bytes changed");
    Require(!std::strstr(Gpu::Metal::NativeVertexFetch::Msl(true),"device uint*") &&
        std::strstr(Gpu::Metal::NativeVertexFetch::Msl(true),"device const uint* dc_vertex_record"),
        "Readonly native-fetch helper retained writable access");
    Require(!std::strstr(Shader::kMslReadOnlyPrelude,"dc_st(") &&
        !std::strstr(Shader::kMslReadOnlyPrelude,"dc_store_fmt(") &&
        !std::strstr(Shader::kMslReadOnlyPrelude,"device uint*"),
        "Readonly prelude retained writable guest access");
    for(auto stage:{Shader::Stage::Vertex,Shader::Stage::Fragment}) {
        auto p=Synthetic(stage);std::string reason;
        Require(Shader::GuestMemoryReadOnlyEligible(p,&reason),"Synthetic readonly proof: "+reason);
        std::vector<std::string> unsupported;
        const auto control=Shader::GenerateMsl(p,Options(stage,false),&unsupported);
        const auto candidate=Shader::GenerateMsl(p,Options(stage,true),&unsupported);
        Require(unsupported.empty() && control!=candidate &&
            candidate.find("device const uint* gm0")!=std::string::npos &&
            candidate.find("device uint* gm0")==std::string::npos,"Generated readonly input qualification missing");
        for(unsigned kind=0;kind<13;++kind) {
            auto changed=p;auto& i=changed.code[1];
            switch(kind) {
            case 0:i.op=u16(Mubuf::BUFFER_STORE_DWORD);break;
            case 1:i.op=u16(Mubuf::BUFFER_ATOMIC_ADD);break;
            case 2:i.enc=Enc::MTBUF;i.op=u16(Mtbuf::TBUFFER_STORE_FORMAT_X);break;
            case 3:i.enc=Enc::MIMG;i.op=u16(Mimg::IMAGE_STORE);break;
            case 4:i.enc=Enc::MIMG;i.op=u16(Mimg::IMAGE_ATOMIC_ADD);break;
            case 5:i.enc=Enc::DS;i.op=u16(Ds::DS_WRITE_B32);break;
            case 6:i.enc=Enc::Unknown;break;
            case 7:changed.fetch_address=0x1000;break;
            case 8:changed.fetch_ud=2;break;
            case 9:changed.code.pop_back();break;
            case 10:++i.pc;break;
            case 11:changed.stage=Shader::Stage::Compute;break;
            case 12:i.enc=Enc::SOP1;i.op=u16(Sop1::S_BCNT0_I32_B64);break;
            }
            // Reflection is deliberately empty, so opcode checks carry this proof.
            changed.buffer_writes.clear();Require(!Shader::GuestMemoryReadOnlyEligible(changed),"Unreflected writer/unsupported context accepted");
        }
        for(unsigned kind=0;kind<10;++kind) {
            auto a=Options(stage,true),b=a;
            switch(kind) {
            case 0:a.tess_stage=Shader::TessStage::Domain;break;
            case 1:a.native_tess_layout=128;break;
            case 2:a.native_vertex_fetch=true;break;
            case 3:a.rect_list=true;break;
            case 4:a.points=true;break;
            case 5:a.debug_capture=true;break;
            case 6:a.probe_pc=0;break;
            case 7:a.static_smrd=true;break;
            case 8:a.direct_smrd=true;break;
            case 9:a.simd_smrd=true;break;
            }
            b=a;b.readonly_guest_memory=false;
            Require(Shader::GenerateMsl(p,a,&unsupported)==Shader::GenerateMsl(p,b,&unsupported),
                "Excluded context changed default generated source");
        }
    }
    auto fetched=SyntheticInline();std::string reason;
    Require(Shader::GuestMemoryReadOnlyEligible(fetched,&reason),"Bounded inline fetch rejected: "+reason);
    for(unsigned kind=0;kind<12;++kind) {
        auto changed=fetched;
        switch(kind) {
        case 0:changed.fetch_address=0;break;
        case 1:changed.fetch_ud=-1;break;
        case 2:changed.fetch_bytes-=4;break;
        case 3:changed.fetch_bytes+=4;break;
        case 4:++changed.code[1].pc;break;
        case 5:++changed.code[4].pc;break;
        case 6:changed.code[0].dst=2;break;
        case 7:changed.code[2].op=u16(Mubuf::BUFFER_STORE_FORMAT_XYZW);break;
        case 8:changed.code[3].op=u16(Sopp::S_BRANCH);changed.has_branches=true;break;
        case 9:changed.code[2].lds=true;break;
        case 10:changed.labels.push_back(Shader::kFetchPcBase);break;
        case 11:changed.code[2].pc=16;break;
        }
        Require(!Shader::GuestMemoryReadOnlyEligible(changed),"Malformed/side-effecting inline fetch accepted");
    }
    std::puts("Readonly independent opcode/context proof and exact default-prelude bytes passed.");
}
std::string WithFetchHelper(std::string source,bool enabled,bool readonly) {
    if(enabled) {
        // Synthetic identities do not admit native fetch. Include the actual
        // helper anyway so its additional map lookup is type-checked by Metal.
        const auto at=source.find("struct VsOut {");Require(at!=std::string::npos,"Missing synthetic vertex output");
        source.insert(at,Gpu::Metal::NativeVertexFetch::Msl(readonly));
    }
    return source;
}
void ConstantMapProofs() {
    namespace Map=Gpu::Metal::ConstantMemoryMap;
    Require(Map::Configured("1"),"Explicit constant-map switch rejected");
    for(const char* value:std::array<const char*,6>{nullptr,"","0","01","true","1x"})
        Require(!Map::Configured(value),"Invalid constant-map switch accepted");
    for(auto stage:{Shader::Stage::Vertex,Shader::Stage::Fragment})for(bool readonly:{false,true})for(bool fetch:{false,true}) {
        if(fetch && stage!=Shader::Stage::Vertex)continue;
        std::vector<std::string> unsupported;
        auto options=Options(stage,readonly);
        auto source=WithFetchHelper(Shader::GenerateMsl(fetch ? SyntheticInline() : Synthetic(stage),options,&unsupported),fetch,readonly);
        Require(unsupported.empty(),"Synthetic constant-map translation unsupported");
        const auto original=source;
        Require(Map::Apply(source),"Generated constant-map ABI rejected");
        Require(source.find("constant DcMap* map;")!=std::string::npos &&
            source.find("constant DcMap& gmap [[buffer(3)]]")!=std::string::npos &&
            source.find("((constant int*)(m.map + 1))")!=std::string::npos,
            "Mapping metadata did not consistently use constant access");
        const auto payload=readonly ? "device const uint* gm0" : "device uint* gm0";
        Require(source.find(payload)!=std::string::npos && original.find(payload)!=std::string::npos,
            "Constant-map mode changed guest payload qualification");
        Require(source.find("ulong p = va + ulong(long(e) << 14);")!=std::string::npos &&
            source.find("i >= m.map->pages")!=std::string::npos &&
            source.find("p >= m.map->limit")!=std::string::npos,
            "Constant-map mode lost mapping arithmetic or bounds");
        const auto transformed=source;
        Require(!Map::Apply(source) && source==transformed,"Repeated transform changed source");
        if(fetch)Require(source.find("((constant int*)(m.map+1))")!=std::string::npos,
            "Native vertex-fetch page lookup lost constant qualification");
        auto foreign_cast=original+"((device const int*)(unexpected_map+1))";
        const auto foreign_before=foreign_cast;
        Require(!Map::Apply(foreign_cast) && foreign_cast==foreign_before,"Unknown page lookup was modified");
        for(std::string_view marker:{"device const DcMap* map;",
                "device const DcMap& gmap [[buffer(3)]]","((device const int*)(m.map + 1))"}) {
            auto altered=original;altered.erase(altered.find(marker),marker.size());
            const auto before=altered;
            Require(!Map::Apply(altered) && altered==before,"Partial ABI was modified");
            altered=original;altered.append(marker);const auto duplicate=altered;
            Require(!Map::Apply(altered) && altered==duplicate,"Duplicate ABI was modified");
        }
    }
    std::puts("Constant-map generated ABI, unchanged payload/bounds and atomic fallback proofs passed.");
}
id<MTLLibrary> Compile(id<MTLDevice> device,const std::string& input,bool generated=false) {
    auto source=input;
    if(Gpu::Metal::ConstantMemoryMap::Configured(std::getenv("BB_METAL_CONSTANT_MEMORY_MAP"))) {
        const bool applied=Gpu::Metal::ConstantMemoryMap::Apply(source);
        Require(applied || !generated,"Actual generated shader rejected constant-map ABI");
    }
    auto options=[MTLCompileOptions new];options.languageVersion=MTLLanguageVersion3_0;
    options.mathMode=MTLMathModeSafe;options.preserveInvariance=YES;
    NSError* error=nil;auto library=[device newLibraryWithSource:@(source.c_str()) options:options error:&error];
    Require(library!=nil,error ? error.localizedDescription.UTF8String : "Metal compile failed");return library;
}
void Reflection(NSArray<id<MTLBinding>>* bindings,bool readonly) {
    unsigned found=0;
    for(id<MTLBinding> binding in bindings) if(binding.type==MTLBindingTypeBuffer &&
        (binding.index==1 || binding.index==2)) {
        Require(binding.access==(readonly ? MTLBindingAccessReadOnly : MTLBindingAccessReadWrite),
            "Actual guest binding reflection did not match RW/RO");++found;
    }
    Require(found==2,"Both guest-memory buffers must be reflected");
}
void GeneratedReflection(id<MTLDevice> device) {
    for(bool readonly:{false,true})for(bool fetch:{false,true}) {
        std::vector<std::string> unsupported;
        auto vertex_options=Options(Shader::Stage::Vertex,readonly);
        auto vertex=Compile(device,WithFetchHelper(Shader::GenerateMsl(SyntheticInline(),vertex_options,&unsupported),fetch,readonly),true);
        auto fragment=Compile(device,Shader::GenerateMsl(Synthetic(Shader::Stage::Fragment),Options(Shader::Stage::Fragment,readonly),&unsupported),true);
        auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=[vertex newFunctionWithName:@"main0"];
        descriptor.fragmentFunction=[fragment newFunctionWithName:@"main0"];
        descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA32Uint;
        NSError* error=nil;MTLRenderPipelineReflection* reflection=nil;
        auto pipeline=[device newRenderPipelineStateWithDescriptor:descriptor options:MTLPipelineOptionBindingInfo reflection:&reflection error:&error];
        Require(pipeline!=nil,error ? error.localizedDescription.UTF8String : "Generated pipeline failed");
        Reflection(reflection.vertexBindings,readonly);Reflection(reflection.fragmentBindings,readonly);
    }
    std::puts("Actual generated ordinary VS/PS both guest inputs reflect RW in control and RO in candidate.");
}
void RawGpu(id<MTLDevice> device) {
    constexpr u32 Bytes=0x20000,Outputs=6;
    const std::string body=R"MSL(
struct Query {ulong address;uint format;uint number;};
kernel void main0(device const Query* queries [[buffer(0)]], GUEST gm0 [[buffer(1)]],GUEST gm1 [[buffer(2)]],
 device const DcMap& gmap [[buffer(3)]],device uint* output [[buffer(4)]],uint tid [[thread_position_in_grid]]) {
 DcMem mem={gm0,gm1,&gmap};Query q=queries[tid];uint4 v=dc_load_fmt(mem,q.address,q.format,q.number);
 output[tid*6]=dc_ld(mem,q.address);output[tid*6+1]=dc_ld(mem,q.address+1ul);
 for(uint k=0;k<4;++k)output[tid*6+2+k]=v[k];
})MSL";
    std::array<id<MTLComputePipelineState>,2> readers;
    for(unsigned readonly=0;readonly<2;++readonly) {
        auto source=std::string(readonly ? Shader::kMslReadOnlyPrelude : Shader::kMslPrelude);
        source+="#define GUEST ";source+=readonly ? "device const uint*\n" : "device uint*\n";source+=body;
        auto library=Compile(device,source);NSError* error=nil;MTLComputePipelineReflection* reflection=nil;
        auto descriptor=[MTLComputePipelineDescriptor new];descriptor.computeFunction=[library newFunctionWithName:@"main0"];
        readers[readonly]=[device newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionBindingInfo reflection:&reflection error:&error];
        Require(readers[readonly]!=nil,"Cannot create raw helper reader");Reflection(reflection.bindings,readonly);
    }
    auto producer=Compile(device,R"MSL(#include <metal_stdlib>
using namespace metal;kernel void main0(device uint* memory [[buffer(0)]],constant uint& seed [[buffer(1)]],uint tid [[thread_position_in_grid]]) {
 memory[tid]=seed^(tid*0x9e3779b9u);
})MSL");
    NSError* error=nil;auto writer=[device newComputePipelineStateWithFunction:[producer newFunctionWithName:@"main0"] error:&error];
    Require(writer!=nil,"Cannot create raw helper writer");auto queue=[device newCommandQueue];
    auto memory=[device newBufferWithLength:Bytes options:MTLResourceStorageModeShared];
    struct Map {Shader::GpuMemMap header;std::array<s32,8> pages;};
    struct Query {u64 address;u32 format,number;};
    static_assert(sizeof(Query)==16);
    std::vector<Query> queries;
    for(u32 format=1;format<=14;++format)for(u32 number=0;number<8;++number)
        for(u64 at:{0x108000ull,0x10fffdull,0x10fff8ull,0x100000ull,0x120000ull,0xffffcull,0x11fff8ull,0x11fffdull})
            queries.push_back({at,format,number});
    auto input=[device newBufferWithBytes:queries.data() length:queries.size()*sizeof(Query) options:MTLResourceStorageModeShared];
    const size_t output_bytes=queries.size()*Outputs*4;
    std::array<id<MTLBuffer>,2> output;
    for(auto& buffer:output) buffer=[device newBufferWithLength:output_bytes options:MTLResourceStorageModeShared];
    for(u32 scenario=0;scenario<8;++scenario) {
        Map mapping{{0x100000,8,0x10000,14,0,Bytes},{}};mapping.pages.fill(-64);
        if(scenario==1)mapping.pages[4]=Shader::GpuMemMap::kUnmapped;
        if(scenario==2)mapping.pages[4]=-63;
        if(scenario==3)mapping.header.split=0x14000;
        if(scenario==4)mapping.header.limit=Bytes-8;
        if(scenario==5)mapping.header.split=0;
        if(scenario==6)mapping.pages[0]=Shader::GpuMemMap::kUnmapped;
        if(scenario==7)mapping.pages[7]=Shader::GpuMemMap::kUnmapped;
        auto map=[device newBufferWithBytes:&mapping length:sizeof(mapping) options:MTLResourceStorageModeShared];
        std::memset(memory.contents,0,Bytes);
        auto command=[queue commandBuffer];auto enc=[command computeCommandEncoder];
        [enc setComputePipelineState:writer];[enc setBuffer:memory offset:0 atIndex:0];
        const u32 seed=0x12345678u+scenario*0x10101u;[enc setBytes:&seed length:4 atIndex:1];
        [enc dispatchThreads:MTLSizeMake(Bytes/4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];[enc endEncoding];
        for(unsigned variant=0;variant<2;++variant) {
            enc=[command computeCommandEncoder];[enc setComputePipelineState:readers[variant]];
            [enc setBuffer:input offset:0 atIndex:0];[enc setBuffer:memory offset:0 atIndex:1];
            [enc setBuffer:memory offset:mapping.header.split atIndex:2];[enc setBuffer:map offset:0 atIndex:3];
            [enc setBuffer:output[variant] offset:0 atIndex:4];
            [enc dispatchThreads:MTLSizeMake(queries.size(),1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];[enc endEncoding];
        }
        [command commit];[command waitUntilCompleted];
        Require(command.status==MTLCommandBufferStatusCompleted,"Raw helper GPU command failed");
        Require(!std::memcmp(output[0].contents,output[1].contents,output_bytes),"Raw load/format/page/split/limit differential differs");
        const auto expected=seed^((0x8000u/4)*0x9e3779b9u);
        Require(static_cast<const u32*>(output[1].contents)[0]==expected,"Readonly reader missed queued nonuniform GPU producer");
    }
    std::printf("Readonly raw GPU differential: %zu format/number/address/map states, exact words including NaNs, RW/RO reflection and queued nonuniform producers passed.\n",queries.size()*8);
}
}
int main(int argc,char** argv) { @autoreleasepool {
    try {
        CpuProofs();ConstantMapProofs();if(argc==1)return 0;
        Require(argc==2 && !std::strcmp(argv[1],"--gpu"),"Use --gpu for actual Metal differential/reflection");
        auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
        GeneratedReflection(device);RawGpu(device);return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"readonly memory: %s\n",error.what());return 1;}
} }
