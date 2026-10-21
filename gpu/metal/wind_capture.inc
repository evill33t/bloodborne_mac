// SPDX-License-Identifier: GPL-2.0-or-later
// Include inside MetalRenderer::Impl, after renderer_capture.inc. Diagnostics
// only; no change to user data, shaders, animation time or native admission.
// Read tickets protect the additional diagnostic GPU copy sources.
// This dedicated path deliberately leaves generic capture_root empty, so the
// production texture uploader/ready-texture admission stays enabled.
std::string wind_capture_root;
u64 wind_capture_first_frame=~0ull,wind_capture_last_frame=~0ull;
unsigned wind_capture_draws=0;
struct WindCaptureDrawStamp {
  u64 frame=0,draw=0,copy_submission=0;
  const void* copy_command=nullptr;
  std::atomic<unsigned> status{0}; // 0 not encoded, 1 same command, 2 late cut
  std::atomic<u64> consumer_nanoseconds{0};
};
std::shared_ptr<WindCaptureDrawStamp> wind_capture_draw_stamp;
void MarkWindCaptureDraw() {
  const auto& stamp=wind_capture_draw_stamp;
  if(!stamp || stamp->frame!=frame_count || stamp->draw!=draw_count)return;
  stamp->consumer_nanoseconds.store(static_cast<u64>(StutterTrace::Now()*1e9),std::memory_order_relaxed);
  stamp->status.store(cmd && enc && (__bridge const void*)cmd==stamp->copy_command ? 1u : 2u,
      std::memory_order_release);
}
unsigned WindCaptureFrames() const {
  static const unsigned frames=[] {
    return WindCapture::ConfiguredFrames(std::getenv("BB_METAL_CAPTURE_WIND_FRAMES"));
  }();
  return frames;
}
void InitWindCapture() {
  const char* path=std::getenv("BB_METAL_WIND_CAPTURE_DIR");
  if(!WindCaptureFrames() || !path || !*path)return;
  std::error_code error;
  const auto root=std::filesystem::weakly_canonical(path,error);
  if(error || !CapturePathAllowed(root)) {
    DC_INFO("gpu: wind capture directory rejected");return;
  }
  std::filesystem::create_directories(root,error);
  if(error) {DC_INFO("gpu: wind capture directory creation failed");return;}
  wind_capture_root=root.string();
  DC_INFO("gpu: wind input capture enabled at %s (%u frames, %u draws/frame; production texture admission retained)",
      wind_capture_root.c_str(),WindCaptureFrames(),WindCapture::DrawsPerFrame);
}
bool WindCaptureRequested(const Program& program) const {
  return WindCaptureFrames() && !wind_capture_root.empty() && program.stage==Stage::Vertex &&
      program.hash==WindCapture::ShaderHash &&
      ((wind_capture_first_frame==~0ull && trace) ||
       (wind_capture_first_frame!=~0ull && frame_count>=wind_capture_first_frame &&
        frame_count-wind_capture_first_frame<WindCaptureFrames()));
}
void CaptureWindInputs(const Program& program,const u32* ud,
    std::span<const StaticSmrd::Snapshot> snapshots) {
  if(!WindCaptureRequested(program))return;
  if(wind_capture_first_frame==~0ull)wind_capture_first_frame=frame_count;
  if(wind_capture_last_frame!=frame_count) {wind_capture_last_frame=frame_count;wind_capture_draws=0;}
  if(wind_capture_draws++>=WindCapture::DrawsPerFrame)return;
  const auto filename=wind_capture_root+"/wind_inputs_f"+std::to_string(frame_count)+
      "_draw"+std::to_string(draw_count);
  const u64 generation=Core::Memory::DirectMapGeneration();
  const double cpu_seconds=StutterTrace::Now();
  const char* root_owner_kind="unresolved";
  NSMutableArray* user=[NSMutableArray new];for(unsigned k=0;k<16;++k)[user addObject:@(ud[k])];
  NSMutableArray* denied=[NSMutableArray new];
  const auto deny=[&](const char* kind,const char* reason,u64 address=0,u64 bytes=0) {
    [denied addObject:@{@"kind":@(kind),@"reason":@(reason),@"base":@(address),@"bytes":@(bytes)}];
  };
  const auto basic_metadata=[&]() -> NSMutableDictionary* {
    return [@{@"version":@1,@"kind":@"dream_wind_input_comparison",@"frame":@(frame_count),
        @"draw":@(draw_count),@"shader_hash":@(program.hash),@"cpu_seconds":@(cpu_seconds),
        @"mapping_generation":@(generation),@"prepared_user_data":[user copy],@"denied":[denied copy],
        @"root_owner_kind":@(root_owner_kind),
        @"native_indirect":@(native_indirect_enabled),
        @"scope":@"CPU bytes at encode versus same-queue GPU copies before draw. Opt-in copies split passes; no requested waits or admission relaxation. CPU read proof excludes tracked writers/callbacks, not uninstrumented raw CPU stores. A direct_root is observed, not CE-snapshot-owned; differing root copies leave actual leaf selection unproven. GPU copies do not establish the later draw read identical bytes."} mutableCopy];
  };
  const auto save_denied=[&]() {
    NSMutableDictionary* metadata=basic_metadata();metadata[@"complete"]=@NO;metadata[@"gpu_copy_scheduled"]=@NO;
    NSData* json=[NSJSONSerialization dataWithJSONObject:metadata options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
    [json writeToFile:@((filename+".json").c_str()) atomically:YES];
  };
  const auto& predicate=CurrentCpuReadReady();
  if(!predicate) {deny("root","missing-cp-guard");save_denied();return;}
  // Prefer the actual pinned PatchUserData owner. A source outside the arena
  // may instead be observed directly under the complete read proof below;
  // an unowned/partial/stale arena pointer must never become a direct source.
  const u64 root=(u64(ud[6])|(u64(ud[7])<<32))&0xffffffffffffull;
  const WindCapture::Range table{root&~3ull,48};
  const StaticSmrd::Window window{arena_va,kArenaSize,arena_head,arena_commit_head,arena_pin};
  const StaticSmrd::Snapshot* owner=nullptr;
  for(const auto& candidate:snapshots) {
    if(!StaticSmrd::Valid(candidate,window) || root<candidate.address ||
        root-candidate.address>=candidate.bytes || table.address<candidate.address ||
        table.address-candidate.address>candidate.bytes ||
        table.bytes>candidate.bytes-(table.address-candidate.address))continue;
    if(owner) {deny("root","ambiguous-prepared-owner",root,48);save_denied();return;}
    owner=&candidate;
  }
  if(!owner && !WindCapture::OutsideArena(table,arena_va,kArenaSize)) {
    deny("root","unowned-or-partial-arena-root",root,48);save_denied();return;
  }
  root_owner_kind=owner ? "owned_ce_snapshot" : "direct_root";
  struct Guards {
    Impl& renderer;const CpuReadReady& predicate;u64 generation;
    u32 callback_reason=UINT32_MAX;
    bool HasPredicate() const {return bool(predicate);}
    bool GenerationStable() const {
      return WindCapture::StableGeneration(generation,Core::Memory::DirectMapGeneration());
    }
    bool Mapping(WindCapture::Range r) {
      id<MTLBuffer> buffer=nil;u64 offset=0;
      return Readable(r.address,r.bytes) && renderer.LocateIndirect(r.address,r.bytes,&buffer,&offset);
    }
    bool CpuReady(WindCapture::Range r) const {return predicate(r.address,r.bytes);}
    bool CallbacksReady(WindCapture::Range r) {
      std::vector<NativeIndirect::Range> physical;
      if(!renderer.PhysicalRanges(r.address,r.bytes,physical))return false;
      // Mandatory even when native_indirect=0; CpuResourceRead's normal
      // callback gate is conditional on the native coherence mode.
      for(const auto& range:physical) {
        callback_reason=renderer.SignalSourceBlock(range);
        if(callback_reason!=UINT32_MAX)return false;
      }
      return true;
    }
    bool WritersReady(WindCapture::Range r) {return renderer.CpuResourceRead(r.address,r.bytes,predicate);}
  } guard{*this,predicate,generation};
  const auto proof=[&](const char* kind,WindCapture::Range range) {
    const auto reason=WindCapture::Ready(range,guard);
    if(reason==WindCapture::Reject::None)return true;
    deny(kind,WindCapture::Names[unsigned(reason)],range.address,range.bytes);
    DC_INFO("gpu wind capture blocked: frame=%llu draw=%llu kind=%s reason=%s callback=%u address=%#llx bytes=%llu",
        frame_count,draw_count,kind,WindCapture::Names[unsigned(reason)],guard.callback_reason,range.address,range.bytes);
    return false;
  };
  if(!proof("prepared_root",table)) {save_denied();return;}
  if(!owner && arena_va) {
    // A different VA may alias the snapshot arena's physical backing. It
    // does not acquire arena ownership by passing the outside-VA check.
    std::vector<NativeIndirect::Range> table_physical,arena_physical;
    if(!PhysicalRanges(table.address,table.bytes,table_physical) ||
        !PhysicalRanges(arena_va,kArenaSize,arena_physical) || !guard.GenerationStable()) {
      deny("root","arena-physical-alias-proof-incomplete",root,48);save_denied();return;
    }
    for(const auto& a:table_physical)for(const auto& b:arena_physical)
      if(NativeIndirect::Overlap(a,b)) {
        deny("root","unowned-arena-physical-alias",root,48);save_denied();return;
      }
  }
  std::array<u32,12> table_words{};
  const void* table_source=owner ? static_cast<const void*>(owner->data+(table.address-owner->address))
      : reinterpret_cast<const void*>(table.address);
  std::memcpy(table_words.data(),table_source,table.bytes);
  if(!guard.GenerationStable()) {deny("root","map-generation-after-read",root,48);save_denied();return;}
  struct Input {
    const char* name;WindCapture::Range range;unsigned table_offset;
    std::array<u32,4> descriptor{};
    std::vector<std::byte> cpu;
    id<MTLBuffer> source=nil;u64 physical=0;
  };
  std::vector<Input> inputs;
  Input root_input{"prepared_root",table,0};
  root_input.cpu.resize(table.bytes);
  std::memcpy(root_input.cpu.data(),table_words.data(),table.bytes);
  id<MTLBuffer> root_buffer=nil;u64 root_physical=0;
  if(!LocateIndirect(table.address,table.bytes,&root_buffer,&root_physical)) {
    deny("root","mapping-after-read",root,48);save_denied();return;
  }
  root_input.source=root_buffer;root_input.physical=root_physical;
  inputs.push_back(std::move(root_input));
  for(const auto& layout:WindCapture::Leaves) {
    Input input{layout.name,{},layout.table_offset};
    std::copy_n(table_words.data()+layout.table_offset/4,4,input.descriptor.begin());
    if(!WindCapture::Decode(input.descriptor,layout.bytes,input.range)) {
      deny(layout.name,"unexpected-descriptor-extent-or-address");continue;
    }
    if(!proof(layout.name,input.range))continue;
    id<MTLBuffer> source=nil;u64 physical=0;
    if(!LocateIndirect(input.range.address,input.range.bytes,&source,&physical)) {
      deny(layout.name,"mapping-after-proof",input.range.address,input.range.bytes);continue;
    }
    input.source=source;input.physical=physical;
    input.cpu.resize(input.range.bytes);
    std::memcpy(input.cpu.data(),reinterpret_cast<const void*>(input.range.address),input.range.bytes);
    if(!guard.GenerationStable()) {deny(layout.name,"map-generation-after-read",input.range.address,input.range.bytes);save_denied();return;}
    inputs.push_back(std::move(input));
  }
  // Recheck before enqueueing the physical GPU observations. A failure saves
  // only denial metadata, never converts the diagnostic into a wait.
  for(const auto& input:inputs)if(!proof(input.name,input.range)) {save_denied();return;}
  u64 total=0;for(const auto& input:inputs)total+=input.range.bytes*2;
  id<MTLBuffer> buffer=[device newBufferWithLength:total options:MTLResourceStorageModeShared];
  if(!buffer) {deny("capture","capture-buffer-allocation");save_denied();return;}
  EndPass();EnsureCommandBuffer();
  if(!cmd) {deny("capture","command-buffer-allocation");save_denied();return;}
  auto blit=[cmd blitCommandEncoder];
  if(!blit) {deny("capture","blit-encoder-allocation");save_denied();return;}
  // Freeze all actual copy source ranges on this command. These leases make
  // later instrumented CP alias writes/remaps wait for the capture source
  // reads, just as normal native indirect reads do. This diagnostic itself
  // never invokes a drain. Uninstrumented raw CPU stores remain observable.
  std::vector<DescriptorRead> reads;
  for(const auto& input:inputs)reads.push_back({{input.range.address,input.range.bytes},
      {input.physical+(input.source==mem[0] ? 0 : mem_split),input.range.bytes}});
  if(!guard.GenerationStable() || !ProtectDescriptorReads(reads)) {
    [blit endEncoding];deny("capture","source-read-lease-or-map-generation");save_denied();return;
  }
  auto stamp=std::make_shared<WindCaptureDrawStamp>();
  stamp->frame=frame_count;stamp->draw=draw_count;stamp->copy_submission=submission_id;
  stamp->copy_command=(__bridge const void*)cmd;wind_capture_draw_stamp=stamp;
  NSMutableArray* records=[NSMutableArray new];u64 offset=0;
  for(const auto& input:inputs) {
    const auto bytes=input.range.bytes;
    std::memcpy(static_cast<std::byte*>(buffer.contents)+offset,input.cpu.data(),bytes);
    [blit copyFromBuffer:input.source sourceOffset:input.physical toBuffer:buffer destinationOffset:offset+bytes size:bytes];
    NSMutableArray* words=[NSMutableArray new];for(auto word:input.descriptor)[words addObject:@(word)];
    [records addObject:@{@"kind":@(input.name),@"base":@(input.range.address),@"bytes":@(bytes),
        @"table_offset":@(input.table_offset),@"descriptor":[words copy],@"cpu_offset":@(offset),
        @"gpu_offset":@(offset+bytes),@"physical_buffer":@(input.source==mem[0] ? 0 : 1),
        @"physical_offset":@(input.physical)}];
    offset+=bytes*2;
  }
  [blit endEncoding];
  NSMutableDictionary* metadata=basic_metadata();metadata[@"inputs"]=[records copy];metadata[@"bytes"]=@(total);
  metadata[@"gpu_copy_scheduled"]=@YES;
  if(owner) {
    metadata[@"prepared_root_generation"]=@(owner->generation);metadata[@"prepared_root_lease"]=@(owner->lease);
    metadata[@"prepared_root_bytes"]=@(owner->bytes);
  }
  metadata[@"copy_submission"]=@(stamp->copy_submission);
  metadata[@"source_read_leases"]=@(reads.size());
  NSMutableArray* root_words=[NSMutableArray new];for(auto word:table_words)[root_words addObject:@(word)];
  metadata[@"prepared_root_words"]=[root_words copy];
  NSDictionary* retained=[metadata copy];
  [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
    @autoreleasepool {
      bool saved=false;
      if(completed.status==MTLCommandBufferStatusCompleted) {
        std::ofstream file(filename+".bin",std::ios::binary);file.write(static_cast<const char*>(buffer.contents),total);file.close();saved=file.good();
      }
      const unsigned draw_status=stamp->status.load(std::memory_order_acquire);
      NSMutableDictionary* result=[retained mutableCopy];result[@"copy_complete"]=@(saved);
      result[@"complete"]=@(saved && draw_status==1);result[@"gpu_status"]=@(completed.status);
      result[@"consumer_same_command"]=@(draw_status==1);result[@"consumer_draw_status"]=@(draw_status);
      result[@"consumer_cpu_seconds"]=@(double(stamp->consumer_nanoseconds.load(std::memory_order_relaxed))/1e9);
      if(draw_status!=1)result[@"consumer_rejection"]=draw_status==2 ? @"late-command-cut" : @"draw-not-encoded-before-copy-completion";
      NSData* json=[NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted|NSJSONWritingSortedKeys error:nil];
      [json writeToFile:@((filename+".json").c_str()) atomically:YES];
    }
  }];
}
