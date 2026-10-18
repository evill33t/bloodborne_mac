// SprjEzSelectBot automation keys and native producers/consumers.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

// Shared keyed-value container used by the EzSelect automation paths. This is
// not an EzSelect singleton: SprjEzSelectBot is a path namespace in this
// shared container, so the keys are modelled instead of an object layout.
inline constexpr Rva SPRJ_EZ_SELECT_BOT_VALUE_CONTAINER_PTR{0x553d710};

// Consumes PlayerWarp values and performs an in-map player warp when the
// requested map is already resident/current. The accepted warp path calls
// WorldRes_PreparePlayerMapTransition, including the canonical null detach for
// the current main player's map-collision entry.
inline constexpr Rva SPRJ_EZ_SELECT_BOT_UPDATE_PLAYER_WARP_FN{0x1548740};

// Captures a map id, player position/facing, and camera angles into the keyed
// container, then enables the move-map and player-warp bot lanes.
inline constexpr Rva SPRJ_EZ_SELECT_BOT_QUEUE_MOVE_MAP_FN{0x154ea30};

enum class SprjEzSelectBotValueKind : std::uint8_t {
    Boolean,
    Float,
    MapName,
    Namespace,
};

struct SprjEzSelectBotKey {
    const char* path;
    Rva string_address;
    SprjEzSelectBotValueKind kind;
};

// Sorted by path.
inline constexpr SprjEzSelectBotKey SPRJ_EZ_SELECT_BOT_KEYS[] = {
    {"SprjEzSelectBot.DebugBootMenuStep", Rva{0x4988322}, SprjEzSelectBotValueKind::Namespace},
    {"SprjEzSelectBot.MenuViewerStep.ItemName", Rva{0x49c0c36}, SprjEzSelectBotValueKind::MapName},
    {"SprjEzSelectBot.MoveMapListStep", Rva{0x49891fc}, SprjEzSelectBotValueKind::Namespace},
    {"SprjEzSelectBot.MoveMapListStep.EnableBot", Rva{0x4968442}, SprjEzSelectBotValueKind::Boolean},
    {"SprjEzSelectBot.MoveMapListStep.ItemName", Rva{0x49683f0}, SprjEzSelectBotValueKind::MapName},
    {"SprjEzSelectBot.MoveMapStep.IsDebugExit", Rva{0x49877bc}, SprjEzSelectBotValueKind::Boolean},
    {"SprjEzSelectBot.PlayerWarp", Rva{0x496814a}, SprjEzSelectBotValueKind::Namespace},
    {"SprjEzSelectBot.PlayerWarp.EnableBot", Rva{0x49681d6}, SprjEzSelectBotValueKind::Boolean},
    {"SprjEzSelectBot.PlayerWarp.cDegX", Rva{0x496836c}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.cDegY", Rva{0x49683ae}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.degX", Rva{0x49682ec}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.degY", Rva{0x496832c}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.igPosX", Rva{0x4968220}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.igPosY", Rva{0x4968264}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.PlayerWarp.igPosZ", Rva{0x49682a8}, SprjEzSelectBotValueKind::Float},
    {"SprjEzSelectBot.SprjModelViewerList", Rva{0x498e2b2}, SprjEzSelectBotValueKind::Namespace},
};
inline constexpr std::size_t SPRJ_EZ_SELECT_BOT_KEY_COUNT =
    sizeof(SPRJ_EZ_SELECT_BOT_KEYS) / sizeof(SPRJ_EZ_SELECT_BOT_KEYS[0]);

namespace detail::ez_select_bot_layout {
static_assert(SPRJ_EZ_SELECT_BOT_KEY_COUNT == 16, "SPRJ_EZ_SELECT_BOT_KEYS count");
static_assert(SPRJ_EZ_SELECT_BOT_KEYS[6].string_address.rva == 0x496814a, "PlayerWarp anchor");
}  // namespace detail::ez_select_bot_layout

}  // namespace bb
