#pragma once

// A decoded guest shader plus what the backend needs to know about it:
// which user-data words feed its resource descriptors, what it exports,
// which PS inputs it reads.
//
// Descriptors (T#, S#) live in scalar registers that the shader fills from
// user data or loads from memory pointed to by user data. The host API has to
// bind textures before the draw, so each image instruction records where its
// descriptor words came from ("provenance"); the renderer evaluates that
// against the draw's user data to find the actual texture.

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "common/types.h"
#include "gpu/shader/gcn.h"

namespace Gpu::Shader {

enum class Stage : u8 { Vertex, Fragment, Compute };

struct SgprSource {
    enum Kind : u8 {
        Unknown,
        UserData,    // user SGPR `ud`
        PointerLoad, // dword at (64-bit pointer in user SGPRs ud, ud+1) + offset
        BufferLoad,  // dword at V#(user SGPRs ud..ud+3).base + offset
    };
    Kind kind = Unknown;
    u8 ud = 0;
    u32 offset = 0; // bytes

    bool operator==(const SgprSource&) const = default;
};

struct ImageResource {
    std::array<SgprSource, 8> tsharp{};
    std::array<SgprSource, 4> ssharp{};
    bool has_sampler = false;
    bool storage = false; // image_store / atomics
    bool reads = false;   // image_load / atomics: a storage image then needs read_write access
    bool compare = false; // depth-compare sampling
    u32 sampler_index = 0;
};

// Resource provenance for a read-capable buffer instruction. This describes
// the resource referenced by the instruction, not the dynamic addresses or
// active lanes reached at runtime. Keep it separate from Program::buffers,
// whose scalar-load list is used by camera discovery.
struct BufferReadResource {
    std::array<SgprSource, 4> descriptor{};
    u32 pc = 0;
    Gcn::Enc encoding = Gcn::Enc::Unknown;
    u16 op = 0;
    u32 immediate_offset = 0; // bytes; zero when the offset is dynamic
    u16 scalar_offset = 0;    // operand code, not its runtime value
    u8 vector_address = 0;
    bool dynamic_scalar_offset = false;
    bool index_enabled = false;
    bool offset_enabled = false;
    bool address64 = false;
    bool writes = false; // atomics read and write the same resource
    bool descriptor_written = false; // another instruction may store through it
    bool HasReadOnlyDescriptor() const {
        if (writes || descriptor_written) return false;
        for (const auto& source:descriptor)
            if (source.kind==SgprSource::Unknown) return false;
        return true;
    }
};

// Fetch-subroutine instructions get pcs in their own range so they never
// collide with branch targets of the main program.
constexpr u32 kFetchPcBase = 0x100000;

struct Program {
    Stage stage = Stage::Vertex;
    u64 address = 0;      // guest address of the main program
    u64 fetch_address = 0; // inlined fetch subroutine, if any
    int fetch_ud = -1;     // user-data index holding its address
    u32 fetch_bytes = 0;   // length of the fetch subroutine (with its S_SETPC)
    u64 hash = 0;         // of the code bytes (plus fetch code)
    u64 retail_id = 0;    // validated OrbShdr metadata, independent of the MSL cache hash
    u32 num_user_sgprs = 0;
    u32 vgpr_comp_cnt = 0; // VS: system VGPRs after the vertex index; CS: TIDIG_COMP_CNT
    u32 compute_rsrc2 = 0; // CS: COMPUTE_PGM_RSRC2 (system SGPR enables, LDS size)
    // bbport: a validated, unmodified 32-bit buffer-copy kernel. Descriptor
    // registers vary between Gnm implementations; never guess their slots.
    int copy_source_ud = -1, copy_destination_ud = -1;
    bool copy_uses_group_id = false;
    int fill_destination_ud = -1, fill_value_ud = -1;
    u32 fill_elements_per_group = 0;

    std::vector<Gcn::Inst> code;
    std::vector<u32> labels; // byte offsets that need a case label (sorted)
    bool has_branches = false;
    bool writes_exec = false;

    // Image instruction pc -> index into `images`.
    std::vector<std::pair<u32, u32>> image_slot_by_pc;
    std::vector<ImageResource> images;
    std::vector<std::array<SgprSource, 4>> samplers;
    std::vector<std::array<SgprSource, 4>> buffers;
    std::vector<BufferReadResource> buffer_reads;
    std::vector<std::array<SgprSource,4>> buffer_writes;

    // VS exports.
    u32 param_mask = 0;
    bool exports_position = false;
    // PS usage.
    u32 attr_mask = 0;      // PS attributes read by v_interp_*
    u32 attr_flat_mask = 0; // ... of which read through v_interp_mov
    u8 mrt_mask = 0;
    bool exports_depth = false;
    bool uses_quad_ops = false;

    std::vector<std::string> unsupported; // opcodes we could not translate

    u32 ImageSlot(u32 pc) const;
};

struct BuildInput {
    Stage stage;
    u64 address;
    const u32* user_data; // the stage's 16 user-data registers
    u32 num_user_sgprs;
    u32 vgpr_comp_cnt;
    const u32* code = nullptr; // tools: decode from here instead of guest memory
    u32 compute_rsrc2 = 0;
};

// Decodes and analyzes a program. Returns null if the code is unreadable.
std::unique_ptr<Program> BuildProgram(const BuildInput& in);

// Evaluates a descriptor dword at draw time.
u32 ResolveSgpr(const SgprSource& src, const u32* user_data);

// Conservative descriptor-range alias check. Unknown/unreadable writers may
// alias any range, so callers must not overwrite their metadata inputs.
bool MayWriteBuffer(const Program& program,const u32* user_data,u64 base,u64 bytes);

} // namespace Gpu::Shader
