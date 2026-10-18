// Native cloth-work owner stored at WorldChrMan + 0xa60.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct CSEzWork;
struct CSEzWorkCompletionHolder;

inline constexpr std::size_t CS_CLOTH_THREAD_SIZE = 0x88;
inline constexpr Rva CS_CLOTH_THREAD_CONSTRUCTOR_FN{0x1e39110};
inline constexpr Rva CS_CLOTH_THREAD_VTABLE{0x534f090};
inline constexpr Rva CS_CLOTH_THREAD_GET_RUNTIME_CLASS_FN{0x1e38f30};
inline constexpr Rva CS_CLOTH_THREAD_SIZE_FN{0x1e3a050};
inline constexpr Rva CS_CLOTH_THREAD_PRE_UPDATE_FN{0x1e392a0};
inline constexpr Rva CS_CLOTH_THREAD_POST_UPDATE_FN{0x1e39370};

// Owns a cloth executor and two inline registered callbacks.
//
// The constructor's vtable resolves CSClothThread metadata; its size method
// returns 0x88. Callback tasks at +0x18/+0x50 use the 0x38 variant and register
// in groups 0x2e (pre-cloth) and 0x37 (post-cloth). Both retain this object's
// address as their owner; a live native instance must not move.
struct CSClothThread {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CLOTH_THREAD_RUNTIME_CLASS;
    const void* vftable;
    CSEzWork* cloth_thread;
    CSEzWorkCompletionHolder* cloth_thread_state;
    SprjCallbackTask38 pre_cloth_task;
    SprjCallbackTask38 post_cloth_task;
};

namespace detail::cloth_thread_layout {
BB_SIZE(CSClothThread, CS_CLOTH_THREAD_SIZE);
BB_OFFSET(CSClothThread, cloth_thread, 0x08);
BB_OFFSET(CSClothThread, cloth_thread_state, 0x10);
BB_OFFSET(CSClothThread, pre_cloth_task, 0x18);
BB_OFFSET(CSClothThread, post_cloth_task, 0x50);
}  // namespace detail::cloth_thread_layout

}  // namespace bb
