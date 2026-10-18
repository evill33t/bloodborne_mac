// GX render-target descriptors and selected native PM4 packets (copied words, not live objects).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Names describe recovered layouts, not C++ RTTI. GPU addresses stay integers:
// render-target bases use 256-byte units; indirect packets use byte addresses.

inline constexpr Rva GNM_SET_COLOR_RENDER_TARGET{0x10738b0};
inline constexpr Rva GNM_SET_DEPTH_RENDER_TARGET{0x10739b0};
inline constexpr Rva GX_CREATE_COLOR_TARGET_VIEW{0x21667a0};
inline constexpr Rva GX_CREATE_DEPTH_TARGET_VIEW{0x2166610};
inline constexpr Rva GX_COLOR_TARGET_DESCRIPTOR_INITIALIZE{0x217a4b0};
inline constexpr Rva GX_DEPTH_TARGET_DESCRIPTOR_INITIALIZE{0x217a210};
inline constexpr Rva GX_BUILD_COLOR_TARGET_FROM_RESOURCE{0x216e1d0};
inline constexpr Rva GX_BUILD_DEPTH_TARGET_FROM_RESOURCE{0x216fa60};
inline constexpr Rva GNM_SET_NUM_INSTANCES{0x1075370};
inline constexpr Rva GNM_CHAIN_DRAW_INDIRECT_BUFFER{0x1078670};
inline constexpr Rva GNM_CHAIN_CONSTANT_INDIRECT_BUFFER{0x11fd850};
inline constexpr Rva GNM_COMPUTE_SURFACE_INFO{0x109ce10};
inline constexpr Rva GNM_BUILD_SURFACE_INPUT_FROM_TEXTURE{0x10a1e80};
inline constexpr Rva GNM_COMPUTE_TEXTURE_MIP_SLICE_OFFSET{0x10a2c40};
// Native SysV signature: u32(u64* size, u32* base_alignment, u32* padded_width,
// u32* padded_height, u32 width, u32 height, u32 slices, u8 mode_raw). Each
// output is optional. Mode zero aligns dimensions to 512, nonzero to 64; base
// alignment is independently 0x800 bytes. Dimensions wrap as u32. Size is
// (((u64(slices) * padded_height * padded_width) << 5) >> 9) + 0x3fff, wrapping
// u64, then AND 0x00ff_ffff_ffff_c000. Zero/oversized inputs are not validated.
// See the compiled C++ port in gnm_surface_layout.cpp.
inline constexpr Rva GNM_COMPUTE_HTILE_INFO{0x109e570};
// Uses descriptor padded width/height, slices=1, mode=HTILE_SURFACE bit 0;
// returns size's low32 without checking HTILE enable or allocation validity.
inline constexpr Rva GNM_GET_HTILE_SLICE_SIZE_BYTES{0x1072630};

// Four PM4 dwords, including the header. Not a native command buffer.
// RVA 0x1078670 emits header 0xc0023f00, address low32/high16 and
// (dwords & 0xfffff) | 0x01900000. Its CE counterpart at RVA 0x11fd850 uses
// opcode 0x33. Only length and chain are decoded from the control word.
struct GnmIndirectBufferPacket {
    std::uint32_t header;
    std::uint32_t base_lo;
    std::uint32_t base_hi_raw;
    std::uint32_t control_raw;

    // A 48-bit byte address, unlike the render descriptors' 256-byte units.
    // Validate alignment and readability before following it.
    constexpr std::uint64_t address() const {
        return static_cast<std::uint64_t>(base_lo) | (static_cast<std::uint64_t>(base_hi_raw & 0xffff) << 32);
    }
    constexpr std::uint32_t dword_count() const { return control_raw & 0xfffff; }
    // Bit 20 requests a tail chain; set by both native writers above.
    constexpr bool is_chain() const { return (control_raw & 0x100000) != 0; }
};

// RVA 0x1075370 emits 0xc0002f00 followed by the caller's unchanged u32.
// Raw zero is preserved; effective hardware draw-count semantics are separate.
struct GnmNumInstancesPacket {
    std::uint32_t header;
    std::uint32_t instances_raw;
};

// Complete 24-dword bound sequence at RVA 0x10739b0: five SET_CONTEXT_REG
// packets and one NOP carrying requested dimensions. The six-dword unbind is a
// different sequence. These words are not a contiguous register block.
struct GnmDepthTargetBindPacket {
    std::uint32_t z_info_header;
    std::uint32_t z_info_register;
    // Z/stencil info, read bases, write bases, padded size and slice.
    std::uint32_t depth_words[8];
    std::uint32_t depth_info_header;
    std::uint32_t depth_info_register;
    std::uint32_t depth_info;
    std::uint32_t depth_view_header;
    std::uint32_t depth_view_register;
    std::uint32_t depth_view;
    std::uint32_t htile_base_header;
    std::uint32_t htile_base_register;
    std::uint32_t htile_base_256;
    std::uint32_t htile_surface_header;
    std::uint32_t htile_surface_register;
    std::uint32_t htile_surface;
    std::uint32_t dimensions_header;
    std::uint32_t width_height;

    // Exact writer framing and bound predicate, not GPU-resource validation.
    // A capture must also establish that this starts at a visited packet boundary.
    constexpr bool matches_native_writer_layout() const {
        return z_info_header == 0xc0086900 && z_info_register == 0x10 &&
               (depth_words[2] != 0 || depth_words[3] != 0) && depth_info_header == 0xc0016900 &&
               depth_info_register == 0x0f && depth_view_header == 0xc0016900 && depth_view_register == 0x02 &&
               htile_base_header == 0xc0016900 && htile_base_register == 0x05 &&
               htile_surface_header == 0xc0016900 && htile_surface_register == 0x2af &&
               dimensions_header == 0xc0001000;
    }
    constexpr std::uint32_t width() const { return width_height & 0xffff; }
    constexpr std::uint32_t height() const { return width_height >> 16; }
};

// Eleven dwords copied to CONTEXT registers 0xa318 + 15 * slot.
// RVA 0x1085f90 clears exactly 0x2c bytes and stores the unpadded dimensions
// at +0x18; the command writer repeats that word in a one-word NOP. Padding or
// pitch must not be inferred from these dimensions or the screen scissor.
struct GnmColorRenderTarget {
    std::uint32_t base_256;
    std::uint32_t pitch;
    std::uint32_t slice;
    std::uint32_t view;
    std::uint32_t info;
    std::uint32_t attrib;
    std::uint32_t width_height;
    std::uint32_t cmask_base_256;
    std::uint32_t cmask_slice;
    std::uint32_t fmask_base_256;
    std::uint32_t fmask_slice;

    // Native getter RVA 0x1086ca0 returns the encoded base; callers shift by 8.
    constexpr std::uint64_t address() const { return static_cast<std::uint64_t>(base_256) << 8; }
    // RVA 0x1086d70 / RVA 0x10868d0: requested dimensions, without tile padding.
    constexpr std::uint32_t width() const { return width_height & 0xffff; }
    constexpr std::uint32_t height() const { return width_height >> 16; }
    // RVA 0x1086cb0 and RVA 0x1085f90: pitch in eight-element units minus one.
    // An element count, not a byte stride or proof of linear storage.
    constexpr std::uint32_t pitch_elements() const { return ((pitch & 0x7ff) + 1) * 8; }
    // RVA 0x1086cc0 and RVA 0x1085f90: padded slice in 64-element units minus one.
    constexpr std::uint32_t slice_elements() const { return ((slice & 0x3fffff) + 1) * 64; }
    // RVA 0x1086cd0 / RVA 0x1086ce0. Last slice is inclusive.
    constexpr std::uint32_t first_slice() const { return view & 0x7ff; }
    constexpr std::uint32_t last_slice() const { return (view >> 13) & 0x7ff; }
    // Inputs to the native format conversion at RVA 0x1086cf0, kept raw.
    constexpr std::uint32_t format_raw() const { return (info >> 2) & 0x1f; }
    constexpr std::uint32_t number_type_raw() const { return (info >> 8) & 7; }
    constexpr std::uint32_t component_swap_raw() const { return (info >> 11) & 3; }
    // RVA 0x10868c0. An exponent, not a decoded sample count.
    constexpr std::uint32_t sample_count_log2() const { return (attrib >> 12) & 7; }
    // RVA 0x1086d20 reads the secondary/FMASK tile-mode field, not color's low
    // five bits. The constructor initially populates both with the same mode.
    constexpr std::uint32_t fmask_tile_mode_raw() const { return (attrib >> 5) & 0x1f; }
    constexpr std::uint32_t color_tile_mode_raw() const { return attrib & 0x1f; }
};

// Depth/stencil descriptor, 0x34 bytes including software dimensions at +0x30.
// Its words go to several non-contiguous CONTEXT register ranges. HTILE is
// metadata and is not interchangeable with the depth image address.
struct GnmDepthRenderTarget {
    std::uint32_t z_info;
    std::uint32_t stencil_info;
    std::uint32_t z_read_base_256;
    std::uint32_t stencil_read_base_256;
    std::uint32_t z_write_base_256;
    std::uint32_t stencil_write_base_256;
    std::uint32_t depth_size;
    std::uint32_t depth_slice;
    std::uint32_t depth_view;
    std::uint32_t htile_base_256;
    std::uint32_t htile_surface;
    std::uint32_t depth_info;
    std::uint32_t width_height;

    constexpr std::uint64_t depth_read_address() const { return static_cast<std::uint64_t>(z_read_base_256) << 8; }
    constexpr std::uint64_t depth_write_address() const { return static_cast<std::uint64_t>(z_write_base_256) << 8; }
    constexpr std::uint64_t htile_address() const { return static_cast<std::uint64_t>(htile_base_256) << 8; }
    constexpr std::uint32_t width() const { return width_height & 0xffff; }
    constexpr std::uint32_t height() const { return width_height >> 16; }
    // RVA 0x1072630 passes these padded dimensions to the HTILE size calculator.
    constexpr std::uint32_t pitch_elements() const { return ((depth_size & 0x7ff) + 1) * 8; }
    constexpr std::uint32_t padded_height() const { return (((depth_size >> 11) & 0x7ff) + 1) * 8; }
    // RVA 0x10726b0 / RVA 0x10726c0 / RVA 0x1072730.
    constexpr std::uint32_t format_raw() const { return z_info & 3; }
    constexpr std::uint32_t sample_count_log2() const { return (z_info >> 2) & 3; }
    constexpr bool htile_enabled() const { return (z_info & 0x20000000) != 0; }
    // RVA 0x1072630 supplies this bit as the mode byte to RVA 0x109e570.
    constexpr std::uint8_t htile_mode_raw() const { return static_cast<std::uint8_t>(htile_surface & 1); }
    // RVA 0x1072800 / RVA 0x1072810. Last slice is inclusive.
    constexpr std::uint32_t first_slice() const { return depth_view & 0x7ff; }
    constexpr std::uint32_t last_slice() const { return (depth_view >> 13) & 0x7ff; }
};

// Copied verbatim to GXColorTargetDescriptor +0x2c after successful creation.
struct GXColorViewParameters {
    std::uint32_t format_raw;
    // Native initializer accepts 1..=8; not the GNM texture-type enum.
    std::uint32_t dimension_raw;
    std::uint32_t mip_level;
    std::uint32_t first_array_slice;
    std::uint32_t array_size;
};

// Copied verbatim to GXDepthTargetDescriptor +0x34 after successful creation.
struct GXDepthViewParameters {
    std::uint32_t format_raw;
    // Native initializer accepts 1..=6.
    std::uint32_t dimension_raw;
    // Includes GX-specific behavior; do not substitute D3D11 flags by name.
    std::uint32_t flags_raw;
    std::uint32_t mip_level;
    std::uint32_t first_array_slice;
    std::uint32_t array_size;
};

struct GXColorTargetDescriptor {
    GnmColorRenderTarget target;
    GXColorViewParameters view;
};

struct GXDepthTargetDescriptor {
    GnmDepthRenderTarget target;
    GXDepthViewParameters view;
    // +0x4c depends on flags==1/3 and resource byte +0x3b. Meaning not yet named.
    std::uint32_t format_control_raw;
};

// RVA 0x21667a0 allocates 0x50 bytes aligned to 8 and retains resource +0x10.
struct GXColorTargetView {
    const void* vftable;
    void* resource;
    GXColorTargetDescriptor descriptor;
};

// RVA 0x2166610 allocates 0x60 bytes aligned to 8 and retains resource +0x10.
struct GXDepthTargetView {
    const void* vftable;
    void* resource;
    GXDepthTargetDescriptor descriptor;
};

namespace detail::gx_layout {
BB_SIZE(GnmIndirectBufferPacket, 0x10);
BB_SIZE(GnmNumInstancesPacket, 0x08);
BB_SIZE(GnmDepthTargetBindPacket, 0x60);
BB_OFFSET(GnmDepthTargetBindPacket, dimensions_header, 0x58);
BB_OFFSET(GnmDepthTargetBindPacket, width_height, 0x5c);
BB_SIZE(GnmColorRenderTarget, 0x2c);
BB_SIZE(GnmDepthRenderTarget, 0x34);
BB_SIZE(GXColorTargetDescriptor, 0x40);
BB_SIZE(GXDepthTargetDescriptor, 0x50);
BB_SIZE(GXColorTargetView, 0x50);
BB_SIZE(GXDepthTargetView, 0x60);
BB_OFFSET(GXColorTargetDescriptor, view, 0x2c);
BB_OFFSET(GXDepthTargetDescriptor, view, 0x34);
BB_OFFSET(GXDepthTargetDescriptor, format_control_raw, 0x4c);
BB_OFFSET(GnmColorRenderTarget, width_height, 0x18);
BB_OFFSET(GnmDepthRenderTarget, width_height, 0x30);
}  // namespace detail::gx_layout

}  // namespace bb
