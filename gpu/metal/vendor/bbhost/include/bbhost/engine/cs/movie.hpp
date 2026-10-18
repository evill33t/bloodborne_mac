// CSMovie: the movie singleton, its requests, and MOVTAE file reloads.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"

namespace bb {

struct CSMovieIns;

// Exact 0x68-byte asserted singleton, allocated aligned to eight by startup.
// Owns one CSMovieIns and borrows the movie TAE data supplied by SprjFile.
// The active pointer aliases the instance while a request remains pending;
// it is not another allocation and does not prove a video frame is available.
// Native destruction frees the instance and unregisters the owner's task.
struct CSMovie {
    const void* vftable;
    // Conditional callback: value_18 is rearmed while playback/reload needs
    // another update, otherwise its dispatcher unregisters on the next visit.
    SprjCallbackTask38 update_task;
    CSMovieIns* instance;
    // Borrowed file-cap +0x90 payload for other:/MovTaeSPRJ.movtae.
    void* movie_tae;
    CSMovieIns* active_instance;
    void* debug_menu;
    std::int32_t debug_movie_index;
    // Defers unloading the TAE until no active request remains.
    std::uint8_t reload_requested;
    // Cleared only when a replacement file cap reaches ready state 4.
    std::uint8_t reload_pending;
    Unknown<2> _unk66;

    static constexpr std::size_t SIZE = 0x68;
    static constexpr Rva SINGLETON_PTR{0x5540490};
    static constexpr Rva NAME_STRING{0x4934522};
    static constexpr Rva MOVIE_TAE_PATH{0x49b8244};
    static constexpr Rva MOVIE_PATH_FORMAT{0x49b8276};
    static constexpr Rva VTABLE{0x5358d50};
    static constexpr Rva TASK_VTABLE{0x53af360};
    static constexpr Rva CONSTRUCTOR_FN{0x1fcac00};
    static constexpr Rva DESTRUCTOR_FN{0x1fcb220};
    static constexpr Rva UPDATE_FN{0x1fcaed0};
    // Selects a movie TAE entry, formats its path, and submits it to the
    // instance. Rejects a missing TAE/instance, reload, or active request.
    // Proven from disassembly: this routine fails Ghidra decompilation.
    static constexpr Rva REQUEST_FN{0x1fcb2e0};
    static constexpr Rva IS_ACTIVE_FN{0x1fcb2d0};
    // Requests ID 3 when DLC flag 3 or CSLocalize mode 1 is set, else ID 2.
    static constexpr Rva REQUEST_DLC_SELECTED_FN{0x1fcb760};
    static constexpr Rva DEBUG_ACTION_FN{0x1fcb820};
    static constexpr SprjTaskGroupIndex UPDATE_TASK_GROUP = SprjTaskGroupIndex::ResStep;

    // Native predicate: active_instance is nonnull. Does not dereference it
    // or assert that setup succeeded or a decoded frame is being displayed.
    bool is_active() const { return active_instance != nullptr; }
};

namespace detail::movie_layout {
BB_SIZE(CSMovie, CSMovie::SIZE);
static_assert(alignof(CSMovie) == 8, "alignof(CSMovie)");
BB_OFFSET(CSMovie, update_task, 0x08);
BB_OFFSET(CSMovie, update_task.registration, 0x18);
BB_OFFSET(CSMovie, update_task.value_18, 0x20);
BB_OFFSET(CSMovie, update_task.owner, 0x28);
BB_OFFSET(CSMovie, update_task.callback, 0x30);
BB_OFFSET(CSMovie, update_task.this_adjustment, 0x38);
BB_OFFSET(CSMovie, instance, 0x40);
BB_OFFSET(CSMovie, movie_tae, 0x48);
BB_OFFSET(CSMovie, active_instance, 0x50);
BB_OFFSET(CSMovie, debug_menu, 0x58);
BB_OFFSET(CSMovie, debug_movie_index, 0x60);
BB_OFFSET(CSMovie, reload_requested, 0x64);
BB_OFFSET(CSMovie, reload_pending, 0x65);
static_assert(static_cast<std::uint32_t>(CSMovie::UPDATE_TASK_GROUP) == 3, "CSMovie::UPDATE_TASK_GROUP");
}  // namespace detail::movie_layout

}  // namespace bb
