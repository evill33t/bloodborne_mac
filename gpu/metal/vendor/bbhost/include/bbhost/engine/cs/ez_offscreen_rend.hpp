// Offscreen renderer owned by the menu assembly renderer.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Descriptive 0x20-byte counted object created by the offscreen constructor.
// Attach replaces output_resource using non-atomic reference counting.
struct OffscreenTextureOwner {
    const void* vftable;
    std::int32_t reference_count;
    Unknown<4> _unk0c;
    void* retained_source;
    void* output_resource;
};

// Descriptive 0x40-byte binding allocated by setup. The first pointer holds
// another reference to renderer +0x40. Cleanup releases it before freeing this
// allocation. Mask values initialize from native rendering globals.
struct OffscreenRenderBinding {
    void* retained_object;
    std::uint32_t mask_a[4];
    std::uint32_t mask_b[4];
    std::uint64_t values_28_bits[3];
};

// Native CSEzOffscreenRend, allocated as 0x70 bytes aligned to eight and stored
// at CSMenuAsmModelRend +0xc8. Identity follows registration of this pointer
// under its native debug name; no DLRuntimeClass is claimed.
//
// Setup creates camera/render resources and stores dimensions as floats.
// Attach inserts the scene into the render manager and retains its output
// resource in texture_owner. Detach removes it from the manager. Clearing
// resources detaches first, queues the scene for delayed release, and frees or
// unreferences the remaining children. Destruction also releases the texture
// repository resource and debug registration.
struct CSEzOffscreenRend {
    static constexpr std::size_t SIZE = 0x70;
    static constexpr Rva INSTANCE_VTABLE{0x534df50};
    static constexpr Rva DEBUG_NAME{0x49a0dea};
    static constexpr Rva CONSTRUCTOR_FN{0x1da6f30};
    static constexpr Rva DESTRUCTOR_FN{0x1da7360};
    static constexpr Rva SETUP_FN{0x1da7aa0};
    static constexpr Rva ATTACH_FN{0x1da87b0};
    static constexpr Rva DETACH_FN{0x1da7680};
    static constexpr Rva CLEAR_RESOURCES_FN{0x1da7770};
    static constexpr Rva COPY_CAMERA_FN{0x1da8880};

    const void* vftable;
    OffscreenTextureOwner* texture_owner;
    void* texture_resource;
    // Borrowed from RendMan +0x08.
    void* render_manager;
    Unknown<0x120>* camera_pair;
    Unknown<0x90>* camera_state;
    Unknown<0x60>* camera;
    Unknown<0x40>* retained_38;
    Unknown<0x40>* retained_40;
    void* scene;
    OffscreenRenderBinding* binding;
    std::uint8_t attached;
    Unknown<7> _unk59;
    void* debug_context;
    float width;
    float height;
};

namespace detail::ez_offscreen_rend_layout {
using T = CSEzOffscreenRend;
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 8, "alignof(CSEzOffscreenRend)");
BB_OFFSET(T, texture_owner, 8);
BB_OFFSET(T, texture_resource, 0x10);
BB_OFFSET(T, render_manager, 0x18);
BB_OFFSET(T, camera_pair, 0x20);
BB_OFFSET(T, camera_state, 0x28);
BB_OFFSET(T, camera, 0x30);
BB_OFFSET(T, retained_38, 0x38);
BB_OFFSET(T, retained_40, 0x40);
BB_OFFSET(T, scene, 0x48);
BB_OFFSET(T, binding, 0x50);
BB_OFFSET(T, attached, 0x58);
BB_OFFSET(T, debug_context, 0x60);
BB_OFFSET(T, width, 0x68);
BB_OFFSET(T, height, 0x6c);
BB_SIZE(OffscreenTextureOwner, 0x20);
BB_OFFSET(OffscreenTextureOwner, reference_count, 8);
BB_OFFSET(OffscreenTextureOwner, retained_source, 0x10);
BB_OFFSET(OffscreenTextureOwner, output_resource, 0x18);
BB_SIZE(OffscreenRenderBinding, 0x40);
BB_OFFSET(OffscreenRenderBinding, mask_a, 8);
BB_OFFSET(OffscreenRenderBinding, mask_b, 0x18);
BB_OFFSET(OffscreenRenderBinding, values_28_bits, 0x28);
}  // namespace detail::ez_offscreen_rend_layout

}  // namespace bb
