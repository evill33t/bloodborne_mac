// SPDX-License-Identifier: GPL-2.0-or-later
#import <Metal/Metal.h>
#include <CommonCrypto/CommonDigest.h>
#include "gpu/metal/mtl_compiler.h"
#include <cassert>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <future>
#include <string>
#include <vector>
#include <unordered_set>
#include <iterator>
#include <cstring>

namespace {
constexpr const char* kAreaCompute="#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }";
id<MTLComputePipelineState> AreaPipeline(Gpu::Metal::ShaderCompiler& compiler,unsigned group) {
    auto descriptor=[MTLComputePipelineDescriptor new];
    descriptor.computeFunction=compiler.Function(100,kAreaCompute,0);
    descriptor.maxTotalThreadsPerThreadgroup=group;
    NSError* error=nil;
    auto result=compiler.ComputePipeline(descriptor,&error);
    assert(result && !error);return result;
}
std::vector<std::filesystem::path> Recipes(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    for(const auto& file:std::filesystem::recursive_directory_iterator(directory))
        if(file.is_regular_file() && !file.is_symlink() && file.path().extension()==".json" &&
            file.path().filename().string().size()==69) result.push_back(file.path());
    return result;
}
std::uint64_t RecipeBytes(const std::filesystem::path& directory) {
    std::uint64_t total=0;
    for(const auto& file:Recipes(directory))total+=std::filesystem::file_size(file);
    return total;
}
std::filesystem::path AreaRecipeFile(const std::filesystem::path& directory,unsigned group) {
    for(const auto& path:Recipes(directory)) {
        NSData* bytes=[NSData dataWithContentsOfFile:@(path.c_str())];
        id recipe=bytes ? [NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil] : nil;
        if([recipe isKindOfClass:NSDictionary.class] && [recipe[@"kind"] isEqual:@"compute"] &&
            [recipe[@"group"] unsignedIntValue]==group)return path;
    }
    return {};
}
std::string FileContents(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::string Digest(const void* bytes,std::size_t count) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes,static_cast<CC_LONG>(count),digest);
    std::string result;for(unsigned char byte:digest){result+="0123456789abcdef"[byte>>4];result+="0123456789abcdef"[byte&15];}return result;
}
std::string Identity(std::string_view name,std::string_view architecture,std::uint64_t major,std::uint64_t minor,std::uint64_t patch) {
    const auto material=Gpu::Metal::PipelineRecipeIdentity::Material(name,architecture,major,minor,patch);
    return Digest(material.data(),material.size());
}
std::filesystem::path RecipeRoot(const std::filesystem::path& directory,id<MTLDevice> device) {
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    return directory/"metal-cache"/"prewarm-v2"/Identity(device.name.UTF8String,
        Gpu::Metal::PipelineRecipeIdentity::Architecture(),os.majorVersion,os.minorVersion,os.patchVersion);
}
std::filesystem::path LegacyRoot(const std::filesystem::path& directory,std::uint64_t registry) {
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    char identity[128];std::snprintf(identity,sizeof(identity),"%llx-%ld.%ld.%ld",static_cast<unsigned long long>(registry),
        os.majorVersion,os.minorVersion,os.patchVersion);
    return directory/"metal-cache"/"prewarm-v1"/identity;
}
int MetadataIdentityTests() {
    using namespace Gpu::Metal::PipelineRecipeIdentity;
    const auto identity=Identity("Example GPU / private device","x86_64",27,0,0);
    assert(identity.size()==64 && identity.find("Example")==std::string::npos && identity.find('/')==std::string::npos);
    assert(identity==Identity("Example GPU / private device","x86_64",27,0,0));
    assert(identity!=Identity("Other GPU","x86_64",27,0,0));
    assert(identity!=Identity("Example GPU / private device","arm64",27,0,0));
    assert(identity!=Identity("Example GPU / private device","x86_64",27,0,1));
    assert(AlternateArchitecture("x86_64")=="arm64");
    assert(AlternateArchitecture("arm64")=="x86_64");
    assert(AlternateArchitecture("unknown").empty());
    assert(Material("ab","c",27,0,0)!=Material("a","bc",27,0,0));
    assert(LegacyDirectory("100000575-27.0.0","27.0.0"));
    assert(!LegacyDirectory("100000575-26.0.0","27.0.0"));
    assert(!LegacyDirectory("../100000575-27.0.0","27.0.0"));
    assert(!LegacyDirectory("100000575-27.0.0/child","27.0.0"));
    assert(!LegacyDirectory("10000000000000000-27.0.0","27.0.0"));
    char directory[]="/tmp/bb-metal-identity-XXXXXX";assert(mkdtemp(directory));
    const std::filesystem::path base=std::filesystem::canonical(directory),target=base/"metal-cache"/"prewarm-v2"/identity;
    assert(PlainDirectories(base,target,true) && !PlainDirectories(base,target,false));
    std::filesystem::create_directories(target);
    assert(PlainDirectories(base,target,false));
    assert(!PlainDirectories(base,base/".."/"outside",true));
    const auto private_directory=base/"private";std::filesystem::create_directory(private_directory);
    std::filesystem::remove_all(base/"metal-cache");
    std::filesystem::create_directory_symlink(private_directory,base/"metal-cache");
    assert(!PlainDirectories(base,target,true));
    std::filesystem::remove(base/"metal-cache");std::filesystem::create_directories(target.parent_path());
    std::filesystem::create_directory_symlink(private_directory,target);
    assert(!PlainDirectories(base,target,false));
    std::filesystem::remove_all(base);
    puts("Source-recipe identity: device/architecture/OS separation, private hashed paths, bounded legacy names and symlink containment passed (no GPU)");
    return 0;
}
void FillOldRecipes(const std::filesystem::path& root,unsigned count,std::uint64_t bytes) {
    std::filesystem::create_directories(root);
    const auto now=std::filesystem::file_time_type::clock::now();
    for(unsigned i=0;i<count;++i) {
        char name[80];std::snprintf(name,sizeof(name),"%064x.json",i);
        const auto path=root/name;
        std::ofstream stream(path,std::ios::binary);
        stream.seekp(bytes-1);stream.put('x');stream.close();
        std::filesystem::last_write_time(path,now-std::chrono::seconds(count-i+60));
    }
}
}

// A real GPU behind a proxy that records archive requests. The production
// cache policy must not reach any archive API, even when environment variables
// request caching or an archive already exists on disk.
@interface BBArchiveSpyDevice : NSProxy {
    id<MTLDevice> underlying;
    std::atomic<unsigned> library_calls;
}
@property(nonatomic) unsigned archiveCalls;
@property(nonatomic,readonly) unsigned libraryCalls;
@property(nonatomic) BOOL captureDescriptors;
@property(nonatomic) std::uint64_t registryOverride;
@property(nonatomic,strong) NSMutableArray<MTLRenderPipelineDescriptor*>* renderDescriptors;
@property(nonatomic,strong) NSMutableArray<MTLComputePipelineDescriptor*>* computeDescriptors;
+ (instancetype)wrapping:(id<MTLDevice>)device;
@end
@implementation BBArchiveSpyDevice
+ (instancetype)wrapping:(id<MTLDevice>)device {
    auto proxy=[BBArchiveSpyDevice alloc]; proxy->underlying=device;
    proxy->library_calls.store(0,std::memory_order_relaxed);
    proxy.renderDescriptors=[NSMutableArray new];proxy.computeDescriptors=[NSMutableArray new];
    return proxy;
}
- (NSMethodSignature*)methodSignatureForSelector:(SEL)selector { return [(NSObject*)underlying methodSignatureForSelector:selector]; }
- (void)forwardInvocation:(NSInvocation*)invocation {
    if(invocation.selector==@selector(newLibraryWithSource:options:error:))
        library_calls.fetch_add(1,std::memory_order_relaxed);
    if(_captureDescriptors) {
        // Capture the actual descriptors decoded by production prewarming at
        // the driver boundary, instead of duplicating its recipe decoder.
        if(invocation.selector==@selector(newRenderPipelineStateWithDescriptor:error:)) {
            __unsafe_unretained MTLRenderPipelineDescriptor* descriptor=nil;
            [invocation getArgument:&descriptor atIndex:2];
            [_renderDescriptors addObject:[descriptor copy]];
        } else if(invocation.selector==@selector(newComputePipelineStateWithDescriptor:options:reflection:error:)) {
            __unsafe_unretained MTLComputePipelineDescriptor* descriptor=nil;
            [invocation getArgument:&descriptor atIndex:2];
            [_computeDescriptors addObject:[descriptor copy]];
        }
    }
    [invocation invokeWithTarget:underlying];
}
- (unsigned)libraryCalls {return library_calls.load(std::memory_order_relaxed);}
- (uint64_t)registryID {return _registryOverride ? _registryOverride : underlying.registryID;}
- (id<MTLBinaryArchive>)newBinaryArchiveWithDescriptor:(MTLBinaryArchiveDescriptor*)descriptor error:(NSError**)error {
    (void)descriptor;
    ++_archiveCalls;
    if (error) *error=[NSError errorWithDomain:@"BBArchiveSpy" code:1 userInfo:nil];
    return nil;
}
@end

static void LegacyRecipeMigration(id<MTLDevice> device) {
    using Compiler=Gpu::Metal::ShaderCompiler;
    char directory[]="/tmp/bb-metal-legacy-XXXXXX";assert(mkdtemp(directory));
    setenv("BB_DATA_DIR",directory,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");unsetenv("BB_METAL_SHADER_PREWARM");
    const auto stable=RecipeRoot(directory,device),legacy=LegacyRoot(directory,0x111),duplicate=LegacyRoot(directory,0x222);
    {
        Compiler compiler(device,2);compiler.EnablePrewarming();
        for(unsigned group:{41u,42u})AreaPipeline(compiler,group);
    }
    const auto originals=Recipes(directory);assert(originals.size()==2);
    std::filesystem::create_directories(legacy);std::filesystem::create_directories(duplicate);
    std::vector<std::pair<std::filesystem::path,std::string>> preserved;
    for(const auto& source:originals)for(const auto& root:{legacy,duplicate}) {
        const auto destination=root/source.filename();std::filesystem::copy_file(source,destination);
        preserved.emplace_back(destination,FileContents(destination));
    }
    NSData* seed=[NSData dataWithContentsOfFile:@(originals.front().c_str())];
    NSDictionary* seed_recipe=[NSJSONSerialization JSONObjectWithData:seed options:0 error:nil];
    const auto write_recipe=[&](const std::filesystem::path& root,unsigned group) {
        std::filesystem::create_directories(root);
        NSMutableDictionary* recipe=[seed_recipe mutableCopy];recipe[@"group"]=@(group);
        NSData* bytes=[NSJSONSerialization dataWithJSONObject:recipe options:NSJSONWritingSortedKeys error:nil];
        const auto path=root/(Digest(bytes.bytes,bytes.length)+".json");
        assert([bytes writeToFile:@(path.c_str()) atomically:YES]);return path;
    };
    // Valid bytes with an incorrect filename and hash-valid invalid schemas
    // must not be admitted or passed to Metal.
    std::ofstream(legacy/(std::string(64,'0')+".json"))<<FileContents(originals.front());
    const auto invalid=write_recipe(legacy,2048);
    const auto protected_root=std::filesystem::path(directory)/"protected";
    const auto protected_link=write_recipe(protected_root,43),protected_hardlink=write_recipe(protected_root,44);
    std::filesystem::create_symlink(protected_link,legacy/protected_link.filename());
    std::filesystem::create_hard_link(protected_hardlink,legacy/protected_hardlink.filename());
    const auto linked_directory=LegacyRoot(directory,0x333);
    std::filesystem::create_directory_symlink(protected_root,linked_directory);
    const auto wrong_os=std::filesystem::path(directory)/"metal-cache"/"prewarm-v1"/"444-0.0.0";
    write_recipe(wrong_os,45);
    for(const auto& file:{invalid,protected_link,protected_hardlink})preserved.emplace_back(file,FileContents(file));
    std::vector<std::filesystem::file_time_type> original_times;
    for(const auto& file:preserved)original_times.push_back(std::filesystem::last_write_time(file.first));
    std::filesystem::remove_all(stable);
    {
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.registryOverride=device.registryID+0x1000;spy.captureDescriptors=YES;
        assert(RecipeRoot(directory,(id<MTLDevice>)spy)==stable); // a different boot ID retains the source namespace
        Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while((compiler.GetWarmupStats().ready<2 || !std::filesystem::exists(stable/originals[0].filename()) ||
               !std::filesystem::exists(stable/originals[1].filename())) && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(compiler.GetWarmupStats().loaded==2 && compiler.GetWarmupStats().ready==2);
        assert(spy.computeDescriptors.count==2 && spy.renderDescriptors.count==0 && spy.libraryCalls==1);
        for(const auto& original:originals)assert(FileContents(stable/original.filename())==FileContents(legacy/original.filename()));
        std::promise<void> started,release;auto gate=release.get_future().share();
        compiler.Submit([&started,gate]{started.set_value();gate.wait();},true);started.get_future().wait();
        std::vector<std::filesystem::file_time_type> stable_times;
        for(const auto& original:originals)stable_times.push_back(std::filesystem::last_write_time(stable/original.filename()));
        for(unsigned repeat=0;repeat<3;++repeat)for(unsigned group:{41u,42u})AreaPipeline(compiler,group);
        assert(compiler.GetWarmupStats().hits==6 && compiler.GetWarmupStats().pending==0);
        assert(spy.computeDescriptors.count==2 && spy.libraryCalls==1 && spy.archiveCalls==0);
        for(unsigned i=0;i<originals.size();++i)
            assert(std::filesystem::last_write_time(stable/originals[i].filename())==stable_times[i]); // no foreground file touch/store
        release.set_value();
    }
    for(unsigned i=0;i<preserved.size();++i) {
        assert(FileContents(preserved[i].first)==preserved[i].second);
        assert(std::filesystem::last_write_time(preserved[i].first)==original_times[i]);
    }
    assert(std::filesystem::is_symlink(linked_directory));
    // A subsequent boot loads the persisted stable entries, without admitting
    // the same legacy source again or compiling a new foreground pipeline.
    {
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.registryOverride=device.registryID+0x2000;spy.captureDescriptors=YES;
        Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(compiler.GetWarmupStats().ready<2 && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(compiler.GetWarmupStats().loaded==2 && compiler.GetWarmupStats().ready==2);
        for(unsigned group:{41u,42u})AreaPipeline(compiler,group);
        assert(compiler.GetWarmupStats().hits==2 && spy.computeDescriptors.count==2 && spy.libraryCalls==1 && spy.archiveCalls==0);
    }
    // Malformed inputs consume the startup read budget even though none are
    // admitted. Put valid source behind 128 MiB of newer sparse invalid files:
    // an admission-only budget would read through and compile those sources.
    {
        const auto bounded_data=std::filesystem::path(directory)/"read-budget";
        const auto bounded_legacy=LegacyRoot(bounded_data,0x111);
        std::filesystem::create_directories(bounded_legacy);
        for(const auto& original:originals) {
            const auto target=bounded_legacy/original.filename();
            std::filesystem::copy_file(stable/original.filename(),target);
            std::filesystem::last_write_time(target,std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
        }
        const auto bytes=Gpu::Metal::PipelinePrewarmLimits::RecipeBytes;
        FillOldRecipes(bounded_legacy,Gpu::Metal::PipelinePrewarmLimits::Bytes/bytes,bytes);
        setenv("BB_DATA_DIR",bounded_data.c_str(),1);
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.captureDescriptors=YES;
        Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
        assert(compiler.GetWarmupStats().loaded==0 && compiler.GetWarmupStats().queued==0);
        assert(spy.libraryCalls==0 && spy.computeDescriptors.count==0 && spy.archiveCalls==0);
        assert(!std::filesystem::exists(RecipeRoot(bounded_data,device)));
    }
    std::filesystem::remove_all(directory);
    puts("Source-recipe migration: changed registry ID, exact-source reuse, bounded read/schema/hash/link admission, stable persistence and untouched legacy inputs passed");
}

static void OtherArchitectureRecipes(id<MTLDevice> device) {
    using Compiler=Gpu::Metal::ShaderCompiler;
    namespace IdentityPolicy=Gpu::Metal::PipelineRecipeIdentity;
    char directory[]="/tmp/bb-metal-other-architecture-XXXXXX";assert(mkdtemp(directory));
    setenv("BB_DATA_DIR",directory,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");unsetenv("BB_METAL_SHADER_PREWARM");
    const auto stable=RecipeRoot(directory,device);
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    const auto other=IdentityPolicy::AlternateArchitecture(IdentityPolicy::Architecture());assert(!other.empty());
    const auto base=std::filesystem::path(directory)/"metal-cache"/"prewarm-v2";
    const auto foreign=base/Identity(device.name.UTF8String,other,os.majorVersion,os.minorVersion,os.patchVersion);
    {
        Compiler compiler(device,2,Compiler::CacheMode::DriverManaged);compiler.EnablePrewarming();
        for(unsigned group:{41u,42u})AreaPipeline(compiler,group);
    }
    const auto originals=Recipes(directory);assert(originals.size()==2);
    std::vector<std::pair<std::filesystem::path,std::string>> preserved;
    std::vector<std::filesystem::file_time_type> times;
    unsigned source_context=0;
    for(const auto& target:{foreign,
            base/Identity("Unrelated GPU",other,os.majorVersion,os.minorVersion,os.patchVersion),
            base/Identity(device.name.UTF8String,other,os.majorVersion,os.minorVersion,os.patchVersion+1)}) {
        std::filesystem::create_directories(target);
        for(const auto& source:originals) {
            NSData* bytes=[NSData dataWithContentsOfFile:@(source.c_str())];
            if(source_context) {
                NSMutableDictionary* recipe=[[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil] mutableCopy];
                recipe[@"group"]=@([recipe[@"group"] unsignedIntValue]+source_context*10);
                bytes=[NSJSONSerialization dataWithJSONObject:recipe options:NSJSONWritingSortedKeys error:nil];
            }
            const auto destination=target/(Digest(bytes.bytes,bytes.length)+".json");
            assert([bytes writeToFile:@(destination.c_str()) atomically:YES]);
            preserved.emplace_back(destination,FileContents(destination));times.push_back(std::filesystem::last_write_time(destination));
        }
        ++source_context;
    }
    std::filesystem::remove_all(stable);
    for(unsigned boot=0;boot<2;++boot) {
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.captureDescriptors=YES;
        Compiler compiler((id<MTLDevice>)spy,2,Compiler::CacheMode::DriverManaged);compiler.EnablePrewarming();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while((compiler.GetWarmupStats().ready<2 || !std::filesystem::exists(stable/originals[0].filename()) ||
               !std::filesystem::exists(stable/originals[1].filename())) && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        assert(compiler.GetWarmupStats().loaded==2 && compiler.GetWarmupStats().ready==2);
        assert(spy.computeDescriptors.count==2 && spy.libraryCalls==1 && spy.archiveCalls==0);
        for(unsigned group:{41u,42u})AreaPipeline(compiler,group);
        assert(compiler.GetWarmupStats().hits==2);
        if(boot==1) {
            AreaPipeline(compiler,43); // Same source, different descriptor must miss.
            assert(compiler.GetWarmupStats().hits==2 && spy.computeDescriptors.count==3);
        }
    }
    for(unsigned k=0;k<preserved.size();++k) {
        assert(FileContents(preserved[k].first)==preserved[k].second);
        assert(std::filesystem::last_write_time(preserved[k].first)==times[k]);
    }
    for(const auto& source:originals)assert(FileContents(stable/source.filename())==FileContents(foreign/source.filename()));
    // An alternate namespace remains subject to the normal no-symlink rule.
    const auto linked_data=std::filesystem::path(directory)/"linked-data";
    const auto linked_base=linked_data/"metal-cache"/"prewarm-v2";std::filesystem::create_directories(linked_base);
    std::filesystem::create_directory_symlink(foreign,linked_base/foreign.filename());
    setenv("BB_DATA_DIR",linked_data.c_str(),1);
    {Compiler compiler(device,2,Compiler::CacheMode::DriverManaged);compiler.EnablePrewarming();assert(compiler.GetWarmupStats().loaded==0);}
    puts("Other-architecture source recipes: current-device compilation, exact reuse, device/OS isolation, deduplication, persistence and symlink rejection passed");
}

static int CapturedRecipes(id<MTLDevice> device,int count,const char* const* paths) {
    char directory[]="/tmp/bb-metal-real-recipes-XXXXXX";assert(mkdtemp(directory));
    const auto root=RecipeRoot(directory,device);std::filesystem::create_directories(root);
    std::vector<std::pair<std::filesystem::path,std::string>> originals;
    std::unordered_set<std::string> sources;
    for(int i=0;i<count;++i) {
        const std::filesystem::path source(paths[i]);
        const auto destination=root/source.filename();
        std::filesystem::copy_file(source,destination);
        originals.emplace_back(destination,FileContents(destination));
        NSData* bytes=[NSData dataWithContentsOfFile:@(destination.c_str())];
        NSDictionary* recipe=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
        assert([recipe isKindOfClass:NSDictionary.class]);
        for(NSString* stage in @[@"vertex",@"fragment",@"source"])
            if([recipe[stage] isKindOfClass:NSString.class] && [recipe[stage] length])
                sources.emplace([recipe[stage] UTF8String]);
    }
    setenv("BB_DATA_DIR",directory,1);unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");
    unsetenv("BB_METAL_SHADER_PREWARM");
    using Compiler=Gpu::Metal::ShaderCompiler;
    {
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.captureDescriptors=YES;
        Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
        while(compiler.GetWarmupStats().ready<static_cast<unsigned>(count) &&
              std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto stats=compiler.GetWarmupStats();
        assert(stats.loaded==static_cast<unsigned>(count) && stats.ready==static_cast<unsigned>(count));
        assert(spy.libraryCalls==sources.size());
        NSArray<MTLRenderPipelineDescriptor*>* renders=[spy.renderDescriptors copy];
        NSArray<MTLComputePipelineDescriptor*>* computes=[spy.computeDescriptors copy];
        assert(renders.count+computes.count==static_cast<unsigned>(count));
        const auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<2;++repeat) {
            for(MTLRenderPipelineDescriptor* descriptor in renders) {
                NSError* error=nil;assert(compiler.RenderPipeline(descriptor,&error) && !error);
            }
            for(MTLComputePipelineDescriptor* descriptor in computes) {
                NSError* error=nil;assert(compiler.ComputePipeline(descriptor,&error) && !error);
            }
        }
        stats=compiler.GetWarmupStats();
        assert(stats.hits==static_cast<unsigned>(count)*2 && stats.pending==0);
        assert(spy.renderDescriptors.count==renders.count && spy.computeDescriptors.count==computes.count);
        assert(spy.libraryCalls==sources.size());
        assert(spy.archiveCalls==0);
        printf("Captured prewarm: %d real recipes (%lu render, %lu compute), %llu foreground cache hits, "
               "zero additional Metal pipeline creation calls; demand total %.3f ms\n",count,
               static_cast<unsigned long>(renders.count),static_cast<unsigned long>(computes.count),
               static_cast<unsigned long long>(stats.hits),
               std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
        printf("Captured prewarm source sharing: %u library calls for %zu exact unique sources\n",
               spy.libraryCalls,sources.size());
    }
    for(const auto& [path,contents]:originals)assert(FileContents(path)==contents);
    std::filesystem::remove_all(directory);
    return 0;
}

static void InvalidSourceRecipes(id<MTLDevice> device) {
    using Compiler=Gpu::Metal::ShaderCompiler;
    char directory[]="/tmp/bb-metal-invalid-source-XXXXXX";assert(mkdtemp(directory));
    unsetenv("BB_GAME_DIR");unsetenv("BB_SOURCE_GAME");unsetenv("BB_METAL_SHADER_PREWARM");
    setenv("BB_DATA_DIR",directory,1);
    constexpr const char* vertex="#include <metal_stdlib>\nusing namespace metal; vertex float4 main0(uint i [[vertex_id]]) { return float4(float(i),0,0,1); }";
    {
        Compiler compiler(device,2);compiler.EnablePrewarming();AreaPipeline(compiler,64);
        auto render=[MTLRenderPipelineDescriptor new];
        render.vertexFunction=compiler.Function(1,vertex,0);
        render.fragmentFunction=compiler.Function(2,"#include <metal_stdlib>\nusing namespace metal; fragment float4 main0() { return float4(1); }",0);
        render.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
        assert(compiler.RenderPipeline(render,nil));
    }
    NSDictionary *compute=nil,*render=nil;
    for(const auto& path:Recipes(directory)) {
        NSData* bytes=[NSData dataWithContentsOfFile:@(path.c_str())];
        NSDictionary* recipe=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
        if([recipe[@"kind"] isEqual:@"compute"])compute=recipe;else render=recipe;
    }
    assert(compute && render);
    NSMutableArray<NSDictionary*>* recipes=[NSMutableArray arrayWithObject:compute];
    // Hash-valid schemas can still contain compiler errors or a valid main0
    // of the wrong stage. Neither may reach Metal's descriptor assertions.
    NSString* invalid=@"#error intentionally invalid cached shader\n";
    for(NSString* source in @[invalid,@(vertex)]) {
        NSMutableDictionary* recipe=[compute mutableCopy];recipe[@"source"]=source;[recipes addObject:recipe];
    }
    for(NSArray* change in @[@[@"vertex",invalid],@[@"fragment",invalid],
                            @[@"vertex",@(kAreaCompute)],@[@"fragment",@(vertex)]]) {
        NSMutableDictionary* recipe=[render mutableCopy];recipe[change[0]]=change[1];[recipes addObject:recipe];
    }
    for(bool parallel:{false,true}) {
        const auto data=std::filesystem::path(directory)/(parallel ? "offline" : "live");
        const auto root=RecipeRoot(data,device);std::filesystem::create_directories(root);
        for(NSDictionary* recipe in recipes) {
            NSData* bytes=[NSJSONSerialization dataWithJSONObject:recipe options:NSJSONWritingSortedKeys error:nil];
            const auto path=root/(Digest(bytes.bytes,bytes.length)+".json");
            assert([bytes writeToFile:@(path.c_str()) atomically:YES]);
        }
        setenv("BB_DATA_DIR",data.c_str(),1);
        auto spy=[BBArchiveSpyDevice wrapping:device];spy.captureDescriptors=YES;
        Compiler compiler((id<MTLDevice>)spy,parallel ? 1 : 2);compiler.EnablePrewarming(parallel);
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
        while(compiler.GetWarmupStats().queued && std::chrono::steady_clock::now()<deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto stats=compiler.GetWarmupStats();
        assert(stats.loaded==7 && stats.ready==1 && !stats.queued);
        assert(spy.computeDescriptors.count==1 && !spy.renderDescriptors.count && !spy.archiveCalls);
        AreaPipeline(compiler,64);assert(compiler.GetWarmupStats().hits==1);
    }
    std::filesystem::remove_all(directory);unsetenv("BB_DATA_DIR");
    puts("Invalid cached shader source/stage rejected before Metal descriptors; live/offline queues complete and valid recipe remains usable");
}

int main(int argc,const char** argv) {
    @autoreleasepool {
        if(argc==2 && !std::strcmp(argv[1],"--metadata-only"))return MetadataIdentityTests();
        MetadataIdentityTests();
        auto device=MTLCreateSystemDefaultDevice(); if (!device) return 77;
        if(argc>=3 && !std::strcmp(argv[1],"--recipes"))return CapturedRecipes(device,argc-2,argv+2);
        if(argc!=1)return 2;
        InvalidSourceRecipes(device);
        char directory[]="/tmp/bb-metal-cache-XXXXXX"; assert(mkdtemp(directory));
        setenv("BB_METAL_CACHE_DIR",directory,1); unsetenv("BB_METAL_PIPELINE_CACHE");
        using Compiler=Gpu::Metal::ShaderCompiler;
        auto compile=[](Compiler& compiler) {
            auto render=[MTLRenderPipelineDescriptor new];
            render.vertexFunction=compiler.Function(1,"#include <metal_stdlib>\nusing namespace metal; vertex float4 main0(uint i [[vertex_id]]) { return float4(float(i),0,0,1); }",0);
            render.fragmentFunction=compiler.Function(2,"#include <metal_stdlib>\nusing namespace metal; fragment float4 main0() { return float4(1); }",0);
            render.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
            render.colorAttachments[0].blendingEnabled=YES;
            render.colorAttachments[0].sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
            render.colorAttachments[0].destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
            render.colorAttachments[0].sourceAlphaBlendFactor=MTLBlendFactorOne;
            render.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            render.stencilAttachmentPixelFormat=MTLPixelFormatDepth32Float_Stencil8;
            render.inputPrimitiveTopology=MTLPrimitiveTopologyClassTriangle;
            NSError* error=nil;
            assert(compiler.RenderPipeline(render,&error) && !error);
            auto compute=[MTLComputePipelineDescriptor new];
            compute.computeFunction=compiler.Function(3,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            assert(compiler.ComputePipeline(compute,&error) && !error);
        };
        auto runtime=[&] {
            setenv("BB_METAL_PIPELINE_CACHE","1",1);
            auto spy=[BBArchiveSpyDevice wrapping:device];
            {
                Compiler compiler((id<MTLDevice>)spy,1);
                compile(compiler); compiler.FlushCache();
                const auto stats=compiler.GetCacheStats();
                assert(!stats.loaded && !stats.hits && !stats.misses && !stats.writes);
            }
            assert(spy.archiveCalls==0);
        };
        runtime(); assert(std::filesystem::is_empty(directory));
        auto run=[&](bool loaded,bool expect_hits) {
            Compiler compiler(device,1,Compiler::CacheMode::OfflineArchive);
            assert(compiler.GetCacheStats().loaded==loaded);
            compile(compiler);
            compiler.FlushCache();
            const auto stats=compiler.GetCacheStats();
            assert(expect_hits ? stats.hits==2 && stats.misses==0 : stats.misses==2 && stats.writes>0);
        };
        run(false,false); run(true,true);
        runtime(); // the live runtime does not even load an existing archive
        unsigned archives=0;
        for (const auto& entry:std::filesystem::directory_iterator(directory)) {
            assert(entry.path().extension()==".metalarc");
            std::ofstream(entry.path(),std::ios::binary|std::ios::trunc)<<"broken archive";
            ++archives;
        }
        assert(archives==1);
        run(false,false); run(true,true); // corruption is recovered atomically
        setenv("BB_METAL_PIPELINE_CACHE","0",1);
        {
            Compiler disabled(device,1,Compiler::CacheMode::OfflineArchive);
            assert(!disabled.GetCacheStats().loaded);
        }
        std::filesystem::remove_all(directory);
        // Source/descriptor prewarming works without binary archive calls.
        char warm_directory[]="/tmp/bb-metal-prewarm-XXXXXX";assert(mkdtemp(warm_directory));
        setenv("BB_DATA_DIR",warm_directory,1);unsetenv("BB_METAL_SHADER_PREWARM");
        setenv("BB_GAME_DIR",warm_directory,1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::is_empty(warm_directory)); // never cache inside the game dump
        const auto alias=std::filesystem::path(warm_directory)/"data-alias";
        std::filesystem::create_directory_symlink(warm_directory,alias);
        setenv("BB_DATA_DIR",alias.c_str(),1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::directory_iterator(warm_directory)!=std::filesystem::directory_iterator{});
        assert(std::distance(std::filesystem::directory_iterator(warm_directory),std::filesystem::directory_iterator{})==1);
        std::filesystem::remove(alias);unsetenv("BB_GAME_DIR");
        setenv("BB_DATA_DIR",warm_directory,1);setenv("BB_METAL_SHADER_PREWARM","0",1);
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();compile(compiler);
        }
        assert(std::filesystem::is_empty(warm_directory));unsetenv("BB_METAL_SHADER_PREWARM");
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();compile(compiler);
            assert(compiler.GetWarmupStats().loaded==0 && spy.archiveCalls==0);
            // A reused guest key with changed translation must compile new code.
            auto a=compiler.Function(99,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            auto b=compiler.Function(99,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(device uint* p [[buffer(0)]],uint i [[thread_position_in_grid]]) { p[i]=7; }",0);
            assert(a && b && a!=b);
        }
        unsigned recipe_count=0;
        for(const auto& entry:std::filesystem::recursive_directory_iterator(warm_directory))
            if(entry.path().extension()==".json") ++recipe_count;
        assert(recipe_count==2);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(compiler.GetWarmupStats().ready<2 && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(compiler.GetWarmupStats().loaded==2 && compiler.GetWarmupStats().ready==2);
            compile(compiler);assert(compiler.GetWarmupStats().hits==2 && spy.archiveCalls==0);
            // A changed descriptor cannot reuse the original warm pipeline.
            auto changed=[MTLComputePipelineDescriptor new];
            changed.computeFunction=compiler.Function(3,"#include <metal_stdlib>\nusing namespace metal; kernel void main0(uint i [[thread_position_in_grid]]) { }",0);
            changed.maxTotalThreadsPerThreadgroup=64;
            assert(compiler.ComputePipeline(changed,nil));assert(compiler.GetWarmupStats().hits==2);
        }
        LegacyRecipeMigration(device);
        OtherArchitectureRecipes(device);
        // A route visiting several areas must learn beyond the former 512
        // recipes. Hold the background worker to exercise pending admission,
        // rather than merely compiling/writing one descriptor at a time.
        char areas_directory[]="/tmp/bb-metal-areas-XXXXXX";assert(mkdtemp(areas_directory));
        constexpr unsigned area_count=513;
        setenv("BB_DATA_DIR",areas_directory,1);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            std::promise<void> started,release;
            auto gate=release.get_future().share();
            compiler.Submit([&started,gate]{started.set_value();gate.wait();},true);
            started.get_future().wait();
            for(unsigned group=1;group<=area_count;++group) AreaPipeline(compiler,group);
            const auto stats=compiler.GetWarmupStats();
            assert(stats.pending==area_count && stats.retained==area_count);
            assert(spy.libraryCalls==1); // all descriptors share the exact compiled function
            assert(stats.pending+stats.queued<=Gpu::Metal::PipelinePrewarmLimits::Recipes);
            assert(stats.pending_bytes+stats.queued_bytes<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            assert(stats.retained_bytes<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            release.set_value();assert(spy.archiveCalls==0);
        }
        assert(Recipes(areas_directory).size()==area_count);
        std::filesystem::path hot_recipe,cold_recipe;
        std::string hot_contents;
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(60);
            while(compiler.GetWarmupStats().ready<area_count && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            const auto stats=compiler.GetWarmupStats();
            assert(stats.loaded==area_count && stats.ready==area_count && stats.retained==area_count);
            assert(spy.libraryCalls==1); // startup also shares the stage across all 513 recipes
            hot_recipe=AreaRecipeFile(areas_directory,area_count);
            cold_recipe=AreaRecipeFile(areas_directory,area_count-1);
            assert(!hot_recipe.empty() && !cold_recipe.empty());
            hot_contents=FileContents(hot_recipe);
            const auto now=std::filesystem::file_time_type::clock::now();
            std::filesystem::last_write_time(hot_recipe,now-std::chrono::hours(3));
            std::filesystem::last_write_time(cold_recipe,now-std::chrono::hours(2));
            const auto untouched=std::filesystem::last_write_time(hot_recipe);
            std::promise<void> started,release;
            auto gate=release.get_future().share();
            compiler.Submit([&started,gate]{started.set_value();gate.wait();},true);
            started.get_future().wait();
            for(unsigned repeat=0;repeat<8;++repeat)AreaPipeline(compiler,area_count);
            const auto pending=compiler.GetWarmupStats();
            assert(pending.hits==8 && pending.pending_touches==1);
            assert(std::filesystem::last_write_time(hot_recipe)==untouched); // no demand-thread disk I/O
            release.set_value();
            const auto touch_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(!compiler.GetWarmupStats().recency_updates && std::chrono::steady_clock::now()<touch_deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(compiler.GetWarmupStats().recency_updates==1);
            assert(std::filesystem::last_write_time(hot_recipe)>std::filesystem::last_write_time(cold_recipe));
            assert(FileContents(hot_recipe)==hot_contents);
            // Fill only the remaining count budget. The promoted learned recipe
            // must survive a new observation; an older unused recipe must go.
            FillOldRecipes(RecipeRoot(areas_directory,device),
                Gpu::Metal::PipelinePrewarmLimits::Recipes-area_count,1);
            AreaPipeline(compiler,area_count+1);
            assert(spy.archiveCalls==0);
        }
        assert(Recipes(areas_directory).size()==Gpu::Metal::PipelinePrewarmLimits::Recipes);
        assert(std::filesystem::exists(hot_recipe) && !std::filesystem::exists(cold_recipe));
        assert(FileContents(hot_recipe)==hot_contents && !AreaRecipeFile(areas_directory,area_count+1).empty());
        std::filesystem::remove_all(areas_directory);
        // A cache entry replaced after loading must not redirect an asynchronous
        // timestamp update into another file, through either link mechanism.
        char touch_directory[]="/tmp/bb-metal-touch-XXXXXX";assert(mkdtemp(touch_directory));
        setenv("BB_DATA_DIR",touch_directory,1);
        {
            Compiler compiler(device,2);compiler.EnablePrewarming();
            for(unsigned group:{700u,701u,702u})AreaPipeline(compiler,group);
        }
        const auto sentinel=std::filesystem::path(touch_directory)/"protected.txt";
        std::ofstream(sentinel)<<"untouched";
        const auto old_time=std::filesystem::file_time_type::clock::now()-std::chrono::hours(1);
        std::filesystem::last_write_time(sentinel,old_time);
        {
            auto spy=[BBArchiveSpyDevice wrapping:device];
            Compiler compiler((id<MTLDevice>)spy,2);compiler.EnablePrewarming();
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
            while(compiler.GetWarmupStats().ready<3 && std::chrono::steady_clock::now()<deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            assert(compiler.GetWarmupStats().ready==3);
            const auto linked=AreaRecipeFile(touch_directory,701),hardlinked=AreaRecipeFile(touch_directory,702);
            assert(!linked.empty() && !hardlinked.empty());
            std::filesystem::remove(linked);std::filesystem::create_symlink(sentinel,linked);
            std::filesystem::remove(hardlinked);std::filesystem::create_hard_link(sentinel,hardlinked);
            for(unsigned repeat=0;repeat<8;++repeat)
                for(unsigned group:{700u,701u,702u})AreaPipeline(compiler,group);
            assert(compiler.GetWarmupStats().pending_touches<=3 && spy.archiveCalls==0);
        }
        assert(FileContents(sentinel)=="untouched" && std::filesystem::last_write_time(sentinel)==old_time);
        std::filesystem::remove_all(touch_directory);
        // A full count or byte budget must evict the oldest regular generated
        // recipe and retain the newly observed one. Sparse files make the
        // 128 MiB disk boundary is inexpensive without compiling 1024 shaders.
        for(bool byte_limit:{false,true}) {
            char bounds_directory[]="/tmp/bb-metal-bounds-XXXXXX";assert(mkdtemp(bounds_directory));
            setenv("BB_DATA_DIR",bounds_directory,1);
            const auto root=RecipeRoot(bounds_directory,device);
            {
                Compiler compiler(device,2);compiler.EnablePrewarming();
                const auto old_count=static_cast<unsigned>(byte_limit ?
                    Gpu::Metal::PipelinePrewarmLimits::Bytes/Gpu::Metal::PipelinePrewarmLimits::RecipeBytes :
                    Gpu::Metal::PipelinePrewarmLimits::Recipes);
                FillOldRecipes(root,old_count,byte_limit ? Gpu::Metal::PipelinePrewarmLimits::RecipeBytes : 1);
                const auto oldest=root/(std::string(64,'0')+".json");assert(std::filesystem::exists(oldest));
                const auto sentinel=std::filesystem::path(bounds_directory)/"protected.txt";
                std::ofstream(sentinel)<<"untouched";
                const auto link=root/(std::string(64,'f')+".json");
                std::filesystem::create_symlink(sentinel,link);
                std::ofstream(root/"unrelated.txt")<<"untouched";
                AreaPipeline(compiler,73);
            }
            assert(Recipes(bounds_directory).size()<=Gpu::Metal::PipelinePrewarmLimits::Recipes);
            assert(RecipeBytes(bounds_directory)<=Gpu::Metal::PipelinePrewarmLimits::Bytes);
            assert(!std::filesystem::exists(root/(std::string(64,'0')+".json")));
            // The only non-placeholder recipe must be the newly learned one.
            unsigned learned=0;
            for(const auto& file:Recipes(bounds_directory)) {
                std::ifstream input(file);char first=0;input.get(first);
                if(first=='{')++learned;
            }
            assert(learned==1);
            assert(std::filesystem::is_symlink(root/(std::string(64,'f')+".json")));
            std::string text;std::ifstream(std::filesystem::path(bounds_directory)/"protected.txt")>>text;
            assert(text=="untouched");text.clear();std::ifstream(root/"unrelated.txt")>>text;assert(text=="untouched");
            std::filesystem::remove_all(bounds_directory);
        }
        setenv("BB_DATA_DIR",warm_directory,1);
        // Corruption is a cache miss; no partially decoded recipe is used.
        for(const auto& entry:std::filesystem::recursive_directory_iterator(warm_directory))
            if(entry.path().extension()==".json") std::ofstream(entry.path(),std::ios::trunc)<<"{broken";
        {
            Compiler compiler(device,1);compiler.EnablePrewarming();assert(!compiler.GetWarmupStats().loaded);
            compile(compiler);
        }
        std::filesystem::remove_all(warm_directory);
        puts("Metal runtime: no binary archives; 513-recipe persistence/reload, coalesced background hit recency, hot-recipe retention at count bound, bounded pending inputs, count/byte eviction, source/symlink/hardlink protection, opt-out and corruption recovery passed; offline archive fixture passed");
    }
}
