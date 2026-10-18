// DLRF runtime-class layouts: the class objects the registration functions fill in.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// These match the registration functions observed in eboot.bin: class globals
// point at the embedded runtime class object, and the ASCII/wide class name
// pointers are written at offsets 0x40 and 0x48.

// DLRF runtime class header.
struct DLRuntimeClass {
    std::uintptr_t vftable;
    const DLRuntimeClass* base_class;
    std::uintptr_t unk10;
    std::uintptr_t unk18;
    std::uintptr_t unk20;
    std::uintptr_t unk28;
    std::uintptr_t unk30;
    std::uintptr_t allocator;
};

// Named DLRF runtime class object.
struct DLRuntimeClassImpl {
    DLRuntimeClass runtime_class;
    const std::uint8_t* name;
    const std::uint16_t* name_w;
};

inline constexpr std::size_t DL_RUNTIME_CLASS_SIZE = 0x40;
inline constexpr std::size_t DL_RUNTIME_CLASS_IMPL_SIZE = 0x50;

namespace detail::dlrf_layout {
BB_SIZE(DLRuntimeClass, DL_RUNTIME_CLASS_SIZE);
BB_SIZE(DLRuntimeClassImpl, DL_RUNTIME_CLASS_IMPL_SIZE);
BB_OFFSET(DLRuntimeClass, vftable, 0x00);
BB_OFFSET(DLRuntimeClass, base_class, 0x08);
BB_OFFSET(DLRuntimeClass, allocator, 0x38);
BB_OFFSET(DLRuntimeClassImpl, runtime_class, 0x00);
BB_OFFSET(DLRuntimeClassImpl, name, 0x40);
BB_OFFSET(DLRuntimeClassImpl, name_w, 0x48);
}  // namespace detail::dlrf_layout

}  // namespace bb
