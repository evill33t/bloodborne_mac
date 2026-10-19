// SPDX-License-Identifier: GPL-2.0-or-later
// Included in MetalRenderer::Impl. Opt-in, intrusive diagnostics only.
std::string capture_root;
u64 capture_bytes=0,capture_sequence=0,capture_scene_frame=~0ull;
std::set<std::string> captured_textures;
u64 capture_buffer_frame=~0ull;
std::set<std::string> captured_buffers;
unsigned fog_capture_frames=0;
bool fog_capture_started=false,fog_capture_particles=false;
u64 fog_capture_target=0;
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
void InitCapture() {
  const char* path=std::getenv("BB_METAL_CAPTURE_DIR");
  if(!path || !*path) return;
  std::error_code error;
  auto root=std::filesystem::weakly_canonical(path,error);if(error) return;
  for(const char* key:{"BB_SOURCE_GAME","BB_GAME_DIR"}) if(const char* game=std::getenv(key);game && *game) {
    const auto source=std::filesystem::weakly_canonical(game,error);if(error) return;
    const auto rel=root.lexically_relative(source);
    if(root==source || (!rel.empty() && *rel.begin()!="..")) return;
  }
  std::filesystem::create_directories(root,error);if(error) return;
  capture_root=root.string();
  if(const char* frames=std::getenv("BB_METAL_CAPTURE_FOG_FRAMES"))
    fog_capture_frames=std::clamp(std::atoi(frames),0,12);
  DC_INFO("gpu: raw capture enabled at %s (64 textures / 512 MiB limit)",capture_root.c_str());
}
void CaptureRaw(id<MTLTexture> texture,const std::string& name) {
  if(!trace || capture_root.empty() || !texture || capture_sequence>=64) return;
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
// Diagnostic checkpoints for the observed saved-view HDR particle group.
// Consecutive frames separate base lighting from intermittent fog dropouts.
// This deliberately drains the GPU; never enable it in a performance run.
void CaptureFogBoundary(const Program* pixel,const RenderTarget* color) {
  if(!fog_capture_frames || capture_root.empty()) return;
  const bool first_particle=pixel && pixel->hash==0x33a4610f6f19410dull && color &&
      color->addr==0x806b240000ull && color->tex.pixelFormat==MTLPixelFormatRGBA16Float;
  if(!fog_capture_started) {
    if(!trace || !first_particle) return;
    fog_capture_started=true;
  }
  const bool previous=trace;trace=true;
  if(first_particle && !fog_capture_particles) {
    fog_capture_target=color->addr;fog_capture_particles=true;
    CaptureRaw(color->tex,"fog_before_particles");
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
  if(fog_capture_started || fog_capture_frames) return;
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
