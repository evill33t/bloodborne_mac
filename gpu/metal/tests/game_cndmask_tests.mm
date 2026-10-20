// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production GCN decoder/emitter and optional Metal execution.
// Synthetic shader bytes only; --capture accepts a private Dream VS dump.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() { std::abort(); }
namespace {
namespace Shader=Gpu::Shader;
using namespace Shader::Gcn;
void Require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
std::string Arm(std::string value,unsigned absolute,unsigned negative) {
    if(absolute)value="("+value+" & 0x7fffffffu)";
    if(negative)value="("+value+" ^ 0x80000000u)";
    return value;
}
std::unique_ptr<Shader::Program> Synthetic() {
    // Set the per-invocation predicate and move both data inputs to VGPRs.
    // Each legal VOP3 select exercises one ABS/NEG combination on both arms.
    std::vector<u32> words{
        0xbe800000u|(u32(Operand::VccLo)<<16)|(u32(Sop1::S_MOV_B64)<<8)|2u,
        0x7e000200u|(4u<<17),0x7e000200u|(5u<<17)|1u};
    for(unsigned absolute=0;absolute<4;++absolute)for(unsigned negative=0;negative<4;++negative) {
        const unsigned slot=absolute*4+negative;
        words.push_back(0xd0000000u|(u32(Vop::V_CNDMASK_B32)<<17)|(absolute<<8)|6u);
        words.push_back(u32(Operand::Vgpr0+4)|(u32(Operand::Vgpr0+5)<<9)|
            (u32(Operand::VccLo)<<18)|(negative<<29));
        words.push_back(0xf8000001u|((ExpTarget::Param0+slot)<<4));
        words.push_back(6u);
    }
    words.push_back(0xbf810000u);
    std::array<u32,16> ud{};
    Shader::BuildInput input{.stage=Shader::Stage::Vertex,.user_data=ud.data(),
        .num_user_sgprs=16,.code=words.data()};
    auto result=Shader::BuildProgram(input);Require(result!=nullptr,"Synthetic shader did not decode");
    return result;
}
void CpuProof(const Shader::Program& p) {
    unsigned selects=0;
    for(const auto& i:p.code)if(i.enc==Enc::VOP3 && i.op==u16(Vop::V_CNDMASK_B32)) {
        const auto absolute=selects/4,negative=selects%4;
        Require(i.vop3 && i.dst==6 && i.src[0]==Operand::Vgpr0+4 &&
            i.src[1]==Operand::Vgpr0+5 && i.src[2]==Operand::VccLo &&
            i.abs==absolute && i.neg==negative,"VOP3 data/modifier decoding changed");
        ++selects;
    }
    Require(selects==16 && p.param_mask==0xffff,"Synthetic modifier/export coverage incomplete");
    std::vector<std::string> unsupported;
    const auto source=Shader::GenerateMsl(p,{},&unsupported);
    Require(unsupported.empty(),"Synthetic CNDMASK translation unsupported");
    for(unsigned absolute=0;absolute<4;++absolute)for(unsigned negative=0;negative<4;++negative) {
        const auto expected="v6 = ((vcc_lo != 0u) ? "+Arm("v5",absolute&2,negative&2)+
            " : "+Arm("v4",absolute&1,negative&1)+");";
        Require(source.find(expected)!=std::string::npos,"Raw-bit CNDMASK arm modifiers missing");
    }
    std::puts("CNDMASK CPU: all 16 VOP3 ABS/NEG combinations decode and emit raw-bit selects.");
}
std::vector<u32> Read(const std::filesystem::path& path) {
    const auto bytes=std::filesystem::file_size(path);
    Require(bytes>=16 && bytes<=65536 && !(bytes&3),"Invalid private shader capture size");
    std::vector<u32> result(bytes/4+64,0xbf810000u);
    std::ifstream stream(path,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(result.data()),bytes)),"Cannot read private shader capture");
    return result;
}
void CaptureProof(const std::filesystem::path& path) {
    auto code=Read(path),fetch=Read(std::filesystem::path(path.string()+".fetch.bin"));
    std::array<u32,16> ud{};const auto pointer=reinterpret_cast<std::uintptr_t>(fetch.data());
    ud[0]=u32(pointer);ud[1]=u32(pointer>>32);
    Shader::BuildInput input{.stage=Shader::Stage::Vertex,.user_data=ud.data(),
        .num_user_sgprs=16,.vgpr_comp_cnt=3,.code=code.data()};
    const auto p=Shader::BuildProgram(input);
    Require(p && p->hash==0xe4abfca709b00ef3ull,"Private Dream VS/fetch hash mismatch");
    const auto found=std::find_if(p->code.begin(),p->code.end(),[](const auto& i){return i.pc==0x158;});
    Require(found!=p->code.end() && found->op==u16(Vop::V_CNDMASK_B32) && found->abs==0 && found->neg==2 &&
        found->src[0]==Operand::Vgpr0+5 && found->src[1]==Operand::Vgpr0+5,
        "Original Dream signed phase select changed");
    unsigned modified=0;
    for(const auto& i:p->code)if(i.enc==Enc::VOP3 && i.op==u16(Vop::V_CNDMASK_B32) && (i.abs || i.neg))++modified;
    std::vector<std::string> unsupported;const auto source=Shader::GenerateMsl(*p,{},&unsupported);
    Require(unsupported.empty() && source.find("v4 = ((vcc_lo != 0u) ? (v5 ^ 0x80000000u) : v5);")!=std::string::npos,
        "Original Dream phase select still loses NEG");
    std::printf("Private Dream VS e4abfca709b00ef3: %u modified CNDMASK instructions; PC0158 NEG preserved.\n",modified);
}
std::string ComputeSource(const Shader::Program& p) {
    std::vector<std::string> unsupported;auto source=Shader::GenerateMsl(p,{},&unsupported);
    Require(unsupported.empty(),"CNDMASK GPU translation unsupported");
    const auto entry=source.find("\nvertex VsOut main0(");Require(entry!=std::string::npos,"Generated vertex entry missing");
    source.resize(entry);
    // Only the fixture's input address space changes. The decoded instruction
    // body, including register setup, modifiers, select and exports, is intact.
    const auto inputs=source.find("constant uint* ud");Require(inputs!=std::string::npos,"Generated UD input missing");
    source.replace(inputs,std::strlen("constant uint* ud"),"thread const uint* ud");
    source+="\nkernel void fixture(constant uint4* inputs [[buffer(0)]],device uint* memory [[buffer(1)]],"
        "device const DcMap& map [[buffer(2)]],device uint* results [[buffer(3)]],uint tid [[thread_position_in_grid]]) {"
        "uint ud[16]={};ud[0]=inputs[tid].x;ud[1]=inputs[tid].y;ud[2]=inputs[tid].z;"
        "DcMem mem={memory,memory,&map};VsOut o=dc_run(0u,0u,ud,mem);";
    for(unsigned slot=0;slot<16;++slot)source+="results[tid*16u+"+std::to_string(slot)+"u]=as_type<uint>(o.p"+
        std::to_string(slot)+".x);";
    return source+="}\n";
}
void GpuProof(id<MTLDevice> device,const Shader::Program& p) {
    constexpr std::array<u32,20> values{0u,0x80000000u,0x3f800000u,0xbf800000u,0x3e800000u,0xbe800000u,
        1u,0x80000001u,0x007fffffu,0x807fffffu,0x7f800000u,0xff800000u,
        0x7fc12345u,0xffc12345u,0x7fa54321u,0xffa54321u,0x7fffffffu,0xffffffffu,0xdeadbeefu,0x55aa55aau};
    std::vector<std::array<u32,4>> cases;
    for(u32 a:values)for(u32 b:values)for(u32 predicate:{0u,0xffffffffu})cases.push_back({a,b,predicate,0u});
    const auto source=ComputeSource(p);
    auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;options.preserveInvariance=YES;
    options.languageVersion=MTLLanguageVersion3_0;
    auto text=[[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
    NSError* error=nil;auto library=[device newLibraryWithSource:text options:options error:&error];
    Require(library!=nil,std::string("CNDMASK Metal compile: ")+(error ? error.localizedDescription.UTF8String : "unknown"));
    auto pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"fixture"] error:&error];
    Require(pipeline!=nil,"Cannot create CNDMASK GPU pipeline");
    auto input=[device newBufferWithBytes:cases.data() length:cases.size()*sizeof(cases[0]) options:MTLResourceStorageModeShared];
    auto output=[device newBufferWithLength:cases.size()*16*4 options:MTLResourceStorageModeShared];
    const Shader::GpuMemMap empty_map{};
    auto memory=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];
    auto mapping=[device newBufferWithBytes:&empty_map length:sizeof(empty_map) options:MTLResourceStorageModeShared];
    Require(input && output && memory && mapping,"Cannot allocate CNDMASK GPU fixture buffers");
    std::memset(output.contents,0x3d,output.length);
    auto queue=[device newCommandQueue];auto command=[queue commandBuffer];auto compute=[command computeCommandEncoder];
    Require(queue && command && compute,"Cannot encode CNDMASK GPU fixture");
    [compute setComputePipelineState:pipeline];[compute setBuffer:input offset:0 atIndex:0];
    [compute setBuffer:memory offset:0 atIndex:1];[compute setBuffer:mapping offset:0 atIndex:2];
    [compute setBuffer:output offset:0 atIndex:3];
    [compute dispatchThreads:MTLSizeMake(cases.size(),1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];
    [compute endEncoding];[command commit];[command waitUntilCompleted];
    Require(!command.error,std::string("CNDMASK GPU execution: ")+(command.error ? command.error.localizedDescription.UTF8String : "unknown"));
    const auto* actual=static_cast<const u32*>(output.contents);
    for(size_t n=0;n<cases.size();++n)for(unsigned absolute=0;absolute<4;++absolute)for(unsigned negative=0;negative<4;++negative) {
        const unsigned arm=cases[n][2] ? 1 : 0,slot=absolute*4+negative;
        const u32 magnitude_mask=absolute&(1u<<arm) ? 0x7fffffffu : 0xffffffffu;
        const u32 sign=negative&(1u<<arm) ? 0x80000000u : 0;
        const u32 expected=(cases[n][arm]&magnitude_mask)^sign;
        if(actual[n*16+slot]!=expected) {
            std::fprintf(stderr,"CNDMASK case%zu abs%u neg%u arm%u input=%08x expected=%08x actual=%08x\n",
                n,absolute,negative,arm,cases[n][arm],expected,actual[n*16+slot]);
            throw std::runtime_error("CNDMASK GPU raw-bit mismatch");
        }
    }
    std::printf("CNDMASK GPU: %zu bit-exact results across both predicates, all ABS/NEG combinations, NaN payloads, infinities, denormals and signed zero.\n",cases.size()*16);
}
} // namespace
int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            bool gpu=false;std::filesystem::path capture;
            for(int k=1;k<argc;++k) {
                const std::string arg=argv[k];
                if(arg=="--gpu")gpu=true;
                else if(arg=="--capture" && k+1<argc)capture=argv[++k];
                else throw std::runtime_error("Use [--gpu] [--capture PRIVATE_VS.bin]");
            }
            const auto p=Synthetic();CpuProof(*p);
            if(!capture.empty())CaptureProof(capture);
            if(gpu) {auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;GpuProof(device,*p);}
            return 0;
        } catch(const std::exception& error) {std::fprintf(stderr,"CNDMASK fixture: %s\n",error.what());return 1;}
    }
}
