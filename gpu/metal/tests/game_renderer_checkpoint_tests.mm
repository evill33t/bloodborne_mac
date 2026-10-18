// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_renderer.h"
#include "gpu/liverpool/registers.h"
#include "src/checkpoint/archive.h"
#include "src/checkpoint/memory.h"
#include "src/runtime_gpu_memory.h"
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
extern "C" void runtime_restart() {__builtin_abort();}

namespace {
using namespace Bb::Checkpoint;
using Renderer=Gpu::Metal::MetalRenderer;
using namespace Gpu::Liverpool;
using Clock=std::chrono::steady_clock;
constexpr const char* Identity="metal-renderer-checkpoint-fixture-v1";
Clock::time_point Deadline() {return Clock::now()+std::chrono::seconds(5);}
StreamSection Bytes(SectionKind kind,std::uint64_t id,std::vector<std::uint8_t> bytes) {
    auto p=std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes));
    return {kind,1,id,p->size(),[p](std::uint64_t offset,std::span<std::uint8_t> out,std::string&) {
        assert(offset<=p->size() && out.size()<=p->size()-offset);std::memcpy(out.data(),p->data()+offset,out.size());return true;
    }};
}
template<class T>void Append(std::vector<std::uint8_t>& bytes,const T* data,std::size_t n) {
    auto* first=reinterpret_cast<const std::uint8_t*>(data);bytes.insert(bytes.end(),first,first+n*sizeof(T));
}
RegFile Registers(std::uint64_t address) {
    const auto shader=address+122880;
    RegFile r;r[Reg::SPI_SHADER_PGM_LO_VS]=std::uint32_t(shader>>8);r[Reg::SPI_SHADER_PGM_LO_PS]=std::uint32_t((shader+256)>>8);
    r[Reg::SPI_SHADER_PGM_RSRC2_VS]=2u<<1;r[Reg::SPI_SHADER_USER_DATA_VS_0]=std::uint32_t(address);r[Reg::SPI_SHADER_USER_DATA_VS_0+1]=std::uint32_t(address>>32);
    r[Reg::VGT_PRIMITIVE_TYPE]=4;r[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=std::uint32_t((address+16384)>>8);
    r[Reg::CB_COLOR0_BASE+Reg::CB_INFO]=(10u<<2)|(1u<<11);r[Reg::CB_COLOR_CONTROL]=1u<<4;r[Reg::CB_TARGET_MASK]=r[Reg::CB_SHADER_MASK]=15;
    r[Reg::SPI_SHADER_COL_FORMAT]=9;r[Reg::PA_CL_VTE_CNTL]=0x43f;
    const float viewport[]{4,4,-4,4,1,0};for(unsigned k=0;k<6;++k)r[Reg::PA_CL_VPORT_XSCALE+k]=std::bit_cast<std::uint32_t>(viewport[k]);
    r[Reg::PA_SC_SCREEN_SCISSOR_BR]=r[Reg::PA_SC_WINDOW_SCISSOR_BR]=r[Reg::PA_SC_GENERIC_SCISSOR_BR]=8u|(8u<<16);
    return r;
}
void CheckFrame(Renderer& renderer,Gpu::Metal::Device& device,std::uint64_t address,unsigned green) {
    renderer.Flip(Gpu::FlipInfo{.address=address,.width=8,.height=8,.pitch=8});std::string error;
    assert(renderer.WaitIdleUntil(Deadline(),error));
    id<MTLTexture> texture=(__bridge id<MTLTexture>)renderer.LatestFrame();assert(texture);
    id<MTLDevice> metal=(__bridge id<MTLDevice>)device.Handle();id<MTLCommandQueue> queue=(__bridge id<MTLCommandQueue>)device.Queue();
    id<MTLBuffer> buffer=[metal newBufferWithLength:256 options:MTLResourceStorageModeShared];
    auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(8,8,1)
        toBuffer:buffer destinationOffset:0 destinationBytesPerRow:32 destinationBytesPerImage:256];
    [blit endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
    auto* pixels=static_cast<const std::uint8_t*>(buffer.contents);
    for(unsigned n=0;n<64;++n)assert(pixels[4*n]==0 && pixels[4*n+1]==green && pixels[4*n+2]==255-green && pixels[4*n+3]==255);
}
void Save(const char* path) {
    std::uint64_t physical=0;std::uintptr_t address=0;
    assert(!runtime_gpu_memory_allocate(0,runtime_gpu_memory_size(),131072,16384,0,&physical));
    assert(!runtime_gpu_memory_map(&address,131072,3,0,physical,16384));
    auto* base=reinterpret_cast<std::uint8_t*>(address);
    const std::uint32_t descriptor[]{std::uint32_t(address+256),std::uint32_t((address+256)>>32)|(16u<<16),3,0xfacu|(7u<<12)|(14u<<15)};
    const float vertices[]{-1,-1,0,1,3,-1,0,1,-1,3,0,1};std::memcpy(base,descriptor,sizeof(descriptor));std::memcpy(base+256,vertices,sizeof(vertices));
    const std::uint32_t vs[]{0xc0820100,0xe00c2000,0x80010100,0xf80008cf,0x04030201,0xbf810000};
    const std::uint32_t ps[]{0x7e0002f2,0x7e020280,0x7e040280,0x7e0602f2,0xf800080f,0x03020100,0xbf810000};
    std::memcpy(base+122880,vs,sizeof(vs));std::memcpy(base+123136,ps,sizeof(ps));
    Gpu::Metal::Device device;Renderer renderer(device);auto r=Registers(address);
    renderer.NoteConstDump(address,16);renderer.Draw(r,Gpu::DrawCall{.count=3});CheckFrame(renderer,device,address+16384,0);
    r[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=std::uint32_t((address+32768)>>8);
    r[Reg::CB_COLOR0_BASE+Reg::CB_INFO]|=1u<<13;r[Reg::CB_COLOR0_BASE+Reg::CB_CMASK]=std::uint32_t((address+98304)>>8);
    r[Reg::CB_COLOR0_BASE+Reg::CB_CLEAR_WORD0]=0xff00ff00;
    r[Reg::DB_Z_INFO]=3|(1u<<29);r[Reg::DB_Z_READ_BASE]=r[Reg::DB_Z_WRITE_BASE]=std::uint32_t((address+65536)>>8);
    r[Reg::DB_STENCIL_INFO]=1;r[Reg::DB_STENCIL_WRITE_BASE]=std::uint32_t((address+81920)>>8);r[Reg::DB_HTILE_DATA_BASE]=std::uint32_t((address+114688)>>8);
    r[Reg::DB_DEPTH_CONTROL]=6|(1u<<4);r[Reg::DB_DEPTH_CLEAR]=std::bit_cast<std::uint32_t>(1.f);
    renderer.Draw(r,Gpu::DrawCall{.count=3});assert(renderer.DmaData(0,address+98304,2,0,256));
    for(unsigned k=0;k<65536;++k)renderer.Gds()[k]=std::uint8_t(k*37+19);
    std::string error;auto capture=renderer.CaptureCheckpoint(Deadline(),error);
    if(!capture){std::fprintf(stderr,"Capture failed: %s\n",error.c_str());std::abort();}
    BbCheckpointMemoryLayout layout{};assert(!runtime_memory_checkpoint_export(&layout,nullptr,0,nullptr,0));
    std::vector<BbCheckpointMemoryBlock> blocks(layout.block_count);std::vector<BbCheckpointMemoryVma> vmas(layout.vma_count);
    assert(!runtime_memory_checkpoint_export(&layout,blocks.data(),blocks.size(),vmas.data(),vmas.size()));
    std::vector<std::uint8_t> maps;Append(maps,&layout,1);Append(maps,blocks.data(),blocks.size());Append(maps,vmas.data(),vmas.size());
    std::vector<StreamSection> sections;sections.push_back(Bytes(SectionKind::MemoryLayout,0,std::move(maps)));
    std::vector<std::uint8_t> fixture;Append(fixture,&address,1);sections.push_back(Bytes(SectionKind::Fixture,0,std::move(fixture)));
    for(const auto& b:blocks)sections.push_back({SectionKind::MemoryBytes,1,b.physical,b.size,[physical=b.physical](std::uint64_t offset,std::span<std::uint8_t> out,std::string&) {
        return !runtime_memory_checkpoint_read(physical+offset,out.data(),out.size());
    }});
    for(const auto& c:capture->Chunks())sections.push_back({SectionKind::Gpu,1,c.id,c.size,c.read});
    assert(SaveStream(path,Identity,sections,error));capture.reset();
    // Make the live targets differ from the file before this process exits.
    r[Reg::CB_COLOR_CONTROL]=2u<<4;renderer.Draw(r,Gpu::DrawCall{.count=3});CheckFrame(renderer,device,address+32768,255);
    std::memset(renderer.Gds(),0,65536);
    std::puts("Metal renderer saved through streaming archive; live targets and GDS deliberately diverged.");
}
void Restore(const char* path) {
    VerifiedArchive archive;std::string error;assert(OpenVerified(path,Identity,archive,error));
    auto read=[&](std::size_t n){std::vector<std::uint8_t> bytes(archive.Sections()[n].size);assert(archive.Read(n,0,bytes,error));return bytes;};
    auto maps=read(0);assert(archive.Sections()[0].kind==SectionKind::MemoryLayout && maps.size()>=sizeof(BbCheckpointMemoryLayout));
    BbCheckpointMemoryLayout layout;std::memcpy(&layout,maps.data(),sizeof(layout));assert(layout.block_count<=4096 && layout.vma_count<=65536);
    assert(maps.size()==sizeof(layout)+layout.block_count*sizeof(BbCheckpointMemoryBlock)+layout.vma_count*sizeof(BbCheckpointMemoryVma));
    std::vector<BbCheckpointMemoryBlock> blocks(layout.block_count);std::vector<BbCheckpointMemoryVma> vmas(layout.vma_count);
    std::memcpy(blocks.data(),maps.data()+sizeof(layout),blocks.size()*sizeof(blocks[0]));std::memcpy(vmas.data(),maps.data()+sizeof(layout)+blocks.size()*sizeof(blocks[0]),vmas.size()*sizeof(vmas[0]));
    assert(!runtime_memory_checkpoint_restore(&layout,blocks.data(),vmas.data()));
    std::vector<Renderer::CheckpointChunk> chunks;
    for(std::size_t n=2;n<archive.Sections().size();++n) {
        const auto& s=archive.Sections()[n];assert(s.version==1);
        if(s.kind==SectionKind::MemoryBytes) {auto bytes=read(n);assert(!runtime_memory_checkpoint_write(s.id,bytes.data(),bytes.size()));}
        else {assert(s.kind==SectionKind::Gpu);chunks.push_back({s.id,s.size,[&,n](std::uint64_t offset,std::span<std::uint8_t> bytes,std::string& why){return archive.Read(n,offset,bytes,why);}});}
    }
    auto fixture=read(1);assert(fixture.size()==8 && archive.Sections()[1].kind==SectionKind::Fixture);std::uint64_t address;std::memcpy(&address,fixture.data(),8);
    Gpu::Metal::Device device;Renderer renderer(device);
    // Bad metadata/extra chunks/plane sizes must leave this fresh renderer usable.
    auto bad=chunks;bad.push_back(chunks[0]);assert(!renderer.RestoreCheckpoint(bad,Deadline(),error));
    bad=chunks;--bad.back().size;assert(!renderer.RestoreCheckpoint(bad,Deadline(),error));
    assert(!renderer.RestoreCheckpoint(chunks,Clock::now()-std::chrono::seconds(1),error));
    if(!renderer.RestoreCheckpoint(chunks,Deadline(),error)){std::fprintf(stderr,"Restore failed: %s\n",error.c_str());std::abort();}
    for(unsigned k=0;k<65536;++k)assert(renderer.Gds()[k]==std::uint8_t(k*37+19));
    // Compare all bytes and logical metadata before continuing GPU execution.
    auto capture=renderer.CaptureCheckpoint(Deadline(),error);assert(capture && capture->Chunks().size()==chunks.size());
    std::map<std::uint64_t,const Renderer::CheckpointChunk*> original;for(const auto& c:chunks)original[c.id]=&c;
    for(const auto& c:capture->Chunks()) {
        const auto& old=*original.at(c.id);assert(old.size==c.size);
        for(std::uint64_t offset=0;offset<c.size;) {
            const auto count=std::min<std::uint64_t>(65536,c.size-offset);std::vector<std::uint8_t> a(count),b(count);
            assert(c.read(offset,a,error) && old.read(offset,b,error) && a==b);offset+=count;
        }
    }
    capture.reset();assert(!renderer.RestoreCheckpoint(chunks,Deadline(),error));
    CheckFrame(renderer,device,address+16384,0);
    auto r=Registers(address);r[Reg::CB_COLOR0_BASE+Reg::CB_BASE]=std::uint32_t((address+32768)>>8);
    r[Reg::CB_COLOR0_BASE+Reg::CB_INFO]|=1u<<13;r[Reg::CB_COLOR0_BASE+Reg::CB_CMASK]=std::uint32_t((address+98304)>>8);
    r[Reg::CB_COLOR0_BASE+Reg::CB_CLEAR_WORD0]=0xff00ff00;r[Reg::CB_COLOR_CONTROL]=2u<<4;
    renderer.Draw(r,Gpu::DrawCall{.count=3});CheckFrame(renderer,device,address+32768,255);
    std::puts("Fresh-process Metal renderer: exact target/depth/stencil/GDS/CE state, pending clear and continued GPU execution passed.");
}
}
int main(int argc,char** argv) {
    @autoreleasepool {
        if(!MTLCreateSystemDefaultDevice())return 77;
        assert(argc==3);if(!std::strcmp(argv[1],"--save"))Save(argv[2]);else {assert(!std::strcmp(argv[1],"--restore"));Restore(argv[2]);}
    }
}
