// Native LOD manager, display records, and distance-bucket storage.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct LOD_BANK;
struct CSLodRecord;
struct CSLodBucket;
struct CSLodGroup;

// Sentinel-free circular record list. Empty head is null; a single record
// links to itself. Native links point at this header, including when it is
// embedded at bucket +0x18. Moving a live bucket would invalidate those links.
struct CSLodRecordList {
    CSLodRecord* head;
    // Null for the manager's default list; otherwise points to the bucket
    // containing this list header.
    CSLodBucket* bucket;
};

struct CSLodGroupNode {
    CSLodGroupNode* next;
    CSLodGroupNode* previous;
    CSLodGroup* group;
};

// Descriptive native list of group pointers, with a separate sentinel node.
struct CSLodGroupList {
    Unknown<8> _unk00;
    CSLodGroupNode* sentinel;
    std::size_t count;
    void* allocator;
};

// Native CSLod singleton, allocated as 0x50 bytes aligned to eight by the
// rendering owner. Singleton assertion strings identify the class; no
// DLRuntimeClass registration is claimed. Native update uses rendering state to
// select record levels and redistribute records between the default list and
// position-based distance buckets. Release unlinks a record, returns an
// in-pool index, or frees a heap fallback.
struct CSLod {
    static constexpr std::size_t SIZE = 0x50;
    static constexpr std::size_t RECORD_CAPACITY = 0x800;
    static constexpr std::size_t RECORD_POOL_BYTES = 0x48000;
    static constexpr Rva SINGLETON_PTR = CS_LOD_SINGLETON_PTR;
    static constexpr Rva INSTANCE_VTABLE{0x534cb00};
    static constexpr Rva NAME_STRING{0x49350bc};
    static constexpr Rva CONSTRUCTOR_FN{0x1d41540};
    static constexpr Rva DESTRUCTOR_FN{0x1d417b0};
    static constexpr Rva UPDATE_FN{0x1d41d60};
    static constexpr Rva ALLOCATE_RECORD_FN{0x1d42440};
    static constexpr Rva RELEASE_RECORD_FN{0x1d41ca0};
    static constexpr Rva MOVE_TO_DEFAULT_LIST_FN{0x1d42550};
    static constexpr Rva DEBUG_DRAW_LIST_FN{0x1d42790};

    const void* vftable;
    // Allocation holds 0x800 u16 indices. Constructor fills 0..0x7ff.
    std::uint16_t* free_indices;
    // Constructor initializes zero despite filling the index array. A later
    // enabling/reset write has not been recovered; do not assume capacity.
    std::uint32_t free_count;
    Unknown<4> _unk14;
    // Native allocation is 0x48000 bytes aligned to 16, with 0x90 stride.
    CSLodRecord* record_pool;
    // Separately allocated 0x10-byte owner, with a null bucket pointer.
    CSLodRecordList* default_records;
    CSLodGroupList groups;
    std::uint64_t value_48_bits;
};

// Descriptive record name; native allocation has no vtable. The display entity
// at SprjFD4ModelDispEntity +0x208 retains this pointer until teardown. The
// constructor initializes level/parameter/link/callback fields, but leaves
// bounds and transform to subsequent setters. Never move a linked record.
struct alignas(16) CSLodRecord {
    static constexpr std::size_t SIZE = 0x90;
    static constexpr Rva CONSTRUCTOR_FN{0x1d42aa0};
    static constexpr Rva UPDATE_LEVEL_FN{0x1d42ef0};
    static constexpr Rva DISTANCE_TO_TRANSITION_FN{0x1d42c20};
    static constexpr Rva SET_AXIS_ALIGNED_BOUNDS_FN{0x1d43060};
    static constexpr Rva INVALIDATE_BUCKET_FN{0x1d43400};

    // Observed levels are 0, 1, and 2; kept raw for incomplete native states.
    std::int32_t level;
    std::int32_t param_id;
    // Borrowed LodBankMan row (0x14 bytes); failed lookup or ID -1 leaves null
    // and level 0.
    const LOD_BANK* parameters;
    float half_extents[4];
    // Three basis vectors followed by the bounds center/translation vector.
    float transform[4][4];
    // Invoked when level changes; native register arguments not modeled.
    const void* level_changed_callback;
    void* callback_context;
    CSLodRecordList* owner;
    CSLodRecord* previous;
    CSLodRecord* next;
    Unknown<8> _unk88;
};

// Descriptive 0x38-byte group, allocated aligned to eight during update. Owns
// five separately allocated 0x28-byte buckets. Native cleanup frees those
// buckets after their records have been moved out by the caller.
struct CSLodGroup {
    static constexpr std::size_t SIZE = 0x38;
    static constexpr Rva CONSTRUCTOR_FN{0x1d439d0};
    static constexpr Rva DESTRUCTOR_FN{0x1d43b90};
    static constexpr Rva UPDATE_BUCKETS_FN{0x1d43c60};
    static constexpr Rva ASSIGN_RECORD_FN{0x1d43f10};

    float reference_position[4];
    CSLodBucket* buckets[5];
};

// Descriptive distance bucket. Constructor thresholds are 1, 3, 8, 16, and 50;
// the adjacent float stores the corresponding squared value. Update tests
// movement from reference_position against threshold before recomputing levels
// of linked records.
struct CSLodBucket {
    float reference_position[4];
    float threshold;
    float threshold_squared;
    CSLodRecordList records;
};

namespace detail::lod_layout {
BB_SIZE(CSLod, CSLod::SIZE);
static_assert(alignof(CSLod) == 8, "alignof(CSLod)");
BB_OFFSET(CSLod, free_indices, 8);
BB_OFFSET(CSLod, free_count, 0x10);
BB_OFFSET(CSLod, record_pool, 0x18);
BB_OFFSET(CSLod, default_records, 0x20);
BB_OFFSET(CSLod, groups, 0x28);
BB_OFFSET(CSLod, value_48_bits, 0x48);
static_assert(CSLod::RECORD_POOL_BYTES == CSLod::RECORD_CAPACITY * sizeof(CSLodRecord), "record pool");
static_assert(CSLod::RECORD_CAPACITY * sizeof(std::uint16_t) == 0x1000, "free index array");
BB_SIZE(CSLodRecord, CSLodRecord::SIZE);
static_assert(alignof(CSLodRecord) == 16, "alignof(CSLodRecord)");
BB_OFFSET(CSLodRecord, param_id, 4);
BB_OFFSET(CSLodRecord, parameters, 8);
BB_OFFSET(CSLodRecord, half_extents, 0x10);
BB_OFFSET(CSLodRecord, transform, 0x20);
BB_OFFSET(CSLodRecord, level_changed_callback, 0x60);
BB_OFFSET(CSLodRecord, callback_context, 0x68);
BB_OFFSET(CSLodRecord, owner, 0x70);
BB_OFFSET(CSLodRecord, previous, 0x78);
BB_OFFSET(CSLodRecord, next, 0x80);
BB_OFFSET(CSLodRecord, _unk88, 0x88);
// sizeof(LOD_BANK) == 0x14 is asserted in the param headers.
BB_SIZE(CSLodGroup, CSLodGroup::SIZE);
static_assert(alignof(CSLodGroup) == 8, "alignof(CSLodGroup)");
BB_OFFSET(CSLodGroup, buckets, 0x10);
BB_SIZE(CSLodBucket, 0x28);
static_assert(alignof(CSLodBucket) == 8, "alignof(CSLodBucket)");
BB_OFFSET(CSLodBucket, threshold, 0x10);
BB_OFFSET(CSLodBucket, threshold_squared, 0x14);
BB_OFFSET(CSLodBucket, records, 0x18);
BB_SIZE(CSLodRecordList, 0x10);
BB_OFFSET(CSLodRecordList, bucket, 8);
BB_SIZE(CSLodGroupList, 0x20);
BB_OFFSET(CSLodGroupList, sentinel, 8);
BB_OFFSET(CSLodGroupList, count, 0x10);
BB_OFFSET(CSLodGroupList, allocator, 0x18);
BB_SIZE(CSLodGroupNode, 0x18);
BB_OFFSET(CSLodGroupNode, previous, 8);
BB_OFFSET(CSLodGroupNode, group, 0x10);
}  // namespace detail::lod_layout

}  // namespace bb
