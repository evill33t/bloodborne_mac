// SPDX-License-Identifier: GPL-2.0-or-later
#include "game_settings.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <unistd.h>

namespace MacSettings {
bool ValidServer(const std::string& value) {
    const auto start=value.starts_with("https://") ? 8u : value.starts_with("http://") ? 7u : 0u;
    if (!start || value.size()<=start || value.size()>512) return false;
    auto authority=value.substr(start);
    if (authority.back()=='/') authority.pop_back();
    if (authority.empty() || authority.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-:[]")!=std::string::npos) return false;
    std::size_t port=std::string::npos;
    if (authority.front()=='[') {
        const auto close=authority.find(']');
        if (close==std::string::npos || close<3 || authority.substr(1,close-1).find_first_not_of("0123456789abcdefABCDEF:.")!=std::string::npos) return false;
        if (close+1<authority.size()) { if (authority[close+1]!=':') return false; port=close+1; }
    } else {
        if (authority.find_first_of("[]")!=std::string::npos) return false;
        port=authority.find(':');
        if (authority.substr(0,port).find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")==std::string::npos) return false;
    }
    if (port!=std::string::npos) {
        if (port==0 || port+1==authority.size()) return false;
        try { std::size_t used=0; const int n=std::stoi(authority.substr(port+1),&used);
            if (used!=authority.size()-port-1 || n<1 || n>65535) return false;
        } catch (...) { return false; }
    }
    return true;
}
void Validate(Values& v) {
    if (v.frame_cap!=0 && v.frame_cap!=30 && v.frame_cap!=60 && v.frame_cap!=90 && v.frame_cap!=120 &&
        v.frame_cap!=144 && v.frame_cap!=165 && v.frame_cap!=240) v.frame_cap=30;
    bool valid=false;
    for (int k=0;k<ResolutionCount;++k)
        valid |= v.render_width==Widths[k] && v.render_height==Heights[k];
    if (!valid) { v.render_width=1920; v.render_height=1080; }
    v.sharpness=std::isfinite(v.sharpness) ? std::clamp(v.sharpness,0.0f,1.0f) : 0;
    if (v.anisotropy!=0 && v.anisotropy!=2 && v.anisotropy!=4 && v.anisotropy!=8 && v.anisotropy!=16) v.anisotropy=16;
    if (!ValidServer(v.server_url)) v.server_url=Values{}.server_url;
    if (!ValidServer(v.matchmaking_url)) v.matchmaking_url=Values{}.matchmaking_url;
    if (v.model_lod!=-2 && v.model_lod!=0 && v.model_lod!=1 && v.model_lod!=2) v.model_lod=0;
}
Values Load(const std::filesystem::path& path) {
    Values v;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input,line)) {
        auto comment=line.find_first_of("#;");
        line=line.substr(0,comment);
        auto eq=line.find('='); if (eq==std::string::npos) continue;
        auto trim=[](std::string s) {
            const auto a=s.find_first_not_of(" \t\r\n");
            return a==std::string::npos ? std::string{} : s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
        };
        const auto key=trim(line.substr(0,eq)), value=trim(line.substr(eq+1));
        try {
            size_t used=0;
            if (key=="online_server") { if (ValidServer(value)) v.server_url=value; continue; }
            if (key=="online_matchmaking") { if (ValidServer(value)) v.matchmaking_url=value; continue; }
            if (key=="render_resolution") {
                for (int k=0;k<ResolutionCount;++k)
                    if (value==std::to_string(Widths[k])+"x"+std::to_string(Heights[k])) {
                        v.render_width=Widths[k]; v.render_height=Heights[k];
                    }
                continue;
            }
            const float n=std::stof(value,&used);
            if (used!=value.size() || !std::isfinite(n)) continue;
            if (key=="frame_cap" && (n==0 || n==30 || n==60 || n==90 || n==120 || n==144 || n==165 || n==240)) v.frame_cap=int(n);
            else if (key=="model_lod" && (n==-2 || n==0 || n==1 || n==2)) v.model_lod=int(n);
            else if (key=="sharpness") v.sharpness=n;
            else if (key=="anisotropy" && (n==0 || n==2 || n==4 || n==8 || n==16)) v.anisotropy=int(n);
            else if (n==0 || n==1) {
                const bool b=n!=0;
                if (key=="metalfx") v.metalfx=b;
                else if (key=="temporal_aa") v.temporal_aa=b;
                else if (key=="vsync") v.vsync=b;
                else if (key=="fullscreen") v.fullscreen=b;
                else if (key=="show_fps") v.show_fps=b;
                else if (key=="effect_game_aa") v.game_aa=b;
                else if (key=="effect_motion_blur") v.motion_blur=b;
                else if (key=="effect_dof") v.dof=b;
                else if (key=="effect_chromatic_aberration") v.chromatic_aberration=b;
                else if (key=="effect_ssao") v.ssao=b;
                else if (key=="effect_dynamic_shadows") v.dynamic_shadows=b;
                else if (key=="online") v.online=b;
                else if (key=="optimized_lights") v.optimized_lights=b;
            }
        } catch (...) { /* Ignore a malformed entry; retain its default. */ }
    }
    Validate(v); return v;
}
bool Save(const std::filesystem::path& path,const Values& values,std::string& error) {
    // The native game menu and Cocoa panel can save on different threads.
    static std::mutex save_mutex;
    std::lock_guard lock(save_mutex);
    Values v=values; Validate(v);
    std::error_code ec;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),ec);
    if (ec) { error=ec.message(); return false; }
    const auto temp=path.string()+".tmp."+std::to_string(getpid());
    std::ofstream out(temp,std::ios::trunc);
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "# Bloodborne macOS settings. F1 or L3 + R3 opens the in-game menu.\n"
        << "frame_cap=" << v.frame_cap << "\nrender_resolution=" << v.render_width << 'x' << v.render_height
        << "\nmetalfx=" << v.metalfx << "\nvsync=" << v.vsync << "\nfullscreen=" << v.fullscreen
        << "\nshow_fps=" << v.show_fps << "\nsharpness=" << v.sharpness
        << "\nanisotropy=" << v.anisotropy << "\ntemporal_aa=" << v.temporal_aa
        << "\neffect_game_aa=" << v.game_aa << "\neffect_motion_blur=" << v.motion_blur
        << "\neffect_dof=" << v.dof << "\neffect_chromatic_aberration=" << v.chromatic_aberration
        << "\neffect_ssao=" << v.ssao << "\neffect_dynamic_shadows=" << v.dynamic_shadows
        << "\nonline=" << v.online << "\nonline_server=" << v.server_url
        << "\nonline_matchmaking=" << v.matchmaking_url << '\n';
    out << "optimized_lights=" << v.optimized_lights << "\nmodel_lod=" << v.model_lod << '\n';
    out.close();
    if (!out) { error="Could not write settings."; std::filesystem::remove(temp,ec); return false; }
    std::filesystem::rename(temp,path,ec);
    if (ec) { error=ec.message(); std::filesystem::remove(temp,ec); return false; }
    error.clear(); return true;
}
bool NeedsRestart(const Values& a,const Values& b) {
    return a.online!=b.online || a.server_url!=b.server_url || a.matchmaking_url!=b.matchmaking_url ||
        a.frame_cap!=b.frame_cap || a.render_width!=b.render_width || a.render_height!=b.render_height ||
        a.game_aa!=b.game_aa || a.motion_blur!=b.motion_blur || a.dof!=b.dof ||
        a.chromatic_aberration!=b.chromatic_aberration || a.ssao!=b.ssao ||
        a.dynamic_shadows!=b.dynamic_shadows;
}
}
