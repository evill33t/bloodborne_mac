// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/game_settings.h"
#include <cassert>
#include <fstream>
#include <limits>
#include <atomic>
#include <thread>
#include <unistd.h>

int main() {
    using namespace MacSettings;
    char directory[]="/tmp/bb-settings-XXXXXX"; assert(mkdtemp(directory));
    const auto path=std::filesystem::path(directory)/"nested/settings.ini";
    Values v=Load(path); assert(v==Values{});
    v.frame_cap=90; v.render_width=1280; v.render_height=720; v.metalfx=true;
    v.game_aa=false; v.motion_blur=false; v.dof=false; v.chromatic_aberration=false;
    v.ssao=false; v.dynamic_shadows=false; v.vsync=false; v.show_fps=false;
    v.fullscreen=true; v.sharpness=1.0f/3;
    v.anisotropy=8; v.temporal_aa=true;v.frame_generation=true;
    std::string error; assert(Save(path,v,error) && error.empty());
    assert(Load(path)==v && NeedsRestart(v,Values{}));
    for (int cap : {30,60,90,120,144,165,240,0}) {
        v.frame_cap=cap; assert(Save(path,v,error)); assert(Load(path)==v);
    }
    v=Values{}; v.metalfx=true; v.vsync=false; v.show_fps=false; v.sharpness=1;v.frame_generation=true;
    assert(!NeedsRestart(v,Values{}));
    for (int filter:{0,2,4,8,16}) {
        v.anisotropy=filter; v.temporal_aa=true;
        assert(Save(path,v,error) && Load(path)==v && !NeedsRestart(v,Values{}));
    }
    v.frame_cap=120; assert(NeedsRestart(v,Values{}));
    v=Values{}; v.online=true; v.server_url="http://127.0.0.1:18671";
    v.matchmaking_url="http://[::1]:18671"; v.optimized_lights=true; v.model_lod=-2;
    assert(Save(path,v,error) && Load(path)==v && NeedsRestart(v,Values{}));
    for (const auto* url:{"file:///tmp/test","https://a@b","https://[","http://host:0","https://host:65536","https://host/path","https://host\r\nX:1"}) assert(!ValidServer(url));
    v=Values{}; v.nightwatch_mode=1; v.nightwatch_players=4; v.nightwatch_port=47600;
    v.nightwatch_name="Hunter_2"; v.nightwatch_code="BBP3-NJR0-E43N-WWN9-2C2P"; v.nightwatch_rot=true;
    assert(Save(path,v,error) && Load(path)==v && NeedsRestart(v,Values{}));
    for (int role:{0,1,2}) { v.nightwatch_mode=role; assert(Save(path,v,error) && Load(path)==v); }
    for (const auto* name:{"","two hunters","x\ny","../save","12345678901234567"}) assert(!ValidNightwatchName(name));
    for (const auto* code:{"BBP3-code\nnightwatch_mode=1","http://host","BBP3-code;comment"}) assert(!ValidNightwatchCode(code));
    assert(ValidNightwatchCode("BBP3-CODE+6-ABCD"));
    v.nightwatch_mode=3;v.nightwatch_players=20;v.nightwatch_port=65536;v.nightwatch_name="bad name";v.nightwatch_code="bad\ncode";
    Validate(v); assert(v.nightwatch_mode==0 && v.nightwatch_players==3 && v.nightwatch_port==9307);
    assert(v.nightwatch_name=="Hunter" && v.nightwatch_code.empty());
    std::ofstream(path) << "frame_cap=60junk\nrender_resolution=1x1\nsharpness=nan\n"
        "vsync=2\nfullscreen=garbage\neffect_game_aa=0\nframe_cap=90\nshow_fps=0 # comment\n";
    v=Load(path);
    assert(v.frame_cap==90 && v.render_width==1920 && v.render_height==1080 && v.sharpness==0);
    assert(v.vsync && !v.fullscreen && !v.game_aa && !v.show_fps);
    assert(!Save(path/"invalid",v,error) && !error.empty());
    assert(Load(path)==v); // a failed save did not replace the existing file
    auto other=v; other.frame_cap=240; other.fullscreen=true;
    std::atomic<bool> saved{true};
    const auto write=[&](const Values& choice) {
        for (int k=0;k<32;++k) { std::string failure; if (!Save(path,choice,failure)) saved=false; }
    };
    std::thread native_menu(write,std::cref(v)),mac_panel(write,std::cref(other));
    native_menu.join(); mac_panel.join();
    assert(saved && (Load(path)==v || Load(path)==other));
    v.frame_cap=143; v.render_width=800; v.sharpness=std::numeric_limits<float>::infinity();
    Validate(v); assert(v.frame_cap==30 && v.render_width==1920 && v.sharpness==0);
    std::filesystem::remove_all(directory);
}
