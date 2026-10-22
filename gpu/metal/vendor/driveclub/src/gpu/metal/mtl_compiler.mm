#include "gpu/metal/mtl_compiler.h"

#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <CommonCrypto/CommonDigest.h>
#include <filesystem>

#include "common/log.h"
#include "gpu/metal/stutter_trace.h"

namespace Gpu::Metal {

ShaderCompiler::ShaderCompiler(id<MTLDevice> device, u32 count,CacheMode cache_mode) : device(device) {
    const char* enabled=std::getenv("BB_METAL_PIPELINE_CACHE");
    const char* root=std::getenv("BB_METAL_CACHE_DIR");
    const char* data=std::getenv("BB_DATA_DIR");
    // Keep archive creation, collection and serialization out of the live game
    // process. NSError handling cannot recover from Metal's SIGSEGV in
    // -[_MTLBinaryArchive airntSerializeToURL:options:error:]. Environment
    // variables intentionally cannot opt the production renderer back into it.
    if (cache_mode==CacheMode::OfflineArchive && (!enabled || std::strcmp(enabled,"0")) &&
        ((root && *root) || (data && *data))) {
        NSString* directory=root && *root ? @(root) : [@(data) stringByAppendingPathComponent:@"metal-cache"];
        const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
        NSString* file=[NSString stringWithFormat:@"pipelines-v1-%llx-%s-%ld.%ld.%ld.metalarc",
            device.registryID,
#if defined(__aarch64__)
            "arm64",
#else
            "x86_64",
#endif
            os.majorVersion,os.minorVersion,os.patchVersion];
        archive_url=[NSURL fileURLWithPath:[directory stringByAppendingPathComponent:file]];
        auto descriptor=[MTLBinaryArchiveDescriptor new];
        if ([NSFileManager.defaultManager fileExistsAtPath:archive_url.path]) {
            descriptor.url=archive_url;
            archive=[device newBinaryArchiveWithDescriptor:descriptor error:nil];
            cache_stats.loaded=archive!=nil;
        }
        if (!archive) {
            descriptor.url=nil;
            archive=[device newBinaryArchiveWithDescriptor:descriptor error:nil];
        }
    }
    for (u32 k = 0; k < count; ++k) {
        // Reserve the other compiler workers for first-use work from the game.
        threads.emplace_back([this,background=k+1==count] { Worker(background); });
    }
}

ShaderCompiler::~ShaderCompiler() {
    {
        std::scoped_lock lk(lock);
        stop = true;
    }
    cv.notify_all();
    for (auto& t : threads) {
        t.join();
    }
    // Cancel expensive queued prewarming at shutdown, but keep observed recipes.
    while(!recipe_writes.empty()) {const auto key=recipe_writes.begin()->first;WriteRecipe(key);}
    while(!recipe_touches.empty()) {
        const auto key=*recipe_touches.begin();recipe_touches.erase(key);
        if(TouchRecipe(key))++warm_stats.recency_updates;
    }
    FlushCache();
}

void ShaderCompiler::SaveArchive() {
    if (!archive || !archive_url || !archive_dirty) return;
    auto manager=NSFileManager.defaultManager;
    if (![manager createDirectoryAtURL:archive_url.URLByDeletingLastPathComponent
        withIntermediateDirectories:YES attributes:nil error:nil]) return;
    auto temporary=[NSURL fileURLWithPath:[archive_url.path stringByAppendingFormat:@".tmp.%d",getpid()]];
    if (![archive serializeToURL:temporary error:nil]) return;
    // A failed write or process crash leaves the previous archive intact.
    if (::rename(temporary.fileSystemRepresentation,archive_url.fileSystemRepresentation)==0) {
        archive_dirty=0; ++cache_stats.writes;
    } else [manager removeItemAtURL:temporary error:nil];
}
void ShaderCompiler::FlushCache() { std::scoped_lock guard(archive_lock); SaveArchive(); }
ShaderCompiler::CacheStats ShaderCompiler::GetCacheStats() {
    std::scoped_lock guard(archive_lock); return cache_stats;
}
id<MTLRenderPipelineState> ShaderCompiler::NewRenderPipeline(MTLRenderPipelineDescriptor* d,NSError** error) {
    if (!archive) return [device newRenderPipelineStateWithDescriptor:d error:error];
    std::scoped_lock guard(archive_lock);
    if (archive) {
        d.binaryArchives=@[archive];
        auto hit=[device newRenderPipelineStateWithDescriptor:d options:MTLPipelineOptionFailOnBinaryArchiveMiss
            reflection:nil error:nil];
        if (hit) { ++cache_stats.hits; return hit; }
        ++cache_stats.misses;
    }
    auto pso=[device newRenderPipelineStateWithDescriptor:d error:error];
    if (pso && archive && [archive addRenderPipelineFunctionsWithDescriptor:d error:nil]) {
        ++archive_dirty;
        if (!cache_stats.writes || archive_dirty>=8) SaveArchive();
    }
    return pso;
}
id<MTLComputePipelineState> ShaderCompiler::NewComputePipeline(MTLComputePipelineDescriptor* d,NSError** error) {
    if (!archive) return [device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:error];
    std::scoped_lock guard(archive_lock);
    if (archive) {
        d.binaryArchives=@[archive];
        auto hit=[device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionFailOnBinaryArchiveMiss
            reflection:nil error:nil];
        if (hit) { ++cache_stats.hits; return hit; }
        ++cache_stats.misses;
    }
    auto pso=[device newComputePipelineStateWithDescriptor:d options:MTLPipelineOptionNone reflection:nil error:error];
    if (pso && archive && [archive addComputePipelineFunctionsWithDescriptor:d error:nil]) {
        ++archive_dirty;
        if (!cache_stats.writes || archive_dirty>=8) SaveArchive();
    }
    return pso;
}

void ShaderCompiler::Submit(std::function<void()> job,bool background) {
    {
        std::scoped_lock lk(lock);
        (background ? background_jobs : jobs).push_back(std::move(job));
    }
    cv.notify_all();
}

void ShaderCompiler::Worker(bool background) {
    pthread_setname_np("GPU shader compiler");
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock lk(lock);
            cv.wait(lk, [this,background] {
                return stop || !jobs.empty() || ((background || parallel_prewarming) && !background_jobs.empty());
            });
            if (stop) {
                return;
            }
            auto& next=jobs.empty() ? background_jobs : jobs;
            job = std::move(next.front());next.pop_front();
        }
        @autoreleasepool {
            job();
        }
    }
}

id<MTLFunction> ShaderCompiler::Function(u64 key, const std::string& src, u64 address) {
    (void)key;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(src.data(),static_cast<CC_LONG>(src.size()),digest);
    std::string identity;for(unsigned char b:digest) {identity+="0123456789abcdef"[b>>4];identity+="0123456789abcdef"[b&15];}
    u64 source_identity=0;
    for(unsigned k=0;k<8;++k) source_identity=(source_identity<<8)|digest[k];
    {
        std::unique_lock lk(fn_lock);
        {
            StutterTrace::Scope timing("shader_cache_wait",0,address,source_identity);
            fn_changed.wait(lk,[&]{return !compiling[identity];});
        }
        if (auto it = functions.find(identity); it != functions.end()) {
            if(it->second && warm_root) function_sources[reinterpret_cast<std::uintptr_t>((__bridge void*)it->second)]=src;
            return it->second;
        }
        compiling[identity]=true;
    }
    MTLCompileOptions* opts = [MTLCompileOptions new];
    opts.mathMode = MTLMathModeSafe;
    // Depth-only and G-buffer variants must produce identical positions for
    // the game's EQUAL depth test, even when their other outputs differ.
    opts.preserveInvariance = YES;
    opts.languageVersion = MTLLanguageVersion3_0;
    NSError* err = nil;
    id<MTLLibrary> lib=nil;
    {
        StutterTrace::Scope timing("shader_library",0,address,source_identity);
        lib=[device newLibraryWithSource:@(src.c_str()) options:opts error:&err];
    }
    id<MTLFunction> fn=nil;
    if(lib) {
        StutterTrace::Scope timing("shader_function",0,address,source_identity);
        fn=[lib newFunctionWithName:@"main0"];
    }
    if (!fn) {
        DC_ERROR("gpu: MSL compile failed for shader %#llx: %s", address,
                 err ? err.localizedDescription.UTF8String : "?");
    }
    {
        std::scoped_lock lk(fn_lock);
        functions[identity]=fn;compiling.erase(identity);
        if(fn && warm_root) function_sources[reinterpret_cast<std::uintptr_t>((__bridge void*)fn)]=src;
    }
    fn_changed.notify_all();return fn;
}

#include "gpu/metal/pipeline_prewarm.inc"

} // namespace Gpu::Metal
