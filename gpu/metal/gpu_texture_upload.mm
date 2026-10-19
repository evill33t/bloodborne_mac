// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/gpu_texture_upload.h"
#include "gpu/metal/stutter_trace.h"
#include "gpu/liverpool/tiling.h"
#include "common/content_hash.h"
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace Gpu::Metal {
namespace {
constexpr std::uint64_t kMaximumBytes=128ull<<20;
constexpr unsigned kMaximumJobs=512;
struct alignas(8) Params {
    std::uint64_t source_offset,destination_offset;
    std::uint32_t width,height,pitch,row_words,tile,pixel_words;
};
static_assert(sizeof(Params)==40);

// Four/eight-byte macro tiling only. The mapping matches original Liverpool
// MacroOffset/MicroIndex: 128x64 macro tiles, eight pipes, sixteen banks,
// bank-height one and 256-byte interleaving. Eight-byte micro tiles span two
// interleave blocks; insert pipe/bank bits after the complete byte offset.
// Raw uint loads/stores plus a blit preserve float/half NaNs and denormals,
// BGRA channel order, and sRGB encoded bytes without typed arithmetic.
constexpr const char* kSource=R"(
#include <metal_stdlib>
using namespace metal;
struct Layout {
    ulong source_offset,destination_offset;
    uint width,height,pitch,row_words,tile,pixel_words;
};
kernel void main0(device const uint* source [[buffer(0)]],
                  device uint* linear [[buffer(1)]],
                  constant Layout& layout [[buffer(2)]],
                  uint2 p [[thread_position_in_grid]]) {
    if(p.x>=layout.width || p.y>=layout.height)return;
    const uint x=p.x&7u,y=p.y&7u;
    uint micro;
    if(layout.tile==10u) {
        if(layout.pixel_words==2u) // eight-byte display: x0,y0,x1,x2,y1,y2
            micro=(x&1u)|((y&1u)<<1)|((x&6u)<<1)|((y&6u)<<3);
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
    const uint bank=((bx^(ty>>3))&1u)|(((bx^(ty>>1)^(ty>>2))&2u))|
                    (((bx^(ty<<1))&4u))|(((bx^(ty<<3))&8u));
    const uint padded_pitch=(layout.pitch+127u)&~127u;
    const ulong macro=(ulong(p.y>>6)*(padded_pitch>>7)+(p.x>>7))*
                      (256ul*layout.pixel_words);
    const ulong total=macro+ulong(micro)*(4ul*layout.pixel_words);
    const ulong offset=(total&255ul)|(ulong(pipe)<<8)|(ulong(bank)<<11)|
                       ((total>>8)<<15);
    const ulong destination=(layout.destination_offset>>2)+ulong(p.y)*layout.row_words+
                            ulong(p.x)*layout.pixel_words;
    const ulong input=(layout.source_offset+offset)>>2;
    linear[destination]=source[input];
    if(layout.pixel_words==2u)linear[destination+1]=source[input+1];
}
)";
bool MacroThin(unsigned tile) {return tile<=4 || tile==10 || tile==14;}
}

struct GpuTextureUploader::Impl {
    id<MTLDevice> device;
    id<MTLComputePipelineState> pipeline;
    std::string error;
};

GpuTextureUploader::GpuTextureUploader(void* handle):impl(std::make_unique<Impl>()) {
    impl->device=(__bridge id<MTLDevice>)handle;
    if(!impl->device){impl->error="GPU texture uploader has no Metal device.";return;}
    NSError* error=nil;
    auto library=[impl->device newLibraryWithSource:@(kSource) options:nil error:&error];
    if(library)impl->pipeline=[impl->device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"main0"] error:&error];
    if(!impl->pipeline)impl->error=error ? error.localizedDescription.UTF8String :
        "GPU texture upload pipeline could not be created.";
}
GpuTextureUploader::~GpuTextureUploader()=default;
bool GpuTextureUploader::Ready() const {return impl->pipeline!=nil;}
const std::string& GpuTextureUploader::Error() const {return impl->error;}
std::uint32_t GpuTextureUploader::BytesPerPixel(std::uint64_t format) {
    switch(format) {
    case MTLPixelFormatR32Float:
    case MTLPixelFormatRGBA8Unorm: case MTLPixelFormatRGBA8Unorm_sRGB:
    case MTLPixelFormatBGRA8Unorm: case MTLPixelFormatBGRA8Unorm_sRGB:return 4;
    case MTLPixelFormatRGBA16Float: case MTLPixelFormatRG32Float:return 8;
    default:return 0;
    }
}
bool GpuTextureUploader::SupportsFormat(std::uint64_t format) {return BytesPerPixel(format)!=0;}

GpuTextureUploader::Result GpuTextureUploader::Encode(void* command_handle,void* texture_handle,
                                                     const Upload& upload,InitialHashes* initial_hashes) {
    if(initial_hashes)*initial_hashes={};
    if(!Ready())return Result::Fallback;
    impl->error.clear();
    auto command=(__bridge id<MTLCommandBuffer>)command_handle;
    auto texture=(__bridge id<MTLTexture>)texture_handle;
    const auto bytes_per_pixel=BytesPerPixel(texture.pixelFormat);
    const auto reject=[&](const char* reason){impl->error=reason;return Result::Fallback;};
    if(!command || !texture || command.commandQueue.device!=impl->device ||
       texture.device!=impl->device || !command.retainedReferences ||
       command.status>=MTLCommandBufferStatusCommitted || texture.framebufferOnly ||
       texture.storageMode==MTLStorageModeMemoryless || !bytes_per_pixel)
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
    struct Job {Params params;unsigned slice,level;};
    std::vector<Job> jobs;
    std::vector<bool> visited(levels*slices,false);
    std::uint64_t scratch_bytes=0;
    for(const auto& level:upload.levels) {
        if(level.level>=levels || level.slice>=slices || !MacroThin(level.tile) ||
           level.width!=std::max<NSUInteger>(texture.width>>level.level,1) ||
           level.height!=std::max<NSUInteger>(texture.height>>level.level,1) ||
           level.pitch<level.width || level.pitch>16384 || level.src_offset%bytes_per_pixel)
            return reject("GPU texture upload level has an unsupported layout or extent.");
        const auto index=level.slice*levels+level.level;
        if(visited[index])return reject("GPU texture upload contains a duplicate slice/mip.");
        visited[index]=true;
        const auto source_bytes=Liverpool::Tiling::SliceSize(level.tile,level.pitch,level.height,bytes_per_pixel);
        const std::uint64_t row=(std::uint64_t(level.width)*bytes_per_pixel+255)&~255ull;
        const auto bytes=row*level.height;
        if(level.src_offset>upload.bytes || source_bytes>upload.bytes-level.src_offset ||
           bytes>kMaximumBytes || scratch_bytes>kMaximumBytes-bytes)
            return reject("GPU texture upload reads outside its physical source allocation.");
        jobs.push_back({{level.src_offset,scratch_bytes,level.width,level.height,
            level.pitch,std::uint32_t(row/4),level.tile,bytes_per_pixel/4},level.slice,level.level});
        scratch_bytes+=bytes;
    }
    if(!scratch_bytes || scratch_bytes>impl->device.maxBufferLength)
        return reject("GPU texture upload scratch allocation exceeds its bounds.");
    // The renderer's outer scope supplies the frame. Match these narrower
    // optional scopes by source address and time; zero is the unknown frame.
    const auto source_address=reinterpret_cast<std::uintptr_t>(upload.source);
    id<MTLBuffer> snapshot=nil,scratch=nil;
    {
        StutterTrace::Scope timing("texture_gpu_snapshot_allocate",0,source_address,upload.bytes,1);
        snapshot=[impl->device newBufferWithLength:upload.bytes options:MTLResourceStorageModeShared];
    }
    {
        StutterTrace::Scope timing("texture_gpu_scratch_allocate",0,source_address,scratch_bytes,1);
        scratch=[impl->device newBufferWithLength:scratch_bytes options:MTLResourceStorageModePrivate];
    }
    if(!snapshot || !snapshot.contents || !scratch)
        return reject("GPU texture upload could not allocate its immutable snapshot and scratch.");
    mach_vm_size_t copied=0;
    kern_return_t copy_result;
    {
        StutterTrace::Scope timing("texture_gpu_source_copy",0,source_address,upload.bytes,1);
        copy_result=mach_vm_read_overwrite(mach_task_self(),reinterpret_cast<mach_vm_address_t>(upload.source),
            upload.bytes,reinterpret_cast<mach_vm_address_t>(snapshot.contents),&copied);
    }
    if(copy_result!=KERN_SUCCESS || copied!=upload.bytes)
        return reject("GPU texture upload source snapshot was not readable.");
    std::vector<std::uint64_t> snapshot_chunks;
    if(initial_hashes) {
        StutterTrace::Scope timing("texture_gpu_snapshot_hash",0,source_address,upload.bytes,1);
        snapshot_chunks=ContentHash::Chunks(snapshot.contents,upload.bytes);
        initial_hashes->bytes_hashed=upload.bytes;
    }

    // All validation, allocation and the one CPU snapshot finish before the
    // first encoder. Resource retention belongs to this existing command buffer.
    StutterTrace::Scope compute_timing("texture_gpu_compute_encode",0,source_address,upload.bytes,1);
    auto compute=[command computeCommandEncoder];
    if(!compute)return reject("GPU texture upload could not create its compute encoder.");
    [compute setComputePipelineState:impl->pipeline];
    [compute setBuffer:snapshot offset:0 atIndex:0];
    [compute setBuffer:scratch offset:0 atIndex:1];
    for(const auto& job:jobs) {
        [compute setBytes:&job.params length:sizeof(job.params) atIndex:2];
        [compute dispatchThreads:MTLSizeMake(job.params.width,job.params.height,1)
            threadsPerThreadgroup:MTLSizeMake(8,8,1)];
    }
    [compute endEncoding];
    compute_timing.End();
    StutterTrace::Scope blit_timing("texture_gpu_blit_encode",0,source_address,upload.bytes,1);
    auto blit=[command blitCommandEncoder];
    if(!blit) {impl->error="GPU texture upload lost its blit encoder after encoding detile work.";return Result::Failed;}
    for(const auto& job:jobs) {
        const auto row=std::uint64_t(job.params.row_words)*4;
        [blit copyFromBuffer:scratch sourceOffset:job.params.destination_offset sourceBytesPerRow:row
            sourceBytesPerImage:row*job.params.height
            sourceSize:MTLSizeMake(job.params.width,job.params.height,1) toTexture:texture
            destinationSlice:job.slice destinationLevel:job.level destinationOrigin:MTLOriginMake(0,0,0)];
    }
    [blit endEncoding];
    if(initial_hashes)initial_hashes->chunks=std::move(snapshot_chunks);
    return Result::Encoded;
}
} // namespace Gpu::Metal
