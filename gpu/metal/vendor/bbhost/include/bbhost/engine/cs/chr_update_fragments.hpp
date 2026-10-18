// Character update fragment runtime metadata.
//
// The traced CSChrThread submissions allocate 0x30-byte member-callback
// fragments; they do not establish fields for these reflected 0x20 classes.
// Their class-pointer xrefs are registration and metadata deletion only.
// Metadata virtual slot +0x40 returns zero for both; it is not evidence of an
// instance constructor or of a 0x20 work-fragment layout.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr Rva CS_CHR_UPDATE_POST_FRAGMENT_SIZE_FN{0x1a66b60};
inline constexpr Rva CS_CHR_UPDATE_PRE_FRAGMENT_SIZE_FN{0x1a67270};

// Marker for the observed CSChrUpdatePostFragment class. Reflection reports
// 0x20 bytes, but no instance constructor/consumer is recovered. Not a native
// memory view (no members, so no size assert).
struct CSChrUpdatePostFragment {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CHR_UPDATE_POST_FRAGMENT_RUNTIME_CLASS;
    static constexpr std::size_t NATIVE_SIZE = 0x20;
    // Runtime metadata's vtable, not an instance vtable.
    static constexpr Rva METADATA_VTABLE{0x53992b0};
    static constexpr Rva SIZE_FN = CS_CHR_UPDATE_POST_FRAGMENT_SIZE_FN;
    // Checks a supplied runtime-class chain, then destroys and frees a matching
    // supplied object. Does not establish object fields.
    static constexpr Rva METADATA_DELETE_FN{0x1a668f0};
    // Metadata virtual +0x40 is a zero-returning stub in this binary.
    static constexpr Rva METADATA_SLOT_40_FN{0x1a668e0};
};

// Marker for the observed CSChrUpdatePreFragment class. Reflection reports
// 0x20 bytes, but field offsets and uses remain unproven. Not a memory view.
struct CSChrUpdatePreFragment {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CHR_UPDATE_PRE_FRAGMENT_RUNTIME_CLASS;
    static constexpr std::size_t NATIVE_SIZE = 0x20;
    // Runtime metadata's vtable, not an instance vtable.
    static constexpr Rva METADATA_VTABLE{0x5399330};
    static constexpr Rva SIZE_FN = CS_CHR_UPDATE_PRE_FRAGMENT_SIZE_FN;
    // Checks a supplied runtime-class chain, then destroys and frees a matching
    // supplied object. Does not establish object fields.
    static constexpr Rva METADATA_DELETE_FN{0x1a67000};
    // Metadata virtual +0x40 is a zero-returning stub in this binary.
    static constexpr Rva METADATA_SLOT_40_FN{0x1a66ff0};
};

}  // namespace bb
