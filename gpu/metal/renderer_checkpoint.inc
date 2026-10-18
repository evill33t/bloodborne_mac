// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Gpu::Metal after MetalRenderer::Impl is complete. This stores
// authoritative GPU state only; shader/upload/view/presentation caches rebuild.
namespace CheckpointDetail {
namespace Raw=RawTextureSnapshot;
using Chunk=MetalRenderer::CheckpointChunk;
using Clock=std::chrono::steady_clock;
constexpr u64 MetadataId=0x1000,GdsId=0x1001,PlaneBase=0x100000000;
constexpr std::size_t MaxChunks=65536,MaxMetadata=32u<<20;
u64 PlaneId(std::size_t target,std::size_t plane) {return PlaneBase+(u64(target)<<16)+plane;}
void Put32(std::vector<u8>& b,u32 v) {for(unsigned k=0;k<4;++k)b.push_back(u8(v>>(k*8)));}
void Put64(std::vector<u8>& b,u64 v) {for(unsigned k=0;k<8;++k)b.push_back(u8(v>>(k*8)));}
struct Reader {
  std::span<const u8> bytes;
  std::size_t offset=0;
  bool ok=true;
  u32 U32() {if(bytes.size()-offset<4){ok=false;return 0;}u32 v=0;for(unsigned k=0;k<4;++k)v|=u32(bytes[offset++])<<(8*k);return v;}
  u64 U64() {if(bytes.size()-offset<8){ok=false;return 0;}u64 v=0;for(unsigned k=0;k<8;++k)v|=u64(bytes[offset++])<<(8*k);return v;}
};
struct Target {
  u64 address=0,written=0;
  u32 flags=0,dim=0,depth_layers=0,bind_slice=0,bind_level=0;
  Raw::Descriptor description;
  std::vector<Raw::Plane> planes;
  u64 clear[5]{};
  u32 stencil=0;
};
struct Meta {u64 address,target;u32 depth;};
struct Alias {u64 address,parent;u32 level,width,height,format;};
struct Dump {u64 address,generation;u32 bytes;};
struct Snapshot {u64 address,generation,va;};
struct Image {
  u64 write_seq=0,frame_count=0,dump_gen=0,arena_va=0,arena_head=0;
  u32 extents[9]{};
  std::vector<Target> targets;
  std::vector<Meta> metas;
  std::vector<Alias> aliases;
  std::vector<Dump> dumps;
  std::vector<Snapshot> snapshots;
};
bool Address(u64 value) {return value && value<=kGpuAddrMask;}
bool Range(u64 address,u64 size) {return Address(address) && size && size<=kGpuAddrMask+1-address;}
bool ReadAll(const Chunk& c,std::vector<u8>& bytes,std::string& error) {
  bytes.resize(c.size);return c.read(0,bytes,error);
}
bool Parse(void* device,std::span<const Chunk> chunks,Image& out,std::string& error) {
  auto fail=[&](const char* why){error=why;return false;};
  if(chunks.size()<2 || chunks.size()>MaxChunks)return fail("Invalid renderer checkpoint chunk count.");
  std::map<u64,const Chunk*> index;
  for(const auto& c:chunks)if(!c.read || !c.size || !index.emplace(c.id,&c).second)return fail("Duplicate or empty renderer checkpoint chunk.");
  auto metadata=index.find(MetadataId),gds=index.find(GdsId);
  if(metadata==index.end() || metadata->second->size>MaxMetadata || gds==index.end() || gds->second->size!=65536)
    return fail("Missing or invalid renderer metadata/GDS chunk.");
  std::vector<u8> bytes;if(!ReadAll(*metadata->second,bytes,error))return false;
  if(bytes.size()<8 || std::memcmp(bytes.data(),"BBMTRL1",8))return fail("Invalid renderer checkpoint signature.");
  Reader r{bytes,8};Image image;
  if(r.U32()!=1 || r.U32()!=0)return fail("Unsupported renderer checkpoint version.");
  image.write_seq=r.U64();image.frame_count=r.U64();image.dump_gen=r.U64();image.arena_va=r.U64();image.arena_head=r.U64();
  for(auto& e:image.extents)e=r.U32();
  const auto targets=r.U32(),metas=r.U32(),aliases=r.U32(),dumps=r.U32(),snapshots=r.U32();
  if(r.U32() || !r.ok || targets>MaxChunks || metas>262144 || aliases>262144 || dumps>262144 || snapshots>dumps ||
     u64(targets)*112+u64(metas)*24+u64(aliases)*32+u64(dumps)*24+u64(snapshots)*24>bytes.size()-r.offset)
    return fail("Invalid renderer checkpoint record counts.");
  if((!image.arena_va && image.arena_head) || (image.arena_va &&
      (!Range(image.arena_va,16u<<20) || (image.arena_va&65535) || (image.arena_head&255))))
    return fail("Invalid renderer constant arena.");
  std::set<u64> expected{MetadataId,GdsId};std::size_t potential_planes=0;
  for(u32 n=0;n<targets;++n) {
    Target t;t.address=r.U64();t.written=r.U64();t.flags=r.U32();t.dim=r.U32();t.depth_layers=r.U32();t.bind_slice=r.U32();t.bind_level=r.U32();
    auto& d=t.description;d.format=r.U32();d.type=r.U32();d.width=r.U32();d.height=r.U32();d.depth=r.U32();d.layers=r.U32();d.levels=r.U32();
    for(auto& c:t.clear)c=r.U64();t.stencil=r.U32();const auto reserved=r.U32();
    const auto type=t.dim==u32(TexDim::Tex2D)?MTLTextureType2D:t.dim==u32(TexDim::Tex2DArray)?MTLTextureType2DArray:MTLTextureType3D;
    const bool depth_format=d.format==MTLPixelFormatDepth32Float || d.format==MTLPixelFormatDepth16Unorm || d.format==MTLPixelFormatDepth32Float_Stencil8;
    if(!r.ok || !Address(t.address) || (t.flags&~7u) || reserved || t.dim>u32(TexDim::Tex3D) || t.written>image.write_seq ||
       d.type!=u32(type) || bool(t.flags&1)!=depth_format || !t.depth_layers ||
       (t.dim==u32(TexDim::Tex3D)?d.depth!=t.depth_layers:d.depth!=1) ||
       (t.dim==u32(TexDim::Tex2DArray)?d.layers!=t.depth_layers:d.layers!=1) ||
       (t.dim==u32(TexDim::Tex2D) && t.depth_layers!=1) || t.bind_level>=d.levels || t.bind_slice>=d.layers)
      return fail("Invalid renderer target metadata.");
    const u64 count=u64(d.levels)*d.layers*(d.format==MTLPixelFormatDepth32Float_Stencil8?2:1);
    if(count>MaxChunks || potential_planes>MaxChunks-count)return fail("Too many renderer texture planes.");
    potential_planes+=count;
    if(!Raw::Layout(device,d,t.planes,error))return false;
    if(t.flags&2)for(std::size_t p=0;p<t.planes.size();++p) {
      const auto id=PlaneId(n,p);auto found=index.find(id);
      if(found==index.end() || found->second->size!=t.planes[p].size)return fail("Missing or invalid renderer texture plane.");
      expected.insert(id);
    }
    image.targets.push_back(std::move(t));
  }
  std::set<u64> addresses;
  for(u32 n=0;n<metas;++n) {
    Meta m{r.U64(),r.U64(),r.U32()};const auto reserved=r.U32();
    if(!r.ok || !Address(m.address) || !Address(m.target) || m.depth>1 || reserved || !addresses.insert(m.address).second)
      return fail("Invalid renderer fast-clear metadata.");
    image.metas.push_back(m);
  }
  addresses.clear();
  for(u32 n=0;n<aliases;++n) {
    Alias a{r.U64(),r.U64(),r.U32(),r.U32(),r.U32(),r.U32()};
    if(!r.ok || !Address(a.address) || !Address(a.parent) || a.level>=15 || !a.width || !a.height || a.width>16384 || a.height>16384 ||
       !a.format || !addresses.insert(a.address).second)return fail("Invalid renderer mip alias.");
    image.aliases.push_back(a);
  }
  std::map<u64,Dump> by_dump;
  u64 end=0;
  for(u32 n=0;n<dumps;++n) {
    const auto address=r.U64();const auto size=r.U32(),reserved=r.U32();const auto generation=r.U64();
    Dump d{address,generation,size};
    if(!r.ok || !Range(d.address,d.bytes) || reserved || !d.generation || d.generation>image.dump_gen || d.address<end)
      return fail("Invalid renderer constant dump.");
    end=d.address+d.bytes;by_dump.emplace(d.address,d);image.dumps.push_back(d);
  }
  addresses.clear();
  for(u32 n=0;n<snapshots;++n) {
    Snapshot s{r.U64(),r.U64(),r.U64()};auto d=by_dump.find(s.address);
    if(!r.ok || !image.arena_va || d==by_dump.end() || s.generation!=d->second.generation || s.va<image.arena_va ||
       s.va-image.arena_va>(16u<<20)-u64(std::min(d->second.bytes,16u<<20)) || d->second.bytes>(16u<<20) ||
       (s.va&255) || !addresses.insert(s.address).second)return fail("Invalid renderer constant snapshot.");
    image.snapshots.push_back(s);
  }
  if(!r.ok || r.offset!=bytes.size() || expected.size()!=chunks.size())return fail("Unexpected renderer checkpoint data.");
  out=std::move(image);error.clear();return true;
}
Chunk Bytes(u64 id,std::vector<u8> bytes) {
  auto p=std::make_shared<const std::vector<u8>>(std::move(bytes));
  return {id,p->size(),[p](u64 offset,std::span<u8> out,std::string& error) {
    if(offset>p->size() || out.size()>p->size()-offset){error="Renderer checkpoint read is out of range.";return false;}
    if(!out.empty())std::memcpy(out.data(),p->data()+offset,out.size());error.clear();return true;
  }};
}
struct PlaneSource {
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLTexture> texture;
  std::size_t index;
  u64 size;
  Clock::time_point deadline;
  std::vector<u8> cache;
  std::mutex lock;
  bool Read(u64 offset,std::span<u8> out,std::string& error) {
    std::lock_guard guard(lock);
    if(offset>size || out.size()>size-offset){error="Renderer plane read is out of range.";return false;}
    if(out.empty()){error.clear();return true;}
    if(cache.empty() && !Raw::Capture((__bridge void*)device,(__bridge void*)queue,(__bridge void*)texture,index,cache,deadline,error))return false;
    std::memcpy(out.data(),cache.data()+offset,out.size());
    if(offset+out.size()==size)std::vector<u8>().swap(cache);
    error.clear();return true;
  }
};
}

struct MetalRenderer::CheckpointCapture::State {
  std::unique_lock<std::mutex> lock;
  std::vector<CheckpointChunk> chunks;
  explicit State(std::mutex& mutex):lock(mutex,std::defer_lock) {}
};
MetalRenderer::CheckpointCapture::CheckpointCapture(std::unique_ptr<State> s):state(std::move(s)) {}
MetalRenderer::CheckpointCapture::~CheckpointCapture()=default;
const std::vector<MetalRenderer::CheckpointChunk>& MetalRenderer::CheckpointCapture::Chunks() const {return state->chunks;}

std::unique_ptr<MetalRenderer::CheckpointCapture> MetalRenderer::CaptureCheckpoint(std::chrono::steady_clock::time_point deadline,std::string& error) {
  using namespace CheckpointDetail;
  if(!WaitIdleUntil(deadline,error))return {};
  auto state=std::make_unique<CheckpointCapture::State>(impl->lock);
  while(!state->lock.try_lock()) {
    if(Clock::now()>=deadline){error="Renderer registry checkpoint timed out.";return {};}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if(impl->cmd || impl->enc || impl->last_committed){error="Renderer submission resumed during checkpoint capture.";return {};}
  @autoreleasepool {
    std::vector<u8> metadata{'B','B','M','T','R','L','1',0};Put32(metadata,1);Put32(metadata,0);
    Put64(metadata,impl->write_seq);Put64(metadata,impl->frame_count);Put64(metadata,impl->dump_gen);Put64(metadata,impl->arena_va);Put64(metadata,impl->arena_head);
    for(const auto e:impl->cb_extent)Put32(metadata,e);Put32(metadata,impl->db_extent);
    std::vector<std::pair<Impl::RenderTarget*,bool>> targets;
    for(auto* map:{&impl->color_targets,&impl->depth_targets}) {
      std::vector<u64> keys;for(const auto& [key,list]:*map)keys.push_back(key);std::sort(keys.begin(),keys.end());
      for(const auto key:keys)for(const auto& t:map->at(key))targets.emplace_back(t.get(),map==&impl->depth_targets);
    }
    std::map<u64,Impl::Meta> metas(impl->metas.begin(),impl->metas.end());
    std::map<u64,Impl::MipAlias> aliases(impl->mip_alias.begin(),impl->mip_alias.end());
    std::map<u64,Impl::Snapshot> snapshots;
    for(const auto& [address,snapshot]:impl->snapshots) {
      const auto d=impl->const_dumps.find(address);
      if(d!=impl->const_dumps.end() && d->second.gen==snapshot.gen && snapshot.va)snapshots.emplace(address,snapshot);
    }
    if(targets.size()>MaxChunks || metas.size()>262144 || aliases.size()>262144 || impl->const_dumps.size()>262144 || snapshots.size()>262144) {
      error="Renderer checkpoint registry exceeds its limit.";return {};
    }
    Put32(metadata,targets.size());Put32(metadata,metas.size());Put32(metadata,aliases.size());Put32(metadata,impl->const_dumps.size());Put32(metadata,snapshots.size());Put32(metadata,0);
    for(std::size_t n=0;n<targets.size();++n) {
      const auto [t,depth]=targets[n];Raw::Descriptor description;std::vector<Raw::Plane> planes;
      if(!Raw::Describe((__bridge void*)t->tex,description,planes,error))return {};
      Put64(metadata,t->addr);Put64(metadata,t->written);Put32(metadata,u32(depth)|(u32(t->initialized)<<1)|(u32(t->clear_pending)<<2));
      Put32(metadata,u32(t->dim));Put32(metadata,t->depth_or_layers);Put32(metadata,t->bind_slice);Put32(metadata,t->bind_level);
      for(const auto v:{description.format,description.type,description.width,description.height,description.depth,description.layers,description.levels})Put32(metadata,v);
      for(const auto v:{t->clear_color.red,t->clear_color.green,t->clear_color.blue,t->clear_color.alpha,t->clear_depth})Put64(metadata,std::bit_cast<u64>(v));
      Put32(metadata,t->clear_stencil);Put32(metadata,0);
      if(t->initialized)for(std::size_t p=0;p<planes.size();++p) {
        if(state->chunks.size()>=MaxChunks-2){error="Renderer checkpoint has too many planes.";return {};}
        auto source=std::make_shared<PlaneSource>();source->device=impl->device;source->queue=impl->queue;source->texture=t->tex;
        source->index=p;source->size=planes[p].size;source->deadline=deadline;
        state->chunks.push_back({PlaneId(n,p),source->size,[source](u64 offset,std::span<u8> bytes,std::string& e){return source->Read(offset,bytes,e);}});
      }
    }
    for(const auto& [address,m]:metas){Put64(metadata,address);Put64(metadata,m.target);Put32(metadata,m.depth);Put32(metadata,0);}
    for(const auto& [address,a]:aliases){Put64(metadata,address);Put64(metadata,a.parent);Put32(metadata,a.level);Put32(metadata,a.width);Put32(metadata,a.height);Put32(metadata,a.fmt);}
    for(const auto& [address,d]:impl->const_dumps){Put64(metadata,address);Put32(metadata,d.bytes);Put32(metadata,0);Put64(metadata,d.gen);}
    for(const auto& [address,s]:snapshots){Put64(metadata,address);Put64(metadata,s.gen);Put64(metadata,s.va);}
    state->chunks.insert(state->chunks.begin(),Bytes(MetadataId,std::move(metadata)));
    std::vector<u8> gds(65536);std::memcpy(gds.data(),impl->gds.contents,gds.size());state->chunks.insert(state->chunks.begin()+1,Bytes(GdsId,std::move(gds)));
    Image validated;if(!Parse((__bridge void*)impl->device,state->chunks,validated,error))return {};
    return std::unique_ptr<CheckpointCapture>(new CheckpointCapture(std::move(state)));
  }
}

bool MetalRenderer::RestoreCheckpoint(std::span<const CheckpointChunk> chunks,std::chrono::steady_clock::time_point deadline,std::string& error) {
  using namespace CheckpointDetail;
  std::unique_lock lock(impl->lock,std::defer_lock);
  while(!lock.try_lock()) {
    if(Clock::now()>=deadline){error="Renderer restore lock timed out.";return false;}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if(impl->cmd || impl->last_committed || impl->frame_count || impl->write_seq || !impl->color_targets.empty() || !impl->depth_targets.empty() ||
     impl->arena_va || !impl->const_dumps.empty() || !impl->metas.empty() || !impl->mip_alias.empty()) {
    error="Renderer checkpoint restore requires a fresh renderer.";return false;
  }
  if(Clock::now()>=deadline){error="Renderer restore deadline expired.";return false;}
  Image image;if(!Parse((__bridge void*)impl->device,chunks,image,error))return false;
  if(image.arena_va && !Readable(image.arena_va,Impl::kArenaSize)){error="Guest constant arena must be restored before the renderer.";return false;}
  std::map<u64,const Chunk*> index;for(const auto& c:chunks)index.emplace(c.id,&c);
  std::vector<u8> gds;if(!ReadAll(*index.at(GdsId),gds,error))return false;
  Impl::TargetMap colors,depths;
  @autoreleasepool {
    for(std::size_t n=0;n<image.targets.size();++n) {
      const auto& t=image.targets[n];const auto& d=t.description;
      auto rt=std::make_unique<Impl::RenderTarget>();
      rt->tex=impl->NewTargetTexture(MTLPixelFormat(d.format),d.width,d.height,d.levels,t.flags&1,TexDim(t.dim),t.depth_layers);
      if(!rt->tex){error="Could not allocate a restored Metal target.";return false;}
      Raw::Descriptor actual;std::vector<Raw::Plane> planes;
      if(!Raw::Describe((__bridge void*)rt->tex,actual,planes,error) || actual!=d){error="Restored Metal target descriptor differs from checkpoint.";return false;}
      if(t.flags&2)for(std::size_t p=0;p<t.planes.size();++p) {
        std::vector<u8> bytes;if(!ReadAll(*index.at(PlaneId(n,p)),bytes,error))return false;
        if(!Raw::Restore((__bridge void*)impl->device,(__bridge void*)impl->queue,(__bridge void*)rt->tex,p,bytes,deadline,error))return false;
      }
      rt->addr=t.address;rt->width=d.width;rt->height=d.height;rt->levels=d.levels;rt->depth_or_layers=t.depth_layers;rt->dim=TexDim(t.dim);
      rt->initialized=t.flags&2;rt->clear_pending=t.flags&4;rt->written=t.written;rt->bind_slice=t.bind_slice;rt->bind_level=t.bind_level;
      rt->clear_color=MTLClearColorMake(std::bit_cast<double>(t.clear[0]),std::bit_cast<double>(t.clear[1]),std::bit_cast<double>(t.clear[2]),std::bit_cast<double>(t.clear[3]));
      rt->clear_depth=std::bit_cast<double>(t.clear[4]);rt->clear_stencil=t.stencil;
      (t.flags&1?depths:colors)[t.address].push_back(std::move(rt));
    }
  }
  // Allocate/validate all host registries before publishing any of them.
  decltype(impl->metas) metas;for(const auto& m:image.metas)metas.emplace(m.address,Impl::Meta{m.target,bool(m.depth)});
  decltype(impl->mip_alias) aliases;for(const auto& a:image.aliases)aliases.emplace(a.address,Impl::MipAlias{a.parent,a.level,a.width,a.height,MTLPixelFormat(a.format)});
  decltype(impl->const_dumps) dumps;for(const auto& d:image.dumps)dumps.emplace(d.address,Impl::ConstDump{d.bytes,d.generation});
  decltype(impl->snapshots) snapshots;for(const auto& s:image.snapshots)snapshots.emplace(s.address,Impl::Snapshot{s.generation,s.va});
  if(Clock::now()>=deadline){error="Renderer restore deadline expired before activation.";return false;}
  impl->color_targets=std::move(colors);impl->depth_targets=std::move(depths);impl->metas=std::move(metas);impl->mip_alias=std::move(aliases);
  impl->const_dumps=std::move(dumps);impl->snapshots=std::move(snapshots);
  impl->write_seq=image.write_seq;impl->frame_count=image.frame_count;impl->dump_gen=image.dump_gen;impl->arena_va=image.arena_va;
  impl->arena_head=image.arena_head;impl->arena_commit_head=image.arena_head;impl->arena_released.store(image.arena_head,std::memory_order_release);
  std::copy(std::begin(image.extents),std::begin(image.extents)+8,std::begin(impl->cb_extent));impl->db_extent=image.extents[8];
  std::memcpy(impl->gds.contents,gds.data(),gds.size());error.clear();return true;
}
