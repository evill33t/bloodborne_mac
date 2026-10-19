#pragma once

// Background Metal shader compilation.
//
// Metal compiles MSL, and again per pipeline, synchronously and slowly
// (seconds for large shaders on AMD). The renderer hands that work to a few
// worker threads so the GPU thread keeps going; Metal's compiler service runs
// the requests in parallel.

#import <Metal/Metal.h>

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/types.h"

namespace Gpu::Metal {

namespace PipelinePrewarmLimits {
inline constexpr std::size_t Recipes=1024;
inline constexpr std::uint64_t Bytes=128ull*1024*1024;
inline constexpr std::uint64_t RecipeBytes=1024*1024;
}

class ShaderCompiler {
public:
    enum class CacheMode {
        DriverManaged,
        OfflineArchive, // isolated development fixture only; never the game renderer
    };
    ShaderCompiler(id<MTLDevice> device, u32 threads,CacheMode cache_mode=CacheMode::DriverManaged);
    ~ShaderCompiler();

    // The "main0" function, cached by the exact MSL source; nil if compilation
    // fails. Guest keys can be reused after streaming. Thread-safe.
    id<MTLFunction> Function(u64 key, const std::string& src, u64 address);

    // Runs `job` on a worker thread.
    void Submit(std::function<void()> job,bool background=false);

    // The runtime relies on Metal's managed cache and preserves cold first-use
    // draws. Explicit binary-archive collection is restricted to offline tools:
    // serializing real game pipelines has crashed inside Metal under Rosetta.
    id<MTLRenderPipelineState> RenderPipeline(MTLRenderPipelineDescriptor* descriptor,NSError** error);
    id<MTLComputePipelineState> ComputePipeline(MTLComputePipelineDescriptor* descriptor,NSError** error);
    struct CacheStats { u64 hits=0, misses=0, writes=0; bool loaded=false; };
    CacheStats GetCacheStats();
    void FlushCache();
    // Plain source/descriptor recipes only. No Metal binary archive APIs.
    void EnablePrewarming();
    struct WarmupStats {
        u64 hits=0,loaded=0,ready=0;
        u64 retained=0,retained_bytes=0,pending=0,pending_bytes=0,queued=0,queued_bytes=0;
        u64 pending_touches=0,recency_updates=0;
    };
    WarmupStats GetWarmupStats();

private:
    void Worker(bool background);

    id<MTLDevice> device;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<std::function<void()>> jobs;
    std::deque<std::function<void()>> background_jobs;
    std::vector<std::thread> threads;
    bool stop = false;

    std::mutex fn_lock;
    std::condition_variable fn_changed;
    std::unordered_map<std::string, id<MTLFunction>> functions;
    std::unordered_map<std::string,bool> compiling;
    std::unordered_map<std::uintptr_t,std::string> function_sources;
    NSString* warm_root=nil;
    std::mutex warm_lock;
    struct WarmPipeline {
        id<MTLRenderPipelineState> render=nil;id<MTLComputePipelineState> compute=nil;
        u64 bytes=0,used=0;
        bool demand_seen=false;
    };
    std::unordered_map<std::string,WarmPipeline> warm_pipelines;
    struct PendingRecipe {NSData* data=nil;u64 queued=0;};
    std::unordered_map<std::string,PendingRecipe> recipe_writes;
    std::unordered_set<std::string> recipe_touches;
    u64 warm_bytes=0,pending_recipe_bytes=0,warm_queued_bytes=0,warm_queued_count=0,warm_sequence=0;
    bool recipe_writer_queued=false;
    bool recipe_touch_writer_queued=false;
    std::mutex recipe_write_lock;
    WarmupStats warm_stats;
    NSDictionary* RenderRecipe(MTLRenderPipelineDescriptor*);
    NSDictionary* ComputeRecipe(MTLComputePipelineDescriptor*);
    void Remember(NSDictionary*,WarmPipeline);
    void WriteRecipe(const std::string&);
    void WritePendingRecipe();
    bool QueueWarmUseLocked(const std::string&,WarmPipeline&);
    bool TouchRecipe(const std::string&);
    void TouchPendingRecipe();
    void StoreWarmLocked(const std::string&,WarmPipeline,u64 bytes);
    void Prewarm(NSDictionary*);
    id<MTLRenderPipelineState> NewRenderPipeline(MTLRenderPipelineDescriptor*,NSError**);
    id<MTLComputePipelineState> NewComputePipeline(MTLComputePipelineDescriptor*,NSError**);
    std::mutex archive_lock;
    id<MTLBinaryArchive> archive=nil;
    NSURL* archive_url=nil;
    CacheStats cache_stats;
    u32 archive_dirty=0;
    void SaveArchive(); // archive_lock held, compiler worker only
};

} // namespace Gpu::Metal
