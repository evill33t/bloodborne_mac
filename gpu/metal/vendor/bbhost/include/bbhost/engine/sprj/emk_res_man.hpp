// Event-file residency owner and its common/area/block handle tables.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/world_info.hpp"

namespace bb {

struct SprjEdfFileCap;
struct SprjEldFileCap;
struct SprjEvdFileCap;
struct WorldRes;

// Descriptive 0x18-byte common-file group embedded at manager +0x490.
// Factory RVAs 0x1425710/0x14258a0/0x1425b40 construct the matching file-cap
// vtables and retain the returned caps, proving these concrete pointer types.
// References are released through the SprjFile service.
struct SprjCommonEventFiles {
    SprjEdfFileCap* edf;
    SprjEldFileCap* eld;
    SprjEvdFileCap* evd;
};

// Descriptive 0x10-byte EMELD/EMEVD pair. Twenty pairs begin at manager +0x4a8
// and sixty begin at +0x5e8. Constructor zeroes all slots; release methods
// release non-null caps through the file service and clear both pointers.
struct SprjMapEventFiles {
    SprjEldFileCap* eld;
    SprjEvdFileCap* evd;
};

// Native singleton allocated as 0x9b0 bytes aligned to eight by RVA 0x17992a0.
// Assertions name SprjEmkResMan; no reflected runtime class is claimed. Its
// vtable has destructor/deleting-destructor slots, not a reflection getter.
//
// The file service returns retained concrete file caps. Requests first release
// the prior common/area/block slots, then load the new names. Area/block
// indices are checked against world_res.world_info's live counts, not
// hard-coded pool capacities; those paths assume a non-null world_res. Native
// readiness accepts absent files and requires state 4 only for non-null file
// caps; out-of-range area/block indices also return true. Readiness does not
// prove resource presence.
//
// Constructor creates missing EVD, EDF and ELD repository singletons.
// Destructor destroys/frees and clears the ELD, EDF, then EVD repositories; it
// does not release these file tables. Separate release methods and the
// WorldRes event state machine own file retirement.
struct SprjEmkResMan {
    static constexpr std::size_t SIZE = 0x9b0;
    static constexpr std::size_t AREA_CAPACITY = 20;
    static constexpr std::size_t BLOCK_CAPACITY = 60;
    static constexpr Rva SINGLETON_PTR = SPRJ_EMK_RES_MAN_SINGLETON_PTR;
    static constexpr Rva NAME_STRING{0x4932483};
    static constexpr Rva VTABLE{0x5351430};
    static constexpr Rva CONSTRUCTOR_FN{0x1eebe10};
    static constexpr Rva DESTRUCTOR_FN{0x1eec050};
    // Releases old common slots, then requests event:/common.emedf/.emeld/.emevd.
    static constexpr Rva REQUEST_COMMON_FILES_FN{0x1eec120};
    static constexpr Rva RELEASE_COMMON_FILES_FN{0x1eec200};
    static constexpr Rva ARE_COMMON_FILES_READY_FN{0x1eec320};
    // Requests event:/mAA.emeld and event:/mAA.emevd using the area's map ID.
    static constexpr Rva REQUEST_AREA_FILES_FN{0x1eec370};
    static constexpr Rva RELEASE_AREA_FILES_FN = SPRJ_EMK_RES_MAN_RELEASE_AREA_EVENT_FILES_FN;
    static constexpr Rva ARE_AREA_FILES_READY_FN{0x1eec780};
    // Normally requests event:/mAA_BB_CC_DD.emeld/.emevd for the block map ID.
    // If block and WorldInfo+0x30 map IDs both start with area 29, the block
    // path is the lookup key and a nested path derived from WorldInfo+0x30 is
    // passed as the file cap's alternate name.
    static constexpr Rva REQUEST_BLOCK_FILES_FN = SPRJ_EMK_RES_MAN_REQUEST_BLOCK_EVENT_FILES_FN;
    static constexpr Rva RELEASE_BLOCK_FILES_FN{0x1eecc00};
    static constexpr Rva ARE_BLOCK_FILES_READY_FN{0x1eeccf0};
    // The three capture methods insert non-null caps into the enabled rapid-
    // reentry helper's file set, incrementing count +0x60 only on first insert.
    // They leave this manager's slots and references intact.
    static constexpr Rva CAPTURE_COMMON_FOR_REENTRY_FN{0x1eecd50};
    static constexpr Rva CAPTURE_AREA_FOR_REENTRY_FN{0x1eecf80};
    static constexpr Rva CAPTURE_BLOCK_FOR_REENTRY_FN{0x1eed140};

    const void* vftable;
    // Constructor writes sixteen -1 words at stride 0x48, then zeroes this
    // entire region. Its role remains unresolved; -1 is not its final default.
    Unknown<0x480> _unk008;
    // Borrowed current WorldRes. Bound/cleared by WorldRes_UpdateEventResources
    // at RVA 0x15659f0; initially null in this constructor.
    WorldRes* world_res;
    SprjCommonEventFiles common_files;
    SprjMapEventFiles area_files[WORLD_INFO_AREA_INFO_COUNT];
    SprjMapEventFiles block_files[WORLD_INFO_BLOCK_INFO_COUNT];
    // Constructor clears this word; no pointer or semantic interpretation yet.
    std::uint64_t value_9a8_bits;
};

namespace detail::emk_res_man_layout {
BB_SIZE(SprjEmkResMan, SprjEmkResMan::SIZE);
static_assert(alignof(SprjEmkResMan) == 8, "alignof(SprjEmkResMan)");
BB_OFFSET(SprjEmkResMan, vftable, 0);
BB_OFFSET(SprjEmkResMan, _unk008, 8);
BB_OFFSET(SprjEmkResMan, world_res, 0x488);
BB_OFFSET(SprjEmkResMan, common_files, 0x490);
BB_OFFSET(SprjEmkResMan, area_files, 0x4a8);
BB_OFFSET(SprjEmkResMan, block_files, 0x5e8);
BB_OFFSET(SprjEmkResMan, value_9a8_bits, 0x9a8);
static_assert(SprjEmkResMan::AREA_CAPACITY == WORLD_INFO_AREA_INFO_COUNT, "area capacity");
static_assert(SprjEmkResMan::BLOCK_CAPACITY == WORLD_INFO_BLOCK_INFO_COUNT, "block capacity");
BB_SIZE(SprjCommonEventFiles, 0x18);
static_assert(alignof(SprjCommonEventFiles) == 8, "alignof(SprjCommonEventFiles)");
BB_OFFSET(SprjCommonEventFiles, edf, 0);
BB_OFFSET(SprjCommonEventFiles, eld, 8);
BB_OFFSET(SprjCommonEventFiles, evd, 0x10);
BB_SIZE(SprjMapEventFiles, 0x10);
static_assert(alignof(SprjMapEventFiles) == 8, "alignof(SprjMapEventFiles)");
BB_OFFSET(SprjMapEventFiles, eld, 0);
BB_OFFSET(SprjMapEventFiles, evd, 8);
static_assert(sizeof(SprjMapEventFiles[20]) == 0x140, "area table");
static_assert(sizeof(SprjMapEventFiles[60]) == 0x3c0, "block table");
static_assert(SprjEmkResMan::SINGLETON_PTR.bn() == 0x059402c8, "SINGLETON_PTR");
static_assert(SprjEmkResMan::RELEASE_AREA_FILES_FN.bn() == 0x022ec690, "RELEASE_AREA_FILES_FN");
static_assert(SprjEmkResMan::REQUEST_BLOCK_FILES_FN.bn() == 0x022ec7d0, "REQUEST_BLOCK_FILES_FN");
}  // namespace detail::emk_res_man_layout

}  // namespace bb
