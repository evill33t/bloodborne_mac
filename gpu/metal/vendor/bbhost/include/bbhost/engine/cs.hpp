// Bloodborne CS classes and systems: every cs/*.hpp header, plus the inventory
// of CS class names observed in the eboot's strings. The inventory is
// deliberately not a layout claim; it shows that Bloodborne already uses a CS
// layer alongside SPRJ and FD4.
#pragma once

#include "bbhost/engine/base.hpp"

#include "bbhost/engine/cs/chr_asm_info_transfer.hpp"
#include "bbhost/engine/cs/chr_create_beh_chara_fragment.hpp"
#include "bbhost/engine/cs/chr_thread.hpp"
#include "bbhost/engine/cs/chr_update_fragments.hpp"
#include "bbhost/engine/cs/cloth_thread.hpp"
#include "bbhost/engine/cs/collect_near_navi_mesh_parts.hpp"
#include "bbhost/engine/cs/death_events.hpp"
#include "bbhost/engine/cs/display_ghost.hpp"
#include "bbhost/engine/cs/dlc.hpp"
#include "bbhost/engine/cs/dungeon_ritual_helper.hpp"
#include "bbhost/engine/cs/enterleave_direction_step.hpp"
#include "bbhost/engine/cs/enterleave_director.hpp"
#include "bbhost/engine/cs/entryfilelist.hpp"
#include "bbhost/engine/cs/ez_offscreen_rend.hpp"
#include "bbhost/engine/cs/ez_work.hpp"
#include "bbhost/engine/cs/ez_work_pool.hpp"
#include "bbhost/engine/cs/gparam_bank.hpp"
#include "bbhost/engine/cs/hit_floor_info.hpp"
#include "bbhost/engine/cs/hk_beh_chara_creater.hpp"
#include "bbhost/engine/cs/lams_capture_step.hpp"
#include "bbhost/engine/cs/lams_capture_thread.hpp"
#include "bbhost/engine/cs/lod.hpp"
#include "bbhost/engine/cs/menu_asm.hpp"
#include "bbhost/engine/cs/menu_asm_model_rend.hpp"
#include "bbhost/engine/cs/model_ins.hpp"
#include "bbhost/engine/cs/movie.hpp"
#include "bbhost/engine/cs/movie_ins.hpp"
#include "bbhost/engine/cs/multi_npc_player_ins_task.hpp"
#include "bbhost/engine/cs/multi_play.hpp"
#include "bbhost/engine/cs/multi_player_ins_task.hpp"
#include "bbhost/engine/cs/network_flow_step.hpp"
#include "bbhost/engine/cs/now_loading.hpp"
#include "bbhost/engine/cs/platform_network.hpp"
#include "bbhost/engine/cs/playgo.hpp"
#include "bbhost/engine/cs/prefetch_for_slow_storage_step.hpp"
#include "bbhost/engine/cs/ps4_trophy_step.hpp"
#include "bbhost/engine/cs/ps_plus_check_step.hpp"
#include "bbhost/engine/cs/request_get_sos_step.hpp"
#include "bbhost/engine/cs/request_summon_step.hpp"
#include "bbhost/engine/cs/session_connect_state_step.hpp"
#include "bbhost/engine/cs/slope_ctrl.hpp"
#include "bbhost/engine/cs/trophy.hpp"

namespace bb {

// Sorted and unique.
inline constexpr const char* OBSERVED_CLASSES[] = {
    "CSChrAsmInfoTransfer",
    "CSChrCreateBehCharaFragment",
    "CSChrThread",
    "CSChrUpdatePostFragment",
    "CSChrUpdatePreFragment",
    "CSClothThread",
    "CSCollectNearNaviMeshParts",
    "CSDisplayGhost",
    "CSDlc",
    "CSDungeonRitualHelper",
    "CSEnterleaveDirectionStep",
    "CSEntryfilelistFileCap",
    "CSEntryfilelistRepository",
    "CSEntryfilelistRepositoryImp",
    "CSEntryfilelistResCap",
    "CSEntryfilelistbndFileCap",
    "CSEzOffscreenRend",
    "CSEzWork",
    "CSEzWorkPool",
    "CSGparamBank",
    "CSGparamBankImp",
    "CSHitFloorInfo",
    "CSHkBehCharaCreater",
    "CSLamsCaptureStep",
    "CSLamsCaptureThread",
    "CSLod",
    "CSMenuAsmModelRend",
    "CSModelIns",
    "CSMovie",
    "CSMovieIns",
    "CSMultiNPCPlayerInsTask",
    "CSMultiPlayMan",
    "CSMultiPlayerInsTask",
    "CSNetworkFlowStep",
    "CSNowLoadingHelper",
    "CSPS4TrophyStep",
    "CSPSPlusCheckStep",
    "CSPlatformNetworkMan",
    "CSPlaygo",
    "CSPrefetchForSlowStorageStep",
    "CSRequestGetSosStep",
    "CSRequestSummonStep",
    "CSSessionConnectStateStep",
    "CSSlopeCtrl",
    "CSTrophy",
};
inline constexpr std::size_t OBSERVED_CLASS_COUNT = sizeof(OBSERVED_CLASSES) / sizeof(OBSERVED_CLASSES[0]);

enum class ObservedStringEncoding {
    Ascii,
    Utf16,
};

struct ObservedCsString {
    const char* value;
    Rva address;
    ObservedStringEncoding encoding;
};

// Sorted and unique by value.
inline constexpr ObservedCsString OBSERVED_DEFERRED_STRINGS[] = {
    {"CSEntryfilelistRepository", Rva{0x493ad11}, ObservedStringEncoding::Ascii},
    {"CSGparamBank", Rva{0x4935187}, ObservedStringEncoding::Ascii},
};
inline constexpr std::size_t OBSERVED_DEFERRED_STRING_COUNT =
    sizeof(OBSERVED_DEFERRED_STRINGS) / sizeof(OBSERVED_DEFERRED_STRINGS[0]);

namespace detail::cs_layout {
static_assert(OBSERVED_CLASS_COUNT == 45, "OBSERVED_CLASSES");
static_assert(OBSERVED_DEFERRED_STRINGS[0].address.rva == 0x493ad11, "CSEntryfilelistRepository string");
static_assert(OBSERVED_DEFERRED_STRINGS[0].encoding == ObservedStringEncoding::Ascii, "CSEntryfilelistRepository string");
}  // namespace detail::cs_layout

}  // namespace bb
