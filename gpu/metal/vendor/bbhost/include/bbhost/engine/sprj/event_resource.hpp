// SPRJ event file caps, repositories, resource caps, and parsed-data ownership.
//
// Native class names are reflected for the six caps and asserted for the three
// repositories. EventFileName and *RuntimeData are descriptive layout names.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct SprjEdfResCap;
struct SprjEldResCap;
struct SprjEvdResCap;
struct SprjEdfRuntimeData;
struct SprjEldRuntimeData;
struct SprjEvdRuntimeData;

// Descriptive 0x38-byte UTF-16 name view at file-cap +0x90. Constructors leave
// the leading eight bytes uncharacterized, set length 0, capacity 7,
// allocator, and flag 1, then copy an optional input name. Storage is eight
// inline UTF-16 units when capacity < 8; otherwise its first word is a native
// allocation pointer.
struct SprjEventFileName {
    Unknown<8> _unk00;
    std::uint8_t inline_or_heap[0x10];
    std::size_t length;
    std::size_t capacity;
    void* allocator;
    std::uint8_t flag_30;
    Unknown<7> _unk31;
};

// The three reflected 0xc8-byte FD4FileCap subclasses below share one shape.
// Loader pins the underlying native file, reads its buffer/size, normalizes
// the inherited resource name, then creates or retains a repository entry.
// The separate virtual +0x20 callback replaces its output name only when
// alternate_name is nonempty, and copies flag_30. Native destruction releases
// resource through repository +0x68, conditionally destroys/frees it, frees
// heap name storage, and destroys FD4FileCap.

// Reflected 0xc8-byte FD4FileCap subclass for EDF data.
struct SprjEdfFileCap {
    static constexpr std::size_t SIZE = 0xc8;
    static constexpr Rva NAME_STRING{0x492fb4b};
    static constexpr Rva NAME_STRING_UTF16{0x4951ba6};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_EDF_FILE_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536a760};
    // Optional UTF-16 name input; a null input is treated as empty.
    static constexpr Rva CONSTRUCTOR_FN{0x141a2f0};
    static constexpr Rva DEFAULT_CONSTRUCTOR_FN{0x141a410};
    static constexpr Rva DESTRUCTOR_FN{0x141a510};
    static constexpr Rva APPLY_NAME_OVERRIDE_FN{0x141a600};
    static constexpr Rva LOAD_RESOURCE_FN{0x141a680};

    FD4FileCap file_cap;
    SprjEdfResCap* resource;
    SprjEventFileName alternate_name;
};

// Reflected 0xc8-byte FD4FileCap subclass for ELD data.
struct SprjEldFileCap {
    static constexpr std::size_t SIZE = 0xc8;
    static constexpr Rva NAME_STRING{0x492fb5a};
    static constexpr Rva NAME_STRING_UTF16{0x4951bc4};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ELD_FILE_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536a850};
    // Optional UTF-16 name input; a null input is treated as empty.
    static constexpr Rva CONSTRUCTOR_FN{0x141b3e0};
    static constexpr Rva DEFAULT_CONSTRUCTOR_FN{0x141b500};
    static constexpr Rva DESTRUCTOR_FN{0x141b600};
    static constexpr Rva APPLY_NAME_OVERRIDE_FN{0x141b6f0};
    static constexpr Rva LOAD_RESOURCE_FN{0x141b770};

    FD4FileCap file_cap;
    SprjEldResCap* resource;
    SprjEventFileName alternate_name;
};

// Reflected 0xc8-byte FD4FileCap subclass for EVD data.
struct SprjEvdFileCap {
    static constexpr std::size_t SIZE = 0xc8;
    static constexpr Rva NAME_STRING{0x492fb69};
    static constexpr Rva NAME_STRING_UTF16{0x4951be2};
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_EVD_FILE_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x536a940};
    // Optional UTF-16 name input; a null input is treated as empty.
    static constexpr Rva CONSTRUCTOR_FN{0x141c4d0};
    static constexpr Rva DEFAULT_CONSTRUCTOR_FN{0x141c5f0};
    static constexpr Rva DESTRUCTOR_FN{0x141c6f0};
    static constexpr Rva APPLY_NAME_OVERRIDE_FN{0x141c7e0};
    static constexpr Rva LOAD_RESOURCE_FN{0x141c860};

    FD4FileCap file_cap;
    SprjEvdResCap* resource;
    SprjEventFileName alternate_name;
};

// The three repositories below share one shape: singletons allocated as 0x98
// bytes aligned to eight by RVA 0x1eebe10. Constructor builds FD4ResRep (same
// 0x68 prefix as FD4ResCap), a hash holder, and an optional debug context. Its
// inherited reflection getter returns FD4ResRep; no separate repository
// runtime-class object is claimed.
//
// Create-or-retain deduplicates by name, destroys an unused candidate on a
// hit, and increments the returned cap's non-atomic count. Parsing occurs only
// for a new cap. Destruction releases debug state and bucket storage, then the
// base; it does not walk buckets to release live resource caps.
struct SprjEdfRepository {
    static constexpr std::size_t SIZE = 0x98;
    static constexpr std::size_t INITIAL_BUCKET_COUNT = 0x11;
    static constexpr Rva SINGLETON_PTR = SPRJ_EDF_REPOSITORY_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4938803};
    static constexpr Rva VTABLE{0x53a9ee0};
    static constexpr Rva CONSTRUCTOR_FN{0x1ee70e0};
    static constexpr Rva DESTRUCTOR_FN{0x1ee7260};
    static constexpr Rva CREATE_OR_RETAIN_FN{0x1ee7300};

    FD4ResCap res_cap;
    FD4ResCapHolder resources;
    void* debug_context;
};

struct SprjEldRepository {
    static constexpr std::size_t SIZE = 0x98;
    static constexpr std::size_t INITIAL_BUCKET_COUNT = 0x11;
    static constexpr Rva SINGLETON_PTR = SPRJ_ELD_REPOSITORY_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x49387f1};
    static constexpr Rva VTABLE{0x53a9f90};
    static constexpr Rva CONSTRUCTOR_FN{0x1ee87c0};
    static constexpr Rva DESTRUCTOR_FN{0x1ee8940};
    static constexpr Rva CREATE_OR_RETAIN_FN{0x1ee89e0};

    FD4ResCap res_cap;
    FD4ResCapHolder resources;
    void* debug_context;
};

struct SprjEvdRepository {
    static constexpr std::size_t SIZE = 0x98;
    static constexpr std::size_t INITIAL_BUCKET_COUNT = 0x11;
    static constexpr Rva SINGLETON_PTR = SPRJ_EVD_REPOSITORY_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4938815};
    static constexpr Rva VTABLE{0x53aa040};
    static constexpr Rva CONSTRUCTOR_FN{0x1eea250};
    static constexpr Rva DESTRUCTOR_FN{0x1eea450};
    static constexpr Rva CREATE_OR_RETAIN_FN{0x1eea510};

    FD4ResCap res_cap;
    FD4ResCapHolder resources;
    void* debug_context;
};

// Reflected 0x70-byte FD4ResCap subclass, allocated with alignment eight. The
// repository initializes data to null and constructs an owned 0x18-byte
// parsed-data object only on insertion. Destruction releases that object's
// separate non-atomic reference, clears data, then destroys the cap base.
struct SprjEdfResCap {
    static constexpr Rva NAME_STRING{0x49387d5};
    static constexpr Rva NAME_STRING_UTF16{0x49ad7e8};
    static constexpr std::size_t SIZE = 0x70;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_EDF_RES_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x53513a0};
    static constexpr Rva DESTRUCTOR_FN{0x1ee78b0};

    FD4ResCap res_cap;
    SprjEdfRuntimeData* data;
};

// Same shape as SprjEdfResCap, for ELD data.
struct SprjEldResCap {
    static constexpr Rva NAME_STRING{0x49387e3};
    static constexpr Rva NAME_STRING_UTF16{0x49ad856};
    static constexpr std::size_t SIZE = 0x70;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ELD_RES_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x53513d0};
    static constexpr Rva DESTRUCTOR_FN{0x1ee8f90};

    FD4ResCap res_cap;
    SprjEldRuntimeData* data;
};

// Reflected 0x98-byte FD4ResCap subclass, allocated with alignment eight.
// BUILD_DEPENDENCIES_FN copies/indexes EMEVD data, looks up linked EVD caps,
// and separately retains their cap counts and parsed-data counts. It looks up
// EDF "common" and the same-name ELD cap without retaining those caps; the
// corresponding parsed-data objects are retained instead.
//
// Destruction releases debug_context and data (recursively releasing parsed
// dependencies when its count expires), decrements each linked cap's count
// directly, then frees the pointer array and destroys the base. Those direct
// decrements do not invoke the holder's unlink/destroy path. EDF/ELD cap
// pointers are borrowed and are not released by this destructor.
struct SprjEvdResCap {
    static constexpr Rva NAME_STRING{0x4938827};
    static constexpr Rva NAME_STRING_UTF16{0x49ad966};
    static constexpr std::size_t SIZE = 0x98;
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_EVD_RES_CAP_RUNTIME_CLASS;
    static constexpr Rva VTABLE{0x5351400};
    static constexpr Rva DESTRUCTOR_FN{0x1eeaae0};
    static constexpr Rva BUILD_DEPENDENCIES_FN{0x1eeabf0};

    FD4ResCap res_cap;
    SprjEvdRuntimeData* data;
    std::int32_t linked_resource_count;
    Unknown<4> _unk74;
    SprjEvdResCap** linked_resources;
    SprjEdfResCap* edf_resource;
    SprjEldResCap* eld_resource;
    void* debug_context;
};

// Descriptive 0x18-byte parsed EDF owner; not a reflected native name.
// Constructor copies the input into a 16-byte-aligned buffer and resolves
// file-relative offsets in place, so the copied bytes differ from the
// serialized file. Reference count starts at 1 and is non-atomic. RELEASE_FN
// decrements it and frees both buffer and owner when the prior count is below
// 2. The surrounding owner is allocated with alignment eight.
struct SprjEdfRuntimeData {
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva CONSTRUCTOR_FN{0x1ee6860};
    static constexpr Rva RELEASE_FN{0x1ee6d10};

    std::size_t source_size;
    std::uint8_t* data;
    std::int32_t reference_count;
    Unknown<4> _unk14;
};

// Same shape as SprjEdfRuntimeData, for ELD data.
struct SprjEldRuntimeData {
    static constexpr std::size_t SIZE = 0x18;
    static constexpr Rva CONSTRUCTOR_FN{0x1ee81e0};
    static constexpr Rva RELEASE_FN{0x1ee83a0};

    std::size_t source_size;
    std::uint8_t* data;
    std::int32_t reference_count;
    Unknown<4> _unk14;
};

// Descriptive 0x38-byte EMEVD owner, allocated with alignment eight.
// Constructor copies source data into a 16-byte-aligned allocation with an
// appended event-ID/index table. source_size describes the input, not the
// larger allocation. Unlike EDF/ELD, file offsets remain in the copied bytes.
//
// The linked_data array is separately allocated and initially zeroed.
// SprjEvdResCap::BUILD_DEPENDENCIES_FN fills it and the EDF/ELD pointers,
// retaining each parsed-data object. All reference counts here are
// non-atomic. Native destruction releases those references recursively, frees
// the linked array and copied buffer. It does not free this owner; callers do
// that after decrementing reference_count from a value below 2.
struct SprjEvdRuntimeData {
    static constexpr std::size_t SIZE = 0x38;
    static constexpr Rva COPY_AND_INDEX_FN{0x1ee9910};
    static constexpr Rva DESTRUCTOR_FN{0x1ee9c10};

    std::size_t source_size;
    std::uint8_t* data;
    std::int32_t linked_data_count;
    Unknown<4> _unk14;
    SprjEvdRuntimeData** linked_data;
    std::int32_t reference_count;
    Unknown<4> _unk24;
    SprjEdfRuntimeData* edf_data;
    SprjEldRuntimeData* eld_data;
};

namespace detail::event_resource_layout {
BB_SIZE(SprjEventFileName, 0x38);
static_assert(alignof(SprjEventFileName) == 8, "alignof(SprjEventFileName)");
BB_OFFSET(SprjEventFileName, inline_or_heap, 8);
BB_OFFSET(SprjEventFileName, length, 0x18);
BB_OFFSET(SprjEventFileName, capacity, 0x20);
BB_OFFSET(SprjEventFileName, allocator, 0x28);
BB_OFFSET(SprjEventFileName, flag_30, 0x30);

#define BB_EVENT_FILE_CAP(T)                         \
    BB_SIZE(T, T::SIZE);                             \
    static_assert(alignof(T) == 8, "alignof(" #T ")"); \
    BB_OFFSET(T, file_cap, 0);                       \
    BB_OFFSET(T, resource, 0x88);                    \
    BB_OFFSET(T, alternate_name, 0x90)
BB_EVENT_FILE_CAP(SprjEdfFileCap);
BB_EVENT_FILE_CAP(SprjEldFileCap);
BB_EVENT_FILE_CAP(SprjEvdFileCap);
#undef BB_EVENT_FILE_CAP

#define BB_EVENT_REPOSITORY(T)                       \
    BB_SIZE(T, T::SIZE);                             \
    static_assert(alignof(T) == 8, "alignof(" #T ")"); \
    BB_OFFSET(T, res_cap, 0);                        \
    BB_OFFSET(T, resources, 0x68);                   \
    BB_OFFSET(T, debug_context, 0x90)
BB_EVENT_REPOSITORY(SprjEdfRepository);
BB_EVENT_REPOSITORY(SprjEldRepository);
BB_EVENT_REPOSITORY(SprjEvdRepository);
#undef BB_EVENT_REPOSITORY

BB_SIZE(SprjEdfResCap, SprjEdfResCap::SIZE);
static_assert(alignof(SprjEdfResCap) == 8, "alignof(SprjEdfResCap)");
BB_OFFSET(SprjEdfResCap, res_cap, 0);
BB_OFFSET(SprjEdfResCap, data, 0x68);
BB_SIZE(SprjEldResCap, SprjEldResCap::SIZE);
static_assert(alignof(SprjEldResCap) == 8, "alignof(SprjEldResCap)");
BB_OFFSET(SprjEldResCap, res_cap, 0);
BB_OFFSET(SprjEldResCap, data, 0x68);
BB_SIZE(SprjEvdResCap, SprjEvdResCap::SIZE);
static_assert(alignof(SprjEvdResCap) == 8, "alignof(SprjEvdResCap)");
BB_OFFSET(SprjEvdResCap, res_cap, 0);
BB_OFFSET(SprjEvdResCap, data, 0x68);
BB_OFFSET(SprjEvdResCap, linked_resource_count, 0x70);
BB_OFFSET(SprjEvdResCap, linked_resources, 0x78);
BB_OFFSET(SprjEvdResCap, edf_resource, 0x80);
BB_OFFSET(SprjEvdResCap, eld_resource, 0x88);
BB_OFFSET(SprjEvdResCap, debug_context, 0x90);

BB_SIZE(SprjEdfRuntimeData, SprjEdfRuntimeData::SIZE);
static_assert(alignof(SprjEdfRuntimeData) == 8, "alignof(SprjEdfRuntimeData)");
BB_OFFSET(SprjEdfRuntimeData, source_size, 0);
BB_OFFSET(SprjEdfRuntimeData, data, 8);
BB_OFFSET(SprjEdfRuntimeData, reference_count, 0x10);
BB_SIZE(SprjEldRuntimeData, SprjEldRuntimeData::SIZE);
static_assert(alignof(SprjEldRuntimeData) == 8, "alignof(SprjEldRuntimeData)");
BB_OFFSET(SprjEldRuntimeData, source_size, 0);
BB_OFFSET(SprjEldRuntimeData, data, 8);
BB_OFFSET(SprjEldRuntimeData, reference_count, 0x10);
BB_SIZE(SprjEvdRuntimeData, SprjEvdRuntimeData::SIZE);
static_assert(alignof(SprjEvdRuntimeData) == 8, "alignof(SprjEvdRuntimeData)");
BB_OFFSET(SprjEvdRuntimeData, source_size, 0);
BB_OFFSET(SprjEvdRuntimeData, data, 8);
BB_OFFSET(SprjEvdRuntimeData, linked_data_count, 0x10);
BB_OFFSET(SprjEvdRuntimeData, linked_data, 0x18);
BB_OFFSET(SprjEvdRuntimeData, reference_count, 0x20);
BB_OFFSET(SprjEvdRuntimeData, edf_data, 0x28);
BB_OFFSET(SprjEvdRuntimeData, eld_data, 0x30);
static_assert(SprjEdfRepository::SINGLETON_PTR.bn() == 0x59402b0, "SprjEdfRepository::SINGLETON_PTR");
static_assert(SprjEldRepository::SINGLETON_PTR.bn() == 0x59402b8, "SprjEldRepository::SINGLETON_PTR");
static_assert(SprjEvdRepository::SINGLETON_PTR.bn() == 0x59402c0, "SprjEvdRepository::SINGLETON_PTR");
}  // namespace detail::event_resource_layout

}  // namespace bb
