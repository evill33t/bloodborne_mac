// SPDX-License-Identifier: GPL-2.0-or-later
// Runs synthetic GCN through the production PM4/Metal renderer, without game data.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "src/runtime_gpu_memory.h"
#include "gcn_fixtures.h"
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
        // Exercise normal launches: their first draws must survive cold
        // shader compilation without a diagnostic environment override.
        unsetenv("DC_SYNC_SHADERS");
        uint64_t physical=0;
        assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical)==0);
        uintptr_t address=0;
        assert(runtime_gpu_memory_map(&address,131072,3,0,physical,16384)==0);
        auto* base=reinterpret_cast<unsigned char*>(address);
        // A user-data descriptor and three float4 vertices covering the viewport.
        uint32_t descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(16u<<16),3,
            0xfacu|(7u<<12)|(14u<<15)};
        memcpy(base,descriptor,sizeof(descriptor));
        const float vertices[]{-1,-1,0,1, 3,-1,0,1, -1,3,0,1};
        memcpy(base+256,vertices,sizeof(vertices));
        auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(2048));
        assert(shader);
        // s_load_dwordx4 s[4:7], s[0:1], 0;
        // buffer_load_format_xyzw v[1:4], v0, s[4:7], 0 idxen;
        // exp pos0 v1,v2,v3,v4; s_endpgm.
        const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
        // Red pixel export, deliberately distinct from blue.
        const uint32_t ps[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,
            0xf800080f,0x03020100,0xbf810000};
        memcpy(shader,vs,sizeof(vs)); memcpy(shader+64,ps,sizeof(ps));
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
        regs[Reg::CB_TARGET_MASK]=15;
        regs[Reg::CB_SHADER_MASK]=15;
        regs[Reg::SPI_SHADER_COL_FORMAT]=9;
        regs[Reg::PA_CL_VTE_CNTL]=0x43f;
        const float viewport[]{4,4,-4,4,1,0};
        for (int i=0;i<6;++i) regs[Reg::PA_CL_VPORT_XSCALE+i]=std::bit_cast<uint32_t>(viewport[i]);
        regs[Reg::PA_SC_SCREEN_SCISSOR_BR]=8u|(8u<<16);
        regs[Reg::PA_SC_WINDOW_SCISSOR_BR]=8u|(8u<<16);
        regs[Reg::PA_SC_GENERIC_SCISSOR_BR]=8u|(8u<<16);
        regs[Reg::PA_SC_VPORT_SCISSOR_0_TL]=6u|(6u<<16);
        regs[Reg::PA_SC_VPORT_SCISSOR_0_TL+1]=8u|(8u<<16);
        regs[Reg::PA_SC_MODE_CNTL_0]=2;
        Gpu::Metal::Device device;
        Gpu::Metal::MetalRenderer renderer(device);
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        // The game disables viewport scissors after tiled lighting without
        // rewriting their last small rectangle. The next pass must cover all 8x8.
        regs[Reg::PA_SC_MODE_CNTL_0]=0;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        // Surface copies use descriptors at UD4/UD8, not assumed UD0/UD4.
        const uint32_t cs[]{0xe0002000,0x80010100,0xbf8c0f70,0xe0102000,0x80020100,0xbf810000};
        memcpy(shader+128,cs,sizeof(cs));
        RegFile compute;
        compute[Reg::COMPUTE_PGM_LO]=uint32_t(uintptr_t(shader+128)>>8);
        compute[Reg::COMPUTE_PGM_RSRC2]=12u<<1;
        compute[Reg::COMPUTE_NUM_THREAD_X]=64;
        compute[Reg::COMPUTE_NUM_THREAD_Y]=compute[Reg::COMPUTE_NUM_THREAD_Z]=1;
        const uint32_t src[]{uint32_t(address+16384),uint32_t(address>>32)|(4u<<16),64,0x24004};
        const uint32_t dst[]{uint32_t(address+32768),uint32_t(address>>32)|(4u<<16),64,0x24004};
        for (int i=0;i<4;++i) {
            compute[Reg::COMPUTE_USER_DATA_0+4+i]=src[i];
            compute[Reg::COMPUTE_USER_DATA_0+8+i]=dst[i];
        }
        renderer.Dispatch(compute,1,1,1);
        renderer.Flip(Gpu::FlipInfo{.address=address+32768,.width=8,.height=8,.pitch=8});
        std::string drain_error;
        assert(renderer.WaitIdleUntil(std::chrono::steady_clock::now()+std::chrono::seconds(5),drain_error));
        id<MTLTexture> frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        id<MTLDevice> metal=(__bridge id<MTLDevice>)device.Handle();
        id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)device.Queue();
        id<MTLBuffer> readback=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        const auto* pixels=static_cast<const unsigned char*>(readback.contents);
        for (int i=0;i<64;++i) {
            if (pixels[i*4]!=0 || pixels[i*4+2]!=255) {
                fprintf(stderr,"Raster pixel %d: B=%u R=%u\n",i,pixels[i*4],pixels[i*4+2]);
                std::abort();
            }
        }
        // Alpha-tested shadow casters need their fragment shader even without
        // a color attachment. Discarded fragments must leave depth untouched.
        const uint32_t kill_ps[]{0x7c200080,0xf800080f,0x00000000,0xbf810000};
        memcpy(shader+384,kill_ps,sizeof(kill_ps));
        for (unsigned discard : {0u,1u}) {
            RegFile shadow=regs;
            const uint64_t z=address+65536+discard*4096;
            const uint64_t color=address+81920+discard*4096;
            shadow[Reg::CB_COLOR_CONTROL]=0;
            shadow[Reg::CB_TARGET_MASK]=0;
            shadow[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+(discard ? 384 : 64))>>8);
            shadow[Reg::DB_SHADER_CONTROL]=discard ? 1u<<6 : 0;
            shadow[Reg::DB_Z_INFO]=3;
            shadow[Reg::DB_Z_READ_BASE]=shadow[Reg::DB_Z_WRITE_BASE]=uint32_t(z>>8);
            shadow[Reg::DB_DEPTH_CONTROL]=6u|(1u<<4); // write / less
            shadow[Reg::PA_CL_CLIP_CNTL]=0x80000;
            renderer.Draw(shadow,Gpu::DrawCall{.count=3});
            // A farther red surface is visible only where the caster discarded.
            auto* v=reinterpret_cast<float*>(base+256);
            for (unsigned n=0;n<3;++n) v[n*4+2]=.5f;
            shadow[Reg::CB_COLOR_CONTROL]=1u<<4;
            shadow[Reg::CB_TARGET_MASK]=15;
            shadow[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(color>>8);
            shadow[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
            shadow[Reg::DB_SHADER_CONTROL]=0;
            renderer.Draw(shadow,Gpu::DrawCall{.count=3});
            renderer.Flip(Gpu::FlipInfo{.address=color,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            frame=(__bridge id<MTLTexture>)renderer.LatestFrame();
            command=[queue commandBuffer]; blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            for (unsigned n=0;n<64;++n) if (pixels[n*4+2]!=(discard ? 255 : 0)) {
                fprintf(stderr,"Depth-only discard=%u pixel %u: R=%u\n",discard,n,pixels[n*4+2]);
                std::abort();
            }
            for (unsigned n=0;n<3;++n) v[n*4+2]=0;
        }
        // Two one-control-point quad patches. The local shader stores the
        // patch's vertex index as a float in LDS; the domain shader uses it to
        // put one patch on each half of the target. This tests isolation and
        // transfer of actual LS data, hull factors, and native tessellation.
        const auto vop1=[](unsigned op,unsigned dst,unsigned src) {
            return 0x7e000000u|(dst<<17)|((op-384)<<9)|src;
        };
        const auto add=[](unsigned dst,unsigned src0,unsigned src1) {
            return (3u<<25)|(dst<<17)|(src1<<9)|src0;
        };
        const uint32_t ls[]{vop1(390,4,256),vop1(385,5,128),
            0xd8000000u|(13u<<18),5u|(4u<<8),0xbf810000};
        std::vector<uint32_t> hs{vop1(385,1,242)};
        for (unsigned k=0;k<6;++k) {
            hs.push_back(0xe0000000u|(28u<<18)|(k*4));
            hs.push_back(0x08000100);
        }
        hs.push_back(0xbf810000);
        const uint32_t ds[]{vop1(385,5,128),0xd8000000u|(54u<<18),(4u<<24)|5u,
            add(0,260,0),add(0,243,0),add(1,257,1),add(1,243,1),
            vop1(385,6,128),vop1(385,7,242),0xf80008cf,0x07060100,0xbf810000};
        memcpy(shader+192,ls,sizeof(ls));
        memcpy(shader+256,hs.data(),hs.size()*4);
        memcpy(shader+320,ds,sizeof(ds));
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+49152)>>8);
        regs[Reg::VGT_PRIMITIVE_TYPE]=9;
        regs[Reg::SPI_SHADER_PGM_LO_LS]=uint32_t(uintptr_t(shader+192)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_HS]=uint32_t(uintptr_t(shader+256)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_HS+3]=8u<<1;
        regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader+320)>>8);
        regs[Reg::VGT_LS_HS_CONFIG]=1u|(1u<<8)|(1u<<14);
        regs[Reg::VGT_TF_PARAM]=2u|(2u<<5);
        renderer.Draw(regs,Gpu::DrawCall{.count=2});
        renderer.Flip(Gpu::FlipInfo{.address=address+49152,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        command=[queue commandBuffer];
        blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        for (int i=0;i<64;++i) {
            if (pixels[i*4]!=0 || pixels[i*4+2]!=255) {
                fprintf(stderr,"Tessellated pixel %d: B=%u R=%u\n",i,pixels[i*4],pixels[i*4+2]);
                std::abort();
            }
        }
        // A repeated completed light-volume draw must reuse its temporary
        // storage rather than allocating fresh Metal buffers every frame.
        renderer.Draw(regs,Gpu::DrawCall{.count=2});
        renderer.Flip(Gpu::FlipInfo{.address=address+49152,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        const auto allocations=renderer.UploadBufferAllocationCount();
        for (unsigned repeat=0;repeat<8;++repeat) {
            renderer.Draw(regs,Gpu::DrawCall{.count=2});
            renderer.Flip(Gpu::FlipInfo{.address=address+49152,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
        }
        if (renderer.UploadBufferAllocationCount()!=allocations) {
            fprintf(stderr,"Completed tessellation buffers were reallocated on repeated frames\n");return 1;
        }
        // Indexed single-control-point patches select their LS vertex on the
        // GPU, including 16-bit unaligned loads, index offsets and base vertex.
        for (unsigned index_size : {2u,4u}) {
            const uint16_t indices16[]{99,1,1};
            const uint32_t indices32[]{99,0,0};
            if (index_size==2) memcpy(base+512,indices16,sizeof(indices16));
            else memcpy(base+512,indices32,sizeof(indices32));
            regs[Reg::VGT_INDX_OFFSET]=index_size==4 ? 1 : 0;
            const uint64_t target=address+(index_size==2 ? 57344 : 61440);
            regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(target>>8);
            renderer.Draw(regs,Gpu::DrawCall{.count=2,.indexed=true,.index_base=address+512,
                .index_size=index_size,.index_offset=1});
            renderer.Flip(Gpu::FlipInfo{.address=target,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
            command=[queue commandBuffer]; blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            for (int i=0;i<64;++i) {
                if (pixels[i*4+2]!=((i%8)>=4 ? 255 : 0)) {
                    fprintf(stderr,"Indexed tessellation size=%u pixel=%d R=%u\n",index_size,i,pixels[i*4+2]);
                    std::abort();
                }
            }
        }
        regs[Reg::VGT_INDX_OFFSET]=0;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+49152)>>8);
        // A Gnm buffer-fill dispatch must erase the private color texture as
        // well as its backing pool. Otherwise old bright pixels leave trails.
        for (unsigned test=0;test<7;++test) {
            const bool twice=test!=0, colored=test>=2;
            if (test==3) regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]|=6u<<8; // sRGB CB
            renderer.Draw(regs,Gpu::DrawCall{.count=2}); // red again
            const auto fill=TestGcn::UniformFill(twice);
            memcpy(shader+128,fill.data(),fill.size()*4);
            compute[Reg::COMPUTE_PGM_RSRC2]=(12u<<1)|(1u<<7);
            const uint32_t target[]{uint32_t(address+49152),uint32_t(address>>32)|(4u<<16),64,0x50fac};
            const uint32_t value[]{uint32_t(address+128),uint32_t(address>>32)|(16u<<16),1,0x77fac};
            memset(base+128,0,16);
            if (colored) {
                float values[]{.25f,.5f,1,1};
                if(test==4) {values[0]=-.5f;values[1]=2;values[2]=.5f;values[3]=0;}
                if(test==5) {values[0]=126.5f/255;values[1]=127.5f/255;values[2]=128.5f/255;values[3]=129.5f/255;}
                if(test==6) {values[0]=.1f;values[1]=.9f;values[2]=1.f/255;values[3]=254.f/255;}
                memcpy(base+128,values,sizeof(values));
            }
            memset(base+49148,0xa5,4);memset(base+49152+64*4,0xa5,4);
            memset(base+49152,0x6d,64*4);
            for (int i=0;i<4;++i) {
                compute[Reg::COMPUTE_USER_DATA_0+4+i]=target[i];
                compute[Reg::COMPUTE_USER_DATA_0+8+i]=value[i];
            }
            renderer.Dispatch(compute,1,1,1);
            renderer.Flip(Gpu::FlipInfo{.address=address+49152,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            if (!colored) for (unsigned k=0;k<64*4;++k) assert(base[49152+k]==0);
            const uint8_t expected[7][4]{{0,0,0,0},{0,0,0,0},{64,128,255,255},{64,128,255,255},
                {0,255,128,0},{126,128,128,130},{26,230,1,254}};
            for(unsigned k=0;k<4;++k)assert(base[49148+k]==0xa5 && base[49152+64*4+k]==0xa5);
            for(unsigned k=0;k<64*4;++k)assert(base[49152+k]==expected[test][k%4]);
            frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
            command=[queue commandBuffer]; blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            for (int i=0;i<64;++i) {
                // Metal attachment clears can differ by one UNORM step near
                // halfway boundaries. That existing clear operation is identical
                // in both paths; the backing-buffer check above stays bit exact.
                const int tolerance=test>=5 ? 1 : 0;
                if (std::abs(int(pixels[i*4])-expected[test][0])>tolerance ||
                    std::abs(int(pixels[i*4+1])-expected[test][1])>tolerance ||
                    std::abs(int(pixels[i*4+2])-expected[test][2])>tolerance) {
                    fprintf(stderr,"Fill test %u pixel %d: B=%u G=%u R=%u\n",test,i,pixels[i*4],pixels[i*4+1],pixels[i*4+2]);
                    std::abort();
                }
            }
        }
        // Face generation uploads integer RGBA texels, then samples the same
        // private texture through normalized and sRGB descriptors. The view
        // must reinterpret the bytes rather than keep integer sample results.
        const uint32_t integer_ps[]{vop1(385,0,255),128,vop1(385,1,255),64,
            vop1(385,2,255),192,vop1(385,3,255),255,
            0xf800080f,0x03020100,0xbf810000};
        const uint32_t sample_ps[]{vop1(385,0,240),vop1(385,1,240),
            0xf0000000u|(32u<<18)|(15u<<8), (2u<<21)|(2u<<8),
            0xf800080f,0x05040302,0xbf810000};
        memcpy(shader+128,integer_ps,sizeof(integer_ps));
        memcpy(shader+192,sample_ps,sizeof(sample_ps));
        regs[Reg::VGT_PRIMITIVE_TYPE]=4;
        regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+128)>>8);
        regs[Reg::SPI_SHADER_COL_FORMAT]=9;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+32768)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(4u<<8); // RGBA8Uint
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        for (unsigned srgb=0;srgb<2;++srgb) {
            regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+192)>>8);
            regs[Reg::SPI_SHADER_PGM_RSRC2_PS]=12u<<1;
            regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+16384)>>8);
            regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
            const uint32_t texture[]{uint32_t((address+32768)>>8),
                uint32_t((address+32768)>>40)|(10u<<20)|((srgb ? 9u : 0u)<<26),
                7u|(7u<<14),0x90e00fac,7u<<13,0,0,0};
            for (unsigned k=0;k<12;++k)
                regs[Reg::SPI_SHADER_USER_DATA_PS_0+k]=k<8 ? texture[k] : 0;
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
            renderer.Flip(Gpu::FlipInfo{.address=address+16384,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
            command=[queue commandBuffer]; blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            const int expected[]{srgb ? 134 : 192,srgb ? 13 : 64,srgb ? 55 : 128,255};
            for (unsigned i=0;i<64;++i) for (unsigned k=0;k<4;++k) {
                if (std::abs(int(pixels[i*4+k])-expected[k])>1) {
                    fprintf(stderr,"Integer texture view srgb=%u pixel=%u channel=%u: expected %d got %u\n",
                        srgb,i,k,expected[k],pixels[i*4+k]); std::abort();
                }
            }
        }
        // Blood compositing can bind a generated integer image as normalized
        // or sRGB color. A transparent decal must preserve its original bytes,
        // including when alternating between formats at the same guest address.
        const uint32_t transparent_ps[]{vop1(385,0,242),vop1(385,1,128),
            vop1(385,2,128),vop1(385,3,128),0xf800080f,0x03020100,0xbf810000};
        memcpy(shader+320,transparent_ps,sizeof(transparent_ps));
        for (unsigned number : {0u,6u,0u}) {
            regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+320)>>8);
            regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+32768)>>8);
            regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(number<<8);
            regs[Reg::CB_BLEND0_CONTROL]=(1u<<30)|(1u<<29)|4u|(5u<<8)|(1u<<24);
            renderer.Draw(regs,Gpu::DrawCall{.count=3});
            renderer.Flip(Gpu::FlipInfo{.address=address+32768,.width=8,.height=8,.pitch=8});
            renderer.WaitIdle();
            frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
            command=[queue commandBuffer]; blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:32 destinationBytesPerImage:256];
            [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            const int expected[]{192,64,128,255};
            for (unsigned i=0;i<64;++i) for (unsigned k=0;k<4;++k) {
                if (std::abs(int(pixels[i*4+k])-expected[k])>1) {
                    fprintf(stderr,"Transparent decal format=%u pixel=%u channel=%u: expected %d got %u\n",
                        number,i,k,expected[k],pixels[i*4+k]); std::abort();
                }
            }
        }
        regs[Reg::CB_BLEND0_CONTROL]=0;
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+192)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((address+16384)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
        // Guest texture memory can reuse an old render-target address at a
        // different size. Sampling must upload the new image, not reuse the
        // stale private surface. Fill the padded tile with uniform green.
        for (unsigned k=0;k<64;++k) {
            base[32768+k*4]=0; base[32768+k*4+1]=255;
            base[32768+k*4+2]=0; base[32768+k*4+3]=255;
        }
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+1]=uint32_t((address+32768)>>40)|(10u<<20);
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+2]=3u|(3u<<14);
        regs[Reg::SPI_SHADER_USER_DATA_PS_0+3]=0x90d00fac;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        renderer.Flip(Gpu::FlipInfo{.address=address+16384,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        command=[queue commandBuffer]; blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        for (unsigned k=0;k<64;++k) assert(pixels[k*4]==0 && pixels[k*4+1]==255 && pixels[k*4+2]==0);
        // A typed clear can precede the first render-target bind. Its bytes
        // must survive a transparent decal, including the neutral alpha of 1.
        const uint64_t first_fill_target=address+28672;
        const auto first_fill=TestGcn::UniformFill(false);
        memcpy(shader+128,first_fill.data(),first_fill.size()*4);
        compute[Reg::COMPUTE_PGM_RSRC2]=(12u<<1)|(1u<<7);
        const float first_value[]{.25f,.5f,1,1};
        memcpy(base+128,first_value,sizeof(first_value));
        const uint32_t first_destination[]{uint32_t(first_fill_target),uint32_t(first_fill_target>>32)|(4u<<16),64,0x50fac};
        const uint32_t first_source[]{uint32_t(address+128),uint32_t(address>>32)|(16u<<16),1,0x77fac};
        for (unsigned k=0;k<4;++k) {
            compute[Reg::COMPUTE_USER_DATA_0+4+k]=first_destination[k];
            compute[Reg::COMPUTE_USER_DATA_0+8+k]=first_source[k];
        }
        renderer.Dispatch(compute,1,1,1);
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+320)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(first_fill_target>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=10u<<2;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_ATTRIB]=8; // linear aligned
        regs[Reg::CB_BLEND0_CONTROL]=0x65000504;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        renderer.Flip(Gpu::FlipInfo{.address=first_fill_target,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        command=[queue commandBuffer]; blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        const int first_expected[]{255,128,64,255};
        for (unsigned i=0;i<64;++i) for (unsigned k=0;k<4;++k) {
            if (pixels[i*4+k]!=first_expected[k]) {
                fprintf(stderr,"Buffer fill before first bind pixel=%u channel=%u: expected %d got %u\n",
                    i,k,first_expected[k],pixels[i*4+k]); std::abort();
            }
        }
        // Nonuniform micro-tiled bytes also survive the first sRGB bind.
        const uint64_t tiled_target=address+4096;
        for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) {
            const unsigned morton=(x&1)|((y&1)<<1)|((x&2)<<1)|((y&2)<<2)|
                ((x&4)<<2)|((y&4)<<3);
            const unsigned char rgba[]{static_cast<unsigned char>(x*32),
                static_cast<unsigned char>(y*32),160,255};
            memcpy(base+4096+morton*4,rgba,4);
        }
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(tiled_target>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(6u<<8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_ATTRIB]=13; // Thin_1dThin
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        renderer.Flip(Gpu::FlipInfo{.address=tiled_target,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        command=[queue commandBuffer]; blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        for (unsigned y=0;y<8;++y) for (unsigned x=0;x<8;++x) {
            const unsigned i=(y*8+x)*4;
            assert(pixels[i]==160 && pixels[i+1]==y*32 && pixels[i+2]==x*32 && pixels[i+3]==255);
        }
        // A blood decal's alpha blend stores transmittance (dst A*(1-src A)).
        // Its new fast-cleared image must start at the programmed neutral alpha
        // of one, even if the CMASK was filled before the target was registered.
        const uint64_t decal_target=address+24576,cmask=address+12288;
        assert(renderer.DmaData(0,cmask,2,0,256));
        regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+320)>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(decal_target>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<13);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_CMASK]=uint32_t(cmask>>8);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_CLEAR_WORD0]=0xff804020;
        regs[Reg::CB_COLOR0_BASE+Reg::CB_CLEAR_WORD1]=0;
        regs[Reg::CB_BLEND0_CONTROL]=0x65000504;
        regs[Reg::CB_COLOR_CONTROL]=2u<<4;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        regs[Reg::CB_COLOR_CONTROL]=1u<<4;
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        renderer.Flip(Gpu::FlipInfo{.address=decal_target,.width=8,.height=8,.pitch=8});
        renderer.WaitIdle();
        frame=(__bridge id<MTLTexture>)renderer.LatestFrame(); assert(frame);
        command=[queue commandBuffer]; blit=[command blitCommandEncoder];
        [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        const int decal_expected[]{128,64,32,255};
        for (unsigned i=0;i<64;++i) for (unsigned k=0;k<4;++k) {
            if (pixels[i*4+k]!=decal_expected[k]) {
                fprintf(stderr,"New fast-clear decal pixel=%u channel=%u: expected %d got %u\n",
                    i,k,decal_expected[k],pixels[i*4+k]); std::abort();
            }
        }
        // The same code uses different system-SGPR locations when USER_SGPR
        // changes. Cached shaders must honor the current resource registers.
        const uint32_t system_cs[]{vop1(385,1,4),0xe0002000u|(28u<<18),0x80000100,0xbf810000};
        memcpy(shader+128,system_cs,sizeof(system_cs));
        RegFile system;
        system[Reg::COMPUTE_PGM_LO]=uint32_t(uintptr_t(shader+128)>>8);
        system[Reg::COMPUTE_NUM_THREAD_X]=system[Reg::COMPUTE_NUM_THREAD_Y]=system[Reg::COMPUTE_NUM_THREAD_Z]=1;
        const uint32_t output[]{uint32_t(address+1024),uint32_t(address>>32)|(4u<<16),1,0x24004};
        for (unsigned k=0;k<4;++k) system[Reg::COMPUTE_USER_DATA_0+k]=output[k];
        system[Reg::COMPUTE_USER_DATA_0+4]=0x12345678;
        for (unsigned count : {4u,8u}) {
            system[Reg::COMPUTE_PGM_RSRC2]=(count<<1)|(1u<<7);
            renderer.Dispatch(system,1,1,1); renderer.WaitIdle();
            uint32_t actual; memcpy(&actual,base+1024,4);
            const uint32_t expected=count==4 ? 0 : 0x12345678;
            if (actual!=expected) {
                fprintf(stderr,"System register layout USER_SGPR=%u: expected %#x got %#x\n",count,expected,actual);
                std::abort();
            }
        }
        puts("Metal raster: scissor, surface copy, indexed tessellation, typed clears, queued color initialization, material views, address reuse and shader register layouts passed");
    }
}
