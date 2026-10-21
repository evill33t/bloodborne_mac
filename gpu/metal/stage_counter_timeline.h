// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/stutter_trace.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace Gpu::Metal::StageCounterTimeline {
using U64=std::uint64_t;
inline constexpr std::size_t PoolSize=16,MaximumPoolSize=256,SampleCount=4,ComputeSampleCount=2;
// Missing option preserves16. Explicit malformed/out-of-range values disable
// this diagnostic instead of silently allocating a different pool size.
inline std::size_t ConfiguredPoolSize(const char* value) noexcept {
    if(!value)return PoolSize;
    if(!*value || *value=='0')return 0;
    std::size_t result=0;
    for(const char* at=value;*at;++at) {
        if(*at<'0' || *at>'9')return 0;
        result=result*10+std::size_t(*at-'0');
        if(result>MaximumPoolSize)return 0;
    }
    return result>=PoolSize ? result : 0;
}
inline constexpr std::size_t MaximumSampleCount=MaximumPoolSize*SampleCount;
inline constexpr std::size_t MaximumTimestampPayload=
    MaximumSampleCount*sizeof(MTLCounterResultTimestamp);
inline constexpr std::size_t SampleBase(std::size_t slot) noexcept {return slot*SampleCount;}
struct ClockPair {U64 cpu=0,gpu=0;double host_before=0,host_after=0;};
struct Encoded {
    U64 vs_key=0,ps_key=0,pipeline_key=0;
    bool vs_known=false,ps_known=false,pipeline_known=false;
};
enum class Kind {Render,Compute};
inline bool ConfiguredCompute(const char* value) noexcept {
    return value && value[0]=='1' && !value[1];
}
struct ComputeInfo {
    U64 program=0,gcn_hash=0,encoded_key=0,pipeline_key=0;
    bool indirect=false,groups_known=false;
    std::array<U64,3> groups{},threads{};
};
struct Pass {
    Kind kind=Kind::Render;
    ComputeInfo compute;
    std::size_t slot=MaximumPoolSize; // invalid for every supported active capacity
    U64 id=0,frame=0,width=0,height=0,colors=0,color0=0,depth=0;
    std::array<U64,8> formats{};
    U64 depth_format=0,draws=0;
    U64 first_vs=0,last_vs=0,first_ps=0,last_ps=0;
    U64 first_vs_hash=0,last_vs_hash=0,first_ps_hash=0,last_ps_hash=0;
    bool mixed_vs=false,mixed_ps=false;
    Encoded first_encoded,last_encoded;
    bool mixed_vs_key=false,mixed_ps_key=false,mixed_pipeline_key=false;
    bool unknown_vs_key=false,unknown_ps_key=false,unknown_pipeline_key=false;
};
inline void NoteDraw(Pass& pass,U64 vs,U64 ps,U64 vs_hash,U64 ps_hash,Encoded encoded={}) {
    if(!pass.draws) {
        pass.first_vs=vs;pass.first_ps=ps;
        pass.first_vs_hash=vs_hash;pass.first_ps_hash=ps_hash;
        pass.first_encoded=encoded;
    } else {
        pass.mixed_vs|=vs!=pass.first_vs || vs_hash!=pass.first_vs_hash;
        pass.mixed_ps|=ps!=pass.first_ps || ps_hash!=pass.first_ps_hash;
        const auto changed=[](U64 first,bool first_known,U64 last,bool last_known) {
            return first_known!=last_known || (first_known && first!=last);
        };
        pass.mixed_vs_key|=changed(pass.first_encoded.vs_key,pass.first_encoded.vs_known,encoded.vs_key,encoded.vs_known);
        pass.mixed_ps_key|=changed(pass.first_encoded.ps_key,pass.first_encoded.ps_known,encoded.ps_key,encoded.ps_known);
        pass.mixed_pipeline_key|=changed(pass.first_encoded.pipeline_key,pass.first_encoded.pipeline_known,encoded.pipeline_key,encoded.pipeline_known);
    }
    ++pass.draws;pass.last_vs=vs;pass.last_ps=ps;
    pass.last_vs_hash=vs_hash;pass.last_ps_hash=ps_hash;
    pass.last_encoded=encoded;
    pass.unknown_vs_key|=!encoded.vs_known;pass.unknown_ps_key|=!encoded.ps_known;
    pass.unknown_pipeline_key|=!encoded.pipeline_known;
}
struct Durations {bool valid=false;double vertex_ms=NAN,fragment_ms=NAN;};
// Apple's documented CPU/GPU reference-pair interpolation. CPU references
// are nanoseconds; raw GPU sample units are never guessed or called cycles.
inline Durations Convert(const std::array<U64,4>& samples,const ClockPair& begin,const ClockPair& end) {
    if(end.cpu<=begin.cpu || end.gpu<=begin.gpu)return {};
    for(const auto sample:samples)
        if(sample==MTLCounterErrorValue || sample<begin.gpu || sample>end.gpu)return {};
    if(samples[1]<samples[0] || samples[3]<samples[2])return {};
    const long double scale=static_cast<long double>(end.cpu-begin.cpu)/(end.gpu-begin.gpu);
    return {true,double(static_cast<long double>(samples[1]-samples[0])*scale/1e6L),
        double(static_cast<long double>(samples[3]-samples[2])*scale/1e6L)};
}
struct ComputeDuration {bool valid=false;double milliseconds=NAN;};
inline ComputeDuration ConvertCompute(const std::array<U64,2>& samples,
                                     const ClockPair& begin,const ClockPair& end) {
    if(end.cpu<=begin.cpu || end.gpu<=begin.gpu)return {};
    for(const auto sample:samples)
        if(sample==MTLCounterErrorValue || sample<begin.gpu || sample>end.gpu)return {};
    if(samples[1]<samples[0])return {};
    const long double scale=static_cast<long double>(end.cpu-begin.cpu)/(end.gpu-begin.gpu);
    return {true,double(static_cast<long double>(samples[1]-samples[0])*scale/1e6L)};
}
class Log final:public std::enable_shared_from_this<Log> {
public:
    ~Log() {if(file)std::fclose(file);}
    static std::shared_ptr<Log> Open(id<MTLDevice> device) {
        const char* path=std::getenv("BB_METAL_STAGE_COUNTER_CSV");
        if(!path || !*path)return {};
        const auto pool_size=ConfiguredPoolSize(std::getenv("BB_METAL_STAGE_COUNTER_SLOTS"));
        if(!pool_size)return Disabled("invalid stage counter slots; expected canonical decimal16..256");
        if(@available(macOS 11.0,*)) {
            if(![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])
                return Disabled("stage-boundary sampling unsupported");
            id<MTLCounterSet> timestamps=nil;
            for(id<MTLCounterSet> set in device.counterSets)
                if([set.name isEqualToString:MTLCommonCounterSetTimestamp])timestamps=set;
            bool has_timestamp=false;
            for(id<MTLCounter> counter in timestamps.counters)
                has_timestamp|=[counter.name isEqualToString:MTLCommonCounterTimestamp];
            if(!timestamps || !has_timestamp)return Disabled("timestamp counter set absent");
            const char* all=std::getenv("BB_METAL_STAGE_COUNTER_ALL_PASSES");
            auto log=std::shared_ptr<Log>(new Log(device,all && !std::strcmp(all,"1"),pool_size,
                ConfiguredCompute(std::getenv("BB_METAL_STAGE_COUNTER_COMPUTE"))));
            // One API object avoids a device-wide object-count ceiling. Each
            // logical slot owns four distinct sample indices until CPU resolve
            // and logging finish; different slots never overwrite each other.
            auto descriptor=[MTLCounterSampleBufferDescriptor new];
            descriptor.counterSet=timestamps;descriptor.storageMode=MTLStorageModeShared;
            descriptor.sampleCount=log->pool_size*SampleCount;
            descriptor.label=@"Bloodborne shared stage timestamp diagnostics";
            NSError* allocation_error=nil;
            log->sample_buffer=[device newCounterSampleBufferWithDescriptor:descriptor error:&allocation_error];
            if(!log->sample_buffer || log->sample_buffer.sampleCount!=descriptor.sampleCount) {
                std::fprintf(stderr,"gpu stage counter allocation: slots=%zu requested-samples=%lu actual-samples=%lu domain=%s code=%ld description=%s\n",
                    log->pool_size,(unsigned long)descriptor.sampleCount,(unsigned long)log->sample_buffer.sampleCount,
                    allocation_error.domain.UTF8String ?: "?",long(allocation_error.code),
                    allocation_error.localizedDescription.UTF8String ?: "no NSError detail");
                return Disabled("shared stage timestamp buffer allocation/capacity failed");
            }
            // Same canonical source-directory exclusions as command_timeline.
            // Also reject nonregular/hardlinked output before writing a header.
            std::error_code error;
            const auto destination=std::filesystem::weakly_canonical(path,error);
            if(error)return Disabled("invalid output path");
            for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
                const char* value=std::getenv(key);if(!value || !*value)continue;
                const auto source=std::filesystem::weakly_canonical(value,error);
                if(error)return Disabled("invalid source-directory guard");
                const auto relative=destination.lexically_relative(source);
                if(destination==source || (!relative.empty() && *relative.begin()!=".."))
                    return Disabled("output is inside original game directory");
            }
            const int fd=::open(destination.c_str(),O_WRONLY|O_CREAT|O_APPEND|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
            if(fd<0)return Disabled("output open failed");
            struct stat status{};
            if(::fstat(fd,&status) || !S_ISREG(status.st_mode) || status.st_nlink!=1) {
                ::close(fd);return Disabled("output must be a regular single-link file");
            }
            log->file=::fdopen(fd,"a");if(!log->file){::close(fd);return Disabled("output stream failed");}
            std::fputs("submission,pass,frame,width,height,colors,format0,format1,format2,format3,format4,format5,format6,format7,depth_format,color0,depth,draws,mixed_gcn_vs,mixed_gcn_ps,first_gcn_vs,last_gcn_vs,first_gcn_ps,last_gcn_ps,first_gcn_vs_hash,last_gcn_vs_hash,first_gcn_ps_hash,last_gcn_ps_hash,first_encoded_vs_key,last_encoded_vs_key,first_encoded_ps_key,last_encoded_ps_key,first_pipeline_cache_key,last_pipeline_cache_key,first_vs_key_known,last_vs_key_known,first_ps_key_known,last_ps_key_known,first_pipeline_key_known,last_pipeline_key_known,mixed_encoded_vs,mixed_encoded_ps,mixed_pipeline_key,unknown_encoded_vs,unknown_encoded_ps,unknown_pipeline_key,vertex_start_raw,vertex_end_raw,fragment_start_raw,fragment_end_raw,cpu0_ns,gpu0_raw,cpu1_ns,gpu1_raw,host0_before_s,host0_after_s,host1_before_s,host1_after_s,vertex_ms,fragment_ms,status,error_code,data_valid,calibration_valid,natural_passes,selected,skipped,exhausted,attach_errors,encode_errors,invalid_results,selection_all_passes,pool_slots,pass_kind,compute_program,compute_gcn_hash,compute_encoded_key,compute_pipeline_key,compute_indirect,compute_groups_known,compute_groups_x,compute_groups_y,compute_groups_z,compute_threads_x,compute_threads_y,compute_threads_z,compute_start_raw,compute_end_raw,compute_ms,selection_compute_passes,compute_natural_passes,compute_selected,compute_skipped,compute_exhausted,compute_attach_errors,compute_encode_errors,compute_invalid_results,counter_schema,compute_coverage\n",log->file);
            if(std::fflush(log->file) || std::ferror(log->file))return Disabled("output header write failed");
            std::fprintf(stderr,"gpu stage counters: enabled timestamp stage boundaries; fixed slots%zu/stride4/shared-buffer1; timestamp payload%zu bytes (driver overhead additional); selection=%s\n",
                log->pool_size,log->pool_size*SampleCount*sizeof(MTLCounterResultTimestamp),
                log->all_passes ? "all natural BeginPass encoders" : "natural material MRT >=5 colors and >=960x540");
            std::fprintf(stderr,"gpu stage counters: compute=%s; schema2; compute coverage excludes uploader/adapters/LS-HS/blits/presentation\n",
                log->compute_passes ? "ordinary Impl::Dispatch stage boundaries" : "off");
            return log;
        }
        return Disabled("macOS 11 stage counter API unavailable");
    }
    std::size_t PoolCapacity() const noexcept {return pool_size;}
    bool ComputeEnabled() const noexcept {return compute_passes;}
    ClockPair Clock() const {
        ClockPair pair;
        MTLTimestamp cpu=0,gpu=0;
        pair.host_before=StutterTrace::Now();
        [device sampleTimestamps:&cpu gpuTimestamp:&gpu];
        pair.host_after=StutterTrace::Now();pair.cpu=cpu;pair.gpu=gpu;return pair;
    }
    // Default selection: existing full/half-resolution material MRT passes.
    // No extra render pass is created. The pool is never waited on or grown.
    std::optional<Pass> Attach(MTLRenderPassDescriptor* descriptor,Pass metadata) {
        ++natural_passes;
        if((!all_passes && (metadata.colors<5 || metadata.width<960 || metadata.height<540)) || output_failed.load()) {
            ++skipped;return {};
        }
        ++selected;
        std::size_t index=0;
        for(;index<pool_size;++index) {
            bool available=false;
            if(pool[index].busy.compare_exchange_strong(available,true,std::memory_order_acquire))break;
        }
        if(index==pool_size){++exhausted;return {};}
        metadata.kind=Kind::Render;metadata.slot=index;metadata.id=++sequence;
        @try {
            auto attachment=descriptor.sampleBufferAttachments[0];
            const auto base=SampleBase(index);
            attachment.sampleBuffer=sample_buffer;
            attachment.startOfVertexSampleIndex=base;attachment.endOfVertexSampleIndex=base+1;
            attachment.startOfFragmentSampleIndex=base+2;attachment.endOfFragmentSampleIndex=base+3;
        } @catch(NSException* exception) {
            (void)exception;
            descriptor.sampleBufferAttachments[0].sampleBuffer=nil;
            pool[index].busy.store(false,std::memory_order_release);++attach_errors;return {};
        }
        return metadata;
    }
    // StageBoundary explicitly covers compute encoder start/end on this device;
    // dispatch-boundary support is a different feature and is not substituted.
    std::optional<Pass> AttachCompute(MTLComputePassDescriptor* descriptor,Pass metadata) {
        ++compute_natural_passes;
        if(!compute_passes || output_failed.load()) {++compute_skipped;return {};}
        ++compute_selected;
        std::size_t index=0;
        for(;index<pool_size;++index) {
            bool available=false;
            if(pool[index].busy.compare_exchange_strong(available,true,std::memory_order_acquire))break;
        }
        if(index==pool_size) {++compute_exhausted;return {};}
        metadata.kind=Kind::Compute;metadata.slot=index;metadata.id=++sequence;
        @try {
            auto attachment=descriptor.sampleBufferAttachments[0];
            const auto base=SampleBase(index);
            attachment.sampleBuffer=sample_buffer;
            attachment.startOfEncoderSampleIndex=base;attachment.endOfEncoderSampleIndex=base+1;
        } @catch(NSException* exception) {
            (void)exception;descriptor.sampleBufferAttachments[0].sampleBuffer=nil;
            Release(index);++compute_attach_errors;return {};
        }
        return metadata;
    }
    void CancelCompute(MTLComputePassDescriptor* descriptor,const Pass& pass) {
        descriptor.sampleBufferAttachments[0].sampleBuffer=nil;
        ++compute_encode_errors;Release(pass.slot);
    }
    void Cancel(MTLRenderPassDescriptor* descriptor,const Pass& pass) {
        descriptor.sampleBufferAttachments[0].sampleBuffer=nil;
        ++encode_errors;Release(pass.slot);
    }
    // Called only from an existing command completion handler. It queues CPU
    // resolution and logging; no renderer pointer, new GPU command or wait.
    void Complete(U64 submission,long status,long error,ClockPair begin,ClockPair end,
        std::shared_ptr<const std::vector<Pass>> passes) {
        auto self=shared_from_this();
        dispatch_async(resolve_queue,^{@autoreleasepool {
            for(const auto& pass:*passes)self->Resolve(submission,status,error,begin,end,pass);
        }});
    }
private:
    struct Slot {std::atomic<bool> busy{false};};
    explicit Log(id<MTLDevice> device,bool all_passes,std::size_t pool_size,bool compute_passes)
        :device(device),all_passes(all_passes),pool_size(pool_size),compute_passes(compute_passes),
        resolve_queue(dispatch_queue_create("bloodborne.stage-counter.resolve",DISPATCH_QUEUE_SERIAL)) {}
    static std::shared_ptr<Log> Disabled(const char* why) {
        std::fprintf(stderr,"gpu stage counters disabled: %s\n",why);return {};
    }
    void Release(std::size_t index) {
        pool[index].busy.store(false,std::memory_order_release);
    }
    void Resolve(U64 submission,long status,long error,const ClockPair& begin,const ClockPair& end,const Pass& pass) {
        const bool compute=pass.kind==Kind::Compute;
        std::array<U64,4> raw{};raw.fill(MTLCounterErrorValue);
        std::array<U64,2> compute_raw{};compute_raw.fill(MTLCounterErrorValue);
        bool valid=false;
        @try {
            if(status==MTLCommandBufferStatusCompleted) {
                const auto count=compute ? ComputeSampleCount : SampleCount;
                NSData* data=[sample_buffer resolveCounterRange:NSMakeRange(SampleBase(pass.slot),count)];
                if(data && data.length==count*sizeof(MTLCounterResultTimestamp)) {
                    const auto* samples=static_cast<const MTLCounterResultTimestamp*>(data.bytes);
                    valid=true;
                    for(std::size_t k=0;k<count;++k) {
                        const auto value=samples[k].timestamp;valid&=value!=MTLCounterErrorValue;
                        if(compute)compute_raw[k]=value;else raw[k]=value;
                    }
                }
            }
        } @catch(NSException* exception) {(void)exception;valid=false;}
        const auto duration=valid && !compute ? Convert(raw,begin,end) : Durations{};
        const auto compute_duration=valid && compute ? ConvertCompute(compute_raw,begin,end) : ComputeDuration{};
        const bool calibration_valid=compute ? compute_duration.valid : duration.valid;
        if(!valid || !calibration_valid) {
            if(compute)++compute_invalid_results;else ++invalid_results;
        }
        // File work is on a private serial worker, not Metal's completion
        // queue. Keep the pool slot busy until logging ends, bounding backlog.
        if(!output_failed.load()) {
            std::fprintf(file,"%llu,%llu,%llu,%llu,%llu,%llu",
                (unsigned long long)submission,(unsigned long long)pass.id,(unsigned long long)pass.frame,
                (unsigned long long)pass.width,(unsigned long long)pass.height,(unsigned long long)pass.colors);
            for(auto format:pass.formats)std::fprintf(file,",%llu",(unsigned long long)format);
            const std::array<U64,14> metadata{pass.depth_format,pass.color0,pass.depth,pass.draws,
                U64(pass.mixed_vs),U64(pass.mixed_ps),pass.first_vs,pass.last_vs,pass.first_ps,pass.last_ps,
                pass.first_vs_hash,pass.last_vs_hash,pass.first_ps_hash,pass.last_ps_hash};
            for(auto value:metadata)std::fprintf(file,",%llu",(unsigned long long)value);
            const auto& first=pass.first_encoded;const auto& last=pass.last_encoded;
            const std::array<U64,18> variants{first.vs_key,last.vs_key,first.ps_key,last.ps_key,
                first.pipeline_key,last.pipeline_key,U64(first.vs_known),U64(last.vs_known),
                U64(first.ps_known),U64(last.ps_known),U64(first.pipeline_known),U64(last.pipeline_known),
                U64(pass.mixed_vs_key),U64(pass.mixed_ps_key),U64(pass.mixed_pipeline_key),
                U64(pass.unknown_vs_key),U64(pass.unknown_ps_key),U64(pass.unknown_pipeline_key)};
            for(auto value:variants)std::fprintf(file,",%llu",(unsigned long long)value);
            if(compute)std::fputs(",,,,",file); // no fabricated vertex/fragment raw timestamps
            else for(auto value:raw)std::fprintf(file,",%llu",(unsigned long long)value);
            std::fprintf(file,",%llu,%llu,%llu,%llu,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%ld,%ld,%u,%u",
                (unsigned long long)begin.cpu,(unsigned long long)begin.gpu,(unsigned long long)end.cpu,(unsigned long long)end.gpu,
                begin.host_before,begin.host_after,end.host_before,end.host_after,duration.vertex_ms,duration.fragment_ms,
                status,error,unsigned(valid),unsigned(calibration_valid));
            for(auto value:{natural_passes.load(),selected.load(),skipped.load(),exhausted.load(),attach_errors.load(),encode_errors.load(),invalid_results.load()})
                std::fprintf(file,",%llu",(unsigned long long)value);
            std::fprintf(file,",%u,%zu,%s",unsigned(all_passes),pool_size,compute ? "compute" : "render");
            const auto& cs=pass.compute;
            const std::array<U64,12> compute_metadata{cs.program,cs.gcn_hash,cs.encoded_key,cs.pipeline_key,
                U64(cs.indirect),U64(cs.groups_known),cs.groups[0],cs.groups[1],cs.groups[2],
                cs.threads[0],cs.threads[1],cs.threads[2]};
            for(auto value:compute_metadata)std::fprintf(file,",%llu",(unsigned long long)value);
            if(compute)for(auto value:compute_raw)std::fprintf(file,",%llu",(unsigned long long)value);
            else std::fputs(",,",file);
            std::fprintf(file,",%.9f,%u",compute_duration.milliseconds,unsigned(compute_passes));
            for(auto value:{compute_natural_passes.load(),compute_selected.load(),compute_skipped.load(),
                    compute_exhausted.load(),compute_attach_errors.load(),compute_encode_errors.load(),compute_invalid_results.load()})
                std::fprintf(file,",%llu",(unsigned long long)value);
            std::fputs(",2,ordinary-dispatch-only\n",file);
            if(std::fflush(file) || std::ferror(file))output_failed=true;
        }
        Release(pass.slot);
    }
    id<MTLDevice> device=nil;
    id<MTLCounterSampleBuffer> sample_buffer=nil;
    const bool all_passes;
    const std::size_t pool_size;
    const bool compute_passes;
    dispatch_queue_t resolve_queue;
    std::array<Slot,MaximumPoolSize> pool;
    U64 sequence=0;
    std::atomic<U64> natural_passes{0},selected{0},skipped{0},exhausted{0},attach_errors{0},encode_errors{0},invalid_results{0};
    std::atomic<U64> compute_natural_passes{0},compute_selected{0},compute_skipped{0},
        compute_exhausted{0},compute_attach_errors{0},compute_encode_errors{0},compute_invalid_results{0};
    std::atomic<bool> output_failed{false};
    std::FILE* file=nullptr;
};
} // namespace Gpu::Metal::StageCounterTimeline
