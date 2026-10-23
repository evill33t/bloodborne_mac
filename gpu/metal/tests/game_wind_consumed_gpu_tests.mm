// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the production atomic helper and the production final-consumer
// binding/completion implementation. No game or original game memory is run.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "game_wind_consumed_fixture.h"
#include "gpu/metal/wind_consumed_msl.h"
#include "gpu/metal/stutter_trace.h"
#include "gpu/renderer.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unistd.h>
extern "C" void runtime_restart() {std::abort();}
namespace {
using namespace WindConsumedFixture;
namespace WindConsumed=Gpu::Metal::WindConsumed;
namespace StutterTrace=Gpu::Metal::StutterTrace;
using u64=std::uint64_t;using u32=std::uint32_t;
using Program=Shader::Program;using Stage=Shader::Stage;using DrawCall=Gpu::DrawCall;
#define DC_INFO(...) ((void)0)
struct Harness {
    id<MTLDevice> device=nil;
    id<MTLCommandBuffer> cmd=nil;
    id<MTLRenderCommandEncoder> enc=nil;
    u64 frame_count=100,draw_count=1,submission_id=7;
    bool trace=false;
    std::filesystem::path allowed;
    bool CapturePathAllowed(const std::filesystem::path& p) const {return p==allowed;}
#include "gpu/metal/wind_consumed_capture.inc"
};
#undef DC_INFO
struct Params {u32 bias=0,flags=0;};
std::string AtomicSource() {
    return std::string("#include <metal_stdlib>\nusing namespace metal;\n")+WindConsumed::MslPrelude+R"MSL(
struct Params {uint bias,flags;};
vertex void record_vs(uint vid [[vertex_id]], uint iid [[instance_id]],
                       constant Params& params [[buffer(0)]], device atomic_uint* record [[buffer(14)]]) {
    if ((vid & 63u) == 0u) {
        uint add = (params.flags & 1u) ? vid : 0u;
        uint wind[4],view[16],camera[3];
        for(uint k=0;k<4u;++k)wind[k]=params.bias+k+add;
        for(uint k=0;k<16u;++k)view[k]=params.bias+0x100u+k+add;
        uint camera_add = (params.flags & 17u) ? vid : 0u;
        for(uint k=0;k<3u;++k)camera[k]=params.bias+0x200u+k+camera_add;
        ulong leaf_add = (params.flags & 4u) ? ulong(vid) : 0ul;
        dc_record_wind_site(record,0u,wind,0x8071234000ul+leaf_add,vid,iid,(params.flags&2u)==0u);
        dc_record_wind_site(record,1u,view,0x8072345000ul+leaf_add,vid,iid,(params.flags&2u)==0u);
        dc_record_wind_site(record,2u,camera,0x8072345000ul+leaf_add,vid,iid,(params.flags&10u)==0u);
        uint row0 = ((params.flags & 32u) && (vid & 64u)) ? 3u : 0u;
        if(params.flags & 512u)row0=0xffffffffu;
        uint matrix[12];
        uint matrix_delta = ((params.flags & 64u) && (vid & 128u)) ? 7u : 0u;
        for(uint k=0;k<12u;++k)matrix[k]=params.bias+0x300u+row0*0x1000u+k+matrix_delta;
        uint4 descriptor = uint4(0x71234000u,0x100080u,(params.flags & 256u) ? 0u : 6u,0x77facu);
        dc_record_wind_matrix(record,matrix,descriptor,uint3(row0,row0+1u,row0+2u),vid,iid);
    }
}
)MSL";
}
id<MTLRenderPipelineState> Pipeline(id<MTLDevice> device) {
    NSError* error=nil;auto library=[device newLibraryWithSource:@(AtomicSource().c_str()) options:nil error:&error];
    Require(library!=nil,std::string("Atomic helper compile: ")+(error ? error.localizedDescription.UTF8String : "unknown"));
    auto descriptor=[MTLRenderPipelineDescriptor new];descriptor.vertexFunction=[library newFunctionWithName:@"record_vs"];
    descriptor.rasterizationEnabled=NO;
    auto result=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    Require(result!=nil,std::string("Atomic helper pipeline: ")+(error ? error.localizedDescription.UTF8String : "unknown"));
    return result;
}
id<MTLRenderCommandEncoder> Encoder(id<MTLCommandBuffer> command,id<MTLRenderPipelineState> pipeline) {
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
    pass.renderTargetWidth=pass.renderTargetHeight=16;pass.defaultRasterSampleCount=1;
    auto encoder=[command renderCommandEncoderWithDescriptor:pass];Require(encoder!=nil,"Cannot create record encoder");
    [encoder setRenderPipelineState:pipeline];return encoder;
}
WindConsumed::Record ReadRecord(const std::filesystem::path& path) {
    WindConsumed::Record record{};std::ifstream stream(path,std::ios::binary);
    Require(bool(stream.read(reinterpret_cast<char*>(record.data()),sizeof(record))),"Cannot read completed private record");
    Require(stream.peek()==std::ifstream::traits_type::eof(),"Unexpected private record extent");return record;
}
NSDictionary* ReadJson(const std::filesystem::path& path) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!std::filesystem::exists(path) && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto data=[NSData dataWithContentsOfFile:@(path.c_str())];
    id result=data ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
    Require([result isKindOfClass:[NSDictionary class]],"Missing completion-owned metadata");return result;
}
void Verify(const WindConsumed::Record& r,const Params p) {
    using namespace WindConsumed;
    Require(Valid(r) && r[WindSamples]==4 && r[ViewSamples]==4 && r[CameraSamples]==4,"Sparse atomic sample count/schema mismatch");
    const auto delta=(p.flags&1) ? 192u : 0u;
    for(unsigned k=0;k<4;++k)Require(r[WindMin+k]==p.bias+k && r[WindMax+k]==p.bias+k+delta,"Wind register bits changed");
    for(unsigned k=0;k<16;++k)Require(r[ViewMin+k]==p.bias+0x100+k && r[ViewMax+k]==p.bias+0x100+k+delta,"View register bits changed");
    const auto camera_delta=(p.flags&17) ? 192u : 0u;
    for(unsigned k=0;k<3;++k)Require(r[CameraMin+k]==p.bias+0x200+k && r[CameraMax+k]==p.bias+0x200+k+camera_delta,"Camera register bits changed");
    Require(r[WindNull]==((p.flags&2) ? 4u : 0u) && r[ViewNull]==r[WindNull],"Null read classification mismatch");
    Require(r[CameraNull]==((p.flags&10) ? 4u : 0u),"Independent camera null classification mismatch");
    Require(SelectedLeafUniform(r,Site::Wind)==!(p.flags&4) && SelectedLeafUniform(r,Site::View)==!(p.flags&4),"Leaf selection classification mismatch");
    Require(SelectedLeafUniform(r,Site::Camera)==!(p.flags&4),"Camera leaf selection classification mismatch");
    Require(r[VertexMin]==0 && r[VertexMax]==192 && r[InstanceMin]==0 && r[InstanceMax]==0,"Selected invocation bounds mismatch");
}
void Lifecycle(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> pipeline) {
    char temporary[]="/tmp/bb-wind-consumed-XXXXXX";const auto directory=::mkdtemp(temporary);
    Require(directory!=nullptr,"Cannot create isolated fixture directory");const auto root=std::filesystem::weakly_canonical(directory);
    auto owner=std::make_unique<Harness>();owner->device=device;owner->allowed=root;
    unsetenv("BB_METAL_WIND_CONSUMED_FRAMES");unsetenv("BB_METAL_WIND_CONSUMED_DIR");owner->InitWindConsumedCapture();
    Require(owner->wind_consumed_records.empty() && owner->wind_consumed_root.empty(),"OFF allocated diagnostic GPU buffers");
    setenv("BB_METAL_WIND_CONSUMED_FRAMES","1",1);setenv("BB_METAL_WIND_CONSUMED_DIR",directory,1);
    owner->InitWindConsumedCapture();Require(owner->wind_consumed_records.size()==2,"Observation allocation is not bounded");
    auto program=SyntheticShape();Require(!owner->WindConsumedRequested(program,true),"Window armed before trace");
    owner->trace=true;Require(!owner->WindConsumedRequested(program,false),"Nonordinary context admitted");
    auto wrong=program;wrong.hash^=1;Require(!owner->WindConsumedRequested(wrong,true),"Wrong original program admitted");
    Require(owner->WindConsumedRequested(program,true),"Eligible context not admitted");
    // A setup command completes before the actual consumer exists. The record
    // binding and handler must attach to the later command, after that cut.
    owner->cmd=[queue commandBuffer];auto prior=owner->cmd;[prior commit];[prior waitUntilCompleted];
    Require(prior.status==MTLCommandBufferStatusCompleted,"Setup cut failed");
    auto consumer=[queue commandBuffer];auto gate=[device newSharedEvent];
    Require(gate!=nil,"Cannot create bounded lifecycle gate");[consumer encodeWaitForEvent:gate value:1];
    owner->cmd=consumer;++owner->submission_id;owner->enc=Encoder(consumer,pipeline);
    const auto consumer_identity=reinterpret_cast<std::uintptr_t>((__bridge const void*)consumer);
    const auto encoder_identity=reinterpret_cast<std::uintptr_t>((__bridge const void*)owner->enc);
    std::array<u32,16> ud{};DrawCall draw;draw.count=256;
    const Params first{0x7fc12300u,1u},second{0x80000000u,6u};
    for(const auto p:{first,second}) {
        Require(owner->BindWindConsumedDraw(program,ud.data(),123,456,20,draw),"Final consumer binding failed");
        [owner->enc setVertexBytes:&p length:sizeof(p) atIndex:0];
        [owner->enc drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:256];++owner->draw_count;
    }
    Require(!owner->BindWindConsumedDraw(program,ud.data(),123,456,20,draw),"Record capacity reused after two draws");
    [owner->enc endEncoding];owner->enc=nil;
    Require(std::filesystem::is_empty(root),"Setup command produced consumer records early");
    // The handlers retain their own buffers/metadata even after the owner and
    // its vector disappear. No renderer lifetime is needed at GPU completion.
    owner.reset();[consumer commit];
    Require(std::filesystem::is_empty(root),"Blocked consumer completed before its actual draw");
    gate.signaledValue=1;[consumer waitUntilCompleted];
    Require(consumer.status==MTLCommandBufferStatusCompleted && !consumer.error,"Consumer GPU execution failed");
    unsigned index=0;
    for(const auto p:{first,second}) {
        const auto stem=root/("wind_consumed_f100_draw"+std::to_string(++index));
        auto meta=ReadJson(stem.string()+".json");Require([meta[@"complete"] boolValue],"Incomplete consumer record");
        Require([meta[@"consumer_command"] unsignedLongLongValue]==consumer_identity &&
            [meta[@"consumer_encoder"] unsignedLongLongValue]==encoder_identity &&
            [meta[@"consumer_submission"] unsignedLongLongValue]==8,"Completion bound to wrong command/encoder");
        Require([meta[@"record_slot"] unsignedIntValue]==index-1,"Per-draw record ownership changed");
        Verify(ReadRecord(stem.string()+".bin"),p);
    }
    Require(std::distance(std::filesystem::directory_iterator(root),std::filesystem::directory_iterator())==4,"Unexpected diagnostic output count");
    unsetenv("BB_METAL_WIND_CONSUMED_FRAMES");unsetenv("BB_METAL_WIND_CONSUMED_DIR");std::filesystem::remove_all(root);
    std::puts("Consumed-wind GPU: wind/VP/camera atomic raw-bit ranges, null/leaf distinction, final-command cut, distinct buffers and owner destruction passed.");
}
void CameraCases(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> pipeline) {
    // Camera variation/null classification must remain independent of the
    // unchanged wind and VP fields. Include negative and NaN raw bit patterns.
    for(const Params p: {Params{0xffc12300u,16u},Params{0x80000000u,8u}}) {
        auto record=[device newBufferWithLength:sizeof(WindConsumed::Record) options:MTLResourceStorageModeShared];
        Require(record && record.contents,"Cannot allocate independent camera fixture record");
        const auto initial=WindConsumed::InitialRecord();std::memcpy(record.contents,initial.data(),sizeof(initial));
        auto command=[queue commandBuffer];auto encoder=Encoder(command,pipeline);
        [encoder setVertexBytes:&p length:sizeof(p) atIndex:0];[encoder setVertexBuffer:record offset:0 atIndex:WindConsumed::Binding];
        [encoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:256];[encoder endEncoding];
        [command commit];[command waitUntilCompleted];Require(!command.error,"Independent camera GPU fixture failed");
        WindConsumed::Record result;std::memcpy(result.data(),record.contents,sizeof(result));Verify(result,p);
        using namespace WindConsumed;
        Require(!Varied(result,Site::Wind,0) && !Varied(result,Site::View,0),"Camera classification leaked into unrelated fields");
        Require(Varied(result,Site::Camera,2)==bool(p.flags&16),"Camera-only variation was not recorded");
    }
    std::puts("Consumed-wind GPU: independent cameraXYZ variation/null classifications passed.");
}
void MatrixCases(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> pipeline) {
    using namespace WindConsumed;
    for(const Params p:{Params{0x7fc12300u,32u},Params{0x80000000u,96u},Params{0,256u},Params{0,512u}}) {
      // Unseeded cases exercise the one-attempt claim/accounting. Preseed each
      // actual original index separately so within-index variation coverage
      // never depends on a weak CAS avoiding its permitted spurious failure.
      for(const auto preset:{~0u,0u,3u}) {
        if((p.flags&(256|512)) && preset!=~0u)continue;
        auto record=[device newBufferWithLength:sizeof(Record) options:MTLResourceStorageModeShared];
        Require(record && record.contents,"Cannot allocate matrix subgroup fixture");
        auto initial=InitialRecord();initial[MatrixSelectedIndex]=preset;std::memcpy(record.contents,initial.data(),sizeof(initial));
        auto command=[queue commandBuffer];auto encoder=Encoder(command,pipeline);
        [encoder setVertexBytes:&p length:sizeof(p) atIndex:0];[encoder setVertexBuffer:record offset:0 atIndex:Binding];
        [encoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:256];[encoder endEncoding];
        [command commit];[command waitUntilCompleted];Require(!command.error,"Matrix subgroup GPU fixture failed");
        Record r;std::memcpy(r.data(),record.contents,sizeof(r));
        Require(Valid(r) && r[MatrixCandidates]==4,"Matrix candidate accounting mismatch");
        Require(r[MatrixSamples]+r[MatrixOtherIndices]+r[MatrixClaimMisses]+r[MatrixRejectedBounds]==4,"Matrix subgroup lost an invocation");
        if(p.flags&(256|512)) {
            Require(!r[MatrixSamples] && r[MatrixSelectedIndex]==~0u && r[MatrixRejectedBounds]==4,"Invalid/sentinel row indices selected a matrix");
            continue;
        }
        if(r[MatrixSelectedIndex]==~0u) {
            Require(preset==~0u && r[MatrixClaimMisses]==4 && !r[MatrixSamples] && !r[MatrixOtherIndices],"Unclaimed matrix record lost spurious misses");
            continue;
        }
        Require(r[MatrixSamples]>=1 && r[MatrixSamples]<=2 && r[MatrixOtherIndices]<=2 &&
            !r[MatrixRejectedBounds] && MatrixIdentityUniform(r),"Matrix index selection mixed legitimate indices");
        const auto index=r[MatrixSelectedIndex];Require(index==0 || index==3,"Selected matrix index is not an observed original index");
        if(preset!=~0u)Require(index==preset && r[MatrixSamples]==2 && r[MatrixOtherIndices]==2,
            "Preselected matrix subgroup lost deterministic variation coverage");
        for(unsigned k=0;k<12;++k) {
            const auto expected=p.bias+0x300u+index*0x1000u+k;
            const auto delta=(p.flags&64) ? 7u : 0u;
            Require((r[MatrixMin+k]==expected || r[MatrixMin+k]==expected+delta) &&
                (r[MatrixMax+k]==expected || r[MatrixMax+k]==expected+delta) && r[MatrixMin+k]<=r[MatrixMax+k],
                "Selected matrix data contains another original index");
            if(r[MatrixSamples]==2)Require(r[MatrixMin+k]==expected && r[MatrixMax+k]==expected+delta &&
                MatrixVaried(r,k)==bool(delta),"Selected matrix subgroup lost within-index variation");
            else Require(!MatrixVaried(r,k) && r[MatrixMin+k]==r[MatrixMax+k],"A single selected sample claimed variation");
        }
      }
    }
    std::puts("Consumed-wind GPU: matrix index subgroup excludes legitimate per-index variation, detects within-index changes and rejects invalid indices.");
}
void Overflow(id<MTLDevice> device,id<MTLCommandQueue> queue,id<MTLRenderPipelineState> pipeline) {
    for(const bool matrix:{false,true}) {
    auto record=[device newBufferWithLength:sizeof(WindConsumed::Record) options:MTLResourceStorageModeShared];
    Require(record && record.contents,"Cannot allocate overflow fixture record");
    auto initial=WindConsumed::InitialRecord();
    if(matrix)initial[WindConsumed::MatrixCandidates]=initial[WindConsumed::MatrixSamples]=~0u;
    else initial[WindConsumed::WindSamples]=initial[WindConsumed::ViewSamples]=initial[WindConsumed::CameraSamples]=~0u;
    std::memcpy(record.contents,initial.data(),sizeof(initial));
    auto command=[queue commandBuffer];auto encoder=Encoder(command,pipeline);const Params p{};
    [encoder setVertexBytes:&p length:sizeof(p) atIndex:0];[encoder setVertexBuffer:record offset:0 atIndex:WindConsumed::Binding];
    [encoder drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:256];[encoder endEncoding];
    [command commit];[command waitUntilCompleted];Require(!command.error,"Overflow GPU fixture failed");
    WindConsumed::Record result;std::memcpy(result.data(),record.contents,sizeof(result));
    Require(result[WindConsumed::CounterOverflow] && !WindConsumed::Valid(result),"Atomic sample overflow was accepted");
    }
}
}
int main(int argc,char** argv) {
    if(argc!=1 && !(argc==3 && std::strcmp(argv[1],"--capture")==0))return 2;
    @autoreleasepool {try {
        auto device=MTLCreateSystemDefaultDevice();if(!device)return 77;
        auto queue=[device newCommandQueue];Require(queue!=nil,"No Metal command queue");
        auto pipeline=Pipeline(device);Lifecycle(device,queue,pipeline);CameraCases(device,queue,pipeline);MatrixCases(device,queue,pipeline);Overflow(device,queue,pipeline);
        if(argc==3) {
            auto original=Original(argv[2]);Require(Shader::VertexWindConsumedEligible(*original),"Original Dream program rejected");
            Shader::MslOptions options;options.wind_consumed=true;std::vector<std::string> unsupported;
            const auto source=Shader::GenerateMsl(*original,options,&unsupported);Require(unsupported.empty(),"Original diagnostic unsupported");
            NSError* error=nil;auto library=[device newLibraryWithSource:@(source.c_str()) options:nil error:&error];
            Require(library!=nil,std::string("Original instrumented VS compile: ")+(error ? error.localizedDescription.UTF8String : "unknown"));
            std::puts("Exact original instrumented Dream VS compiled successfully.");
        }
    }catch(const std::exception& e){std::fprintf(stderr,"Consumed-wind GPU fixture: %s\n",e.what());return 1;}}
}
