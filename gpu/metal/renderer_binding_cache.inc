// Included inside MetalRenderer::Impl. Only Draw's identity bindings use this
// cache. User data, diagnostic buffers, tessellation inputs and depth bounds
// remain unconditional. A command buffer retaining references keeps raw keys
// alive until its encoder ends; every encoder boundary advances the epoch.
struct DrawBindingCache {
  struct Key {
    const void* object=nullptr;
    u64 epoch=0;
  };
  struct Counts {
    u64 intended=0,emitted=0,skipped=0;
  };
  bool enabled=[] {
    const char* value=std::getenv("BB_METAL_BINDING_CACHE");
    return value && !std::strcmp(value,"1");
  }();
  bool active=false;
  u64 epoch=0;
  // Metal permits 128 textures and 16 samplers per stage. Unrecognized slots
  // always bind, including fragment texture30 written by depth-bounds setup.
  std::array<std::array<Key,3>,2> buffers{};
  std::array<std::array<Key,128>,2> textures{};
  std::array<std::array<Key,16>,2> samplers{};
  Counts buffer_counts,texture_counts,sampler_counts;

  void Reset(bool retained=false) {
    active=enabled && retained;
    if (!enabled) return;
    if (++epoch==0) {
      buffers={};textures={};samplers={};
      ++epoch;
    }
  }
  bool Need(Key* key,const void* object,Counts& counts) {
    if (!enabled) return true;
    ++counts.intended;
    if (active && key && key->epoch==epoch && key->object==object) {
      ++counts.skipped;
      return false;
    }
    ++counts.emitted;
    if (active && key) *key={object,epoch};
    return true;
  }
  bool Buffer(bool fragment,u32 slot,const void* object) {
    // All eligible buffers use offset0; other slots/offsets are never cached.
    Key* key=slot>=Shader::Binding::Mem0 && slot<=Shader::Binding::MemMap
        ? &buffers[fragment][slot-Shader::Binding::Mem0] : nullptr;
    return Need(key,object,buffer_counts);
  }
  bool Texture(bool fragment,u32 slot,const void* object) {
    Key* key=slot<textures[fragment].size() &&
        !(fragment && slot==Shader::kDepthBoundsTexture)
        ? &textures[fragment][slot] : nullptr;
    return Need(key,object,texture_counts);
  }
  bool Sampler(bool fragment,u32 slot,const void* object) {
    Key* key=slot<samplers[fragment].size() ? &samplers[fragment][slot] : nullptr;
    return Need(key,object,sampler_counts);
  }
} binding_cache;
