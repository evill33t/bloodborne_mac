// FD4 runtime pieces: delayed deletion, task time, hash strings, resource caps and param files.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t FD4_TIME_SIZE = 0x10;
inline constexpr std::size_t FD4_TASK_DATA_PREFIX_SIZE = 0x10;
inline constexpr std::size_t FD4_BASIC_HASH_STRING_SIZE = 0x40;
inline constexpr std::size_t FD4_RESOURCE_NAME_SIZE = 0x48;
inline constexpr std::size_t FD4_RES_CAP_SIZE = 0x68;
inline constexpr std::size_t FD4_FILE_CAP_SIZE = 0x88;
inline constexpr std::size_t FD4_RES_CAP_HOLDER_SIZE = 0x28;
inline constexpr std::size_t FD4_PARAM_RES_CAP_SIZE = 0x78;
inline constexpr std::size_t PARAM_FILE_BASE_HEADER_SIZE = 0x34;

inline constexpr Rva FD4_DELAY_DELETE_MANAGER_SINGLETON{0x54b1d10};
inline constexpr std::size_t FD4_DELAY_DELETE_MANAGER_SIZE = 0x4a8;
inline constexpr Rva FD4_DELAY_DELETE_MANAGER_CONSTRUCT_FN{0x0f5ce30};
inline constexpr Rva FD4_DELAY_DELETE_MANAGER_DESTRUCT_FN{0x0f5d040};
inline constexpr Rva FD4_DELAY_DELETE_MANAGER_UPDATE_FN{0x0f5d200};
inline constexpr Rva FD4_DELAY_DELETE_MANAGER_ENQUEUE_FN{0x0f5d2b0};
// Descriptor object initialized with vtable RVA 0x532e0a0 by native startup.
inline constexpr Rva CHR_DELAY_DELETE_CALLBACK{0x556d5d0};
// Callback virtual +0x10: actor virtual destructor +8, then owning-heap free.
inline constexpr Rva CHR_DELAY_DELETE_DESTROY_AND_FREE_FN{0x18349e0};

struct FD4DelayDeleteQueue;
struct FD4DelayDeleteNode;
struct FD4DelayDeleteCallback;
struct FD4ResCapHolder;

// Shared native delayed-deletion owner used by WorldChrMan and other systems.
// Allocation size and tail fields verified in RVA 0x20270f0 / RVA 0x0f5ce30.
// Does not synchronize access to the queue.
struct FD4DelayDeleteManager {
    Unknown<0x498> _unk000;
    void* queue_allocator;
    FD4DelayDeleteQueue* queue;
};

// Native list owner allocated separately from the manager.
struct FD4DelayDeleteQueue {
    Unknown<0x08> _unk00;
    FD4DelayDeleteNode* sentinel;
    std::uint64_t count;
    void* allocator;
};

// A 0x28-byte native list node. Sentinel payload fields are not valid work.
struct FD4DelayDeleteNode {
    FD4DelayDeleteNode* next;
    FD4DelayDeleteNode* previous;
    FD4DelayDeleteCallback* callback;
    void* object;
    // Update decrements this, then executes when the previous value <= 0.
    // Thus initial 1 requires two manager updates, not a timed wait.
    std::int32_t remaining_updates;
    Unknown<0x04> _unk24;
};

// Polymorphic descriptor. The queue calls its vtable +0x10 with the object.
// Callback lifetime is external to the queue; actor callback storage is static.
struct FD4DelayDeleteCallback {
    const void* vtable;
};

// Renamed from ObservedStringEncoding: fd4, frpg, cs and sprj each define one.
enum class ObservedStringEncoding_fd4 {
    Ascii,
    Utf16,
};

struct ObservedDebugMenuString {
    const char* value;
    Rva address;
    ObservedStringEncoding_fd4 encoding;
};

inline constexpr const char* OBSERVED_DEBUG_CLASSES[] = {
    "FD4DebugMenuManager",
    "FD4DebugMenuReportSystem",
    "FD4DebugMenuShareStringManager",
};

inline constexpr ObservedDebugMenuString OBSERVED_DEBUG_MENU_STRINGS[] = {
    {"Core.IO.Alias.cap_debugmenu", Rva{0x49bb5da}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu", Rva{0x48b99bc}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu Guide", Rva{0x48b9958}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu/OldNode", Rva{0x48b98c6}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu/SubWindow", Rva{0x48ba350}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenuLayout", Rva{0x48bb558}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenuReport_id", Rva{0x48ba3dc}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu_Open", Rva{0x48ba3a4}, ObservedStringEncoding_fd4::Utf16},
    {"DebugMenu_ReachedNode", Rva{0x48ba378}, ObservedStringEncoding_fd4::Utf16},
    {"FD4/Debug/DebugMenu", Rva{0x48b9994}, ObservedStringEncoding_fd4::Utf16},
    {"FD4/Debug/DebugMenu/Control Guide", Rva{0x48b9914}, ObservedStringEncoding_fd4::Utf16},
    {"FD4/Debug/DebugMenu/Layout", Rva{0x48b9aee}, ObservedStringEncoding_fd4::Utf16},
    {"SPRJ.DebugMenu.DoesSystemValidate", Rva{0x49bd64a}, ObservedStringEncoding_fd4::Utf16},
    {"SPRJ.DebugMenu.DoesUseNewDebugMenu", Rva{0x49b6efa}, ObservedStringEncoding_fd4::Utf16},
    {"cap_debugmenu:/bookmark.txt", Rva{0x49bd68e}, ObservedStringEncoding_fd4::Utf16},
    {"capture:/debugmenu", Rva{0x49bb612}, ObservedStringEncoding_fd4::Utf16},
};

// Debug-menu singletons: markers only, no layout claimed.
struct FD4DebugMenuManager {
    static constexpr Rva SINGLETON_PTR = FD4_DEBUG_MENU_MANAGER_SINGLETON_PTR;
};
struct FD4DebugMenuReportSystem {
    static constexpr Rva SINGLETON_PTR = FD4_DEBUG_MENU_REPORT_SYSTEM_SINGLETON_PTR;
};
struct FD4DebugMenuShareStringManager {
    static constexpr Rva SINGLETON_PTR = FD4_DEBUG_MENU_SHARE_STRING_MANAGER_SINGLETON_PTR;
};

// FD4 time layout from the DS3/Sekiro-era runtime family.
struct FD4Time {
    std::uintptr_t vftable;
    float time;
};

// Verified prefix of the frame data passed to task callbacks (their second
// argument). Callback RVA 0x19381e0 reads the frame delta from +0x08. The
// scheduler may append more fields; those are not modeled.
struct FD4TaskDataPrefix {
    FD4Time delta_time;
};

// FD4 hash-string layout. The inner string is opaque; the total shape is
// anchored by FD4/SPRJ structures that place the hash at +0x38 and the dirty
// byte at +0x3c.
struct FD4BasicHashString {
    std::uintptr_t vftable;
    Unknown<0x30> inner;
    std::uint32_t hash;
    bool needs_hashing;
    std::uint8_t _pad3d[3];
};

// Resource-name view recovered from RVA 0xf83c30 and native hash-holder
// consumers. The name at resource +0x08 spans 0x48 bytes: its hash/dirty byte
// are at resource +0x48/+0x4c. Not interchangeable with the smaller
// FD4BasicHashString used by task-group prefixes. Native code uses inline
// UTF-16 storage at resource +0x18 when capacity (+0x30) is below eight;
// otherwise that storage contains a pointer.
struct FD4ResourceName {
    std::uintptr_t vftable;
    Unknown<0x38> inner;
    std::uint32_t hash;
    std::uint8_t needs_hashing;
    std::uint8_t _pad45[3];
};

// Managed-resource token used by FD4 resource capabilities. Constructor RVA
// 0xf83c30 and holder insert/release at 0xfe6a70/0xfe6ae0 establish the
// 0x68-byte prefix. Counts use ordinary (non-atomic) native integer updates.
struct FD4ResCap {
    std::uintptr_t vftable;
    FD4ResourceName name;
    FD4ResCapHolder* owning_repository;
    FD4ResCap* next_item;
    std::int32_t reference_count;
    // Release refuses to unlink while this is positive; wider meaning open.
    std::int32_t value_64;
};

// Native FD4FileCap base of CS entry-file-list caps. Constructor RVA 0xfe6ff0
// extends FD4ResCap through +0x87; reflected size method RVA 0x29b52f0 agrees.
struct FD4FileCap {
    FD4ResCap res_cap;
    // Native file-wrapper subobject begins here, with pointer at +0x70.
    const void* file_wrapper_vftable;
    void* file;
    std::uint8_t state;
    // Raw flags/padding. Constructor writes 0x82, 0xf8, 0xff to +0x79..+0x7b.
    std::uint8_t raw_79[7];
    // Copied from constructor seed +0x10; the observed CS seeds pass null.
    void* seed_pointer;

    static constexpr std::size_t SIZE = FD4_FILE_CAP_SIZE;
    static constexpr Rva VTABLE{0x52f65d0};
    static constexpr Rva CONSTRUCTOR_FN{0xfe6ff0};
    static constexpr Rva DESTRUCTOR_FN{0xfe7090};
};

// Hash holder header for resource capabilities.
struct FD4ResCapHolder {
    std::uintptr_t vftable;
    void* allocator;
    std::uint64_t _unk10;
    std::uint32_t _unk18;
    std::uint32_t bucket_count;
    FD4ResCap** buckets;

    // Number of hash buckets currently addressable (0 when unallocated).
    std::size_t bucket_len() const { return (buckets && bucket_count) ? bucket_count : 0; }
    // Head of bucket i (may be null), or null when out of range.
    FD4ResCap* bucket(std::size_t i) const { return i < bucket_len() ? buckets[i] : nullptr; }
    // fn(FD4ResCap&) for every cap in every bucket, following next_item.
    template <typename Fn>
    void for_each(Fn&& fn) const {
        for (std::size_t i = 0; i < bucket_len(); ++i) {
            for (FD4ResCap* c = buckets[i]; c; c = c->next_item) fn(*c);
        }
    }
};

// Runtime metadata prepended at offset -0x10 from the param file.
struct ParamFileMetadata {
    // Unaligned offset from the beginning of the param file to the end of the
    // name/descriptor data. The row lookup table begins at this value aligned
    // up to 16 bytes.
    std::uint32_t after_name_offset;
    std::uint8_t _reserved[0x0c];
};

// Entry in the runtime lookup table that maps param IDs to row indices.
struct RowLookupEntry {
    std::uint32_t param_id;
    std::uint32_t index;
};

// 32-bit row descriptor.
struct RowDescriptor32 {
    std::uint32_t id;
    std::uint32_t data_offset;
    std::uint32_t name_offset;
};

// 64-bit row descriptor.
struct RowDescriptor64 {
    std::uint32_t id;
    std::uint32_t _pad04;
    std::uint64_t data_offset;
    std::uint64_t name_offset;
};

// In-memory parameter file. The 1.09 row lookup functions read the row count
// at +0x0a, choose row descriptor arrays at +0x34, +0x44 or +0x48 from the
// format bytes at +0x2d/+0x2e, and locate the lookup table via the metadata at
// -0x10. Row types (P) come from bbhost/params.hpp; the caller must pick the
// one matching this file.
struct ParamFile {
    std::uint8_t header[PARAM_FILE_BASE_HEADER_SIZE];

    static constexpr std::size_t BASE_HEADER_SIZE = PARAM_FILE_BASE_HEADER_SIZE;
    static constexpr std::uint32_t LOOKUP_TABLE_ALIGNMENT = 0x10;

    // Name of the row struct this param uses ("" when absent).
    const char* struct_name() const {
        if (has_offset_param_type()) {
            std::uint32_t off = u32_at(0x10);
            return off ? reinterpret_cast<const char*>(bytes() + off) : "";
        }
        return reinterpret_cast<const char*>(bytes() + 0x0c);
    }
    // The revision of this paramdef struct type.
    std::uint16_t paramdef_version() const { return u16_at(0x08); }
    // The number of rows this file contains.
    std::size_t row_count() const { return u16_at(0x0a); }

    // Row index for a param ID, or -1 (binary search of the lookup table).
    std::ptrdiff_t find_index(std::uint32_t id) const {
        const RowLookupEntry* table = lookup_table();
        std::size_t lo = 0, hi = row_count();
        while (lo < hi) {
            std::size_t mid = lo + (hi - lo) / 2;
            if (table[mid].param_id < id) {
                lo = mid + 1;
            } else if (table[mid].param_id > id) {
                hi = mid;
            } else {
                return static_cast<std::ptrdiff_t>(table[mid].index);
            }
        }
        return -1;
    }

    // Row with the given ID as P, or null.
    template <typename P>
    P* get_row_by_id(std::uint32_t id) const {
        std::ptrdiff_t index = find_index(id);
        return index < 0 ? nullptr : get_row_by_index<P>(static_cast<std::size_t>(index));
    }
    // Row at a row index as P, or null.
    template <typename P>
    P* get_row_by_index(std::size_t row_index) const {
        std::size_t off = row_data_offset(row_index);
        return off ? reinterpret_cast<P*>(const_cast<std::uint8_t*>(bytes()) + off) : nullptr;
    }

    // Row ID at a row index (no bounds check beyond row_count; 0 when out of range).
    std::uint32_t row_id(std::size_t row_index) const {
        if (row_index >= row_count()) return 0;
        if (uses_64_bit_row_descriptors()) return descriptors64()[row_index].id;
        return descriptors32()[row_index].id;
    }

    const ParamFileMetadata& metadata() const {
        return *reinterpret_cast<const ParamFileMetadata*>(bytes() - sizeof(ParamFileMetadata));
    }
    const RowLookupEntry* lookup_table() const {
        std::uint32_t a = LOOKUP_TABLE_ALIGNMENT;
        std::size_t aligned = (metadata().after_name_offset + a - 1) / a * a;
        return reinterpret_cast<const RowLookupEntry*>(bytes() + aligned);
    }
    const RowDescriptor32* descriptors32() const {
        return reinterpret_cast<const RowDescriptor32*>(bytes() + row_descriptor_offset());
    }
    const RowDescriptor64* descriptors64() const {
        return reinterpret_cast<const RowDescriptor64*>(bytes() + row_descriptor_offset());
    }
    std::size_t row_descriptor_offset() const {
        if (format_2d() == 2) return 0x34;
        if (!uses_64_bit_row_descriptors()) return 0x44;
        return 0x48;
    }
    bool has_offset_param_type() const { return (header[0x2d] & 0x80) != 0; }
    bool uses_64_bit_row_descriptors() const { return header[0x2d] >= 4 && (header[0x2e] & 2) != 0; }
    std::uint8_t format_2d() const { return header[0x2d]; }

private:
    const std::uint8_t* bytes() const { return header; }
    // Data offset of a row (0 when out of range).
    std::size_t row_data_offset(std::size_t row_index) const {
        if (row_index >= row_count()) return 0;
        if (uses_64_bit_row_descriptors()) return static_cast<std::size_t>(descriptors64()[row_index].data_offset);
        return descriptors32()[row_index].data_offset;
    }
    std::uint16_t u16_at(std::size_t o) const {
        return static_cast<std::uint16_t>(header[o] | (header[o + 1] << 8));
    }
    std::uint32_t u32_at(std::size_t o) const {
        return static_cast<std::uint32_t>(header[o]) | (static_cast<std::uint32_t>(header[o + 1]) << 8) |
               (static_cast<std::uint32_t>(header[o + 2]) << 16) | (static_cast<std::uint32_t>(header[o + 3]) << 24);
    }
};

// Parameter resource capability layout.
struct FD4ParamResCap {
    FD4ResCap res_cap;
    std::uint64_t size;
    ParamFile* data;

    // The in-memory param file, or null if the game has not populated it.
    ParamFile* param_file() const { return data; }
    // Name of the row struct used by this parameter, or null.
    const char* struct_name() const { return data ? data->struct_name() : nullptr; }
    // Row with the given id as P (P must match the file's row struct), or null.
    template <typename P>
    P* get(std::uint32_t id) const {
        return data ? data->get_row_by_id<P>(id) : nullptr;
    }
};

namespace detail::fd4_layout {
BB_SIZE(FD4DelayDeleteManager, FD4_DELAY_DELETE_MANAGER_SIZE);
BB_SIZE(FD4DelayDeleteNode, 0x28);
BB_SIZE(FD4Time, FD4_TIME_SIZE);
BB_SIZE(FD4TaskDataPrefix, FD4_TASK_DATA_PREFIX_SIZE);
BB_SIZE(FD4BasicHashString, FD4_BASIC_HASH_STRING_SIZE);
BB_SIZE(FD4ResourceName, FD4_RESOURCE_NAME_SIZE);
BB_SIZE(FD4ResCap, FD4_RES_CAP_SIZE);
BB_SIZE(FD4FileCap, FD4_FILE_CAP_SIZE);
BB_SIZE(FD4ResCapHolder, FD4_RES_CAP_HOLDER_SIZE);
BB_SIZE(FD4ParamResCap, FD4_PARAM_RES_CAP_SIZE);
BB_SIZE(ParamFile, PARAM_FILE_BASE_HEADER_SIZE);
BB_OFFSET(FD4Time, time, 0x08);
BB_OFFSET(FD4TaskDataPrefix, delta_time, 0x00);
BB_OFFSET(FD4BasicHashString, hash, 0x38);
BB_OFFSET(FD4BasicHashString, needs_hashing, 0x3c);
BB_OFFSET(FD4ResourceName, hash, 0x40);
BB_OFFSET(FD4ResourceName, needs_hashing, 0x44);
BB_OFFSET(FD4ResCap, name, 0x08);
BB_OFFSET(FD4ResCap, owning_repository, 0x50);
BB_OFFSET(FD4ResCap, next_item, 0x58);
BB_OFFSET(FD4ResCap, reference_count, 0x60);
BB_OFFSET(FD4ResCap, value_64, 0x64);
BB_OFFSET(FD4FileCap, file_wrapper_vftable, 0x68);
BB_OFFSET(FD4FileCap, file, 0x70);
BB_OFFSET(FD4FileCap, state, 0x78);
BB_OFFSET(FD4FileCap, raw_79, 0x79);
BB_OFFSET(FD4FileCap, seed_pointer, 0x80);
BB_OFFSET(FD4ResCapHolder, _unk18, 0x18);
BB_OFFSET(FD4ResCapHolder, bucket_count, 0x1c);
BB_OFFSET(FD4ResCapHolder, buckets, 0x20);
BB_OFFSET(FD4ParamResCap, size, 0x68);
BB_OFFSET(FD4ParamResCap, data, 0x70);
BB_SIZE(ParamFileMetadata, 0x10);
static_assert(FD4DebugMenuManager::SINGLETON_PTR == Rva{0x54b3570}, "FD4DebugMenuManager::SINGLETON_PTR");
static_assert(FD4DebugMenuReportSystem::SINGLETON_PTR == Rva{0x54b35f0}, "FD4DebugMenuReportSystem::SINGLETON_PTR");
static_assert(FD4DebugMenuShareStringManager::SINGLETON_PTR == Rva{0x54b38f0},
              "FD4DebugMenuShareStringManager::SINGLETON_PTR");
}  // namespace detail::fd4_layout

}  // namespace bb
