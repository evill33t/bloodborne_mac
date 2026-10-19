// SPDX-License-Identifier: GPL-2.0-or-later
// Included in MetalRenderer::Impl. Opt-in diagnostics only.
std::string capture_root;
u64 capture_bytes=0,capture_sequence=0,capture_scene_frame=~0ull;
std::set<std::string> captured_textures;
u64 capture_buffer_frame=~0ull;
std::set<std::string> captured_buffers;
unsigned fog_capture_frames=0;
bool fog_capture_started=false,fog_capture_particles=false;
u64 fog_capture_target=0;
unsigned fog_async_frames=0;
bool fog_async_started=false,fog_async_particles=false;
u64 fog_async_frame=~0ull,fog_async_depth_frame=~0ull,fog_async_target=0;
u64 fog_async_bytes=0,fog_async_sequence=0;
std::string fog_async_root;
static constexpr u64 kFogAsyncLimit=256ull<<20;
unsigned fog_constant_frames=0;
u64 fog_constant_first_frame=~0ull,fog_constant_sequence=0,fog_constant_bytes=0;
u64 fog_audit_first_frame=~0ull,fog_audit_rows=0;
std::ofstream fog_draw_audit;
void CaptureFogDrawEntry(const RegFile& r,const DrawCall& draw) {
  if(!fog_constant_frames || capture_root.empty() || (u64(r.ColorTarget(0,Reg::CB_BASE))<<8)!=0x806b240000ull) return;
  if(fog_audit_first_frame==~0ull) {
    if(!trace) return;
    fog_audit_first_frame=frame_count;
    fog_draw_audit.open(capture_root+"/fog_draw_entries.csv");
    fog_draw_audit<<"frame,draw,cpu_s,ps,vs,count,instances,first_vertex,index_base,index_offset,index_size,primitive,clip_control,color_control,target_mask,col_format";
    for(unsigned k=0;k<16;++k) fog_draw_audit<<",ps_ud"<<k;
    fog_draw_audit<<'\n';
  }
  if(frame_count-fog_audit_first_frame>=fog_constant_frames || fog_audit_rows++>=16384 || !fog_draw_audit) return;
  fog_draw_audit<<frame_count<<','<<draw_count<<','<<std::to_string(StutterTrace::Now())<<','
      <<r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS)<<','<<r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS)<<','
      <<draw.count<<','<<draw.instances<<','<<draw.first_vertex<<','<<draw.index_base<<','<<draw.index_offset<<','<<draw.index_size<<','
      <<unsigned(r.Primitive())<<','<<r[Reg::PA_CL_CLIP_CNTL]<<','<<r[Reg::CB_COLOR_CONTROL]<<','
      <<r[Reg::CB_TARGET_MASK]<<','<<r[Reg::SPI_SHADER_COL_FORMAT];
  const auto* ud=r.UserData(Reg::SPI_SHADER_USER_DATA_PS_0);
  for(unsigned k=0;k<16;++k) fog_draw_audit<<','<<ud[k];
  fog_draw_audit<<'\n';
}
void CaptureFogInputs(const Program* pixel,const u32* ud,const char* stage="fragment") {
  // Compare CPU-at-encode with same-queue GPU buffer reads. A mismatch is
  // evidence of changing input bytes, not by itself proof of an invalid writer.
  if(!pixel || !fog_constant_frames || capture_root.empty()) return;
  const bool fragment=pixel->hash==0x33a4610f6f19410dull;
  const bool local=pixel->hash==0x030976bf71b6a530ull || pixel->hash==0x53b4731b549fb1f0ull;
  const bool domain=pixel->hash==0xc4b45ba023e435cfull;
  if(!fragment && !local && !domain) return;
  if(fog_constant_first_frame==~0ull) {
    if(!trace || !fragment) return;
    fog_constant_first_frame=frame_count;
  }
  if(frame_count-fog_constant_first_frame>=fog_constant_frames || fog_constant_sequence>=4096) return;
  struct Input {u64 base,bytes,physical;id<MTLBuffer> source;std::array<u32,4> descriptor;u64 dump_base,dump_bytes,dump_generation;bool covered;const char* kind="payload";u64 descriptor_bytes=0;};
  std::vector<Input> inputs;
  u64 total=0;
  auto resources=pixel->buffers;
  if(local) for(const auto& read:pixel->buffer_reads) {
    const auto& source=read.descriptor[0];
    const u32 crossfade_slot=pixel->hash==0x030976bf71b6a530ull ? 3 : 7;
    if(read.HasReadOnlyDescriptor() && source.kind==Shader::SgprSource::PointerLoad && source.ud==2 &&
       (source.offset==16 || source.offset==crossfade_slot*16) &&
       std::find(resources.begin(),resources.end(),read.descriptor)==resources.end()) resources.push_back(read.descriptor);
  }
  for(const auto& resource:resources) {
    std::array<u32,4> descriptor{};bool valid=true;
    for(unsigned k=0;k<4;++k) {
      const auto& p=resource[k];
      if(p.kind==Shader::SgprSource::Unknown || p.ud>=16 ||
          (p.kind!=Shader::SgprSource::UserData && p.ud>=15)) {valid=false;break;}
      if(p.kind!=Shader::SgprSource::UserData) {
        const auto pointer=p.kind==Shader::SgprSource::PointerLoad
            ? (u64(ud[p.ud])|(u64(ud[p.ud+1])<<32))&0xFFFFFFFFFFFFull
            : u64(ud[p.ud])|(u64(ud[p.ud+1]&0xfff)<<32);
        if(pointer+p.offset<pointer || !Readable(pointer+p.offset,4)) {valid=false;break;}
      }
      descriptor[k]=Shader::ResolveSgpr(p,ud);
    }
    if(!valid) continue;
    const u64 base=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&kGpuAddrMask;
    const u64 declared=u64(descriptor[2])*std::max(1u,(descriptor[1]>>16)&0x3fff);
    const u64 bytes=std::min<u64>(declared,8192);
    if(!base || !bytes || (base&3) || (bytes&3) || !Readable(base,bytes)) continue;
    id<MTLBuffer> source=nil;u64 physical=0;
    if(!Locate(base,bytes,&source,&physical)) continue;
    Input input{base,bytes,physical,source,descriptor,0,0,0,false};
    input.descriptor_bytes=declared;
    auto dump=const_dumps.upper_bound(base);
    if(dump!=const_dumps.begin()) {
      --dump;
      if(base-dump->first<dump->second.bytes) {
        input.dump_base=dump->first;input.dump_bytes=dump->second.bytes;input.dump_generation=dump->second.gen;
        input.covered=bytes<=input.dump_bytes-(base-input.dump_base);
      }
    }
    total+=((bytes+3)&~3ull)*2;inputs.push_back(input);
    if(inputs.size()>=8) break;
  }
  const u64 table_address=(u64(ud[2])|(u64(ud[3])<<32))&0xFFFFFFFFFFFFull;
  id<MTLBuffer> table_source=nil;u64 table_physical=0;
  if(!(table_address&3) && Readable(table_address,64) && Locate(table_address,64,&table_source,&table_physical)) {
    inputs.push_back(Input{table_address,64,table_physical,table_source,{},0,0,0,false,"descriptor_table"});total+=128;
  }
  if(inputs.empty() || total>(64ull<<20)-fog_constant_bytes) return;
  id<MTLBuffer> buffer=[device newBufferWithLength:total options:MTLResourceStorageModeShared];
  if(!buffer) return;
  std::memset(buffer.contents,0,total);
  EndPass();EnsureCommandBuffer();auto blit=[cmd blitCommandEncoder];
  NSMutableArray* records=[NSMutableArray new];u64 offset=0;
  for(const auto& input:inputs) {
    const u64 stride=(input.bytes+3)&~3ull;
    std::memcpy(static_cast<char*>(buffer.contents)+offset,reinterpret_cast<const void*>(input.base),input.bytes);
    [blit copyFromBuffer:input.source sourceOffset:input.physical toBuffer:buffer destinationOffset:offset+stride size:input.bytes];
    NSMutableArray* words=[NSMutableArray new];for(u32 word:input.descriptor)[words addObject:@(word)];
    [records addObject:@{@"kind":@(input.kind),@"base":@(input.base),@"bytes":@(input.bytes),@"cpu_offset":@(offset),@"gpu_offset":@(offset+stride),
        @"descriptor":words,@"cpu_dump_address":@(input.dump_base),@"cpu_dump_bytes":@(input.dump_bytes),
        @"cpu_dump_generation":@(input.dump_generation),@"cpu_dump_covers_resource":@(input.covered),
        @"descriptor_bytes":@(input.descriptor_bytes ? input.descriptor_bytes : input.bytes)}];
    offset+=stride*2;
  }
  [blit endEncoding];
  const auto base=capture_root+"/fog_inputs_f"+std::to_string(frame_count)+"_draw"+std::to_string(draw_count)+"_"+std::to_string(++fog_constant_sequence);
  fog_constant_bytes+=total;
  NSMutableArray* user=[NSMutableArray new];for(unsigned k=0;k<16;++k)[user addObject:@(ud[k])];
  NSMutableArray* table=[NSMutableArray new];const u64 root=(u64(ud[2])|(u64(ud[3])<<32))&0xFFFFFFFFFFFFull;
  if(Readable(root,64)) for(unsigned k=0;k<16;++k)[table addObject:@(reinterpret_cast<const u32*>(root)[k])];
  NSDictionary* metadata=@{@"version":@1,@"kind":@"fog_input_comparison",@"frame":@(frame_count),@"draw":@(draw_count),
      @"bytes":@(total),@"shader_hash":@(pixel->hash),@"mapping_generation":@(Core::Memory::DirectMapGeneration()),@"inputs":[records copy],
      @"stage":@(stage),
      @"prepared_user_data":user,@"prepared_ud2_table":table,
      @"scope":@"CPU bytes at encode versus same-queue GPU copy before draw; captures split passes and do not wait"};
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    @autoreleasepool {
      bool saved=false;
      if(completed.status==MTLCommandBufferStatusCompleted) {
        std::ofstream file(base+".bin",std::ios::binary);file.write(static_cast<const char*>(buffer.contents),total);file.close();saved=file.good();
      }
      NSMutableDictionary* result=[metadata mutableCopy];result[@"complete"]=@(saved);result[@"gpu_status"]=@(completed.status);
      NSData* json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
      [json writeToFile:@((base+".json").c_str()) atomically:YES];
    }
  }];
}
u64 dream_material_previous_hash=0,dream_material_previous_target=0;
std::set<u64> dream_material_captured;
void CaptureDreamMaterialBoundary(const Program* pixel,RenderTarget* material) {
  // A bounded, opt-in pass boundary capture for the observed Dream glow.
  // These are raw float material values before any lighting or tone mapping.
  static const bool enabled=std::getenv("BB_METAL_CAPTURE_DREAM_MATERIAL") &&
      std::string_view(std::getenv("BB_METAL_CAPTURE_DREAM_MATERIAL"))=="1";
  if(!enabled || !trace || capture_root.empty()) return;
  const u64 hash=pixel ? pixel->hash : 0;
  if(dream_material_previous_hash && hash!=dream_material_previous_hash &&
      dream_material_captured.size()<8 && dream_material_captured.insert(dream_material_previous_hash).second) {
    if(auto* target=Latest(color_targets,dream_material_previous_target);
        target && target->initialized && !target->clear_pending)
      CaptureRaw(target->tex,"dream_material_after_"+Hex(dream_material_previous_hash)+"_draw_"+std::to_string(draw_count-1));
  }
  const bool candidate=hash==0xed27830505b11b1dull || hash==0x238239f6494224e8ull ||
      hash==0xb70e6969714f1af0ull || hash==0xcdb338726e9e90baull;
  dream_material_previous_hash=candidate && material ? hash : 0;
  dream_material_previous_target=material ? material->addr : 0;
}
void CaptureMotionConstants(const Program& program,const u32* ud) {
  if(!trace || capture_root.empty() || (program.hash!=0xba34012ce43413a9ull &&
      program.hash!=0x507b55c2109a1e25ull && program.hash!=0x474f614296148b2bull)) return;
  if(capture_buffer_frame!=frame_count) {capture_buffer_frame=frame_count;captured_buffers.clear();}
  for(const auto& source:program.buffers) {
    std::array<u32,4> words{};bool valid=true;
    for(unsigned k=0;k<4;++k) {
      if(source[k].kind==Shader::SgprSource::Unknown) {valid=false;break;}
      words[k]=Shader::ResolveSgpr(source[k],ud);
    }
    const u64 base=(u64(words[0])|(u64(words[1]&0xfff)<<32))&kGpuAddrMask;
    const u64 count=std::min<u64>(32768,u64(words[2])*std::max(1u,(words[1]>>16)&0x3fff));
    if(!valid || !count || !Readable(base,count) || captured_buffers.size()>=128) continue;
    const auto key=Hex(program.hash)+"_"+Hex(base);
    if(!captured_buffers.insert(key).second) continue;
    const auto name=capture_root+"/constants_f"+std::to_string(frame_count)+"_"+key;
    std::ofstream bytes(name+".bin",std::ios::binary);
    bytes.write(reinterpret_cast<const char*>(base),count);
    std::ofstream metadata(name+".json");
    metadata<<"{\"frame\":"<<frame_count<<",\"draw\":"<<draw_count<<",\"shader_hash\":"<<program.hash
      <<",\"base\":"<<base<<",\"bytes\":"<<count<<",\"descriptor\":[";
    for(unsigned k=0;k<4;++k) metadata<<(k?",":"")<<words[k];
    metadata<<"]}\n";
  }
}
static bool CapturePathAllowed(const std::filesystem::path& root) {
  std::error_code error;
  for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) if(const char* game=std::getenv(key);game && *game) {
    const auto source=std::filesystem::weakly_canonical(game,error);if(error) return false;
    const auto rel=root.lexically_relative(source);
    if(root==source || (!rel.empty() && *rel.begin()!="..")) return false;
  }
  return true;
}
void InitCapture() {
  const char* path=std::getenv("BB_METAL_CAPTURE_DIR");
  if(!path || !*path) return;
  std::error_code error;
  auto root=std::filesystem::weakly_canonical(path,error);if(error || !CapturePathAllowed(root)) return;
  std::filesystem::create_directories(root,error);if(error) return;
  capture_root=root.string();
  if(const char* value=std::getenv("BB_METAL_CAPTURE_FOG_CONSTANT_FRAMES")) {
    unsigned requested=0;bool valid=true;
    for(const char* p=value;*p;++p) {if(*p<'0' || *p>'9'){valid=false;break;}requested=std::min(64u,requested*10u+unsigned(*p-'0'));}
    if(valid) fog_constant_frames=requested;
  }
  if(const char* frames=std::getenv("BB_METAL_CAPTURE_FOG_FRAMES"))
    fog_capture_frames=std::clamp(std::atoi(frames),0,12);
  if(const char* frames=std::getenv("BB_METAL_CAPTURE_FOG_ASYNC_FRAMES");frames && *frames) {
    // Reject malformed/negative values; saturating each digit avoids atoi overflow.
    unsigned requested=0;bool valid=true;
    for(const char* q=frames;*q;++q) {
      if(*q<'0' || *q>'9') {valid=false;break;}
      requested=std::min(240u,requested*10u+unsigned(*q-'0'));
    }
    if(valid && requested) {
      const auto directory=std::filesystem::weakly_canonical(root/"fog_async",error);
      if(!error && CapturePathAllowed(directory)) {
        std::filesystem::create_directories(directory,error);
        if(!error) {
          fog_async_root=directory.string();fog_async_frames=requested;
          // A dedicated async run must not quietly reintroduce synchronous snapshots.
          fog_capture_frames=0;
          DC_INFO("gpu: async fog ROI capture enabled at %s (%u frames / 256 MiB lifetime limit)",
              fog_async_root.c_str(),fog_async_frames);
        }
      }
    }
  }
  DC_INFO("gpu: raw capture enabled at %s (64 textures / 512 MiB limit)",capture_root.c_str());
}
void CaptureRaw(id<MTLTexture> texture,const std::string& name) {
  if(fog_async_frames || fog_async_started || !trace || capture_root.empty() || !texture || capture_sequence>=64) return;
  const auto key=std::to_string(frame_count)+"_"+name+"_"+std::to_string(reinterpret_cast<std::uintptr_t>((__bridge void*)texture));
  if(!captured_textures.insert(key).second) return;
  RawTextureSnapshot::Descriptor descriptor;std::vector<RawTextureSnapshot::Plane> planes;std::string error;
  if(!RawTextureSnapshot::Describe((__bridge void*)texture,descriptor,planes,error) || planes.empty() ||
      planes[0].size>(64ull<<20) || capture_bytes+planes[0].size>(512ull<<20)) return;
  // Finish all producers while holding the renderer lock. No unrelated work
  // may pass this exact retail boundary before the queue's raw blit.
  Commit(true);
  std::vector<std::uint8_t> bytes;
  if(!RawTextureSnapshot::Capture((__bridge void*)device,(__bridge void*)queue,(__bridge void*)texture,0,bytes,
      std::chrono::steady_clock::now()+std::chrono::seconds(10),error)) {
    DC_WARN("gpu: raw capture %s failed: %s",name.c_str(),error.c_str());return;
  }
  capture_bytes+=bytes.size();
  const auto base=capture_root+"/frame_"+std::to_string(frame_count)+"_"+std::to_string(++capture_sequence)+"_"+name;
  std::ofstream binary(base+".bin",std::ios::binary);binary.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  const auto& p=planes[0];
  NSMutableDictionary* info=[@{@"version":@1,@"frame":@(frame_count),@"draw":@(draw_count),@"submission":@(submission_id),
      @"name":@(name.c_str()),@"format":@(descriptor.format),@"type":@(descriptor.type),
      @"width":@(p.width),@"height":@(p.height),@"depth":@(p.depth),@"level":@(p.level),@"slice":@(p.slice),
      @"aspect":@(unsigned(p.aspect)),@"texel_bytes":@(p.texel_bytes),@"row_bytes":@(p.row_bytes),
      @"image_bytes":@(p.image_bytes),@"bytes":@(bytes.size()),@"complete":@(binary.good())} mutableCopy];
  if(scene_camera) {
    NSMutableArray* view=[NSMutableArray new];NSMutableArray* inverse=[NSMutableArray new];NSMutableArray* projection=[NSMutableArray new];
    for(float f:scene_camera->view)[view addObject:@(f)];for(float f:scene_camera->inverse)[inverse addObject:@(f)];
    for(float f:scene_camera->projection)[projection addObject:@(f)];
    info[@"camera"]=@{@"view":view,@"inverse":inverse,@"projection":projection,@"frame":@(scene_camera_frame),
        @"width":@(scene_camera->width),@"height":@(scene_camera->height)};
  }
  NSData* json=[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
  [json writeToFile:@((base+".json").c_str()) atomically:YES];
  DC_INFO("gpu: raw capture %s, %llu bytes",base.c_str(),u64(bytes.size()));
}
// These fixed full-HD regions are the retained saved-view analysis coordinates.
// The blit only cuts the current pass; it neither commits nor waits for the GPU.
bool CaptureFogAsyncTarget(const RenderTarget* target,const char* phase,const Program* pixel) {
  if(!target || !target->initialized || target->clear_pending || fog_async_root.empty()) return false;
  id<MTLTexture> texture=target->tex;
  if(!texture || texture.textureType!=MTLTextureType2D || texture.width!=1920 || texture.height!=1080 ||
      texture.device!=device || texture.mipmapLevelCount!=1 || texture.sampleCount!=1 ||
      texture.storageMode==MTLStorageModeMemoryless) return false;
  const auto format=texture.pixelFormat;
  const bool depth_copy=std::strcmp(phase,"sampled_depth_copy")==0;
  const u32 texel_bytes=!depth_copy && format==MTLPixelFormatRGBA16Float ? 8 :
      depth_copy && (format==MTLPixelFormatR32Float || format==MTLPixelFormatDepth32Float ||
                     format==MTLPixelFormatDepth32Float_Stencil8) ? 4 : 0;
  if(!texel_bytes) return false;
  struct Region {const char* name;u32 x,y,width,height;u64 offset,row_bytes,bytes;};
  Region regions[]{
      {"far_city",1290,570,190,110,0,0,0},
      {"left_fog",770,410,55,120,0,0,0},
      {"arch_intrados",1280,278,220,67,0,0,0}};
  u64 total=0;
  for(auto& region:regions) {
    region.offset=total;region.row_bytes=(u64(region.width)*texel_bytes+255)&~255ull;
    region.bytes=region.row_bytes*region.height;total+=region.bytes;
  }
  if(total>device.maxBufferLength || fog_async_bytes>kFogAsyncLimit || total>kFogAsyncLimit-fog_async_bytes) {
    fog_async_frames=0;
    DC_WARN("gpu: async fog ROI capture reached its separate 256 MiB limit");return false;
  }
  id<MTLBuffer> buffer=[device newBufferWithLength:total options:MTLResourceStorageModeShared];
  if(!buffer) {
    fog_async_frames=0;
    DC_WARN("gpu: async fog ROI staging allocation failed");return false;
  }
  // Snapshot every renderer-owned value before the callback. The callback owns
  // the buffer and immutable metadata and must never retain/use an Impl pointer.
  // EndPass may itself submit profiling work, so record the expected submission
  // only after this boundary has selected the command buffer for the ROI blit.
  EndPass();EnsureCommandBuffer();
  const auto sequence=++fog_async_sequence;
  const auto base=fog_async_root+"/frame_"+std::to_string(frame_count)+"_"+std::to_string(sequence)+"_"+phase;
  NSMutableArray* roi_info=[NSMutableArray new];
  for(const auto& region:regions) [roi_info addObject:@{@"name":@(region.name),@"x":@(region.x),@"y":@(region.y),
      @"width":@(region.width),@"height":@(region.height),@"offset":@(region.offset),
      @"texel_bytes":@(texel_bytes),@"row_bytes":@(region.row_bytes),@"image_bytes":@(region.bytes),@"bytes":@(region.bytes)}];
  NSMutableDictionary* info=[@{@"version":@1,@"kind":@"fog_roi_capture",@"phase":@(phase),
      @"frame":@(frame_count),@"draw":@(draw_count),@"sequence":@(sequence),
      @"expected_submission":@(submission_id+1),@"mapping_generation":@(Core::Memory::DirectMapGeneration()),
      @"trigger_shader_hash":@(pixel ? pixel->hash : 0),@"source_address":@(target->addr),
      @"source_written":@(target->written),@"source_depth_revision":@(target->depth_revision),
      @"source_format":@(format),@"source_type":@(texture.textureType),@"source_width":@(texture.width),
      @"source_height":@(texture.height),@"source_level":@0,@"source_slice":@0,
      @"aspect":@(format==MTLPixelFormatDepth32Float || format==MTLPixelFormatDepth32Float_Stencil8 ? 1 : 0),
      @"bytes":@(total),@"regions":[roi_info copy],@"padding_zeroed":@YES,
      @"encode_steady_seconds":@(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count()),
      @"scope":@"Exact ROI bytes in same-queue blit order; async completion write; no capture wait"} mutableCopy];
  if(scene_camera) {
    NSMutableArray* view=[NSMutableArray new];NSMutableArray* inverse=[NSMutableArray new];NSMutableArray* projection=[NSMutableArray new];
    for(float f:scene_camera->view)[view addObject:@(f)];for(float f:scene_camera->inverse)[inverse addObject:@(f)];
    for(float f:scene_camera->projection)[projection addObject:@(f)];
    info[@"camera"]=@{@"view":[view copy],@"inverse":[inverse copy],@"projection":[projection copy],
        @"frame":@(scene_camera_frame),@"width":@(scene_camera->width),@"height":@(scene_camera->height)};
  }
  NSDictionary* metadata=[info copy];
  id<MTLBlitCommandEncoder> blit=[cmd blitCommandEncoder];
  if(!blit) {fog_async_frames=0;DC_WARN("gpu: async fog ROI blit creation failed");return false;}
  [blit fillBuffer:buffer range:NSMakeRange(0,total) value:0];
  const auto option=format==MTLPixelFormatDepth32Float_Stencil8 ? MTLBlitOptionDepthFromDepthStencil : MTLBlitOptionNone;
  for(const auto& region:regions)
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(region.x,region.y,0)
        sourceSize:MTLSizeMake(region.width,region.height,1) toBuffer:buffer destinationOffset:region.offset
        destinationBytesPerRow:region.row_bytes destinationBytesPerImage:region.bytes options:option];
  [blit endEncoding];
  fog_async_bytes+=total;
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    @autoreleasepool {
      const bool gpu_ok=completed.status==MTLCommandBufferStatusCompleted;
      bool written=false;
      if(gpu_ok && buffer.contents) {
        std::ofstream output(base+".bin",std::ios::binary);
        output.write(static_cast<const char*>(buffer.contents),total);output.close();written=output.good();
      }
      NSMutableDictionary* result=[metadata mutableCopy];
      result[@"complete"]=@(gpu_ok && written);result[@"gpu_status"]=@(completed.status);
      result[@"gpu_start_seconds"]=@(completed.GPUStartTime);result[@"gpu_end_seconds"]=@(completed.GPUEndTime);
      if(completed.label) result[@"command_label"]=[completed.label copy];
      if(!gpu_ok) result[@"error"]=completed.error.localizedDescription ?: @"Metal ROI transfer did not complete";
      else if(!written) result[@"error"]=@"Could not write the completed ROI buffer";
      NSData* json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
      const bool saved=json && [json writeToFile:@((base+".json").c_str()) atomically:YES];
      if(!gpu_ok || !written || !saved) DC_WARN("gpu: async fog ROI capture failed at %s",base.c_str());
    }
  }];
  return true;
}
void CaptureFogAsyncBoundary(const Program* pixel,const RenderTarget* color) {
  if(!fog_async_frames || fog_async_root.empty()) return;
  const bool first_particle=pixel && pixel->hash==0x33a4610f6f19410dull && color &&
      color->tex && color->tex.pixelFormat==MTLPixelFormatRGBA16Float &&
      color->tex.textureType==MTLTextureType2D && color->tex.width==1920 && color->tex.height==1080;
  if(!fog_async_started) {
    if(!trace || !first_particle) return;
    fog_async_started=true;
  }
  if(fog_async_particles && fog_async_frame!=frame_count) {
    fog_async_particles=false;--fog_async_frames;
    DC_WARN("gpu: async fog ROI frame %llu had no observed closing boundary",fog_async_frame);
    if(!fog_async_frames) return;
  }
  if(first_particle && !fog_async_particles && fog_async_frame!=frame_count) {
    fog_async_frame=frame_count;fog_async_target=color->addr;fog_async_particles=true;
    CaptureFogAsyncTarget(color,"before_particles",pixel);
    if(fog_async_frames && fog_async_depth_frame!=frame_count && fog_async_target==0x806b240000ull) {
      auto* depth=Latest(depth_targets,0x806a6c8000ull);
      auto* other=Latest(color_targets,0x806a6c8000ull);
      if(other && (!depth || other->written>depth->written)) depth=other;
      if(CaptureFogAsyncTarget(depth,"sampled_depth_copy",pixel)) fog_async_depth_frame=frame_count;
    }
  } else if(fog_async_particles && (!color || color->addr!=fog_async_target)) {
    CaptureFogAsyncTarget(Latest(color_targets,fog_async_target),"after_particles",pixel);
    fog_async_particles=false;
    if(fog_async_frames && !--fog_async_frames)
      DC_INFO("gpu: async fog ROI capture queued %llu buffers (%llu bytes)",fog_async_sequence,fog_async_bytes);
  }
}
// Diagnostic checkpoints for the observed saved-view HDR particle group.
// Consecutive frames separate base lighting from intermittent fog dropouts.
// The legacy synchronous path drains the GPU. Both modes alter pass boundaries
// and are diagnostics, not performance measurements.
void CaptureFogBoundary(const Program* pixel,const RenderTarget* color) {
  if(fog_async_frames || fog_async_started) {CaptureFogAsyncBoundary(pixel,color);return;}
  if(!fog_capture_frames || capture_root.empty()) return;
  const bool first_particle=pixel && pixel->hash==0x33a4610f6f19410dull && color &&
      color->tex && color->tex.pixelFormat==MTLPixelFormatRGBA16Float &&
      color->tex.textureType==MTLTextureType2D && color->tex.width==1920 && color->tex.height==1080;
  if(!fog_capture_started) {
    if(!trace || !first_particle) return;
    fog_capture_started=true;
  }
  const bool previous=trace;trace=true;
  if(first_particle && !fog_capture_particles) {
    fog_capture_target=color->addr;fog_capture_particles=true;
    CaptureRaw(color->tex,"fog_before_particles");
    if(fog_capture_target==0x806b240000ull)
      if(auto* depth=Latest(depth_targets,0x804f3f0000ull);depth && depth->initialized && !depth->clear_pending)
        CaptureRaw(depth->tex,"fog_scene_depth");
  } else if(fog_capture_particles && (!color || color->addr!=fog_capture_target)) {
    if(auto* target=Latest(color_targets,fog_capture_target);target && target->initialized && !target->clear_pending)
      CaptureRaw(target->tex,"fog_after_particles");
    fog_capture_particles=false;--fog_capture_frames;
  }
  trace=previous;
}
void CaptureSceneBoundary() {
  // Reserve the bounded capture budget for the consecutive fog checkpoints.
  if(fog_capture_started || fog_capture_frames || fog_async_started || fog_async_frames) return;
  if(!trace || capture_root.empty() || capture_scene_frame==frame_count) return;
  capture_scene_frame=frame_count;
  NativeMotionCapture::Observation motion;
  if(NativeMotionCapture::Latest(motion)) {
    const auto& words=motion.source_descriptor;
    const u64 base=((u64(words[0])|(u64(words[1]&255)<<32))<<8)&kGpuAddrMask;
    auto* target=motion.registration_matched && motion.descriptor_readable ? Latest(color_targets,base) : nullptr;
    std::ofstream metadata(capture_root+"/native_boundary_"+std::to_string(frame_count)+".json");
    metadata<<"{\"frame\":"<<frame_count<<",\"cpu_observation\":"<<motion.sequence<<",\"mode\":"<<motion.mode
      <<",\"velocity_format\":"<<motion.velocity_format<<",\"source_base\":"<<base
      <<",\"source_width\":"<<motion.source_width<<",\"source_height\":"<<motion.source_height
      <<",\"render_target_found\":"<<(target?"true":"false")<<",\"last_write\":"<<(target?target->written:0)
      <<",\"scope\":\"CPU metadata correlated at pre-tone-map GPU boundary; vector convention unvalidated\"}\n";
    if(target && target->initialized && !target->clear_pending)
      CaptureRaw(target->tex,"native_external_"+Hex(base)+"_registration_"+std::to_string(motion.sequence));
  }
  for(auto* target:{temporal_depth,temporal_color}) if(target && target->initialized && !target->clear_pending)
    CaptureRaw(target->tex,(target==temporal_depth ? "scene_depth_" : "scene_hdr_")+Hex(target->addr)+"_write_"+std::to_string(target->written));
}
