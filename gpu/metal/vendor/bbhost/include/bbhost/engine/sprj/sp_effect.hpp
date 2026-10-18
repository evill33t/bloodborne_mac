// Native SpEffect addresses and offsets verified in the Bloodborne 1.09 eboot.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Research addresses and offsets, not callable ABI wrappers. Effect list
// mutations belong on the native character-owning thread.

inline constexpr std::size_t CHR_INS_SP_EFFECT_OFFSET = 0x1c8;
inline constexpr std::size_t CHR_INS_APPLY_SP_EFFECT_VTABLE_OFFSET = 0x3f0;

// Wrapper used by Lua_MultiDoping and NpcIns vfunc +0x3f0. SysV RDI=target,
// ESI=effect ID, RDX=source, ECX=inner mode, R8B=control, R9D=extra argument;
// XMM0..4 carry five floats, with another integer on the stack. Nonzero R8B
// permits local application to a map actor with a valid +0x90 selector and
// suppresses this wrapper's packet 0x24 broadcast. It does not change
// persistent actor authority or skip effect eligibility.
inline constexpr Rva CHR_INS_APPLY_SP_EFFECT_FN{0x18c6db0};
// Local insertion plus native per-effect callbacks, without the outer
// wrapper's authority checks or broadcast. Has a different control contract.
inline constexpr Rva CHR_INS_APPLY_SP_EFFECT_LOCAL_FN{0x18c69d0};
inline constexpr Rva WORLD_CHR_BROADCAST_SP_EFFECT_FN{0x18c7080};
inline constexpr Rva SPEFFECT_ELIGIBILITY_AND_INSERT_FN{0x17e28d0};
// Refreshes a matching ID for ordinary category-0 effects; different IDs can
// coexist. Categories and stateInfo have additional native exceptions.
inline constexpr Rva SPEFFECT_INSERT_OR_REFRESH_FN{0x17e3130};
// Per-node native update: SysV RDI=node, XMM0=elapsed time, EAX=result.
// Unconditional insertion starts with low flag bits 2 (pending), excluded from
// HP multipliers. This update activates it (4/8); zero elapsed time performs
// that transition without decrementing duration or interval timers. Not the
// container update; does not run its actor callbacks.
inline constexpr Rva SPEFFECT_UPDATE_NODE_FN{0x17ea980};
// Takes container, node, and update-counters flag; returns the next node.
// Unlinks and frees with the native allocator. Not a remove-by-ID API.
inline constexpr Rva SPEFFECT_REMOVE_NODE_FN{0x17e1d90};
// Event wrapper resolves an entity, finds its effect ID, and removes the
// matching node with counter maintenance. Requires native event context.
inline constexpr Rva EVENT_REMOVE_SP_EFFECT_BY_ID_FN{0x1331990};

inline constexpr std::size_t SPEFFECT_LIST_HEAD_OFFSET = 0x08;
inline constexpr std::size_t SPEFFECT_OWNER_OFFSET = 0x18;
inline constexpr std::size_t SPEFFECT_STATE_COUNTERS_OFFSET = 0x20;
inline constexpr std::size_t SPEFFECT_NODE_SIZE = 0x68;
inline constexpr std::size_t SPEFFECT_NODE_FLAGS_OFFSET = 0x1c;
inline constexpr std::size_t SPEFFECT_NODE_ID_OFFSET = 0x40;
inline constexpr std::size_t SPEFFECT_NODE_PARAM_ROW_OFFSET = 0x48;
inline constexpr std::size_t SPEFFECT_NODE_NEXT_OFFSET = 0x58;
inline constexpr std::size_t SPEFFECT_NODE_PREVIOUS_OFFSET = 0x60;
// The max-HP multiplier skips nodes with any of these bits set.
inline constexpr std::uint32_t SPEFFECT_HP_INACTIVE_MASK = 0x800c0003;

// Row flag at +0x162 bit 1. Excludes the row from the current-HP adjustment
// multiplier, but not the maximum-HP multiplier. The generated param type
// exposes b_curr_hp_independe_max_hp; prefer its accessor to raw bit writes.
inline constexpr std::size_t SPEFFECT_PARAM_CURRENT_HP_INDEPENDENT_BYTE_OFFSET = 0x162;
inline constexpr std::uint8_t SPEFFECT_PARAM_CURRENT_HP_INDEPENDENT_MASK = 0x02;

// Resolves combat team from +0x88 and effect/action overrides into a native
// team descriptor (vtable at +0, team value at +8).
inline constexpr Rva CHR_INS_RESOLVE_EFFECTIVE_TEAM_FN{0x18c2910};
// Native effect eligibility uses [source_team * 32 + target_team]. A
// relationship is not by itself proof that an actor is a map enemy.
inline constexpr Rva SP_EFFECT_OPPOSITION_MATRIX{0x4731560};
inline constexpr std::size_t SP_EFFECT_TEAM_MATRIX_STRIDE = 32;

}  // namespace bb
