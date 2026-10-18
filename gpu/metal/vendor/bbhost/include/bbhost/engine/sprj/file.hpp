// SPRJ file service, repository, and constructor seeds.
//
// Runtime registration, reflected sizes, constructors, and consumers were
// verified in eboot.bin. Static anchors are image RVAs.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4/file.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct SprjFileAllocationLimiter;

// Reflected 0x98-byte file service, published under singleton spelling
// SprjFile. Startup supplies a SprjFileRepository through SprjFileSeed; the
// FD4 base borrows it and leaves owned_repository/update_task null. A seed
// without a repository follows the FD4 fallback ownership path.
//
// EDF/ELD/EVD request helpers look up the full name in repository.files,
// create a missing 0xc8-byte cap, retain it, then dispatch new/existing-cap
// loading. Named ELD/EVD variants accept a separate lookup key and stored name
// override. Their return value is a file cap, not the underlying file.
// Teardown removes debug nodes in reverse order, then destroys the FD4 base.
struct SprjFileImp {
    static constexpr std::size_t SIZE = 0x98;
    static constexpr Rva SINGLETON_PTR = SPRJ_FILE_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x492fbad};
    static constexpr Rva NAME_STRING_UTF16{0x4951d44};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FILE_IMP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536ad20};
    static constexpr Rva CONSTRUCTOR_FN{0x14211b0};
    static constexpr Rva DESTRUCTOR_FN{0x1421310};
    static constexpr Rva REMOVE_DEBUG_NODES_FN{0x1421340};
    // Non-null cap: increment its non-atomic inherited count at +0x60.
    static constexpr Rva RETAIN_FILE_CAP_FN{0x1421470};
    static constexpr Rva RELEASE_FILE_CAP_FN = FD4FileManagerImp::RELEASE_FILE_CAP_FN;
    static constexpr Rva REQUEST_EDF_FN{0x1425710};
    static constexpr Rva REQUEST_ELD_FN{0x14258a0};
    static constexpr Rva REQUEST_NAMED_ELD_FN{0x1425a30};
    static constexpr Rva REQUEST_EVD_FN{0x1425b40};
    static constexpr Rva REQUEST_NAMED_EVD_FN{0x1425cd0};
    // Deduplicates under "PREFETCH:" plus the requested path, retains a
    // 0x88-byte FD4FileCap-compatible object, and dispatches on stream index 4.
    static constexpr Rva REQUEST_PREFETCH_FN{0x1426510};
    // Requests worker-mode bit 0 in the active repository's flags_204 byte.
    // The step's update applies the mode, copies bit 0 into bit 1, and clears
    // bit 0. Callers must reassert it to retain the requested mode next update.
    static constexpr Rva SET_REPOSITORY_FLAG_FN{0x14267f0};
    static constexpr Rva SET_FLAG_90_FN{0x1426800};
    static constexpr Rva GET_DEBUG_ROOT_FN{0x1426810};

    FD4FileManagerImp manager;
    std::uint32_t value_20;
    // Native defaults: [24, 32, 12, 48, 12, 16, 32, 12, 56, 12].
    std::uint32_t values_24[10];
    // Native defaults: [2, 2, 2, 2, 2, 6, 12, 4, 12, 2].
    std::uint32_t values_4c[10];
    Unknown<4> _unk74;
    void* debug_nodes[3];
    // Raw bytes initialized from a native predicate XOR 1; meaning unresolved.
    std::uint8_t flag_90;
    std::uint8_t flag_91;
    Unknown<2> _unk92;
    // Constructor initializes -1.
    std::int32_t value_94;
};

// Reflected 0x248-byte FD4FileRepository subclass. Startup allocates it
// separately and supplies it to the service; it is not the manager's owned
// fallback. Constructor clears base flags_204 bits 0/1 and adds debug menus.
// Teardown removes the five stream folders, queue folder, and root before base
// teardown frees streams, helper, lists, and holder bucket allocations.
struct SprjFileRepository {
    static constexpr std::size_t SIZE = 0x248;
    static constexpr Rva NAME_STRING{0x492fbb9};
    static constexpr Rva NAME_STRING_UTF16{0x495202c};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FILE_REPOSITORY_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x53210f0};
    static constexpr Rva CONSTRUCTOR_FN{0x14272a0};
    static constexpr Rva DESTRUCTOR_FN{0x1427b40};
    static constexpr Rva REMOVE_DEBUG_NODES_FN{0x1427b70};
    // Virtual +0x18 thunk to FD4FileRepository::UPDATE_STREAMS_FN.
    static constexpr Rva UPDATE_STREAMS_FN{0x1427e20};
    // Reconciles requested/previous mode bits with loader-worker states.
    // Consults native configuration File.EnableIngameMuiltithreadLoad.
    static constexpr Rva APPLY_WORKER_MODE_FN{0x1427e50};
    // Native debug callback uses indices 0..4 for one stream, 5 for all.
    static constexpr Rva TOGGLE_STREAM_DEBUG_FN{0x1428460};
    static constexpr Rva DISPLAY_STREAM_DEBUG_FN{0x1428500};

    FD4FileRepository repository;
    void* debug_root;
    void* debug_queue_folder;
    void* debug_stream_folders[5];
    // Diagnostic display toggles, individually addressable native bytes.
    std::uint8_t stream_debug_flags[5];
    // Toggling this broadcasts to all five; toggling one recomputes their AND.
    std::uint8_t all_stream_debug_flag;
    Unknown<2> _unk246;
};

// Reflected 0x30-byte FD4FileRepositorySeed subclass. Base allocator comes from
// native global state, with bucket counts 9973/97/257. These are constructor
// inputs; the seed itself is temporary stack storage at startup.
struct SprjFileRepositorySeed {
    static constexpr std::size_t SIZE = 0x30;
    static constexpr Rva NAME_STRING{0x492fbcc};
    static constexpr Rva NAME_STRING_UTF16{0x4952052};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FILE_REPOSITORY_SEED_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536af10};
    static constexpr Rva CONSTRUCTOR_FN{0x1429d80};
    static constexpr Rva GET_BUFFER_ALLOCATOR_FN{0x1429f60};

    FD4FileRepositorySeed seed;
    // Virtual +0xd0 uses this when non-null, otherwise a global allocator.
    void* buffer_allocator_override;
    // Optional allocation limiter, returned by virtual +0x40/+0x50. Its
    // presence enables admission/retirement callbacks at virtual +0x38/+0x48.
    SprjFileAllocationLimiter* loader_context;
};

// Reflected 0x20-byte FD4FileManagerSeed subclass, with no additional fields.
// Constructor obtains a task-group id from SprjTaskGroup (+0x18, then +0x5c)
// when available, otherwise zero. Startup assigns its separate repository
// through the inherited setter before constructing SprjFileImp.
struct SprjFileSeed {
    static constexpr std::size_t SIZE = 0x20;
    static constexpr Rva NAME_STRING{0x492fbe3};
    static constexpr Rva NAME_STRING_UTF16{0x4952080};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_FILE_SEED_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536b0b0};
    static constexpr Rva CONSTRUCTOR_FN{0x142aa70};
    static constexpr Rva SET_REPOSITORY_FN = FD4FileManagerSeed::SET_REPOSITORY_FN;

    FD4FileManagerSeed seed;
};

namespace detail::file_layout {
BB_SIZE(SprjFileImp, 0x98);
BB_SIZE(SprjFileImp, SprjFileImp::SIZE);
static_assert(alignof(SprjFileImp) == 8, "alignof(SprjFileImp)");
BB_OFFSET(SprjFileImp, manager, 0);
BB_OFFSET(SprjFileImp, value_20, 0x20);
BB_OFFSET(SprjFileImp, values_24, 0x24);
BB_OFFSET(SprjFileImp, values_4c, 0x4c);
BB_OFFSET(SprjFileImp, debug_nodes, 0x78);
BB_OFFSET(SprjFileImp, flag_90, 0x90);
BB_OFFSET(SprjFileImp, flag_91, 0x91);
BB_OFFSET(SprjFileImp, value_94, 0x94);
BB_SIZE(SprjFileRepository, 0x248);
BB_SIZE(SprjFileRepository, SprjFileRepository::SIZE);
static_assert(alignof(SprjFileRepository) == 8, "alignof(SprjFileRepository)");
BB_OFFSET(SprjFileRepository, repository, 0);
BB_OFFSET(SprjFileRepository, debug_root, 0x208);
BB_OFFSET(SprjFileRepository, debug_queue_folder, 0x210);
BB_OFFSET(SprjFileRepository, debug_stream_folders, 0x218);
BB_OFFSET(SprjFileRepository, stream_debug_flags, 0x240);
BB_OFFSET(SprjFileRepository, all_stream_debug_flag, 0x245);
BB_SIZE(SprjFileRepositorySeed, 0x30);
BB_SIZE(SprjFileRepositorySeed, SprjFileRepositorySeed::SIZE);
static_assert(alignof(SprjFileRepositorySeed) == 8, "alignof(SprjFileRepositorySeed)");
BB_OFFSET(SprjFileRepositorySeed, seed, 0);
BB_OFFSET(SprjFileRepositorySeed, buffer_allocator_override, 0x20);
BB_OFFSET(SprjFileRepositorySeed, loader_context, 0x28);
BB_SIZE(SprjFileSeed, 0x20);
BB_SIZE(SprjFileSeed, SprjFileSeed::SIZE);
static_assert(alignof(SprjFileSeed) == 8, "alignof(SprjFileSeed)");
BB_OFFSET(SprjFileSeed, seed, 0);
static_assert(SprjFileImp::SINGLETON_PTR.bn() == 0x0593b118, "SprjFileImp::SINGLETON_PTR");
static_assert(SprjFileImp::RELEASE_FILE_CAP_FN.bn() == 0x0137f1b0, "SprjFileImp::RELEASE_FILE_CAP_FN");
}  // namespace detail::file_layout

}  // namespace bb
