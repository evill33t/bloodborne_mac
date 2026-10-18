// World navigation ownership (SprjWorldNvmManager) and the parts consumed by CS path collection.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

struct CSCollectNearNaviMeshParts;
struct FD4FileCap;
struct WorldAreaInfo;
struct WorldInfo;

struct SprjNaviMeshParts;
struct SprjWorldBlockNvm;

// Descriptive names for SprjWorldBlockNvm::UPDATE_FN's six cases, not
// reflected enum names. Separate from WorldBlockResLoadState and the prepare step.
enum class SprjWorldBlockNvmState : std::int32_t {
    WaitingForArea = 0,
    Preparing = 1,
    Active = 2,
    UnloadRequested = 3,
    Unloading = 4,
    Unloaded = 5,
};

// Native debug name, constructor stride 0x20. Area initialization borrows the
// corresponding WorldAreaInfo and points into the manager's inline block pool.
struct SprjWorldAreaNvm {
    WorldAreaInfo* info;
    std::uint32_t block_count;
    Unknown<4> _unk0c;
    SprjWorldBlockNvm* blocks;
    // Constructor writes 5; state names remain unclassified.
    std::int32_t state_raw;
    std::uint16_t flags_raw;
    Unknown<2> _unk1e;
};

// Native debug name, constructor/destructor stride 0xb0. Blocks own a pointer
// array at +0x60; its entries borrow the manager's navigation-part pool.
// Cleanup resets those parts, frees the array, and releases both SprjFile
// caps. SprjWorldBlockNvmPrepare borrows this record and dispatches UPDATE_FN
// during setup. File caps at +0x40/+0x48 have references retained
// independently of WorldBlockRes; the context cap at +0x50 is borrowed
// without a retain.
struct alignas(16) SprjWorldBlockNvm {
    Unknown<0x30> _transform00;
    std::uint32_t block_id;
    std::uint32_t world_block_index;
    SprjWorldAreaNvm* area;
    FD4FileCap* nvmhktbnd_file;
    FD4FileCap* nva_file;
    FD4FileCap* context_file;
    std::uint32_t part_count;
    // Ascending during creation, descending during destruction; may be -1.
    std::int32_t part_cursor;
    SprjNaviMeshParts** parts;
    Unknown<0x28> _unk68;
    std::int32_t state_raw;  // SprjWorldBlockNvmState
    // Setup phases 0..7; allocation/part setup failure can write 8. The
    // preparation step's wait tests this as a signed integer > 5.
    std::int32_t setup_phase_raw;
    std::int32_t teardown_phase_raw;
    // Preparation callbacks write 1 after UPDATE_FN; wider consumers unproven.
    std::uint8_t prepare_update_marker;
    std::uint8_t reload_requested;
    std::uint8_t unload_requested;
    std::uint8_t _unk9f;
    // WaitingForArea increments this until the old value exceeds 253.
    std::int32_t area_wait_updates;
    Unknown<0x0c> _unka4;

    static constexpr Rva CLEANUP_FN{0x1e247f0};
    static constexpr Rva UPDATE_FN{0x1e24a60};
    static constexpr Rva DETACH_DEBUG_FN{0x1e27a70};
    static constexpr Rva BUILD_NVA_PATH_FN{0x1ac8a00};
    static constexpr Rva BUILD_NVMHKTBND_PATH_FN{0x1ac8be0};

    // Whether state_raw is one of UPDATE_FN's six cases.
    constexpr bool has_known_state() const { return state_raw >= 0 && state_raw <= 5; }
    SprjWorldBlockNvmState state() const { return static_cast<SprjWorldBlockNvmState>(state_raw); }
};

// Descriptive 0x60-byte connection row. Expansion matches destination_part_id
// and reverse_connection_index against the preceding part/exit. A reverse
// index of -1 matches any exit. Positive costs below the remaining budget are
// followed.
struct NaviMeshConnection {
    float position[4];
    std::uint32_t destination_part_id;
    std::int32_t reverse_connection_index;
    // Costs to the other connection indices inside this part; -1 initially.
    float distances[16];
    Unknown<8> _unk58;
};

// Debug-identified native part. Constructor RVA 0x1e1e820 initializes each
// 0x750-byte, 16-byte-aligned pool entry and all 16 connection records. No
// proven vtable/reflection record. Unknown resource and transform internals
// remain opaque.
//
// Query results borrow these objects. Source-descriptor +0x98 supplies the
// part ID used for semantic deduplication. The distance map's ordered key is
// the native part pointer, so its iteration order is not distance order.
struct alignas(16) SprjNaviMeshParts {
    Unknown<0x30> _transform00;
    std::int32_t state_raw;
    Unknown<4> _unk34;
    void* pointer_38;
    void* pointer_40;
    void* source_descriptor;
    Unknown<0xd8> _unk50;
    std::uint32_t identity_words_raw[3];
    std::int32_t slot_index_raw;
    std::int32_t connection_count;
    Unknown<4> _unk13c;
    NaviMeshConnection connections[16];
    // Constructor sets low two bits to 01, preserving the upper six.
    std::uint8_t flags_740;
    std::uint8_t flags_741;
    Unknown<14> _unk742;

    static constexpr std::size_t SIZE = 0x750;
    static constexpr Rva CONSTRUCTOR_FN{0x1e1e820};
    // Resets the live part and unregisters debug state without freeing its slot.
    static constexpr Rva CLEANUP_FN{0x1e1ef30};
};

// Descriptive pair accepted by the collector and the synchronous query.
struct NaviMeshLocation {
    std::uint32_t part_id;
    // Signed native index; its exact navigation-element terminology is unproven.
    std::int32_t element_index;

    static const NaviMeshLocation INVALID;

    // Native gate only; does not prove the ID/index resolves in loaded data.
    constexpr bool is_valid() const { return part_id != 0xffffffffu && element_index >= 0; }
};
inline constexpr NaviMeshLocation NaviMeshLocation::INVALID = {0xffffffffu, -1};

struct NaviMeshDistanceNode;

// Descriptive 0x20-byte tree header shared by collection and backread paths.
// Owns sentinel/nodes through allocator; node payloads borrow navigation
// parts. The leading word is unclassified.
struct NaviMeshDistanceMap {
    Unknown<8> _unk00;
    NaviMeshDistanceNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Native 0x30-byte node allocated aligned to eight. Sentinel's flag at +0x19
// is one, and its links point to itself when empty; its payload is not a result.
struct NaviMeshDistanceNode {
    NaviMeshDistanceNode* left;
    NaviMeshDistanceNode* parent;
    NaviMeshDistanceNode* right;
    std::uint8_t color_raw;
    std::uint8_t is_sentinel;
    Unknown<6> _unk1a;
    SprjNaviMeshParts* part;
    float distance;
    Unknown<4> _unk2c;
};

// Native singleton, allocated as 0x2ed0 bytes aligned to 16 by RVA 0x154b7e0
// and 0x19ba850. Assertions name SprjWorldNvmManager; its debug registration
// names the same instance SprjWorldNvmManagerImp. No runtime reflection or
// vtable at offset zero is inferred.
//
// Only area_count/block_count inline records are initialized. The areas/blocks
// pointers refer into these pools, and blocks point back into the area pool;
// live instances must not move. All 1024 parts in the separately allocated
// pool are constructed, with a 16-byte allocation cookie before the first.
//
// Native teardown deletes the collector before detaching debug/AI helpers,
// cleaning blocks and areas, and destroying the part pool and result tree.
struct alignas(16) SprjWorldNvmManager {
    SprjWorldAreaNvm area_pool[20];
    SprjWorldBlockNvm block_pool[60];
    WorldInfo* world_info;
    std::uint32_t area_count;
    Unknown<4> _unk2bcc;
    SprjWorldAreaNvm* areas;
    std::uint32_t block_count;
    Unknown<4> _unk2bdc;
    SprjWorldBlockNvm* blocks;
    // Owns 1024 elements; native deletion uses the cookie, not this pointer.
    SprjNaviMeshParts* part_pool;
    Unknown<0x60>* helper_2bf0;
    Unknown<0x10>* ai_debug_listener;
    CSCollectNearNaviMeshParts* collector;
    // Constructor writes 0x3ff and -1; meanings beyond pool bookkeeping open.
    std::int32_t pool_indices_raw[2];
    Unknown<0x60> _unk2c10;
    // Receives copies when debug byte +0x2c93 is enabled.
    NaviMeshDistanceMap debug_distances;
    Unknown<3> _unk2c90;
    std::uint8_t copy_distances_for_debug;
    Unknown<0x23c> _unk2c94;

    static constexpr std::size_t SIZE = 0x2ed0;
    static constexpr Rva SINGLETON_PTR{0x5540208};
    static constexpr Rva CONSTRUCTOR_FN{0x1e2b050};
    static constexpr Rva DESTRUCTOR_FN{0x1e2b890};
    static constexpr Rva REGISTER_DEBUG_FN{0x1e2f810};
    static constexpr Rva UNREGISTER_DEBUG_FN{0x1e327e0};
    static constexpr Rva FIND_PART_FN{0x1e2dab0};
    // Inlines the collector's pending-query stores; does not dispatch it.
    static constexpr Rva SUBMIT_COLLECT_QUERY_FN{0x1e2db60};
    // Dispatches through collector virtual +0xd0 using FD4Time with value
    // 30.0, then checks current step == Result. Does not loop to completion.
    static constexpr Rva UPDATE_COLLECT_QUERY_FN{0x1e2db90};
    static constexpr Rva COPY_COLLECT_RESULT_FN{0x1e2dc10};
    // Independent synchronous path used by WorldBackRead; does not drive the
    // owned collector. Its separate lower-bound global also contains zero.
    static constexpr Rva COLLECT_BACKREAD_PARTS_FN{0x1e2cfd0};
    static constexpr std::size_t PART_CAPACITY = 1024;
};

// Debug registration and singleton assertions identify the same allocation.
using SprjWorldNvmManagerImp = SprjWorldNvmManager;

namespace detail::world_nvm_layout {
BB_SIZE(SprjWorldNvmManager, 0x2ed0);
BB_SIZE(SprjWorldNvmManager, SprjWorldNvmManager::SIZE);
static_assert(alignof(SprjWorldNvmManager) == 16, "alignof(SprjWorldNvmManager)");
BB_OFFSET(SprjWorldNvmManager, area_pool, 0);
BB_OFFSET(SprjWorldNvmManager, block_pool, 0x280);
BB_OFFSET(SprjWorldNvmManager, world_info, 0x2bc0);
BB_OFFSET(SprjWorldNvmManager, area_count, 0x2bc8);
BB_OFFSET(SprjWorldNvmManager, areas, 0x2bd0);
BB_OFFSET(SprjWorldNvmManager, block_count, 0x2bd8);
BB_OFFSET(SprjWorldNvmManager, blocks, 0x2be0);
BB_OFFSET(SprjWorldNvmManager, part_pool, 0x2be8);
BB_OFFSET(SprjWorldNvmManager, helper_2bf0, 0x2bf0);
BB_OFFSET(SprjWorldNvmManager, ai_debug_listener, 0x2bf8);
BB_OFFSET(SprjWorldNvmManager, collector, 0x2c00);
BB_OFFSET(SprjWorldNvmManager, pool_indices_raw, 0x2c08);
BB_OFFSET(SprjWorldNvmManager, debug_distances, 0x2c70);
BB_OFFSET(SprjWorldNvmManager, copy_distances_for_debug, 0x2c93);
static_assert(SprjWorldNvmManager::PART_CAPACITY * sizeof(SprjNaviMeshParts) + 0x10 == 0x1d4010,
              "part pool allocation");
BB_SIZE(SprjWorldAreaNvm, 0x20);
BB_OFFSET(SprjWorldAreaNvm, block_count, 0x08);
BB_OFFSET(SprjWorldAreaNvm, blocks, 0x10);
BB_OFFSET(SprjWorldAreaNvm, state_raw, 0x18);
BB_OFFSET(SprjWorldAreaNvm, flags_raw, 0x1c);
BB_SIZE(SprjWorldBlockNvm, 0xb0);
BB_OFFSET(SprjWorldBlockNvm, block_id, 0x30);
BB_OFFSET(SprjWorldBlockNvm, world_block_index, 0x34);
BB_OFFSET(SprjWorldBlockNvm, area, 0x38);
BB_OFFSET(SprjWorldBlockNvm, nvmhktbnd_file, 0x40);
BB_OFFSET(SprjWorldBlockNvm, nva_file, 0x48);
BB_OFFSET(SprjWorldBlockNvm, context_file, 0x50);
BB_OFFSET(SprjWorldBlockNvm, part_count, 0x58);
BB_OFFSET(SprjWorldBlockNvm, part_cursor, 0x5c);
BB_OFFSET(SprjWorldBlockNvm, parts, 0x60);
BB_OFFSET(SprjWorldBlockNvm, state_raw, 0x90);
BB_OFFSET(SprjWorldBlockNvm, setup_phase_raw, 0x94);
BB_OFFSET(SprjWorldBlockNvm, teardown_phase_raw, 0x98);
BB_OFFSET(SprjWorldBlockNvm, prepare_update_marker, 0x9c);
BB_OFFSET(SprjWorldBlockNvm, reload_requested, 0x9d);
BB_OFFSET(SprjWorldBlockNvm, unload_requested, 0x9e);
BB_OFFSET(SprjWorldBlockNvm, area_wait_updates, 0xa0);
BB_SIZE(SprjNaviMeshParts, 0x750);
BB_SIZE(SprjNaviMeshParts, SprjNaviMeshParts::SIZE);
static_assert(alignof(SprjNaviMeshParts) == 16, "alignof(SprjNaviMeshParts)");
BB_OFFSET(SprjNaviMeshParts, state_raw, 0x30);
BB_OFFSET(SprjNaviMeshParts, pointer_38, 0x38);
BB_OFFSET(SprjNaviMeshParts, pointer_40, 0x40);
BB_OFFSET(SprjNaviMeshParts, source_descriptor, 0x48);
BB_OFFSET(SprjNaviMeshParts, identity_words_raw, 0x128);
BB_OFFSET(SprjNaviMeshParts, slot_index_raw, 0x134);
BB_OFFSET(SprjNaviMeshParts, connection_count, 0x138);
BB_OFFSET(SprjNaviMeshParts, connections, 0x140);
BB_OFFSET(SprjNaviMeshParts, flags_740, 0x740);
BB_OFFSET(SprjNaviMeshParts, flags_741, 0x741);
BB_SIZE(NaviMeshConnection, 0x60);
BB_OFFSET(NaviMeshConnection, destination_part_id, 0x10);
BB_OFFSET(NaviMeshConnection, reverse_connection_index, 0x14);
BB_OFFSET(NaviMeshConnection, distances, 0x18);
BB_SIZE(NaviMeshDistanceMap, 0x20);
BB_OFFSET(NaviMeshDistanceMap, sentinel, 0x08);
BB_OFFSET(NaviMeshDistanceMap, count, 0x10);
BB_OFFSET(NaviMeshDistanceMap, allocator, 0x18);
BB_SIZE(NaviMeshDistanceNode, 0x30);
static_assert(alignof(NaviMeshDistanceNode) == 8, "alignof(NaviMeshDistanceNode)");
BB_OFFSET(NaviMeshDistanceNode, parent, 0x08);
BB_OFFSET(NaviMeshDistanceNode, right, 0x10);
BB_OFFSET(NaviMeshDistanceNode, color_raw, 0x18);
BB_OFFSET(NaviMeshDistanceNode, is_sentinel, 0x19);
BB_OFFSET(NaviMeshDistanceNode, part, 0x20);
BB_OFFSET(NaviMeshDistanceNode, distance, 0x28);
BB_SIZE(NaviMeshLocation, 8);
BB_OFFSET(NaviMeshLocation, element_index, 4);
static_assert(!NaviMeshLocation::INVALID.is_valid(), "NaviMeshLocation::INVALID");
}  // namespace detail::world_nvm_layout

}  // namespace bb
