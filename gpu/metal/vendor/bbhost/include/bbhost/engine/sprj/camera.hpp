// Camera-system runtime metadata: SprjCamera, the ChrCam follow cameras and
// the WorldChrManDbg camera override.
#pragma once

#include "bbhost/engine/base.hpp"
#include "bbhost/engine/symbols.hpp"

namespace bb {

struct ChrIns;

inline constexpr std::size_t SPRJ_CAMERA_SIZE = 0x40;
inline constexpr std::size_t SPRJ_CAMERA_VIEW_COUNT = 5;
inline constexpr std::size_t SPRJ_CAMERA_VIEW_SIZE = 0x60;
inline constexpr std::size_t CHR_CAM_OWNER_SIZE = 0x1150;
inline constexpr std::size_t CHR_CAM_OWNER_EX_FOLLOW_CAM_OFFSET = 0x60;
inline constexpr std::size_t CHR_CAM_OWNER_FOLLOW_CAM_OFFSET = 0x68;
inline constexpr std::size_t CHR_CAM_OWNER_GLOBAL_OFFSET = 0x2830;
inline constexpr std::size_t CHR_EX_FOLLOW_CAM_SIZE = 0x350;
inline constexpr std::size_t CHR_FOLLOW_CAM_SIZE = 0x1d0;
inline constexpr std::size_t WORLD_CHR_MAN_DBG_SIZE = 0x140;

// Instruction boundary in ChrExFollowCam_Update right after the selected
// character/controller transform is scaled, before the camera basis is built.
// A hook site, not a callable function.
inline constexpr Rva CHR_EX_FOLLOW_CAM_PHYSICS_TRANSFORM_SITE{0x143b095};
// Instruction boundary in ChrExFollowCam_Update where the selected model
// transform root is loaded from ChrIns+0x3b0 -> +0x68. A hook site.
inline constexpr Rva CHR_EX_FOLLOW_CAM_MODEL_TRANSFORM_SITE{0x143b918};
// Instruction boundary in ChrIns_WriteInterpolatedTransformOutput where the
// interpolated transform delta is multiplied by the model-transform scalar; the
// next instruction adds and stores the output vector. Not a callback.
inline constexpr Rva CHR_INS_INTERPOLATED_TRANSFORM_DELTA_SITE{0x1e389e1};
// Entry of the per-character transform-output routine holding the site above.
// Descriptive name; class ownership is not recovered from RTTI.
inline constexpr Rva CHR_INS_WRITE_INTERPOLATED_TRANSFORM_OUTPUT_FN{0x1e38910};

// Observed SprjCamera singleton. Constructor RVA 0x1a07f10 initializes five
// 0x60 camera helpers at 0x08..0x28, a flag byte at 0x30 and an optional
// debug/menu node at 0x38.
struct SprjCamera {
    void* vftable;
    Unknown<SPRJ_CAMERA_VIEW_SIZE>* views[SPRJ_CAMERA_VIEW_COUNT];
    std::uint8_t flags;
    Unknown<0x07> _pad31;
    Unknown<1>* debug_node;

    static constexpr Rva SINGLETON_PTR = SPRJ_CAMERA_SINGLETON_PTR;
};

// Extended player-follow camera at ChrCam owner +0x60. RVA 0x143a0e0
// constructs this 0x350-byte object and RVA 0x143ac60 updates it. +0x140/+0x144
// are the angles consumed while building the camera basis, not render matrices.
struct ChrExFollowCam {
    Unknown<0x40> _unk000;
    // Final camera world position written by RVA 0x143ac60.
    float camera_position[4];
    Unknown<0x70> _unk050;
    // Reference position used to derive the final orbit direction.
    float orbit_reference_position[4];
    // Follow focus. Live remote-follow tests put this at the remote character
    // position plus the camera-height offset.
    float focus_position[4];
    Unknown<0x60> _unk0e0;
    float current_rotation_x;
    float current_rotation_y;
    float target_rotation_x;
    float target_rotation_y;
    Unknown<0x48> _unk150;
    float chr_translation_chase_rate;
    float chr_translation_chase_rate_normal;
    float chr_translation_chase_rate_escape_wall;
    float target_translation_chase_rate;
    float target_rotation_x_chase_rate;
    float target_chase_rate_xz;
    float target_chase_rate_xz_normal;
    float target_chase_rate_xz_escape_wall;
    float target_chase_rate_y;
    float target_chase_rate_min_angle;
    float target_chase_rate_max_angle;
    float target_chase_distance_rate;
    float target_chase_rate_lock_xz;
    float target_chase_rate_lock_y;
    float target_chase_rate_min_angle_lock;
    float target_chase_rate_max_angle_lock;
    float translation_control_end_wait_time;
    float translation_control_end_release_time;
    float rotation_control_end_wait_time;
    float rotation_control_end_release_time;
    float rotation_high_speed_rate_increase_time;
    float rotation_range_max_x;
    float rotation_range_min_x;
    float rotation_range_max_x_lock;
    float rotation_range_min_x_lock;
    float rotation_range_lerp_height_begin;
    float rotation_range_lerp_height_end;
    float rotation_speed_x;
    float rotation_speed_y;
    float rotation_speed_min_x;
    float rotation_speed_min_y;
    float rotation_speed_max_x;
    float rotation_speed_max_y;
    float rotation_high_speed_x;
    float rotation_high_speed_y;
    float rotation_high_speed_min_x;
    float rotation_high_speed_min_y;
    float rotation_high_speed_max_x;
    float rotation_high_speed_max_y;
    float option_rotation_speed_rate;
    float direct_pitch_angle;
    float direct_pitch_rate;
    float lock_rotation_chase_rate;
    float lock_rotation_chase_play_angle;
    float lock_rotation_x_shift_ratio;
    Unknown<0x20> _unk24c;
    // Automatic pitch/yaw targets used by the follow-angle chase path.
    float automatic_desired_pitch;
    float automatic_desired_yaw;
    Unknown<0x20> _unk274;
    // Extra yaw correction subtracted by one update branch. Bypassing it did not
    // stop the observed remote-follow orbit.
    float per_frame_yaw_correction;
    Unknown<0xb8> _unk298;

    static constexpr Rva UPDATE_FN = CHR_EX_FOLLOW_CAM_UPDATE_FN;
    static constexpr Rva OWNER_ROOT_PTR = CHR_CAM_OWNER_ROOT_PTR;
};

// Legacy follow camera at ChrCam owner +0x68. RVA 0x14332a0 constructs this
// 0x1d0-byte object; owner update mode 2 advances it through RVA 0x1433860.
struct ChrFollowCam {
    Unknown<CHR_FOLLOW_CAM_SIZE> _unk000;
};

// Owner selecting between the legacy and extended follow-camera paths.
struct ChrCamOwner {
    Unknown<0x60> _unk000;
    ChrExFollowCam* ex_follow_cam;
    ChrFollowCam* follow_cam;
    Unknown<0x08> _unk070;
    Unknown<1>* camera_helper;
    // One-shot reset request consumed by RVA 0x14368b0: resets the extended,
    // legacy and helper camera state, then is cleared.
    bool camera_reset_requested;
    Unknown<0x03> _pad081;
    // Observed values: 2 updates follow_cam; 3 updates ex_follow_cam.
    std::int32_t active_mode;
    Unknown<0x10c8> _unk088;
};

// Camera-related fields of the WorldChrManDbg singleton.
struct WorldChrManDbg {
    Unknown<0x100> _unk000;
    // Unidentified debug context next to the camera override; the native camera
    // caller at RVA 0x193bdee does not consume it.
    Unknown<1>* unknown_context_100;
    // Nullable ChrIns override. Null selects WorldChrMan::main_player.
    ChrIns* camera_follow_override;
    Unknown<0x30> _unk110;

    static constexpr Rva SINGLETON_PTR = WORLD_CHR_MAN_DBG_SINGLETON_PTR;
};

// Marker for the observed SprjCameraStep task class (runtime-class
// registration and three named step callbacks). Not a layout type.
struct SprjCameraStep {
    static constexpr const RuntimeClassSymbol& RUNTIME_CLASS = SPRJ_CAMERA_STEP_RUNTIME_CLASS;
    static constexpr const StepTemplateSymbol& STEP_TEMPLATE = SPRJ_CAMERA_STEP_TEMPLATE;
};

namespace detail::camera_layout {
static_assert(SprjCamera::SINGLETON_PTR.rva == 0x553e8f8, "SprjCamera::SINGLETON_PTR");
static_assert(SprjCameraStep::RUNTIME_CLASS.runtime_class_ptr.rva == 0x5579e60, "SprjCameraStep runtime class");
static_assert(SprjCameraStep::STEP_TEMPLATE.table.rva == 0x5579e00, "SprjCameraStep step table");
static_assert(SprjCameraStep::STEP_TEMPLATE.steps_count == 3, "SprjCameraStep step count");
BB_SIZE(SprjCamera, SPRJ_CAMERA_SIZE);
BB_OFFSET(SprjCamera, views, 0x08);
BB_OFFSET(SprjCamera, flags, 0x30);
BB_OFFSET(SprjCamera, debug_node, 0x38);
BB_SIZE(ChrExFollowCam, CHR_EX_FOLLOW_CAM_SIZE);
BB_OFFSET(ChrExFollowCam, camera_position, 0x40);
BB_OFFSET(ChrExFollowCam, orbit_reference_position, 0xc0);
BB_OFFSET(ChrExFollowCam, focus_position, 0xd0);
BB_OFFSET(ChrExFollowCam, current_rotation_x, 0x140);
BB_OFFSET(ChrExFollowCam, current_rotation_y, 0x144);
BB_OFFSET(ChrExFollowCam, target_rotation_x, 0x148);
BB_OFFSET(ChrExFollowCam, target_rotation_y, 0x14c);
BB_OFFSET(ChrExFollowCam, chr_translation_chase_rate, 0x198);
BB_OFFSET(ChrExFollowCam, rotation_speed_x, 0x204);
BB_OFFSET(ChrExFollowCam, rotation_speed_y, 0x208);
BB_OFFSET(ChrExFollowCam, direct_pitch_rate, 0x23c);
BB_OFFSET(ChrExFollowCam, lock_rotation_chase_rate, 0x240);
BB_OFFSET(ChrExFollowCam, lock_rotation_x_shift_ratio, 0x248);
BB_OFFSET(ChrExFollowCam, automatic_desired_pitch, 0x26c);
BB_OFFSET(ChrExFollowCam, automatic_desired_yaw, 0x270);
BB_OFFSET(ChrExFollowCam, per_frame_yaw_correction, 0x294);
static_assert(ChrExFollowCam::UPDATE_FN.rva == 0x143ac60, "ChrExFollowCam::UPDATE_FN");
static_assert(ChrExFollowCam::OWNER_ROOT_PTR.rva == 0x553e860, "ChrExFollowCam::OWNER_ROOT_PTR");
BB_SIZE(ChrFollowCam, CHR_FOLLOW_CAM_SIZE);
BB_SIZE(ChrCamOwner, CHR_CAM_OWNER_SIZE);
BB_OFFSET(ChrCamOwner, ex_follow_cam, CHR_CAM_OWNER_EX_FOLLOW_CAM_OFFSET);
BB_OFFSET(ChrCamOwner, follow_cam, CHR_CAM_OWNER_FOLLOW_CAM_OFFSET);
BB_OFFSET(ChrCamOwner, camera_helper, 0x78);
BB_OFFSET(ChrCamOwner, camera_reset_requested, 0x80);
BB_OFFSET(ChrCamOwner, active_mode, 0x84);
BB_SIZE(WorldChrManDbg, WORLD_CHR_MAN_DBG_SIZE);
BB_OFFSET(WorldChrManDbg, unknown_context_100, 0x100);
BB_OFFSET(WorldChrManDbg, camera_follow_override, 0x108);
static_assert(WorldChrManDbg::SINGLETON_PTR.rva == 0x553e880, "WorldChrManDbg::SINGLETON_PTR");
}  // namespace detail::camera_layout

}  // namespace bb
