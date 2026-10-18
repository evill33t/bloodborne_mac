// Incremental path-distance collection owned by SprjWorldNvmManager.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"
#include "bbhost/engine/sprj/world_nvm.hpp"

namespace bb {

struct SprjNaviMeshParts;

inline constexpr Rva COLLECT_NEAR_NAVI_WAIT_ADDRESS{0x1e19690};
inline constexpr Rva COLLECT_NEAR_NAVI_GET_PATH_DIST_ADDRESS{0x1e198f0};
inline constexpr Rva COLLECT_NEAR_NAVI_COLLECT_ADDRESS{0x1e1a170};
inline constexpr Rva COLLECT_NEAR_NAVI_RESULT_ADDRESS{0x1e1a1f0};
inline constexpr Rva COLLECT_NEAR_NAVI_FINISH_ADDRESS{0x1e1a2d0};

// Five named callbacks. The generic dispatcher's wider bounds are not states.
enum class CSCollectNearNaviMeshPartsStep : std::int32_t {
    Wait = 0,
    GetPathDist = 1,
    CollectNearby = 2,
    Result = 3,
    Finish = 4,
};

// Descriptive 0x20-byte inline query record. Submit copies a full position
// vector and location pair, and stores the distance limit without validation.
struct CSCollectNearNaviQuery {
    float position[4];
    NaviMeshLocation location;
    float max_path_distance;
    Unknown<4> _unk1c;
};

// Descriptive 0x10-byte candidate produced by GetPathDist. Pointers borrow the
// world's navigation-part pool; the index identifies the source part's exit.
struct CSCollectNearNaviCandidate {
    SprjNaviMeshParts* part;
    float distance;
    std::int32_t source_connection_index;
};

// Native 0x28-byte wrapper with a vector at +8. Constructor reserves 64 rows
// (initial capacity, not a maximum). Leading word is opaque.
struct CSCollectNearNaviCandidates {
    Unknown<8> _unk00;
    DLVector<CSCollectNearNaviCandidate> entries;
};

// Reflected 0x170-byte local step, separately allocated aligned to 16 and owned
// at SprjWorldNvmManager +0x2c00. No inline Sprj scheduling task exists; the
// manager exposes explicit submit, dispatch, and result-copy functions.
//
// Wait consumes the pending query and seeds the result map with its source
// part at distance zero. GetPathDist measures one source connection per call;
// CollectNearby recursively extends those routes using interconnection costs.
// Result is a stable ready state. Taking results copies the map and
// invalidates the active location, so a later Result update returns to Wait.
//
// A newer valid query restarts for XYZ displacement squared >= 9. Smaller moves
// discard the pending query even if its IDs or distance limit changed. Results
// are borrowed part pointers; native teardown frees containers, not parts.
struct alignas(16) CSCollectNearNaviMeshParts {
    // RUNTIME_CLASS: probable base "SprjStepLocal< CSCollectNearNaviMeshParts >".
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = CS_COLLECT_NEAR_NAVI_MESH_PARTS_RUNTIME_CLASS;
    // STEP_TEMPLATE: its five step callbacks are the COLLECT_NEAR_NAVI_*_ADDRESS constants in order.
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = CS_COLLECT_NEAR_NAVI_MESH_PARTS_TEMPLATE;
    static constexpr std::size_t SIZE = 0x170;
    static constexpr Rva VTABLE{0x534ea00};
    static constexpr Rva CONSTRUCTOR_FN{0x1e191d0};
    static constexpr Rva DESTRUCTOR_FN{0x1e19550};
    static constexpr Rva SIZE_GETTER_FN{0x1e1cde0};
    static constexpr Rva DISPATCH_FN{0x1e1c4e0};
    static constexpr Rva ADVANCE_FN{0x1e1da30};
    static constexpr Rva SUBMIT_FN{0x1e1a2e0};
    static constexpr Rva HAS_RESULT_FN{0x1e1a300};
    // Clears the caller's map, copies entries, then invalidates active location.
    // Does not check Result state, clear the source map, or transfer ownership.
    static constexpr Rva COPY_RESULT_FN{0x1e1a320};
    static constexpr Rva EXPAND_CONNECTIONS_FN{0x1e20d60};
    static constexpr std::size_t INITIAL_CANDIDATE_CAPACITY = 64;
    static constexpr float RESTART_DISTANCE_SQUARED = 9.0f;

    const void* vftable;
    const void* callback_table;
    Unknown<0x40> _condition_dispatcher10;
    std::int32_t current_step;
    std::int32_t requested_step;
    std::uint8_t continue_this_update;
    Unknown<7> _unk59;
    void* allocator;
    std::uint8_t debug_flags[2];
    Unknown<6> _unk6a;
    void* debug_menu;
    Unknown<0x38> _debug_string78;
    std::int32_t* execution_counts;
    // Conditionally refreshed by the debug dispatcher; may be stale.
    const std::uint16_t* execution_label;
    std::uint8_t debug_step_requested;
    Unknown<3> _unkc1;
    std::int32_t debug_step;
    Unknown<8> _unkc8;
    CSCollectNearNaviQuery pending;
    CSCollectNearNaviQuery active;
    NaviMeshDistanceMap distances;
    CSCollectNearNaviCandidates candidates;
    SprjNaviMeshParts* source_part;
    std::int32_t next_connection_index;
    Unknown<12> _unk164;

    // True when current_step names one of the five registered steps.
    bool step_known() const { return current_step >= 0 && current_step <= 4; }
    CSCollectNearNaviMeshPartsStep step() const { return static_cast<CSCollectNearNaviMeshPartsStep>(current_step); }
    bool has_result() const { return current_step == static_cast<std::int32_t>(CSCollectNearNaviMeshPartsStep::Result); }
    bool is_finished() const { return current_step == -1; }

    // Ordered comparison from GetPathDist/Result; NaN does not restart.
    static bool movement_requires_restart(float distance_squared) {
        return distance_squared >= RESTART_DISTANCE_SQUARED;
    }
    // Strict bounds for a newly measured route. The source seed at zero is
    // inserted separately. NaN in either operand fails this predicate.
    static bool accepts_path_distance(float distance, float limit) { return distance > 0.0f && distance < limit; }
};

namespace detail::collect_near_navi_mesh_parts_layout {
using T = CSCollectNearNaviMeshParts;
BB_SIZE(T, 0x170);
BB_SIZE(T, T::SIZE);
static_assert(alignof(T) == 16, "alignof(CSCollectNearNaviMeshParts)");
BB_OFFSET(T, callback_table, 0x08);
BB_OFFSET(T, _condition_dispatcher10, 0x10);
BB_OFFSET(T, current_step, 0x50);
BB_OFFSET(T, requested_step, 0x54);
BB_OFFSET(T, continue_this_update, 0x58);
BB_OFFSET(T, allocator, 0x60);
BB_OFFSET(T, debug_flags, 0x68);
BB_OFFSET(T, debug_menu, 0x70);
BB_OFFSET(T, _debug_string78, 0x78);
BB_OFFSET(T, execution_counts, 0xb0);
BB_OFFSET(T, execution_label, 0xb8);
BB_OFFSET(T, debug_step_requested, 0xc0);
BB_OFFSET(T, debug_step, 0xc4);
BB_OFFSET(T, pending, 0xd0);
BB_OFFSET(T, active, 0xf0);
BB_OFFSET(T, distances, 0x110);
BB_OFFSET(T, candidates, 0x130);
BB_OFFSET(T, source_part, 0x158);
BB_OFFSET(T, next_connection_index, 0x160);
BB_SIZE(CSCollectNearNaviQuery, 0x20);
BB_OFFSET(CSCollectNearNaviQuery, location, 0x10);
BB_OFFSET(CSCollectNearNaviQuery, max_path_distance, 0x18);
BB_SIZE(CSCollectNearNaviCandidate, 0x10);
BB_OFFSET(CSCollectNearNaviCandidate, distance, 0x08);
BB_OFFSET(CSCollectNearNaviCandidate, source_connection_index, 0x0c);
BB_SIZE(CSCollectNearNaviCandidates, 0x28);
BB_OFFSET(CSCollectNearNaviCandidates, entries, 0x08);
}  // namespace detail::collect_near_navi_mesh_parts_layout

}  // namespace bb
