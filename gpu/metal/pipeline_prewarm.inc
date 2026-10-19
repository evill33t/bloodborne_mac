// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Gpu::Metal in mtl_compiler.mm. Recipes are disposable local
// compiler inputs. A current descriptor AND exact source must match for a hit.
namespace {
NSData* RecipeData(NSDictionary* recipe) {
    return recipe ? [NSJSONSerialization dataWithJSONObject:recipe options:NSJSONWritingSortedKeys error:nil] : nil;
}
std::string RecipeKey(NSDictionary* recipe) {
    NSData* data=RecipeData(recipe);if(!data) return {};
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];CC_SHA256(data.bytes,static_cast<CC_LONG>(data.length),digest);
    std::string key;for(unsigned char b:digest){key+="0123456789abcdef"[b>>4];key+="0123456789abcdef"[b&15];}return key;
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
}
ShaderCompiler::WarmupStats ShaderCompiler::GetWarmupStats() {std::lock_guard guard(warm_lock);return warm_stats;}
void ShaderCompiler::EnablePrewarming() {
    if(warm_root) return;
    const char* enabled=std::getenv("BB_METAL_SHADER_PREWARM");const char* data=std::getenv("BB_DATA_DIR");
    if(archive || !data || !*data || (enabled && !std::strcmp(enabled,"0"))) return;
    NSString* root=[NSURL fileURLWithPath:[@(data) stringByAppendingPathComponent:@"metal-cache/prewarm-v1"]].URLByResolvingSymlinksInPath.path;
    for(const char* variable:{"BB_GAME_DIR","BB_SOURCE_GAME"}) if(const char* game=std::getenv(variable);game && *game) {
        NSString* source=[NSURL fileURLWithPath:@(game)].URLByResolvingSymlinksInPath.path;
        if([root isEqualToString:source] || [root hasPrefix:[source stringByAppendingString:@"/"]]) return;
    }
    // Separate devices/OS versions; exact source and descriptors guard code changes.
    const auto os=NSProcessInfo.processInfo.operatingSystemVersion;
    root=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"%llx-%ld.%ld.%ld",device.registryID,
        os.majorVersion,os.minorVersion,os.patchVersion]];
    warm_root=root;
    NSArray<NSString*>* files=[[NSFileManager.defaultManager contentsOfDirectoryAtPath:root error:nil]
        sortedArrayUsingSelector:@selector(compare:)];
    unsigned count=0;std::uint64_t total=0;
    for(NSString* file in files) {
        if(![file.pathExtension isEqualToString:@"json"] || count>=64) continue;
        NSURL* url=[NSURL fileURLWithPath:[root stringByAppendingPathComponent:file]];
        NSNumber* size=nil;NSNumber* regular=nil;NSNumber* symlink=nil;
        [url getResourceValue:&size forKey:NSURLFileSizeKey error:nil];
        [url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil];
        [url getResourceValue:&symlink forKey:NSURLIsSymbolicLinkKey error:nil];
        if(!regular.boolValue || symlink.boolValue || size.unsignedLongLongValue>1024*1024 || total+size.unsignedLongLongValue>32ull*1024*1024) continue;
        NSData* bytes=[NSData dataWithContentsOfURL:url options:0 error:nil];
        if(!bytes || bytes.length>1024*1024) continue;
        id recipe=[NSJSONSerialization JSONObjectWithData:bytes options:0 error:nil];
        if(![recipe isKindOfClass:NSDictionary.class] || ![file.stringByDeletingPathExtension isEqualToString:@(RecipeKey(recipe).c_str())]) continue;
        total+=bytes.length;++count;
        {std::lock_guard guard(warm_lock);++warm_stats.loaded;}
        Submit([this,recipe] {Prewarm(recipe);},true);
    }
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
    NSData* bytes=RecipeData(recipe);if(!bytes || bytes.length>1024*1024) return;
    {
        std::lock_guard guard(warm_lock);
        if(warm_pipelines.size()<64 || warm_pipelines.contains(key)) warm_pipelines[key]=pipeline;
        if(recipe_writes.size()>=64 || !recipe_writes.emplace(key,bytes).second) return;
    }
    // Disk writes must not hold a warm-cache lookup lock or delay a first draw.
    Submit([this,key]{WriteRecipe(key);},true);
}
void ShaderCompiler::WriteRecipe(const std::string& key) {
    std::lock_guard writer(recipe_write_lock);
    NSData* bytes=nil;
    {
        std::lock_guard guard(warm_lock);
        auto it=recipe_writes.find(key);if(it==recipe_writes.end()) return;
        bytes=it->second;recipe_writes.erase(it);
    }
    auto manager=NSFileManager.defaultManager;
    if(![manager createDirectoryAtPath:warm_root withIntermediateDirectories:YES attributes:nil error:nil]) return;
    NSString* path=[warm_root stringByAppendingPathComponent:[@(key.c_str()) stringByAppendingString:@".json"]];
    if(![manager fileExistsAtPath:path] && [manager contentsOfDirectoryAtPath:warm_root error:nil].count>=64) return;
    [bytes writeToFile:path options:NSDataWritingAtomic error:nil];
}
id<MTLRenderPipelineState> ShaderCompiler::RenderPipeline(MTLRenderPipelineDescriptor* d,NSError** error) {
    NSDictionary* recipe=RenderRecipe(d);const auto key=RecipeKey(recipe);
    if(!key.empty()) {std::lock_guard guard(warm_lock);auto it=warm_pipelines.find(key);
        if(it!=warm_pipelines.end() && it->second.render){++warm_stats.hits;if(error)*error=nil;return it->second.render;}}
    auto result=NewRenderPipeline(d,error);Remember(recipe,{result,nil});return result;
}
id<MTLComputePipelineState> ShaderCompiler::ComputePipeline(MTLComputePipelineDescriptor* d,NSError** error) {
    NSDictionary* recipe=ComputeRecipe(d);const auto key=RecipeKey(recipe);
    if(!key.empty()) {std::lock_guard guard(warm_lock);auto it=warm_pipelines.find(key);
        if(it!=warm_pipelines.end() && it->second.compute){++warm_stats.hits;if(error)*error=nil;return it->second.compute;}}
    auto result=NewComputePipeline(d,error);Remember(recipe,{nil,result});return result;
}
void ShaderCompiler::Prewarm(NSDictionary* r) {
    @autoreleasepool {
        if(![r[@"version"] isEqual:@1]) return;
        WarmPipeline pipeline;
        if([r[@"kind"] isEqual:@"compute"]) {
            if(![r[@"source"] isKindOfClass:NSString.class] || [r[@"source"] length]>512*1024 ||
                !RecipeNumber(r[@"group"],1024) || !RecipeNumber(r[@"multiple"],1)) return;
            auto d=[MTLComputePipelineDescriptor new];d.computeFunction=Function(0,[r[@"source"] UTF8String],0);
            d.maxTotalThreadsPerThreadgroup=[r[@"group"] unsignedIntValue];
            d.threadGroupSizeIsMultipleOfThreadExecutionWidth=[r[@"multiple"] boolValue];
            if(d.computeFunction) pipeline.compute=NewComputePipeline(d,nil);
        } else if([r[@"kind"] isEqual:@"render"]) {
            NSArray* s=r[@"state"];NSArray* colors=r[@"colors"];
            if(![s isKindOfClass:NSArray.class] || s.count!=13 || ![colors isKindOfClass:NSArray.class] || colors.count!=8 ||
                ![r[@"vertex"] isKindOfClass:NSString.class] || ![r[@"fragment"] isKindOfClass:NSString.class] ||
                [r[@"vertex"] length]>512*1024 || [r[@"fragment"] length]>512*1024 ||
                !RecipeFormat(s[0]) || !RecipeFormat(s[1])) return;
            for(unsigned i=2;i<13;++i) if(!RecipeNumber(s[i],i==8 ? 64 : i==2 ? 8 : i==12 ? 2 : i==6 || i==7 || i==10 ? 3 : 1)) return;
            if(![device supportsTextureSampleCount:[s[2] unsignedIntValue]]) return;
            auto d=[MTLRenderPipelineDescriptor new];
            for(unsigned k=0;k<8;++k) {
                NSArray* c=colors[k];if(![c isKindOfClass:NSArray.class] || c.count!=9 || !RecipeFormat(c[0])) return;
                for(unsigned i=1;i<9;++i) if(!RecipeNumber(c[i],i==1 ? 15 : i==2 ? 1 : i==5 || i==8 ? 4 : 18)) return;
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
            if(warm_pipelines.size()<64 || warm_pipelines.contains(key)) {warm_pipelines[key]=pipeline;++warm_stats.ready;}}
    }
}
