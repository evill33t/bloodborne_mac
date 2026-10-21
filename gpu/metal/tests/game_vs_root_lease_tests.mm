// SPDX-License-Identifier: GPL-2.0-or-later
// Optional actual-renderer lease fixture. Captured original shader bytes are
// read only from an explicit private directory; no game bytes are embedded.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/tests/root_lease_fixture.h"
#include "gpu/metal/direct_root_read.h"
#include "gpu/metal/native_vertex_fetch.h"
#include "gpu/metal/native_indirect.h"
#include "gpu/liverpool/command_processor.h"
#include "gpu/shader/program.h"
#include "gpu/shader/msl.h"
#include "src/runtime_gpu_memory.h"
extern "C" {
#include "src/runtime.h"
}
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

extern "C" void runtime_restart(){std::abort();}
namespace {
using namespace Gpu;
using namespace Gpu::Liverpool;
namespace Root=Gpu::Metal::DirectRootRead;
namespace Native=Gpu::Metal::NativeVertexFetch;
namespace Fixture=RootLeaseFixture;
using Renderer=Gpu::Metal::MetalRenderer;
using Device=Gpu::Metal::Device;
using Fixture::Require;
constexpr u64 Bytes=0x100000,Page=16384;
constexpr u64 Table=0,Records=0x4000,IndirectConstants=0x8000,Bones=0xc000,
    View=0x10000,Const8=0x14000,Const12=0x18000,Probe=0x1c000,
    TargetA=0x20000,TargetB=0x24000,SimpleVS=0x30000,SimplePS=0x34000,RedPS=0x35000,
    SimpleVertices=0x38000,SimpleDescriptor=0x3c000,OriginalVS=0x40000,Fetch=0x58000;
using Descriptor=std::array<u32,4>;
using Unmap=int(*)(void*,u64);
struct PrivateData {
    std::filesystem::path directory;
    PrivateData() {
        const char* temporary=std::getenv("TMPDIR");
        std::string name=std::string(temporary ? temporary : "/private/tmp")+"/bb-vs-root-renderer-XXXXXX";
        std::vector<char> bytes(name.begin(),name.end());bytes.push_back(0);
        auto* path=mkdtemp(bytes.data());Require(path!=nullptr,"private fixture data directory");directory=path;
        setenv("BB_DATA_DIR",path,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");
        unsetenv("BB_METAL_CACHE_DIR");setenv("BB_METAL_PIPELINE_CACHE","0",1);setenv("BB_METAL_SHADER_PREWARM","0",1);
        unsetenv("BB_METAL_STUTTER_TRACE");unsetenv("BB_METAL_FRAME_WORK_CSV");
    }
    ~PrivateData(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
struct Mapping {
    u64 physical=0;uintptr_t address=0,alias=0;Unmap unmap=nullptr;
    Mapping() {
        Require(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),Bytes,Page,0,&physical),"physical allocation");
        Require(!runtime_gpu_memory_map(&address,Bytes,3,0,physical,Page),"primary mapping");
        Require(!runtime_gpu_memory_map(&alias,Bytes,3,0,physical,Page) && alias!=address,"physical alias mapping");
        unmap=reinterpret_cast<Unmap>(runtime_memory_resolve("cQke9UuBQOk#p#J"));Require(unmap!=nullptr,"runtime unmap resolver");
        std::memset(reinterpret_cast<void*>(address),0,Bytes);
    }
    ~Mapping() {
        if(alias && unmap)unmap(reinterpret_cast<void*>(alias),Bytes);
        if(address && unmap)unmap(reinterpret_cast<void*>(address),Bytes);
    }
    u64 At(u64 offset)const{return u64(address)+offset;}
    template<class T>void Put(u64 offset,const T& value){std::memcpy(reinterpret_cast<void*>(At(offset)),&value,sizeof(value));}
    void Copy(u64 offset,const void* source,size_t bytes){Require(offset<=Bytes && bytes<=Bytes-offset,"fixture memory bounds");std::memcpy(reinterpret_cast<void*>(At(offset)),source,bytes);}
};
struct Hooks {
    explicit Hooks(Renderer& renderer) {
        Require(runtime_gpu_memory_set_mutation_hooks(&renderer,
            [](void* p)->int{return static_cast<Renderer*>(p)->BeginGuestMemoryMutation();},
            [](void* p){static_cast<Renderer*>(p)->EndGuestMemoryMutation();}),"paired mutation hooks");
    }
    ~Hooks(){if(!runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr))std::abort();}
};
Descriptor Buffer(u64 base,u32 stride,u32 records,u32 format=0x20077fac) {
    return {u32(base),u32(base>>32)|(stride<<16),records,format};
}
void Pointer(RegFile& r,unsigned slot,u64 address) {
    r[Reg::SPI_SHADER_USER_DATA_VS_0+slot]=u32(address);
    r[Reg::SPI_SHADER_USER_DATA_VS_0+slot+1]=u32(address>>32);
}
std::vector<u32> ReadWords(const std::filesystem::path& path) {
    const auto bytes=std::filesystem::file_size(path);
    Require(bytes>=16 && bytes<=65536 && !(bytes&3),"original capture size/alignment");
    std::vector<u32> words(bytes/4+64,0xbf810000u);std::ifstream stream(path,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(words.data()),std::streamsize(bytes))),"read original capture");return words;
}
std::unique_ptr<Gpu::Shader::Program> LoadCapture(Mapping& memory,const std::filesystem::path& directory,u64 hash,RegFile& r) {
    char suffix[32];std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path selected;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if(entry.is_regular_file() && name.starts_with("vs_") && name.ends_with(suffix)){selected=entry.path();break;}
    }
    Require(!selected.empty(),"missing original VS capture");
    const auto code=ReadWords(selected),fetch=ReadWords(selected.string()+".fetch.bin");
    Require(code.size()*4<=Fetch-OriginalVS,"code/fetch fixture overlap");
    memory.Copy(OriginalVS,code.data(),code.size()*4);memory.Copy(Fetch,fetch.data(),fetch.size()*4);
    r[Reg::SPI_SHADER_PGM_LO_VS]=u32(memory.At(OriginalVS)>>8);
    r[Reg::SPI_SHADER_PGM_LO_VS+1]=u32(memory.At(OriginalVS)>>40);
    r[Reg::SPI_SHADER_PGM_RSRC1_VS]=3u<<24;r[Reg::SPI_SHADER_PGM_RSRC2_VS]=16u<<1;
    Pointer(r,0,memory.At(Fetch));
    auto program=Gpu::Shader::BuildProgram({.stage=Gpu::Shader::Stage::Vertex,.address=memory.At(OriginalVS),
        .user_data=r.UserData(Reg::SPI_SHADER_USER_DATA_VS_0),.num_user_sgprs=16,.vgpr_comp_cnt=3,
        .code=reinterpret_cast<const u32*>(memory.At(OriginalVS))});
    Require(program && program->hash==hash && Gpu::Shader::VertexNativeFetchEligible(*program) &&
        Gpu::Shader::GuestMemoryReadOnlyEligible(*program),"actual captured VS identity/shape/no-writer proof");
    return program;
}
RegFile DrawState(const Mapping& memory) {
    RegFile draw;
    draw[Reg::VGT_PRIMITIVE_TYPE]=4;draw[Reg::CB_COLOR0_BASE]=u32(memory.At(TargetB)>>8);
    draw[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
    draw[Reg::CB_COLOR_CONTROL]=1u<<4;draw[Reg::CB_TARGET_MASK]=draw[Reg::CB_SHADER_MASK]=15;
    draw[Reg::SPI_SHADER_COL_FORMAT]=9;draw[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{4,4,-4,4,1,0};
    for(unsigned k=0;k<6;++k)draw[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<u32>(viewport[k]);
    draw[Reg::PA_SC_SCREEN_SCISSOR_BR]=draw[Reg::PA_SC_WINDOW_SCISSOR_BR]=8u|(8u<<16);
    draw[Reg::PA_SC_GENERIC_SCISSOR_BR]=8u|(8u<<16);
    draw[Reg::SPI_SHADER_PGM_LO_PS]=u32(memory.At(SimplePS)>>8);
    draw[Reg::SPI_SHADER_PGM_LO_PS+1]=u32(memory.At(SimplePS)>>40);
    return draw;
}
RegFile Inputs(Mapping& memory,u64 hash,const std::filesystem::path& directory) {
    auto draw=DrawState(memory);auto program=LoadCapture(memory,directory,hash,draw);
    const auto* layout=Native::Layout(program->hash);Require(layout!=nullptr,"unknown native VS layout");
    Pointer(draw,2,memory.At(Table));Pointer(draw,6,memory.At(IndirectConstants));
    for(unsigned k=0;k<layout->attributes;++k)
        memory.Put(Table+k*16,Buffer(memory.At(Records)+Native::Offsets[k],layout->stride,3,Native::Formats[k]));
    const std::array<std::array<float,3>,3> positions{{{-1,-1,.5f},{3,-1,.5f},{-1,3,.5f}}};
    for(unsigned vertex=0;vertex<3;++vertex) {
        std::array<u32,9> packed{};
        for(unsigned component=0;component<3;++component)packed[component]=std::bit_cast<u32>(positions[vertex][component]);
        packed[3]=packed[4]=0x007f7f7fu;packed[5]=0xff000000u;
        memory.Copy(Records+vertex*layout->stride,packed.data(),layout->stride);
    }
    memory.Put(IndirectConstants,Buffer(memory.At(Bones),16,3));
    memory.Put(IndirectConstants+16,Buffer(memory.At(View),16,64));
    for(unsigned row=0;row<3;++row){std::array<float,4> value{};value[row]=1;memory.Put(Bones+row*16,value);}
    for(unsigned row=0;row<4;++row){std::array<float,4> value{};value[row]=1;memory.Put(View+0x320+row*16,value);}
    const auto d8=Buffer(memory.At(Const8),16,4),d12=Buffer(memory.At(Const12),16,26);
    for(unsigned k=0;k<4;++k){draw[Reg::SPI_SHADER_USER_DATA_VS_0+8+k]=d8[k];draw[Reg::SPI_SHADER_USER_DATA_VS_0+12+k]=d12[k];}
    const std::array<float,4> uv{1,1,0,0};memory.Put(Const8,uv);memory.Put(Const8+0x30,uv);memory.Put(Const12+0x30,uv);
    memory.Put(Const12+0x50,u32(0));
    // Synthetic unchanged constant-blue PS and ordinary inline-fetch warm VS.
    const u32 blue[]{0x7e000280,0x7e020280,0x7e0402f2,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    const u32 red[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    const u32 simple[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    memory.Copy(SimplePS,blue,sizeof(blue));memory.Copy(RedPS,red,sizeof(red));memory.Copy(SimpleVS,simple,sizeof(simple));
    const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};memory.Copy(SimpleVertices,vertices,sizeof(vertices));
    memory.Put(SimpleDescriptor,Buffer(memory.At(SimpleVertices),16,3,0xfacu|(7u<<12)|(14u<<15)));
    return draw;
}
std::array<u8,4> Pixel(Device& device,Renderer& renderer,u64 address) {
    renderer.Flip({.address=address,.width=8,.height=8,.pitch=8});Fixture::Drain(renderer);
    auto texture=(__bridge id<MTLTexture>)renderer.LatestFrame();Require(texture!=nil,"actual renderer target frame");
    auto metal=(__bridge id<MTLDevice>)device.Handle();auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
    auto result=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];Require(result!=nil,"pixel readback allocation");
    auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];Require(blit!=nil,"pixel blit encoder");
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(8,8,1) toBuffer:result destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    [blit endEncoding];[command commit];Fixture::Completed(command,"pixel readback");
    std::array<u8,4> pixel;std::memcpy(pixel.data(),static_cast<const u8*>(result.contents)+4*(4*8+4),4);return pixel;
}
void Blue(Device& device,Renderer& renderer,u64 address) {
    const auto pixel=Pixel(device,renderer,address);
    Require(pixel[0]==255 && pixel[1]==0 && pixel[2]==0 && pixel[3]==255,"nonzero original VS draw must cover centre in blue");
}
u64 Count(const Renderer& renderer,Root::Reject result) {return renderer.DirectVertexRootProofCount(u32(result));}
void Run(const std::filesystem::path& directory,u64 hash) {
    Mapping memory;auto target=Inputs(memory,hash,directory);
    Device device;Renderer renderer(device);Hooks hooks(renderer);
    renderer.SetCpuReadGuard([](GuestAddr,size_t){return true;});
    auto ordinary=target;ordinary[Reg::SPI_SHADER_PGM_LO_VS]=u32(memory.At(SimpleVS)>>8);
    ordinary[Reg::SPI_SHADER_PGM_LO_VS+1]=u32(memory.At(SimpleVS)>>40);
    ordinary[Reg::SPI_SHADER_PGM_RSRC1_VS]=0;ordinary[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;
    std::fill_n(ordinary.Data()+Reg::SPI_SHADER_USER_DATA_VS_0,16,0);
    Pointer(ordinary,0,memory.At(SimpleDescriptor));ordinary[Reg::CB_COLOR0_BASE]=u32(memory.At(TargetA)>>8);
    renderer.Draw(ordinary,DrawCall{.count=3});renderer.Draw(target,DrawCall{.count=3});
    Require(Count(renderer,Root::Reject::None)==1,"original VS root must positively admit while warming");
    Blue(device,renderer,memory.At(TargetB)); // proves useful geometry and warms flip path
    const auto recordA=[&]{renderer.Draw(ordinary,DrawCall{.count=3});};
    const auto recordB=[&] {
        const auto hits=Count(renderer,Root::Reject::None);
        renderer.Draw(target,DrawCall{.count=3});
        Require(Count(renderer,Root::Reject::None)==hits+1,"target consumer B did not admit exactly one VS root");
    };
    auto red=ordinary;red[Reg::CB_COLOR0_BASE]=u32(memory.At(TargetB)>>8);
    red[Reg::SPI_SHADER_PGM_LO_PS]=u32(memory.At(RedPS)>>8);
    red[Reg::SPI_SHADER_PGM_LO_PS+1]=u32(memory.At(RedPS)>>40);
    const auto prime=[&] {
        renderer.Draw(red,DrawCall{.count=3});
        const auto pixel=Pixel(device,renderer,memory.At(TargetB));
        Require(pixel[0]==0 && pixel[1]==0 && pixel[2]==255 && pixel[3]==255,"consumer target red precondition");
    };
    prime();
    const u32 reduced=0;
    Fixture::HoldConsumer(device,renderer,memory.At(Probe),recordA,recordB,[&] {
        renderer.CpuWrite(u64(memory.alias)+Table+8,&reduced,4,false);
    },"VS physical alias CpuWrite");
    Require(Fixture::Word(memory.At(Table+8))==reduced,"alias CPU edit not published");
    Blue(device,renderer,memory.At(TargetB));
    const u32 restored=3;renderer.CpuWrite(memory.At(Table+8),&restored,4,false);
    // This draw consumes the alias VA; unmap and replace that VA through the
    // actual paired runtime mutation hooks while the PRIMARY mapping remains.
    prime();Pointer(target,2,u64(memory.alias)+Table);
    Fixture::HoldConsumer(device,renderer,memory.At(Probe),recordA,recordB,[&] {
        Require(!memory.unmap(reinterpret_cast<void*>(memory.alias),Bytes),"leased alias unmap");memory.alias=0;
        Require(!runtime_gpu_memory_map(&memory.alias,Bytes,3,0,memory.physical,Page),"alias replacement mapping");
    },"VS actual alias unmap/remap");
    Blue(device,renderer,memory.At(TargetB));Pointer(target,2,memory.At(Table));
    const auto sentinel=[&](Root::Reject reason,const char* label) {
        const auto hits=Count(renderer,Root::Reject::None),before=Count(renderer,reason);
        renderer.Draw(target,DrawCall{.count=3});
        Require(Count(renderer,Root::Reject::None)==hits && Count(renderer,reason)==before+1,std::string(label)+" did not use exact sentinel reason");
        Fixture::Drain(renderer);Blue(device,renderer,memory.At(TargetB));
    };
    prime();renderer.SetCpuReadGuard({});sentinel(Root::Reject::MissingGuard,"empty CP predicate");
    prime();renderer.SetCpuReadGuard([&](GuestAddr address,size_t bytes) {
        return !Gpu::Metal::NativeIndirect::Overlap({address,bytes},{memory.At(Table),u64(Native::Layout(hash)->attributes)*16});
    });sentinel(Root::Reject::CpuPending,"effective CP pending table");
    renderer.SetCpuReadGuard([](GuestAddr,size_t){return true;});
    prime();
    {
        // Install the real CP pre-enqueue physical-alias fence predicate.
        // EOP IRQ0 preserves the descriptor value; the queue gate keeps both
        // the callback effect and CP pending-fence entry outstanding.
        Fixture::QueueGate gate(device);CommandProcessor cp;cp.SetRenderer(&renderer);
        const auto label=u64(memory.alias)+Table;const auto value=Fixture::Word(memory.At(Table));
        std::vector<u32> fence{0xc0044700u,0x14,u32(label),u32(label>>32)|(1u<<29),value,0};
        cp.SubmitGraphics({{{fence.data(),fence.size()},{}}});cp.WaitIdle();
        const auto hits=Count(renderer,Root::Reject::None),before=Count(renderer,Root::Reject::CpuPending);
        renderer.Draw(target,DrawCall{.count=3});
        const bool rejected=Count(renderer,Root::Reject::None)==hits && Count(renderer,Root::Reject::CpuPending)==before+1;
        gate.Open();Fixture::Drain(renderer);
        Require(rejected && !cp.Execution()->Failed(),"actual CP physical-alias fence did not force VS sentinel");
        Blue(device,renderer,memory.At(TargetB));
    }
    renderer.SetCpuReadGuard([](GuestAddr,size_t){return true;});
    std::atomic<bool> callback=false;
    prime();renderer.Signal([&]{callback.store(true,std::memory_order_release);});
    sentinel(Root::Reject::Callback,"queued unknown callback");Require(callback.load(std::memory_order_acquire),"unknown callback not drained");
    prime();callback=false;renderer.SignalCpuWriteOnly(u64(memory.alias)+Table,16,[&]{callback.store(true,std::memory_order_release);});
    sentinel(Root::Reject::Callback,"queued physical-alias callback");Require(callback.load(std::memory_order_acquire),"alias callback not drained");
    // Known physically disjoint effects preserve admission, without a global
    // callback drain at the root check. The callback body intentionally writes no guest bytes.
    callback=false;renderer.SignalCpuWriteOnly(memory.At(Probe),4,[&]{callback.store(true,std::memory_order_release);});
    recordB();Require(!callback.load(std::memory_order_acquire),"disjoint callback unexpectedly forced a root admission drain");
    Fixture::Drain(renderer);Require(callback.load(std::memory_order_acquire),"disjoint callback not drained");
    Require(renderer.DirectVertexRootProofCount(UINT32_MAX)==0,"invalid diagnostic counter index");
    std::printf("Actual renderer VS %016llx: nonzero pixels, post-cut alias/mapping consumer leases and CP/callback sentinels passed.\n",(unsigned long long)hash);
}
} // namespace
int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            Require(argc==2,"supply explicit original shader capture directory");
            if(!MTLCreateSystemDefaultDevice())return 77;
            setenv("BB_METAL_NATIVE_INDIRECT","1",1);setenv("BB_METAL_NATIVE_TEXTURE_COHERENCE","1",1);
            setenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT","1",1);setenv("BB_METAL_DIRECT_VERTEX_ROOT","1",1);
            setenv("BB_METAL_STATIC_SMRD","0",1);setenv("BB_METAL_DIRECT_SMRD","0",1);setenv("BB_METAL_SIMD_SMRD","0",1);
            // Diagnostic cuts would invalidate this fixture's explicit A/B topology.
            for(const char* option:{"DC_GPU_PROFILE","DC_SERIAL","DC_TRACE_CS","DC_DUMP_PASSES","BB_METAL_STAGE_COUNTER_CSV","BB_METAL_COMMAND_TIMELINE_CSV","DC_PROBE","DC_DEBUG_VSOUT","DC_DEBUG_FLAT","DC_DEBUG_CLEAR","DC_DUMP_SHADERS","DC_DUMP_FRAMES","DC_TRACE_FILE","DC_TRACE_FRAME","DC_GPU_TRACE","DC_DUMP_TEXTURE","BB_METAL_CAPTURE_TEXTURE","BB_METAL_CAPTURE_DIR","BB_METAL_CAPTURE_DREAM_MATERIAL"})unsetenv(option);
            const auto captures=std::filesystem::canonical(argv[1]);PrivateData private_data;
            for(const auto hash:{Native::Hash32,Native::Hash36})Run(captures,hash);
            return 0;
        } catch(const std::exception& error){std::fprintf(stderr,"VS root lease fixture: %s\n",error.what());return 1;}
    }
}
