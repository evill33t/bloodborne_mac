// Entry-file-list repository, file loaders, and owned serialized resources.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/fd4.hpp"

namespace bb {

struct CSEntryfilelistResCap;

// Reflected 0x90-byte FD4FileCap subclass. Constructor RVA 0x19cba40 clears the
// resource pointer after constructing the 0x88 base. LOAD_RESOURCE_FN reads the
// native file buffer/size, normalizes its name, creates or retains a repository
// resource, and copies data only for a newly inserted resource. Destruction
// releases that reference, may destroy/free the resource, poisons the pointer
// with 0xdeadbeef, then destroys the file base.
struct CSEntryfilelistFileCap {
    // RUNTIME_CLASS: base FD4FileCap
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_ENTRYFILELIST_FILE_CAP_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x90;
    static constexpr Rva VTABLE{0x5395cb0};
    static constexpr Rva CONSTRUCTOR_FN{0x19cba40};
    static constexpr Rva DESTRUCTOR_FN{0x19cbb90};
    static constexpr Rva LOAD_RESOURCE_FN{0x19cbc50};

    FD4FileCap file_cap;
    CSEntryfilelistResCap* resource;
};

// Native spelling preserves lowercase bnd. This reflected 0x98-byte class has
// the same FD4FileCap base as the single-file cap, with an array/count tail.
// LOAD_RESOURCES_FN enumerates the loaded binder, creates or retains one
// repository resource per entry name, and copies newly inserted payloads.
// Destruction releases each non-null resource, frees the pointer array, and
// destroys the file base. The array has an eight-byte pointer stride.
struct CSEntryfilelistbndFileCap {
    // RUNTIME_CLASS: base FD4FileCap
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_ENTRYFILELISTBND_FILE_CAP_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x98;
    static constexpr Rva VTABLE{0x5395bc0};
    static constexpr Rva CONSTRUCTOR_FN{0x19ca760};
    static constexpr Rva DESTRUCTOR_FN{0x19ca810};
    static constexpr Rva LOAD_RESOURCES_FN{0x19ca990};

    FD4FileCap file_cap;
    CSEntryfilelistResCap** resources;
    std::uint32_t resource_count;
    Unknown<4> _unk94;
};

// Concrete singleton allocated as 0x90 bytes aligned to eight by RVA 0x201b750.
// Registration links to FD4ResRep, whose constructor adds no fields to the 0x68
// FD4ResCap prefix. The shorter CSEntryfilelistRepository assertion name refers
// to this singleton; no separate interface layout is established.
//
// The inline hash holder starts with 251 zeroed buckets. Create-or-retain paths
// deduplicate names, destroy the unused candidate on a hit, and increment the
// returned resource's non-atomic count. Release unlinks at zero only when the
// secondary resource word permits it, then destroys/frees the resource.
// Repository destruction frees bucket storage and its own resource-cap prefix;
// it does not walk buckets to release still-live resources.
struct CSEntryfilelistRepositoryImp {
    // RUNTIME_CLASS: base FD4ResRep
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_ENTRYFILELIST_REPOSITORY_IMP_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x90;
    static constexpr std::size_t INITIAL_BUCKET_COUNT = 0xfb;
    static constexpr Rva SINGLETON_PTR = CS_ENTRYFILELIST_REPOSITORY_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x493ad11};
    static constexpr Rva VTABLE{0x5333420};
    static constexpr Rva CONSTRUCTOR_FN{0x19cca60};
    static constexpr Rva DESTRUCTOR_FN{0x19ccb50};
    // Name and raw byte buffer input; copies only for a newly inserted cap.
    static constexpr Rva CREATE_OR_RETAIN_BYTES_FN{0x19ccbd0};
    // Serializes the supplied entry-list builder only for a new name. The
    // native SprjRapidReentryHelper publisher uses this after releasing its
    // prior named resource; other references can keep that entry alive.
    static constexpr Rva CREATE_OR_RETAIN_BUILDER_FN{0x19cccd0};
    static constexpr Rva RELEASE_RESOURCE_FN{0x19cce50};
    // Checks RespawnPoint.entryfilelistbnd: absent file or native state 4 succeeds.
    static constexpr Rva IS_RESPAWN_BUNDLE_READY_FN{0x19cceb0};

    FD4ResCap res_cap;
    FD4ResCapHolder resources;
};

// Reflected 0x78-byte FD4ResCap subclass, allocated with alignment eight by the
// repository's create-or-retain paths. Owns a copied/serialized byte buffer.
// Constructor paths initialize data/size to zero. REPLACE_BYTES_FN frees the
// previous buffer, allocates/copies the new one, and stores size only on
// success. Native destruction frees data then destroys the resource-cap prefix.
struct CSEntryfilelistResCap {
    // RUNTIME_CLASS: base FD4ResCap
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_ENTRYFILELIST_RES_CAP_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x78;
    static constexpr Rva VTABLE{0x5333450};
    static constexpr Rva DESTRUCTOR_FN{0x19cdb30};
    static constexpr Rva REPLACE_BYTES_FN{0x19cdbd0};

    FD4ResCap res_cap;
    std::uint8_t* data;
    std::size_t size;
};

namespace detail::entryfilelist_layout {
BB_SIZE(CSEntryfilelistFileCap, CSEntryfilelistFileCap::SIZE);
static_assert(alignof(CSEntryfilelistFileCap) == 8, "alignof(CSEntryfilelistFileCap)");
BB_OFFSET(CSEntryfilelistFileCap, file_cap, 0);
BB_OFFSET(CSEntryfilelistFileCap, resource, 0x88);
BB_SIZE(CSEntryfilelistbndFileCap, CSEntryfilelistbndFileCap::SIZE);
static_assert(alignof(CSEntryfilelistbndFileCap) == 8, "alignof(CSEntryfilelistbndFileCap)");
BB_OFFSET(CSEntryfilelistbndFileCap, file_cap, 0);
BB_OFFSET(CSEntryfilelistbndFileCap, resources, 0x88);
BB_OFFSET(CSEntryfilelistbndFileCap, resource_count, 0x90);
BB_SIZE(CSEntryfilelistRepositoryImp, CSEntryfilelistRepositoryImp::SIZE);
static_assert(alignof(CSEntryfilelistRepositoryImp) == 8, "alignof(CSEntryfilelistRepositoryImp)");
BB_OFFSET(CSEntryfilelistRepositoryImp, res_cap, 0);
BB_OFFSET(CSEntryfilelistRepositoryImp, resources, 0x68);
BB_SIZE(CSEntryfilelistResCap, CSEntryfilelistResCap::SIZE);
static_assert(alignof(CSEntryfilelistResCap) == 8, "alignof(CSEntryfilelistResCap)");
BB_OFFSET(CSEntryfilelistResCap, res_cap, 0);
BB_OFFSET(CSEntryfilelistResCap, data, 0x68);
BB_OFFSET(CSEntryfilelistResCap, size, 0x70);
static_assert(CSEntryfilelistbndFileCap::RUNTIME_CLASS.runtime_class_ptr.bn() == 0x05978330, "bnd runtime_class_ptr");
static_assert(CSEntryfilelistbndFileCap::RUNTIME_CLASS.runtime_class.bn() == 0x05978338, "bnd runtime_class");
static_assert(CSEntryfilelistbndFileCap::RUNTIME_CLASS.registration_function.bn() == 0x01dcb130, "bnd registration");
static_assert(CSEntryfilelistRepositoryImp::SINGLETON_PTR.bn() == 0x0593e8c0, "repository singleton");
}  // namespace detail::entryfilelist_layout

}  // namespace bb
