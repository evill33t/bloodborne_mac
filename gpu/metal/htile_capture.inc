// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside MetalRenderer::Impl, after its diagnostics fields. Diagnostic
// state only: do not serialize this registry into renderer checkpoints.
//
// Integration:
//   At the depth NoteMeta call: ObserveHtileCaptureLayout(r,zbase,w,h).
//   After Dispatch's GetProgram, before metadata substitutions:
//     TraceHtileReaders(*cs,ud,gx,gy,gz).
// CaptureRaw(id<MTLTexture>, const std::string&) is provided by the renderer.
// Existing MslSource/GetProgram dump_dir files preserve actual translated MSL
// variants; this helper additionally saves original cached GCN for readers.

struct HtileCaptureLayout {
  u64 target=0,bytes=0,generation=0;
  u32 width=0,height=0,pitch=0,padded_height=0;
  // Z_INFO, STENCIL_INFO, DEPTH_INFO, HTILE_SURFACE, DEPTH_SIZE,
  // DEPTH_SLICE, DEPTH_VIEW, Z_READ_BASE, Z_WRITE_BASE, DEPTH_CLEAR.
  std::array<u32,10> raw{};
};
std::unordered_map<u64,HtileCaptureLayout> htile_capture_layouts;
u64 htile_capture_frame=~0ull;
u32 htile_capture_dispatches=0;
std::unordered_set<u64> htile_capture_saved_shaders;

void ObserveHtileCaptureLayout(const RegFile& r,u64 target,u32 width,u32 height) {
  if(capture_root.empty()) return;
  const u64 meta=u64(r[Reg::DB_HTILE_DATA_BASE])<<8;
  if (!meta || !ZInfo::TileSurface(r[Reg::DB_Z_INFO])) return;
  HtileCaptureLayout layout;
  layout.target=target; layout.width=width; layout.height=height;
  layout.generation=Core::Memory::DirectMapGeneration();
  const u32 depth_size=r[Reg::DB_DEPTH_SIZE];
  layout.pitch=((depth_size&0x7ff)+1)*8;
  layout.padded_height=(((depth_size>>11)&0x7ff)+1)*8;
  layout.raw={r[Reg::DB_Z_INFO],r[Reg::DB_STENCIL_INFO],r[0xA00F],r[0xA2AF],
      depth_size,r[Reg::DB_DEPTH_SLICE],r[Reg::DB_DEPTH_VIEW],
      r[Reg::DB_Z_READ_BASE],r[Reg::DB_Z_WRITE_BASE],r[Reg::DB_DEPTH_CLEAR]};
  // Verified native allocation calculation (RVA 0x109e570). This is a range
  // estimate for descriptor matching, not an HTILE address/encoding decoder.
  const u64 alignment=(layout.raw[3]&1) ? 64 : 512;
  const u64 padded_width=(u64(layout.pitch)+alignment-1)&~(alignment-1);
  const u64 padded_height=(u64(layout.padded_height)+alignment-1)&~(alignment-1);
  // The native slice-size helper (RVA 0x1072630) uses one slice. An array's
  // allocation may be larger, so do not infer additional slices from the view.
  layout.bytes=((padded_width*padded_height/16)+16383)&~16383ull;
  htile_capture_layouts[meta]=layout;
}

// Returns descriptor/instruction overlaps. These are static read sites with
// resolved resources, not proof a branch/lane performs a particular load.
u32 TraceHtileReaders(const Program& program,const u32* ud,u32 gx,u32 gy,u32 gz) {
  if (!trace || capture_root.empty()) return 0;
  if (htile_capture_frame!=frame_count) {
    htile_capture_frame=frame_count; htile_capture_dispatches=0;
  }
  // A live trace can include many copies. Bound diagnostic disk and log work.
  if (htile_capture_dispatches++>=4096) return 0;
  const char* directory=dump_dir ? dump_dir : dump_frames;
  char stem[160];
  std::snprintf(stem,sizeof(stem),"htile_f%llu_d%llu_cs%016llx",
      frame_count,stat_dispatches,program.hash);
  if(program.stage!=Stage::Compute) std::snprintf(stem,sizeof(stem),"htile_f%llu_draw%llu_s%u_h%016llx",
      frame_count,draw_count,unsigned(program.stage),program.hash);
  std::ofstream output;
  if (directory) output.open(std::string(directory)+"/"+stem+".json");
  if (output) {
    output<<"{\"schema\":1,\"frame\":"<<frame_count<<",\"dispatch\":"<<stat_dispatches
      <<",\"shader_address\":"<<program.address<<",\"shader_hash\":"<<program.hash
      <<",\"retail_id\":"<<program.retail_id<<",\"has_branches\":"<<(program.has_branches?"true":"false")
      <<",\"groups\":["<<gx<<','<<gy<<','<<gz<<"],\"user_data\":[";
    for (unsigned k=0;k<16;++k) output<<(k?",":"")<<ud[k];
    output<<"],\"scope\":\"resolved descriptor overlaps; dynamic lane addresses are not captured\",\"reads\":[";
  }
  bool first=true;
  u32 matches=0,unknown=0;
  std::set<u64> captured_depth;
  for (const auto& read:program.buffer_reads) {
    std::array<u32,4> descriptor{};
    bool resolved=true;
    for (unsigned k=0;k<4;++k) {
      const auto& source=read.descriptor[k];
      // Unknown provenance must not silently become a valid descriptor word.
      if (source.kind==Shader::SgprSource::Unknown || source.ud>=16 ||
          (source.kind!=Shader::SgprSource::UserData && source.ud>=15)) resolved=false;
      else {
        bool readable=true;
        if (source.kind!=Shader::SgprSource::UserData) {
          const u64 pointer=source.kind==Shader::SgprSource::PointerLoad
            ? (u64(ud[source.ud])|(u64(ud[source.ud+1])<<32))&0xFFFFFFFFFFFFull
            : u64(ud[source.ud])|(u64(ud[source.ud+1]&0xfff)<<32);
          const u64 address=pointer+source.offset;
          readable=address>=pointer && address<=~0ull-4 && Readable(address,4);
        }
        if (readable) descriptor[k]=Shader::ResolveSgpr(source,ud);
        else resolved=false;
      }
    }
    if (!resolved) ++unknown;
    const u64 base=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&kGpuAddrMask;
    const u32 stride=(descriptor[1]>>16)&0x3fff;
    const u64 bytes=u64(descriptor[2])*std::max(1u,stride);
    const bool valid=resolved && base && bytes && bytes<=kGpuAddrMask+1-base;
    u64 dump_address=0,dump_bytes=0,dump_generation=0;
    bool dump_covers_resource=false;
    if(valid) {
      auto dump=const_dumps.upper_bound(base);
      if(dump!=const_dumps.begin()) {
        --dump;
        if(base-dump->first<dump->second.bytes) {
          dump_address=dump->first;dump_bytes=dump->second.bytes;dump_generation=dump->second.gen;
          dump_covers_resource=bytes<=dump_bytes-(base-dump_address);
        }
      }
    }
    if (output) {
      output<<(first?"":",")<<"{\"pc\":"<<read.pc<<",\"encoding\":"<<unsigned(read.encoding)
        <<",\"op\":"<<read.op<<",\"resolved\":"<<(resolved?"true":"false")
        <<",\"descriptor\":[";
      for (unsigned k=0;k<4;++k) output<<(k?",":"")<<descriptor[k];
      output<<"],\"provenance\":[";
      for (unsigned k=0;k<4;++k) {
        const auto& source=read.descriptor[k];
        output<<(k?",":"")<<"["<<unsigned(source.kind)<<','<<unsigned(source.ud)<<','<<source.offset<<"]";
      }
      output<<"],\"base\":"<<base<<",\"descriptor_bytes\":"<<bytes
        <<",\"cpu_dump_address\":"<<dump_address<<",\"cpu_dump_bytes\":"<<dump_bytes
        <<",\"cpu_dump_generation\":"<<dump_generation<<",\"cpu_dump_covers_resource\":"<<(dump_covers_resource?"true":"false")
        <<",\"immediate_offset\":"<<read.immediate_offset<<",\"scalar_offset_operand\":"<<read.scalar_offset
        <<",\"dynamic_scalar_offset\":"<<(read.dynamic_scalar_offset?"true":"false")
        <<",\"vaddr\":"<<unsigned(read.vector_address)<<",\"idxen\":"<<(read.index_enabled?"true":"false")
        <<",\"offen\":"<<(read.offset_enabled?"true":"false")<<",\"addr64\":"<<(read.address64?"true":"false")
        <<",\"writes\":"<<(read.writes?"true":"false")
        <<",\"resource_written_by_shader\":"<<(read.descriptor_written?"true":"false")<<",\"htile\":[";
    }
    first=false;
    bool first_meta=true;
    for (const auto& [meta,layout]:htile_capture_layouts) {
      const auto registered=metas.find(meta);
      if (!valid || registered==metas.end() || !registered->second.depth ||
          registered->second.target!=layout.target || !layout.bytes ||
          layout.bytes>kGpuAddrMask+1-meta || base>=meta+layout.bytes || meta>=base+bytes) continue;
      ++matches;
      const bool generation_matches=layout.generation==Core::Memory::DirectMapGeneration();
      RenderTarget* depth=Latest(depth_targets,layout.target);
      DC_INFO("gpu: HTILE read resource cs=%#llx hash=%#llx pc=%#x op=%u desc=%08x/%08x/%08x/%08x "
          "range=%#llx+%#llx metadata=%#llx+%#llx depth=%#llx %ux%u mapping=%s dynamic-address=yes",
          program.address,program.hash,read.pc,read.op,descriptor[0],descriptor[1],descriptor[2],descriptor[3],
          base,bytes,meta,layout.bytes,layout.target,layout.width,layout.height,generation_matches?"same":"changed");
      if (output) {
        output<<(first_meta?"":",")<<"{\"base\":"<<meta<<",\"estimated_slice_bytes\":"<<layout.bytes
          <<",\"depth_base\":"<<layout.target<<",\"width\":"<<layout.width<<",\"height\":"<<layout.height
          <<",\"pitch\":"<<layout.pitch<<",\"padded_height\":"<<layout.padded_height
          <<",\"mapping_generation\":"<<layout.generation<<",\"mapping_matches\":"<<(generation_matches?"true":"false")
          <<",\"depth_revision\":"<<(depth?depth->depth_revision:0)<<",\"last_write\":"<<(depth?depth->written:0)
          <<",\"clear_pending\":"<<(depth&&depth->clear_pending?"true":"false")<<",\"raw_registers\":[";
        for (unsigned k=0;k<layout.raw.size();++k) output<<(k?",":"")<<layout.raw[k];
        output<<"]}";
      }
      first_meta=false;
      if (program.stage==Stage::Compute && !read.descriptor_written && base>=meta && base<meta+layout.bytes &&
          depth && depth->initialized && !depth->clear_pending && generation_matches &&
          depth->width==layout.width && depth->height==layout.height && captured_depth.insert(meta).second)
        CaptureRaw(depth->tex,std::string(stem)+"_meta"+Hex(meta)+"_depth"+Hex(layout.target));
    }
    if (output) output<<"]}";
  }
  if (output) output<<"],\"overlaps\":"<<matches<<",\"unknown_provenance_reads\":"<<unknown<<"}\n";
  DC_INFO("gpu: HTILE resource audit cs=%#llx hash=%#llx reads=%zu unresolved=%u overlaps=%u",
      program.address,program.hash,program.buffer_reads.size(),unknown,matches);
  if (matches && directory && htile_capture_saved_shaders.insert(program.hash).second) {
    // Use the cache's immutable bytes instead of rereading guest memory that
    // may already have been recycled by shader streaming.
    const auto shaders_at=shaders.find(program.address^(u64(program.stage)<<60));
    if (shaders_at!=shaders.end()) for (const auto& entry:shaders_at->second) {
      if (entry.prog.get()!=&program) continue;
      std::ofstream code(std::string(directory)+"/"+stem+".gcn",std::ios::binary);
      code.write(reinterpret_cast<const char*>(entry.code.data()),entry.code.size()*sizeof(u32));
      break;
    }
  }
  return matches;
}
