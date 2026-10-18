// WorldInfo area/block metadata, the WorldRes map-resource owner, and the WorldAreaChr/WorldBlockChr pools.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/sprj/chr_set.hpp"

namespace bb {

struct ChrIns;
struct FD4FileCap;
struct SprjWorldBlockNvmPrepare;

inline constexpr std::size_t WORLD_INFO_SIZE = 0x2b18;
inline constexpr std::size_t WORLD_INFO_AREA_INFO_OFFSET = 0x90;
inline constexpr std::size_t WORLD_INFO_AREA_INFO_COUNT = 0x14;
inline constexpr std::size_t WORLD_INFO_BLOCK_INFO_OFFSET = 0x590;
inline constexpr std::size_t WORLD_INFO_BLOCK_INFO_COUNT = 0x3c;
inline constexpr std::size_t WORLD_INFO_DEBUG_NODE_OFFSET = 0x2b10;
inline constexpr std::size_t WORLD_INFO_OWNER_PREFIX_SIZE = 0x30;
inline constexpr std::size_t WORLD_AREA_INFO_SIZE = 0x40;
inline constexpr std::size_t WORLD_BLOCK_INFO_SIZE = 0xa0;
inline constexpr std::size_t WORLD_AREA_CHR_SIZE = 0x20;
inline constexpr std::size_t WORLD_BLOCK_CHR_SIZE = 0x160;
inline constexpr std::size_t WORLD_RES_SIZE = 0x13830;
inline constexpr std::size_t WORLD_RES_AREA_OFFSET = 0x2b60;
inline constexpr std::size_t WORLD_RES_AREA_COUNT = 0x14;
inline constexpr std::size_t WORLD_RES_AREA_SIZE = 0x110;
inline constexpr std::size_t WORLD_RES_BLOCK_OFFSET = 0x40a0;
inline constexpr std::size_t WORLD_RES_BLOCK_COUNT = 0x3c;
inline constexpr std::size_t WORLD_RES_BLOCK_SIZE = 0x420;
inline constexpr std::size_t WORLD_RES_SELECTION_COUNT = 3;
inline constexpr std::size_t WORLD_RES_LOAD_CONTEXT_SIZE = 0xe0;
// Slots 0 and 1 remain selected after their blocks reach resident state 7.
inline constexpr std::size_t WORLD_RES_PERSISTENT_SELECTION_COUNT = 2;
// Slot 2 is cleared by WorldRes_Update as soon as its block reaches state 7.
inline constexpr std::size_t WORLD_RES_TRANSITION_SELECTION_INDEX = 2;
inline constexpr std::size_t WORLD_BLOCK_RES_STREAM_GROUP_COUNT = 3;
inline constexpr std::size_t WORLD_BLOCK_RES_STREAM_PARTITION_COUNT = 5;
inline constexpr std::size_t WORLD_BLOCK_RES_FILES_PER_PARTITION = 5;

inline constexpr Rva WORLD_RES_INITIALIZE_FROM_LOAD_LIST_FN{0x15635f0};
inline constexpr Rva WORLD_RES_UPDATE_FN{0x1565450};
inline constexpr Rva WORLD_RES_UPDATE_EVENT_RESOURCES_FN{0x15659f0};
inline constexpr Rva WORLD_RES_REQUEST_ALL_BLOCK_FILES_FN{0x1565f80};
inline constexpr Rva WORLD_RES_LOAD_CONTEXT_CONSTRUCTOR_FN{0x1547650};
inline constexpr Rva WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR{0x553b148};
inline constexpr Rva WORLD_AREA_RES_UPDATE_READINESS_FN{0x1551220};
inline constexpr Rva WORLD_AREA_RES_UPDATE_FN{0x1551390};
inline constexpr Rva WORLD_BLOCK_RES_UPDATE_FN{0x155a170};
inline constexpr Rva WORLD_BLOCK_RES_ADVANCE_LOAD_STATE_FN{0x155aae0};
inline constexpr Rva WORLD_BLOCK_RES_REQUEST_FILES_FN{0x15614e0};
inline constexpr Rva SPRJ_EMK_RES_MAN_RELEASE_AREA_EVENT_FILES_FN{0x1eec690};
inline constexpr Rva SPRJ_EMK_RES_MAN_REQUEST_BLOCK_EVENT_FILES_FN{0x1eec7d0};

namespace detail {
// min(len, cap), or 0 when the list pointer is null.
inline std::size_t world_counted_len(const void* ptr, std::size_t len, std::size_t cap) {
    return ptr ? (len < cap ? len : cap) : 0;
}
}  // namespace detail

struct WorldInfo;
struct WorldBlockInfo;
struct WorldBlockChr;

// Area metadata embedded in WorldInfo.
struct WorldAreaInfo {
    void* vftable;
    Unknown<3> _unk08;
    std::uint8_t area_number;
    WorldInfo* owner;
    std::uint32_t world_area_index;
    std::uint32_t world_block_index;
    std::uint32_t block_info_length;
    std::uint32_t _pad24;
    WorldBlockInfo* block_info;
    bool is_lock;
    Unknown<0x07> _pad31;
    std::uintptr_t _debug_node;  // raw debug/menu node pointer

    // The currently initialized block infos for this area (count, element).
    std::size_t block_info_count() const {
        return detail::world_counted_len(block_info, block_info_length, WORLD_INFO_BLOCK_INFO_COUNT);
    }
    WorldBlockInfo* block_info_at(std::size_t i) const;
};

// Block metadata embedded in WorldInfo.
struct WorldBlockInfo {
    void* vftable;
    std::uint32_t block_id;
    std::uint32_t _pad0c;
    Unknown<0x10> _unk10;
    WorldInfo* owner;
    WorldAreaInfo* world_area_info;
    std::uint32_t world_block_index;
    std::uint32_t area_block_index;
    std::uintptr_t _debug_node0;  // raw first debug/menu node pointer
    std::uint32_t block_id_copy;
    std::uint32_t _unk44;
    std::uint32_t _unk48;
    std::uint32_t _unk4c;
    std::uint32_t _unk50;
    std::uint32_t _unk54;
    Unknown<0x38> _unk58;
    bool is_lock;
    Unknown<0x07> _pad91;
    std::uintptr_t _debug_node1;  // raw second debug/menu node pointer
};

inline WorldBlockInfo* WorldAreaInfo::block_info_at(std::size_t i) const {
    return i < block_info_count() ? &block_info[i] : nullptr;
}

// Constructor-proven WorldInfo footprint. Only world_area_info_len /
// world_block_info_len entries of the inline pools are initialized.
struct WorldInfo {
    void* vftable;
    std::uint32_t world_area_info_len;
    std::uint32_t _pad0c;
    WorldAreaInfo* world_area_info_list_ptr;
    std::uint32_t world_block_info_len;
    std::uint32_t _pad1c;
    WorldBlockInfo* world_block_info_list_ptr;
    std::uint64_t _unk28;
    Unknown<0x50> _unk30;
    std::uint32_t _unk80;
    bool is_lock;
    Unknown<0x0b> _pad85;
    WorldAreaInfo world_area_info[WORLD_INFO_AREA_INFO_COUNT];
    WorldBlockInfo world_block_info[WORLD_INFO_BLOCK_INFO_COUNT];
    std::uintptr_t _debug_node;  // raw debug/menu node pointer at +0x2b10

    std::size_t area_info_count() const {
        return detail::world_counted_len(world_area_info_list_ptr, world_area_info_len, WORLD_INFO_AREA_INFO_COUNT);
    }
    WorldAreaInfo* area_info_at(std::size_t i) const {
        return i < area_info_count() ? &world_area_info_list_ptr[i] : nullptr;
    }
    std::size_t block_info_count() const {
        return detail::world_counted_len(world_block_info_list_ptr, world_block_info_len, WORLD_INFO_BLOCK_INFO_COUNT);
    }
    WorldBlockInfo* block_info_at(std::size_t i) const {
        return i < block_info_count() ? &world_block_info_list_ptr[i] : nullptr;
    }
    // fn(WorldAreaInfo&) for each initialized area; its blocks are reached
    // through WorldAreaInfo::block_info_at.
    template <typename Fn>
    void for_each_area(Fn&& fn) const {
        for (std::size_t i = 0, n = area_info_count(); i < n; ++i) fn(world_area_info_list_ptr[i]);
    }
};

// Validated prefix of WorldInfoOwner used by the character handle lookup.
struct WorldInfoOwner {
    Unknown<0x08> _unk00;
    std::int32_t world_area_chr_count;
    Unknown<0x0c> _unk0c;
    std::int32_t world_block_chr_count;
    std::uint32_t _unk1c;
    WorldAreaInfo* world_area_chr_info_ptr;
    std::uintptr_t _unk28;
};

// Descriptive states of WorldRes+0x2b50, observed in RVA 0x15659f0. The
// native field stays an i32. FilesReady permits missing files and does not
// guarantee registration: native common-resource activation has more gates.
enum class WorldResEventResourceState : std::int32_t {
    // With no request, clear the manager's WorldRes pointer. With a request,
    // bind this WorldRes, request common files, and enter WaitingForFiles.
    Idle = 0,
    // Wait for absent-or-state-4 file caps; cancellation enters ReleasePending.
    WaitingForFiles = 1,
    // Native activation was considered. Cancellation considers deactivation
    // and then enters ReleasePending; additional native gates can skip both.
    FilesReady = 2,
    // Release common files and return to Idle. The manager's WorldRes pointer
    // is cleared by the subsequent Idle update if no request is present.
    ReleasePending = 3,
};

// Observed values for the ten-state block resource lifecycle. Intermediate
// names describe lifecycle phase, not an unverified individual operation.
enum class WorldBlockResLoadState : std::int32_t {
    Unloaded = 0,
    BeginLoad = 1,
    Loading = 2,
    Preparing = 3,
    ActivationDecision = 4,
    FinalizingUnload = 5,
    Activating = 6,
    Resident = 7,
    DeleteWait = 8,
    BeginUnload = 9,
};

// Descriptive inline loading record at WorldBlockRes +0xd0, not a separately
// reflected class. The owner retains file caps and owns the two preparation
// allocations. SprjWorldBlockNvmPrepare only borrows its file inputs.
struct WorldBlockResLoadingState {
    Unknown<8> _unk00;
    // Passed to navigation preparation and borrowed by the navigation block.
    // Exact derived file-cap type remains unproven.
    FD4FileCap* context_file;
    Unknown<0x18> _unk10;
    FD4FileCap* nvmhktbnd_file;
    FD4FileCap* nva_file;
    Unknown<0x40> _unk38;
    // Separate 0x48-byte preparation object; native class name unproven.
    Unknown<0x48>* other_prepare;
    SprjWorldBlockNvmPrepare* navigation_prepare;
};

struct WorldRes;
struct WorldAreaRes;

// One block-level resource-loader record embedded in WorldRes. Owns three
// base files and three groups of files for five internal streaming
// partitions. The partition file matrix is constructor-proven as
// [group][partition][file]; the resource family of every group and column is
// still being named.
struct WorldBlockRes {
    void* vftable;
    WorldBlockInfo* world_block_info;
    WorldRes* owner;
    WorldAreaRes* parent_area;
    // Five native 0x20-byte spatial/streaming records.
    Unknown<0xa0> _stream_bounds;
    bool policy_active;
    // One-frame force bit. WorldRes_Update consumes it by setting selected.
    bool force_selected_once;
    // Vanilla's current block selection byte.
    bool selected;
    // Selection after area-readiness folding.
    bool area_requested;
    // Computed eligibility used by WorldBlockRes_Update.
    bool eligible;
    // Final desired-resident byte; requires the parent area to be active.
    bool desired_resident;
    std::uint16_t _padc6;
    std::int32_t load_state;  // WorldBlockResLoadState
    std::int32_t transition_delay;
    WorldBlockResLoadingState loading;
    bool base_files_pending;
    Unknown<0x07> _pad159;
    std::uintptr_t base_files[3];
    bool partition_files_pending;
    Unknown<0x07> _pad179;
    std::uintptr_t partition_files[WORLD_BLOCK_RES_STREAM_GROUP_COUNT][WORLD_BLOCK_RES_STREAM_PARTITION_COUNT]
                                  [WORLD_BLOCK_RES_FILES_PER_PARTITION];
    std::int32_t partition_ids[WORLD_BLOCK_RES_STREAM_PARTITION_COUNT];
    Unknown<0x14> _unk3ec;
    // Index into partition_ids; live Central Yharnam uses slot 0 for ID 27.
    std::int32_t active_partition_slot;
    std::uint32_t _unk404;
    std::uintptr_t _unk408;
    std::uintptr_t _unk410;
    std::uint16_t _unk418;
    bool patch_texture_files_enabled;
    Unknown<0x05> _tail41b;

    WorldBlockResLoadState state() const { return static_cast<WorldBlockResLoadState>(load_state); }
    bool is_resident() const { return load_state == static_cast<std::int32_t>(WorldBlockResLoadState::Resident); }
};

// One area-level resource-loader record embedded in WorldRes. The area state
// machine owns shared resources and drives each child block. desired is
// copied into requested before the block selection bytes are folded into the
// area decision. State 6 is the observed active state required before a child
// block can become resident.
struct WorldAreaRes {
    void* vftable;
    WorldAreaInfo* world_area_info;
    WorldRes* owner;
    std::uint32_t block_res_len;
    std::uint32_t _pad1c;
    WorldBlockRes* block_res_list_ptr;
    bool requested;
    bool has_pending_child;
    bool child_transition_complete;
    std::uint8_t _pad2b;
    std::int32_t load_state;
    bool resource_files_pending;
    Unknown<0x07> _pad31;
    // Twenty-four area-scoped SprjFile handles.
    std::uintptr_t resource_files[24];
    bool partition_files_pending;
    Unknown<0x07> _padf9;
    std::uintptr_t _unk100;
    // External desired-area input, folded together with child selections.
    bool desired;
    bool patch_texture_files_enabled;
    Unknown<0x06> _tail10a;

    std::size_t block_res_count() const {
        return detail::world_counted_len(block_res_list_ptr, block_res_len, WORLD_RES_BLOCK_COUNT);
    }
    WorldBlockRes* block_res_at(std::size_t i) const {
        return i < block_res_count() ? &block_res_list_ptr[i] : nullptr;
    }
};

// Fixed-capacity map-resource owner used by Bloodborne's field loader.
// MoveMapController selects an area load list, then WorldRes owns the
// corresponding live area and block resource records. The constructor embeds
// storage for at most 20 areas and 60 blocks; the two length fields report how
// much of each pool is currently initialized.
struct WorldRes {
    WorldInfo world_info;
    std::uint32_t world_area_res_len;
    std::uint32_t _pad2b1c;
    WorldAreaRes* world_area_res_list_ptr;
    float remaining_time_to_activation;
    float time_between_activations;
    std::uint32_t world_block_res_len;
    std::uint32_t _pad2b34;
    WorldBlockRes* world_block_res_list_ptr;
    // Native block IDs selected for residency. Not map IDs, and distinct from
    // the area/map records parsed by the load-list routine. Slots 0 and 1 are
    // persistent; slot 2 is a transition slot that WorldRes_Update clears to
    // -1 once its block reaches resident state 7. Vanilla can therefore
    // request three blocks during a transition but retains at most two.
    std::int32_t selection_block_ids[WORLD_RES_SELECTION_COUNT];
    // Set while the 0xe0-byte asynchronous load context owns this object.
    bool load_context_active;
    // Aggregate pending flag for partition-level requests at block +0x178.
    bool partition_requests_pending;
    // Aggregate pending flag for base block requests at block +0x158.
    bool base_block_requests_pending;
    // Requests the common event-resource lifecycle owned by SprjEmkResMan.
    bool event_resources_requested;
    std::int32_t event_resource_state;  // WorldResEventResourceState
    Unknown<0x0c> _unk2b54;
    WorldAreaRes world_area_res[WORLD_RES_AREA_COUNT];
    WorldBlockRes world_block_res[WORLD_RES_BLOCK_COUNT];
    Unknown<0x10> _tail13820;

    // The transient block ID cleared after its block first reaches state 7.
    std::int32_t transition_selection_block_id() const {
        return selection_block_ids[WORLD_RES_TRANSITION_SELECTION_INDEX];
    }
    // Currently initialized area/block records, capped to the embedded pools.
    std::size_t area_res_count() const {
        return detail::world_counted_len(world_area_res_list_ptr, world_area_res_len, WORLD_RES_AREA_COUNT);
    }
    WorldAreaRes* area_res_at(std::size_t i) const {
        return i < area_res_count() ? &world_area_res_list_ptr[i] : nullptr;
    }
    std::size_t block_res_count() const {
        return detail::world_counted_len(world_block_res_list_ptr, world_block_res_len, WORLD_RES_BLOCK_COUNT);
    }
    WorldBlockRes* block_res_at(std::size_t i) const {
        return i < block_res_count() ? &world_block_res_list_ptr[i] : nullptr;
    }
};

// The current asynchronous map-load context published at
// WORLD_RES_LOAD_CONTEXT_SINGLETON_PTR. The constructor proves the two
// WorldRes back-pointers, request inputs, nested helper, and vtable boundary;
// the remaining bytes stay opaque.
struct WorldResLoadContext {
    std::uintptr_t vtable;
    WorldRes* world_res_primary;
    WorldRes* world_res_secondary;
    void* request_owner;
    std::int32_t block_index;
    std::uint32_t unknown_024;
    void* request_data;
    Unknown<0x38> unknown_030;
    void* nested_load_helper;
    std::uint64_t unknown_070;
    std::uint64_t unknown_078;
    std::uint16_t unknown_080;
    std::uint8_t unknown_082;
    std::uint8_t unknown_083;
    std::int32_t unknown_084;
    Unknown<0x28> unknown_088;
    std::uintptr_t secondary_vtable;
    std::uint32_t unknown_0b8;
    std::uint32_t unknown_0bc;
    std::uint64_t unknown_0c0;
    std::uint16_t unknown_0c8;
    std::uint16_t unknown_0ca;
    Unknown<0x04> unknown_0cc;
    Unknown<0x10> unknown_0d0;
};

// Embedded WorldAreaChr entry.
struct WorldAreaChr {
    void* vftable;
    WorldAreaInfo* world_area_info;
    std::uint32_t _unk10;
    std::uint32_t _pad14;
    WorldBlockChr* world_block_chr;
};

// Embedded WorldBlockChr entry.
struct WorldBlockChr {
    void* vftable;
    WorldBlockInfo* world_block_info;
    WorldAreaChr* world_area_chr;
    Unknown<0xa0> _unk18;
    ChrSet<ChrIns> chr_set;
    Unknown<0x90> _unkd0;
};

namespace detail::world_info_layout {
BB_SIZE(WorldResEventResourceState, 4);
BB_SIZE(WorldInfo, WORLD_INFO_SIZE);
BB_SIZE(WorldAreaInfo, WORLD_AREA_INFO_SIZE);
BB_SIZE(WorldBlockInfo, WORLD_BLOCK_INFO_SIZE);
BB_OFFSET(WorldInfo, world_area_info_len, 0x08);
BB_OFFSET(WorldInfo, world_area_info_list_ptr, 0x10);
BB_OFFSET(WorldInfo, world_block_info_len, 0x18);
BB_OFFSET(WorldInfo, world_block_info_list_ptr, 0x20);
BB_OFFSET(WorldInfo, is_lock, 0x84);
BB_OFFSET(WorldInfo, world_area_info, WORLD_INFO_AREA_INFO_OFFSET);
BB_OFFSET(WorldInfo, world_block_info, WORLD_INFO_BLOCK_INFO_OFFSET);
BB_OFFSET(WorldInfo, _debug_node, WORLD_INFO_DEBUG_NODE_OFFSET);
BB_OFFSET(WorldAreaInfo, area_number, 0x0b);
BB_OFFSET(WorldAreaInfo, owner, 0x10);
BB_OFFSET(WorldAreaInfo, world_area_index, 0x18);
BB_OFFSET(WorldAreaInfo, world_block_index, 0x1c);
BB_OFFSET(WorldAreaInfo, block_info_length, 0x20);
BB_OFFSET(WorldAreaInfo, block_info, 0x28);
BB_OFFSET(WorldAreaInfo, is_lock, 0x30);
BB_OFFSET(WorldAreaInfo, _debug_node, 0x38);
BB_OFFSET(WorldBlockInfo, block_id, 0x08);
BB_OFFSET(WorldBlockInfo, _unk10, 0x10);
BB_OFFSET(WorldBlockInfo, owner, 0x20);
BB_OFFSET(WorldBlockInfo, world_area_info, 0x28);
BB_OFFSET(WorldBlockInfo, world_block_index, 0x30);
BB_OFFSET(WorldBlockInfo, area_block_index, 0x34);
BB_OFFSET(WorldBlockInfo, _debug_node0, 0x38);
BB_OFFSET(WorldBlockInfo, block_id_copy, 0x40);
BB_OFFSET(WorldBlockInfo, is_lock, 0x90);
BB_OFFSET(WorldBlockInfo, _debug_node1, 0x98);
BB_SIZE(WorldInfoOwner, WORLD_INFO_OWNER_PREFIX_SIZE);
BB_OFFSET(WorldInfoOwner, world_area_chr_count, 0x08);
BB_OFFSET(WorldInfoOwner, world_block_chr_count, 0x18);
BB_OFFSET(WorldInfoOwner, world_area_chr_info_ptr, 0x20);
BB_SIZE(WorldRes, WORLD_RES_SIZE);
BB_SIZE(WorldAreaRes, WORLD_RES_AREA_SIZE);
BB_SIZE(WorldBlockRes, WORLD_RES_BLOCK_SIZE);
BB_OFFSET(WorldRes, world_area_res_len, 0x2b18);
BB_OFFSET(WorldRes, world_area_res_list_ptr, 0x2b20);
BB_OFFSET(WorldRes, world_block_res_len, 0x2b30);
BB_OFFSET(WorldRes, world_block_res_list_ptr, 0x2b38);
BB_OFFSET(WorldRes, selection_block_ids, 0x2b40);
static_assert(offsetof(WorldRes, selection_block_ids) + WORLD_RES_TRANSITION_SELECTION_INDEX * 4 == 0x2b48,
              "WorldRes transition selection slot");
BB_OFFSET(WorldRes, load_context_active, 0x2b4c);
BB_OFFSET(WorldRes, partition_requests_pending, 0x2b4d);
BB_OFFSET(WorldRes, base_block_requests_pending, 0x2b4e);
BB_OFFSET(WorldRes, event_resources_requested, 0x2b4f);
BB_OFFSET(WorldRes, event_resource_state, 0x2b50);
BB_OFFSET(WorldRes, world_area_res, WORLD_RES_AREA_OFFSET);
BB_OFFSET(WorldRes, world_block_res, WORLD_RES_BLOCK_OFFSET);
BB_SIZE(WorldResLoadContext, WORLD_RES_LOAD_CONTEXT_SIZE);
BB_OFFSET(WorldResLoadContext, world_res_primary, 0x08);
BB_OFFSET(WorldResLoadContext, world_res_secondary, 0x10);
BB_OFFSET(WorldResLoadContext, request_owner, 0x18);
BB_OFFSET(WorldResLoadContext, block_index, 0x20);
BB_OFFSET(WorldResLoadContext, request_data, 0x28);
BB_OFFSET(WorldResLoadContext, nested_load_helper, 0x68);
BB_OFFSET(WorldResLoadContext, secondary_vtable, 0xb0);
BB_OFFSET(WorldAreaRes, world_area_info, 0x08);
BB_OFFSET(WorldAreaRes, owner, 0x10);
BB_OFFSET(WorldAreaRes, block_res_len, 0x18);
BB_OFFSET(WorldAreaRes, block_res_list_ptr, 0x20);
BB_OFFSET(WorldAreaRes, requested, 0x28);
BB_OFFSET(WorldAreaRes, load_state, 0x2c);
BB_OFFSET(WorldAreaRes, resource_files_pending, 0x30);
BB_OFFSET(WorldAreaRes, resource_files, 0x38);
BB_OFFSET(WorldAreaRes, partition_files_pending, 0xf8);
BB_OFFSET(WorldAreaRes, desired, 0x108);
BB_OFFSET(WorldAreaRes, patch_texture_files_enabled, 0x109);
BB_OFFSET(WorldBlockRes, world_block_info, 0x08);
BB_OFFSET(WorldBlockRes, owner, 0x10);
BB_OFFSET(WorldBlockRes, parent_area, 0x18);
BB_OFFSET(WorldBlockRes, policy_active, 0xc0);
BB_OFFSET(WorldBlockRes, force_selected_once, 0xc1);
BB_OFFSET(WorldBlockRes, selected, 0xc2);
BB_OFFSET(WorldBlockRes, area_requested, 0xc3);
BB_OFFSET(WorldBlockRes, eligible, 0xc4);
BB_OFFSET(WorldBlockRes, desired_resident, 0xc5);
BB_OFFSET(WorldBlockRes, load_state, 0xc8);
BB_OFFSET(WorldBlockRes, transition_delay, 0xcc);
BB_OFFSET(WorldBlockRes, loading, 0xd0);
BB_OFFSET(WorldBlockRes, base_files_pending, 0x158);
BB_OFFSET(WorldBlockRes, base_files, 0x160);
BB_OFFSET(WorldBlockRes, partition_files_pending, 0x178);
BB_OFFSET(WorldBlockRes, partition_files, 0x180);
BB_OFFSET(WorldBlockRes, partition_ids, 0x3d8);
BB_OFFSET(WorldBlockRes, active_partition_slot, 0x400);
BB_OFFSET(WorldBlockRes, patch_texture_files_enabled, 0x41a);
BB_SIZE(WorldBlockResLoadingState, 0x88);
static_assert(alignof(WorldBlockResLoadingState) == 8, "alignof(WorldBlockResLoadingState)");
BB_OFFSET(WorldBlockResLoadingState, context_file, 0x08);
BB_OFFSET(WorldBlockResLoadingState, nvmhktbnd_file, 0x28);
BB_OFFSET(WorldBlockResLoadingState, nva_file, 0x30);
BB_OFFSET(WorldBlockResLoadingState, other_prepare, 0x78);
BB_OFFSET(WorldBlockResLoadingState, navigation_prepare, 0x80);
static_assert(offsetof(WorldBlockRes, loading) + offsetof(WorldBlockResLoadingState, navigation_prepare) == 0x150,
              "WorldBlockRes navigation_prepare at +0x150");
BB_SIZE(WorldAreaChr, WORLD_AREA_CHR_SIZE);
BB_SIZE(WorldBlockChr, WORLD_BLOCK_CHR_SIZE);
BB_OFFSET(WorldBlockChr, world_block_info, 0x08);
BB_OFFSET(WorldBlockChr, world_area_chr, 0x10);
BB_OFFSET(WorldBlockChr, chr_set, 0xb8);
}  // namespace detail::world_info_layout

}  // namespace bb
