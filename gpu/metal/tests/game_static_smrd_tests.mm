// SPDX-License-Identifier: GPL-2.0-or-later
// CPU snapshot contracts plus optional original-fragment GPU differential tests.
// Retail shader bytes are supplied at runtime; every uniform/texture is synthetic.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/metal/static_smrd.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() { std::abort(); }
namespace {
namespace Shader=Gpu::Shader;
namespace Static=Gpu::Metal::StaticSmrd;
using namespace Shader::Gcn;
constexpr u64 Hash68=0x68fcb859536adcd4ull,HashAff=0xaff190d9945a53e0ull;
constexpr u64 Arena=0x10000,Capacity=0x4000,Payload=0x20000,MemoryBytes=0x40000;
constexpr u32 Width=4,Height=4,Targets=6;
void Require(bool value,const std::string& what) {
    if(!value) throw std::runtime_error(what);
}
void SetPointer(std::array<u32,16>& ud,u32 root,u64 address) {
    ud[root]=u32(address);ud[root+1]=u32(address>>32);
}

void CpuPacking() {
    std::vector<std::byte> bytes(8192);
    for(size_t k=0;k<bytes.size();++k) bytes[k]=std::byte((k*37+13)&255);
    Static::Window window{Arena,Capacity,8192,0,0};
    std::array<Static::Snapshot,2> snapshots{{
        {Arena,4096,7,7,0,bytes.data()},
        {Arena+4096,4096,11,11,4096,bytes.data()+4096}}};
    for(const auto hash:{Hash68,HashAff}) for(u32 unaligned:{0u,1u,2u,3u,4u,255u}) {
        std::array<u32,16> ud{};
        SetPointer(ud,2,Arena+unaligned);SetPointer(ud,12,Arena+4096+unaligned);
        Static::Packed packed;
        Require(Static::Pack(hash,ud,snapshots,window,packed),"Valid prepared snapshot was rejected");
        Require(packed.count==(hash==Hash68 ? 84u : 104u),"Wrong static SMRD packed length");
        u32 at=0;
        for(const auto& load:Static::Layout(hash)) {
            const size_t offset=(load.root==2 ? 0 : 4096)+((unaligned+load.offset)&~3u);
            for(u32 k=0;k<load.words;++k) {
                u32 expected;std::memcpy(&expected,bytes.data()+offset+k*4,4);
                Require(packed.words[at++]==expected,"Packer changed an original scalar word");
            }
        }
    }
    std::array<u32,16> ud{};SetPointer(ud,2,Arena+896);SetPointer(ud,12,Arena);
    std::array<Static::Snapshot,1> shared{{{Arena,4096,7,7,0,bytes.data()}}};
    Static::Packed packed;
    Require(Static::Pack(HashAff,ud,shared,window,packed),"Shared snapshot roots rejected");
    const auto reject=[&](std::span<const Static::Snapshot> spans,Static::Window state,
                          const std::array<u32,16>& data,u64 hash,const char* label) {
        Static::Packed result;result.count=123;result.words.fill(0xdecafbad);
        Require(!Static::Pack(hash,data,spans,state,result),std::string(label)+" accepted");
        Require(!result.count && std::all_of(result.words.begin(),result.words.end(),[](u32 word){return !word;}),
                std::string(label)+" exposed partial packed data");
    };
    reject(shared,window,ud,Hash68^1,"Unknown shader");
    reject({},window,ud,Hash68,"Absent authoritative preparation");
    for(unsigned kind=0;kind<9;++kind) {
        auto altered=shared;
        switch(kind) {
        case 0:++altered[0].current_generation;break;
        case 1:altered[0].generation=altered[0].current_generation=0;break;
        case 2:altered[0].lease=Gpu::Metal::SnapshotArena::NoLease;break;
        case 3:++altered[0].address;break;
        case 4:altered[0].bytes=895;break;
        case 5:altered[0].data=nullptr;break;
        case 6:altered[0].bytes=Capacity+1;break;
        case 7:altered[0].lease=8192;break;
        case 8:altered[0].lease=Capacity;break;
        }
        reject(altered,window,ud,HashAff,"Invalid snapshot generation/lease/range");
    }
    for(unsigned kind=0;kind<7;++kind) {
        auto altered=window;
        switch(kind) {
        case 0:altered.pin=Gpu::Metal::SnapshotArena::NoLease;break;
        case 1:altered.pin=1;break;
        case 2:altered.epoch=256;break;
        case 3:altered.head=Capacity+4096;break;
        case 4:altered.capacity=0;break;
        case 5:++altered.capacity;break;
        case 6:altered.address=~u64(0)-128;break;
        }
        reject(shared,altered,ud,HashAff,"Invalid pinned arena window");
    }
    auto short_span=shared;short_span[0].bytes=1055;
    reject(short_span,window,ud,HashAff,"Final word crosses snapshot boundary");
    std::array<Static::Snapshot,2> duplicate{shared[0],shared[0]};
    reject(duplicate,window,ud,HashAff,"Ambiguous snapshot ownership");
    auto outside=ud;SetPointer(outside,12,Arena-4);
    reject(shared,window,outside,HashAff,"Unprepared root before snapshot");
    outside=ud;SetPointer(outside,12,Arena+4096);
    reject(shared,window,outside,HashAff,"Root beyond snapshot");
    outside=ud;outside[13]|=0x1000;
    reject(shared,window,outside,HashAff,"Unexpected upper pointer bits");
    // A new epoch must re-prepare the same source into a newly pinned lease.
    auto renewed=shared;renewed[0].lease=Capacity;renewed[0].current_generation=renewed[0].generation=12;
    Static::Window next{Arena,Capacity,Capacity+4096,Capacity,Capacity};
    Require(Static::Pack(HashAff,ud,renewed,next,packed),"New epoch lease could not be packed");
    std::puts("Static SMRD snapshot ownership/generation/lease/bounds regressions passed.");
}

void CpuAddresses() {
    std::array<s32,MemoryBytes/16384> entries{};
    Shader::GpuMemMap map{0,entries.size(),MemoryBytes/2,14,0,MemoryBytes};
    std::array<u32,16> ud{};SetPointer(ud,2,Arena);SetPointer(ud,12,Arena+4096);
    const auto physical=[&](u64 encoded) {return (encoded>>1)*4+((encoded&1) ? map.split : 0);};
    for(const auto hash:{Hash68,HashAff}) for(u32 unaligned:{0u,1u,2u,3u}) {
        auto words=ud;words[2]+=unaligned;words[12]+=unaligned;
        const auto block=Static::TranslateRoots(hash,words,map,entries);
        Require(block.ValidRoots()==2 && physical(block.roots[0])==Arena &&
            physical(block.roots[1])==Arena+4096,"Valid/unaligned physical roots translated incorrectly");
        words[3]|=0xabcd0000u;words[13]|=0x12340000u;
        Require(Static::TranslateRoots(hash,words,map,entries).roots==block.roots,"48-bit root mask changed");
    }
    entries[Arena/16384]=8;
    auto block=Static::TranslateRoots(HashAff,ud,map,entries);
    Require(block.ValidRoots()==2 && (block.roots[0]&1) && physical(block.roots[0])==Arena+8*16384,
        "Positive physical delta/second buffer translation failed");
    entries[Arena/16384]=-4;
    block=Static::TranslateRoots(HashAff,ud,map,entries);
    Require(block.ValidRoots()==2 && block.roots[0]==0 && physical(block.roots[1])==4096,
        "Negative physical delta/physical zero translation failed");
    entries[Arena/16384]=-5;
    Require(!Static::TranslateRoots(HashAff,ud,map,entries).ValidRoots(),"Physical underflow accepted");
    entries[Arena/16384]=Shader::GpuMemMap::kUnmapped;
    Require(!Static::TranslateRoots(HashAff,ud,map,entries).ValidRoots(),"Unmapped root page accepted");
    entries.fill(0);
    auto crossing=ud;SetPointer(crossing,2,0x13ff0);SetPointer(crossing,12,0x18000);
    Require(Static::TranslateRoots(HashAff,crossing,map,entries).ValidRoots()==2,"Contiguous page crossing rejected");
    entries[5]=Shader::GpuMemMap::kUnmapped;
    block=Static::TranslateRoots(HashAff,crossing,map,entries);
    Require(block.roots[0]==Static::AddressBlock::Fallback && block.roots[1]!=Static::AddressBlock::Fallback,
        "Partial mapped root did not retain per-root fallback");
    entries[5]=1;
    Require(Static::TranslateRoots(HashAff,crossing,map,entries).roots[0]==Static::AddressBlock::Fallback,
        "Discontinuous physical page accepted");
    entries[4]=entries[5]=2;
    Require(Static::TranslateRoots(HashAff,crossing,map,entries).ValidRoots()==2,"Contiguous remapped pages rejected");
    entries.fill(0);
    auto split=map;split.split=0x13ff4;
    block=Static::TranslateRoots(HashAff,crossing,split,entries);
    Require(block.roots[0]==Static::AddressBlock::Fallback && block.roots[1]!=Static::AddressBlock::Fallback,
        "Root range crossing physical buffers accepted");
    split.split=0x13ff0+160;
    Require(Static::TranslateRoots(HashAff,crossing,split,entries).ValidRoots()==2,"Exact split boundary rejected");
    auto limited=map;limited.limit=0x13ff0+159;
    Require(!Static::TranslateRoots(HashAff,crossing,limited,entries).ValidRoots(),"Truncated physical extent accepted");
    crossing=ud;SetPointer(crossing,12,0x17ff0);entries[6]=Shader::GpuMemMap::kUnmapped;
    block=Static::TranslateRoots(Hash68,crossing,map,entries);
    Require(block.roots[0]!=Static::AddressBlock::Fallback && block.roots[1]==Static::AddressBlock::Fallback,
        "Global uniform root page gap accepted");
    entries.fill(0);
    for(unsigned kind=0;kind<8;++kind) {
        auto invalid=map;
        switch(kind) {
        case 0:++invalid.base;break;
        case 1:invalid.shift=13;break;
        case 2:invalid.shift=22;break;
        case 3:++invalid.pages;break;
        case 4:invalid.split=invalid.limit+4;break;
        case 5:++invalid.split;break;
        case 6:invalid.limit=0;break;
        case 7:invalid.base=0x20000;break;
        }
        Require(!Static::TranslateRoots(HashAff,ud,invalid,entries).ValidRoots(),"Invalid map header accepted");
    }
    Require(!Static::TranslateRoots(HashAff,ud,map,std::span<const s32>(entries).first(4)).ValidRoots(),
        "Truncated immutable map buffer accepted");
    auto outside=ud;outside[3]|=0x1000;
    block=Static::TranslateRoots(HashAff,outside,map,entries);
    Require(block.roots[0]==Static::AddressBlock::Fallback && block.roots[1]!=Static::AddressBlock::Fallback,
        "Out-of-map 48-bit root accepted");
    Require(!Static::TranslateRoots(HashAff^1,ud,map,entries).ValidRoots(),"Unknown shader address mode accepted");
    std::puts("GPU-time direct SMRD map/page/gap/mask/bounds/sentinel regressions passed.");
}

std::unique_ptr<Shader::Program> Capture(const std::filesystem::path& directory,u64 hash) {
    char suffix[32];std::snprintf(suffix,sizeof(suffix),"_%016llx.bin",(unsigned long long)hash);
    std::filesystem::path file;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if(entry.is_regular_file() && name.starts_with("ps_") && name.ends_with(suffix)) {file=entry.path();break;}
    }
    Require(!file.empty(),std::string("Missing original fragment capture ")+suffix);
    const auto size=std::filesystem::file_size(file);
    Require(size>=8 && size<=65536 && !(size&3),"Invalid captured fragment length");
    std::vector<u32> code(size/4+64,0xbf810000u);
    std::ifstream stream(file,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(code.data()),size)),"Cannot read original fragment capture");
    std::array<u32,16> ud{};
    for(u32 sgprs=0;sgprs<=16;++sgprs) for(u32 components=0;components<4;++components) {
        Shader::BuildInput input{.stage=Shader::Stage::Fragment,.address=0,.user_data=ud.data(),
            .num_user_sgprs=sgprs,.vgpr_comp_cnt=components,.code=code.data()};
        auto result=Shader::BuildProgram(input);
        if(result && result->hash==hash) return result;
    }
    throw std::runtime_error("Original fragment capture hash/metadata mismatch");
}
size_t Count(const std::string& source,const char* word) {
    size_t count=0;for(size_t at=0;(at=source.find(word,at))!=std::string::npos;at+=std::strlen(word)) ++count;
    return count;
}
Shader::MslOptions Options(const Shader::Program& p,bool enabled,bool addresses=false) {
    Shader::MslOptions o;o.static_smrd=enabled && !addresses;o.direct_smrd=enabled && addresses;o.col_format=0x444444;
    o.vs_param_mask=63;o.ps_input_addr=0x1001;
    for(u32 k=0;k<32;++k) o.ps_input_cntl[k]=k;
    o.textures.resize(p.images.size());return o;
}
void CapturedSimdProof(const Shader::Program& original) {
    Require(Shader::FragmentSimdSmrdEligible(original),"Original scalar addresses failed uniform provenance proof");
    auto options=Options(original,false);options.simd_smrd=true;
    std::vector<std::string> unsupported;
    const auto source=Shader::GenerateMsl(original,options,&unsupported);
    u32 loads=0;
    for(const auto& i:original.code) if(i.enc==Enc::SMRD && (i.op<=4 || (i.op>=8 && i.op<=12))) ++loads;
    Require(unsupported.empty() && Count(source,"if (simd_is_helper_thread())")==loads &&
        Count(source,"if (simd_is_first())")==loads && Count(source,"simd_broadcast_first(")>=loads,
        "Uniform scalar loads lost helper fallback/election/broadcast");
    for(bool addresses:{false,true}) {
        auto conflicting=Options(original,true,addresses);conflicting.simd_smrd=true;
        Require(Shader::GenerateMsl(original,conflicting,&unsupported)==
            Shader::GenerateMsl(original,Options(original,false),&unsupported),"Conflicting SIMD mode did not fail closed");
    }
    for(u32 kind=0;kind<4;++kind) {
        auto changed=original;
        // Clobber a nested descriptor immediately before its scalar payload
        // read. The pre-existing original-UD-root proof must still pass, so
        // this exercises the additional whole-address uniformity proof.
        bool inserted=false;
        for(size_t k=1;k<changed.code.size();++k) {
            const auto& load=changed.code[k];
            auto& clobber=changed.code[k-1];
            if(load.enc!=Enc::SMRD || load.op<8 || load.op>12 || load.sbase<16 ||
               clobber.enc==Enc::SMRD || IsBranch(clobber)) continue;
            if(kind<2) {
                clobber.enc=Enc::VOP1;clobber.op=u16(Vop::V_READFIRSTLANE_B32);
                clobber.dst=load.sbase;clobber.src[0]=Operand::Vgpr0;
                if(kind==1) {clobber.enc=Enc::VOPC;clobber.op=2;clobber.sdst=load.sbase;}
            } else if(kind==2) {
                clobber.enc=Enc::MUBUF;clobber.op=u16(Mubuf::BUFFER_STORE_DWORD);
            } else {
                clobber.enc=Enc::MIMG;clobber.op=u16(Mimg::IMAGE_STORE);
            }
            inserted=true;break;
        }
        Require(inserted && Shader::FragmentStaticSmrdEligible(changed),"Nested-address negative fixture invalid");
        Require(!Shader::FragmentSimdSmrdEligible(changed),"Varying descriptor or guest memory write was accepted");
    }
    // Two paths assigning distinct scalar constants are individually uniform
    // but become varying at their divergent join. Preserve a real forward
    // branch in 68fc and inject the different value on its nonempty arm.
    if(original.hash==Hash68) {
        auto changed=original;
        auto branch=std::find_if(changed.code.begin(),changed.code.end(),[](const Inst& i){return IsBranch(i);});
        Require(branch!=changed.code.end() && branch+1!=changed.code.end(),"Missing CFG join for scalar uniformity regression");
        const auto target=BranchTarget(*branch);
        auto load=std::find_if(branch+1,changed.code.end(),[&](const Inst& i){
            return i.pc>=target && i.enc==Enc::SMRD && i.op>=8 && i.op<=12 && i.sbase>=16;
        });
        Require(load!=changed.code.end(),"Missing post-join nested load");
        auto& arm=*(branch+1);arm.enc=Enc::SOP1;arm.op=u16(Sop1::S_MOV_B32);
        arm.dst=load->sbase;arm.src[0]=Operand::Zero;
        Require(Shader::FragmentStaticSmrdEligible(changed) && !Shader::FragmentSimdSmrdEligible(changed),
            "Different scalar values on divergent CFG arms were merged as uniform");
    }
    std::printf("Original %016llx SIMD scalar-address provenance/helper-fallback proof passed (%u loads).\n",
        (unsigned long long)original.hash,loads);
}
void CapturedProof(const Shader::Program& original) {
    Require(Shader::FragmentStaticSmrdEligible(original),"Original exact layout failed CFG proof");
    const auto layout=Static::Layout(original.hash);
    std::vector<std::string> unsupported;
    const auto before=Shader::GenerateMsl(original,Options(original,false),&unsupported);
    Require(unsupported.empty(),"Original shader contains unsupported operations");
    const auto after=Shader::GenerateMsl(original,Options(original,true),&unsupported);
    Require(unsupported.empty(),"Optimized shader contains unsupported operations");
    Require(Count(before,"device uint* p_ = dc_ptr_n")-Count(after,"device uint* p_ = dc_ptr_n")==layout.size(),
            "Unexpected number of lowered direct loads");
    Require(after.find("constant uint* dc_smrd [[buffer(13)]]")!=std::string::npos,"Static binding missing");
    const auto payload_lines=[](const std::string& source) {
        std::vector<std::string> result;
        for(size_t begin=0,end;begin<source.size();begin=end+1) {
            end=source.find('\n',begin);if(end==std::string::npos) end=source.size();
            const auto line=source.substr(begin,end-begin);
            if(line.find("dc_ptr_n(mem, (dc_vbase")!=std::string::npos) result.push_back(line);
        }
        return result;
    };
    Require(payload_lines(before)==payload_lines(after),"Nested mutable payload read changed");
    const auto direct=Shader::GenerateMsl(original,Options(original,true,true),&unsupported);
    Require(unsupported.empty() && direct.find("constant ulong* dc_smrd_roots [[buffer(13)]]")!=std::string::npos,
        "GPU-time direct-root binding missing");
    Require(Count(direct,"ulong root_ = dc_smrd_roots")==layout.size() &&
        Count(direct,"else p_ = dc_ptr_n")==layout.size(),"Per-load address sentinel fallback missing");
    Require(payload_lines(before)==payload_lines(direct),"Address mode changed nested mutable payload read");
    auto conflicting=Options(original,true);conflicting.direct_smrd=true;
    Require(Shader::GenerateMsl(original,conflicting,&unsupported)==before,"Conflicting modes did not fail closed");
    for(unsigned kind=0;kind<7;++kind) {
        auto changed=original;
        auto selected=std::find_if(changed.code.begin(),changed.code.end(),[&](const Inst& i){return i.pc==layout.front().pc;});
        switch(kind) {
        case 0:changed.hash^=1;break;
        case 1:changed.stage=Shader::Stage::Compute;break;
        case 2:selected->imm=false;break;
        case 3:++selected->sbase;break;
        case 4:++selected->dst;break;
        case 5:++selected->smrd_offset;break;
        case 6:changed.num_user_sgprs=13;break;
        }
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Changed original layout was accepted");
        for(bool addresses:{false,true}) {
            const auto fallback=Shader::GenerateMsl(changed,Options(changed,true,addresses),&unsupported);
            Require(fallback.find("constant uint* dc_smrd")==std::string::npos &&
                fallback.find("constant ulong* dc_smrd_roots")==std::string::npos,
                "Rejected layout still consumes buffer 13");
        }
    }
    for(const auto reg:{2u,3u,12u,13u}) {
        auto changed=original;
        auto& clobber=changed.code.front();
        clobber.enc=Enc::SOP1;clobber.op=u16(Sop1::S_MOV_B32);clobber.dst=u16(reg);clobber.src[0]=Operand::Zero;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Clobbered original root was accepted");
    }
    auto changed=original;
    auto branch=std::find_if(changed.code.begin(),changed.code.end(),[](const Inst& i){return IsBranch(i);});
    if(branch!=changed.code.end()) {
        Require(original.hash==Hash68 && branch+1!=changed.code.end(),"Unexpected original branch layout");
        auto& arm=*(branch+1);arm.enc=Enc::SOP1;arm.op=u16(Sop1::S_MOV_B32);arm.dst=2;arm.src[0]=Operand::Zero;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Root clobber on one CFG arm was accepted at join");
        changed=original;
        branch=std::find_if(changed.code.begin(),changed.code.end(),[](const Inst& i){return IsBranch(i);});
        branch->simm16=-1;
        Require(!Shader::FragmentStaticSmrdEligible(changed),"Back edge was accepted");
    } else {
        // Aff implements its EXEC selection without a scalar branch. Its four
        // original-root clobber regressions above still exercise the proof.
        Require(original.hash==HashAff,"Expected original 68fc branch missing");
    }
    std::printf("Original %016llx direct SMRD layout/CFG/fallback proof passed.\n",(unsigned long long)original.hash);
}

id<MTLLibrary> Compile(id<MTLDevice> device,const std::string& source,const char* label) {
    auto options=[MTLCompileOptions new];options.mathMode=MTLMathModeSafe;
    NSError* error=nil;
    auto text=[[NSString alloc] initWithBytes:source.data() length:source.size() encoding:NSUTF8StringEncoding];
    auto library=[device newLibraryWithSource:text options:options error:&error];
    Require(library!=nil,std::string(label)+": "+(error ? error.localizedDescription.UTF8String : "Metal compile failed"));
    return library;
}

std::vector<u16> Render(id<MTLDevice> device,id<MTLCommandQueue> queue,
        id<MTLRenderPipelineState> pipeline,id<MTLBuffer> memory,id<MTLBuffer> map,
        const std::array<u32,16>& ud,const Static::Packed& packed,
        const Shader::Program& program,id<MTLTexture> texture,id<MTLSamplerState> sampler,bool reversed,
        const Static::AddressBlock* addresses=nullptr,u64 split=MemoryBytes,
        std::array<u32,Shader::kProbeWords>* observation=nullptr,u32 geometry=0,bool derivative_target=false) {
    const u32 width=derivative_target ? 32 : Width,height=derivative_target ? 13 : Height;
    const u32 target_count=Targets+u32(derivative_target);
    std::vector<id<MTLTexture>> targets(target_count);
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
    for(u32 k=0;k<target_count;++k) {
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
            width:width height:height mipmapped:NO];
        descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageRenderTarget;
        targets[k]=[device newTextureWithDescriptor:descriptor];
        pass.colorAttachments[k].texture=targets[k];pass.colorAttachments[k].loadAction=MTLLoadActionClear;
        pass.colorAttachments[k].storeAction=MTLStoreActionStore;
        pass.colorAttachments[k].clearColor=MTLClearColorMake(.125,.125,.125,.125);
    }
    const u64 row=width*8,plane=row*height;
    auto output=[device newBufferWithLength:plane*target_count options:MTLResourceStorageModeShared];
    id<MTLBuffer> probe=nil;
    if(observation) {
        probe=[device newBufferWithLength:Shader::kProbeWords*4 options:MTLResourceStorageModeShared];
        std::memset(probe.contents,0,probe.length);
    }
    auto command=[queue commandBuffer];auto enc=[command renderCommandEncoderWithDescriptor:pass];
    const MTLViewport viewport{0,0,double(width),double(height),0,1};
    [enc setRenderPipelineState:pipeline];[enc setViewport:viewport];
    [enc setVertexBytes:&geometry length:sizeof(geometry) atIndex:0];
    if(geometry==3) [enc setScissorRect:MTLScissorRect{1,1,width-3,height-2}];
    [enc setFrontFacingWinding:reversed ? MTLWindingClockwise : MTLWindingCounterClockwise];
    [enc setFragmentBytes:ud.data() length:64 atIndex:0];
    [enc setFragmentBuffer:memory offset:0 atIndex:1];
    [enc setFragmentBuffer:memory offset:(split<MemoryBytes ? split : 0) atIndex:2];
    [enc setFragmentBuffer:map offset:0 atIndex:3];
    if(observation) [enc setFragmentBuffer:probe offset:0 atIndex:Shader::kProbeBinding];
    if(addresses) [enc setFragmentBytes:addresses length:sizeof(*addresses) atIndex:Shader::Binding::StaticSmrd];
    else if(packed.count) [enc setFragmentBytes:packed.words.data() length:packed.count*4 atIndex:Shader::Binding::StaticSmrd];
    for(u32 k=0;k<program.images.size();++k) [enc setFragmentTexture:texture atIndex:k];
    for(u32 k=0;k<program.samplers.size();++k) [enc setFragmentSamplerState:sampler atIndex:k];
    [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:geometry==4 ? 6 : 3];[enc endEncoding];
    auto blit=[command blitCommandEncoder];
    for(u32 k=0;k<target_count;++k) [blit copyFromTexture:targets[k] sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
        sourceSize:MTLSizeMake(width,height,1) toBuffer:output destinationOffset:plane*k
        destinationBytesPerRow:row destinationBytesPerImage:plane];
    [blit endEncoding];[command commit];[command waitUntilCompleted];
    Require(command.status==MTLCommandBufferStatusCompleted,"Fragment differential GPU execution failed");
    if(observation) std::memcpy(observation->data(),probe.contents,probe.length);
    const auto* words=static_cast<const u16*>(output.contents);
    return {words,words+plane*target_count/2};
}
bool Nan(u16 bits) {return (bits&0x7c00u)==0x7c00u && (bits&0x3ffu);}
void ComparePixels(const std::vector<u16>& reference,const std::vector<u16>& optimized) {
    Require(reference.size()==optimized.size(),"Differential pixel size differs");
    u32 finite=0,nonzero=0,changed_from_clear=0;
    for(size_t k=0;k<reference.size();++k) {
        Require(reference[k]==optimized[k] || (Nan(reference[k]) && Nan(optimized[k])),
                "Original/static SMRD fragment output differs at half word "+std::to_string(k));
        if((reference[k]&0x7c00u)!=0x7c00u) {
            ++finite;nonzero+=(reference[k]&0x7fffu)!=0;changed_from_clear+=reference[k]!=0x3000;
        }
    }
    Require(finite>reference.size()/4 && nonzero>16 && changed_from_clear>16,
            "Synthetic fragment inputs did not produce meaningful finite output");
}

void GpuDifferential(id<MTLDevice> device,const Shader::Program& program,bool addresses=false,bool simd=false) {
    const std::string vertex=R"MSL(#include <metal_stdlib>
using namespace metal;
struct VOut {float4 pos [[position]];float4 p0 [[user(p0)]];float4 p1 [[user(p1)]];
float4 p2 [[user(p2)]];float4 p3 [[user(p3)]];float4 p4 [[user(p4)]];float4 p5 [[user(p5)]];};
vertex VOut vmain(uint vid [[vertex_id]],constant uint& geometry [[buffer(0)]]) {
 float2 q[6]={float2(-1,-1),float2(3,-1),float2(-1,3),float2(0),float2(0),float2(0)};VOut o;
 if(geometry==1u) {q[0]=float2(-.93,-.81);q[1]=float2(.86,-.57);q[2]=float2(-.24,.91);}
 if(geometry==2u) {q[0]=float2(-.97,-.83);q[1]=float2(.91,.37);q[2]=float2(.94,.58);}
 if(geometry==4u) {q[0]=float2(-.93,-.85);q[1]=float2(-.03,-.71);q[2]=float2(-.55,.87);
                   q[3]=float2(.10,-.80);q[4]=float2(.45,.85);q[5]=float2(.97,-.58);}
 o.pos=float4(q[vid],.5,1);o.p0=float4(.3,.4,.8,1);o.p1=float4(.2,.5,.7,1);
 o.p2=float4(.4,.1,.9,1);o.p3=float4(.35,.65,.5,1);o.p4=float4(.1,.2,.3,1);o.p5=float4(.6,.4,.2,1);
 if(geometry!=0u) {float2 uv=q[vid]*.5+.5;o.p2.xy+=uv*.375;o.p3.xy+=uv*.625;o.p5.xy+=uv*.25;}
 return o;
}
kernel void writer(device uint* memory [[buffer(0)]],constant uint& value [[buffer(1)]],uint tid [[thread_position_in_grid]]) {
 if(tid<0x10000u/4u) memory[0x20000u/4u+tid]=value;
}
kernel void table_writer(device uint* memory [[buffer(0)]],constant uint2& control [[buffer(1)]],uint tid [[thread_position_in_grid]]) {
 if(control.y==0u && tid<12u) memory[0x10000u/4u+tid*4u]=0x30000u+tid*4096u;
 if(control.y==1u && tid<896u/4u) memory[0x11000u/4u+tid]=as_type<uint>(as_type<float>(control.x)+float(tid&3u)*.0625f);
})MSL";
    auto support=Compile(device,vertex,"fixture vertex/writer");auto vs=[support newFunctionWithName:@"vmain"];
    auto writer_function=[support newFunctionWithName:@"writer"];
    NSError* error=nil;auto writer=[device newComputePipelineStateWithFunction:writer_function error:&error];
    Require(writer!=nil,"Cannot compile ordered GPU payload writer");
    auto table_writer=[device newComputePipelineStateWithFunction:[support newFunctionWithName:@"table_writer"] error:&error];
    Require(table_writer!=nil,"Cannot compile ordered GPU direct-table writer");
    Static::Load observed_load{};
    u32 observation_pc=~0u;
    const bool mutable_tables=addresses || simd;
    if(mutable_tables) {
        // The UD12 loads feed renderer-resolved T#/S# descriptors and are dead
        // in ordinary translated fragment arithmetic. Observe a real selected
        // load before overwrite so GPU-time values/ordering are tested directly,
        // while all six original game MRTs remain compared unchanged.
        for(const auto& load:Static::Layout(program.hash)) if(load.root==12) observed_load=load;
        auto instruction=std::find_if(program.code.begin(),program.code.end(),[&](const Inst& i){return i.pc==observed_load.pc;});
        Require(instruction!=program.code.end() && instruction+1!=program.code.end(),"UD12 observation instruction missing");
        observation_pc=(instruction+1)->pc;
    }
    std::array<id<MTLRenderPipelineState>,2> pipelines;
    for(unsigned enabled=0;enabled<2;++enabled) {
        std::vector<std::string> unsupported;
        auto options=Options(program,simd ? false : enabled,addresses);options.simd_smrd=simd && enabled;
        if(mutable_tables) {options.probe_pc=observation_pc;options.probe_x=~0u;}
        auto source=Shader::GenerateMsl(program,options,&unsupported);
        Require(unsupported.empty(),"Captured fragment translation unsupported");
        if(simd) {
            // Add a seventh fixture-only MRT without changing any of the six
            // original outputs. Derivatives of a loaded scalar times pixel
            // position must remain valid across uncovered helper neighbours.
            auto at=source.find("struct PsOut {");Require(at!=std::string::npos,"Missing fragment output struct");
            source.insert(at+std::strlen("struct PsOut {"),"\n float4 fixture_derivative [[color(6)]];");
            at=source.find("PsOut out = {};");Require(at!=std::string::npos,"Missing fragment output initialization");
            source.insert(at+std::strlen("PsOut out = {};"),"\n float4 fixture_derivative = float4(0);");
            at=source.find("    if (atomic_exchange_explicit((device atomic_uint*)&dbg[7]");
            Require(at!=std::string::npos,"Missing original scalar observation point");
            const auto reg="as_type<float>(s"+std::to_string(observed_load.destination)+")";
            source.insert(at," fixture_derivative = float4(dfdx("+reg+" * pin.pos.x), dfdy("+reg+
                " * pin.pos.y), "+reg+", 1.0f);\n");
            for(size_t end=0;(end=source.find("return out;",end))!=std::string::npos;) {
                source.insert(end,"out.fixture_derivative = fixture_derivative; ");
                end+=std::strlen("out.fixture_derivative = fixture_derivative; return out;");
            }
        }
        auto library=Compile(device,source,"captured fragment");
        auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=vs;
        descriptor.fragmentFunction=[library newFunctionWithName:@"main0"];
        for(u32 k=0;k<Targets+u32(simd);++k) descriptor.colorAttachments[k].pixelFormat=MTLPixelFormatRGBA16Float;
        pipelines[enabled]=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        Require(pipelines[enabled]!=nil,std::string("Cannot create differential render pipeline: ")+
            (error ? error.localizedDescription.UTF8String : "unknown error"));
    }
    auto memory=[device newBufferWithLength:MemoryBytes options:MTLResourceStorageModeShared];
    struct Mapping {Shader::GpuMemMap header;std::array<s32,MemoryBytes/16384> pages;};
    Mapping mapping{};mapping.header={0,mapping.pages.size(),MemoryBytes,14,0,MemoryBytes};
    auto map=[device newBufferWithBytes:&mapping length:sizeof(mapping) options:MTLResourceStorageModeShared];
    const u32 texture_width=simd ? 32 : 1;
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
        width:texture_width height:texture_width mipmapped:simd];descriptor.storageMode=MTLStorageModeShared;
    auto texture=[device newTextureWithDescriptor:descriptor];
    for(u32 level=0;level<texture.mipmapLevelCount;++level) {
        const u32 side=std::max(1u,texture_width>>level);std::vector<u8> pixels(side*side*4);
        for(u32 y=0;y<side;++y) for(u32 x=0;x<side;++x) {
            const u32 at=(y*side+x)*4;
            pixels[at]=simd ? u8(32+(x*17+y*7+level*23)%192) : 96;
            pixels[at+1]=simd ? u8(32+(x*5+y*19+level*31)%192) : 161;
            pixels[at+2]=simd ? u8(32+(x*11+y*3+level*41)%192) : 223;pixels[at+3]=255;
        }
        [texture replaceRegion:MTLRegionMake2D(0,0,side,side) mipmapLevel:level withBytes:pixels.data() bytesPerRow:side*4];
    }
    auto sampler_descriptor=[MTLSamplerDescriptor new];sampler_descriptor.minFilter=MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter=MTLSamplerMinMagFilterLinear;
    if(simd) sampler_descriptor.mipFilter=MTLSamplerMipFilterLinear;
    auto sampler=[device newSamplerStateWithDescriptor:sampler_descriptor];auto queue=[device newCommandQueue];
    bool producer_observed=false;
    std::array<bool,2> table_producer_observed{};
    bool partial_coverage_observed=false,derivative_observed=false;
    for(u32 scenario=0;scenario<8;++scenario) {
        auto* data=static_cast<std::byte*>(memory.contents);std::memset(data,0,MemoryBytes);
        auto* words=reinterpret_cast<u32*>(data);
        for(u32 at=0;at<MemoryBytes/4;++at) words[at]=std::bit_cast<u32>(.25f+scenario*.03125f);
        const u32 unaligned=scenario%4;
        std::array<u32,16> ud{};SetPointer(ud,2,Arena+unaligned);SetPointer(ud,12,Arena+4096+unaligned);
        for(u32 slot=0;slot<12;++slot) {
            const u64 base=Payload+slot*4096;
            const u32 resource[]{u32(base),u32(base>>32)|(16u<<16),256,0xfacu|(7u<<12)|(14u<<15)};
            std::memcpy(data+Arena+slot*16,resource,sizeof(resource));
        }
        std::array<Static::Snapshot,2> snapshots{{{Arena,4096,1,1,0,data+Arena},
            {Arena+4096,4096,2,2,4096,data+Arena+4096}}};
        Static::Packed packed;
        Static::AddressBlock roots;
        if(addresses) {
            mapping.header.split=(scenario==1 || scenario==6) ? Arena/2 :
                scenario==5 ? Arena+64 : MemoryBytes;
            std::memcpy(map.contents,&mapping,sizeof(mapping));
            roots=Static::TranslateRoots(program.hash,ud,mapping.header,mapping.pages);
            Require(roots.ValidRoots()==(scenario==5 ? 1u : 2u),"Fixture mapped roots unexpectedly rejected");
            // Exercise one-root and all-root sentinel fallbacks with valid
            // mutable tables; all variants must keep their original GPU reads.
            if(scenario==2 || scenario==4) roots.roots[0]=Static::AddressBlock::Fallback;
            if(scenario==3 || scenario==4) roots.roots[1]=Static::AddressBlock::Fallback;
        } else if(!simd) Require(Static::Pack(program.hash,ud,snapshots,{Arena,Capacity,8192,0,0},packed),"Fixture snapshot packing failed");
        const auto* root_block=addresses ? &roots : nullptr;
        struct State {std::vector<u16> pixels;std::array<u32,Shader::kProbeWords> observation{};};
        auto render_pair=[&] {
            State reference,optimized;
            reference.pixels=Render(device,queue,pipelines[0],memory,map,ud,packed,program,texture,sampler,
                scenario&1,root_block,mapping.header.split,mutable_tables ? &reference.observation : nullptr,
                simd ? scenario%4+1 : 0,simd);
            optimized.pixels=Render(device,queue,pipelines[1],memory,map,ud,packed,program,texture,sampler,
                scenario&1,root_block,mapping.header.split,mutable_tables ? &optimized.observation : nullptr,
                simd ? scenario%4+1 : 0,simd);
            ComparePixels(reference.pixels,optimized.pixels);
            if(mutable_tables) {
                Require(reference.observation[0]==1 && optimized.observation[0]==1,"Selected UD12 GPU load was not observed");
                for(u32 k=0;k<observed_load.words;++k) {
                    const u32 at=104+observed_load.destination+k;
                    Require(reference.observation[at]==optimized.observation[at],"Original/direct UD12 load word differs");
                    Require((reference.observation[at]&0x7f800000u)!=0x7f800000u,"Selected UD12 test words are not finite");
                }
            }
            if(simd) {
                const size_t begin=32*13*4*Targets;u32 covered=0,clear=0,finite_game=0,nonzero_game=0;
                for(size_t k=begin;k<reference.pixels.size();k+=4) {
                    if(reference.pixels[k+3]==0x3000) {++clear;continue;}
                    Require(reference.pixels[k+3]==0x3c00,"Derivative fixture coverage marker invalid");
                    ++covered;
                    for(u32 target=0;target<Targets;++target) for(u32 component=0;component<4;++component) {
                        const auto value=reference.pixels[target*32*13*4+(k-begin)+component];
                        if((value&0x7c00u)!=0x7c00u) {++finite_game;nonzero_game+=(value&0x7fffu)!=0;}
                    }
                    const auto f=[](u16 bits) {return float(std::bit_cast<_Float16>(bits));};
                    const float scalar=f(reference.pixels[k+2]);
                    Require(std::isfinite(scalar) && std::abs(f(reference.pixels[k])-scalar)<.005f &&
                        std::abs(f(reference.pixels[k+1])-scalar)<.005f,"Loaded scalar derivative differs at a partial quad");
                    derivative_observed|=std::abs(scalar)>.1f;
                }
                Require(covered>0 && finite_game>covered*Targets && nonzero_game>covered,
                    "Covered game MRTs did not contain meaningful finite results");
                if(covered && clear) for(u32 y=0;y<13;y+=2) for(u32 x=0;x<32;x+=2) {
                    u32 mask=0;
                    for(u32 lane=0;lane<4;++lane) {
                        const u32 px=x+(lane&1),py=y+(lane>>1);
                        if(py<13 && reference.pixels[begin+(py*32+px)*4+3]==0x3c00) mask|=1u<<lane;
                    }
                    partial_coverage_observed|=mask && mask!=15;
                }
            }
            return reference;
        };
        const auto before=render_pair();
        // Pack first, then write mutable payload on the GPU. Descriptors and
        // pinned snapshots stay immutable; both PS variants must read new data.
        auto command=[queue commandBuffer];auto compute=[command computeCommandEncoder];
        [compute setComputePipelineState:writer];[compute setBuffer:memory offset:0 atIndex:0];
        const u32 value=std::bit_cast<u32>(.75f+scenario*.015625f);
        [compute setBytes:&value length:4 atIndex:1];
        [compute dispatchThreads:MTLSizeMake(0x10000/4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        [compute endEncoding];[command commit];
        // The same serial queue orders the writer before both subsequent draws.
        const auto after=render_pair();
        Require(command.status==MTLCommandBufferStatusCompleted,"Ordered GPU payload writer failed");
        const size_t original_words=simd ? 32*13*4*Targets : after.pixels.size();
        for(size_t k=0;k<original_words;++k)
            if(before.pixels[k]!=after.pixels[k] && !Nan(before.pixels[k]) && !Nan(after.pixels[k])) producer_observed=true;
        if(mutable_tables) {
            auto previous=after;
            for(u32 root=0;root<2;++root) {
                auto update=[queue commandBuffer];auto enc=[update computeCommandEncoder];
                [enc setComputePipelineState:table_writer];[enc setBuffer:memory offset:0 atIndex:0];
                const std::array<u32,2> control{std::bit_cast<u32>(.625f+scenario*.015625f),root};
                [enc setBytes:control.data() length:sizeof(control) atIndex:1];
                [enc dispatchThreads:MTLSizeMake(896/4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
                [enc endEncoding];[update commit];
                const auto current=render_pair();
                Require(update.status==MTLCommandBufferStatusCompleted,"Ordered direct-root GPU writer failed");
                if(root==0) {
                    for(size_t k=0;k<original_words;++k)
                        if(previous.pixels[k]!=current.pixels[k] && !Nan(previous.pixels[k]) && !Nan(current.pixels[k]))
                            table_producer_observed[root]=true;
                } else for(u32 k=0;k<observed_load.words;++k) {
                    const u32 at=104+observed_load.destination+k;
                    if(previous.observation[at]!=current.observation[at]) table_producer_observed[root]=true;
                }
                previous=current;
            }
        }
    }
    Require(producer_observed,"Synthetic differential never observed the ordered GPU payload writer");
    if(mutable_tables) Require(table_producer_observed[0] && table_producer_observed[1],
        "Synthetic differential did not observe finite changes from both direct UD table writers");
    if(simd) Require(partial_coverage_observed && derivative_observed,"Partial-quad scalar derivatives were not meaningfully exercised");
    std::printf("Original %016llx %s full-fragment/GPU-producer differential passed (%u states).\n",
        (unsigned long long)program.hash,simd ? "SIMD scalar/helper/derivatives" : addresses ? "GPU-time addresses" : "snapshot words",
        mutable_tables ? 32u : 16u);
}
} // namespace

int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            CpuPacking();CpuAddresses();
            if(argc==1) return 0;
            Require(argc==3 && (std::string(argv[1])=="--cpu-captures" ||
                std::string(argv[1])=="--gpu-captures" || std::string(argv[1])=="--gpu-direct-captures" ||
                std::string(argv[1])=="--gpu-simd-captures"),
                "Use --cpu-captures DIR, --gpu-captures DIR, --gpu-direct-captures DIR or --gpu-simd-captures DIR");
            auto a=Capture(argv[2],Hash68),b=Capture(argv[2],HashAff);CapturedProof(*a);CapturedProof(*b);
            CapturedSimdProof(*a);CapturedSimdProof(*b);
            if(std::string(argv[1])=="--cpu-captures") return 0;
            auto device=MTLCreateSystemDefaultDevice();if(!device) return 77;
            const bool addresses=std::string(argv[1])=="--gpu-direct-captures";
            const bool simd=std::string(argv[1])=="--gpu-simd-captures";
            GpuDifferential(device,*a,addresses,simd);GpuDifferential(device,*b,addresses,simd);return 0;
        } catch(const std::exception& error) {
            std::fprintf(stderr,"static SMRD fixture: %s\n",error.what());return 1;
        }
    }
}
