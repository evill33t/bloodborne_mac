// SPDX-License-Identifier: GPL-2.0-or-later
// Queue two frames while the GPU is held at an event. Updating a CPU texture
// must preserve the pixels already referenced by the earlier queued frame.
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "src/runtime_gpu_memory.h"
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

extern "C" void runtime_restart() { std::abort(); }
int main() {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        uint64_t physical=0;
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),65536,16384,0,&physical)==0);
        uintptr_t address=0;
        assert(runtime_gpu_memory_map(&address,65536,3,0,physical,16384)==0);
        auto* bytes=reinterpret_cast<unsigned char*>(address);
        const uint32_t descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(16u<<16),3,
            0xfacu|(7u<<12)|(14u<<15)};
        memcpy(bytes,descriptor,sizeof(descriptor));
        const float vertices[]{-1,-1,0,1, 3,-1,0,1, -1,3,0,1};
        memcpy(bytes+256,vertices,sizeof(vertices));
        auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(1024));assert(shader);
        const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
        const uint32_t ps[]{0x7e0002f0,0x7e0202f0,0xf0000000u|(32u<<18)|(15u<<8),
            (2u<<21)|(2u<<8),0xf800080f,0x05040302,0xbf810000};
        memcpy(shader,vs,sizeof(vs));memcpy(shader+64,ps,sizeof(ps));
        using namespace Gpu::Liverpool;
        RegFile regs;
        regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
        regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
        regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=12u<<1;
        regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address);
        regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(address>>32);
        const uint32_t texture[]{uint32_t((address+16384)>>8),
            uint32_t((address+16384)>>40)|(10u<<20),7u|(7u<<14),0x90800fac,15u<<13,0,0,0};
        for (unsigned k=0;k<12;++k) regs[Reg::SPI_SHADER_USER_DATA_PS_0+k]=k<8 ? texture[k] : 0;
        regs[Reg::VGT_PRIMITIVE_TYPE]=4;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+32768)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
        regs[Reg::CB_COLOR_CONTROL]=1u<<4;
        regs[Reg::CB_TARGET_MASK]=regs[Reg::CB_SHADER_MASK]=15;
        regs[Reg::SPI_SHADER_COL_FORMAT]=9;
        regs[Reg::PA_CL_VTE_CNTL]=0x43f;
        const float viewport[]{4,4,-4,4,1,0};
        for (unsigned k=0;k<6;++k) regs[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<uint32_t>(viewport[k]);
        for (auto reg:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,
                      Reg::PA_SC_GENERIC_SCISSOR_BR}) regs[reg]=8u|(8u<<16);
        const auto fill=[&](unsigned channel) {
            // Linear-aligned RGBA rows have a 64-byte pitch, including padding.
            for (unsigned k=0;k<128;++k) for (unsigned c=0;c<4;++c)
                bytes[16384+k*4+c]=(c==channel || c==3) ? 255 : 0;
        };
        Gpu::Metal::Device device;
        Gpu::Metal::MetalRenderer renderer(device);
        const auto metal=(__bridge id<MTLDevice>)device.Handle();
        const auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
        const auto draw=[&] {
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
            renderer.Flip(Gpu::FlipInfo{.address=address+32768,.width=8,.height=8,.pitch=8});
            return (__bridge id<MTLTexture>)renderer.LatestFrame();
        };
        fill(0);draw();renderer.WaitIdle(); // warm the shaders and texture cache
        auto gate=[metal newSharedEvent];assert(gate);
        auto blocked=[queue commandBuffer];[blocked encodeWaitForEvent:gate value:1];[blocked commit];
        std::atomic<bool> timed_out{false};
        std::jthread watchdog([gate,&timed_out](std::stop_token stop) {
            for (unsigned k=0;k<200 && !stop.stop_requested();++k)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested()) {timed_out=true;gate.signaledValue=1;}
        });
        fill(1);id<MTLTexture> first=draw();
        fill(2);id<MTLTexture> second=draw();
        gate.signaledValue=1;watchdog.request_stop();watchdog.join();renderer.WaitIdle();
        if (timed_out) {fputs("CPU texture update waited for the in-flight GPU read\n",stderr);return 1;}
        auto readback=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
        const auto check=[&](id<MTLTexture> frame,unsigned channel) {
            auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
            const auto* pixels=static_cast<const unsigned char*>(readback.contents);
            const unsigned bgra_channel=2-channel;
            for (unsigned k=0;k<64;++k) for (unsigned c=0;c<4;++c) {
                const unsigned expected=(c==bgra_channel || c==3) ? 255 : 0;
                if (pixels[k*4+c]!=expected) {
                    fprintf(stderr,"Queued frame channel=%u pixel=%u component=%u: expected %u got %u\n",
                        channel,k,c,expected,pixels[k*4+c]);return false;
                }
            }
            return true;
        };
        if (!check(first,1) || !check(second,2)) return 1;
        puts("Metal uploaded textures: queued frames preserve their own pixels without CPU/GPU waits");
    }
}
