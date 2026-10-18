// CSPlaygo: the PlayGo owner, its platform implementation, polling, and
// storage-state policy. CSPlaygo is asserted as a singleton; CSPlaygoImp is a
// descriptive name for its implementation object (no reflected class claimed).
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/sprj/task.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct CSPrefetchForSlowStorageStep;

inline constexpr std::size_t CS_PLAYGO_SIZE = 0x70;
inline constexpr std::size_t CS_PLAYGO_IMP_SIZE = 0x1a0;

inline constexpr std::size_t CS_PLAYGO_IMP_OFFSET = 0x08;
inline constexpr std::size_t CS_PLAYGO_UPDATE_TASK_OFFSET = 0x10;
inline constexpr std::size_t CS_PLAYGO_STATE_OFFSET = 0x48;
inline constexpr std::size_t CS_PLAYGO_SUB_STATE_OFFSET = 0x4c;
inline constexpr std::size_t CS_PLAYGO_PREFETCH_STEP_OFFSET = 0x50;
// Historical name; the member is a local prefetch step, not a queue.
inline constexpr std::size_t CS_PLAYGO_QUEUE_OFFSET = CS_PLAYGO_PREFETCH_STEP_OFFSET;
inline constexpr std::size_t CS_PLAYGO_FLAGS_OFFSET = 0x58;
inline constexpr std::size_t CS_PLAYGO_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET = 0x60;
inline constexpr std::size_t CS_PLAYGO_STATE_OVERRIDES_OFFSET = 0x68;
// Historical name; these words override the two storage states (-1 disables).
inline constexpr std::size_t CS_PLAYGO_SENTINELS_OFFSET = CS_PLAYGO_STATE_OVERRIDES_OFFSET;

// Names come directly from the native debug label table at RVA 0x53591c0.
// FastStorage is also the native fallback for a failed locus query or zero
// chunks; it does not by itself prove successful installation or polling.
enum class CSPlaygoStorageState : std::uint32_t {
    FirstChunk = 0,
    SlowStorage = 1,
    FastStorage = 2,
};

// True and `out` set when `value` names a storage state.
inline constexpr bool from_raw(std::uint32_t value, CSPlaygoStorageState& out) {
    if (value > 2) return false;
    out = static_cast<CSPlaygoStorageState>(value);
    return true;
}

// Native successful-query reduction, preserving input order. Locus 0 sets
// FirstChunk and locus 2 sets SlowStorage, even after an earlier 0. Other
// bytes leave the result unchanged; the initial result is FastStorage. This is
// not a minimum/worst-state calculation and does not query the platform.
inline constexpr CSPlaygoStorageState storage_state_from_loci(const std::uint8_t* loci, std::size_t count) {
    CSPlaygoStorageState state = CSPlaygoStorageState::FastStorage;
    for (std::size_t i = 0; i < count; ++i) {
        if (loci[i] == 0) state = CSPlaygoStorageState::FirstChunk;
        else if (loci[i] == 2) state = CSPlaygoStorageState::SlowStorage;
    }
    return state;
}

// Exact 0x1a0-byte implementation allocated aligned to eight by CSPlaygo.
// Construction allocates a 2 MiB work buffer (alignment 16), loads sysmodule
// 0x83, and calls scePlayGoInitialize. Open enumerates at most 100 chunk IDs,
// reads their loci, and registers poll_task on SystemStep (1) when state != 2.
// The task rearms itself while waiting or after a non-2 successful result.
// Task dispatcher RVA 0x159da40 clears value_18 before invoking a callback; if
// already zero it unregisters instead. Registration RVA 0x159da80 arms it.
//
// Its own native destructor frees work_memory and unregisters poll_task.
// CSPlaygo's observed destructor does not call this destructor or free imp.
struct CSPlaygoImp {
    const void* vftable;
    std::uint8_t* work_memory;
    std::uint32_t playgo_handle;
    std::uint16_t chunk_ids[100];
    std::uint8_t loci[100];
    // Written by scePlayGoGetChunkId; native consumers do not clamp it.
    std::uint32_t chunk_count;
    Unknown<4> _unk144;
    SprjCallbackTask38 poll_task;
    // time at implementation +0x188 is decremented by frame delta and reset
    // from POLL_INTERVAL_GLOBAL. Static initializer writes 30.0 seconds there.
    FD4Time poll_remaining;
    std::uint32_t storage_state;
    std::uint8_t opened;
    Unknown<3> _unk195;
    // Initially -1. Cached before scePlayGoSetInstallSpeed; failures do not
    // roll the cache back. Inputs other than 0/2 normalize to 1.
    std::int32_t last_install_speed;
    Unknown<4> _unk19c;

    static constexpr std::size_t SIZE = CS_PLAYGO_IMP_SIZE;
    static constexpr std::size_t CHUNK_CAPACITY = 100;
    static constexpr std::size_t WORK_MEMORY_SIZE = 0x200000;
    static constexpr Rva VTABLE{0x53591f0};
    static constexpr Rva POLL_TASK_VTABLE{0x53afd50};
    static constexpr Rva POLL_INTERVAL_GLOBAL{0x569e540};
    static constexpr Rva CONSTRUCTOR_FN{0x1fde730};
    static constexpr Rva DESTRUCTOR_FN{0x1fdea00};
    static constexpr Rva OPEN_FN{0x1fdead0};
    static constexpr Rva POLL_FN{0x1fde870};
    static constexpr Rva SET_INSTALL_SPEED_FN{0x1fdec10};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::SystemStep;
};

// Exact 0x70-byte owner. Its ResStep (3) callback refreshes sub_state, creates
// and drives a prefetch step only while sub_state == 1, selects install speed,
// clears both request bytes, and writes task value_18 = 1 to rearm execution.
// state is a separate snapshot refreshed by REFRESH_STATES_FN, not every tick.
//
// Native destruction removes its debug menu and unregisters update_task. It
// does not destroy imp or an outstanding prefetch_step.
struct CSPlaygo {
    void* vftable;
    CSPlaygoImp* imp;
    SprjCallbackTask38 update_task;
    // Snapshot/override slot 0, refreshed explicitly by REFRESH_STATES_FN.
    std::uint32_t state;
    // Current/override slot 1, refreshed by the ResStep update.
    std::uint32_t sub_state;
    CSPrefetchForSlowStorageStep* prefetch_step;
    // SprjFileStep::UPDATE_FN sets byte 0 to 1 if any of its five repository
    // streams is nonempty. Byte 1 takes precedence for install-speed request
    // 0; otherwise byte 0 selects request 1, and neither selects request 2.
    // CSPlaygo update consumes and clears both bytes.
    std::uint8_t flags[2];
    Unknown<0x06> _pad5a;
    void* primal_system_debug_menu;
    // Independent debug overrides for state/sub_state; -1 uses native state.
    std::int32_t state_overrides[2];

    static constexpr std::size_t SIZE = CS_PLAYGO_SIZE;
    static constexpr Rva SINGLETON_PTR = CS_PLAYGO_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493ad31};
    static constexpr Rva VTABLE{0x5359150};
    static constexpr Rva UPDATE_TASK_VTABLE{0x53afcb0};
    static constexpr Rva CONSTRUCTOR_FN{0x1fdd530};
    static constexpr Rva DESTRUCTOR_FN{0x1fdd940};
    static constexpr Rva UPDATE_FN{0x1fdd790};
    static constexpr Rva OPEN_FN{0x1fdd9d0};
    static constexpr Rva REFRESH_STATES_FN{0x1fdda00};
    // Writes native event flag 0xbd6078 from state == FirstChunk.
    static constexpr Rva WRITE_EVENT_FLAG_FN{0x1fddaa0};
    static constexpr Rva MARK_FILE_ACTIVITY_FN{0x1fddb10};
    static constexpr SprjTaskGroupIndex TASK_GROUP = SprjTaskGroupIndex::ResStep;

    // False for an index past the two flag bytes.
    bool is_flag_set(std::size_t index) const { return index < 2 && flags[index] != 0; }

    // Snapshot of the native request selection. Does not call the platform
    // API, normalize external requests, mutate the cache, or clear flags.
    std::uint32_t requested_install_speed() const { return flags[1] != 0 ? 0 : flags[0] != 0 ? 1 : 2; }
};

namespace detail::playgo_layout {
BB_SIZE(CSPlaygo, CS_PLAYGO_SIZE);
static_assert(alignof(CSPlaygo) == 8, "alignof(CSPlaygo)");
BB_OFFSET(CSPlaygo, imp, CS_PLAYGO_IMP_OFFSET);
BB_OFFSET(CSPlaygo, update_task, CS_PLAYGO_UPDATE_TASK_OFFSET);
BB_OFFSET(CSPlaygo, state, CS_PLAYGO_STATE_OFFSET);
BB_OFFSET(CSPlaygo, sub_state, CS_PLAYGO_SUB_STATE_OFFSET);
BB_OFFSET(CSPlaygo, prefetch_step, CS_PLAYGO_PREFETCH_STEP_OFFSET);
BB_OFFSET(CSPlaygo, flags, CS_PLAYGO_FLAGS_OFFSET);
BB_OFFSET(CSPlaygo, primal_system_debug_menu, CS_PLAYGO_PRIMAL_SYSTEM_DEBUG_MENU_OFFSET);
BB_OFFSET(CSPlaygo, state_overrides, CS_PLAYGO_STATE_OVERRIDES_OFFSET);
BB_SIZE(CSPlaygoImp, CS_PLAYGO_IMP_SIZE);
static_assert(alignof(CSPlaygoImp) == 8, "alignof(CSPlaygoImp)");
BB_OFFSET(CSPlaygoImp, work_memory, 8);
BB_OFFSET(CSPlaygoImp, playgo_handle, 0x10);
BB_OFFSET(CSPlaygoImp, chunk_ids, 0x14);
BB_OFFSET(CSPlaygoImp, loci, 0xdc);
BB_OFFSET(CSPlaygoImp, chunk_count, 0x140);
BB_OFFSET(CSPlaygoImp, poll_task, 0x148);
BB_OFFSET(CSPlaygoImp, poll_remaining, 0x180);
BB_OFFSET(CSPlaygoImp, poll_remaining.time, 0x188);
BB_OFFSET(CSPlaygoImp, storage_state, 0x190);
BB_OFFSET(CSPlaygoImp, opened, 0x194);
BB_OFFSET(CSPlaygoImp, last_install_speed, 0x198);
BB_OFFSET(CSPlaygo, update_task.registration, 0x20);
BB_OFFSET(CSPlaygo, update_task.value_18, 0x28);
BB_OFFSET(CSPlaygo, update_task.owner, 0x30);
BB_OFFSET(CSPlaygo, update_task.callback, 0x38);
BB_OFFSET(CSPlaygoImp, poll_task.registration, 0x158);
BB_OFFSET(CSPlaygoImp, poll_task.value_18, 0x160);
BB_OFFSET(CSPlaygoImp, poll_task.owner, 0x168);
BB_OFFSET(CSPlaygoImp, poll_task.callback, 0x170);
static_assert(static_cast<std::uint32_t>(CSPlaygo::TASK_GROUP) == 3, "CSPlaygo::TASK_GROUP");
static_assert(static_cast<std::uint32_t>(CSPlaygoImp::TASK_GROUP) == 1, "CSPlaygoImp::TASK_GROUP");
}  // namespace detail::playgo_layout

}  // namespace bb
