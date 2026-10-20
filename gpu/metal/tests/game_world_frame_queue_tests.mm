// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic queue policy plus actual renderer Flip/presentation-commit
// boundaries. No window, original game assets or display callback is needed.
#import <Metal/Metal.h>
#include "gpu/metal/world_frame_queue.h"
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
#include <future>
#include <thread>

extern "C" void runtime_restart() {std::abort();}
using namespace std::chrono_literals;
using Gpu::Metal::WorldFrameQueue;

static void CpuCases() {
    using Result=WorldFrameQueue::Result;
    auto execution=std::make_shared<Gpu::ExecutionState>();
    WorldFrameQueue queue;
    const auto a=queue.Track(),b=queue.Track();assert(a && b);
    assert(queue.Outstanding()==2 && queue.PeakOutstanding()==2 && !queue.Track());
    // Disabled admission never waits or discards the existing unfinished work.
    assert(queue.WaitUntilOutstandingBelow(0,execution,WorldFrameQueue::Clock::now())==Result::Ready);
    assert(queue.Outstanding()==2);
    assert(queue.WaitUntilOutstandingBelow(2,execution,WorldFrameQueue::Clock::now()+15ms)==Result::TimedOut);
    assert(!execution->Failed() && queue.Outstanding()==2 && queue.WaitCount()==1);
    // Out-of-order callbacks are permitted: one completion is sufficient;
    // this policy is not a full-GPU drain.
    WorldFrameQueue::Complete(b,execution);
    assert(queue.WaitUntilOutstandingBelow(2,execution,WorldFrameQueue::Clock::now())==Result::Ready);
    assert(queue.Outstanding()==1 && !a->load());
    const auto c=queue.Track();assert(c && queue.Outstanding()==2);
    WorldFrameQueue::Complete(a,execution);WorldFrameQueue::Complete(c,execution);
    assert(queue.Outstanding()==0 && queue.PeakOutstanding()==2);
    assert(queue.WaitUntilOutstandingBelow(3,execution,WorldFrameQueue::Clock::now())==Result::Failed);

    // Completion racing the initial epoch/predicate read must not lose a wake.
    for(unsigned k=0;k<24;++k) {
        WorldFrameQueue racing;const auto first=racing.Track(),second=racing.Track();
        auto wait=std::async(std::launch::async,[&] {
            return racing.WaitUntilOutstandingBelow(2,execution,WorldFrameQueue::Clock::now()+1s);
        });
        WorldFrameQueue::Complete(first,execution);
        assert(wait.wait_for(1s)==std::future_status::ready && wait.get()==Result::Ready);
        assert(!second->load());WorldFrameQueue::Complete(second,execution);
    }
    auto failed=std::make_shared<Gpu::ExecutionState>();WorldFrameQueue stopped;
    const auto s0=stopped.Track(),s1=stopped.Track();assert(s0 && s1);
    auto waiter=std::async(std::launch::async,[&] {
        return stopped.WaitUntilOutstandingBelow(2,failed,WorldFrameQueue::Clock::now()+2s);
    });
    failed->Fail("fixture execution stopped");
    assert(waiter.wait_for(1s)==std::future_status::ready && waiter.get()==Result::Failed);
    assert(stopped.Outstanding()==2 && failed->Error()=="fixture execution stopped");
    WorldFrameQueue::Complete(s0,failed);WorldFrameQueue::Complete(s1,failed);

    // A completion retains neither its producer queue nor renderer owner.
    WorldFrameQueue::Ticket detached;
    {WorldFrameQueue temporary;detached=temporary.Track();}
    auto command_failure=std::make_shared<Gpu::ExecutionState>();
    WorldFrameQueue::Complete(detached,command_failure,false);
    assert(detached->load() && command_failure->Failed());
    puts("World frame policy: fixed bound, disabled mode, partial completion, timeout, failure and wakeup races passed");
}

static void Drain(Gpu::Metal::MetalRenderer& renderer) {
    std::string error;
    if(!renderer.WaitIdleUntil(std::chrono::steady_clock::now()+10s,error)) {
        std::fprintf(stderr,"world queue GPU drain: %s\n",error.c_str());std::abort();
    }
}

static void GpuCase(unsigned limit) {
    setenv("BB_METAL_WORLD_FRAME_LIMIT",limit ? "2" : "0",1);
    assert(WorldFrameQueue::ConfiguredLimit()==limit);
    std::uint64_t physical=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical));
    std::uintptr_t address=0;
    assert(!runtime_gpu_memory_map(&address,131072,3,0,physical,16384));
    auto* bytes=reinterpret_cast<unsigned char*>(address);
    const std::uint32_t descriptor[]{std::uint32_t(address+256),
        std::uint32_t((address+256)>>32)|(16u<<16),3,0xfacu|(7u<<12)|(14u<<15)};
    const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
    std::memcpy(bytes,descriptor,sizeof(descriptor));std::memcpy(bytes+256,vertices,sizeof(vertices));
    auto* shader=static_cast<std::uint32_t*>(bbmetal_guest_allocate(1024));assert(shader);
    const std::uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    const std::uint32_t ps[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    std::memcpy(shader,vs,sizeof(vs));std::memcpy(shader+64,ps,sizeof(ps));
    using namespace Gpu::Liverpool;
    RegFile registers;
    registers[Reg::SPI_SHADER_PGM_LO_VS]=std::uint32_t(std::uintptr_t(shader)>>8);
    registers[Reg::SPI_SHADER_PGM_LO_PS]=std::uint32_t(std::uintptr_t(shader+64)>>8);
    registers[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    registers[Reg::SPI_SHADER_USER_DATA_VS_0]=std::uint32_t(address);
    registers[Reg::SPI_SHADER_USER_DATA_VS_0+1]=std::uint32_t(address>>32);
    registers[Reg::VGT_PRIMITIVE_TYPE]=4;
    registers[Reg::CB_COLOR0_BASE]=std::uint32_t((address+16384)>>8);
    registers[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    registers[Reg::CB_COLOR_CONTROL]=1u<<4;
    registers[Reg::CB_TARGET_MASK]=registers[Reg::CB_SHADER_MASK]=15;
    registers[Reg::SPI_SHADER_COL_FORMAT]=9;registers[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{4,4,-4,4,1,0};
    for(unsigned k=0;k<6;++k)registers[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<std::uint32_t>(viewport[k]);
    for(auto reg:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,Reg::PA_SC_GENERIC_SCISSOR_BR})
        registers[reg]=8u|(8u<<16);

    Gpu::Metal::Device device;Gpu::Metal::MetalRenderer renderer(device);
    const auto metal=(__bridge id<MTLDevice>)device.Handle();
    const auto command_queue=(__bridge id<MTLCommandQueue>)device.Queue();
    const Gpu::FlipInfo flip{.address=address+16384,.width=8,.height=8,.pitch=8};
    renderer.Draw(registers,Gpu::DrawCall{.count=3});renderer.Flip(flip);Drain(renderer);

    // Two separate gates distinguish waiting for the first world from draining
    // all queued GPU work. The second world must still be held when its Flip
    // returns after the first world's completion.
    auto first_gate=[metal newSharedEvent],second_gate=[metal newSharedEvent];assert(first_gate && second_gate);
    auto blocker=[command_queue commandBuffer];[blocker encodeWaitForEvent:first_gate value:1];[blocker commit];
    std::atomic<bool> rescued=false;
    std::jthread watchdog([first_gate,second_gate,&rescued](std::stop_token stop) {
        for(unsigned k=0;k<300 && !stop.stop_requested();++k)std::this_thread::sleep_for(10ms);
        if(!stop.stop_requested()) {rescued=true;first_gate.signaledValue=1;second_gate.signaledValue=1;}
    });
    auto readback=[metal newBufferWithLength:512 options:MTLResourceStorageModeShared];assert(readback);
    std::atomic<unsigned> acknowledged=0;
    std::promise<void> second_ack;auto acknowledged_twice=second_ack.get_future();
    renderer.SetFrameReadyCallback([&](void* frame) {
        // A real ordered GPU read stands in for presentation. Commit and ACK
        // return while the GPU is blocked; this callback must not await a slot.
        const auto index=acknowledged.load();assert(index<2 && frame);
        auto command=[command_queue commandBuffer];auto copy=[command blitCommandEncoder];
        [copy copyFromTexture:(__bridge id<MTLTexture>)frame sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:index*256
            destinationBytesPerRow:32 destinationBytesPerImage:256];
        [copy endEncoding];[command commit];
        if(acknowledged.fetch_add(1,std::memory_order_release)+1==2)second_ack.set_value();
    });
    std::memset(bytes+4096,0,8);
    auto first=std::async(std::launch::async,[&] {
        @autoreleasepool {assert(renderer.DmaData(0,address+4096,2,0x11223344,4));renderer.Flip(flip);}
    });
    const bool first_ready=first.wait_for(1s)==std::future_status::ready;
    if(!first_ready) {first_gate.signaledValue=1;second_gate.signaledValue=1;first.wait();}
    assert(first_ready && acknowledged.load()==1);first.get();
    assert(*reinterpret_cast<std::uint32_t*>(bytes+4096)==0);

    blocker=[command_queue commandBuffer];[blocker encodeWaitForEvent:second_gate value:1];[blocker commit];
    auto second=std::async(std::launch::async,[&] {
        @autoreleasepool {assert(renderer.DmaData(0,address+4100,2,0x55667788,4));renderer.Flip(flip);}
    });
    const bool callback_ready=acknowledged_twice.wait_for(1s)==std::future_status::ready;
    if(!callback_ready) {first_gate.signaledValue=1;second_gate.signaledValue=1;second.wait();}
    assert(callback_ready && acknowledged.load()==2);
    if(limit) {
        const bool producer_blocked=second.wait_for(30ms)==std::future_status::timeout;
        assert(producer_blocked);
        // Releasing only world1 must release the producer, leaving world2
        // behind gate2. Waiting for all GPU work would deadlock this check.
        first_gate.signaledValue=1;
        const bool partial_ready=second.wait_for(1s)==std::future_status::ready;
        if(!partial_ready)second_gate.signaledValue=1;
        assert(partial_ready);
        assert(*reinterpret_cast<std::uint32_t*>(bytes+4096)==0x11223344);
        assert(*reinterpret_cast<std::uint32_t*>(bytes+4100)==0);
    } else {
        // Default behavior admits both frames, even when neither has executed.
        const bool default_ready=second.wait_for(1s)==std::future_status::ready;
        if(!default_ready) {first_gate.signaledValue=1;second_gate.signaledValue=1;}
        assert(default_ready);
        assert(*reinterpret_cast<std::uint32_t*>(bytes+4096)==0);
        assert(*reinterpret_cast<std::uint32_t*>(bytes+4100)==0);
    }
    first_gate.signaledValue=1;second_gate.signaledValue=1;second.get();
    watchdog.request_stop();watchdog.join();Drain(renderer);
    assert(!rescued && !renderer.Execution()->Failed());
    assert(*reinterpret_cast<std::uint32_t*>(bytes+4096)==0x11223344);
    assert(*reinterpret_cast<std::uint32_t*>(bytes+4100)==0x55667788);
    // Drain includes both world commands; ensure both ordered presentation
    // reads have completed before examining their pixels.
    auto completed=[command_queue commandBuffer];[completed commit];[completed waitUntilCompleted];assert(!completed.error);
    const auto* pixels=static_cast<const unsigned char*>(readback.contents);
    for(unsigned frame=0;frame<2;++frame)for(unsigned pixel=0;pixel<64;++pixel) {
        const auto* p=pixels+frame*256+pixel*4;assert(p[0]==0 && p[1]==0 && p[2]==255 && p[3]==255);
    }
    renderer.SetFrameReadyCallback({});
    std::printf("World frame GPU: limit%u, presentation ACK nonblocking, producer %s, partial completion and ordered pixels passed\n",
        limit,limit ? "bounded after second Flip" : "unchanged");
}

int main(int argc,char** argv) {
    CpuCases();
    if(argc<2 || std::strcmp(argv[1],"--gpu"))return 0;
    @autoreleasepool {
        if(!MTLCreateSystemDefaultDevice())return 77;
        GpuCase(0);GpuCase(2);
    }
}
