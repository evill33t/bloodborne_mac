// SPDX-License-Identifier: GPL-2.0-or-later
// Queue two frames while the GPU is held at an event. Updating a CPU texture
// must preserve the pixels already referenced by the earlier queued frame.
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "gpu/liverpool/tiling.h"
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
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical)==0);
        uintptr_t address=0;
        assert(runtime_gpu_memory_map(&address,131072,3,0,physical,16384)==0);
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
        assert(Tiling::StorageLayers(11,1,true)==8);
        assert(Tiling::StorageLayers(11,1,false)==6);
        assert(Tiling::StorageLayers(11,2,true)==16);
        assert(Tiling::StorageLayers(11,3,true)==32);
        assert(Tiling::StorageLayers(13,5,true)==8);
        assert(Tiling::StorageLayers(13,5,false)==5);
        assert(Tiling::StorageLayers(10,32,true)==1);
        Tiling::MipLevel bc6[8];
        assert(Tiling::MipLayout(13,128,128,Tiling::StorageLayers(11,1,true),8,16,true,true,bc6)==0x34000);
        const uint64_t offsets[]{0,0x20000,0x28000,0x2a000,0x2c000,0x2e000,0x30000,0x32000};
        for(unsigned k=0;k<8;++k) assert(bc6[k].offset==offsets[k]);
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

        // Sample every visible face at every mip from physical array storage.
        // The padding faces have distinct colours, so using the view's face
        // count as the mip stride cannot accidentally pass this regression.
        const uint32_t mip_ps[]{0x7e00020c,0x7e02020d,0x7e04020e,0x7e06020f,
            0xf0000000u|(36u<<18)|(15u<<8),(2u<<21)|(4u<<8),
            0xf800080f,0x07060504,0xbf810000};
        memcpy(shader+64,mip_ps,sizeof(mip_ps));
        regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=16u<<1;
        struct ViewCase {bool cube,pad;unsigned physical,visible,first,depth,base_level;bool reverse=false;};
        const ViewCase cases[]{
            {true,true,8,6,0,1,0}, {true,false,6,6,0,1,0},
            {false,false,8,3,0,8,0}, {false,false,8,3,3,8,0},
            {true,true,16,6,6,2,0}, {false,false,8,3,3,8,1},
            {false,false,8,3,3,8,1,true}};
        for(unsigned mode=0;mode<sizeof(cases)/sizeof(cases[0]);++mode) {
            const auto c=cases[mode];
            const bool cube=c.cube,pad=c.pad;
            const unsigned physical_layers=c.physical,visible_layers=c.visible;
            size_t offset=0;
            for(unsigned level=0;level<5;++level) {
                const size_t slice_bytes=level ? 256 : 1024;
                for(unsigned face=0;face<physical_layers;++face) {
                    const unsigned char rgba[]{static_cast<unsigned char>(20+level*25+face*3),
                        static_cast<unsigned char>(40+face*12),static_cast<unsigned char>(30+level*30),255};
                    for(size_t at=0;at<slice_bytes;at+=4)
                        memcpy(bytes+49152+offset+face*slice_bytes+at,rgba,4);
                }
                offset+=slice_bytes*physical_layers;
            }
            const uint64_t base=address+49152;
            const uint32_t mip_texture[]{uint32_t(base>>8),uint32_t(base>>40)|(10u<<20),15u|(15u<<14),
                ((cube?11u:13u)<<28)|(13u<<20)|(pad?1u<<25:0)|(4u<<16)|(c.base_level<<12)|(c.reverse?0x32eu:0xfacu),
                (15u<<13)|(c.depth-1),c.first|((c.first+visible_layers-1)<<13),0,0};
            for(unsigned k=0;k<8;++k)regs[Reg::SPI_SHADER_USER_DATA_PS_0+k]=mip_texture[k];
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+8]=0;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+9]=0xfffu<<12;
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+10]=1u<<26; // nearest mip
            regs[Reg::SPI_SHADER_USER_DATA_PS_0+11]=0;
            for(unsigned level=0;level<5-c.base_level;++level)for(unsigned face=0;face<visible_layers;++face) {
                regs[Reg::SPI_SHADER_USER_DATA_PS_0+12]=std::bit_cast<uint32_t>(cube?1.5f:.5f);
                regs[Reg::SPI_SHADER_USER_DATA_PS_0+13]=std::bit_cast<uint32_t>(cube?1.5f:.5f);
                regs[Reg::SPI_SHADER_USER_DATA_PS_0+14]=std::bit_cast<uint32_t>(float(face));
                regs[Reg::SPI_SHADER_USER_DATA_PS_0+15]=std::bit_cast<uint32_t>(float(level));
                id<MTLTexture> frame=draw();renderer.WaitIdle();
                auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
                [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                    destinationBytesPerRow:32 destinationBytesPerImage:256];
                [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
                const auto* pixels=static_cast<const unsigned char*>(readback.contents);
                const unsigned physical_level=level+c.base_level,physical_face=face+c.first;
                const unsigned red=20+physical_level*25+physical_face*3,blue=30+physical_level*30;
                const unsigned expected[]{c.reverse?red:blue,40+physical_face*12,c.reverse?blue:red,255};
                for(unsigned pixel=0;pixel<64;++pixel)for(unsigned channel=0;channel<4;++channel)
                    if(pixels[pixel*4+channel]!=expected[channel]) {
                        fprintf(stderr,"Mip storage mode=%u level=%u face=%u pixel=%u channel=%u: expected %u got %u\n",
                            mode,level,face,pixel,channel,expected[channel],pixels[pixel*4+channel]);return 1;
                    }
            }
        }
        puts("Metal mip storage: padded/unpadded cubes, cube arrays, nonzero base slices/mips preserve all face/mip colours");
    }
}
