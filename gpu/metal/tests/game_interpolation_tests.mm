// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "gpu/shader/msl.h"
#include "gpu/shader/msl_prelude.h"
#include "gpu/metal/mtl_compiler.h"
#include "src/runtime_gpu_memory.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" void runtime_restart() { std::abort(); }
int main() {
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        uint64_t physical=0;
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),65536,16384,0,&physical)==0);
        uintptr_t address=0;
        assert(runtime_gpu_memory_map(&address,65536,3,0,physical,16384)==0);
        auto* base=reinterpret_cast<unsigned char*>(address);
        const uint32_t descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(32u<<16),3,
            0xfacu|(7u<<12)|(14u<<15)};
        memcpy(base,descriptor,sizeof(descriptor));
        // Identical screen triangle in both modes, with a varying clip W.
        // The red attribute is 1 only at B. At pixel (3,3), B's affine weight
        // is 3.5/16, while its perspective weight is (3.5/16/4)/(1-3.5/16*3/4).
        const float vertices[]{-1,-1,0,1, 0,0,0,1,
            12,-4,0,4, 1,0,0,1, -1,3,0,1, 0,0,0,1};
        memcpy(base+256,vertices,sizeof(vertices));
        auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(2048));assert(shader);
        const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,
            0xe00c2010,0x80010500,0xf80008cf,0x04030201,
            0xf8000a0f,0x08070605,0xbf810000};
        memcpy(shader,vs,sizeof(vs));
        using namespace Gpu::Liverpool;
        RegFile regs;
        regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
        regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
        regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address);
        regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(address>>32);
        regs[Reg::VGT_PRIMITIVE_TYPE]=4;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+16384)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
        regs[Reg::CB_COLOR_CONTROL]=1u<<4;
        regs[Reg::CB_TARGET_MASK]=regs[Reg::CB_SHADER_MASK]=15;
        regs[Reg::SPI_SHADER_COL_FORMAT]=9;
        regs[Reg::PA_CL_VTE_CNTL]=0x43f;
        const float viewport[]{4,4,-4,4,1,0};
        for (unsigned i=0;i<6;++i) regs[Reg::PA_CL_VPORT_XSCALE+i]=std::bit_cast<uint32_t>(viewport[i]);
        for (auto reg : {Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,
                        Reg::PA_SC_GENERIC_SCISSOR_BR}) regs[reg]=8u|(8u<<16);
        Gpu::Metal::Device device;
        Gpu::Metal::MetalRenderer renderer(device);
        auto metal=(__bridge id<MTLDevice>)device.Handle();
        auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
        auto readback=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
        // The usual full-screen viewport is an identity in clip space. Its
        // implementation must not round distant depth through z/w and *w.
        Gpu::Metal::ShaderCompiler compiler(metal,1);
        std::string source=Gpu::Shader::kMslPrelude;
        source+=R"(
            kernel void main0(device const float4* positions [[buffer(0)]],
                constant DcDraw& draw [[buffer(1)]], device float4* result [[buffer(2)]],
                uint index [[thread_position_in_grid]]) {
                result[index]=dc_viewport(positions[index],draw);
            })";
        auto function=compiler.Function(0x76500001,source,0);assert(function);
        NSError* error=nil;
        auto pipeline=[metal newComputePipelineStateWithFunction:function error:&error];assert(pipeline);
        const float positions[]{.3f,-.5f,123.45f,124.f,
            9.3f,-6.7f,31.324f,31.4f, -37.53f,19.1f,98.97f,99.f,
            .173f,.612f,.99973f,1.01f};
        auto input=[metal newBufferWithBytes:positions length:sizeof(positions) options:MTLResourceStorageModeShared];
        auto output=[metal newBufferWithLength:sizeof(positions) options:MTLResourceStorageModeShared];
        auto draw=Gpu::Shader::MakeDrawConsts(0x43f,{960,960,-540,540,1,0},1920,1080);
        auto command=[queue commandBuffer];auto encoder=[command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];[encoder setBuffer:input offset:0 atIndex:0];
        [encoder setBytes:&draw length:sizeof(draw) atIndex:1];
        [encoder setBuffer:output offset:0 atIndex:2];
        [encoder dispatchThreads:MTLSizeMake(4,1,1) threadsPerThreadgroup:MTLSizeMake(4,1,1)];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
        auto* clip=static_cast<const float*>(output.contents);
        for (unsigned i=0;i<16;++i) if (clip[i]!=positions[i]) {
            fprintf(stderr,"Full-screen viewport changed clip component %u: %.9g -> %.9g\n",i,positions[i],clip[i]);
            return 1;
        }

        // Atlas quadrants, non-default Z range, and pre-divided XY/Z inputs.
        // Compare homogeneous output with a double-precision viewport oracle.
        const std::array<float,6> atlas{1024,3072,-1024,1024,.5f,.5f};
        for (uint32_t vte : {0x43fu,0x53fu,0x63fu,0x73fu,0x400u}) {
            draw=Gpu::Shader::MakeDrawConsts(vte,atlas,4096,4096);
            command=[queue commandBuffer];encoder=[command computeCommandEncoder];
            [encoder setComputePipelineState:pipeline];[encoder setBuffer:input offset:0 atIndex:0];
            [encoder setBytes:&draw length:sizeof(draw) atIndex:1];
            [encoder setBuffer:output offset:0 atIndex:2];
            [encoder dispatchThreads:MTLSizeMake(4,1,1) threadsPerThreadgroup:MTLSizeMake(4,1,1)];
            [encoder endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
            for (unsigned n=0;n<4;++n) for (unsigned c=0;c<4;++c) {
                const double w=positions[n*4+3];
                double expected=positions[n*4+c];
                if (c<3) {
                    if (!(vte&(c<2 ? 0x100 : 0x200))) expected/=w;
                    expected=expected*((vte&(1u<<(2*c))) ? atlas[2*c] : 1.0)+
                        ((vte&(2u<<(2*c))) ? atlas[2*c+1] : 0.0);
                    if (c==0) expected=expected*2/4096-1;
                    if (c==1) expected=1-expected*2/4096;
                    expected*=w;
                }
                if (std::abs(clip[n*4+c]-expected)>2e-6*std::max(1.0,std::abs(expected))) {
                    fprintf(stderr,"Viewport vte=%#x component=%u: expected %.9g got %.9g\n",
                        vte,n*4+c,expected,clip[n*4+c]);return 1;
                }
            }
        }
        for (unsigned mode : {1u,5u,6u,7u}) {
            const bool mixed=mode==7;
            const unsigned bary=mixed ? 2 : 0;
            // v_interp_p1/p2 v6, bary.{i,j}, attr0.x; export (v6,0,0,1).
            const uint32_t ps[]{0xc8000000u|(6u<<18)|bary,
                0xc8010000u|(6u<<18)|(bary+1),0x7e0e0280,0x7e100280,0x7e1202f2,
                0xf800080f,0x09080706,0xbf810000};
            memcpy(shader+64,ps,sizeof(ps));
            regs[Reg::SPI_PS_INPUT_ADDR]=mixed ? (1u<<1)|(1u<<5) : 1u<<mode;
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
            renderer.Flip(Gpu::FlipInfo{.address=address+16384,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            auto frame=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(frame);
            auto cmd=[queue commandBuffer];auto blit=[cmd blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding];[cmd commit];[cmd waitUntilCompleted];assert(!cmd.error);
            const double weight=3.5/16;
            const int expected=std::lround(255*(mode==1 ? weight/4/(1-weight*3/4) : weight));
            const auto* pixels=static_cast<const unsigned char*>(readback.contents);
            const int actual=pixels[(3*8+3)*4+2];
            std::printf("Interpolation mode %u: expected red %d, got %d\n",mode,expected,actual);
            if (std::abs(actual-expected)>1) return 1;
        }
        puts("Metal interpolation: clip precision, atlas viewports, perspective, linear, centroid and mixed barycentric layouts passed");
    }
}
