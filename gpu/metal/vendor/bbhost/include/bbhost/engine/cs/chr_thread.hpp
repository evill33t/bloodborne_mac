// Native character-work owner stored at WorldChrMan + 0xa58.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSEzWork;
struct CSEzWorkCompletionHolder;

inline constexpr std::size_t CS_CHR_THREAD_SIZE = 0x28;
inline constexpr Rva CS_CHR_THREAD_CONSTRUCTOR_FN{0x1a64a60};
inline constexpr Rva CS_CHR_THREAD_DESTRUCTOR_FN{0x1a64bf0};
inline constexpr Rva CS_CHR_THREAD_VTABLE{0x5335ae0};
inline constexpr Rva CS_CHR_THREAD_GET_RUNTIME_CLASS_FN{0x1a64880};
inline constexpr Rva CS_CHR_THREAD_SIZE_FN{0x1a662b0};
inline constexpr Rva CS_CHR_THREAD_QUEUE_CHR_VIRTUAL_D0_FN{0x1a64e80};
inline constexpr Rva CS_CHR_THREAD_QUEUE_CHR_VIRTUAL_E8_FN{0x1a65020};
// Queues native character callback RVA 0x18bfb00.
inline constexpr Rva CS_CHR_THREAD_QUEUE_CHR_CALLBACK_FN{0x1a651c0};
inline constexpr Rva CS_CHR_THREAD_QUEUE_CHR_VIRTUAL_108_FN{0x1a652d0};
// Queues SprjTargetBankManager::UPDATE_FN on the character executor.
inline constexpr Rva CS_CHR_THREAD_QUEUE_TARGET_BANK_FN{0x1a653e0};
// Queues SprjWorldAiManager::TICK_LUA_FN on the character executor.
inline constexpr Rva CS_CHR_THREAD_QUEUE_WORLD_AI_FN{0x1a655c0};
inline constexpr Rva CS_CHR_THREAD_WAKE_WORKERS_FN{0x1a65130};
// Waits for character completion, unreferences it, and clears the holder.
inline constexpr Rva CS_CHR_THREAD_WAIT_AND_CLEAR_COMPLETION_FN{0x1a65140};

// Owns character-update and back-initialization work executors.
//
// Constructor RVA 0x1a64a60 installs vtable RVA 0x5335ae0; its first virtual
// resolves this class's runtime metadata, whose size method returns 0x28. The
// separately allocated 0xe8 objects are CSEzWork instances named CSChrThread
// (mode 2) and CSChrBackInitialize (mode 1). State fields point to eight-byte
// completion holders, not counters. Character and manager submissions use
// 0x30-byte CSEzWorkMemberFragment objects borrowing their callback targets.
// WorldChrMan queues a batch, wakes the workers, and waits before dependent
// character-update phases.
struct CSChrThread {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CHR_THREAD_RUNTIME_CLASS;
    const void* vftable;
    CSEzWork* chr_thread;
    CSEzWorkCompletionHolder* chr_thread_state;
    CSEzWork* back_initialize_thread;
    CSEzWorkCompletionHolder* back_initialize_thread_state;
};

namespace detail::chr_thread_layout {
BB_SIZE(CSChrThread, CS_CHR_THREAD_SIZE);
BB_OFFSET(CSChrThread, chr_thread, 0x08);
BB_OFFSET(CSChrThread, chr_thread_state, 0x10);
BB_OFFSET(CSChrThread, back_initialize_thread, 0x18);
BB_OFFSET(CSChrThread, back_initialize_thread_state, 0x20);
}  // namespace detail::chr_thread_layout

}  // namespace bb
