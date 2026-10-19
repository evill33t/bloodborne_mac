// SPDX-License-Identifier: GPL-2.0-or-later
// Compare the production detiler with its original address calculation, then
// sample nonuniform guest textures through translated GCN and the real cache.
#import <Metal/Metal.h>
#include "gpu/liverpool/tiling.h"
#include "gpu/liverpool/registers.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
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
    for(unsigned tile:{0u,1u,2u,3u,4u,10u,14u}) for(const auto size:sizes)
        for(unsigned alignment:{0u,1u,3u}) {
            CpuCase(tile,size[0],size[1],size[0]+(alignment ? 17 : 0),4,alignment);++cases;
        }
    // Non-four-byte, linear, micro-tiled and thick callers retain the oracle.
    for(unsigned tile:{0u,4u,10u,14u,5u,9u,13u,8u,31u,19u})
        for(unsigned bytes:{1u,2u,4u,8u,16u})
            for(unsigned z=0;z<Tiling::Thickness(tile);++z) {
                CpuCase(tile,37,19,53,bytes,1,z);++cases;
            }
    printf("Detile: %u byte-exact CPU cases, odd/large edges, padded pitches, unaligned pointers and canaries (%.3fs; mode=%s)\n",
        cases,std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),
        std::getenv("BB_METAL_FAST_DETILE"));
}

static std::array<unsigned char,4> Pattern(unsigned x,unsigned y,unsigned version,bool floating) {
    const unsigned char red=(x*17+y*29+version*61)&255;
    return {red,static_cast<unsigned char>(floating ? 0 : (x*43+y*7+version*37)&255),
        static_cast<unsigned char>(floating ? 0 : (x*11+y*53+version*83)&255),255};
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
    CpuTests();
    if(argc>1 && !std::strcmp(argv[1],"--cpu-only"))return 0;
    @autoreleasepool {return GpuTests();}
}
