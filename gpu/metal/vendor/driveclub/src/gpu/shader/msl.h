#pragma once

// GCN -> Metal Shading Language translation.
//
// Every GCN wave lane becomes one Metal invocation. Scalar and vector
// registers turn into local variables; 64-bit lane masks (EXEC, VCC, v_cmp
// results) are kept per lane as all-ones / all-zero words, so the bitwise
// scalar ops that combine masks work unchanged. Guest memory is reached
// through two Metal buffers that alias the whole direct-memory object, with a
// page table translating guest virtual addresses to offsets.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "common/types.h"
#include "gpu/shader/program.h"

namespace Gpu::Shader {

// Fixed resource bindings shared by every generated shader.
namespace Binding {
constexpr u32 UserData = 0; // constant uint[16]
constexpr u32 Mem0 = 1;     // device uint*: direct memory [0, split)
constexpr u32 Mem1 = 2;     // device uint*: direct memory [split, end)
constexpr u32 MemMap = 3;   // constant DcMap
constexpr u32 Draw = 4;     // constant DcDraw (vertex stage)
constexpr u32 Gds = 5;      // device uint[16K]: global data share (compute stage)
constexpr u32 TessLds = 8;
constexpr u32 TessFactors = 9;
constexpr u32 TessParams = 10;
constexpr u32 TessLocalUserData = 11;
constexpr u32 TessHullUserData = 12;
} // namespace Binding

// Host-side mirrors of the MSL structs in the prelude.
// Header of the page table bound at Binding::MemMap; `pages` s32 entries
// follow it, each the page's (phys - va) in 16 KB units, or kUnmapped.
struct GpuMemMap {
    static constexpr u32 kMinShift = 14;
    static constexpr u32 kMaxShift = 21;
    static constexpr s32 kUnmapped = INT32_MIN;
    u64 base;
    u64 pages;
    u64 split;
    u32 shift;
    u32 pad;
    u64 limit; // physical size
};
struct alignas(16) GpuDrawConsts {
    float vp_scale[4];  // combined clip-space XY scale and guest viewport Z scale
    float vp_offset[4]; // combined clip-space XY offset and guest viewport Z offset
    float rt_scale[2];  // reserved; kept for draw-constant layout stability
    u32 vte;            // PA_CL_VTE_CNTL
    u32 pad;
};

// Combine the viewport with the target transform once per draw. Computing
// (2*offset-size)/size avoids a cancellation after a rounded reciprocal; the
// common full-target viewport is exactly identity even with contracted math.
inline GpuDrawConsts MakeDrawConsts(u32 vte, const std::array<float,6>& vp,
                                    u32 width, u32 height) {
    GpuDrawConsts d{};
    d.vp_scale[0]=float(2.0*((vte&1) ? vp[0] : 1.0)/width);
    d.vp_offset[0]=float((2.0*((vte&2) ? vp[1] : 0.0)-width)/width);
    d.vp_scale[1]=float(-2.0*((vte&4) ? vp[2] : 1.0)/height);
    d.vp_offset[1]=float((height-2.0*((vte&8) ? vp[3] : 0.0))/height);
    d.vp_scale[2]=(vte&16) ? vp[4] : 1.0f;
    d.vp_offset[2]=(vte&32) ? vp[5] : 0.0f;
    d.vte=vte;
    return d;
}

// A separate LDS region per patch preserves the local/hull/domain ABI without
// relying on Metal's threadgroup layout matching the GCN tessellation hardware.
constexpr u32 kTessLdsWords = 16384;
struct GpuTessConsts {
    u32 base_vertex;
    u32 patch_count;
    u32 index_size = 0;
    u32 reserved = 0;
    u64 index_base = 0;
};
enum class TessStage : u8 { None, Local, Hull, Domain };

enum class TexDim : u8 { Tex2D, Tex2DArray, Tex3D };
enum class TexSample : u8 { Float, Uint, Sint };

struct TextureType {
    TexDim dim = TexDim::Tex2D;
    TexSample sample = TexSample::Float;
    u8 compare = 3; // depth-compare sampling: S# DEPTH_COMPARE_FUNC (ref <op> texel), default LEQUAL
    bool cube = false; // a cube (Tex2DArray of faces) sampled with V_CUBE* coordinates
    bool ufloat = false; // unsigned small-float storage (R11G11B10): writes clamp like the CB does
    bool native_depth = false; // comparison operations use Metal's depth filtering
    bool operator==(const TextureType&) const = default;
};

enum class OutType : u8 { Float, Uint, Sint };

constexpr u32 kProbeWords = 256;
constexpr u32 kProbeExportNan = 0xFFFFFFFEu; // probe_pc: at color exports that are NaN/Inf
constexpr u32 kProbeBinding = 7;
constexpr u32 kDepthBoundsBuffer = 6;
constexpr u32 kDepthBoundsTexture = 30;

struct MslOptions {
    TessStage tess_stage = TessStage::None;
    u32 native_tess_layout = 0; // checked Bloodborne LS layouts: 128 or 144
    u32 native_tess_factor = 0; // checked constant outer factors: 1 or 3
    // Vertex.
    bool rect_list = false; // expand RECTLIST: 3 vertices -> 2 triangles
    bool debug_capture = false; // write positions to buffer(6) (debugging)
    bool points = false;          // point list: VsOut carries [[point_size]]
    bool vtx_point_size = false;  // PA_CL_VS_OUT_CNTL.USE_VTX_POINT_SIZE: POS1.x is the size
    // Fragment.
    std::array<u32, 32> ps_input_cntl{}; // SPI_PS_INPUT_CNTL_n
    u32 vs_param_mask = 0;               // params the paired VS exports
    u32 ps_input_addr = 0;               // SPI_PS_INPUT_ADDR
    u32 col_format = 0;                  // SPI_SHADER_COL_FORMAT
    std::array<OutType, 8> mrt_type{};
    u32 mrt_ufloat = 0; // MRTs stored as unsigned small floats (R11G11B10)
    bool depth_target = true; // false drops depth exports (no depth attachment)
    // DB_DEPTH_CONTROL.DEPTH_BOUNDS_ENABLE: discard where the stored depth (a copy at
    // texture(kDepthBoundsTexture)) is outside [min, max] (buffer(kDepthBoundsBuffer)).
    bool depth_bounds = false;
    // Remove only translator-inserted kills proven unreachable by the fragment
    // CFG. This never forces early depth/stencil tests or removes depth bounds.
    bool opaque_exports = false;
    // Register probe (debugging): before the instruction at `probe_pc`, the
    // fragment at pixel (probe_x, probe_y) -- for compute, the thread at that
    // global grid position -- writes its
    // registers to buffer(7): [0] hit, [1] exec, [2] vcc, [3] m0, [8 + n] vN
    // (n < 96), [104 + n] sN (n < 104). kProbeWords per slot.
    u32 probe_pc = ~0u;
    u32 probe_x = 0, probe_y = 0;
    // Both.
    std::vector<TextureType> textures; // per Program::images slot
};

// Returns the MSL source; the entry point is "main0". Opcodes that could not
// be translated are appended to `unsupported`.
std::string GenerateMsl(const Program& p, const MslOptions& o, std::vector<std::string>* unsupported);

// Conservative proof for the translated per-lane EXEC model. Every reachable
// export must have full EXEC, and every reachable exit must follow an export.
// Unsupported control flow and loops retain the original fragment kills.
bool FragmentExportsOpaque(const Program& p);

} // namespace Gpu::Shader
