// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Gpu::Metal in mtl_compiler.mm. Recipes are disposable local
// compiler inputs. A current descriptor AND exact source must match for a hit.
namespace {
struct RecipeFile {NSString* name;std::uint64_t bytes;double modified;};
std::string RecipeDigest(const void* bytes,std::size_t count) {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(bytes,static_cast<CC_LONG>(count),digest);
    std::string key;for(unsigned char b:digest){key+="0123456789abcdef"[b>>4];key+="0123456789abcdef"[b&15];}return key;
}
bool RecipeFilename(NSString* name) {
    if(name.length!=69 || ![name.pathExtension isEqualToString:@"json"]) return false;
    for(unsigned i=0;i<64;++i) {
        const auto c=[name characterAtIndex:i];
        if(!((c>='0' && c<='9') || (c>='a' && c<='f'))) return false;
    }
    return true;
}
std::vector<RecipeFile> RecipeInventory(NSString* root) {
    std::vector<RecipeFile> result;
    for(NSString* name in [NSFileManager.defaultManager contentsOfDirectoryAtPath:root error:nil]) {
        if(!RecipeFilename(name)) continue;
        NSURL* url=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:name]];
        NSNumber *size=nil,*regular=nil,*symlink=nil;NSDate* modified=nil;
        [url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
        [url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:nil];
        [url getResourceValue:&modified forKey:NSURLContentModificationDateKey error:nil];
        if(regular.boolValue && !symlink.boolValue && size)
            result.push_back({name,size.unsignedLongLongValue,modified.timeIntervalSince1970});
    }
    return result;
}
NSData* RecipeData(NSDictionary* recipe) {
    return recipe ? [NSJSONSerialization dataWithJSONObject:recipe options:NSJSONWritingSortedKeys error:nil] : nil;
}
std::string RecipeKey(NSDictionary* recipe) {
    NSData* data=RecipeData(recipe);if(!data) return {};
    return RecipeDigest(data.bytes,data.length);
}
bool RecipeNumber(id value,unsigned maximum) {
    return [value isKindOfClass:NSNumber.class] && [value doubleValue]==[value unsignedLongLongValue] &&
        [value unsignedLongLongValue]<=maximum;
}
bool RecipeFormat(id value) {
    if(!RecipeNumber(value,600)) return false;
    // Formats produced by this backend, including its depth-only pipelines.
    switch([value unsignedIntValue]) {
    case MTLPixelFormatInvalid:case MTLPixelFormatR8Unorm:case MTLPixelFormatR8Snorm:
    case MTLPixelFormatR8Uint:case MTLPixelFormatR8Sint:case MTLPixelFormatR16Unorm:
    case MTLPixelFormatR16Snorm:case MTLPixelFormatR16Uint:case MTLPixelFormatR16Sint:case MTLPixelFormatR16Float:
    case MTLPixelFormatRG8Unorm:case MTLPixelFormatRG8Snorm:case MTLPixelFormatRG8Uint:case MTLPixelFormatRG8Sint:
    case MTLPixelFormatR32Uint:case MTLPixelFormatR32Sint:case MTLPixelFormatR32Float:
    case MTLPixelFormatRG16Unorm:case MTLPixelFormatRG16Snorm:case MTLPixelFormatRG16Uint:case MTLPixelFormatRG16Sint:case MTLPixelFormatRG16Float:
    case MTLPixelFormatRGBA8Unorm:case MTLPixelFormatRGBA8Unorm_sRGB:case MTLPixelFormatRGBA8Snorm:
    case MTLPixelFormatRGBA8Uint:case MTLPixelFormatRGBA8Sint:case MTLPixelFormatBGRA8Unorm:case MTLPixelFormatBGRA8Unorm_sRGB:
    case MTLPixelFormatRGB10A2Unorm:case MTLPixelFormatRGB10A2Uint:case MTLPixelFormatRG11B10Float:case MTLPixelFormatRGB9E5Float:
    case MTLPixelFormatRG32Uint:case MTLPixelFormatRG32Sint:case MTLPixelFormatRG32Float:
    case MTLPixelFormatRGBA16Unorm:case MTLPixelFormatRGBA16Snorm:case MTLPixelFormatRGBA16Uint:case MTLPixelFormatRGBA16Sint:case MTLPixelFormatRGBA16Float:
    case MTLPixelFormatRGBA32Uint:case MTLPixelFormatRGBA32Sint:case MTLPixelFormatRGBA32Float:
    case MTLPixelFormatDepth16Unorm:case MTLPixelFormatDepth32Float:case MTLPixelFormatDepth32Float_Stencil8:case MTLPixelFormatStencil8:return true;
    default:return false;
    }
}
bool RecipeSchema(NSDictionary* r) {
    if(![r isKindOfClass:NSDictionary.class] || ![r[@"version"] isEqual:@1])return false;
    if([r[@"kind"] isEqual:@"compute"])return r.count==5 &&
        [r[@"source"] isKindOfClass:NSString.class] && [r[@"source"] length] &&
        [r[@"source"] length]<=PipelinePrewarmLimits::RecipeBytes/2 &&
        RecipeNumber(r[@"group"],1024) && RecipeNumber(r[@"multiple"],1);
    if(![r[@"kind"] isEqual:@"render"] || r.count!=6)return false;
    NSArray* s=r[@"state"];NSArray* colors=r[@"colors"];
    if(![s isKindOfClass:NSArray.class] || s.count!=13 || ![colors isKindOfClass:NSArray.class] || colors.count!=8 ||
        ![r[@"vertex"] isKindOfClass:NSString.class] || ![r[@"vertex"] length] ||
        ![r[@"fragment"] isKindOfClass:NSString.class] ||
        [r[@"vertex"] length]>PipelinePrewarmLimits::RecipeBytes/2 || [r[@"fragment"] length]>PipelinePrewarmLimits::RecipeBytes/2 ||
        !RecipeFormat(s[0]) || !RecipeFormat(s[1]))return false;
    for(unsigned i=2;i<13;++i)if(!RecipeNumber(s[i],i==8 ? 64 : i==2 ? 8 : i==12 ? 2 : i==6 || i==7 || i==10 ? 3 : 1))return false;
    for(unsigned k=0;k<8;++k) {
        NSArray* c=colors[k];if(![c isKindOfClass:NSArray.class] || c.count!=9 || !RecipeFormat(c[0]))return false;
        for(unsigned i=1;i<9;++i)if(!RecipeNumber(c[i],i==1 ? 15 : i==2 ? 1 : i==5 || i==8 ? 4 : 18))return false;
    }
    return true;
}
bool RecipeRootSafe(NSString* root,bool allow_missing) {
    const std::filesystem::path target(root.fileSystemRepresentation);
    return PipelineRecipeIdentity::PlainDirectories(target.parent_path().parent_path().parent_path(),target,allow_missing);
}
// Bounded startup inventory. Never recurse, follow a cache-directory symlink,
// or materialize an unbounded directory listing before enforcing the limit.
std::vector<RecipeFile> StartupRecipes(NSString* root,std::size_t& remaining) {
    std::vector<RecipeFile> result;
    std::error_code error;
    for(std::filesystem::directory_iterator it(root.fileSystemRepresentation,error),end;
        !error && it!=end && remaining;it.increment(error)) {
        --remaining;
        const auto path=it->path();NSString* name=@(path.filename().c_str());
        if(!RecipeFilename(name))continue;
        struct stat status{};
        if(::lstat(path.c_str(),&status) || !S_ISREG(status.st_mode) || status.st_nlink!=1 || status.st_size<0)continue;
        result.push_back({name,static_cast<std::uint64_t>(status.st_size),
            double(status.st_mtimespec.tv_sec)+double(status.st_mtimespec.tv_nsec)*1e-9});
    }
    return result;
}
NSData* ReadRecipe(NSString* path,std::uint64_t& read_budget) {
    const int fd=::open(path.fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return nil;
    struct stat status{};
    if(::fstat(fd,&status) || !S_ISREG(status.st_mode) || status.st_nlink!=1 || status.st_size<=0 ||
        static_cast<std::uint64_t>(status.st_size)>PipelinePrewarmLimits::RecipeBytes ||
        static_cast<std::uint64_t>(status.st_size)>read_budget){::close(fd);return nil;}
    // Charge every attempted input before reading or validating it. Rejected
    // hashes, malformed JSON and interrupted reads must not bypass the bound.
    read_budget-=static_cast<std::uint64_t>(status.st_size);
    NSMutableData* bytes=[NSMutableData dataWithLength:static_cast<NSUInteger>(status.st_size)];
    std::size_t copied=0;
    while(copied<bytes.length) {
        const auto count=::read(fd,static_cast<char*>(bytes.mutableBytes)+copied,bytes.length-copied);
        if(count<=0)break;
        copied+=static_cast<std::size_t>(count);
    }
    struct stat after{};
    const bool complete=copied==bytes.length && !::fstat(fd,&after) && after.st_size==status.st_size &&
        after.st_mtimespec.tv_sec==status.st_mtimespec.tv_sec && after.st_mtimespec.tv_nsec==status.st_mtimespec.tv_nsec;
    ::close(fd);return complete ? bytes : nil;
}
}
ShaderCompiler::WarmupStats ShaderCompiler::GetWarmupStats() {
    std::lock_guard guard(warm_lock);auto result=warm_stats;
    result.retained=warm_pipelines.size();result.retained_bytes=warm_bytes;
    result.pending=recipe_writes.size();result.pending_bytes=pending_recipe_bytes;
    result.queued=warm_queued_count;result.queued_bytes=warm_queued_bytes;
    result.pending_touches=recipe_touches.size();
    return result;
}
void ShaderCompiler::EnablePrewarming() {
    if(warm_root) return;
    const char* enabled=std::getenv("BB_METAL_SHADER_PREWARM");const char* data=std::getenv("BB_DATA_DIR");
    if(archive || !data || !*data || (enabled && !std::strcmp(enabled,"0"))) return;
    NSString* resolved_data=[NSURL fileURLWithPath:@(data)].URLByResolvingSymlinksInPath.path;
    NSString* root=[resolved_data stringByAppendingPathComponent:@"metal-cache/prewarm-v2"];
    for(const char* variable:{"BB_GAME_DIR","BB_SOURCE_GAME"}) if(const char* game=std::getenv(variable);game && *game) {
        NSString* source=[NSURL fileURLWithPath:@(game)].URLByResolvingSymlinksInPath.path;
        if([root isEqualToString:source] || [root hasPrefix:[source stringByAppendingString:@"/"]]) return;
    }
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    NSString* name=device.name;if(!name.length || name.length>4096)return;
    const auto material=PipelineRecipeIdentity::Material(name.UTF8String,PipelineRecipeIdentity::Architecture(),
        os.majorVersion,os.minorVersion,os.patchVersion);
    const auto identity=RecipeDigest(material.data(),material.size());
    root=[root stringByAppendingPathComponent:@(identity.c_str())];
    if(!RecipeRootSafe(root,true))return;
    warm_root=root;
    struct Candidate {NSString* root;RecipeFile file;unsigned priority;};
    std::vector<Candidate> files;
    std::size_t remaining=PipelineRecipeIdentity::InventoryEntries;
    if(RecipeRootSafe(root,false))for(const auto& file:StartupRecipes(root,remaining))files.push_back({root,file,0});
    NSString* legacy=[resolved_data stringByAppendingPathComponent:@"metal-cache/prewarm-v1"];
    const auto version=PipelineRecipeIdentity::OsVersion(os.majorVersion,os.minorVersion,os.patchVersion);
    NSString* current=[NSString stringWithFormat:@"%llx-%s",device.registryID,version.c_str()];
    // Old IDs carry no persistent hardware identity. Import only source JSON
    // from this data root and OS, then compile on the current device. Full source
    // and descriptor equality still controls every eventual foreground hit.
    std::vector<std::pair<NSString*,double>> directories;
    if(PipelineRecipeIdentity::PlainDirectories(resolved_data.fileSystemRepresentation,legacy.fileSystemRepresentation,false)) {
        std::error_code error;
        std::size_t examined=0;
        for(std::filesystem::directory_iterator it(legacy.fileSystemRepresentation,error),end;
            !error && it!=end && examined<PipelineRecipeIdentity::InventoryEntries;it.increment(error),++examined) {
            const auto path=it->path();const auto component=path.filename().string();
            if(!PipelineRecipeIdentity::LegacyDirectory(component,version))continue;
            NSString* directory=@(path.c_str());if(!RecipeRootSafe(directory,false))continue;
            struct stat status{};if(::lstat(path.c_str(),&status))continue;
            directories.emplace_back(directory,double(status.st_mtimespec.tv_sec)+double(status.st_mtimespec.tv_nsec)*1e-9);
        }
    }
    std::sort(directories.begin(),directories.end(),[&](const auto& a,const auto& b) {
        const bool ac=[a.first.lastPathComponent isEqualToString:current],bc=[b.first.lastPathComponent isEqualToString:current];
        if(ac!=bc)return ac;
        return a.second!=b.second ? a.second>b.second : [a.first compare:b.first]==NSOrderedAscending;
    });
    if(directories.size()>PipelineRecipeIdentity::LegacyDirectories)directories.resize(PipelineRecipeIdentity::LegacyDirectories);
    for(const auto& directory:directories)for(const auto& file:StartupRecipes(directory.first,remaining))
        files.push_back({directory.first,file,[directory.first.lastPathComponent isEqualToString:current] ? 1u : 2u});
    // Stable/current-runtime inputs first, then recently observed older inputs.
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) {
        if(a.priority!=b.priority)return a.priority<b.priority;
        return a.file.modified!=b.file.modified ? a.file.modified>b.file.modified :
            [a.file.name compare:b.file.name]==NSOrderedAscending;
    });
    unsigned count=0,stable_count=0,legacy_count=0;std::uint64_t total=0;
    std::uint64_t read_budget=PipelinePrewarmLimits::Bytes;
    std::unordered_set<std::string> admitted;
    for(const auto& entry:files) {
        if(count>=PipelinePrewarmLimits::Recipes || !read_budget) break;
        if(entry.file.bytes>PipelinePrewarmLimits::RecipeBytes || entry.file.bytes>PipelinePrewarmLimits::Bytes-total ||
            entry.file.bytes>read_budget ||
            !RecipeRootSafe(entry.root,false))continue;
        const auto key=std::string(entry.file.name.stringByDeletingPathExtension.UTF8String);
        if(admitted.contains(key))continue;
        NSData* bytes=ReadRecipe([entry.root stringByAppendingPathComponent:entry.file.name],read_budget);
        if(!bytes || bytes.length>PipelinePrewarmLimits::RecipeBytes || bytes.length>PipelinePrewarmLimits::Bytes-total) continue;
        if(RecipeDigest(bytes.bytes,bytes.length)!=key)continue;
        id recipe=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
        if(!RecipeSchema(recipe) || RecipeKey(recipe)!=key)continue;
        admitted.insert(key);total+=bytes.length;++count;
        const bool imported=entry.priority!=0;
        imported ? ++legacy_count : ++stable_count;
        const auto queued_bytes=bytes.length;
        {std::lock_guard guard(warm_lock);++warm_stats.loaded;++warm_queued_count;warm_queued_bytes+=queued_bytes;}
        Submit([this,recipe,bytes,key,imported,queued_bytes] {
            Prewarm(recipe);
            bool write=false;
            {
                std::lock_guard guard(warm_lock);--warm_queued_count;warm_queued_bytes-=queued_bytes;
                // Persist only after successful compilation on this device.
                // Release the startup reservation before reusing that bounded
                // input slot for the existing background atomic writer.
                if(imported && warm_pipelines.contains(key) && !recipe_writes.contains(key) &&
                    recipe_writes.size()+warm_queued_count<PipelinePrewarmLimits::Recipes &&
                    queued_bytes<=PipelinePrewarmLimits::Bytes-pending_recipe_bytes-warm_queued_bytes) {
                    recipe_writes.emplace(key,PendingRecipe{bytes,++warm_sequence});pending_recipe_bytes+=queued_bytes;
                    if(!recipe_writer_queued){recipe_writer_queued=true;write=true;}
                }
            }
            if(write)Submit([this]{WritePendingRecipe();},true);
        },true);
    }
    DC_INFO("gpu prewarm recipes: namespace=%s source=%u legacy=%u bytes=%llu read_bytes=%llu",identity.c_str(),
        stable_count,legacy_count,static_cast<unsigned long long>(total),
        static_cast<unsigned long long>(PipelinePrewarmLimits::Bytes-read_budget));
}
NSDictionary* ShaderCompiler::RenderRecipe(MTLRenderPipelineDescriptor* d) {
    if(!warm_root) return nil;
    // Metal supplies an empty descriptor by default on some OS versions.
    // Only procedural vertex inputs are admitted; explicit layouts need their
    // own complete recipe schema before they can be reused.
    if(d.vertexDescriptor) for(unsigned k=0;k<31;++k)
        if(d.vertexDescriptor.attributes[k].format!=MTLVertexFormatInvalid || d.vertexDescriptor.layouts[k].stride) return nil;
    std::lock_guard guard(fn_lock);
    const auto vertex=function_sources.find(reinterpret_cast<std::uintptr_t>((__bridge void*)d.vertexFunction));
    const auto fragment=function_sources.find(reinterpret_cast<std::uintptr_t>((__bridge void*)d.fragmentFunction));
    if(vertex==function_sources.end() || (d.fragmentFunction && fragment==function_sources.end())) return nil;
    NSMutableArray* colors=[NSMutableArray new];
    for(unsigned k=0;k<8;++k) {auto c=d.colorAttachments[k];[colors addObject:@[@(c.pixelFormat),@(c.writeMask),@(c.blendingEnabled),
        @(c.sourceRGBBlendFactor),@(c.destinationRGBBlendFactor),@(c.rgbBlendOperation),
        @(c.sourceAlphaBlendFactor),@(c.destinationAlphaBlendFactor),@(c.alphaBlendOperation)]];}
    return @{ @"version":@1,@"kind":@"render",@"vertex":@(vertex->second.c_str()),
        @"fragment":fragment==function_sources.end() ? @"" : @(fragment->second.c_str()),@"colors":colors,
        @"state":@[@(d.depthAttachmentPixelFormat),@(d.stencilAttachmentPixelFormat),@(d.rasterSampleCount),
        @(d.rasterizationEnabled),@(d.alphaToCoverageEnabled),@(d.alphaToOneEnabled),@(d.inputPrimitiveTopology),
        @(d.tessellationPartitionMode),@(d.maxTessellationFactor),@(d.tessellationFactorFormat),
        @(d.tessellationFactorStepFunction),@(d.tessellationOutputWindingOrder),@(d.tessellationControlPointIndexType)]};
}
NSDictionary* ShaderCompiler::ComputeRecipe(MTLComputePipelineDescriptor* d) {
    if(!warm_root) return nil;
    std::lock_guard guard(fn_lock);const auto source=function_sources.find(reinterpret_cast<std::uintptr_t>((__bridge void*)d.computeFunction));
    return source==function_sources.end() ? nil : @{ @"version":@1,@"kind":@"compute",@"source":@(source->second.c_str()),
        @"group":@(d.maxTotalThreadsPerThreadgroup),@"multiple":@(d.threadGroupSizeIsMultipleOfThreadExecutionWidth)};
}
void ShaderCompiler::Remember(NSDictionary* recipe,WarmPipeline pipeline) {
    if(!recipe || (!pipeline.render && !pipeline.compute)) return;
    const auto key=RecipeKey(recipe);if(key.empty()) return;
    NSData* bytes=RecipeData(recipe);if(!bytes || bytes.length>PipelinePrewarmLimits::RecipeBytes) return;
    bool schedule=false;
    {
        std::lock_guard guard(warm_lock);
        // A demand compilation already queues an atomic recipe write, which
        // refreshes disk recency. Warm hits need a separate bounded touch.
        pipeline.demand_seen=true;
        StoreWarmLocked(key,pipeline,bytes.length);
        if(recipe_writes.contains(key)) return;
        while(recipe_writes.size()+warm_queued_count>=PipelinePrewarmLimits::Recipes ||
                bytes.length>PipelinePrewarmLimits::Bytes-pending_recipe_bytes-warm_queued_bytes) {
            // Queued startup recipes cannot be cancelled mid-compilation. Keep
            // the combined serialized inputs bounded until they finish.
            if(recipe_writes.empty()) return;
            auto oldest=recipe_writes.begin();
            for(auto it=recipe_writes.begin();it!=recipe_writes.end();++it)
                if(it->second.queued<oldest->second.queued) oldest=it;
            pending_recipe_bytes-=oldest->second.data.length;recipe_writes.erase(oldest);
        }
        recipe_writes.emplace(key,PendingRecipe{bytes,++warm_sequence});
        pending_recipe_bytes+=bytes.length;
        if(!recipe_writer_queued) {recipe_writer_queued=true;schedule=true;}
    }
    // Disk writes must not hold a warm-cache lookup lock or delay a first draw.
    // One outstanding writer job also bounds queue metadata when pending
    // entries are evicted during a large burst of first-use pipelines.
    if(schedule) Submit([this]{WritePendingRecipe();},true);
}
void ShaderCompiler::StoreWarmLocked(const std::string& key,WarmPipeline pipeline,u64 bytes) {
    if(auto old=warm_pipelines.find(key);old!=warm_pipelines.end()) {
        pipeline.demand_seen=pipeline.demand_seen || old->second.demand_seen;
        warm_bytes-=old->second.bytes;warm_pipelines.erase(old);
    }
    while(warm_pipelines.size()>=PipelinePrewarmLimits::Recipes ||
            bytes>PipelinePrewarmLimits::Bytes-warm_bytes) {
        auto oldest=warm_pipelines.begin();
        for(auto it=warm_pipelines.begin();it!=warm_pipelines.end();++it)
            if(it->second.used<oldest->second.used) oldest=it;
        warm_bytes-=oldest->second.bytes;warm_pipelines.erase(oldest);
    }
    pipeline.bytes=bytes;pipeline.used=++warm_sequence;
    warm_pipelines.emplace(key,pipeline);warm_bytes+=bytes;
}
void ShaderCompiler::WritePendingRecipe() {
    std::string key;
    {
        std::lock_guard guard(warm_lock);
        if(recipe_writes.empty()) {recipe_writer_queued=false;return;}
        auto oldest=recipe_writes.begin();
        for(auto it=recipe_writes.begin();it!=recipe_writes.end();++it)
            if(it->second.queued<oldest->second.queued) oldest=it;
        key=oldest->first;
    }
    WriteRecipe(key);
    {
        std::lock_guard guard(warm_lock);
        if(recipe_writes.empty()) {recipe_writer_queued=false;return;}
    }
    // Return to the queue between files, retaining demand-job priority.
    Submit([this]{WritePendingRecipe();},true);
}
void ShaderCompiler::WriteRecipe(const std::string& key) {
    std::lock_guard writer(recipe_write_lock);
    NSData* bytes=nil;
    {
        std::lock_guard guard(warm_lock);
        auto it=recipe_writes.find(key);if(it==recipe_writes.end()) return;
        bytes=it->second.data;pending_recipe_bytes-=bytes.length;recipe_writes.erase(it);
    }
    auto manager=NSFileManager.defaultManager;
    if(!RecipeRootSafe(warm_root,true))return;
    if(![manager createDirectoryAtPath:warm_root withIntermediateDirectories:YES attributes:nil error:nil]) return;
    if(!RecipeRootSafe(warm_root,false))return;
    NSString* path=[warm_root stringByAppendingPathComponent:[@(key.c_str()) stringByAppendingString:@".json"]];
    NSURL* destination=[NSURL fileURLWithPath:path];NSNumber *symlink=nil,*regular=nil;
    [destination getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:nil];
    [destination getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
    if(symlink.boolValue || ([manager fileExistsAtPath:path] && !regular.boolValue)) return;
    auto files=RecipeInventory(warm_root);
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b) {
        return a.modified!=b.modified ? a.modified<b.modified : [a.name compare:b.name]==NSOrderedAscending;
    });
    std::uint64_t total=0;std::size_t count=0;
    for(const auto& file:files) if(![file.name isEqualToString:destination.lastPathComponent]) {
        if(file.bytes>UINT64_MAX-total) return;
        total+=file.bytes;++count;
    }
    for(const auto& file:files) {
        if(count<PipelinePrewarmLimits::Recipes && total<=PipelinePrewarmLimits::Bytes-bytes.length) break;
        if([file.name isEqualToString:destination.lastPathComponent]) continue;
        // Only regular, nonsymlink, generated-key JSON entries are eligible.
        // Never follow or remove unrelated files in the cache directory.
        NSURL* candidate=[NSURL fileURLWithPath:[warm_root stringByAppendingPathComponent:file.name]];
        NSNumber *link=nil,*plain=nil;
        [candidate getResourceValue:&link forKey:NSURLIsSymbolicLinkKey error:nil];
        [candidate getResourceValue:&plain forKey:NSURLIsRegularFileKey error:nil];
        if(!link.boolValue && plain.boolValue && [manager removeItemAtURL:candidate error:nil]) {
            total-=file.bytes;--count;
        }
    }
    if(count>=PipelinePrewarmLimits::Recipes || total>PipelinePrewarmLimits::Bytes-bytes.length) return;
    [bytes writeToFile:path options:NSDataWritingAtomic error:nil];
}
bool ShaderCompiler::QueueWarmUseLocked(const std::string& key,WarmPipeline& pipeline) {
    if(pipeline.demand_seen)return false;
    pipeline.demand_seen=true;
    if(recipe_writes.contains(key) || recipe_touches.size()>=PipelinePrewarmLimits::Recipes)return false;
    recipe_touches.insert(key);
    if(recipe_touch_writer_queued)return false;
    recipe_touch_writer_queued=true;
    return true;
}
bool ShaderCompiler::TouchRecipe(const std::string& key) {
    std::lock_guard writer(recipe_write_lock);
    NSString* name=[@(key.c_str()) stringByAppendingString:@".json"];
    if(!warm_root || !RecipeFilename(name) || !RecipeRootSafe(warm_root,false))return false;
    NSString* path=[warm_root stringByAppendingPathComponent:name];
    // Update the already opened file, never a symlink or a hard-linked source
    // file substituted into the disposable cache. Do not rewrite its contents.
    const int fd=::open(path.fileSystemRepresentation,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    if(fd<0)return false;
    struct stat status{};
    bool updated=false;
    if(!::fstat(fd,&status) && S_ISREG(status.st_mode) && status.st_nlink==1) {
        struct timespec times[2]{};
        times[0].tv_nsec=UTIME_OMIT;
        times[1].tv_nsec=UTIME_NOW;
        updated=::futimens(fd,times)==0;
    }
    ::close(fd);
    return updated;
}
void ShaderCompiler::TouchPendingRecipe() {
    std::string key;
    {
        std::lock_guard guard(warm_lock);
        if(recipe_touches.empty()){recipe_touch_writer_queued=false;return;}
        key=*recipe_touches.begin();recipe_touches.erase(key);
    }
    const bool updated=TouchRecipe(key);
    {
        std::lock_guard guard(warm_lock);
        if(updated)++warm_stats.recency_updates;
        if(recipe_touches.empty()){recipe_touch_writer_queued=false;return;}
    }
    // One outstanding job; yield between files to retain demand-job priority.
    Submit([this]{TouchPendingRecipe();},true);
}
id<MTLRenderPipelineState> ShaderCompiler::RenderPipeline(MTLRenderPipelineDescriptor* d,NSError** error) {
    NSDictionary* recipe=RenderRecipe(d);const auto key=RecipeKey(recipe);
    id<MTLRenderPipelineState> hit=nil;bool touch=false;
    if(!key.empty()) {std::lock_guard guard(warm_lock);auto it=warm_pipelines.find(key);
        if(it!=warm_pipelines.end() && it->second.render){it->second.used=++warm_sequence;++warm_stats.hits;
            touch=QueueWarmUseLocked(key,it->second);hit=it->second.render;}}
    if(hit){if(touch)Submit([this]{TouchPendingRecipe();},true);if(error)*error=nil;return hit;}
    auto result=NewRenderPipeline(d,error);Remember(recipe,{result,nil});return result;
}
id<MTLComputePipelineState> ShaderCompiler::ComputePipeline(MTLComputePipelineDescriptor* d,NSError** error) {
    NSDictionary* recipe=ComputeRecipe(d);const auto key=RecipeKey(recipe);
    id<MTLComputePipelineState> hit=nil;bool touch=false;
    if(!key.empty()) {std::lock_guard guard(warm_lock);auto it=warm_pipelines.find(key);
        if(it!=warm_pipelines.end() && it->second.compute){it->second.used=++warm_sequence;++warm_stats.hits;
            touch=QueueWarmUseLocked(key,it->second);hit=it->second.compute;}}
    if(hit){if(touch)Submit([this]{TouchPendingRecipe();},true);if(error)*error=nil;return hit;}
    auto result=NewComputePipeline(d,error);Remember(recipe,{nil,result});return result;
}
void ShaderCompiler::Prewarm(NSDictionary* r) {
    @autoreleasepool {
        if(!RecipeSchema(r))return;
        WarmPipeline pipeline;
        if([r[@"kind"] isEqual:@"compute"]) {
            auto d=[MTLComputePipelineDescriptor new];d.computeFunction=Function(0,[r[@"source"] UTF8String],0);
            d.maxTotalThreadsPerThreadgroup=[r[@"group"] unsignedIntValue];
            d.threadGroupSizeIsMultipleOfThreadExecutionWidth=[r[@"multiple"] boolValue];
            if(d.computeFunction) pipeline.compute=NewComputePipeline(d,nil);
        } else if([r[@"kind"] isEqual:@"render"]) {
            NSArray* s=r[@"state"];NSArray* colors=r[@"colors"];
            if(![device supportsTextureSampleCount:[s[2] unsignedIntValue]]) return;
            auto d=[MTLRenderPipelineDescriptor new];
            for(unsigned k=0;k<8;++k) {
                NSArray* c=colors[k];
                auto a=d.colorAttachments[k];a.pixelFormat=MTLPixelFormat([c[0] unsignedIntValue]);a.writeMask=MTLColorWriteMask([c[1] unsignedIntValue]);
                a.blendingEnabled=[c[2] boolValue];a.sourceRGBBlendFactor=MTLBlendFactor([c[3] unsignedIntValue]);
                a.destinationRGBBlendFactor=MTLBlendFactor([c[4] unsignedIntValue]);a.rgbBlendOperation=MTLBlendOperation([c[5] unsignedIntValue]);
                a.sourceAlphaBlendFactor=MTLBlendFactor([c[6] unsignedIntValue]);a.destinationAlphaBlendFactor=MTLBlendFactor([c[7] unsignedIntValue]);a.alphaBlendOperation=MTLBlendOperation([c[8] unsignedIntValue]);
            }
            d.depthAttachmentPixelFormat=MTLPixelFormat([s[0] unsignedIntValue]);d.stencilAttachmentPixelFormat=MTLPixelFormat([s[1] unsignedIntValue]);
            d.rasterSampleCount=[s[2] unsignedIntValue];d.rasterizationEnabled=[s[3] boolValue];d.alphaToCoverageEnabled=[s[4] boolValue];d.alphaToOneEnabled=[s[5] boolValue];
            d.inputPrimitiveTopology=MTLPrimitiveTopologyClass([s[6] unsignedIntValue]);d.tessellationPartitionMode=MTLTessellationPartitionMode([s[7] unsignedIntValue]);
            d.maxTessellationFactor=[s[8] unsignedIntValue];d.tessellationFactorFormat=MTLTessellationFactorFormat([s[9] unsignedIntValue]);
            d.tessellationFactorStepFunction=MTLTessellationFactorStepFunction([s[10] unsignedIntValue]);d.tessellationOutputWindingOrder=MTLWinding([s[11] unsignedIntValue]);
            d.tessellationControlPointIndexType=MTLTessellationControlPointIndexType([s[12] unsignedIntValue]);
            d.vertexFunction=Function(0,[r[@"vertex"] UTF8String],0);
            if([r[@"fragment"] length]) d.fragmentFunction=Function(0,[r[@"fragment"] UTF8String],0);
            if(d.vertexFunction && (![r[@"fragment"] length] || d.fragmentFunction)) pipeline.render=NewRenderPipeline(d,nil);
        }
        if(pipeline.render || pipeline.compute) {std::lock_guard guard(warm_lock);
            const auto key=RecipeKey(r);
            StoreWarmLocked(key,pipeline,RecipeData(r).length);++warm_stats.ready;}
    }
}
