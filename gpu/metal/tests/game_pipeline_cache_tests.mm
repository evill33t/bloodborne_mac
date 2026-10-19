// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_compiler.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <future>
#include <string>
#include <vector>

namespace {
constexpr const char* kAreaCompute="#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }";
id<MTLComputePipelineState> AreaPipeline(Gpu::Metal::ShaderCompiler& compiler,unsigned group) {
    auto descriptor=[MTLComputePipelineDescriptor new];
    descriptor.computeFunction=compiler.Function(100,kAreaCompute,0);
    descriptor.maxTotalThreadsPerThreadgroup=group;
    NSError* error=nil;
    auto result=compiler.ComputePipeline(descriptor,&error);
    assert(result && !error);return result;
}
std::vector<std::filesystem::path> Recipes(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    for(const auto& file:std::filesystem::recursive_directory_iterator(directory))
        if(file.is_regular_file() && !file.is_symlink() && file.path().extension()==".json" &&
            file.path().filename().string().size()==69) result.push_back(file.path());
    return result;
}
std::uint64_t RecipeBytes(const std::filesystem::path& directory) {
    std::uint64_t total=0;
    for(const auto& file:Recipes(directory))total+=std::filesystem::file_size(file);
    return total;
}
std::filesystem::path RecipeRoot(const std::filesystem::path& directory,id<MTLDevice> device) {
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    char identity[128];
    std::snprintf(identity,sizeof(identity),"%llx-%ld.%ld.%ld",device.registryID,
        os.majorVersion,os.minorVersion,os.patchVersion);
    return directory/"metal-cache"/"prewarm-v1"/identity;
}
void FillOldRecipes(const std::filesystem::path& root,unsigned count,std::uint64_t bytes) {
    std::filesystem::create_directories(root);
    const auto now=std::filesystem::file_time_type::clock::now();
    for(unsigned i=0;i<count;++i) {
        char name[80];std::snprintf(name,sizeof(name),"%064x.json",i);
        const auto path=root/name;
        std::ofstream stream(path,std::ios::binary);
        stream.seekp(bytes-1);stream.put('x');stream.close();
        std::filesystem::last_write_time(path,now-std::chrono::seconds(count-i+60));
    }
}
}

// A real GPU behind a proxy that records archive requests. The production
// cache policy must not reach any archive API, even when environment variables
// request caching or an archive already exists on disk.
@interface BBArchiveSpyDevice : NSProxy {
    id<MTLDevice> underlying;
}
@property(nonatomic) unsigned archiveCalls;
+ (instancetype)wrapping:(id<MTLDevice>)device;
@end
@implementation BBArchiveSpyDevice
+ (instancetype)wrapping:(id<MTLDevice>)device {
    auto proxy=[BBArchiveSpyDevice alloc]; proxy->underlying=device; return proxy;
}
- (NSMethodSignature*)methodSignatureForSelector:(SEL)selector { return [(NSObject*)underlying methodSignatureForSelector:selector]; }
- (void)forwardInvocation:(NSInvocation*)invocation { [invocation invokeWithTarget:underlying]; }
- (id<MTLBinaryArchive>)newBinaryArchiveWithDescriptor:(MTLBinaryArchiveDescriptor*)descriptor error:(NSError**)error {
    (void)descriptor;
    ++_archiveCalls;
    if (error) *error=[NSError errorWithDomain:@"BBArchiveSpy" code:1 userInfo:nil];
    return nil;
}
@end

int main() {
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice(); if (!device) return 77;
        char directory[]="/tmp/bb-metal-cache-XXXXXX"; assert(mkdtemp(directory));
        setenv("BB_METAL_CACHE_DIR",directory,1); unsetenv("BB_METAL_PIPELINE_CACHE");
        using Compiler=Gpu::Metal::ShaderCompiler;
        auto compile=[](Compiler& compiler) {
            auto render=[MTLRenderPipelineDescriptor new];
            render.vertexFunction=compiler.Function(1,"#include <metal_stdlib>\nusing namespace metal; vertex float4 main0(uint i [[vertex_id]]) { return float4(float(i),0,0,1); }",0);
            render.fragmentFunction=compiler.Function(2,"#include <metal_stdlib>\nusing namespace metal; fragment float4 main0() { return float4(1); }",0);
            render.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
            render.colorAttachments[0].blendingEnabled=YES;
            render.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
            render.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
            render.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;
            render.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            render.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            render.inputPrimitiveTopology=MTLPrimitiveTopologyClassTriangle;
            NSError* error=nil;
            assert(compiler.RenderPipeline(render,&error) && !error);
            auto compute=[MTLComputePipelineDescriptor new];
            compute.computeFunction=compiler.Function(3,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            assert(compiler.ComputePipeline(compute,&error) && !error);
        };
        auto runtime=[&] {
            setenv("BB_METAL_PIPELINE_CACHE","1",1);
            auto spy=[BBArchiveSpyDevice wrapping:device];
            {
                Compiler compiler((id<MTLDevice>)spy,1);
                compile(compiler); compiler.FlushCache();
                const auto stats=compiler.GetCacheStats();
                assert(!stats.loaded && !stats.hits && !stats.misses && !stats.writes);
            }
            assert(spy.archiveCalls==0);
        };
        runtime(); assert(std::filesystem::is_empty(directory));
        auto run=[&](bool loaded,bool expect_hits) {
            Compiler compiler(device,1,Compiler::CacheMode::OfflineArchive);
            assert(compiler.GetCacheStats().loaded==loaded);
            compile(compiler);
            compiler.FlushCache();
            const auto stats=compiler.GetCacheStats();
            assert(expect_hits ? stats.hits==2 && stats.misses==0 : stats.misses==2 && stats.writes>0);
        };
        run(false,false); run(true,true);
        runtime(); // the live runtime does not even load an existing archive
        unsigned archives=0;
        for (const auto& entry:std::filesystem::directory_iterator(directory)) {
            assert(entry.path().extension()==".metalarc");
            std::ofstream(entry.path(),std::ios::binary|std::ios::trunc)<<"broken archive";
            ++archives;
        }
        assert(archives==1);
        run(false,false); run(true,true); // corruption is recovered atomically
        setenv("BB_METAL_PIPELINE_CACHE","0",1);
        {
            Compiler disabled(device,1,Compiler::CacheMode::OfflineArchive);
            assert(!disabled.GetCacheStats().loaded);
        }
        std::filesystem::remove_all(directory);
        // Source/descriptor prewarming works without binary archive calls.
        char warm_directory[]="/tmp/bb-metal-prewarm-XXXXXX";assert(mkdtemp(warm_directory));
        setenv("BB_DATA_DIR",warm_directory,1);unsetenv("BB_METAL_SHADER_PREWARM");
        setenv("BB_GAME_DIR",warm_directory,1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::is_empty(warm_directory)); // never cache inside the game dump
        const auto alias=std::filesystem::path(warm_directory)/"data-alias";
        std::filesystem::create_directory_symlink(warm_directory,alias);
        setenv("BB_DATA_DIR",alias.c_str(),1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::directory_iterator(warm_directory)!=std::filesystem::directory_iterator{});
        assert(std::distance(std::filesystem::directory_iterator(warm_directory),std::filesystem::directory_iterator{})==1);
        std::filesystem::remove(alias);unsetenv("BB_GAME_DIR");
        setenv("BB_DATA_DIR",warm_directory,1);setenv("BB_METAL_SHADER_PREWARM","0",1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::is_empty(warm_directory));unsetenv("BB_METAL_SHADER_PREWARM");
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();compile(compiler);
            assert(compiler.GetWarmupStats().loaded==0 && spy.archiveCalls==0);
            // A reused guest key with changed translation must compile new code.
            auto a=compiler.Function(99,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            auto b=compiler.Function(99,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(device uint* p [[buffer(0)]],uint i [[thread_position_in_grid]]) { p[i]=7; }",0);
            assert(a && b && a!=b);
        }
        unsigned recipe_count=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(warm_directory))
            if(entry.path().extension()==".json") ++recipe_count;
        assert(recipe_count==2);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(compiler.GetWarmupStats().ready<2 && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(compiler.GetWarmupStats().loaded==2 && compiler.GetWarmupStats().ready==2);
            compile(compiler);assert(compiler.GetWarmupStats().hits==2 && spy.archiveCalls==0);
            // A changed descriptor cannot reuse the original warm pipeline.
            auto changed=[MTLComputePipelineDescriptor new];
            changed.computeFunction=compiler.Function(3,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            changed.maxTotalThreadsPerThreadgroup=64;
            assert(compiler.ComputePipeline(changed,nil));assert(compiler.GetWarmupStats().hits==2);
        }
        // A route visiting several areas must learn beyond the original 64
        // recipes. Hold the background worker to exercise pending admission,
        // rather than merely compiling/writing one descriptor at a time.
        char areas_directory[]="/tmp/bb-metal-areas-XXXXXX";assert(mkdtemp(areas_directory));
        setenv("BB_DATA_DIR",areas_directory,1);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            std::promise<void> started,release;
            auto gate=release.get_future().share();
            compiler.Submit([&started,gate]{started.set_value();gate.wait();},true);
            started.get_future().wait();
            for(unsigned group=1;group<=72;++group) AreaPipeline(compiler,group);
            const auto stats=compiler.GetWarmupStats();
            assert(stats.pending==72 && stats.retained==72);
            assert(stats.pending+stats.queued<=Gpu::Metal::PipelinePrewarmLimits::Recipes);
            assert(stats.pending_bytes+stats.queued_bytes<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            assert(stats.retained_bytes<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            release.set_value();assert(spy.archiveCalls==0);
        }
        assert(Recipes(areas_directory).size()==72);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(compiler.GetWarmupStats().ready<72 && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto stats=compiler.GetWarmupStats();
            assert(stats.loaded==72 && stats.ready==72 && stats.retained==72);
            AreaPipeline(compiler,72);assert(compiler.GetWarmupStats().hits==1);
            assert(spy.archiveCalls==0);
        }
        std::filesystem::remove_all(areas_directory);
        // A full count or byte budget must evict the oldest regular generated
        // recipe and retain the newly observed one. Sparse files make the
        // 128 MiB disk boundary is inexpensive without compiling 512 shaders.
        for(bool byte_limit:{false,true}) {
            char bounds_directory[]="/tmp/bb-metal-bounds-XXXXXX";assert(mkdtemp(bounds_directory));
            setenv("BB_DATA_DIR",bounds_directory,1);
            const auto root=RecipeRoot(bounds_directory,device);
            {
                Compiler compiler(device,2);compiler.EnablePrewarming();
                const auto old_count=static_cast<unsigned>(byte_limit ?
                    Gpu::Metal::PipelinePrewarmLimits::Bytes/Gpu::Metal::PipelinePrewarmLimits::RecipeBytes :
                    Gpu::Metal::PipelinePrewarmLimits::Recipes);
                FillOldRecipes(root,old_count,byte_limit ? Gpu::Metal::PipelinePrewarmLimits::RecipeBytes : 1);
                const auto oldest=root/(std::string(64,'0')+".json");assert(std::filesystem::exists(oldest));
                const auto sentinel=std::filesystem::path(bounds_directory)/"protected.txt";
                std::ofstream(sentinel)<<"untouched";
                const auto link=root/(std::string(64,'f')+".json");
                std::filesystem::create_symlink(sentinel,link);
                std::ofstream(root/"unrelated.txt")<<"untouched";
                AreaPipeline(compiler,73);
            }
            assert(Recipes(bounds_directory).size()<=Gpu::Metal::PipelinePrewarmLimits::Recipes);
            assert(RecipeBytes(bounds_directory)<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            assert(!std::filesystem::exists(root/(std::string(64,'0')+".json")));
            // The only non-placeholder recipe must be the newly learned one.
            unsigned learned=0;
            for(const auto& file:Recipes(bounds_directory)) {
                std::ifstream input(file);char first=0;input.get(first);
                if(first=='{')++learned;
            }
            assert(learned==1);
            assert(std::filesystem::is_symlink(root/(std::string(64,'f')+".json")));
            std::string text;std::ifstream(std::filesystem::path(bounds_directory)/"protected.txt")>>text;
            assert(text=="untouched");text.clear();std::ifstream(root/"unrelated.txt")>>text;assert(text=="untouched");
            std::filesystem::remove_all(bounds_directory);
        }
        setenv("BB_DATA_DIR",warm_directory,1);
        // Corruption is a cache miss; no partially decoded recipe is used.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(warm_directory))
            if(entry.path().extension()==".json") std::ofstream(entry.path(),std::ios::trunc)<<"{broken";
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();assert(!compiler.GetWarmupStats().loaded);
            compile(compiler);
        }
        std::filesystem::remove_all(warm_directory);
        puts("Metal runtime: no binary archives; 72-recipe persistence/reload, warm hit beyond 64, bounded pending inputs, oldest-recipe count/byte eviction, source/symlink protection, opt-out and corruption recovery passed; offline archive fixture passed");
    }
}
