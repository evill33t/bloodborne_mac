// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside MetalRenderer::Impl. Interpret only the proven full-atlas
// clear; keep the guest RMW kernel and every uncertain case on their old path.
bool shadow_htile_clear_enabled=[] {
  const char* value=std::getenv("BB_METAL_SHADOW_HTILE_CLEAR");
  return !value || std::strcmp(value,"0");
}();
u64 shadow_htile_clear_attempts=0,shadow_htile_clear_applied=0;
std::array<u64,10> shadow_htile_clear_rejected{};
std::vector<u32> shadow_htile_clear_snapshot;
struct PreparedShadowHtileClear {
  ShadowHtileClear::Plan plan;
  RenderTarget* target=nullptr;
  __strong id<MTLTexture> texture=nil;
  std::vector<DescriptorRead> reads;
};
std::optional<PreparedShadowHtileClear> PrepareShadowHtileClear(const Program& program,
    const u32* user,u32 gx,u32 gy,u32 gz,u32 tx,u32 ty,u32 tz,bool indirect) {
  if(!shadow_htile_clear_enabled || program.hash!=ShadowHtileClear::ShaderHash)return {};
  ++shadow_htile_clear_attempts;
  const auto reject=[&](ShadowHtileClear::Reject reason) -> std::optional<PreparedShadowHtileClear> {
    ++shadow_htile_clear_rejected[unsigned(reason)];return {};
  };
  // CpuResourceRead's complete callback/writer proof requires these modes.
  if(!native_indirect_enabled || !native_texture_coherence || !gpu_written_texture_snapshot)
    return reject(ShadowHtileClear::Reject::Readiness);
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
  const auto shape=ShadowHtileClear::Recognize(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,
      surface,{}, {}, {}, {});
  if(shape.reason!=ShadowHtileClear::Reject::Readiness)return reject(shape.reason);
  const auto ready=CurrentCpuReadReady();
  if(!ready)return reject(ShadowHtileClear::Reject::Readiness);
  const auto uniform_address=ShadowHtileClear::Base(prepared.subspan<8,4>());
  const auto generation=Core::Memory::DirectMapGeneration();
  if(generation!=surface.mapping_generation || !CpuResourceRead(uniform_address,8,ready))
    return reject(ShadowHtileClear::Reject::Readiness);
  std::array<u32,2> uniform;
  std::memcpy(uniform.data(),reinterpret_cast<const void*>(uniform_address),sizeof(uniform));
  const ShadowHtileClear::CpuReadProof uniform_proof{uniform_address,8,generation,
      Core::Memory::DirectMapGeneration(),true};
  if(uniform!=std::array<u32,2>{ShadowHtileClear::Words,ShadowHtileClear::ClearWord})
    return reject(ShadowHtileClear::Reject::Uniform);
  if(!CpuResourceRead(metadata,ShadowHtileClear::MetadataBytes,ready))
    return reject(ShadowHtileClear::Reject::Readiness);
  try {shadow_htile_clear_snapshot.resize(ShadowHtileClear::Words);}
  catch(const std::bad_alloc&) {return reject(ShadowHtileClear::Reject::Readiness);}
  std::memcpy(shadow_htile_clear_snapshot.data(),reinterpret_cast<const void*>(metadata),
      ShadowHtileClear::MetadataBytes);
  const ShadowHtileClear::CpuReadProof metadata_proof{metadata,ShadowHtileClear::MetadataBytes,
      generation,Core::Memory::DirectMapGeneration(),true};
  const auto decision=ShadowHtileClear::Recognize(program,prepared,{gx,gy,gz},{tx,ty,tz},indirect,
      surface,uniform_proof,uniform,metadata_proof,shadow_htile_clear_snapshot);
  if(!decision.plan)return reject(decision.reason);
  PreparedShadowHtileClear result{*decision.plan,target,target->tex,{}};
  for(const auto range:{NativeIndirect::Range{uniform_address,8},
      NativeIndirect::Range{metadata,ShadowHtileClear::MetadataBytes}}) {
    id<MTLBuffer> buffer=nil;u64 offset=0;
    if(!LocateIndirect(range.address,range.bytes,&buffer,&offset))
      return reject(ShadowHtileClear::Reject::Readiness);
    result.reads.push_back({range,{offset+(buffer==mem[0] ? 0 : mem_split),range.bytes}});
  }
  if(Core::Memory::DirectMapGeneration()!=generation)return reject(ShadowHtileClear::Reject::Readiness);
  return result;
}
void LeaseShadowHtileClear(std::optional<PreparedShadowHtileClear>& prepared) {
  if(!prepared)return;
  // Attach both complete inputs to the ACTUAL consumer command, after all
  // setup cuts. CP writes through physical aliases and mapping changes must
  // wait for that command. Capacity failure preserves the original dispatch.
  const auto ready=CurrentCpuReadReady();
  bool current=bool(ready);
  for(const auto& read:prepared->reads)
    current=current && CpuResourceRead(read.range.address,read.range.bytes,ready);
  if(!cmd || !current || Core::Memory::DirectMapGeneration()!=prepared->plan.generation ||
      !ProtectDescriptorReads(prepared->reads)) {
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];prepared.reset();
  }
}
void ApplyShadowHtileClear(const std::optional<PreparedShadowHtileClear>& prepared) {
  if(!prepared)return;
  const auto& clear=*prepared;
  const auto binding=shadow_htile_clear_surfaces.find(clear.plan.metadata);
  if(Core::Memory::DirectMapGeneration()!=clear.plan.generation ||
      binding==shadow_htile_clear_surfaces.end() || binding->second.target!=clear.plan.target ||
      binding->second.mapping_generation!=clear.plan.generation || clear.target->tex!=clear.texture) {
    ++shadow_htile_clear_rejected[unsigned(ShadowHtileClear::Reject::Readiness)];return;
  }
  // The original compute has been encoded. BeginPass will emit its native
  // equivalent in queue order, advancing the depth revision only then.
  clear.target->clear_depth=std::bit_cast<float>(clear.plan.depth_clear_word);
  clear.target->clear_pending=true;clear.target->velocity.safe=false;
  ++shadow_htile_clear_applied;
  if(trace)DC_INFO("gpu: shadow HTILE full clear depth %#llx (meta %#llx), original RMW retained",
      clear.plan.target,clear.plan.metadata);
}
