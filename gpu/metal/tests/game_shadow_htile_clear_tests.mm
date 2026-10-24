// SPDX-License-Identifier: GPL-2.0-or-later
// Private original-CS input only. Exercise the real renderer HTILE->native
// depth lifetime, while retaining the original metadata RMW execution.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <CommonCrypto/CommonDigest.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/metal/raw_texture_snapshot.h"
#include "gpu/liverpool/registers.h"
#include "gpu/shader/program.h"
#include "src/runtime_gpu_memory.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" void runtime_restart() {std::abort();}
extern "C" std::uintptr_t runtime_memory_resolve(const char*);
namespace {
using namespace Gpu;
using namespace Gpu::Liverpool;
using Renderer=Gpu::Metal::MetalRenderer;
namespace Raw=Gpu::Metal::RawTextureSnapshot;
using Clock=std::chrono::steady_clock;
constexpr u64 ShaderHash=0x595f38bd3818a17bull;
constexpr unsigned Atlas=4096,MetadataWords=262144,MetadataBytes=MetadataWords*4;
constexpr u64 AllocationBytes=66ull<<20,MetadataOffset=65536,DepthOffset=2ull<<20;
constexpr unsigned Rsrc2=0x98;
// Actual original CS14 register probe: count0x40000, replacement0xfffffff0.
constexpr u32 ObservedReplacement=0xfffffff0u;
void Require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
auto Deadline() {return Clock::now()+std::chrono::seconds(15);}
struct PrivateData {
    std::filesystem::path directory;
    PrivateData() {
        const char* temporary=std::getenv("TMPDIR");
        std::string name=std::string(temporary ? temporary : "/private/tmp")+"/bb-shadow-htile-XXXXXX";
        std::vector<char> bytes(name.begin(),name.end());bytes.push_back(0);
        auto* path=mkdtemp(bytes.data());Require(path,"Cannot create fixture-owned directory");directory=path;
        setenv("BB_DATA_DIR",path,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");
        unsetenv("BB_METAL_CACHE_DIR");setenv("BB_METAL_PIPELINE_CACHE","0",1);
        setenv("BB_METAL_SHADER_PREWARM","0",1);
        for(const char* key:{"DC_GPU_PROFILE","DC_SERIAL","BB_METAL_COMMAND_TIMELINE_CSV",
            "BB_METAL_STAGE_COUNTER_CSV","BB_METAL_STUTTER_TRACE","BB_METAL_FRAME_WORK_CSV",
            "BB_METAL_CAPTURE_DIR","BB_METAL_CAPTURE_TEXTURE","DC_DUMP_SHADERS","DC_DUMP_FRAMES",
            "DC_TRACE_FILE","DC_DUMP_PASSES","DC_DUMP_TEXTURE","DC_PROBE","DC_DEBUG_VSOUT",
            "DC_DEBUG_FLAT","DC_DEBUG_CLEAR","DC_TRACE_CB","DC_DUMP_CS","DC_PEEK_CS"})unsetenv(key);
        setenv("BB_METAL_NATIVE_INDIRECT","1",1);setenv("BB_METAL_NATIVE_TEXTURE_COHERENCE","1",1);
        setenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT","1",1);
    }
    ~PrivateData(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
std::vector<u32> Original(const std::filesystem::path& path) {
    Require(std::filesystem::is_regular_file(path) && std::filesystem::file_size(path)==88,
        "Expected the exact88-byte original shadow metadata CS capture");
    std::vector<u32> code(88/4+64,0xbf810000u);
    std::ifstream stream(path,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(code.data()),88)),"Cannot read original shadow CS");
    std::array<unsigned char,CC_SHA256_DIGEST_LENGTH> digest{};CC_SHA256(code.data(),88,digest.data());
    std::string hex;
    for(const auto byte:digest){char value[3];std::snprintf(value,sizeof(value),"%02x",byte);hex+=value;}
    Require(hex=="2e7cc32511685e42091fc7b1502a75bb8049d79d89053f3e698abff772bc0a66",
        "Original shadow metadata CS digest mismatch");return code;
}
std::array<u32,4> Descriptor(u64 base,u32 stride,u32 records,u32 word3,u32 ignored=0) {
    return {u32(base),u32(base>>32)|(stride<<16)|ignored,records,word3};
}
u32 Word(std::span<const u8> bytes,std::size_t at) {
    Require(at<=bytes.size() && bytes.size()-at>=4,"Short checkpoint metadata");
    u32 result=0;for(unsigned k=0;k<4;++k)result|=u32(bytes[at+k])<<(8*k);return result;
}
u64 Qword(std::span<const u8> bytes,std::size_t at) {return u64(Word(bytes,at))|(u64(Word(bytes,at+4))<<32);}
void CheckDepth(Renderer& renderer,Gpu::Metal::Device& device,u64 address,u64 metadata,float expected,const char* step) {
    std::string error;auto capture=renderer.CaptureCheckpoint(Deadline(),error);
    Require(bool(capture),std::string(step)+" checkpoint: "+error);
    const auto& chunks=capture->Chunks();
    const auto header=std::find_if(chunks.begin(),chunks.end(),[](const auto& chunk){return chunk.id==0x1000;});
    Require(header!=chunks.end() && header->size<65536,"Missing/oversized checkpoint metadata");
    std::vector<u8> bytes(header->size);Require(header->read(0,bytes,error),"Cannot read renderer metadata: "+error);
    Require(bytes.size()>=116 && !std::memcmp(bytes.data(),"BBMTRL1",8) && Word(bytes,8)==1,"Checkpoint ABI changed");
    const auto count=Word(bytes,92),metas=Word(bytes,96);
    Require(count<=16 && metas<=16 && 116+u64(count)*112+u64(metas)*24<=bytes.size(),"Invalid checkpoint targets/metas");
    bool registered=false;
    for(unsigned k=0;k<metas;++k) {
        const auto at=116+std::size_t(count)*112+k*24;
        if(Qword(bytes,at)==metadata && Qword(bytes,at+8)==address && Word(bytes,at+16)==1)registered=true;
    }
    Require(registered,"Synthetic HTILE was not registered to the actual depth target");
    unsigned matches=0;
    for(unsigned target=0;target<count;++target) {
        const auto at=116+std::size_t(target)*112;if(Qword(bytes,at)!=address || !(Word(bytes,at+16)&1u))continue;++matches;
        const auto flags=Word(bytes,at+16);
        Require((flags&3u)==3u && !(flags&4u),"Depth target is uninitialized or still has an unapplied clear");
        Raw::Descriptor description{Word(bytes,at+36),Word(bytes,at+40),Word(bytes,at+44),Word(bytes,at+48),
            Word(bytes,at+52),Word(bytes,at+56),Word(bytes,at+60)};
        Require(description.format==MTLPixelFormatDepth32Float && description.width==Atlas &&
            description.height==Atlas && description.levels==1 && description.layers==1,
            "Fixture did not retain the exact4096x4096D32 atlas");
        std::vector<Raw::Plane> planes;
        Require(Raw::Layout(device.Handle(),description,planes,error) && planes.size()==1,"D32 layout: "+error);
        const auto id=0x100000000ull+(u64(target)<<16);
        const auto plane=std::find_if(chunks.begin(),chunks.end(),[&](const auto& chunk){return chunk.id==id;});
        Require(plane!=chunks.end() && plane->size==planes[0].size,"Missing authoritative D32 plane");
        // Full-screen immutable triangles should reach every probe, including
        // all four quadrants and the far edge; CPU guest backing is never read
        // as the source of depth truth.
        for(const auto& point:std::array<std::array<unsigned,2>,9>{{{0,0},{1,1},{2047,2047},
                {2048,2048},{4095,4095},{3072,1024},{1024,3072},{4095,0},{0,4095}}}) {
            std::array<u8,4> raw{};const u64 offset=u64(point[1])*planes[0].row_bytes+u64(point[0])*4;
            Require(plane->read(offset,raw,error),"Cannot read actual D32 pixels: "+error);
            const float observed=std::bit_cast<float>(Word(raw,0));
            if(observed!=expected) {
                char message[256];std::snprintf(message,sizeof(message),"%s depth(%u,%u): expected %.9g, got %.9g",
                    step,point[0],point[1],expected,observed);throw std::runtime_error(message);
            }
        }
    }
    Require(matches==1,"Ambiguous/missing4096D32 target");
}
std::array<u64,2> TargetVersions(Renderer& renderer,u64 depth,u64 color) {
    std::string error;auto capture=renderer.CaptureCheckpoint(Deadline(),error);Require(bool(capture),"Version checkpoint: "+error);
    const auto& chunks=capture->Chunks();
    const auto header=std::find_if(chunks.begin(),chunks.end(),[](const auto& chunk){return chunk.id==0x1000;});
    Require(header!=chunks.end() && header->size<65536,"Missing target version metadata");
    std::vector<u8> bytes(header->size);Require(header->read(0,bytes,error),"Cannot read target versions: "+error);
    const auto count=Word(bytes,92);Require(116+u64(count)*112<=bytes.size(),"Short target version table");
    std::array<u64,2> versions{};std::array<unsigned,2> matches{};
    for(unsigned n=0;n<count;++n) {
        const auto at=116+std::size_t(n)*112;const bool is_depth=Word(bytes,at+16)&1u;
        if(Qword(bytes,at)!=(is_depth ? depth : color))continue;
        const unsigned slot=is_depth ? 0 : 1;versions[slot]=Qword(bytes,at+8);++matches[slot];
    }
    Require(matches==std::array<unsigned,2>{1,1},"Ambiguous/missing colour/depth owner versions");return versions;
}
void Run(const std::filesystem::path& original,u32 replacement,bool proof_enabled,bool lease_enabled,bool gpu_enabled,
    bool gpu_disabled_control) {
    const auto code=Original(original);PrivateData private_data;
    setenv("BB_METAL_SHADOW_HTILE_GPU",gpu_enabled ? "1" : "0",1);
    setenv("BB_METAL_SHADOW_HTILE_REBIND",gpu_enabled ? "1" : "0",1);
    setenv("BB_METAL_SHADOW_HTILE_AUDIT","1",1);
    if(!proof_enabled)setenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT","0",1);
    u64 physical=0;std::uintptr_t address=0;
    Require(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),AllocationBytes,16384,0,&physical),"Cannot allocate fixture guest backing");
    Require(!runtime_gpu_memory_map(&address,AllocationBytes,3,0,physical,16384),"Cannot map fixture guest backing");
    std::uintptr_t alias=0;
    if(lease_enabled)Require(!runtime_gpu_memory_map(&alias,AllocationBytes,3,0,physical,16384) && alias!=address,
        "Cannot create a distinct physical alias before mapping/target capture");
    auto* memory=reinterpret_cast<u8*>(address);
    const u64 metadata=address+MetadataOffset,depth=address+DepthOffset,leaf=address+4096,cs_address=address+16384;
    // Complete immutable descriptors/vertices/code/leaf are prepared before
    // any command is recorded; no source is edited while the GPU reads it.
    for(unsigned k=0;k<2;++k) {
        const auto descriptor=Descriptor(address+512+k*256,16,3,0xfacu|(7u<<12)|(14u<<15));
        std::memcpy(memory+k*64,descriptor.data(),sizeof(descriptor));
        const float z=k ? .8f : .2f;
        const std::array<float,12> vertices{-1,-1,z,1,3,-1,z,1,-1,3,z,1};
        std::memcpy(memory+512+k*256,vertices.data(),sizeof(vertices));
    }
    constexpr std::array<u32,6> vertex_code{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    std::memcpy(memory+8192,vertex_code.data(),sizeof(vertex_code));
    std::memcpy(reinterpret_cast<void*>(cs_address),code.data(),code.size()*sizeof(u32));
    const std::array<u32,4> constants{MetadataWords,replacement,0x1289abcd,0x61520439};
    std::memcpy(reinterpret_cast<void*>(leaf),constants.data(),sizeof(constants));
    auto* words=reinterpret_cast<u32*>(metadata);std::vector<u32> before(MetadataWords);
    const auto FillMetadata=[&](bool preserved_tile) {
        for(unsigned k=0;k<MetadataWords;++k) {
            const unsigned low=(k&15u)==13u ? 14u : k&15u;
            before[k]=(0x81234560u^(k*0x9e3779b0u))|low;
        }
        // The final dword proves the whole declared bank is considered. Any
        // lowNibble13 blocks a blanket clear; its native depth is not known.
        if(preserved_tile)before.back()=0x8123456du;
    };
    FillMetadata(false);std::memcpy(words,before.data(),MetadataBytes);
    const u32 guard_before=0x186534a9u,guard_after=0xab431967u;
    std::memcpy(reinterpret_cast<void*>(metadata-4),&guard_before,4);
    std::memcpy(reinterpret_cast<void*>(metadata+MetadataBytes),&guard_after,4);
    RegFile draw;
    draw[Reg::SPI_SHADER_PGM_LO_VS]=u32((address+8192)>>8);draw[Reg::SPI_SHADER_PGM_HI_VS]=u32((address+8192)>>40);
    draw[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;draw[Reg::VGT_PRIMITIVE_TYPE]=4;
    draw[Reg::DB_Z_INFO]=3u|(1u<<29);
    draw[Reg::DB_Z_READ_BASE]=draw[Reg::DB_Z_WRITE_BASE]=u32(depth>>8);
    draw[Reg::DB_HTILE_DATA_BASE]=u32(metadata>>8);draw[Reg::DB_DEPTH_SIZE]=511u|(511u<<11);
    draw[Reg::DB_DEPTH_CONTROL]=0x16; // Z enabled, writes, LESS. Native clear stays1.
    draw[Reg::DB_DEPTH_CLEAR]=std::bit_cast<u32>(1.f);draw[Reg::PA_CL_VTE_CNTL]=0x43f;
    const std::array<float,6> viewport{2048,2048,-2048,2048,1,0};
    for(unsigned k=0;k<viewport.size();++k)draw[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<u32>(viewport[k]);
    for(const auto reg:{Reg::PA_SC_SCREEN_SCISSOR_BR,Reg::PA_SC_WINDOW_SCISSOR_BR,Reg::PA_SC_GENERIC_SCISSOR_BR})
        draw[reg]=Atlas|(Atlas<<16);
    RegFile compute;compute[Reg::COMPUTE_PGM_LO]=u32(cs_address>>8);compute[Reg::COMPUTE_PGM_HI]=u32(cs_address>>40);
    compute[Reg::COMPUTE_PGM_RSRC2]=Rsrc2;compute[Reg::COMPUTE_NUM_THREAD_X]=64;
    compute[Reg::COMPUTE_NUM_THREAD_Y]=compute[Reg::COMPUTE_NUM_THREAD_Z]=1;
    const auto metadata_descriptor=Descriptor(metadata,4,MetadataWords,0x24004u,0x3000u);
    const auto leaf_descriptor=Descriptor(leaf,16,1,0x77facu,0x8000u);
    for(unsigned k=0;k<4;++k) {
        compute[Reg::COMPUTE_USER_DATA_0+4+k]=metadata_descriptor[k];
        compute[Reg::COMPUTE_USER_DATA_0+8+k]=leaf_descriptor[k];
    }
    Shader::BuildInput input{.stage=Shader::Stage::Compute,.user_data=compute.UserData(Reg::COMPUTE_USER_DATA_0),
        .num_user_sgprs=12,.vgpr_comp_cnt=0,.code=reinterpret_cast<const u32*>(cs_address)};
    input.compute_rsrc2=Rsrc2;const auto decoded=Shader::BuildProgram(input);
    Require(decoded && decoded->hash==ShaderHash && decoded->unsupported.empty() &&
        decoded->compute_rsrc2==Rsrc2 && decoded->num_user_sgprs==12,"Original CS/register ABI proof failed");
    Gpu::Metal::Device device;Renderer renderer(device);
    const auto Dispatch=[&] {
        if(gpu_enabled || gpu_disabled_control)Require(renderer.DmaData(0,metadata+MetadataBytes-4,2,before.back(),4),
            "Cannot encode pending same-queue final metadata writer");
        renderer.Dispatch(compute,4096,1,1);
    };
    // This fixture owns all synthetic memory, installs no deferred CP stores,
    // and never mutates the scalar leaf. Renderer GPU/callback/map proofs still
    // run; the nonempty CP predicate models the absence of pending CP writes.
    renderer.SetCpuReadGuard([](GuestAddr,std::size_t){return true;});
    draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
    renderer.Draw(draw,DrawCall{.count=3});
    CheckDepth(renderer,device,depth,metadata,.2f,"first pass");
    Dispatch();
    draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
    renderer.Draw(draw,DrawCall{.count=3});
    // Checkpoint drains both commands, but reads only actual native depth.
    // Without the recurring-clear notification LESS retains stale .2 here.
    const bool expect_clear=proof_enabled && !gpu_disabled_control;
    CheckDepth(renderer,device,depth,metadata,expect_clear ? .8f : .2f,
        expect_clear ? "after original HTILE RMW clear" : "disabled proof/candidate must reject native clear");
    const auto CheckMetadata=[&](unsigned expected_preserved,bool uniform_unchanged=true,std::optional<u32> last_override={}) {
        unsigned preserved=0,replaced=0;
        for(unsigned k=0;k<MetadataWords;++k) {
            const bool keep=(before[k]&15u)==13u;
            const u32 expected=last_override && k==MetadataWords-1 ? *last_override : keep ? before[k] : replacement;
            if(words[k]!=expected) {
                char message[192];std::snprintf(message,sizeof(message),"Original HTILE RMW word%u: expected%08x got%08x (lowNibble13=%d)",
                    k,expected,words[k],keep);throw std::runtime_error(message);
            }
            if(keep)++preserved;else ++replaced;
        }
        Require(preserved==expected_preserved && replaced==MetadataWords-preserved,
            "Metadata RMW replacement/preservation coverage was empty");
        u32 actual_before=0,actual_after=0;
        std::memcpy(&actual_before,reinterpret_cast<const void*>(metadata-4),4);
        std::memcpy(&actual_after,reinterpret_cast<const void*>(metadata+MetadataBytes),4);
        Require(actual_before==guard_before && actual_after==guard_after,"Original CS escaped its complete declared metadata range");
        if(uniform_unchanged)Require(!std::memcmp(reinterpret_cast<const void*>(leaf),constants.data(),sizeof(constants)),
            "Original CS modified its uniform leaf");
    };
    CheckMetadata(0);

    // Conservative negative: original lowNibble13 is preserved, with unknown
    // depth semantics. The original CS must execute, but full native clear
    // must be rejected even when13 occurs only in the LAST metadata word.
    FillMetadata(true);renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
    draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
    draw[Reg::DB_DEPTH_CONTROL]=0x76; // ALWAYS + writes seeds the same target .2.
    renderer.Draw(draw,DrawCall{.count=3});
    CheckDepth(renderer,device,depth,metadata,.2f,"lowNibble13 negative seed");
    Dispatch();
    draw[Reg::DB_DEPTH_CONTROL]=0x16;
    draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
    renderer.Draw(draw,DrawCall{.count=3});
    CheckDepth(renderer,device,depth,metadata,.2f,"lowNibble13 must reject blanket native clear");
    CheckMetadata(1);

    if(lease_enabled) {
        Require(proof_enabled && alias,"Lease case requires all proof modes and a physical alias");
        const auto metal=(__bridge id<MTLDevice>)device.Handle();
        const auto queue=(__bridge id<MTLCommandQueue>)device.Queue();
        const auto WaitForConsumer=[&](u64 destination,u32 changed,const char* label) {
            auto gate=[metal newSharedEvent];Require(gate!=nil,"Cannot create fixture queue gate");
            struct ReleaseGate {__strong id<MTLSharedEvent> gate;~ReleaseGate(){gate.signaledValue=1;}} release{gate};
            auto blocked=[queue commandBuffer];Require(blocked!=nil,"Cannot create fixture gate command");
            [blocked encodeWaitForEvent:gate value:1];
            std::atomic<bool> timed_out{false};
            // Start before committing a queue wait: construction failure must
            // never strand GPU teardown. The scope guard also releases it.
            std::jthread watchdog([gate,&timed_out](std::stop_token stop) {
                for(unsigned k=0;k<1000 && !stop.stop_requested();++k)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                if(!stop.stop_requested()){timed_out=true;gate.signaledValue=1;}
            });
            [blocked commit];
            // All previous renderer commands were drained by CheckDepth.
            // Thus a correct ticket belongs to THIS actual consumer command,
            // not to an older completed command or an eligibility-only plan.
            Dispatch();
            const auto waits=renderer.IndirectCpuWriteWaitCount();
            std::atomic<bool> entered{false},finished{false};
            std::jthread writer([&] {
                entered.store(true,std::memory_order_release);
                renderer.CpuWrite(destination,&changed,sizeof(changed),false);
                finished.store(true,std::memory_order_release);
            });
            const auto entry_deadline=Clock::now()+std::chrono::seconds(2);
            while(!entered.load(std::memory_order_acquire) && Clock::now()<entry_deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const bool blocked_while_closed=entered.load(std::memory_order_acquire) && !finished.load(std::memory_order_acquire);
            gate.signaledValue=1;writer.join();watchdog.request_stop();watchdog.join();
            Require(!timed_out.load() && blocked_while_closed,std::string(label)+" CPU alias write overtook the blocked original CS");
            Require(renderer.IndirectCpuWriteWaitCount()==waits+1,std::string(label)+" did not wait on an actual consuming-command read ticket");
            u32 observed=0;std::memcpy(&observed,reinterpret_cast<const void*>(destination),sizeof(observed));
            Require(observed==changed,std::string(label)+" CPU alias mutation did not execute after its wait");
            Require(blocked.status==MTLCommandBufferStatusCompleted && !blocked.error,"Fixture queue gate failed");
        };
        const auto Seed=[&] {
            FillMetadata(false);renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
            renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
            draw[Reg::DB_DEPTH_CONTROL]=0x76;renderer.Draw(draw,DrawCall{.count=3});
            CheckDepth(renderer,device,depth,metadata,.2f,"lease case seed");
        };
        Seed();
        // If the leaf is changed before the CS consumes it, every output is
        // FFFC000F instead ofFFF0. The physical-alias wait and full output
        // oracle together catch a missing/stale/wrong-command uniform lease.
        WaitForConsumer(alias+4096+4,0xfffc000fu,"uniform8B");
        CheckMetadata(0,false);
        draw[Reg::DB_DEPTH_CONTROL]=0x16;
        draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
        renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.8f,"leased original clear cannot be retrofitted by later leaf edit");
        Seed();
        WaitForConsumer(alias+MetadataOffset+MetadataBytes-4,0x8123456du,"metadata1MiB");
        CheckMetadata(0,true,0x8123456du);
        // Do not infer post-write tile13 depth semantics. Only the ordered CS
        // output, completed alias mutation, and full-range read-ticket wait
        // are asserted here; native clear behavior was checked above.
        if(gpu_enabled) {
            Seed();std::uintptr_t extra_alias=0;
            Require(!runtime_gpu_memory_map(&extra_alias,AllocationBytes,3,0,physical,16384),"Cannot create unrelated mapping for rebound lease");
            WaitForConsumer(alias+DepthOffset+u64(Atlas)*Atlas*4-4,0x3f123456u,"rebound depth64MiB");
            CheckMetadata(0);
        }
        std::puts("Original HTILE read leases: blocked consuming command protects uniform8B and complete1MiB metadata through physical aliases");
    }
    if(gpu_enabled) {
        // The source uniform can also have a queued GPU writer. Its value
        // must be consumed on the GPU, never speculatively copied on the CPU.
        FillMetadata(false);renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
        renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
        draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
        draw[Reg::DB_DEPTH_CONTROL]=0x76;renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"GPU uniform rejection seed");
        Require(renderer.DmaData(0,leaf+4,2,0xfffc000fu,4),"Cannot encode pending uniform writer");
        Dispatch();draw[Reg::DB_DEPTH_CONTROL]=0x16;
        draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
        renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"GPU invalid uniform must suppress native clear");
        for(unsigned k=0;k<MetadataWords;++k)Require(words[k]==0xfffc000fu,"GPU invalid-uniform fallback skipped original RMW");
        renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
        renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
        Require(renderer.DmaData(0,leaf,2,MetadataWords-1,4),"Cannot encode pending uniform count writer");
        Dispatch();renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"GPU partial uniform count must suppress native clear");
        for(unsigned k=0;k<MetadataWords;++k)Require(words[k]==(k==MetadataWords-1 ? before[k] : ObservedReplacement),
            "GPU partial-count fallback changed original RMW bounds");
        // The same queued GPU writer does not authorize a read over a pending
        // CP-side write. Only the exact source-overlap check is rejected, so
        // the original shader still executes and its output remains testable.
        renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
        renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
        renderer.SetCpuReadGuard([&](GuestAddr at,std::size_t bytes) {
            return at>=metadata+MetadataBytes || at+bytes<=metadata;
        });
        Dispatch();renderer.SetCpuReadGuard([](GuestAddr,std::size_t){return true;});
        renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"pending CP input must reject GPU clear candidate");
        CheckMetadata(0);
        // Unrelated map edits can recover the observed association only after
        // proving the complete metadata+depth physical spans and VMA lifetime
        // identities still refer to this exact target/texture.
        renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
        renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
        std::uintptr_t later_alias=0;
        Require(!runtime_gpu_memory_map(&later_alias,AllocationBytes,3,0,physical,16384),"Cannot change fixture map generation");
        Dispatch();renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.8f,"unchanged complete association must recover after unrelated map change");
        CheckMetadata(0);
        for(bool pending_gpu:{false,true})for(bool physical_alias:{false,true}) {
            FillMetadata(false);before[0]=MetadataWords;before[1]=ObservedReplacement;
            renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
            draw[Reg::DB_DEPTH_CONTROL]=0x76;renderer.Draw(draw,DrawCall{.count=3});
            CheckDepth(renderer,device,depth,metadata,.2f,"input-alias negative seed");
            const auto alias_leaf=Descriptor(physical_alias ? alias+MetadataOffset : metadata,16,1,0x77facu,0x8000u);
            for(unsigned k=0;k<4;++k)compute[Reg::COMPUTE_USER_DATA_0+8+k]=alias_leaf[k];
            if(pending_gpu)Dispatch();else renderer.Dispatch(compute,4096,1,1);
            draw[Reg::DB_DEPTH_CONTROL]=0x16;
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
            renderer.Draw(draw,DrawCall{.count=3});
            CheckDepth(renderer,device,depth,metadata,.2f,physical_alias ? "physical input alias must reject native clear" : "direct input alias must reject native clear");
            CheckMetadata(0);
        }
        for(unsigned k=0;k<4;++k)compute[Reg::COMPUTE_USER_DATA_0+8+k]=leaf_descriptor[k];
        constexpr std::array<u32,7> pixel_code{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
        renderer.CpuWrite(address+12288,pixel_code.data(),sizeof(pixel_code),false);
        for(bool physical_alias:{false,true}) {
            FillMetadata(true);renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
            renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
            draw[Reg::DB_DEPTH_CONTROL]=0x76;renderer.Draw(draw,DrawCall{.count=3});
            CheckDepth(renderer,device,depth,metadata,.2f,"newer-colour-alias negative seed");
            const u64 color_address=physical_alias ? alias+DepthOffset : depth;
            auto color=draw;color[Reg::DB_Z_INFO]=color[Reg::DB_DEPTH_CONTROL]=0;
            color[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=u32(color_address>>8);
            color[Reg::CB_COLOR0_BASE+Reg::CB_PITCH]=Atlas/8-1;
            color[Reg::CB_COLOR0_BASE+Reg::CB_SLICE]=Atlas*Atlas/64-1;
            color[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);
            color[Reg::CB_COLOR_CONTROL]=1u<<4;color[Reg::CB_TARGET_MASK]=color[Reg::CB_SHADER_MASK]=15;
            color[Reg::SPI_SHADER_COL_FORMAT]=9;
            color[Reg::SPI_SHADER_PGM_LO_PS]=u32((address+12288)>>8);color[Reg::SPI_SHADER_PGM_HI_PS]=u32((address+12288)>>40);
            renderer.Draw(color,DrawCall{.count=3});
            const auto versions=TargetVersions(renderer,depth,color_address);
            Require(versions[1]>versions[0],"Colour alias did not become the newer owner");
            Dispatch();
            Require(TargetVersions(renderer,depth,color_address)==versions,"Suppressed conditional clear promoted old depth over newer colour alias");
            CheckDepth(renderer,device,depth,metadata,.2f,"newer colour alias must preserve native depth and owner");
            CheckMetadata(1);
        }
        FillMetadata(false);
        const auto unmap=reinterpret_cast<int(*)(void*,u64)>(runtime_memory_resolve("cQke9UuBQOk#p#J"));
        Require(unmap,"Cannot resolve fixture unmap");
        u64 new_physical=0;
        Require(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),AllocationBytes,16384,0,&new_physical),"Cannot allocate alternate fixture backing");
        const auto SeedDepth=[&] {
            renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
            renderer.CpuWrite(leaf,constants.data(),sizeof(constants),false);
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32(address>>32);
            draw[Reg::DB_DEPTH_CONTROL]=0x76;renderer.Draw(draw,DrawCall{.count=3});
            CheckDepth(renderer,device,depth,metadata,.2f,"mapping rejection seed");
            draw[Reg::DB_DEPTH_CONTROL]=0x16;
            draw[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+64);draw[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+64)>>32);
        };
        const auto Remap=[&](u64 destination,u64 bytes,u64 physical_at) {
            Require(!unmap(reinterpret_cast<void*>(destination),bytes),"Cannot unmap fixture subrange");
            std::uintptr_t fixed=destination;
            Require(!runtime_gpu_memory_map(&fixed,bytes,3,0x10,physical_at,16384) && fixed==destination,"Cannot replace fixture subrange mapping");
        };
        SeedDepth();Remap(metadata,MetadataBytes,new_physical+MetadataOffset);
        renderer.CpuWrite(metadata,before.data(),MetadataBytes,false);
        Dispatch();renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"changed metadata physical backing must reject rebound");CheckMetadata(0);
        SeedDepth();Remap(depth,u64(Atlas)*Atlas*4,new_physical+DepthOffset);
        Dispatch();renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"changed depth physical backing must reject rebound");CheckMetadata(0);
        SeedDepth();Remap(metadata,MetadataBytes,new_physical+MetadataOffset);
        Dispatch();renderer.Draw(draw,DrawCall{.count=3});
        CheckDepth(renderer,device,depth,metadata,.2f,"same physical address with new VMA identity must reject ABA rebound");CheckMetadata(0);
        std::puts("GPU conditional HTILE: pre-RMW inputs, invalid count/value/final-word13, CPU/GPU direct/physical input aliases, newer colour ownership, complete leases, CP guard, unchanged-binding recovery and metadata/depth/ABA rejection passed");
    }
    renderer.SetCpuReadGuard({});
    std::printf("Original HTILE CS:4096D32 %s; final-word13 rejects blanket clear; all262144 RMW words, guards and uniform leaf checked (clear=%08x).\n",
        expect_clear ? ".2->.8 only for full clear" : "disabled proof/candidate keeps .2",replacement);
}
} // namespace
int main(int argc,char** argv) {
    if(argc==1){std::puts("Private original HTILE CS required; skipped");return 77;}
    if(argc!=2 && argc!=3){std::fprintf(stderr,"usage:%s /absolute/path/to/original-CS.bin [--proof-off|--lease|--gpu|--gpu-off|0xfffffff0]\n",argv[0]);return 2;}
    u32 replacement=ObservedReplacement;bool proof_enabled=true,lease_enabled=false,gpu_enabled=false,gpu_disabled_control=false;
    if(argc==3) {
        if(!std::strcmp(argv[2],"--proof-off"))proof_enabled=false;
        else if(!std::strcmp(argv[2],"--lease"))lease_enabled=true;
        else if(!std::strcmp(argv[2],"--gpu")){gpu_enabled=true;lease_enabled=true;}
        else if(!std::strcmp(argv[2],"--gpu-off"))gpu_disabled_control=true;
        else {
            char* end=nullptr;const auto parsed=std::strtoull(argv[2],&end,0);
            if(!argv[2][0] || !end || *end || parsed!=ObservedReplacement)return 2;
            replacement=u32(parsed);
        }
    }
    @autoreleasepool {
        if(!MTLCreateSystemDefaultDevice())return 77;
        try {Run(argv[1],replacement,proof_enabled,lease_enabled,gpu_enabled,gpu_disabled_control);return 0;}
        catch(const std::exception& error){std::fprintf(stderr,"Shadow HTILE regression:%s\n",error.what());return 1;}
    }
}
