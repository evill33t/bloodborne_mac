// Reflected assembly-model hierarchy (SprjAsmModel/ChrAsmModel), distinct from
// the CS menu assembly helpers.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/cs/model_ins.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

// Descriptive polymorphic buffer of 0x30-byte records. Native resize changes
// capacity and storage; the source count is assigned separately.
struct AsmModelRecordBuffer {
    const void* vftable;
    std::int32_t count;
    std::int32_t capacity;
    Unknown<0x30>* records;
};

// Descriptive second array element. Constructor zeroes value_08 and storage;
// the element destructor is virtual slot zero.
struct AsmModelPartList {
    const void* vftable;
    std::int32_t value_08;
    std::uint8_t _unk0c[4];
    void* storage;
};

// Reflected 0x220-byte base used by ChrAsmModel. The constructor creates a
// separately retained 0x960-byte model and three arrays sized by part_count.
// Child classes and most rendering values are unclassified. +0x180 receives
// aligned 16-byte stores; the derived allocation requests 16-byte alignment.
struct alignas(16) SprjAsmModel {
    const void* vftable;
    // Non-atomic signed reference count at pointee +0x08.
    Unknown<0x960>* retained_model;
    void* source;
    std::uint16_t configuration;
    // Constructor copies only the low four bits of its configuration input.
    std::uint8_t configuration_flags;
    std::uint8_t value_1b;
    std::uint8_t _unk1c[4];
    std::uint32_t values_20_bits[3][4];
    AsmModelRecordBuffer source_records;
    std::uint32_t values_68_bits[8];
    std::int32_t part_count;
    std::uint8_t _unk8c[4];
    // part_count records of 0x30 bytes, initially zeroed.
    Unknown<0x30>* part_records;
    // part_count buffer descriptors; a native array header precedes this pointer.
    AsmModelRecordBuffer* part_buffers;
    // part_count 0x18-byte descriptors with a separate native array header.
    AsmModelPartList* part_lists;
    std::int32_t value_a8;
    // Initialized to all ones; consumers also modify individual flag bytes.
    std::uint8_t flags_ac[4];
    std::uint32_t values_b0_bits[4];
    std::uint8_t flag_c0;
    std::uint8_t flag_c1;
    std::uint8_t flags_c2;
    std::uint8_t _unkc3[5];
    ModelTransitionSet transitions;
    // Both initialized to 1.0f bits.
    std::uint32_t values_170_bits[2];
    std::uint8_t _unk178[8];
    std::uint32_t values_180_bits[8][4];
    std::uint32_t values_200_bits[4];
    std::uint64_t value_210_bits;
    std::uint64_t value_218_bits;

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_ASM_MODEL_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x220;
    static constexpr Rva INSTANCE_VTABLE{0x539d2a0};
    static constexpr Rva SIZE_FN{0x1cb2740};
    static constexpr Rva CONSTRUCTOR_FN{0x1cab350};
    static constexpr Rva DESTRUCTOR_FN{0x1cab930};
};

// Reflected character assembly model, 0x300 bytes: the complete SprjAsmModel
// plus 24 owned per-part state pointers. The constructor passes part_count=24
// to the base; the destructor removes all 24 parts (cloth, collision and
// face-generation resources included), then destroys the base arrays. The
// owner stores this at its +0x1c8; that owner type is unresolved.
struct alignas(16) ChrAsmModel {
    SprjAsmModel super_asm_model;
    void* context;
    void* owned_parts[24];
    std::uint64_t value_2e8_bits;
    // Initialized to 24, the base allocation's part count.
    std::uint8_t part_count;
    std::uint8_t flag_2f1;
    std::uint8_t _unk2f2[0x0e];

    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CHR_ASM_MODEL_RUNTIME_CLASS;
    static constexpr std::size_t SIZE = 0x300;
    static constexpr Rva INSTANCE_VTABLE{0x539cdd0};
    static constexpr Rva SIZE_FN{0x1c9b920};
    static constexpr Rva CONSTRUCTOR_FN{0x1c95910};
    static constexpr Rva DESTRUCTOR_FN{0x1c95a70};
    static constexpr Rva REMOVE_PART_FN{0x1c95bd0};
    static constexpr Rva OWNER_CREATE_FN{0x16fb1b0};
};

namespace detail::asm_model_layout {
BB_SIZE(SprjAsmModel, SprjAsmModel::SIZE);
static_assert(alignof(SprjAsmModel) == 16, "alignof(SprjAsmModel)");
BB_OFFSET(SprjAsmModel, retained_model, 8);
BB_OFFSET(SprjAsmModel, source, 0x10);
BB_OFFSET(SprjAsmModel, configuration, 0x18);
BB_OFFSET(SprjAsmModel, configuration_flags, 0x1a);
BB_OFFSET(SprjAsmModel, values_20_bits, 0x20);
BB_OFFSET(SprjAsmModel, source_records, 0x50);
BB_OFFSET(SprjAsmModel, values_68_bits, 0x68);
BB_OFFSET(SprjAsmModel, part_count, 0x88);
BB_OFFSET(SprjAsmModel, part_records, 0x90);
BB_OFFSET(SprjAsmModel, part_buffers, 0x98);
BB_OFFSET(SprjAsmModel, part_lists, 0xa0);
BB_OFFSET(SprjAsmModel, value_a8, 0xa8);
BB_OFFSET(SprjAsmModel, flags_ac, 0xac);
BB_OFFSET(SprjAsmModel, values_b0_bits, 0xb0);
BB_OFFSET(SprjAsmModel, flag_c0, 0xc0);
BB_OFFSET(SprjAsmModel, transitions, 0xc8);
BB_OFFSET(SprjAsmModel, values_170_bits, 0x170);
BB_OFFSET(SprjAsmModel, values_180_bits, 0x180);
BB_OFFSET(SprjAsmModel, values_200_bits, 0x200);
BB_OFFSET(SprjAsmModel, value_210_bits, 0x210);
BB_OFFSET(SprjAsmModel, value_218_bits, 0x218);
BB_SIZE(AsmModelRecordBuffer, 0x18);
BB_OFFSET(AsmModelRecordBuffer, count, 8);
BB_OFFSET(AsmModelRecordBuffer, capacity, 0xc);
BB_OFFSET(AsmModelRecordBuffer, records, 0x10);
BB_SIZE(AsmModelPartList, 0x18);
BB_OFFSET(AsmModelPartList, value_08, 8);
BB_OFFSET(AsmModelPartList, storage, 0x10);
BB_SIZE(ChrAsmModel, ChrAsmModel::SIZE);
static_assert(alignof(ChrAsmModel) == 16, "alignof(ChrAsmModel)");
BB_OFFSET(ChrAsmModel, super_asm_model, 0);
BB_OFFSET(ChrAsmModel, context, 0x220);
BB_OFFSET(ChrAsmModel, owned_parts, 0x228);
BB_OFFSET(ChrAsmModel, value_2e8_bits, 0x2e8);
BB_OFFSET(ChrAsmModel, part_count, 0x2f0);
BB_OFFSET(ChrAsmModel, flag_2f1, 0x2f1);
}  // namespace detail::asm_model_layout

}  // namespace bb
