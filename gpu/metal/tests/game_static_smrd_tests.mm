// SPDX-License-Identifier: GPL-2.0-or-later
// CPU snapshot contracts plus optional original-fragment GPU differential tests.
// Retail shader bytes are supplied at runtime; every uniform/texture is synthetic.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/metal/static_smrd.h"
#include <algorithm>
#include <array>
#include <bit>
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
namespace Static=Gpu::Metal::StaticSmrd;
using namespace Shader::Gcn;
constexpr u64 Hash68=0x68fcb859536adcd4ull,HashAff=0xaff190d9945a53e0ull;
constexpr u64 Arena=0x10000,Capacity=0x4000,Payload=0x20000,MemoryBytes=0x40000;
constexpr u32 Width=4,Height=4,Targets=6;
void Require(bool value,const std::string& what) {
    if(!value) throw std::runtime_error(what);
}
void SetPointer(std::array<u32,16>& ud,u32 root,u64 address) {
    ud[root]=u32(address);ud[root+1]=u32(address>>32);
}

void CpuPacking() {
    std::vector<std::byte> bytes(8192);
    for(size_t k=0;k<bytes.size();++k) bytes[k]=std::byte((k*37+13)&255);
    Static::Window window{Arena,Capacity,8192,0,0};
    std::array<Static::Snapshot,2> snapshots{{
        {Arena,4096,7,7,0,bytes.data()},
        {Arena+4096,4096,11,11,4096,bytes.data()+4096}}};
    for(const auto hash:{Hash68,HashAff}) for(u32 unaligned:{0u,1u,2u,3u,4u,255u}) {
        std::array<u32,16> ud{};
        SetPointer(ud,2,Arena+unaligned);SetPointer(ud,12,Arena+4096+unaligned);
        Static::Packed packed;
        Require(Static::Pack(hash,ud,snapshots,window,packed),"Valid prepared snapshot was rejected");
        Require(packed.count==(hash==Hash68 ? 84u : 104u),"Wrong static SMRD packed length");
        u32 at=0;
        for(const auto& load:Static::Layout(hash)) {
            const size_t offset=(load.root==2 ? 0 : 4096)+((unaligned+load.offset)&~3u);
            for(u32 k=0;k<load.words;++k) {
                u32 expected;std::memcpy(&expected,bytes.data()+offset+k*4,4);
                Require(packed.words[at++]==expected,"Packer changed an original scalar word");
            }
        }
    }
    std::array<u32,16> ud{};SetPointer(ud,2,Arena+896);SetPointer(ud,12,Arena);
    std::array<Static::Snapshot,1> shared{{{Arena,4096,7,7,0,bytes.data()}}};
    Static::Packed packed;
    Require(Static::Pack(HashAff,ud,shared,window,packed),"Shared snapshot roots rejected");
    const auto reject=[&](std::span<const Static::Snapshot> spans,Static::Window state,
                          const std::array<u32,16>& data,u64 hash,const char* label) {
        Static::Packed result;result.count=123;result.words.fill(0xdecafbad);
        Require(!Static::Pack(hash,data,spans,state,result),std::string(label)+" accepted");
        Require(!result.count && std::all_of(result.words.begin(),result.words.end(),[](u32 word){return !word;}),
                std::string(label)+" exposed partial packed data");
    };
    reject(shared,window,ud,Hash68^1,"Unknown shader");
    reject({},window,ud,Hash68,"Absent authoritative preparation");
    for(unsigned kind=0;kind<9;++kind) {
        auto altered=shared;
        switch(kind) {
        case 0:++altered[0].current_generation;break;
        case 1:altered[0].generation=altered[0].current_generation=0;break;
        case 2:altered[0].lease=Gpu::Metal::SnapshotArena::NoLease;break;
        case 3:++altered[0].address;break;
        case 4:altered[0].bytes=895;break;
        case 5:altered[0].data=nullptr;break;
        case 6:altered[0].bytes=Capacity+1;break;
        case 7:altered[0].lease=8192;break;
        case 8:altered[0].lease=Capacity;break;
        }
        reject(altered,window,ud,HashAff,"Invalid snapshot generation/lease/range");
    }
    for(unsigned kind=0;kind<7;++kind) {
        auto altered=window;
        switch(kind) {
        case 0:altered.pin=Gpu::Metal::SnapshotArena::NoLease;break;
        case 1:altered.pin=1;break;
        case 2:altered.epoch=256;break;
        case 3:altered.head=Capacity+4096;break;
        case 4:altered.capacity=0;break;
        case 5:++altered.capacity;break;
        case 6:altered.address=~u64(0)-128;break;
        }
        reject(shared,altered,ud,HashAff,"Invalid pinned arena window");
    }
    auto short_span=shared;short_span[0].bytes=1055;
    reject(short_span,window,ud,HashAff,"Final word crosses snapshot boundary");
    std::array<Static::Snapshot,2> duplicate{shared[0],shared[0]};
    reject(duplicate,window,ud,HashAff,"Ambiguous snapshot ownership");
    auto outside=ud;SetPointer(outside,12,Arena-4);
    reject(shared,window,outside,HashAff,"Unprepared root before snapshot");
    outside=ud;SetPointer(outside,12,Arena+4096);
    reject(shared,window,outside,HashAff,"Root beyond snapshot");
    outside=ud;outside[13]|=0x1000;
    reject(shared,window,outside,HashAff,"Unexpected upper pointer bits");
    // A new epoch must re-prepare the same source into a newly pinned lease.
    auto renewed=shared;renewed[0].lease=Capacity;renewed[0].current_generation=renewed[0].generation=12;
    Static::Window next{Arena,Capacity,Capacity+4096,Capacity,Capacity};
    Require(Static::Pack(HashAff,ud,renewed,next,packed),"New epoch lease could not be packed");
    std::puts("Static SMRD snapshot ownership/generation/lease/bounds regressions passed.");
}

std::unique_ptr<Shader::Program> Capture(const std::filesystem::path& directory,u64 hash) {
    char suffix[32];std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path file;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if(entry.is_regular_file() && name.starts_with("ps_") && name.ends_with(suffix)) {file=entry.path();break;}
    }
    Require(!file.empty(),std::string("Missing original fragment capture ")+suffix);
    const auto size=std::filesystem::file_size(file);
    Require(size>=8 && size<=65536 && !(size&3),"Invalid captured fragment length");
    std::vector<u32> code(size/4+64,0xbf810000u);
    std::ifstream stream(file,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(code.data()),size)),"Cannot read original fragment capture");
    std::array<u32,16> ud{};
    for(u32 sgprs=0;sgprs<=16;++sgprs) for(u32 components=0;components<4;++components) {
        Shader::BuildInput input{.stage=Shader::Stage::Fragment,.address=0,.user_data=ud.data(),
            .num_user_sgprs=sgprs,.vgpr_comp_cnt=components,.code=code.data()};
        auto result=Shader::BuildProgram(input);
        if(result && result->hash==hash) return result;
    }
    throw std::runtime_error("Original fragment capture hash/metadata mismatch");
}
size_t Count(const std::string& source,const char* word) {
    size_t count=0;for(size_t at=0;(at=source.find(word,at))!=std::string::npos;at+=std::strlen(word)) ++count;
    return count;
}
Shader::MslOptions Options(const Shader::Program& p,bool enabled) {
    Shader::MslOptions o;o.static_smrd=enabled;o.col_format=0x444444;
    o.vs_param_mask=63;o.ps_input_addr=0x1001;
    for(u32 k=0;k<32;++k) o.ps_input_cntl[k]=k;
    o.textures.resize(p.images.size());return o;
}
void CapturedProof(const Shader::Program& original) {
    Require(Shader::FragmentStaticSmrdEligible(original),"Original exact layout failed CFG proof");
    const auto layout=Static::Layout(original.hash);
    std::vector<std::string> unsupported;
    const auto before=Shader::GenerateMsl(original,Options(original,false),&unsupported);
    Require(unsupported.empty(),"Original shader contains unsupported operations");
    const auto after=Shader::GenerateMsl(original,Options(original,true),&unsupported);
    Require(unsupported.empty(),"Optimized shader contains unsupported operations");
    Require(Count(before,"device uint* p_ = dc_ptr_n")-Count(after,"device uint* p_ = dc_ptr_n")==layout.size(),
            "Unexpected number of lowered direct loads");
    Require(after.find("constant uint* dc_smrd [[buffer(13)]]")!=std::string::npos,"Static binding missing");
    const auto payload_lines=[](const std::string& source) {
        std::vector<std::string> result;
        for(size_t begin=0,end;begin<source.size();begin=end+1) {
            end=source.find('\n',begin);if(end==std::string::npos) end=source.size();
            const auto line=source.substr(begin,end-begin);
            if(line.find("dc_ptr_n(mem, (dc_vbase")!=std::string::npos) result.push_back(line);
        }
        return result;
    };
    Require(payload_lines(before)==payload_lines(after),"Nested mutable payload read changed");
    for(unsigned kind=0;kind<7;++kind) {
        auto changed=original;
        auto selected=std::find_if(changed.code.begin(),changed.code.end(),[&](const Inst& i){return i.pc==layout.front().pc;});
        switch(kind) {
        case 0:changed.hash^=1;break;
        case 1:changed.stage=Shader::Stage::Compute;break;
        case 2:selected->imm=false;break;
        case 3:++selected->sbase;break;
        case 4:++selected->dst;break;
        case 5:++selected->smrd_offset;break;
        case 6:changed.num_user_sgprs=13;break;
        }
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Changed original layout was accepted");
        const auto fallback=Shader::GenerateMsl(changed,Options(changed,true),&unsupported);
        Require(fallback.find("constant uint* dc_smrd")==std::string::npos,"Rejected layout still consumes buffer 13");
    }
    for(const auto reg:{2u,3u,12u,13u}) {
        auto changed=original;
        auto& clobber=changed.code.front();
        clobber.enc=Enc::SOP1;clobber.op=u16(Sop1::S_MOV_B32);clobber.dst=u16(reg);clobber.src[0]=Operand::Zero;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Clobbered original root was accepted");
    }
    auto changed=original;
    auto branch=std::find_if(changed.code.begin(),changed.code.end(),[](const Inst& i){return IsBranch(i);});
    if(branch!=changed.code.end()) {
        Require(original.hash==Hash68 && branch+1!=changed.code.end(),"Unexpected original branch layout");
        auto& arm=*(branch+1);arm.enc=Enc::SOP1;arm.op=u16(Sop1::S_MOV_B32);arm.dst=2;arm.src[0]=Operand::Zero;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Root clobber on one CFG arm was accepted at join");
        changed=original;
        branch=std::find_if(changed.code.begin(),changed.code.end(),[](const Inst& i){return IsBranch(i);});
        branch->simm16=-1;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Back edge was accepted");
    } else {
        // Aff implements its EXEC selection without a scalar branch. Its four
        // original-root clobber regressions above still exercise the proof.
        Require(original.hash==HashAff,"Expected original 68fc branch missing");
    }
    std::printf("Original %016llx direct SMRD layout/CFG/fallback proof passed.\n",(unsigned long long)original.hash);
}

id<MTLLibrary> Compile(id<MTLDevice> device,const std::string& source,const char* label) {
    auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    NSError* error=nil;
    auto text=[[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
    auto library=[device newLibraryWithSource:text options:options error:&error];
    Require(library!=nil,std::string(label)+": "+(error ? error.localizedDescription.UTF8String : "Metal compile failed"));
    return library;
}

std::vector<u16> Render(id<MTLDevice> device,id<MTLCommandQueue> queue,
        id<MTLRenderPipelineState> pipeline,id<MTLBuffer> memory,id<MTLBuffer> map,
        const std::array<u32,16>& ud,const Static::Packed& packed,
        const Shader::Program& program,id<MTLTexture> texture,id<MTLSamplerState> sampler,bool reversed) {
    std::array<id<MTLTexture>,Targets> targets;
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
    for(u32 k=0;k<Targets;++k) {
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:Width height:Height mipmapped:NO];
        descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageRenderTarget;
        targets[k]=[device newTextureWithDescriptor:descriptor];
        pass.colorAttachments[k].texture=targets[k];pass.colorAttachments[k].loadAction=MTLLoadActionClear;
        pass.colorAttachments[k].storeAction=MTLStoreActionStore;
        pass.colorAttachments[k].clearColor=MTLClearColorMake(.125,.125,.125,.125);
    }
    const u64 row=Width*8,plane=row*Height;
    auto output=[device newBufferWithLength:plane*Targets options:MTLResourceStorageModeShared];
    auto command=[queue commandBuffer];auto enc=[command renderCommandEncoderWithDescriptor:pass];
    const MTLViewport viewport{0,0,Width,Height,0,1};
    [enc setRenderPipelineState:pipeline];[enc setViewport:viewport];
    [enc setFrontFacingWinding:reversed ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [enc setFragmentBytes:ud.data() length:64 atIndex:0];
    [enc setFragmentBuffer:memory offset:0 atIndex:1];[enc setFragmentBuffer:memory offset:0 atIndex:2];
    [enc setFragmentBuffer:map offset:0 atIndex:3];
    [enc setFragmentBytes:packed.words.data() length:packed.count*4 atIndex:Shader::Binding::StaticSmrd];
    for(u32 k=0;k<program.images.size();++k) [enc setFragmentTexture:texture atIndex:k];
    for(u32 k=0;k<program.samplers.size();++k) [enc setFragmentSamplerState:sampler atIndex:k];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[enc endEncoding];
    auto blit=[command blitCommandEncoder];
    for(u32 k=0;k<Targets;++k) [blit copyFromTexture:targets[k] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(Width,Height,1) toBuffer:output destinationOffset:plane*k
        destinationBytesPerRow:row destinationBytesPerImage:plane];
    [blit endEncoding];[command commit];[command waitUntilCompleted];
    Require(command.status==MTLCommandBufferStatusCompleted,"Fragment differential GPU execution failed");
    const auto* words=static_cast<const u16*>(output.contents);
    return {words,words+plane*Targets/2};
}
bool Nan(u16 bits) {return (bits&0x7c00u)==0x7c00u && (bits&0x3ffu);}
void ComparePixels(const std::vector<u16>& reference,const std::vector<u16>& optimized) {
    Require(reference.size()==optimized.size(),"Differential pixel size differs");
    u32 finite=0,nonzero=0,changed_from_clear=0;
    for(size_t k=0;k<reference.size();++k) {
        Require(reference[k]==optimized[k] || (Nan(reference[k]) && Nan(optimized[k])),
                "Original/static SMRD fragment output differs at half word "+std::to_string(k));
        if((reference[k]&0x7c00u)!=0x7c00u) {
            ++finite;nonzero+=(reference[k]&0x7fffu)!=0;changed_from_clear+=reference[k]!=0x3000;
        }
    }
    Require(finite>reference.size()/4 && nonzero>16 && changed_from_clear>16,
            "Synthetic fragment inputs did not produce meaningful finite output");
}

void GpuDifferential(id<MTLDevice> device,const Shader::Program& program) {
    const std::string vertex=R"MSL(#include <metal_stdlib>
using namespace metal;
struct VOut {float4 pos [[position]];float4 p0 [[user(p0)]];float4 p1 [[user(p1)]];
float4 p2 [[user(p2)]];float4 p3 [[user(p3)]];float4 p4 [[user(p4)]];float4 p5 [[user(p5)]];};
vertex VOut vmain(uint vid [[vertex_id]]) {
 float2 q[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};VOut o;
 o.pos=float4(q[vid],.5,1);o.p0=float4(.3,.4,.8,1);o.p1=float4(.2,.5,.7,1);
 o.p2=float4(.4,.1,.9,1);o.p3=float4(.35,.65,.5,1);o.p4=float4(.1,.2,.3,1);o.p5=float4(.6,.4,.2,1);return o;
}
kernel void writer(device uint* memory [[buffer(0)]],constant uint& value [[buffer(1)]],uint tid [[thread_position_in_grid]]) {
 if(tid<0x10000u/4u) memory[0x20000u/4u+tid]=value;
})MSL";
    auto support=Compile(device,vertex,"fixture vertex/writer");auto vs=[support newFunctionWithName:@"vmain"];
    auto writer_function=[support newFunctionWithName:@"writer"];
    NSError* error=nil;auto writer=[device newComputePipelineStateWithFunction:writer_function error:&error];
    Require(writer!=nil,"Cannot compile ordered GPU payload writer");
    std::array<id<MTLRenderPipelineState>,2> pipelines;
    for(unsigned enabled=0;enabled<2;++enabled) {
        std::vector<std::string> unsupported;
        const auto source=Shader::GenerateMsl(program,Options(program,enabled),&unsupported);
        Require(unsupported.empty(),"Captured fragment translation unsupported");
        auto library=Compile(device,source,"captured fragment");
        auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=vs;
        descriptor.fragmentFunction=[library newFunctionWithName:@"main0"];
        for(u32 k=0;k<Targets;++k) descriptor.colorAttachments[k].pixelFormat=MTLPixelFormatRGBA16Float;
        pipelines[enabled]=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        Require(pipelines[enabled]!=nil,std::string("Cannot create differential render pipeline: ")+
            (error ? error.localizedDescription.UTF8String : "unknown error"));
    }
    auto memory=[device newBufferWithLength:MemoryBytes options:MTLResourceStorageModeShared];
    struct Mapping {Shader::GpuMemMap header;std::array<s32,MemoryBytes/16384> pages;};
    Mapping mapping{};mapping.header={0,mapping.pages.size(),MemoryBytes,14,0,MemoryBytes};
    auto map=[device newBufferWithBytes:&mapping length:sizeof(mapping) options:MTLResourceStorageModeShared];
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:1 height:1 mipmapped:NO];descriptor.storageMode=MTLStorageModeShared;
    auto texture=[device newTextureWithDescriptor:descriptor];
    const u8 pixel[]{96,161,223,255};[texture replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:pixel bytesPerRow:4];
    auto sampler_descriptor=[MTLSamplerDescriptor new];sampler_descriptor.minFilter=MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter=MTLSamplerMinMagFilterLinear;
    auto sampler=[device newSamplerStateWithDescriptor:sampler_descriptor];auto queue=[device newCommandQueue];
    bool producer_observed=false;
    for(u32 scenario=0;scenario<8;++scenario) {
        auto* data=static_cast<std::byte*>(memory.contents);std::memset(data,0,MemoryBytes);
        auto* words=reinterpret_cast<u32*>(data);
        for(u32 at=0;at<MemoryBytes/4;++at) words[at]=std::bit_cast<u32>(.25f+scenario*.03125f);
        const u32 unaligned=scenario%4;
        std::array<u32,16> ud{};SetPointer(ud,2,Arena+unaligned);SetPointer(ud,12,Arena+4096+unaligned);
        for(u32 slot=0;slot<12;++slot) {
            const u64 base=Payload+slot*4096;
            const u32 resource[]{u32(base),u32(base>>32)|(16u<<16),256,0xfacu|(7u<<12)|(14u<<15)};
            std::memcpy(data+Arena+slot*16,resource,sizeof(resource));
        }
        std::array<Static::Snapshot,2> snapshots{{{Arena,4096,1,1,0,data+Arena},
            {Arena+4096,4096,2,2,4096,data+Arena+4096}}};
        Static::Packed packed;
        Require(Static::Pack(program.hash,ud,snapshots,{Arena,Capacity,8192,0,0},packed),"Fixture snapshot packing failed");
        const auto before=Render(device,queue,pipelines[0],memory,map,ud,packed,program,texture,sampler,scenario&1);
        ComparePixels(before,Render(device,queue,pipelines[1],memory,map,ud,packed,program,texture,sampler,scenario&1));
        // Pack first, then write mutable payload on the GPU. Descriptors and
        // pinned snapshots stay immutable; both PS variants must read new data.
        auto command=[queue commandBuffer];auto compute=[command computeCommandEncoder];
        [compute setComputePipelineState:writer];[compute setBuffer:memory offset:0 atIndex:0];
        const u32 value=std::bit_cast<u32>(.75f+scenario*.015625f);
        [compute setBytes:&value length:4 atIndex:1];
        [compute dispatchThreads:MTLSizeMake(0x10000/4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        [compute endEncoding];[command commit];
        // The same serial queue orders the writer before both subsequent draws.
        const auto after=Render(device,queue,pipelines[0],memory,map,ud,packed,program,texture,sampler,scenario&1);
        Require(command.status==MTLCommandBufferStatusCompleted,"Ordered GPU payload writer failed");
        ComparePixels(after,Render(device,queue,pipelines[1],memory,map,ud,packed,program,texture,sampler,scenario&1));
        for(size_t k=0;k<after.size();++k)
            if(before[k]!=after[k] && !Nan(before[k]) && !Nan(after[k])) producer_observed=true;
    }
    Require(producer_observed,"Synthetic differential never observed the ordered GPU payload writer");
    std::printf("Original %016llx full-fragment/GPU-producer differential passed (16 states).\n",
        (unsigned long long)program.hash);
}
} // namespace

int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            CpuPacking();
            if(argc==1) return 0;
            Require(argc==3 && (std::string(argv[1])=="--cpu-captures" ||
                std::string(argv[1])=="--gpu-captures"),"Use --cpu-captures DIR or --gpu-captures DIR");
            auto a=Capture(argv[2],Hash68),b=Capture(argv[2],HashAff);CapturedProof(*a);CapturedProof(*b);
            if(std::string(argv[1])=="--cpu-captures") return 0;
            auto device=MTLCreateSystemDefaultDevice();if(!device) return 77;
            GpuDifferential(device,*a);GpuDifferential(device,*b);return 0;
        } catch(const std::exception& error) {
            std::fprintf(stderr,"static SMRD fixture: %s\n",error.what());return 1;
        }
    }
}
