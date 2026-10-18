// YEBIS texture, framebuffer and bound-surface layouts (native memory, not host COM wrappers).
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Pointer fields are evidence, not permission to call through them with the
// host ABI. See the YEBIS color-target, framebuffer and depth port notes in
// docs/, alongside the three-binary graphics comparison.

inline constexpr Rva CREATE_RENDER_TARGET{0x0c72f80};
inline constexpr Rva CREATE_DEPTH_STENCIL_SURFACE{0x0c73100};
inline constexpr Rva CREATE_SURFACE_2D{0x0c78ca0};
inline constexpr Rva CREATE_RESOURCE_VIEW{0x0c76b80};
inline constexpr Rva REGISTER_D3D_SURFACE{0x0c78740};
inline constexpr Rva DESTROY_SURFACE{0x0c774c0};
inline constexpr Rva RELEASE_SURFACE{0x0c82cb0};
inline constexpr Rva INITIALIZE_DEVICE{0x0c71080};
inline constexpr Rva FORMAT_TO_TYPELESS{0x0c4e2c0};
inline constexpr Rva DEPTH_FROM_TYPELESS{0x0c4e4f0};
inline constexpr Rva DEPTH_FROM_RESOURCE_FORMAT{0x0c4e530};
inline constexpr Rva MSAA_TO_SAMPLE_COUNT{0x0c4f520};
inline constexpr Rva GENERATE_FRAMEBUFFER_TABLE{0x0c77680};
inline constexpr Rva ATTACH_FRAMEBUFFER{0x0c77780};
inline constexpr Rva DETACH_FRAMEBUFFER{0x0c777c0};
inline constexpr Rva DESTROY_FRAMEBUFFER_TABLE{0x0c77830};
inline constexpr Rva ATTACH_OM_VIEW{0x0c779a0};
inline constexpr Rva ATTACH_TO_FRAMEBUFFER{0x0c77ae0};
inline constexpr Rva DETACH_ONE_ATTACHMENT{0x0c77f10};
inline constexpr Rva DETACH_FROM_FRAMEBUFFER{0x0c78150};
inline constexpr Rva SET_RENDER_TARGET{0x0c713e0};
inline constexpr Rva SET_DEPTH_STENCIL_SURFACE{0x0c714d0};
inline constexpr Rva GET_RENDER_TARGET{0x0c717b0};
inline constexpr Rva GET_DEPTH_STENCIL_SURFACE{0x0c71810};
inline constexpr Rva PPFX_GNM_INITIALIZE{0x0bb4b40};
inline constexpr Rva PPFX_BASE_INITIALIZE{0x0bb1c20};
inline constexpr Rva POST_EFFECT_INITIALIZE{0x0bc98b0};

inline constexpr std::size_t COLOR_ATTACHMENT_COUNT = 8;
// Ten table slots exist; slot 9's full semantics remain unresolved.
inline constexpr std::size_t FRAMEBUFFER_ATTACHMENT_COUNT = 10;
inline constexpr std::uint32_t DEPTH_ATTACHMENT_RAW = 8;
// Sentinel used for no self-attachment or a wildcard detach filter.
inline constexpr std::uint32_t UNKNOWN_ATTACHMENT_RAW = 0xffffffffu;

inline constexpr std::uint32_t INVALID_ARGUMENT_RAW = 0x80000003u;
inline constexpr std::uint32_t RESOURCE_CREATION_FAILED = 0x80004005u;
// Native getter writes null and returns this value for an empty tracked slot.
inline constexpr std::uint32_t EMPTY_TRACKED_SURFACE_RAW = 0xffffff00u;

// Exact switch at FORMAT_TO_TYPELESS; DS3 PS4 and PC helpers have the same
// groups. The hole at 0x1a, zero, and every value above 0x41 pass through.
constexpr std::uint32_t typeless_format(std::uint32_t format_raw) {
    if (format_raw >= 0x01 && format_raw <= 0x04) return 0x01;
    if (format_raw >= 0x05 && format_raw <= 0x08) return 0x05;
    if (format_raw >= 0x09 && format_raw <= 0x0e) return 0x09;
    if (format_raw >= 0x0f && format_raw <= 0x12) return 0x0f;
    if (format_raw >= 0x13 && format_raw <= 0x16) return 0x13;
    if (format_raw >= 0x17 && format_raw <= 0x19) return 0x17;
    if (format_raw >= 0x1b && format_raw <= 0x20) return 0x1b;
    if (format_raw >= 0x21 && format_raw <= 0x26) return 0x21;
    if (format_raw >= 0x27 && format_raw <= 0x2b) return 0x27;
    if (format_raw >= 0x2c && format_raw <= 0x2f) return 0x2c;
    if (format_raw >= 0x30 && format_raw <= 0x34) return 0x30;
    if (format_raw >= 0x35 && format_raw <= 0x3b) return 0x35;
    if (format_raw >= 0x3c && format_raw <= 0x41) return 0x3c;
    return format_raw;
}

// Only zero becomes one. No clamp or power-of-two validation is performed.
constexpr std::uint32_t sample_count(std::uint32_t samples_raw) { return samples_raw == 0 ? 1 : samples_raw; }

// Four-case typeless-only helper, also present at DS3 PC RVA 0x17031c0.
constexpr std::uint32_t depth_from_typeless(std::uint32_t format_raw) {
    switch (format_raw) {
        case 0x13: return 0x14;
        case 0x27: return 0x28;
        case 0x2c: return 0x2d;
        case 0x35: return 0x37;
        default: return 0;
    }
}

// Exact RVA 0x0c4e530 mapping. Includes shader-readable family members;
// already-typed depth formats return zero, not themselves.
constexpr std::uint32_t depth_from_resource_format(std::uint32_t format_raw) {
    switch (format_raw) {
        case 0x13: case 0x15: case 0x16: return 0x14;
        case 0x27: case 0x29: return 0x28;
        case 0x2c: case 0x2e: case 0x2f: return 0x2d;
        case 0x35: case 0x38: return 0x37;
        default: return 0;
    }
}

// Native 44-byte description passed to the D3D11-shaped device's +0x28 slot.
// SDK D3D11_TEXTURE2D_DESC layout, distinct from GNM register words.
struct YebisTexture2DDescriptor {
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mip_levels;
    std::uint32_t array_size;
    std::uint32_t format_raw;
    std::uint32_t sample_count;
    std::uint32_t sample_quality;
    std::uint32_t usage_raw;
    std::uint32_t bind_flags_raw;
    std::uint32_t cpu_access_flags_raw;
    std::uint32_t misc_flags_raw;
};

// Native/SDK 24-byte DSV descriptor. Union bytes remain raw for all dimensions.
// Usage-2 texture factory uses dimension 3 or 5, zero flags and mip zero.
struct YebisDepthStencilViewDescriptor {
    std::uint32_t format_raw;
    std::uint32_t dimension_raw;
    std::uint32_t flags_raw;
    std::uint32_t dimension_data[3];
};

struct YebisFramebufferAttachmentNode;

// A 0x128-byte prefix of the 0x178-byte surface allocation at RVA 0x0c78260.
// Unknown ranges remain unknown. Must not be applied to DS3.
struct YebisSurfaceOwnershipPrefix {
    const void* vtable;
    std::int32_t reference_count;
    std::uint32_t resource_kind_raw;
    std::uint32_t storage_kind_raw;
    std::uint8_t reserved_014[4];
    // Borrowed YEBIS owner; constructor stores it without AddRef.
    void* gpu_device;
    std::uint8_t owns_storage;
    std::uint8_t reserved_021[3];
    std::uint32_t usage_raw;
    std::uint32_t resource_flags_raw;
    std::uint8_t reserved_02c[4];
    // Retained texture/resource, released by RVA 0x0c77230.
    void* resource;
    std::uint8_t reserved_038[0xb0];
    // Ten surface pointers. Cross-surface entries retain the attached surface;
    // self-attachment does not AddRef. Native detach anomalies are documented.
    void** framebuffer_attachments;
    std::uint32_t self_attachment_raw;
    std::uint8_t reserved_0f4[0x0c];
    YebisFramebufferAttachmentNode* reverse_attachment_list;
    std::uint64_t reverse_attachment_count;
    // Retained generic view. May alias a specialized view with a second ref.
    void* surface_view;
    void* render_target_view;
    void* depth_stencil_view;
};

// Reverse-list node allocated by RVA 0x0c77ae0; owner is a borrowed pointer.
struct YebisFramebufferAttachmentNode {
    YebisFramebufferAttachmentNode* next;
    YebisFramebufferAttachmentNode* previous;
    std::uint32_t attachment_raw;
    std::uint8_t reserved_014[4];
    void* framebuffer_owner;
};

// Observed 24-byte cursor passed to detach-one at RVA 0x0c781f5. The first 16
// bytes are unresolved/possibly uninitialized; only the node field is
// established. Not a general C++ iterator implementation.
struct YebisFramebufferAttachmentCursor {
    std::uint8_t reserved_000[0x10];
    YebisFramebufferAttachmentNode* current;
};

// Known output-merger tail of the surface. Count includes null slot holes.
struct YebisOutputMergerViews {
    std::uint32_t color_count;
    std::uint8_t reserved_004[4];
    void* color_views[COLOR_ATTACHMENT_COUNT];
    void* depth_stencil_view;
};

// Full allocation extent, with unknown ranges retained inside the prefix.
// Layout knowledge only; not a recovered callable C++ class.
struct YebisSurfaceLayout {
    YebisSurfaceOwnershipPrefix ownership;
    YebisOutputMergerViews output_merger;
};

// Known 0x918-byte prefix of the 0xa50-byte YEBIS device allocation. Bound
// surfaces are retained native objects, distinct from the context's current
// COM views. Setting non-null tracked depth does not bind a DSV.
struct YebisDeviceBindingPrefix {
    std::uint8_t reserved_000[0x10];
    void* context;
    void* master_context;
    void* d3d_device;
    std::uint8_t reserved_028[0x8a8];
    YebisSurfaceLayout* bound_surfaces[COLOR_ATTACHMENT_COUNT];
    YebisSurfaceLayout* tracked_depth_surface;
};

namespace detail::yebis_layout {
BB_SIZE(YebisTexture2DDescriptor, 0x2c);
BB_OFFSET(YebisTexture2DDescriptor, format_raw, 0x10);
BB_OFFSET(YebisTexture2DDescriptor, sample_count, 0x14);
BB_OFFSET(YebisTexture2DDescriptor, bind_flags_raw, 0x20);
BB_SIZE(YebisDepthStencilViewDescriptor, 0x18);
BB_OFFSET(YebisDepthStencilViewDescriptor, flags_raw, 0x08);
BB_OFFSET(YebisDepthStencilViewDescriptor, dimension_data, 0x0c);
BB_SIZE(YebisSurfaceOwnershipPrefix, 0x128);
BB_OFFSET(YebisSurfaceOwnershipPrefix, gpu_device, 0x18);
BB_OFFSET(YebisSurfaceOwnershipPrefix, resource, 0x30);
BB_OFFSET(YebisSurfaceOwnershipPrefix, framebuffer_attachments, 0xe8);
BB_OFFSET(YebisSurfaceOwnershipPrefix, self_attachment_raw, 0xf0);
BB_OFFSET(YebisSurfaceOwnershipPrefix, reverse_attachment_list, 0x100);
BB_OFFSET(YebisSurfaceOwnershipPrefix, reverse_attachment_count, 0x108);
BB_OFFSET(YebisSurfaceOwnershipPrefix, surface_view, 0x110);
BB_OFFSET(YebisSurfaceOwnershipPrefix, depth_stencil_view, 0x120);
BB_SIZE(YebisFramebufferAttachmentNode, 0x20);
BB_OFFSET(YebisFramebufferAttachmentNode, framebuffer_owner, 0x18);
BB_SIZE(YebisFramebufferAttachmentCursor, 0x18);
BB_OFFSET(YebisFramebufferAttachmentCursor, current, 0x10);
BB_SIZE(YebisOutputMergerViews, 0x50);
BB_OFFSET(YebisOutputMergerViews, color_views, 0x08);
BB_OFFSET(YebisOutputMergerViews, depth_stencil_view, 0x48);
BB_SIZE(YebisSurfaceLayout, 0x178);
BB_OFFSET(YebisSurfaceLayout, output_merger, 0x128);
BB_SIZE(YebisDeviceBindingPrefix, 0x918);
BB_OFFSET(YebisDeviceBindingPrefix, context, 0x10);
BB_OFFSET(YebisDeviceBindingPrefix, d3d_device, 0x20);
BB_OFFSET(YebisDeviceBindingPrefix, bound_surfaces, 0x8d0);
BB_OFFSET(YebisDeviceBindingPrefix, tracked_depth_surface, 0x910);
}  // namespace detail::yebis_layout

}  // namespace bb
