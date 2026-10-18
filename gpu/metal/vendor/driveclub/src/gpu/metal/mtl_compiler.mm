#include "gpu/metal/mtl_compiler.h"

#include <pthread.h>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "common/log.h"

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
        threads.emplace_back([this] { Worker(); });
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
id<MTLRenderPipelineState> ShaderCompiler::RenderPipeline(MTLRenderPipelineDescriptor* d,NSError** error) {
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
id<MTLComputePipelineState> ShaderCompiler::ComputePipeline(MTLComputePipelineDescriptor* d,NSError** error) {
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

void ShaderCompiler::Submit(std::function<void()> job) {
    {
        std::scoped_lock lk(lock);
        jobs.push_back(std::move(job));
    }
    cv.notify_one();
}

void ShaderCompiler::Worker() {
    pthread_setname_np("GPU shader compiler");
    while (true) {
        std::function<void()> job;
        {
            std::unique_lock lk(lock);
            cv.wait(lk, [this] { return stop || !jobs.empty(); });
            if (stop) {
                return;
            }
            job = std::move(jobs.front());
            jobs.pop_front();
        }
        @autoreleasepool {
            job();
        }
    }
}

id<MTLFunction> ShaderCompiler::Function(u64 key, const std::string& src, u64 address) {
    {
        std::scoped_lock lk(fn_lock);
        if (auto it = functions.find(key); it != functions.end()) {
            return it->second;
        }
    }
    MTLCompileOptions* opts = [MTLCompileOptions new];
    opts.mathMode = MTLMathModeSafe;
    // Depth-only and G-buffer variants must produce identical positions for
    // the game's EQUAL depth test, even when their other outputs differ.
    opts.preserveInvariance = YES;
    opts.languageVersion = MTLLanguageVersion3_0;
    NSError* err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(src.c_str()) options:opts error:&err];
    id<MTLFunction> fn = lib ? [lib newFunctionWithName:@"main0"] : nil;
    if (!fn) {
        DC_ERROR("gpu: MSL compile failed for shader %#llx: %s", address,
                 err ? err.localizedDescription.UTF8String : "?");
    }
    std::scoped_lock lk(fn_lock);
    return functions.emplace(key, fn).first->second;
}

} // namespace Gpu::Metal
