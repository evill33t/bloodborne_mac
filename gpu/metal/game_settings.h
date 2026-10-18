// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <filesystem>
#include <string>

namespace MacSettings {
struct Values {
    int frame_cap = 30; // 0 = uncapped; the timing mode changes at restart
    int render_width = 1920, render_height = 1080;
    bool metalfx = false, vsync = true, fullscreen = false, show_fps = true;
    float sharpness = 0;
    int anisotropy = 16; // 0 preserves the game's samplers
    bool temporal_aa = false; // opt-in, resolved before HUD/post-processing
    bool game_aa = true, motion_blur = true, dof = true;
    bool chromatic_aberration = true, ssao = true, dynamic_shadows = true;
    bool online = false;
    bool optimized_lights = false; // opt-in until measured with the direct Metal backend
    int model_lod = 0; // -2 highest, 0 original, 1 lower, 2 lowest
    std::string server_url = "https://thehuntersdream.com";
    std::string matchmaking_url = "https://thehuntersdream.com:18671";
    bool operator==(const Values&) const = default;
};
inline constexpr int Widths[]{960,1280,1440,1600,1920,2560,3840};
inline constexpr int Heights[]{540,720,810,900,1080,1440,2160};
inline constexpr int ResolutionCount = 7;
void Validate(Values& values);
bool ValidServer(const std::string& value);
Values Load(const std::filesystem::path& path);
bool Save(const std::filesystem::path& path,const Values& values,std::string& error);
bool NeedsRestart(const Values& current,const Values& startup);
}
