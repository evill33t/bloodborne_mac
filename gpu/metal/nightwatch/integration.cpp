// SPDX-License-Identifier: GPL-3.0-or-later
#include "integration.h"
#include <cstdlib>
#include <stdexcept>
#include "host/plugins.h"
#include "gpu/metal/bbhost/features.h"
#include "../vendor/nightwatch/gpu/shim/party/party_code.h"
#ifdef BB_HAS_NIGHTWATCH
#include "../vendor/nightwatch/gpu/shim/party/party_runtime.h"
#include "../vendor/nightwatch/gpu/shim/party/party_director.h"
#include "../vendor/nightwatch/gpu/shim/party/coop_hooks.h"
#include "../vendor/nightwatch/gpu/shim/party/game_state.h"
#include "../vendor/nightwatch/gpu/shim/party/lamp_travel.h"
#include "../vendor/nightwatch/gpu/shim/bbport_settings.h"
#include "../vendor/nightwatch/gpu/shim/net/bbnet_internal.h"
#include "engine/addr.h"
extern "C" void nw_net_configure(const char*,const char*);
extern "C" void nw_net_set_image(void*,std::uint64_t);
extern "C" void nw_net_start();
extern "C" void bbmac_register_hle(const char*,void*);
#endif
namespace BbGameMenu { void InstallNightwatchMessages(); }
namespace MacNightwatch {
namespace { bool enabled=false; unsigned char* image=nullptr; std::uint64_t image_size=0; }
bool Available() {
#ifdef BB_HAS_NIGHTWATCH
    return true;
#else
    return false;
#endif
}
bool Active() { return enabled; }
bool ValidJoinCode(const std::string& text) {
#ifdef BB_HAS_NIGHTWATCH
    party::PartyCode code; std::string error;
    return MacSettings::ValidNightwatchCode(text) && !text.empty() && party::decode_party_code(text,&code,&error);
#else
    return false;
#endif
}
void Configure(const MacSettings::Values& v) {
    enabled=Available() && v.nightwatch_mode!=0;
    setenv("BB_PARTY",enabled ? (v.nightwatch_mode==1 ? "host" : "join") : "0",1);
    if (!enabled) {
        if (const auto previous=std::getenv("BB_MAC_NIGHTWATCH_PREVIOUS_ONLINE")) {
            if (std::string(previous)=="unset") unsetenv("BB_ONLINE");
            else setenv("BB_ONLINE",previous,1);
            unsetenv("BB_MAC_NIGHTWATCH_PREVIOUS_ONLINE");
        }
        return;
    }
    const char* base=std::getenv("BB_MAC_BASE_USER_DIR"),*user=std::getenv("BB_USER_DIR");
    if (!base || !user || std::filesystem::absolute(std::filesystem::path(base)/"nightwatch").lexically_normal()!=std::filesystem::absolute(user).lexically_normal())
        throw std::runtime_error("Launch Nightwatch through run.sh to keep co-op saves separate.");
#ifdef BB_HAS_NIGHTWATCH
    if (v.nightwatch_mode==2) {
        party::PartyCode code; std::string error;
        if (!party::decode_party_code(v.nightwatch_code,&code,&error)) throw std::runtime_error("Nightwatch party code: "+error);
    }
#endif
    for (const auto& plugin:plugins_catalog())
        if (plugin.loaded && plugin.enabled) throw std::runtime_error("Disable gameplay plugins before starting Nightwatch co-op.");
    setenv("BB_OUT_DIR",(std::filesystem::path(std::getenv("BB_DATA_DIR"))/"prepared").c_str(),1);
    setenv("BB_GPU_USER_DIR",std::getenv("BB_USER_DIR"),1);
    if (!std::getenv("BB_MAC_NIGHTWATCH_PREVIOUS_ONLINE")) {
        const auto previous=std::getenv("BB_ONLINE");
        setenv("BB_MAC_NIGHTWATCH_PREVIOUS_ONLINE",previous ? previous : "unset",1);
    }
    setenv("BB_ONLINE","1",1);
    setenv("BB_PARTY_NAME",v.nightwatch_name.c_str(),1);
    setenv("BB_PARTY_CODE",v.nightwatch_code.c_str(),1);
    setenv("BB_PARTY_MAX",std::to_string(v.nightwatch_players).c_str(),1);
    setenv("BB_PARTY_PORT",std::to_string(v.nightwatch_port).c_str(),1);
    setenv("BB_PARTY_ROT",v.nightwatch_rot ? "1" : "0",1);
    setenv("BB_PARTY_B2","1",1);
    setenv("BB_PARTY_SAVE","separate",1);
    setenv("BB_PARTY_RENDEZVOUS","0",1);
    // Starting a game always leaves Continue / New Game to the player.
    setenv("BB_PARTY_AUTOCONTINUE","0",1);
    setenv("BB_PARTY_RESTARTED","0",1);
#ifdef BB_HAS_NIGHTWATCH
    BbSettings::Get().party_rot=v.nightwatch_rot;
    nw_net_configure(std::getenv("BB_GAME_DIR"),std::getenv("BB_USER_DIR"));
    bbnet::set_guest_reader(&coop::SafeRead);
    bbnet::set_guest_writer(&coop::SafeWrite);
#endif
}
void Register() {
#ifdef BB_HAS_NIGHTWATCH
    if (!enabled) return;
    for (auto table:{bbnet::kNetExports,bbnet::kNpExports,bbnet::kMatching2Exports,bbnet::kSignalingExports,bbnet::kHttpExports})
        for (auto entry=table;entry->name;++entry) bbmac_register_hle(entry->name,entry->fn);
#endif
}
void SetImage(unsigned char* p,std::uint64_t size) { image=p; image_size=size;
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) nw_net_set_image(p,size);
#endif
}
void InstallHooks() {
#ifdef BB_HAS_NIGHTWATCH
    if (!enabled) return;
    const char* hash=std::getenv("BB_MAC_EBOOT_SHA256");
    if (!hash || !engine_eboot_supported(hash)) throw std::runtime_error("Nightwatch requires a supported Bloodborne 1.09 executable.");
    coop::HooksInit(image,image_size);
    coop::lamptravel::Install(image,image_size);
    BbGameMenu::InstallNightwatchMessages();
    coop::PartyInit(image,image_size);
    MacFeatures::SetGameTick(&coop::CoopTick);
    coop::MarkImageHooksDone();
#endif
}
void Install() {
    InstallHooks();
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) nw_net_start();
#endif
}
void Shutdown() {
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) party::runtime_shutdown();
#endif
}
std::string Status() {
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) return party::party_status_line();
#endif
    return Available() ? "Nightwatch is off. Select Host or Join and apply & restart." : "Nightwatch currently requires the Rosetta CPU runtime.";
}
std::string Code(bool lan) {
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) { auto s=party::runtime_status(); return lan ? s.lan_code : s.internet_code; }
#endif
    return {};
}
void Leave() {
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) party::runtime_leave();
#endif
}
void Rejoin() {
#ifdef BB_HAS_NIGHTWATCH
    if (enabled) party::runtime_rejoin();
#endif
}
}
