// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the production detiler with its original address calculation, then
// sample nonuniform guest textures through translated GCN and the real cache.
#import <Metal/Metal.h>
#include "gpu/liverpool/tiling.h"
#include "gpu/liverpool/registers.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/metal/gpu_texture_upload.h"
#include "common/content_hash.h"
#include "src/runtime_gpu_memory.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

extern "C" void runtime_restart() { std::abort(); }
using namespace Gpu::Liverpool;

static void CpuCase(unsigned tile,unsigned width,unsigned height,unsigned pitch,
                    unsigned bytes,unsigned misalignment,unsigned z=0) {
    constexpr size_t guard=64;
    const size_t size=Tiling::SliceSize(tile,pitch,height,bytes);
    const size_t visible=size_t(width)*height*bytes;
    std::vector<unsigned char> source(size+guard*2+misalignment,0xb7);
    std::vector<unsigned char> reference(visible+guard*2+misalignment,0xa5),actual=reference;
    auto* src=source.data()+guard+misalignment;
    auto* ref=reference.data()+guard+misalignment;
    auto* dst=actual.data()+guard+misalignment;
    // Distinct bytes throughout pipe, bank and macro-tile boundaries.
    uint32_t random=0x983271ab;
    for(size_t n=0;n<size;++n) {random^=random<<13;random^=random>>17;random^=random<<5;src[n]=random;}
    Tiling::DetileSliceReference(tile,src,ref,width,height,pitch,bytes,z);
    Tiling::DetileSlice(tile,src,dst,width,height,pitch,bytes,z);
    if(actual!=reference) {
        fprintf(stderr,"Detile mismatch tile=%u %ux%u pitch=%u bytes=%u misalign=%u z=%u\n",
            tile,width,height,pitch,bytes,misalignment,z);
        std::abort();
    }
    for(size_t n=0;n<guard+misalignment;++n) assert(actual[n]==0xa5 && source[n]==0xb7);
    for(size_t n=guard+misalignment+visible;n<actual.size();++n) assert(actual[n]==0xa5);
    for(size_t n=guard+misalignment+size;n<source.size();++n) assert(source[n]==0xb7);
}

static void CpuTests() {
    const auto start=std::chrono::steady_clock::now();
    const std::array<std::array<unsigned,2>,11> sizes{{
        {1,1},{3,7},{8,8},{9,9},{63,65},{127,63},{129,65},{257,131},
        {1025,513},{4095,129},{4096,4096}}};
    unsigned cases=0;
    for(unsigned bytes:{1u,2u,4u})
        for(unsigned tile:{0u,1u,2u,3u,4u,10u,14u}) for(const auto size:sizes)
            for(unsigned alignment:{0u,1u,3u}) {
                CpuCase(tile,size[0],size[1],size[0]+(alignment ? 17 : 0),bytes,alignment);++cases;
            }
    // Also cover 8/16-byte, linear, micro-tiled and thick fallback callers.
    for(unsigned tile:{0u,4u,10u,14u,5u,9u,13u,8u,31u,19u})
        for(unsigned bytes:{1u,2u,4u,8u,16u})
            for(unsigned z=0;z<Tiling::Thickness(tile);++z) {
                CpuCase(tile,37,19,53,bytes,1,z);++cases;
            }
    // Include an odd final fragment, not only whole macro-tile allocations.
    std::vector<unsigned char> hash_bytes(Gpu::ContentHash::kChunkBytes+37,0x19);
    const auto old_hashes=Gpu::ContentHash::Chunks(hash_bytes.data(),hash_bytes.size());
    assert(old_hashes.size()==2);
    assert(old_hashes[0]==Gpu::ContentHash::Bytes(hash_bytes.data(),Gpu::ContentHash::kChunkBytes));
    assert(old_hashes[1]==Gpu::ContentHash::Bytes(hash_bytes.data()+Gpu::ContentHash::kChunkBytes,37));
    hash_bytes.back()^=0x80;
    const auto new_hashes=Gpu::ContentHash::Chunks(hash_bytes.data(),hash_bytes.size());
    assert(old_hashes[0]==new_hashes[0] && old_hashes[1]!=new_hashes[1]);
    assert(Gpu::ContentHash::Chunks(nullptr,0).empty());
    printf("Detile: %u byte-exact CPU cases, odd/large edges, padded pitches, unaligned pointers and canaries (%.3fs; mode=%s)\n",
        cases,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),
        std::getenv("BB_METAL_FAST_DETILE"));
}

static std::array<unsigned char,4> Pattern(unsigned x,unsigned y,unsigned version,bool floating) {
    const unsigned char red=(x*17+y*29+version*61)&255;
    return {red,static_cast<unsigned char>(floating ? 0 : (x*43+y*7+version*37)&255),
        static_cast<unsigned char>(floating ? 0 : (x*11+y*53+version*83)&255),255};
}

static bool GpuUploadBitsTests() {
    auto device=MTLCreateSystemDefaultDevice();
    auto queue=[device newCommandQueue];
    Gpu::Metal::GpuTextureUploader uploader((__bridge void*)device);
    if(!uploader.Ready()) {fprintf(stderr,"%s\n",uploader.Error().c_str());return false;}
    const bool packed=std::getenv("BB_METAL_GPU_BATCH_MIPS") &&
        !std::strcmp(std::getenv("BB_METAL_GPU_BATCH_MIPS"),"1");
    unsigned cases=0;
    for(unsigned tile:{0u,1u,2u,3u,4u,5u,9u,10u,13u,14u})
    for(MTLPixelFormat format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,
            MTLPixelFormatRGBA8Unorm_sRGB,MTLPixelFormatBGRA8Unorm,MTLPixelFormatBGRA8Unorm_sRGB,
            MTLPixelFormatRGBA16Float,MTLPixelFormatRG32Float,
            MTLPixelFormatBC1_RGBA,MTLPixelFormatBC1_RGBA_sRGB,
            MTLPixelFormatBC2_RGBA,MTLPixelFormatBC2_RGBA_sRGB,
            MTLPixelFormatBC3_RGBA,MTLPixelFormatBC3_RGBA_sRGB,
            MTLPixelFormatBC4_RUnorm,MTLPixelFormatBC4_RSnorm,
            MTLPixelFormatBC5_RGUnorm,MTLPixelFormatBC5_RGSnorm,
            MTLPixelFormatBC6H_RGBFloat,MTLPixelFormatBC6H_RGBUfloat,
            MTLPixelFormatBC7_RGBAUnorm,MTLPixelFormatBC7_RGBAUnorm_sRGB}) {
        constexpr unsigned width=1025,height=129;
        const unsigned bpe=Gpu::Metal::GpuTextureUploader::BytesPerElement(format),words=bpe/4;
        const auto block=Gpu::Metal::GpuTextureUploader::BlockExtent(format);
        const unsigned width_el=(width+block-1)/block,height_el=(height+block-1)/block,pitch=width_el+17;
        assert(bpe==4 || bpe==8 || bpe==16);
        const auto size=Tiling::SliceSize(tile,pitch,height_el,bpe);
        std::vector<uint32_t> source(size/4),reference(width_el*height_el*words);
        uint32_t random=0x918372aa;
        for(auto& word:source) {random^=random<<13;random^=random>>17;random^=random<<5;word=random;}
        // Include special float32 and packed float16 encodings. Typed loads
        // could canonicalize NaNs/denormals or lose the second raw word.
        const uint32_t special[]{0,0x80000000,1,0x007fffff,0x7f800000,0xff800000,0x7fc12345,0x7fa54321,
            0x03ff0001,0x7c00fc00,0x7e017c01,0x3555bbff,0xfc007bff,0xfe017d55};
        std::copy(std::begin(special),std::end(special),source.begin());
        Tiling::DetileSliceReference(tile,reinterpret_cast<const unsigned char*>(source.data()),
            reinterpret_cast<unsigned char*>(reference.data()),width_el,height_el,pitch,bpe);
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
        descriptor.storageMode=MTLStorageModePrivate;
        auto texture=[device newTextureWithDescriptor:descriptor];
        const size_t row=(width_el*bpe+255)&~255u;
        auto readback=[device newBufferWithLength:row*height_el options:MTLResourceStorageModeShared];
        auto command=[queue commandBuffer];
        Gpu::Metal::GpuTextureUploader::Upload upload{source.data(),size,{{0,width,height,pitch,tile,0,0}}};
        Gpu::Metal::GpuTextureUploader::InitialHashes snapshot_hashes;
        const auto expected_hashes=Gpu::ContentHash::Chunks(source.data(),size);
        using Result=Gpu::Metal::GpuTextureUploader::Result;
        // Preflight rejects malformed coverage/ranges without poisoning the
        // command buffer; the subsequent valid request must still execute.
        auto invalid=upload;invalid.levels[0].src_offset=size;
        snapshot_hashes.chunks={123};snapshot_hashes.bytes_hashed=999;
        assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,invalid,&snapshot_hashes)==Result::Fallback);
        assert(snapshot_hashes.chunks.empty() && !snapshot_hashes.bytes_hashed);
        invalid=upload;invalid.levels.push_back(invalid.levels[0]);
        assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,invalid)==Result::Fallback);
        invalid=upload;invalid.levels[0].src_offset=bpe==8 ? 4 : 2;
        assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,invalid)==Result::Fallback);
        if(!tile && format==MTLPixelFormatR32Float) {
            invalid=upload;invalid.source=reinterpret_cast<const void*>(1);
            assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,invalid,&snapshot_hashes)==Result::Fallback);
            assert(snapshot_hashes.chunks.empty() && !snapshot_hashes.bytes_hashed);
            assert(size%Gpu::ContentHash::kChunkBytes); // this allocation ends in a partial chunk
        }
        if(uploader.Encode((__bridge void*)command,(__bridge void*)texture,upload,&snapshot_hashes)!=Result::Encoded) {
            fprintf(stderr,"GPU upload rejected tile=%u format=%lu: %s\n",tile,(unsigned long)format,uploader.Error().c_str());return false;
        }
        // The GPU has not been submitted. A changed source must not change
        // the immutable texture version or its corresponding hash baseline.
        assert(snapshot_hashes.chunks==expected_hashes && snapshot_hashes.bytes_hashed==size);
        std::fill(source.begin(),source.end(),0xdeadc0de);
        assert(snapshot_hashes.chunks!=Gpu::ContentHash::Chunks(source.data(),size));
        auto blit=[command blitCommandEncoder];
        [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:row destinationBytesPerImage:row*height_el];
        [blit endEncoding];[command commit];[command waitUntilCompleted];
        if(command.error) {fprintf(stderr,"%s\n",command.error.localizedDescription.UTF8String);return false;}
        for(unsigned y=0;y<height_el;++y)
            if(memcmp(static_cast<const unsigned char*>(readback.contents)+y*row,reference.data()+y*width_el*words,width_el*bpe)) {
                fprintf(stderr,"GPU raw upload mismatch tile=%u format=%lu row=%u\n",tile,(unsigned long)format,y);return false;
            }
        ++cases;
    }
    // Six visible cube faces occupy eight physical slices per mip. Exercise
    // that padding and independent mip dimensions in the generic upload path.
    for(MTLPixelFormat format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,
            MTLPixelFormatRGBA16Float,MTLPixelFormatRG32Float,
            MTLPixelFormatBC1_RGBA,MTLPixelFormatBC3_RGBA,
            MTLPixelFormatBC5_RGSnorm,MTLPixelFormatBC7_RGBAUnorm_sRGB}) {
        constexpr unsigned width=257,height=129,levels=9,slices=6,storage=8;
        const unsigned bpe=Gpu::Metal::GpuTextureUploader::BytesPerElement(format),words=bpe/4;
        const auto block=Gpu::Metal::GpuTextureUploader::BlockExtent(format);
        Gpu::Metal::GpuTextureUploader::Upload upload;
        std::vector<std::vector<uint32_t>> references;
        size_t total=0;
        Tiling::MipLevel mips[levels];
        const auto total_layout=Tiling::MipLayout(0,width,height,storage,levels,bpe,block>1,true,mips);
        for(unsigned lv=0;lv<levels;++lv) {
            const unsigned w=std::max(width>>lv,1u),h=std::max(height>>lv,1u);
            const auto& m=mips[lv];
            for(unsigned slice=0;slice<slices;++slice)
                upload.levels.push_back({m.offset+m.slice_size*slice,w,h,m.pitch_el,m.tile_index,slice,lv});
        }
        // Coverage order is intentionally unrelated to mip or slice order.
        // The packed dispatch must map each group to its supplied job.
        std::reverse(upload.levels.begin(),upload.levels.end());
        total=total_layout;
        std::vector<uint32_t> source(total/4);
        for(size_t n=0;n<source.size();++n)source[n]=uint32_t(n*0x9e3779b9u)^uint32_t(n>>7);
        for(const auto& level:upload.levels) {
            const auto w=(level.width+block-1)/block,h=(level.height+block-1)/block;
            references.emplace_back(w*h*words);
            Tiling::DetileSliceReference(level.tile,reinterpret_cast<const unsigned char*>(source.data())+level.src_offset,
                reinterpret_cast<unsigned char*>(references.back().data()),w,h,level.pitch,bpe);
        }
        upload.source=source.data();upload.bytes=total;
        auto descriptor=[MTLTextureDescriptor new];descriptor.textureType=MTLTextureType2DArray;
        descriptor.pixelFormat=format;descriptor.width=width;descriptor.height=height;
        descriptor.arrayLength=slices;descriptor.mipmapLevelCount=levels;descriptor.storageMode=MTLStorageModeManaged;
        auto texture=[device newTextureWithDescriptor:descriptor];auto command=[queue commandBuffer];
        using Result=Gpu::Metal::GpuTextureUploader::Result;
        auto invalid=upload;invalid.levels.pop_back();
        assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,invalid)==Result::Fallback);
        if(uploader.Encode((__bridge void*)command,(__bridge void*)texture,upload)!=Result::Encoded)return false;
        std::fill(source.begin(),source.end(),0xbadc0ffe);
        std::vector<id<MTLBuffer>> outputs;
        auto blit=[command blitCommandEncoder];
        for(const auto& level:upload.levels) {
            const auto w=(level.width+block-1)/block,h=(level.height+block-1)/block;
            const size_t row=(w*bpe+255)&~255u;
            auto output=[device newBufferWithLength:row*h options:MTLResourceStorageModeShared];
            outputs.push_back(output);
            [blit copyFromTexture:texture sourceSlice:level.slice sourceLevel:level.level sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(level.width,level.height,1) toBuffer:output destinationOffset:0
                destinationBytesPerRow:row destinationBytesPerImage:row*h];
        }
        [blit endEncoding];[command commit];[command waitUntilCompleted];if(command.error)return false;
        for(size_t n=0;n<upload.levels.size();++n) {
            const auto& level=upload.levels[n];
            const auto w=(level.width+block-1)/block,h=(level.height+block-1)/block;
            const size_t row=(w*bpe+255)&~255u;
            for(unsigned y=0;y<h;++y)
                if(memcmp(static_cast<const unsigned char*>(outputs[n].contents)+y*row,
                    references[n].data()+y*w*words,w*bpe))return false;
            ++cases;
        }
    }
    // Every supported format also uses a real multi-job dispatch. Include odd
    // compressed edges; raw-byte equality covers NaNs and invalid BC blocks.
    for(MTLPixelFormat format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm,
            MTLPixelFormatRGBA8Unorm_sRGB,MTLPixelFormatBGRA8Unorm,MTLPixelFormatBGRA8Unorm_sRGB,
            MTLPixelFormatRGBA16Float,MTLPixelFormatRG32Float,
            MTLPixelFormatBC1_RGBA,MTLPixelFormatBC1_RGBA_sRGB,
            MTLPixelFormatBC2_RGBA,MTLPixelFormatBC2_RGBA_sRGB,
            MTLPixelFormatBC3_RGBA,MTLPixelFormatBC3_RGBA_sRGB,
            MTLPixelFormatBC4_RUnorm,MTLPixelFormatBC4_RSnorm,
            MTLPixelFormatBC5_RGUnorm,MTLPixelFormatBC5_RGSnorm,
            MTLPixelFormatBC6H_RGBFloat,MTLPixelFormatBC6H_RGBUfloat,
            MTLPixelFormatBC7_RGBAUnorm,MTLPixelFormatBC7_RGBAUnorm_sRGB}) {
        constexpr unsigned width=19,height=11,slices=3;
        const unsigned bpe=Gpu::Metal::GpuTextureUploader::BytesPerElement(format);
        const auto block=Gpu::Metal::GpuTextureUploader::BlockExtent(format);
        const unsigned w=(width+block-1)/block,h=(height+block-1)/block;
        const auto slice_size=Tiling::SliceSize(13,w,h,bpe);
        std::vector<unsigned char> source(slice_size*slices);
        for(size_t n=0;n<source.size();++n)source[n]=(n*37+(n>>7)*61)&255;
        std::vector<std::vector<unsigned char>> reference(slices,std::vector<unsigned char>(w*h*bpe));
        Gpu::Metal::GpuTextureUploader::Upload upload{source.data(),source.size(),{}};
        for(unsigned slice:{2u,0u,1u}) {
            upload.levels.push_back({slice*slice_size,width,height,w,13,slice,0});
            Tiling::DetileSliceReference(13,source.data()+slice*slice_size,reference[slice].data(),w,h,w,bpe);
        }
        auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
        d.textureType=MTLTextureType2DArray;d.arrayLength=slices;d.storageMode=MTLStorageModePrivate;
        auto texture=[device newTextureWithDescriptor:d];auto command=[queue commandBuffer];
        const auto before=uploader.GetStats();
        Gpu::Metal::GpuTextureUploader::InitialHashes hashes;
        const auto expected_hashes=Gpu::ContentHash::Chunks(source.data(),source.size());
        assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,upload,&hashes)==Gpu::Metal::GpuTextureUploader::Result::Encoded);
        const auto after=uploader.GetStats();
        assert(after.jobs-before.jobs==slices && after.compute_dispatches-before.compute_dispatches==(packed ? 1 : slices));
        assert(after.packed_uploads-before.packed_uploads==unsigned(packed));
        assert(hashes.chunks==expected_hashes && hashes.bytes_hashed==source.size());
        std::fill(source.begin(),source.end(),0xba);
        const size_t row=(w*bpe+255)&~255u,plane=row*h;
        auto output=[device newBufferWithLength:plane*slices options:MTLResourceStorageModeShared];
        auto blit=[command blitCommandEncoder];
        for(unsigned slice=0;slice<slices;++slice)
            [blit copyFromTexture:texture sourceSlice:slice sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:output destinationOffset:plane*slice
                destinationBytesPerRow:row destinationBytesPerImage:plane];
        [blit endEncoding];[command commit];[command waitUntilCompleted];
        if(command.error)return false;
        for(unsigned slice=0;slice<slices;++slice) for(unsigned y=0;y<h;++y)
            if(memcmp(static_cast<unsigned char*>(output.contents)+plane*slice+row*y,
                reference[slice].data()+w*bpe*y,w*bpe))return false;
        cases+=slices;
    }
    // Exercise the 512-job limit and >4 KiB metadata. Five uploads overflow
    // two metadata pages on one unsubmitted command. Advancing to a different
    // command and back must preserve every still-queued source/scratch/record.
    {
        constexpr unsigned slices=512,versions=8;
        constexpr auto format=MTLPixelFormatBC1_RGBA;
        const auto slice_size=Tiling::SliceSize(13,1,1,8);
        const size_t row=256;
        auto first=[queue commandBuffer],second=[queue commandBuffer];
        struct Check {id<MTLTexture> texture;id<MTLBuffer> output;std::vector<unsigned char> expected;};
        std::vector<Check> checks;
        for(unsigned version=0;version<versions;++version) {
            std::vector<unsigned char> source(slice_size*slices),expected(slices*8);
            for(size_t n=0;n<source.size();++n)source[n]=(n*19+(n>>8)*43+version*71)&255;
            Gpu::Metal::GpuTextureUploader::Upload upload{source.data(),source.size(),{}};
            for(unsigned job=0;job<slices;++job) {
                const unsigned slice=(job*73)%slices;
                upload.levels.push_back({slice_size*slice,1,1,1,13,slice,0});
                Tiling::DetileSliceReference(13,source.data()+slice_size*slice,expected.data()+slice*8,1,1,1,8);
            }
            auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:1 height:1 mipmapped:NO];
            d.textureType=MTLTextureType2DArray;d.arrayLength=slices;d.storageMode=MTLStorageModePrivate;
            auto texture=[device newTextureWithDescriptor:d];
            auto command=(version<5 || version==7) ? first : second;
            const auto before=uploader.GetStats();
            Gpu::Metal::GpuTextureUploader::InitialHashes hashes;
            const auto expected_hashes=Gpu::ContentHash::Chunks(source.data(),source.size());
            assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,upload,&hashes)==Gpu::Metal::GpuTextureUploader::Result::Encoded);
            const auto after=uploader.GetStats();
            assert(after.jobs-before.jobs==slices && after.compute_dispatches-before.compute_dispatches==(packed ? 1 : slices));
            assert(hashes.chunks==expected_hashes);
            std::fill(source.begin(),source.end(),0xdd);
            checks.push_back({texture,[device newBufferWithLength:row*slices options:MTLResourceStorageModeShared],std::move(expected)});
        }
        for(unsigned which:{1u,0u}) {
            auto command=which ? second : first;auto blit=[command blitCommandEncoder];
            for(unsigned version=0;version<versions;++version) {
                if(unsigned(version>=5 && version<7)!=which)continue;
                const auto& check=checks[version];
                for(unsigned slice=0;slice<slices;++slice)
                    [blit copyFromTexture:check.texture sourceSlice:slice sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                        sourceSize:MTLSizeMake(1,1,1) toBuffer:check.output destinationOffset:row*slice
                        destinationBytesPerRow:row destinationBytesPerImage:row];
            }
            [blit endEncoding];[command commit];[command waitUntilCompleted];if(command.error)return false;
        }
        for(const auto& check:checks) for(unsigned slice=0;slice<slices;++slice)
            if(memcmp(static_cast<unsigned char*>(check.output.contents)+row*slice,check.expected.data()+slice*8,8))return false;
        cases+=slices*versions;
    }
    // Multiple page rollovers on one unsubmitted command, then a different
    // command submitted first. Neither source nor scratch ranges may be reused
    // while either command still owns them, even if the arena owner advances.
    for(MTLPixelFormat format:{MTLPixelFormatBC1_RGBA,MTLPixelFormatBC3_RGBA}) {
        constexpr unsigned width=513,height=513,block_width=129,block_height=129,pitch=137;
        const auto bpe=Gpu::Metal::GpuTextureUploader::BytesPerElement(format);
        const auto size=Tiling::SliceSize(14,pitch,block_height,bpe);
        const size_t row=(block_width*bpe+255)&~255u;
        auto first=[queue commandBuffer],second=[queue commandBuffer];
        struct Check {id<MTLTexture> texture;id<MTLBuffer> output;std::vector<unsigned char> expected;};
        std::vector<Check> checks;
        for(unsigned version=0;version<32;++version) {
            std::vector<unsigned char> source(size),expected(block_width*block_height*bpe);
            for(size_t i=0;i<source.size();++i)source[i]=(i*17+(i>>9)*31+version*53)&255;
            Tiling::DetileSliceReference(14,source.data(),expected.data(),block_width,block_height,pitch,bpe);
            auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
            d.storageMode=MTLStorageModePrivate;
            auto texture=[device newTextureWithDescriptor:d];
            auto command=version<24 ? first : second;
            Gpu::Metal::GpuTextureUploader::Upload upload{source.data(),size,{{0,width,height,pitch,14,0,0}}};
            Gpu::Metal::GpuTextureUploader::InitialHashes hashes;
            assert(uploader.Encode((__bridge void*)command,(__bridge void*)texture,upload,&hashes)==Gpu::Metal::GpuTextureUploader::Result::Encoded);
            assert(hashes.chunks==Gpu::ContentHash::Chunks(source.data(),size));
            std::fill(source.begin(),source.end(),0xdb);
            checks.push_back({texture,[device newBufferWithLength:row*block_height options:MTLResourceStorageModeShared],std::move(expected)});
        }
        for(unsigned which:{1u,0u}) {
            auto command=which ? second : first;
            auto blit=[command blitCommandEncoder];
            for(unsigned n=which?24:0;n<(which?32:24);++n) {
                auto& check=checks[n];
                [blit copyFromTexture:check.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(width,height,1) toBuffer:check.output destinationOffset:0
                    destinationBytesPerRow:row destinationBytesPerImage:row*block_height];
            }
            [blit endEncoding];[command commit];[command waitUntilCompleted];
            if(command.error){fprintf(stderr,"Staging page test: %s\n",command.error.localizedDescription.UTF8String);return false;}
        }
        for(auto& check:checks) {
            for(unsigned y=0;y<block_height;++y)
                if(memcmp(static_cast<unsigned char*>(check.output.contents)+y*row,
                    check.expected.data()+y*block_width*bpe,block_width*bpe))return false;
            ++cases;
        }
    }
    // Exercise the runtime-backed snapshot integration, not only its host
    // fallback. Both queued versions must keep their exact initial hashes and
    // pixels after the guest changes; include padding and a partial hash chunk.
    {
        constexpr unsigned width=19,height=11,slices=3,bpe=4;
        constexpr size_t allocation=131072,source_bytes=Gpu::ContentHash::kChunkBytes+37;
        uint64_t physical=0;uintptr_t address=0;
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),allocation,16384,0,&physical)==0);
        assert(runtime_gpu_memory_map(&address,allocation,3,0,physical,16384)==0);
        auto* source=reinterpret_cast<unsigned char*>(address);
        const auto slice_size=Tiling::SliceSize(13,width,height,bpe);
        assert(slice_size*slices<source_bytes);
        Gpu::Metal::GpuTextureUploader::Upload upload{source,source_bytes,{}};
        for(unsigned slice:{1u,2u,0u})upload.levels.push_back({slice*slice_size,width,height,width,13,slice,0});
        const auto before=uploader.GetStats();
        const size_t row=256,plane=row*height;
        auto command=[queue commandBuffer];
        std::vector<id<MTLTexture>> textures;
        std::vector<std::vector<unsigned char>> expected;
        for(unsigned version=0;version<2;++version) {
            for(size_t n=0;n<source_bytes;++n)source[n]=(n*43+(n>>7)*17+version*83)&255;
            expected.emplace_back(width*height*bpe*slices);
            for(unsigned slice=0;slice<slices;++slice)
                Tiling::DetileSliceReference(13,source+slice_size*slice,
                    expected.back().data()+width*height*bpe*slice,width,height,width,bpe);
            auto d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float width:width height:height mipmapped:NO];
            d.textureType=MTLTextureType2DArray;d.arrayLength=slices;d.storageMode=MTLStorageModePrivate;
            textures.push_back([device newTextureWithDescriptor:d]);
            const auto expected_hashes=Gpu::ContentHash::Chunks(source,source_bytes);
            Gpu::Metal::GpuTextureUploader::InitialHashes hashes;
            assert(uploader.Encode((__bridge void*)command,(__bridge void*)textures.back(),upload,&hashes)==Gpu::Metal::GpuTextureUploader::Result::Encoded);
            assert(hashes.chunks==expected_hashes && hashes.bytes_hashed==source_bytes);
        }
        std::memset(source,0xda,source_bytes);
        auto output=[device newBufferWithLength:plane*slices*2 options:MTLResourceStorageModeShared];
        auto blit=[command blitCommandEncoder];
        for(unsigned version=0;version<2;++version) for(unsigned slice=0;slice<slices;++slice)
            [blit copyFromTexture:textures[version] sourceSlice:slice sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:output destinationOffset:plane*(version*slices+slice)
                destinationBytesPerRow:row destinationBytesPerImage:plane];
        [blit endEncoding];[command commit];[command waitUntilCompleted];if(command.error)return false;
        for(unsigned version=0;version<2;++version) for(unsigned slice=0;slice<slices;++slice) for(unsigned y=0;y<height;++y)
            if(memcmp(static_cast<unsigned char*>(output.contents)+plane*(version*slices+slice)+row*y,
                expected[version].data()+(slice*height+y)*width*bpe,width*bpe))return false;
        const auto after=uploader.GetStats();
        const bool mapped=std::getenv("BB_METAL_GPU_MAPPED_SNAPSHOT") &&
            !std::strcmp(std::getenv("BB_METAL_GPU_MAPPED_SNAPSHOT"),"1");
        assert(after.mapped_snapshots-before.mapped_snapshots==(mapped ? 2 : 0));
        assert(after.mach_snapshots-before.mach_snapshots==(mapped ? 0 : 2));
        assert(after.mapped_bytes-before.mapped_bytes==(mapped ? source_bytes*2 : 0));
        cases+=2*slices;
    }
    const auto stats=uploader.GetStats();
    if(packed)assert(stats.packed_uploads && stats.compute_dispatches<stats.jobs);
    else assert(!stats.packed_uploads && stats.compute_dispatches==stats.jobs);
    assert(stats.mapped_snapshots+stats.mach_snapshots==stats.uploads && stats.mach_snapshots>0);
    printf("GPU upload: %u raw-bit format/layout/slice/mip cases, bank boundaries, special floats, snapshot mutation, staging-page rollover, reversed submissions and invalid-request fallback passed; packed=%u uploads=%llu jobs=%llu dispatches=%llu packed_uploads=%llu mapped_snapshots=%llu mach_snapshots=%llu\n",
        cases,unsigned(packed),(unsigned long long)stats.uploads,(unsigned long long)stats.jobs,
        (unsigned long long)stats.compute_dispatches,(unsigned long long)stats.packed_uploads,
        (unsigned long long)stats.mapped_snapshots,(unsigned long long)stats.mach_snapshots);
    return true;
}

static bool GpuSourceBitsTests() {
    auto device=MTLCreateSystemDefaultDevice();auto queue=[device newCommandQueue];
    Gpu::Metal::GpuTextureUploader uploader((__bridge void*)device);
    if(!uploader.Ready())return false;
    unsigned cases=0;
    for(unsigned tile:{8u,31u,13u,14u})
    for(MTLPixelFormat format:{MTLPixelFormatR32Float,MTLPixelFormatRGBA8Unorm_sRGB,
            MTLPixelFormatRGBA16Float,MTLPixelFormatBC1_RGBA,MTLPixelFormatBC7_RGBAUnorm}) {
        const unsigned bpe=Gpu::Metal::GpuTextureUploader::BytesPerElement(format);
        const unsigned block=Gpu::Metal::GpuTextureUploader::BlockExtent(format);
        // The last case exceeds the 4MiB source page so both still-queued
        // versions survive private snapshot arena rollover.
        const bool rollover=tile==14 && format==MTLPixelFormatBC7_RGBAUnorm;
        const unsigned width=rollover ? 4101 : 37,height=rollover ? 1029 : 19;
        constexpr unsigned levels=3,slices=2;
        // MipLayout takes a texel pitch and converts BC texels to blocks.
        // Level.pitch below is its resulting element/block pitch.
        const unsigned pitch=width+13*block;
        Tiling::MipLevel mips[16];
        const auto bytes=Tiling::MipLayout(tile,pitch,height,slices,levels,bpe,block==4,false,mips);
        std::array<std::vector<uint32_t>,2> patterns;
        for(unsigned version=0;version<2;++version) {
            patterns[version].resize(bytes/4);uint32_t random=0x918372aau^(version*0x6b1732a9u);
            for(auto& word:patterns[version]) {random^=random<<13;random^=random>>17;random^=random<<5;word=random;}
            const uint32_t special[]{0x80000000u,1u,0x7fc12345u,0xffa54321u,0x7e017c01u,0xfc007bffu};
            std::copy(std::begin(special),std::end(special),patterns[version].begin());
        }
        auto source=[device newBufferWithLength:bytes+512 options:MTLResourceStorageModePrivate];
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
        descriptor.textureType=MTLTextureType2DArray;descriptor.arrayLength=slices;
        descriptor.mipmapLevelCount=levels;descriptor.storageMode=MTLStorageModePrivate;
        std::array<id<MTLTexture>,2> textures;
        auto command=[queue commandBuffer];
        Gpu::Metal::GpuTextureUploader::Upload upload{reinterpret_cast<const void*>(1),bytes,{}};
        for(unsigned level=0;level<levels;++level)for(unsigned slice=0;slice<slices;++slice)
            upload.levels.push_back({mips[level].offset+mips[level].slice_size*slice,
                std::max(width>>level,1u),std::max(height>>level,1u),mips[level].pitch_el,
                mips[level].tile_index,slice,level});
        // Deliberately reverse the jobs: source/scratch offsets must follow
        // each job rather than the submission index or natural mip order.
        std::reverse(upload.levels.begin(),upload.levels.end());
        for(unsigned version=0;version<2;++version) {
            textures[version]=[device newTextureWithDescriptor:descriptor];
            auto producer=[device newBufferWithBytes:patterns[version].data() length:bytes options:MTLResourceStorageModeShared];
            auto blit=[command blitCommandEncoder];
            [blit copyFromBuffer:producer sourceOffset:0 toBuffer:source destinationOffset:256 size:bytes];
            [blit endEncoding];
            using Result=Gpu::Metal::GpuTextureUploader::Result;
            assert(uploader.EncodeGpuSource((__bridge void*)command,(__bridge void*)textures[version],upload,
                (__bridge void*)source,source.length-4)==Result::Fallback);
            auto invalid=upload;invalid.levels[0].src_offset=bytes;
            assert(uploader.EncodeGpuSource((__bridge void*)command,(__bridge void*)textures[version],invalid,
                (__bridge void*)source,256)==Result::Fallback);
            const auto result=uploader.EncodeGpuSource((__bridge void*)command,(__bridge void*)textures[version],upload,
                (__bridge void*)source,256);
            if(result!=Result::Encoded) {
                fprintf(stderr,"GPU source encode rejected result=%u tile=%u format=%lu block=%u version=%u texels=%ux%u pitch_texels=%u bytes=%llu source_offset=256 source_length=%lu: %s\n",
                    unsigned(result),tile,(unsigned long)format,block,version,width,height,pitch,
                    (unsigned long long)bytes,(unsigned long)source.length,uploader.Error().c_str());
                for(const auto& level:upload.levels)
                    fprintf(stderr,"  slice=%u level=%u tile=%u visible=%ux%u pitch_el=%u offset=%llu\n",
                        level.slice,level.level,level.tile,level.width,level.height,level.pitch,
                        (unsigned long long)level.src_offset);
                return false;
            }
            // No valid CPU pointer was supplied. A second queued GPU writer
            // must not alter the already encoded immutable snapshot version.
            blit=[command blitCommandEncoder];
            [blit fillBuffer:source range:NSMakeRange(256,bytes) value:0x5a];[blit endEncoding];
        }
        [command commit];[command waitUntilCompleted];
        if(command.error){fprintf(stderr,"%s\n",command.error.localizedDescription.UTF8String);return false;}
        for(unsigned version=0;version<2;++version)for(const auto& level:upload.levels) {
            const unsigned w=(level.width+block-1)/block,h=(level.height+block-1)/block;
            const auto row=(w*bpe+255)&~255u;
            std::vector<unsigned char> reference(w*h*bpe);
            Tiling::DetileSliceReference(level.tile,reinterpret_cast<const unsigned char*>(patterns[version].data())+level.src_offset,
                reference.data(),w,h,level.pitch,bpe);
            auto readback=[device newBufferWithLength:row*h options:MTLResourceStorageModeShared];
            auto read=[queue commandBuffer];auto blit=[read blitCommandEncoder];
            [blit copyFromTexture:textures[version] sourceSlice:level.slice sourceLevel:level.level
                sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(level.width,level.height,1)
                toBuffer:readback destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row*h];
            [blit endEncoding];[read commit];[read waitUntilCompleted];assert(!read.error);
            for(unsigned y=0;y<h;++y)if(std::memcmp(static_cast<const unsigned char*>(readback.contents)+row*y,
                    reference.data()+w*bpe*y,w*bpe)) {
                fprintf(stderr,"GPU source snapshot mismatch tile=%u format=%lu version=%u level=%u slice=%u row=%u\n",
                    tile,(unsigned long)format,version,level.level,level.slice,y);return false;
            }
            ++cases;
        }
    }
    const auto stats=uploader.GetStats();
    assert(stats.uploads==40 && stats.jobs==240 && !stats.mach_snapshots && !stats.mapped_snapshots);
    puts("GPU source snapshots: 240 bit-exact raw mip/slice versions, GPU producers/overwrites, invalid CPU pointer, linear/thin/macro, NaNs, nonzero source/arena offsets, private arena rollover and range fallback passed");
    return cases==240;
}

static int GpuTests() {
    if(!MTLCreateSystemDefaultDevice()) return 77;
    constexpr unsigned width=129,height=65,out_width=136,out_height=72;
    constexpr size_t allocation=1<<20,texture_offset=65536,target_offset=524288;
    uint64_t physical=0;
    assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),allocation,16384,0,&physical)==0);
    uintptr_t address=0;
    assert(runtime_gpu_memory_map(&address,allocation,3,0,physical,16384)==0);
    auto* bytes=reinterpret_cast<unsigned char*>(address);
    const uint32_t vertex_descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(32u<<16),3,
        0xfacu|(7u<<12)|(14u<<15)};
    memcpy(bytes,vertex_descriptor,sizeof(vertex_descriptor));
    const float vertices[]{-1,-1,0,1, 0,1,0,1, 3,-1,0,1, 2,1,0,1, -1,3,0,1, 0,-1,0,1};
    memcpy(bytes+256,vertices,sizeof(vertices));
    auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(2048));assert(shader);
    const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xe00c2010,0x80010500,
        0xf80008cf,0x04030201,0xf8000a0f,0x08070605,0xbf810000};
    // Linear attr0.xy, nearest sample t0/s8, export RGBA.
    const uint32_t ps[]{0xc8000000u|(6u<<18),0xc8010001u|(6u<<18),
        0xc8000100u|(7u<<18),0xc8010101u|(7u<<18),
        0xf0000000u|(32u<<18)|(15u<<8),(2u<<21)|(8u<<8)|6u,
        0xf800080f,0x0b0a0908,0xbf810000};
    memcpy(shader,vs,sizeof(vs));memcpy(shader+64,ps,sizeof(ps));
    RegFile regs;
    regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
    regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
    regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=12u<<1;
    regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address);
    regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(address>>32);
    regs[Reg::SPI_PS_INPUT_ADDR]=1u<<5;
    regs[Reg::VGT_PRIMITIVE_TYPE]=4;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+target_offset)>>8);
    regs[Reg::CB_COLOR0_BASE+Reg::CB_PITCH]=out_width/8-1;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_SLICE]=out_width*out_height/64-1;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    regs[Reg::CB_COLOR_CONTROL]=1u<<4;
    regs[Reg::CB_TARGET_MASK]=regs[Reg::CB_SHADER_MASK]=15;
    regs[Reg::SPI_SHADER_COL_FORMAT]=9;
    regs[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{out_width/2.f,out_width/2.f,-float(out_height)/2,out_height/2.f,1,0};
    for(unsigned k=0;k<6;++k)regs[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<uint32_t>(viewport[k]);
    for(auto r:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,Reg::PA_SC_GENERIC_SCISSOR_BR})
        regs[r]=out_width|(out_height<<16);
    Gpu::Metal::Device device;
    Gpu::Metal::MetalRenderer renderer(device);
    auto metal=(__bridge id<MTLDevice>)device.Handle();
    auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    constexpr unsigned read_row=(out_width*4+255)&~255u;
    auto readback=[metal newBufferWithLength:read_row*out_height options:MTLResourceStorageModeShared];
    const auto draw=[&] {
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        renderer.Flip(Gpu::FlipInfo{.address=address+target_offset,.width=out_width,.height=out_height,.pitch=out_width});
        return (__bridge id<MTLTexture>)renderer.LatestFrame();
    };
    for(unsigned tile:{0u,10u,14u})for(bool floating:{false,true}) {
        const size_t size=Tiling::SliceSize(tile,width,height,4);
        std::vector<uint32_t> indices(size/4),offsets(width*height);
        for(unsigned k=0;k<indices.size();++k)indices[k]=k;
        // Build input using the unchanged original address calculation.
        Tiling::DetileSliceReference(tile,reinterpret_cast<unsigned char*>(indices.data()),
            reinterpret_cast<unsigned char*>(offsets.data()),width,height,width,4);
        const auto fill=[&](unsigned version) {
            memset(bytes+texture_offset,0x73,size);
            for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x) {
                const auto rgba=Pattern(x,y,version,floating);
                auto* destination=bytes+texture_offset+size_t(offsets[y*width+x])*4;
                if(floating) {const float value=rgba[0]/255.f;memcpy(destination,&value,4);}
                else memcpy(destination,rgba.data(),4);
            }
        };
        const uint64_t base=address+texture_offset;
        const uint32_t descriptor[]{uint32_t(base>>8),uint32_t(base>>40)|
            ((floating ? 4u : 10u)<<20)|(floating ? 7u<<26 : 0),
            (width-1)|((height-1)<<14),(9u<<28)|(tile<<20)|0xfacu,(width-1)<<13,0,0,0};
        for(unsigned k=0;k<8;++k)regs[Reg::SPI_SHADER_USER_DATA_PS_0+k]=descriptor[k];
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+8]=2u|(2u<<3); // clamp edge, nearest
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+9]=0xfffu<<12;
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+10]=regs[Reg::SPI_SHADER_USER_DATA_PS_0+11]=0;
        fill(0);draw();renderer.WaitIdle();
        auto gate=[metal newSharedEvent];assert(gate);
        auto blocked=[queue commandBuffer];[blocked encodeWaitForEvent:gate value:1];[blocked commit];
        std::atomic<bool> timed_out{false};
        std::jthread watchdog([gate,&timed_out](std::stop_token stop) {
            for(unsigned k=0;k<300 && !stop.stop_requested();++k)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(!stop.stop_requested()){timed_out=true;gate.signaledValue=1;}
        });
        fill(1);id<MTLTexture> first=draw();
        fill(2);id<MTLTexture> second=draw();
        gate.signaledValue=1;watchdog.request_stop();watchdog.join();renderer.WaitIdle();
        assert(!timed_out);
        const auto check=[&](id<MTLTexture> frame,unsigned version) {
            auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(out_width,out_height,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:read_row destinationBytesPerImage:read_row*out_height];
            [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
            const auto* pixels=static_cast<const unsigned char*>(readback.contents);
            for(unsigned y=0;y<out_height;++y)for(unsigned x=0;x<out_width;++x) {
                const auto expected=Pattern(unsigned((x+.5)*width/out_width),
                    unsigned((y+.5)*height/out_height),version,floating);
                for(unsigned c=0;c<4;++c) {
                    const unsigned value=expected[c==0 ? 2 : c==2 ? 0 : c];
                    if(std::abs(int(pixels[y*read_row+x*4+c])-int(value))>1) {
                        fprintf(stderr,"GPU detile tile=%u float=%u version=%u (%u,%u) c=%u expected=%u got=%u\n",
                            tile,floating,version,x,y,c,value,pixels[y*read_row+x*4+c]);return false;
                    }
                }
            }
            return true;
        };
        if(!check(first,1) || !check(second,2))return 1;
    }
    puts("Detile: production tile0/Display/Thin R32Float and RGBA8 nonuniform uploads, partial edges and queued versions passed");
    return 0;
}

int main(int argc,char** argv) {
    if(!std::getenv("BB_METAL_FAST_DETILE"))setenv("BB_METAL_FAST_DETILE","1",1);
    if(argc>1 && !std::strcmp(argv[1],"--gpu-upload-only")) {
        @autoreleasepool {
            if(!MTLCreateSystemDefaultDevice())return 77;
            return GpuUploadBitsTests() && GpuSourceBitsTests() ? 0 : 1;
        }
    }
    CpuTests();
    if(argc>1 && !std::strcmp(argv[1],"--cpu-only"))return 0;
    @autoreleasepool {
        if(std::getenv("BB_METAL_GPU_TEXTURE_UPLOAD") && !std::strcmp(std::getenv("BB_METAL_GPU_TEXTURE_UPLOAD"),"1") && (!GpuUploadBitsTests() || !GpuSourceBitsTests()))return 1;
        return GpuTests();
    }
}
