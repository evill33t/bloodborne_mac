// SoloParamRepositoryImp: the singleton holding the game's solo param
// resources (validated prefix through the fallback holder).
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/fd4.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t PARAM_RES_CAP_SIZE = 0x78;
inline constexpr std::size_t SOLO_PARAM_HOLDER_SIZE = 0x48;
inline constexpr std::size_t SOLO_PARAM_REPOSITORY_HEADER_SIZE = 0x70;
inline constexpr std::size_t SOLO_PARAM_REPOSITORY_PREFIX_SIZE = 0x1228;

// Number of named 1.09 solo params in the registration table.
inline constexpr std::size_t KNOWN_SOLO_PARAM_COUNT = 0x3e;
// Fallback holder index used for unrecognized solo param names.
inline constexpr std::size_t UNKNOWN_SOLO_PARAM_INDEX = 0x3e;
// Total holder count accepted by the 1.09 registration path.
inline constexpr std::size_t SOLO_PARAM_HOLDER_COUNT = 0x3f;
inline constexpr std::size_t SOLO_PARAM_HOLDER_BASE_OFFSET = 0x70;
inline constexpr std::size_t SOLO_PARAM_HOLDER_STRIDE = 0x48;

// Native singleton storage and method addresses verified in Ghidra: the
// native repository machinery only, not permission to call or mutate it
// without an owning adapter.
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_SINGLETON_STORAGE{0x55939d0};
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_RUNTIME_OBJECT_STORAGE{0x55939d8};
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_CONSTRUCT_FN{0x1f27ce0};
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_ADD_PARAM_RES_CAP_FN{0x1f28000};
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_REMOVE_PARAM_RES_CAP_FN{0x1f280b0};
inline constexpr Rva SOLO_PARAM_REPOSITORY_IMP_REGISTER_RUNTIME_CLASS_FN{0x1f28370};

inline constexpr std::size_t solo_param_holder_offset(std::size_t index) {
    return SOLO_PARAM_HOLDER_BASE_OFFSET + index * SOLO_PARAM_HOLDER_STRIDE;
}

// Wrapper used by SoloParamRepository.
struct ParamResCap {
    FD4ResCap res_cap;
    std::uint32_t _unk68;
    std::uint32_t _pad6c;
    FD4ParamResCap* param_res_cap;

    FD4ParamResCap* fd4_param_res_cap() const { return param_res_cap; }
};

// Bloodborne/Sekiro-style holder for solo param res caps.
struct SoloParamHolder {
    std::uint32_t res_cap_count;
    std::uint32_t _pad04;
    ParamResCap* res_caps[8];

    // The res cap at index, or null.
    ParamResCap* get_res_cap(std::size_t index) const { return index < 8 ? res_caps[index] : nullptr; }
    // fn(ParamResCap&) for every non-null res cap.
    template <typename Fn>
    void for_each_res_cap(Fn&& fn) const {
        for (ParamResCap* r : res_caps) {
            if (r) fn(*r);
        }
    }
};

// Validated start of the SoloParamRepository singleton. 1.09 accesses holders
// as 0x70 + index * 0x48.
struct SoloParamRepositoryHeader {
    FD4ResCap res_cap;
    std::uint32_t _unk68;
    std::uint32_t _pad6c;
};

// Validated prefix of the SoloParamRepositoryImp singleton through fallback
// holder 62: the 1.09 add/remove path maps 62 named params plus a fallback.
//
// The param-typed accessors take a P with `static constexpr std::size_t
// INDEX` (its holder index) and `using StructType = <row struct>`. There is no
// get_equip_param(ItemId).
struct SoloParamRepositoryImp {
    static constexpr RuntimeClassSymbol RUNTIME_CLASS = SOLO_PARAM_REPOSITORY_IMP_RUNTIME_CLASS;

    SoloParamRepositoryHeader header;
    SoloParamHolder solo_param_holders[SOLO_PARAM_HOLDER_COUNT];

    // All validated holder slots, including the fallback holder.
    static constexpr std::size_t holder_count() { return SOLO_PARAM_HOLDER_COUNT; }
    SoloParamHolder* holder_by_index(std::size_t index) {
        return index < SOLO_PARAM_HOLDER_COUNT ? &solo_param_holders[index] : nullptr;
    }
    const SoloParamHolder* holder_by_index(std::size_t index) const {
        return index < SOLO_PARAM_HOLDER_COUNT ? &solo_param_holders[index] : nullptr;
    }
    // The fallback holder used for unknown solo param names.
    SoloParamHolder& unknown_holder() { return solo_param_holders[UNKNOWN_SOLO_PARAM_INDEX]; }
    const SoloParamHolder& unknown_holder() const { return solo_param_holders[UNKNOWN_SOLO_PARAM_INDEX]; }
    // fn(FD4ParamResCap&) for each loaded param resource in the prefix.
    template <typename Fn>
    void for_each_param(Fn&& fn) const {
        for (const SoloParamHolder& h : solo_param_holders) {
            h.for_each_res_cap([&](ParamResCap& r) {
                if (r.param_res_cap) fn(*r.param_res_cap);
            });
        }
    }

    template <typename P>
    const SoloParamHolder& holder() const { return solo_param_holders[P::INDEX]; }
    template <typename P>
    SoloParamHolder& holder() { return solo_param_holders[P::INDEX]; }
    // The first FD4 param resource for P, or null.
    template <typename P>
    FD4ParamResCap* fd4_param_res_cap() const {
        ParamResCap* r = holder<P>().get_res_cap(0);
        return r ? r->param_res_cap : nullptr;
    }
    // A row of P by param ID, or null. The file's struct name is not checked;
    // that is left to the caller.
    template <typename P>
    typename P::StructType* get(std::uint32_t param_id) const {
        FD4ParamResCap* p = fd4_param_res_cap<P>();
        return p ? p->template get<typename P::StructType>(param_id) : nullptr;
    }
    // A row of P by row index (not the param ID), or null.
    template <typename P>
    typename P::StructType* get_row_by_index(std::size_t row_index) const {
        FD4ParamResCap* p = fd4_param_res_cap<P>();
        ParamFile* f = p ? p->param_file() : nullptr;
        return f ? f->template get_row_by_index<typename P::StructType>(row_index) : nullptr;
    }
    // The row index for a param ID, or -1.
    template <typename P>
    std::ptrdiff_t get_index_by_param_id(std::uint32_t param_id) const {
        FD4ParamResCap* p = fd4_param_res_cap<P>();
        ParamFile* f = p ? p->param_file() : nullptr;
        return f ? f->find_index(param_id) : -1;
    }
};

// Backwards-compatible name for the validated repository prefix.
using SoloParamRepositoryPrefix = SoloParamRepositoryImp;

namespace detail::solo_param_repository_layout {
BB_SIZE(ParamResCap, PARAM_RES_CAP_SIZE);
BB_SIZE(SoloParamHolder, SOLO_PARAM_HOLDER_SIZE);
BB_SIZE(SoloParamRepositoryHeader, SOLO_PARAM_REPOSITORY_HEADER_SIZE);
BB_SIZE(SoloParamRepositoryPrefix, SOLO_PARAM_REPOSITORY_PREFIX_SIZE);
BB_OFFSET(ParamResCap, res_cap, 0x00);
BB_OFFSET(ParamResCap, param_res_cap, 0x70);
BB_OFFSET(SoloParamHolder, res_cap_count, 0x00);
BB_OFFSET(SoloParamHolder, res_caps, 0x08);
BB_OFFSET(SoloParamRepositoryHeader, res_cap, 0x00);
BB_OFFSET(SoloParamRepositoryPrefix, header, 0x00);
BB_OFFSET(SoloParamRepositoryPrefix, solo_param_holders, SOLO_PARAM_HOLDER_BASE_OFFSET);
static_assert(solo_param_holder_offset(0) == 0x70, "holder 0");
static_assert(solo_param_holder_offset(15) == 0x4a8, "holder 15");
static_assert(solo_param_holder_offset(17) == 0x538, "holder 17");
static_assert(solo_param_holder_offset(41) == 0xbf8, "holder 41");
static_assert(solo_param_holder_offset(61) == 0x1198, "holder 61");
static_assert(solo_param_holder_offset(UNKNOWN_SOLO_PARAM_INDEX) == 0x11e0, "fallback holder");
static_assert(SoloParamRepositoryImp::RUNTIME_CLASS.runtime_class_ptr.rva == 0x55939d0, "runtime class ptr");
}  // namespace detail::solo_param_repository_layout

}  // namespace bb
