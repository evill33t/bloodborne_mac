// SPDX-License-Identifier: GPL-2.0-or-later
// Execute translated GCN image instructions on known depth texels. A filtered
// comparison must compare the four depths first, then filter their results.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/metal/mtl_compiler.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>

extern "C" void runtime_restart() { std::abort(); }
using namespace Gpu::Shader;
using namespace Gpu::Shader::Gcn;

static Program SampleProgram(Mimg op) {
    Program p;
    p.stage=Stage::Fragment;
    p.mrt_mask=1;
    for (unsigned k=0;k<3;++k) {
        Inst mov;
        mov.pc=k*4; mov.enc=Enc::VOP1; mov.op=uint16_t(Vop::V_MOV_B32);
        mov.dst=k; mov.src[0]=Operand::FloatHalf;
        p.code.push_back(mov);
    }
    Inst image;
    image.pc=12; image.enc=Enc::MIMG; image.size=2;
    image.op=uint16_t(op); image.vaddr=0; image.vdata=3; image.dmask=1;
    p.code.push_back(image);
    p.image_slot_by_pc.emplace_back(12,0);
    ImageResource resource;
    resource.compare=true; resource.has_sampler=true;
    p.images.push_back(resource); p.samplers.emplace_back();
    Inst exp;
    exp.pc=20; exp.enc=Enc::EXP; exp.size=2; exp.en=15; exp.done=true;
    if (op==Mimg::IMAGE_GATHER4_C) {
        for (unsigned k=0;k<4;++k) exp.vsrc[k]=3+k;
    } else {
        for (unsigned k=0;k<4;++k) exp.vsrc[k]=3;
    }
    p.code.push_back(exp);
    Inst end; end.pc=28; end.enc=Enc::SOPP; end.op=uint16_t(Sopp::S_ENDPGM);
    p.code.push_back(end);
    return p;
}

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if (!device) return 77;
        id<MTLCommandQueue> queue=[device newCommandQueue];
        Gpu::Metal::ShaderCompiler compiler(device,1);
        const std::string vertex=R"(
            #include <metal_stdlib>
            using namespace metal;
            vertex float4 main0(uint vid [[vertex_id]]) {
                float2 p=float2((vid<<1)&2,vid&2);
                return float4(p*2.0f-1.0f,0.0f,1.0f);
            })";
        id<MTLFunction> vs=compiler.Function(1,vertex,0); assert(vs);
        auto* dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
            width:2 height:2 mipmapped:NO];
        dd.storageMode=MTLStorageModePrivate;
        dd.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> depth=[device newTextureWithDescriptor:dd]; assert(depth);
        const float texels[]{0,1,1,1};
        id<MTLBuffer> upload=[device newBufferWithBytes:texels length:sizeof(texels)
            options:MTLResourceStorageModeShared];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        auto blit=[command blitCommandEncoder];
        [blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:8 sourceBytesPerImage:16
            sourceSize:MTLSizeMake(2,2,1) toTexture:depth destinationSlice:0 destinationLevel:0
            destinationOrigin:MTLOriginMake(0,0,0)];
        [blit endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
        auto* cd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
            width:1 height:1 mipmapped:NO];
        cd.storageMode=MTLStorageModeShared; cd.usage=MTLTextureUsageRenderTarget;
        id<MTLTexture> color=[device newTextureWithDescriptor:cd]; assert(color);
        // With less / greater comparisons, 3/4 and 1/4 of the four texels pass.
        // Comparing the filtered depth instead would return 1 and 0.
        unsigned key=2;
        for (bool gather : {false,true}) for (bool greater : {false,true}) {
            const Program p=SampleProgram(gather ? Mimg::IMAGE_GATHER4_C : Mimg::IMAGE_SAMPLE_C_LZ);
            MslOptions options;
            options.col_format=9;
            TextureType type; type.native_depth=true; type.compare=greater ? 4 : 1;
            options.textures.push_back(type);
            std::vector<std::string> unsupported;
            const auto source=GenerateMsl(p,options,&unsupported); assert(unsupported.empty());
            id<MTLFunction> ps=compiler.Function(key++,source,0); assert(ps);
            auto* pd=[MTLRenderPipelineDescriptor new];
            pd.vertexFunction=vs; pd.fragmentFunction=ps;
            pd.colorAttachments[0].pixelFormat=cd.pixelFormat;
            NSError* error=nil;
            auto pipeline=[device newRenderPipelineStateWithDescriptor:pd error:&error];
            if (!pipeline) { fprintf(stderr,"%s\n",error.localizedDescription.UTF8String); std::abort(); }
            auto* sd=[MTLSamplerDescriptor new];
            sd.minFilter=sd.magFilter=MTLSamplerMinMagFilterLinear;
            sd.sAddressMode=sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
            sd.compareFunction=greater ? MTLCompareFunctionGreater : MTLCompareFunctionLess;
            auto sampler=[device newSamplerStateWithDescriptor:sd]; assert(sampler);
            command=[queue commandBuffer];
            auto* pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture=color;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;
            pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            auto encoder=[command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:pipeline];
            [encoder setFragmentTexture:depth atIndex:0];
            [encoder setFragmentSamplerState:sampler atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted]; assert(!command.error);
            float rgba[4]{};
            [color getBytes:rgba bytesPerRow:16 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
            const float result=gather ? rgba[0]+rgba[1]+rgba[2]+rgba[3] : rgba[0];
            const float expected=gather ? (greater ? 1 : 3) : (greater ? .25f : .75f);
            if (std::abs(result-expected)>.0001f) {
                fprintf(stderr,"%s shadow comparison: expected %g got %g\n",gather ? "gather" : "filtered",expected,result);
                std::abort();
            }
        }
        puts("Metal shadows: translated GCN filtered and gathered depth comparisons passed");
    }
}
