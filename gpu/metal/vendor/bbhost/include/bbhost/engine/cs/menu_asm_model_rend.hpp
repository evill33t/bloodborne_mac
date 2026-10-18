// Menu character-model rendering and appearance setup state.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/game_data_man.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct CSEzOffscreenRend;
struct CSModelIns;
struct MenuAsmAssembly;
struct MenuAsmRequest;

// Descriptive view of one inline equipment/appearance snapshot, size 0x1b8.
// Not a separately identified runtime class. The final five payload bytes are
// copied with FACE data; their individual meanings are unknown. Equipment
// references require native assignment/release and must not be memcpy'd.
struct MenuAsmAppearanceSnapshot {
    PlayerEquipmentData equipment;
    GameDataFacePayload face;
    std::uint8_t value_1b0;
    std::uint8_t values_1b1[4];
    Unknown<3> _unk1b5;
};

// Base menu model renderer, reflected size 0x800.
//
// Constructor RVA 0x1da9520 creates a separate 0x70 model context, embeds a
// callback task bound to this object, and initializes three appearance
// snapshots. STEP_Init_Setup copies requested -> setup; STEP_Finish_Setup
// copies setup -> active and rebuilds face_data. Equipment copies use native
// reference assignment; the FACE payload and five following bytes use memcpy.
//
// Destructor 0x1daa890 clears model resources, releases the context and
// assembly request, releases retained references, destroys the equipment
// snapshots, then unregisters the callback. Native instances must stay at
// their allocated addresses because callback_task.owner points back here. The
// SprjStepLocal prefix, child class identities, and unnamed fields remain under
// study. Derived menu renderers can extend beyond this base extent.
struct CSMenuAsmModelRend {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_MENU_ASM_MODEL_REND_RUNTIME_CLASS;
    // STEP_TEMPLATE: 9 steps
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_MENU_ASM_MODEL_REND_TEMPLATE;
    static constexpr std::size_t SIZE = 0x800;
    static constexpr Rva INSTANCE_VTABLE{0x53a4980};
    static constexpr Rva SIZE_FN{0x1daf250};
    static constexpr Rva CONSTRUCTOR_FN{0x1da9520};
    static constexpr Rva DESTRUCTOR_FN{0x1daa890};
    static constexpr Rva CLEAR_MODEL_RESOURCES_FN{0x1daab70};
    static constexpr Rva TASK_CALLBACK_FN{0x1daa530};

    const void* vftable;
    Unknown<0xc0> _step_local;
    CSEzOffscreenRend* model_context;
    SprjCallbackTask38 callback_task;
    std::int32_t configuration[3];
    Unknown<4> _unk114;
    // Four pairs initialized to [1, 10], passed to native assembly setup.
    std::uint32_t setup_values[4][2];
    MenuAsmAppearanceSnapshot active_appearance;
    MenuAsmAppearanceSnapshot setup_appearance;
    MenuAsmAppearanceSnapshot requested_appearance;
    std::uint8_t request_pending;
    Unknown<7> _unk661;
    // STEP_Init_Setup allocates this 0x7c0-byte child with alignment eight.
    MenuAsmRequest* assembly_request;
    CSModelIns* model_instance;
    MenuAsmAssembly* assembly;
    // Released through the native delay-delete manager when its count expires.
    void* delayed_resource;
    FaceData face_data;
    void* owned_state_7a0;
    void* owned_buffer_7a8;
    void* retained_object_7b0;
    std::int32_t value_7b8;
    std::int32_t value_7bc;
    std::uint8_t flag_7c0;
    std::uint8_t flag_7c1;
    Unknown<6> _unk7c2;
    Unknown<0x20> _state_7c8;
    void* retained_object_7e8;
    // STEP_Init_Play decrements this positive counter before advancing.
    std::int32_t play_delay_frames;
    Unknown<4> _unk7f4;
    void* context_7f8;
};

namespace detail::menu_asm_model_rend_layout {
using T = CSMenuAsmModelRend;
BB_SIZE(T, 0x800);
BB_SIZE(T, T::SIZE);
BB_OFFSET(T, model_context, 0xc8);
BB_OFFSET(T, callback_task, 0xd0);
BB_OFFSET(T, configuration, 0x108);
BB_OFFSET(T, setup_values, 0x118);
BB_OFFSET(T, active_appearance, 0x138);
BB_OFFSET(T, setup_appearance, 0x2f0);
BB_OFFSET(T, requested_appearance, 0x4a8);
BB_OFFSET(T, request_pending, 0x660);
BB_OFFSET(T, assembly_request, 0x668);
BB_OFFSET(T, model_instance, 0x670);
BB_OFFSET(T, assembly, 0x678);
BB_OFFSET(T, delayed_resource, 0x680);
BB_OFFSET(T, face_data, 0x688);
BB_OFFSET(T, owned_state_7a0, 0x7a0);
BB_OFFSET(T, retained_object_7b0, 0x7b0);
BB_OFFSET(T, flag_7c1, 0x7c1);
BB_OFFSET(T, _state_7c8, 0x7c8);
BB_OFFSET(T, retained_object_7e8, 0x7e8);
BB_OFFSET(T, play_delay_frames, 0x7f0);
BB_OFFSET(T, context_7f8, 0x7f8);
using S = MenuAsmAppearanceSnapshot;
BB_SIZE(S, 0x1b8);
BB_OFFSET(S, face, 0xc0);
BB_OFFSET(S, value_1b0, 0x1b0);
BB_OFFSET(S, values_1b1, 0x1b1);
static_assert(offsetof(S, _unk1b5) - offsetof(S, face) == 0xf5, "FACE payload + five bytes");
}  // namespace detail::menu_asm_model_rend_layout

}  // namespace bb
