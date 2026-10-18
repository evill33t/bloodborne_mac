// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_compiler.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <unistd.h>

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
        puts("Metal runtime: no binary-archive API calls on cold/warm caches, even with explicit cache environment; offline fixture: warm hits and corruption recovery passed");
    }
}
