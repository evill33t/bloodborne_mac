#include "gpu/metal/mtl_renderer.h"

#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/log.h"
#include "common/content_hash.h"
#include "gpu/metal/parallel_chunk_hash.h"
#include "gpu/metal/constant_memory_map.h"
#include "core/memory.h"
#include "gpu/liverpool/pm4.h"
#include "gpu/liverpool/registers.h"
#include "gpu/liverpool/tiling.h"
#include "gpu/metal/mtl_compiler.h"
#include "gpu/metal/mtl_device.h"
#include "gpu/metal/mtl_formats.h"
#include "gpu/shader/msl.h"
#include "gpu/shader/msl_prelude.h"
#include "gpu/shader/program.h"
#include "gpu/metal/raw_texture_snapshot.h"
#include "gpu/metal/texture_cache_policy.h"
#include "gpu/metal/temporal_aa.h"
#include "gpu/metal/frame_generation.h"
#include "gpu/metal/native_motion_capture.h"
#include "gpu/metal/stutter_trace.h"
#include "gpu/metal/command_timeline.h"
#include "gpu/metal/command_budget.h"
#include "gpu/metal/stage_counter_timeline.h"
#include "gpu/metal/native_tessellation.h"
#include "gpu/metal/snapshot_arena.h"
#include "gpu/metal/static_smrd.h"
#include "gpu/metal/wind_capture_contract.h"
#include "gpu/metal/wind_consumed_contract.h"
#include "gpu/metal/ps_root_read.h"
#include "gpu/metal/gpu_texture_upload.h"
#include "gpu/metal/native_vertex_fetch.h"
#include "gpu/metal/bound_vertex_fetch.h"
#include "gpu/metal/direct_root_span.h"
#include "gpu/metal/direct_root_read.h"
#include "gpu/metal/native_indirect.h"
#include "gpu/metal/guest_writer_prune_epoch.h"
#include "gpu/metal/native_writer_proof.h"
#include "gpu/metal/native_writer_offsets.h"
#include "gpu/metal/native_worklist_extent.h"
#include "gpu/metal/texture_write_journal.h"
#include "gpu/metal/world_frame_queue.h"

namespace Gpu::Metal {

using namespace Liverpool;
using Shader::OutType;
using Shader::Program;
using Shader::Stage;
using Shader::TexDim;
using Shader::TexSample;
using Shader::TextureType;

// Match dc_pack's float32 UNORM8 conversion, including ties to even. This is
// deliberately limited to the four-byte typed format observed in scene fills.
std::optional<u32> PackedRgba8Fill(u32 format,u32 number,u32 stride,const u32* words) {
  if (format!=10 || number!=NumFormat::Unorm || stride!=4) return std::nullopt;
  u32 packed=0;
  for (u32 k=0;k<4;++k) {
    const float value=std::bit_cast<float>(words[k]);
    if (!std::isfinite(value)) return std::nullopt;
    const float scaled=std::clamp(value,0.f,1.f)*255.f;
    u32 channel=static_cast<u32>(scaled);
    const float fraction=scaled-float(channel);
    if (fraction>.5f || (fraction==.5f && (channel&1))) ++channel;
    packed|=channel<<(8*k);
  }
  return packed;
}

namespace {

constexpr u64 kUploadChunk = 4ull << 20;
constexpr u64 kGpuAddrMask =
    (1ull << 40) - 1; // Liverpool virtual addresses are 40 bits
constexpr u32 kNopSizeHint = 0xC0001000; // one-dword NOP carrying a target size

u64 Fnv(const void *data, size_t size, u64 h = 0xcbf29ce484222325ull) {
  const auto *p = static_cast<const u8 *>(data);
  for (size_t i = 0; i < size; ++i) {
    h ^= p[i];
    h *= 0x100000001b3ull;
  }
  return h;
}

std::string Hex(u64 v) {
  char b[24];
  std::snprintf(b, sizeof(b), "%#llx", static_cast<unsigned long long>(v));
  return b;
}

template <typename T> u64 Hash(const T &v, u64 seed = 0xcbf29ce484222325ull) {
  return Fnv(&v, sizeof(T), seed);
}

bool Readable(u64 addr, u64 size) {
  Core::Memory::Vma vma;
  return addr && Core::Memory::Query(addr, false, &vma) &&
         addr + size <= vma.start + vma.size;
}

// Scissor registers: x in [14:0], y in [30:16].
struct Rect {
  s32 x0, y0, x1, y1;
};
Rect ScissorReg(u32 tl, u32 br) {
  return {static_cast<s32>(tl & 0x7FFF), static_cast<s32>((tl >> 16) & 0x7FFF),
          static_cast<s32>(br & 0x7FFF), static_cast<s32>((br >> 16) & 0x7FFF)};
}
Rect Intersect(Rect a, Rect b) {
  return {std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1),
          std::min(a.y1, b.y1)};
}

float Half(u32 h) {
  const u32 e = (h >> 10) & 0x1F, m = h & 0x3FF;
  const float v = e == 0    ? std::ldexp(static_cast<float>(m), -24)
                  : e == 31 ? (m ? NAN : INFINITY)
                            : std::ldexp(static_cast<float>(m | 0x400),
                                         static_cast<int>(e) - 25);
  return (h & 0x8000) ? -v : v;
}

// Unsigned small float with a 5-bit exponent and `mbits` of mantissa
// (R11G11B10).
float SmallFloat(u32 v, u32 mbits) {
  const u32 e = (v >> mbits) & 0x1F, m = v & ((1u << mbits) - 1);
  if (e == 0) {
    return std::ldexp(static_cast<float>(m), -14 - static_cast<int>(mbits));
  }
  return e == 31
             ? INFINITY
             : std::ldexp(static_cast<float>(m | (1u << mbits)),
                          static_cast<int>(e) - 15 - static_cast<int>(mbits));
}

// CB_COLOR_CLEAR_WORD0/1 (the target's packed format) as a Metal clear color.
MTLClearColor ClearColor(u32 info, u32 w0, u32 w1) {
  const u32 fmt = ColorInfo::Format(info), nt = ColorInfo::NumberType(info);
  const bool snorm = nt == NumFormat::Snorm,
             integer = nt == NumFormat::Uint || nt == NumFormat::Sint;
  auto n8 = [&](u32 v) {
    return integer ? double(v & 0xFF)
           : snorm ? std::max(-1.0, s8(v) / 127.0)
                   : (v & 0xFF) / 255.0;
  };
  auto n16 = [&](u32 v) {
    v &= 0xFFFF;
    return nt == NumFormat::Float ? double(Half(v))
           : integer              ? double(v)
           : snorm                ? std::max(-1.0, s16(v) / 32767.0)
                                  : v / 65535.0;
  };
  auto f32 = [&](u32 v) {
    float f;
    std::memcpy(&f, &v, 4);
    return integer ? double(v) : double(f);
  };
  double c[4] = {0, 0, 0, integer ? 1.0 : 1.0};
  switch (fmt) {
  case ColorFormat::C8:
    c[0] = n8(w0);
    break;
  case ColorFormat::C8_8:
    c[0] = n8(w0);
    c[1] = n8(w0 >> 8);
    break;
  case ColorFormat::C8_8_8_8:
    for (u32 k = 0; k < 4; ++k) {
      c[k] = n8(w0 >> (8 * k));
    }
    if (ColorInfo::CompSwap(info) == 1) { // BGRA in memory
      std::swap(c[0], c[2]);
    }
    break;
  case ColorFormat::C16:
    c[0] = n16(w0);
    break;
  case ColorFormat::C16_16:
    c[0] = n16(w0);
    c[1] = n16(w0 >> 16);
    break;
  case ColorFormat::C16_16_16_16:
    c[0] = n16(w0);
    c[1] = n16(w0 >> 16);
    c[2] = n16(w1);
    c[3] = n16(w1 >> 16);
    break;
  case ColorFormat::C32:
    c[0] = f32(w0);
    break;
  case ColorFormat::C32_32:
    c[0] = f32(w0);
    c[1] = f32(w1);
    break;
  case ColorFormat::C10_11_11:
    c[0] = SmallFloat(w0 & 0x7FF, 6);
    c[1] = SmallFloat((w0 >> 11) & 0x7FF, 6);
    c[2] = SmallFloat(w0 >> 22, 5);
    break;
  case ColorFormat::C11_11_10:
    c[0] = SmallFloat(w0 & 0x3FF, 5);
    c[1] = SmallFloat((w0 >> 10) & 0x7FF, 6);
    c[2] = SmallFloat(w0 >> 21, 6);
    break;
  case ColorFormat::C2_10_10_10:
    c[0] = (w0 & 0x3FF) / 1023.0;
    c[1] = ((w0 >> 10) & 0x3FF) / 1023.0;
    c[2] = ((w0 >> 20) & 0x3FF) / 1023.0;
    c[3] = (w0 >> 30) / 3.0;
    break;
  default:
    break;
  }
  return MTLClearColorMake(c[0], c[1], c[2], c[3]);
}

struct PipelineKey {
  u64 vs = 0;
  u64 ps = 0;
  u32 color_fmt[8] = {};
  u32 blend[8] = {};
  u32 write_mask = 0;
  u32 depth_fmt = 0;
  u32 shader_mask = 0;
  u32 reserved = 0; // bbport: keep the raw-byte pipeline hash free of padding.
};

constexpr const char *kCopyShader = R"(
#include <metal_stdlib>
using namespace metal;
struct V { float4 pos [[position]]; float2 uv; };
vertex V copy_vs(uint vid [[vertex_id]]) {
    V o;
    o.uv = float2((vid << 1) & 2, vid & 2);
    o.pos = float4(o.uv.x * 2.0 - 1.0, 1.0 - o.uv.y * 2.0, 0.0, 1.0);
    return o;
}
fragment float4 debug_fs() { return float4(1.0, 0.0, 1.0, 1.0); }
fragment float4 dump_fs(V in [[stage_in]], texture2d<float> src [[texture(0)]], constant float2& opt [[buffer(1)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    float3 c = (opt.x != 0.0 ? float3(src.sample(s, in.uv).a) * 0.5 : src.sample(s, in.uv).rgb) * opt.y;
    if (any(isnan(c)) || any(isinf(c))) return float4(1.0, 0.0, 1.0, 1.0);
    return float4(max(c, 0.0) / (1.0 + max(c, 0.0)) * 2.0, 1.0);
}
kernel void fill32(device uint* dst [[buffer(0)]], constant uint2& vc [[buffer(1)]],
                   uint i [[thread_position_in_grid]]) {
    if (i < vc.y) dst[i] = vc.x;
}
kernel void detile32(device const uint* src [[buffer(0)]], device const uint* offsets [[buffer(1)]],
                     device uint* dst [[buffer(2)]], constant uint3& layout [[buffer(3)]],
                     uint i [[thread_position_in_grid]]) {
    if (i < layout.x * layout.y)
        dst[(i / layout.x) * layout.z + i % layout.x] = src[offsets[i]];
}
fragment float4 copy_fs(V in [[stage_in]], texture2d<float> src [[texture(0)]],
                        constant float2& scale [[buffer(0)]]) {
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    return float4(src.sample(s, in.uv * scale).rgb, 1.0);
}
)";

} // namespace

struct MetalRenderer::Impl {
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> queue = nil;
  std::recursive_mutex lock;

  // ---- guest memory view ----
  id<MTLBuffer> mem[2] = {nil, nil};
  u64 mem_split = 0;
  id<MTLBuffer> mem_map = nil;
  u64 mem_map_gen = ~0ull;
  std::vector<Core::Memory::DirectRange> mem_ranges; // sorted by start
  id<MTLComputePipelineState> fill_pipeline = nil;
  id<MTLComputePipelineState> detile_pipeline = nil;
  std::unordered_map<u64,id<MTLBuffer>> detile_maps;

  // ---- recording ----
  id<MTLCommandBuffer> cmd = nil;
  id<MTLCommandBuffer> last_committed = nil;
  std::shared_ptr<CommandTimeline::Log> command_timeline=CommandTimeline::Open();
  double command_created=0;
  std::shared_ptr<StageCounterTimeline::Log> stage_counter_log;
  StageCounterTimeline::ClockPair stage_counter_clock;
  std::optional<StageCounterTimeline::Pass> stage_counter_pass;
  std::vector<StageCounterTimeline::Pass> stage_counter_passes;
  id<MTLRenderCommandEncoder> enc = nil;
  struct PassState {
    u64 color[8] = {};
    u32 slice[8] = {};
    u64 depth = 0;
    bool operator==(const PassState &) const = default;
  } pass;
  u32 pass_width = 0, pass_height = 0;
  // Completed upload storage can be reused; an in-flight command retains
  // its own chunks. Bound idle storage so a large scene does not pin its peak.
  struct UploadPool {
    std::mutex mutex;
    std::vector<id<MTLBuffer>> free;
    u64 bytes=0;
    static constexpr u64 limit=512ull<<20;
    id<MTLBuffer> Take(u64 size) {
      std::scoped_lock lock(mutex);
      auto best=free.end();
      for (auto it=free.begin();it!=free.end();++it)
        if ((*it).length>=size && (*it).length<=size*2 &&
            (best==free.end() || (*it).length<(*best).length)) best=it;
      if (best==free.end()) return nil;
      id<MTLBuffer> result=*best;
      bytes-=result.length;free.erase(best);return result;
    }
    void Return(std::vector<id<MTLBuffer>>& buffers) {
      std::scoped_lock lock(mutex);
      for (auto buffer:buffers) if (buffer.length<=limit-bytes) {
        bytes+=buffer.length;free.push_back(buffer);
      }
      buffers.clear();
    }
  };
  std::shared_ptr<UploadPool> upload_pool=std::make_shared<UploadPool>();
  std::atomic<u64> upload_allocations{0};
  std::vector<id<MTLBuffer>> chunks;
  id<MTLBuffer> chunk = nil;
  std::shared_ptr<Gpu::ExecutionState> execution;
  u64 submission_id=0,last_vs=0,last_ps=0,last_cs=0;
  u64 chunk_off = 0;
  u32 work_since_commit = 0; // draws + dispatches in the open command buffer
  const u32 command_budget_us=CommandBudget::Parse(std::getenv("BB_METAL_COMMAND_BUDGET_US"));
  u64 command_budget_commits=0;

  // Signals: an event value encoded between passes, so fences do not cut
  // command buffers. Callbacks run in order on the listener's queue.
  id<MTLSharedEvent> signal_event = nil;
  MTLSharedEventListener *signal_listener = nil;
  u64 signal_value = 0;
  struct SignalEffect {
    enum class Kind {Unknown,NoDirectMemoryWrite,PhysicalWrite};
    Kind kind=Kind::Unknown;
    std::vector<NativeIndirect::Range> physical;
    u64 mapping_generation=0;
  };
  struct SignalCallback {
    u64 value;
    std::function<void()> done;
    SignalEffect effect;
  };
  struct Signals {
    std::mutex lock;
    std::deque<SignalCallback> queue;
    std::condition_variable changed;
    std::size_t active=0;
    // The listener dispatch queue is serial. Moving the popped effect here
    // under lock prevents an admission gap before the callback's actual store.
    std::optional<SignalEffect> active_effect;
    std::array<u64,3> enqueued_effects{};
    bool closed=false;
  };
  std::shared_ptr<Signals> signals=std::make_shared<Signals>();

  // ---- caches ----
  // A program is reused only while its whole code (and fetch subroutine) is
  // unchanged: streaming puts other shaders at freed addresses, and GCN
  // programs often share their first instructions (shadPS4 keys shaders by a
  // hash of all of the code).
  struct ShaderEntry {
    std::unique_ptr<Program> prog;
    std::vector<u32> code;
    std::vector<u32> fetch;
  };
  std::unordered_map<u64, std::vector<ShaderEntry>> shaders;
  std::unordered_map<u64, id<MTLRenderPipelineState>> pipelines;

  // Preserve first-use draws while the compiler workers build a cold pipeline.
  std::unique_ptr<ShaderCompiler> compiler;
  std::unordered_set<u64> pending;
  std::mutex done_lock;
  std::condition_variable done_cv;
  std::vector<std::pair<u64, id<MTLRenderPipelineState>>> done_render;
  std::vector<std::pair<u64, id<MTLComputePipelineState>>> done_compute;
  std::unordered_map<u64, id<MTLDepthStencilState>> depth_states;
  std::unordered_map<u64, id<MTLSamplerState>> samplers;
  std::atomic<unsigned> anisotropy{16};
  std::atomic<bool> temporal_aa{false};

  // Guest surfaces rendered on the host GPU stay there. The same address can
  // be bound with different sizes or formats (aliased memory); each variant
  // keeps its own texture, and readers get the one written last.
  struct RenderTarget {
    id<MTLTexture> tex = nil;
    u64 addr = 0;
    u32 width = 0, height = 0;
    u32 depth_or_layers = 1;
    TexDim dim = TexDim::Tex2D;
    bool initialized = false;
    id<MTLTexture> bounds_copy = nil;
    // Content changes, independent of render-pass/attachment changes. A
    // read-only lighting pass must not invalidate the cached depth copy.
    u64 depth_revision = 0;
    u64 bounds_seq = 0;
    bool clear_pending = false; // fast-cleared (CMASK/HTILE): next pass clears
    MTLClearColor clear_color{};
    double clear_depth = 1.0;
    u32 clear_stencil = 0;
    u64 written = 0;    // write_seq of the last pass/dispatch writing it
    struct NativeVelocity {
      u64 frame=~0ull,generation=0;
      std::array<float,2> uv_scale{};
      bool safe=false,produced=false;
    } velocity;
    u32 bind_slice = 0; // array slice the CB registers point at (CB_COLOR_VIEW)
    u32 bind_level = 0; // mip level, when the CB address is a level of this one
    u32 levels = 1; // mip levels (compute shaders build mip chains in place)
    std::unordered_map<u64, id<MTLTexture>>
        views; // (base, count, format) -> view
  };
  using TargetMap =
      std::unordered_map<u64, std::vector<std::unique_ptr<RenderTarget>>>;
  TargetMap color_targets; // by address
  TargetMap depth_targets;
  std::unique_ptr<TemporalAA> temporal;
  std::atomic<bool> frame_generation_enabled{false};
  std::unique_ptr<FrameGeneration> frame_generation;
  std::shared_ptr<const GeneratedFrame> generated_frame;
  std::atomic<const char*> frame_generation_status{"Off"};
  bool frame_generation_hud_pending=false;
  RenderTarget *temporal_depth=nullptr,*temporal_color=nullptr;
  bool temporal_resolved=false;
  std::atomic<bool> temporal_active{false};
  std::optional<SceneCamera> scene_camera;
  u64 scene_camera_frame=~0ull;
  u64 write_seq = 0;

  // Fast-clear metadata surfaces (CMASK / HTILE) -> the surface they describe.
  struct Meta {
    u64 target = 0;
    bool depth = false;
  };
  std::unordered_map<u64, Meta> metas;

  // Render targets whose address is a mip level of a texture's memory (a T#
  // with levels): the game renders straight into that level, e.g. the 256x256
  // cubes of level 1 of the 512x512 environment cube array. Like shadPS4's
  // ImageInfo::MipOf, such a target is that level of the larger image.
  struct MipAlias {
    u64 parent = 0;
    u32 level = 0;
    u32 width = 0, height = 0;
    MTLPixelFormat fmt = MTLPixelFormatInvalid;
  };
  std::unordered_map<u64, MipAlias> mip_alias; // by level address
  u64 resolving_shader = 0; // shader whose resources are being resolved (logs)
  std::vector<std::unique_ptr<RenderTarget>> retired; // merged into a parent

  // Render target sizes the Gnm driver hides in a NOP after the CB/DB
  // register writes (width | height << 16); 0 when absent.
  u32 cb_extent[8] = {};
  u32 db_extent = 0;

  // Guest textures are re-uploaded when their memory changes. Each keeps
  // hashes of its 64 KB chunks; a texture that never changed has one chunk
  // checked per frame in turn, one that did (glyph caches, streamed or
  // CPU-animated textures) is checked in full every frame it is used.
  static constexpr u64 kChunk = ContentHash::kChunkBytes;
  struct TextureEntry {
    id<MTLTexture> tex = nil;
    id<MTLTexture> base_view = nil; // levels from the T# BASE_LEVEL
    std::vector<u64> chunks;
    bool hash_baseline_valid=true;
    u32 next_chunk = 0;
    bool hot = false;
    u64 frame = 0;
    u64 mapping_generation = ~0ull;
    u64 source_write_epoch=0,source_range_generation=~0ull;
    std::vector<NativeIndirect::Range> source_physical;
    bool source_physical_complete=false;
    u64 addr = 0;
    u64 size = 0;
    double last_used = 0;
    u64 allocated_bytes = 0;
    u64 version = 0;
  };
  std::unordered_map<u64, TextureEntry> textures;
  FrameWork::State frame_work;
  double next_texture_collection = 0;
  u64 texture_budget = 0;
  u64 cached_texture_bytes = 0,texture_version = 0;
  std::vector<TextureRetirementTicket> texture_retirement;
  std::size_t texture_retirement_cursor = 0;
  bool bounded_texture_retirement = !std::getenv("BB_METAL_BOUNDED_TEXTURE_RETIRE") ||
      std::strcmp(std::getenv("BB_METAL_BOUNDED_TEXTURE_RETIRE"),"0")!=0;
  bool texture_retire_diagnostics = std::getenv("BB_METAL_TEXTURE_RETIRE_DIAGNOSTICS") &&
      std::strcmp(std::getenv("BB_METAL_TEXTURE_RETIRE_DIAGNOSTICS"),"1")==0;
  void CollectTextures() {
    const double now=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const bool discover=now>=next_texture_collection;
    if (!discover && (!bounded_texture_retirement || texture_retirement_cursor>=texture_retirement.size())) return;
    StutterTrace::Scope timing("texture_collection",frame_count,textures.size(),cached_texture_bytes);
    if (!texture_budget) {
      texture_budget=std::clamp<u64>(device.recommendedMaxWorkingSetSize/8,256ull<<20,2ull<<30);
      if (const char* env=std::getenv("BB_METAL_TEXTURE_CACHE_MB")) {
        char* end=nullptr; const u64 mb=std::strtoull(env,&end,10);
        if (end!=env && !*end && mb>=64 && mb<=16384) texture_budget=mb<<20;
      }
    }
    std::vector<u64> expired;
    if(discover) {
      next_texture_collection=now+1;
      std::vector<CachedTexture> candidates;
      {
        StutterTrace::Scope scan("texture_collection_scan",frame_count,textures.size(),cached_texture_bytes,
            texture_retire_diagnostics ? 0 : 10);
        candidates.reserve(textures.size());
        for(const auto& [key,e]:textures)candidates.push_back({key,e.allocated_bytes,e.last_used,e.version});
      }
      {
        StutterTrace::Scope policy("texture_collection_policy",frame_count,candidates.size(),cached_texture_bytes,
            texture_retire_diagnostics ? 0 : 10);
        expired=ExpiredTextures(std::move(candidates),texture_budget,now);
        if(bounded_texture_retirement) {
          // Refresh the metadata-only queue from the current oldest-first
          // policy result. No resource gains an additional strong reference.
          texture_retirement.clear();texture_retirement_cursor=0;
          texture_retirement.reserve(expired.size());
          for(u64 key:expired) {
            const auto& e=textures.at(key);
            texture_retirement.push_back({key,e.version,e.last_used});
          }
        }
      }
    }
    if(bounded_texture_retirement && texture_retirement_cursor>=texture_retirement.size())return;
    if(!bounded_texture_retirement && expired.empty())return;
    const double retire_start=StutterTrace::Output() ? StutterTrace::Now() : 0;
    if(bounded_texture_retirement) {
      expired=TextureRetirementBatch(texture_retirement,texture_retirement_cursor,
          [&](u64 key)->std::optional<CachedTexture> {
            const auto found=textures.find(key);if(found==textures.end())return {};
            const auto& e=found->second;
            return CachedTexture{key,e.allocated_bytes,e.last_used,e.version};
          },texture_budget,cached_texture_bytes,now);
    }
    u64 retired_bytes=0;
    for(u64 key:expired) {
      const auto found=textures.find(key);if(found==textures.end())continue;
      retired_bytes+=found->second.allocated_bytes;
      cached_texture_bytes-=found->second.allocated_bytes;
      textures.erase(found);
    }
    if(retire_start)StutterTrace::Record("texture_collection_retire",retire_start,StutterTrace::Now(),
        frame_count,expired.size(),retired_bytes,texture_retire_diagnostics ? 0 : 10);
    if(bounded_texture_retirement && texture_retirement_cursor>=texture_retirement.size()) {
      texture_retirement.clear();texture_retirement_cursor=0;
    }
  }
  std::unordered_map<u32, id<MTLTexture>> dummy_textures;
  id<MTLSamplerState> default_sampler = nil;
  id<MTLBuffer> quad_indices = nil;
  id<MTLBuffer> gds = nil; // 64 KB global data share

  // ---- constant-engine snapshots ----
  // The CE dumps resource tables into a small ring that the guest rewrites
  // while draws that read it may still be queued on the host GPU. A draw
  // whose user data points into a dump gets a pointer to a copy in `arena`
  // (guest memory the renderer owns) taken when the draw is recorded.
  struct ConstDump {
    u32 bytes = 0;
    u64 gen = 0;
  };
  std::map<u64, ConstDump> const_dumps; // by guest address
  u64 dump_gen = 0;
  struct Snapshot {
    u64 gen = 0;
    u64 va = 0;
    u64 lease = SnapshotArena::NoLease;
  };
  std::unordered_map<u64, Snapshot>
      snapshots; // dump address -> its latest copy
  static constexpr u64 kArenaSize = 16ull << 20;
  u64 arena_va = 0;
  u64 arena_head = 0; // bytes handed out so far (wraps modulo kArenaSize)
  std::atomic<u64> arena_released{0}; // head values the GPU is done with
  u64 arena_commit_head = 0; // head when the last command buffer was committed
  u64 arena_pin = SnapshotArena::NoLease; // prepared but not yet bound stage data

  // ---- presentation ----
  id<MTLRenderPipelineState> copy_pipeline = nil;
  id<MTLRenderPipelineState> dump_pipeline =
      nil; // DC_DUMP_PASSES: x / (1 + x), NaN/Inf magenta
  id<MTLTexture> frames[3] = {nil, nil, nil};
  u32 frame_index = 0;
  std::atomic<void *> latest_frame{nullptr};
  std::atomic<u64> flipped_frames{0}; // main-thread FPS sampling
  std::atomic<u64> depth_bounds_copies{0};
  std::function<void(void*)> frame_ready;
  u64 frame_count = 0;

  // ---- diagnostics ----
  const char *dump_dir = nullptr;
  const char *dump_frames = nullptr; // DC_DUMP_FRAMES=dir: flips as PPMs
  u64 dump_every = 1;                // n-th flip; 0 captures only requested traces
  bool debug_clear = false; // DC_DEBUG_CLEAR: new targets start dark blue
  u64 dump_texture =
      0; // DC_DUMP_TEXTURE=<addr>: write its uploads next to the frame dumps
  u64 debug_vsout = 0; // DC_DEBUG_VSOUT=<vs address>: dump its first positions
  id<MTLBuffer> debug_vs_buf = nil;
  id<MTLFunction> debug_fs =
      nil; // DC_DEBUG_FLAT: every pixel shader outputs magenta
  bool trace = false;
  u64 trace_frame = 0; // DC_TRACE_FRAME=n: DC_GPU_TRACE for flip n only
  const char *trace_file = nullptr; // bbport: changing the token requests one live frame
  u64 trace_token = 0;
  u64 dump_ps = 0; // optional second hex word in DC_TRACE_FILE: PS input textures
  u64 trace_on_ps = 0; // `token ps !`: wait for that draw instead of tracing an arbitrary frame
  bool triggered_trace = false, dump_selected_outputs = false;
  bool dump_passes =
      false; // DC_DUMP_PASSES: with DC_TRACE_FRAME, every pass's targets
  struct PassDump {
    id<MTLBuffer> buf;
    u32 w, h;
    std::string name;
    bool raw_rgba = false;
  };
  std::vector<PassDump> pass_dumps;
  std::vector<u64> peek_addrs; // DC_PEEK_CS=<cs>: its V# buffers, printed after
                               // the traced frame
  // DC_PROBE=<shader>:<pc>:<x>,<y>: registers of one fragment (compute: group
  // x, thread y) before instruction pc, for every draw/dispatch of the traced
  // frame using that shader.
  struct Probe {
    u64 shader = 0;
    u32 pc = ~0u, x = 0, y = 0;
  } probe;
  id<MTLBuffer> probe_buf = nil;
  std::vector<std::string>
      probe_slots; // what each slot of probe_buf belongs to
  static constexpr u32 kProbeSlots = 4096;
  id<MTLTexture> pass_tex[9] = {};
  u64 pass_addr[9] = {};
  bool stats = false;   // DC_GPU_STATS: per-second GPU time and submit counts
  bool profile = false; // DC_GPU_PROFILE: GPU time per shader (one command
                        // buffer per call)
  bool profile_passes = false; // DC_GPU_PROFILE=passes: less intrusive per-pass timing
  bool profile_trace_only = false; // DC_GPU_PROFILE=trace: only requested trace frames
  bool Profiling() const { return profile && (!profile_trace_only || trace); }
  bool fast_typed_fills = true;
  bool native_tessellation = true;
  bool native_tessellation_extended=[] {
    const char* value=std::getenv("BB_METAL_NATIVE_TESSELLATION_EXTENDED");
    return value && !std::strcmp(value,"1");
  }();
  bool native_tessellation_inner_two=[] {
    const char* value=std::getenv("BB_METAL_NATIVE_TESSELLATION_INNER_TWO");
    return value && !std::strcmp(value,"1");
  }();
  bool opaque_exports = true;
  std::unique_ptr<GpuTextureUploader> texture_uploader;
  u64 gpu_texture_uploads=0,gpu_texture_upload_bytes=0;
  bool gpu_written_texture_snapshot=std::getenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT") &&
      !std::strcmp(std::getenv("BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT"),"1");
  u64 texture_gpu_snapshots=0,texture_gpu_snapshot_history_versions=0;
  std::array<u64,13> texture_gpu_snapshot_rejects{};
  bool static_smrd_enabled=std::getenv("BB_METAL_STATIC_SMRD") && std::strcmp(std::getenv("BB_METAL_STATIC_SMRD"),"1")==0;
  bool direct_vertex_root_enabled=std::getenv("BB_METAL_DIRECT_VERTEX_ROOT") &&
      !std::strcmp(std::getenv("BB_METAL_DIRECT_VERTEX_ROOT"),"1");
  u64 direct_vertex_root_draws=0,direct_vertex_root_hits=0,direct_vertex_root_fallbacks=0;
  std::array<u64,unsigned(DirectRootRead::Reject::Count)> direct_vertex_root_rejects{};
  bool direct_smrd_enabled=std::getenv("BB_METAL_DIRECT_SMRD") && std::strcmp(std::getenv("BB_METAL_DIRECT_SMRD"),"1")==0;
  bool simd_smrd_enabled=std::getenv("BB_METAL_SIMD_SMRD") && std::strcmp(std::getenv("BB_METAL_SIMD_SMRD"),"1")==0;
  bool native_vertex_fetch_enabled=!std::getenv("BB_METAL_NATIVE_VERTEX_FETCH") || std::strcmp(std::getenv("BB_METAL_NATIVE_VERTEX_FETCH"),"0")!=0;
  // The four additional identities pass GPU equivalence checks, but their
  // gameplay throughput is not repeatable yet. Keep them opt-in.
  bool native_vertex_fetch_extended_enabled =
      std::getenv("BB_METAL_NATIVE_VERTEX_FETCH_EXTENDED") &&
      std::strcmp(std::getenv("BB_METAL_NATIVE_VERTEX_FETCH_EXTENDED"),"1")==0;
  std::unordered_map<u64,bool> native_vertex_fetch_programs;
  u64 native_vertex_fetch_draws=0;
  u64 native_vertex_fetch_extended_draws=0;
  // Admission measurement only. No shader option, pointer binding or GPU
  // read lease is changed until actual owned V# coverage is established.
  bool bound_vertex_fetch_audit=std::getenv("BB_METAL_BOUND_VERTEX_FETCH_AUDIT") &&
      !std::strcmp(std::getenv("BB_METAL_BOUND_VERTEX_FETCH_AUDIT"),"1");
  enum class BoundReject : u32 { Context,Shape,Guard,DumpMissing,DumpBounds,DumpPending,
      MapChanged,DumpChanged,Snapshot,PayloadMap,PayloadCp,PayloadCallback,Lease,Count };
  std::array<u64,u32(BoundReject::Count)> bound_vertex_rejects{};
  std::array<u64,u32(BoundVertexFetch::Reject::Count)> bound_vertex_snapshot_rejects{};
  std::unordered_set<u64> bound_vertex_diagnostics;
  std::unordered_set<u64> bound_vertex_positive_diagnostics;
  u64 bound_vertex_candidates=0,bound_vertex_source_ready=0,bound_vertex_metadata_ready=0;
  struct BoundVertexSource {u64 address=0,bytes=0,generation=0,map_generation=0,root_offset=0;};
  bool readonly_guest_memory_enabled=std::getenv("BB_METAL_READONLY_GUEST_MEMORY") &&
      std::strcmp(std::getenv("BB_METAL_READONLY_GUEST_MEMORY"),"1")==0;
  bool constant_memory_map_enabled=ConstantMemoryMap::Configured(std::getenv("BB_METAL_CONSTANT_MEMORY_MAP"));
  u64 constant_memory_map_sources=0,constant_memory_map_fallbacks=0;
  std::unordered_map<const Program*,bool> readonly_guest_programs;
  u64 readonly_guest_vs_bindings=0,readonly_guest_ps_bindings=0;
  bool native_indirect_enabled=std::getenv("BB_METAL_NATIVE_INDIRECT") &&
      std::strcmp(std::getenv("BB_METAL_NATIVE_INDIRECT"),"1")==0;
  bool native_texture_coherence=TextureWriteJournal::Enabled(native_indirect_enabled,
      std::getenv("BB_METAL_NATIVE_TEXTURE_COHERENCE"));
  bool native_worklist_extent=TextureWriteJournal::Enabled(native_indirect_enabled,
      std::getenv("BB_METAL_NATIVE_WORKLIST_EXTENT"));
  u64 native_worklist_refined=0,native_worklist_fallbacks=0;
  std::array<u64,7> native_worklist_fallback_reason{};
  TextureWriteJournal texture_write_journal{TextureWriteJournal::ConfiguredLimit(
      std::getenv("BB_METAL_TEXTURE_JOURNAL_RECORDS")),0,
      native_texture_coherence && TextureWriteJournal::PageIndexEnabled(
          std::getenv("BB_METAL_TEXTURE_JOURNAL_PAGE_INDEX")) ? Core::Memory::kDirectSize : 0};
  u64 texture_coherence_checks=0,texture_coherence_refreshes=0,texture_coherence_waits=0;
  WorldFrameQueue world_frames;
  const unsigned world_frame_limit=WorldFrameQueue::ConfiguredLimit();
  id<MTLComputePipelineState> indirect_draw_adapter=nil;
  u64 native_indirect_draws=0,native_indirect_dispatches=0,indirect_cpu_write_waits=0;
  bool indirect_argument_trace=std::getenv("BB_METAL_INDIRECT_ARGUMENT_TRACE") &&
      std::strcmp(std::getenv("BB_METAL_INDIRECT_ARGUMENT_TRACE"),"1")==0;
  std::chrono::steady_clock::time_point indirect_argument_sample[2]{};
  struct IndirectRead {
    NativeIndirect::Range range;
    NativeIndirect::Range physical;
    std::shared_ptr<std::atomic<bool>> completed;
  };
  struct DescriptorRead {
    NativeIndirect::Range range,physical;
  };
  std::vector<IndirectRead> indirect_reads;
  static constexpr std::size_t kIndirectReadLimit=512;
  using CpuReadReady=std::function<bool(GuestAddr,std::size_t)>;
  CpuReadReady cp_cpu_read_ready;
  CpuReadReady active_cpu_read_ready;
  const CpuReadReady& CurrentCpuReadReady() const {
    return active_cpu_read_ready ? active_cpu_read_ready : cp_cpu_read_ready;
  }
  struct ScopedCpuReadReady {
    Impl& owner;
    CpuReadReady previous;
    ScopedCpuReadReady(Impl& owner,CpuReadReady next):owner(owner),
        previous(std::move(owner.active_cpu_read_ready)) {owner.active_cpu_read_ready=std::move(next);}
    ~ScopedCpuReadReady() {owner.active_cpu_read_ready=std::move(previous);}
  };
  struct GuestWriter {
    std::vector<NativeIndirect::Range> physical;
    bool unknown=false;
    std::shared_ptr<std::atomic<bool>> completed;
    u64 submission=0;
  };
  std::vector<GuestWriter> guest_writers;
  GuestWriterPruneEpoch writer_prune{GuestWriterPruneEpoch::ConfiguredEnabled(
      std::getenv("BB_METAL_WRITER_PRUNE_EPOCH"))};
  static constexpr std::size_t kGuestWriterLimit=512;
  u64 indirect_resource_rejects=0,unknown_guest_writers=0;
  bool indirect_read_trace=std::getenv("BB_METAL_INDIRECT_READ_TRACE") &&
      std::strcmp(std::getenv("BB_METAL_INDIRECT_READ_TRACE"),"1")==0;
  std::unordered_set<u64> indirect_read_failures;
  bool indirect_writer_trace=std::getenv("BB_METAL_INDIRECT_WRITER_TRACE") &&
      std::strcmp(std::getenv("BB_METAL_INDIRECT_WRITER_TRACE"),"1")==0;
  std::unordered_map<const Program*,u32> writer_trace_seen;
  std::unordered_map<const Program*,NativeWriter::Proof> native_writer_proofs;
  std::unordered_map<const Program*,std::unordered_map<u64,NativeWriter::OffsetProof>> native_writer_offset_proofs;
  u32 writer_trace_lines=0;
  std::unordered_map<const Program*,bool> static_smrd_programs;
  std::unordered_map<u64,bool> simd_smrd_programs;
  u64 static_smrd_attempts=0,static_smrd_draws=0,static_smrd_rejects=0,static_smrd_bytes=0;
  u64 direct_smrd_draws=0,direct_smrd_roots=0,direct_smrd_fallbacks=0;
  std::array<u64,unsigned(PsRootRead::Reject::Count)> direct_smrd_root_results{};
  std::array<u64,13> direct_smrd_callback_rejects{};
  u64 native_tess_draws=0,translated_tess_draws=0;
  id<MTLBuffer> native_tess_factors[3]{};
  struct ProfileEntry {
    double seconds = 0;
    u64 calls = 0;
  };
  std::mutex profile_lock;
  std::unordered_map<std::string, ProfileEntry> profile_data;
  std::atomic<u64> gpu_ns{0};
  u64 stat_hashed = 0;
  unsigned parallel_texture_hash_workers=ParallelChunkHash::ConfiguredWorkers(
      std::getenv("BB_METAL_PARALLEL_TEXTURE_HASH"));
  bool texture_hash_profile_enabled=parallel_texture_hash_workers ||
      ParallelChunkHash::Histogram::ConfiguredEnabled(std::getenv("BB_METAL_TEXTURE_HASH_PROFILE"));
  ParallelChunkHash::Histogram texture_hash_histogram;
  u64 parallel_texture_hash_invalid=0;

  u64 stat_commits = 0, stat_waits = 0, stat_dispatches = 0, stat_draws = 0;
  u64 stat_depth_copies = 0, stat_upload_base = 0;
  double stat_wait_s = 0;
  std::chrono::steady_clock::time_point stat_t0 =
      std::chrono::steady_clock::now();
  u64 draw_count = 0;
  std::set<std::string> reported;

  void ReportOnce(const std::string &what) {
    if (reported.insert(what).second) {
      DC_WARN("gpu: %s", what.c_str());
    }
  }

  // ================= setup =================

#include "gpu/metal/renderer_capture.inc"
#include "gpu/metal/wind_capture.inc"
#include "gpu/metal/wind_consumed_capture.inc"
#include "gpu/metal/htile_capture.inc"
#include "gpu/metal/native_velocity.inc"
#include "gpu/metal/renderer_binding_cache.inc"

  void Init() {
    stage_counter_log=StageCounterTimeline::Log::Open(device);
    InitCapture();
    InitWindCapture();
    InitWindConsumedCapture();
    // Immutable snapshots and raw GPU detiling avoid the large CPU upload
    // stalls. Unsupported layouts retain the reference uploader; 0 disables
    // this path for diagnostics without changing the game's source files.
    if(const char* option=std::getenv("BB_METAL_GPU_TEXTURE_UPLOAD"); !option || std::strcmp(option,"0")) {
      texture_uploader=std::make_unique<GpuTextureUploader>((__bridge void*)device);
      if(!texture_uploader->Ready()) texture_uploader.reset();
    }
    dump_dir = std::getenv("DC_DUMP_SHADERS");
    dump_frames = std::getenv("DC_DUMP_FRAMES");
    trace_file = std::getenv("DC_TRACE_FILE");
    if (const char *e = std::getenv("DC_DUMP_EVERY")) {
      dump_every = std::strtoull(e, nullptr, 10);
    }
    debug_clear = std::getenv("DC_DEBUG_CLEAR") != nullptr;
    if (const char *v = std::getenv("DC_DEBUG_VSOUT")) {
      debug_vsout = std::strtoull(v, nullptr, 16);
    }
    trace = std::getenv("DC_GPU_TRACE") != nullptr;
    dump_passes = std::getenv("DC_DUMP_PASSES") != nullptr;
    if (const char *e = std::getenv("DC_TRACE_FRAME")) {
      trace_frame = std::strtoull(e, nullptr, 10);
    }
    if (const char *e = std::getenv("DC_DUMP_TEXTURE")) {
      dump_texture = std::strtoull(e, nullptr, 16);
    }
    if (const char *e = std::getenv("DC_PROBE")) {
      char *q = nullptr;
      probe.shader = std::strtoull(e, &q, 16);
      if (!std::strcmp(
              e,
              "*:nan")) { // every pixel shader's first non-finite color export
        probe.shader = ~0ull;
        probe.pc = Shader::kProbeExportNan;
        probe.x = ~0u;
        q = const_cast<char *>(e + std::strlen(e));
      }
      if (*q == ':') {
        probe.pc = std::strtoul(q + 1, &q, 16);
      }
      // '*': any pixel; 'nan<k>': the first whose vk is NaN/Inf
      if (*q == ':' && q[1] == '*') {
        probe.x = ~0u;
      } else if (*q == ':' && !std::strncmp(q + 1, "nan", 3)) {
        probe.x = ~1u;
        probe.y = std::strtoul(q + 4, &q, 10);
      } else {
        probe.x = *q == ':' ? std::strtoul(q + 1, &q, 10) : 0;
      }
      if (*q == ',') {
        probe.y = std::strtoul(q + 1, &q, 10);
      }
      probe_buf =
          [device newBufferWithLength:kProbeSlots * Shader::kProbeWords * 4
                              options:MTLResourceStorageModeShared];
      std::memset(probe_buf.contents, 0, probe_buf.length);
    }
    stats = std::getenv("DC_GPU_STATS") != nullptr;
    profile = std::getenv("DC_GPU_PROFILE") != nullptr;
    profile_trace_only = profile && (!std::strcmp(std::getenv("DC_GPU_PROFILE"),"trace") ||
        !std::strcmp(std::getenv("DC_GPU_PROFILE"),"trace-draws"));
    profile_passes = profile && (!std::strcmp(std::getenv("DC_GPU_PROFILE"),"trace") ||
        !std::strcmp(std::getenv("DC_GPU_PROFILE"),"passes"));
    if (const char* option=std::getenv("BB_METAL_TYPED_FILL")) fast_typed_fills=std::strcmp(option,"0")!=0;
    if (const char* option=std::getenv("BB_METAL_NATIVE_TESSELLATION")) native_tessellation=std::strcmp(option,"0")!=0;
    if (const char* option=std::getenv("BB_METAL_OPAQUE_EXPORTS")) opaque_exports=std::strcmp(option,"0")!=0;
    compiler = std::make_unique<ShaderCompiler>(device, 4,ShaderCompiler::CacheMode::DriverManaged);
    compiler->EnablePrewarming();
    signal_event = [device newSharedEvent];
    signal_listener = [[MTLSharedEventListener alloc]
        initWithDispatchQueue:dispatch_queue_create("gpu signals",
                                                    DISPATCH_QUEUE_SERIAL)];

    void *view = Core::Memory::DirectHostView();
    const u64 total = Core::Memory::kDirectSize;
    mem_split = (total / 2) & ~0xFFFFull;
    if (view) {
      mem[0] = [device newBufferWithBytesNoCopy:view
                                         length:mem_split
                                        options:MTLResourceStorageModeShared
                                    deallocator:nil];
      mem[1] =
          [device newBufferWithBytesNoCopy:static_cast<u8 *>(view) + mem_split
                                    length:total - mem_split
                                   options:MTLResourceStorageModeShared
                               deallocator:nil];
    }
    if (!mem[0] || !mem[1]) {
      DC_ERROR("gpu: cannot expose guest memory to Metal");
    }

    std::vector<u32> quads(0x10000 * 6);
    for (u32 q = 0; q < 0x10000; ++q) {
      const u32 b = q * 4;
      const u32 idx[6] = {b, b + 1, b + 2, b, b + 2, b + 3};
      std::memcpy(&quads[q * 6], idx, sizeof(idx));
    }
    quad_indices = [device newBufferWithBytes:quads.data()
                                       length:quads.size() * 4
                                      options:MTLResourceStorageModeShared];

    gds = [device newBufferWithLength:0x10000
                              options:MTLResourceStorageModeShared];
    std::memset(gds.contents, 0, 0x10000);

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    default_sampler = [device newSamplerStateWithDescriptor:sd];

    NSError *err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(kCopyShader)
                                              options:nil
                                                error:&err];
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    if (std::getenv("DC_DEBUG_FLAT")) {
      debug_fs = [lib newFunctionWithName:@"debug_fs"];
    }
    pd.vertexFunction = [lib newFunctionWithName:@"copy_vs"];
    pd.fragmentFunction = [lib newFunctionWithName:@"copy_fs"];
    pd.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    copy_pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    pd.fragmentFunction = [lib newFunctionWithName:@"dump_fs"];
    dump_pipeline = [device newRenderPipelineStateWithDescriptor:pd error:&err];
    fill_pipeline = [device
        newComputePipelineStateWithFunction:[lib newFunctionWithName:@"fill32"]
                                      error:&err];
    detile_pipeline = [device
        newComputePipelineStateWithFunction:[lib newFunctionWithName:@"detile32"]
                                      error:&err];
    if (!copy_pipeline) {
      DC_ERROR("gpu: copy pipeline: %s", err.localizedDescription.UTF8String);
    }
  }

  // Metal compiles lazily and stalls the GPU thread meanwhile; name the slow
  // ones.
  static void ReportSlowCompile(const char *what, u64 a, u64 b,
                                std::chrono::steady_clock::time_point t0) {
    const double s =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    if (s > 0.5) {
      DC_WARN("gpu: %s %#llx/%#llx took %.1f s to compile", what, a, b, s);
    }
  }

  void UpdateMemMap() {
    const u64 gen = Core::Memory::DirectMapGeneration();
    if (gen == mem_map_gen && mem_map) {
      return;
    }
    StutterTrace::Scope timing("memory_map_refresh",frame_count,gen);
    mem_map_gen = gen;
    mem_ranges = Core::Memory::DirectMappings();
    std::sort(mem_ranges.begin(), mem_ranges.end(),
              [](const auto &x, const auto &y) { return x.start < y.start; });
    const auto &ranges = mem_ranges;
    // Largest page size at which no page holds two mappings.
    u32 shift = Shader::GpuMemMap::kMaxShift;
    for (; shift > Shader::GpuMemMap::kMinShift; --shift) {
      const u64 page = 1ull << shift;
      u64 prev_end = 0;
      bool ok = true;
      for (const auto &r : ranges) {
        ok = ok && (r.start & (page - 1)) == 0 &&
             ((prev_end + page - 1) & ~(page - 1)) <= r.start;
        prev_end = r.start + r.size;
      }
      if (ok) {
        break;
      }
    }
    const u64 page = 1ull << shift;
    const u64 lo = ranges.empty() ? 0 : ranges.front().start & ~(page - 1);
    const u64 hi =
        ranges.empty() ? 0 : ranges.back().start + ranges.back().size;
    const u64 pages = (hi - lo + page - 1) >> shift;
    constexpr u64 header = sizeof(Shader::GpuMemMap) / 4;
    std::vector<s32> table(header + pages, Shader::GpuMemMap::kUnmapped);
    auto &map = *reinterpret_cast<Shader::GpuMemMap *>(table.data());
    map = {lo, pages, mem_split, shift, 0, Core::Memory::kDirectSize};
    for (const auto &r : ranges) {
      if (((r.start ^ r.phys) & ((1ull << Shader::GpuMemMap::kMinShift) - 1)) !=
          0) {
        ReportOnce("direct mapping not 16 KB aligned");
      }
      const s32 delta = static_cast<s32>(
          (static_cast<s64>(r.phys) - static_cast<s64>(r.start)) >>
          Shader::GpuMemMap::kMinShift);
      for (u64 va = r.start & ~(page - 1); va < r.start + r.size; va += page) {
        table[header + ((va - lo) >> shift)] = delta;
      }
    }
    // A fresh buffer: work already recorded keeps reading the old table.
    mem_map = [device newBufferWithBytes:table.data()
                                  length:table.size() * 4
                                 options:MTLResourceStorageModeShared];
  }

  // Guest range [va, va + size) as a range of one of the `mem` buffers.
  bool Locate(u64 va, u64 size, id<MTLBuffer> *buf, u64 *off) {
    UpdateMemMap();
    auto it =
        std::upper_bound(mem_ranges.begin(), mem_ranges.end(), va,
                         [](u64 a, const auto &r) { return a < r.start; });
    if (it == mem_ranges.begin()) {
      return false;
    }
    --it;
    if (va + size > it->start + it->size) {
      return false;
    }
    const u64 phys = it->phys + (va - it->start);
    if (phys + size <= mem_split) {
      *buf = mem[0];
      *off = phys;
    } else if (phys >= mem_split) {
      *buf = mem[1];
      *off = phys - mem_split;
    } else {
      return false;
    }
    return *buf != nil;
  }

  bool LocateIndirect(u64 address,u64 bytes,id<MTLBuffer>* buffer,u64* offset) {
    if(!NativeIndirect::Valid({address,bytes}) ||
        !Locate(address,bytes,buffer,offset) || !*buffer ||
        (*offset&3) || *offset>(*buffer).length || bytes>(*buffer).length-*offset) return false;
    return true;
  }
  bool PhysicalRanges(u64 address,u64 bytes,std::vector<NativeIndirect::Range>& out) {
    if(!bytes || address>UINT64_MAX-bytes) return false;
    UpdateMemMap();
    for(u64 at=address;at<address+bytes;) {
      auto it=std::upper_bound(mem_ranges.begin(),mem_ranges.end(),at,
          [](u64 address,const auto& mapping){return address<mapping.start;});
      if(it==mem_ranges.begin()) return false;
      --it;
      if(at<it->start || at-it->start>=it->size) return false;
      const u64 count=std::min(it->size-(at-it->start),address+bytes-at);
      if(out.size()>=64) return false;
      out.push_back({it->phys+(at-it->start),count});at+=count;
    }
    return true;
  }
  void PruneGuestWriters() {writer_prune.Prune(guest_writers);}
  bool CpuResourceRead(u64 address,u64 bytes,const CpuReadReady& ready={}) {
    const auto reject=[&](u32 reason,const char* label,NativeIndirect::Range writer={},
        NativeIndirect::Range read={}) {
      if(indirect_read_trace && indirect_read_failures.size()<128) {
        const u64 key=Hash(reason,Hash(bytes,Hash(address)));
        if(indirect_read_failures.insert(key).second)
          DC_INFO("gpu indirect read: frame=%llu address=%#llx bytes=%llu reason=%s "
              "writer=%#llx+%llu physical_read=%#llx+%llu vs=%#llx ps=%#llx cs=%#llx",
              frame_count,address,bytes,label,writer.address,writer.bytes,read.address,read.bytes,
              last_vs,last_ps,last_cs);
      }
      return false;
    };
    if(!bytes || address>UINT64_MAX-bytes || !Readable(address,bytes))
      return reject(1,"unreadable-or-invalid");
    if(ready && !ready(address,bytes))return reject(2,"pending-cpu-fence");
    PruneGuestWriters();
    // Unknown closures (including arbitrary IRQ handlers) may change code or
    // descriptors outside their announced label. Check before any CPU read,
    // not only after the texture payload reaches GPU-source admission.
    const bool callback_proof=gpu_written_texture_snapshot && native_indirect_enabled && native_texture_coherence;
    if(guest_writers.empty() && !callback_proof) return true;
    std::vector<NativeIndirect::Range> physical;
    if(!PhysicalRanges(address,bytes,physical)) {
      // Runtime low mappings use private anonymous pages; flexible memory is
      // backed above kDirectSize. Neither aliases the mem0/mem1 pool. Every
      // guest mapping of that pool appears in DirectMappings. Exclude the
      // separate host backing view too, and never allow partial map/gap spans.
      for(const auto& mapping:mem_ranges)
        if(NativeIndirect::Overlap({address,bytes},{mapping.start,mapping.size}))
          return reject(3,"partial-direct-mapping");
      for(id<MTLBuffer> buffer:mem) if(buffer && NativeIndirect::Overlap({address,bytes},
          {reinterpret_cast<u64>(buffer.contents),buffer.length}))return reject(4,"host-backing-view");
      // The negative-alias proof excludes GPU direct-pool writers only. Even
      // NoDirectMemoryWrite callbacks can change private labels/status, and
      // a typed direct-write contract does not exclude other host-private
      // stores. Neither proves an arbitrary private CPU read disjoint.
      if(callback_proof) {
        std::scoped_lock lk(signals->lock);
        if(!signals->queue.empty() || signals->active)return reject(7,"pending-private-callback-effect");
      }
      return true;
    }
    if(callback_proof)for(const auto& range:physical)
      if(SignalSourceBlock(range)!=UINT32_MAX)return reject(7,"pending-callback-effect");
    for(const auto& writer:guest_writers) {
      if(writer.unknown) {
        if(writer_prune.RetainedCompleted(writer.completed,GuestWriterPruneEpoch::Use::CpuReject))continue;
        return reject(5,"unknown-writer");
      }
      // A completion may publish its flag before advancing the shared epoch.
      // Check only a ticket which would otherwise reject this current read.
      const auto readable=[&] {
        for(const auto& a:writer.physical)for(const auto& b:physical)
          if(NativeIndirect::Overlap(a,b)) {
            if(writer_prune.RetainedCompleted(writer.completed,GuestWriterPruneEpoch::Use::CpuReject))return true;
            return reject(6,"physical-writer-overlap",a,b);
          }
        return true;
      };
      if(!readable())return false;
    }
    return true;
  }
  bool ReadDescriptorWord(const Shader::SgprSource& source,const u32* ud,u32& value,
      const CpuReadReady& ready={},std::vector<DescriptorRead>* reads=nullptr,
      bool producer=false) {
    if(source.ud>=16) return false;
    if(source.kind==Shader::SgprSource::UserData) {value=ud[source.ud];return true;}
    // A producer's pointer-loaded V# can itself be changed by an earlier CPU
    // fence callback. Without the CP's read predicate, never infer its extent.
    const auto& predicate=ready ? ready : CurrentCpuReadReady();
    if(producer && !predicate) return false;
    if(source.ud>=15 || (source.kind!=Shader::SgprSource::PointerLoad &&
        source.kind!=Shader::SgprSource::BufferLoad)) return false;
    const u64 base=source.kind==Shader::SgprSource::PointerLoad ?
        (u64(ud[source.ud])|(u64(ud[source.ud+1])<<32))&0xffffffffffffull :
        u64(ud[source.ud])|(u64(ud[source.ud+1]&0xfff)<<32);
    if(base>UINT64_MAX-source.offset) return false;
    const u64 address=base+source.offset;
    // SMRD aligns its GPU word address down. Do not infer an extent from a
    // different unaligned CPU memcpy value or protect the wrong four bytes.
    if(producer && (address&3)) return false;
    if(!CpuResourceRead(address,4,predicate)) return false;
    if(reads) {
      id<MTLBuffer> buffer=nil;u64 offset=0;
      if(!LocateIndirect(address,4,&buffer,&offset)) return false;
      const u64 physical=offset+(buffer==mem[0] ? 0 : mem_split);
      bool collected=false;
      for(auto& read:*reads) {
        if(address>=read.range.address && address-read.range.address<read.range.bytes &&
            physical==read.physical.address+(address-read.range.address)) {collected=true;break;}
        if(address==read.range.address+read.range.bytes &&
            physical==read.physical.address+read.physical.bytes) {
          read.range.bytes+=4;read.physical.bytes+=4;collected=true;break;
        }
      }
      if(!collected) {
        if(reads->size()>=64) return false;
        reads->push_back({{address,4},{physical,4}});
      }
    }
    std::memcpy(&value,reinterpret_cast<const void*>(address),4);return true;
  }
  void TraceProgramWriter(const Program& program,u32 reason,const char* label,u32 pc=0,
      const std::array<Shader::SgprSource,4>* descriptor=nullptr,u64 ranges=0,u64 reads=0) {
    if(!indirect_writer_trace || writer_trace_lines>=1024) return;
    auto& seen=writer_trace_seen[&program];
    if(seen&reason) return;
    seen|=reason;++writer_trace_lines;
    const std::array<Shader::SgprSource,4> absent{};
    const auto& d=descriptor ? *descriptor : absent;
    DC_INFO("gpu indirect writer: program=%#llx hash=%016llx reason=%s pc=%#x ranges=%llu descriptor_reads=%llu "
        "provenance=%u:%u:%#x,%u:%u:%#x,%u:%u:%#x,%u:%u:%#x",
        program.address,program.hash,label,pc,ranges,reads,
        d[0].kind,d[0].ud,d[0].offset,d[1].kind,d[1].ud,d[1].offset,
        d[2].kind,d[2].ud,d[2].offset,d[3].kind,d[3].ud,d[3].offset);
  }
  bool ProtectDescriptorReads(const std::vector<DescriptorRead>& reads) {
    PruneIndirectReads();
    if(indirect_reads.size()>kIndirectReadLimit || reads.size()>kIndirectReadLimit-indirect_reads.size()) return false;
    if(reads.empty()) return true;
    auto completed=std::make_shared<std::atomic<bool>>(false);
    for(const auto& read:reads)indirect_reads.push_back({read.range,read.physical,completed});
    [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {completed->store(true,std::memory_order_release);}];
    return true;
  }
  void TrackGuestWriter(std::vector<NativeIndirect::Range> physical,bool unknown=false) {
    if(physical.empty() && !unknown) return;
    PruneGuestWriters();
    // The current command is ordered after all older commands. Replacing a
    // full registry with one unknown ticket remains conservative and bounded.
    if(guest_writers.size()>=kGuestWriterLimit) {guest_writers.clear();unknown=true;physical.clear();}
    if(native_texture_coherence)texture_write_journal.Publish(physical,unknown);
    if(unknown) ++unknown_guest_writers;
    auto completed=std::make_shared<std::atomic<bool>>(false);
    guest_writers.push_back({std::move(physical),unknown,completed,submission_id});
    writer_prune.Published();
    if(const auto epoch=writer_prune.SharedState()) {
      [cmd addCompletedHandler:^(id<MTLCommandBuffer>){GuestWriterPruneEpoch::Complete(completed,epoch);}];
    } else {
      [cmd addCompletedHandler:^(id<MTLCommandBuffer>){completed->store(true,std::memory_order_release);}];
    }
  }
  void NoteTextureCpuWrite(u64 address,u64 bytes,bool deferred=false) {
    if(!native_texture_coherence || !bytes)return;
    std::vector<NativeIndirect::Range> physical;
    const bool complete=PhysicalRanges(address,bytes,physical);
    texture_write_journal.Publish(physical,!complete,deferred);
  }
  // This is called only at a CPU texture-content read boundary. In particular
  // native preflight still returns false without recording/draining anything.
  bool ReadyTextureSource(u64 address,u64 bytes,bool cpu_callbacks) {
    const auto signals_idle=[&] {
      std::scoped_lock lk(signals->lock);
      return signals->queue.empty() && !signals->active;
    };
    // Forgotten history still requires the caller's full content/hash check.
    // It does not require draining disjoint callbacks when the current source
    // has complete direct backing and BOTH the CP's pre-enqueue fence guard
    // and queued/active callback effects prove it CPU-readable. Private or
    // unresolved spans retain the original global-callback fallback below.
    if(gpu_written_texture_snapshot && native_indirect_enabled && native_texture_coherence &&
        CurrentCpuReadReady()) {
      std::vector<NativeIndirect::Range> physical;
      if(PhysicalRanges(address,bytes,physical) && !physical.empty() &&
          CpuResourceRead(address,bytes,CurrentCpuReadReady()))return true;
    }
    if(CpuResourceRead(address,bytes) && (!cpu_callbacks || signals_idle()))return true;
    ++texture_coherence_waits;
    StutterTrace::Scope timing("texture_source_wait",frame_count,address,bytes,0);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    Commit(false);
    const auto command=last_committed;
    while(command && command.status!=MTLCommandBufferStatusCompleted) {
      if(command.status==MTLCommandBufferStatusError || execution->Failed() ||
          std::chrono::steady_clock::now()>=deadline) {
        execution->Fail("GPU texture-source writer did not finish before the bounded CPU-read deadline.");return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(cpu_callbacks) {
      std::unique_lock lk(signals->lock);
      if(!signals->changed.wait_until(lk,deadline,[&] {
          return execution->Failed() || (signals->queue.empty() && !signals->active);
      })) {
        execution->Fail("CPU texture-source fence callbacks did not finish before the bounded deadline.");return false;
      }
    }
    // Command status may become Completed before its completion handlers have
    // published the writer tickets. Respect the same bounded deadline here.
    while(!execution->Failed() && !CpuResourceRead(address,bytes)) {
      if(std::chrono::steady_clock::now()>=deadline) {
        execution->Fail("Texture-source write ownership remained unresolved after the GPU drain.");return false;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return !execution->Failed();
  }
  void TextureSourceRanges(TextureEntry& e,u64 address,u64 bytes,u64 generation) {
    if(e.source_range_generation==generation && e.addr==address && e.size==bytes)return;
    e.source_physical.clear();
    e.source_physical_complete=PhysicalRanges(address,bytes,e.source_physical);
    e.source_range_generation=generation;
  }
  void RememberTextureSource(TextureEntry& e,u64 address,u64 bytes,u64 generation) {
    if(!native_texture_coherence)return;
    TextureSourceRanges(e,address,bytes,generation);
    e.source_write_epoch=texture_write_journal.Epoch();
  }
  bool RefineWorklistWriter(const Program& program,const u32* ud,const CpuReadReady& ready,
      std::array<u32,3> threads,std::array<u32,3> groups,
      std::vector<NativeIndirect::Range>& written,std::vector<DescriptorRead>& read) {
    if(!native_worklist_extent || !NativeWorklist::Matches(program))return false;
    const auto fallback=[&](const char* reason,u32 category) {
      ++native_worklist_fallbacks;
      ++native_worklist_fallback_reason[category];
      TraceProgramWriter(program,4096,reason,64,&program.buffer_writes[0]);return false;
    };
    // Group counts are supplied ONLY for an ordinary direct dispatch. Native
    // indirect arguments stay on the original full-descriptor footprint.
    if(threads!=std::array<u32,3>{64,1,1} || !groups[0] || groups[0]>NativeWorklist::kJobs ||
        groups[1]!=1 || groups[2]!=1)return fallback("worklist-dispatch-context",0);
    if(!NativeWorklist::Layout(ud[9],ud[13]) || ud[11]!=0x6c1ac || ud[10]<groups[0] ||
        ud[15]!=0x24004)
      return fallback("worklist-resource-layout",1);
    const u64 table=u64(ud[8])|(u64(ud[9]&0xfff)<<32);
    const u64 destination=u64(ud[12])|(u64(ud[13]&0xfff)<<32);
    const u64 bytes=u64(groups[0])*sizeof(NativeWorklist::Job);
    const auto& predicate=ready ? ready : CurrentCpuReadReady();
    id<MTLBuffer> buffer=nil;u64 offset=0;
    if(!predicate || table>kGpuAddrMask || destination>kGpuAddrMask || (table&3) ||
        !CpuResourceRead(table,bytes,predicate) || !LocateIndirect(table,bytes,&buffer,&offset))
      return fallback("worklist-input-unready",2);
    // Complete input XYZ loads must translate, not just the words later used
    // for bounds. NUM_RECORDS>=groups excludes the OOB allzero load case,
    // which would otherwise still admit lane0 and store destination index0.
    std::vector<NativeWorklist::Job> jobs(groups[0]);
    std::memcpy(jobs.data(),reinterpret_cast<const void*>(table),bytes);
    const auto plan=NativeWorklist::Bound(jobs,ud[14]);
    if(!plan)return fallback(plan.reason,3);
    const NativeIndirect::Range source{offset+(buffer==mem[0] ? 0 : mem_split),bytes};
    std::vector<NativeIndirect::Range> precise;
    for(const auto& range:plan.indices) {
      const u64 first=destination+u64(range.first)*4;
      const u64 extent=(u64(range.last)-range.first+1)*4;
      if(first>kGpuAddrMask || extent>kGpuAddrMask-first || !PhysicalRanges(first&~3ull,
          first+extent-(first&~3ull),precise))return fallback("worklist-output-unmapped",4);
    }
    for(const auto& range:precise)if(NativeIndirect::Overlap(source,range))
      return fallback("worklist-self-modification",5);
    PruneIndirectReads();
    if(read.size()>=64 || indirect_reads.size()>kIndirectReadLimit ||
        read.size()+1>kIndirectReadLimit-indirect_reads.size())return fallback("worklist-read-capacity",6);
    // Protect even an empty output set. Otherwise a later CPU table rewrite
    // could activate stores after the metadata claimed this producer is empty.
    read.push_back({{table,bytes},source});
    written.insert(written.end(),precise.begin(),precise.end());
    ++native_worklist_refined;
    TraceProgramWriter(program,2048,"worklist-proven",64,&program.buffer_writes[0],precise.size(),jobs.size());
    return true;
  }
  void TrackProgramWriter(const Program& program,const u32* ud,const CpuReadReady& ready={},
      std::array<u32,3> compute_threads={},std::array<u32,3> compute_groups={}) {
    if(!native_indirect_enabled) return;
    // Storage images resolve to separately allocated Metal textures/views;
    // their image_store instructions cannot write guest mem0/mem1 backing.
    // Raw buffer stores in the same program still need the proof below.
    bool unknown=!program.unsupported.empty();
    if(program.buffer_writes.empty() && !unknown) return;
    if(unknown) {
      TraceProgramWriter(program,1,"unsupported-program");TrackGuestWriter({},true);return;
    }
    // Linear reflection is not a reaching-definition proof, including for
    // some scalar clobbers. Cache the bounded code-only proof for every writer.
    auto [proof,inserted]=native_writer_proofs.try_emplace(&program);
    if(inserted)proof->second=NativeWriter::Prove(program);
    if(!proof->second) {
      TraceProgramWriter(program,1024,proof->second.reason,0,
          program.buffer_writes.empty() ? nullptr : &program.buffer_writes.front());
      TrackGuestWriter({},true);return;
    }
    std::vector<NativeIndirect::Range> written;
    std::vector<DescriptorRead> read;
    std::size_t descriptor_index=0;
    for(const auto& instruction:program.code) {
      using namespace Shader::Gcn;
      if(!NativeWriter::IsBufferWriter(instruction))continue;
      if(descriptor_index>=program.buffer_writes.size()) {
        TraceProgramWriter(program,2,"store-reflection-mismatch",instruction.pc);unknown=true;break;
      }
      const auto& source=program.buffer_writes[descriptor_index++];
      std::array<u32,4> descriptor{};
      // The CFG proof accepts one exact S_LOAD_DWORDX4 per pointer V#.
      // Its GPU load returns all zeros unless its WHOLE footprint translates
      // to one contiguous buffer, so prove that before individual word copies.
      if(source[0].kind==Shader::SgprSource::PointerLoad) {
        const auto& root=source[0];const auto& predicate=ready ? ready : CurrentCpuReadReady();
        const u64 table=(u64(ud[root.ud])|(u64(ud[root.ud+1])<<32))&0xffffffffffffull;
        id<MTLBuffer> buffer=nil;u64 offset=0;
        if(!predicate || table>UINT64_MAX-root.offset ||
            !CpuResourceRead(table+root.offset,16,predicate) ||
            !LocateIndirect(table+root.offset,16,&buffer,&offset)) {
          TraceProgramWriter(program,8,"descriptor-footprint-unready",instruction.pc,&source);
          unknown=true;continue;
        }
      }
      // Current MSL does not bound arbitrary vector/scalar byte offsets by
      // V#.NUM_RECORDS. count*stride alone would underestimate these writes.
      if(instruction.addr64) {
        TraceProgramWriter(program,4,"address64",instruction.pc,&source);unknown=true;continue;
      }
      bool resolved=true;
      for(unsigned k=0;k<4;++k)
        resolved=ReadDescriptorWord(source[k],ud,descriptor[k],ready,&read,true) && resolved;
      if(!resolved) {
        TraceProgramWriter(program,8,"descriptor-unready-or-unmapped",instruction.pc,&source);
        unknown=true;continue;
      }
      const u64 base=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&kGpuAddrMask;
      const u64 stride=(descriptor[1]>>16)&0x3fff;
      if(RefineWorklistWriter(program,ud,ready,compute_threads,compute_groups,written,read))continue;
      u64 begin=0,end=0;
      if(!instruction.idxen && !stride) {
        // The production MSL bounds its complete uint byte offset by z for
        // raw byte-addressed stores, including dynamic vector/scalar offsets.
        if(!descriptor[2]) continue;
        end=u64(descriptor[2])+15;
      } else {
        u32 maximum_offset=instruction.offset;
        if(instruction.offen && instruction.soffset==Operand::Zero &&
            program.stage==Shader::Stage::Compute && compute_threads[0] &&
            compute_threads[1] && compute_threads[2] && compute_threads[0]<=65535 &&
            compute_threads[1]<=65535 && compute_threads[2]<=65535) {
          const u64 key=u64(compute_threads[0])|(u64(compute_threads[1])<<16)|(u64(compute_threads[2])<<32);
          auto& cache=native_writer_offset_proofs[&program];
          auto offsets=cache.find(key);
          // A program with continually changing group dimensions cannot grow
          // the diagnostic/admission cache without bound. New contexts fall
          // back once the first eight exact configurations have been proved.
          if(offsets==cache.end() && cache.size()<8)
            offsets=cache.emplace(key,NativeWriter::ProveOffsets(program,compute_threads)).first;
          maximum_offset=offsets!=cache.end() && offsets->second &&
              descriptor_index<=offsets->second.maximum.size() ?
              offsets->second.maximum[descriptor_index-1] : UINT32_MAX;
        } else if(instruction.offen)maximum_offset=UINT32_MAX;
        if(maximum_offset==UINT32_MAX || instruction.soffset!=Operand::Zero) {
          TraceProgramWriter(program,16,"dynamic-indexed-offset",instruction.pc,&source);
          unknown=true;continue;
        }
        const u64 records=instruction.idxen ? descriptor[2] : 1;
        if(!records) continue;
        // The vector proof bounds the COMPLETE uint byte offset. Indexed
        // MSL only checks idx<records: offsets are never clamped to stride,
        // resource byte size or the VMA containing the descriptor's base.
        begin=instruction.offen ? 0 : instruction.offset;
        end=(records-1)*stride+maximum_offset+16;
      }
      const u64 start=(base+begin)&~3ull;
      if(end>kGpuAddrMask-base || !PhysicalRanges(start,base+end-start,written)) {
        TraceProgramWriter(program,32,"write-extent-unmapped-or-overflow",instruction.pc,&source);
        unknown=true;
      }
    }
    if(descriptor_index!=program.buffer_writes.size()) {
      TraceProgramWriter(program,2,"store-reflection-mismatch");unknown=true;
    }
    for(const auto& a:read)for(const auto& b:written)
      if(NativeIndirect::Overlap(a.physical,b)) {
        TraceProgramWriter(program,64,"self-modified-descriptor");unknown=true;
      }
    // Freeze a proved table for the producer's whole command lifetime before
    // publishing its write ranges. CP writes through any physical alias and
    // mapping mutations already drain this bounded read registry. Even a
    // zero-record table needs protection: a later host edit could activate it.
    if(!unknown && !ProtectDescriptorReads(read)) {
      TraceProgramWriter(program,128,"descriptor-read-capacity");unknown=true;
    }
    PruneGuestWriters();
    if(guest_writers.size()>=kGuestWriterLimit) {
      TraceProgramWriter(program,256,"writer-registry-capacity");unknown=true;
    }
    if(!unknown)TraceProgramWriter(program,0x80000000u,"proven",0,nullptr,written.size(),read.size());
    TrackGuestWriter(std::move(written),unknown);
  }
  // No CPU payload read: only exact direct backing and tracked same-queue
  // writers. CP callbacks cannot be ordered by the source-copy GPU command.
  u32 SignalSourceBlock(NativeIndirect::Range source) {
    const u64 generation=Core::Memory::DirectMapGeneration();
    std::scoped_lock lk(signals->lock);
    const auto blocked=[&](const SignalEffect& effect,bool active)->u32 {
      if(effect.kind==SignalEffect::Kind::NoDirectMemoryWrite)return UINT32_MAX;
      if(effect.kind!=SignalEffect::Kind::PhysicalWrite)return active ? 4 : 3;
      // A raw trusted closure is not a mapping lease. Any map change makes
      // its captured physical destination uncertain until it has returned.
      if(effect.mapping_generation!=generation)return active ? 12 : 11;
      for(const auto& range:effect.physical)
        if(NativeIndirect::Overlap(range,source))return active ? 10 : 9;
      return UINT32_MAX;
    };
    if(signals->closed)return 3;
    for(const auto& callback:signals->queue) {
      const auto reason=blocked(callback.effect,false);
      if(reason!=UINT32_MAX)return reason;
    }
    if(signals->active) {
      if(!signals->active_effect)return 4;
      return blocked(*signals->active_effect,true);
    }
    return UINT32_MAX;
  }
  bool GpuTextureSource(u64 address,u64 bytes,const CpuReadReady& ready,
      id<MTLBuffer>* source,u64* offset) {
    if(!gpu_written_texture_snapshot || !native_indirect_enabled || !native_texture_coherence)return false;
    const auto reject=[&](u32 reason){++texture_gpu_snapshot_rejects[reason];return false;};
    if(!texture_uploader || !texture_uploader->Ready() || !bytes || bytes>(128ull<<20) ||
        (bytes&3))return reject(0);
    const auto& predicate=ready ? ready : CurrentCpuReadReady();
    if(!predicate)return reject(1);
    if(!predicate(address,bytes))return reject(2);
    if(!IndirectReadAvailable())return reject(5);
    if(!LocateIndirect(address,bytes,source,offset))return reject(6);
    const NativeIndirect::Range physical{*offset+(*source==mem[0] ? 0 : mem_split),bytes};
    if(const auto reason=SignalSourceBlock(physical);reason!=UINT32_MAX)return reject(reason);
    PruneGuestWriters();bool overlap=false;
    for(const auto& writer:guest_writers) {
      if(writer.unknown) {
        if(writer_prune.RetainedCompleted(writer.completed,GuestWriterPruneEpoch::Use::GpuUnknown))continue;
        return reject(7);
      }
      bool intersects=false;
      for(const auto& range:writer.physical)intersects|=NativeIndirect::Overlap(range,physical);
      // A retained completed writer is never a positive pending-producer
      // witness, even if its epoch notification is not visible yet.
      if(intersects && !writer_prune.RetainedCompleted(writer.completed,
          GuestWriterPruneEpoch::Use::GpuWitness))overlap=true;
    }
    return overlap || reject(8);
  }
  bool TextureCpuRead(const u32* t,const CpuReadReady& ready) {
    const u64 address=((u64(t[0])|(u64(t[1]&0xff)<<32))<<8)&kGpuAddrMask;
    const u32 format=(t[1]>>20)&63,number=(t[1]>>26)&15,type=t[3]>>28;
    if(!address || !format || type<8) return true; // null descriptor -> dummy
    // Keep this proof narrower than GetTexture's complete view/array logic.
    if(type!=9 || mip_alias.count(address)) return false;
    const u32 width=(t[2]&0x3fff)+1,height=((t[2]>>14)&0x3fff)+1;
    const u32 first=(t[3]>>12)&15,last=std::max(first,(t[3]>>16)&15);
    auto* color=Latest(color_targets,address,TexDim::Tex2D,width,height);
    auto* depth=Latest(depth_targets,address,TexDim::Tex2D,width,height);
    if(color && (color->width!=width || color->height!=height)) color=nullptr;
    if(depth && (depth->width!=width || depth->height!=height)) depth=nullptr;
    // Mirror GetTexture's latest-writer selection, including its color tie.
    // Unknown zero counts cannot grow surfaces or run NoteMipLevels, which
    // can migrate child surfaces and change the alias registry on the CPU.
    const auto* target=color && (!depth || color->written>=depth->written) ? color : depth;
    if(target) return target->initialized && !target->clear_pending &&
        target->levels>last && last==0;
    const auto fi=TextureFormat(format,number);
    const u32 tiling=(t[3]>>20)&31,pitch=((t[4]>>13)&0x3fff)+1;
    if(!fi.bytes_per_block || fi.gpu_only || !Tiling::Supported(tiling)) return false;
    const u32 levels=std::min(last+1,1u+u32(std::log2(std::max(width,height))));
    Tiling::MipLevel mips[16];
    const u64 bytes=Tiling::MipLayout(tiling,pitch,height,1,levels,fi.bytes_per_block,
        fi.compressed,(t[3]>>25)&1,mips);
    if(CpuResourceRead(address,bytes,ready))return true;
    if(!gpu_written_texture_snapshot || !texture_uploader || !texture_uploader->SupportsFormat(fi.format) ||
        std::getenv("BB_METAL_CAPTURE_TEXTURE") || !capture_root.empty() || dump_texture==address)return false;
    u64 key=Hash(address);for(u32 k=1;k<6;++k)key=Hash(t[k],key);
    const auto cached=textures.find(key);
    const u64 generation=Core::Memory::DirectMapGeneration();
    // Match GetTexture's exact existing entry and mapping gates without
    // creating or changing an entry during native admission. Forgotten write
    // history still invalidates its content, but a fresh immutable GPU copy
    // may recover it only after GpuTextureSource proves all CURRENT writers.
    if(cached==textures.end() || !cached->second.tex ||
        cached->second.mapping_generation!=generation ||
        cached->second.source_range_generation!=generation ||
        !cached->second.source_physical_complete || cached->second.size!=bytes)return false;
    for(u32 level=0;level<levels;++level)
      if(!texture_uploader->SupportsGpuSourceTile(mips[level].tile_index))return false;
    id<MTLBuffer> source=nil;u64 offset=0;
    return GpuTextureSource(address,bytes,ready,&source,&offset);
  }
  bool NativeResources(const Program& program,const u32* ud,const CpuReadReady& ready) {
    for(const auto& image:program.images) {
      if(image.storage) return false;
      std::array<u32,8> texture{};
      for(unsigned k=0;k<8;++k) if(!ReadDescriptorWord(image.tsharp[k],ud,texture[k],ready)) return false;
      if(!TextureCpuRead(texture.data(),ready)) return false;
      if(image.has_sampler) for(const auto& source:image.ssharp) {
        u32 value;if(!ReadDescriptorWord(source,ud,value,ready)) return false;
      }
    }
    for(const auto& sampler:program.samplers)for(const auto& source:sampler) {
      u32 value;if(!ReadDescriptorWord(source,ud,value,ready)) return false;
    }
    for(u32 k=0;k+1<16;++k) {
      const u64 address=ud[k]|(u64(ud[k+1]&0xfff)<<32);
      auto dump=const_dumps.upper_bound(address);
      if(dump==const_dumps.begin()) continue;
      --dump;
      if(address-dump->first<dump->second.bytes) {
        if(!CpuResourceRead(dump->first,dump->second.bytes,ready)) return false;
        ++k;
      }
    }
    return true;
  }
  bool RejectBoundVertex(BoundReject reason,const Program& program,const u32* ud,
      const BoundVertexSource& source,const char* detail="") {
    ++bound_vertex_rejects[u32(reason)];
    const u64 root=(u64(ud[2])|(u64(ud[3])<<32))&0xffffffffffffull;
    const u64 key=Hash(u32(reason),Hash(root,Hash(program.hash)));
    if(bound_vertex_diagnostics.size()<64 && bound_vertex_diagnostics.insert(key).second)
      DC_INFO("gpu bound vertex audit reject: frame=%llu vs=%#llx hash=%#llx reason=%u detail=%s root=%#llx dump=%#llx+%llu gen=%llu map=%llu",
          frame_count,program.address,program.hash,u32(reason),detail,root,
          source.address,source.bytes,source.generation,source.map_generation);
    return false;
  }
  bool InspectBoundVertexSource(const Program& program,const u32* ud,bool wrapper,
      BoundVertexSource& source) {
    source={};++bound_vertex_candidates;
    if(!wrapper || !gpu_written_texture_snapshot || !native_indirect_enabled || !native_texture_coherence)
      return RejectBoundVertex(BoundReject::Context,program,ud,source);
    auto [proof,inserted]=native_vertex_fetch_programs.try_emplace(program.hash,false);
    if(inserted)proof->second=Shader::VertexNativeFetchEligible(program);
    if(!proof->second)return RejectBoundVertex(BoundReject::Shape,program,ud,source);
    const auto& ready=CurrentCpuReadReady();
    if(!ready)return RejectBoundVertex(BoundReject::Guard,program,ud,source);
    const u64 root=(u64(ud[2])|(u64(ud[3])<<32))&0xffffffffffffull;
    const u64 patched_lookup=u64(ud[2])|(u64(ud[3]&0xfff)<<32);
    auto dump=const_dumps.upper_bound(patched_lookup);
    if(root!=patched_lookup || dump==const_dumps.begin())
      return RejectBoundVertex(BoundReject::DumpMissing,program,ud,source);
    --dump;
    source={dump->first,dump->second.bytes,dump->second.gen,Core::Memory::DirectMapGeneration()};
    const u64 table=root&~3ull;
    const u64 bytes=u64(NativeVertexFetch::Layout(program.hash)->attributes)*16;
    if(root<source.address || root-source.address>=source.bytes || table<source.address ||
        bytes>source.bytes-(table-source.address))
      return RejectBoundVertex(BoundReject::DumpBounds,program,ud,source);
    source.root_offset=root-source.address;
    // BEFORE PatchUserData's memcpy: ownership of its later arena allocation
    // does not prove this complete guest source was CPU-readable now.
    if(!CpuResourceRead(source.address,source.bytes,ready))
      return RejectBoundVertex(BoundReject::DumpPending,program,ud,source);
    ++bound_vertex_source_ready;return true;
  }
  void InspectBoundVertexPrepared(const Program& program,const u32* ud,
      const BoundVertexSource& source,std::span<const StaticSmrd::Snapshot> snapshots) {
    if(source.map_generation!=Core::Memory::DirectMapGeneration()) {
      RejectBoundVertex(BoundReject::MapChanged,program,ud,source);return;
    }
    const auto dump=const_dumps.find(source.address);
    if(dump==const_dumps.end() || dump->second.bytes!=source.bytes || dump->second.gen!=source.generation) {
      RejectBoundVertex(BoundReject::DumpChanged,program,ud,source);return;
    }
    const StaticSmrd::Window window{arena_va,kArenaSize,arena_head,arena_commit_head,arena_pin};
    const u64 patched_root=(u64(ud[2])|(u64(ud[3])<<32))&0xffffffffffffull;
    const StaticSmrd::Snapshot* owner=nullptr;
    for(const auto& snapshot:snapshots)if(StaticSmrd::Valid(snapshot,window) &&
        snapshot.generation==source.generation && snapshot.bytes==source.bytes &&
        source.root_offset<snapshot.bytes && patched_root==snapshot.address+source.root_offset) {
      if(owner) {RejectBoundVertex(BoundReject::Snapshot,program,ud,source,"ambiguous-source-owner");return;}
      owner=&snapshot;
    }
    if(!owner) {RejectBoundVertex(BoundReject::Snapshot,program,ud,source,"root-not-prepared-from-source");return;}
    BoundVertexFetch::Prepared prepared;
    const auto result=BoundVertexFetch::Prepare(program.hash,std::span<const u32,16>(ud,16),
        std::span<const StaticSmrd::Snapshot>(owner,1),window,prepared);
    if(result!=BoundVertexFetch::Reject::None) {
      ++bound_vertex_snapshot_rejects[u32(result)];
      RejectBoundVertex(BoundReject::Snapshot,program,ud,source,BoundVertexFetch::Names[u32(result)]);return;
    }
    id<MTLBuffer> buffer=nil;u64 offset=0;
    if(!LocateIndirect(prepared.payload,prepared.bytes,&buffer,&offset)) {
      RejectBoundVertex(BoundReject::PayloadMap,program,ud,source);return;
    }
    const auto& ready=CurrentCpuReadReady();
    if(!ready || !ready(prepared.payload,prepared.bytes)) {
      RejectBoundVertex(BoundReject::PayloadCp,program,ud,source);return;
    }
    const NativeIndirect::Range physical{offset+(buffer==mem[0] ? 0 : mem_split),prepared.bytes};
    if(SignalSourceBlock(physical)!=UINT32_MAX) {
      RejectBoundVertex(BoundReject::PayloadCallback,program,ud,source);return;
    }
    if(!IndirectReadAvailable()) {RejectBoundVertex(BoundReject::Lease,program,ud,source);return;}
    // This is an observation, not an admission: no payload pointer is bound,
    // and no read lease is published by the audit. A future GPU path must
    // lease these bytes on the ACTUAL consumer command after any setup cuts.
    ++bound_vertex_metadata_ready;
    if(bound_vertex_positive_diagnostics.size()<16 &&
        bound_vertex_positive_diagnostics.insert(Hash(source.address,Hash(program.hash))).second)
      DC_INFO("gpu bound vertex audit ready: frame=%llu hash=%#llx dump=%#llx+%llu gen=%llu prepared-table=%#llx payload=%#llx+%llu stride=%u attributes=%u records=%u..%u; metadata only",
          frame_count,program.hash,source.address,source.bytes,source.generation,prepared.table,
          prepared.payload,prepared.bytes,prepared.stride,prepared.attributes,prepared.min_records,prepared.max_records);
  }
  StaticSmrd::AddressBlock GuardedDirectSmrd(const Program& vertex,const Program& pixel,
      std::span<const u32,16> prepared,id<MTLBuffer> draw_map,u64 generation,bool ordinary) {
    StutterTrace::Scope timing("draw_direct_ps_root",frame_count,vertex.hash,pixel.hash);
    StaticSmrd::AddressBlock addresses;
    if(draw_map.length>=sizeof(Shader::GpuMemMap)) {
      const auto* header=static_cast<const Shader::GpuMemMap*>(draw_map.contents);
      const auto* entries=reinterpret_cast<const std::int32_t*>(header+1);
      const auto count=(draw_map.length-sizeof(*header))/sizeof(*entries);
      addresses=StaticSmrd::TranslateRoots(pixel.hash,prepared,*header,{entries,count});
    }
    std::optional<bool> read_only;
    struct Guards {
      Impl& owner;const Program& vertex;const Program& pixel;
      id<MTLBuffer> map;u64 generation;bool ordinary;std::optional<bool>& read_only;
      const CpuReadReady& predicate;
      DirectRootSpan::Span span{};
      id<MTLBuffer> buffer=nil;u64 offset=0;
      bool Context() const {
        return ordinary && owner.cmd && owner.enc && owner.native_indirect_enabled &&
            owner.native_texture_coherence && owner.gpu_written_texture_snapshot;
      }
      bool StagesReadOnly() {
        if(!read_only) {
          const auto eligible=[&](const Program& program) {
            auto [entry,inserted]=owner.readonly_guest_programs.try_emplace(&program,false);
            if(inserted)entry->second=Shader::GuestMemoryReadOnlyEligible(program);
            return entry->second;
          };
          read_only=eligible(vertex) && eligible(pixel);
        }
        return *read_only;
      }
      bool HasCpuPredicate() const {return bool(predicate);}
      bool GenerationStable() const {return generation==Core::Memory::DirectMapGeneration();}
      bool Mapping(const PsRootRead::Request& request) {
        if(map.length<sizeof(Shader::GpuMemMap))return false;
        const auto* header=static_cast<const Shader::GpuMemMap*>(map.contents);
        const auto* entries=reinterpret_cast<const std::int32_t*>(header+1);
        const auto count=(map.length-sizeof(*header))/sizeof(*entries);
        if(!DirectRootSpan::TranslateSpan(request.address,request.bytes,*header,{entries,count},span))return false;
        // ARC write-back parameters require a local object, not a guard member.
        id<MTLBuffer> located=nil;
        if(!owner.LocateIndirect(request.address,request.bytes,&located,&offset))return false;
        buffer=located;return true;
      }
      bool PhysicalMatch(const PsRootRead::Request& request) const {
        return DirectRootSpan::MatchBacking(span,request.encoded,unsigned(buffer==owner.mem[1]),
            offset,buffer.length,generation,Core::Memory::DirectMapGeneration());
      }
      bool CpuReady(const PsRootRead::Request& request) const {return predicate(request.address,request.bytes);}
      bool CallbacksReady() {
        const auto reason=owner.SignalSourceBlock({span.physical,span.bytes});
        if(reason==UINT32_MAX)return true;
        if(reason<owner.direct_smrd_callback_rejects.size())++owner.direct_smrd_callback_rejects[reason];
        return false;
      }
      PsRootRead::Reject WritersReady() {
        owner.PruneGuestWriters();
        for(const auto& writer:owner.guest_writers) {
          if(writer.unknown)return PsRootRead::Reject::GpuUnknown;
          // A previous command on this queue is ordered. An overlapping writer
          // in this command may still belong to this render encoder: metadata
          // alone is not proof of an intra-pass stage barrier. Do not add one.
          if(writer.submission>=owner.submission_id)for(const auto& range:writer.physical)
            if(NativeIndirect::Overlap(range,{span.physical,span.bytes}))
              return PsRootRead::Reject::GpuCurrentCommand;
        }
        return PsRootRead::Reject::None;
      }
      bool PublishRead(const PsRootRead::Request& request) {
        if(!owner.IndirectReadAvailable())return false;
        owner.TrackIndirectRead(request.address,request.bytes,buffer,offset);return true;
      }
    };
    for(unsigned slot=0;slot<2;++slot) {
      const auto request=PsRootRead::MakeRequest(pixel.hash,slot,prepared,addresses.roots[slot]);
      Guards guard{*this,vertex,pixel,draw_map,generation,ordinary,read_only,CurrentCpuReadReady()};
      const auto result=PsRootRead::Protect(request,guard);
      ++direct_smrd_root_results[unsigned(result)];
      if(result!=PsRootRead::Reject::None)addresses.roots[slot]=StaticSmrd::AddressBlock::Fallback;
    }
    ++direct_smrd_draws;direct_smrd_roots+=addresses.ValidRoots();
    direct_smrd_fallbacks+=2-addresses.ValidRoots();return addresses;
  }
  StaticSmrd::AddressBlock GuardedVertexRoot(const Program& vertex,const Program* pixel,
      std::span<const u32,16> prepared,id<MTLBuffer> draw_map,u64 generation,bool ordinary) {
    StutterTrace::Scope timing("draw_direct_vs_root",frame_count,vertex.hash,pixel ? pixel->hash : 0);
    StaticSmrd::AddressBlock addresses;
    const auto* layout=NativeVertexFetch::Layout(vertex.hash);
    DirectRootRead::Request request{DirectRootSpan::SmrdRoot(prepared[2],prepared[3]),
        layout ? u64(layout->attributes)*16 : 0,DirectRootSpan::Fallback};
    if(draw_map.length>=sizeof(Shader::GpuMemMap)) {
      const auto* header=static_cast<const Shader::GpuMemMap*>(draw_map.contents);
      const auto* entries=reinterpret_cast<const std::int32_t*>(header+1);
      const auto count=(draw_map.length-sizeof(*header))/sizeof(*entries);
      DirectRootSpan::Span span;
      if(DirectRootSpan::TranslateSpan(request.address,request.bytes,*header,{entries,count},span))
        request.encoded=span.encoded;
    }
    struct Guards {
      Impl& owner;const Program& vertex;const Program* pixel;
      id<MTLBuffer> map;u64 generation;bool ordinary;const CpuReadReady& predicate;
      DirectRootSpan::Span span{};id<MTLBuffer> buffer=nil;u64 offset=0;
      bool Context() const {
        return ordinary && owner.cmd && owner.enc && owner.native_indirect_enabled &&
            owner.native_texture_coherence && owner.gpu_written_texture_snapshot;
      }
      bool StagesReadOnly() {
        const auto eligible=[&](const Program* program) {
          if(!program)return true;
          auto [entry,inserted]=owner.readonly_guest_programs.try_emplace(program,false);
          if(inserted)entry->second=Shader::GuestMemoryReadOnlyEligible(*program);
          return entry->second;
        };
        return eligible(&vertex) && eligible(pixel);
      }
      bool HasCpuPredicate() const {return bool(predicate);}
      bool GenerationStable() const {return generation==Core::Memory::DirectMapGeneration();}
      bool Mapping(const DirectRootRead::Request& request) {
        if(map.length<sizeof(Shader::GpuMemMap))return false;
        const auto* header=static_cast<const Shader::GpuMemMap*>(map.contents);
        const auto* entries=reinterpret_cast<const std::int32_t*>(header+1);
        const auto count=(map.length-sizeof(*header))/sizeof(*entries);
        if(!DirectRootSpan::TranslateSpan(request.address,request.bytes,*header,{entries,count},span))return false;
        // ARC write-back parameters require a local object, not a guard member.
        id<MTLBuffer> located=nil;
        if(!owner.LocateIndirect(request.address,request.bytes,&located,&offset))return false;
        buffer=located;return true;
      }
      bool PhysicalMatch(const DirectRootRead::Request& request) const {
        return DirectRootSpan::MatchBacking(span,request.encoded,unsigned(buffer==owner.mem[1]),
            offset,buffer.length,generation,Core::Memory::DirectMapGeneration());
      }
      bool CpuReady(const DirectRootRead::Request& request) const {return predicate(request.address,request.bytes);}
      bool CallbacksReady() {return owner.SignalSourceBlock({span.physical,span.bytes})==UINT32_MAX;}
      DirectRootRead::Reject WritersReady() {
        owner.PruneGuestWriters();
        for(const auto& writer:owner.guest_writers) {
          if(writer.unknown)return DirectRootRead::Reject::GpuUnknown;
          // Older commands on this queue are ordered; metadata alone does not
          // establish an intra-pass stage barrier for this command's writers.
          if(writer.submission>=owner.submission_id)for(const auto& range:writer.physical)
            if(NativeIndirect::Overlap(range,{span.physical,span.bytes}))
              return DirectRootRead::Reject::GpuCurrentCommand;
        }
        return DirectRootRead::Reject::None;
      }
      bool PublishRead(const DirectRootRead::Request& request) {
        if(!owner.IndirectReadAvailable())return false;
        owner.TrackIndirectRead(request.address,request.bytes,buffer,offset);return true;
      }
    };
    Guards guard{*this,vertex,pixel,draw_map,generation,ordinary,CurrentCpuReadReady()};
    const auto result=DirectRootRead::Protect(request,guard);
    ++direct_vertex_root_rejects[unsigned(result)];
    if(result==DirectRootRead::Reject::None)addresses.roots[0]=request.encoded;
    ++direct_vertex_root_draws;
    if(result==DirectRootRead::Reject::None)++direct_vertex_root_hits;
    else ++direct_vertex_root_fallbacks;
    return addresses;
  }
  void PruneIndirectReads() {
    std::erase_if(indirect_reads,[](const IndirectRead& read) {
      return read.completed->load(std::memory_order_acquire);
    });
  }
  bool IndirectReadAvailable() {
    PruneIndirectReads();return indirect_reads.size()<kIndirectReadLimit;
  }
  void TrackIndirectRead(u64 address,u64 bytes,id<MTLBuffer> buffer,u64 offset) {
    auto completed=std::make_shared<std::atomic<bool>>(false);
    const u64 physical=offset+(buffer==mem[0] ? 0 : mem_split);
    indirect_reads.push_back({{address,bytes},{physical,bytes},completed});
    [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {completed->store(true,std::memory_order_release);}];
  }
  void TraceIndirectArguments(id<MTLBuffer> arguments,u64 address,bool dispatch) {
    if(!indirect_argument_trace) return;
    const auto now=std::chrono::steady_clock::now();
    auto& last=indirect_argument_sample[dispatch ? 1 : 0];
    if(now-last<std::chrono::seconds(1)) return;
    last=now;
    const u64 frame=frame_count;
    const auto time=std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
    // This dedicated buffer is immutable after its adapter/copy. The block
    // retains it; no renderer state or additional GPU/CPU synchronization.
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if(completed.status!=MTLCommandBufferStatusCompleted) return;
      const auto* words=static_cast<const u32*>(arguments.contents);
      DC_INFO("gpu indirect args: %s frame=%llu steady_ns=%lld source=%#llx words=%u,%u,%u,%u",
          dispatch ? "dispatch" : "draw",frame,time,address,words[0],words[1],words[2],dispatch ? 0 : words[3]);
    }];
  }
  bool ProtectIndirectWrite(u64 address,u64 bytes) {
    PruneIndirectReads();
    if(indirect_reads.empty() || !bytes) return !execution->Failed();
    UpdateMemMap();
    bool conflict=address>UINT64_MAX-bytes;
    for(u64 at=address;!conflict && at<address+bytes;) {
      auto mapping=std::upper_bound(mem_ranges.begin(),mem_ranges.end(),at,
          [](u64 address,const auto& range){return address<range.start;});
      if(mapping==mem_ranges.begin()) {conflict=true;break;}
      --mapping;
      if(at<mapping->start || at-mapping->start>=mapping->size) {conflict=true;break;}
      const u64 available=mapping->size-(at-mapping->start);
      const u64 extent=std::min(available,address+bytes-at);
      const u64 physical=mapping->phys+(at-mapping->start);
      for(const auto& read:indirect_reads)
        if(NativeIndirect::Overlap(read.range,{at,extent}) ||
            NativeIndirect::Overlap(read.physical,{physical,extent})) {conflict=true;break;}
      at+=extent;
    }
    if(conflict) {
      ++indirect_cpu_write_waits;
      StutterTrace::Scope timing("indirect_cpu_write_wait",frame_count,address,bytes,0);
      Commit(false);
      const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
      const auto command=last_committed;
      while(command && command.status!=MTLCommandBufferStatusCompleted) {
        if(command.status==MTLCommandBufferStatusError || std::chrono::steady_clock::now()>=deadline) {
          execution->Fail("GPU indirect read did not complete before a CP memory write.");return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      PruneIndirectReads();
    }
    return !execution->Failed();
  }

  // CP DMA on the host GPU, in order with the work around it.
  bool DmaData(u32 dst_sel, u64 dst, u32 src_sel, u64 src, u32 bytes) {
    if (trace) {
      DC_INFO("gpu: dma %u:%#llx <- %u:%#llx, %u bytes", dst_sel, dst, src_sel,
              src, bytes);
    }
    if ((bytes & 3) || (dst & 3) || (src_sel != 2 && (src & 3))) {
      return false;
    }
    id<MTLBuffer> dbuf = nil, sbuf = nil;
    u64 doff = 0, soff = 0;
    if (dst_sel == 1) {
      if (dst + bytes > gds.length) {
        return false;
      }
      dbuf = gds;
      doff = dst;
    } else if (!Locate(dst, bytes, &dbuf, &doff)) {
      return false;
    }
    if (src_sel == 1) {
      if (src + bytes > gds.length) {
        return false;
      }
      sbuf = gds;
      soff = src;
    } else if (src_sel != 2 && !Locate(src, bytes, &sbuf, &soff)) {
      return false;
    }
    const u32 value = static_cast<u32>(src);
    if (src_sel == 2 && dst_sel != 1 && value != ~0u && !expanding_htile &&
        MarkMetaCleared(dst, bytes)) {
      EndPass(); // the next pass over the surface loads its clear value
    }
    EndPass();
    EnsureCommandBuffer();
    if (sbuf) {
      id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
      [b copyFromBuffer:sbuf
               sourceOffset:soff
                   toBuffer:dbuf
          destinationOffset:doff
                       size:bytes];
      [b endEncoding];
    } else if (value == (value & 0xFF) * 0x01010101u) {
      id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
      [b fillBuffer:dbuf
              range:NSMakeRange(doff, bytes)
              value:static_cast<u8>(value)];
      [b endEncoding];
    } else if (fill_pipeline) {
      const u32 vc[2] = {value, bytes / 4};
      id<MTLComputeCommandEncoder> e = [cmd computeCommandEncoder];
      [e setComputePipelineState:fill_pipeline];
      [e setBuffer:dbuf offset:doff atIndex:0];
      [e setBytes:vc length:sizeof(vc) atIndex:1];
      [e dispatchThreads:MTLSizeMake(bytes / 4, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
      [e endEncoding];
    } else {
      return false;
    }
    if(dst_sel!=1 && native_indirect_enabled)
      TrackGuestWriter({{doff+(dbuf==mem[0] ? 0 : mem_split),bytes}});
    return true;
  }

  // ================= recording =================

  void EnsureCommandBuffer() {
    if (!cmd) {
      StutterTrace::Scope timing("command_buffer_create",frame_count,last_vs,last_ps,1);
      cmd = [queue commandBuffer];
      if(command_timeline || command_budget_us)command_created=StutterTrace::Now();
      if(stage_counter_log)stage_counter_clock=stage_counter_log->Clock();
    }
  }

  void EndPass() {
    binding_cache.Reset();
    if (enc) {
      StutterTrace::Scope timing("render_pass_end",frame_count,last_vs,last_ps,1);
      [enc endEncoding];
      timing.End();
      enc = nil;
      if(stage_counter_pass) {
        stage_counter_passes.push_back(std::move(*stage_counter_pass));
        stage_counter_pass.reset();
      }
      if (dump_passes && trace && dump_frames) {
        DumpPass();
      }
      if (Profiling() && profile_passes && cmd) {
        const auto* target=reinterpret_cast<RenderTarget*>(pass.color[0]);
        const auto* depth=reinterpret_cast<RenderTarget*>(pass.depth);
        const std::string what="pass color "+Hex(target ? target->addr : 0)+
            " depth "+Hex(depth ? depth->addr : 0);
        pass={};
        ProfileCut(what);
      }
    }
    pass = {};
  }

  // DC_DUMP_PASSES: small BGRA copies of the targets of the pass just ended.
  void DumpPass() {
    for (u32 k = 0; k < 9; ++k) {
      id<MTLTexture> t = pass_tex[k];
      pass_tex[k] = nil;
      if (!t || !copy_pipeline) {
        continue;
      }
      if (t.textureType ==
          MTLTextureType2DArray) { // each slice (cube faces) on its own
        for (NSUInteger sl = 0; sl < std::min<NSUInteger>(t.arrayLength, 12);
             ++sl) {
          DumpTexture([t newTextureViewWithPixelFormat:t.pixelFormat
                                           textureType:MTLTextureType2D
                                                levels:NSMakeRange(0, 1)
                                                slices:NSMakeRange(sl, 1)],
                      k, pass_addr[k]);
        }
      } else if (t.textureType == MTLTextureType2D) {
        DumpTexture(t, k, pass_addr[k]);
      }
    }
  }

  void DumpTexture(id<MTLTexture> t, u32 k, u64 addr) {
    if (t.pixelFormat==MTLPixelFormatRGBA8Uint)
      t=[t newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm];
    const MTLPixelFormat f = t.pixelFormat;
    static const bool raw_rgba = std::getenv("DC_DUMP_RGBA") != nullptr;
    if (raw_rgba && t.textureType == MTLTextureType2D && t.width <= 4096 && t.height <= 4096 &&
        (f == MTLPixelFormatRGBA8Unorm || f == MTLPixelFormatRGBA8Unorm_sRGB)) {
      for (u32 level=0; level<t.mipmapLevelCount; ++level) {
        const u32 rw=std::max<u32>(1,static_cast<u32>(t.width)>>level);
        const u32 rh=std::max<u32>(1,static_cast<u32>(t.height)>>level);
        const u32 row=(rw*4+63)&~63u;
        id<MTLBuffer> raw=[device newBufferWithLength:u64(row)*rh options:MTLResourceStorageModeShared];
        auto b=[cmd blitCommandEncoder];
        [b copyFromTexture:t sourceSlice:0 sourceLevel:level sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(rw,rh,1) toBuffer:raw destinationOffset:0
            destinationBytesPerRow:row destinationBytesPerImage:u64(row)*rh];
        [b endEncoding];
        char name[128];
        std::snprintf(name,sizeof(name),"pass_%04zu_c%u_%llx_d%llu_l%u_rgba",
                      pass_dumps.size(),k,addr,draw_count,level);
        pass_dumps.push_back({raw,rw,rh,name,true});
      }
    }
    if (k < 8 &&
        (f == MTLPixelFormatRGBA8Uint || f == MTLPixelFormatR32Uint ||
         f == MTLPixelFormatRG32Uint || f == MTLPixelFormatRGBA32Uint ||
         f == MTLPixelFormatR16Uint || f == MTLPixelFormatR8Uint ||
         f == MTLPixelFormatRG16Uint || f == MTLPixelFormatRGBA16Uint)) {
      return;
    }
    const u32 w = std::max<u32>(1, static_cast<u32>(t.width) / 4),
              h = std::max<u32>(1, static_cast<u32>(t.height) / 4);
    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:w
                                    height:h
                                 mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget;
    d.storageMode = MTLStorageModePrivate;
    id<MTLTexture> dst = [device newTextureWithDescriptor:d];
    MTLRenderPassDescriptor *rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cmd renderCommandEncoderWithDescriptor:rp];
    const float scale[2] = {1, 1};
    [e setRenderPipelineState:dump_pipeline];
    // DC_DUMP_ALPHA: show alpha; DC_DUMP_GAIN=<x>: scale before the x/(1+x)
    // display curve
    static const float dump_opt[2] = {
        std::getenv("DC_DUMP_ALPHA") ? 1.0f : 0.0f,
        std::getenv("DC_DUMP_GAIN")
            ? std::strtof(std::getenv("DC_DUMP_GAIN"), nullptr)
            : 1.0f};
    [e setFragmentBytes:dump_opt length:sizeof(dump_opt) atIndex:1];
    [e setFragmentTexture:t atIndex:0];
    [e setFragmentBytes:scale length:sizeof(scale) atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
    id<MTLBuffer> buf =
        [device newBufferWithLength:u64(w) * h * 4
                            options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
    [b copyFromTexture:dst
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(w, h, 1)
                        toBuffer:buf
               destinationOffset:0
          destinationBytesPerRow:w * 4
        destinationBytesPerImage:u64(w) * h * 4];
    [b endEncoding];
    char name[128];
    std::snprintf(name, sizeof(name), "pass_%04zu_%s%u_%llx_d%llu",
                  pass_dumps.size(), k == 8 ? "z" : "c", k, addr, draw_count);
    pass_dumps.push_back({buf, w, h, name});
  }

  void WritePassDumps() {
    for (const auto &pd : pass_dumps) {
      char path[512];
      if (pd.raw_rgba) {
        std::snprintf(path,sizeof(path),"%s/%s.pam",dump_frames,pd.name.c_str());
        std::ofstream out(path,std::ios::binary);
        out << "P7\nWIDTH " << pd.w << "\nHEIGHT " << pd.h
            << "\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n";
        const u32 row=(pd.w*4+63)&~63u;
        const auto* pixels=static_cast<const char*>(pd.buf.contents);
        for (u32 y=0;y<pd.h;++y) out.write(pixels+u64(y)*row,pd.w*4);
        continue;
      }
      std::snprintf(path, sizeof(path), "%s/%s.ppm", dump_frames,
                    pd.name.c_str());
      std::ofstream out(path, std::ios::binary);
      out << "P6\n" << pd.w << " " << pd.h << "\n255\n";
      const auto *px = static_cast<const u8 *>(pd.buf.contents);
      for (u64 i = 0; i < u64(pd.w) * pd.h; ++i) {
        const char rgb[3] = {static_cast<char>(px[i * 4 + 2]),
                             static_cast<char>(px[i * 4 + 1]),
                             static_cast<char>(px[i * 4])};
        out.write(rgb, 3);
      }
    }
    pass_dumps.clear();
  }

  void CommitAtEncoderBoundary() {
    if(!cmd)return;
    const auto reason=CommandBudget::Due(work_since_commit,command_budget_us,
        command_created,command_budget_us ? StutterTrace::Now() : 0);
    if(reason==CommandBudget::Reason::None)return;
    if(reason==CommandBudget::Reason::ElapsedTime)++command_budget_commits;
    Commit(false);
  }

  void Commit(bool wait) {
    EndPass();
    if (!cmd) {
      if (wait) {
        WaitLast();
      }
      return;
    }
    if (!chunks.empty() || chunk) {
      // Keep upload memory alive until the GPU is done with it.
      __block std::vector<id<MTLBuffer>> keep = std::move(chunks);
      if (chunk) {
        keep.push_back(chunk);
      }
      const auto pool=upload_pool;
      [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
        pool->Return(keep);
      }];
      chunks.clear();
      chunk = nil;
      chunk_off = 0;
    }
    if (arena_va) {
      arena_commit_head = arena_head;
      const u64 head = SnapshotArena::ReleasePoint(arena_head,arena_pin);
      [cmd addCompletedHandler:^(id<MTLCommandBuffer>) {
        SnapshotArena::Publish(arena_released,head);
      }];
    }
    if (stats) {
      ++stat_commits;
      [cmd addCompletedHandler:^(id<MTLCommandBuffer> c) {
        gpu_ns += static_cast<u64>((c.GPUEndTime - c.GPUStartTime) * 1e9);
      }];
    }
    const auto state=execution;
    const std::string context="Metal submission "+std::to_string(++submission_id)+
        " frame="+std::to_string(frame_count)+" vs="+Hex(last_vs)+" ps="+Hex(last_ps)+" cs="+Hex(last_cs);
    cmd.label=@(context.c_str());
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      if(completed.status==MTLCommandBufferStatusError) {
        const char* detail=completed.error.localizedDescription.UTF8String;
        const auto message=context+": "+(detail ? detail : "command execution failed");
        if(state->Fail(message)) DC_ERROR("gpu: %s",message.c_str());
      }
      state->Notify();
    }];
    if(command_timeline) {
      const auto log=command_timeline;
      const CommandTimeline::Submission submission{submission_id,frame_count,last_vs,last_ps,last_cs,
          command_created,StutterTrace::Now(),wait};
      const auto scheduled=std::make_shared<std::atomic<double>>(0);
      [cmd addScheduledHandler:^(id<MTLCommandBuffer>) {
        scheduled->store(StutterTrace::Now(),std::memory_order_release);
      }];
      [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        log->Complete(submission,scheduled->load(std::memory_order_acquire),completed.GPUStartTime,
            completed.GPUEndTime,StutterTrace::Now(),long(completed.status),long(completed.error.code));
      }];
    }
    if(stage_counter_log && !stage_counter_passes.empty()) {
      const auto log=stage_counter_log;
      const auto begin=stage_counter_clock;
      const auto stage_submission=submission_id;
      const auto passes=std::make_shared<const std::vector<StageCounterTimeline::Pass>>(std::move(stage_counter_passes));
      stage_counter_passes.clear();
      [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        const auto end=log->Clock();
        log->Complete(stage_submission,long(completed.status),long(completed.error.code),begin,end,passes);
      }];
    }
    [cmd commit];
    last_committed = cmd;
    cmd = nil;
    work_since_commit = 0;
    if (wait) {
      WaitLast();
    }
  }

  void NoteConstDump(u64 addr, u32 bytes) {
    if(!bytes || addr>UINT64_MAX-bytes) return;
    // A new dump replaces whatever older dumps it overlaps.
    auto it = const_dumps.lower_bound(addr);
    if (it != const_dumps.begin() &&
        std::prev(it)->first + std::prev(it)->second.bytes > addr) {
      --it;
    }
    while (it != const_dumps.end() && it->first < addr + bytes) {
      it = const_dumps.erase(it);
    }
    const_dumps[addr] = {bytes, ++dump_gen};
  }

  bool InitArena() {
    u64 phys = 0;
    VAddr va = 0;
    if (Core::Memory::AllocateDirect(0, Core::Memory::kDirectSize, kArenaSize,
                                     64 << 10, 0, &phys) != 0 ||
        Core::Memory::MapDirect(
            &va, kArenaSize,
            Core::Memory::kProtCpuRead | Core::Memory::kProtCpuWrite |
                Core::Memory::kProtGpuRead,
            0, phys, 64 << 10, "gpu constant snapshots") != 0) {
      ReportOnce("cannot allocate the constant snapshot arena");
      return false;
    }
    arena_va = va;
    return true;
  }

  // Guest address of `bytes` of arena space the GPU is not using.
  u64 ArenaAlloc(u64 bytes) {
    if (!arena_va && !InitArena()) {
      return 0;
    }
    const auto plan=SnapshotArena::Allocate(kArenaSize,arena_head,
        arena_released.load(std::memory_order_acquire),bytes);
    if(plan.status!=SnapshotArena::Status::Ready) return 0;
    SnapshotArena::Publish(arena_released,plan.released);
    arena_head=plan.end;
    return arena_va+plan.begin%kArenaSize;
  }

  // Prepare all stages together, before this draw/dispatch creates an encoder.
  // A rare capacity drain is safe here; it cannot cut a half-configured draw.
  bool PatchUserData(std::span<u32* const> stages,std::vector<StaticSmrd::Snapshot>* prepared=nullptr) {
    if(prepared) prepared->clear();
    if(const_dumps.empty()) return true;
    struct Reference {u32* ud;u32 word;u64 source,offset;};
    std::vector<Reference> references;
    std::map<u64,ConstDump> needed;
    for(auto* ud:stages) for(u32 k=0;k+1<16;++k) {
      const u64 v=ud[k]|(u64(ud[k+1]&0xfff)<<32);
      auto it=const_dumps.upper_bound(v);
      if(it==const_dumps.begin()) continue;
      --it;
      const u64 offset=v-it->first;
      if(offset>=it->second.bytes) continue;
      references.push_back({ud,k,it->first,offset});needed.emplace(it->first,it->second);++k;
    }
    if(needed.empty()) return true;
    const auto fail=[&](const char* why) {execution->Fail(why);return false;};
    for(const auto& [source,dump]:needed)
      if(!Readable(source,dump.bytes)) return fail("GPU constant snapshot source is not mapped.");
    const auto reusable=[&](u64 source,const ConstDump& dump) {
      const auto it=snapshots.find(source);
      return it!=snapshots.end() && it->second.gen==dump.gen && arena_va &&
          SnapshotArena::Reusable(it->second.lease,dump.bytes,arena_head,arena_commit_head,kArenaSize) &&
          it->second.va==arena_va+it->second.lease%kArenaSize;
    };
    u64 bytes=0;
    const auto required=[&] {
      u64 sum=0;
      for(const auto& [source,dump]:needed) if(!reusable(source,dump)) {
        const auto size=SnapshotArena::Aligned(dump.bytes);
        if(!size || size>kArenaSize || sum>kArenaSize-size) return SnapshotArena::NoLease;
        sum+=size;
      }
      return sum;
    };
    bytes=required();
    if(bytes==SnapshotArena::NoLease) return fail("One draw exceeds the GPU constant snapshot capacity.");
    u64 allocation=0;
    if(bytes && !(allocation=ArenaAlloc(bytes))) {
      if(arena_pin!=SnapshotArena::NoLease) return fail("GPU constant snapshot preparation overlapped another draw.");
      // Commit every outstanding use, including the open command buffer. An
      // epoch change makes all cached entries require a fresh allocation.
      Commit(true);
      if(execution->Failed()) return false;
      arena_commit_head=arena_head;
      SnapshotArena::Publish(arena_released,arena_head);
      bytes=required();
      if(bytes==SnapshotArena::NoLease || !(allocation=ArenaAlloc(bytes)))
        return fail("GPU constant snapshots did not fit after draining queued work.");
    }
    u64 copied=0;
    const u64 begin=bytes ? arena_head-bytes : 0;
    for(const auto& [source,dump]:needed) {
      auto& snapshot=snapshots[source];
      if(!reusable(source,dump)) {
        snapshot={dump.gen,allocation+copied,begin+copied};
        std::memcpy(reinterpret_cast<void*>(snapshot.va),reinterpret_cast<const void*>(source),dump.bytes);
        copied+=SnapshotArena::Aligned(dump.bytes);
      }
      arena_pin=std::min(arena_pin,snapshot.lease);
      if(prepared) prepared->push_back({snapshot.va,dump.bytes,snapshot.gen,dump.gen,snapshot.lease,
          reinterpret_cast<const std::byte*>(snapshot.va)});
    }
    for(const auto& ref:references) {
      const u64 address=snapshots.at(ref.source).va+ref.offset;
      ref.ud[ref.word]=u32(address);
      ref.ud[ref.word+1]=(ref.ud[ref.word+1]&~0xfffu)|u32(address>>32);
    }
    return true;
  }

  // DC_GPU_PROFILE: puts the work since the last cut in its own command
  // buffer and charges its GPU time to `what`.
  void ProfileCut(std::string what) {
    if (!cmd) {
      return;
    }
    EndPass();
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> c) {
      std::scoped_lock lk(profile_lock);
      auto &e = profile_data[what];
      e.seconds += c.GPUEndTime - c.GPUStartTime;
      ++e.calls;
    }];
    Commit(false);
  }

  void ProfileReport() {
    std::vector<std::pair<std::string, ProfileEntry>> v;
    {
      std::scoped_lock lk(profile_lock);
      v.assign(profile_data.begin(), profile_data.end());
      profile_data.clear();
    }
    std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) {
      return a.second.seconds > b.second.seconds;
    });
    double total = 0;
    for (const auto &[k, e] : v) {
      total += e.seconds;
    }
    DC_INFO("gpu profile: %.1f ms GPU total", total * 1e3);
    for (size_t k = 0; k < v.size() && k < 15; ++k) {
      DC_INFO("gpu profile: %7.1f ms %6llu calls  %s",
              v[k].second.seconds * 1e3, (unsigned long long)v[k].second.calls,
              v[k].first.c_str());
    }
  }

  // Command buffers on one queue complete in order, and their completion
  // handlers run in order on Metal's serial completion queue.
  void SignalWithEffect(std::function<void()> done,SignalEffect effect,bool unknown_content=false) {
    EndPass();
    EnsureCommandBuffer();
    const u64 v = ++signal_value;
    const auto callbacks=signals;
    const auto state=execution;
    // Unknown closures may modify more than an announced fence destination.
    // Retain their invalidation after completion as well as during admission.
    // The renderer lock covers this marker and the following queue insertion.
    if(native_texture_coherence && unknown_content)
      texture_write_journal.Publish({},true,true);
    {
      std::scoped_lock lk(callbacks->lock);
      ++callbacks->enqueued_effects[static_cast<std::size_t>(effect.kind)];
      callbacks->queue.push_back({v,std::move(done),std::move(effect)});
    }
    [cmd encodeSignalEvent:signal_event value:v];
    [signal_event notifyListener:signal_listener
                         atValue:v
                           block:^(id<MTLSharedEvent> e, uint64_t) {
                             RunSignals(callbacks,state,e.signaledValue);
                           }];
  }
  void Signal(std::function<void()> done) {SignalWithEffect(std::move(done),SignalEffect{},true);}
  SignalEffect CpuWriteSignalEffect(u64 address,u64 bytes) {
    SignalEffect effect;
    // Effect ranges serve only the opt-in GPU-source admission. Keep the
    // default renderer and native-without-snapshots free of this map walk.
    if(!gpu_written_texture_snapshot || !native_indirect_enabled || !native_texture_coherence)return effect;
    if(!bytes || !PhysicalRanges(address,bytes,effect.physical) || effect.physical.empty())return effect;
    for(const auto& range:effect.physical)
      if(!range.bytes || range.address>=Core::Memory::kDirectSize ||
          range.bytes>Core::Memory::kDirectSize-range.address) {effect.physical.clear();return effect;}
    effect.kind=SignalEffect::Kind::PhysicalWrite;
    effect.mapping_generation=Core::Memory::DirectMapGeneration();
    return effect;
  }

  // Listener queue: runs, in order, the callbacks of every value reached.
  static void RunSignals(const std::shared_ptr<Signals>& callbacks,
      const std::shared_ptr<Gpu::ExecutionState>& state,u64 reached) {
    for (;;) {
      std::function<void()> fn;
      {
        std::scoped_lock lk(callbacks->lock);
        if(callbacks->closed || state->Failed()) {callbacks->queue.clear();callbacks->changed.notify_all();return;}
        if (callbacks->queue.empty() || callbacks->queue.front().value > reached) {
          return;
        }
        // All listener notifications run on one serial dispatch queue. Keep
        // the effect visible from the pop until after the closure returns.
        if(callbacks->active || callbacks->active_effect) {
          state->Fail("GPU signal listener lost its serial callback ordering.");
          callbacks->changed.notify_all();return;
        }
        fn = std::move(callbacks->queue.front().done);
        callbacks->active_effect.emplace(std::move(callbacks->queue.front().effect));
        callbacks->queue.pop_front();
        ++callbacks->active;
      }
      try {fn();}
      catch(const std::exception& error) {state->Fail(std::string("GPU completion callback failed: ")+error.what());}
      catch(...) {state->Fail("GPU completion callback failed");}
      {
        std::scoped_lock lk(callbacks->lock);
        --callbacks->active;
        callbacks->active_effect.reset();
      }
      callbacks->changed.notify_all();
    }
  }

  // Command buffers on one queue complete in order: the last one is enough.
  void WaitLast() {
    StutterTrace::Scope timing("gpu_wait",frame_count);
    if (last_committed) {
      const auto t0 = std::chrono::steady_clock::now();
      [last_committed waitUntilCompleted];
      if (stats) {
        ++stat_waits;
        stat_wait_s +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count();
      }
      last_committed = nil;
    }
  }

  // Bump-allocates transient GPU-visible memory for this command buffer.
  std::pair<id<MTLBuffer>, u64> Upload(const void *data, u64 size) {
    const u64 bytes = size;
    size = (size + 15) & ~15ull;
    if (!chunk || chunk_off + size > chunk.length) {
      if (chunk) {
        chunks.push_back(chunk);
      }
      const u64 capacity=std::max(kUploadChunk,size);
      chunk=upload_pool->Take(capacity);
      if (!chunk) {
        StutterTrace::Scope timing("upload_buffer_create",frame_count,capacity,0,1);
        chunk=[device newBufferWithLength:capacity options:MTLResourceStorageModeShared];
        ++upload_allocations;
      }
      chunk_off = 0;
    }
    const u64 off = chunk_off;
    if (data) {
      std::memcpy(static_cast<u8 *>(chunk.contents) + off, data, bytes);
    }
    chunk_off += size;
    return {chunk, off};
  }

  // ================= shaders =================

  Program* NativeProgram(Stage stage,u64 address,const u32* ud,u32 user_sgprs,
      u32 vgpr_comp_cnt,u32 compute_rsrc2,const CpuReadReady& ready) {
    const auto found=shaders.find(address^(u64(stage)<<60));
    if(found==shaders.end()) return nullptr; // decoding belongs to the drained path
    for(const auto& entry:found->second) {
      const auto& program=*entry.prog;
      if(program.num_user_sgprs!=user_sgprs || program.vgpr_comp_cnt!=vgpr_comp_cnt ||
          program.compute_rsrc2!=compute_rsrc2 || entry.code.empty()) continue;
      if(!CpuResourceRead(address,entry.code.size()*4,ready)) continue;
      if(program.fetch_ud>=0) {
        if(program.fetch_ud>=15 || entry.fetch.empty()) continue;
        const u64 fetch=u64(ud[program.fetch_ud])|(u64(ud[program.fetch_ud+1])<<32);
        if(fetch!=program.fetch_address || !CpuResourceRead(fetch,entry.fetch.size()*4,ready)) continue;
      }
      // Guard both complete cached byte spans BEFORE either comparison. A
      // queued producer can alter code just as it can alter a resource table.
      if(std::memcmp(entry.code.data(),reinterpret_cast<const void*>(address),entry.code.size()*4)) continue;
      if(program.fetch_ud>=0 && std::memcmp(entry.fetch.data(),
          reinterpret_cast<const void*>(program.fetch_address),entry.fetch.size()*4)) continue;
      return entry.prog.get();
    }
    return nullptr;
  }

  Program *GetProgram(Stage stage, u64 addr, const u32 *ud, u32 num_user_sgprs,
                      u32 vgpr_comp_cnt, u32 compute_rsrc2 = 0) {
    if (!Readable(addr, 16)) {
      return nullptr;
    }
    const u32 *code = reinterpret_cast<const u32 *>(addr);
    auto &list = shaders[addr ^ (static_cast<u64>(stage) << 60)];
    for (auto &e : list) {
      if (!Readable(addr, e.code.size() * 4) ||
          std::memcmp(e.code.data(), code, e.code.size() * 4) != 0) {
        continue;
      }
      const Program &p = *e.prog;
      if (p.num_user_sgprs!=num_user_sgprs || p.vgpr_comp_cnt!=vgpr_comp_cnt ||
          p.compute_rsrc2!=compute_rsrc2) continue;
      if (p.fetch_ud >= 0) {
        const u64 f = u64(ud[p.fetch_ud]) | (u64(ud[p.fetch_ud + 1]) << 32);
        if (f != p.fetch_address || !Readable(f, e.fetch.size() * 4) ||
            std::memcmp(e.fetch.data(), reinterpret_cast<const void *>(f),
                        e.fetch.size() * 4) != 0) {
          continue;
        }
      }
      return e.prog.get();
    }
    StutterTrace::Scope timing("shader_decode",frame_count,addr);
    Shader::BuildInput in{stage, addr, ud, num_user_sgprs, vgpr_comp_cnt};
    in.compute_rsrc2 = compute_rsrc2;
    auto prog = Shader::BuildProgram(in);
    if (!prog) {
      ReportOnce("cannot decode shader at " + std::to_string(addr));
      return nullptr;
    }
    // Only the retail shader's bounded metadata identifies a scene pass. Never
    // equate the local MSL hash or a guest address with bbport's shader IDs.
    if (code[0]==0xbeeb03ffu && Readable(addr,8)) {
      const u64 bytes=(u64(code[1])+1)*8+32;
      if (bytes<=65536 && Readable(addr,bytes))
        prog->retail_id=RetailShaderID({code,size_t(bytes/4)}).value_or(0);
    }
    if (dump_dir) {
      char path[512];
      std::snprintf(path, sizeof(path), "%s/%s_%llx_%016llx.bin", dump_dir,
                    stage == Stage::Vertex     ? "vs"
                    : stage == Stage::Fragment ? "ps"
                                               : "cs",
                    addr, prog->hash);
      const u32 size = Shader::Gcn::ProgramSize(code);
      std::ofstream(path, std::ios::binary)
          .write(reinterpret_cast<const char *>(code), size);
      if (prog->fetch_address && prog->fetch_bytes) {
        std::ofstream(std::string(path)+".fetch.bin",std::ios::binary)
            .write(reinterpret_cast<const char*>(prog->fetch_address),prog->fetch_bytes);
      }
    }
    ShaderEntry e;
    u32 bytes = 16;
    for (const auto &i : prog->code) {
      if (i.pc < Shader::kFetchPcBase) {
        bytes = std::max<u32>(bytes, i.pc + i.size * 4);
      }
    }
    e.code.assign(code, code + bytes / 4);
    if (prog->fetch_address) {
      const auto *f = reinterpret_cast<const u32 *>(prog->fetch_address);
      e.fetch.assign(f, f + prog->fetch_bytes / 4);
    }
    e.prog = std::move(prog);
    list.push_back(std::move(e));
    return list.back().prog.get();
  }

  bool ReadOnlyGuestMemory(const Program& program,const Shader::MslOptions& options) {
    if(!readonly_guest_memory_enabled || options.tess_stage!=Shader::TessStage::None ||
        options.native_tess_layout || options.rect_list ||
        options.points || options.debug_capture || options.probe_pc!=~0u ||
        options.direct_vertex_root || options.static_smrd || options.direct_smrd || options.simd_smrd)return false;
    // ShaderEntry retains every decoded version for the renderer lifetime.
    // Cache by that stable object, including its stage/fetch/SGPR context.
    auto [entry,inserted]=readonly_guest_programs.try_emplace(&program,false);
    if(inserted)entry->second=Shader::GuestMemoryReadOnlyEligible(program);
    return entry->second;
  }

  // MSL for `p`; GPU thread only (reports and dumps).
  std::string MslSource(const Program &p, const Shader::MslOptions &o,
                        u64 key) {
    StutterTrace::Scope timing("shader_msl",frame_count,p.address,key);
    std::vector<std::string> unsupported;
    std::string src = Shader::GenerateMsl(p, o, &unsupported);
    if(constant_memory_map_enabled) {
      if(ConstantMemoryMap::Apply(src))++constant_memory_map_sources;
      else ++constant_memory_map_fallbacks;
    }
    for (const auto &u : unsupported) {
      ReportOnce("shader op not translated: " + u + " at " + Hex(p.address) +
                 " stage " + std::to_string(static_cast<int>(p.stage)));
    }
    if (dump_dir) {
      char path[512];
      std::snprintf(path, sizeof(path), "%s/%s_%016llx_%016llx.metal", dump_dir,
                    p.stage == Stage::Vertex     ? "vs"
                    : p.stage == Stage::Fragment ? "ps"
                                                 : "cs",
                    p.hash, key);
      std::ofstream(path) << src;
    }
    return src;
  }

  // Moves finished pipelines into the caches.
  void TakeCompiled() {
    std::scoped_lock lk(done_lock);
    for (auto &[k, pso] : done_render) {
      pipelines[k] = pso;
      pending.erase(k);
    }
    for (auto &[k, pso] : done_compute) {
      compute_pipelines[k] = pso;
      pending.erase(k);
    }
    done_render.clear();
    done_compute.clear();
  }

  // Blocks until the pipeline `key` has been compiled.
  void WaitCompiled(u64 key) {
    StutterTrace::Scope timing("pipeline_wait",frame_count,key);
    while (pending.count(key)) {
      {
        std::unique_lock lk(done_lock);
        done_cv.wait(
            lk, [&] { return !done_render.empty() || !done_compute.empty(); });
      }
      TakeCompiled();
    }
  }

  // ================= textures =================

  // Stand-in for textures we cannot provide: white, or zero for what the
  // hardware itself reads as zero (null or INVALID-format descriptors).
  id<MTLTexture> Dummy(TextureType tt, bool zero = false) {
    const u32 key = static_cast<u32>(tt.dim) |
                    (static_cast<u32>(tt.sample) << 4) | (zero ? 0x100u : 0u);
    auto it = dummy_textures.find(key);
    if (it != dummy_textures.end()) {
      return it->second;
    }
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = tt.dim == TexDim::Tex2D        ? MTLTextureType2D
                    : tt.dim == TexDim::Tex2DArray ? MTLTextureType2DArray
                                                   : MTLTextureType3D;
    d.pixelFormat = tt.sample == TexSample::Float  ? MTLPixelFormatRGBA8Unorm
                    : tt.sample == TexSample::Uint ? MTLPixelFormatRGBA8Uint
                                                   : MTLPixelFormatRGBA8Sint;
    d.width = d.height = 1;
    if (tt.dim == TexDim::Tex3D) {
      d.depth = 1;
    } else if (tt.dim == TexDim::Tex2DArray) {
      d.arrayLength = 1;
    }
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    id<MTLTexture> t = [device newTextureWithDescriptor:d];
    const u32 white = zero                            ? 0u
                      : tt.sample == TexSample::Float ? 0xFFFFFFFFu
                                                      : 0x01010101u;
    [t replaceRegion:MTLRegionMake3D(0, 0, 0, 1, 1, 1)
          mipmapLevel:0
                slice:0
            withBytes:&white
          bytesPerRow:4
        bytesPerImage:4];
    dummy_textures[key] = t;
    return t;
  }

  // How a shader samples `f`: float, uint or sint.
  static TexSample SampleKind(MTLPixelFormat f) {
    switch (f) {
    case MTLPixelFormatR8Uint:
    case MTLPixelFormatRG8Uint:
    case MTLPixelFormatRGBA8Uint:
    case MTLPixelFormatR16Uint:
    case MTLPixelFormatRG16Uint:
    case MTLPixelFormatRGBA16Uint:
    case MTLPixelFormatR32Uint:
    case MTLPixelFormatRG32Uint:
    case MTLPixelFormatRGBA32Uint:
    case MTLPixelFormatRGB10A2Uint:
      return TexSample::Uint;
    case MTLPixelFormatR8Sint:
    case MTLPixelFormatRG8Sint:
    case MTLPixelFormatRGBA8Sint:
    case MTLPixelFormatR16Sint:
    case MTLPixelFormatRG16Sint:
    case MTLPixelFormatRGBA16Sint:
    case MTLPixelFormatR32Sint:
    case MTLPixelFormatRG32Sint:
    case MTLPixelFormatRGBA32Sint:
      return TexSample::Sint;
    default:
      return TexSample::Float;
    }
  }

  // Content checks cover every byte, including small edits to dynamic textures.
  u64 Hash64(const u8 *p, u64 size) {
    stat_hashed += size;
    return ContentHash::Bytes(p, size);
  }

  u64 ChunkHash(u64 addr, u64 size, u32 chunk) {
    const u64 off = u64(chunk) * kChunk;
    return Hash64(reinterpret_cast<const u8 *>(addr) + off,
                  std::min(kChunk, size - off));
  }

  // Optional synchronous CPU worker scan. The caller is GetTexture after
  // ReadyTextureSource has validated the complete source. Renderer ownership
  // remains held; workers only read source/expected bytes and return counters.
  bool TextureChangedObserved(TextureEntry& e,u64 addr,u64 size,bool complete) {
    const u64 before=stat_hashed;
    const u64 expected_chunks=size/kChunk+(size%kChunk!=0);
    const bool full=e.hot || complete;
    const bool eligible=full && size>=(1ull<<20) && native_texture_coherence &&
        native_indirect_enabled && bool(CurrentCpuReadReady()) &&
        expected_chunks && expected_chunks==e.chunks.size();
    bool parallel=false,changed=false;
    if(eligible && parallel_texture_hash_workers) {
      const auto result=ParallelChunkHash::Compare(reinterpret_cast<const void*>(addr),
          size,std::span<const u64>(e.chunks),parallel_texture_hash_workers);
      // This is the owner thread; no worker accesses renderer statistics.
      stat_hashed+=result.bytes;parallel=result.parallel;
      changed=!result.valid || result.changed;
      if(!result.valid)++parallel_texture_hash_invalid;
    } else {
      const u32 n=static_cast<u32>(e.chunks.size());
      if(full) {
        for(u32 c=0;c<n;++c)if(ChunkHash(addr,size,c)!=e.chunks[c]) {changed=true;break;}
      } else {
        const u32 c=e.next_chunk++%n;changed=ChunkHash(addr,size,c)!=e.chunks[c];
      }
    }
    if(texture_hash_profile_enabled)texture_hash_histogram.Note(size,full,eligible,
        parallel,changed,stat_hashed-before);
    return changed;
  }

  // True if the texture memory differs from what was uploaded.
  bool TextureChanged(TextureEntry &e, u64 addr, u64 size,bool complete=false) {
    StutterTrace::Scope timing("texture_change_check",frame_count,addr,size,1);
    if(texture_hash_profile_enabled)return TextureChangedObserved(e,addr,size,complete);
    const u32 n = static_cast<u32>(e.chunks.size());
    if (e.hot || complete) {
      for (u32 c = 0; c < n; ++c) {
        if (ChunkHash(addr, size, c) != e.chunks[c]) {
          return true;
        }
      }
      return false;
    }
    const u32 c = e.next_chunk++ % n;
    return ChunkHash(addr, size, c) != e.chunks[c];
  }

  // Decodes a T# and returns a Metal texture for it.
  id<MTLTexture> GetTexture(const u32 t[8], TextureType *type_out,
                            bool storage = false) {
    const u64 addr =
        ((u64(t[0]) | (u64(t[1] & 0xFF) << 32)) << 8) & kGpuAddrMask;
    const u32 dfmt = (t[1] >> 20) & 0x3F;
    const u32 nfmt = (t[1] >> 26) & 0xF;
    const u32 width = (t[2] & 0x3FFF) + 1;
    const u32 height = ((t[2] >> 14) & 0x3FFF) + 1;
    StutterTrace::Scope resource_timing("texture_resolve",frame_count,addr,
        (u64(width)<<32)|height,1);
    const u32 swz = t[3] & 0xFFF;
    const u32 tiling = (t[3] >> 20) & 0x1F;
    const u32 ttype = t[3] >> 28;
    const u32 depth = (t[4] & 0x1FFF) + 1;
    const u32 pitch = ((t[4] >> 13) & 0x3FFF) + 1;
    const u32 last_array = (t[5] >> 13) & 0x1FFF;
    const u32 base_array = std::min<u32>(t[5] & 0x1FFF, last_array);
    const u32 base_level = (t[3] >> 12) & 0xF;
    const u32 last_level = std::max(base_level, (t[3] >> 16) & 0xF);

    const TextureFormatInfo fi = TextureFormat(dfmt, nfmt);
    TextureType tt;
    tt.sample = fi.sample;
    if (ttype == 10) {
      tt.dim = TexDim::Tex3D;
    } else if (ttype == 11 || ttype == 12 || ttype == 13 || ttype == 15) {
      tt.dim = TexDim::Tex2DArray;
      tt.cube = ttype == 11 && !storage;
    }
    *type_out = tt;
    if (!addr || ttype < 8 || dfmt == 0) {
      return Dummy(tt, true);
    }

    // Slices a descriptor of an array covers (a cube is 6 faces from its base).
    const u32 slice_count = tt.dim != TexDim::Tex2DArray ? 1
                            : ttype == 11
                                ? std::max<u32>(6, last_array - base_array + 1)
                                : last_array - base_array + 1;

    // A level of a larger image (see MipAlias): that level of it.
    if (auto it = mip_alias.find(addr);
        it != mip_alias.end() && it->second.width == width &&
        it->second.height == height && fi.format != MTLPixelFormatInvalid &&
        PixelBytes(fi.format) == PixelBytes(it->second.fmt)) {
      if (RenderTarget *p = MipParent(addr, it->second.fmt, width, height)) {
        const u32 lv = it->second.level;
        if (storage) {
          p->initialized = true;
          p->clear_pending = false;
          p->written = ++write_seq;
          p->velocity.safe=false;
          type_out->ufloat = p->tex.pixelFormat == MTLPixelFormatRG11B10Float;
          return TargetView(*p, lv + base_level, 1, MTLPixelFormatInvalid,
                            base_array, slice_count);
        }
        tt.sample = SampleKind(p->tex.pixelFormat);
        *type_out = tt;
        return TargetView(*p, lv + base_level, last_level - base_level + 1,
                          MTLPixelFormatInvalid, base_array, slice_count, swz);
      }
    }

    // Render-to-texture: sample the render target directly.
    if (!storage) {
      RenderTarget *color = Latest(color_targets, addr, tt.dim, width, height);
      RenderTarget *depth_target = tt.dim == TexDim::Tex2D
          ? Latest(depth_targets, addr, tt.dim, width, height) : nullptr;
      if (color && (color->width != width || color->height != height)) {
        color = nullptr;
      }
      if (depth_target &&
          (depth_target->width != width || depth_target->height != height)) {
        depth_target = nullptr;
      }
      // As shadPS4's image views: the T# format (same texel size) and swizzle.
      // Shadow and colour surfaces can reuse an allocation at the SAME extent.
      // Pick the latest writer across both registries, not always colour first.
      if (RenderTarget *rt = color;
          rt && (!depth_target || rt->written >= depth_target->written)) {
        // Streaming can reuse a previous surface's address for a new texture
        // of a different size. A mismatched cached target is not a view of
        // that image; fall through to its current uploaded bytes instead.
        // An array read past the slices rendered so far: grow it (shadPS4 sizes
        // images from the T#), the new slices read as zero until written.
        if (tt.dim == TexDim::Tex2DArray &&
            rt->depth_or_layers < base_array + slice_count &&
            rt->width == width && rt->height == height) {
          rt = &GetColorTarget(addr, rt->tex.pixelFormat, width, height,
                               rt->levels, TexDim::Tex2DArray,
                               base_array + slice_count);
        }
        if (last_level >= rt->levels && rt->width == width &&
            rt->height == height) { // the T# has the levels: so does the image
          rt = &GetColorTarget(addr, rt->tex.pixelFormat, width, height,
                               last_level + 1, rt->dim, rt->depth_or_layers);
        }
        if (!fi.compressed && fi.bytes_per_block) {
          NoteMipLevels(*rt, t, fi.bytes_per_block);
        }
        MTLPixelFormat view_fmt = MTLPixelFormatInvalid;
        if (fi.format != MTLPixelFormatInvalid && !fi.compressed &&
            fi.format != rt->tex.pixelFormat && PixelBytes(fi.format) &&
            PixelBytes(fi.format) == PixelBytes(rt->tex.pixelFormat)) {
          // The descriptor controls interpretation, including integer to
          // normalized/sRGB views. Face generation uploads RGBA8Uint, then
          // samples those same bytes as RGBA8Unorm_sRGB for compositing.
          view_fmt = fi.format;
        }
        if (view_fmt == MTLPixelFormatInvalid) {
          tt.sample = SampleKind(
              rt->tex.pixelFormat); // the shader reads what the view holds
          *type_out = tt;
        }
        return TargetView(*rt, base_level, last_level - base_level + 1,
                          view_fmt, base_array, slice_count, swz);
      }
      if (RenderTarget *rt = depth_target) {
        return swz == kIdentitySwizzle
                   ? rt->tex
                   : TargetView(*rt, 0, rt->levels, MTLPixelFormatInvalid, 0,
                                1, swz);
      }
    }

    const u32 layers = tt.dim == TexDim::Tex2DArray ? base_array + slice_count
                       : tt.dim == TexDim::Tex3D    ? depth
                                                    : 1;

    if (storage) {
      // Written by shaders: keep it GPU-side like a render target, so
      // later draws sample what the compute pass produced.
      if(auto* old=Latest(color_targets,addr)) old->velocity.safe=false;
      if (fi.format == MTLPixelFormatInvalid || fi.compressed) {
        ReportOnce("storage image type/format not supported (dim=" +
                   std::to_string(ttype) + " dfmt=" + std::to_string(dfmt) +
                   " nfmt=" + std::to_string(nfmt) +
                   " addr=" + std::to_string(addr) + " " +
                   std::to_string(width) + "x" + std::to_string(height) + ")");
        return Dummy(tt);
      }
      const MTLPixelFormat f = fi.format == MTLPixelFormatRGBA8Unorm_sRGB
                                   ? MTLPixelFormatRGBA8Unorm
                                   : fi.format;
      type_out->ufloat = f == MTLPixelFormatRG11B10Float;
      // A new image starts as its guest memory (shadPS4 uploads an image
      // before a shader writes it): a shader may write only part of it, and
      // the rest is what the game put there (e.g. a color grading LUT).
      id<MTLTexture> initial = nil;
      if (!Latest(color_targets, addr, tt.dim, width, height) &&
          !fi.gpu_only) {
        TextureType mt;
        initial = GetTexture(t, &mt, false);
        if (initial.pixelFormat != f || initial.width != width >> base_level ||
            initial.textureType != (tt.dim == TexDim::Tex3D
                                        ? MTLTextureType3D
                                    : tt.dim == TexDim::Tex2DArray
                                        ? MTLTextureType2DArray
                                        : MTLTextureType2D)) {
          initial = nil;
        }
      }
      RenderTarget &target = GetColorTarget(addr, f, width, height,
                                            last_level + 1, tt.dim, layers);
      if (initial && !target.initialized) {
        EndPass();
        EnsureCommandBuffer();
        id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
        const NSUInteger n =
            std::min<NSUInteger>(initial.mipmapLevelCount,
                                 target.levels - std::min(base_level,
                                                          target.levels - 1));
        const NSUInteger slices =
            tt.dim == TexDim::Tex2DArray
                ? std::min<NSUInteger>(initial.arrayLength,
                                       target.tex.arrayLength)
                : 1;
        [b copyFromTexture:initial
                 sourceSlice:0
                 sourceLevel:0
                   toTexture:target.tex
            destinationSlice:0
            destinationLevel:base_level
                  sliceCount:slices
                  levelCount:n];
        [b endEncoding];
      }
      target.initialized = true;
      target.clear_pending = false;
      target.written = ++write_seq;
      target.velocity.safe=false;
      return TargetView(target, base_level, 1, MTLPixelFormatInvalid,
                        base_array, slice_count);
    }

    if (fi.format == MTLPixelFormatInvalid || fi.gpu_only) {
      ReportOnce("texture format dfmt=" + std::to_string(dfmt) +
                 " nfmt=" + std::to_string(nfmt) + " not supported");
      return Dummy(tt);
    }
    if (!Tiling::Supported(tiling)) {
      ReportOnce("texture tile mode " + std::to_string(tiling) +
                 " not supported yet");
      return Dummy(tt);
    }
    u64 key = Hash(addr);
    for (u32 k = 1; k < 6; ++k) {
      key = Hash(t[k], key);
    }
    TextureEntry &e = textures[key];
    // T# BASE_LEVEL..LAST_LEVEL: a view of those levels.
    auto view = [&]() -> id<MTLTexture> {
      if (base_level >= e.tex.mipmapLevelCount) return Dummy(tt);
      if (base_level == 0 && base_array == 0) {
        return e.tex;
      }
      if (!e.base_view) {
        StutterTrace::Scope timing("texture_view_create",frame_count,addr,
            (u64(base_level)<<32)|base_array,1);
        e.base_view = [e.tex
            newTextureViewWithPixelFormat:e.tex.pixelFormat
                              textureType:e.tex.textureType
                                   levels:NSMakeRange(base_level,
                                                      e.tex.mipmapLevelCount - base_level)
                                   slices:NSMakeRange(
                                              e.tex.textureType==MTLTextureType2DArray ? base_array : 0,
                                              e.tex.textureType ==
                                                         MTLTextureType2DArray
                                                     ? slice_count
                                                     : 1)];
      }
      return e.base_view;
    };
    const u64 generation = Core::Memory::DirectMapGeneration();
    const bool source_remapped=e.tex && e.mapping_generation!=generation;
    bool force_content_check=false,source_callbacks=false;
    if(native_texture_coherence && e.tex &&
        (e.mapping_generation!=generation || !texture_write_journal.Current(e.source_write_epoch))) {
      ++texture_coherence_checks;
      TextureSourceRanges(e,addr,e.size,generation);
      const auto change=texture_write_journal.Inspect(e.source_write_epoch,
          e.source_physical,e.source_physical_complete);
      force_content_check=e.mapping_generation!=generation || change.FullCheck();
      // A remap can move this VA onto a still-deferred CPU fence destination
      // whose old physical span was previously checked as disjoint. Its
      // epoch may already have been observed: drain callbacks on remap too.
      source_callbacks=change.cpu_callbacks || e.mapping_generation!=generation;
      // Unrelated writes preserve the fast path without reading/hashing any
      // texel. Completed overlapping writes remain in the bounded journal.
      if(!force_content_check)e.source_write_epoch=texture_write_journal.Epoch();
    }
    if (e.tex && e.frame == frame_count && e.mapping_generation == generation && !force_content_check) {
      // Hundreds of draws share descriptors within a frame. Target lookup
      // above still runs first, so newly GPU-written surfaces take precedence.
      // Mapping changes must revalidate the backing range before this shortcut.
      return view();
    }
    e.last_used=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const u32 width_el = fi.compressed ? (width + 3) / 4 : width;
    const u32 height_el = fi.compressed ? (height + 3) / 4 : height;
    const u32 thick = Tiling::Thickness(tiling);
    // The whole mip chain (3D textures: level 0 only), laid out like shadPS4's
    // ComputeImageSize: levels one after another, each with all its slices.
    const u32 max_levels =
        1 + static_cast<u32>(std::log2(std::max(width, height)));
    const u32 levels =
        tt.dim == TexDim::Tex3D ? 1 : std::min(last_level + 1, max_levels);
    const bool pow2 = (t[3] >> 25) & 1;
    // T# DEPTH defines the full physical array allocation. BASE/LAST_ARRAY
    // only select visible faces. In particular POW2_PAD cubes store eight
    // slices per mip even though the Metal view exposes six faces.
    const u32 storage_layers=tt.dim==TexDim::Tex3D ? layers
        : Tiling::StorageLayers(ttype,depth,pow2);
    if(layers>storage_layers) {
      ReportOnce("texture array view exceeds its physical layer allocation");
      return Dummy(tt);
    }
    Tiling::MipLevel mips[16];
    const u64 size =
        Tiling::MipLayout(tiling, pitch, height, storage_layers, levels,
                          fi.bytes_per_block, fi.compressed, pow2, mips);
    if (!Readable(addr, size)) {
      ReportOnce("texture memory not mapped");
      return Dummy(tt);
    }
    e.mapping_generation = generation;
    if (e.tex && e.frame == frame_count && !force_content_check) {
      return view();
    }
    bool gpu_source=false;id<MTLBuffer> gpu_source_buffer=nil;u64 gpu_source_offset=0;
    if(native_texture_coherence) {
      if(!e.tex) {
        TextureSourceRanges(e,addr,size,generation);
        const auto change=texture_write_journal.Inspect(e.source_write_epoch,
            e.source_physical,e.source_physical_complete);
        source_callbacks|=change.cpu_callbacks;
      }
      // Historical uncertainty still forces a new version. A complete current
      // callback-effect proof can safely copy its present bytes on the GPU;
      // it does not validate/reuse the old CPU hash baseline or skip a remap.
      bool supported=gpu_written_texture_snapshot && tt.dim==TexDim::Tex2D && !source_remapped &&
          e.tex && e.size==size && e.source_range_generation==generation && e.source_physical_complete &&
          !std::getenv("BB_METAL_CAPTURE_TEXTURE") && capture_root.empty() && dump_texture!=addr && texture_uploader &&
          texture_uploader->SupportsFormat(fi.format);
      for(u32 level=0;supported && level<levels;++level)
        supported=texture_uploader->SupportsGpuSourceTile(mips[level].tile_index);
      gpu_source=supported && GpuTextureSource(addr,size,CurrentCpuReadReady(),&gpu_source_buffer,&gpu_source_offset);
      if(!gpu_source && !ReadyTextureSource(addr,size,source_callbacks))return Dummy(tt);
      if(force_content_check)++texture_coherence_refreshes;
    }
    if (e.tex) {
      if (!gpu_source && e.hash_baseline_valid && !TextureChanged(e, addr, size,force_content_check)) {
        e.frame = frame_count;
        RememberTextureSource(e,addr,size,generation);
        return view();
      }
      e.hot = true;
    }

    StutterTrace::Scope timing("texture_upload",frame_count,addr,size);
    // replaceRegion writes CPU-side storage immediately. An older command
    // buffer may still sample the previous upload: give each changed version
    // its own texture and let Metal retain that version until its reads finish.
    // Reusing e.tex here changes the pixels of already queued frames.
    e.base_view = nil;
    {
      StutterTrace::Scope timing("texture_create",frame_count,addr,size,1);
      MTLTextureDescriptor *d = [MTLTextureDescriptor new];
      d.textureType = tt.dim == TexDim::Tex2D        ? MTLTextureType2D
                      : tt.dim == TexDim::Tex2DArray ? MTLTextureType2DArray
                                                     : MTLTextureType3D;
      d.pixelFormat = fi.format;
      d.width = width;
      d.height = height;
      d.mipmapLevelCount = levels;
      if (tt.dim == TexDim::Tex3D) {
        d.depth = layers;
      } else if (tt.dim == TexDim::Tex2DArray) {
        d.arrayLength = layers;
      }
      d.usage = MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModeManaged;
      d.swizzle =
          MTLTextureSwizzleChannelsMake(Swizzle(swz), Swizzle(swz >> 3),
                                        Swizzle(swz >> 6), Swizzle(swz >> 9));
      cached_texture_bytes-=e.allocated_bytes;
      e.tex = [device newTextureWithDescriptor:d];
      e.allocated_bytes=e.tex ? e.tex.allocatedSize : 0;
      e.version=++texture_version;
      cached_texture_bytes+=e.allocated_bytes;
      if (!e.tex) {
        return Dummy(tt);
      }
    }
    const auto *src = reinterpret_cast<const u8 *>(addr);
    static const u64 capture_texture=std::getenv("BB_METAL_CAPTURE_TEXTURE")
        ? std::strtoull(std::getenv("BB_METAL_CAPTURE_TEXTURE"),nullptr,16) : 0;
    static const bool capture_cubes=std::getenv("BB_METAL_CAPTURE_TEXTURE") &&
        std::string_view(std::getenv("BB_METAL_CAPTURE_TEXTURE"))=="bc6-cubes";
    static std::set<u64> captured_guest_textures;
    const bool capture_candidate=(capture_texture && addr==capture_texture) ||
        (capture_cubes && dfmt==40 && width==128 && height==128 && layers==6 && levels==8);
    const u64 capture_identity=capture_candidate && !capture_root.empty() && size<=(1ull<<20)
        ? Hash(addr,Fnv(src,size)) : 0;
    const bool capture_guest=capture_identity && captured_guest_textures.size()<32 &&
        captured_guest_textures.insert(capture_identity).second;
    const auto capture_stem=capture_guest ? capture_root+"/guest_texture_"+Hex(addr)+"_"+Hex(capture_identity) : std::string{};
    if(capture_guest) {
      std::ofstream(capture_stem+"_tiled.bin",std::ios::binary).write(reinterpret_cast<const char*>(src),size);
      std::ofstream info(capture_stem+".json");
      info<<"{\"address\":"<<addr<<",\"bytes\":"<<size<<",\"format\":"<<unsigned(fi.format)
          <<",\"width\":"<<width<<",\"height\":"<<height<<",\"layers\":"<<layers
          <<",\"levels\":"<<levels<<",\"descriptor\":[";
      for(unsigned k=0;k<8;++k) info<<(k?",":"")<<t[k];
      info<<"],\"scope\":\"Guest upload bytes and exact detiled planes before Metal upload\"}\n";
    }
    bool gpu_uploaded=false;
    GpuTextureUploader::InitialHashes snapshot_hashes;
    if(texture_uploader &&
       tt.dim!=TexDim::Tex3D && !capture_guest && dump_texture!=addr &&
       texture_uploader->SupportsFormat(fi.format)) {
      GpuTextureUploader::Upload upload{src,size,{}};
      for(u32 lv=0;lv<levels;++lv) for(u32 l=0;l<layers;++l)
        upload.levels.push_back({mips[lv].offset+mips[lv].slice_size*l,
            std::max(width>>lv,1u),std::max(height>>lv,1u),mips[lv].pitch_el,
            mips[lv].tile_index,tt.dim==TexDim::Tex2DArray ? l : 0,lv});
      const bool gpu_snapshot_selected=gpu_source;
      EndPass();EnsureCommandBuffer();
      StutterTrace::Scope upload_timing("texture_gpu_upload_encode",frame_count,addr,size,1);
      std::optional<StutterTrace::Scope> compressed_timing;
      if(fi.compressed)compressed_timing.emplace("texture_gpu_compressed_upload",frame_count,addr,size,1);
      if(gpu_source) {
        // Recheck capacity/mapping after EndPass (profiling may submit it).
        id<MTLBuffer> checked=nil;u64 checked_offset=0;
        if(GpuTextureSource(addr,size,CurrentCpuReadReady(),&checked,&checked_offset) &&
            checked==gpu_source_buffer && checked_offset==gpu_source_offset) {
          TrackIndirectRead(addr,size,checked,checked_offset);
        } else gpu_source=false;
      }
      GpuTextureUploader::Result result;
      if(gpu_source)result=texture_uploader->EncodeGpuSource((__bridge void*)cmd,(__bridge void*)e.tex,
          upload,(__bridge void*)gpu_source_buffer,gpu_source_offset);
      else {
        if(gpu_snapshot_selected && !ReadyTextureSource(addr,size,source_callbacks))return Dummy(tt);
        EnsureCommandBuffer();
        result=texture_uploader->Encode((__bridge void*)cmd,(__bridge void*)e.tex,upload,&snapshot_hashes);
      }
      if(result==GpuTextureUploader::Result::Fallback && gpu_source) {
        gpu_source=false;
        if(!ReadyTextureSource(addr,size,source_callbacks))return Dummy(tt);
        EnsureCommandBuffer();
        result=texture_uploader->Encode((__bridge void*)cmd,(__bridge void*)e.tex,upload,&snapshot_hashes);
      }
      stat_hashed+=snapshot_hashes.bytes_hashed;
      if(result==GpuTextureUploader::Result::Failed) {
        execution->Fail(texture_uploader->Error());
        return Dummy(tt);
      }
      gpu_uploaded=result==GpuTextureUploader::Result::Encoded;
      if(gpu_uploaded) {
        ++gpu_texture_uploads;gpu_texture_upload_bytes+=size;
        if(gpu_source) {
          ++texture_gpu_snapshots;
          if(source_callbacks)++texture_gpu_snapshot_history_versions;
        }
      }
    }
    // A selected GPU source must never reach a CPU content path without
    // first restoring CPU visibility, including future diagnostic branches.
    if(gpu_source && !gpu_uploaded) {
      gpu_source=false;
      if(!ReadyTextureSource(addr,size,source_callbacks))return Dummy(tt);
    }
    std::vector<u8> linear;
    for (u32 lv = 0; !gpu_uploaded && lv < levels; ++lv) {
      StutterTrace::Scope mip_timing("texture_mip_upload",frame_count,addr,
          (u64(lv)<<32)|layers,1);
      const Tiling::MipLevel &m = mips[lv];
      const u32 w = std::max(width >> lv, 1u), h = std::max(height >> lv, 1u);
      const u32 w_el = fi.compressed ? (w + 3) / 4 : w,
                h_el = fi.compressed ? (h + 3) / 4 : h;
      const u64 row = u64(w_el) * fi.bytes_per_block;
      const u64 image = row * h_el;
      linear.resize(image);
      const u32 level_thick = Tiling::Thickness(m.tile_index);
      for (u32 l = 0; l < layers; ++l) {
        const u64 group_offset = m.offset + m.slice_size * (l / level_thick);
        {
          StutterTrace::Scope timing("texture_detile",frame_count,addr,
              (u64(lv)<<32)|l,1);
          Tiling::DetileSlice(m.tile_index, src + group_offset, linear.data(),
                              w_el, h_el, m.pitch_el, fi.bytes_per_block,
                              l % level_thick);
        }
        if(capture_guest) {
          const auto path=capture_stem+"_level_"+std::to_string(lv)+"_slice_"+std::to_string(l)+".bin";
          std::ofstream(path,std::ios::binary).write(reinterpret_cast<const char*>(linear.data()),image);
        }
        if (dump_texture == addr && dump_frames && l == 0 &&
            lv == 0) { // DC_DUMP_TEXTURE=<addr>: raw linear bytes
          char path[512];
          std::snprintf(path, sizeof(path), "%s/tex_%llx_%ux%u_bpe%u_f%llu.raw",
                        dump_frames, addr, width_el, height_el,
                        fi.bytes_per_block, frame_count);
          std::ofstream(path, std::ios::binary)
              .write(reinterpret_cast<const char *>(linear.data()), image);
        }
        const MTLRegion region = tt.dim == TexDim::Tex3D
                                     ? MTLRegionMake3D(0, 0, l, w, h, 1)
                                     : MTLRegionMake2D(0, 0, w, h);
        StutterTrace::Scope replace_timing("texture_replace_region",frame_count,addr,
            (u64(lv)<<32)|l,1);
        [e.tex replaceRegion:region
                 mipmapLevel:lv
                       slice:tt.dim == TexDim::Tex2DArray ? l : 0
                   withBytes:linear.data()
                 bytesPerRow:row
               bytesPerImage:image];
      }
    }
    (void)thick;
    {
      StutterTrace::Scope timing("texture_hash_initial",frame_count,addr,size,1);
      e.hash_baseline_valid=!gpu_source;
      if(gpu_uploaded && gpu_source) {
        e.chunks.clear();e.next_chunk=0;
      } else if(gpu_uploaded) {
        // Match the uploaded version even if the guest rewrites its source
        // after the immutable snapshot. This scan was counted inside Encode.
        e.chunks=std::move(snapshot_hashes.chunks);
      } else {
        e.chunks.resize((size + kChunk - 1) / kChunk);
        for (u32 c = 0; c < e.chunks.size(); ++c) {
          e.chunks[c] = ChunkHash(addr, size, c);
        }
      }
    }
    e.frame = frame_count;
    e.addr = addr;
    e.size = size;
    RememberTextureSource(e,addr,size,generation);
    return view();
  }

  static u8 SamplerCompare(u32 word) {
    u8 compare = (word >> 12) & 7;
    if (std::getenv("DC_CMP_ALWAYS")) return 7;
    if (std::getenv("DC_CMP_FLIP") && compare >= 1 && compare <= 6 &&
        compare != 2 && compare != 5) {
      compare = compare == 1 ? 6 : compare == 3 ? 4 : compare == 4 ? 3 : 1;
    }
    return compare;
  }

  id<MTLSamplerState> GetSampler(const u32 s[4],bool comparison) {
    const auto requested=anisotropy.load(std::memory_order_relaxed);
    const u64 key = Hash(comparison,Hash(requested,Fnv(s, 16)));
    auto it = samplers.find(key);
    if (it != samplers.end()) {
      return it->second;
    }
    StutterTrace::Scope timing("sampler_create",frame_count,key,0,1);
    MTLSamplerDescriptor *d = [MTLSamplerDescriptor new];
    d.sAddressMode = AddressMode(s[0]);
    d.tAddressMode = AddressMode(s[0] >> 3);
    d.rAddressMode = AddressMode(s[0] >> 6);
    const u32 aniso = (s[0] >> 9) & 7;
    const u32 mag = (s[2] >> 20) & 3, min = (s[2] >> 22) & 3,
              mip = (s[2] >> 26) & 3;
    d.magFilter = (mag & 1) ? MTLSamplerMinMagFilterLinear
                            : MTLSamplerMinMagFilterNearest;
    d.minFilter = (min & 1) ? MTLSamplerMinMagFilterLinear
                            : MTLSamplerMinMagFilterNearest;
    d.mipFilter = mip == 0   ? MTLSamplerMipFilterNotMipmapped
                  : mip == 1 ? MTLSamplerMipFilterNearest
                             : MTLSamplerMipFilterLinear;
    if (min >= 2 || mag >= 2) {
      d.maxAnisotropy = std::min<u32>(16, 1u << aniso);
    }
    d.maxAnisotropy=SceneAnisotropy(requested,unsigned(d.maxAnisotropy),min,mag,mip,comparison);
    d.lodMinClamp = static_cast<float>(s[1] & 0xFFF) / 256.0f;
    d.lodMaxClamp = static_cast<float>((s[1] >> 12) & 0xFFF) / 256.0f;
    d.compareFunction = CompareFunc(SamplerCompare(s[0]));
    const u32 border = (s[3] >> 30) & 3;
    d.borderColor = border == 0   ? MTLSamplerBorderColorTransparentBlack
                    : border == 1 ? MTLSamplerBorderColorOpaqueBlack
                                  : MTLSamplerBorderColorOpaqueWhite;
    id<MTLSamplerState> st = [device newSamplerStateWithDescriptor:d];
    samplers[key] = st ? st : default_sampler;
    return samplers[key];
  }

  // ================= render targets =================

  // The variant at `addr` written last; with a size, the last one of that size
  // if there is one (aliased memory holds surfaces of several sizes, and a T#
  // means the one matching its own extent, like shadPS4's image lookup).
  static RenderTarget *Latest(TargetMap &map, u64 addr,
                              std::optional<TexDim> dim = std::nullopt,
                              u32 width = 0, u32 height = 0) {
    auto it = map.find(addr);
    if (it == map.end()) {
      return nullptr;
    }
    RenderTarget *best = nullptr;
    RenderTarget *sized = nullptr;
    for (auto &t : it->second) {
      if (dim && t->dim != *dim) {
        continue;
      }
      if (!best || t->written > best->written) {
        best = t.get();
      }
      if (width && t->width == width && t->height == height &&
          (!sized || t->written > sized->written)) {
        sized = t.get();
      }
    }
    return sized ? sized : best;
  }

  id<MTLTexture> NewTargetTexture(MTLPixelFormat fmt, u32 width, u32 height,
                                  u32 levels, bool depth,
                                  TexDim dim = TexDim::Tex2D,
                                  u32 depth_or_layers = 1) {
    StutterTrace::Scope timing("target_texture_create",frame_count,
        (u64(width)<<32)|height,(u64(fmt)<<32)|depth_or_layers,1);
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.pixelFormat = fmt;
    d.width = width;
    d.height = height;
    d.mipmapLevelCount = levels;
    d.storageMode = MTLStorageModePrivate;
    if (dim == TexDim::Tex3D) {
      d.textureType = MTLTextureType3D;
      d.depth = std::max(depth_or_layers, 1u);
      d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite |
                MTLTextureUsagePixelFormatView;
    } else if (dim == TexDim::Tex2DArray) {
      d.textureType = MTLTextureType2DArray;
      d.arrayLength = std::max(depth_or_layers, 1u);
      d.usage = depth
                    ? MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead
                    : MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                          MTLTextureUsageShaderWrite |
                          MTLTextureUsagePixelFormatView;
    } else {
      d.textureType = MTLTextureType2D;
      d.usage = depth
                    ? MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                          MTLTextureUsagePixelFormatView
                    : MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead |
                          MTLTextureUsageShaderWrite |
                          MTLTextureUsagePixelFormatView;
    }
    return [device newTextureWithDescriptor:d];
  }

  RenderTarget &GetTarget(TargetMap &map, u64 addr, MTLPixelFormat fmt,
                          u32 width, u32 height, bool depth, u32 levels = 1,
                          TexDim dim = TexDim::Tex2D, u32 depth_or_layers = 1) {
    auto &list = map[addr];
    const u32 max_levels =
        1 + static_cast<u32>(std::log2(std::max(width, height)));
    levels = std::clamp<u32>(levels, 1, max_levels);
    const bool array = dim == TexDim::Tex2DArray;
    for (auto &t : list) {
      // Arrays are one surface whatever slice range a descriptor covers: they
      // grow.
      if (t->width != width || t->height != height ||
          t->dim != dim ||
          (!array && t->depth_or_layers != depth_or_layers)) {
        continue;
      }
      if (t->tex.pixelFormat != fmt) {
        if (depth || !ColorByteViewCompatible(t->tex.pixelFormat,fmt)) continue;
        StutterTrace::Scope timing("target_reformat",frame_count,addr,fmt,1);
        id<MTLTexture> view=[t->tex newTextureViewWithPixelFormat:fmt];
        if (!view) continue;
        EndPass();
        t->tex=view;
        t->views.clear();
      }
      const u32 want_layers =
          array ? std::max(t->depth_or_layers, depth_or_layers)
                : t->depth_or_layers;
      if (t->levels < levels ||
          t->depth_or_layers < want_layers) { // grow, keeping what is there
        StutterTrace::Scope timing("target_grow",frame_count,addr,
            (u64(levels)<<32)|want_layers,1);
        const u32 new_levels = std::max(t->levels, levels);
        id<MTLTexture> tex = NewTargetTexture(fmt, width, height, new_levels,
                                              depth, dim, want_layers);
        EndPass();
        EnsureCommandBuffer();
        id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
        const u32 slices = array ? t->depth_or_layers : 1;
        for (u32 l = 0; l < t->levels; ++l) {
          [b copyFromTexture:t->tex
                   sourceSlice:0
                   sourceLevel:l
                     toTexture:tex
              destinationSlice:0
              destinationLevel:l
                    sliceCount:slices
                    levelCount:1];
        }
        [b endEncoding];
        t->tex = tex;
        t->levels = new_levels;
        t->depth_or_layers = want_layers;
        t->views.clear();
      }
      return *t;
    }
    StutterTrace::Scope timing("target_create",frame_count,addr,
        (u64(width)<<32)|height,1);
    auto rt = std::make_unique<RenderTarget>();
    rt->tex = NewTargetTexture(fmt, width, height, levels, depth, dim,
                               depth_or_layers);
    rt->addr = addr;
    rt->width = width;
    rt->height = height;
    rt->levels = levels;
    rt->dim = dim;
    rt->depth_or_layers = depth_or_layers;
    DC_INFO("gpu: %s target %#llx %ux%u (dim=%d d/l=%u) fmt %lu (shader %#llx)",
            depth ? "depth" : "render", addr, width, height, (int)dim,
            depth_or_layers, (unsigned long)fmt,
            (unsigned long long)resolving_shader);
    list.push_back(std::move(rt));
    return *list.back();
  }

  // Levels [base, base + count) and, for arrays, slices [sbase, sbase + scount)
  // of `rt` (the texture itself when that is all of it).
  static constexpr u32 kIdentitySwizzle = 0xFAC; // T# DST_SEL x=R y=G z=B w=A

  id<MTLTexture> TargetView(RenderTarget &rt, u32 base, u32 count,
                            MTLPixelFormat fmt = MTLPixelFormatInvalid,
                            u32 sbase = 0, u32 scount = ~0u,
                            u32 swz = kIdentitySwizzle) {
    if (fmt == MTLPixelFormatInvalid) {
      fmt = rt.tex.pixelFormat;
    }
    base = std::min(base, rt.levels - 1);
    count = std::clamp<u32>(count, 1, rt.levels - base);
    const u32 total = rt.tex.textureType == MTLTextureType2DArray
                          ? static_cast<u32>(rt.tex.arrayLength)
                          : 1;
    sbase = std::min(sbase, total - 1);
    scount = std::clamp<u32>(scount, 1, total - sbase);
    swz &= 0xFFF;
    if (base == 0 && count == rt.levels && sbase == 0 && scount == total &&
        fmt == rt.tex.pixelFormat && swz == kIdentitySwizzle) {
      return rt.tex;
    }
    u64 key = Hash(base | (u64(count) << 8) | (u64(fmt) << 16) |
                   (u64(sbase) << 32) | (u64(scount) << 46));
    key = Hash(swz, key);
    auto it = rt.views.find(key);
    if (it != rt.views.end()) {
      return it->second;
    }
    StutterTrace::Scope timing("target_view_create",frame_count,rt.addr,key,1);
    id<MTLTexture> v = [rt.tex
        newTextureViewWithPixelFormat:fmt
                          textureType:rt.tex.textureType
                               levels:NSMakeRange(base, count)
                               slices:NSMakeRange(sbase, scount)
                              swizzle:MTLTextureSwizzleChannelsMake(
                                          Swizzle(swz), Swizzle(swz >> 3),
                                          Swizzle(swz >> 6),
                                          Swizzle(swz >> 9))];
    rt.views[key] = v;
    return v;
  }

  RenderTarget &GetColorTarget(u64 addr, MTLPixelFormat fmt, u32 width,
                               u32 height, u32 levels = 1,
                               TexDim dim = TexDim::Tex2D,
                               u32 depth_or_layers = 1) {
    return GetTarget(color_targets, addr, fmt, width, height, false, levels,
                     dim, depth_or_layers);
  }

  RenderTarget &GetDepthTarget(u64 addr, MTLPixelFormat fmt, u32 width,
                               u32 height) {
    return GetTarget(depth_targets, addr, fmt, width, height, true);
  }

  void NoteMeta(u64 meta, u64 target, bool depth) {
    if (meta) {
      metas[meta] = {target, depth};
    }
  }

  // A fill or a clear shader rewrote metadata in [addr, addr + bytes):
  // the surfaces it describes now read as their clear value.
  bool expanding_htile = false;

  bool MarkMetaCleared(u64 addr, u64 bytes) {
    bool any = false;
    for (const auto &[meta, m] : metas) {
      if (meta < addr || meta >= addr + bytes) {
        continue;
      }
      auto it = (m.depth ? depth_targets : color_targets).find(m.target);
      if (it != (m.depth ? depth_targets : color_targets).end()) {
        for (auto &t : it->second) {
          t->clear_pending = true;
          t->velocity.safe=false;
        }
      }
      any = true;
      if (trace) {
        DC_INFO("gpu:   fast clear of %s %#llx (meta %#llx)",
                m.depth ? "depth" : "color", m.target, meta);
      }
    }
    return any;
  }

  void OnContextRegs(const u32 *body, u32 n,size_t available) {
    if (n < 2) {
      return;
    }
    const u32 reg = kContextRegBase + (body[0]&0xffff);
    const u32 count = n - 1;
    const u32 base0 = Reg::CB_COLOR0_BASE, stride = Reg::kColorTargetStride;
    if (reg >= base0 && reg < base0 + 8 * stride) {
      const u32 cb = (reg - base0) / stride, field = (reg - base0) % stride;
      if (field == Reg::CB_BASE) {
        cb_extent[cb] = available>=size_t(n)+2 && (count == 0x0E || count == 0x0D || count == 0x0B) &&
                                body[n] == kNopSizeHint
                            ? body[n + 1]
                            : 0;
      } else if (field == Reg::CB_CMASK && count == 4 && available>=size_t(n)+2 &&
                 body[n] == kNopSizeHint) {
        cb_extent[cb] = body[n + 1];
      }
    } else if (reg == Reg::DB_Z_INFO) {
      db_extent = count == 8 && available>=23 && body[21] == kNopSizeHint ? body[22] : 0;
    }
  }

  static bool HasStencil(id<MTLTexture> t) {
    return t.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
  }

  void BeginPass(RenderTarget *colors[8], RenderTarget *depth, bool clear_depth,
                 float depth_value, bool clear_stencil = false,
                 u32 stencil_value = 0) {
    EndPass();
    StutterTrace::Scope timing("render_pass_begin",frame_count,last_vs,last_ps,1);
    CommitAtEncoderBoundary(); // publish ready work before more CPU recording
    EnsureCommandBuffer();
    MTLRenderPassDescriptor *rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    PassState ps;
    u32 w = ~0u, h = ~0u;
    const u64 seq = ++write_seq;
    for (u32 k = 0; k < 8; ++k) {
      if (!colors[k]) {
        continue;
      }
      RenderTarget &rt = *colors[k];
      rp.colorAttachments[k].texture = rt.tex;
      rp.colorAttachments[k].slice = rt.bind_slice;
      rp.colorAttachments[k].level = rt.bind_level;
      rp.colorAttachments[k].loadAction = rt.initialized && !rt.clear_pending
                                              ? MTLLoadActionLoad
                                              : MTLLoadActionClear;
      rp.colorAttachments[k].clearColor =
          rt.clear_pending ? rt.clear_color
          : debug_clear    ? MTLClearColorMake(0.1, 0.1, 0.3, 1)
                           : MTLClearColorMake(0, 0, 0, 0);
      rp.colorAttachments[k].storeAction = MTLStoreActionStore;
      rt.initialized = true;
      rt.clear_pending = false;
      if (rp.colorAttachments[k].loadAction == MTLLoadActionClear)
        rt.written = seq;
      ps.color[k] = reinterpret_cast<u64>(&rt);
      ps.slice[k] = rt.bind_slice | (rt.bind_level << 24);
      w = std::min(w, std::max(rt.width >> rt.bind_level, 1u));
      h = std::min(h, std::max(rt.height >> rt.bind_level, 1u));
    }
    if (depth) {
      const bool clear =
          clear_depth || depth->clear_pending || !depth->initialized;
      if (clear) ++depth->depth_revision;
      rp.depthAttachment.texture = depth->tex;
      rp.depthAttachment.loadAction =
          clear ? MTLLoadActionClear : MTLLoadActionLoad;
      rp.depthAttachment.clearDepth = clear_depth ? depth_value
                                      : depth->clear_pending
                                          ? depth->clear_depth
                                          : 1.0;
      rp.depthAttachment.storeAction = MTLStoreActionStore;
      if (HasStencil(depth->tex)) {
        const bool sclear =
            clear_stencil || depth->clear_pending || !depth->initialized;
        rp.stencilAttachment.texture = depth->tex;
        rp.stencilAttachment.loadAction =
            sclear ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.stencilAttachment.clearStencil = clear_stencil ? stencil_value
                                            : depth->clear_pending
                                                ? depth->clear_stencil
                                                : 0;
        rp.stencilAttachment.storeAction = MTLStoreActionStore;
      }
      depth->initialized = true;
      depth->clear_pending = false;
      // Loading an attachment preserves its old contents. Promoting it here
      // would hide a newer colour/depth alias of the same guest allocation.
      // This registry resolves the Z allocation. Stencil has a separate
      // guest base, so a stencil-only clear does not replace its depth bytes.
      if (clear) depth->written = seq;
      ps.depth = reinterpret_cast<u64>(depth);
      w = std::min(w, depth->width);
      h = std::min(h, depth->height);
    }
    pass_width = w;
    pass_height = h;
    rp.renderTargetWidth = w;
    rp.renderTargetHeight = h;
    if(stage_counter_log) {
      StageCounterTimeline::Pass metadata;
      metadata.frame=frame_count;metadata.width=w;metadata.height=h;
      metadata.depth=depth ? depth->addr : 0;
      metadata.depth_format=depth ? depth->tex.pixelFormat : MTLPixelFormatInvalid;
      for(u32 k=0;k<8;++k)if(colors[k]) {
        ++metadata.colors;metadata.formats[k]=colors[k]->tex.pixelFormat;
        if(!k)metadata.color0=colors[k]->addr;
      }
      stage_counter_pass=stage_counter_log->Attach(rp,std::move(metadata));
    }
    {
      StutterTrace::Scope encoder_timing("render_encoder_create",frame_count,last_vs,last_ps,1);
      enc = [cmd renderCommandEncoderWithDescriptor:rp];
      if(!enc && stage_counter_pass) {
        stage_counter_log->Cancel(rp,*stage_counter_pass);stage_counter_pass.reset();
        enc = [cmd renderCommandEncoderWithDescriptor:rp];
      }
    }
    binding_cache.Reset(enc && cmd.retainedReferences);
    pass = ps;
    if (dump_passes && trace) {
      for (u32 k = 0; k < 8; ++k) {
        pass_tex[k] = colors[k] ? colors[k]->tex : nil;
        pass_addr[k] = colors[k] ? colors[k]->addr : 0;
      }
      pass_tex[8] = depth ? depth->tex : nil;
      pass_addr[8] = depth ? depth->addr : 0;
    }
  }

  // ================= draws =================

  struct TessBuffers {
    id<MTLBuffer> lds = nil, factors = nil;
    u64 lds_offset = 0, factor_offset = 0;
    NativeTessellationConfig native;
    std::array<u32,16> local_ud{},hull_ud{};
    Shader::GpuTessConsts params{};
  };

  bool PrepareTessellation(const RegFile& r, const DrawCall& dc,u64 domain_hash,
                           bool allow_native,TessBuffers& out,const u32* local_ud,const u32* hull_ud) {
    const u32 config = r[Reg::VGT_LS_HS_CONFIG], tf = r[Reg::VGT_TF_PARAM];
    // The light-volume shaders use one input/output control point and keep
    // their attributes in LDS. Other patch layouts still require integration.
    if (Bits(config,8,6)!=1 || Bits(config,14,6)!=1 || Bits(tf,0,2)!=2 ||
        Bits(tf,2,3)>3 || Bits(tf,5,3)<2 || Bits(tf,5,3)>3 ||
        (dc.indexed && dc.index_size!=2 && dc.index_size!=4) || dc.instances>1 || dc.count>4096) {
      ReportOnce("tessellation layout not supported: config=" + Hex(config) + " tf=" + Hex(tf)+
          " count="+std::to_string(dc.count)+" indexed="+std::to_string(dc.indexed)+
          " instances="+std::to_string(dc.instances));
      return false;
    }
    if (!dc.count) return false;
    const u64 index_base=dc.index_base+u64(dc.index_offset)*dc.index_size;
    if (dc.indexed && !Readable(index_base,u64(dc.count)*dc.index_size)) {
      ReportOnce("tessellation index buffer is not mapped"); return false;
    }
    if (trace) DC_INFO("gpu: tess count=%u idx=%d LSrsrc2=%#x HSrsrc2=%#x",dc.count,dc.indexed,
        r[Reg::SPI_SHADER_PGM_LO_LS+3],r[Reg::SPI_SHADER_PGM_LO_HS+3]);
    Program* programs[2]{};
    Resources resources[2];
    std::vector<TextureType> types[2];
    const u32* user[2]{};
    id<MTLComputePipelineState> stages[2]{};
    const u32 registers[]{Reg::SPI_SHADER_PGM_LO_LS,Reg::SPI_SHADER_PGM_LO_HS};
    for (u32 k=0;k<2;++k) {
      const u32 lo=registers[k];
      const u64 address=r.ProgramAddr(lo);
      user[k]=k ? hull_ud : local_ud;
      programs[k]=GetProgram(Stage::Vertex,address,user[k],Bits(r[lo+3],1,5),
                            Bits(r[lo+2],24,2));
      if (!programs[k]) return false;
    }
    if(domain_hash==0xc4b45ba023e435cfull) CaptureFogInputs(programs[0],user[0],"local");
    const auto native=native_tessellation && allow_native
        ? NativeTessellation(programs[0]->hash,programs[1]->hash,domain_hash,
            native_tessellation_extended,native_tessellation_inner_two)
        : NativeTessellationConfig{};
    if (native && programs[0]->num_user_sgprs==6 && programs[1]->num_user_sgprs==8) {
      const u32 slot=native.inner_factor==2 ? 2 : native.outer_factor==3 ? 1 : 0;
      if (!native_tess_factors[slot]) {
        // One immutable factor record for all patches. Hardware tessellation,
        // winding and partitioning are retained exactly; no readback or compute.
        std::array<std::uint16_t,128> factors{};
        const auto record=NativeTessellationFactorRecord(native);
        std::copy(record.begin(),record.end(),factors.begin());
        native_tess_factors[slot]=[device newBufferWithBytes:factors.data() length:sizeof(factors)
            options:MTLResourceStorageModeShared];
      }
      if (native_tess_factors[slot]) {
        out.native=native;out.factors=native_tess_factors[slot];
        out.params={r[Reg::VGT_INDX_OFFSET],dc.count,dc.indexed ? dc.index_size : 0u,0,dc.indexed ? index_base : 0};
        std::memcpy(out.local_ud.data(),user[0],64);std::memcpy(out.hull_ud.data(),user[1],64);
        ++native_tess_draws;
        return true;
      }
    }
    ++translated_tess_draws;
    for (u32 k=0;k<2;++k) {
      const u64 address=r.ProgramAddr(registers[k]);
      Resolve(*programs[k],user[k],types[k],resources[k]);
      Shader::MslOptions options;
      options.tess_stage=k==0 ? Shader::TessStage::Local : Shader::TessStage::Hull;
      options.textures=types[k];
      u64 key=Hash(options.tess_stage,Hash(programs[k]->hash));
      key=Hash(programs[k]->num_user_sgprs,key);
      key=Fnv(types[k].data(),types[k].size()*sizeof(TextureType),key);
      auto found=tess_pipelines.find(key);
      if (found==tess_pipelines.end()) {
        std::vector<std::string> unsupported;
        Shader::GenerateMsl(*programs[k],options,&unsupported);
        if (!unsupported.empty()) {
          ReportOnce("tessellation shader not translated at " + Hex(address) + ": " + unsupported.front());
          return false;
        }
        id<MTLFunction> fn=compiler->Function(key,MslSource(*programs[k],options,key),address);
        NSError* error=nil;
        if(fn) {
          // These translated LS/HS kernels need the same source-recipe replay
          // as ordinary compute pipelines. The direct device convenience API
          // bypassed persistence, repeating first-use compilation each launch.
          auto descriptor=[MTLComputePipelineDescriptor new];
          descriptor.computeFunction=fn;
          StutterTrace::Scope timing("tessellation_pipeline",frame_count,programs[k]->hash,k);
          stages[k]=compiler->ComputePipeline(descriptor,&error);
        }
        if (!stages[k]) {
          ReportOnce("tessellation pipeline failed at " + Hex(address) + ": " +
              (error ? error.localizedDescription.UTF8String : "shader compile failed"));
          return false;
        }
        tess_pipelines.emplace(key,stages[k]);
      } else stages[k]=found->second;
    }
    EndPass();
    EnsureCommandBuffer();
    const u64 lds_bytes=u64(dc.count)*Shader::kTessLdsWords*4;
    auto [lds,lds_offset]=Upload(nullptr,lds_bytes);
    auto [factors,factor_offset]=Upload(nullptr,u64(dc.count)*12);
    auto clear=[cmd blitCommandEncoder];
    [clear fillBuffer:lds range:NSMakeRange(lds_offset,lds_bytes) value:0];
    [clear fillBuffer:factors range:NSMakeRange(factor_offset,u64(dc.count)*12) value:0];
    [clear endEncoding];
    UpdateMemMap();
    const Shader::GpuTessConsts params{r[Reg::VGT_INDX_OFFSET],dc.count,
        dc.indexed ? dc.index_size : 0u,0,dc.indexed ? index_base : 0};
    auto compute=[cmd computeCommandEncoder];
    if(!compute) return false;
    for (u32 k=0;k<2;++k) {
      u32 ud[16];
      std::memcpy(ud,user[k],sizeof(ud));
      if (k) [compute memoryBarrierWithScope:MTLBarrierScopeBuffers];
      [compute setComputePipelineState:stages[k]];
      [compute setBytes:ud length:sizeof(ud) atIndex:Shader::Binding::UserData];
      [compute setBuffer:mem[0] offset:0 atIndex:Shader::Binding::Mem0];
      [compute setBuffer:mem[1] offset:0 atIndex:Shader::Binding::Mem1];
      [compute setBuffer:mem_map offset:0 atIndex:Shader::Binding::MemMap];
      [compute setBuffer:lds offset:lds_offset atIndex:Shader::Binding::TessLds];
      [compute setBuffer:factors offset:factor_offset atIndex:Shader::Binding::TessFactors];
      [compute setBytes:&params length:sizeof(params) atIndex:Shader::Binding::TessParams];
      for (u32 t=0;t<resources[k].tex.size();++t) [compute setTexture:resources[k].tex[t] atIndex:t];
      for (u32 s=0;s<resources[k].smp.size();++s) [compute setSamplerState:resources[k].smp[s] atIndex:s];
      // The generated prep kernels write LDS/factors only to the distinct
      // Upload buffers bound above. Their guest stores still use ordinary
      // reflected buffer/image instructions and the already patched stage UD.
      TrackProgramWriter(*programs[k],ud);
      [compute dispatchThreads:MTLSizeMake(dc.count,1,1) threadsPerThreadgroup:MTLSizeMake(32,1,1)];
    }
    if(native_indirect_enabled && !programs[0]->buffer_writes.empty() && !programs[1]->buffer_writes.empty()) {
      TraceProgramWriter(*programs[0],512,"two-writing-stages");
      TraceProgramWriter(*programs[1],512,"two-writing-stages");TrackGuestWriter({},true);
    }
    [compute endEncoding];
    out.lds=lds;out.factors=factors;out.lds_offset=lds_offset;out.factor_offset=factor_offset;
    return true;
  }

  std::unordered_map<u64,id<MTLComputePipelineState>> tess_pipelines;

  // DB_DEPTH_CONTROL + DB_STENCIL_CONTROL + DB_STENCILREFMASK(_BF), the way
  // shadPS4's UpdateDepthStencilState reads them.
  id<MTLDepthStencilState> GetDepthState(u32 db_control, bool depth_clear,
                                         bool stencil, u32 st_control,
                                         u32 ref_front, u32 ref_back,
                                         bool stencil_clear) {
    const bool backface = (db_control >> 7) & 1;
    if (!backface) {
      ref_back = ref_front;
    }
    u64 key = Hash(db_control & 0xFFF00FFFu);
    key = Hash(depth_clear, key);
    if (stencil) {
      key = Hash(st_control, key);
      key = Hash(ref_front & 0x00FFFF00u,
                 key); // masks; the reference is set per draw
      key = Hash(ref_back & 0x00FFFF00u, key);
      key = Hash(stencil_clear, key);
    }
    auto it = depth_states.find(key);
    if (it != depth_states.end()) {
      return it->second;
    }
    StutterTrace::Scope timing("depth_state_create",frame_count,key,0,1);
    MTLDepthStencilDescriptor *d = [MTLDepthStencilDescriptor new];
    const bool z_enable = (db_control >> 1) & 1;
    d.depthCompareFunction =
        z_enable ? CompareFunc(db_control >> 4) : MTLCompareFunctionAlways;
    d.depthWriteEnabled = z_enable && ((db_control >> 2) & 1) && !depth_clear;
    if (stencil && (db_control & 1)) {
      auto face = [&](u32 fail, u32 zpass, u32 zfail, u32 func, u32 ref) {
        MTLStencilDescriptor *f = [MTLStencilDescriptor new];
        f.stencilFailureOperation = StencilOp(fail);
        f.depthStencilPassOperation = StencilOp(zpass);
        f.depthFailureOperation = StencilOp(zfail);
        f.stencilCompareFunction = CompareFunc(func);
        f.readMask = (ref >> 8) & 0xFF;
        f.writeMask = stencil_clear ? 0 : (ref >> 16) & 0xFF;
        return f;
      };
      d.frontFaceStencil = face(st_control, st_control >> 4, st_control >> 8,
                                db_control >> 8, ref_front);
      d.backFaceStencil =
          backface ? face(st_control >> 12, st_control >> 16, st_control >> 20,
                          db_control >> 20, ref_back)
                   : face(st_control, st_control >> 4, st_control >> 8,
                          db_control >> 8, ref_front);
    }
    id<MTLDepthStencilState> st = [device newDepthStencilStateWithDescriptor:d];
    depth_states[key] = st;
    return st;
  }

  // GCN REPLACE_OP writes STENCILOPVAL, REPLACE_TEST the test value (shadPS4: a
  // face with a ReplaceOp takes its reference from op_val).
  static u32 StencilRef(u32 st_ops, u32 refmask) {
    const bool op = (st_ops & 0xF) == 4 || ((st_ops >> 4) & 0xF) == 4 ||
                    ((st_ops >> 8) & 0xF) == 4;
    return op ? refmask >> 24 : refmask & 0xFF;
  }

  struct Resources {
    std::vector<id<MTLTexture>> tex;
    std::vector<id<MTLSamplerState>> smp;
  };
  void ObserveCamera(const Program& program,const u32* ud,float viewport_y) {
    if (!temporal || temporal->HasCamera()) return;
    for (const auto& source:program.buffers) {
      u32 descriptor[4];
      for (unsigned k=0;k<4;++k) descriptor[k]=Shader::ResolveSgpr(source[k],ud);
      const u64 stride=(descriptor[1]>>16)&0x3fff;
      if (u64(descriptor[2])*std::max<u64>(1,stride)!=864) continue;
      const u64 base=(u64(descriptor[0]) | (u64(descriptor[1]&0xfff)<<32)) & kGpuAddrMask;
      if (!Readable(base,864)) continue;
      std::array<float,216> data;
      std::memcpy(data.data(),reinterpret_cast<const void*>(base),sizeof(data));
      if (auto camera=ReadSceneCamera(data,viewport_y)) {scene_camera=*camera;scene_camera_frame=frame_count;temporal->ObserveCamera(*camera);}
    }
  }
  // Evaluates each image instruction's descriptors against the user data.
  void Resolve(const Program &p, const u32 *ud, std::vector<TextureType> &types,
               Resources &out) {
    StutterTrace::Scope timing("resource_resolve",frame_count,p.address,p.images.size(),1);
    std::vector<std::array<u32, 4>> ssharps;
    std::vector<bool> comparison(p.samplers.size(),false);
    for (const auto &ss : p.samplers) {
      std::optional<StutterTrace::Scope> descriptor_timing;
      if(FrameWork::Enabled())descriptor_timing.emplace("descriptor_resolve",frame_count,p.address,4);
      std::array<u32, 4> s{};
      for (u32 k = 0; k < 4; ++k) {
        s[k] = Shader::ResolveSgpr(ss[k], ud);
      }
      ssharps.push_back(s);
    }
    for (const auto &img : p.images) {
      u32 t[8];
      {
        std::optional<StutterTrace::Scope> descriptor_timing;
        if(FrameWork::Enabled())descriptor_timing.emplace("descriptor_resolve",frame_count,p.address,8);
        for (u32 k = 0; k < 8; ++k)t[k] = Shader::ResolveSgpr(img.tsharp[k], ud);
      }
      TextureType tt;
      id<MTLTexture> tex = GetTexture(t, &tt, img.storage);
      if (trace && (t[0] == 0 || std::getenv("DC_TRACE_TSHARP"))) {
        DC_INFO("gpu:   T# %08x %08x %08x %08x %08x %08x %08x %08x src kind %u "
                "ud %u off %u",
                t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7],
                img.tsharp[0].kind, img.tsharp[0].ud, img.tsharp[0].offset);
      }
      if (trace) {
        DC_INFO("gpu:   tex%zu addr=%#llx dfmt=%u nfmt=%u %ux%u tile=%u "
                "type=%u swz=%#x%s",
                out.tex.size(), (u64(t[0]) | (u64(t[1] & 0xFF) << 32)) << 8,
                (t[1] >> 20) & 0x3F, (t[1] >> 26) & 0xF, (t[2] & 0x3FFF) + 1,
                ((t[2] >> 14) & 0x3FFF) + 1, (t[3] >> 20) & 0x1F, t[3] >> 28,
                t[3] & 0xFFF, img.storage ? " storage" : "");
        DC_INFO("gpu:     bound: type %lu %lux%lu layers %lu levels %lu fmt "
                "%lu base_array %u last_array %u",
                (unsigned long)tex.textureType, (unsigned long)tex.width,
                (unsigned long)tex.height, (unsigned long)tex.arrayLength,
                (unsigned long)tex.mipmapLevelCount,
                (unsigned long)tex.pixelFormat, t[5] & 0x1FFF,
                (t[5] >> 13) & 0x1FFF);
      }
      if (img.compare && img.has_sampler &&
          img.sampler_index < ssharps.size()) {
        comparison[img.sampler_index]=true;
        tt.compare = SamplerCompare(ssharps[img.sampler_index][0]);
        tt.native_depth = tex.pixelFormat == MTLPixelFormatDepth16Unorm ||
            tex.pixelFormat == MTLPixelFormatDepth32Float ||
            tex.pixelFormat == MTLPixelFormatDepth32Float_Stencil8;
        if (trace) {
          const auto &ss = ssharps[img.sampler_index];
          DC_INFO("gpu:   compare sampler S# %08x %08x %08x %08x func %u",
                  ss[0], ss[1], ss[2], ss[3], tt.compare);
        }
      }
      types.push_back(tt);
      out.tex.push_back(tex);
    }
    for (size_t i=0;i<ssharps.size();++i) {
      out.smp.push_back(GetSampler(ssharps[i].data(),comparison[i]));
    }
  }

  // The target whose mip level lives at `addr` with this size and format.
  RenderTarget *MipParent(u64 addr, MTLPixelFormat fmt, u32 width, u32 height) {
    auto it = mip_alias.find(addr);
    if (it == mip_alias.end() || it->second.fmt != fmt ||
        it->second.width != width || it->second.height != height) {
      return nullptr;
    }
    RenderTarget *p = Latest(color_targets, it->second.parent, std::nullopt,
                             width << it->second.level,
                             height << it->second.level);
    return p && p->levels > it->second.level && p->tex.pixelFormat == fmt
               ? p
               : nullptr;
  }

  // shadPS4 MipOf for a sampled render target: each level past the first of
  // the T#'s memory becomes an alias; a target already rendered there is
  // copied into that level and retired.
  void NoteMipLevels(RenderTarget &rt, const u32 t[8], u32 bytes_per_el) {
    const u32 tiling = (t[3] >> 20) & 0x1F;
    const u32 last_level = (t[3] >> 16) & 0xF;
    if (!last_level || rt.levels <= 1 || !Tiling::Supported(tiling)) {
      return;
    }
    StutterTrace::Scope timing("target_mip_alias",frame_count,rt.addr,rt.levels,1);
    // ImageInfo NumLayers: depth + 1 slices (cubes: full cubes), pow2 padded.
    const u32 ttype = t[3] >> 28;
    const bool pow2 = (t[3] >> 25) & 1;
    const u32 layers = Tiling::StorageLayers(ttype,(t[4] & 0x1FFF)+1,pow2);
    const u32 pitch = ((t[4] >> 13) & 0x3FFF) + 1;
    const u32 levels = std::min(last_level + 1, rt.levels);
    Tiling::MipLevel mips[16];
    Tiling::MipLayout(tiling, pitch, rt.height, layers, levels, bytes_per_el,
                      false, pow2, mips);
    for (u32 m = 1; m < levels; ++m) {
      const u64 a = rt.addr + mips[m].offset;
      MipAlias alias{rt.addr, m, std::max(rt.width >> m, 1u),
                     std::max(rt.height >> m, 1u), rt.tex.pixelFormat};
      auto [it, added] = mip_alias.try_emplace(a, alias);
      if (!added) {
        continue;
      }
      // Already rendered as a target of its own: move its slices in.
      auto list = color_targets.find(a);
      if (list == color_targets.end()) {
        continue;
      }
      auto &v = list->second;
      for (size_t i = 0; i < v.size();) {
        RenderTarget &c = *v[i];
        if (c.width != alias.width || c.height != alias.height ||
            c.tex.pixelFormat != alias.fmt || c.levels < 1) {
          ++i;
          continue;
        }
        if (c.initialized) {
          const u32 n = std::min<u32>(
              c.dim == TexDim::Tex2DArray ? c.depth_or_layers : 1,
              rt.dim == TexDim::Tex2DArray ? rt.depth_or_layers : 1);
          EndPass();
          EnsureCommandBuffer();
          id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
          [b copyFromTexture:c.tex
                   sourceSlice:0
                   sourceLevel:0
                     toTexture:rt.tex
              destinationSlice:0
              destinationLevel:m
                    sliceCount:n
                    levelCount:1];
          [b endEncoding];
          rt.written = std::max(rt.written, c.written);
        }
        DC_INFO("gpu: target %#llx %ux%u is level %u of %#llx", a, c.width,
                c.height, m, rt.addr);
        retired.push_back(std::move(v[i]));
        v.erase(v.begin() + i);
      }
      if (v.empty()) {
        color_targets.erase(list);
      }
    }
  }

  // Preserve queued guest writes before the first render bind of a 32-bit
  // color surface. CPU uploads would race preceding compute buffer clears.
  void InitializeColorFromGuest(RenderTarget& rt, u32 tile, u32 pitch) {
    if (rt.initialized || rt.clear_pending || rt.dim!=TexDim::Tex2D || rt.levels!=1 ||
        PixelBytes(rt.tex.pixelFormat)!=4 || pitch<rt.width || !detile_pipeline ||
        !Tiling::Supported(tile) || Tiling::Thickness(tile)!=1) return;
    StutterTrace::Scope timing("color_guest_init",frame_count,rt.addr,
        (u64(tile)<<32)|pitch,1);
    const u64 bytes=Tiling::SliceSize(tile,pitch,rt.height,4);
    id<MTLBuffer> source=nil; u64 offset=0;
    if (!bytes || bytes>UINT32_MAX || !Locate(rt.addr,bytes,&source,&offset)) return;
    u64 key=Hash(tile); key=Hash(pitch,key); key=Hash(rt.width,key); key=Hash(rt.height,key);
    auto& offsets=detile_maps[key];
    if (!offsets) {
      StutterTrace::Scope map_timing("color_detile_map_build",frame_count,rt.addr,bytes,1);
      std::vector<u32> indexed(bytes/4),linear(u64(rt.width)*rt.height);
      for (u32 i=0;i<indexed.size();++i) indexed[i]=i;
      Tiling::DetileSlice(tile,reinterpret_cast<const u8*>(indexed.data()),
          reinterpret_cast<u8*>(linear.data()),rt.width,rt.height,pitch,4);
      StutterTrace::Scope allocation_timing("color_detile_map_buffer",frame_count,rt.addr,
          linear.size()*4,1);
      offsets=[device newBufferWithBytes:linear.data() length:linear.size()*4 options:MTLResourceStorageModeShared];
    }
    const u32 row=(rt.width*4+255)&~255u;
    id<MTLBuffer> linear;
    {
      StutterTrace::Scope allocation_timing("color_guest_staging",frame_count,rt.addr,
          u64(row)*rt.height,1);
      linear=[device newBufferWithLength:u64(row)*rt.height options:MTLResourceStorageModePrivate];
    }
    StutterTrace::Scope encode_timing("color_guest_encode",frame_count,rt.addr,bytes,1);
    EndPass(); EnsureCommandBuffer();
    auto e=[cmd computeCommandEncoder];
    [e setComputePipelineState:detile_pipeline];
    [e setBuffer:source offset:offset atIndex:0];
    [e setBuffer:offsets offset:0 atIndex:1];
    [e setBuffer:linear offset:0 atIndex:2];
    const u32 layout[4]={rt.width,rt.height,row/4,0};
    [e setBytes:layout length:sizeof(layout) atIndex:3];
    [e dispatchThreads:MTLSizeMake(u64(rt.width)*rt.height,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
    [e endEncoding];
    auto b=[cmd blitCommandEncoder];
    [b copyFromBuffer:linear sourceOffset:0 sourceBytesPerRow:row sourceBytesPerImage:u64(row)*rt.height
        sourceSize:MTLSizeMake(rt.width,rt.height,1) toTexture:rt.tex destinationSlice:0 destinationLevel:0
        destinationOrigin:MTLOriginMake(0,0,0)];
    [b endEncoding];
    rt.initialized=true; rt.written=++write_seq;
  }

  // Color target `k` as the CB registers describe it; notes its CMASK and
  // clear color for fast clears.
  RenderTarget &ColorTargetFor(const RegFile &r, u32 k) {
    const u32 info = r.ColorTarget(k, Reg::CB_INFO);
    const u64 base = u64(r.ColorTarget(k, Reg::CB_BASE)) << 8;
    u32 width = ((r.ColorTarget(k, Reg::CB_PITCH) & 0x7FF) + 1) * 8;
    const u32 slice = ((r.ColorTarget(k, Reg::CB_SLICE) & 0x3FFFFF) + 1) * 64;
    u32 height = slice / width;
    if (cb_extent[k]) { // the real size; pitch and slice are padded
      width = cb_extent[k] & 0xFFFF;
      height = cb_extent[k] >> 16;
    }
    const ColorFormatInfo cf =
        ColorTargetFormat(ColorInfo::Format(info), ColorInfo::NumberType(info), ColorInfo::CompSwap(info));
    width = std::max(width, 1u);
    height = std::max(height, 1u);
    // Rendering into one slice of an array (cube faces):
    // CB_COLOR_VIEW.SLICE_START.
    const u32 view = r.ColorTarget(k, Reg::CB_VIEW);
    const u32 slice_start = view & 0x7FF, slice_max = (view >> 13) & 0x7FF;
    if (RenderTarget *p = MipParent(base, cf.format, width, height)) {
      p->bind_slice = slice_start;
      p->bind_level = mip_alias[base].level;
      p->clear_color = ClearColor(info, r.ColorTarget(k, Reg::CB_CLEAR_WORD0),
                                  r.ColorTarget(k, Reg::CB_CLEAR_WORD1));
      return *p;
    }
    bool array = slice_start || slice_max;
    if (!array) {
      if (RenderTarget *a = Latest(color_targets, base, TexDim::Tex2DArray)) {
        array = a->width == width && a->height == height &&
                a->tex.pixelFormat == cf.format;
      }
    }
    RenderTarget &rt = array
                           ? GetColorTarget(base, cf.format, width, height, 1,
                                            TexDim::Tex2DArray, slice_start + 1)
                           : GetColorTarget(base, cf.format, width, height);
    rt.bind_slice = array ? slice_start : 0;
    rt.bind_level = 0;
    rt.clear_color = ClearColor(info, r.ColorTarget(k, Reg::CB_CLEAR_WORD0),
                                r.ColorTarget(k, Reg::CB_CLEAR_WORD1));
    if (!ColorInfo::FastClear(info))
      InitializeColorFromGuest(rt,r.ColorTarget(k,Reg::CB_ATTRIB)&31,
          ((r.ColorTarget(k,Reg::CB_PITCH)&0x7ff)+1)*8);
    if (ColorInfo::FastClear(info)) {
      // The SDK can fill CMASK before the first bind registers this surface.
      // A new target must therefore inherit its programmed clear value rather
      // than defaulting to transparent black. Blood maps use neutral alpha 1.
      if (!rt.initialized) {
        rt.clear_pending=true;
        if (trace) DC_INFO("gpu: initial fast clear %#llx words %08x %08x -> %g %g %g %g",
            base,r.ColorTarget(k,Reg::CB_CLEAR_WORD0),r.ColorTarget(k,Reg::CB_CLEAR_WORD1),
            rt.clear_color.red,rt.clear_color.green,rt.clear_color.blue,rt.clear_color.alpha);
      }
      NoteMeta(u64(r.ColorTarget(k, Reg::CB_CMASK)) << 8, base, false);
    }
    return rt;
  }

  // CB_COLOR_CONTROL.MODE = eliminate: tiles still fast-cleared get the
  // clear color. Here: a target with a pending clear is cleared now.
  void EliminateFastClear(const RegFile &r) {
    const u32 info = r.ColorTarget(0, Reg::CB_INFO);
    if (!ColorInfo::Format(info) || !r.ColorTarget(0, Reg::CB_BASE)) {
      return;
    }
    RenderTarget &rt = ColorTargetFor(r, 0);
    if (!rt.clear_pending) {
      return;
    }
    RenderTarget *colors[8] = {&rt};
    BeginPass(colors, nullptr, false, 1.0f);
    EndPass();
  }

  // True if `p` stores through the V# in user SGPRs ud..ud+3 and never loads
  // from it.
  static bool OnlyStoresTo(const Program &p, u32 ud) {
    bool store = false;
    for (const auto &i : p.code) {
      if ((i.enc != Shader::Gcn::Enc::MUBUF &&
           i.enc != Shader::Gcn::Enc::MTBUF) ||
          i.srsrc != ud) {
        continue;
      }
      const bool is_store =
          (i.op >= 4 && i.op <= 7) ||
          (i.enc == Shader::Gcn::Enc::MUBUF && i.op >= 24 && i.op <= 31);
      if (!is_store) {
        return false;
      }
      store = true;
    }
    return store;
  }

  // Probe slot for this draw/dispatch of the probed shader, or -1.
  bool WantsProbe(u64 shader) const {
    return probe_buf && trace && (shader == probe.shader || probe.shader == ~0ull) &&
        probe_slots.size() < kProbeSlots;
  }

  int ProbeSlot(u64 shader, const std::string &what) {
    if (!WantsProbe(shader)) {
      return -1;
    }
    probe_slots.push_back(what);
    return static_cast<int>(probe_slots.size() - 1);
  }

  void PrintProbes() {
    const u32 *d = static_cast<const u32 *>(probe_buf.contents);
    for (size_t k = 0; k < probe_slots.size(); ++k) {
      const u32 *w = d + k * Shader::kProbeWords;
      if (!w[0]) {
        if (probe.shader != ~0ull) {
          DC_INFO("probe %zu (%s): not reached", k, probe_slots[k].c_str());
        }
        continue;
      }
      DC_INFO("probe %zu (%s) at %u,%u: exec=%#x vcc=%#x m0=%#x", k,
              probe_slots[k].c_str(), w[4], w[5], w[1], w[2], w[3]);
      std::string line;
      for (u32 n = 0; n < 96; ++n) {
        float f;
        std::memcpy(&f, &w[8 + n], 4);
        char b[64];
        std::snprintf(b, sizeof(b), " v%u=%08x(%g)", n, w[8 + n], f);
        line += b;
        if (n % 6 == 5) {
          DC_INFO("probe  %s", line.c_str());
          line.clear();
        }
      }
      for (u32 n = 0; n < 104; ++n) {
        char b[32];
        std::snprintf(b, sizeof(b), " s%u=%08x", n, w[104 + n]);
        line += b;
        if (n % 8 == 7) {
          DC_INFO("probe  %s", line.c_str());
          line.clear();
        }
      }
    }
    probe_slots.clear();
    std::memset(probe_buf.contents, 0, probe_buf.length);
  }

  void Draw(const RegFile &r, const DrawCall &dc,id<MTLBuffer> indirect=nil);
  void Dispatch(const RegFile &r, u32 x, u32 y, u32 z,
      id<MTLBuffer> indirect=nil,u64 indirect_offset=0,u64 indirect_address=0);
  bool DrawIndirect(const RegFile&,const DrawCall&,u64,const CpuReadReady&);
  bool DispatchIndirect(const RegFile&,u64,const CpuReadReady&);
  void Flip(const FlipInfo &f);
  std::unordered_map<u64, id<MTLComputePipelineState>> compute_pipelines;

  // DC_DEBUG_MEMTEST: reads `addr` through the shader memory path and
  // compares with what the CPU sees.
  void MemTest(u64 addr) {
    static bool done = false;
    if (done || !Readable(addr, 16)) {
      return;
    }
    done = true;
    std::string src = Shader::kMslPrelude;
    src += "kernel void memtest(device uint* gm0 [[buffer(1)]], device uint* "
           "gm1 [[buffer(2)]], "
           "device const DcMap& gmap [[buffer(3)]], constant ulong& addr "
           "[[buffer(4)]], device uint* out [[buffer(5)]]) {"
           " DcMem mem = {gm0, gm1, &gmap}; for (uint i = 0; i < 4; ++i) "
           "out[i] = dc_ld(mem, addr + i * 4);"
           " out[4] = uint(gmap.pages); out[5] = uint(gmap.split >> 20); }";
    NSError *err = nil;
    id<MTLLibrary> lib = [device newLibraryWithSource:@(src.c_str())
                                              options:nil
                                                error:&err];
    id<MTLComputePipelineState> cp = [device
        newComputePipelineStateWithFunction:[lib newFunctionWithName:@"memtest"]
                                      error:&err];
    if (!cp) {
      DC_ERROR("memtest: %s", err.localizedDescription.UTF8String);
      return;
    }
    UpdateMemMap();
    id<MTLBuffer> out =
        [device newBufferWithLength:64 options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> c = [queue commandBuffer];
    id<MTLComputeCommandEncoder> e = [c computeCommandEncoder];
    [e setComputePipelineState:cp];
    [e setBuffer:mem[0] offset:0 atIndex:1];
    [e setBuffer:mem[1] offset:0 atIndex:2];
    [e setBuffer:mem_map offset:0 atIndex:3];
    [e setBytes:&addr length:8 atIndex:4];
    [e setBuffer:out offset:0 atIndex:5];
    [e dispatchThreads:MTLSizeMake(1, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];
    [e endEncoding];
    [c commit];
    [c waitUntilCompleted];
    const u32 *g = static_cast<const u32 *>(out.contents);
    const u32 *cpu = reinterpret_cast<const u32 *>(addr);
    DC_INFO("memtest %#llx: gpu %08x %08x %08x %08x | cpu %08x %08x %08x %08x "
            "| ranges %u split %u MB (%s)",
            addr, g[0], g[1], g[2], g[3], cpu[0], cpu[1], cpu[2], cpu[3], g[4],
            g[5], c.error ? c.error.localizedDescription.UTF8String : "ok");
  }

  // Reads a presented frame back and writes it as a PPM (debug only).
  void DumpFrame(id<MTLTexture> tex) {
    const u32 w = static_cast<u32>(tex.width), h = static_cast<u32>(tex.height);
    id<MTLBuffer> buf =
        [device newBufferWithLength:u64(w) * h * 4
                            options:MTLResourceStorageModeShared];
    id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
    [b copyFromTexture:tex
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(w, h, 1)
                        toBuffer:buf
               destinationOffset:0
          destinationBytesPerRow:w * 4
        destinationBytesPerImage:u64(w) * h * 4];
    [b endEncoding];
    Commit(true);
    EnsureCommandBuffer();
    char path[512];
    std::snprintf(path, sizeof(path), "%s/frame_%04llu.ppm", dump_frames,
                  frame_count);
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << w << " " << h << "\n255\n";
    const auto *px = static_cast<const u8 *>(buf.contents);
    std::vector<u8> rgb(u64(w) * h * 3);
    for (u64 i = 0; i < u64(w) * h; ++i) { // BGRA -> RGB
      rgb[i * 3] = px[i * 4 + 2];
      rgb[i * 3 + 1] = px[i * 4 + 1];
      rgb[i * 3 + 2] = px[i * 4];
    }
    out.write(reinterpret_cast<const char *>(rgb.data()),
              static_cast<std::streamsize>(rgb.size()));
  }
};

bool MetalRenderer::Impl::DrawIndirect(const RegFile& r,const DrawCall& dc,u64 address,const CpuReadReady& ready) {
  // Address extraction retains its conservative drained-path admission.
  if(!native_indirect_enabled ||
      dc.indexed || !IndirectReadAvailable() ||
      r[Reg::DB_RENDER_CONTROL] || Bits(r[Reg::CB_COLOR_CONTROL],4,3)!=1) return false;
  switch(r.Primitive()) {
  case PrimType::PointList:case PrimType::LineList:case PrimType::LineStrip:
  case PrimType::TriList:case PrimType::TriStrip:break;
  default:return false; // CPU assembly, rect expansion and tessellation need counts
  }
  // A zero-count packet previously never reached the renderer. Do not let
  // unknown counts perform pending/initial clears or materialize new targets.
  bool attachment=false;
  for(u32 k=0;k<8;++k) {
    if(!((r[Reg::CB_TARGET_MASK]>>(4*k))&15) ||
        !((r[Reg::SPI_SHADER_COL_FORMAT]>>(4*k))&15)) continue;
    const auto address=u64(r.ColorTarget(k,Reg::CB_BASE))<<8;
    const auto found=color_targets.find(address);
    if(!address || mip_alias.count(address) || depth_targets.count(address) ||
        found==color_targets.end() || found->second.size()!=1 ||
        r.ColorTarget(k,Reg::CB_VIEW)) return false;
    const auto& rt=*found->second.front();
    const u32 info=r.ColorTarget(k,Reg::CB_INFO);
    const auto format=ColorTargetFormat(ColorInfo::Format(info),ColorInfo::NumberType(info),ColorInfo::CompSwap(info));
    const u32 pitch=((r.ColorTarget(k,Reg::CB_PITCH)&0x7ff)+1)*8;
    const u32 slice=((r.ColorTarget(k,Reg::CB_SLICE)&0x3fffff)+1)*64;
    const u32 w=cb_extent[k] ? cb_extent[k]&0xffff : pitch;
    const u32 h=cb_extent[k] ? cb_extent[k]>>16 : slice/pitch;
    if(!rt.initialized || rt.clear_pending || rt.dim!=TexDim::Tex2D ||
        rt.width!=w || rt.height!=h || rt.tex.pixelFormat!=format.format) return false;
    attachment=true;
  }
  if(r[Reg::DB_DEPTH_CONTROL]&3) {
    const auto address=u64(r[Reg::DB_Z_WRITE_BASE])<<8;
    const auto found=depth_targets.find(address);
    if(!address || mip_alias.count(address) || color_targets.count(address) ||
        found==depth_targets.end() || found->second.size()!=1) return false;
    const auto& rt=*found->second.front();
    const u32 size=r[Reg::DB_DEPTH_SIZE];
    const u32 w=db_extent ? db_extent&0xffff : ((size&0x7ff)+1)*8;
    const u32 h=db_extent ? db_extent>>16 : (((size>>11)&0x7ff)+1)*8;
    const auto format=(r[Reg::DB_STENCIL_INFO]&1) && r[Reg::DB_STENCIL_WRITE_BASE] ?
        MTLPixelFormatDepth32Float_Stencil8 : (r[Reg::DB_Z_INFO]&3)==1 ?
        MTLPixelFormatDepth16Unorm : MTLPixelFormatDepth32Float;
    if(!rt.initialized || rt.clear_pending || rt.width!=w || rt.height!=h ||
        !(r[Reg::DB_Z_INFO]&3) || rt.tex.pixelFormat!=format) return false;
    attachment=true;
  }
  if(!attachment) return false;
  const u32* vud=r.UserData(Reg::SPI_SHADER_USER_DATA_VS_0);
  const u32* pud=r.UserData(Reg::SPI_SHADER_USER_DATA_PS_0);
  auto* vertex=NativeProgram(Stage::Vertex,r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS),vud,
      Bits(r[Reg::SPI_SHADER_PGM_RSRC2_VS],1,5),Bits(r[Reg::SPI_SHADER_PGM_RSRC1_VS],24,2),0,ready);
  const u64 pixel_address=r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS);
  auto* pixel=pixel_address ? NativeProgram(Stage::Fragment,pixel_address,pud,
      Bits(r[Reg::SPI_SHADER_PGM_RSRC2_PS],1,5),0,0,ready) : nullptr;
  // Scalar-root specializations change setup only for these exact layouts.
  // Keep their indirect draws conservative; unrelated PS programs use the
  // identical preparation with either option on or off.
  if((static_smrd_enabled || direct_smrd_enabled) && pixel &&
      !StaticSmrd::Layout(pixel->hash).empty())return false;
  unsigned target_count=0;
  for(u32 k=0;k<8;++k) if(((r[Reg::CB_TARGET_MASK]>>(4*k))&15) &&
      ((r[Reg::SPI_SHADER_COL_FORMAT]>>(4*k))&15)) ++target_count;
  const bool camera_read=target_count>=5 && (r[Reg::DB_DEPTH_CONTROL]&3) &&
      (temporal_aa.load(std::memory_order_relaxed) || frame_generation_enabled.load(std::memory_order_relaxed) || !capture_root.empty()) &&
      (!temporal || !temporal->HasCamera());
  if(!vertex || (pixel_address && !pixel) || camera_read ||
      !NativeResources(*vertex,vud,ready) || (pixel && !NativeResources(*pixel,pud,ready))) {
    ++indirect_resource_rejects;return false;
  }
  id<MTLBuffer> source=nil;u64 offset=0;
  if(!LocateIndirect(address,16,&source,&offset)) return false;
  if(!indirect_draw_adapter) {
    NSError* error=nil;
    auto library=[device newLibraryWithSource:@(NativeIndirect::DrawAdapterMsl) options:nil error:&error];
    if(library) indirect_draw_adapter=[device newComputePipelineStateWithFunction:
        [library newFunctionWithName:@"dc_indirect_draw"] error:&error];
    if(!indirect_draw_adapter) {ReportOnce("indirect draw argument adapter could not compile");return false;}
  }
  // Dedicated immutable output: the normal transient upload pool may recycle
  // a completed adapter's chunk while shader compilation delays its consumer.
  auto arguments=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  if(!arguments) return false;
  EndPass();EnsureCommandBuffer();
  auto adapter=[cmd computeCommandEncoder];
  if(!adapter) return false;
  [adapter setComputePipelineState:indirect_draw_adapter];
  [adapter setBuffer:source offset:offset atIndex:0];
  [adapter setBuffer:arguments offset:0 atIndex:1];
  const u32 first=r[Reg::VGT_INDX_OFFSET];
  [adapter setBytes:&first length:sizeof(first) atIndex:2];
  [adapter dispatchThreadgroups:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
  [adapter endEncoding];
  TrackIndirectRead(address,16,source,offset);
  TraceIndirectArguments(arguments,address,false);
  DrawCall unknown=dc;unknown.count=1;unknown.instances=1;
  Draw(r,unknown,arguments);++native_indirect_draws;
  return true;
}

bool MetalRenderer::Impl::DispatchIndirect(const RegFile& r,u64 address,const CpuReadReady& ready) {
  if(!native_indirect_enabled || !IndirectReadAvailable()) return false;
  id<MTLBuffer> source=nil;u64 offset=0;
  if(!LocateIndirect(address,12,&source,&offset)) return false;
  const u64 program_address=r.ProgramAddr(Reg::COMPUTE_PGM_LO);
  const u32 rsrc2=r[Reg::COMPUTE_PGM_RSRC2];
  const u32* ud=r.UserData(Reg::COMPUTE_USER_DATA_0);
  auto* cs=NativeProgram(Stage::Compute,program_address,ud,Bits(rsrc2,1,5),Bits(rsrc2,11,2),rsrc2,ready);
  // These paths also update private texture aliases or temporal state on the
  // CPU. Their zero/count-dependent semantics require the drained path.
  if(!cs || !cs->unsupported.empty() || cs->retail_id==0x9a9cf8a9 ||
      cs->copy_source_ud>=0 || cs->fill_destination_ud>=0) return false;
  if(!NativeResources(*cs,ud,ready)) {++indirect_resource_rejects;return false;}
  for(const auto& image:cs->images) if(image.storage) return false;
  for(u32 k=0;k+3<16;++k) {
    const u64 base=(u64(ud[k])|(u64(ud[k+1]&0xfff)<<32))&kGpuAddrMask;
    if(metas.count(base) && OnlyStoresTo(*cs,k)) return false;
  }
  for(const auto& read:cs->buffer_reads) {
    if(!read.HasReadOnlyDescriptor()) continue;
    std::array<u32,4> descriptor{};
    for(unsigned k=0;k<4;++k) if(!ReadDescriptorWord(read.descriptor[k],ud,descriptor[k],ready)) {
      ++indirect_resource_rejects;return false;
    }
    const u64 base=(u64(descriptor[0])|(u64(descriptor[1]&0xfff)<<32))&kGpuAddrMask;
    const auto meta=metas.find(base);
    if(meta!=metas.end() && meta->second.depth) return false;
  }
  auto arguments=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];
  if(!arguments) return false;
  EndPass();EnsureCommandBuffer();
  auto copy=[cmd blitCommandEncoder];
  if(!copy) return false;
  // Latch after preceding GPU producers and before the consuming CS can
  // overwrite the original (or an alias). Never expose its RW guest buffer
  // directly to Metal's indirect argument decoder.
  [copy copyFromBuffer:source sourceOffset:offset toBuffer:arguments destinationOffset:0 size:12];
  [copy endEncoding];
  TrackIndirectRead(address,12,source,offset);
  TraceIndirectArguments(arguments,address,true);
  Dispatch(r,0,0,0,arguments,0,0);++native_indirect_dispatches;
  return true;
}

void MetalRenderer::Impl::Draw(const RegFile &r, const DrawCall &dc,id<MTLBuffer> indirect) {
  const u64 ps_addr = r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS);
  if (trace_on_ps && trace_on_ps==ps_addr) {
    trace_on_ps=0;
    triggered_trace=true;
    trace=true;
    DC_INFO("gpu: armed trace triggered by PS %#llx",ps_addr);
  }
  static const u64 trace_cb = std::getenv("DC_TRACE_CB")
      ? std::strtoull(std::getenv("DC_TRACE_CB"), nullptr, 16) : 0;
  static const bool trace_materials=std::getenv("DC_TRACE_MATERIALS")!=nullptr;
  static u32 material_traces=0;
  const u32 extent=cb_extent[0];
  const bool material=trace_materials && material_traces<512 &&
      (extent&0xffff)==(extent>>16) && (extent&0xffff)>=128 && (extent&0xffff)<=512 &&
      ColorInfo::NumberType(r.ColorTarget(0,Reg::CB_INFO))==ColorInfo::kNumberSrgb;
  if (material) ++material_traces;
  const bool trace_target = material || (trace_cb &&
      (u64(r.ColorTarget(0, Reg::CB_BASE)) << 8) == trace_cb);
  struct RestoreDrawTrace {
    Impl& renderer;
    bool previous, target;
    bool outputs;
    ~RestoreDrawTrace() {
      if (outputs && renderer.enc) {
        const auto pass=renderer.pass;
        renderer.EndPass();
        renderer.EnsureCommandBuffer();
        for (u32 k=0;k<8;++k) {
          const auto* rt=reinterpret_cast<const RenderTarget*>(pass.color[k]);
          if (rt && rt->tex.width<=4096 && rt->tex.height<=4096)
            renderer.DumpTexture(rt->tex,16+k,rt->addr);
        }
      } else if (target) renderer.EndPass();
      renderer.trace = previous;
    }
  } restore_trace{*this, trace, trace_target,
      trace && dump_selected_outputs && dump_frames && dump_ps==ps_addr};
  if (trace_target) trace = true;
  ++draw_count;
  ++stat_draws;
  ++work_since_commit;
  CaptureFogDrawEntry(r,dc);
  const u64 vs_addr = r.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS);
  const u32 cb_mode = Bits(r[Reg::CB_COLOR_CONTROL], 4, 3);
  const u32 db_render = r[Reg::DB_RENDER_CONTROL];
  const u32 db_control = r[Reg::DB_DEPTH_CONTROL];
  const PrimType prim = r.Primitive();
  const bool tessellated = prim == PrimType::Patch;

  if (trace) {
    DC_INFO("gpu: draw #%llu prim=%#x count=%u idx=%d vs=%#llx ps=%#llx "
            "cb_mode=%u db_render=%#x "
            "db_ctl=%#x cb0=%#llx info=%#x mask=%#x z=%#llx/%#llx zinfo=%#x "
            "blend0=%#x colfmt=%#x db_shader=%#x",
            draw_count, static_cast<u32>(prim), dc.count, dc.indexed, vs_addr,
            ps_addr, cb_mode, db_render, db_control,
            u64(r.ColorTarget(0, Reg::CB_BASE)) << 8,
            r.ColorTarget(0, Reg::CB_INFO), r[Reg::CB_TARGET_MASK],
            u64(r[Reg::DB_Z_READ_BASE]) << 8, u64(r[Reg::DB_Z_WRITE_BASE]) << 8,
            r[Reg::DB_Z_INFO], r[Reg::CB_BLEND0_CONTROL],
            r[Reg::SPI_SHADER_COL_FORMAT], r[Reg::DB_SHADER_CONTROL]);
    DC_INFO("gpu:   PS interpolation addr=%#x input0=%#x input1=%#x input2=%#x input3=%#x",
        r[Reg::SPI_PS_INPUT_ADDR],r[Reg::SPI_PS_INPUT_CNTL_0],r[Reg::SPI_PS_INPUT_CNTL_0+1],
        r[Reg::SPI_PS_INPUT_CNTL_0+2],r[Reg::SPI_PS_INPUT_CNTL_0+3]);
    if (r.ColorTarget(0, Reg::CB_VIEW)) {
      DC_INFO("gpu:   cb0 view=%#x attrib=%#x pitch=%#x slice=%#x",
              r.ColorTarget(0, Reg::CB_VIEW), r.ColorTarget(0, Reg::CB_ATTRIB),
              r.ColorTarget(0, Reg::CB_PITCH), r.ColorTarget(0, Reg::CB_SLICE));
    }
    if (static_cast<u32>(prim)==9) {
      for (u32 lo : {0x2D48u,0x2D08u}) {
        const u64 address=r.ProgramAddr(lo);
        const u32* ud=r.UserData(lo+4);
        DC_INFO("gpu: tess %s=%#llx rsrc1=%#x rsrc2=%#x ls_hs=%#x tf=%#x ud=%08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x",
          lo==0x2D48u ? "ls" : "hs",address,r[lo+2],r[lo+3],r[0xA2D6],r[0xA2DB],
          ud[0],ud[1],ud[2],ud[3],ud[4],ud[5],ud[6],ud[7],ud[8],ud[9],ud[10],ud[11],ud[12],ud[13],ud[14],ud[15]);
        if (address) GetProgram(Stage::Compute,address,ud,Bits(r[lo+3],1,5),0,r[lo+3]);
      }
    }
  }
  if (!vs_addr) {
    return;
  }
  if ((r[Reg::PA_CL_CLIP_CNTL] >> 22) &
      1) { // DX_RASTERIZATION_KILL: nothing reaches the rasterizer
    return;
  }
  static const u64 skip_ps =
      std::getenv("DC_SKIP_PS")
          ? std::strtoull(std::getenv("DC_SKIP_PS"), nullptr, 16)
          : 0;
  if (skip_ps && ps_addr == skip_ps) { // debug: drop one pixel shader's draws
    return;
  }

  // ---- attachments ----
  if (cb_mode == 2 || cb_mode == 5) { // fast-clear eliminate / FMASK decompress
    EliminateFastClear(r);
    return;
  }
  RenderTarget *colors[8] = {};
  MTLPixelFormat color_fmt[8] = {};
  OutType out_type[8] = {};
  u32 col_format = ps_addr ? r[Reg::SPI_SHADER_COL_FORMAT] : 0;
  const u32 target_mask = r[Reg::CB_TARGET_MASK];
  bool any_color = false;
  if (cb_mode == 1 && ps_addr) {
    for (u32 k = 0; k < kNumColorTargets; ++k) {
      const u32 info = r.ColorTarget(k, Reg::CB_INFO);
      const u64 base = u64(r.ColorTarget(k, Reg::CB_BASE)) << 8;
      const u32 fmt = ColorInfo::Format(info);
      if (!fmt || !base || !((target_mask >> (4 * k)) & 0xF) ||
          !((col_format >> (4 * k)) & 0xF)) {
        col_format &= ~(0xFu << (4 * k));
        continue;
      }
      const ColorFormatInfo cf =
          ColorTargetFormat(fmt, ColorInfo::NumberType(info), ColorInfo::CompSwap(info));
      colors[k] = &ColorTargetFor(r, k);
      color_fmt[k] = cf.format;
      out_type[k] = cf.out;
      any_color = true;
    }
  } else {
    col_format = 0;
  }

  RenderTarget *depth = nullptr;
  const u32 z_info = r[Reg::DB_Z_INFO];
  const u32 zfmt = z_info & 3;
  const bool depth_clear = db_render & 1;
  const bool stencil_clear = (db_render >> 1) & 1;
  // shadPS4 StencilValid(): a stencil format and a stencil address.
  const bool stencil_valid =
      (r[Reg::DB_STENCIL_INFO] & 1) && r[Reg::DB_STENCIL_WRITE_BASE];
  const bool z_used = ((db_control >> 1) & 1) || depth_clear ||
                      (stencil_valid && ((db_control & 1) || stencil_clear));
  MTLPixelFormat depth_fmt = MTLPixelFormatInvalid;
  if (zfmt && z_used) {
    const u64 zbase = u64(r[Reg::DB_Z_WRITE_BASE]) << 8;
    const u32 size = r[Reg::DB_DEPTH_SIZE];
    u32 w = ((size & 0x7FF) + 1) * 8, h = (((size >> 11) & 0x7FF) + 1) * 8;
    if (db_extent) {
      w = db_extent & 0xFFFF;
      h = db_extent >> 16;
    }
    depth_fmt = stencil_valid ? MTLPixelFormatDepth32Float_Stencil8
                : zfmt == 1   ? MTLPixelFormatDepth16Unorm
                              : MTLPixelFormatDepth32Float;
    if (zbase && w && h) {
      depth = &GetDepthTarget(zbase, depth_fmt, w, h);
      depth->clear_depth = r.F(Reg::DB_DEPTH_CLEAR);
      depth->clear_stencil = r[Reg::DB_STENCIL_CLEAR] & 0xFF;
      if (ZInfo::TileSurface(z_info)) {
        NoteMeta(u64(r[Reg::DB_HTILE_DATA_BASE]) << 8, zbase, true);
        ObserveHtileCaptureLayout(r,zbase,w,h);
      }
    } else {
      depth_fmt = MTLPixelFormatInvalid;
    }
  }
  if (!any_color && !depth) {
    if (trace)
      DC_INFO("gpu:   skipped: no attachments");
    return;
  }

  PassState want;
  for (u32 k = 0; k < 8; ++k) {
    want.color[k] = reinterpret_cast<u64>(colors[k]);
    want.slice[k] =
        colors[k] ? colors[k]->bind_slice | (colors[k]->bind_level << 24) : 0;
  }
  want.depth = reinterpret_cast<u64>(depth);
  if ((depth_clear || (stencil_clear && stencil_valid)) && depth) {
    // The draw only exists to clear depth and/or stencil.
    if (!depth_clear) {
      // Stencil only: keep depth, so end any pass and reload it.
      BeginPass(colors, depth, false, 1.0f, true,
                r[Reg::DB_STENCIL_CLEAR] & 0xFF);
    } else {
      BeginPass(colors, depth, true, r.F(Reg::DB_DEPTH_CLEAR), stencil_clear,
                r[Reg::DB_STENCIL_CLEAR] & 0xFF);
    }
    return;
  }
  // ---- shaders ----
  const u32 *vs_ud = r.UserData(Reg::SPI_SHADER_USER_DATA_VS_0);
  const u32 *ps_ud = r.UserData(Reg::SPI_SHADER_USER_DATA_PS_0);
  Program *vs = GetProgram(Stage::Vertex, vs_addr, vs_ud,
                           Bits(r[Reg::SPI_SHADER_PGM_RSRC2_VS], 1, 5),
                           Bits(r[Reg::SPI_SHADER_PGM_RSRC1_VS], 24, 2));
  // Shadow/depth prepasses can still alpha-test or export depth/coverage.
  // DB_SHADER_CONTROL also requests execution for shader side effects. Keep
  // the fragment stage for these passes even when every color target is off.
  const bool needs_ps = ps_addr &&
      (any_color || (r[Reg::DB_SHADER_CONTROL] & 0x7c7u));
  Program *ps = needs_ps
                    ? GetProgram(Stage::Fragment, ps_addr, ps_ud,
                                 Bits(r[Reg::SPI_SHADER_PGM_RSRC2_PS], 1, 5), 0)
                    : nullptr;
  if (!vs || (needs_ps && !ps)) {
    if (trace)
      DC_INFO("gpu:   skipped: shader decode failed");
    return;
  }

  CaptureDreamMaterialBoundary(ps,colors[5]);
  CaptureFogBoundary(ps,colors[0]);

  if (trace) { // DC_PEEK_PS=<ps>: its first two V# buffers, printed after the
               // traced frame
    static const u64 peek_ps =
        std::getenv("DC_PEEK_PS")
            ? std::strtoull(std::getenv("DC_PEEK_PS"), nullptr, 16)
            : 0;
    if (ps_addr == peek_ps) {
      for (u32 k = 0; k < 8; k += 4) {
        peek_addrs.push_back(
            (u64(ps_ud[k]) | (u64(ps_ud[k + 1] & 0xFFF) << 32)) & kGpuAddrMask);
        peek_addrs.push_back(peek_addrs.back() + 64);
      }
      // DC_PEEK_PS_AT=<ud>:<byte offset>,...: 8 floats at V#(ud)+off, read when
      // the draw is recorded
      if (const char *at = std::getenv("DC_PEEK_PS_AT")) {
        for (const char *q = at; *q;) {
          char *e = nullptr;
          const u32 n = std::strtoul(q, &e, 10);
          const u32 off = *e == ':' ? std::strtoul(e + 1, &e, 16) : 0;
          const u64 a =
              ((u64(ps_ud[n & 15]) | (u64(ps_ud[(n + 1) & 15] & 0xFFF) << 32)) &
               kGpuAddrMask) +
              off;
          if (Readable(a, 32)) {
            const float *v = reinterpret_cast<const float *>(a);
            DC_INFO("gpu:   peek ud%u+%#x: %g %g %g %g %g %g %g %g", n, off,
                    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
          }
          q = *e ? e + 1 : e;
        }
      }
    }
  }
  // ---- resources ----
  if (temporal_aa.load(std::memory_order_relaxed) || frame_generation_enabled.load(std::memory_order_relaxed) || !capture_root.empty()) {
    if (!temporal) temporal=std::make_unique<TemporalAA>((__bridge void*)device);
    unsigned targets=0;
    for (auto* target:colors) targets+=target!=nullptr;
    if (depth && targets>=5) {
      temporal_depth=depth;
      ObserveCamera(*vs,vs_ud,r.F(Reg::PA_CL_VPORT_XSCALE+2));
      if (ps) ObserveCamera(*ps,ps_ud,r.F(Reg::PA_CL_VPORT_XSCALE+2));
    }
    if (depth && depth==temporal_depth && colors[0] && targets<=2 &&
        colors[0]->tex.pixelFormat==MTLPixelFormatRGBA16Float &&
        colors[0]->width==depth->width && colors[0]->height==depth->height) temporal_color=colors[0];
  } else if (temporal) {
    temporal->Invalidate(); temporal_depth=temporal_color=nullptr;
  }
  std::vector<TextureType> vs_types, ps_types;
  Resources vs_res, ps_res;
  resolving_shader = ps_addr;
  Resolve(*vs, vs_ud, vs_types, vs_res);
  if (ps) {
    Resolve(*ps, ps_ud, ps_types, ps_res);
  }
  if (trace && dump_frames && ps && (trace_target || (dump_ps && ps_addr == dump_ps))) {
    EndPass();
    EnsureCommandBuffer();
    for (u32 k = 0; k < ps_res.tex.size(); ++k) {
      id<MTLTexture> t = ps_res.tex[k];
      if (t.textureType != MTLTextureType2D) continue;
      DC_INFO("gpu: dump PS input %u for draw %llu", k, draw_count);
      DumpTexture(t, k, ps_addr);
    }
  }

  bool frame_generation_display_copy=false;
  if(frame_generation_enabled.load(std::memory_order_relaxed)) {
    if(!frame_generation)frame_generation=std::make_unique<FrameGeneration>((__bridge void*)device);
    if(std::getenv("BB_METAL_FRAMEGEN_TRACE") && frame_generation->NeedsHud(frame_count) && frame_count%300==0)
      DC_INFO("framegen HUD candidate frame=%llu vs=%llx retail=%llx ps=%llx color=%llx",frame_count,vs->hash,vs->retail_id,ps ? ps->hash : 0,colors[0] ? colors[0]->addr : 0);
    const auto ui_shader=[](u64 id) {
      switch(id) {case 0x34e8a281:case 0x81d336ce:case 0x09957251:case 0x24042a9b:case 0xa400228b:
        case 0xbb4d5f8c:case 0xe94b5b06:case 0x9951d199:case 0x44072322:case 0xa334a845:return true;default:return false;}
    };
    if(ui_shader(vs->retail_id))frame_generation_hud_pending=true;
    if(frame_generation_hud_pending && frame_generation->NeedsHud(frame_count) && colors[0] && colors[0]->initialized && !colors[0]->clear_pending) {
      EndPass();EnsureCommandBuffer();
      if(frame_generation->Hud((__bridge void*)cmd,(__bridge void*)colors[0]->tex,frame_count))frame_generation_hud_pending=false;
    }
    if(std::getenv("BB_METAL_FRAMEGEN_TRACE") && frame_count%300==0 &&
        (vs->hash==0x601fd916b9e41328ull || (ps && ps->hash==0x1c472a7564e37de2ull))) {
      DC_INFO("framegen COPY candidate f=%llu vs=%llx ps=%llx prim=%x idx=%u count=%u instances=%u first=%u base=%u target=%llx size=%ux%u depth=%u mask=%x shader=%x blend=%x textures=%zu samplers=%zu viewport=%g,%g,%g,%g,%g,%g",frame_count,
        vs->hash,ps ? ps->hash : 0,unsigned(prim),unsigned(dc.indexed),dc.count,dc.instances,dc.first_vertex,r[Reg::VGT_INDX_OFFSET],colors[0] ? colors[0]->addr : 0,
        colors[0] ? colors[0]->width : 0,colors[0] ? colors[0]->height : 0,unsigned(depth!=nullptr),target_mask,r[Reg::CB_SHADER_MASK],r[Reg::CB_BLEND0_CONTROL],ps_res.tex.size(),ps_res.smp.size(),
        r.F(Reg::PA_CL_VPORT_XSCALE),r.F(Reg::PA_CL_VPORT_XSCALE+1),r.F(Reg::PA_CL_VPORT_XSCALE+2),r.F(Reg::PA_CL_VPORT_XSCALE+3),r.F(Reg::PA_CL_VPORT_XSCALE+4),r.F(Reg::PA_CL_VPORT_XSCALE+5));
    }
    // Verified retail display copy: procedural full-screen rectangle, integer
    // scene pixels, then the actual bound gamma LUT. No address-only matching.
    if(ps && vs->hash==0x601fd916b9e41328ull && ps->hash==0x1c472a7564e37de2ull &&
        colors[0] && !depth && colors[0]->width==1920 && colors[0]->height==1080 &&
        std::count_if(colors,colors+8,[](auto* c){return c!=nullptr;})==1 &&
        prim==PrimType::RectList && !dc.indexed && dc.count==3 && std::max(1u,dc.instances)==1 &&
        dc.first_vertex==0 && r[Reg::VGT_INDX_OFFSET]==0 && (target_mask&15)==15 && (r[Reg::CB_SHADER_MASK]&15)==15 &&
        !(r[Reg::CB_BLEND0_CONTROL]&(1u<<30)) && ps_res.tex.size()==2 && ps_res.smp.size()==1 &&
        ps_res.tex[0].width==1920 && ps_res.tex[0].height==1080 &&
        r.F(Reg::PA_CL_VPORT_XSCALE)==960 && r.F(Reg::PA_CL_VPORT_XSCALE+1)==960 &&
        r.F(Reg::PA_CL_VPORT_XSCALE+2)==-540 && r.F(Reg::PA_CL_VPORT_XSCALE+3)==540) {
      Rect area{0,0,1920,1080};
      for(auto reg:{Reg::PA_SC_SCREEN_SCISSOR_TL,Reg::PA_SC_WINDOW_SCISSOR_TL,Reg::PA_SC_GENERIC_SCISSOR_TL})
        area=Intersect(area,ScissorReg(r[reg],r[reg+1]));
      if(r[Reg::PA_SC_MODE_CNTL_0]&2)area=Intersect(area,ScissorReg(r[Reg::PA_SC_VPORT_SCISSOR_0_TL],r[Reg::PA_SC_VPORT_SCISSOR_0_TL+1]));
      if(area.x0==0 && area.y0==0 && area.x1==1920 && area.y1==1080) {
        EndPass();EnsureCommandBuffer();
        frame_generation_display_copy=frame_generation->DisplayCopy((__bridge void*)cmd,(__bridge void*)colors[0]->tex,
            (__bridge void*)ps_res.tex[0],(__bridge void*)ps_res.tex[1],(__bridge void*)ps_res.smp[0],frame_count);
      }
    }
  }
  // One transaction reserves every stage before tessellation or render
  // encoders are created. Keep its leases pinned across diagnostic commits.
  u32 vud[16],pud[16],lud[16],hud[16];
  std::memcpy(vud,vs_ud,sizeof(vud));std::memcpy(pud,ps_ud,sizeof(pud));
  std::array<u32*,4> prepared{vud};
  std::size_t prepared_count=1;
  if(ps) prepared[prepared_count++]=pud;
  if(tessellated) {
    std::memcpy(lud,r.UserData(Reg::SPI_SHADER_PGM_LO_LS+4),sizeof(lud));
    std::memcpy(hud,r.UserData(Reg::SPI_SHADER_PGM_LO_HS+4),sizeof(hud));
    prepared[prepared_count++]=lud;prepared[prepared_count++]=hud;
  }
  SnapshotArena::Pin snapshot_pin(arena_pin);
  BoundVertexSource bound_vertex_source;
  const bool inspect_bound_vertex=bound_vertex_fetch_audit && NativeVertexFetch::Layout(vs->hash) &&
      InspectBoundVertexSource(*vs,vud,!tessellated && prim!=PrimType::RectList &&
          prim!=PrimType::PointList && !debug_vsout && !probe_buf,bound_vertex_source);
  bool static_smrd_eligible=false;
  if((static_smrd_enabled || direct_smrd_enabled) && ps && !probe_buf && !StaticSmrd::Layout(ps->hash).empty()) {
    if(static_smrd_enabled) ++static_smrd_attempts;
    auto [entry,inserted]=static_smrd_programs.try_emplace(ps,false);
    if(inserted) entry->second=Shader::FragmentStaticSmrdEligible(*ps);
    static_smrd_eligible=entry->second;
  }
  const bool use_direct_smrd=direct_smrd_enabled && !static_smrd_enabled && static_smrd_eligible;
  const bool capture_wind=WindCaptureRequested(*vs);
  std::vector<StaticSmrd::Snapshot> static_smrd_sources;
  if(!PatchUserData(std::span<u32* const>(prepared.data(),prepared_count),
      (capture_wind || inspect_bound_vertex || (static_smrd_enabled && static_smrd_eligible)) ? &static_smrd_sources : nullptr)) return;
  if(inspect_bound_vertex)InspectBoundVertexPrepared(*vs,vud,bound_vertex_source,static_smrd_sources);
  StaticSmrd::Packed static_smrd_packed;
  const bool use_static_smrd=static_smrd_enabled && !direct_smrd_enabled && static_smrd_eligible && StaticSmrd::Pack(ps->hash,std::span<const u32,16>(pud),static_smrd_sources,
      {arena_va,kArenaSize,arena_head,arena_commit_head,arena_pin},static_smrd_packed);
  if(static_smrd_enabled && static_smrd_eligible) {
    if(use_static_smrd) {++static_smrd_draws;static_smrd_bytes+=u64(static_smrd_packed.count)*4;}
    else ++static_smrd_rejects;
  }
  if(capture_wind)CaptureWindInputs(*vs,vud,static_smrd_sources);
  CaptureFogInputs(ps,pud);
  if(ps && ps->hash==0x33a4610f6f19410dull) CaptureFogInputs(vs,vud,"domain");
  TessBuffers tess;
  // Preserve a preparation boundary when a draw samples one of its own
  // attachments. Fusing pure setup must not weaken a feedback dependency.
  const auto texture_root=[](id<MTLTexture> texture) {
    while(texture.parentTexture)texture=texture.parentTexture;
    return texture;
  };
  bool allow_native_tess=true;
  if(tessellated) for(const auto* resources:{&vs_res,&ps_res}) for(id<MTLTexture> sampled:resources->tex) {
    const auto root=texture_root(sampled);
    if(depth && root==texture_root(depth->tex))allow_native_tess=false;
    for(auto* color:colors)if(color && root==texture_root(color->tex))allow_native_tess=false;
  }
  if (tessellated && !PrepareTessellation(r,dc,vs->hash,allow_native_tess,tess,lud,hud)) return;

  bool pending_clear = depth && depth->clear_pending;
  for (u32 k = 0; k < 8; ++k) {
    pending_clear = pending_clear || (colors[k] && colors[k]->clear_pending);
  }
  // Depth bounds test (Metal on AMD has none): the pixel shader compares a copy
  // of the depth buffer, taken when the depth may have changed since the last
  // copy.
  const bool depth_bounds = depth && ps && ((db_control >> 3) & 1);
  if (depth_bounds &&
      (!depth->bounds_copy || depth->bounds_seq != depth->depth_revision ||
       depth->clear_pending || !depth->initialized)) {
    if (depth->clear_pending ||
        !depth->initialized) { // apply the pending clear before copying
      BeginPass(colors, depth, false, 1.0f);
    }
    EndPass();
    EnsureCommandBuffer();
    if (!depth->bounds_copy) {
      MTLTextureDescriptor *d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:depth->tex.pixelFormat
                                       width:depth->width
                                      height:depth->height
                                   mipmapped:NO];
      d.usage = MTLTextureUsageShaderRead;
      d.storageMode = MTLStorageModePrivate;
      depth->bounds_copy = [device newTextureWithDescriptor:d];
    }
    id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
    [b copyFromTexture:depth->tex
             sourceSlice:0
             sourceLevel:0
               toTexture:depth->bounds_copy
        destinationSlice:0
        destinationLevel:0
              sliceCount:1
              levelCount:1];
    [b endEncoding];
    depth_bounds_copies.fetch_add(1, std::memory_order_relaxed);
    ++stat_depth_copies;
    BeginPass(colors, depth, false, 1.0f);
    depth->bounds_seq = depth->depth_revision;
  } else if (!enc || !(want == pass) || pending_clear) {
    BeginPass(colors, depth, false, 1.0f);
  }

  // ---- functions ----
  // DC_PROBE_CB=<addr>: with DC_PROBE=*:nan, only draws into that color target.
  static const u64 probe_cb =
      std::getenv("DC_PROBE_CB")
          ? std::strtoull(std::getenv("DC_PROBE_CB"), nullptr, 16)
          : 0;
  const bool probe_target =
      !probe_cb || (u64(r.ColorTarget(0, Reg::CB_BASE)) << 8) == probe_cb;
  const int probe_slot =
      ps && probe_target && WantsProbe(ps_addr)
          ? ProbeSlot(ps_addr,
                      "draw " + std::to_string(draw_count) + " ps " +
                          Hex(ps_addr) + " cb0 " +
                          Hex(u64(r.ColorTarget(0, Reg::CB_BASE)) << 8))
          : -1;
  const bool rect = prim == PrimType::RectList;
  Shader::MslOptions vo;
  vo.tess_stage = tessellated ? Shader::TessStage::Domain : Shader::TessStage::None;
  vo.native_tess_layout=tess.native.local_layout;
  vo.native_tess_factor=tess.native.outer_factor;
  vo.native_tess_inner_factor=tess.native.inner_factor;
  vo.rect_list = rect;
  vo.points = prim == PrimType::PointList;
  vo.vtx_point_size = vo.points && (r[Reg::PA_CL_VS_OUT_CNTL] & 1);
  const bool capture = !tessellated && debug_vsout && vs_addr == debug_vsout && !debug_vs_buf;
  vo.debug_capture = capture;
  vo.textures = vs_types;
  if(native_vertex_fetch_enabled && vo.tess_stage==Shader::TessStage::None &&
      !vo.native_tess_layout && !rect && !vo.points && !debug_vsout && !probe_buf &&
      NativeVertexFetch::Layout(vs->hash) &&
      (native_vertex_fetch_extended_enabled || !NativeVertexFetch::IsExtended(vs->hash))) {
    auto [entry,inserted]=native_vertex_fetch_programs.try_emplace(vs->hash,false);
    if(inserted) entry->second=Shader::VertexNativeFetchEligible(*vs);
    vo.native_vertex_fetch=entry->second;
    if(vo.native_vertex_fetch) {
      ++native_vertex_fetch_draws;
      if(NativeVertexFetch::IsExtended(vs->hash)) ++native_vertex_fetch_extended_draws;
    }
  }
  if(direct_vertex_root_enabled && native_indirect_enabled && native_texture_coherence &&
      gpu_written_texture_snapshot && vo.tess_stage==Shader::TessStage::None &&
      !vo.native_tess_layout && !rect && !vo.points && !debug_vsout && !probe_buf &&
      NativeVertexFetch::Layout(vs->hash)) {
    auto [entry,inserted]=native_vertex_fetch_programs.try_emplace(vs->hash,false);
    if(inserted)entry->second=Shader::VertexNativeFetchEligible(*vs);
    vo.direct_vertex_root=entry->second;
  }
  vo.readonly_guest_memory=ReadOnlyGuestMemory(*vs,vo);
  if(vo.readonly_guest_memory)++readonly_guest_vs_bindings;
  vo.wind_consumed=WindConsumedRequested(*vs,vo.tess_stage==Shader::TessStage::None &&
      !vo.native_tess_layout && !rect && !vo.points && !debug_vsout && !probe_buf &&
      !vo.native_vertex_fetch && !vo.direct_vertex_root && !vo.readonly_guest_memory &&
      !vo.static_smrd && !vo.direct_smrd && !vo.simd_smrd,any_color);
  u64 vs_key = Hash(vs->hash);
  if(vo.wind_consumed)vs_key=Hash(WindConsumed::CacheTag,vs_key);
  if(vo.direct_vertex_root)vs_key=Hash(0x5653524f4f543031ull,vs_key); // distinct VS root-address ABI
  if(vo.readonly_guest_memory)vs_key=Hash(0x524f475545535431ull,vs_key);
  if(vo.native_vertex_fetch) vs_key=Hash(NativeVertexFetch::CacheTag,vs_key);
  vs_key = Hash(vo.tess_stage, vs_key);
  vs_key = Hash(vo.native_tess_layout,vs_key);
  vs_key = Hash(vo.native_tess_factor,vs_key);
  // Retain existing inner1 keys; inner2 must never share that source variant.
  if(vo.native_tess_layout && vo.native_tess_inner_factor!=1)
    vs_key=Hash(vo.native_tess_inner_factor,vs_key);
  vs_key = Hash(rect, vs_key);
  vs_key = Hash(u32(vo.points) | (u32(vo.vtx_point_size) << 1), vs_key);
  vs_key = Hash(capture, vs_key);
  vs_key = Fnv(vs_types.data(), vs_types.size() * sizeof(TextureType), vs_key);
  u64 ps_key = 0;
  Shader::MslOptions fo;
  if (ps) {
    for (u32 k = 0; k < 32; ++k) {
      fo.ps_input_cntl[k] = r[Reg::SPI_PS_INPUT_CNTL_0 + k];
    }
    fo.vs_param_mask = vs->param_mask;
    fo.ps_input_addr = r[Reg::SPI_PS_INPUT_ADDR];
    fo.col_format = col_format;
    for (u32 k = 0; k < 8; ++k) {
      fo.mrt_type[k] = out_type[k];
      if (color_fmt[k] == MTLPixelFormatRG11B10Float) {
        fo.mrt_ufloat |= 1u << k;
      }
    }
    fo.textures = ps_types;
    fo.depth_target = depth != nullptr;
    fo.depth_bounds = depth_bounds;
    // Only the generator's conservative CFG proof can remove a kill. Keep
    // diagnostic probes on the original path; depth/coverage rules stay Metal's.
    fo.opaque_exports=opaque_exports && probe_slot<0;
    fo.static_smrd=use_static_smrd;
    fo.direct_smrd=use_direct_smrd;
    if(simd_smrd_enabled && !static_smrd_enabled && !direct_smrd_enabled && !probe_buf &&
        (ps->hash==0x68fcb859536adcd4ull || ps->hash==0xaff190d9945a53e0ull)) {
      auto [entry,inserted]=simd_smrd_programs.try_emplace(ps->hash,false);
      if(inserted) entry->second=Shader::FragmentSimdSmrdEligible(*ps);
      fo.simd_smrd=entry->second;
    }
    if (probe_slot >= 0) {
      fo.probe_pc = probe.pc;
      fo.probe_x = probe.x;
      fo.probe_y = probe.y;
    }
    fo.readonly_guest_memory=ReadOnlyGuestMemory(*ps,fo);
    if(fo.readonly_guest_memory)++readonly_guest_ps_bindings;
    ps_key = Hash(ps->hash);
    if(fo.readonly_guest_memory)ps_key=Hash(0x524f475545535431ull,ps_key);
    ps_key = Hash(fo.probe_pc, ps_key);
    ps_key = Hash(fo.probe_x, ps_key);
    ps_key = Hash(fo.probe_y, ps_key);
    ps_key = Hash(fo.depth_target, ps_key);
    ps_key = Hash(fo.depth_bounds, ps_key);
    ps_key = Hash(fo.opaque_exports, ps_key);
    if(fo.static_smrd) ps_key=Hash(0x53534d524431ull,ps_key);
    if(fo.direct_smrd) ps_key=Hash(StaticSmrd::AddressCacheTag,ps_key);
    if(fo.simd_smrd) ps_key=Hash(0x53494d44534d5244ull,ps_key);
    ps_key = Fnv(fo.ps_input_cntl.data(), sizeof(fo.ps_input_cntl), ps_key);
    ps_key = Hash(fo.vs_param_mask, ps_key);
    ps_key = Hash(fo.ps_input_addr, ps_key);
    ps_key = Hash(fo.col_format, ps_key);
    ps_key = Fnv(fo.mrt_type.data(), sizeof(fo.mrt_type), ps_key);
    ps_key = Hash(fo.mrt_ufloat, ps_key);
    ps_key =
        Fnv(ps_types.data(), ps_types.size() * sizeof(TextureType), ps_key);
  }

  // ---- pipeline ----
  PipelineKey pk;
  pk.vs = vs_key;
  pk.ps = ps_key;
  for (u32 k = 0; k < 8; ++k) {
    pk.color_fmt[k] = static_cast<u32>(color_fmt[k]);
    pk.blend[k] = colors[k] ? r[Reg::CB_BLEND0_CONTROL + k] : 0;
  }
  pk.write_mask = target_mask;
  pk.shader_mask = r[Reg::CB_SHADER_MASK];
  pk.depth_fmt = static_cast<u32>(depth_fmt);
  pk.reserved = tessellated ? 0x80000000u | (r[Reg::VGT_TF_PARAM] & 0xff) : 0;
  const u64 pkey = Hash(pk);
  TakeCompiled();
  auto pit = pipelines.find(pkey);
  if (pit == pipelines.end()) {
    if (!pending.count(pkey)) {
      MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
      for (u32 k = 0; k < 8; ++k) {
        if (!colors[k]) {
          continue;
        }
        MTLRenderPipelineColorAttachmentDescriptor *ca = d.colorAttachments[k];
        ca.pixelFormat = color_fmt[k];
        const u32 b = pk.blend[k];
        const u32 wm = (target_mask >> (4 * k)) & 0xF;
        ca.writeMask = ((wm & 1) ? MTLColorWriteMaskRed : 0) |
                       ((wm & 2) ? MTLColorWriteMaskGreen : 0) |
                       ((wm & 4) ? MTLColorWriteMaskBlue : 0) |
                       ((wm & 8) ? MTLColorWriteMaskAlpha : 0);
        const bool blendable = out_type[k] == OutType::Float &&
                               color_fmt[k] != MTLPixelFormatRGBA32Float;
        // CB_SHADER_MASK without alpha: the hardware blends with source alpha 1
        // (shadPS4 swaps SrcAlpha factors for One/Zero).
        const bool alpha_masked =
            !((r[Reg::CB_SHADER_MASK] >> (4 * k + 3)) & 1);
        if ((b >> 30) & 1 && blendable && !debug_fs) {
          ca.blendingEnabled = YES;
          auto fixa = [&](u32 f) {
            return !alpha_masked ? BlendFactor(f)
                   : f == 4      ? MTLBlendFactorOne
                   : f == 5      ? MTLBlendFactorZero
                                 : BlendFactor(f);
          };
          ca.sourceRGBBlendFactor = fixa(b & 0x1F);
          ca.rgbBlendOperation = BlendOp((b >> 5) & 7);
          ca.destinationRGBBlendFactor = fixa((b >> 8) & 0x1F);
          if ((b >> 29) & 1) {
            ca.sourceAlphaBlendFactor = BlendFactor((b >> 16) & 0x1F);
            ca.alphaBlendOperation = BlendOp((b >> 21) & 7);
            ca.destinationAlphaBlendFactor = BlendFactor((b >> 24) & 0x1F);
          } else {
            ca.sourceAlphaBlendFactor = ca.sourceRGBBlendFactor;
            ca.alphaBlendOperation = ca.rgbBlendOperation;
            ca.destinationAlphaBlendFactor = ca.destinationRGBBlendFactor;
          }
        }
      }
      d.depthAttachmentPixelFormat = depth_fmt;
      if (tessellated) {
        const u32 tf=r[Reg::VGT_TF_PARAM];
        const MTLTessellationPartitionMode partition[]{MTLTessellationPartitionModeInteger,
            MTLTessellationPartitionModePow2,MTLTessellationPartitionModeFractionalOdd,
            MTLTessellationPartitionModeFractionalEven};
        d.tessellationPartitionMode=partition[Bits(tf,2,3)];
        d.maxTessellationFactor=64;
        d.tessellationFactorFormat=MTLTessellationFactorFormatHalf;
        d.tessellationFactorStepFunction=tess.native ? MTLTessellationFactorStepFunctionConstant
                                                   : MTLTessellationFactorStepFunctionPerPatch;
        d.tessellationOutputWindingOrder=Bits(tf,5,3)==2 ? MTLWindingClockwise : MTLWindingCounterClockwise;
      }
      if (depth_fmt == MTLPixelFormatDepth32Float_Stencil8) {
        d.stencilAttachmentPixelFormat = depth_fmt;
      }
      pending.insert(pkey);
      std::string vsrc = MslSource(*vs, vo, vs_key);
      std::string fsrc = ps ? MslSource(*ps, fo, ps_key) : std::string();
      const bool flat = debug_fs && ps && out_type[0] == OutType::Float;
      const u64 compile_frame=frame_count;
      const double queued=StutterTrace::Output() ? StutterTrace::Now() : 0;
      compiler->Submit([this, d, vsrc = std::move(vsrc), fsrc = std::move(fsrc),
                        vs_key, ps_key, pkey, flat, vs_addr, ps_addr,compile_frame,queued] {
        // Capture the requesting frame/key, rather than reading renderer state
        // from a compiler worker. These opt-in phases separate a queue delay,
        // an exact-source function miss and driver pipeline creation.
        if(queued) StutterTrace::Record("pipeline_queue",queued,StutterTrace::Now(),compile_frame,pkey);
        id<MTLRenderPipelineState> pso = nil;
        id<MTLFunction> vfn=nil,ffn=nil;
        {
          StutterTrace::Scope timing("pipeline_vertex_function",compile_frame,pkey,vs_addr);
          vfn=compiler->Function(vs_key,vsrc,vs_addr);
        }
        if(!fsrc.empty()) {
          StutterTrace::Scope timing("pipeline_fragment_function",compile_frame,pkey,ps_addr);
          ffn=compiler->Function(ps_key,fsrc,ps_addr);
        }
        if (vfn && (fsrc.empty() || ffn)) {
          d.vertexFunction = vfn;
          d.fragmentFunction = flat ? debug_fs : ffn;
          NSError *err = nil;
          const auto t0 = std::chrono::steady_clock::now();
          {
            StutterTrace::Scope timing("pipeline_create",compile_frame,pkey,vs_addr);
            pso=compiler->RenderPipeline(d,&err);
          }
          ReportSlowCompile("pipeline", vs_addr, ps_addr, t0);
          if (!pso) {
            DC_ERROR("gpu: pipeline (vs %#llx ps %#llx): %s", vs_addr, ps_addr,
                     err ? err.localizedDescription.UTF8String : "?");
          }
        }
        {
          std::scoped_lock lk(done_lock);
          done_render.emplace_back(pkey, pso);
        }
        done_cv.notify_all();
      });
    }
    WaitCompiled(pkey);
    pit = pipelines.find(pkey);
  }
  id<MTLRenderPipelineState> pso = pit->second;
  if (!pso) {
    if (trace)
      DC_INFO("gpu:   skipped: no pipeline");
    return;
  }

  // ---- fixed function ----
  Rect sc{0, 0, static_cast<s32>(pass_width), static_cast<s32>(pass_height)};
  sc = Intersect(sc, ScissorReg(r[Reg::PA_SC_SCREEN_SCISSOR_TL],
                                r[Reg::PA_SC_SCREEN_SCISSOR_BR]));
  sc = Intersect(sc, ScissorReg(r[Reg::PA_SC_WINDOW_SCISSOR_TL],
                                r[Reg::PA_SC_WINDOW_SCISSOR_BR]));
  sc = Intersect(sc, ScissorReg(r[Reg::PA_SC_GENERIC_SCISSOR_TL],
                                r[Reg::PA_SC_GENERIC_SCISSOR_BR]));
  // bbport: tiled lighting leaves a small viewport scissor behind. It must
  // stop affecting later passes when VPORT_SCISSOR_ENABLE is cleared.
  if (r[Reg::PA_SC_MODE_CNTL_0] & 2)
    sc = Intersect(sc, ScissorReg(r[Reg::PA_SC_VPORT_SCISSOR_0_TL],
                                  r[Reg::PA_SC_VPORT_SCISSOR_0_TL + 1]));
  if (trace) {
    DC_INFO("gpu:   vte=%#x vp=[%g %g | %g %g | %g %g] scissor=(%d,%d)-(%d,%d) "
            "[scr %#x %#x win %#x %#x gen %#x %#x vp %#x %#x mode0 %#x] "
            "pass=%ux%u clip=%#x su=%#x",
            r[Reg::PA_CL_VTE_CNTL], r.F(Reg::PA_CL_VPORT_XSCALE),
            r.F(Reg::PA_CL_VPORT_XSCALE + 1), r.F(Reg::PA_CL_VPORT_XSCALE + 2),
            r.F(Reg::PA_CL_VPORT_XSCALE + 3), r.F(Reg::PA_CL_VPORT_XSCALE + 4),
            r.F(Reg::PA_CL_VPORT_XSCALE + 5), sc.x0, sc.y0, sc.x1, sc.y1,
            r[Reg::PA_SC_SCREEN_SCISSOR_TL], r[Reg::PA_SC_SCREEN_SCISSOR_BR],
            r[Reg::PA_SC_WINDOW_SCISSOR_TL], r[Reg::PA_SC_WINDOW_SCISSOR_BR],
            r[Reg::PA_SC_GENERIC_SCISSOR_TL], r[Reg::PA_SC_GENERIC_SCISSOR_BR],
            r[Reg::PA_SC_VPORT_SCISSOR_0_TL],
            r[Reg::PA_SC_VPORT_SCISSOR_0_TL + 1], r[0xA292], pass_width,
            pass_height, r[Reg::PA_CL_CLIP_CNTL], r[Reg::PA_SU_SC_MODE_CNTL]);
  }
  if (sc.x1 <= sc.x0 || sc.y1 <= sc.y0) {
    return;
  }

  {
    StutterTrace::Scope timing("draw_pipeline_bind",frame_count,vs_addr,ps_addr,1);
    [enc setRenderPipelineState:pso];
  }
  StutterTrace::Scope fixed_timing("draw_fixed_bind",frame_count,vs_addr,ps_addr,1);
  [enc setViewport:MTLViewport{0, 0, static_cast<double>(pass_width),
                               static_cast<double>(pass_height), 0, 1}];
  [enc setScissorRect:MTLScissorRect{static_cast<NSUInteger>(sc.x0),
                                     static_cast<NSUInteger>(sc.y0),
                                     static_cast<NSUInteger>(sc.x1 - sc.x0),
                                     static_cast<NSUInteger>(sc.y1 - sc.y0)}];
  if (depth) {
    const bool st = HasStencil(depth->tex);
    const u32 st_ops = r[Reg::DB_STENCIL_CONTROL];
    const u32 ref_f = r[Reg::DB_STENCILREFMASK],
              ref_b = r[Reg::DB_STENCILREFMASK_BF];
    [enc setDepthStencilState:GetDepthState(db_control, depth_clear, st, st_ops,
                                            ref_f, ref_b, stencil_clear)];
    if (st && (db_control & 1)) {
      const bool backface = (db_control >> 7) & 1;
      [enc setStencilFrontReferenceValue:StencilRef(st_ops, ref_f)
                      backReferenceValue:backface
                                             ? StencilRef(st_ops >> 12, ref_b)
                                             : StencilRef(st_ops, ref_f)];
    }
  }
  // PA_CL_CLIP_CNTL.ZCLIP_NEAR/FAR_DISABLE: depth is clamped instead of clipped
  // (shadPS4 ZclipEnable(): clipping only when both are enabled).
  const u32 clip_cntl = r[Reg::PA_CL_CLIP_CNTL];
  const bool zclip = !((clip_cntl >> 26) & 1) && !((clip_cntl >> 27) & 1);
  [enc setDepthClipMode:zclip ? MTLDepthClipModeClip : MTLDepthClipModeClamp];
  const u32 su = r[Reg::PA_SU_SC_MODE_CNTL];
  static const bool no_cull = std::getenv("DC_NO_CULL") != nullptr;
  // shadPS4 IsPrimitiveCulled: only triangle-type primitives are culled (not
  // rect lists, points or lines).
  const bool cullable = prim == PrimType::TriList ||
                        prim == PrimType::TriStrip ||
                        prim == PrimType::TriFan ||
                        prim == PrimType::QuadList ||
                        prim == PrimType::Polygon || tessellated;
  const bool cull_front = (su & 1) && !no_cull && cullable,
             cull_back = ((su >> 1) & 1) && !no_cull && cullable;
  [enc setCullMode:(cull_front && cull_back) ? MTLCullModeNone
                   : cull_front              ? MTLCullModeFront
                   : cull_back               ? MTLCullModeBack
                                             : MTLCullModeNone];
  [enc setFrontFacingWinding:((su >> 2) & 1) ? MTLWindingClockwise
                                             : MTLWindingCounterClockwise];
  if (depth) {
    // Polygon offset (shadow maps rely on it): POLY_OFFSET_FRONT/BACK_ENABLE.
    const bool front = (su >> 11) & 1, back = (su >> 12) & 1;
    if (front || back) {
      const u32 base = Reg::PA_SU_POLY_OFFSET_FRONT_SCALE + (front ? 0 : 2);
      [enc setDepthBias:r.F(base + 1)
             slopeScale:r.F(base) / 16.0f
                  clamp:r.F(Reg::PA_SU_POLY_OFFSET_CLAMP)];
    } else {
      [enc setDepthBias:0 slopeScale:0 clamp:0];
    }
  }
  [enc setBlendColorRed:r.F(Reg::CB_BLEND_RED)
                  green:r.F(Reg::CB_BLEND_RED + 1)
                   blue:r.F(Reg::CB_BLEND_RED + 2)
                  alpha:r.F(Reg::CB_BLEND_RED + 3)];

  std::array<float,6> viewport;
  for (u32 k=0;k<viewport.size();++k) viewport[k]=r.F(Reg::PA_CL_VPORT_XSCALE+k);
  const auto consts=Shader::MakeDrawConsts(r[Reg::PA_CL_VTE_CNTL],viewport,pass_width,pass_height);
  fixed_timing.End();

  // ---- bindings ----
  if (std::getenv("DC_DEBUG_MEMTEST")) {
    MemTest(vs_addr);
  }
  if(trace && !capture_root.empty()) {
    TraceHtileReaders(*vs,vud,0,0,0);CaptureMotionConstants(*vs,vud);
    if(ps) {TraceHtileReaders(*ps,pud,0,0,0);CaptureMotionConstants(*ps,pud);}
  }
  if(indirect) {
    // Unknown GPU counts must never prove a native velocity producer/dilation.
    for(auto* color:colors) if(color) color->velocity.safe=false;
  } else ObserveNativeVelocity(r,*vs,ps,vud,pud,colors,ps_res.tex,dc,sc);
  // On-demand nested material constants, using the same snapshotted user data
  // as the GPU. Syntax: DC_PEEK_PS_CHAIN=2:30:60,2:30:20; all offsets are hex,
  // intermediate offsets load a guest pointer and the last reads eight floats.
  if (trace && ps && dump_ps && ps_addr==dump_ps) {
    if (const char* chains=std::getenv("DC_PEEK_PS_CHAIN")) {
      const char* q=chains;
      for (unsigned chain=0;*q && chain<8;++chain) {
        const char* begin=q;
        char* end=nullptr;
        const auto index=std::strtoul(q,&end,10);
        if (end==q || index>=15 || *end!=':') break;
        u64 address=(u64(pud[index]) | (u64(pud[index+1]&0xfff)<<32))&kGpuAddrMask;
        bool valid=true;
        q=end+1;
        for (unsigned step=0;step<8;++step) {
          const auto offset=std::strtoull(q,&end,16);
          if (end==q || offset>kGpuAddrMask-address) { valid=false;break; }
          address+=offset;
          q=end;
          if (*q!=':') break;
          if (!Readable(address,8)) { valid=false;break; }
          std::memcpy(&address,reinterpret_cast<const void*>(address),8);
          address&=kGpuAddrMask;
          ++q;
          if (step==7) valid=false;
        }
        if (valid && (*q==',' || !*q) && Readable(address,32)) {
          float values[8];std::memcpy(values,reinterpret_cast<const void*>(address),32);
          const std::string label(begin,q-begin);
          DC_INFO("gpu:   chain %s @%#llx: %g %g %g %g %g %g %g %g",label.c_str(),address,
              values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7]);
        }
        while (*q && *q!=',') ++q;
        if (*q==',') ++q;
      }
    }
  }
  StutterTrace::Scope binding_timing("draw_bind_resources",frame_count,vs_addr,ps_addr,1);
  UpdateMemMap();
  id<MTLBuffer> draw_map=mem_map;
  const u64 draw_map_generation=mem_map_gen;
  [enc setVertexBytes:vud length:64 atIndex:Shader::Binding::UserData];
  if(binding_cache.Buffer(false,Shader::Binding::Mem0,(__bridge const void*)mem[0]))
    [enc setVertexBuffer:mem[0] offset:0 atIndex:Shader::Binding::Mem0];
  if(binding_cache.Buffer(false,Shader::Binding::Mem1,(__bridge const void*)mem[1]))
    [enc setVertexBuffer:mem[1] offset:0 atIndex:Shader::Binding::Mem1];
  if(binding_cache.Buffer(false,Shader::Binding::MemMap,(__bridge const void*)draw_map))
    [enc setVertexBuffer:draw_map offset:0 atIndex:Shader::Binding::MemMap];
  [enc setVertexBytes:&consts
               length:sizeof(consts)
              atIndex:Shader::Binding::Draw];
  if (tessellated) {
    if (tess.native) {
      [enc setVertexBytes:tess.local_ud.data() length:64 atIndex:Shader::Binding::TessLocalUserData];
      [enc setVertexBytes:tess.hull_ud.data() length:64 atIndex:Shader::Binding::TessHullUserData];
      [enc setVertexBytes:&tess.params length:sizeof(tess.params) atIndex:Shader::Binding::TessParams];
    } else [enc setVertexBuffer:tess.lds offset:tess.lds_offset atIndex:Shader::Binding::TessLds];
  }
  for (u32 k = 0; k < vs_res.tex.size(); ++k) {
    StutterTrace::Scope timing("draw_vertex_texture_bind",frame_count,vs_addr,k,1);
    if(binding_cache.Texture(false,k,(__bridge const void*)vs_res.tex[k]))
      [enc setVertexTexture:vs_res.tex[k] atIndex:k];
  }
  for (u32 u = 0; u < vs_res.smp.size() && u < 16; ++u) {
    if(binding_cache.Sampler(false,u,(__bridge const void*)vs_res.smp[u]))
      [enc setVertexSamplerState:vs_res.smp[u] atIndex:u];
  }
  if (ps) {
    [enc setFragmentBytes:pud length:64 atIndex:Shader::Binding::UserData];
    if(fo.static_smrd) [enc setFragmentBytes:static_smrd_packed.words.data()
        length:static_smrd_packed.count*sizeof(u32) atIndex:Shader::Binding::StaticSmrd];
    if(binding_cache.Buffer(true,Shader::Binding::Mem0,(__bridge const void*)mem[0]))
      [enc setFragmentBuffer:mem[0] offset:0 atIndex:Shader::Binding::Mem0];
    if(binding_cache.Buffer(true,Shader::Binding::Mem1,(__bridge const void*)mem[1]))
      [enc setFragmentBuffer:mem[1] offset:0 atIndex:Shader::Binding::Mem1];
    if(binding_cache.Buffer(true,Shader::Binding::MemMap,(__bridge const void*)draw_map))
      [enc setFragmentBuffer:draw_map offset:0 atIndex:Shader::Binding::MemMap];
    for (u32 k = 0; k < ps_res.tex.size(); ++k) {
      StutterTrace::Scope timing("draw_fragment_texture_bind",frame_count,ps_addr,k,1);
      if(binding_cache.Texture(true,k,(__bridge const void*)ps_res.tex[k]))
        [enc setFragmentTexture:ps_res.tex[k] atIndex:k];
    }
    for (u32 u = 0; u < ps_res.smp.size() && u < 16; ++u) {
      if(binding_cache.Sampler(true,u,(__bridge const void*)ps_res.smp[u]))
        [enc setFragmentSamplerState:ps_res.smp[u] atIndex:u];
    }
    if (depth_bounds) {
      const float zb[2] = {r.F(Reg::DB_DEPTH_BOUNDS_MIN),
                           r.F(Reg::DB_DEPTH_BOUNDS_MAX)};
      if (trace) {
        DC_INFO("gpu:   depth bounds [%g, %g] depth fmt %lu", zb[0], zb[1],
                (unsigned long)depth->tex.pixelFormat);
      }
      [enc setFragmentBytes:zb
                     length:sizeof(zb)
                    atIndex:Shader::kDepthBoundsBuffer];
      [enc setFragmentTexture:depth->bounds_copy
                      atIndex:Shader::kDepthBoundsTexture];
    }
    if (probe_slot >= 0) {
      [enc setFragmentBuffer:probe_buf
                      offset:u64(probe_slot) * Shader::kProbeWords * 4
                     atIndex:Shader::kProbeBinding];
    }
  }
  binding_timing.End();

  if(native_indirect_enabled) {
    TrackProgramWriter(*vs,vud);
    if(ps) TrackProgramWriter(*ps,pud);
    // Two guest-writing stages may mutate each other's loaded V# tables.
    // Their independent CPU extraction is not proof of the combined extent.
    if(ps && !vs->buffer_writes.empty() && !ps->buffer_writes.empty()) {
      TraceProgramWriter(*vs,512,"two-writing-stages");TraceProgramWriter(*ps,512,"two-writing-stages");
      TrackGuestWriter({},true);
    }
    // The exact native LS/HS replacements only load guest memory and write
    // thread-local LDS/fixed factors. Translated preparation was conservatively
    // registered as an unknown writer at its own compute encoder above.
  }

  if (capture) {
    debug_vs_buf = [device newBufferWithLength:64 * 16
                                       options:MTLResourceStorageModeShared];
    [enc setVertexBuffer:debug_vs_buf offset:0 atIndex:6];
    DC_INFO("gpu: capturing VS %#llx ud: %08x %08x %08x %08x %08x %08x %08x "
            "%08x %08x %08x %08x %08x %08x %08x %08x %08x",
            vs_addr, vs_ud[0], vs_ud[1], vs_ud[2], vs_ud[3], vs_ud[4], vs_ud[5],
            vs_ud[6], vs_ud[7], vs_ud[8], vs_ud[9], vs_ud[10], vs_ud[11],
            vs_ud[12], vs_ud[13], vs_ud[14], vs_ud[15]);
  }
  // Address proof and read ticket belong to the ACTUAL consumer command,
  // after all setup/index uploads. No guest descriptor/payload bytes are read.
  const auto prepare_vertex_root=[&] {
    if(!vo.direct_vertex_root)return;
    const bool ordinary=!tessellated && !rect && !vo.points && !vo.debug_capture && !probe_buf;
    const auto roots=GuardedVertexRoot(*vs,ps,std::span<const u32,16>(vud),draw_map,
        draw_map_generation,ordinary);
    [enc setVertexBytes:&roots length:sizeof(roots) atIndex:Shader::Binding::StaticSmrd];
  };
  // ---- primitive assembly ----
  const auto prepare_direct_smrd=[&] {
    if(!fo.direct_smrd)return;
    // Publish only after all setup/Upload/profiling boundaries, on the command
    // that actually encodes this draw. Only address/map metadata is inspected.
    const bool ordinary=!tessellated && !rect && !vo.points && !vo.debug_capture && !probe_buf;
    const auto addresses=GuardedDirectSmrd(*vs,*ps,std::span<const u32,16>(pud),
        draw_map,draw_map_generation,ordinary);
    [enc setFragmentBytes:&addresses length:sizeof(addresses) atIndex:Shader::Binding::StaticSmrd];
  };
  const auto note_stage_draw=[&] {
    if(capture_wind)MarkWindCaptureDraw();
    if(stage_counter_pass) {
      // debug_fs replaces the translated fragment function without a compiler
      // cache key. Preserve GCN provenance and mark its encoded PS key unknown.
      const bool ps_key_known=!(debug_fs && ps && out_type[0]==OutType::Float);
      StageCounterTimeline::NoteDraw(*stage_counter_pass,vs_addr,ps_addr,vs->hash,ps ? ps->hash : 0,
          {vs_key,ps_key_known ? ps_key : 0,pkey,true,ps_key_known,true});
    }
    unsigned wind_targets=0;
    if(vo.wind_consumed)for(auto* color:colors)wind_targets+=color!=nullptr;
    if(vo.wind_consumed && !BindWindConsumedDraw(*vs,vud,vs_key,pkey,draw_map_generation,dc,
        pass_width,pass_height,wind_targets)) {
      execution->Fail("Consumed-wind diagnostic could not bind its owned actual-consumer record.");
      return false;
    }
    const bool depth_written = depth && (db_control & 6u) == 6u;
    // Stamp possible writes on the actual encoded draw, including changes
    // from read-only to writable state within the same render pass.
    const u64 seq=++write_seq;
    for(auto* target:colors)if(target)target->written=seq;
    if(depth_written)depth->written=seq;
    // Select the bounded draw's pre-draw snapshot first; only a depth write
    // invalidates it for the following draw. Stencil-only writes leave it valid.
    if(depth_written)++depth->depth_revision;
    if(frame_generation_enabled.load(std::memory_order_relaxed) && frame_generation)
      for(auto* target:colors)if(target)frame_generation->Draw((__bridge void*)target->tex,target->written,
          target==colors[0] && frame_generation_display_copy);
    return true;
  };
  if (tessellated) {
    prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
    StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
    [enc setTessellationFactorBuffer:tess.factors offset:tess.factor_offset instanceStride:0];
    [enc drawPatches:1 patchStart:0 patchCount:dc.count patchIndexBuffer:nil
        patchIndexBufferOffset:0 instanceCount:1 baseInstance:0];
    return;
  }
  const u32 base_vertex = r[Reg::VGT_INDX_OFFSET];
  const NSUInteger instances = std::max<u32>(dc.instances, 1);
  MTLPrimitiveType mprim = MTLPrimitiveTypeTriangle;
  bool to_list = false; // fans and quads are rebuilt as triangle lists
  switch (prim) {
  case PrimType::PointList:
    mprim = MTLPrimitiveTypePoint;
    break;
  case PrimType::LineList:
    mprim = MTLPrimitiveTypeLine;
    break;
  case PrimType::LineStrip:
    mprim = MTLPrimitiveTypeLineStrip;
    break;
  case PrimType::TriList:
    mprim = MTLPrimitiveTypeTriangle;
    break;
  case PrimType::TriStrip:
    mprim = MTLPrimitiveTypeTriangleStrip;
    break;
  case PrimType::TriFan:
  case PrimType::Polygon:
  case PrimType::QuadList:
    to_list = true;
    break;
  case PrimType::RectList:
    if (dc.indexed) {
      ReportOnce("indexed RECTLIST draws are not supported");
      return;
    }
    {
      prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
      StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
      [enc drawPrimitives:MTLPrimitiveTypeTriangle
              vertexStart:0
              vertexCount:(dc.count / 3) * 6
            instanceCount:instances];
    }
    return;
  default:
    ReportOnce("primitive type " + std::to_string(static_cast<u32>(prim)) +
               " not supported");
    return;
  }

  if (!dc.indexed && !to_list) {
    prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
    StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
    if(indirect) {
      [enc drawPrimitives:mprim indirectBuffer:indirect indirectBufferOffset:0];
      return;
    }
    [enc drawPrimitives:mprim
            vertexStart:base_vertex
            vertexCount:dc.count
          instanceCount:instances];
    return;
  }
  if (!dc.indexed && prim == PrimType::QuadList) {
    const u32 quads = std::min<u32>(dc.count / 4, 0x10000);
    prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
    StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
    [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                    indexCount:quads * 6
                     indexType:MTLIndexTypeUInt32
                   indexBuffer:quad_indices
             indexBufferOffset:0
                 instanceCount:instances
                    baseVertex:base_vertex
                  baseInstance:0];
    return;
  }

  // Indexed lists and strips read the guest index buffer in place (Metal
  // restarts strips at 0xFFFF / 0xFFFFFFFF like VGT_MULTI_PRIM_IB_RESET_INDX =
  // -1).
  if (dc.indexed && !to_list && (dc.index_size == 2 || dc.index_size == 4)) {
    const u64 src = dc.index_base + u64(dc.index_offset) * dc.index_size;
    id<MTLBuffer> ibuf = nil;
    u64 ioff = 0;
    if ((src % dc.index_size) == 0 &&
        Locate(src, u64(dc.count) * dc.index_size, &ibuf, &ioff)) {
      prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
      StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
      [enc drawIndexedPrimitives:mprim
                      indexCount:dc.count
                       indexType:dc.index_size == 2 ? MTLIndexTypeUInt16
                                                    : MTLIndexTypeUInt32
                     indexBuffer:ibuf
               indexBufferOffset:ioff
                   instanceCount:instances
                      baseVertex:base_vertex
                    baseInstance:0];
      return;
    }
  }

  // Gather this draw's indices (guest buffer or implicit 0..n-1).
  std::vector<u32> idx(dc.count);
  if (dc.indexed) {
    const u64 src = dc.index_base + u64(dc.index_offset) * dc.index_size;
    if (!Readable(src, u64(dc.count) * dc.index_size)) {
      ReportOnce("index buffer not mapped");
      return;
    }
    if (dc.index_size == 4) {
      std::memcpy(idx.data(), reinterpret_cast<const void *>(src),
                  dc.count * 4ull);
    } else {
      const auto *s16 = reinterpret_cast<const u16 *>(src);
      for (u32 k = 0; k < dc.count; ++k) {
        idx[k] =
            s16[k] == 0xFFFF ? 0xFFFFFFFFu : s16[k]; // keep the restart index
      }
    }
  } else {
    for (u32 k = 0; k < dc.count; ++k) {
      idx[k] = k;
    }
  }
  if (to_list) {
    std::vector<u32> list;
    if (prim == PrimType::QuadList) {
      for (u32 q = 0; q + 3 < dc.count; q += 4) {
        list.insert(list.end(), {idx[q], idx[q + 1], idx[q + 2], idx[q],
                                 idx[q + 2], idx[q + 3]});
      }
    } else {
      for (u32 k = 1; k + 1 < dc.count; ++k) {
        list.insert(list.end(), {idx[0], idx[k], idx[k + 1]});
      }
    }
    idx.swap(list);
    mprim = MTLPrimitiveTypeTriangle;
  }
  if (idx.empty()) {
    return;
  }
  auto [buf, off] = Upload(idx.data(), idx.size() * 4);
  prepare_direct_smrd();prepare_vertex_root();if(!note_stage_draw())return;
  StutterTrace::Scope timing("draw_encode",frame_count,vs_addr,ps_addr,1);
  [enc drawIndexedPrimitives:mprim
                  indexCount:idx.size()
                   indexType:MTLIndexTypeUInt32
                 indexBuffer:buf
           indexBufferOffset:off
               instanceCount:instances
                  baseVertex:base_vertex
                baseInstance:0];
}

void MetalRenderer::Impl::Dispatch(const RegFile &r, u32 gx, u32 gy, u32 gz,
    id<MTLBuffer> indirect,u64 indirect_offset,u64 indirect_address) {
  const u64 addr = r.ProgramAddr(Reg::COMPUTE_PGM_LO);
  // Trace an infrequent upload kernel even when it runs outside a requested
  // frame (for example while the game generates a character's face texture).
  static const u64 trace_cs = std::getenv("DC_TRACE_CS")
      ? std::strtoull(std::getenv("DC_TRACE_CS"), nullptr, 16) : 0;
  struct RestoreTrace {
    bool& value;
    bool previous;
    ~RestoreTrace() { value = previous; }
  } restore_trace{trace, trace};
  if (trace_cs && addr == trace_cs) trace = true;
  const u32 rsrc2 = r[Reg::COMPUTE_PGM_RSRC2];
  const u32 *ud = r.UserData(Reg::COMPUTE_USER_DATA_0);
  const u32 tx = std::max<u32>(1, r[Reg::COMPUTE_NUM_THREAD_X] & 0xFFFF);
  const u32 ty = std::max<u32>(1, r[Reg::COMPUTE_NUM_THREAD_Y] & 0xFFFF);
  const u32 tz = std::max<u32>(1, r[Reg::COMPUTE_NUM_THREAD_Z] & 0xFFFF);
  if (trace) {
    DC_INFO("gpu: dispatch cs=%#llx groups %ux%ux%u threads %ux%ux%u rsrc2=%#x "
            "ud=%08x %08x %08x %08x %08x %08x %08x "
            "%08x %08x %08x %08x %08x %08x %08x %08x %08x",
            addr, gx, gy, gz, tx, ty, tz, rsrc2, ud[0], ud[1], ud[2], ud[3],
            ud[4], ud[5], ud[6], ud[7], ud[8], ud[9], ud[10], ud[11], ud[12],
            ud[13], ud[14], ud[15]);
  }
  if (!addr || (!indirect && (!gx || !gy || !gz))) {
    return;
  }
  ++stat_dispatches;
  if (trace) {
    static const u64 peek_cs =
        std::getenv("DC_PEEK_CS")
            ? std::strtoull(std::getenv("DC_PEEK_CS"), nullptr, 16)
            : 0;
    if (addr == peek_cs) {
      for (u32 k = 0; k + 1 < 12; k += 4) {
        peek_addrs.push_back((u64(ud[k]) | (u64(ud[k + 1] & 0xFFF) << 32)) &
                             kGpuAddrMask);
      }
      // DC_PEEK_TABLE=<ud>,<dword>: also the V# found at pointer ud[n..n+1] +
      // dword*4
      if (const char *pt = std::getenv("DC_PEEK_TABLE")) {
        char *e = nullptr;
        const u32 n = std::strtoul(pt, &e, 10);
        const u32 off = *e ? std::strtoul(e + 1, nullptr, 10) : 0;
        const u64 table = u64(ud[n]) | (u64(ud[n + 1] & 0xFFFF) << 32);
        if (n + 1 < 16 && Readable(table + off * 4, 16)) {
          const u32 *v = reinterpret_cast<const u32 *>(table + off * 4);
          DC_INFO("gpu: peek table %#llx+%u: V# %08x %08x %08x %08x", table,
                  off, v[0], v[1], v[2], v[3]);
          peek_addrs.push_back((u64(v[0]) | (u64(v[1] & 0xFFF) << 32)) &
                               kGpuAddrMask);
        }
      }
    }
  }
  Program *cs = GetProgram(Stage::Compute, addr, ud, Bits(rsrc2, 1, 5),
                           Bits(rsrc2, 11, 2), rsrc2);
  if (!cs) return;
  TraceHtileReaders(*cs,ud,gx,gy,gz);
  if(!indirect && cs->retail_id==0x9a9cf8a9) CaptureSceneBoundary();
  if(!indirect && cs->retail_id==0x9a9cf8a9 && frame_generation_enabled.load(std::memory_order_relaxed) &&
      scene_camera && scene_camera_frame==frame_count && temporal_depth) {
    EndPass();EnsureCommandBuffer();
    if(!frame_generation)frame_generation=std::make_unique<FrameGeneration>((__bridge void*)device);
    const auto motion=NativeVelocityInput();
    frame_generation->Scene((__bridge void*)cmd,(__bridge void*)temporal_depth->tex,*scene_camera,frame_count,motion ? &*motion : nullptr);
  }
  if (!indirect && cs->retail_id==0x9a9cf8a9 && temporal_aa.load(std::memory_order_relaxed) &&
      temporal && temporal_depth && temporal_color) {
    // The retail scene-postprocessing boundary used by bbport. Resolve HDR
    // before tone mapping and HUD composition, retaining the guest's targets.
    EndPass(); EnsureCommandBuffer();
    const auto motion=NativeVelocityInput();
    if (void* output=temporal->Encode((__bridge void*)cmd,(__bridge void*)temporal_color->tex,
                                     (__bridge void*)temporal_depth->tex,frame_count,motion ? &*motion : nullptr)) {
      auto blit=[cmd blitCommandEncoder];
      [blit copyFromTexture:(__bridge id<MTLTexture>)output sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
          sourceSize:MTLSizeMake(temporal_color->width,temporal_color->height,1) toTexture:temporal_color->tex
          destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
      [blit endEncoding];
      temporal_color->written=++write_seq; temporal_resolved=true;
      CaptureRaw(temporal_color->tex,"scene_resolved_hdr_"+Hex(temporal_color->addr));
    }
  }
  // Typed buffer clears also affect private Metal render targets. Running the
  // GCN kernel alone clears only the backing pool, leaving old emissive pixels
  // on the GPU to form trails as the camera moves. Recognize only a complete
  // canonical uniform fill here; update backing aliases as well as textures.
  if (!indirect && cs->fill_destination_ud>=0 && tx==64 && ty==1 && tz==1 && gy==1 && gz==1) {
    const u32* destination=ud+cs->fill_destination_ud;
    const u32* value=ud+cs->fill_value_ud;
    const u64 dst=(u64(destination[0]) | (u64(destination[1]&0xfff)<<32)) & kGpuAddrMask;
    const u64 source=(u64(value[0]) | (u64(value[1]&0xfff)<<32)) & kGpuAddrMask;
    const u32 stride=(destination[1]>>16)&0x3fff;
    const u32 format=(destination[3]>>15)&15, number=(destination[3]>>12)&7;
    const auto fi=TextureFormat(format,number);
    auto targets=color_targets.find(dst);
    if (targets!=color_targets.end() && stride && stride==fi.bytes_per_block &&
        !fi.compressed && !(destination[1]&0x80000000u) &&
        u64(gx)*cs->fill_elements_per_group>=destination[2] &&
        !Latest(color_targets,source) && !Latest(depth_targets,source) && Readable(source,16)) {
      const u32* words=reinterpret_cast<const u32*>(source);
      const bool zero=!(words[0] | words[1] | words[2] | words[3]);
      double value[4];
      bool finite=true;
      for (u32 k=0;k<4;++k) {
        value[k]=std::bit_cast<float>(words[k]);
        finite=finite && std::isfinite(value[k]);
      }
      const bool floating=finite && fi.sample==TexSample::Float &&
          (number==NumFormat::Unorm || number==NumFormat::Snorm || number==NumFormat::Float);
      if (trace) DC_INFO("gpu: typed fill %#llx value %08x %08x %08x %08x",dst,words[0],words[1],words[2],words[3]);
      if (zero || floating) {
        for (auto& rt:targets->second) {
          const MTLPixelFormat pixel=rt->tex.pixelFormat;
          const bool bgra=pixel==MTLPixelFormatBGRA8Unorm || pixel==MTLPixelFormatBGRA8Unorm_sRGB;
          const bool srgb=pixel==MTLPixelFormatBGRA8Unorm_sRGB || pixel==MTLPixelFormatRGBA8Unorm_sRGB;
          const bool compatible=pixel==fi.format ||
              (fi.format==MTLPixelFormatRGBA8Unorm && (bgra || srgb));
          if (rt->dim!=TexDim::Tex2D || rt->levels!=1 ||
              PixelBytes(rt->tex.pixelFormat)!=stride ||
              (!zero && !compatible) ||
              u64(rt->width)*rt->height>destination[2]) continue;
          EndPass();
          EnsureCommandBuffer();
          // Clear through the buffer's raw format. This preserves byte order
          // and avoids an sRGB transfer (including its rounding) during a fill.
          id<MTLTexture> view=compatible ? TargetView(*rt,0,1,fi.format) : rt->tex;
          auto* clear=[MTLRenderPassDescriptor renderPassDescriptor];
          clear.colorAttachments[0].texture=view;
          clear.colorAttachments[0].loadAction=MTLLoadActionClear;
          clear.colorAttachments[0].storeAction=MTLStoreActionStore;
          clear.colorAttachments[0].clearColor=MTLClearColorMake(value[0],value[1],value[2],value[3]);
          [[cmd renderCommandEncoderWithDescriptor:clear] endEncoding];
          rt->initialized=true;
          rt->clear_pending=false;
          rt->written=++write_seq;
          ObserveNativeVelocityClear(*rt,value);
          if (trace) DC_INFO("gpu: typed fill clears target %#llx (%ux%u)",dst,rt->width,rt->height);
        }
      }
      // A canonical raw-zero fill has the same bytes in float, normalized and
      // integer formats. Metal's blit clear avoids millions of translated GCN
      // invocations while preserving exactly the descriptor's record range.
      const u64 bytes=u64(destination[2])*stride;
      id<MTLBuffer> buffer=nil; u64 offset=0;
      if (zero && bytes && !(bytes&3) && !(dst&3) &&
          Locate(dst,bytes,&buffer,&offset)) {
        EndPass(); EnsureCommandBuffer();
        auto clear=[cmd blitCommandEncoder];
        [clear fillBuffer:buffer range:NSMakeRange(offset,bytes) value:0];
        [clear endEncoding];
        if(native_indirect_enabled) TrackGuestWriter({{offset+(buffer==mem[0] ? 0 : mem_split),bytes}});
        if (trace) DC_INFO("gpu: typed zero fill uses blit (%llu bytes)",bytes);
        return;
      }
      // The original typed kernel performs page translation and format packing
      // for every record. For a checked uniform RGBA8 fill, pack once and use
      // the existing contiguous Metal fill kernel. Texture aliases were cleared
      // above; the backing bytes (including descriptor padding) still change in
      // queue order. Unknown formats/values/mappings retain the original kernel.
      const auto packed=fast_typed_fills ? PackedRgba8Fill(format,number,stride,words) : std::nullopt;
      if (packed && fill_pipeline && bytes && bytes<=UINT32_MAX && !(dst&3) &&
          Locate(dst,bytes,&buffer,&offset)) {
        EndPass(); EnsureCommandBuffer();
        const u32 constants[]{*packed,u32(bytes/4)};
        auto fill=[cmd computeCommandEncoder];
        [fill setComputePipelineState:fill_pipeline];
        [fill setBuffer:buffer offset:offset atIndex:0];
        [fill setBytes:constants length:sizeof(constants) atIndex:1];
        [fill dispatchThreads:MTLSizeMake(bytes/4,1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        [fill endEncoding];
        if(native_indirect_enabled) TrackGuestWriter({{offset+(buffer==mem[0] ? 0 : mem_split),bytes}});
        if (trace) DC_INFO("gpu: typed pattern fill uses Metal (%llu bytes, %08x)",bytes,*packed);
        return;
      }
    }
  }
  // GPU-only render targets have no current pixels in the CPU backing pool.
  // Recognize the actual copy kernel, including its descriptor registers,
  // before substituting an ordered Metal texture copy.
  if (!indirect && cs->copy_source_ud>=0 && tx==64 && ty==1 && tz==1 && gy==1 && gz==1 &&
      (cs->copy_uses_group_id || gx==1)) {
    const u32* source=ud+cs->copy_source_ud;
    const u32* destination=ud+cs->copy_destination_ud;
    const u64 src = (u64(source[0]) | (u64(source[1] & 0xFFF) << 32)) & kGpuAddrMask;
    const u64 dst = (u64(destination[0]) | (u64(destination[1] & 0xFFF) << 32)) & kGpuAddrMask;
    RenderTarget *from_color = Latest(color_targets, src);
    RenderTarget *from_z = Latest(depth_targets, src);
    const bool from_depth = from_z &&
        (!from_color || from_z->written > from_color->written);
    RenderTarget *from = from_depth ? from_z : from_color;
    const u64 words=from ? u64(from->width)*from->height*
        (from_depth ? 4 : PixelBytes(from->tex.pixelFormat))/4 : 0;
    const bool raw32=source[3]==0x24004 && destination[3]==0x24004 &&
        ((source[1]>>16)&0x3fff)==4 && ((destination[1]>>16)&0x3fff)==4;
    if (from && from->levels==1 && from->dim==TexDim::Tex2D && dst && dst != src && raw32 && words &&
        source[2]>=words && destination[2]>=words && u64(gx)*tx>=words) {
      RenderTarget &to =
          GetTarget(from_depth ? depth_targets : color_targets, dst,
                    from->tex.pixelFormat, from->width, from->height,
                    from_depth, from->levels, from->dim, from->depth_or_layers);
      EndPass();
      EnsureCommandBuffer();
      id<MTLBlitCommandEncoder> b = [cmd blitCommandEncoder];
      [b copyFromTexture:from->tex toTexture:to.tex];
      [b endEncoding];
      to.initialized = true;
      to.clear_pending = from->clear_pending;
      to.clear_color = from->clear_color;
      to.clear_depth = from->clear_depth;
      to.clear_stencil = from->clear_stencil;
      if (from_depth) ++to.depth_revision;
      to.written = ++write_seq;
      to.velocity.safe=false;
      if (trace || from->dim != TexDim::Tex2D) {
        DC_INFO("gpu:   surface copy %#llx -> %#llx (%ux%u dim %d layers %u fmt %lu)",
                src, dst, from->width, from->height, (int)from->dim,
                from->depth_or_layers, (unsigned long)from->tex.pixelFormat);
      }
      return;
    }
  }
  // A shader that only stores to a CMASK/HTILE surface it gets straight
  // from user data is a fast clear (others read metadata, e.g. HTILE for
  // tile depth bounds, and run normally).
  for (u32 k = 0; k + 3 < 16 && !metas.empty(); ++k) {
    const u64 base =
        (u64(ud[k]) | (u64(ud[k + 1] & 0xFFF) << 32)) & kGpuAddrMask;
    if (metas.count(base) && OnlyStoresTo(*cs, k) && MarkMetaCleared(base, 1)) {
      EndPass();
      return;
    }
  }
  // Full-range fallback is only for an actual read-only metadata descriptor.
  // Retail shadow kernels also read/modify/write HTILE state bits. Replacing
  // their input with depth bounds corrupts that state and adds redundant fills.
  // Do not interpret arbitrary overlapping user-data words as descriptors.
  std::unordered_set<u64> expanded_metadata;
  for (const auto& read:cs->buffer_reads) {
    if (metas.empty() || !read.HasReadOnlyDescriptor()) continue;
    std::array<u32,4> descriptor;
    for (unsigned k=0;k<4;++k) descriptor[k]=Shader::ResolveSgpr(read.descriptor[k],ud);
    const u64 base =
        (u64(descriptor[0]) | (u64(descriptor[1] & 0xFFF) << 32)) & kGpuAddrMask;
    auto m = metas.find(base);
    if (m == metas.end() || !m->second.depth || !expanded_metadata.insert(base).second) {
      continue;
    }
    const u32 stride = (descriptor[1] >> 16) & 0x3FFF;
    const u64 bytes = u64(descriptor[2]) * (stride ? stride : 1);
    if (bytes && bytes < (64ull << 20) && !Shader::MayWriteBuffer(*cs,ud,base,bytes)) {
      expanding_htile = true; // our own fill, not the game clearing depth
      DmaData(0, base, 2, 0xFFFC000Fu, static_cast<u32>(bytes & ~3ull));
      expanding_htile = false;
    }
  }
  std::vector<TextureType> types;
  Resources res;
  resolving_shader = addr;
  Resolve(*cs, ud, types, res);

  Shader::MslOptions o;
  o.textures = types;
  const int probe_slot = WantsProbe(addr) ? ProbeSlot(
      addr, "dispatch " + std::to_string(stat_dispatches) + " cs " + Hex(addr)) : -1;
  if (probe_slot >= 0) {
    o.probe_pc = probe.pc;
    o.probe_x = probe.x;
    o.probe_y = probe.y;
  }
  u64 key = Hash(cs->hash);
  key = Hash(o.probe_pc, key);
  key = Hash(rsrc2, key);
  key = Fnv(types.data(), types.size() * sizeof(TextureType), key);
  const u32 group = tx * ty * tz;
  const u64 pkey = Hash(group, key);
  TakeCompiled();
  auto it = compute_pipelines.find(pkey);
  if (it == compute_pipelines.end()) {
    if (!pending.count(pkey)) {
      pending.insert(pkey);
      std::string src=MslSource(*cs,o,key);
      const u64 compile_frame=frame_count;
      const double queued=StutterTrace::Output() ? StutterTrace::Now() : 0;
      compiler->Submit([this, src = std::move(src), key, pkey, group,
                        addr,compile_frame,queued] {
        if(queued) StutterTrace::Record("pipeline_queue",queued,StutterTrace::Now(),compile_frame,pkey);
        id<MTLComputePipelineState> pso = nil;
        id<MTLFunction> fn=nil;
        {
          StutterTrace::Scope timing("pipeline_compute_function",compile_frame,pkey,addr);
          fn=compiler->Function(key,src,addr);
        }
        if(fn) {
          MTLComputePipelineDescriptor *d = [MTLComputePipelineDescriptor new];
          d.computeFunction = fn;
          d.maxTotalThreadsPerThreadgroup =
              group; // the guest's group size must fit as is
          NSError *err = nil;
          const auto t0 = std::chrono::steady_clock::now();
          {
            StutterTrace::Scope timing("pipeline_create",compile_frame,pkey,addr);
            pso=compiler->ComputePipeline(d,&err);
          }
          ReportSlowCompile("compute pipeline", addr, 0, t0);
          if (!pso) {
            DC_ERROR("gpu: compute pipeline %#llx: %s", addr,
                     err ? err.localizedDescription.UTF8String : "?");
          } else if (pso.maxTotalThreadsPerThreadgroup < group) {
            DC_ERROR("gpu: compute shader %#llx needs %u threads per group, "
                     "Metal allows %lu",
                     addr, group,
                     (unsigned long)pso.maxTotalThreadsPerThreadgroup);
            pso = nil;
          }
        }
        {
          std::scoped_lock lk(done_lock);
          done_compute.emplace_back(pkey, pso);
        }
        done_cv.notify_all();
      });
    }
    // Dispatches often produce data later work depends on: wait.
    WaitCompiled(pkey);
    it = compute_pipelines.find(pkey);
  }
  id<MTLComputePipelineState> pso = it->second;
  if (!pso) {
    return;
  }

  EndPass();
  ++work_since_commit;
  CommitAtEncoderBoundary();
  u32 cud[16];
  std::memcpy(cud, ud, sizeof(cud));
  u32* prepared[]{cud};
  SnapshotArena::Pin snapshot_pin(arena_pin);
  if(!PatchUserData(prepared)) return;
  EnsureCommandBuffer();
  UpdateMemMap();
  id<MTLComputeCommandEncoder> e=nil;
  std::optional<StageCounterTimeline::Pass> compute_counter;
  if(stage_counter_log && stage_counter_log->ComputeEnabled()) {
    auto descriptor=[MTLComputePassDescriptor computePassDescriptor];
    descriptor.dispatchType=MTLDispatchTypeSerial;
    StageCounterTimeline::Pass metadata;metadata.frame=frame_count;
    metadata.compute={addr,cs->hash,key,pkey,bool(indirect),!indirect,
        {indirect ? u64(0) : u64(gx),indirect ? u64(0) : u64(gy),indirect ? u64(0) : u64(gz)},
        {tx,ty,tz}};
    compute_counter=stage_counter_log->AttachCompute(descriptor,std::move(metadata));
    if(compute_counter) {
      @try {e=[cmd computeCommandEncoderWithDescriptor:descriptor];}
      @catch(NSException* exception) {(void)exception;e=nil;}
      if(!e) {stage_counter_log->CancelCompute(descriptor,*compute_counter);compute_counter.reset();}
    }
  }
  // Disabled, unsupported, exhausted or failed sampling preserves the exact
  // original encoder route; it adds no encoder/cut/commit or readiness change.
  if(!e)e=[cmd computeCommandEncoder];
  if(!e) {execution->Fail("Metal could not encode a compute dispatch.");return;}
  [e setComputePipelineState:pso];
  [e setBytes:cud length:64 atIndex:Shader::Binding::UserData];
  [e setBuffer:mem[0] offset:0 atIndex:Shader::Binding::Mem0];
  [e setBuffer:mem[1] offset:0 atIndex:Shader::Binding::Mem1];
  [e setBuffer:mem_map offset:0 atIndex:Shader::Binding::MemMap];
  [e setBuffer:gds offset:0 atIndex:Shader::Binding::Gds];
  for (u32 k = 0; k < res.tex.size(); ++k) {
    [e setTexture:res.tex[k] atIndex:k];
  }
  for (u32 u = 0; u < res.smp.size() && u < 16; ++u) {
    [e setSamplerState:res.smp[u] atIndex:u];
  }
  if (probe_slot >= 0) {
    [e setBuffer:probe_buf
          offset:u64(probe_slot) * Shader::kProbeWords * 4
         atIndex:Shader::kProbeBinding];
  }
  // LDS_SIZE is in 128-dword units on CI; Metal wants a multiple of 16 bytes.
  const u32 lds = std::max<u32>(16, Bits(rsrc2, 15, 9) * 512);
  [e setThreadgroupMemoryLength:std::min<u32>(lds, 32768) atIndex:0];
  if(indirect) {
    TrackProgramWriter(*cs,cud,{}, {tx,ty,tz});
    if(indirect_address) TrackIndirectRead(indirect_address,12,indirect,indirect_offset);
    [e dispatchThreadgroupsWithIndirectBuffer:indirect indirectBufferOffset:indirect_offset
        threadsPerThreadgroup:MTLSizeMake(tx,ty,tz)];
  } else {
    TrackProgramWriter(*cs,cud,{}, {tx,ty,tz}, {gx,gy,gz});
    [e dispatchThreadgroups:MTLSizeMake(gx, gy, gz)
        threadsPerThreadgroup:MTLSizeMake(tx, ty, tz)];
  }
  [e endEncoding];
  if(compute_counter)stage_counter_passes.push_back(std::move(*compute_counter));
  if (dump_passes && trace && dump_frames) {
    u32 n = 0;
    for (u32 k = 0; k < res.tex.size() && n < 8; ++k) {
      if (cs->images[k].storage &&
          (res.tex[k].textureType == MTLTextureType2D ||
           res.tex[k].textureType == MTLTextureType2DArray)) {
        pass_tex[n] = res.tex[k];
        pass_addr[n++] = addr;
      }
    }
    DumpPass();
  }
  // DC_DUMP_CS=<cs>: with DC_TRACE_FRAME, the textures that shader samples.
  static const u64 dump_cs = std::getenv("DC_DUMP_CS")
                                 ? std::strtoull(std::getenv("DC_DUMP_CS"),
                                                 nullptr, 16)
                                 : 0;
  if (dump_cs == addr && trace && dump_frames) {
    for (u32 k = 0; k < res.tex.size(); ++k) {
      id<MTLTexture> t = res.tex[k];
      if (cs->images[k].storage || !copy_pipeline) {
        continue;
      }
      const NSUInteger n =
          t.textureType == MTLTextureType2DArray ? t.arrayLength : 1;
      for (NSUInteger l = 0; l < std::min<NSUInteger>(t.mipmapLevelCount, 3);
           ++l) {
        for (NSUInteger sl = 0; sl < std::min<NSUInteger>(n, 12); ++sl) {
          if (t.textureType != MTLTextureType2D &&
              t.textureType != MTLTextureType2DArray) {
            continue;
          }
          DumpTexture([t newTextureViewWithPixelFormat:t.pixelFormat
                                           textureType:MTLTextureType2D
                                                levels:NSMakeRange(l, 1)
                                                slices:NSMakeRange(sl, 1)],
                      k, (u64(k) << 8) | (l << 4) | sl);
        }
      }
    }
  }
}

void MetalRenderer::Impl::Flip(const FlipInfo &f) {
  FinishNativeVelocityFrame();
  frame_generation_hud_pending=false;
  // Finish the previous frame's last pass under its own trace flag before
  // selecting the next request. Diagnostic profiling never cuts normal frames.
  if (profile_trace_only) EndPass();
  ++frame_count;
  CollectTextures();
  temporal_active.store(temporal_resolved,std::memory_order_relaxed);
  temporal_resolved=false; temporal_depth=temporal_color=nullptr;
  if (temporal) temporal->EndFrame();
  if (trace_file) {
    u64 token = 0;
    if (std::ifstream request(trace_file); request >> token && token != trace_token) {
      trace_token = token;
      dump_ps = 0;
      trace_on_ps=0;
      dump_selected_outputs=false;
      request >> std::hex >> dump_ps;
      request >> std::ws;
      const bool armed=request.peek()=='!';
      if (armed) {
        request.get();
        trace_on_ps=dump_ps;
        dump_selected_outputs=true;
      }
      u32 pc=0,x=0,y=0;
      if (dump_ps && request >> std::hex >> pc >> std::dec >> x >> y) {
        probe={dump_ps,pc,x,y};
        if (!probe_buf) probe_buf=[device newBufferWithLength:kProbeSlots*Shader::kProbeWords*4
            options:MTLResourceStorageModeShared];
        if (probe_buf) std::memset(probe_buf.contents,0,probe_buf.length);
      } else if (!std::getenv("DC_PROBE")) probe.shader=0;
      trace_frame = armed ? 0 : frame_count;
      if (armed) trace=false;
      DC_INFO("gpu: live trace %s for frame %llu",armed ? "armed" : "requested",frame_count);
    }
  }
  if (trace_frame) {
    trace = frame_count == trace_frame;
  }
  if (triggered_trace) trace=true;
  if (stats) {
    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - stat_t0).count();
    static u64 frames = 0;
    ++frames;
    if (dt >= 1.0) {
      DC_INFO(
          "gpu stats: %.1f fps, gpu busy %.0f%%, wait %.0f%%, per frame: %.0f "
          "draws %.0f dispatches %.1f commits %.1f waits %.1f MB hashed %.1f depth copies %.1f upload allocs, "
          "%zu pipelines compiling",
          frames / dt, gpu_ns.exchange(0) / 1e9 / dt * 100,
          stat_wait_s / dt * 100, stat_draws / double(frames),
          stat_dispatches / double(frames), stat_commits / double(frames),
          stat_waits / double(frames),
          stat_hashed / double(frames) / (1 << 20),
          stat_depth_copies / double(frames),
          (upload_allocations.load(std::memory_order_relaxed)-stat_upload_base) / double(frames),
          pending.size());
      if(texture_hash_profile_enabled) {
        DC_INFO("gpu texture hash workers: requested=%u invalid-scans=%llu (cumulative; synchronous CPU reads only)",
            parallel_texture_hash_workers,parallel_texture_hash_invalid);
        for(unsigned k=0;k<texture_hash_histogram.bins.size();++k) {
          const auto& bin=texture_hash_histogram.bins[k];
          if(!bin.checks)continue;
          DC_INFO("gpu texture hash size: bin=%s checks=%llu full=%llu eligible=%llu parallel=%llu changed=%llu actual-bytes=%llu (cumulative; CPU checks only)",
              ParallelChunkHash::Histogram::Names[k],bin.checks,bin.full,bin.eligible,
              bin.parallel,bin.changed,bin.bytes);
        }
      }
      const auto warm=compiler->GetWarmupStats();
      if(constant_memory_map_enabled)DC_INFO("gpu constant map: %llu source variants, %llu unchanged fallbacks (cumulative; immutable mapping metadata only)",
          constant_memory_map_sources,constant_memory_map_fallbacks);
      if(command_budget_us)DC_INFO("gpu command budget: %u us, %llu elapsed-time commits at natural encoder boundaries (cumulative)",
          command_budget_us,command_budget_commits);
      DC_INFO("gpu prewarm: %llu recipes loaded, %llu ready, %llu pipeline hits",
          warm.loaded,warm.ready,warm.hits);
      if(temporal) DC_INFO("gpu temporal: %llu resolved frames, %llu native velocity inputs",
          temporal->ResolvedFrames(),temporal->NativeMotionFrames());
      if(NativeVelocityEnabled()) DC_INFO("gpu velocity: %llu clears, %llu producers, %llu camera matches, %llu filters (cumulative)",
          velocity_clears,velocity_producers,velocity_camera_matches,velocity_filters);
      DC_INFO("gpu tessellation: %llu native draws, %llu translated preparation draws (cumulative)",
          native_tess_draws,translated_tess_draws);
      if(static_smrd_enabled) DC_INFO("gpu static SMRD: %llu attempts, %llu packed draws, %llu snapshot rejects, %llu bytes (cumulative)",
          static_smrd_attempts,static_smrd_draws,static_smrd_rejects,static_smrd_bytes);
      if(texture_uploader) DC_INFO("gpu texture upload: %llu versions, %llu bytes (cumulative)",
          gpu_texture_uploads,gpu_texture_upload_bytes);
      if(texture_uploader) {
        const auto upload=texture_uploader->GetStats();
        DC_INFO("gpu upload work: %llu uploads, %llu jobs, %llu dispatches, %llu packed uploads, %llu mapped snapshots/%llu bytes, %llu mach snapshots/%llu bytes (cumulative)",
            static_cast<unsigned long long>(upload.uploads),static_cast<unsigned long long>(upload.jobs),
            static_cast<unsigned long long>(upload.compute_dispatches),static_cast<unsigned long long>(upload.packed_uploads),
            static_cast<unsigned long long>(upload.mapped_snapshots),static_cast<unsigned long long>(upload.mapped_bytes),
            static_cast<unsigned long long>(upload.mach_snapshots),static_cast<unsigned long long>(upload.mach_bytes));
      }
      if(native_vertex_fetch_enabled) DC_INFO("gpu native vertex fetch: %llu guarded candidate draws (cumulative)",
          native_vertex_fetch_draws);
      if(native_vertex_fetch_extended_enabled) DC_INFO("gpu extended vertex fetch: %llu guarded candidate draws (cumulative)",
          native_vertex_fetch_extended_draws);
      if(bound_vertex_fetch_audit) {
        DC_INFO("gpu bound vertex audit: %llu candidates, %llu complete dump sources ready, %llu metadata ready; NO GPU bindings or leases (cumulative)",
            bound_vertex_candidates,bound_vertex_source_ready,bound_vertex_metadata_ready);
        DC_INFO("gpu bound vertex audit rejects: context=%llu shape=%llu guard=%llu dump-missing=%llu dump-bounds=%llu dump-pending=%llu map-changed=%llu dump-changed=%llu snapshot=%llu payload-map=%llu payload-cp=%llu payload-callback=%llu lease=%llu (cumulative)",
            bound_vertex_rejects[0],bound_vertex_rejects[1],bound_vertex_rejects[2],bound_vertex_rejects[3],
            bound_vertex_rejects[4],bound_vertex_rejects[5],bound_vertex_rejects[6],bound_vertex_rejects[7],
            bound_vertex_rejects[8],bound_vertex_rejects[9],bound_vertex_rejects[10],bound_vertex_rejects[11],bound_vertex_rejects[12]);
        DC_INFO("gpu bound vertex snapshot rejects: layout=%llu root=%llu ownership=%llu ambiguous=%llu flags=%llu format=%llu empty=%llu base=%llu overflow=%llu (cumulative; source-owner failures also use snapshot total)",
            bound_vertex_snapshot_rejects[1],bound_vertex_snapshot_rejects[2],bound_vertex_snapshot_rejects[3],
            bound_vertex_snapshot_rejects[4],bound_vertex_snapshot_rejects[5],bound_vertex_snapshot_rejects[6],
            bound_vertex_snapshot_rejects[7],bound_vertex_snapshot_rejects[8],bound_vertex_snapshot_rejects[9]);
      }
      if(readonly_guest_memory_enabled) DC_INFO("gpu readonly guest: %llu qualified vertex bindings, %llu qualified fragment bindings (cumulative)",
          readonly_guest_vs_bindings,readonly_guest_ps_bindings);
      if(native_indirect_enabled) DC_INFO("gpu indirect: %llu native draws, %llu native dispatches, %llu CP write waits, %zu pending reads (cumulative)",
          native_indirect_draws,native_indirect_dispatches,indirect_cpu_write_waits,indirect_reads.size());
      if(native_indirect_enabled) DC_INFO("gpu indirect resources: %llu rejected admissions, %llu unknown writer tickets, %zu pending writers (cumulative)",
          indirect_resource_rejects,unknown_guest_writers,guest_writers.size());
      if(writer_prune.Enabled()) {
        const auto& prune=writer_prune.Statistics();
        DC_INFO("gpu writer prune epoch: calls=%llu scans=%llu skipped=%llu examined=%llu removed=%llu published=%llu completion-epoch=%llu conditional-checks=%llu completed-cpu-ignored=%llu completed-unknown-ignored=%llu completed-witness-ignored=%llu (cumulative; compaction only)",
            prune.calls,prune.scans,prune.skipped,prune.examined,prune.removed,prune.published,
            writer_prune.CompletionEpoch(),prune.conditional_checks,prune.completed_ignored[0],
            prune.completed_ignored[1],prune.completed_ignored[2]);
      }
      if(gpu_written_texture_snapshot)DC_INFO("gpu texture snapshots: %llu GPU-time versions, %llu recovered historical callback uncertainty; source probes rejected [context=%llu missing-guard=%llu cp-overlap=%llu callback-queued-unknown=%llu callback-active-unknown=%llu lease=%llu mapping=%llu unknown-gpu-writer=%llu no-overlap=%llu callback-queued-overlap=%llu callback-active-overlap=%llu callback-queued-remap=%llu callback-active-remap=%llu] (cumulative)",
          texture_gpu_snapshots,texture_gpu_snapshot_history_versions,texture_gpu_snapshot_rejects[0],texture_gpu_snapshot_rejects[1],
          texture_gpu_snapshot_rejects[2],texture_gpu_snapshot_rejects[3],texture_gpu_snapshot_rejects[4],
          texture_gpu_snapshot_rejects[5],texture_gpu_snapshot_rejects[6],texture_gpu_snapshot_rejects[7],texture_gpu_snapshot_rejects[8],
          texture_gpu_snapshot_rejects[9],texture_gpu_snapshot_rejects[10],texture_gpu_snapshot_rejects[11],texture_gpu_snapshot_rejects[12]);
      if(gpu_written_texture_snapshot) {
        std::array<u64,3> enqueued{},queued{},active{};
        {
          std::scoped_lock lk(signals->lock);enqueued=signals->enqueued_effects;
          for(const auto& callback:signals->queue)++queued[static_cast<std::size_t>(callback.effect.kind)];
          if(signals->active_effect)++active[static_cast<std::size_t>(signals->active_effect->kind)];
        }
        DC_INFO("gpu callback effects: enqueued [unknown=%llu independent=%llu physical=%llu]; queued [%llu,%llu,%llu] active [%llu,%llu,%llu] (cumulative enqueues)",
            enqueued[0],enqueued[1],enqueued[2],queued[0],queued[1],queued[2],active[0],active[1],active[2]);
      }
      if(native_texture_coherence) DC_INFO("gpu texture coherence: %llu checks, %llu full refresh checks, %llu source waits, %zu retained ranges (cumulative)",
          texture_coherence_checks,texture_coherence_refreshes,texture_coherence_waits,texture_write_journal.Size());
      if(native_texture_coherence) {
        const auto journal=texture_write_journal.GetStats();
        DC_INFO("gpu texture journal: capacity=%zu retained=%zu evictions=%llu queries=%llu queried-source-bytes=%llu forgotten-queries=%llu forgotten-source-bytes=%llu examined-records=%llu (cumulative; supplied physical spans, not hash bytes)",
            texture_write_journal.Capacity(),texture_write_journal.Size(),journal.evicted_records,
            journal.queries,journal.queried_source_bytes,journal.forgotten_queries,
            journal.forgotten_source_bytes,journal.examined_records);
        DC_INFO("gpu texture journal pages: enabled=%u extent=%llu bytes=%zu queries=%llu checks=%llu updates=%llu allocation-failures=%llu (cumulative; 16KiB physical pages)",
            unsigned(texture_write_journal.PageIndexActive()),texture_write_journal.PageIndexExtent(),
            texture_write_journal.PageIndexBytes(),journal.indexed_queries,journal.page_checks,
            journal.page_updates,journal.index_allocation_failures);
      }
      if(native_worklist_extent) DC_INFO("gpu worklist extents: %llu refined, %llu conservative fallbacks "
          "[context=%llu layout=%llu input=%llu overflow=%llu output=%llu self=%llu capacity=%llu] (cumulative)",
          native_worklist_refined,native_worklist_fallbacks,native_worklist_fallback_reason[0],
          native_worklist_fallback_reason[1],native_worklist_fallback_reason[2],native_worklist_fallback_reason[3],
          native_worklist_fallback_reason[4],native_worklist_fallback_reason[5],native_worklist_fallback_reason[6]);
      if(direct_vertex_root_enabled) DC_INFO("gpu direct VS root: %llu draws, %llu proved roots, %llu sentinel roots; address metadata only (cumulative)",
          direct_vertex_root_draws,direct_vertex_root_hits,direct_vertex_root_fallbacks);
      if(direct_vertex_root_enabled)for(unsigned reason=1;reason<direct_vertex_root_rejects.size();++reason)
        if(direct_vertex_root_rejects[reason])DC_INFO("gpu direct VS root sentinel: %s=%llu (cumulative)",
            DirectRootRead::Names[reason],direct_vertex_root_rejects[reason]);
      if(direct_smrd_enabled) DC_INFO("gpu direct SMRD: %llu draws, %llu translated roots, %llu fallback roots (cumulative)",
          direct_smrd_draws,direct_smrd_roots,direct_smrd_fallbacks);
      if(direct_smrd_enabled) {
        DC_INFO("gpu direct SMRD proof: admitted=%llu untranslated=%llu layout=%llu context=%llu stage-writes=%llu missing-guard=%llu generation=%llu mapping=%llu physical-key=%llu cp-pending=%llu callback=%llu unknown-gpu=%llu current-command-gpu=%llu lease=%llu (cumulative)",
            direct_smrd_root_results[0],direct_smrd_root_results[1],direct_smrd_root_results[2],direct_smrd_root_results[3],
            direct_smrd_root_results[4],direct_smrd_root_results[5],direct_smrd_root_results[6],direct_smrd_root_results[7],
            direct_smrd_root_results[8],direct_smrd_root_results[9],direct_smrd_root_results[10],direct_smrd_root_results[11],
            direct_smrd_root_results[12],direct_smrd_root_results[13]);
        DC_INFO("gpu direct SMRD callback rejects: queued-unknown=%llu active-unknown=%llu queued-overlap=%llu active-overlap=%llu queued-remap=%llu active-remap=%llu (cumulative)",
            direct_smrd_callback_rejects[3],direct_smrd_callback_rejects[4],direct_smrd_callback_rejects[9],
            direct_smrd_callback_rejects[10],direct_smrd_callback_rejects[11],direct_smrd_callback_rejects[12]);
      }
      if(binding_cache.enabled) DC_INFO("gpu binding cache: buffers %llu/%llu, textures %llu/%llu, samplers %llu/%llu APIs skipped/intended (cumulative)",
          binding_cache.buffer_counts.skipped,binding_cache.buffer_counts.intended,
          binding_cache.texture_counts.skipped,binding_cache.texture_counts.intended,
          binding_cache.sampler_counts.skipped,binding_cache.sampler_counts.intended);
      stat_upload_base=upload_allocations.load(std::memory_order_relaxed);
      stat_hashed = 0;
      stat_depth_copies = 0;
      frames = stat_commits = stat_waits = stat_draws = stat_dispatches = 0;
      if (profile) {
        ProfileReport();
      }
      stat_wait_s = 0;
      stat_t0 = now;
    }
  }
  EndPass();
  EnsureCommandBuffer();

  __strong id<MTLTexture> &dst = frames[frame_index];
  if (!dst || dst.width != f.width || dst.height != f.height) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:f.width
                                    height:f.height
                                 mipmapped:NO];
    d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    d.storageMode = MTLStorageModePrivate;
    dst = [device newTextureWithDescriptor:d];
  }

  id<MTLTexture> src = nil;
  float scale[2] = {1.0f, 1.0f};
  RenderTarget *rt = Latest(color_targets, f.address);
  if (rt && rt->tex) {
    id<MTLTexture> t = rt->tex;
    // Scan-out shows the stored bytes: sample sRGB targets without decoding.
    if (t.pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB) {
      t = [t newTextureViewWithPixelFormat:MTLPixelFormatRGBA8Unorm];
    } else if (t.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB) {
      t = [t newTextureViewWithPixelFormat:MTLPixelFormatBGRA8Unorm];
    }
    src = t;
    scale[0] = static_cast<float>(f.width) / static_cast<float>(rt->width);
    scale[1] = static_cast<float>(f.height) / static_cast<float>(rt->height);
  } else if (Readable(f.address, u64(f.pitch) * f.height * 4)) {
    // Nothing rendered there on the GPU: the CPU wrote the image.
    MTLTextureDescriptor *d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                     width:f.width
                                    height:f.height
                                 mipmapped:NO];
    d.storageMode = MTLStorageModeManaged;
    d.usage = MTLTextureUsageShaderRead;
    src = [device newTextureWithDescriptor:d];
    [src replaceRegion:MTLRegionMake2D(0, 0, f.width, f.height)
           mipmapLevel:0
             withBytes:reinterpret_cast<const void *>(f.address)
           bytesPerRow:f.pitch * 4];
  }
  if (src && copy_pipeline) {
    MTLRenderPassDescriptor *rp =
        [MTLRenderPassDescriptor renderPassDescriptor];
    rp.colorAttachments[0].texture = dst;
    rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTLRenderCommandEncoder> e = [cmd renderCommandEncoderWithDescriptor:rp];
    [e setRenderPipelineState:copy_pipeline];
    [e setFragmentTexture:src atIndex:0];
    [e setFragmentBytes:scale length:sizeof(scale) atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [e endEncoding];
  }
  if (debug_vs_buf) {
    Commit(true);
    EnsureCommandBuffer();
    const float *d = static_cast<const float *>(debug_vs_buf.contents);
    for (u32 v = 0; v < 8; ++v) {
      DC_INFO("gpu: vs out %u: raw (%g %g %g %g) -> clip (%g %g %g %g)", v,
              d[v * 8], d[v * 8 + 1], d[v * 8 + 2], d[v * 8 + 3], d[v * 8 + 4],
              d[v * 8 + 5], d[v * 8 + 6], d[v * 8 + 7]);
    }
    debug_vsout = 0;
  }
  if (src && dump_frames && (dump_every ? frame_count % dump_every == 0 : trace)) {
    DumpFrame(dst);
  }
  if (!pass_dumps.empty()) {
    Commit(true);
    EnsureCommandBuffer();
    WritePassDumps();
  }
  if (probe_buf && !probe_slots.empty()) {
    Commit(true);
    EnsureCommandBuffer();
    PrintProbes();
  }
  if (!peek_addrs.empty()) {
    Commit(true);
    EnsureCommandBuffer();
    for (u64 a : peek_addrs) {
      if (Readable(a, 32)) {
        const float *v = reinterpret_cast<const float *>(a);
        DC_INFO("gpu: peek %#llx: %g %g %g %g %g %g %g %g", a, v[0], v[1], v[2],
                v[3], v[4], v[5], v[6], v[7]);
      }
    }
    peek_addrs.clear();
  }
  if (const char *peek = std::getenv("DC_PEEK");
      peek && trace) { // debug: DC_PEEK=addr,addr: guest floats
    Commit(true);
    EnsureCommandBuffer();
    for (const char *q = peek; *q;) {
      char *end = nullptr;
      const u64 a = std::strtoull(q, &end, 16);
      if (Readable(a, 32)) {
        const float *v = reinterpret_cast<const float *>(a);
        DC_INFO("gpu: peek %#llx: %g %g %g %g %g %g %g %g", a, v[0], v[1], v[2],
                v[3], v[4], v[5], v[6], v[7]);
      }
      q = *end ? end + 1 : end;
    }
  }
  if(world_frame_limit) {
    EnsureCommandBuffer();
    const auto ticket=world_frames.Track();
    if(!ticket) {execution->Fail("Metal world frame admission exceeded its bounded ticket capacity.");return;}
    const auto state=execution;
    [cmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
      WorldFrameQueue::Complete(ticket,state,completed.status==MTLCommandBufferStatusCompleted);
    }];
  }
  if(frame_generation_enabled.load(std::memory_order_relaxed) && frame_generation && rt && src) {
    auto packet=frame_generation->Finish((__bridge void*)cmd,(__bridge void*)rt->tex,(__bridge void*)dst,rt->written,frame_count-1);
    if(!packet)packet=frame_generation->RealFrame((__bridge void*)cmd,(__bridge void*)dst,frame_count-1);
    std::atomic_store_explicit(&generated_frame,std::move(packet),std::memory_order_release);
    frame_generation_status.store(frame_generation->Status(),std::memory_order_relaxed);
    if(frame_count%300==0)frame_generation->Report();
  } else {
    std::atomic_store_explicit(&generated_frame,std::shared_ptr<const GeneratedFrame>{},std::memory_order_release);
    if(frame_generation)frame_generation->Invalidate();
    frame_generation_status.store("Off",std::memory_order_relaxed);
  }
  Commit(false);
  if (triggered_trace) {
    triggered_trace=false;
    dump_selected_outputs=false;
    trace=false;
  }
  if (src) {
    latest_frame.store((__bridge void *)dst, std::memory_order_release);
    flipped_frames.fetch_add(1, std::memory_order_relaxed);
    if (frame_ready) frame_ready((__bridge void*)dst);
    frame_index = (frame_index + 1) % 3;
  }
  if(world_frame_limit) {
    StutterTrace::Scope timing("world_frame_admission_wait",frame_count,world_frames.Outstanding(),world_frame_limit,1);
    const auto result=world_frames.WaitUntilOutstandingBelow(world_frame_limit,execution,
        std::chrono::steady_clock::now()+std::chrono::seconds(10));
    if(result!=WorldFrameQueue::Result::Ready)
      execution->Fail(result==WorldFrameQueue::Result::TimedOut ?
          "Metal world frame admission timed out." : "Metal world frame failed before producer admission.");
  }
  if (frame_count <= 3 || frame_count % 300 == 0) {
    DC_INFO("gpu: flip #%llu %#llx %ux%u (%s), %llu draws so far", frame_count,
            f.address, f.width, f.height, rt ? "gpu target" : "cpu memory",
            draw_count);
  }
}

MetalRenderer::MetalRenderer(Device &device) : impl(std::make_unique<Impl>()) {
  if(ContentHash::NativeSimd())DC_INFO("gpu texture validation: native NEON XXH3 (complete byte coverage)");
  impl->execution=Execution();
  impl->device = (__bridge id<MTLDevice>)device.Handle();
  impl->queue = (__bridge id<MTLCommandQueue>)device.Queue();
  impl->Init();
}

MetalRenderer::~MetalRenderer() {
  std::scoped_lock lk(impl->lock);
  impl->Commit(true);
  // Event-listener blocks can run after command-buffer completion handlers.
  // Their shared state stays alive, but they must never call through Impl.
  std::lock_guard signals(impl->signals->lock);
  impl->signals->closed=true;impl->signals->queue.clear();
}

bool MetalRenderer::DrawIndirect(const RegFile& regs,const DrawCall& draw,GuestAddr address,
    const std::function<bool(GuestAddr,std::size_t)>& ready) {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  const auto predicate=ready ? ready : impl->CurrentCpuReadReady();
  Impl::ScopedCpuReadReady scoped_ready(*impl,predicate);
  if(Execution()->Failed() || (predicate && !predicate(address,16))) return false;
  impl->last_vs=regs.ProgramAddr(Reg::SPI_SHADER_PGM_LO_VS);
  impl->last_ps=regs.ProgramAddr(Reg::SPI_SHADER_PGM_LO_PS);
  StutterTrace::Scope timing("draw_indirect_record",impl->frame_count,address,impl->last_vs);
  @autoreleasepool {return impl->DrawIndirect(regs,draw,address,predicate);}
}

bool MetalRenderer::DispatchIndirect(const RegFile& regs,GuestAddr address,
    const std::function<bool(GuestAddr,std::size_t)>& ready) {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  const auto predicate=ready ? ready : impl->CurrentCpuReadReady();
  Impl::ScopedCpuReadReady scoped_ready(*impl,predicate);
  if(Execution()->Failed() || (predicate && !predicate(address,12))) return false;
  impl->last_cs=regs.ProgramAddr(Reg::COMPUTE_PGM_LO);
  StutterTrace::Scope timing("dispatch_indirect_record",impl->frame_count,address,impl->last_cs);
  @autoreleasepool {return impl->DispatchIndirect(regs,address,predicate);}
}

void MetalRenderer::CpuWrite(GuestAddr address,const void* source,std::size_t bytes,bool constants) {
  std::scoped_lock lk(impl->lock);
  if(Execution()->Failed() || !impl->ProtectIndirectWrite(address,bytes)) return;
  std::memcpy(reinterpret_cast<void*>(address),source,bytes);
  impl->NoteTextureCpuWrite(address,bytes);
  if(constants) impl->NoteConstDump(address,static_cast<u32>(bytes));
}
void MetalRenderer::SetCpuReadGuard(std::function<bool(GuestAddr,std::size_t)> ready) {
  std::scoped_lock lk(impl->lock);impl->cp_cpu_read_ready=std::move(ready);
}

bool MetalRenderer::BeginGuestMemoryMutation() {
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(!impl->lock.try_lock()) {
    if(std::chrono::steady_clock::now()>=deadline) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if(!impl->ProtectIndirectWrite(0,UINT64_MAX)) {impl->lock.unlock();return false;}
  return true;
}
void MetalRenderer::EndGuestMemoryMutation() {impl->lock.unlock();}

std::uint64_t MetalRenderer::NativeIndirectDrawCount() const {
  std::scoped_lock lk(impl->lock);return impl->native_indirect_draws;
}
std::uint64_t MetalRenderer::NativeIndirectDispatchCount() const {
  std::scoped_lock lk(impl->lock);return impl->native_indirect_dispatches;
}
std::uint64_t MetalRenderer::IndirectCpuWriteWaitCount() const {
  std::scoped_lock lk(impl->lock);return impl->indirect_cpu_write_waits;
}
std::uint64_t MetalRenderer::DirectSmrdRootProofCount(std::uint32_t result) const {
  std::scoped_lock lk(impl->lock);
  return result<impl->direct_smrd_root_results.size() ? impl->direct_smrd_root_results[result] : 0;
}
std::uint64_t MetalRenderer::DirectVertexRootProofCount(std::uint32_t result) const {
  std::scoped_lock lk(impl->lock);
  return result<impl->direct_vertex_root_rejects.size() ? impl->direct_vertex_root_rejects[result] : 0;
}
std::uint64_t MetalRenderer::TextureGpuSnapshotCount() const {
  std::scoped_lock lk(impl->lock);return impl->texture_gpu_snapshots;
}
std::uint64_t MetalRenderer::TextureCoherenceWaitCount() const {
  std::scoped_lock lk(impl->lock);return impl->texture_coherence_waits;
}
std::uint64_t MetalRenderer::TextureCoherenceRefreshCount() const {
  std::scoped_lock lk(impl->lock);return impl->texture_coherence_refreshes;
}
std::uint64_t MetalRenderer::ParallelTextureHashCheckCount() const {
  std::scoped_lock lk(impl->lock);std::uint64_t checks=0;
  for(const auto& bin:impl->texture_hash_histogram.bins)checks+=bin.parallel;
  return checks;
}

void MetalRenderer::Draw(const RegFile &regs, const DrawCall &draw) {
  const auto lock_start=FrameWork::Enabled()?StutterTrace::Now():0;
  std::scoped_lock lk(impl->lock);
  if(lock_start) {const auto end=StutterTrace::Now();if(end-lock_start>.0001)FrameWork::Span(impl->frame_count,"renderer_lock",lock_start,end,0);}
  FrameWork::Bind frame_work(impl->frame_work);
  if(Execution()->Failed()) return;
  impl->last_vs=regs.ProgramAddr(Liverpool::Reg::SPI_SHADER_PGM_LO_VS);
  impl->last_ps=regs.ProgramAddr(Liverpool::Reg::SPI_SHADER_PGM_LO_PS);
  StutterTrace::Scope timing("draw_record",impl->frame_count,impl->last_vs,impl->last_ps);
  double pool_retire_start=0;
  @autoreleasepool {
    {
      StutterTrace::Scope work("draw_work",impl->frame_count,impl->last_vs,impl->last_ps);
      impl->Draw(regs, draw);
    }
    if (impl->Profiling() && !impl->profile_passes) {
      char what[64];
      std::snprintf(what, sizeof(what), "vs %#llx ps %#llx",
                    regs.ProgramAddr(Liverpool::Reg::SPI_SHADER_PGM_LO_VS),
                    regs.ProgramAddr(Liverpool::Reg::SPI_SHADER_PGM_LO_PS));
      impl->ProfileCut(what);
    }
    if(StutterTrace::Output()) pool_retire_start=StutterTrace::Now();
  }
  if(pool_retire_start) StutterTrace::Record("draw_autorelease",pool_retire_start,
      StutterTrace::Now(),impl->frame_count,impl->last_vs,impl->last_ps,1);
}

void MetalRenderer::Dispatch(const RegFile &regs, std::uint32_t x,
                             std::uint32_t y, std::uint32_t z) {
  const auto lock_start=FrameWork::Enabled()?StutterTrace::Now():0;
  std::scoped_lock lk(impl->lock);
  if(lock_start) {const auto end=StutterTrace::Now();if(end-lock_start>.0001)FrameWork::Span(impl->frame_count,"renderer_lock",lock_start,end,1);}
  FrameWork::Bind frame_work(impl->frame_work);
  if(Execution()->Failed()) return;
  impl->last_cs=regs.ProgramAddr(Liverpool::Reg::COMPUTE_PGM_LO);
  StutterTrace::Scope timing("dispatch_record",impl->frame_count,impl->last_cs);
  @autoreleasepool {
    impl->Dispatch(regs, x, y, z);
    if (impl->Profiling()) {
      char what[64];
      std::snprintf(what, sizeof(what), "cs %#llx %ux%ux%u",
                    regs.ProgramAddr(Liverpool::Reg::COMPUTE_PGM_LO), x, y, z);
      impl->ProfileCut(what);
    }
  }
}

void MetalRenderer::OnSubmit() {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  std::optional<StutterTrace::Scope> timing;
  if(FrameWork::Enabled())timing.emplace("submit_record",impl->frame_count);
  // DC_SERIAL: wait for the GPU after every submit (debug: rules out CPU/GPU
  // races on guest memory).
  static const bool serial = std::getenv("DC_SERIAL") != nullptr;
  @autoreleasepool {
    impl->Commit(serial);
  }
}

void MetalRenderer::WaitIdle() {
  std::scoped_lock lk(impl->lock);
  @autoreleasepool {
    impl->Commit(true);
  }
}
bool MetalRenderer::WaitIdleUntil(std::chrono::steady_clock::time_point deadline,std::string& error) {
  if(Execution()->Failed()){error=Execution()->Error();return false;}
  // Do not block behind a Flip whose presentation has not been acknowledged.
  // Used by both checkpoint workers and CPU consumers of GPU-produced data.
  std::unique_lock<std::recursive_mutex> lock(impl->lock,std::defer_lock);
  while (!lock.try_lock()) {
    if (std::chrono::steady_clock::now()>=deadline) {error="Renderer did not reach a GPU drain boundary.";return false;}
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  @autoreleasepool {
    impl->Commit(false);
    id<MTLCommandBuffer> command=impl->last_committed;
    while (command && command.status!=MTLCommandBufferStatusCompleted) {
      if (command.status==MTLCommandBufferStatusError) {
        error=std::string(command.label.UTF8String ?: "Metal submission")+": "+
            (command.error.localizedDescription.UTF8String ?: "command execution failed");return false;
      }
      if (std::chrono::steady_clock::now()>=deadline) {error="Metal work did not complete before the GPU drain timeout.";return false;}
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(Execution()->Failed()){error=Execution()->Error();return false;}
    std::unique_lock signals(impl->signals->lock);
    if (!impl->signals->changed.wait_until(signals,deadline,[&] {
      return Execution()->Failed() || (impl->signals->queue.empty() && !impl->signals->active);
    })) {error="GPU fence callbacks did not finish before the GPU drain timeout.";return false;}
    if(Execution()->Failed()){error=Execution()->Error();return false;}
    impl->last_committed=nil;
    impl->arena_commit_head=impl->arena_head;
    SnapshotArena::Publish(impl->arena_released,impl->arena_head);
    error.clear();return true;
  }
}

#include "gpu/metal/renderer_checkpoint.inc"

void MetalRenderer::Signal(std::function<void()> done) {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  if(Execution()->Failed()) return;
  @autoreleasepool {
    impl->Signal(std::move(done));
  }
}
void MetalRenderer::NotePendingCpuWrite(GuestAddr address,std::size_t bytes) {
  std::scoped_lock lk(impl->lock);
  impl->NoteTextureCpuWrite(address,bytes,true);
}
void MetalRenderer::SignalCpuWrite(GuestAddr address,std::size_t bytes,std::function<void()> done) {
  std::scoped_lock lk(impl->lock);
  if(Execution()->Failed())return;
  @autoreleasepool {
    // One critical section: no other recording ring can observe a journal
    // marker whose callback has not yet been enqueued on the signal worker.
    impl->NoteTextureCpuWrite(address,bytes,true);
    impl->Signal(std::move(done));
  }
}
void MetalRenderer::SignalNoDirectMemoryWrite(std::function<void()> done) {
  std::scoped_lock lk(impl->lock);
  if(Execution()->Failed())return;
  @autoreleasepool {
    Impl::SignalEffect effect;effect.kind=Impl::SignalEffect::Kind::NoDirectMemoryWrite;
    impl->SignalWithEffect(std::move(done),std::move(effect));
  }
}
void MetalRenderer::SignalCpuWriteOnly(GuestAddr address,std::size_t bytes,std::function<void()> done) {
  std::scoped_lock lk(impl->lock);
  if(Execution()->Failed())return;
  @autoreleasepool {
    auto effect=impl->CpuWriteSignalEffect(address,bytes);
    // The precise content marker and callback effect are published under one
    // admission lock. Unresolved/non-direct destinations stay unknown.
    impl->NoteTextureCpuWrite(address,bytes,true);
    impl->SignalWithEffect(std::move(done),std::move(effect));
  }
}

void MetalRenderer::Flip(const FlipInfo &flip) {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  if(Execution()->Failed()) return;
  const auto begin=FrameWork::Enabled()?StutterTrace::Now():0;
  const auto frame=impl->frame_count;
  if(begin)FrameWork::End(impl->frame_work,frame,begin);
  @autoreleasepool {
    impl->Flip(flip);
  }
  if(begin) {
    const auto end=StutterTrace::Now();
    FrameWork::Span(frame,"flip",begin,end);
    FrameWork::Begin(impl->frame_work,impl->frame_count,end);
  }
}

bool MetalRenderer::DmaData(std::uint32_t dst_sel, std::uint64_t dst,
                            std::uint32_t src_sel, std::uint64_t src,
                            std::uint32_t bytes) {
  std::scoped_lock lk(impl->lock);
  FrameWork::Bind frame_work(impl->frame_work);
  std::optional<StutterTrace::Scope> timing;
  if(FrameWork::Enabled())timing.emplace("dma_record",impl->frame_count);
  @autoreleasepool {
    return impl->DmaData(dst_sel, dst, src_sel, src, bytes);
  }
}

void MetalRenderer::OnContextRegs(const std::uint32_t *body,
                                  std::uint32_t body_dwords,std::size_t available_words) {
  std::scoped_lock lk(impl->lock);
  impl->OnContextRegs(body, body_dwords,available_words);
}

void MetalRenderer::NoteConstDump(std::uint64_t addr, std::uint32_t bytes) {
  std::scoped_lock lk(impl->lock);
  impl->NoteConstDump(addr, bytes);
}

std::uint8_t *MetalRenderer::Gds() {
  return static_cast<std::uint8_t *>(impl->gds.contents);
}

void *MetalRenderer::LatestFrame() const {
  return impl->latest_frame.load(std::memory_order_acquire);
}

std::uint64_t MetalRenderer::FrameCount() const {
    return impl->flipped_frames.load(std::memory_order_relaxed);
}

std::uint64_t MetalRenderer::DepthBoundsCopyCount() const {
    return impl->depth_bounds_copies.load(std::memory_order_relaxed);
}
std::uint64_t MetalRenderer::UploadBufferAllocationCount() const {
  return impl->upload_allocations.load(std::memory_order_relaxed);
}
std::uint64_t MetalRenderer::CommandBudgetCommitCount() const {
  std::scoped_lock lock(impl->lock);
  return impl->command_budget_commits;
}


void MetalRenderer::SetFrameReadyCallback(std::function<void(void*)> callback) {
    std::scoped_lock lock(impl->lock);
    impl->frame_ready=std::move(callback);
}

void MetalRenderer::SetGraphicsOptions(int anisotropy,bool temporal_aa) {
    impl->anisotropy.store(anisotropy==0 || anisotropy==2 || anisotropy==4 || anisotropy==8 || anisotropy==16
        ? unsigned(anisotropy) : 16u,std::memory_order_relaxed);
    impl->temporal_aa.store(temporal_aa,std::memory_order_relaxed);
}
bool MetalRenderer::TemporalAAActive() const { return impl->temporal_active.load(std::memory_order_relaxed); }
void MetalRenderer::SetFrameGeneration(bool enabled) {impl->frame_generation_enabled.store(enabled,std::memory_order_relaxed);}
std::shared_ptr<const GeneratedFrame> MetalRenderer::LatestGeneratedFrame() const {return std::atomic_load_explicit(&impl->generated_frame,std::memory_order_acquire);}
const char* MetalRenderer::FrameGenerationStatus() const {return impl->frame_generation_status.load(std::memory_order_relaxed);}

} // namespace Gpu::Metal
