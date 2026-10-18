// SprjMoveMapListStep layout and step metadata.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t SPRJ_MOVE_MAP_LIST_STEP_SIZE = 0xf0;
inline constexpr std::size_t SPRJ_MOVE_MAP_LIST_STEP_DATA_REQUEST_OFFSET = 0xd0;
inline constexpr std::size_t SPRJ_MOVE_MAP_LIST_STEP_ATTACHMENT_OFFSET = 0xd8;

inline constexpr Rva SPRJ_MOVE_MAP_LIST_STEP_CONSTRUCTOR_FN{0x19c1a90};
inline constexpr Rva SPRJ_MOVE_MAP_LIST_STEP_DESTRUCTOR_FN{0x19c1ca0};
inline constexpr Rva SPRJ_MOVE_MAP_LIST_STEP_REGISTER_FN{0x19c2110};

enum class SprjMoveMapListStepIndex : std::int32_t {
    Init = 0,
    InitForData = 1,
    WaitForData = 2,
    Wait = 3,
    Finish = 4,
};

// Intrusive attachment maintained by the task owner. The native destructor
// unlinks this node through owner + 0x80, then fixes the adjacent previous and
// next links before clearing all three fields.
struct SprjMoveMapTaskAttachment {
    void* owner;
    void* previous;
    void* next;
};

// Generic move-map list preload task. STEP_Wait_forData owns the asynchronous
// request at +0xd0: it creates a request of native type 10, waits for request
// state 3, releases it and advances. Map choice and the actual move request
// belong to the surrounding debug/menu tasks.
struct SprjMoveMapListStep {
    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SPRJ_MOVE_MAP_LIST_STEP_RUNTIME_CLASS;
    static constexpr StepTemplateSymbol STEP_TEMPLATE = SPRJ_MOVE_MAP_LIST_STEP_TEMPLATE;

    Unknown<SPRJ_MOVE_MAP_LIST_STEP_DATA_REQUEST_OFFSET> _step_task;
    void* data_request;
    SprjMoveMapTaskAttachment attachment;
};

namespace detail::move_map_list_step_layout {
BB_SIZE(SprjMoveMapTaskAttachment, 0x18);
BB_SIZE(SprjMoveMapListStep, SPRJ_MOVE_MAP_LIST_STEP_SIZE);
BB_OFFSET(SprjMoveMapListStep, data_request, SPRJ_MOVE_MAP_LIST_STEP_DATA_REQUEST_OFFSET);
BB_OFFSET(SprjMoveMapListStep, attachment, SPRJ_MOVE_MAP_LIST_STEP_ATTACHMENT_OFFSET);
static_assert(SprjMoveMapListStep::STEP_TEMPLATE.steps_count == 5, "SprjMoveMapListStep step count");
}  // namespace detail::move_map_list_step_layout

}  // namespace bb
