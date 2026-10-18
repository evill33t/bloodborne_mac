// WorldBackRead: the distance-based character/collision backread scheduler and its two profiles.
#pragma once

#include "bbhost/engine/base.hpp"

namespace bb {

inline constexpr std::size_t WORLD_BACK_READ_SIZE = 0xfd0;
inline constexpr std::size_t WORLD_BACK_READ_PROFILE_SIZE = 0x7a8;
inline constexpr std::size_t WORLD_BACK_READ_BLOCK_CAPACITY = 0x3c;

inline constexpr float WORLD_BACK_READ_NORMAL_START_DISTANCE = 20.0f;
inline constexpr float WORLD_BACK_READ_NORMAL_END_DISTANCE = 25.0f;
inline constexpr float WORLD_BACK_READ_HIT_START_DISTANCE = 80.0f;
inline constexpr float WORLD_BACK_READ_HIT_END_DISTANCE = 90.0f;
inline constexpr float WORLD_BACK_READ_START_DELAY = 0.2f;
inline constexpr float WORLD_BACK_READ_END_DELAY = 1.3f;

// Bounds used by the native WorldBackRead debug controls.
inline constexpr float WORLD_BACK_READ_DEBUG_DISTANCE_MIN = 1.0f;
inline constexpr float WORLD_BACK_READ_DEBUG_DISTANCE_MAX = 120.0f;
inline constexpr float WORLD_BACK_READ_DEBUG_DISTANCE_STEP = 1.0f;
inline constexpr float WORLD_BACK_READ_DEBUG_DELAY_MAX = 10.0f;
inline constexpr float WORLD_BACK_READ_DEBUG_DELAY_STEP = 0.1f;

// Distance and delay policy used by WorldBackRead for one candidate lane.
// Bloodborne constructs two profiles: normal defaults to 20/25 metres and hit
// to 80/90 metres, both with 0.2/1.3 second start/end delays.
// WorldBackRead_Update @ RVA 0x15556b0 passes the greater of the two end
// distances to SprjWorldNvmManagerImp_CollectBackreadPartsByDistance @ RVA
// 0x1e2cfd0. The 60 records are rebuilt from live candidates during each
// profile update.
struct WorldBackReadProfile {
    float start_distance;
    float end_distance;
    float start_delay;
    float end_delay;
    Unknown<0x38>* candidate_list;
    Unknown<0x20> _pad18[WORLD_BACK_READ_BLOCK_CAPACITY];
    void* runtime_config;
    void* debug_config;
};

// Character backread scheduler owned by the active world-load context. It
// controls distance-based character/collision candidate activation and is
// distinct from WorldRes file and renderer residency: changing these profiles
// can reduce activation churn but does not retain SprjFile handles or map
// geometry by itself.
struct WorldBackRead {
    void* vftable;
    void* owner;
    void* candidate_source;
    std::uintptr_t _unk18;
    std::uintptr_t _unk20;
    std::uintptr_t _unk28;
    float current_position[4];
    float previous_position[4];
    bool position_dirty;
    Unknown<0x07> _pad51;
    WorldBackReadProfile normal;
    WorldBackReadProfile hit;
    Unknown<0x08> _navigation_query;
    void* candidate_tree;
    Unknown<0x18> _tailfb8;
};

namespace detail::world_back_read_layout {
BB_SIZE(WorldBackReadProfile, WORLD_BACK_READ_PROFILE_SIZE);
BB_OFFSET(WorldBackReadProfile, start_distance, 0x00);
BB_OFFSET(WorldBackReadProfile, end_distance, 0x04);
BB_OFFSET(WorldBackReadProfile, start_delay, 0x08);
BB_OFFSET(WorldBackReadProfile, end_delay, 0x0c);
BB_OFFSET(WorldBackReadProfile, candidate_list, 0x10);
BB_OFFSET(WorldBackReadProfile, runtime_config, 0x798);
BB_OFFSET(WorldBackReadProfile, debug_config, 0x7a0);
BB_SIZE(WorldBackRead, WORLD_BACK_READ_SIZE);
BB_OFFSET(WorldBackRead, current_position, 0x30);
BB_OFFSET(WorldBackRead, previous_position, 0x40);
BB_OFFSET(WorldBackRead, position_dirty, 0x50);
BB_OFFSET(WorldBackRead, normal, 0x58);
BB_OFFSET(WorldBackRead, hit, 0x800);
BB_OFFSET(WorldBackRead, candidate_tree, 0xfb0);
}  // namespace detail::world_back_read_layout

}  // namespace bb
