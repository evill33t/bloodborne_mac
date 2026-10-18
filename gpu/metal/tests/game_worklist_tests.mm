// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the append/consume + masked lane-count patterns in retail compute
// shaders. Check list indices and counter isolation independently of rendering.
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/metal/mtl_compiler.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Gpu::Shader;
using namespace Gpu::Shader::Gcn;
extern "C" void runtime_restart() { std::abort(); }

static Program Worklist(bool consume,bool sparse) {
    Program p;p.stage=Stage::Compute;p.num_user_sgprs=5;p.writes_exec=sparse;
    const auto add=[&](Enc enc,unsigned op) -> Inst& {
        Inst i;i.enc=enc;i.op=op;i.pc=p.code.size()*8;i.size=2;i.sdst=Operand::VccLo;
        p.code.push_back(i);return p.code.back();
    };
    auto& m0=add(Enc::SOP1,unsigned(Sop1::S_MOV_B32));m0.dst=Operand::M0;m0.src[0]=4;
    if (sparse) {
        auto& bit=add(Enc::VOP2,unsigned(Vop::V_AND_B32));
        bit.dst=1;bit.src[0]=Operand::Zero+1;bit.src[1]=Operand::Vgpr0;
        auto& cmp=add(Enc::VOPC,unsigned(Vop::V_CMP_U32)+2); // equal
        cmp.src[0]=Operand::Zero;cmp.src[1]=Operand::Vgpr0+1;cmp.sdst=Operand::VccLo;
        auto& mask=add(Enc::SOP1,unsigned(Sop1::S_AND_SAVEEXEC_B64));
        mask.dst=6;mask.src[0]=Operand::VccLo;
    }
    if (consume) {
        auto& ds=add(Enc::DS,unsigned(Ds::DS_CONSUME));ds.dst=2;ds.gds=true;ds.offset0=4;
    }
    auto& hi=add(Enc::VOP2,unsigned(Vop::V_MBCNT_HI_U32_B32));
    hi.dst=3;hi.src[0]=Operand::ExecHi;hi.src[1]=Operand::Zero;
    auto& lo=add(Enc::VOP2,unsigned(Vop::V_MBCNT_LO_U32_B32));
    lo.dst=3;lo.src[0]=Operand::ExecLo;lo.src[1]=Operand::Vgpr0+3;
    if (!consume) {
        auto& ds=add(Enc::DS,unsigned(Ds::DS_APPEND));ds.dst=2;ds.gds=true;ds.offset0=4;
    }
    auto& index=add(Enc::VOP2,unsigned(consume ? Vop::V_SUB_I32 : Vop::V_ADD_I32));
    index.dst=2;index.src[0]=Operand::Vgpr0+2;index.src[1]=Operand::Vgpr0+3;
    if (consume) {
        auto& dec=add(Enc::VOP2,unsigned(Vop::V_SUB_I32));
        dec.dst=2;dec.src[0]=Operand::Vgpr0+2;dec.src[1]=Operand::Zero+1;
    }
    auto& store=add(Enc::MUBUF,unsigned(Mubuf::BUFFER_STORE_DWORD));
    store.vaddr=0;store.vdata=2;store.idxen=true;store.soffset=Operand::Zero;
    add(Enc::SOPP,unsigned(Sopp::S_ENDPGM));
    return p;
}

int main() {
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice();if (!device) return 77;
        auto queue=[device newCommandQueue];
        Gpu::Metal::ShaderCompiler compiler(device,1);
        auto memory=[device newBufferWithLength:16384 options:MTLResourceStorageModeShared];
        auto gds=[device newBufferWithLength:65536 options:MTLResourceStorageModeShared];
        // One virtual 16 KiB page, directly backed by memory at physical zero.
        struct Map {uint64_t base,pages,split;uint32_t shift,pad;uint64_t limit;int32_t page;};
        const Map map{0,1,16384,14,0,16384,0};
        unsigned key=1,cases=0;
        for (bool consume:{true,false}) for (bool sparse:{false,true}) {
            std::vector<std::string> unsupported;
            const auto source=GenerateMsl(Worklist(consume,sparse),{},&unsupported);
            assert(unsupported.empty());
            auto fn=compiler.Function(key++,source,0);assert(fn);
            NSError* error=nil;
            auto pipeline=[device newComputePipelineStateWithFunction:fn error:&error];
            if (!pipeline) {fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
            for (unsigned count:{32u,64u}) for (unsigned base:{0u,32u}) {
                std::memset(memory.contents,0xff,memory.length);
                auto* counters=static_cast<uint32_t*>(gds.contents);
                std::fill(counters,counters+gds.length/4,1000u);
                const uint32_t ud[]{4096,4u<<16,64,0x24004,(base<<16)|(base ? 512u : 0u)};
                auto command=[queue commandBuffer];auto encoder=[command computeCommandEncoder];
                [encoder setComputePipelineState:pipeline];
                [encoder setBytes:ud length:sizeof(ud) atIndex:0];
                [encoder setBuffer:memory offset:0 atIndex:1];
                [encoder setBuffer:memory offset:0 atIndex:2];
                [encoder setBytes:&map length:sizeof(map) atIndex:3];
                [encoder setBuffer:gds offset:0 atIndex:5];
                [encoder setThreadgroupMemoryLength:1024 atIndex:0];
                [encoder dispatchThreads:MTLSizeMake(count,1,1) threadsPerThreadgroup:MTLSizeMake(count,1,1)];
                [encoder endEncoding];[command commit];[command waitUntilCompleted];assert(!command.error);
                const auto* output=static_cast<const uint32_t*>(memory.contents)+1024;
                std::vector<uint32_t> indices;
                for (unsigned lane=0;lane<count;++lane) {
                    if (sparse && (lane&1)) {assert(output[lane]==0xffffffffu);continue;}
                    indices.push_back(output[lane]);
                }
                std::sort(indices.begin(),indices.end());
                const unsigned first=consume ? 1000-indices.size() : 1000;
                for (unsigned i=0;i<indices.size();++i) if (indices[i]!=first+i) {
                    fprintf(stderr,"%s sparse=%u lanes=%u base=%u index %u: expected %u got %u\n",
                        consume ? "consume" : "append",sparse,count,base,i,first+i,indices[i]);
                    return 1;
                }
                const unsigned target=(base+4)/4;
                for (unsigned i=0;i<gds.length/4;++i) {
                    const unsigned expected=i==target ? (consume ? first : first+indices.size()) : 1000;
                    if (counters[i]!=expected) {
                        fprintf(stderr,"Counter %u: expected %u got %u\n",i,expected,counters[i]);return 1;
                    }
                }
                ++cases;
            }
        }
        printf("Metal work lists: %u append/consume, masked lanes and GDS base-isolation cases passed\n",cases);
    }
}
