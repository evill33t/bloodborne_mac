// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_compiler.h"
#include "gpu/metal/constant_memory_map.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {
const char* Source=R"(
#include <metal_stdlib>
using namespace metal;
struct DcMap { ulong base; ulong pages; ulong split; uint shift; uint pad; ulong limit; };
struct DcMem { device uint* lo; device uint* hi; device const DcMap* map; };
kernel void main0(device uint* result [[buffer(0)]], device const DcMap& gmap [[buffer(3)]]) {
    DcMem m={nullptr,nullptr,&gmap};
    result[0]=uint(gmap.base)+uint(((device const int*)(m.map + 1))[0]);
})";
std::string Read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};
}
auto Recipes(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> files;
    for(const auto& entry:std::filesystem::recursive_directory_iterator(root))
        if(entry.path().extension()==".json")files.push_back(entry.path());
    return files;
}
void Ready(Gpu::Metal::ShaderCompiler& compiler,unsigned count) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(compiler.GetWarmupStats().queued && std::chrono::steady_clock::now()<until)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const auto stats=compiler.GetWarmupStats();assert(stats.loaded==count && stats.ready==count && !stats.queued);
}
auto Pipeline(Gpu::Metal::ShaderCompiler& compiler,const std::string& source) {
    auto d=[MTLComputePipelineDescriptor new];d.computeFunction=compiler.Function(1,source,0);
    assert(d.computeFunction);d.maxTotalThreadsPerThreadgroup=32;
    auto pipeline=compiler.ComputePipeline(d,nil);assert(pipeline);return pipeline;
}
void Execute(id<MTLDevice> device,id<MTLComputePipelineState> pipeline) {
    std::uint64_t map[6]={7,0,0,0,0,9};
    auto input=[device newBufferWithBytes:map length:sizeof(map) options:MTLResourceStorageModeShared];
    auto output=[device newBufferWithLength:4 options:MTLResourceStorageModeShared];
    *static_cast<unsigned*>(output.contents)=0xdeadbeef;
    auto command=[[device newCommandQueue] commandBuffer];auto encoder=[command computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];[encoder setBuffer:output offset:0 atIndex:0];[encoder setBuffer:input offset:0 atIndex:3];
    [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];[encoder endEncoding];
    [command commit];[command waitUntilCompleted];assert(command.status==MTLCommandBufferStatusCompleted);
    assert(*static_cast<unsigned*>(output.contents)==16);
}
}
int main() { @autoreleasepool {
    auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
    char directory[]="/tmp/bb-metal-variant-XXXXXX";assert(mkdtemp(directory));
    const std::filesystem::path root=directory;
    setenv("BB_DATA_DIR",directory,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");unsetenv("BB_METAL_SHADER_PREWARM");
    setenv("BB_METAL_CONSTANT_MEMORY_MAP","0",1);
    {Gpu::Metal::ShaderCompiler compiler(device,1);compiler.EnablePrewarming(true);Execute(device,Pipeline(compiler,Source));}
    const auto originals=Recipes(root);assert(originals.size()==1);
    const auto bytes=Read(originals[0]);const auto stamp=std::filesystem::last_write_time(originals[0]);
    std::string selected=Source;assert(Gpu::Metal::ConstantMemoryMap::Apply(selected));
    setenv("BB_METAL_CONSTANT_MEMORY_MAP","1",1);
    for(unsigned launch=0;launch<2;++launch) {
        Gpu::Metal::ShaderCompiler compiler(device,2);compiler.EnablePrewarming(true);Ready(compiler,1);
        Execute(device,Pipeline(compiler,selected));assert(compiler.GetWarmupStats().hits==1);
    }
    const auto derived=Recipes(root);assert(derived.size()==2);
    assert(derived[0].parent_path()!=derived[1].parent_path());
    assert(Read(originals[0])==bytes && std::filesystem::last_write_time(originals[0])==stamp);
    setenv("BB_METAL_CONSTANT_MEMORY_MAP","0",1);
    {Gpu::Metal::ShaderCompiler compiler(device,2);compiler.EnablePrewarming(true);Ready(compiler,1);
     Execute(device,Pipeline(compiler,Source));assert(compiler.GetWarmupStats().hits==1);}
    assert(Read(originals[0])==bytes);std::filesystem::remove_all(root);
    puts("Selected mapping variant: real GPU output, warm hits, source preservation, deduplication and OFF reuse passed");
}}
