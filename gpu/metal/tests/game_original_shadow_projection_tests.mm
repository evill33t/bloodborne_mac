// SPDX-License-Identifier: GPL-2.0-or-later
// Private-input regression: execute the original four-cascade shadow-mask PS
// against synthetic geometry, matrices and depth texels. No game bytes ship here.
// Usage: test /absolute/path/to/captured-shaders
// This tests shader arithmetic/projection/PCF, not live uniform ownership.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gpu/shader/msl.h"
#include "gpu/metal/mtl_compiler.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void runtime_restart() { std::abort(); }
namespace {
namespace S=Gpu::Shader;
using namespace S::Gcn;
constexpr u64 ShaderHash=0x513b8cfa1ec711b2ull;
constexpr unsigned Atlas=32,Quadrant=Atlas/2,MemoryBytes=65536;
constexpr u64 Root=0x1000,Matrices=0x2000,Filter=0x3000;
using Matrix=std::array<std::array<float,4>,4>;
struct Camera {float angle,tx,ty,tz;};
struct Point {float x,y,z;};
void Require(bool condition,const std::string& message) {
    if(!condition) throw std::runtime_error(message);
}
std::unique_ptr<S::Program> Load(const std::filesystem::path& directory) {
    std::filesystem::path path;
    for(const auto& entry:std::filesystem::directory_iterator(directory)) {
        const auto name=entry.path().filename().string();
        if(entry.is_regular_file() && name.starts_with("ps_") &&
           name.ends_with("_513b8cfa1ec711b2.bin")) {path=entry.path();break;}
    }
    Require(!path.empty(),"Original shadow PS capture is missing");
    const auto size=std::filesystem::file_size(path);
    Require(size>=8 && size<=65536 && !(size&3),"Invalid original shader length");
    std::vector<u32> code(size/4+64,0xbf810000u);
    std::ifstream input(path,std::ios::binary);
    Require(bool(input.read(reinterpret_cast<char*>(code.data()),size)),"Cannot read shader capture");
    std::array<u32,16> ud{};
    // Retained main0 prologue initializes s0..s15 from UD and s16 onward to
    // zero. The current Program hash also binds (USER_SGPR=16,VGPR_COMP_CNT=0,
    // COMPUTE_RSRC2=0); do not infer a usable register ABI from code alone.
    S::BuildInput build{.stage=S::Stage::Fragment,.address=0,.user_data=ud.data(),
        .num_user_sgprs=16,.vgpr_comp_cnt=0,.code=code.data()};
    auto p=S::BuildProgram(build);
    Require(p && p->hash==ShaderHash && p->num_user_sgprs==16 && p->vgpr_comp_cnt==0,
        "Original shadow PS hash/register metadata mismatch");
    return p;
}
Matrix Identity() {
    Matrix result{};for(unsigned k=0;k<4;++k)result[k][k]=1;return result;
}
Matrix CameraToWorld(const Camera& camera) {
    const float c=std::cos(camera.angle),s=std::sin(camera.angle);
    return {{{c,0,s,camera.tx},{0,1,0,camera.ty},{-s,0,c,camera.tz},{0,0,0,1}}};
}
Point World(const Camera& camera,Point view) {
    const float c=std::cos(camera.angle),s=std::sin(camera.angle);
    return {c*view.x+s*view.z+camera.tx,view.y+camera.ty,
        -s*view.x+c*view.z+camera.tz};
}
Point View(const Camera& camera,Point world) {
    const float c=std::cos(camera.angle),s=std::sin(camera.angle);
    const float x=world.x-camera.tx,z=world.z-camera.tz;
    return {c*x-s*z,world.y-camera.ty,s*x+c*z};
}
Matrix Light(unsigned cascade) {
    // Each cascade occupies one atlas quadrant. Projective W is deliberately
    // nonconstant; simply omitting/reordering division by W must fail.
    const float u=(cascade&1) ? .75f : .25f,v=(cascade&2) ? .75f : .25f;
    return {{{.1f+u*.04f,0,u*.03f,u},{v*.04f,.1f,v*.03f,v},
        {.45f*.04f,0,.35f+.45f*.03f,.45f},{.04f,0,.03f,1}}};
}
Matrix Product(const Matrix& left,const Matrix& right) {
    Matrix result{};
    for(unsigned r=0;r<4;++r) for(unsigned c=0;c<4;++c)
        for(unsigned k=0;k<4;++k)result[r][c]+=left[r][k]*right[k][c];
    return result;
}
float Texel(int x,int y) {
    if(x<0 || y<0 || x>=int(Atlas) || y>=int(Atlas))return 0;
    // Distinct quadrant phases expose a wrong cascade index. Distinct X/Y
    // coefficients expose matrix transposes and swapped UV components.
    const int cascade=x/int(Quadrant)+2*(y/int(Quadrant));
    return (float(((x%Quadrant)*5+(y%Quadrant)*3+cascade*2)%8)+.5f)/8;
}
float Compare(float reference,float value,bool greater) {
    return greater ? reference>value : reference<value;
}
float Pcf(float u,float v,float reference,bool greater) {
    const double x=double(u)*Atlas-.5,y=double(v)*Atlas-.5;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const double fx=x-ix,fy=y-iy;
    // Independent scalar bilinear comparison oracle: compare texels first.
    return float((1-fy)*((1-fx)*Compare(reference,Texel(ix,iy),greater)+
                         fx*Compare(reference,Texel(ix+1,iy),greater))+
                      fy*((1-fx)*Compare(reference,Texel(ix,iy+1),greater)+
                         fx*Compare(reference,Texel(ix+1,iy+1),greater)));
}
using ControlValues=std::array<std::array<float,4>,5>;
ControlValues GeometricControl(const Camera& camera,Point view,float a,float b) {
    const Point world=World(camera,view);
    const unsigned cascade=unsigned(view.z>.25f)+unsigned(view.z>.5f)+unsigned(view.z>.75f);
    const float w=.04f*world.x+.03f*world.z+1;
    const float u=((cascade&1) ? .75f : .25f)+.1f*world.x/w;
    const float v=((cascade&2) ? .75f : .25f)+.1f*world.y/w;
    const float reference=.45f+.35f*world.z/w;
    // The original PS fades shadow opacity with view-space distance.
    const float fade=std::clamp(1000.0f-std::sqrt(view.x*view.x+view.y*view.y+view.z*view.z),0.f,1.f);
    return {{{u+b,v-a,reference,0},{u+a,v+b,reference,0},
        {u-a,v-b,reference,0},{u-b,v+a,reference,0},{fade,0,0,0}}};
}
float Analytic(const ControlValues& control,bool greater) {
    float shadow=0;
    for(unsigned k=0;k<4;++k)shadow+=Pcf(control[k][0],control[k][1],control[k][2],greater)*.25f;
    const float fade=control[4][0];
    return 1-fade*(1-shadow);
}
void Descriptor(unsigned char* memory,u64 at,u64 base,u32 stride,u32 records) {
    const u32 words[]{u32(base),u32(base>>32)|(stride<<16),records,0x77fac};
    std::memcpy(memory+at,words,sizeof(words));
}
void Run(const std::filesystem::path& directory,id<MTLDevice> device) {
    const auto program=Load(directory);
    S::MslOptions options;options.col_format=4;options.vs_param_mask=3;options.ps_input_addr=2;
    for(unsigned k=0;k<32;++k)options.ps_input_cntl[k]=k;
    Require(program->images.size()==2 && program->samplers.size()==2,"Unexpected original resource layout");
    options.textures.resize(2);
    options.textures[0].dim=options.textures[1].dim=S::TexDim::Tex2D;
    options.textures[0].sample=options.textures[1].sample=S::TexSample::Float;
    options.textures[1].native_depth=true;
    S::GpuMemMap mapping{0,MemoryBytes/16384,MemoryBytes,14,0,MemoryBytes};
    std::array<s32,MemoryBytes/16384> entries{};
    auto memory=[device newBufferWithLength:MemoryBytes options:MTLResourceStorageModeShared];
    auto map=[device newBufferWithLength:sizeof(mapping)+sizeof(entries) options:MTLResourceStorageModeShared];
    Require(memory && map,"Cannot allocate synthetic guest memory");
    std::memcpy(map.contents,&mapping,sizeof(mapping));
    std::memcpy(static_cast<unsigned char*>(map.contents)+sizeof(mapping),entries.data(),sizeof(entries));
    Gpu::Metal::ShaderCompiler compiler(device,1);
    const std::string vertex=R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Out {float4 pos [[position]];float4 p0 [[user(p0)]];float4 p1 [[user(p1)]];};
        vertex Out main0(uint id [[vertex_id]],constant float4& at [[buffer(0)]]) {
            float2 uv=float2((id<<1)&2,id&2);
            return {float4(uv*2-1,0,1),at,float4(uv,0,1)};
        })";
    auto vs=compiler.Function(1,vertex,0);
    Require(bool(vs),"Cannot compile shadow projection fixture vertex shader");
    // This control has no matrix, cascade, reciprocal, or buffer-descriptor
    // code. CPU geometry supplies each projected UV/reference independently.
    // It isolates native sampler precision from the original PS arithmetic.
    const std::string control_source=R"(
        #include <metal_stdlib>
        using namespace metal;
        fragment float4 main0(depth2d<float> atlas [[texture(0)]],sampler comparison [[sampler(0)]],
            constant float4* geometry [[buffer(0)]]) {
            float result=0;
            for(uint k=0;k<4;++k)
                result+=atlas.sample_compare(comparison,geometry[k].xy,geometry[k].z,level(0.0f))*.25f;
            return float4(0,0,0,1-geometry[4].x*(1-result));
        })";
    auto control_ps=compiler.Function(4,control_source,0);
    Require(bool(control_ps),"Cannot compile native shadow sampling control");
    auto control_descriptor=[MTLRenderPipelineDescriptor new];
    control_descriptor.vertexFunction=vs;control_descriptor.fragmentFunction=control_ps;
    control_descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA32Float;
    NSError* control_error=nil;
    auto control_pipeline=[device newRenderPipelineStateWithDescriptor:control_descriptor error:&control_error];
    Require(bool(control_pipeline),control_error ? control_error.localizedDescription.UTF8String : "Cannot create native shadow sampling control");
    std::array<id<MTLRenderPipelineState>,2> pipelines{};
    for(unsigned mode=0;mode<2;++mode) {
        options.textures[1].compare=mode ? 4 : 1;
        std::vector<std::string> unsupported;
        const auto source=S::GenerateMsl(*program,options,&unsupported);
        Require(unsupported.empty(),"Original shadow shader has unsupported instructions");
        // Native depth comparison delegates compare-before-filter to Metal's
        // bound sampler. Keep the descriptor mode consistent too; accidentally
        // taking software comparison would fail this structural prerequisite.
        size_t taps=0,position=0;
        while((position=source.find("t1.sample_compare(",position))!=std::string::npos) {++taps;++position;}
        Require(taps==4,"Original depth texture did not use four native comparison samples");
        auto ps=compiler.Function(2+mode,source,0);
        Require(bool(ps),"Cannot compile original shadow projection fixture");
        auto pipeline_descriptor=[MTLRenderPipelineDescriptor new];
        pipeline_descriptor.vertexFunction=vs;pipeline_descriptor.fragmentFunction=ps;
        pipeline_descriptor.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA32Float;
        NSError* error=nil;
        pipelines[mode]=[device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:&error];
        Require(bool(pipelines[mode]),error ? error.localizedDescription.UTF8String : "Cannot create pipeline");
    }
    auto queue=[device newCommandQueue];
    auto atlas_descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
        width:Atlas height:Atlas mipmapped:NO];
    atlas_descriptor.storageMode=MTLStorageModePrivate;atlas_descriptor.usage=MTLTextureUsageShaderRead;
    auto atlas=[device newTextureWithDescriptor:atlas_descriptor];
    std::array<float,Atlas*Atlas> depths{};
    for(unsigned y=0;y<Atlas;++y)for(unsigned x=0;x<Atlas;++x)depths[y*Atlas+x]=Texel(x,y);
    auto upload=[device newBufferWithBytes:depths.data() length:sizeof(depths) options:MTLResourceStorageModeShared];
    auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
    [blit copyFromBuffer:upload sourceOffset:0 sourceBytesPerRow:Atlas*4 sourceBytesPerImage:sizeof(depths)
        sourceSize:MTLSizeMake(Atlas,Atlas,1) toTexture:atlas destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(0,0,0)];
    [blit endEncoding];[command commit];[command waitUntilCompleted];Require(!command.error,"Atlas upload failed");
    auto sampled_descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
        width:1 height:1 mipmapped:NO];
    sampled_descriptor.storageMode=MTLStorageModeShared;sampled_descriptor.usage=MTLTextureUsageShaderRead;
    auto sampled=[device newTextureWithDescriptor:sampled_descriptor];
    auto output_descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float
        width:1 height:1 mipmapped:NO];
    output_descriptor.storageMode=MTLStorageModeShared;output_descriptor.usage=MTLTextureUsageRenderTarget;
    auto output=[device newTextureWithDescriptor:output_descriptor];
    auto control_output=[device newTextureWithDescriptor:output_descriptor];
    Require(atlas && upload && sampled && output && control_output && queue,"Cannot allocate fixture textures/queue");
    const std::array<Camera,3> cameras{{{0,0,0,0},{.37f,.23f,-.12f,.08f},{-.61f,-.19f,.17f,-.06f}}};
    const std::array<Point,4> points{{{-.75f,-.3f,0},{.4f,.6f,0},{.05f,-.17f,0},{.8f,-.7f,0}}};
    const std::array<float,9> ranges{.125f,.25f,.375f,.5f,.625f,.75f,.875f,
        std::nextafter(.25f,1.f),std::nextafter(.75f,1.f)};
    unsigned cases=0;
    float maximum_hardware_analytic_error=0,maximum_original_control_error=0;
    unsigned maximum_hardware_analytic_case=0,maximum_original_control_case=0;
    for(bool fixed_world:{false,true})for(bool greater:{false,true})for(bool taps:{false,true})
    for(const auto& camera:cameras)for(Point point:points)for(float range:ranges) {
        point.z=range;
        const Point view=fixed_world ? View(camera,point) : point;
        const float a=taps ? .5f/Atlas : 0,b=taps ? 1.5f/Atlas : 0;
        const auto control=GeometricControl(camera,view,a,b);
        auto* bytes=static_cast<unsigned char*>(memory.contents);std::memset(bytes,0,MemoryBytes);
        Descriptor(bytes,Root+0x30,Matrices,16,21);Descriptor(bytes,Root+0x40,Filter,16,32);
        const auto identity=Identity();std::memcpy(bytes+Matrices,&identity,sizeof(identity));
        for(unsigned k=0;k<4;++k) {
            const auto light=Product(Light(k),CameraToWorld(camera));
            std::memcpy(bytes+Matrices+64+64*k,&light,sizeof(light));
        }
        const float splits[]{.25f,.5f,.75f,1};std::memcpy(bytes+Matrices+0x140,splits,sizeof(splits));
        const float filter[]{1000,1,a,b};std::memcpy(bytes+Filter+0x120,filter,sizeof(filter));
        std::array<u32,16> ud{};ud[2]=u32(Root);ud[3]=u32(Root>>32);
        [sampled replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&view.z bytesPerRow:4];
        auto descriptor=[MTLSamplerDescriptor new];
        descriptor.minFilter=descriptor.magFilter=MTLSamplerMinMagFilterLinear;
        descriptor.sAddressMode=descriptor.tAddressMode=MTLSamplerAddressModeClampToBorderColor;
        descriptor.borderColor=MTLSamplerBorderColorTransparentBlack;
        auto plain=[device newSamplerStateWithDescriptor:descriptor];
        descriptor.compareFunction=greater ? MTLCompareFunctionGreater : MTLCompareFunctionLess;
        auto comparison=[device newSamplerStateWithDescriptor:descriptor];
        const float vertex_data[]{view.x,view.y,0,1};
        command=[queue commandBuffer];auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=output;pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        auto encoder=[command renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipelines[greater ? 1 : 0]];
        [encoder setVertexBytes:vertex_data length:sizeof(vertex_data) atIndex:0];
        [encoder setFragmentBytes:ud.data() length:sizeof(ud) atIndex:0];
        [encoder setFragmentBuffer:memory offset:0 atIndex:1];[encoder setFragmentBuffer:memory offset:0 atIndex:2];
        [encoder setFragmentBuffer:map offset:0 atIndex:3];
        [encoder setFragmentTexture:sampled atIndex:0];[encoder setFragmentTexture:atlas atIndex:1];
        [encoder setFragmentSamplerState:plain atIndex:0];[encoder setFragmentSamplerState:comparison atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];
        auto control_pass=[MTLRenderPassDescriptor renderPassDescriptor];
        control_pass.colorAttachments[0].texture=control_output;
        control_pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        control_pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        encoder=[command renderCommandEncoderWithDescriptor:control_pass];
        [encoder setRenderPipelineState:control_pipeline];
        [encoder setVertexBytes:vertex_data length:sizeof(vertex_data) atIndex:0];
        [encoder setFragmentBytes:control.data() length:sizeof(control) atIndex:0];
        [encoder setFragmentTexture:atlas atIndex:0];[encoder setFragmentSamplerState:comparison atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [encoder endEncoding];[command commit];[command waitUntilCompleted];
        Require(!command.error,"Original shadow shader execution failed");
        float rgba[4]{};[output getBytes:rgba bytesPerRow:16 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
        float hardware[4]{};[control_output getBytes:hardware bytesPerRow:16 fromRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0];
        const float expected=Analytic(control,greater);
        const float hardware_analytic_error=std::abs(hardware[3]-expected);
        const float original_control_error=std::abs(rgba[3]-hardware[3]);
        if(hardware_analytic_error>maximum_hardware_analytic_error) {
            maximum_hardware_analytic_error=hardware_analytic_error;maximum_hardware_analytic_case=cases;
        }
        if(original_control_error>maximum_original_control_error) {
            maximum_original_control_error=original_control_error;maximum_original_control_case=cases;
        }
        // Primary equivalence bound is unchanged. Original output explicitly
        // truncates to FP16 (below 2^-11 error for alpha in [0,1)); .0012 leaves
        // room for floating projection arithmetic while hardware filtering is
        // shared. The scalar oracle is only a coarse geometric sanity gate:
        // .01 is an explicit test ceiling, not a claimed Metal precision spec.
        if(!std::isfinite(rgba[3]) || !std::isfinite(hardware[3]) ||
           original_control_error>.0012f || hardware_analytic_error>.01f) {
            char message[512];std::snprintf(message,sizeof(message),
                "Shadow case %u fixedWorld=%d greater=%d taps=%d camera=%g view=(%g,%g,%g): analytic %.9g native-control %.9g original %.9g (control error %.9g analytic error %.9g)",
                cases,fixed_world,greater,taps,camera.angle,view.x,view.y,view.z,expected,hardware[3],rgba[3],original_control_error,hardware_analytic_error);
            throw std::runtime_error(message);
        }
        ++cases;
    }
    std::printf("Original shadow PS: %u analytic cascade, projective camera/light, fixed-world and rotated four-tap PCF cases passed\n",cases);
    std::printf("Maximum native-control versus scalar analytic alpha error: %.9g (case %u); maximum original versus native-control error: %.9g (case %u)\n",
        maximum_hardware_analytic_error,maximum_hardware_analytic_case,maximum_original_control_error,maximum_original_control_case);
}
}
int main(int argc,char** argv) {
    if(argc!=2) {std::puts("Private captured shadow shader directory required; skipped");return 77;}
    @autoreleasepool {
        auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
        try {Run(argv[1],device);return 0;}
        catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
    }
}
