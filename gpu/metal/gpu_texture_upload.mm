// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/gpu_texture_upload.h"
#include "gpu/metal/stutter_trace.h"
#include "gpu/liverpool/tiling.h"
#include "common/content_hash.h"
#include "src/runtime_gpu_memory.h"
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <utility>

namespace Gpu::Metal {
namespace {
constexpr std::uint64_t kMaximumBytes=128ull<<20;
constexpr std::uint64_t kStagingPageBytes=4ull<<20;
constexpr std::uint64_t kMetadataPageBytes=64ull<<10;
constexpr unsigned kMaximumJobs=512;
struct alignas(8) Params {
    std::uint64_t source_offset,destination_offset;
    std::uint32_t width,height,pitch,row_words,tile,pixel_words;
};
static_assert(sizeof(Params)==40);
struct alignas(8) PackedJob {
    Params params;
    std::uint32_t group_end,groups_per_row;
};
static_assert(sizeof(PackedJob)==48);

// Raw 4/8/16-byte elements, including compressed blocks. The mapping matches
// Liverpool MacroOffset/MicroIndex and the 1D mip tail. Sixteen-byte blocks
// use 64x64 macro tiles/eight banks; 4/8-byte elements use 128x64/sixteen banks.
// Insert pipe/bank bits after the complete byte offset, including all words.
// Raw uint loads/stores plus a blit preserve float/half NaNs and denormals,
// BGRA channel order, sRGB and compressed block bytes without typed arithmetic.
constexpr const char* kSource=R"(
#include <metal_stdlib>
using namespace metal;
struct Layout {
    ulong source_offset,destination_offset;
    uint width,height,pitch,row_words,tile,pixel_words;
};
static inline void detile(device const uint* source,device uint* linear,
                          constant Layout& layout,uint2 p) {
    if(p.x>=layout.width || p.y>=layout.height)return;
    const uint x=p.x&7u,y=p.y&7u;
    uint micro;
    if(layout.tile==10u || layout.tile==9u) {
        if(layout.pixel_words==2u) // eight-byte display: x0,y0,x1,x2,y1,y2
            micro=(x&1u)|((y&1u)<<1)|((x&6u)<<1)|((y&6u)<<3);
        else if(layout.pixel_words==4u) // sixteen-byte display: y0,x0,x1,x2,y1,y2
            micro=(y&1u)|((x&7u)<<1)|((y&6u)<<3);
        else // four-byte display: x0,x1,y0,x2,y1,y2
            micro=(x&3u)|((y&1u)<<2)|((x&4u)<<1)|((y&6u)<<3);
    }
    else // thin and depth: x0,y0,x1,y1,x2,y2
        micro=(x&1u)|((y&1u)<<1)|((x&2u)<<1)|((y&2u)<<2)|
              ((x&4u)<<2)|((y&4u)<<3);
    const uint tx=p.x>>3,ty=p.y>>3;
    const uint pipe=((tx^ty^(tx>>1))&1u)|(((tx^ty)&2u))|
                    (((tx^ty)&4u));
    const uint bx=tx>>3;
    const bool wide=layout.pixel_words==4u;
    const uint bank=wide ? ((bx^(ty>>2))&1u)|((bx^ty^(ty>>1))&2u)|((bx^(ty<<2))&4u) :
                    ((bx^(ty>>3))&1u)|((bx^(ty>>1)^(ty>>2))&2u)|
                    ((bx^(ty<<1))&4u)|((bx^(ty<<3))&8u);
    const uint pitch_shift=wide?6u:7u;
    const uint pitch_mask=(1u<<pitch_shift)-1u;
    const uint padded_pitch=(layout.pitch+pitch_mask)&~pitch_mask;
    const ulong macro=(ulong(p.y>>6)*(padded_pitch>>pitch_shift)+(p.x>>pitch_shift))*
                      (256ul*layout.pixel_words);
    const ulong total=macro+ulong(micro)*(4ul*layout.pixel_words);
    const bool micro_only=layout.tile==5u || layout.tile==9u || layout.tile==13u;
    const ulong offset=micro_only ?
        (ulong(ty)*((layout.pitch+7u)>>3)+tx)*(256ul*layout.pixel_words)+ulong(micro)*(4ul*layout.pixel_words) :
        (total&255ul)|(ulong(pipe)<<8)|(ulong(bank)<<11)|((total>>8)<<(wide?14u:15u));
    const ulong destination=(layout.destination_offset>>2)+ulong(p.y)*layout.row_words+
                            ulong(p.x)*layout.pixel_words;
    const ulong input=(layout.source_offset+offset)>>2;
    linear[destination]=source[input];
    if(layout.pixel_words>=2u)linear[destination+1]=source[input+1];
    if(layout.pixel_words==4u) {
        linear[destination+2]=source[input+2];linear[destination+3]=source[input+3];
    }
}
kernel void main0(device const uint* source [[buffer(0)]],
                  device uint* linear [[buffer(1)]],
                  constant Layout& layout [[buffer(2)]],
                  uint2 p [[thread_position_in_grid]]) {
    detile(source,linear,layout,p);
}
struct PackedJob {Layout layout;uint group_end,groups_per_row;};
kernel void packed_mips(device const uint* source [[buffer(0)]],
                        device uint* linear [[buffer(1)]],
                        constant PackedJob* jobs [[buffer(2)]],
                        constant uint& count [[buffer(3)]],
                        uint3 group [[threadgroup_position_in_grid]],
                        uint3 lane [[thread_position_in_threadgroup]]) {
    // All lanes of an 8x8 group address one immutable mip/slice. Packing group
    // ranges avoids dispatching the largest mip's rectangle for every tail.
    uint lo=0u,hi=count-1u;
    while(lo<hi) {
        uint mid=(lo+hi)>>1;
        if(group.x>=jobs[mid].group_end)lo=mid+1u;else hi=mid;
    }
    uint first=lo ? jobs[lo-1u].group_end : 0u;
    uint index=group.x-first;
    uint2 p=uint2((index%jobs[lo].groups_per_row)*8u+lane.x,
                  (index/jobs[lo].groups_per_row)*8u+lane.y);
    detile(source,linear,jobs[lo].layout,p);
}
)";
bool MacroThin(unsigned tile) {return tile<=4 || tile==10 || tile==14;}
bool MicroThin(unsigned tile) {return tile==5 || tile==9 || tile==13;}
}

struct GpuTextureUploader::Impl {
    id<MTLDevice> device;
    id<MTLComputePipelineState> pipeline;
    id<MTLComputePipelineState> packed_pipeline;
    std::string error;
    bool extended=true;
    bool batch_mips=false;
    bool mapped_snapshot=false;
    Stats stats;
    // Each command owns immutable, append-only source/scratch ranges. Encoders
    // retain their buffers; advancing to another command never recycles them.
    __weak id<MTLCommandBuffer> staging_command=nil;
    struct Arena {
        id<MTLBuffer> buffer=nil;
        std::uint64_t head=0;
    } snapshots,scratch,metadata;
    id<MTLBuffer> Reserve(Arena& arena,std::uint64_t bytes,MTLResourceOptions options,
                         std::uint64_t& offset,std::uint64_t page_bytes=kStagingPageBytes) {
        const auto begin=(arena.head+255)&~255ull;
        if(!arena.buffer || begin>arena.buffer.length || bytes>arena.buffer.length-begin) {
            arena.buffer=[device newBufferWithLength:std::max(page_bytes,bytes) options:options];
            arena.head=0;offset=0;
        } else offset=begin;
        if(arena.buffer)arena.head=offset+bytes;
        return arena.buffer;
    }
    void BeginStaging(id<MTLCommandBuffer> command) {
        if(command==staging_command)return;
        staging_command=command;snapshots={};scratch={};metadata={};
    }
};

GpuTextureUploader::GpuTextureUploader(void* handle):impl(std::make_unique<Impl>()) {
    impl->device=(__bridge id<MTLDevice>)handle;
    const char* extended=std::getenv("BB_METAL_GPU_COMPRESSED_UPLOAD");
    impl->extended=!extended || std::strcmp(extended,"0");
    const char* batch_mips=std::getenv("BB_METAL_GPU_BATCH_MIPS");
    impl->batch_mips=batch_mips && !std::strcmp(batch_mips,"1");
    const char* mapped_snapshot=std::getenv("BB_METAL_GPU_MAPPED_SNAPSHOT");
    impl->mapped_snapshot=mapped_snapshot && !std::strcmp(mapped_snapshot,"1");
    if(!impl->device){impl->error="GPU texture uploader has no Metal device.";return;}
    NSError* error=nil;
    auto library=[impl->device newLibraryWithSource:@(kSource) options:nil error:&error];
    if(library)impl->pipeline=[impl->device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"main0"] error:&error];
    if(library && impl->batch_mips)impl->packed_pipeline=[impl->device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"packed_mips"] error:&error];
    if(impl->batch_mips && !impl->packed_pipeline) {
        impl->pipeline=nil;
        impl->error=error ? error.localizedDescription.UTF8String : "GPU packed mip upload pipeline could not be created.";
        return;
    }
    if(!impl->pipeline)impl->error=error ? error.localizedDescription.UTF8String :
        "GPU texture upload pipeline could not be created.";
}
GpuTextureUploader::~GpuTextureUploader()=default;
bool GpuTextureUploader::Ready() const {return impl->pipeline!=nil;}
const std::string& GpuTextureUploader::Error() const {return impl->error;}
GpuTextureUploader::Stats GpuTextureUploader::GetStats() const {return impl->stats;}
std::uint32_t GpuTextureUploader::BytesPerElement(std::uint64_t format) {
    switch(format) {
    case MTLPixelFormatR32Float:
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB:return 4;
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatRG32Float:return 8;
    case MTLPixelFormatBC1_RGBA: case MTLPixelFormatBC1_RGBA_sRGB:
    case MTLPixelFormatBC4_RUnorm: case MTLPixelFormatBC4_RSnorm:return 8;
    case MTLPixelFormatBC2_RGBA: case MTLPixelFormatBC2_RGBA_sRGB:
    case MTLPixelFormatBC3_RGBA: case MTLPixelFormatBC3_RGBA_sRGB:
    case MTLPixelFormatBC5_RGUnorm: case MTLPixelFormatBC5_RGSnorm:
    case MTLPixelFormatBC6H_RGBFloat: case MTLPixelFormatBC6H_RGBUfloat:
    case MTLPixelFormatBC7_RGBAUnorm: case MTLPixelFormatBC7_RGBAUnorm_sRGB:return 16;
    default:return 0;
    }
}
std::uint32_t GpuTextureUploader::BlockExtent(std::uint64_t format) {
    return format>=MTLPixelFormatBC1_RGBA && format<=MTLPixelFormatBC7_RGBAUnorm_sRGB ? 4 : 1;
}
bool GpuTextureUploader::SupportsFormat(std::uint64_t format) const {
    return BytesPerElement(format) && (BlockExtent(format)==1 ||
        (impl->extended && impl->device.supportsBCTextureCompression));
}

GpuTextureUploader::Result GpuTextureUploader::Encode(void* command_handle,void* texture_handle,
                                                     const Upload& upload,InitialHashes* initial_hashes) {
    if(initial_hashes)*initial_hashes={};
    if(!Ready())return Result::Fallback;
    impl->error.clear();
    auto command=(__bridge id<MTLCommandBuffer>)command_handle;
    auto texture=(__bridge id<MTLTexture>)texture_handle;
    const auto bytes_per_pixel=BytesPerElement(texture.pixelFormat);
    const auto block=BlockExtent(texture.pixelFormat);
    const auto reject=[&](const char* reason){impl->error=reason;return Result::Fallback;};
    if(!command || !texture || command.commandQueue.device!=impl->device ||
       texture.device!=impl->device || !command.retainedReferences ||
       command.status>=MTLCommandBufferStatusCommitted || texture.framebufferOnly ||
       texture.storageMode==MTLStorageModeMemoryless || !SupportsFormat(texture.pixelFormat))
        return reject("GPU texture upload has an unsupported Metal destination or command buffer.");
    const bool array=texture.textureType==MTLTextureType2DArray;
    if(!array && texture.textureType!=MTLTextureType2D)
        return reject("GPU texture upload supports only 2D images and arrays.");
    const auto slices=array ? texture.arrayLength : 1;
    const auto levels=texture.mipmapLevelCount;
    if(!levels || levels>16 || !slices || slices>kMaximumJobs ||
       levels>kMaximumJobs/slices || upload.levels.size()!=levels*slices ||
       !upload.source || !upload.bytes || upload.bytes>kMaximumBytes ||
       upload.bytes>impl->device.maxBufferLength ||
       reinterpret_cast<std::uintptr_t>(upload.source)>std::numeric_limits<std::uintptr_t>::max()-upload.bytes)
        return reject("GPU texture upload source or complete mip coverage exceeds its bounds.");
    struct Job {Params params;unsigned slice,level,width,height;};
    std::vector<Job> jobs;
    jobs.reserve(upload.levels.size());
    std::vector<bool> visited(levels*slices,false);
    std::uint64_t scratch_bytes=0;
    for(const auto& level:upload.levels) {
        const auto width_el=(level.width+block-1)/block,height_el=(level.height+block-1)/block;
        if(level.level>=levels || level.slice>=slices ||
           !(MacroThin(level.tile) || (impl->extended && MicroThin(level.tile))) ||
           level.width!=std::max<NSUInteger>(texture.width>>level.level,1) ||
           level.height!=std::max<NSUInteger>(texture.height>>level.level,1) ||
           level.pitch<width_el || level.pitch>16384 || level.src_offset%bytes_per_pixel)
            return reject("GPU texture upload level has an unsupported layout or extent.");
        const auto index=level.slice*levels+level.level;
        if(visited[index])return reject("GPU texture upload contains a duplicate slice/mip.");
        visited[index]=true;
        const auto source_bytes=Liverpool::Tiling::SliceSize(level.tile,level.pitch,height_el,bytes_per_pixel);
        const std::uint64_t row=(std::uint64_t(width_el)*bytes_per_pixel+255)&~255ull;
        const auto bytes=row*height_el;
        if(level.src_offset>upload.bytes || source_bytes>upload.bytes-level.src_offset ||
           bytes>kMaximumBytes || scratch_bytes>kMaximumBytes-bytes)
            return reject("GPU texture upload reads outside its physical source allocation.");
        jobs.push_back({{level.src_offset,scratch_bytes,width_el,height_el,
            level.pitch,std::uint32_t(row/4),level.tile,bytes_per_pixel/4},level.slice,level.level,level.width,level.height});
        scratch_bytes+=bytes;
    }
    if(!scratch_bytes || scratch_bytes>impl->device.maxBufferLength)
        return reject("GPU texture upload scratch allocation exceeds its bounds.");
    const bool packed=impl->batch_mips && jobs.size()>1;
    std::vector<PackedJob> packed_jobs;
    std::uint64_t group_count=0;
    if(packed) {
        packed_jobs.reserve(jobs.size());
        for(const auto& job:jobs) {
            const std::uint64_t across=(std::uint64_t(job.params.width)+7)/8;
            const auto down=(std::uint64_t(job.params.height)+7)/8;
            group_count+=across*down;
            if(!across || !down || group_count>std::numeric_limits<std::uint32_t>::max())
                return reject("GPU texture upload packed dispatch exceeds its group bounds.");
            packed_jobs.push_back({job.params,std::uint32_t(group_count),std::uint32_t(across)});
        }
    }
    // The renderer's outer scope supplies the frame. Match these narrower
    // optional scopes by source address and time; zero is the unknown frame.
    const auto source_address=reinterpret_cast<std::uintptr_t>(upload.source);
    id<MTLBuffer> snapshot=nil,scratch=nil,metadata=nil;
    std::uint64_t snapshot_offset=0,scratch_offset=0,metadata_offset=0;
    impl->BeginStaging(command);
    {
        StutterTrace::Scope timing("texture_gpu_snapshot_allocate",0,source_address,upload.bytes,1);
        snapshot=impl->Reserve(impl->snapshots,upload.bytes,MTLResourceStorageModeShared,snapshot_offset);
    }
    {
        StutterTrace::Scope timing("texture_gpu_scratch_allocate",0,source_address,scratch_bytes,1);
        scratch=impl->Reserve(impl->scratch,scratch_bytes,MTLResourceStorageModePrivate,scratch_offset);
    }
    if(!snapshot || !snapshot.contents || !scratch)
        return reject("GPU texture upload could not allocate its immutable snapshot and scratch.");
    if(packed) {
        StutterTrace::Scope timing("texture_gpu_job_metadata",0,source_address,packed_jobs.size(),1);
        metadata=impl->Reserve(impl->metadata,packed_jobs.size()*sizeof(PackedJob),
            MTLResourceStorageModeShared,metadata_offset,kMetadataPageBytes);
        if(!metadata || !metadata.contents)
            return reject("GPU texture upload could not allocate its immutable job metadata.");
        for(auto& job:packed_jobs) {
            job.params.source_offset+=snapshot_offset;
            job.params.destination_offset+=scratch_offset;
        }
        std::memcpy(static_cast<unsigned char*>(metadata.contents)+metadata_offset,
            packed_jobs.data(),packed_jobs.size()*sizeof(PackedJob));
    }
    auto* snapshot_bytes=static_cast<unsigned char*>(snapshot.contents)+snapshot_offset;
    mach_vm_size_t copied=0;
    kern_return_t copy_result;
    bool mapped_copy=false;
    {
        StutterTrace::Scope timing("texture_gpu_source_copy",0,source_address,upload.bytes,1);
        if(impl->mapped_snapshot)mapped_copy=runtime_gpu_memory_snapshot(source_address,snapshot_bytes,upload.bytes)!=0;
        if(mapped_copy) {copy_result=KERN_SUCCESS;copied=upload.bytes;}
        else copy_result=mach_vm_read_overwrite(mach_task_self(),reinterpret_cast<mach_vm_address_t>(upload.source),
                upload.bytes,reinterpret_cast<mach_vm_address_t>(snapshot_bytes),&copied);
    }
    if(copy_result!=KERN_SUCCESS || copied!=upload.bytes)
        return reject("GPU texture upload source snapshot was not readable.");
    std::vector<std::uint64_t> snapshot_chunks;
    if(initial_hashes) {
        StutterTrace::Scope timing("texture_gpu_snapshot_hash",0,source_address,upload.bytes,1);
        snapshot_chunks=ContentHash::Chunks(snapshot_bytes,upload.bytes);
        initial_hashes->bytes_hashed=upload.bytes;
    }

    // All validation, allocation and the one CPU snapshot finish before the
    // first encoder. Resource retention belongs to this existing command buffer.
    StutterTrace::Scope compute_timing("texture_gpu_compute_encode",0,source_address,upload.bytes,1);
    auto compute=[command computeCommandEncoder];
    if(!compute)return reject("GPU texture upload could not create its compute encoder.");
    [compute setComputePipelineState:packed ? impl->packed_pipeline : impl->pipeline];
    [compute setBuffer:snapshot offset:0 atIndex:0];
    [compute setBuffer:scratch offset:0 atIndex:1];
    if(packed) {
        const auto count=std::uint32_t(packed_jobs.size());
        [compute setBuffer:metadata offset:metadata_offset atIndex:2];
        [compute setBytes:&count length:sizeof(count) atIndex:3];
        [compute dispatchThreadgroups:MTLSizeMake(group_count,1,1)
            threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    } else {
        for(const auto& job:jobs) {
            auto params=job.params;params.source_offset+=snapshot_offset;params.destination_offset+=scratch_offset;
            [compute setBytes:&params length:sizeof(params) atIndex:2];
            [compute dispatchThreads:MTLSizeMake(job.params.width,job.params.height,1)
                threadsPerThreadgroup:MTLSizeMake(8,8,1)];
        }
    }
    [compute endEncoding];
    compute_timing.End();
    StutterTrace::Scope blit_timing("texture_gpu_blit_encode",0,source_address,upload.bytes,1);
    auto blit=[command blitCommandEncoder];
    if(!blit) {impl->error="GPU texture upload lost its blit encoder after encoding detile work.";return Result::Failed;}
    for(const auto& job:jobs) {
        const auto row=std::uint64_t(job.params.row_words)*4;
        [blit copyFromBuffer:scratch sourceOffset:scratch_offset+job.params.destination_offset sourceBytesPerRow:row
            sourceBytesPerImage:row*job.params.height
            sourceSize:MTLSizeMake(job.width,job.height,1) toTexture:texture
            destinationSlice:job.slice destinationLevel:job.level destinationOrigin:MTLOriginMake(0,0,0)];
    }
    [blit endEncoding];
    if(initial_hashes)initial_hashes->chunks=std::move(snapshot_chunks);
    ++impl->stats.uploads;impl->stats.jobs+=jobs.size();
    impl->stats.compute_dispatches+=packed ? 1 : jobs.size();
    impl->stats.packed_uploads+=packed;
    if(mapped_copy) {++impl->stats.mapped_snapshots;impl->stats.mapped_bytes+=upload.bytes;}
    else {++impl->stats.mach_snapshots;impl->stats.mach_bytes+=upload.bytes;}
    return Result::Encoded;
}
} // namespace Gpu::Metal
