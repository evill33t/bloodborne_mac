// Native FD4 file-service bases (manager, seeds, repository, stream queues) used by the SPRJ file classes.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"

namespace bb {

// Constructors, consumers and teardown were inspected in eboot.bin. Memory
// views only: no ownership, queue iteration, synchronization or call ABI.

struct FD4FileRepository;
struct FD4FileRepositoryListNode;
struct FD4FileStreamQueue;

// The service borrows a supplied repository. Only a null seed repository
// creates an owned 0x208-byte fallback and a separately allocated update task.
// Native teardown unregisters/frees that task and destroys/frees the owned
// repository; it does not destroy a supplied repository.
struct FD4FileManagerImp {
    const void* vftable;
    FD4FileRepository* repository;
    FD4FileRepository* owned_repository;
    // Native 0x18-byte task, only present for the fallback repository path.
    void* update_task;

    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva VTABLE{0x52f0490};
    static constexpr Rva CONSTRUCTOR_FN{0xf7eec0};
    static constexpr Rva DESTRUCTOR_FN{0xf7f050};
    // Virtual +0x30 forwards to repository RELEASE_FILE_CAP_FN.
    static constexpr Rva RELEASE_FILE_CAP_FN{0xf7f1b0};
};

// Native constructor initializes repository to null. The task group is used
// only when the manager creates its fallback repository/update task.
struct FD4FileManagerSeed {
    const void* vftable;
    void* allocator;
    FD4FileRepository* repository;
    std::int32_t task_group_id;
    std::uint8_t _unk1c[4];

    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva VTABLE{0x52f0570};
    static constexpr Rva CONSTRUCTOR_FN{0xf7f560};
    static constexpr Rva SET_REPOSITORY_FN{0xf7f5d0};
};

// Bucket-count getters are virtual +0x20/+0x28/+0x30. The constructor
// initializes them to 9973, 97 and 257 respectively. Other configuration is
// supplied through virtual getters, not additional stored fields.
struct FD4FileRepositorySeed {
    const void* vftable;
    void* allocator;
    std::uint32_t file_bucket_count;
    std::uint32_t secondary_file_bucket_count;
    std::uint32_t resource_bucket_count;
    std::uint8_t _unk1c[4];

    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva VTABLE{0x52f0720};
    static constexpr Rva CONSTRUCTOR_FN{0xf82670};
};

// List view used twice in FD4FileRepository. Native teardown frees nodes and
// sentinel; a sentinel's payload is not an element.
struct FD4FileRepositoryList {
    std::uint8_t _unk00[8];
    FD4FileRepositoryListNode* sentinel;
    std::size_t count;
    void* allocator;
};

// pending_files payloads are FD4FileCap pointers; the payload type of
// list_1c8 has not been established.
struct FD4FileRepositoryListNode {
    FD4FileRepositoryListNode* next;
    FD4FileRepositoryListNode* previous;
    void* payload;
};

// Native 0x208-byte repository. Reflection links it to FD4ResRep, whose stored
// prefix is represented by FD4ResCap here. The three holders have independent
// bucket allocations; their teardown does not walk live caps.
struct FD4FileRepository {
    FD4ResCap res_cap;
    FD4ResCapHolder files;
    // Selected on release when file-cap byte +0x7a has bit 1 set.
    FD4ResCapHolder secondary_files;
    // Release searches this list for file caps in state 3.
    FD4FileRepositoryList pending_files;
    FD4FileStreamQueue* streams[5];
    void* work_groups[5];
    // Incremented by the native stream update, with native integer wrapping.
    std::uint32_t update_count;
    std::uint8_t _unk12c[4];
    // gettimeofday seconds * 1'000'000 + microseconds, written at FrameBegin.
    std::int64_t frame_begin_time_us;
    // Selects the row of values_13c/values_164 during native update; reset to 0.
    std::uint8_t flag_138;
    std::uint8_t _unk139[3];
    // Populated by seed virtual +0xc0 with (row, stream) arguments.
    std::uint32_t values_13c[2][5];
    // Populated by seed virtual +0xc8 with (row, stream) arguments.
    std::uint32_t values_164[2][5];
    std::uint8_t _unk18c[4];
    FD4ResCapHolder resources;
    // Separately allocated eight-byte polymorphic object, destroyed by base.
    void* helper_1b8;
    void* allocator_1c0;
    FD4FileRepositoryList list_1c8;
    // Seed virtual +0xa8; SPRJ's inherited getter returns seed allocator +8.
    void* allocator_1e8;
    std::uint8_t flag_1f0;
    std::uint8_t _unk1f1[3];
    std::uint32_t value_1f4;
    // Constructor initializes both floats to 0.3; purpose unresolved.
    float value_1f8;
    float value_1fc;
    // Constructor initializes this word to 0x10000.
    std::uint32_t value_200;
    // SPRJ clears the low two bits after base construction.
    std::uint8_t flags_204;
    std::uint8_t _unk205[3];

    static constexpr std::size_t SIZE = 0x208;
    static constexpr std::size_t STREAM_COUNT = 5;
    static constexpr Rva VTABLE{0x52f0640};
    static constexpr Rva CONSTRUCTOR_FN{0xf7f8d0};
    static constexpr Rva DESTRUCTOR_FN{0xf805f0};
    // Decrements/unlinks through the selected holder. A queued cap is marked
    // for deferred destruction; otherwise native destruction/free can occur
    // here, after removing a state-3 cap from pending_files if present.
    static constexpr Rva RELEASE_FILE_CAP_FN{0xf81880};
    // Consumes ready/cancelled queued caps. Marked caps receive virtual +0x18,
    // native cleanup, virtual destructor +8 and owning-heap free.
    static constexpr Rva UPDATE_STREAMS_FN{0xf81240};
    // Drains completed work, calls virtual +0x18, retires pending-list entries
    // and submits queued file work using the selected seed-limit table row.
    static constexpr Rva UPDATE_FN{0xf80850};
    static constexpr Rva RECORD_FRAME_BEGIN_TIME_FN{0xf81300};
    // Compares a selected queue's read and write indices. No native bounds
    // check or mutex acquisition was observed in this predicate.
    static constexpr Rva IS_STREAM_EMPTY_FN{0xf81390};
};

// 0x40-byte queue, allocated five times by the repository. The 0x18-byte
// mutex prefix is opaque. Native operations lock through virtual +0x18 and
// unlock through +0x28. Read advancement wraps by capacity; popped entries are
// not necessarily cleared, so non-null entries do not mark live ones.
struct FD4FileStreamQueue {
    std::uint8_t _mutex[0x18];
    std::int32_t read_index;
    std::int32_t write_index;
    std::int32_t capacity;
    std::uint8_t _unk24[4];
    FD4FileCap** entries;
    void* allocator;
    void* pointer_38;

    static constexpr std::size_t SIZE = 0x40;
    static constexpr Rva CONSTRUCTOR_FN{0x10359e0};
    static constexpr Rva DESTRUCTOR_FN{0x1035a70};
    static constexpr Rva POP_READY_FN{0x1035dc0};
    static constexpr Rva PUSH_FRONT_FN{0x1035e40};
};

namespace detail::fd4_file_layout {
BB_SIZE(FD4FileManagerImp, FD4FileManagerImp::SIZE);
static_assert(alignof(FD4FileManagerImp) == 8, "alignof(FD4FileManagerImp)");
BB_OFFSET(FD4FileManagerImp, repository, 8);
BB_OFFSET(FD4FileManagerImp, owned_repository, 0x10);
BB_OFFSET(FD4FileManagerImp, update_task, 0x18);
BB_SIZE(FD4FileManagerSeed, FD4FileManagerSeed::SIZE);
static_assert(alignof(FD4FileManagerSeed) == 8, "alignof(FD4FileManagerSeed)");
BB_OFFSET(FD4FileManagerSeed, allocator, 8);
BB_OFFSET(FD4FileManagerSeed, repository, 0x10);
BB_OFFSET(FD4FileManagerSeed, task_group_id, 0x18);
BB_SIZE(FD4FileRepositorySeed, FD4FileRepositorySeed::SIZE);
static_assert(alignof(FD4FileRepositorySeed) == 8, "alignof(FD4FileRepositorySeed)");
BB_OFFSET(FD4FileRepositorySeed, allocator, 8);
BB_OFFSET(FD4FileRepositorySeed, file_bucket_count, 0x10);
BB_OFFSET(FD4FileRepositorySeed, secondary_file_bucket_count, 0x14);
BB_OFFSET(FD4FileRepositorySeed, resource_bucket_count, 0x18);

BB_SIZE(FD4FileRepository, FD4FileRepository::SIZE);
static_assert(alignof(FD4FileRepository) == 8, "alignof(FD4FileRepository)");
BB_OFFSET(FD4FileRepository, res_cap, 0);
BB_OFFSET(FD4FileRepository, files, 0x68);
BB_OFFSET(FD4FileRepository, secondary_files, 0x90);
BB_OFFSET(FD4FileRepository, pending_files, 0xb8);
BB_OFFSET(FD4FileRepository, streams, 0xd8);
BB_OFFSET(FD4FileRepository, work_groups, 0x100);
BB_OFFSET(FD4FileRepository, update_count, 0x128);
BB_OFFSET(FD4FileRepository, frame_begin_time_us, 0x130);
BB_OFFSET(FD4FileRepository, flag_138, 0x138);
BB_OFFSET(FD4FileRepository, values_13c, 0x13c);
BB_OFFSET(FD4FileRepository, values_164, 0x164);
BB_OFFSET(FD4FileRepository, resources, 0x190);
BB_OFFSET(FD4FileRepository, helper_1b8, 0x1b8);
BB_OFFSET(FD4FileRepository, allocator_1c0, 0x1c0);
BB_OFFSET(FD4FileRepository, list_1c8, 0x1c8);
BB_OFFSET(FD4FileRepository, allocator_1e8, 0x1e8);
BB_OFFSET(FD4FileRepository, flag_1f0, 0x1f0);
BB_OFFSET(FD4FileRepository, value_1f4, 0x1f4);
BB_OFFSET(FD4FileRepository, value_1f8, 0x1f8);
BB_OFFSET(FD4FileRepository, value_1fc, 0x1fc);
BB_OFFSET(FD4FileRepository, value_200, 0x200);
BB_OFFSET(FD4FileRepository, flags_204, 0x204);

BB_SIZE(FD4FileRepositoryList, 0x20);
static_assert(alignof(FD4FileRepositoryList) == 8, "alignof(FD4FileRepositoryList)");
BB_OFFSET(FD4FileRepositoryList, sentinel, 8);
BB_OFFSET(FD4FileRepositoryList, count, 0x10);
BB_OFFSET(FD4FileRepositoryList, allocator, 0x18);
BB_SIZE(FD4FileRepositoryListNode, 0x18);
static_assert(alignof(FD4FileRepositoryListNode) == 8, "alignof(FD4FileRepositoryListNode)");
BB_OFFSET(FD4FileRepositoryListNode, previous, 8);
BB_OFFSET(FD4FileRepositoryListNode, payload, 0x10);
BB_SIZE(FD4FileStreamQueue, FD4FileStreamQueue::SIZE);
static_assert(alignof(FD4FileStreamQueue) == 8, "alignof(FD4FileStreamQueue)");
BB_OFFSET(FD4FileStreamQueue, read_index, 0x18);
BB_OFFSET(FD4FileStreamQueue, write_index, 0x1c);
BB_OFFSET(FD4FileStreamQueue, capacity, 0x20);
BB_OFFSET(FD4FileStreamQueue, entries, 0x28);
BB_OFFSET(FD4FileStreamQueue, allocator, 0x30);
BB_OFFSET(FD4FileStreamQueue, pointer_38, 0x38);
}  // namespace detail::fd4_file_layout

}  // namespace bb
