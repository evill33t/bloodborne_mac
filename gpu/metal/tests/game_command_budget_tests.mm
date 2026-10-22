// SPDX-License-Identifier: GPL-2.0-or-later
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/command_budget.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "gpu/liverpool/pm4.h"
#include "src/runtime_gpu_memory.h"
#include "gcn_fixtures.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <limits>
#include <thread>
#include <vector>

extern "C" void runtime_restart() {std::abort();}
static void Policy() {
    using namespace Gpu::Metal::CommandBudget;
    const char* invalid[]{nullptr,"","0","-1","249","16001","1000x"," 1000","4294967296"};
    for(const char* text:invalid)assert(!Parse(text));
    assert(Parse("250")==250 && Parse("2000")==2000 && Parse("16000")==16000);
    // A disabled budget must retain the original work-count decision even
    // after arbitrarily slow CPU recording. An empty buffer never ages out.
    for(unsigned work=0;work<500;++work) {
        assert(Due(work,0,1,100)==(work>=400 ? Reason::WorkLimit : Reason::None));
        if(work>0 && work<400)assert(Due(work,2000,1,1.003)==Reason::ElapsedTime);
    }
    assert(Due(0,2000,1,100)==Reason::None);
    assert(Due(1,2000,1,1.001)==Reason::None);
    assert(Due(1,2000,1,.9)==Reason::None);
    assert(Due(1,2000,0,2)==Reason::None);
    assert(Due(1,2000,1,std::numeric_limits<double>::quiet_NaN())==Reason::None);
    std::puts("Command budget: disabled scheduling, strict bounds, count limit, elapsed time and clock guards passed");
}
static void GpuTest(bool benchmark) {
    using namespace Gpu::Liverpool;
    using Clock=std::chrono::steady_clock;
    unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");unsetenv("DC_SYNC_SHADERS");
    uint64_t physical=0;uintptr_t address=0;
    const unsigned width=benchmark ? 1920 : 8,height=benchmark ? 1080 : 8;
    const auto bytes=benchmark ? 32ull<<20 : 131072;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),bytes,16384,0,&physical));
    assert(!runtime_gpu_memory_map(&address,bytes,3,0,physical,16384));
    auto* base=reinterpret_cast<unsigned char*>(address);
    const uint32_t descriptor[]{uint32_t(address+256),uint32_t((address+256)>>32)|(16u<<16),3,
        0xfacu|(7u<<12)|(14u<<15)};
    std::memcpy(base,descriptor,sizeof(descriptor));
    const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
    std::memcpy(base+256,vertices,sizeof(vertices));
    auto* shader=static_cast<uint32_t*>(bbmetal_guest_allocate(2048));assert(shader);
    const uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    const uint32_t ps[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    std::memcpy(shader,vs,sizeof(vs));std::memcpy(shader+64,ps,sizeof(ps));
    RegFile regs;
    regs[Reg::SPI_SHADER_PGM_LO_VS]=uint32_t(uintptr_t(shader)>>8);
    regs[Reg::SPI_SHADER_PGM_LO_PS]=uint32_t(uintptr_t(shader+64)>>8);
    regs[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    regs[Reg::SPI_SHADER_USER_DATA_VS_0]=uint32_t(address);
    regs[Reg::SPI_SHADER_USER_DATA_VS_0+1]=uint32_t(address>>32);
    regs[Reg::VGT_PRIMITIVE_TYPE]=4;
    regs[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    regs[Reg::CB_COLOR_CONTROL]=1u<<4;regs[Reg::CB_TARGET_MASK]=regs[Reg::CB_SHADER_MASK]=15;
    regs[Reg::SPI_SHADER_COL_FORMAT]=9;regs[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{float(width)*.5f,float(width)*.5f,-float(height)*.5f,float(height)*.5f,1,0};
    for(unsigned k=0;k<6;++k)regs[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<uint32_t>(viewport[k]);
    for(unsigned reg:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,Reg::PA_SC_GENERIC_SCISSOR_BR})
        regs[reg]=width|(height<<16);
    Gpu::Metal::Device device;
    const std::vector<bool> modes=benchmark ? std::vector<bool>{false,true,true,false} : std::vector<bool>{false,true};
    for(bool enabled:modes) {
        setenv("BB_METAL_COMMAND_BUDGET_US",enabled ? "1000" : "0",1);
        Gpu::Metal::MetalRenderer renderer(device);
        uint32_t context[17]{};
        context[0]=Reg::CB_COLOR0_BASE-kContextRegBase;
        context[15]=0xc0001000;context[16]=width|(height<<16);
        renderer.OnContextRegs(context,15,17); // actual attachment extent, not just final presentation size
        renderer.NoteConstDump(address,16); // prepared CE roots must survive the cut
        const auto a=address+(benchmark ? 1ull<<20 : 16384),b=address+(benchmark ? 12ull<<20 : 32768);
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(a>>8);
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        const auto initial=renderer.CommandBudgetCommitCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        assert(renderer.CommandBudgetCommitCount()==initial); // no split inside the reused pass
        std::promise<void> signaled;auto done=signaled.get_future();
        renderer.SignalNoDirectMemoryWrite([&]{signaled.set_value();});
        regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t(b>>8);
        const auto before=Clock::now();
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        if(enabled) {
            assert(renderer.CommandBudgetCommitCount()>initial);
            // Work reaches the GPU before an explicit CP submit or frame flip.
            assert(done.wait_for(std::chrono::seconds(2))==std::future_status::ready);
        } else {
            assert(!renderer.CommandBudgetCommitCount());
            assert(done.wait_for(std::chrono::milliseconds(0))==std::future_status::timeout);
        }
        const auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
        renderer.OnSubmit();assert(done.wait_for(std::chrono::seconds(2))==std::future_status::ready);
        renderer.WaitIdle();
        const auto fresh_count=renderer.CommandBudgetCommitCount();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
        renderer.Draw(regs,Gpu::DrawCall{.count=3});
        assert(renderer.CommandBudgetCommitCount()==fresh_count); // no stale timestamp / empty-buffer commit
        renderer.WaitIdle();
        if(benchmark) {
            // Synthetic CPU-limited recording, not gameplay FPS. Shader/target
            // creation above is drained before timing. Both modes encode the
            // same 16 natural passes and no presentation or readback is timed.
            constexpr unsigned passes=16;
            std::array<std::promise<double>,passes> completed;
            std::array<std::future<double>,passes> observed;
            std::array<Clock::time_point,passes> recorded;
            for(unsigned n=0;n<passes;++n)observed[n]=completed[n].get_future();
            const auto started=Clock::now();
            for(unsigned n=0;n<passes;++n) {
                regs[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=uint32_t((n%2 ? b : a)>>8);
                renderer.Draw(regs,Gpu::DrawCall{.count=3});
                recorded[n]=Clock::now();
                renderer.SignalNoDirectMemoryWrite([&,n]{completed[n].set_value(
                    std::chrono::duration<double,std::milli>(Clock::now()-recorded[n]).count());});
                std::this_thread::sleep_for(std::chrono::milliseconds(3));
            }
            const auto recorded_end=Clock::now();
            renderer.OnSubmit();renderer.WaitIdle();
            const auto gpu_done=Clock::now();
            double sum=0,maximum=0;
            for(auto& future:observed) {
                assert(future.wait_for(std::chrono::seconds(2))==std::future_status::ready);
                const auto latency=future.get();sum+=latency;maximum=std::max(maximum,latency);
            }
            std::printf("BUDGET_BENCH {\"enabled\":%s,\"width\":%u,\"height\":%u,\"passes\":%u,"
                "\"cpu_record_ms\":%.6f,\"record_to_gpu_complete_ms\":%.6f,\"callback_mean_ms\":%.6f,\"callback_max_ms\":%.6f}\n",
                enabled ? "true" : "false",width,height,passes,
                std::chrono::duration<double,std::milli>(recorded_end-started).count(),
                std::chrono::duration<double,std::milli>(gpu_done-started).count(),sum/passes,maximum);
        }
        for(const auto target:{a,b}) {
            renderer.Flip(Gpu::FlipInfo{.address=target,.width=width,.height=height,.pitch=width});renderer.WaitIdle();
            auto frame=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(frame);
            assert(frame.width==width && frame.height==height);
            auto metal=(__bridge id<MTLDevice>)device.Handle();
            auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
            auto readback=[metal newBufferWithLength:size_t(width)*height*4 options:MTLResourceStorageModeShared];
            auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
            [blit copyFromTexture:frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
                destinationBytesPerRow:width*4 destinationBytesPerImage:size_t(width)*height*4];
            [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
            const auto* pixels=static_cast<const unsigned char*>(readback.contents);
            for(unsigned i=0;i<width*height;++i)assert(!pixels[i*4] && !pixels[i*4+1] && pixels[i*4+2]==255 && pixels[i*4+3]==255);
        }
        assert(!renderer.Execution()->Failed());
        std::printf("Command budget %s: callback progress before explicit submit=%s, check %.3f ms; both targets and CE snapshot survived\n",
            enabled ? "on" : "off",enabled ? "yes" : "no",elapsed);
    }
}
int main(int argc,char** argv) { @autoreleasepool {
    Policy();
    if(argc==1)return 0;
    if(argc!=2 || (std::strcmp(argv[1],"--gpu") && std::strcmp(argv[1],"--benchmark")))return 2;
    if(!MTLCreateSystemDefaultDevice())return 77;
    GpuTest(!std::strcmp(argv[1],"--benchmark"));return 0;
}}
