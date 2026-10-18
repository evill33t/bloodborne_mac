// Character ASM transfer storage and its native teardown contract.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Descriptive record view, not a separately identified runtime class. Only the
// retained pointer at +0x08 is established by the destructor's loop.
struct CSChrAsmTransferReference {
    std::uint64_t value_00;
    void* retained_object;
};

// Reflected 0x180-byte transfer object, with two banks of retained references.
//
// Destructor RVA 0x1c9bd00 clears the embedded request-update system first,
// then releases bank +0x100 followed by bank +0x80, each in reverse order. It
// atomically decrements the pointee's signed reference count at +0x08, invokes
// virtual slot zero on the last release, and clears each pointer. Finally it
// destroys the embedded system (FD4RequestUpdateSystemEx by native source
// paths; opaque here). Size from 0x1c9c4c0; metadata delete helper 0x1c9c250
// calls this destructor before freeing. Construction, producer/consumer wiring,
// reference target types and the first word of each record are unverified.
struct CSChrAsmInfoTransfer {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_CHR_ASM_INFO_TRANSFER_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x180;
    static constexpr Rva INSTANCE_VTABLE{0x534ac50};
    static constexpr Rva SIZE_FN{0x1c9c4c0};
    static constexpr Rva DESTRUCTOR_FN{0x1c9bd00};
    static constexpr Rva METADATA_DELETE_FN{0x1c9c250};
    // Receives the embedded system at transfer +0x08.
    static constexpr Rva CLEAR_REQUESTS_FN{0xfe4ea0};
    // Receives the embedded system at transfer +0x08.
    static constexpr Rva DESTROY_REQUEST_SYSTEM_FN{0xfe4c90};

    const void* vftable;
    Unknown<0x78> _request_update_system;
    CSChrAsmTransferReference references_80[8];
    CSChrAsmTransferReference references_100[8];
};

namespace detail::chr_asm_info_transfer_layout {
BB_SIZE(CSChrAsmInfoTransfer, 0x180);
BB_SIZE(CSChrAsmInfoTransfer, CSChrAsmInfoTransfer::SIZE);
static_assert(alignof(CSChrAsmInfoTransfer) == 8, "alignof(CSChrAsmInfoTransfer)");
BB_OFFSET(CSChrAsmInfoTransfer, _request_update_system, 0x08);
BB_OFFSET(CSChrAsmInfoTransfer, references_80, 0x80);
BB_OFFSET(CSChrAsmInfoTransfer, references_100, 0x100);
BB_SIZE(CSChrAsmTransferReference, 0x10);
BB_OFFSET(CSChrAsmTransferReference, retained_object, 8);
// Native reverse loops release the last pointer at +0xf8 / +0x178.
static_assert(offsetof(CSChrAsmInfoTransfer, references_80) + 7 * sizeof(CSChrAsmTransferReference) +
                      offsetof(CSChrAsmTransferReference, retained_object) ==
                  0xf8,
              "references_80 last pointer");
static_assert(offsetof(CSChrAsmInfoTransfer, references_100) + 7 * sizeof(CSChrAsmTransferReference) +
                      offsetof(CSChrAsmTransferReference, retained_object) ==
                  0x178,
              "references_100 last pointer");
}  // namespace detail::chr_asm_info_transfer_layout

}  // namespace bb
