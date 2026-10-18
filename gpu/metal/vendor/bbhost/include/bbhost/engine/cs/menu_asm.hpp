// Assembly storage used by CSMenuAsmModelRend.
//
// Names here are descriptive: neither helper has been tied to a reflected
// class name. In particular, MenuAsmAssembly is not the separately reflected
// 0x300-byte ChrAsmModel.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct CSEzOffscreenRend;
struct CSModelIns;

// Descriptive 0x58-byte record. Current/pending storage is interleaved, not two
// complete snapshots. The two 16-byte values are only four-byte aligned and
// retain raw bits until their semantics are established.
struct MenuAsmRequestRecord {
    std::int32_t current_id;
    std::int32_t pending_id;
    void* current_resource;
    void* pending_resource;
    std::uint32_t current_mask_a;
    std::uint32_t pending_mask_a;
    std::uint32_t current_mask_b;
    std::uint32_t pending_mask_b;
    std::uint8_t current_flag;
    std::uint8_t pending_flag;
    Unknown<2> _unk2a;
    std::uint32_t current_value_bits[4];
    std::uint32_t pending_value_bits[4];
    std::int32_t current_value_4c;
    std::int32_t pending_value_50;
    Unknown<4> _unk54;
};

// Owned helper at renderer +0x668. Init_Setup allocates 0x7c0 bytes aligned to
// eight and constructs 22 records. Native update requests partsbnd files, waits
// on its request state and countdown, then promotes pending resources. Native
// destruction queues current resources for delayed deletion, releases pending
// resources through SprjFile, and clears the request state at +0x10.
struct MenuAsmRequest {
    static constexpr std::size_t SIZE = 0x7c0;
    static constexpr std::size_t RECORD_COUNT = 22;
    static constexpr Rva INSTANCE_VTABLE{0x534ad80};
    static constexpr Rva DESTRUCTOR_FN{0x1ca9670};
    static constexpr Rva UPDATE_FN{0x1ca9790};

    const void* vftable;
    std::int32_t countdown;
    Unknown<4> _unk0c;
    // Native request helper, passed to file loads and cleanup RVA 0xf7ebc0.
    Unknown<0x18> request_state;
    std::uint8_t force_reload;
    // Update can advance this from 1 to 2 after resource promotion.
    std::uint8_t phase;
    std::uint8_t flag_2a;
    std::uint8_t flag_2b;
    std::uint8_t flag_2c;
    std::uint8_t flag_2d;
    Unknown<2> _unk2e;
    MenuAsmRequestRecord records[22];
};

// Owned assembly helper at renderer +0x678, allocated 0x310 bytes aligned to 16
// by Wait_Setup. It owns 24 part objects and holds two independent banks of
// native counted references. Destruction tears down parts before releasing
// references and clearing resource bindings.
struct alignas(16) MenuAsmAssembly {
    static constexpr std::size_t SIZE = 0x310;
    static constexpr std::size_t PART_COUNT = 24;
    static constexpr Rva INSTANCE_VTABLE{0x534ac70};
    static constexpr Rva CONSTRUCTOR_FN{0x1c9c4d0};
    static constexpr Rva DESTRUCTOR_FN{0x1c9c950};
    static constexpr Rva BIND_DEBUG_CONTEXT_FN{0x1c9c7d0};
    static constexpr Rva APPLY_REQUEST_FN{0x1c9cd20};
    // Argument mapping in Apply: each part slot selects one request record.
    // Records 6 and 7 each feed four slots; records 10..13 are not used here.
    static constexpr std::size_t PART_TO_REQUEST_RECORD[24] = {
        0, 14, 1, 2, 3, 4, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 9, 15, 16, 17, 18, 19, 20, 21,
    };

    const void* vftable;
    CSModelIns* model_instance;
    std::uint64_t value_10_bits;
    // Assigned from renderer +0x680 during Wait_Setup.
    void* resource_context;
    std::uint32_t flags;
    Unknown<4> _unk24;
    void* owned_parts[24];
    // Constructor allocates one 0x80-byte object per slot and retains it.
    void* retained_resources[24];
    void* retained_secondary[20];
    // Apply selects these from current resources in request records 6 and 7.
    void* resource_bindings[4];
    // Constructor and Apply both set all eight bytes to 0xfe.
    std::uint8_t part_state[8];
    std::uint32_t values_270_bits[8][4];
    std::int32_t value_2f0;
    Unknown<4> _unk2f4;
    // Set to renderer +0xc8's 0x70-byte context during Wait_Setup.
    CSEzOffscreenRend* model_context;
    std::uint64_t value_300_bits;
    // Native debug-object lookup/binding uses the label CSModelIns.
    void* debug_context;
};

namespace detail::menu_asm_layout {
using R = MenuAsmRequestRecord;
BB_SIZE(R, 0x58);
static_assert(alignof(R) == 8, "alignof(MenuAsmRequestRecord)");
BB_OFFSET(R, pending_id, 4);
BB_OFFSET(R, current_resource, 8);
BB_OFFSET(R, pending_resource, 0x10);
BB_OFFSET(R, current_mask_a, 0x18);
BB_OFFSET(R, pending_mask_a, 0x1c);
BB_OFFSET(R, current_mask_b, 0x20);
BB_OFFSET(R, pending_mask_b, 0x24);
BB_OFFSET(R, current_flag, 0x28);
BB_OFFSET(R, pending_flag, 0x29);
BB_OFFSET(R, current_value_bits, 0x2c);
BB_OFFSET(R, pending_value_bits, 0x3c);
BB_OFFSET(R, current_value_4c, 0x4c);
BB_OFFSET(R, pending_value_50, 0x50);
BB_SIZE(MenuAsmRequest, MenuAsmRequest::SIZE);
static_assert(alignof(MenuAsmRequest) == 8, "alignof(MenuAsmRequest)");
BB_OFFSET(MenuAsmRequest, countdown, 8);
BB_OFFSET(MenuAsmRequest, request_state, 0x10);
BB_OFFSET(MenuAsmRequest, force_reload, 0x28);
BB_OFFSET(MenuAsmRequest, phase, 0x29);
BB_OFFSET(MenuAsmRequest, records, 0x30);
static_assert(0x30 + MenuAsmRequest::RECORD_COUNT * 0x58 == 0x7c0, "MenuAsmRequest records extent");
using A = MenuAsmAssembly;
BB_SIZE(A, A::SIZE);
static_assert(alignof(A) == 16, "alignof(MenuAsmAssembly)");
BB_OFFSET(A, model_instance, 8);
BB_OFFSET(A, resource_context, 0x18);
BB_OFFSET(A, flags, 0x20);
BB_OFFSET(A, owned_parts, 0x28);
BB_OFFSET(A, retained_resources, 0xe8);
BB_OFFSET(A, retained_secondary, 0x1a8);
BB_OFFSET(A, resource_bindings, 0x248);
BB_OFFSET(A, part_state, 0x268);
BB_OFFSET(A, values_270_bits, 0x270);
BB_OFFSET(A, value_2f0, 0x2f0);
BB_OFFSET(A, model_context, 0x2f8);
BB_OFFSET(A, value_300_bits, 0x300);
BB_OFFSET(A, debug_context, 0x308);
}  // namespace detail::menu_asm_layout

}  // namespace bb
