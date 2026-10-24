// Included inside MetalRenderer::Impl. Draw-only encoder state cache.
// Inline user data and draw constants use exact copied-byte keys;
// diagnostic buffers, tessellation inputs and depth bounds remain unconditional.
// A command buffer retaining references keeps raw identity keys alive until its
// encoder ends; every encoder boundary advances the epoch.
struct DrawBindingCache {
  struct Key {
    const void* object=nullptr;
    u64 epoch=0;
  };
  struct Counts {
    u64 intended=0,emitted=0,skipped=0;
  };
  struct InlineKey {
    std::array<unsigned char,64> bytes{};
    u64 epoch=0;
    u32 size=0;
  };
  bool enabled=[] {
    const char* value=std::getenv("BB_METAL_BINDING_CACHE");
    return value && (!std::strcmp(value,"1") || !std::strcmp(value,"2"));
  }();
  bool inline_enabled=[] {
    const char* value=std::getenv("BB_METAL_BINDING_CACHE");
    return value && !std::strcmp(value,"2");
  }();
  bool active=false;
  u64 epoch=0;
  // Metal permits 128 textures and 16 samplers per stage. Unrecognized slots
  // always bind, including fragment texture30 written by depth-bounds setup.
  std::array<std::array<Key,3>,2> buffers{};
  std::array<std::array<Key,128>,2> textures{};
  std::array<std::array<Key,16>,2> samplers{};
  // Only these slots are set exclusively through Bytes in Draw. Other inline
  // bindings can be overwritten by diagnostic or primitive-specific paths.
  std::array<InlineKey,3> inline_data{};
  Counts buffer_counts,texture_counts,sampler_counts,inline_counts;

  void Reset(bool retained=false) {
    active=enabled && retained;
    if (!enabled) return;
    if (++epoch==0) {
      buffers={};textures={};samplers={};inline_data={};
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
  bool Bytes(bool fragment,u32 slot,const void* data,u32 size) {
    if(!enabled || !inline_enabled)return true;
    ++inline_counts.intended;
    InlineKey* key=slot==Shader::Binding::UserData ? &inline_data[fragment] :
        !fragment && slot==Shader::Binding::Draw ? &inline_data[2] : nullptr;
    if(active && key && data && size && size<=key->bytes.size()) {
      // The pointer is not an identity: stack addresses are reused for changed
      // user data, and distinct sources can hold the same exact Metal payload.
      if(key->epoch==epoch && key->size==size && !std::memcmp(key->bytes.data(),data,size)) {
        ++inline_counts.skipped;
        return false;
      }
      std::memcpy(key->bytes.data(),data,size);
      key->size=size;
      key->epoch=epoch;
    } else if(key) {
      // An unconditional/unsupported write still replaces this slot's encoder
      // state. It must not leave an older eligible payload reusable afterward.
      key->epoch=0;
    }
    ++inline_counts.emitted;
    return true;
  }
} binding_cache;
