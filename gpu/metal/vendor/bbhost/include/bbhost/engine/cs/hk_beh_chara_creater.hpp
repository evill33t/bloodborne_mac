// Native behavior-character creation/deletion request ring.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

inline constexpr std::size_t CS_HK_BEH_CHARA_CREATER_SIZE = 0x48;
inline constexpr std::size_t CS_HK_BEH_CHARA_REQUEST_SIZE = 0x30;
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_CONSTRUCTOR_FN{0x1ddd930};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_DESTRUCTOR_FN{0x1ddda60};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_DISPATCH_FN{0x1dddcb0};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_PROCESS_REQUEST_FN{0x1dddf50};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_QUEUE_CREATE_FN{0x1dde1e0};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_QUEUE_DELETE_FN{0x1dde3a0};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_TAKE_RESULT_FN{0x1dde690};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_SIZE_FN{0x1ddef10};
inline constexpr Rva CS_HK_BEH_CHARA_CREATER_VTABLE{0x534e5d0};

// Descriptive name for the native 0x30 request record; no recovered
// runtime-class registration. Request IDs are initialized to index + 1, reused
// with the ring slot, and are not ChrHandle values.
//
// Queue-create RVA 0x1dde1e0 retains the animation resource, sets state 1 and
// operation 0, and links the record. Queue-delete RVA 0x1dde3a0 also sets state
// 1, with operation 1 and an existing behavior object. Dispatch sets state 2;
// processing writes 3 on success or 4 on creation failure. Taking a result at
// RVA 0x1dde690 transfers the behavior pointer, releases the animation
// reference, and resets the record while preserving its ID.
struct CSHkBehCharaRequest {
    CSHkBehCharaRequest* next;
    // Created object or object awaiting destruction, depending on operation.
    void* behavior;
    std::uint32_t request_id;
    // Caller-supplied creation value; initialized to -1. Exact meaning open.
    std::int32_t creation_value;
    // Retained resource released through AnibndRepository during cleanup.
    void* animation_resource;
    // 0 denotes an available record; other observed values documented above.
    std::uint32_t state;
    // 0 creates; the observed delete path writes 1.
    std::uint32_t operation;
    // 1 requests reclamation once the request is no longer running.
    std::uint32_t release_requested;
    std::uint32_t _pad2c;
};

// Owns a fixed-capacity request ring and its pending linked list.
//
// Constructor RVA 0x1ddd930 allocates capacity * 0x30 + 0x10 bytes and stores
// the entries after the allocation header. Callsite RVA 0x1e14fed passes
// capacity 0x100, after allocating this 0x48-byte owner aligned to eight; the
// reflected size method agrees. Native processing reaches this owner through
// SprjHkBehManager (global RVA 0x55401f8), then its +0x40 field. TimeAct
// consumer RVA 0x1a27810 independently names that singleton.
//
// Dispatch detaches pending requests and either calls submit_request with
// their IDs or processes them directly when that pointer is null. Native code
// holds the inline mutex while managing entries and retained resources.
struct CSHkBehCharaCreater {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_HK_BEH_CHARA_CREATER_RUNTIME_CLASS;
    const void* vftable;
    const void* submit_request;
    CSHkBehCharaRequest* requests;
    CSHkBehCharaRequest* pending_head;
    CSHkBehCharaRequest* pending_tail;
    std::uint32_t capacity;
    // Last allocated index; initialized to capacity - 1.
    std::uint32_t allocation_cursor;
    // Start of the inline 0x18 PS4 DLLightMutex subobject.
    const void* mutex_vftable;
    void* mutex_handle;
    std::uint8_t mutex_initialized;
    Unknown<7> _pad41;
};

namespace detail::hk_beh_chara_creater_layout {
using T = CSHkBehCharaCreater;
BB_SIZE(T, CS_HK_BEH_CHARA_CREATER_SIZE);
static_assert(alignof(T) == 8, "alignof(CSHkBehCharaCreater)");
BB_OFFSET(T, submit_request, 0x08);
BB_OFFSET(T, requests, 0x10);
BB_OFFSET(T, pending_head, 0x18);
BB_OFFSET(T, pending_tail, 0x20);
BB_OFFSET(T, capacity, 0x28);
BB_OFFSET(T, allocation_cursor, 0x2c);
BB_OFFSET(T, mutex_vftable, 0x30);
BB_OFFSET(T, mutex_handle, 0x38);
BB_OFFSET(T, mutex_initialized, 0x40);
using R = CSHkBehCharaRequest;
BB_SIZE(R, CS_HK_BEH_CHARA_REQUEST_SIZE);
BB_OFFSET(R, next, 0x00);
BB_OFFSET(R, behavior, 0x08);
BB_OFFSET(R, request_id, 0x10);
BB_OFFSET(R, creation_value, 0x14);
BB_OFFSET(R, animation_resource, 0x18);
BB_OFFSET(R, state, 0x20);
BB_OFFSET(R, operation, 0x24);
BB_OFFSET(R, release_requested, 0x28);
}  // namespace detail::hk_beh_chara_creater_layout

}  // namespace bb
