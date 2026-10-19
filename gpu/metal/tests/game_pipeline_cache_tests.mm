// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_compiler.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <chrono>

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
        // Corruption is a cache miss; no partially decoded recipe is used.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(warm_directory))
            if(entry.path().extension()==".json") std::ofstream(entry.path(),std::ios::trunc)<<"{broken";
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();assert(!compiler.GetWarmupStats().loaded);
            compile(compiler);
        }
        std::filesystem::remove_all(warm_directory);
        puts("Metal runtime: no binary archives; exact-source/descriptor prewarming, source-directory protection, opt-out and corruption recovery passed; offline archive fixture passed");
    }
}
