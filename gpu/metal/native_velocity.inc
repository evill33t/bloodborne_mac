// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside MetalRenderer::Impl. Contracts verified against raw game
// captures and the original producer/dilation GCN, not CPU registration timing.
std::optional<SceneCamera> velocity_previous_camera;
u64 velocity_previous_frame=~0ull;
RenderTarget* velocity_candidate=nullptr;
u64 velocity_clears=0,velocity_producers=0,velocity_camera_matches=0,velocity_filters=0;
static bool ReadVelocityBytes(u64 address,void* out,std::size_t size) {
  if(!address || !size || address>UINT64_MAX-size) return false;
  mach_vm_size_t copied=0;
  return mach_vm_read_overwrite(mach_task_self(),address,size,reinterpret_cast<mach_vm_address_t>(out),&copied)==KERN_SUCCESS && copied==size;
}
bool NativeVelocityEnabled() const {
  static const bool enabled=[] {const char* value=std::getenv("BB_METAL_NATIVE_MOTION");return !value || std::strcmp(value,"0");}();
  return enabled && temporal_aa.load(std::memory_order_relaxed);
}
bool VelocityFrame(const RenderTarget& target) const {
  return target.velocity.safe && target.velocity.frame==frame_count &&
      target.velocity.generation==Core::Memory::DirectMapGeneration();
}
void ObserveNativeVelocityClear(RenderTarget& target,const double value[4]) {
  target.velocity={};
  if(!NativeVelocityEnabled() || target.tex.pixelFormat!=MTLPixelFormatRGBA8Unorm ||
      target.dim!=TexDim::Tex2D || target.levels!=1 || target.depth_or_layers!=1 ||
      std::abs(value[0]-127.0/255)>1e-6 || std::abs(value[1]-127.0/255)>1e-6 || value[2]!=1 || value[3]!=0) return;
  target.velocity.frame=frame_count;target.velocity.generation=Core::Memory::DirectMapGeneration();target.velocity.safe=true;
  ++velocity_clears;
}
static std::array<float,16> VelocityViewProjection(const SceneCamera& camera) {
  std::array<float,16> result{};
  for(unsigned k=0;k<4;++k) {
    result[k]=camera.view[k]*camera.projection[0];
    result[4+k]=camera.view[4+k]*camera.projection[1];
    result[8+k]=camera.view[8+k]*camera.projection[2]+(k==3 ? camera.projection[3] : 0);
    result[12+k]=camera.view[8+k];
  }
  return result;
}
bool VelocityCameraContract(const Program& vertex,const u32* ud) const {
  const bool verified=vertex.hash==0x474f614296148b2bull || vertex.hash==0x5c02aa3a37ccb160ull ||
      vertex.hash==0x8b2c854fa597d4c3ull || vertex.hash==0xde3f0ba6e79fcc74ull || vertex.hash==0x659fa635717689b7ull;
  if(!verified || !scene_camera || scene_camera_frame!=frame_count ||
      !velocity_previous_camera || velocity_previous_frame+1!=frame_count) return false;
  // This exact VS reads current/previous VP matrices from the 192-byte V# in
  // user data 8..11. Match both to observed rendered cameras before admitting
  // its second transform path as the preceding rendered frame.
  const u64 address=(u64(ud[8])|(u64(ud[9]&0xfff)<<32))&kGpuAddrMask;
  const u32 stride=(ud[9]>>16)&0x3fff;
  if(u64(ud[10])*std::max(1u,stride)!=192) return false;
  std::array<float,32> matrices;if(!ReadVelocityBytes(address,matrices.data(),sizeof(matrices))) return false;
  const auto current=VelocityViewProjection(*scene_camera),previous=VelocityViewProjection(*velocity_previous_camera);
  for(unsigned k=0;k<32;++k) {
    const float expected=k<16 ? current[k] : previous[k-16];
    if(!std::isfinite(matrices[k]) || std::abs(matrices[k]-expected)>2e-4f*std::max(1.f,std::abs(expected))) return false;
  }
  return true;
}
std::optional<std::array<float,2>> VelocityScale(const Program& pixel,const u32* ud) const {
  if(pixel.hash!=0xba34012ce43413a9ull && pixel.hash!=0xdc9a11395024c0e9ull) return {};
  std::array<u32,4> descriptor{};
  if(pixel.hash==0xdc9a11395024c0e9ull) std::copy_n(ud+4,4,descriptor.begin());
  else {
    const u64 table=(u64(ud[2])|(u64(ud[3]&0xfff)<<32))&kGpuAddrMask;
    if(table>kGpuAddrMask-16 || !ReadVelocityBytes(table+16,descriptor.data(),16)) return {};
  }
  const u64 address=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&kGpuAddrMask;
  if(u64(descriptor[2])*std::max(1u,(descriptor[1]>>16)&0x3fff)!=176) return {};
  std::array<float,2> scale;if(!ReadVelocityBytes(address,scale.data(),8)) return {};
  if(!std::isfinite(scale[0]) || !std::isfinite(scale[1]) || scale[0]<.01f || scale[0]>1024 || scale[1]>-.01f || scale[1]<-1024) return {};
  return std::array<float,2>{-1/scale[0],1/scale[1]};
}
static bool VelocityDescriptorWord(const Shader::SgprSource& source,const u32* ud,u32& value) {
  if(source.ud>=16 || source.kind==Shader::SgprSource::Unknown) return false;
  if(source.kind==Shader::SgprSource::UserData) {value=ud[source.ud];return true;}
  if(source.ud>=15) return false;
  const u64 base=source.kind==Shader::SgprSource::PointerLoad
      ? (u64(ud[source.ud])|(u64(ud[source.ud+1])<<32))&0xffffffffffffull
      : u64(ud[source.ud])|(u64(ud[source.ud+1]&0xfff)<<32);
  return base<=UINT64_MAX-source.offset && ReadVelocityBytes(base+source.offset,&value,4);
}
bool VelocityFilterContract(const RegFile& registers,const Program& vertex,const Program& pixel,
    const u32* vud,const u32* pud,const RenderTarget& destination,const DrawCall& draw) const {
  // Captured VS 0x80d1ceb100 (hash fb7258cd66f11a0f) forwards two vertex
  // streams verbatim: float4 clip position and float2 UV. Its hash alone does
  // not prove the supplied geometry covers the image or preserves its UVs.
  if(vertex.hash!=0xfb7258cd66f11a0full || registers.Primitive()!=PrimType::TriStrip ||
      draw.indexed || draw.count!=4 || std::max(draw.instances,1u)!=1 || registers[Reg::VGT_INDX_OFFSET]!=0 ||
      registers[Reg::PA_CL_VTE_CNTL]!=0x43f || (registers[Reg::DB_DEPTH_CONTROL]&3) ||
      (registers[Reg::PA_SU_SC_MODE_CNTL]&3) || (registers[Reg::CB_SHADER_MASK]&15)!=15) return false;
  const std::array<float,6> expected{destination.width*.5f,destination.width*.5f,
      destination.height*-.5f,destination.height*.5f,1,0};
  for(unsigned k=0;k<6;++k)
    if(registers.F(Reg::PA_CL_VPORT_XSCALE+k)!=expected[k]) return false;
  const u64 table=(u64(vud[2])|(u64(vud[3])<<32))&0xffffffffffffull;
  std::array<u32,8> streams{};
  if(!ReadVelocityBytes(table,streams.data(),sizeof(streams))) return false;
  const auto position=streams.data(),uv=streams.data()+4;
  // The captured interleaved layout has a 24-byte stride and four records;
  // formats are FLOAT_32_32_32_32/identity and FLOAT_32_32/XY01.
  if(((position[1]>>16)&0x3fff)!=24 || position[2]!=4 || (position[3]&0x7ffff)!=0x77fac ||
      ((uv[1]>>16)&0x3fff)!=24 || uv[2]!=4 || (uv[3]&0x7ffff)!=0x5f22c) return false;
  const u64 position_base=(u64(position[0])|(u64(position[1]&0xfff)<<32))&kGpuAddrMask;
  const u64 uv_base=(u64(uv[0])|(u64(uv[1]&0xfff)<<32))&kGpuAddrMask;
  if(position_base>kGpuAddrMask-96 || uv_base!=position_base+16) return false;
  std::array<std::array<float,4>,4> corners{};
  unsigned seen=0;
  for(unsigned k=0;k<4;++k) {
    auto& p=corners[k];std::array<float,2> at{};
    if(!ReadVelocityBytes(position_base+k*24,p.data(),sizeof(p)) ||
        !ReadVelocityBytes(uv_base+k*24,at.data(),sizeof(at))) return false;
    for(float f:p) if(!std::isfinite(f)) return false;
    if(std::abs(std::abs(p[0])-1)>1e-6f || std::abs(std::abs(p[1])-1)>1e-6f ||
        p[2]<0 || p[2]>1 || p[3]!=1 || !std::isfinite(at[0]) || !std::isfinite(at[1]) ||
        std::abs(at[0]-(p[0]*.5f+.5f))>1e-6f || std::abs(at[1]-(.5f-p[1]*.5f))>1e-6f) return false;
    const unsigned bit=1u<<((p[0]>0 ? 1 : 0)+(p[1]>0 ? 2 : 0));
    if(seen&bit) return false;
    seen|=bit;
  }
  // Four unique corners cover both halves only when the strip's shared edge
  // is the diagonal; another ordering can overlap triangles and leave a hole.
  if(seen!=15 || corners[1][0]!=-corners[2][0] || corners[1][1]!=-corners[2][1]) return false;
  if(pixel.images.size()!=1 || pixel.images[0].storage || pixel.samplers.size()!=1) return false;
  std::array<u32,8> texture{};std::array<u32,4> sampler{};
  for(unsigned k=0;k<8;++k)
    if(!VelocityDescriptorWord(pixel.images[0].tsharp[k],pud,texture[k])) return false;
  for(unsigned k=0;k<4;++k)
    if(!VelocityDescriptorWord(pixel.samplers[0][k],pud,sampler[k])) return false;
  // Resolve the actual T# as well as matching the bound texture: its parent
  // alone cannot distinguish swizzled views, another mip, or a shifted UV grid.
  return ((texture[1]>>20)&0x3f)==10 && ((texture[1]>>26)&15)==0 &&
      (texture[2]&0x3fff)+1==destination.width && ((texture[2]>>14)&0x3fff)+1==destination.height &&
      (texture[3]&0xfffff)==0xfac && (texture[3]>>28)==9 &&
      (sampler[0]&7)==2 && ((sampler[0]>>3)&7)==2 && (sampler[1]&0xfff)==0;
}
void ObserveNativeVelocity(const RegFile& registers,const Program& vertex,const Program* pixel,
    const u32* vud,const u32* pud,RenderTarget* const colors[8],const std::vector<id<MTLTexture>>& textures,
    const DrawCall& draw,const Rect& scissor) {
  if(!NativeVelocityEnabled()) return;
  auto* destination=colors[0];
  const bool single=destination && std::count_if(colors,colors+8,[](auto* p){return p!=nullptr;})==1;
  const bool plain=single && destination->tex.pixelFormat==MTLPixelFormatRGBA8Unorm &&
      destination->dim==TexDim::Tex2D && destination->levels==1 && destination->depth_or_layers==1 &&
      (registers[Reg::CB_TARGET_MASK]&15)==15 && !(registers[Reg::CB_BLEND0_CONTROL]&(1u<<30)) &&
      registers.F(Reg::PA_CL_VPORT_XSCALE)>0 && registers.F(Reg::PA_CL_VPORT_XSCALE+2)<0;
  auto prior=destination ? destination->velocity : RenderTarget::NativeVelocity{};
  for(unsigned k=0;k<8;++k) if(colors[k] && (registers[Reg::CB_TARGET_MASK]&(15u<<(4*k)))) colors[k]->velocity.safe=false;
  if(!plain || !pixel) return;
  if(pixel->hash==0xba34012ce43413a9ull || pixel->hash==0xdc9a11395024c0e9ull) {
    ++velocity_producers;
    // Preserve the clear plus all preceding producers only while their exact
    // scale and previous/current camera contract continue to agree.
    const auto scale=VelocityScale(*pixel,pud);
    const bool cameras=VelocityCameraContract(vertex,vud);
    if(cameras) ++velocity_camera_matches;
    if(!prior.safe || prior.frame!=frame_count || prior.generation!=Core::Memory::DirectMapGeneration() ||
        !scale || !cameras || (prior.produced && prior.uv_scale!=*scale)) return;
    destination->velocity=prior;destination->velocity.uv_scale=*scale;destination->velocity.produced=true;
    velocity_candidate=destination;
  } else if(pixel->hash==0x846ee73768d3e184ull && textures.size()==1 &&
      VelocityFilterContract(registers,vertex,*pixel,vud,pud,*destination,draw)) {
    // Use the texture actually bound for this draw, including its parent when
    // the normal resolver supplied a view. Never re-resolve a CPU registration.
    RenderTarget* source=nullptr;
    for(const auto& [address,variants]:color_targets) for(const auto& target:variants)
      if(target->tex==textures[0] || target->tex==textures[0].parentTexture) source=target.get();
    if(!source || source==destination || !VelocityFrame(*source) || !source->velocity.produced ||
        source->dim!=TexDim::Tex2D || source->levels!=1 || source->depth_or_layers!=1 ||
        source->tex.pixelFormat!=MTLPixelFormatRGBA8Unorm || source->width!=destination->width || source->height!=destination->height) return;
    const bool full=scissor.x0==0 && scissor.y0==0 &&
        scissor.x1==s32(destination->width) && scissor.y1==s32(destination->height);
    // An uncleared ping-pong target is safe only for a proven full overwrite.
    // With a partial scissor, untouched pixels must already be current vectors
    // with the same scale, or the canonical empty clear (alpha zero).
    if(!full && (!prior.safe || prior.frame!=frame_count || prior.generation!=Core::Memory::DirectMapGeneration() ||
        (prior.produced && prior.uv_scale!=source->velocity.uv_scale))) return;
    destination->velocity=source->velocity;velocity_candidate=destination;
    ++velocity_filters;
  }
}
std::optional<MotionInput> NativeVelocityInput() const {
  if(!NativeVelocityEnabled() || !velocity_candidate || !VelocityFrame(*velocity_candidate) || !velocity_candidate->velocity.produced ||
      velocity_candidate->dim!=TexDim::Tex2D || velocity_candidate->levels!=1 || velocity_candidate->depth_or_layers!=1 ||
      !temporal_color || velocity_candidate->width!=(temporal_color->width+1)/2 || velocity_candidate->height!=(temporal_color->height+1)/2 ||
      velocity_previous_frame+1!=frame_count) return {};
  return MotionInput{(__bridge void*)velocity_candidate->tex,frame_count,velocity_previous_frame,velocity_candidate->velocity.uv_scale};
}
void FinishNativeVelocityFrame() {
  if(scene_camera && scene_camera_frame==frame_count) {velocity_previous_camera=scene_camera;velocity_previous_frame=frame_count;}
  else {velocity_previous_camera.reset();velocity_previous_frame=~0ull;}
  velocity_candidate=nullptr;
}
