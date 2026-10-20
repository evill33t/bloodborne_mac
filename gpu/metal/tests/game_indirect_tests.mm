// SPDX-License-Identifier: GPL-2.0-or-later
// Synthetic PM4 and production Metal paths, without original game assets.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/native_indirect.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/command_processor.h"
#include "gpu/liverpool/tiling.h"
#include "gpu/shader/gcn.h"
#include "gpu/shader/program.h"
#include "src/runtime_gpu_memory.h"
extern "C" {
#include "src/runtime.h"
}
#include "gcn_fixtures.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <thread>

extern "C" void runtime_restart() {std::abort();}
using namespace Gpu;
using namespace Gpu::Liverpool;

struct ProbeRenderer final:Renderer {
    bool accept=true;
    unsigned waits=0,native_draws=0,native_dispatches=0;
    GuestAddr address=0;
    std::vector<DrawCall> draws;
    RegFile observed;
    std::array<std::uint8_t,65536> gds{};
    void Draw(const RegFile& r,const DrawCall& d) override {draws.push_back(d);observed=r;}
    void Dispatch(const RegFile& r,u32,u32,u32) override {observed=r;}
    bool delay_signals=false;
    std::vector<std::function<void()>> signals;
    bool DrawIndirect(const RegFile&,const DrawCall&,GuestAddr a,const std::function<bool(GuestAddr,std::size_t)>& ready) override {
        if(!accept || (ready && !ready(a,16)))return false;++native_draws;address=a;return true;
    }
    bool DispatchIndirect(const RegFile&,GuestAddr a,const std::function<bool(GuestAddr,std::size_t)>& ready) override {
        if(!accept || (ready && !ready(a,12)))return false;++native_dispatches;address=a;return true;
    }
    void OnSubmit() override {}
    void WaitIdle() override {++waits;}
    bool WaitIdleUntil(std::chrono::steady_clock::time_point,std::string&) override {
        ++waits;for(auto& signal:signals)signal();signals.clear();return true;
    }
    void Signal(std::function<void()> done) override {if(delay_signals)signals.push_back(std::move(done));else done();}
    void NoteConstDump(u64,u32) override {}
    void Flip(const FlipInfo&) override {}
    std::uint8_t* Gds() override {return gds.data();}
};

static void Packet(std::vector<u32>& out,u32 op,std::initializer_list<u32> body) {
    out.push_back(0xc0000000u|u32(body.size()-1)<<16|op<<8);
    out.insert(out.end(),body);
}
static std::vector<u32> Base(u64 address,u32 selector=1) {
    std::vector<u32> out;Packet(out,Op::SetBase,{selector,u32(address),u32(address>>32)});return out;
}
static void Execute(ProbeRenderer& renderer,std::vector<u32>& words) {
    CommandProcessor cp;cp.SetRenderer(&renderer);
    cp.SubmitGraphics({{{words.data(),words.size()},{}}});cp.WaitIdle();
    assert(!cp.Execution()->Failed());
}
static void Cpu(u64 address,u64 alias) {
    using namespace Gpu::Metal::NativeIndirect;
    assert(Valid({address,12}));assert(!Valid({0,12}));assert(!Valid({address+1,12}));
    assert(!Valid({UINT64_MAX-3,12}));assert(!Valid({address,0}));
    assert(Overlap({address,12},{address+11,1}));
    assert(!Overlap({address,12},{address+12,1}));
    assert(Overlap({address,12},{UINT64_MAX-1,12}));
    assert(!Overlap({address,12},{address,0}));
    assert(DrawDestinationsSupported(0,0));
    assert(!DrawDestinationsSupported(0x4c,0));assert(!DrawDestinationsSupported(0,0x4c));
    // An accepting backend must not force a CPU read or GPU drain.
    for(bool dispatch:{false,true}) {
        ProbeRenderer renderer;auto words=Base(address);
        if(dispatch) Packet(words,Op::DispatchIndirect,{0,1});
        else Packet(words,Op::DrawIndirect,{0,0,0,2});
        Execute(renderer,words);
        assert(renderer.waits==0 && renderer.address==address);
        assert(renderer.native_dispatches==unsigned(dispatch));
        assert(renderer.native_draws==unsigned(!dispatch));
    }
    auto* args=reinterpret_cast<u32*>(address);
    // Nonzero SH destinations retain signed base-vertex/first-instance bits
    // and the drained path. Nonzero indirect instances override NUM_INSTANCES.
    for(bool indexed:{false,true}) {
        const std::array<u32,5> values{3,2,4,0xfffffffbu,7};
        std::memcpy(args,values.data(),sizeof(values));
        ProbeRenderer renderer;auto words=Base(address);
        Packet(words,Op::NumInstances,{9});
        Packet(words,indexed?Op::DrawIndexIndirect:Op::DrawIndirect,{0,0x51,0x52,indexed?0u:2u});
        Execute(renderer,words);
        assert(renderer.waits==1 && renderer.native_draws==0 && renderer.draws.size()==1);
        assert(renderer.draws[0].instances==2 && renderer.draws[0].indexed==indexed);
        assert(renderer.observed[0x2c51]==values[indexed?3:2]);
        assert(renderer.observed[0x2c52]==values[indexed?4:3]);
    }
    // Zero instances are a no-op even with NUM_INSTANCES=9. SH updates still
    // precede that decision; a later direct packet observes them.
    args[0]=3;args[1]=0;args[2]=4;args[3]=7;
    ProbeRenderer zero;auto words=Base(address);
    Packet(words,Op::NumInstances,{9});Packet(words,Op::DrawIndirect,{0,0x51,0x52,2});
    Packet(words,Op::DrawIndexAuto,{3,2});Execute(zero,words);
    assert(zero.draws.size()==1 && zero.draws[0].instances==9);
    assert(zero.observed[0x2c51]==4 && zero.observed[0x2c52]==7);
    // Backend rejection and unknown SET_BASE selectors retain the old drain.
    for(u32 selector:{1u,2u}) {
        args[0]=args[1]=args[2]=1;ProbeRenderer rejected;rejected.accept=selector!=1;
        auto q=Base(address,selector);Packet(q,Op::DispatchIndirect,{0,1});Execute(rejected,q);
        assert(rejected.waits==1 && rejected.native_dispatches==0);
    }
    // Signal-based fence stores are CPU callbacks, not GPU writes. Their
    // argument ranges (including physical aliases and unknown clock values)
    // must complete through the drained path before native admission.
    for(unsigned selector:{1u,3u}) {
        args[0]=args[1]=args[2]=1;ProbeRenderer pending;pending.delay_signals=true;
        auto q=Base(address);const u64 destination=selector==1 ? alias : address;
        Packet(q,Op::EventWriteEop,{0x14,u32(destination),u32(destination>>32)|(selector<<29),1,0});
        Packet(q,Op::DispatchIndirect,{0,1});Execute(pending,q);
        assert(pending.waits==1 && pending.native_dispatches==0 && pending.signals.empty());
    }
    puts("indirect CPU: range/packet admission, drained register state and zero-instance cases passed");
}

static void Drain(Gpu::Metal::MetalRenderer& renderer) {
    std::string error;
    if(!renderer.WaitIdleUntil(std::chrono::steady_clock::now()+std::chrono::seconds(15),error)) {
        fprintf(stderr,"indirect drain: %s\n",error.c_str());std::abort();
    }
}
static std::array<unsigned char,4> Pixel(Gpu::Metal::Device& device,Gpu::Metal::MetalRenderer& renderer,u64 address) {
    renderer.Flip(FlipInfo{.address=address,.width=8,.height=8,.pitch=8});Drain(renderer);
    auto texture=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(texture);
    auto metal=(__bridge id<MTLDevice>)device.Handle();
    auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    auto readback=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
    auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(8,8,1) toBuffer:readback destinationOffset:0
        destinationBytesPerRow:32 destinationBytesPerImage:256];
    [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
    std::array<unsigned char,4> pixel;
    std::memcpy(pixel.data(),static_cast<const unsigned char*>(readback.contents)+4*(4*8+4),4);return pixel;
}
static void GpuCases(u64 address,u64 alias) {
    setenv("BB_METAL_NATIVE_INDIRECT","1",1);
    using namespace Gpu::Shader::Gcn;
    auto* base=reinterpret_cast<unsigned char*>(address);
    // Mapped code allows real queued DMA code/fetch overwrite regressions.
    auto* shader=reinterpret_cast<u32*>(base+98304);
    const auto vmov=[](u32 dst,u32 src){return 0x7e000000u|(dst<<17)|(1u<<9)|src;};
    // Group X -> output[group] = 23. This is a generic raw store, not the
    // canonical typed fill/copy substitution recognized by the renderer.
    const u32 cs[]{vmov(0,4),vmov(1,Operand::Zero+23),
        0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80000100,0xbf810000};
    std::memcpy(shader,cs,sizeof(cs));
    RegFile compute;compute[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader)>>8);
    compute[Reg::COMPUTE_PGM_RSRC2]=(4u<<1)|(1u<<7);
    compute[Reg::COMPUTE_NUM_THREAD_X]=compute[Reg::COMPUTE_NUM_THREAD_Y]=compute[Reg::COMPUTE_NUM_THREAD_Z]=1;
    auto output=[&](u64 at) {
        compute[Reg::COMPUTE_USER_DATA_0]=u32(at);
        compute[Reg::COMPUTE_USER_DATA_0+1]=u32(at>>32)|(4u<<16);
        compute[Reg::COMPUTE_USER_DATA_0+2]=64;
        compute[Reg::COMPUTE_USER_DATA_0+3]=0x24004;
    };
    Gpu::Metal::Device device;Gpu::Metal::MetalRenderer renderer(device);
    assert(runtime_gpu_memory_set_mutation_hooks(&renderer,
        [](void* p)->int{return static_cast<Gpu::Metal::MetalRenderer*>(p)->BeginGuestMemoryMutation();},
        [](void* p){static_cast<Gpu::Metal::MetalRenderer*>(p)->EndGuestMemoryMutation();}));
    struct RemoveHooks {~RemoveHooks(){assert(runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr));}} remove_hooks;
    auto groups=[&](u32 x) {
        assert(renderer.DmaData(0,address,2,x,4));
        assert(renderer.DmaData(0,address+4,2,1,8));
    };
    std::memset(base+4096,0,3072);
    assert(!renderer.DispatchIndirect(compute,address)); // uncached code must use the drained decoder
    output(address+60000);renderer.Dispatch(compute,1,1,1);Drain(renderer);
    // GPU-produced arguments, followed by a second GPU write to the same VA
    // before CPU completion. Each dispatch must retain its ordered version.
    groups(7);output(address+4096);assert(renderer.DispatchIndirect(compute,address));
    groups(3);output(address+4352);assert(renderer.DispatchIndirect(compute,address));
    groups(0);output(address+4608);assert(renderer.DispatchIndirect(compute,address));
    Drain(renderer);
    for(unsigned row=0;row<3;++row) for(unsigned k=0;k<64;++k) {
        const auto* out=reinterpret_cast<const u32*>(base+4096+row*256);
        assert(out[k]==(k<(row==0?7u:row==1?3u:0u)?23u:0u));
    }
    // CPU writes are serialized with admission, and wait only when their
    // range overlaps an earlier GPU indirect read. Zero-width adjacency does
    // not conflict. The first output must not see the later CPU value.
    groups(2);output(address+4864);assert(renderer.DispatchIndirect(compute,address));
    const u32 four[]{4,1,1};renderer.CpuWrite(address,four,sizeof(four),false);
    assert(renderer.IndirectCpuWriteWaitCount()>=1);
    output(address+5120);assert(renderer.DispatchIndirect(compute,address));Drain(renderer);
    for(unsigned row=0;row<2;++row) for(unsigned k=0;k<64;++k) {
        const auto* out=reinterpret_cast<const u32*>(base+4864+row*256);
        assert(out[k]==(k<(row?4u:2u)?23u:0u));
    }
    // Different guest virtual mappings may reference exactly the same direct
    // physical allocation. Protect the bytes, not just their virtual address.
    groups(6);output(address+5376);assert(renderer.DispatchIndirect(compute,address));
    const u32 one[]{1,1,1};const auto before_alias=renderer.IndirectCpuWriteWaitCount();
    renderer.CpuWrite(alias,one,sizeof(one),false);
    assert(renderer.IndirectCpuWriteWaitCount()==before_alias+1);
    output(address+5632);assert(renderer.DispatchIndirect(compute,address));Drain(renderer);
    for(unsigned row=0;row<2;++row) for(unsigned k=0;k<64;++k) {
        const auto* out=reinterpret_cast<const u32*>(base+5376+row*256);
        assert(out[k]==(k<(row?1u:6u)?23u:0u));
    }
    assert(!renderer.DispatchIndirect(compute,address+1));
    assert(!renderer.DispatchIndirect(compute,UINT64_MAX-3));
    // Block the queue briefly so unmapping is guaranteed to encounter an
    // unread argument version. The runtime's paired mutation guard must drain
    // it before changing mappings, including when the read uses an alias.
    auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    auto metal=(__bridge id<MTLDevice>)device.Handle();
    auto gate=[metal newSharedEvent];auto blocked=[queue commandBuffer];
    [blocked encodeWaitForEvent:gate value:1];[blocked commit];
    groups(5);output(address+5888);assert(renderer.DispatchIndirect(compute,alias));
    std::thread release([gate] {std::this_thread::sleep_for(std::chrono::milliseconds(30));gate.signaledValue=1;});
    auto unmap=reinterpret_cast<int(*)(void*,u64)>(runtime_memory_resolve("cQke9UuBQOk#p#J"));assert(unmap);
    assert(unmap(reinterpret_cast<void*>(alias),131072)==0);release.join();
    for(unsigned k=0;k<64;++k)assert(reinterpret_cast<const u32*>(base+5888)[k]==(k<5?23u:0u));
    // Each dimension may independently make the dispatch a no-op.
    for(unsigned dimension:{1u,2u}) {
        groups(7);assert(renderer.DmaData(0,address+4*dimension,2,0,4));
        output(address+5888+dimension*256);assert(renderer.DispatchIndirect(compute,address));
    }
    // A separate generic kernel writes unique X/Y/Z group positions, proving
    // the complete GPU-time group argument layout and all three built-ins.
    const auto v2=[](Vop op,u32 dst,u32 a,u32 b) {
        return ((u32(op)-256)<<25)|(dst<<17)|(b<<9)|a;
    };
    const u32 xyz[]{vmov(0,4),vmov(1,5),vmov(2,6),
        v2(Vop::V_LSHLREV_B32,1,Operand::Zero+3,1),
        v2(Vop::V_ADD_I32,0,Operand::Vgpr0,1),
        v2(Vop::V_LSHLREV_B32,2,Operand::Zero+5,2),
        v2(Vop::V_ADD_I32,0,Operand::Vgpr0,2),vmov(3,Operand::Zero+23),
        0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80000300,0xbf810000};
    std::memcpy(shader+320,xyz,sizeof(xyz));output(address+6656);
    auto compute_xyz=compute;compute_xyz[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+320)>>8);
    compute_xyz[Reg::COMPUTE_PGM_RSRC2]=(4u<<1)|(7u<<7);
    renderer.Dispatch(compute_xyz,1,1,1);Drain(renderer);std::memset(base+6656,0,256);
    const u32 cube[]{2,2,2};renderer.CpuWrite(address,cube,sizeof(cube),false);
    assert(renderer.DispatchIndirect(compute_xyz,address));Drain(renderer);
    for(unsigned row=0;row<2;++row) for(unsigned k=0;k<64;++k)
        assert(reinterpret_cast<const u32*>(base+6144+row*256)[k]==0);
    for(unsigned k=0;k<64;++k) {
        const bool written=(k%8)<2 && ((k/8)%4)<2 && k/32<2;
        assert(reinterpret_cast<const u32*>(base+6656)[k]==(written?23u:0u));
    }
    // Count-dependent typed aliases must retain the drained implementation.
    const auto fill=TestGcn::UniformFill(false);std::memcpy(shader+64,fill.data(),fill.size()*4);
    auto typed=compute;typed[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+64)>>8);
    typed[Reg::COMPUTE_PGM_RSRC2]=(12u<<1)|(1u<<7);
    const auto fill_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Compute,
        uintptr_t(shader+64),typed.UserData(Reg::COMPUTE_USER_DATA_0),12,0,fill.data(),
        typed[Reg::COMPUTE_PGM_RSRC2]});
    assert(fill_program && fill_program->fill_destination_ud==4 && fill_program->fill_value_ud==8);
    renderer.Dispatch(typed,1,1,1);Drain(renderer);
    assert(!renderer.DispatchIndirect(typed,address));

    const u32 vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    const u32 red[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    const u32 blue[]{0x7e000280,0x7e020280,0x7e0402f2,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    std::memcpy(shader+128,vs,sizeof(vs));std::memcpy(shader+192,red,sizeof(red));std::memcpy(shader+256,blue,sizeof(blue));
    const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};std::memcpy(base+8192,vertices,sizeof(vertices));
    const u32 descriptor[]{u32(address+8192),u32((address+8192)>>32)|(16u<<16),3,0xfacu|(7u<<12)|(14u<<15)};
    // This CE-style snapshot makes the first draw allocate its snapshot arena
    // while it already owns the renderer mutex: mapping guards must reenter.
    renderer.CpuWrite(address+8256,descriptor,sizeof(descriptor),true);
    RegFile draw;draw[Reg::SPI_SHADER_PGM_LO_VS]=u32(uintptr_t(shader+128)>>8);
    draw[Reg::SPI_SHADER_PGM_LO_PS]=u32(uintptr_t(shader+192)>>8);
    draw[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+8256);
    draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+8256)>>32);
    draw[Reg::VGT_PRIMITIVE_TYPE]=4;draw[Reg::CB_COLOR0_BASE]=u32((address+16384)>>8);
    draw[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    draw[Reg::CB_COLOR_CONTROL]=1u<<4;draw[Reg::CB_TARGET_MASK]=draw[Reg::CB_SHADER_MASK]=15;
    draw[Reg::SPI_SHADER_COL_FORMAT]=9;draw[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{4,4,-4,4,1,0};
    for(unsigned k=0;k<6;++k)draw[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<u32>(viewport[k]);
    draw[Reg::PA_SC_SCREEN_SCISSOR_BR]=draw[Reg::PA_SC_WINDOW_SCISSOR_BR]=8u|(8u<<16);
    draw[Reg::PA_SC_GENERIC_SCISSOR_BR]=8u|(8u<<16);
    // No native zero-count operation may initialize an unseen target.
    assert(!renderer.DrawIndirect(draw,DrawCall{},address+32));
    renderer.Draw(draw,DrawCall{.count=3});
    draw[Reg::SPI_SHADER_PGM_LO_PS]=u32(uintptr_t(shader+256)>>8);
    renderer.Draw(draw,DrawCall{.count=0});Drain(renderer); // warm a second PS without changing pixels
    assert(renderer.DmaData(0,address+32,2,3,4));assert(renderer.DmaData(0,address+36,2,1,4));
    // Deliberately nonzero unused fields: current no-SH-destination ABI keeps
    // VGT_INDX_OFFSET and instance base zero rather than applying them twice.
    const u32 unused[]{19,29};renderer.CpuWrite(address+40,unused,sizeof(unused),false);
    assert(renderer.DrawIndirect(draw,DrawCall{},address+32));
    auto pixel=Pixel(device,renderer,address+16384);assert(pixel[0]==255 && pixel[2]==0);
    draw[Reg::SPI_SHADER_PGM_LO_PS]=u32(uintptr_t(shader+192)>>8);
    assert(renderer.DmaData(0,address+32,2,0,4));assert(renderer.DrawIndirect(draw,DrawCall{},address+32));
    pixel=Pixel(device,renderer,address+16384);assert(pixel[0]==255 && pixel[2]==0);
    assert(renderer.DmaData(0,address+32,2,3,4));assert(renderer.DmaData(0,address+36,2,0,4));
    assert(renderer.DrawIndirect(draw,DrawCall{},address+32));
    pixel=Pixel(device,renderer,address+16384);assert(pixel[0]==255 && pixel[2]==0);
    auto special=draw;special[Reg::DB_RENDER_CONTROL]=1;assert(!renderer.DrawIndirect(special,DrawCall{},address+32));
    special=draw;special[Reg::VGT_PRIMITIVE_TYPE]=0x11;assert(!renderer.DrawIndirect(special,DrawCall{},address+32));
    assert(!renderer.DrawIndirect(draw,DrawCall{.indexed=true},address+32));
    assert(renderer.NativeIndirectDispatchCount()==11 && renderer.NativeIndirectDrawCount()==3);

    // The consuming CS may write its own source argument allocation. The
    // dedicated GPU snapshot latches all three dimensions before it starts.
    std::vector<u32> self{vmov(0,8),vmov(1,9),vmov(2,10),
        v2(Vop::V_LSHLREV_B32,1,Operand::Zero+3,1),v2(Vop::V_ADD_I32,0,Operand::Vgpr0,1),
        v2(Vop::V_LSHLREV_B32,2,Operand::Zero+5,2),v2(Vop::V_ADD_I32,0,Operand::Vgpr0,2),
        vmov(3,Operand::Zero+23),0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80000300,
        vmov(4,Operand::Zero),v2(Vop::V_LSHLREV_B32,0,Operand::Zero+4,0)};
    for(unsigned k=0;k<3;++k) {
        // Only group 0 has a byte offset inside the 12-byte raw descriptor;
        // the other groups must not race to write its argument words.
        self.push_back(0xe0001000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18)|4*k);
        self.push_back(0x80010400);
    }
    self.push_back(0xbf810000);std::memcpy(shader+512,self.data(),self.size()*4);
    output(address+7168);auto self_compute=compute;
    self_compute[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+512)>>8);
    self_compute[Reg::COMPUTE_PGM_RSRC2]=(8u<<1)|(7u<<7);
    self_compute[Reg::COMPUTE_USER_DATA_0+4]=u32(address);
    self_compute[Reg::COMPUTE_USER_DATA_0+5]=u32(address>>32);
    self_compute[Reg::COMPUTE_USER_DATA_0+6]=12;
    self_compute[Reg::COMPUTE_USER_DATA_0+7]=0x24004;
    renderer.Dispatch(self_compute,1,1,1);Drain(renderer);std::memset(base+7168,0,256);
    renderer.CpuWrite(address,cube,sizeof(cube),false);
    assert(renderer.DispatchIndirect(self_compute,address));Drain(renderer);
    for(unsigned k=0;k<64;++k) {
        const bool written=(k%8)<2 && ((k/8)%4)<2 && k/32<2;
        assert(reinterpret_cast<const u32*>(base+7168)[k]==(written?23u:0u));
    }
    for(unsigned k=0;k<3;++k)assert(reinterpret_cast<const u32*>(base)[k]==0);

    // Uploaded texels and loaded descriptors are CPU inputs to setup. A
    // queued writer must reject native admission before those bytes are read.
    const u32 sample[]{0x7e0002f0,0x7e0202f0,0xf0000000u|(32u<<18)|(15u<<8),
        (2u<<21)|(2u<<8),0xf800080f,0x05040302,0xbf810000};
    std::memcpy(shader+384,sample,sizeof(sample));
    const u32 texture[]{u32((address+32768)>>8),u32((address+32768)>>40)|(10u<<20),
        7u|(7u<<14),0x90800fac,15u<<13,0,0,0};
    auto sampled=draw;sampled[Reg::SPI_SHADER_PGM_LO_PS]=u32(uintptr_t(shader+384)>>8);
    sampled[Reg::SPI_SHADER_PGM_RSRC2_PS]=12u<<1;
    for(unsigned k=0;k<12;++k)sampled[Reg::SPI_SHADER_USER_DATA_PS_0+k]=k<8 ? texture[k] : 0;
    std::fill_n(reinterpret_cast<u32*>(base+32768),128,0xff0000ffu);
    renderer.Draw(sampled,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[2]==255 && pixel[1]==0);
    const u32 triangle[]{3,1,0,0};renderer.CpuWrite(address+32,triangle,sizeof(triangle),false);
    // Re-create a second mapping of the same physical texels after the earlier
    // unmap test, so the conflict is proven by physical identity.
    std::array<BbDirectMapping,64> mappings{};
    const auto mapping_count=runtime_gpu_memory_mappings(mappings.data(),mappings.size());
    u64 alias_physical=UINT64_MAX;
    for(unsigned k=0;k<std::min<std::size_t>(mapping_count,mappings.size());++k)
        if(mappings[k].start==address)alias_physical=mappings[k].physical;
    assert(alias_physical!=UINT64_MAX);
    uintptr_t texture_alias=0;
    assert(!runtime_gpu_memory_map(&texture_alias,131072,3,0,alias_physical,16384));
    auto guarded=[&](auto&& work) {
        auto event=[metal newSharedEvent];assert(event);
        auto blocker=[queue commandBuffer];[blocker encodeWaitForEvent:event value:1];[blocker commit];
        std::atomic<bool> timeout{false};
        std::jthread watchdog([event,&timeout](std::stop_token stop) {
            for(unsigned k=0;k<300 && !stop.stop_requested();++k)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(!stop.stop_requested()){timeout=true;event.signaledValue=1;}
        });
        work();event.signaledValue=1;watchdog.request_stop();watchdog.join();Drain(renderer);assert(!timeout);
    };
    guarded([&] {
        assert(renderer.DmaData(0,texture_alias+32768,2,0xff00ff00u,512));
        assert(!renderer.DrawIndirect(sampled,DrawCall{},address+32));
    });
    renderer.Draw(sampled,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[1]==255 && pixel[2]==0);
    guarded([&] {
        assert(renderer.DmaData(0,address+64000,2,0,16));
        assert(renderer.DrawIndirect(sampled,DrawCall{},address+32)); // unrelated producer is safe
    });
    pixel=Pixel(device,renderer,address+16384);assert(pixel[1]==255 && pixel[2]==0);
    guarded([&] {
        output(address+32768+4*64+4*4);
        renderer.Dispatch(compute,1,1,1); // reflected raw V# writer to the sampled centre texel
        assert(!renderer.DrawIndirect(sampled,DrawCall{},address+32));
    });
    renderer.Draw(sampled,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[2]==23 && pixel[1]==0 && pixel[0]==0);
    guarded([&] {
        output(address+64000);renderer.Dispatch(compute,1,1,1);
        assert(renderer.DrawIndirect(sampled,DrawCall{},address+32));
    });
    pixel=Pixel(device,renderer,address+16384);assert(pixel[2]==23 && pixel[1]==0);

    // s_load_dwordx8 s[16:23],s[0:1],0, followed by image_sample t16/s12.
    const u32 loaded[]{0xc0000000u|(u32(Smrd::S_LOAD_DWORDX8)<<22)|(16u<<15)|0x100u,
        0x7e0002f0,0x7e0202f0,0xf0000000u|(32u<<18)|(15u<<8),
        (4u<<16)|(3u<<21)|(2u<<8),0xf800080f,0x05040302,0xbf810000};
    std::memcpy(shader+448,loaded,sizeof(loaded));
    auto from_table=sampled;from_table[Reg::SPI_SHADER_PGM_LO_PS]=u32(uintptr_t(shader+448)>>8);
    from_table[Reg::SPI_SHADER_PGM_RSRC2_PS]=16u<<1;
    for(unsigned k=0;k<16;++k)from_table[Reg::SPI_SHADER_USER_DATA_PS_0+k]=0;
    from_table[Reg::SPI_SHADER_USER_DATA_PS_0]=u32(address+36864);
    from_table[Reg::SPI_SHADER_USER_DATA_PS_0+1]=u32((address+36864)>>32);
    std::memcpy(base+36864,texture,sizeof(texture));
    const auto loaded_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Fragment,
        uintptr_t(shader+448),from_table.UserData(Reg::SPI_SHADER_USER_DATA_PS_0),16,0,loaded});
    assert(loaded_program && loaded_program->images.size()==1);
    assert(loaded_program->images[0].tsharp[0].kind==Gpu::Shader::SgprSource::PointerLoad);
    renderer.Draw(from_table,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[2]==23 && pixel[1]==0);
    auto blue_texture=std::array<u32,8>{};std::copy_n(texture,8,blue_texture.begin());
    blue_texture[0]=u32((address+40960)>>8);
    std::fill_n(reinterpret_cast<u32*>(base+40960),128,0xffff0000u);
    std::memcpy(base+45056,blue_texture.data(),32);
    guarded([&] {
        assert(renderer.DmaData(0,texture_alias+36864,0,address+45056,32));
        assert(!renderer.DrawIndirect(from_table,DrawCall{},address+32));
    });
    renderer.Draw(from_table,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[0]==255 && pixel[1]==0 && pixel[2]==0);
    // A valid runtime descriptor whose provenance cannot be bounded must be
    // an unknown writer ticket, even though this instance writes elsewhere.
    const u32 unknown_writer[]{0xc0000000u|(u32(Smrd::S_LOAD_DWORDX4)<<22)|(4u<<15)|0x100u,
        0x80000000u|(u32(Sop2::S_ADD_U32)<<23)|(4u<<16)|(u32(Operand::Zero)<<8)|4u,
        vmov(0,2),vmov(1,Operand::Zero+23),
        0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80010100,0xbf810000};
    std::memcpy(shader+576,unknown_writer,sizeof(unknown_writer));
    output(address+64000);
    std::memcpy(base+49152,compute.UserData(Reg::COMPUTE_USER_DATA_0),16);
    auto uncertain=compute;uncertain[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+576)>>8);
    uncertain[Reg::COMPUTE_PGM_RSRC2]=(2u<<1)|(1u<<7);
    uncertain[Reg::COMPUTE_USER_DATA_0]=u32(address+49152);
    uncertain[Reg::COMPUTE_USER_DATA_0+1]=u32((address+49152)>>32);
    const auto unknown_compute=uncertain;
    const auto uncertain_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Compute,
        uintptr_t(shader+576),uncertain.UserData(Reg::COMPUTE_USER_DATA_0),2,0,unknown_writer,
        uncertain[Reg::COMPUTE_PGM_RSRC2]});
    assert(uncertain_program && uncertain_program->buffer_writes.size()==1 &&
        uncertain_program->buffer_writes[0][0].kind==Gpu::Shader::SgprSource::Unknown);
    const u32 once[]{1,1,1};renderer.CpuWrite(address,once,sizeof(once),false);
    guarded([&] {
        renderer.Dispatch(uncertain,1,1,1);
        assert(!renderer.DrawIndirect(sampled,DrawCall{},address+32));
        output(address+64512);
        // The shader now lives in guest GPU backing too. Unknown writes
        // block its CPU code comparison even though it has no sampled inputs.
        assert(!renderer.DispatchIndirect(compute,address));
    });
    renderer.Dispatch(compute,1,1,1);Drain(renderer); // explicit drained fallback
    assert(reinterpret_cast<const u32*>(base+64000)[0]==23);
    assert(reinterpret_cast<const u32*>(base+64512)[0]==23);
    // Without a CP read-ready predicate a pointer table remains unknown.
    const u32 pointer_writer[]{unknown_writer[0],vmov(0,2),vmov(1,Operand::Zero+23),
        0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80010100,0xbf810000};
    std::memcpy(shader+640,pointer_writer,sizeof(pointer_writer));
    uncertain[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+640)>>8);
    const auto pointer_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Compute,
        uintptr_t(shader+640),uncertain.UserData(Reg::COMPUTE_USER_DATA_0),2,0,pointer_writer,
        uncertain[Reg::COMPUTE_PGM_RSRC2]});
    assert(pointer_program && pointer_program->buffer_writes.size()==1 &&
        pointer_program->buffer_writes[0][0].kind==Gpu::Shader::SgprSource::PointerLoad);
    guarded([&] {
        renderer.Dispatch(uncertain,1,1,1);
        assert(!renderer.DrawIndirect(sampled,DrawCall{},address+32));
    });
    // These direct renderer fixtures have no asynchronous CP fence stores.
    // Install the same ready contract that a live CommandProcessor supplies.
    renderer.SetCpuReadGuard([](GuestAddr,std::size_t){return true;});
    const u64 centre=address+32768+4*64+4*4;
    const u32 centre_descriptor[]{u32(centre),u32(centre>>32)|(4u<<16),1,0x24004};
    renderer.CpuWrite(address+49152,centre_descriptor,sizeof(centre_descriptor),false);
    guarded([&] {
        renderer.Dispatch(uncertain,1,1,1);
        assert(!renderer.DrawIndirect(sampled,DrawCall{},address+32)); // proved affected texel extent
        assert(renderer.DrawIndirect(from_table,DrawCall{},address+32)); // independent blue payload
    });
    pixel=Pixel(device,renderer,address+16384);assert(pixel[0]==255 && pixel[1]==0);

    // A later CP write to the table must not redirect an already queued
    // producer. Exercise the same VA, a physical alias, and zero records:
    // zero-record descriptors also need a read ticket before CPU activation.
    for(unsigned variant=0;variant<3;++variant) {
        const u64 first=address+70400,second=address+70656;
        const u32 before[]{u32(first),u32(first>>32)|(4u<<16),variant==2 ? 0u : 1u,0x24004};
        const u32 after[]{u32(second),u32(second>>32)|(4u<<16),1,0x24004};
        const u32 zero=0;
        renderer.CpuWrite(first,&zero,4,false);renderer.CpuWrite(second,&zero,4,false);
        renderer.CpuWrite(address+49152,before,sizeof(before),false);
        auto event=[metal newSharedEvent];auto blocker=[queue commandBuffer];
        [blocker encodeWaitForEvent:event value:1];[blocker commit];
        renderer.Dispatch(uncertain,1,1,1);
        const auto waits=renderer.IndirectCpuWriteWaitCount();
        std::thread release([event] {std::this_thread::sleep_for(std::chrono::milliseconds(30));event.signaledValue=1;});
        renderer.CpuWrite((variant==1 ? texture_alias : address)+49152,after,sizeof(after),false);
        release.join();Drain(renderer);
        assert(renderer.IndirectCpuWriteWaitCount()==waits+1);
        assert(reinterpret_cast<const u32*>(base+70400)[0]==(variant==2 ? 0u : 23u));
        assert(reinterpret_cast<const u32*>(base+70656)[0]==0);
        renderer.Dispatch(uncertain,1,1,1);Drain(renderer);
        assert(reinterpret_cast<const u32*>(base+70656)[0]==23);
    }

    // The paired runtime mutation guard likewise drains a producer before
    // unmapping its descriptor alias, while its other mapping remains live.
    const u64 first=address+70912;
    const u32 mapped_descriptor[]{u32(first),u32(first>>32)|(4u<<16),1,0x24004};
    renderer.CpuWrite(address+49152,mapped_descriptor,sizeof(mapped_descriptor),false);
    auto alias_producer=uncertain;
    alias_producer[Reg::COMPUTE_USER_DATA_0]=u32(texture_alias+49152);
    alias_producer[Reg::COMPUTE_USER_DATA_0+1]=u32((texture_alias+49152)>>32);
    auto descriptor_gate=[metal newSharedEvent];auto descriptor_blocker=[queue commandBuffer];
    [descriptor_blocker encodeWaitForEvent:descriptor_gate value:1];[descriptor_blocker commit];
    renderer.Dispatch(alias_producer,1,1,1);
    const auto mutation_waits=renderer.IndirectCpuWriteWaitCount();
    std::thread descriptor_release([descriptor_gate] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));descriptor_gate.signaledValue=1;
    });
    assert(!unmap(reinterpret_cast<void*>(texture_alias),131072));descriptor_release.join();Drain(renderer);
    assert(renderer.IndirectCpuWriteWaitCount()==mutation_waits+1);
    assert(reinterpret_cast<const u32*>(base+70912)[0]==23);

    // A producer that overwrites its own table cannot have a stable extent.
    const u32 self_descriptor[]{u32(address+49152),u32((address+49152)>>32)|(4u<<16),1,0x24004};
    renderer.CpuWrite(address+49152,self_descriptor,sizeof(self_descriptor),false);
    guarded([&] {
        renderer.Dispatch(uncertain,1,1,1);
        assert(!renderer.DrawIndirect(from_table,DrawCall{},address+32));
    });
    renderer.CpuWrite(address+49152,mapped_descriptor,sizeof(mapped_descriptor),false);
    for(unsigned lowbits=1;lowbits<4;++lowbits) {
        auto unaligned=uncertain;
        unaligned[Reg::COMPUTE_USER_DATA_0]=u32(address+49152+lowbits);
        guarded([&] {
            renderer.Dispatch(unaligned,1,1,1);
            assert(!renderer.DrawIndirect(from_table,DrawCall{},address+32));
        });
    }
    // Linear reflection sees descriptor B, but a branch skips its definition
    // and the GPU actually stores through A. Such writers stay unknown.
    const auto smov64=[](u32 destination,u32 source) {
        return 0xbe800000u|(destination<<16)|(u32(Sop1::S_MOV_B64)<<8)|source;
    };
    const u32 branch_writer[]{smov64(8,0),smov64(10,2),
        0xbf800000u|(u32(Sopp::S_BRANCH)<<16)|2u,smov64(8,4),smov64(10,6),
        vmov(0,Operand::Zero),vmov(1,Operand::Zero+23),
        0xe0002000u|(u32(Mubuf::BUFFER_STORE_DWORD)<<18),0x80020100,0xbf810000};
    std::memcpy(shader+768,branch_writer,sizeof(branch_writer));
    auto branch_compute=compute;branch_compute[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+768)>>8);
    branch_compute[Reg::COMPUTE_PGM_RSRC2]=8u<<1;
    const u64 branch_first=centre,branch_second=address+71168;
    const u32 branch_descriptors[]{u32(branch_first),u32(branch_first>>32)|(4u<<16),1,0x24004,
        u32(branch_second),u32(branch_second>>32)|(4u<<16),1,0x24004};
    for(unsigned k=0;k<8;++k)branch_compute[Reg::COMPUTE_USER_DATA_0+k]=branch_descriptors[k];
    const auto branch_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Compute,
        uintptr_t(shader+768),branch_compute.UserData(Reg::COMPUTE_USER_DATA_0),8,0,branch_writer});
    assert(branch_program && branch_program->has_branches && branch_program->buffer_writes.size()==1 &&
        branch_program->buffer_writes[0][0].kind==Gpu::Shader::SgprSource::UserData &&
        branch_program->buffer_writes[0][0].ud==4);
    const u32 cleared=0;
    renderer.CpuWrite(branch_first,&cleared,4,false);renderer.CpuWrite(branch_second,&cleared,4,false);
    guarded([&] {
        renderer.Dispatch(branch_compute,1,1,1);
        assert(!renderer.DrawIndirect(from_table,DrawCall{},address+32));
    });
    assert(*reinterpret_cast<const u32*>(branch_first)==23 && *reinterpret_cast<const u32*>(branch_second)==0);

    // Descriptor read protection remains bounded. An exhausted registry must
    // become unknown, rather than publish a writer extent without its lease.
    guarded([&] {
        for(unsigned k=0;k<513;++k)renderer.Dispatch(uncertain,1,1,1);
        assert(!renderer.DrawIndirect(from_table,DrawCall{},address+32));
    });

    // Cached shader bytes are CPU inputs too. A queued DMA mutation must
    // reject native admission before the renderer compares or decodes them.
    output(address+60000);
    guarded([&] {
        assert(renderer.DmaData(0,uintptr_t(shader+1),2,vmov(1,Operand::Zero+31),4));
        assert(!renderer.DispatchIndirect(compute,address));
    });
    renderer.Dispatch(compute,1,1,1);Drain(renderer);
    assert(reinterpret_cast<const u32*>(base+60000)[0]==31);
    renderer.CpuWrite(uintptr_t(shader+1),cs+1,4,false);

    // A small called fetch subroutine proves the separately cached span is
    // guarded as well as the main program. Mutating NOP0 -> NOP1 changes the
    // bytes without changing its output, so the drained decode can be checked.
    const u32 called_vs[]{0xbe800000u|(4u<<16)|(u32(Sop1::S_SWAPPC_B64)<<8),
        0xf80008cf,0x04030201,0xbf810000};
    const u32 fetch[]{0xbf800000,0xc0820300,0xe00c2000,0x80010100,
        0xbe800000u|(u32(Sop1::S_SETPC_B64)<<8)|4u};
    std::memcpy(shader+832,called_vs,sizeof(called_vs));std::memcpy(shader+896,fetch,sizeof(fetch));
    auto called=draw;called[Reg::SPI_SHADER_PGM_LO_VS]=u32(uintptr_t(shader+832)>>8);
    called[Reg::SPI_SHADER_PGM_RSRC2_VS]=4u<<1;
    called[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(uintptr_t(shader+896));
    called[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(u64(uintptr_t(shader+896))>>32);
    called[Reg::SPI_SHADER_USER_DATA_VS_0+2]=u32(address+8256);
    called[Reg::SPI_SHADER_USER_DATA_VS_0+3]=u32((address+8256)>>32);
    const auto called_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Vertex,
        uintptr_t(shader+832),called.UserData(Reg::SPI_SHADER_USER_DATA_VS_0),4,0,called_vs});
    assert(called_program && called_program->fetch_address==uintptr_t(shader+896) &&
        called_program->fetch_bytes==sizeof(fetch));
    renderer.Draw(called,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[2]==255 && pixel[0]==0);
    assert(renderer.DrawIndirect(called,DrawCall{},address+32));Drain(renderer);
    guarded([&] {
        assert(renderer.DmaData(0,uintptr_t(shader+896),2,0xbf800001,4));
        assert(!renderer.DrawIndirect(called,DrawCall{},address+32));
    });
    renderer.Draw(called,DrawCall{.count=3});pixel=Pixel(device,renderer,address+16384);
    assert(pixel[2]==255 && pixel[0]==0);

    // Low-map code is backed by independent anonymous CPU pages. Even an
    // unknown guest-buffer writer cannot touch it through mem0/mem1.
    auto* low_code=static_cast<u32*>(bbmetal_guest_allocate(1024));assert(low_code);
    std::memcpy(low_code,cs,sizeof(cs));output(address+60928);
    auto low_compute=compute;low_compute[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(low_code)>>8);
    renderer.Dispatch(low_compute,1,1,1);Drain(renderer);
    renderer.CpuWrite(address+60928,&once[0],4,false);
    guarded([&] {
        renderer.Dispatch(unknown_compute,1,1,1);
        assert(renderer.DispatchIndirect(low_compute,address));
    });
    assert(reinterpret_cast<const u32*>(base+60928)[0]==23);

    // Image stores write a private Metal texture, never the no-copy guest
    // buffer. They must not create an unknown raw-buffer writer ticket that
    // blocks an independent CPU texture upload/read during native setup.
    const u32 image_writer[]{vmov(0,Operand::Zero+8),vmov(1,Operand::Zero+8),
        vmov(2,Operand::Zero),vmov(3,Operand::Zero),
        vmov(4,Operand::FloatHalf+2),vmov(5,Operand::FloatHalf+2),
        0xf0000000u|(u32(Mimg::IMAGE_STORE)<<18)|(15u<<8),2u<<8,0xbf810000};
    std::memcpy(shader+704,image_writer,sizeof(image_writer));
    const u32 private_texture[]{u32((address+53248)>>8),u32((address+53248)>>40)|(10u<<20),
        15u|(15u<<14),0x90800fac,15u<<13,0,0,0};
    std::fill_n(reinterpret_cast<u32*>(base+53248),256,0x11223344u);
    auto private_compute=compute;
    private_compute[Reg::COMPUTE_PGM_LO]=u32(uintptr_t(shader+704)>>8);
    private_compute[Reg::COMPUTE_PGM_RSRC2]=8u<<1;
    for(unsigned k=0;k<8;++k)private_compute[Reg::COMPUTE_USER_DATA_0+k]=private_texture[k];
    const auto image_program=Gpu::Shader::BuildProgram({Gpu::Shader::Stage::Compute,
        uintptr_t(shader+704),private_compute.UserData(Reg::COMPUTE_USER_DATA_0),8,0,image_writer,
        private_compute[Reg::COMPUTE_PGM_RSRC2]});
    assert(image_program && image_program->buffer_writes.empty() && image_program->images.size()==1 &&
        image_program->images[0].storage && image_program->unsupported.empty());
    renderer.Dispatch(private_compute,1,1,1);Drain(renderer);
    assert(!renderer.DispatchIndirect(private_compute,address)); // storage-image admission still falls back
    guarded([&] {
        renderer.Dispatch(private_compute,1,1,1);
        assert(renderer.DrawIndirect(sampled,DrawCall{},address+32));
    });
    pixel=Pixel(device,renderer,address+16384);assert(pixel[2]==23 && pixel[1]==0);
    assert(reinterpret_cast<const u32*>(base+53248)[8*16+8]==0x11223344u);
    auto private_sample=sampled;
    for(unsigned k=0;k<8;++k)private_sample[Reg::SPI_SHADER_USER_DATA_PS_0+k]=private_texture[k];
    assert(renderer.DrawIndirect(private_sample,DrawCall{},address+32));
    pixel=Pixel(device,renderer,address+16384);assert(pixel[0]==255 && pixel[1]==0 && pixel[2]==0);

    // Mipped GPU target sampling may grow/migrate CPU alias metadata even
    // for GPU count0. Keep that setup on the drained path.
    auto mipped_sample=private_sample;mipped_sample[Reg::SPI_SHADER_USER_DATA_PS_0+3]|=1u<<16;
    renderer.Draw(mipped_sample,DrawCall{.count=3});Drain(renderer);
    assert(!renderer.DrawIndirect(mipped_sample,DrawCall{},address+32));
    Tiling::MipLevel private_mips[2];
    Tiling::MipLayout(8,16,16,1,2,4,false,false,private_mips);
    const auto child_address=address+53248+private_mips[1].offset;
    auto depth_only=draw;
    depth_only[Reg::CB_TARGET_MASK]=depth_only[Reg::CB_SHADER_MASK]=0;
    depth_only[Reg::SPI_SHADER_COL_FORMAT]=0;
    depth_only[Reg::DB_Z_INFO]=3;depth_only[Reg::DB_DEPTH_CONTROL]=0x76;
    depth_only[Reg::DB_SHADER_CONTROL]=0x10;
    depth_only[Reg::DB_Z_READ_BASE]=depth_only[Reg::DB_Z_WRITE_BASE]=u32(child_address>>8);
    renderer.Draw(depth_only,DrawCall{.count=3});Drain(renderer);
    const u32 zero_draw[]{0,1,0,0};renderer.CpuWrite(address+32,zero_draw,sizeof(zero_draw),false);
    assert(!renderer.DrawIndirect(depth_only,DrawCall{},address+32)); // existing color mip alias

    // Both registries can retain a surface at the same VA. A zero native
    // draw must not promote stale color above newer depth (or vice versa).
    depth_only[Reg::DB_Z_READ_BASE]=depth_only[Reg::DB_Z_WRITE_BASE]=u32((address+16384)>>8);
    renderer.Draw(depth_only,DrawCall{.count=3});Drain(renderer);
    assert(!renderer.DrawIndirect(draw,DrawCall{},address+32));
    assert(!renderer.DrawIndirect(depth_only,DrawCall{},address+32));
    renderer.CpuWrite(address+32,triangle,sizeof(triangle),false);
    // These optional modes can add CPU leaf reads beyond ordinary resource
    // setup. An initialized, otherwise eligible target must still fallback.
    for(const char* option:{"BB_METAL_STATIC_SMRD","BB_METAL_DIRECT_SMRD"}) {
        const char* previous=std::getenv(option);const bool present=previous!=nullptr;
        const std::string saved=previous ? previous : "";
        setenv(option,"1",1);
        {
            Gpu::Metal::MetalRenderer optional(device);
            optional.Draw(draw,DrawCall{.count=3});Pixel(device,optional,address+16384);
            assert(!optional.DrawIndirect(draw,DrawCall{},address+32));
            assert(optional.NativeIndirectDrawCount()==0);
        }
        if(present)setenv(option,saved.c_str(),1);else unsetenv(option);
    }
    puts("indirect GPU: ordered snapshots/self-modification, CPU/physical alias protection, zero dimensions/instances and pending resource producer guards passed");
}
int main(int argc,char** argv) {
    @autoreleasepool {
        u64 physical=0;assert(runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical)==0);
        uintptr_t address=0;assert(runtime_gpu_memory_map(&address,131072,3,0,physical,16384)==0);
        uintptr_t alias=0;assert(runtime_gpu_memory_map(&alias,131072,3,0,physical,16384)==0);
        assert(alias!=address);Cpu(address,alias);
        if(argc>1 && std::strcmp(argv[1],"--gpu")==0) {
            if(!MTLCreateSystemDefaultDevice())return 77;
            GpuCases(address,alias);
        }
    }
}
