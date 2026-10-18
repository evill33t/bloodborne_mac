// SPDX-License-Identifier: GPL-2.0-or-later
// Check successive lighting draws against GPU-written depth, without game data.
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "src/runtime_gpu_memory.h"
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void runtime_restart() { std::abort(); }

int main() {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        uint64_t physical=0;
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical)==0);
        uintptr_t address=0;
        assert(runtime_gpu_memory_map(&address,131072,3,0,physical,16384)==0);
        auto* bytes=reinterpret_cast<unsigned char*>(address);
        // Each draw has immutable vertices: CPU edits to an in-flight vertex
        // buffer would test a different race instead of depth-copy coherence.
        for (unsigned k=0;k<2;++k) {
            const auto vertices_address=address+256+k*256;
            const uint32_t descriptor[]{uint32_t(vertices_address),uint32_t(vertices_address>>32)|(16u<<16),3,
                0xfacu|(7u<<12)|(14u<<15)};
            memcpy(bytes+k*64,descriptor,sizeof(descriptor));
            const float z=k ? .75f : .25f;
            const float vertices[]{-1,-1,z,1, 3,-1,z,1, -1,3,z,1};
            memcpy(bytes+256+k*256,vertices,sizeof(vertices));
        }
        auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(1024));assert(shader);
        const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
        const uint32_t ps[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,
            0xf800080f,0x03020100,0xbf810000};
        memcpy(shader,vs,sizeof(vs));memcpy(shader+64,ps,sizeof(ps));
        using namespace Gpu::Liverpool;
        RegFile regs;
        regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
        regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
        regs[Reg::VGT_PRIMITIVE_TYPE]=4;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
        regs[Reg::CB_SHADER_MASK]=15;
        regs[Reg::SPI_SHADER_COL_FORMAT]=9;
        regs[Reg::DB_Z_INFO]=3;
        regs[Reg::DB_Z_READ_BASE]=regs[Reg::DB_Z_WRITE_BASE]=uint32_t((address+16384)>>8);
        regs[Reg::DB_SHADER_CONTROL]=0x10;
        regs[Reg::PA_CL_VTE_CNTL]=0x43f;
        const float viewport[]{4,4,-4,4,1,0};
        for (unsigned k=0;k<6;++k) regs[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<uint32_t>(viewport[k]);
        for (auto reg : {Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,
                        Reg::PA_SC_GENERIC_SCISSOR_BR}) regs[reg]=8u|(8u<<16);
        const auto vertices=[&](unsigned k) {
            regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address+k*64);
            regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t((address+k*64)>>32);
        };
        const auto color=[&](unsigned k) {
            regs[Reg::CB_COLOR_CONTROL]=1u<<4;
            regs[Reg::CB_TARGET_MASK]=15;
            regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+32768+k*4096)>>8);
        };
        Gpu::Metal::Device device;
        Gpu::Metal::MetalRenderer renderer(device);
        // Seed depth=.25, then a bounded draw accepts that depth and writes .75.
        vertices(0);
        regs[Reg::DB_DEPTH_CONTROL]=0x76; // depth enabled, write, always
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        vertices(1);color(0);
        regs[Reg::DB_DEPTH_CONTROL]=0x7e; // also depth bounds
        regs[Reg::DB_DEPTH_BOUNDS_MIN]=std::bit_cast<uint32_t>(.2f);
        regs[Reg::DB_DEPTH_BOUNDS_MAX]=std::bit_cast<uint32_t>(.3f);
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        // This later light must see .75 and reject the old [.2,.3] range.
        color(1);regs[Reg::DB_DEPTH_CONTROL]=0x7a; // bounds, no writes
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        const auto metal=(__bridge id<MTLDevice>)device.Handle();
        const auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
        auto readback=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
        const auto check=[&](unsigned target,unsigned expected) {
            renderer.Flip(Gpu::FlipInfo{.address=address+32768+target*4096,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            auto frame=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(frame);
            auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
            const auto* pixels=static_cast<const unsigned char*>(readback.contents);
            for (unsigned k=0;k<64;++k) if (pixels[k*4+2]!=expected) {
                fprintf(stderr,"Depth bounds target %u pixel %u: expected red %u, got %u\n",
                    target,k,expected,pixels[k*4+2]);
                return false;
            }
            return true;
        };
        if (!check(0,255) || !check(1,0)) return 1;
        assert(renderer.DepthBoundsCopyCount()==2);
        // A read-only pass without bounds and attachment switches must keep
        // the valid copy usable. The newly accepted range is [.7,.8].
        regs[Reg::DB_DEPTH_BOUNDS_MIN]=std::bit_cast<uint32_t>(.7f);
        regs[Reg::DB_DEPTH_BOUNDS_MAX]=std::bit_cast<uint32_t>(.8f);
        for (unsigned k=2;k<6;++k) {
            color(k);
            regs[Reg::DB_DEPTH_CONTROL]=k==3 ? 0x72 : 0x7a;
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
        }
        if (!check(5,255)) return 1;
        assert(renderer.DepthBoundsCopyCount()==2);
        for (unsigned k=0;k<256;++k) {
            color(2+(k&1));
            regs[Reg::DB_DEPTH_CONTROL]=(k&1) ? 0x7a : 0x72;
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
        }
        if (!check(3,255)) return 1;
        assert(renderer.DepthBoundsCopyCount()==2);
        // Clears change depth even without a depth-writing draw afterward.
        regs[Reg::CB_COLOR_CONTROL]=0;regs[Reg::CB_TARGET_MASK]=0;
        regs[Reg::DB_RENDER_CONTROL]=1;
        regs[Reg::DB_DEPTH_CLEAR]=std::bit_cast<uint32_t>(.4f);
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        color(6);regs[Reg::DB_RENDER_CONTROL]=0;
        regs[Reg::DB_DEPTH_BOUNDS_MIN]=std::bit_cast<uint32_t>(.35f);
        regs[Reg::DB_DEPTH_BOUNDS_MAX]=std::bit_cast<uint32_t>(.45f);
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        if (!check(6,255)) return 1;
        assert(renderer.DepthBoundsCopyCount()==3);
        // Streaming reuses a shadow-map address for a smaller colour texture.
        // The descriptor must select its new bytes, not the old 8x8 depth=.4.
        for (unsigned k=0;k<64;++k) {
            const float value=.75f;memcpy(bytes+16384+k*4,&value,4);
        }
        const auto mov=[](unsigned dst,unsigned src) { return 0x7e000200u|(dst<<17)|src; };
        const uint32_t sample_ps[]{mov(0,240),mov(1,240),
            0xf0000000u|(32u<<18)|(15u<<8),(2u<<21)|(2u<<8),
            0xf800080f,0x05040302,0xbf810000};
        memcpy(shader+128,sample_ps,sizeof(sample_ps));
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+128)>>8);
        regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=12u<<1;
        regs[Reg::DB_DEPTH_CONTROL]=0;
        const uint32_t texture[]{uint32_t((address+16384)>>8),
            uint32_t((address+16384)>>40)|(4u<<20)|(7u<<26),
            3u|(3u<<14),0x90000fac,3u<<13,0,0,0};
        for (unsigned k=0;k<12;++k) regs[Reg::SPI_SHADER_USER_DATA_PS_0+k]=k<8 ? texture[k] : 0;
        color(7);renderer.Draw(regs,Gpu::DrawCall{.count=3});
        if (!check(7,191)) return 1;
        // The same allocation can alternate between colour and shadow depth
        // at identical extents too. Readers must select the latest writer.
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+16384)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(4u<<2)|(7u<<8); // R32Float
        regs[Reg::SPI_SHADER_COL_FORMAT]=1;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        const auto sample_alias=[&](unsigned target) {
            color(target);
            regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
            regs[Reg::SPI_SHADER_COL_FORMAT]=9;
            regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+128)>>8);
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+2]=7u|(7u<<14);
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+4]=7u<<13;
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
        };
        sample_alias(8);if (!check(8,255)) return 1;
        regs[Reg::CB_COLOR_CONTROL]=0;regs[Reg::CB_TARGET_MASK]=0;
        regs[Reg::DB_RENDER_CONTROL]=1;regs[Reg::DB_DEPTH_CONTROL]=2;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        regs[Reg::DB_RENDER_CONTROL]=0;regs[Reg::DB_DEPTH_CONTROL]=0;
        sample_alias(9);if (!check(9,102)) return 1;
        // An ordered GPU depth copy must also invalidate a destination's
        // existing bounds snapshot. Its source aliases an older colour image.
        const uint64_t destination=address+98304;
        regs[Reg::DB_Z_READ_BASE]=regs[Reg::DB_Z_WRITE_BASE]=uint32_t(destination>>8);
        regs[Reg::CB_COLOR_CONTROL]=0;regs[Reg::CB_TARGET_MASK]=0;
        regs[Reg::DB_DEPTH_CONTROL]=0x76;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
        regs[Reg::DB_DEPTH_CONTROL]=0x7a;
        regs[Reg::DB_DEPTH_BOUNDS_MIN]=std::bit_cast<uint32_t>(.7f);
        regs[Reg::DB_DEPTH_BOUNDS_MAX]=std::bit_cast<uint32_t>(.8f);
        color(10);renderer.Draw(regs,Gpu::DrawCall{.count=3});
        if (!check(10,255)) return 1;
        const uint32_t cs[]{0xe0002000,0x80010100,0xbf8c0f70,0xe0102000,0x80020100,0xbf810000};
        memcpy(shader+192,cs,sizeof(cs));
        RegFile compute;
        compute[Reg::COMPUTE_PGM_LO]=uint32_t(uintptr_t(shader+192)>>8);
        compute[Reg::COMPUTE_PGM_RSRC2]=12u<<1;
        compute[Reg::COMPUTE_NUM_THREAD_X]=64;
        compute[Reg::COMPUTE_NUM_THREAD_Y]=compute[Reg::COMPUTE_NUM_THREAD_Z]=1;
        const uint32_t src[]{uint32_t(address+16384),uint32_t((address+16384)>>32)|(4u<<16),64,0x24004};
        const uint32_t dst[]{uint32_t(destination),uint32_t(destination>>32)|(4u<<16),64,0x24004};
        for (unsigned k=0;k<4;++k) {
            compute[Reg::COMPUTE_USER_DATA_0+4+k]=src[k];
            compute[Reg::COMPUTE_USER_DATA_0+8+k]=dst[k];
        }
        renderer.Dispatch(compute,1,1,1);
        regs[Reg::DB_DEPTH_BOUNDS_MIN]=std::bit_cast<uint32_t>(.35f);
        regs[Reg::DB_DEPTH_BOUNDS_MAX]=std::bit_cast<uint32_t>(.45f);
        color(11);renderer.Draw(regs,Gpu::DrawCall{.count=3});
        if (!check(11,255)) return 1;
        assert(renderer.DepthBoundsCopyCount()==5);
        puts("Metal depth: successive writes, read-only copy reuse and colour/shadow address reuse passed");
    }
}
