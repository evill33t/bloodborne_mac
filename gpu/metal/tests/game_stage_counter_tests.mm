// SPDX-License-Identifier: GPL-2.0-or-later
// Pure contracts by default. --gpu uses only fixture-owned Metal resources and
// temp files; it does not instantiate the game, renderer or guest runtime.
#import <Metal/Metal.h>
#include "gpu/metal/stage_counter_timeline.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Stage=Gpu::Metal::StageCounterTimeline;
using Stage::U64;
namespace fs=std::filesystem;
#define CHECK(condition) do {if(!(condition))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #condition);} while(false)

static void Near(double actual,double expected,double tolerance=1e-14) {
    CHECK(std::isfinite(actual));CHECK(std::abs(actual-expected)<=tolerance);
}
static void Invalid(const std::array<U64,4>& raw,Stage::ClockPair begin,Stage::ClockPair end) {
    const auto value=Stage::Convert(raw,begin,end);
    CHECK(!value.valid);CHECK(std::isnan(value.vertex_ms));CHECK(std::isnan(value.fragment_ms));
}
static void PureContracts() {
    const Stage::ClockPair begin{1000,50},end{3000,1050};
    const std::array<U64,4> samples{100,200,300,700};
    const auto value=Stage::Convert(samples,begin,end);
    CHECK(value.valid);Near(value.vertex_ms,.0002);Near(value.fragment_ms,.0008);
    // Subtract uint64 values before conversion. A double-first implementation
    // loses these differences at this magnitude, even though they are valid.
    const U64 origin=~U64(0)-20000;
    const auto large=Stage::Convert({origin+100,origin+150,origin+300,origin+900},
        {origin,origin},{origin+2000,origin+1000});
    CHECK(large.valid);Near(large.vertex_ms,.0001);Near(large.fragment_ms,.0012);
    Invalid(samples,begin,{begin.cpu,end.gpu});
    Invalid(samples,begin,{begin.cpu-1,end.gpu});
    Invalid(samples,begin,{end.cpu,begin.gpu});
    Invalid(samples,begin,{end.cpu,begin.gpu-1});
    for(std::size_t k=0;k<samples.size();++k) {
        auto raw=samples;raw[k]=MTLCounterErrorValue;Invalid(raw,begin,end);
        raw=samples;raw[k]=begin.gpu-1;Invalid(raw,begin,end);
        raw=samples;raw[k]=end.gpu+1;Invalid(raw,begin,end);
    }
    Invalid({200,100,300,700},begin,end);
    Invalid({100,200,700,300},begin,end);
    const auto zero=Stage::Convert({begin.gpu,begin.gpu,end.gpu,end.gpu},begin,end);
    CHECK(zero.valid);Near(zero.vertex_ms,0);Near(zero.fragment_ms,0);

    Stage::Pass pass;
    const Stage::Encoded first{101,201,301,true,true,true};
    Stage::NoteDraw(pass,1,2,11,22,first);
    Stage::NoteDraw(pass,1,2,11,22,first);
    CHECK(pass.draws==2 && !pass.mixed_vs && !pass.mixed_ps);
    CHECK(!pass.mixed_vs_key && !pass.mixed_ps_key && !pass.mixed_pipeline_key);
    // Identical GCN source pair, different compiled variants, then return to A.
    Stage::NoteDraw(pass,1,2,11,22,{102,202,302,true,true,true});
    Stage::NoteDraw(pass,1,2,11,22,first);
    CHECK(!pass.mixed_vs && !pass.mixed_ps);
    CHECK(pass.mixed_vs_key && pass.mixed_ps_key && pass.mixed_pipeline_key);
    CHECK(pass.first_encoded.vs_key==101 && pass.last_encoded.vs_key==101);
    Stage::NoteDraw(pass,3,2,33,22,first);
    Stage::NoteDraw(pass,1,4,11,44,first);
    Stage::NoteDraw(pass,1,2,11,22,first);
    CHECK(pass.mixed_vs && pass.mixed_ps && pass.draws==7);
    CHECK(pass.first_vs==1 && pass.first_ps==2 && pass.last_vs==1 && pass.last_ps==2);
    // A replacement debug fragment has no translator/compiler cache key.
    Stage::NoteDraw(pass,1,2,11,22,{101,0,301,true,false,true});
    Stage::NoteDraw(pass,1,2,11,22,first);
    CHECK(pass.unknown_ps_key && !pass.unknown_vs_key && !pass.unknown_pipeline_key);
    CHECK(pass.mixed_ps_key && pass.last_encoded.ps_known);
    Stage::Pass unknown;
    Stage::NoteDraw(unknown,0,0,0,0);
    CHECK(unknown.draws==1 && unknown.unknown_vs_key && unknown.unknown_ps_key && unknown.unknown_pipeline_key);
    CHECK(!unknown.first_encoded.ps_known && !unknown.mixed_ps_key);
    puts("Stage counters: integer calibration, invalid samples, GCN/compiled variant and unknown-label contracts passed");
}

struct Environment {
    struct Previous {std::string key;std::optional<std::string> value;};
    std::vector<Previous> saved;
    Environment() {
        for(const char* key:{"BB_METAL_STAGE_COUNTER_CSV","BB_METAL_STAGE_COUNTER_ALL_PASSES","BB_SOURCE_GAME","BB_GAME_DIR"}) {
            const char* value=std::getenv(key);
            saved.push_back({key,value ? std::optional<std::string>(value) : std::nullopt});
            CHECK(!::unsetenv(key));
        }
    }
    ~Environment() {
        for(const auto& value:saved) {
            if(value.value)::setenv(value.key.c_str(),value.value->c_str(),1);
            else ::unsetenv(value.key.c_str());
        }
    }
    static void Set(const char* key,const fs::path& value) {CHECK(!::setenv(key,value.c_str(),1));}
};
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        auto pattern=(fs::temp_directory_path()/"bb-metal-stage-counter.XXXXXX").string();
        std::vector<char> bytes(pattern.begin(),pattern.end());bytes.push_back(0);
        const char* created=::mkdtemp(bytes.data());CHECK(created);path=created;
    }
    ~TemporaryDirectory() {std::error_code error;fs::remove_all(path,error);}
};
static std::string ReadText(const fs::path& path) {
    std::ifstream file(path,std::ios::binary);CHECK(file);
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
static void WriteText(const fs::path& path,const char* text) {
    std::ofstream file(path,std::ios::binary);CHECK(file);file<<text;file.close();CHECK(file);
}
static void Bounded(const char* label,const std::function<bool()>& ready) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!ready()) {
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error(std::string("timeout: ")+label);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
static bool Capable(id<MTLDevice> device) {
    if(@available(macOS 11.0,*)) {
        if(!device || ![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary])return false;
        for(id<MTLCounterSet> set in device.counterSets)
            if([set.name isEqualToString:MTLCommonCounterSetTimestamp])
                for(id<MTLCounter> counter in set.counters)
                    if([counter.name isEqualToString:MTLCommonCounterTimestamp])return true;
    }
    return false;
}
static void OutputGuards(id<MTLDevice> device,const fs::path& root) {
    const auto source=root/"source",other=root/"other";
    fs::create_directory(source);fs::create_directory(other);
    const auto original=source/"reference.bin";WriteText(original,"fixture-original-unchanged\n");
    const auto expected=ReadText(original);
    CHECK(!Stage::Log::Open(nil)); // Flag absent: no device query or file output.
    for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) {
        Environment::Set(key,source);Environment::Set("BB_METAL_STAGE_COUNTER_CSV",original);
        CHECK(!Stage::Log::Open(device));CHECK(ReadText(original)==expected);
        Environment::Set("BB_METAL_STAGE_COUNTER_CSV",source/"must-not-create.csv");
        CHECK(!Stage::Log::Open(device));CHECK(!fs::exists(source/"must-not-create.csv"));
        const auto alias=other/"source-alias";
        fs::create_symlink(original,alias);Environment::Set("BB_METAL_STAGE_COUNTER_CSV",alias);
        CHECK(!Stage::Log::Open(device));CHECK(ReadText(original)==expected);fs::remove(alias);
        CHECK(!::unsetenv(key));
    }
    const auto hardlink=other/"hardlink";fs::create_hard_link(original,hardlink);
    Environment::Set("BB_METAL_STAGE_COUNTER_CSV",hardlink);
    CHECK(!Stage::Log::Open(device));CHECK(ReadText(original)==expected);fs::remove(hardlink);
    Environment::Set("BB_METAL_STAGE_COUNTER_CSV",other);
    CHECK(!Stage::Log::Open(device));
    Environment::Set("BB_METAL_STAGE_COUNTER_CSV",other/"missing-parent"/"log.csv");
    CHECK(!Stage::Log::Open(device));
    // Without O_NONBLOCK the writer open can hang before reaching fstat. A
    // bounded reader watchdog unblocks that faulty implementation and fails it.
    const auto fifo=other/"invalid.fifo";CHECK(!::mkfifo(fifo.c_str(),0600));
    Environment::Set("BB_METAL_STAGE_COUNTER_CSV",fifo);
    std::atomic<bool> finished{false},watchdog_unblocked{false};
    std::thread watchdog([&] {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        while(!finished && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(!finished) {
            watchdog_unblocked=true;
            const int fd=::open(fifo.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);
            if(fd>=0) {
                const auto release_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
                while(!finished && std::chrono::steady_clock::now()<release_deadline)
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ::close(fd);
            }
        }
    });
    const auto rejected=Stage::Log::Open(device);finished=true;watchdog.join();
    CHECK(!rejected);CHECK(!watchdog_unblocked);CHECK(ReadText(original)==expected);
    CHECK(!::unsetenv("BB_METAL_STAGE_COUNTER_CSV"));
}

constexpr unsigned Width=960,Height=540,Targets=5;
using Textures=std::array<id<MTLTexture>,Targets>;
using Pixels=std::array<std::vector<unsigned char>,Targets>;
static Textures MakeTextures(id<MTLDevice> device,unsigned width=Width,unsigned height=Height) {
    Textures textures;
    for(auto& texture:textures) {
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
            width:width height:height mipmapped:NO];
        descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageRenderTarget;
        texture=[device newTextureWithDescriptor:descriptor];CHECK(texture);
    }
    return textures;
}
static id<MTLRenderPipelineState> MakePipeline(id<MTLDevice> device,unsigned targets=Targets,
    MTLPixelFormat format=MTLPixelFormatRGBA8Unorm) {
    NSString* source=@"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct V{float4 p [[position]];};\n"
        "vertex V stage_vs(uint i [[vertex_id]]){const float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return {float4(p[i],0,1)};}\n"
        "struct F{float4 a [[color(0)]];float4 b [[color(1)]];float4 c [[color(2)]];float4 d [[color(3)]];float4 e [[color(4)]];};\n"
        "fragment F stage_ps(){return {float4(1,0,0,1),float4(0,1,0,1),float4(0,0,1,1),float4(1,1,0,1),float4(0,1,1,1)};}\n"
        "fragment float4 stage_single(){return float4(1,0,0,1);}\n";
    NSError* error=nil;auto library=[device newLibraryWithSource:source options:nil error:&error];
    if(!library)throw std::runtime_error(error.localizedDescription.UTF8String ?: "Metal library creation failed");
    auto descriptor=[MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction=[library newFunctionWithName:@"stage_vs"];
    descriptor.fragmentFunction=[library newFunctionWithName:targets==1 ? @"stage_single" : @"stage_ps"];
    for(unsigned k=0;k<targets;++k)descriptor.colorAttachments[k].pixelFormat=format;
    auto pipeline=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if(!pipeline)throw std::runtime_error(error.localizedDescription.UTF8String ?: "Metal pipeline creation failed");
    return pipeline;
}
static std::optional<Stage::Pass> Encode(id<MTLCommandBuffer> command,id<MTLRenderPipelineState> pipeline,
    const Textures& textures,const std::shared_ptr<Stage::Log>& log,U64 frame) {
    auto descriptor=[MTLRenderPassDescriptor renderPassDescriptor];
    Stage::Pass metadata;metadata.frame=frame;metadata.width=textures[0].width;metadata.height=textures[0].height;
    metadata.colors=Targets;metadata.color0=0x1000;
    for(unsigned k=0;k<Targets;++k) {
        descriptor.colorAttachments[k].texture=textures[k];
        descriptor.colorAttachments[k].loadAction=MTLLoadActionClear;
        descriptor.colorAttachments[k].clearColor=MTLClearColorMake(0,0,0,0);
        descriptor.colorAttachments[k].storeAction=MTLStoreActionStore;
        metadata.formats[k]=textures[k].pixelFormat;
    }
    auto sampled=log ? log->Attach(descriptor,metadata) : std::nullopt;
    auto encoder=[command renderCommandEncoderWithDescriptor:descriptor];
    if(!encoder && sampled){log->Cancel(descriptor,*sampled);sampled.reset();}
    CHECK(encoder);
    [encoder setRenderPipelineState:pipeline];
    if(sampled)Stage::NoteDraw(*sampled,0x10,0x20,0x100,0x200,{0x1001,0x2001,0x3001,true,true,true});
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];return sampled;
}
struct Completion {
    std::atomic<bool> done{false};std::atomic<long> status{0},error{0};
};
static void ArmCompletion(id<MTLCommandBuffer> command,const std::shared_ptr<Stage::Log>& log,
    Stage::ClockPair begin,std::vector<Stage::Pass> passes,U64 submission,const std::shared_ptr<Completion>& state) {
    const auto retained=log;
    const auto batch=std::make_shared<const std::vector<Stage::Pass>>(std::move(passes));
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if(retained)retained->Complete(submission,long(completed.status),long(completed.error.code),begin,retained->Clock(),batch);
        state->status=long(completed.status);state->error=long(completed.error.code);state->done=true;
    }];
}
static void CompleteCommand(id<MTLCommandBuffer> command) {
    const auto state=std::make_shared<Completion>();ArmCompletion(command,{}, {},{},0,state);
    [command commit];Bounded("unsampled command completion",[&]{return state->done.load();});
    CHECK(state->status==MTLCommandBufferStatusCompleted && !state->error);
}
static Pixels ReadPixels(const Textures& textures) {
    Pixels values;
    for(unsigned k=0;k<Targets;++k) {
        values[k].resize(Width*Height*4);
        [textures[k] getBytes:values[k].data() bytesPerRow:Width*4 fromRegion:MTLRegionMake2D(0,0,Width,Height) mipmapLevel:0];
    }
    return values;
}
static void ExpectedPixels(const Pixels& pixels) {
    constexpr std::array<std::array<unsigned char,4>,Targets> expected{{{255,0,0,255},{0,255,0,255},
        {0,0,255,255},{255,255,0,255},{0,255,255,255}}};
    for(unsigned k=0;k<Targets;++k)for(std::size_t i=0;i<pixels[k].size();++i)CHECK(pixels[k][i]==expected[k][i%4]);
}
using CsvRow=std::map<std::string,std::string>;
static std::vector<std::string> Split(const std::string& text) {
    std::vector<std::string> values;std::size_t start=0;
    do {const auto end=text.find(',',start);values.push_back(text.substr(start,end-start));
        if(end==std::string::npos)break;start=end+1;}while(true);
    return values;
}
static std::vector<CsvRow> ReadRows(const fs::path& path) {
    std::ifstream file(path);std::string line;std::vector<std::string> header;std::vector<CsvRow> rows;
    while(std::getline(file,line)) {
        if(file.eof())break; // Ignore an in-progress row that has no final newline.
        const auto columns=Split(line);if(columns.empty())continue;
        if(columns[0]=="submission"){header=columns;continue;}
        CHECK(!header.empty() && columns.size()==header.size());CsvRow row;
        for(std::size_t k=0;k<header.size();++k)row[header[k]]=columns[k];rows.push_back(std::move(row));
    }
    return rows;
}
static U64 Number(const CsvRow& row,const char* name) {return std::stoull(row.at(name));}
static void ValidRows(const std::vector<CsvRow>& rows) {
    for(const auto& row:rows) {
        CHECK(Number(row,"data_valid")==1 && Number(row,"calibration_valid")==1);
        CHECK(Number(row,"status")==MTLCommandBufferStatusCompleted && !Number(row,"error_code"));
        CHECK(Number(row,"width")==Width && Number(row,"height")==Height && Number(row,"colors")==Targets);
        CHECK(Number(row,"draws")==1 && Number(row,"first_gcn_vs")==0x10 && Number(row,"last_gcn_ps")==0x20);
        CHECK(Number(row,"first_encoded_vs_key")==0x1001 && Number(row,"last_encoded_ps_key")==0x2001);
        CHECK(Number(row,"first_pipeline_cache_key")==0x3001 && Number(row,"last_pipeline_cache_key")==0x3001);
        CHECK(!Number(row,"mixed_encoded_vs") && !Number(row,"mixed_encoded_ps") && !Number(row,"unknown_encoded_ps"));
        CHECK(!Number(row,"selection_all_passes"));
        for(unsigned k=0;k<Targets;++k)CHECK(Number(row,("format"+std::to_string(k)).c_str())==MTLPixelFormatRGBA8Unorm);
        const Stage::ClockPair begin{Number(row,"cpu0_ns"),Number(row,"gpu0_raw")};
        const Stage::ClockPair end{Number(row,"cpu1_ns"),Number(row,"gpu1_raw")};
        const auto duration=Stage::Convert({Number(row,"vertex_start_raw"),Number(row,"vertex_end_raw"),
            Number(row,"fragment_start_raw"),Number(row,"fragment_end_raw")},begin,end);
        CHECK(duration.valid && duration.fragment_ms>0);
        Near(std::stod(row.at("vertex_ms")),duration.vertex_ms,5.1e-10);
        Near(std::stod(row.at("fragment_ms")),duration.fragment_ms,5.1e-10);
        CHECK(Number(row,"exhausted")>=1 && Number(row,"skipped")>=1);
        CHECK(!Number(row,"attach_errors") && !Number(row,"encode_errors") && !Number(row,"invalid_results"));
    }
}
struct FixtureRendererOwner {
    id<MTLCommandQueue> queue=nil;id<MTLRenderPipelineState> pipeline=nil;
    Textures targets;std::shared_ptr<Stage::Log> log;
};
struct ReleaseEventOnExit {
    id<MTLSharedEvent> event=nil;
    ~ReleaseEventOnExit(){event.signaledValue=1;}
};
static void AllPassSelection(id<MTLDevice> device,const fs::path& root) {
    auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float
        width:32 height:32 mipmapped:NO];
    descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageRenderTarget;
    auto texture=[device newTextureWithDescriptor:descriptor];CHECK(texture);
    const auto make_pass=[&] {
        auto pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=texture;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;return pass;
    };
    Stage::Pass metadata;metadata.width=metadata.height=32;metadata.colors=1;
    metadata.formats[0]=MTLPixelFormatRGBA16Float;metadata.frame=200;
    unsigned index=0;
    for(const char* value:{"0","01","1 ","true",""}) {
        const auto path=root/("strict-selection-"+std::to_string(index++)+".csv");
        Environment::Set("BB_METAL_STAGE_COUNTER_CSV",path);
        CHECK(!::setenv("BB_METAL_STAGE_COUNTER_ALL_PASSES",value,1));
        const auto log=Stage::Log::Open(device);CHECK(log);
        CHECK(!log->Attach(make_pass(),metadata));CHECK(ReadRows(path).empty());
    }
    const auto csv=root/"all-passes.csv";
    Environment::Set("BB_METAL_STAGE_COUNTER_CSV",csv);
    CHECK(!::setenv("BB_METAL_STAGE_COUNTER_ALL_PASSES","1",1));
    auto log=Stage::Log::Open(device);CHECK(log);
    auto queue=[device newCommandQueue];CHECK(queue);
    auto command=[queue commandBuffer];CHECK(command);
    const auto begin=log->Clock();auto pass_descriptor=make_pass();
    auto pass=log->Attach(pass_descriptor,metadata);CHECK(pass);
    auto encoder=[command renderCommandEncoderWithDescriptor:pass_descriptor];CHECK(encoder);
    auto pipeline=MakePipeline(device,1,MTLPixelFormatRGBA16Float);
    [encoder setRenderPipelineState:pipeline];
    Stage::NoteDraw(*pass,0x10,0x21,0x100,0x201,{0x1001,0x2101,0x3101,true,true,true});
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];
    const auto complete=std::make_shared<Completion>();ArmCompletion(command,log,begin,{*pass},3,complete);
    [command commit];Bounded("all-pass small HDR command",[&]{return complete->done.load();});
    CHECK(complete->status==MTLCommandBufferStatusCompleted && !complete->error);
    Bounded("all-pass asynchronous row",[&]{return ReadRows(csv).size()==1;});
    const auto rows=ReadRows(csv);const auto& row=rows[0];
    CHECK(Number(row,"selection_all_passes")==1 && Number(row,"colors")==1);
    CHECK(Number(row,"width")==32 && Number(row,"height")==32);
    CHECK(Number(row,"format0")==MTLPixelFormatRGBA16Float);
    CHECK(Number(row,"data_valid")==1 && Number(row,"calibration_valid")==1);
    CHECK(Number(row,"selected")==1 && !Number(row,"skipped") && !Number(row,"exhausted"));
    std::array<std::uint16_t,32*32*4> pixels{};
    [texture getBytes:pixels.data() bytesPerRow:32*8 fromRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0];
    constexpr std::array<std::uint16_t,4> expected{0x3c00,0,0,0x3c00};
    for(std::size_t k=0;k<pixels.size();++k)CHECK(pixels[k]==expected[k%4]);
    CHECK(!::unsetenv("BB_METAL_STAGE_COUNTER_ALL_PASSES"));
}
static int GpuContracts() {
    auto device=MTLCreateSystemDefaultDevice();if(!Capable(device))return 77;
    Environment environment;TemporaryDirectory temporary;
    OutputGuards(device,temporary.path);
    auto owner=std::make_unique<FixtureRendererOwner>();
    owner->queue=[device newCommandQueue];CHECK(owner->queue);
    owner->pipeline=MakePipeline(device);owner->targets=MakeTextures(device);
    // A complete, unsampled natural pass establishes the pixel baseline.
    Pixels expected;
    @autoreleasepool {
        auto baseline=[owner->queue commandBuffer];CHECK(baseline);
        CHECK(!Encode(baseline,owner->pipeline,owner->targets,{},0));CompleteCommand(baseline);
        expected=ReadPixels(owner->targets);ExpectedPixels(expected);
    }
    const auto csv=temporary.path/"stage.csv";Environment::Set("BB_METAL_STAGE_COUNTER_CSV",csv);
    owner->log=Stage::Log::Open(device);CHECK(owner->log);
    @autoreleasepool {
        auto command=[owner->queue commandBuffer];CHECK(command);
        const auto begin=owner->log->Clock();std::vector<Stage::Pass> passes;
        // A genuine small five-MRT pass must skip; the next16 natural passes reserve
        // every pool slot before submission, so asynchronous timing cannot mask exhaustion.
        const auto small=MakeTextures(device,32,32);
        CHECK(!Encode(command,owner->pipeline,small,owner->log,1));
        for(std::size_t k=0;k<Stage::PoolSize;++k) {
            const auto pass=Encode(command,owner->pipeline,owner->targets,owner->log,k+2);
            CHECK(pass && pass->slot==k);passes.push_back(*pass);
        }
        CHECK(!Encode(command,owner->pipeline,owner->targets,owner->log,99));
        const auto complete=std::make_shared<Completion>();ArmCompletion(command,owner->log,begin,std::move(passes),1,complete);
        [command commit];Bounded("sampled command completion",[&]{return complete->done.load();});
        CHECK(complete->status==MTLCommandBufferStatusCompleted && !complete->error);
        Bounded("16 asynchronously resolved rows",[&]{return ReadRows(csv).size()==Stage::PoolSize;});
        ValidRows(ReadRows(csv));CHECK(ReadPixels(owner->targets)==expected);
    }

    // A recovered slot is used by a real pass, then the fixture renderer/log
    // owner is destroyed before completion. Only the completion capture keeps
    // the log/sample pool alive. The fixture gate is never production behavior.
    std::weak_ptr<Stage::Log> weak=owner->log;
    const auto recovered_complete=std::make_shared<Completion>();
    @autoreleasepool {
        auto recovered=[owner->queue commandBuffer];CHECK(recovered);
        ReleaseEventOnExit gate{[device newSharedEvent]};CHECK(gate.event);
        [recovered encodeWaitForEvent:gate.event value:1];
        const auto recovery_begin=owner->log->Clock();
        const auto pass=Encode(recovered,owner->pipeline,owner->targets,owner->log,100);CHECK(pass);
        ArmCompletion(recovered,owner->log,recovery_begin,{*pass},2,recovered_complete);
        owner.reset();CHECK(!weak.expired());[recovered commit];
        CHECK(!recovered_complete->done);gate.event.signaledValue=1;
        Bounded("completion after renderer/log owner release",[&]{return recovered_complete->done.load();});
        CHECK(recovered_complete->status==MTLCommandBufferStatusCompleted && !recovered_complete->error);
    }
    // Drain fixture autoreleases as well as strong command locals before
    // checking destruction; Metal need not destroy a handler while its command
    // remains autoreleased in an enclosing pool.
    Bounded("recovered asynchronous row and final log release",[&]{return ReadRows(csv).size()==Stage::PoolSize+1 && weak.expired();});
    const auto rows=ReadRows(csv);ValidRows(rows);CHECK(Number(rows.back(),"submission")==2 && Number(rows.back(),"frame")==100);
    AllPassSelection(device,temporary.path);
    puts("Stage counters GPU: natural passes,16-slot exhaustion/recovery, every MRT pixel, calibrated rows and retained completion lifetime passed");
    return 0;
}
int main(int argc,char** argv) {
    @autoreleasepool {
        try {
            PureContracts();
            if(argc==2 && !std::strcmp(argv[1],"--gpu"))return GpuContracts();
            CHECK(argc==1);return 0;
        } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
    }
}
