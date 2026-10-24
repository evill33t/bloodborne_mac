// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside MetalRenderer::Impl. Interpret only the proven full-atlas
// clear; keep the guest RMW kernel and every uncertain case on their old path.
bool shadow_htile_clear_enabled=[] {
  const char* value=std::getenv("BB_METAL_SHADOW_HTILE_CLEAR");
  return !value || std::strcmp(value,"0");
}();
u64 shadow_htile_clear_attempts=0,shadow_htile_clear_applied=0;
std::array<u64,10> shadow_htile_clear_rejected{};
bool shadow_htile_clear_audit=[] {
  const char* value=std::getenv("BB_METAL_SHADOW_HTILE_AUDIT");
  return value && !std::strcmp(value,"1");
}();
std::array<u64,11> shadow_htile_readiness_sites{};
std::array<u64,8> shadow_htile_uniform_read_reasons{},shadow_htile_metadata_read_reasons{};
std::vector<u32> shadow_htile_clear_snapshot;
id<MTLComputePipelineState> shadow_htile_decision_pipeline=nil;
id<MTLRenderPipelineState> shadow_htile_depth_pipeline=nil;
id<MTLDepthStencilState> shadow_htile_depth_state=nil;
bool shadow_htile_gpu_enabled=[] {
  const char* value=std::getenv("BB_METAL_SHADOW_HTILE_GPU");
  return value && !std::strcmp(value,"1");
}();
bool shadow_htile_rebind_enabled=[] {
  const char* value=std::getenv("BB_METAL_SHADOW_HTILE_REBIND");
  return value && !std::strcmp(value,"1");
}();
struct ShadowHtileGpuCounts {std::atomic<u64> encoded{0},cleared{0};};
std::shared_ptr<ShadowHtileGpuCounts> shadow_htile_gpu_counts=std::make_shared<ShadowHtileGpuCounts>();
struct ShadowHtileBindingProof {
  u64 generation=0;RenderTarget* target=nullptr;
  __weak id<MTLTexture> texture=nil;
  std::vector<ShadowHtileClear::MappingSpan> spans;
};
std::unordered_map<u64,ShadowHtileBindingProof> shadow_htile_binding_proofs;
u64 shadow_htile_binding_revalidated=0,shadow_htile_binding_identity_rejected=0;
u64 shadow_htile_gpu_color_owner_rejected=0;
struct PreparedShadowHtileClear {
  ShadowHtileClear::Plan plan;
  RenderTarget* target=nullptr;
  __strong id<MTLTexture> texture=nil;
  std::vector<DescriptorRead> reads;
  std::vector<DescriptorRead> binding_reads;
  bool gpu=false,leased=false,rebound=false;
  std::array<__strong id<MTLBuffer>,2> inputs{};
  std::array<u64,2> offsets{};
  __strong id<MTLBuffer> arguments=nil;
  __strong id<MTLCommandBuffer> consumer=nil;
};
std::optional<std::vector<ShadowHtileClear::MappingSpan>> ShadowHtileMappingSpans(
    const ShadowHtileClear::Surface& surface,u64 generation) {
  if(Core::Memory::DirectMapGeneration()!=generation)return {};
  UpdateMemMap();
  if(mem_map_gen!=generation)return {};
  std::vector<ShadowHtileClear::MappingSpan> spans;
  for(const auto range:{NativeIndirect::Range{surface.metadata,ShadowHtileClear::MetadataBytes},
      NativeIndirect::Range{surface.target,ShadowHtileClear::DepthBytes}}) {
    if(!NativeIndirect::Valid(range))return {};
    for(u64 at=range.address;at<range.address+range.bytes;) {
      auto mapping=std::upper_bound(mem_ranges.begin(),mem_ranges.end(),at,
          [](u64 address,const auto& candidate){return address<candidate.start;});
      if(mapping==mem_ranges.begin())return {};
      --mapping;
      if(at<mapping->start || at-mapping->start>=mapping->size)return {};
      const u64 bytes=std::min(mapping->size-(at-mapping->start),range.address+range.bytes-at);
      BbGpuLabel identity{};
      if(!runtime_gpu_label_capture(at,4,&identity) || !identity.identity)return {};
      ShadowHtileClear::AppendMappingSpan(spans,{at,bytes,mapping->phys+(at-mapping->start),identity.identity});
      at+=bytes;
    }
  }
  if(Core::Memory::DirectMapGeneration()!=generation)return {};
  return spans;
}
void ObserveShadowHtileBindingProof(const ShadowHtileClear::Surface& surface) {
  if(!shadow_htile_rebind_enabled)return;
  if(surface.width!=ShadowHtileClear::Width || surface.height!=ShadowHtileClear::Height ||
      surface.pitch!=ShadowHtileClear::Width || surface.padded_height!=ShadowHtileClear::Height)return;
  auto targets=depth_targets.find(surface.target);if(targets==depth_targets.end())return;
  RenderTarget* target=nullptr;unsigned count=0;
  for(const auto& candidate:targets->second)if(candidate->tex){target=candidate.get();++count;}
  if(count!=1 || !target || target->tex.pixelFormat!=MTLPixelFormatDepth32Float ||
      target->dim!=TexDim::Tex2D || target->levels!=1 || target->depth_or_layers!=1)return;
  const auto previous=shadow_htile_binding_proofs.find(surface.metadata);
  if(previous!=shadow_htile_binding_proofs.end() && previous->second.generation==surface.mapping_generation &&
      previous->second.target==target && previous->second.texture==target->tex)return;
  auto spans=ShadowHtileMappingSpans(surface,surface.mapping_generation);
  if(!spans){shadow_htile_binding_proofs.erase(surface.metadata);return;}
  shadow_htile_binding_proofs[surface.metadata]={surface.mapping_generation,target,target->tex,std::move(*spans)};
}
bool ShadowHtileGpuPipelines() {
  if(shadow_htile_decision_pipeline && shadow_htile_depth_pipeline && shadow_htile_depth_state)return true;
  if(!shadow_htile_gpu_enabled)return false;
  auto decision=compiler->Function(0,ShadowHtileClear::DecisionMsl,ShadowHtileClear::ShaderHash);
  auto vertex=compiler->Function(0,ShadowHtileClear::DepthMsl,ShadowHtileClear::ShaderHash);
  if(!decision || !vertex)return false;
  NSError* error=nil;
  auto compute=[MTLComputePipelineDescriptor new];compute.computeFunction=decision;
  shadow_htile_decision_pipeline=compiler->ComputePipeline(compute,&error);
  auto render=[MTLRenderPipelineDescriptor new];render.vertexFunction=vertex;
  render.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
  shadow_htile_depth_pipeline=compiler->RenderPipeline(render,&error);
  auto depth=[MTLDepthStencilDescriptor new];depth.depthCompareFunction=MTLCompareFunctionAlways;
  depth.depthWriteEnabled=YES;shadow_htile_depth_state=[device newDepthStencilStateWithDescriptor:depth];
  if(!shadow_htile_decision_pipeline || !shadow_htile_depth_pipeline || !shadow_htile_depth_state) {
    DC_ERROR("gpu: shadow HTILE conditional clear pipeline: %s",error ? error.localizedDescription.UTF8String : "?");
    return false;
  }
  return true;
}
// GPU reads may follow known queued writers; CPU reads may not. Keep every
// other admission condition, including CP/callback/mapping and unknown writers.
bool ShadowHtileGpuRead(NativeIndirect::Range range,u64 generation,
    DescriptorRead* read=nullptr,id<MTLBuffer>* input=nullptr,u64* input_offset=nullptr) {
  const auto& ready=CurrentCpuReadReady();
  if(!ready || !ready(range.address,range.bytes) || Core::Memory::DirectMapGeneration()!=generation)return false;
  id<MTLBuffer> buffer=nil;u64 offset=0;
  if(!LocateIndirect(range.address,range.bytes,&buffer,&offset))return false;
  const NativeIndirect::Range physical{offset+(buffer==mem[0] ? 0 : mem_split),range.bytes};
  if(SignalSourceBlock(physical)!=UINT32_MAX)return false;
  PruneGuestWriters();
  for(const auto& writer:guest_writers)if(writer.unknown &&
      !writer_prune.RetainedCompleted(writer.completed,GuestWriterPruneEpoch::Use::GpuUnknown))return false;
  if(Core::Memory::DirectMapGeneration()!=generation)return false;
  if(read)*read={range,physical};if(input)*input=buffer;if(input_offset)*input_offset=offset;
  return true;
}
bool ShadowHtileGpuColorOwnerSafe(const RenderTarget& depth,u64 generation) {
  std::vector<NativeIndirect::Range> physical_depth;
  bool located=false;
  for(const auto& [address,targets]:color_targets)for(const auto& color:targets) {
    if(!color->tex || (!color->initialized && !color->clear_pending) || color->written<depth.written)continue;
    // A conditional zero draw must not promote the old depth allocation over
    // a newer colour owner. Exact-base colour ties already win GetTexture.
    if(NativeIndirect::Overlap({address,4},{depth.addr,ShadowHtileClear::DepthBytes}))return false;
    if(!located) {
      if(!PhysicalRanges(depth.addr,ShadowHtileClear::DepthBytes,physical_depth))return false;
      located=true;
    }
    std::vector<NativeIndirect::Range> physical_color;
    if(!PhysicalRanges(address,4,physical_color))return false;
    for(const auto& a:physical_color)for(const auto& b:physical_depth)if(NativeIndirect::Overlap(a,b))return false;
  }
  return Core::Memory::DirectMapGeneration()==generation;
}
std::optional<PreparedShadowHtileClear> PrepareShadowHtileClear(const Program& program,
    const u32* user,u32 gx,u32 gy,u32 gz,u32 tx,u32 ty,u32 tz,bool indirect) {
  if(!shadow_htile_clear_enabled || program.hash!=ShadowHtileClear::ShaderHash)return {};
  ++shadow_htile_clear_attempts;
  const auto reject=[&](ShadowHtileClear::Reject reason) -> std::optional<PreparedShadowHtileClear> {
    ++shadow_htile_clear_rejected[unsigned(reason)];return {};
  };
  const auto unready=[&](unsigned site) {
    if(shadow_htile_clear_audit)++shadow_htile_readiness_sites[site];
    return reject(ShadowHtileClear::Reject::Readiness);
  };
  // CpuResourceRead's complete callback/writer proof requires these modes.
  if(!native_indirect_enabled || !native_texture_coherence || !gpu_written_texture_snapshot)
    return unready(0);
  if(!ShadowHtileClear::ProgramEligible(program))return reject(ShadowHtileClear::Reject::Program);
  const std::span<const u32,16> prepared(user,16);
  const auto metadata=ShadowHtileClear::Base(prepared.subspan<4,4>());
  const auto binding=shadow_htile_clear_surfaces.find(metadata);
  const auto owner=metas.find(metadata);
  if(binding==shadow_htile_clear_surfaces.end() || owner==metas.end() || !owner->second.depth ||
      owner->second.target!=binding->second.target)return reject(ShadowHtileClear::Reject::Surface);
  auto surface=binding->second;
  auto targets=depth_targets.find(surface.target);
  if(targets==depth_targets.end())return reject(ShadowHtileClear::Reject::Surface);
  RenderTarget* target=nullptr;
  for(const auto& candidate:targets->second)if(candidate->tex) {
    ++surface.matching_targets;target=candidate.get();
  }
  if(!target || surface.matching_targets!=1)return reject(ShadowHtileClear::Reject::Surface);
  surface.initialized=target->initialized;
  surface.depth32=target->tex.pixelFormat==MTLPixelFormatDepth32Float && target->dim==TexDim::Tex2D;
  surface.levels=target->levels;surface.layers=target->depth_or_layers;
  if(target->width!=surface.width || target->height!=surface.height ||
      std::bit_cast<u32>(float(target->clear_depth))!=surface.depth_clear_word)
    return reject(ShadowHtileClear::Reject::Surface);
  // Validate shape/descriptors before inspecting any pointed-to bytes.
  auto shape=ShadowHtileClear::RecognizeShape(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,surface);
  if(!shape.plan)return reject(shape.reason);
  const auto ready=CurrentCpuReadReady();
  if(!ready)return unready(1);
  const auto uniform_address=ShadowHtileClear::Base(prepared.subspan<8,4>());
  const auto generation=Core::Memory::DirectMapGeneration();
  std::vector<DescriptorRead> input_reads;
  for(const auto range:{NativeIndirect::Range{uniform_address,8},
      NativeIndirect::Range{metadata,ShadowHtileClear::MetadataBytes}}) {
    id<MTLBuffer> buffer=nil;u64 offset=0;
    if(!LocateIndirect(range.address,range.bytes,&buffer,&offset))return unready(6);
    input_reads.push_back({range,{offset+(buffer==mem[0] ? 0 : mem_split),range.bytes}});
  }
  if(Core::Memory::DirectMapGeneration()!=generation)return unready(8);
  if(NativeIndirect::Overlap(input_reads[0].physical,input_reads[1].physical))
    return reject(ShadowHtileClear::Reject::InputAlias);
  std::vector<DescriptorRead> binding_reads;
  if(generation!=surface.mapping_generation) {
    if(!shadow_htile_rebind_enabled)return unready(2);
    const auto proof=shadow_htile_binding_proofs.find(metadata);
    const auto current=ShadowHtileMappingSpans(surface,generation);
    if(proof==shadow_htile_binding_proofs.end() || proof->second.generation!=surface.mapping_generation ||
        proof->second.target!=target || proof->second.texture!=target->tex || !current ||
        *current!=proof->second.spans) {
      ++shadow_htile_binding_identity_rejected;return unready(2);
    }
    // This is a complete current association proof, including VMA lifetime
    // identities. Unmap/reuse back to the same VA/physical bytes still rejects.
    // Lease both complete backing ranges on the actual consuming command.
    for(const auto& span:*current)binding_reads.push_back({{span.address,span.bytes},{span.physical,span.bytes}});
    surface.mapping_generation=generation;
    shape=ShadowHtileClear::RecognizeShape(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,surface);
    if(!shape.plan || Core::Memory::DirectMapGeneration()!=generation)return unready(2);
  }
  const auto gpu_fallback=[&]() -> std::optional<PreparedShadowHtileClear> {
    if(!shadow_htile_gpu_enabled)return {};
    if(!ShadowHtileGpuColorOwnerSafe(*target,generation)){++shadow_htile_gpu_color_owner_rejected;return {};}
    if(!ShadowHtileGpuPipelines())return {};
    PreparedShadowHtileClear result{*shape.plan,target,target->tex,{},binding_reads};result.gpu=true;
    result.rebound=!binding_reads.empty();
    const std::array<NativeIndirect::Range,2> ranges{{{uniform_address,8},{metadata,ShadowHtileClear::MetadataBytes}}};
    for(unsigned n=0;n<ranges.size();++n) {
      DescriptorRead read;id<MTLBuffer> input=nil;u64 offset=0;
      if(!ShadowHtileGpuRead(ranges[n],generation,&read,&input,&offset))return {};
      result.reads.push_back(read);result.inputs[n]=input;result.offsets[n]=offset;
    }
    const auto decision=ShadowHtileClear::RecognizeGpu(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,
        surface,{uniform_address,8,generation,Core::Memory::DirectMapGeneration(),true},
        {metadata,ShadowHtileClear::MetadataBytes,generation,Core::Memory::DirectMapGeneration(),true});
    if(!decision.plan)return {};
    const std::array<u32,4> arguments{3,1,0,0};
    result.arguments=[device newBufferWithBytes:arguments.data() length:sizeof(arguments)
        options:MTLResourceStorageModeShared];
    if(!result.arguments)return {};
    return result;
  };
  u32 read_reason=0;
  if(!CpuResourceRead(uniform_address,8,ready,&read_reason)) {
    if(read_reason==6)if(auto result=gpu_fallback())return result;
    if(shadow_htile_clear_audit && read_reason<8)++shadow_htile_uniform_read_reasons[read_reason];
    return unready(3);
  }
  std::array<u32,2> uniform;
  std::memcpy(uniform.data(),reinterpret_cast<const void*>(uniform_address),sizeof(uniform));
  const ShadowHtileClear::CpuReadProof uniform_proof{uniform_address,8,generation,
      Core::Memory::DirectMapGeneration(),true};
  if(uniform!=std::array<u32,2>{ShadowHtileClear::Words,ShadowHtileClear::ClearWord})
    return reject(ShadowHtileClear::Reject::Uniform);
  if(!CpuResourceRead(metadata,ShadowHtileClear::MetadataBytes,ready,&read_reason)) {
    if(read_reason==6)if(auto result=gpu_fallback())return result;
    if(shadow_htile_clear_audit && read_reason<8)++shadow_htile_metadata_read_reasons[read_reason];
    return unready(4);
  }
  try {shadow_htile_clear_snapshot.resize(ShadowHtileClear::Words);}
  catch(const std::bad_alloc&) {return unready(5);}
  std::memcpy(shadow_htile_clear_snapshot.data(),reinterpret_cast<const void*>(metadata),
      ShadowHtileClear::MetadataBytes);
  const ShadowHtileClear::CpuReadProof metadata_proof{metadata,ShadowHtileClear::MetadataBytes,
      generation,Core::Memory::DirectMapGeneration(),true};
  const auto decision=ShadowHtileClear::Recognize(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,
      surface,uniform_proof,uniform,metadata_proof,shadow_htile_clear_snapshot);
  if(!decision.plan) {
    if(decision.reason==ShadowHtileClear::Reject::Readiness)return unready(7);
    return reject(decision.reason);
  }
  PreparedShadowHtileClear result{*decision.plan,target,target->tex,{},binding_reads};
  result.rebound=!binding_reads.empty();
  result.reads=std::move(input_reads);
  if(Core::Memory::DirectMapGeneration()!=generation)return unready(8);
  return result;
}
void FinalizeShadowHtileBinding(const PreparedShadowHtileClear& prepared) {
  if(!prepared.rebound)return;
  // Publish the freshly proven association only after its complete backing
  // spans have been leased on the actual command which will consume it.
  shadow_htile_clear_surfaces.at(prepared.plan.metadata).mapping_generation=prepared.plan.generation;
  shadow_htile_binding_proofs.at(prepared.plan.metadata).generation=prepared.plan.generation;
  ++shadow_htile_binding_revalidated;
}
void LeaseShadowHtileClear(std::optional<PreparedShadowHtileClear>& prepared) {
  if(!prepared)return;
  if(prepared->gpu) {
    // Already leased before the GPU decision. No intervening commit/cut may
    // move the original RMW onto a different consumer command.
    if(prepared->leased && prepared->consumer==cmd &&
        Core::Memory::DirectMapGeneration()==prepared->plan.generation)return;
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];prepared.reset();
    if(shadow_htile_clear_audit)++shadow_htile_readiness_sites[9];
    return;
  }
  // Attach both complete inputs to the ACTUAL consumer command, after all
  // setup cuts. CP writes through physical aliases and mapping changes must
  // wait for that command. Capacity failure preserves the original dispatch.
  const auto ready=CurrentCpuReadReady();
  bool current=bool(ready);
  for(const auto& read:prepared->reads)
    current=current && CpuResourceRead(read.range.address,read.range.bytes,ready);
  auto reads=prepared->reads;reads.insert(reads.end(),prepared->binding_reads.begin(),prepared->binding_reads.end());
  if(!cmd || !current || Core::Memory::DirectMapGeneration()!=prepared->plan.generation ||
      !ProtectDescriptorReads(reads)) {
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];prepared.reset();
    if(shadow_htile_clear_audit)++shadow_htile_readiness_sites[9];
  } else FinalizeShadowHtileBinding(*prepared);
}
// Runs BEFORE the original RMW, so its low-nibble preservation test observes
// exactly the inputs consumed by that kernel. Input leases cover this decision,
// the original dispatch and the conditional depth draw on the same command.
void EncodeShadowHtileDecision(std::optional<PreparedShadowHtileClear>& prepared) {
  if(!prepared || !prepared->gpu)return;
  bool current=cmd && Core::Memory::DirectMapGeneration()==prepared->plan.generation;
  for(unsigned n=0;n<prepared->reads.size();++n) {
    DescriptorRead read;id<MTLBuffer> input=nil;u64 offset=0;
    current=current && ShadowHtileGpuRead(prepared->reads[n].range,prepared->plan.generation,&read,&input,&offset) &&
        input==prepared->inputs[n] && offset==prepared->offsets[n] &&
        read.physical.address==prepared->reads[n].physical.address && read.physical.bytes==prepared->reads[n].physical.bytes;
  }
  auto reads=prepared->reads;reads.insert(reads.end(),prepared->binding_reads.begin(),prepared->binding_reads.end());
  if(!current || !ProtectDescriptorReads(reads)) {
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];prepared.reset();
    if(shadow_htile_clear_audit)++shadow_htile_readiness_sites[9];return;
  }
  FinalizeShadowHtileBinding(*prepared);
  auto encoder=[cmd computeCommandEncoder];
  if(!encoder){execution->Fail("Metal could not encode a shadow HTILE decision.");prepared.reset();return;}
  [encoder setComputePipelineState:shadow_htile_decision_pipeline];
  for(unsigned n=0;n<2;++n)[encoder setBuffer:prepared->inputs[n] offset:prepared->offsets[n] atIndex:n];
  [encoder setBuffer:prepared->arguments offset:0 atIndex:2];
  [encoder dispatchThreads:MTLSizeMake(ShadowHtileClear::Words,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)];
  [encoder endEncoding];prepared->leased=true;prepared->consumer=cmd;
}
void ApplyShadowHtileClear(const std::optional<PreparedShadowHtileClear>& prepared) {
  if(!prepared)return;
  const auto& clear=*prepared;
  const auto binding=shadow_htile_clear_surfaces.find(clear.plan.metadata);
  if(Core::Memory::DirectMapGeneration()!=clear.plan.generation ||
      binding==shadow_htile_clear_surfaces.end() || binding->second.target!=clear.plan.target ||
      binding->second.mapping_generation!=clear.plan.generation || clear.target->tex!=clear.texture) {
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];
    if(shadow_htile_clear_audit)++shadow_htile_readiness_sites[10];
    return;
  }
  if(clear.gpu) {
    if(!clear.leased || clear.consumer!=cmd)return;
    if(!ShadowHtileGpuColorOwnerSafe(*clear.target,clear.plan.generation)){++shadow_htile_gpu_color_owner_rejected;return;}
    auto pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.depthAttachment.texture=clear.texture;
    pass.depthAttachment.loadAction=clear.target->clear_pending ? MTLLoadActionClear : MTLLoadActionLoad;
    pass.depthAttachment.clearDepth=clear.target->clear_depth;pass.depthAttachment.storeAction=MTLStoreActionStore;
    auto encoder=[cmd renderCommandEncoderWithDescriptor:pass];
    if(!encoder){execution->Fail("Metal could not encode a conditional shadow depth clear.");return;}
    [encoder setRenderPipelineState:shadow_htile_depth_pipeline];[encoder setDepthStencilState:shadow_htile_depth_state];
    [encoder setCullMode:MTLCullModeNone];
    [encoder setViewport:MTLViewport{0,0,ShadowHtileClear::Width,ShadowHtileClear::Height,0,1}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle indirectBuffer:clear.arguments indirectBufferOffset:0];
    [encoder endEncoding];
    clear.target->clear_pending=false;clear.target->velocity.safe=false;++clear.target->depth_revision;
    clear.target->written=++write_seq;
    const auto counts=shadow_htile_gpu_counts;const auto arguments=clear.arguments;
    ++counts->encoded;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if(completed.status==MTLCommandBufferStatusCompleted && *static_cast<const u32*>(arguments.contents)==3)
        ++counts->cleared;
    }];
    return;
  }
  // The original compute has been encoded. BeginPass will emit its native
  // equivalent in queue order, advancing the depth revision only then.
  clear.target->clear_depth=std::bit_cast<float>(clear.plan.depth_clear_word);
  clear.target->clear_pending=true;clear.target->velocity.safe=false;
  ++shadow_htile_clear_applied;
  if(trace)DC_INFO("gpu: shadow HTILE full clear depth %#llx (meta %#llx), original RMW retained",
      clear.plan.target,clear.plan.metadata);
}
void ReportShadowHtileAudit() const {
  const auto input_aliases=shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::InputAlias)];
  if(input_aliases || shadow_htile_gpu_color_owner_rejected)
    DC_INFO("gpu shadow HTILE alias: input-overlap=%llu conditional-color-owner=%llu (cumulative)",
        input_aliases,shadow_htile_gpu_color_owner_rejected);
  if(!shadow_htile_clear_audit)return;
  const auto values=[](const auto& counters) {
    std::string result;
    for(const auto value:counters) {if(!result.empty())result+=',';result+=std::to_string(value);}
    return result;
  };
  DC_INFO("gpu shadow HTILE audit: sites=[%s] uniform-read=[%s] metadata-read=[%s] (cumulative)",
      values(shadow_htile_readiness_sites).c_str(),values(shadow_htile_uniform_read_reasons).c_str(),
      values(shadow_htile_metadata_read_reasons).c_str());
  DC_INFO("gpu shadow HTILE conditional: encoded=%llu completed-clears=%llu (cumulative)",
      shadow_htile_gpu_counts->encoded.load(),shadow_htile_gpu_counts->cleared.load());
  DC_INFO("gpu shadow HTILE binding: revalidated=%llu identity-rejected=%llu (cumulative)",
      shadow_htile_binding_revalidated,shadow_htile_binding_identity_rejected);
}
