// Blood-message ghost presentation and its borrowed request data.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct ReplayGhostIns;

// Indices populated by native registration. The larger diagnostic bounds in
// dispatch/advance do not establish additional valid callbacks.
enum class CSDisplayGhostStep : std::int32_t {
    Init = 0,
    InitForLoadResource = 1,
    WaitForLoadResource = 2,
    InitForBloodMessageGhost = 3,
    UpdateForBloodMessageGhost = 4,
    WaitForFadeOut = 5,
    WaitForPlayRequest = 6,
    InitForReleaseResource = 7,
    WaitForReleaseResource = 8,
    Finish = 9,
};

// Descriptive consumer view of borrowed blood-message appearance data. Native
// reads establish bytes through +0x3d; rounded to 0x40 for alignment. Not an
// independently sized/reflected class. The request producer borrows
// database-item +0x84 without retaining it.
struct CSDisplayGhostAppearancePrefix {
    // First eight IDs use native lookup RVA 0x1a87260; the remaining five use
    // 0x1a87390. Temporary Gaitem references are released after copying.
    std::uint32_t equipment_ids[13];
    // Consumed by ReplayGhostIns::APPLY_PACKED_COLOR_FN.
    std::uint32_t packed_color;
    // Copied directly to ReplayGhostIns +0x570; exact enum unclassified.
    std::uint8_t presentation_kind_raw;
    // Signed bytes, each divided by 100.0 and written to actor +0x4e8..+0x4f8.
    std::int8_t appearance_weights[5];
    Unknown<2> _unclassified_3e;
};

// Native local step: reflected and allocated as 0x130, aligned to 16.
//
// Blood-message owner RVA 0x1456860 stores this at +0x28; its update at
// 0x1456a70 dispatches virtual +0xd0 with FD4Time. No embedded scheduler task.
// Init creates a ChrType-11 ReplayGhostIns, then waits for play requests while
// retaining that actor between presentations.
//
// A nonzero pose request replaces the pending position, heading and borrowed
// appearance pointer, sets command=1, and interrupts the current presentation.
// Zero pose is ignored. Stop sets command=0 and interruption=1. Neither changes
// the step index. WaitForPlayRequest consumes command 1, copies appearance to
// the actor, clears command/interruption and requests 2. Resource readiness
// plus a strictly expired delay permits 3; 3/4 react to interruption by
// requesting fade step 5. Fade completion returns to 6.
//
// Release step 7 schedules WorldChrMan delayed deletion and clears ghost; 8
// advances without checking deletion completion, and 9 requests -1. The
// destructor only releases step/debug storage: it does not remove ghost or
// free/retain request_appearance.
struct alignas(16) CSDisplayGhost {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_DISPLAY_GHOST_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_DISPLAY_GHOST_TEMPLATE;
    static constexpr std::size_t SIZE = 0x130;
    static constexpr Rva INSTANCE_VTABLE{0x5335b90};
    static constexpr Rva METADATA_VTABLE{0x5399500};
    static constexpr Rva SIZE_FN{0x1a6b4b0};
    static constexpr Rva CONSTRUCTOR_FN{0x1a67be0};
    static constexpr Rva DESTRUCTOR_FN{0x1a6a000};
    static constexpr Rva DELETING_DESTRUCTOR_FN{0x1a6a080};
    static constexpr Rva REQUEST_PLAY_FN{0x1a67e10};
    static constexpr Rva REQUEST_STOP_FN{0x1a67e50};
    static constexpr Rva DISPATCH_FN{0x1a6aba0};
    static constexpr Rva EXECUTE_FN{0x1a6aee0};
    static constexpr Rva IS_FINISHED_FN{0x1a6a110};
    static constexpr Rva ADVANCE_FN{0x1a6be20};
    static constexpr Rva BLOOD_MESSAGE_REQUEST_PRODUCER_FN{0x1457d30};
    static constexpr Rva OWNER_CONSTRUCTOR_FN{0x1456860};
    static constexpr Rva OWNER_UPDATE_FN{0x1456a70};
    static constexpr Rva DEFAULT_LOAD_DELAY{0x557d120};
    // Registration initializes the mutable default to ~2.3333 (bits 0x40155555).
    static constexpr float INITIAL_LOAD_DELAY = 2.3333332538604736f;

    SprjStepLocalC8 local_step;
    // 1 means a pending play request, 0 means none/stop. Initially zero.
    std::int32_t request_command;
    std::uint8_t interrupt_presentation;
    Unknown<3> _unknown_cd;
    // Presentation fade copied to actor +0x20c. Starts at zero; step 4 raises
    // it toward 1, step 5 lowers/clamps it to zero.
    float fade;
    Unknown<4> _unknown_d4;
    // WorldChr-owned actor; release uses delayed removal, not direct free.
    ReplayGhostIns* ghost;
    // Constructor writes -1; no consumer established.
    std::int32_t value_e0;
    Unknown<4> _unknown_e4;
    // Reset from DEFAULT_LOAD_DELAY when consuming a play request.
    FD4Time resource_wait;
    // Active pose, copied from request_pose when command 1 is consumed.
    // InitForBloodMessageGhost uses 0x1963b + 10 * pose as an action ID when
    // the actor's behavior-module gate permits it.
    std::uint8_t pose;
    Unknown<7> _unknown_f9;
    // Aligned four-float position copied by the request producer.
    float request_position[4];
    float request_heading;
    Unknown<4> _unknown_114;
    const CSDisplayGhostAppearancePrefix* request_appearance;
    std::uint8_t request_pose;
    Unknown<0xf> _unknown_121;

    static bool step_known(std::int32_t raw) { return raw >= 0 && raw <= 9; }
    CSDisplayGhostStep current_step() const { return static_cast<CSDisplayGhostStep>(local_step.current_step); }
    CSDisplayGhostStep requested_step() const { return static_cast<CSDisplayGhostStep>(local_step.requested_step); }
    // WaitForLoadResource's gate after subtracting the current frame delta. The
    // caller supplies actor +0x4b5.
    bool resource_wait_can_advance(bool actor_resources_ready) const {
        return actor_resources_ready && resource_wait.time < 0.0f;
    }
};

namespace detail::display_ghost_layout {
using T = CSDisplayGhost;
BB_SIZE(T, 0x130);
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 16, "alignof(CSDisplayGhost)");
BB_OFFSET(T, local_step, 0);
BB_OFFSET(T, request_command, 0xc8);
BB_OFFSET(T, interrupt_presentation, 0xcc);
BB_OFFSET(T, fade, 0xd0);
BB_OFFSET(T, ghost, 0xd8);
BB_OFFSET(T, value_e0, 0xe0);
BB_OFFSET(T, resource_wait, 0xe8);
static_assert(offsetof(T, resource_wait) + offsetof(FD4Time, time) == 0xf0, "CSDisplayGhost resource_wait.time");
BB_OFFSET(T, pose, 0xf8);
BB_OFFSET(T, request_position, 0x100);
BB_OFFSET(T, request_heading, 0x110);
BB_OFFSET(T, request_appearance, 0x118);
BB_OFFSET(T, request_pose, 0x120);
BB_SIZE(CSDisplayGhostAppearancePrefix, 0x40);
static_assert(alignof(CSDisplayGhostAppearancePrefix) == 4, "alignof(CSDisplayGhostAppearancePrefix)");
BB_OFFSET(CSDisplayGhostAppearancePrefix, packed_color, 0x34);
BB_OFFSET(CSDisplayGhostAppearancePrefix, presentation_kind_raw, 0x38);
BB_OFFSET(CSDisplayGhostAppearancePrefix, appearance_weights, 0x39);
}  // namespace detail::display_ghost_layout

}  // namespace bb
