// FrpgNetMan network-identity surface: signaling session state, MatchingObjectRef, NPID window.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/frpg.hpp"
#include "bbhost/engine/sprj/application_network.hpp"

namespace bb {

// Observation-only. Does not expose a PlayerData get-or-create operation as an
// identity lookup and adds no hooks or coordinator behavior.

// The already-reduced FrpgNetMan prefix from frpg.hpp (one copy of the 0xc58 layout).
using FrpgNetMan = FrpgNetManPrefix;

inline constexpr Rva FRPG_NET_MAN_SINGLETON{0x553b120};
inline constexpr Rva PROCESS_NP_SIGNALING_CONNECTION_SESSION_STATE{0x0cd8bb0};
inline constexpr Rva PROCESS_NP_SIGNALING_CONNECTION_SESSION_STATE_THUNK{0x0cda1e0};
inline constexpr Rva MATCHING_OBJECT_REF_IS_VALID{0x0ca1400};
inline constexpr Rva MATCHING_OBJECT_REF_MATCHES_TARGET{0x0ca1c70};
inline constexpr Rva MATCHING_OBJECT_REF_COPY_CONSTRUCT{0x0ca1500};
inline constexpr Rva MATCHING_OBJECT_REF_RESET{0x0ca1570};
inline constexpr Rva NP_SESSION_ADDRESS_COPY_NPID_BYTES{0x0ca25e0};
// sprj/application_network.hpp defines the same value as PLAYER_DATA_MANAGER_SINGLETON_sprj.
inline constexpr Rva PLAYER_DATA_MANAGER_SINGLETON{0x56c6d70};
inline constexpr Rva PLAYER_DATA_MANAGER_GET_SINGLETON{0x0ca48f0};
inline constexpr Rva PLAYER_DATA_MANAGER_GET_OR_CREATE_BY_NPID{0x0ca4570};

// Native intrusive wrapper retained at signaling-session +0x18. Do not copy:
// bytes +0x08..+0x1f are live mutex state. Native code retains a wrapper with
// MatchingObjectRef_CopyConstruct and releases it exactly once with reset.
struct MatchingObjectRef {
    void* object;
    void* mutex_vtable;
    std::uint8_t mutex_storage[0x10];
};

// Native 0x30-byte temporary owner used while exporting a signaling NPID. Its
// embedded reference at +0x08 is copy-constructed; the resolved native session
// address is stored at +0x28.
struct NpSessionAddressRef {
    void* vtable;
    MatchingObjectRef address_reference;
    void* resolved_session_address;
};

// NPID-bearing window inside the native session-address object. The bytes
// before the window and the owner/lifetime of the containing object are not
// modeled.
#pragma pack(push, 1)
struct NpSessionAddressNpIdWindow {
    std::uint8_t unknown_000[0x36];
    OrbisNpId np_id;
};
#pragma pack(pop)

// Reduced state context consumed by ProcessNpSignalingConnectionSessionState.
// Ghidra does not prove the RTTI/class name of the native astruct_66
// parameter: a role/layout name, not a runtime-class claim.
struct NpSignalingConnectionSession {
    std::uint8_t unknown_000[0x0c];
    std::uint32_t state;
    std::uint8_t unknown_010[0x08];
    MatchingObjectRef address_object_ref;
    std::uint8_t unknown_038[0x76];
    std::uint8_t activation_complete_flag;
    std::uint8_t unknown_0af;
    std::uint32_t connection_id;
    std::uint32_t substate;
};

// Complete native PlayerData layout is not proven; opaque so a guessed field
// cannot be treated as identity state. Use through pointers only.
struct PlayerData;

using ProcessNpSignalingConnectionSessionStateAbi = void(BB_GAME_ABI*)(NpSignalingConnectionSession*);
using MatchingObjectRefIsValidAbi = bool(BB_GAME_ABI*)(MatchingObjectRef*);
using MatchingObjectRefMatchesTargetAbi = bool(BB_GAME_ABI*)(MatchingObjectRef*, MatchingObjectRef*);
using MatchingObjectRefCopyConstructAbi = void(BB_GAME_ABI*)(MatchingObjectRef*, const MatchingObjectRef*);
using MatchingObjectRefResetAbi = void(BB_GAME_ABI*)(MatchingObjectRef*);
using NpSessionAddressCopyNpidBytesAbi = void(BB_GAME_ABI*)(const void*, std::uint8_t*);

namespace detail::frpg_net_man_layout {
BB_SIZE(MatchingObjectRef, 0x20);
BB_OFFSET(NpSessionAddressRef, address_reference, 0x08);
BB_OFFSET(NpSessionAddressRef, resolved_session_address, 0x28);
BB_SIZE(NpSessionAddressRef, 0x30);
BB_SIZE(OrbisNpId, 36);
BB_OFFSET(NpSessionAddressNpIdWindow, np_id, 0x36);
BB_SIZE(NpSessionAddressNpIdWindow, 0x5a);
BB_OFFSET(NpSignalingConnectionSession, state, 0x0c);
BB_OFFSET(NpSignalingConnectionSession, address_object_ref, 0x18);
BB_OFFSET(NpSignalingConnectionSession, activation_complete_flag, 0xae);
BB_OFFSET(NpSignalingConnectionSession, connection_id, 0xb0);
BB_OFFSET(NpSignalingConnectionSession, substate, 0xb4);
BB_SIZE(NpSignalingConnectionSession, 0xb8);
BB_SIZE(FrpgNetMan, 0xc58);
}  // namespace detail::frpg_net_man_layout

}  // namespace bb
