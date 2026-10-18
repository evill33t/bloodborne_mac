// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/bbgpu.h"
#include "src/runtime_gpu_memory.h"
#include "hle/hle.h"
#include "hle/kernel/equeue.h"
#include "hle/video_out/video_out.h"
#include "gpu/liverpool/command_processor.h"
#include "gpu/metal/frontend_snapshot.h"
#include "gpu/shader/program.h"
#include "gcn_fixtures.h"
#include <array>
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <future>

extern "C" void runtime_restart() { std::abort(); }
template<class Fn> Fn Import(const char* name) {
    auto address=bbgpu_resolve((Hle::Nid(name)+"#test#test").c_str());
    assert(address);
    return reinterpret_cast<Fn>(address);
}
int main() {
    bbgpu_register_kernel();
    assert(Hle::Nid("sceVideoOutGetFlipStatus")=="SbU3dwp80lQ");
    auto create=Import<int (*)(uint32_t*,const char*)>("sceKernelCreateEqueue");
    auto destroy=Import<int (*)(uint32_t)>("sceKernelDeleteEqueue");
    auto add=Import<int (*)(uint32_t,uint64_t)>("sceKernelAddUserEvent");
    auto trigger=Import<int (*)(uint32_t,uint64_t,void*)>("sceKernelTriggerUserEvent");
    auto wait=Import<int (*)(uint32_t,Hle::Kernel::GuestKevent*,int,int*,const uint32_t*)>("sceKernelWaitEqueue");
    struct { uint32_t handle=0, canary=0xcafebabe; } queue;
    assert(create(&queue.handle,"metal ABI test")==0 && queue.handle);
    assert(queue.canary==0xcafebabe); // a host pointer must never overwrite guest storage
    assert(add(queue.handle,0x100000001ull)==0);
    Hle::Kernel::GuestKevent event{};
    uint32_t timeout=0; int count=-1;
    assert(wait(queue.handle,&event,1,&count,&timeout)==int32_t(0x8002003c) && count==0);
    assert(trigger(queue.handle,0x100000001ull,reinterpret_cast<void*>(0x1234))==0);
    assert(wait(queue.handle,&event,1,&count,&timeout)==0 && count==1);
    assert(event.ident==0x100000001ull && event.data==0x1234 && event.filter==-11);
    assert(destroy(queue.handle)==0);
    assert(wait(queue.handle,&event,1,&count,&timeout)==int32_t(0x80020009));

    // Reject kernels which change the value or do not expose both descriptors.
    // Incorrect copy recognition would silently replace real shader work.
    uint32_t copy[]{0xe0002000,0x80010100,0xbf8c0f70,0xe0102000,0x80020100,0xbf810000};
    Gpu::Shader::BuildInput input{.stage=Gpu::Shader::Stage::Compute,.user_data=nullptr,
        .num_user_sgprs=12,.code=copy};
    auto program=Gpu::Shader::BuildProgram(input);
    assert(program && program->copy_source_ud==4 && program->copy_destination_ud==8);
    copy[4]=0x80020200; // store v2 instead of the loaded v1
    program=Gpu::Shader::BuildProgram(input);
    assert(program && program->copy_source_ud==-1);
    copy[4]=0x80020100; input.num_user_sgprs=8;
    program=Gpu::Shader::BuildProgram(input);
    assert(program && program->copy_source_ud==-1);

    for (bool twice : {false,true}) {
        auto fill=TestGcn::UniformFill(twice);
        input.code=fill.data(); input.num_user_sgprs=12; input.compute_rsrc2=1u<<7;
        program=Gpu::Shader::BuildProgram(input);
        assert(program && program->fill_destination_ud==4 && program->fill_value_ud==8);
        assert(program->fill_elements_per_group==(twice ? 128 : 64));
        // A changed channel value must never be classified as a uniform fill.
        fill[twice ? 9 : 3]^=1u;
        program=Gpu::Shader::BuildProgram(input);
        assert(program && program->fill_destination_ud==-1);
        fill=TestGcn::UniformFill(twice); input.code=fill.data(); input.compute_rsrc2=0;
        program=Gpu::Shader::BuildProgram(input);
        assert(program && program->fill_destination_ud==-1);
    }

    using namespace Gpu::Liverpool;
    setenv("BB_METAL_CP_VALIDATE","1",1);
    CommandProcessor cp;
    auto execute=[&](const uint32_t* words,size_t count) {
        cp.SubmitGraphics({{{words,count},{}}}); cp.WaitIdle();
    };
    std::array<uint32_t,258> words{};
    words.front()=0x12345678; words.back()=0x87654321;
    auto init=Import<uint32_t (*)(uint32_t*,uint32_t)>("sceGnmDrawInitDefaultHardwareState200");
    assert(init(words.data()+1,255)==0);
    assert(init(words.data()+1,256)==256);
    assert(words.front()==0x12345678 && words.back()==0x87654321);
    execute(words.data()+1,256);
    assert(cp.Regs()[Reg::PA_CL_VTE_CNTL]==0x43f);

    const uint32_t ps[]{0x123456,0,0x100,0x200,0x300,0x400,0x500,0x600,0x700,0x800,0x900,0xa00};
    auto update=Import<int (*)(uint32_t*,uint32_t,const uint32_t*)>("sceGnmUpdatePsShader");
    assert(update(words.data(),40,ps)==0);
    execute(words.data(),40);
    assert(cp.Regs()[Reg::SPI_SHADER_PGM_LO_PS]==ps[0]);
    assert(cp.Regs()[0xa1c4]==ps[4] && cp.Regs()[0xa08f]==ps[11]);
    assert(update(words.data(),40,nullptr)==0);
    execute(words.data(),40);
    assert(cp.Regs()[Reg::SPI_SHADER_PGM_LO_PS]==0 && cp.Regs()[0xa203]==0);
    assert(cp.GetStats().validated_register_packets>0 && cp.GetStats().register_mismatches==0);
    const auto checked=cp.GetStats().validated_register_packets.load();
    // A flagged register offset still addresses only its low 16 bits.
    const uint32_t flagged[]{0xc0016900,0x10000110,0x1357};
    execute(flagged,3); assert(cp.Regs()[0xa110]==0x1357);
    assert(cp.GetStats().validated_register_packets==checked+1 && cp.GetStats().register_mismatches==0);
    // Both a physically truncated packet and a complete but short DRAW_INDEX_2
    // are refused before any payload/diagnostic read beyond their buffers.
    const uint32_t truncated[]{0xc0042700}; execute(truncated,1);
    const uint32_t short_draw[]{0xc0002700,0}; execute(short_draw,2);
    assert(cp.GetStats().malformed_packets==2);
    execute(words.data(),40); assert(cp.GetStats().register_mismatches==0);

    // A checkpoint freezes admission as well as draining work. Reconstruct
    // the frontend on fresh native ring threads, then release queued callers.
    CommandProcessor::CheckpointState state;std::string checkpoint_error;
    // Gnm virtual queue IDs increase across unmap/map cycles; they are not
    // restricted to the number of physical hardware queues.
    cp.SubmitCompute(1024,{});cp.WaitIdle();
    assert(cp.PauseForCheckpoint(state,std::chrono::steady_clock::now()+std::chrono::seconds(1),checkpoint_error));
    assert(state.registers[Reg::PA_CL_VTE_CNTL]==0x43f);
    std::vector<std::uint8_t> encoded;assert(Gpu::Metal::FrontendSnapshot::Encode(state,encoded,checkpoint_error));
    CommandProcessor::CheckpointState decoded;
    assert(Gpu::Metal::FrontendSnapshot::Decode(encoded,decoded,checkpoint_error) && decoded==state);
    auto bad_encoding=encoded;bad_encoding.pop_back();
    assert(!Gpu::Metal::FrontendSnapshot::Decode(bad_encoding,decoded,checkpoint_error) && decoded==state);
    bad_encoding=encoded;bad_encoding[48+4*(RegFile::kSize+0xc000/4)+4]=1;
    assert(!Gpu::Metal::FrontendSnapshot::Decode(bad_encoding,decoded,checkpoint_error) && decoded==state);
    auto queued=std::async(std::launch::async,[&]{cp.SubmitGraphics({});});
    assert(queued.wait_for(std::chrono::milliseconds(5))==std::future_status::timeout);
    cp.ResumeAfterCheckpoint();queued.get();cp.WaitIdle();
    CommandProcessor restored;
    auto invalid_state=state;invalid_state.registers.pop_back();
    assert(!restored.RestoreCheckpoint(invalid_state,checkpoint_error));
    assert(restored.RestoreCheckpoint(state,checkpoint_error));
    assert(restored.Regs()[Reg::PA_CL_VTE_CNTL]==0x43f);
    auto restored_queued=std::async(std::launch::async,[&]{restored.SubmitGraphics({});});
    assert(restored_queued.wait_for(std::chrono::milliseconds(5))==std::future_status::timeout);
    restored.ResumeAfterCheckpoint();restored_queued.get();restored.WaitIdle();
    // A CPU/GPU dependency may prevent an idle boundary. Timeout must reopen
    // admission and leave the previous output intact so gameplay can continue.
    CommandProcessor blocked;
    std::promise<void> entered,release;
    auto released=release.get_future().share();
    blocked.SubmitGraphics({},[&]{entered.set_value();released.wait();});
    assert(entered.get_future().wait_for(std::chrono::seconds(1))==std::future_status::ready);
    CommandProcessor::CheckpointState unchanged;unchanged.version=99;
    assert(!blocked.PauseForCheckpoint(unchanged,std::chrono::steady_clock::now()+std::chrono::milliseconds(10),checkpoint_error));
    assert(unchanged.version==99);
    auto recovered=std::async(std::launch::async,[&]{blocked.SubmitGraphics({});});
    assert(recovered.wait_for(std::chrono::seconds(1))==std::future_status::ready);
    recovered.get();release.set_value();blocked.WaitIdle();

    auto& video=Hle::VideoOut::Driver::Instance();
    auto get=Import<int (*)(int,Hle::VideoOut::FlipStatus*)>("sceVideoOutGetFlipStatus");
    auto close=Import<int (*)(int)>("sceVideoOutClose");
    const int port=video.Open(0,0); assert(port>0);
    Hle::VideoOut::FlipStatus status;
    assert(get(port,&status)==0 && status.count==0 && status.current_buffer==-1);
    assert(video.GetBufferLabelAddress(port)<(1ull<<40));
    video.MarkFlipPending(port,2);
    assert(get(port,&status)==0 && status.flip_pending_num==1);
    video.OnFlipDone(port,2,12345);
    assert(get(port,&status)==0 && status.count==1 && status.flip_arg==12345);
    assert(status.current_buffer==2 && status.flip_pending_num==0 && status.tsc);
    // Limits pace game flips, independently of the guest's original 30 Hz
    // flip setting. Changing to uncapped releases an in-progress wait.
    using Clock=std::chrono::steady_clock;
    for (int limit : {30,60,90,120}) {
        video.SetFrameLimit(limit);
        const auto start=Clock::now();
        for (int k=0;k<7;++k) video.WaitFlipVblank(port);
        const double elapsed=std::chrono::duration<double>(Clock::now()-start).count();
        assert(elapsed>=5.5/limit && elapsed<12.0/limit);
    }
    video.SetFrameLimit(1); video.WaitFlipVblank(port);
    auto pending=std::async(std::launch::async,[&] { video.WaitFlipVblank(port); });
    assert(pending.wait_for(std::chrono::milliseconds(25))==std::future_status::timeout);
    video.SetFrameLimit(0);
    assert(pending.wait_for(std::chrono::milliseconds(100))==std::future_status::ready);
    pending.get();
    video.SetFrameLimit(-1);
    assert(close(port)==0 && get(port,&status)!=0);
    std::puts("Metal integration: guest event ABI, PM4 shader state and flip lifecycle passed");
}
