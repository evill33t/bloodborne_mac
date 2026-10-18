// CSMovieIns: the movie playback instance, its retained texture, and the
// temporary playback heap.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Named callbacks in registration order. Unnamed/terminal table slots are not
// enumerators; stored step values remain integers.
enum class CSMovieStepIndex : std::int32_t {
    Init = 0,
    WaitRequest = 1,
    InitSetup = 2,
    WaitSetup = 3,
    FinishSetup = 4,
    InitPlay = 5,
    WaitPlay = 6,
    FinishPlay = 7,
    Finish = 8,
};

// True and `out` set when `value` names a registered step.
inline constexpr bool from_raw(std::int32_t value, CSMovieStepIndex& out) {
    if (value < 0 || value > 8) return false;
    out = static_cast<CSMovieStepIndex>(value);
    return true;
}

// Descriptive 0x38-byte native wide-string wrapper used at +0x78 and +0xe0.
// Capacity <= 7 selects the inline 16-byte storage; larger capacity selects a
// pointer in that storage. Native teardown frees it through allocator.
struct CSMovieWideString {
    Unknown<8> _unk00;
    Unknown<0x10> inline_or_heap_storage;
    std::size_t length;
    std::size_t capacity;
    void* allocator;
    std::uint8_t flag_30;
    Unknown<7> _unk31;
};

// Descriptive 0x68-byte reference-counted texture adapter. When a decoded
// frame is accepted, its descriptor type byte must be 3. Invalid/null input
// clears frame but leaves copied metadata stale. Virtual getters fall back to
// fallback_texture when frame is null; the frame itself is borrowed.
struct CSMovieTexture {
    const void* vftable;
    std::int32_t reference_count;
    Unknown<4> _unk0c;
    // Retained from the native default-texture owner during construction.
    void* fallback_texture;
    void* frame;
    // Copies of frame +0x30/+0x38/+0x40; kept as raw native words.
    std::uint64_t frame_words[3];
    // Eleven scalar descriptor values copied by SET_FRAME_FN. Meanings beyond
    // their observed widths and source offsets remain unproven.
    std::uint32_t frame_metadata[11];
    Unknown<4> _unk64;

    static constexpr std::size_t SIZE = 0x68;
    static constexpr Rva VTABLE{0x53af3b0};
    static constexpr Rva SET_FRAME_FN{0x1fcc510};
    static constexpr Rva DESTRUCTOR_FN{0x1fcc4b0};
};

// Descriptive inline heap wrapper spanning CSMovieIns +0x170..+0x5f8. Setup
// attempts a 120 MiB allocation and uses its allocator interface at +0x18 when
// ready, otherwise the backend factory falls back to the global movie
// allocator. The arena internals and mutex remain opaque.
struct CSMoviePlaybackHeap {
    void* parent_allocator;
    std::uint32_t configuration;
    Unknown<4> _unk0c;
    void* allocation;
    const void* allocator_vftable;
    CSMoviePlaybackHeap* allocator_owner;
    Unknown<0x440> _arena;
    Unknown<0x18> _mutex;
    std::uint32_t allocation_flags;
    Unknown<4> _unk484;

    static constexpr std::size_t SIZE = 0x488;
    static constexpr std::size_t REQUESTED_BYTES = 0x7800000;
    static constexpr Rva INITIALIZE_FN{0x2965580};
    static constexpr Rva ALLOCATOR_VTABLE{0x52e70c0};
};

// Exact 0x5f8-byte SprjStepLocal subclass, allocated aligned to eight by
// CSMovie and its standalone factory. Its inline callback task invokes virtual
// +0xd0. Requests register that task in ResStep, and updates rearm it while
// pending.
//
// Init/WaitRequest/Setup/Play callbacks advance the local step. FinishPlay
// destroys the player/heap and requests WaitRequest with stop_requested set;
// WaitRequest then clears both request bytes. STEP_Finish itself is a no-op.
// Native destruction also removes the texture repository entry, releases the
// retained texture and fallback, and unregisters the task.
struct CSMovieIns {
    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _dispatcher;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    CSMovieWideString debug_string;
    // Optional ten-word debug counter array, allocated by RVA 0x1fd0050.
    std::int32_t* execution_counts;
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    CSMovieTexture* texture;
    // TexRepository registration for SYSTEX_MOVIE; released in destruction.
    void* texture_registration;
    // Native backend factory allocates 0x2e0 bytes. Internal class/layout is
    // not inferred from the virtual playback interface.
    void* player;
    CSMovieWideString movie_path;
    // Borrowed TAE entry selected by CSMovie; drives subtitle events.
    void* movie_tae_entry;
    // Initialized to 1.0; request copies a setting or fallback into this word,
    // which is passed to the backend's open options. Precise meaning unproven.
    float playback_value_120;
    std::uint8_t playback_flag_124;
    // Request stores this separately; the captured setup path reads +0x124.
    std::uint8_t playback_flag_125;
    Unknown<2> _unk126;
    // Negative values become INT32_MAX in the backend's open options.
    std::int32_t playback_option_128;
    Unknown<4> _unk12c;
    SprjCallbackTask38 update_task;
    // Set on accepted request; remains set through setup/playback/cleanup.
    std::uint8_t request_pending;
    // Cancellation or natural completion. WaitRequest clears both bytes.
    std::uint8_t stop_requested;
    Unknown<6> _unk16a;
    CSMoviePlaybackHeap playback_heap;

    static constexpr std::size_t SIZE = 0x5f8;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_MOVIE_INS_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_MOVIE_INS_TEMPLATE;
    static constexpr Rva VTABLE{0x5358dd0};
    static constexpr Rva TASK_VTABLE{0x53af5a0};
    static constexpr Rva CONSTRUCTOR_FN{0x1fcce70};
    static constexpr Rva DESTRUCTOR_FN{0x1fcd2e0};
    static constexpr Rva FACTORY_FN{0x1fcd8c0};
    static constexpr Rva UPDATE_FN{0x1fcd290};
    static constexpr Rva EXECUTE_FN{0x1fd0cf0};
    static constexpr Rva REQUEST_FN{0x1fcd7b0};
    static constexpr Rva REQUEST_STOP_FN{0x1fcd870};
    static constexpr Rva INIT_FN{0x1fcd910};
    static constexpr Rva WAIT_REQUEST_FN{0x1fcd930};
    static constexpr Rva INIT_SETUP_FN{0x1fcd980};
    static constexpr Rva WAIT_SETUP_FN{0x1fcdef0};
    static constexpr Rva FINISH_SETUP_FN{0x1fce210};
    static constexpr Rva INIT_PLAY_FN{0x1fd1d70};
    static constexpr Rva WAIT_PLAY_FN{0x1fce3b0};
    static constexpr Rva FINISH_PLAY_FN{0x1fce650};
    static constexpr Rva FINISH_FN{0x1fce820};
    static constexpr SprjTaskGroupIndex UPDATE_TASK_GROUP = SprjTaskGroupIndex::ResStep;

    // Direct request predicate. Owner-level requests additionally check the
    // movie TAE, owner reload flags, and owner's active pointer.
    bool can_accept_request() const { return request_pending == 0; }

    // Pure view of the native signed-option conversion in STEP_Init_Setup.
    std::int32_t effective_playback_option() const {
        return playback_option_128 < 0 ? INT32_MAX : playback_option_128;
    }
};

namespace detail::movie_ins_layout {
BB_SIZE(CSMovieIns, CSMovieIns::SIZE);
static_assert(alignof(CSMovieIns) == 8, "alignof(CSMovieIns)");
BB_OFFSET(CSMovieIns, callback_table, 0x08);
BB_OFFSET(CSMovieIns, current_step, 0x50);
BB_OFFSET(CSMovieIns, requested_step, 0x54);
BB_OFFSET(CSMovieIns, continue_this_update, 0x58);
BB_OFFSET(CSMovieIns, allocator, 0x60);
BB_OFFSET(CSMovieIns, debug_flags, 0x68);
BB_OFFSET(CSMovieIns, debug_menu, 0x70);
BB_OFFSET(CSMovieIns, debug_string, 0x78);
BB_OFFSET(CSMovieIns, execution_counts, 0xb0);
BB_OFFSET(CSMovieIns, execution_label, 0xb8);
BB_OFFSET(CSMovieIns, debug_step_requested, 0xc0);
BB_OFFSET(CSMovieIns, debug_step, 0xc4);
BB_OFFSET(CSMovieIns, texture, 0xc8);
BB_OFFSET(CSMovieIns, texture_registration, 0xd0);
BB_OFFSET(CSMovieIns, player, 0xd8);
BB_OFFSET(CSMovieIns, movie_path, 0xe0);
BB_OFFSET(CSMovieIns, movie_tae_entry, 0x118);
BB_OFFSET(CSMovieIns, playback_value_120, 0x120);
BB_OFFSET(CSMovieIns, playback_flag_124, 0x124);
BB_OFFSET(CSMovieIns, playback_flag_125, 0x125);
BB_OFFSET(CSMovieIns, playback_option_128, 0x128);
BB_OFFSET(CSMovieIns, update_task, 0x130);
BB_OFFSET(CSMovieIns, update_task.registration, 0x140);
BB_OFFSET(CSMovieIns, update_task.value_18, 0x148);
BB_OFFSET(CSMovieIns, update_task.owner, 0x150);
BB_OFFSET(CSMovieIns, update_task.callback, 0x158);
BB_OFFSET(CSMovieIns, update_task.this_adjustment, 0x160);
BB_OFFSET(CSMovieIns, request_pending, 0x168);
BB_OFFSET(CSMovieIns, stop_requested, 0x169);
BB_OFFSET(CSMovieIns, playback_heap, 0x170);
static_assert(static_cast<std::uint32_t>(CSMovieIns::UPDATE_TASK_GROUP) == 3, "CSMovieIns::UPDATE_TASK_GROUP");
BB_SIZE(CSMovieWideString, 0x38);
static_assert(alignof(CSMovieWideString) == 8, "alignof(CSMovieWideString)");
BB_OFFSET(CSMovieWideString, inline_or_heap_storage, 0x08);
BB_OFFSET(CSMovieWideString, length, 0x18);
BB_OFFSET(CSMovieWideString, capacity, 0x20);
BB_OFFSET(CSMovieWideString, allocator, 0x28);
BB_OFFSET(CSMovieWideString, flag_30, 0x30);
BB_SIZE(CSMovieTexture, CSMovieTexture::SIZE);
static_assert(alignof(CSMovieTexture) == 8, "alignof(CSMovieTexture)");
BB_OFFSET(CSMovieTexture, reference_count, 0x08);
BB_OFFSET(CSMovieTexture, fallback_texture, 0x10);
BB_OFFSET(CSMovieTexture, frame, 0x18);
BB_OFFSET(CSMovieTexture, frame_words, 0x20);
BB_OFFSET(CSMovieTexture, frame_metadata, 0x38);
BB_OFFSET(CSMovieTexture, _unk64, 0x64);
BB_SIZE(CSMoviePlaybackHeap, CSMoviePlaybackHeap::SIZE);
static_assert(alignof(CSMoviePlaybackHeap) == 8, "alignof(CSMoviePlaybackHeap)");
BB_OFFSET(CSMoviePlaybackHeap, configuration, 0x08);
BB_OFFSET(CSMoviePlaybackHeap, allocation, 0x10);
BB_OFFSET(CSMoviePlaybackHeap, allocator_vftable, 0x18);
BB_OFFSET(CSMoviePlaybackHeap, allocator_owner, 0x20);
BB_OFFSET(CSMoviePlaybackHeap, _arena, 0x28);
BB_OFFSET(CSMoviePlaybackHeap, _mutex, 0x468);
BB_OFFSET(CSMoviePlaybackHeap, allocation_flags, 0x480);
BB_OFFSET(CSMovieIns, playback_heap.allocation_flags, 0x5f0);
static_assert(CSMoviePlaybackHeap::REQUESTED_BYTES == 120 * 1024 * 1024, "REQUESTED_BYTES");
}  // namespace detail::movie_ins_layout

}  // namespace bb
