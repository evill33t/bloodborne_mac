// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production encoder cache through translated GCN. Consecutive
// scissored draws share an encoder but change textures, samplers and user data.
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "gpu/shader/msl.h"
#include "src/runtime_gpu_memory.h"
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

extern "C" void runtime_restart() { std::abort(); }
namespace Shader=Gpu::Shader;
struct CacheFixture {
#include "gpu/metal/renderer_binding_cache.inc"
};

static void CpuContracts() {
    CacheFixture fixture;
    auto& cache=fixture.binding_cache;
    cache.enabled=true;
    int a=0,b=0;
    cache.Reset(true);
    for(bool fragment:{false,true}) {
        assert(cache.Buffer(fragment,Shader::Binding::Mem0,&a));
        assert(!cache.Buffer(fragment,Shader::Binding::Mem0,&a));
        assert(cache.Buffer(fragment,Shader::Binding::MemMap,&a));
        assert(!cache.Buffer(fragment,Shader::Binding::MemMap,&a));
        // A new map must rebind each stage independently.
        assert(cache.Buffer(fragment,Shader::Binding::MemMap,&b));
        assert(cache.Texture(fragment,0,&a));
        assert(!cache.Texture(fragment,0,&a));
        assert(cache.Texture(fragment,0,&b));
        assert(cache.Sampler(fragment,0,&a));
        assert(!cache.Sampler(fragment,0,&a));
        assert(cache.Sampler(fragment,0,&b));
    }
    // Out-of-band depth bounds can overwrite slot30. No identity there is
    // cached, including an ordinary shader binding the same slot afterward.
    assert(cache.Texture(true,Shader::kDepthBoundsTexture,&a));
    assert(cache.Texture(true,Shader::kDepthBoundsTexture,&a));
    assert(cache.Buffer(false,Shader::Binding::UserData,&a));
    assert(cache.Buffer(false,Shader::Binding::UserData,&a));
    assert(cache.Texture(true,128,&a) && cache.Texture(true,128,&a));
    assert(cache.Sampler(true,16,&a) && cache.Sampler(true,16,&a));
    cache.Reset();
    assert(cache.Texture(true,0,&b) && cache.Texture(true,0,&b));
    cache.Reset(true);
    assert(cache.Texture(true,0,&b));
    assert(!cache.Texture(true,0,&b));
    cache.epoch=std::numeric_limits<u64>::max();
    cache.Reset(true);
    assert(cache.Texture(true,0,&b));
    assert(!cache.Texture(true,0,&b));
    assert(cache.buffer_counts.intended==cache.buffer_counts.emitted+cache.buffer_counts.skipped);
    assert(cache.texture_counts.intended==cache.texture_counts.emitted+cache.texture_counts.skipped);
    assert(cache.sampler_counts.intended==cache.sampler_counts.emitted+cache.sampler_counts.skipped);
    cache.enabled=false;
    assert(cache.Texture(true,0,&b) && cache.Texture(true,0,&b));
    puts("Binding cache epochs, per-stage map identity, unretained bypass and reserved-slot contracts passed");
}

static int ProductionPixels() {
    if(!MTLCreateSystemDefaultDevice()) return 77;
    using namespace Gpu::Liverpool;
    constexpr unsigned width=16,height=8;
    constexpr size_t allocation=131072,texture_offset=16384,target_offset=32768;
    uint64_t physical=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),allocation,16384,0,&physical));
    uintptr_t address=0;
    assert(!runtime_gpu_memory_map(&address,allocation,3,0,physical,16384));
    auto* bytes=reinterpret_cast<unsigned char*>(address);
    const auto initialize_vertices=[](uintptr_t base) {
        const uint32_t descriptor[]{uint32_t(base+256),uint32_t((base+256)>>32)|(16u<<16),3,
            0xfacu|(7u<<12)|(14u<<15)};
        const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
        memcpy(reinterpret_cast<void*>(base),descriptor,sizeof(descriptor));
        memcpy(reinterpret_cast<void*>(base+256),vertices,sizeof(vertices));
    };
    initialize_vertices(address);
    for(unsigned texture=0;texture<2;++texture)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        const unsigned char rgba[]{static_cast<unsigned char>(texture ? (x<4 ? 0 : 255) : (x<4 ? 255 : 0)),
            static_cast<unsigned char>(texture ? 255 : 0),static_cast<unsigned char>(!texture && x>=4 ? 255 : 0),255};
        memcpy(bytes+texture_offset+texture*4096+y*64+x*4,rgba,4);
    }
    auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(1024));assert(shader);
    const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    // s_load_dwordx2 s[14:15],s[12:13],0; move coordinates to v[0:1];
    // sample t0/s8 and export RGBA. Both stages read the guest memory map.
    const uint32_t ps[]{0xc0000000u|(1u<<22)|(14u<<15)|(6u<<9)|0x100u,
        0x7e00020e,0x7e02020f,0xf0000000u|(32u<<18)|(15u<<8),
        (2u<<21)|(2u<<8),0xf800080f,0x05040302,0xbf810000};
    memcpy(shader,vs,sizeof(vs));memcpy(shader+64,ps,sizeof(ps));
    RegFile regs;
    regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
    regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
    regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=14u<<1;
    regs[Reg::VGT_PRIMITIVE_TYPE]=4;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_PITCH]=width/8-1;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_SLICE]=width*height/64-1;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    regs[Reg::CB_COLOR_CONTROL]=1u<<4;
    regs[Reg::CB_TARGET_MASK]=regs[Reg::CB_SHADER_MASK]=15;
    regs[Reg::SPI_SHADER_COL_FORMAT]=9;
    regs[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{width/2.f,width/2.f,-float(height)/2,height/2.f,1,0};
    for(unsigned k=0;k<6;++k)regs[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<uint32_t>(viewport[k]);
    for(auto r:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,Reg::PA_SC_GENERIC_SCISSOR_BR})
        regs[r]=width|(height<<16);
    struct Case {unsigned texture;bool linear;float u;std::array<unsigned,4> rgba;};
    const Case cases[]{
        {0,false,.25f,{255,0,0,255}},{0,false,.25f,{255,0,0,255}},
        {0,true,.5f,{128,0,128,255}},{1,true,.5f,{128,255,0,255}},
        {1,false,.5f,{255,255,0,255}},{1,false,.25f,{0,255,0,255}},
        {0,false,.75f,{0,0,255,255}},{0,false,.25f,{255,0,0,255}}};
    for(unsigned k=0;k<8;++k) {
        const float coordinates[]{cases[k].u,.5f};
        memcpy(bytes+1024+k*16,coordinates,sizeof(coordinates));
    }
    Gpu::Metal::Device device;
    Gpu::Metal::MetalRenderer renderer(device);
    auto metal=(__bridge id<MTLDevice>)device.Handle();
    auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    auto readback=[metal newBufferWithLength:width*height*4 options:MTLResourceStorageModeShared];
    uintptr_t added_address=0;
    for(unsigned pass=0;pass<2;++pass) {
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+target_offset+pass*4096)>>8);
        for(unsigned k=0;k<8;++k) {
            if(!pass && k==6) {
                uint64_t extra_physical=0;
                assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),16384,16384,0,&extra_physical));
                const auto before=runtime_gpu_memory_generation();
                assert(!runtime_gpu_memory_map(&added_address,16384,3,0,extra_physical,16384));
                assert(runtime_gpu_memory_generation()!=before);
                initialize_vertices(added_address);
                // These immutable values become visible through a new table
                // while an existing render encoder still has prior draws.
                memcpy(reinterpret_cast<void*>(added_address+1024),bytes+1024,8*16);
            }
            const auto& c=cases[k];
            const uintptr_t vertex_base=k>=6 ? added_address : address;
            const uintptr_t coordinate_base=vertex_base+1024+k*16;
            regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(vertex_base);
            regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(vertex_base>>32);
            const uint64_t base=address+texture_offset+c.texture*4096;
            const uint32_t descriptor[]{uint32_t(base>>8),uint32_t(base>>40)|(10u<<20),
                7u|(7u<<14),0x90800fac,15u<<13,0,0,0};
            for(unsigned n=0;n<8;++n)regs[Reg::SPI_SHADER_USER_DATA_PS_0+n]=descriptor[n];
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+8]=0;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+9]=0xfffu<<12;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+10]=c.linear ? (1u<<20)|(1u<<22) : 0;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+11]=0;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+12]=uint32_t(coordinate_base);
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+13]=uint32_t(coordinate_base>>32);
            regs[Reg::PA_SC_GENERIC_SCISSOR_TL]=k*2;
            regs[Reg::PA_SC_GENERIC_SCISSOR_BR]=(k*2+2)|(height<<16);
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
        }
        renderer.Flip(Gpu::FlipInfo{.address=address+target_offset+pass*4096,.width=width,.height=height,.pitch=width});
        renderer.WaitIdle();
        auto frame=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(frame);
        auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:width*4 destinationBytesPerImage:width*height*4];
        [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
        const auto* pixels=static_cast<const unsigned char*>(readback.contents);
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)for(unsigned component=0;component<4;++component) {
            const auto& expected=cases[x/2].rgba;
            const unsigned expected_byte=expected[component<3 ? 2-component : 3];
            const unsigned actual=pixels[(y*width+x)*4+component];
            if(std::abs(int(actual)-int(expected_byte))>1) {
                fprintf(stderr,"Binding cache pass=%u (%u,%u)c%u expected%u got%u mode=%s\n",
                    pass,x,y,component,expected_byte,actual,std::getenv("BB_METAL_BINDING_CACHE"));
                return 1;
            }
        }
    }
    puts("Production binding cache: same-pass texture/sampler/UD changes, both-stage map refresh and new-encoder reset preserve every pixel");
    return 0;
}

int main(int argc,char** argv) {
    if(!std::getenv("BB_METAL_BINDING_CACHE"))setenv("BB_METAL_BINDING_CACHE","1",1);
    CpuContracts();
    if(argc>1 && !std::strcmp(argv[1],"--cpu-only"))return 0;
    @autoreleasepool {return ProductionPixels();}
}
