// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic GCN exercises the production G-buffer/camera/postprocess hooks.
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
#include <vector>

extern "C" void runtime_restart() { std::abort(); }
int main() {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        constexpr uint64_t bytes=1<<20;
        uint64_t physical=0; assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),bytes,16384,0,&physical));
        uintptr_t address=0; assert(!runtime_gpu_memory_map(&address,bytes,3,0,physical,16384));
        auto* base=reinterpret_cast<unsigned char*>(address);
        uint32_t descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(16u<<16),3,0xfacu|(7u<<12)|(14u<<15)};
        std::memcpy(base,descriptor,sizeof(descriptor));
        const float vertices[]{-1,-1,.5,1,3,-1,.5,1,-1,3,.5,1};
        std::memcpy(base+256,vertices,sizeof(vertices));
        auto constants=reinterpret_cast<float*>(base+4096);
        std::memset(constants,0,864);
        constants[0]=3000; constants[1]=1.f/3000; constants[4]=constants[5]=64;
        for (unsigned i:{0u,5u,10u}) constants[8+i]=constants[180+i]=1;
        constants[52]=constants[57]=constants[62]=1; constants[63]=-.1;
        auto* shaders=static_cast<uint32_t*>(bbmetal_guest_allocate(4096)); assert(shaders);
        const uint32_t vs[]{0xc0000000u|(10u<<22)|(8u<<15)|(6u<<9)|0x100u,
            0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
        std::memcpy(shaders,vs,sizeof(vs));
        std::vector<uint32_t> ps{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2};
        for (unsigned target=0;target<5;++target) { ps.push_back(0xf800000fu|(target<<4)); ps.push_back(0x03020100); }
        ps[ps.size()-2]|=0x800; ps.push_back(0xbf810000);
        std::memcpy(shaders+64,ps.data(),ps.size()*4);
        // A no-op postprocess shader with validated retail-style metadata.
        uint32_t cs[16]{0xbeeb03ffu,3,0xbf810000};
        std::memcpy(reinterpret_cast<char*>(cs)+32,"OrbShdr",7);
        uint64_t id=0x9a9cf8a9; std::memcpy(reinterpret_cast<char*>(cs)+48,&id,8);
        std::memcpy(shaders+128,cs,sizeof(cs));
        using namespace Gpu::Liverpool;
        RegFile r;
        r[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shaders)>>8);
        r[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shaders+64)>>8);
        r[Reg::SPI_SHADER_PGM_RSRC2_VS]=16u<<1;
        r[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address);
        r[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(address>>32);
        const uint32_t cb[]{uint32_t(address+4096),uint32_t((address+4096)>>32)|(4u<<16),216,0x24004};
        for (unsigned k=0;k<4;++k) r[Reg::SPI_SHADER_USER_DATA_VS_0+12+k]=cb[k];
        r[Reg::VGT_PRIMITIVE_TYPE]=4; r[Reg::CB_COLOR_CONTROL]=1u<<4;
        for (unsigned k=0;k<5;++k) {
            auto reg=Reg::CB_COLOR0_BASE+k*Reg::kColorTargetStride;
            r[reg+Reg::CB_BASE]=uint32_t((address+65536+k*65536)>>8);
            r[reg+Reg::CB_INFO]=(12u<<2)|(7u<<8);
            r[reg+Reg::CB_PITCH]=7; r[reg+Reg::CB_SLICE]=63;
        }
        r[Reg::CB_TARGET_MASK]=r[Reg::CB_SHADER_MASK]=0xfffff;
        r[Reg::SPI_SHADER_COL_FORMAT]=0x99999;
        r[Reg::DB_Z_INFO]=3;
        r[Reg::DB_Z_READ_BASE]=r[Reg::DB_Z_WRITE_BASE]=uint32_t((address+7*65536)>>8);
        r[Reg::DB_DEPTH_SIZE]=7u|(7u<<11); r[Reg::DB_DEPTH_CONTROL]=6u|(7u<<4);
        r[Reg::PA_CL_VTE_CNTL]=0x43f; r[Reg::PA_CL_CLIP_CNTL]=0x80000;
        const float viewport[]{32,32,-32,32,1,0};
        for (unsigned i=0;i<6;++i) r[Reg::PA_CL_VPORT_XSCALE+i]=std::bit_cast<uint32_t>(viewport[i]);
        r[Reg::PA_SC_SCREEN_SCISSOR_BR]=r[Reg::PA_SC_WINDOW_SCISSOR_BR]=r[Reg::PA_SC_GENERIC_SCISSOR_BR]=64u|(64u<<16);
        Gpu::Metal::Device device;
        Gpu::Metal::MetalRenderer renderer(device);
        const auto frame=[&](bool enabled,bool valid_camera,bool valid_id) {
            renderer.SetGraphicsOptions(16,enabled); constants[0]=valid_camera ? 3000 : 100;
            id=valid_id ? 0x9a9cf8a9 : 0x11111111;
            // Change cached code padding too, forcing metadata revalidation.
            cs[3]++; std::memcpy(reinterpret_cast<char*>(cs)+48,&id,8);
            std::memcpy(shaders+128,cs,sizeof(cs));
            r[Reg::CB_TARGET_MASK]=0xfffff;
            renderer.Draw(r,Gpu::DrawCall{.count=3});
            r[Reg::CB_TARGET_MASK]=15;
            renderer.Draw(r,Gpu::DrawCall{.count=3});
            RegFile compute;
            compute[Reg::COMPUTE_PGM_LO]=uint32_t(uintptr_t(shaders+128)>>8);
            compute[Reg::COMPUTE_NUM_THREAD_X]=compute[Reg::COMPUTE_NUM_THREAD_Y]=compute[Reg::COMPUTE_NUM_THREAD_Z]=1;
            renderer.Dispatch(compute,1,1,1);
            renderer.Flip(Gpu::FlipInfo{.address=address+65536,.width=64,.height=64,.pitch=64});
            renderer.WaitIdle();
            assert(renderer.LatestFrame());
            assert(renderer.TemporalAAActive()==(enabled && valid_camera && valid_id));
        };
        frame(true,true,true);
        frame(false,true,true);
        frame(true,false,true);
        frame(true,true,false);
        puts("Production Metal temporal integration: G-buffer camera detection, scene boundary, live disable and invalid-input fallbacks passed");
    }
}
