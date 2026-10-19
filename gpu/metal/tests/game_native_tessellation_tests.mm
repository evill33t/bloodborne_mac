// SPDX-License-Identifier: GPL-2.0-or-later
// Differential execution of captured canonical light-volume LS/HS programs.
// Proprietary shader bytes are supplied at runtime and are never embedded here.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/shader/msl_prelude.h"
#include "gpu/metal/native_tessellation.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
namespace Shader = Gpu::Shader;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
constexpr u32 kPatches = 8;
constexpr u32 kWords = Shader::kTessLdsWords;
constexpr u32 kGuardBytes = 256;
constexpr u32 kCanary = 0xdecafbad;
constexpr u32 kMemoryBytes = 1u << 20;
constexpr u32 kLocalTable = 0x1000, kHullTable = 0x2000, kHullUnused = 0x3000;
constexpr u32 kIndexBase = 0x4000;
struct Params { u32 layout, outer_factor, query_count, pad; };
struct Mapping {
    Shader::GpuMemMap header;
    std::array<std::int32_t, kMemoryBytes / 16384> pages;
};
static_assert(offsetof(Mapping,pages) == sizeof(Shader::GpuMemMap));

void Require(bool value, const std::string& what) {
    if (!value) throw std::runtime_error(what);
}

id<MTLComputePipelineState> Compile(id<MTLDevice> device,
                                  const std::string& source,
                                  const char* label) {
    NSError* error = nil;
    auto options = [MTLCompileOptions new];
    // Compare the same mathematical contract without introducing reassociation
    // differences between the translated reference and the native helper.
    options.mathMode = MTLMathModeSafe;
    NSString* text = [[NSString alloc] initWithBytes:source.data()
        length:source.size() encoding:NSUTF8StringEncoding];
    auto library = [device newLibraryWithSource:text options:options error:&error];
    if (!library) throw std::runtime_error(std::string(label) + ": " +
        (error ? error.localizedDescription.UTF8String : "Metal library failed"));
    auto function = [library newFunctionWithName:@"main0"];
    auto pipeline = function ? [device newComputePipelineStateWithFunction:function
        error:&error] : nil;
    if (!pipeline) throw std::runtime_error(std::string(label) + ": " +
        (error ? error.localizedDescription.UTF8String : "Metal pipeline failed"));
    return pipeline;
}

std::unique_ptr<Shader::Program> CapturedProgram(
        const std::filesystem::path& directory, u64 hash,u32 user_sgprs) {
    char suffix[32];
    std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path selected;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && name.starts_with("vs_") && name.ends_with(suffix)) {
            selected = entry.path();
            break;
        }
    }
    Require(!selected.empty(), std::string("Missing captured original shader ") + suffix);
    const auto bytes = std::filesystem::file_size(selected);
    Require(bytes >= 16 && bytes <= 65536 && !(bytes & 3), "Invalid captured shader size");
    // Padding protects the decoder's bounded look-ahead after the final word.
    std::vector<u32> code(bytes / 4 + 64, 0xbf810000u);
    std::ifstream input(selected,std::ios::binary);
    Require(bool(input.read(reinterpret_cast<char*>(code.data()),bytes)), "Cannot read shader capture");
    std::array<u32,16> user{};
    std::unique_ptr<Shader::Program> program;
    // The hash also includes system-input metadata. The capture filename binds
    // the expected result; do not silently substitute a different decode.
    for (u32 components = 0; components < 4; ++components) {
        Shader::BuildInput build{.stage=Shader::Stage::Vertex,.address=0,
            .user_data=user.data(),.num_user_sgprs=user_sgprs,
            .vgpr_comp_cnt=components,.code=code.data()};
        auto candidate = Shader::BuildProgram(build);
        if (candidate && candidate->hash == hash) { program=std::move(candidate); break; }
    }
    Require(bool(program), "Captured shader hash/system-input metadata mismatch");
    Require(program->images.empty(), "Unexpected sampled images in canonical LS/HS");
    return program;
}

id<MTLComputePipelineState> CapturedPipeline(id<MTLDevice> device,
        const std::filesystem::path& directory, u64 hash,
        u32 user_sgprs, Shader::TessStage stage) {
    const auto program=CapturedProgram(directory,hash,user_sgprs);
    Shader::MslOptions options;
    options.tess_stage = stage;
    std::vector<std::string> unsupported;
    const auto source = Shader::GenerateMsl(*program,options,&unsupported);
    Require(unsupported.empty(), "Original LS/HS contains an unsupported instruction");
    return Compile(device,source,"captured tessellation preparation");
}

std::int32_t Signed24(u32 word) {
    return static_cast<std::int32_t>(word << 8) >> 8;
}

u32 Product24(u32 a,u32 b) {
    return static_cast<u32>(std::int64_t(Signed24(a)) * Signed24(b));
}

std::vector<u32> Queries(u32 hull_base) {
    std::vector<u32> result;
    for (u32 base : {0u,4u,64u,128u,256u,1024u,60000u})
        for (u32 offset=0;offset<=144;offset+=4) result.push_back(base+offset);
    // Include unaligned word aliases and every sparse hull write/adjacent word.
    for (u32 offset=0;offset<=84;offset+=4) {
        result.push_back(hull_base+offset);
        result.push_back(hull_base+offset+1);
        result.push_back(hull_base+offset+3);
    }
    result.push_back(0xfffffffcu);
    result.push_back(0xffffffffu);
    std::sort(result.begin(),result.end());
    result.erase(std::unique(result.begin(),result.end()),result.end());
    return result;
}

void FillMemory(id<MTLBuffer> buffer, u32 profile, u32 index_mode,
        u32 multiplicand, u32 multiplier, std::array<u32,16>& local,
        std::array<u32,16>& hull, Shader::GpuTessConsts& tess) {
    auto* bytes=static_cast<std::uint8_t*>(buffer.contents);
    std::memset(bytes,0,buffer.length);
    auto* descriptors=reinterpret_cast<u32*>(bytes+kLocalTable);
    for (u32 descriptor=0;descriptor<9;++descriptor) {
        const u32 address=0x10000 + descriptor*0x4000;
        const u32 format=profile==1 ? 10 : profile==2 ? 12 : 14;
        const u32 number=profile==1 ? 0 : profile==2 ? 1 : profile==3 ? 4 : 7;
        const u32 stride=format==10 ? 4 : format==12 ? 8 : 16;
        const u32 swizzle=profile==3 ? (7u | (1u<<3) | (0u<<6) | (4u<<9)) : 0xfacu;
        descriptors[descriptor*4]=profile==4 && descriptor==7 ? 60u*16384 : address;
        descriptors[descriptor*4+1]=stride<<16;
        descriptors[descriptor*4+2]=profile==3 ? 3 : 12;
        descriptors[descriptor*4+3]=(format<<15)|(number<<12)|swizzle;
        auto* words=reinterpret_cast<u32*>(bytes+address);
        for (u32 i=0;i<256;++i) {
            // Finite values exercise normalized/signed/integer conversions and
            // descriptor component defaults without NaN comparison ambiguity.
            words[i]=profile==0 || profile==4
                ? 0x3e800000u + ((descriptor*131u+i*17u)&0x7ffffu)
                : 0x8091a2b3u ^ (descriptor*0x1020304u+i*0x13579u);
        }
    }
    auto* hs=reinterpret_cast<u32*>(bytes+kHullTable);
    hs[0]=multiplicand; hs[2]=multiplier;
    local.fill(0); local[2]=kLocalTable;
    hull.fill(0); hull[0]=kHullUnused; hull[4]=kHullTable;
    hull[5]=16u<<16; hull[6]=1; hull[7]=0x77fac;
    const std::array<u32,kPatches> indices{7,0,5,2,6,1,4,3};
    if (index_mode==1) {
        auto* target=reinterpret_cast<std::uint16_t*>(bytes+kIndexBase+2);
        for (u32 i=0;i<kPatches;++i) target[i]=indices[i];
        tess.index_size=2; tess.index_base=kIndexBase+2;
    } else if (index_mode==2) {
        std::memcpy(bytes+kIndexBase+4,indices.data(),sizeof(indices));
        tess.index_size=4; tess.index_base=kIndexBase+4;
    } else { tess.index_size=0; tess.index_base=0; }
}

void BindReference(id<MTLComputeCommandEncoder> encoder,
        id<MTLComputePipelineState> pipeline, const std::array<u32,16>& user,
        id<MTLBuffer> memory, const Mapping& mapping, id<MTLBuffer> lds,
        id<MTLBuffer> factors, const Shader::GpuTessConsts& params) {
    [encoder setComputePipelineState:pipeline];
    [encoder setBytes:user.data() length:sizeof(user) atIndex:0];
    [encoder setBuffer:memory offset:0 atIndex:1];
    [encoder setBuffer:memory offset:0 atIndex:2];
    [encoder setBytes:&mapping length:sizeof(mapping) atIndex:3];
    [encoder setBuffer:lds offset:kGuardBytes atIndex:8];
    [encoder setBuffer:factors offset:kGuardBytes atIndex:9];
    [encoder setBytes:&params length:sizeof(params) atIndex:10];
    [encoder dispatchThreads:MTLSizeMake(kPatches,1,1)
        threadsPerThreadgroup:MTLSizeMake(kPatches,1,1)];
}

void CheckCanaries(id<MTLBuffer> buffer) {
    const auto* words=static_cast<const u32*>(buffer.contents);
    const auto total=buffer.length/4;
    for (u32 i=0;i<kGuardBytes/4;++i)
        Require(words[i]==kCanary && words[total-1-i]==kCanary,
            "Reference shader changed an allocation canary");
}

void InitializeGuarded(id<MTLBuffer> buffer) {
    auto* words=static_cast<u32*>(buffer.contents);
    std::fill(words,words+buffer.length/4,0);
    std::fill(words,words+kGuardBytes/4,kCanary);
    std::fill(words+(buffer.length-kGuardBytes)/4,words+buffer.length/4,kCanary);
}

// Invoke the actual translated domain body from a compute entry. The reference
// consumes the original LS/HS buffer, while the changed version uses the same
// sparse initialization and LDS-load indirection as the shipping patch entry.
// This deliberately retains all generated arithmetic and output parameters.
id<MTLComputePipelineState> DomainPipeline(id<MTLDevice> device,
        const Shader::Program& program,u32 layout,u32 outer_factor) {
    Shader::MslOptions options;
    options.tess_stage=Shader::TessStage::Domain;
    options.native_tess_layout=layout;
    options.native_tess_factor=outer_factor;
    std::vector<std::string> unsupported;
    auto source=Shader::GenerateMsl(program,options,&unsupported);
    Require(unsupported.empty(),"Unsupported instruction in captured domain");
    const auto entry=source.find("[[patch(quad, 1)]] vertex VsOut main0");
    Require(entry!=std::string::npos,"Cannot isolate generated domain body");
    source.resize(entry);
    source+=R"MSL(
kernel void main0(constant uint* ud [[buffer(0)]], device uint* lo [[buffer(1)]],
    device uint* hi [[buffer(2)]], device const DcMap& map [[buffer(3)]],
    constant DcDraw& draw [[buffer(4)]], device uint* dense [[buffer(8)]],
    constant DcTess& params [[buffer(10)]], constant uint* local [[buffer(11)]],
    constant uint* hull [[buffer(12)]], device uint* output [[buffer(13)]],
    constant float2* coordinates [[buffer(14)]], uint sample [[thread_position_in_grid]]) {
    uint patch=sample/9u;
    if(patch>=params.patch_count)return;
    float2 uv=coordinates[sample%9u];
    DcMem mem={lo,hi,&map};
)MSL";
    if (layout) {
        source+="    DcNativeTess data; dc_native_tess_init(data,mem,local,hull,params,patch,"+
            std::to_string(layout)+"u,"+std::to_string(outer_factor)+"u);\n";
        source+="    VsOut value=dc_run(0u,0u,ud,mem,uv,patch,data);\n";
    } else source+="    VsOut value=dc_run(0u,0u,ud,mem,uv,patch,dense+patch*16384u);\n";
    const u32 fields=1+std::popcount(program.param_mask);
    source+="    value.pos=dc_viewport(value.pos,draw);\n    uint at=sample*"+
        std::to_string(fields*4)+"u;\n";
    const auto write=[&](const std::string& field) {
        source+="    {uint4 words=as_type<uint4>(value."+field+"); for(uint i=0;i<4u;++i)output[at+i]=words[i]; at+=4u;}\n";
    };
    write("pos");
    for(u32 i=0;i<32;++i) if(program.param_mask&(1u<<i))write("p"+std::to_string(i));
    source+="}\n";
    return Compile(device,source,"captured domain output equivalence");
}

void DomainUniforms(id<MTLBuffer> memory,u32 lds_base,std::array<u32,16>& user) {
    constexpr u32 constants=0x5000,metadata=0x6000;
    auto* bytes=static_cast<std::uint8_t*>(memory.contents);
    auto* values=reinterpret_cast<float*>(bytes+constants);
    for(u32 i=0;i<256;++i)values[i]=0.125f+float((i*17u)%31u)*0.01f;
    // Identity camera matrix and positive fog/fade divisors keep a substantial
    // set of outputs finite. These are synthetic inputs, not game constants.
    std::fill(values,values+16,0.0f);
    for(u32 i=0;i<4;++i)values[i*5]=1.0f;
    const std::array<float,4> camera{0,0,0,1},fade{50,40,2,3};
    std::memcpy(bytes+constants+0x160,camera.data(),sizeof(camera));
    std::memcpy(bytes+constants+0x170,fade.data(),sizeof(fade));
    auto* meta=reinterpret_cast<u32*>(bytes+metadata);
    std::fill(meta,meta+16,0u);
    meta[3]=lds_base; meta[6]=144;
    user.fill(0);
    user[4]=constants; user[5]=16u<<16; user[6]=256; user[7]=0x77fac;
    user[8]=metadata; user[9]=4u<<16; user[10]=16; user[11]=0x24004;
}

void BindDomain(id<MTLComputeCommandEncoder> encoder,id<MTLComputePipelineState> pipeline,
        const std::array<u32,16>& domain,const std::array<u32,16>& local,
        const std::array<u32,16>& hull,id<MTLBuffer> memory,const Mapping& mapping,
        id<MTLBuffer> lds,const Shader::GpuTessConsts& tess,
        const Shader::GpuDrawConsts& draw,id<MTLBuffer> output,id<MTLBuffer> coordinates) {
    [encoder setComputePipelineState:pipeline];
    [encoder setBytes:domain.data() length:sizeof(domain) atIndex:0];
    [encoder setBuffer:memory offset:0 atIndex:1];
    [encoder setBuffer:memory offset:0 atIndex:2];
    [encoder setBytes:&mapping length:sizeof(mapping) atIndex:3];
    [encoder setBytes:&draw length:sizeof(draw) atIndex:4];
    [encoder setBuffer:lds offset:kGuardBytes atIndex:8];
    [encoder setBytes:&tess length:sizeof(tess) atIndex:10];
    [encoder setBytes:local.data() length:sizeof(local) atIndex:11];
    [encoder setBytes:hull.data() length:sizeof(hull) atIndex:12];
    [encoder setBuffer:output offset:0 atIndex:13];
    [encoder setBuffer:coordinates offset:0 atIndex:14];
    [encoder dispatchThreads:MTLSizeMake(kPatches*9,1,1)
        threadsPerThreadgroup:MTLSizeMake(8,1,1)];
}

void CheckDomainOutputs(id<MTLDevice> device,id<MTLCommandQueue> queue,
        const std::filesystem::path& directory,
        const std::array<id<MTLComputePipelineState>,2>& local_pipelines,
        const std::array<id<MTLComputePipelineState>,2>& hull_pipelines,
        id<MTLBuffer> memory,const Mapping& mapping,id<MTLBuffer> lds,id<MTLBuffer> factors) {
    const std::array<u64,4> hashes{0xb9885bd6a84245b4ull,0x164fc97094ae66d7ull,
        0x59911a4cf68be57cull,0xc4b45ba023e435cfull};
    const std::array<float,18> uv{0,0, 1,0, 0,1, 1,1, .5f,.5f, .25f,.75f,
        .75f,.25f, .1f,.3f, .9f,.8f};
    auto coordinates=[device newBufferWithBytes:uv.data() length:sizeof(uv)
        options:MTLResourceStorageModeShared];
    auto reference=[device newBufferWithLength:32768 options:MTLResourceStorageModeShared];
    auto native=[device newBufferWithLength:32768 options:MTLResourceStorageModeShared];
    Require(coordinates && reference && native,"Domain fixture allocation failed");
    // Exercise the generated wrapper's homogeneous viewport transform too.
    const auto draw=Shader::MakeDrawConsts(0x43f,{960,960,-540,540,1,0},1920,1080);
    u64 comparisons=0,finite_components=0,nan_components=0;
    unsigned cases=0;
    for(const auto hash:hashes) {
        const auto program=CapturedProgram(directory,hash,12);
        auto dense_pipeline=DomainPipeline(device,*program,0,0);
        const u32 words=(1+std::popcount(program->param_mask))*4;
        u64 meaningful_positions=0;
        for(u32 layout=0;layout<2;++layout) for(u32 hull=0;hull<2;++hull) {
            auto fused_pipeline=DomainPipeline(device,*program,layout ? 144 : 128,hull ? 3 : 1);
            for(u32 index=0;index<3;++index) for(u32 domain_base:{0u,4u,128u}) {
                @autoreleasepool {
                    InitializeGuarded(lds); InitializeGuarded(factors);
                    Shader::GpuTessConsts tess{index ? 2u : 0u,kPatches,0,0,0};
                    std::array<u32,16> local_user,hull_user,domain_user;
                    // Positive finite float vertex streams and a nonoverlapping
                    // hull range isolate domain math from preparation edge cases.
                    FillMemory(memory,0,index,1,256,local_user,hull_user,tess);
                    DomainUniforms(memory,domain_base,domain_user);
                    std::memset(reference.contents,0xcd,reference.length);
                    std::memset(native.contents,0xab,native.length);
                    auto command=[queue commandBuffer];
                    auto preparation=[command computeCommandEncoder];
                    BindReference(preparation,local_pipelines[layout],local_user,memory,mapping,lds,factors,tess);
                    [preparation memoryBarrierWithScope:MTLBarrierScopeBuffers];
                    BindReference(preparation,hull_pipelines[hull],hull_user,memory,mapping,lds,factors,tess);
                    [preparation endEncoding];
                    auto dense=[command computeCommandEncoder];
                    BindDomain(dense,dense_pipeline,domain_user,local_user,hull_user,memory,mapping,
                        lds,tess,draw,reference,coordinates);
                    [dense endEncoding];
                    auto fused=[command computeCommandEncoder];
                    BindDomain(fused,fused_pipeline,domain_user,local_user,hull_user,memory,mapping,
                        lds,tess,draw,native,coordinates);
                    [fused endEncoding]; [command commit]; [command waitUntilCompleted];
                    Require(command.status==MTLCommandBufferStatusCompleted,
                        command.error ? command.error.localizedDescription.UTF8String : "Domain command failed");
                    CheckCanaries(lds); CheckCanaries(factors);
                    const auto* expected=static_cast<const u32*>(reference.contents);
                    const auto* actual=static_cast<const u32*>(native.contents);
                    for(u32 sample=0;sample<kPatches*9;++sample) {
                        bool finite_position=true,nonzero_position=false;
                        for(u32 component=0;component<words;++component) {
                            const auto a=expected[sample*words+component],b=actual[sample*words+component];
                            const float af=std::bit_cast<float>(a),bf=std::bit_cast<float>(b);
                            const bool both_nan=std::isnan(af) && std::isnan(bf);
                            if(a!=b && !both_nan) {
                                std::fprintf(stderr,"Domain mismatch hash=%016llx layout=%u hull=%u index=%u ldsbase=%u sample=%u component=%u expected=%#x actual=%#x\n",
                                    (unsigned long long)hash,layout ? 144u : 128u,hull,index,domain_base,
                                    sample,component,a,b);
                                throw std::runtime_error("Captured domain output changed");
                            }
                            ++comparisons;
                            if(std::isfinite(af))++finite_components;
                            if(both_nan)++nan_components;
                            if(component<4) {
                                finite_position=finite_position && std::isfinite(af);
                                if(component<3 && af!=0)nonzero_position=true;
                            }
                        }
                        if(finite_position && nonzero_position)++meaningful_positions;
                    }
                    ++cases;
                }
            }
        }
        Require(meaningful_positions>0,"Domain fixture produced no finite nonzero positions");
        std::printf("Domain %016llx: %llu finite nonzero clip positions compared\n",
            (unsigned long long)hash,(unsigned long long)meaningful_positions);
    }
    std::printf("Actual captured domain outputs: %u cases, %llu float components, %llu finite, %llu paired NaNs; finite/infinite bits exact, NaN payloads ignored\n",
        cases,(unsigned long long)comparisons,(unsigned long long)finite_components,
        (unsigned long long)nan_components);
}
} // namespace

int main(int argc,char** argv) {
    if (argc<2) {
        std::puts("Native tessellation equivalence requires an original shader capture directory");
        return 77;
    }
    @autoreleasepool {
        try {
            const std::filesystem::path directory=argv[1];
            Require(std::filesystem::is_directory(directory),"Shader capture directory does not exist");
            auto device=MTLCreateSystemDefaultDevice();
            if (!device) return 77;
            auto queue=[device newCommandQueue]; Require(bool(queue),"Metal queue unavailable");
            const std::array<u64,2> local_hashes{0x030976bf71b6a530ull,0x53b4731b549fb1f0ull};
            const std::array<u64,2> hull_hashes{0xf9f5fc457bc359fdull,0xf916abafa71dddf5ull};
            std::array<id<MTLComputePipelineState>,2> local_pipelines,hull_pipelines;
            for (u32 i=0;i<2;++i) {
                local_pipelines[i]=CapturedPipeline(device,directory,local_hashes[i],6,Shader::TessStage::Local);
                hull_pipelines[i]=CapturedPipeline(device,directory,hull_hashes[i],8,Shader::TessStage::Hull);
            }
            std::string source=Shader::kMslPrelude;
            source+=Gpu::Metal::NativeTessellationMsl();
            source+=R"MSL(
struct TestParams { uint layout, outer_factor, query_count, pad; };
kernel void main0(constant uint* local [[buffer(0)]], device uint* lo [[buffer(1)]],
    device uint* hi [[buffer(2)]], device const DcMap& map [[buffer(3)]],
    constant uint* hull [[buffer(4)]], constant TestParams& p [[buffer(5)]],
    constant DcTess& tess [[buffer(10)]], device uint* output [[buffer(11)]],
    constant uint* queries [[buffer(12)]], uint patch [[thread_position_in_grid]]) {
    if (patch>=tess.patch_count) return;
    DcMem memory={lo,hi,&map};
    DcNativeTess data;
    dc_native_tess_init(data,memory,local,hull,tess,patch,p.layout,p.outer_factor);
    for (uint i=0;i<p.query_count;++i)
        output[patch*p.query_count+i]=dc_native_tess_load(data,queries[i]);
}
)MSL";
            auto native=Compile(device,source,"native sparse tessellation");
            auto memory=[device newBufferWithLength:kMemoryBytes options:MTLResourceStorageModeShared];
            auto lds=[device newBufferWithLength:kPatches*kWords*4+2*kGuardBytes
                options:MTLResourceStorageModeShared];
            auto factors=[device newBufferWithLength:kPatches*12+2*kGuardBytes
                options:MTLResourceStorageModeShared];
            auto queries=[device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
            auto output=[device newBufferWithLength:kPatches*4096 options:MTLResourceStorageModeShared];
            Require(memory && lds && factors && queries && output,"Metal test allocation failed");
            Mapping mapping{{0,kMemoryBytes/16384,kMemoryBytes,14,0,kMemoryBytes},{}};
            mapping.pages[60]=Shader::GpuMemMap::kUnmapped;
            const std::array<std::array<u32,2>,8> hull_products{{
                {0,1},{1,4},{1,16},{0xffffffu,0xffff80u},
                {0x800000u,0x800000u},{1,256},{1,65440},{0xffffffu,16}}};
            unsigned cases=0,full_reference=0,sparse_reference=0;
            for (u32 layout=0;layout<2;++layout) for (u32 hull=0;hull<2;++hull)
            for (u32 index=0;index<3;++index) for (u32 base : {0u,5u,0xfffffffcu})
            for (u32 profile=0;profile<5;++profile) for (const auto& product:hull_products) {
                @autoreleasepool {
                    const u32 hull_base=Product24(product[0],product[1]);
                    // A translated negative base would perform an out-of-range
                    // GPU write. Compare its sparse semantics independently,
                    // rather than deliberately issuing an invalid Metal access.
                    const bool safe_hull=hull_base<=kWords*4-84;
                    const auto addresses=Queries(hull_base);
                    Require(addresses.size()*4<=queries.length,"Too many test queries");
                    std::memcpy(queries.contents,addresses.data(),addresses.size()*4);
                    InitializeGuarded(lds); InitializeGuarded(factors);
                    std::memset(output.contents,0xcd,output.length);
                    Shader::GpuTessConsts tess{base,kPatches,0,0,0};
                    std::array<u32,16> local_user,hull_user;
                    FillMemory(memory,profile,index,product[0],product[1],local_user,hull_user,tess);
                    const Params params{layout ? 144u : 128u,hull ? 3u : 1u,u32(addresses.size()),0};
                    auto command=[queue commandBuffer];
                    auto encoder=[command computeCommandEncoder];
                    BindReference(encoder,local_pipelines[layout],local_user,memory,mapping,lds,factors,tess);
                    if (safe_hull) {
                        [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
                        BindReference(encoder,hull_pipelines[hull],hull_user,memory,mapping,lds,factors,tess);
                    }
                    [encoder endEncoding];
                    auto test=[command computeCommandEncoder];
                    [test setComputePipelineState:native];
                    [test setBytes:local_user.data() length:sizeof(local_user) atIndex:0];
                    [test setBuffer:memory offset:0 atIndex:1];
                    [test setBuffer:memory offset:0 atIndex:2];
                    [test setBytes:&mapping length:sizeof(mapping) atIndex:3];
                    [test setBytes:hull_user.data() length:sizeof(hull_user) atIndex:4];
                    [test setBytes:&params length:sizeof(params) atIndex:5];
                    [test setBytes:&tess length:sizeof(tess) atIndex:10];
                    [test setBuffer:output offset:0 atIndex:11];
                    [test setBuffer:queries offset:0 atIndex:12];
                    [test dispatchThreads:MTLSizeMake(kPatches,1,1)
                        threadsPerThreadgroup:MTLSizeMake(kPatches,1,1)];
                    [test endEncoding]; [command commit]; [command waitUntilCompleted];
                    Require(command.status==MTLCommandBufferStatusCompleted,
                        command.error ? command.error.localizedDescription.UTF8String : "Metal command failed");
                    CheckCanaries(lds); CheckCanaries(factors);
                    const auto* reference=reinterpret_cast<const u32*>(static_cast<const std::uint8_t*>(lds.contents)+kGuardBytes);
                    const auto* actual=static_cast<const u32*>(output.contents);
                    if (safe_hull) {
                        const auto* halfs=reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(factors.contents)+kGuardBytes);
                        for (u32 patch=0;patch<kPatches;++patch) for (u32 factor=0;factor<6;++factor)
                            Require(halfs[patch*6+factor]==(hull && factor<4 ? 0x4200u : 0x3c00u),
                                "Original hull factors differ from the fixed-factor contract");
                    }
                    for (u32 patch=0;patch<kPatches;++patch) for (u32 q=0;q<addresses.size();++q) {
                        const u32 word=addresses[q]>>2;
                        u32 expected=word<kWords ? reference[patch*kWords+word] : 0;
                        if (!safe_hull) for (u32 factor=0;factor<6;++factor)
                            if (word==(u32(hull_base+factor*16)>>2))
                                expected=hull && factor<4 ? 0x40400000u : 0x3f800000u;
                        if (actual[patch*addresses.size()+q]!=expected) {
                            std::fprintf(stderr,"Mismatch layout=%u hull=%u index=%u base=%#x profile=%u hullbase=%#x patch=%u address=%#x expected=%#x actual=%#x\n",
                                params.layout,hull,index,base,profile,hull_base,patch,addresses[q],expected,
                                actual[patch*addresses.size()+q]);
                            return 1;
                        }
                    }
                    ++cases; safe_hull ? ++full_reference : ++sparse_reference;
                }
            }
            std::printf("Native tessellation: %u cases, %u original LS+HS GPU comparisons, %u original LS plus sparse hull oracles; indexed16/32, signed/overflow bases, formats/swizzles/OOB and allocation canaries passed\n",
                cases,full_reference,sparse_reference);
            CheckDomainOutputs(device,queue,directory,local_pipelines,hull_pipelines,
                memory,mapping,lds,factors);
            return 0;
        } catch (const std::exception& error) {
            std::fprintf(stderr,"Native tessellation fixture: %s\n",error.what());
            return 1;
        }
    }
}
