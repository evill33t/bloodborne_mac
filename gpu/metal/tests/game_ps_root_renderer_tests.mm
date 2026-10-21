// SPDX-License-Identifier: GPL-2.0-or-later
// Optional original-capture renderer integration. Never opens game/cache files for writing.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/metal/direct_root_read.h"
#include "gpu/metal/raw_texture_snapshot.h"
#include "gpu/metal/static_smrd.h"
#include "gpu/metal/tests/root_lease_fixture.h"
#include "gpu/liverpool/registers.h"
#include "gpu/liverpool/command_processor.h"
#include "gpu/shader/program.h"
#include "gpu/shader/msl.h"
#include "src/runtime_gpu_memory.h"
extern "C" {
#include "src/runtime.h"
}
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
extern "C" void runtime_restart() {std::abort();}

namespace {
namespace Shader=Gpu::Shader;
namespace Raw=Gpu::Metal::RawTextureSnapshot;
using Renderer=Gpu::Metal::MetalRenderer;
using namespace Gpu;
using namespace Gpu::Liverpool;
using Clock=std::chrono::steady_clock;
constexpr u64 MemoryBytes=2u<<20,Root2=0x40000,Root12=0x44000,Payload=0x50000,
    Texture=0x70000,Alternative=0x90000,Probe=0x180000,Position=0x1b0000,
    PixelCode=0x1c0000,VertexCode=0x1d0000;
constexpr std::array<u64,2> Hashes{0x68fcb859536adcd4ull,0xaff190d9945a53e0ull};
void Require(bool value,const std::string& message) {if(!value)throw std::runtime_error(message);}
auto Deadline() {return Clock::now()+std::chrono::seconds(15);}
void Drain(Renderer& renderer) {std::string error;Require(renderer.WaitIdleUntil(Deadline(),error),"Renderer drain: "+error);}
void Pointer(std::array<u32,16>& ud,unsigned slot,u64 value) {ud[slot]=u32(value);ud[slot+1]=u32(value>>32);}
struct PrivateData {
    std::filesystem::path directory;
    PrivateData() {
        const char* temporary=std::getenv("TMPDIR");
        std::string name=std::string(temporary ? temporary : "/private/tmp")+"/bb-ps-root-renderer-XXXXXX";
        std::vector<char> bytes(name.begin(),name.end());bytes.push_back(0);
        auto* path=mkdtemp(bytes.data());Require(path,"Cannot create fixture-owned data directory");directory=path;
        setenv("BB_DATA_DIR",path,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");
        unsetenv("BB_METAL_CACHE_DIR");setenv("BB_METAL_PIPELINE_CACHE","0",1);
        setenv("BB_METAL_SHADER_PREWARM","0",1);unsetenv("DC_GPU_PROFILE");unsetenv("DC_SERIAL");
        unsetenv("BB_METAL_COMMAND_TIMELINE_CSV");unsetenv("BB_METAL_STAGE_COUNTER_CSV");
        unsetenv("BB_METAL_STUTTER_TRACE");unsetenv("BB_METAL_FRAME_WORK_CSV");
        for(const char* key:{"BB_METAL_CAPTURE_DIR","BB_METAL_CAPTURE_TEXTURE","DC_DUMP_SHADERS",
            "DC_DUMP_FRAMES","DC_TRACE_FILE","DC_DUMP_PASSES","DC_DUMP_TEXTURE","DC_PROBE",
            "DC_DEBUG_VSOUT","DC_DEBUG_FLAT","DC_DEBUG_CLEAR","DC_TRACE_CB","DC_DUMP_CS"})unsetenv(key);
        setenv("BB_METAL_NATIVE_INDIRECT","1",1);setenv("BB_METAL_NATIVE_TEXTURE_COHERENCE","1",1);
        setenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT","1",1);
        setenv("BB_METAL_STATIC_SMRD","0",1);setenv("BB_METAL_SIMD_SMRD","0",1);
        setenv("BB_METAL_DIRECT_VERTEX_ROOT","0",1);setenv("BB_METAL_TEXTURE_JOURNAL_PAGE_INDEX","0",1);
    }
    ~PrivateData(){std::error_code error;std::filesystem::remove_all(directory,error);}
};
std::vector<u32> Original(const std::filesystem::path& directory,u64 hash) {
    char suffix[32];std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path selected;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if(entry.is_regular_file() && name.starts_with("ps_") && name.ends_with(suffix)) {selected=entry.path();break;}
    }
    Require(!selected.empty(),"Missing original PS capture");const auto bytes=std::filesystem::file_size(selected);
    Require(bytes>=8 && bytes<=65536 && !(bytes&3),"Invalid original PS capture length");
    std::vector<u32> code(bytes/4+64,0xbf810000u);std::ifstream stream(selected,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(code.data()),bytes)),"Cannot read original PS capture");return code;
}
std::vector<u32> Vertex() {
    using namespace Shader::Gcn;
    // Existing public full-screen position fetch, then six explicit constant parameters.
    std::vector<u32> words{0xc0820100,0xe00c2000,0x80010100};
    constexpr float parameters[6][4]{{.3f,.4f,.8f,1},{.2f,.5f,.7f,1},{.4f,.1f,.9f,1},
        {.35f,.65f,.5f,1},{.1f,.2f,.3f,1},{.6f,.4f,.2f,1}};
    for(unsigned k=0;k<6;++k)for(unsigned j=0;j<4;++j) {
        words.push_back(0x7e000000u|((8+4*k+j)<<17)|(1u<<9)|Operand::Literal);
        words.push_back(std::bit_cast<u32>(parameters[k][j]));
    }
    const auto literal=[&](Vop op,u32 destination,u32 rhs,float value) {
        words.push_back(((u32(op)-256)<<25)|(destination<<17)|(rhs<<9)|Operand::Literal);
        words.push_back(std::bit_cast<u32>(value));
    };
    // Match the existing original-material differential's spatial UV inputs;
    // derivatives of constant parameters would not exercise material quad math.
    for(unsigned axis=0;axis<2;++axis) {
        literal(Vop::V_MUL_F32,32+axis,1+axis,.5f);
        literal(Vop::V_ADD_F32,32+axis,32+axis,.5f);
    }
    for(const auto& [parameter,scale]:std::array<std::pair<u32,float>,3>{{{2,.375f},{3,.625f},{5,.25f}}})
        for(unsigned axis=0;axis<2;++axis) {
            literal(Vop::V_MUL_F32,34,32+axis,scale);
            const u32 destination=8+4*parameter+axis;
            words.push_back(((u32(Vop::V_ADD_F32)-256)<<25)|(destination<<17)|(34u<<9)|
                (u32(Operand::Vgpr0)+destination));
        }
    words.insert(words.end(),{0xf80008cf,0x04030201});
    for(unsigned k=0;k<6;++k) {
        words.push_back(0xf800000fu|((u32(ExpTarget::Param0)+k)<<4));
        words.push_back((8+4*k)|((9+4*k)<<8)|((10+4*k)<<16)|((11+4*k)<<24));
    }
    words.push_back(0xbf810000u);return words;
}
struct Material {
    u64 physical=0,address=0,alias=0;
    std::array<u32,16> ud{};
    std::unique_ptr<Shader::Program> program;
    std::array<u64,6> targets{};
    unsigned metadata_sgprs=0;
    Material(const std::filesystem::path& captures,u64 hash) {
        Require(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),MemoryBytes,16384,0,&physical),"Cannot allocate fixture memory");
        uintptr_t first=0,second=0;
        Require(!runtime_gpu_memory_map(&first,MemoryBytes,3,0,physical,16384) &&
            !runtime_gpu_memory_map(&second,MemoryBytes,3,0,physical,16384) && first!=second,"Cannot map fixture aliases");
        address=first;alias=second;std::memset(reinterpret_cast<void*>(address),0,MemoryBytes);
        const auto original=Original(captures,hash);std::memcpy(reinterpret_cast<void*>(address+PixelCode),original.data(),original.size()*4);
        Pointer(ud,2,address+Root2);Pointer(ud,12,address+Root12);
        for(unsigned sgprs=0;sgprs<=16 && !program;++sgprs)for(unsigned components=0;components<4 && !program;++components) {
            auto candidate=Shader::BuildProgram({.stage=Shader::Stage::Fragment,.address=address+PixelCode,
                .user_data=ud.data(),.num_user_sgprs=sgprs,.vgpr_comp_cnt=components,.code=original.data()});
            if(candidate && candidate->hash==hash) {metadata_sgprs=sgprs;program=std::move(candidate);}
        }
        Require(program && Shader::FragmentStaticSmrdEligible(*program) && Shader::GuestMemoryReadOnlyEligible(*program),
            "Original PS identity/static/no-writer proof failed");
        for(unsigned k=0;k<12;++k) {
            const u64 base=address+Payload+k*4096;
            const u32 descriptor[]{u32(base),u32(base>>32)|(16u<<16),256,0xfacu|(7u<<12)|(14u<<15)};
            std::memcpy(reinterpret_cast<void*>(address+Root2+k*16),descriptor,sizeof(descriptor));
            for(unsigned word=0;word<1024;++word) {
                reinterpret_cast<u32*>(base)[word]=std::bit_cast<u32>(.25f);
                reinterpret_cast<u32*>(address+Alternative+k*4096)[word]=std::bit_cast<u32>(.75f);
            }
        }
        std::fill_n(reinterpret_cast<u32*>(address+Root12),1024,std::bit_cast<u32>(.25f));
        const u32 texture[]{u32((address+Texture)>>8),u32((address+Texture)>>40)|(10u<<20),
            7u|(7u<<14),0x90800fac,15u<<13,0,0,0};
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<16;++x)
            reinterpret_cast<u32*>(address+Texture)[y*16+x]=0xff604080u+(x&1u)*0x00081020u;
        std::map<u64,u32> assigned;
        const auto store=[&](const Shader::SgprSource& source,u32 value) {
            Require(source.ud<16,"Invalid descriptor provenance SGPR");
            if(source.kind==Shader::SgprSource::UserData) {ud[source.ud]=value;return;}
            Require(source.ud<15 && (source.kind==Shader::SgprSource::PointerLoad ||
                source.kind==Shader::SgprSource::BufferLoad),"Unknown material descriptor provenance");
            const u64 base=source.kind==Shader::SgprSource::PointerLoad ?
                (u64(ud[source.ud])|(u64(ud[source.ud+1])<<32))&0xffffffffffffull :
                u64(ud[source.ud])|(u64(ud[source.ud+1]&0xfff)<<32);
            const u64 destination=(base+source.offset)&~u64(3);
            Require(destination>=address && destination-address<=MemoryBytes-4,"Material provenance escaped fixture memory");
            auto [it,inserted]=assigned.emplace(destination,value);
            Require(inserted || it->second==value,"Conflicting material descriptor provenance");
            std::memcpy(reinterpret_cast<void*>(destination),&value,4);
        };
        for(const auto& image:program->images)for(unsigned k=0;k<8;++k)store(image.tsharp[k],texture[k]);
        const std::array<u32,4> sampler{};
        for(const auto& sources:program->samplers)for(unsigned k=0;k<4;++k)store(sources[k],sampler[k]);
        for(const auto& image:program->images)for(unsigned k=0;k<8;++k)
            Require(Shader::ResolveSgpr(image.tsharp[k],ud.data())==texture[k],"Material T# provenance did not round-trip");
        for(const auto& sources:program->samplers)for(unsigned k=0;k<4;++k)
            Require(Shader::ResolveSgpr(sources[k],ud.data())==sampler[k],"Material S# provenance did not round-trip");
        Require((u64(ud[2])|(u64(ud[3])<<32))==address+Root2 &&
            (u64(ud[12])|(u64(ud[13])<<32))==address+Root12,"Resource construction changed root pointers");
        const auto vertex=Vertex();std::memcpy(reinterpret_cast<void*>(address+VertexCode),vertex.data(),vertex.size()*4);
        const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};
        std::memcpy(reinterpret_cast<void*>(address+Position+256),vertices,sizeof(vertices));
        const u64 positions=address+Position+256;
        const u32 descriptor[]{u32(positions),u32(positions>>32)|(16u<<16),3,0xfacu|(7u<<12)|(14u<<15)};
        std::memcpy(reinterpret_cast<void*>(address+Position),descriptor,sizeof(descriptor));
        std::array<u32,16> vud{};Pointer(vud,0,address+Position);
        auto vertex_program=Shader::BuildProgram({.stage=Shader::Stage::Vertex,.address=address+VertexCode,
            .user_data=vud.data(),.num_user_sgprs=2,.vgpr_comp_cnt=0,.code=vertex.data()});
        Require(vertex_program && vertex_program->exports_position && vertex_program->param_mask==63 &&
            Shader::GuestMemoryReadOnlyEligible(*vertex_program),"Synthetic six-parameter VS/no-writer proof failed");
        for(unsigned k=0;k<6;++k)targets[k]=address+0x10000+k*16384;
    }
    RegFile Registers(bool alternate=false) const {
        RegFile r;r[Reg::SPI_SHADER_PGM_LO_VS]=u32((address+VertexCode)>>8);
        r[Reg::SPI_SHADER_PGM_LO_VS+1]=u32((address+VertexCode)>>40);
        r[Reg::SPI_SHADER_PGM_LO_PS]=u32((address+PixelCode)>>8);
        r[Reg::SPI_SHADER_PGM_LO_PS+1]=u32((address+PixelCode)>>40);
        r[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;r[Reg::SPI_SHADER_PGM_RSRC2_PS]=metadata_sgprs<<1;
        r[Reg::SPI_SHADER_USER_DATA_VS_0]=u32(address+Position);r[Reg::SPI_SHADER_USER_DATA_VS_0+1]=u32((address+Position)>>32);
        for(unsigned k=0;k<16;++k)r[Reg::SPI_SHADER_USER_DATA_PS_0+k]=ud[k];
        r[Reg::VGT_PRIMITIVE_TYPE]=4;r[Reg::CB_COLOR_CONTROL]=1u<<4;
        r[Reg::CB_TARGET_MASK]=r[Reg::CB_SHADER_MASK]=0xffffff;r[Reg::SPI_SHADER_COL_FORMAT]=0x444444;
        r[Reg::SPI_PS_INPUT_ADDR]=0x1001;
        for(unsigned k=0;k<32;++k)r[Reg::SPI_PS_INPUT_CNTL_0+k]=k;
        for(unsigned k=0;k<6;++k) {
            const auto reg=Reg::CB_COLOR0_BASE+k*Reg::kColorTargetStride;
            r[reg+Reg::CB_BASE]=u32((targets[k]+(alternate ? 0x100000 : 0))>>8);
            r[reg+Reg::CB_INFO]=(ColorFormat::C16_16_16_16<<2)|(NumFormat::Float<<8);
        }
        r[Reg::PA_CL_VTE_CNTL]=0x43f;const float viewport[]{4,4,-4,4,1,0};
        for(unsigned k=0;k<6;++k)r[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<u32>(viewport[k]);
        r[Reg::PA_SC_SCREEN_SCISSOR_BR]=r[Reg::PA_SC_WINDOW_SCISSOR_BR]=r[Reg::PA_SC_GENERIC_SCISSOR_BR]=8u|(8u<<16);
        return r;
    }
};
using Pixels=std::array<std::vector<u16>,6>;
u32 Word(std::span<const u8> bytes,size_t at) {Require(at<=bytes.size() && bytes.size()-at>=4,"Short checkpoint metadata");u32 v=0;for(unsigned k=0;k<4;++k)v|=u32(bytes[at+k])<<(8*k);return v;}
u64 Qword(std::span<const u8> bytes,size_t at) {return u64(Word(bytes,at))|(u64(Word(bytes,at+4))<<32);}
Pixels ReadMrt(Renderer& renderer,Gpu::Metal::Device& device,const Material& material,bool alternate=false) {
    std::string error;auto capture=renderer.CaptureCheckpoint(Deadline(),error);Require(bool(capture),"Checkpoint capture: "+error);
    const auto& chunks=capture->Chunks();
    const auto metadata=std::find_if(chunks.begin(),chunks.end(),[](const auto& chunk){return chunk.id==0x1000;});
    Require(metadata!=chunks.end(),"Missing checkpoint metadata");std::vector<u8> bytes(metadata->size);
    Require(metadata->read(0,bytes,error),"Checkpoint metadata read: "+error);
    Require(bytes.size()>=116 && !std::memcmp(bytes.data(),"BBMTRL1",8) && Word(bytes,8)==1,"Unsupported checkpoint fixture format");
    const u32 count=Word(bytes,92);Require(count<=128 && 116+u64(count)*112<=bytes.size(),"Invalid checkpoint target count");
    Pixels result;std::array<bool,6> found{};
    for(u32 target=0;target<count;++target) {
        const size_t at=116+target*112;const u64 address=Qword(bytes,at);
        for(unsigned k=0;k<6;++k)if(address==material.targets[k]+(alternate ? 0x100000 : 0)) {
            Raw::Descriptor description{Word(bytes,at+36),Word(bytes,at+40),Word(bytes,at+44),Word(bytes,at+48),
                Word(bytes,at+52),Word(bytes,at+56),Word(bytes,at+60)};
            Require(!found[k] && description.format==MTLPixelFormatRGBA16Float && description.width==8 &&
                description.height==8 && description.levels==1 && description.layers==1,"Unexpected MRT format/shape");
            std::vector<Raw::Plane> planes;Require(Raw::Layout(device.Handle(),description,planes,error) && planes.size()==1,"MRT layout: "+error);
            const auto id=0x100000000ull+(u64(target)<<16);
            const auto plane=std::find_if(chunks.begin(),chunks.end(),[&](const auto& chunk){return chunk.id==id;});
            Require(plane!=chunks.end() && plane->size==planes[0].size,"Missing MRT plane");
            std::vector<u8> raw(plane->size);Require(plane->read(0,raw,error),"MRT raw read: "+error);
            result[k].resize(8*8*4);
            for(unsigned y=0;y<8;++y)std::memcpy(result[k].data()+y*8*4,raw.data()+y*planes[0].row_bytes,8*8);
            found[k]=true;
        }
    }
    Require(std::all_of(found.begin(),found.end(),[](bool value){return value;}),"Not all six original MRTs were read");return result;
}
bool Nan(u16 value){return (value&0x7c00u)==0x7c00u && (value&0x3ffu);}
void Equal(const Pixels& a,const Pixels& b) {
    unsigned total_nonzero=0;
    for(unsigned target=0;target<6;++target) {
        Require(a[target].size()==b[target].size(),"MRT sizes differ");unsigned finite=0;
        for(size_t k=0;k<a[target].size();++k) {
            Require(a[target][k]==b[target][k] || (Nan(a[target][k]) && Nan(b[target][k])),"Original renderer MRT mismatch");
            if((a[target][k]&0x7c00u)!=0x7c00u) {++finite;total_nonzero+=(a[target][k]&0x7fffu)!=0;}
        }
        Require(finite>a[target].size()/4,"Original material MRT was not finite");
    }
    Require(total_nonzero>32,"Original material MRTs were empty");
}
bool Different(const Pixels& a,const Pixels& b) {
    for(unsigned target=0;target<6;++target)for(size_t k=0;k<a[target].size();++k)
        if(a[target][k]!=b[target][k] && !Nan(a[target][k]) && !Nan(b[target][k]))return true;
    return false;
}
struct Hooks {
    explicit Hooks(Renderer& renderer) {Require(runtime_gpu_memory_set_mutation_hooks(&renderer,
        [](void* p)->int{return static_cast<Renderer*>(p)->BeginGuestMemoryMutation();},
        [](void* p){static_cast<Renderer*>(p)->EndGuestMemoryMutation();}),"Cannot install fixture mutation hooks");}
    ~Hooks(){if(!runtime_gpu_memory_set_mutation_hooks(nullptr,nullptr,nullptr))std::abort();}
};
u64 Hits(const Renderer& renderer) {return renderer.DirectSmrdRootProofCount(unsigned(Gpu::Metal::DirectRootRead::Reject::None));}
void Run(Gpu::Metal::Device& device,Material& material) {
    const auto regs=material.Registers();Pixels reference;
    setenv("BB_METAL_DIRECT_SMRD","0",1);
    {Renderer baseline(device);baseline.SetCpuReadGuard([](GuestAddr,size_t){return true;});
        baseline.Draw(regs,DrawCall{.count=3});reference=ReadMrt(baseline,device,material);Equal(reference,reference);
        Require(Hits(baseline)==0,"Disabled direct mode published root proof");}
    setenv("BB_METAL_DIRECT_SMRD","1",1);Renderer renderer(device);Hooks hooks(renderer);
    renderer.SetCpuReadGuard([](GuestAddr,size_t){return true;});
    renderer.Draw(regs,DrawCall{.count=3});Equal(reference,ReadMrt(renderer,device,material));
    Require(Hits(renderer)==2,"Original material did not prove both direct roots");
    Require(renderer.DirectSmrdRootProofCount(UINT32_MAX)==0,"Invalid root proof counter index");
    // Actual queued registry and CP alias proof, independently of model guards.
    // The root2 edit preserves its value, keeping ordinary resource setup valid.
    for(const bool cpu_fence:{false,true}) {
        Drain(renderer);RootLeaseFixture::QueueGate gate(device);
        const auto alias=material.alias+Root2;
        const u32 value=*reinterpret_cast<const u32*>(alias);
        std::unique_ptr<CommandProcessor> cp;
        std::vector<u32> fence;
        if(cpu_fence) {
            cp=std::make_unique<CommandProcessor>();cp->SetRenderer(&renderer);
            fence={0xc0044700u,0x14,u32(alias),u32(alias>>32)|(1u<<29),value,0};
            cp->SubmitGraphics({{{fence.data(),fence.size()},{}}});cp->WaitIdle();
        } else {
            renderer.SignalCpuWriteOnly(alias,4,[alias,value]{std::memcpy(reinterpret_cast<void*>(alias),&value,4);});
            renderer.OnSubmit();
        }
        const auto reason=cpu_fence ? Gpu::Metal::DirectRootRead::Reject::CpuPending :
            Gpu::Metal::DirectRootRead::Reject::Callback;
        const auto rejected=renderer.DirectSmrdRootProofCount(unsigned(reason)),hits=Hits(renderer);
        renderer.Draw(regs,DrawCall{.count=3});
        const bool exact_rejection=renderer.DirectSmrdRootProofCount(unsigned(reason))==rejected+1;
        const bool other_root_admitted=Hits(renderer)==hits+1;
        gate.Open();Drain(renderer);
        Require(exact_rejection && other_root_admitted,"Queued actual CP/callback alias did not isolate one-root fallback");
        Require(!cp || !cp->Execution()->Failed(),"Actual CP alias fixture failed");
        cp.reset();renderer.SetCpuReadGuard([](GuestAddr,size_t){return true;});
        Equal(reference,ReadMrt(renderer,device,material));
    }
    // Select a real descriptor-base edit that changes finite original outputs.
    unsigned selected=UINT32_MAX;u32 old_base=0,new_base=0;Pixels changed;
    for(unsigned slot=0;slot<10 && selected==UINT32_MAX;++slot) {
        const u64 word=material.address+Root2+slot*16;
        const u32 before=*reinterpret_cast<const u32*>(word),after=u32(material.address+Alternative+slot*4096);
        renderer.CpuWrite(word,&after,4,false);renderer.Draw(regs,DrawCall{.count=3});auto candidate=ReadMrt(renderer,device,material);
        renderer.CpuWrite(word,&before,4,false);
        if(Different(reference,candidate)) {selected=slot;old_base=before;new_base=after;changed=std::move(candidate);}
    }
    Require(selected!=UINT32_MAX,"No finite original MRT observes a root2 descriptor-base edit");
    const u64 source=material.address+Root2+selected*16,alias=material.alias+Root2+selected*16;
    // No CPU table snapshot: a known previous queue command can update the
    // descriptor after CPU preparation and the original material must see it.
    for(const bool prior_command:{false,true}) {
        Drain(renderer);RootLeaseFixture::QueueGate gate(device);
        Require(renderer.DmaData(0,alias,2,new_base,4),"Actual root GPU descriptor writer encoding failed");
        if(prior_command)renderer.OnSubmit();
        const auto hits=Hits(renderer);
        const auto rejected=renderer.DirectSmrdRootProofCount(unsigned(Gpu::Metal::DirectRootRead::Reject::GpuCurrentCommand));
        renderer.Draw(regs,DrawCall{.count=3});
        const bool proof=Hits(renderer)==hits+(prior_command ? 2 : 1) &&
            renderer.DirectSmrdRootProofCount(unsigned(Gpu::Metal::DirectRootRead::Reject::GpuCurrentCommand))==rejected+unsigned(!prior_command);
        const bool stale_cpu=*reinterpret_cast<const u32*>(source)==old_base;
        gate.Open();Drain(renderer);
        Require(proof && stale_cpu,"Actual current/prior GPU descriptor writer proof differed");
        Equal(changed,ReadMrt(renderer,device,material));
        renderer.CpuWrite(source,&old_base,4,false);renderer.Draw(regs,DrawCall{.count=3});Drain(renderer);
    }
    auto a=material.Registers(true),b=regs;
    // Ensure both target sets and all pipelines are warm before the discriminating cut.
    renderer.Draw(a,DrawCall{.count=3});renderer.Draw(b,DrawCall{.count=3});Drain(renderer);
    const auto before=Hits(renderer);
    RootLeaseFixture::HoldConsumer(device,renderer,material.address+Probe,
        [&]{renderer.Draw(a,DrawCall{.count=3});},
        [&]{const auto hits=Hits(renderer);renderer.Draw(b,DrawCall{.count=3});Require(Hits(renderer)==hits+2,"Consumer B roots did not admit");},
        [&]{renderer.CpuWrite(alias,&new_base,4,false);},"PS alias CPU write");
    Require(Hits(renderer)>=before+4,"Expected actual renderer root admissions absent");
    Equal(reference,ReadMrt(renderer,device,material));
    renderer.Draw(regs,DrawCall{.count=3});Equal(changed,ReadMrt(renderer,device,material));
    renderer.CpuWrite(source,&old_base,4,false);renderer.Draw(regs,DrawCall{.count=3});Drain(renderer);
    // Unmap only the alias; the source's primary mapping remains for inspection.
    const auto unmap=reinterpret_cast<int(*)(void*,u64)>(runtime_memory_resolve("cQke9UuBQOk#p#J"));Require(unmap,"Missing runtime unmap");
    RootLeaseFixture::HoldConsumer(device,renderer,material.address+Probe,
        [&]{renderer.Draw(a,DrawCall{.count=3});},
        [&]{const auto hits=Hits(renderer);renderer.Draw(b,DrawCall{.count=3});Require(Hits(renderer)==hits+2,"Remap consumer roots did not admit");},
        [&]{Require(!unmap(reinterpret_cast<void*>(material.alias),MemoryBytes),"Fixture alias unmap failed");},"PS alias mapping mutation");
    Equal(reference,ReadMrt(renderer,device,material));
    uintptr_t remapped=0;Require(!runtime_gpu_memory_map(&remapped,MemoryBytes,3,0,material.physical,16384),"Fixture remap failed");
    material.alias=remapped;renderer.Draw(regs,DrawCall{.count=3});Equal(reference,ReadMrt(renderer,device,material));
    Require(!renderer.Execution()->Failed(),"Renderer execution failed");
    std::printf("PS renderer %016llx: six original MRTs, finite root edit, post-cut CPU alias and mapping tickets passed\n",(unsigned long long)material.program->hash);
}
} // namespace
int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            Require(argc==3 && std::string(argv[1])=="--gpu-captures","Usage: bb-metal-ps-root-renderer-tests --gpu-captures CAPTURE_DIR");
            if(!MTLCreateSystemDefaultDevice())return 77;
            const auto captures=std::filesystem::canonical(argv[2]);PrivateData private_data;Gpu::Metal::Device device;
            for(const auto hash:Hashes){Material material(captures,hash);Run(device,material);}
            return 0;
        } catch(const std::exception& error){std::fprintf(stderr,"PS root renderer fixture: %s\n",error.what());return 1;}
    }
}
