// SPDX-License-Identifier: GPL-3.0-or-later
// Darwin services for the selected bbhost engine/plugin modules.
#include "features.h"
#include "core/elf.h"
#include "core/portable.h"
#include "core/host_clock.h"
#include "core/config.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "core/thunk.h"
#include "hle/fs.h"
#include "host/settings.h"
#include "host/plugins.h"
#include "log.h"
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <thread>
#include <mutex>
#include <set>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <utility>
#include <SDL3/SDL.h>

extern "C" void* runtime_low_map(std::size_t,int);
extern "C" void runtime_file_set_overlay(int (*)(const char*,char*,std::size_t));
extern "C" void bbmac_register_hle(const char*,void*);

namespace {
std::mutex fs_mutex,settings_mutex;
std::string app0,data,mods,generated,source_game;
std::vector<std::string> plugin_overlays;
MacSettings::Values startup_settings,live_settings;
std::uint64_t settings_serial=1;
bool settings_pending=false;
bool restart_requested=false;
std::string pending_message;
std::set<std::uint64_t> writable_code;
std::string env(const char* key,const char* fallback="") {
    const char* value=std::getenv(key); return value ? value : fallback;
}
std::string relative(const char* path) {
    std::filesystem::path p(path ? path : "");
    p=p.lexically_normal();
    if (!p.is_absolute()) p=std::filesystem::path("/app0")/p;
    auto text=p.generic_string();
    if (text.starts_with("/dvdroot_ps4/")) text="/app0"+text;
    if (text.starts_with("/hostapp/")) text="/app0/"+text.substr(9);
    if (!text.starts_with("/app0/dvdroot_ps4/")) return {};
    return text.substr(6);
}
std::string existing(const std::string& root,const std::string& rel) {
    if (root.empty() || rel.empty()) return {};
    const auto p=std::filesystem::path(root)/rel;
    std::error_code ec;
    return std::filesystem::is_regular_file(p,ec) ? p.string() : "";
}
std::string overlay_locked(const std::string& rel,bool plugins) {
    // A merged loose-file mod differs from the source dump by its resolved path.
    auto user=existing(app0,rel);
    if (!user.empty() && !source_game.empty()) {
        std::error_code ec;
        if (!std::filesystem::equivalent(user,std::filesystem::path(source_game)/rel,ec)) return user;
    }
    if (auto p=existing(mods,rel); !p.empty()) return p;
    if (plugins) for (auto it=plugin_overlays.rbegin();it!=plugin_overlays.rend();++it)
        if (auto p=existing(*it,rel); !p.empty()) return p;
    return existing(generated,rel);
}
int overlay_resolve(const char* path,char* output,std::size_t size) {
    const auto rel=relative(path); if (rel.empty()) return 0;
    std::scoped_lock lock(fs_mutex);
    const auto p=overlay_locked(rel,true);
    if (p.empty()) return 0;
    if (p.size()>=size) return -1;
    std::memcpy(output,p.c_str(),p.size()+1); return 1;
}
bool protect(GuestMemory* mem,std::uint64_t at,std::size_t size,int prot) {
    if (!mem || at<mem->slide || size>mem->size || at-mem->slide>mem->size-size) return false;
    const auto page=std::uint64_t(getpagesize());
    const auto lo=at&~(page-1),hi=(at+size+page-1)&~(page-1);
    for (auto p=lo;p<hi;p+=page) {
        const int actual=writable_code.contains(p) ? PROT_READ|PROT_WRITE|PROT_EXEC : prot;
        if (mprotect(reinterpret_cast<void*>(p),page,actual)) return false;
    }
    return true;
}
void* exec_near(std::uint64_t at) {
    for (std::uint64_t delta=0x10000;delta<0x70000000;delta+=0x10000) {
        mach_vm_address_t address=(at&~0xffffull)+delta;
        if (mach_vm_allocate(mach_task_self(),&address,0x1000,VM_FLAGS_FIXED)!=KERN_SUCCESS) continue;
        if (mprotect(reinterpret_cast<void*>(address),0x1000,PROT_READ|PROT_WRITE|PROT_EXEC)==0)
            return reinterpret_cast<void*>(address);
        mach_vm_deallocate(mach_task_self(),address,0x1000);
    }
    return nullptr;
}
}

namespace MacFeatures {
void Initialize(const MacSettings::Values& v) {
    startup_settings=live_settings=v;
    UpdateGraphics(v);
    data=env("BB_DATA_DIR","out/macos-data"); app0=env("BB_GAME_DIR");
    source_game=env("BB_SOURCE_GAME");
    mods=env("BB_MAC_MOD_OVERLAY",env("BB_MODS_DIR",(std::filesystem::path(data)/"mods").c_str()).c_str());
    setenv("BBHOST_CONFIG_DIR",data.c_str(),1);
    std::map<std::string,std::string> values; std::string error;
    const auto file=config_user_file();
    if (std::filesystem::exists(file) && !config_parse_toml(file,&values,&error))
        throw std::runtime_error("Invalid mod configuration: "+error);
    for (const auto& [key,value]:values) config_value_set(key,value);
    // This THD adapter does not negotiate entry into a different ruleset's
    // world. Do not load disabled plugins as cross-world visitors by default.
    if (!values.contains("plugins.join_modded_worlds")) config_value_set("plugins.join_modded_worlds","false");
    setenv("BBHOST_GAME_FPS",v.frame_cap>120 ? "0" : std::to_string(v.frame_cap).c_str(),1);
    if (v.frame_cap>120) setenv("BBHOST_UNCAPPED_FPS",std::to_string(v.frame_cap).c_str(),1);
    else unsetenv("BBHOST_UNCAPPED_FPS");
    runtime_file_set_overlay(overlay_resolve);
    plugins_load();
}
void SetSettings(const MacSettings::Values& v) {
    std::scoped_lock lock(settings_mutex); live_settings=v; ++settings_serial;
    UpdateGraphics(v);
}
bool TakeSettings(MacSettings::Values& v) {
    std::scoped_lock lock(settings_mutex);
    if (!settings_pending) return false;
    v=live_settings; settings_pending=false; return true;
}
MacSettings::Values Settings() { std::scoped_lock lock(settings_mutex); return live_settings; }
bool RequestSettings(const MacSettings::Values& requested) {
    auto v=requested; MacSettings::Validate(v);
    std::scoped_lock lock(settings_mutex);
    std::string error;
    if (!MacSettings::Save(env("BB_MAC_SETTINGS",(std::filesystem::path(data)/"macos-settings.ini").c_str()),v,error)) {
        pending_message="Could not save settings: "+error; return false;
    }
    live_settings=v; settings_pending=true; ++settings_serial;
    if (MacSettings::NeedsRestart(v,startup_settings)) pending_message="Settings saved. Restart to apply the pending changes.";
    return true;
}
std::string TakeMessage() {
    std::scoped_lock lock(settings_mutex); return std::exchange(pending_message,{});
}
void RequestRestart() { std::scoped_lock lock(settings_mutex); restart_requested=true; }
bool TakeRestartRequest() { std::scoped_lock lock(settings_mutex); return std::exchange(restart_requested,false); }
}

HostSettings host_settings() {
    std::scoped_lock lock(settings_mutex);
    HostSettings s; s.serial=settings_serial;
    s.frame_cap=startup_settings.frame_cap; s.vsync=live_settings.vsync;
    s.fullscreen=live_settings.fullscreen; s.fps_counter=live_settings.show_fps;
    s.res_width=startup_settings.render_width; s.res_height=startup_settings.render_height;
    s.anti_alias=startup_settings.game_aa; s.ssao=startup_settings.ssao;
    s.motion_blur=startup_settings.motion_blur; s.depth_of_field=startup_settings.dof;
    s.chromatic_aberration=startup_settings.chromatic_aberration;
    return s;
}
std::uint64_t host_opt_serial() { std::scoped_lock lock(settings_mutex); return settings_serial; }
void host_message_show(const char* message,float) {
    std::scoped_lock lock(settings_mutex); pending_message=message ? message : "";
}
bool host_read_safe(const void* address,void* output,std::size_t size) {
    if (!address || (!output && size)) return false;
    mach_vm_size_t copied=0;
    return mach_vm_read_overwrite(mach_task_self(),reinterpret_cast<mach_vm_address_t>(address),size,
        reinterpret_cast<mach_vm_address_t>(output),&copied)==KERN_SUCCESS && copied==size;
}
bool hle_kernel_va_mapped(std::uint64_t address,std::uint64_t size) {
    if (!size || address+size<address) return false;
    mach_vm_address_t at=address; mach_vm_size_t bytes=0;
    vm_region_basic_info_data_64_t info{}; mach_msg_type_number_t count=VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object=MACH_PORT_NULL;
    const auto result=mach_vm_region(mach_task_self(),&at,&bytes,VM_REGION_BASIC_INFO_64,
        reinterpret_cast<vm_region_info_t>(&info),&count,&object);
    if (object) mach_port_deallocate(mach_task_self(),object);
    return result==KERN_SUCCESS && address>=at && size<=bytes && address-at<=bytes-size && (info.protection&VM_PROT_READ);
}
bool guest_protect_rx(GuestMemory* m,std::uint64_t a,std::size_t n) { return protect(m,a,n,PROT_READ|PROT_EXEC); }
bool guest_protect_rw(GuestMemory* m,std::uint64_t a,std::size_t n) { return protect(m,a,n,PROT_READ|PROT_WRITE); }
bool guest_protect_rwx(GuestMemory* m,std::uint64_t a,std::size_t n) { return protect(m,a,n,PROT_READ|PROT_WRITE|PROT_EXEC); }
void guest_keep_writable(GuestMemory* m,std::uint64_t a) {
    writable_code.insert(a&~std::uint64_t(getpagesize()-1)); guest_protect_rwx(m,a,1);
}
void* host_page_alloc(std::size_t n,bool exec) { return runtime_low_map(n,PROT_READ|PROT_WRITE|(exec ? PROT_EXEC : 0)); }
void host_page_free(void* p,std::size_t n) { munmap(p,n); }
void host_sleep_us(std::uint64_t us) { std::this_thread::sleep_for(std::chrono::microseconds(us)); }
void host_sleep_until(std::chrono::steady_clock::time_point t) { std::this_thread::sleep_until(t); }
std::uint64_t host_clock_monotonic_ns() { return SDL_GetTicksNS(); }
std::int64_t host_clock_realtime_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
bool host_mkdir(const char* path) { std::error_code ec; std::filesystem::create_directories(path,ec); return !ec; }
bool host_list_dir(const char* path,std::vector<std::string>* names) {
    std::error_code ec;
    for (std::filesystem::directory_iterator it(path,ec),end; !ec && it!=end;it.increment(ec)) names->push_back(it->path().filename().string());
    return !ec;
}
const void* host_memmem(const void* h,std::size_t n,const void* needle,std::size_t k) { return memmem(h,n,needle,k); }
void register_hle_fn(const char* name,void* f) { bbmac_register_hle(name,f); }
const char* hle_fs_data_root() { return data.c_str(); }
const char* hle_fs_app0_root() { return app0.c_str(); }
const char* hle_fs_update_root() { return ""; }
const char* hle_fs_mods_root() { return mods.c_str(); }
void hle_fs_add_plugin_overlay(const char* p) { std::scoped_lock lock(fs_mutex); plugin_overlays.emplace_back(p); }
void hle_fs_set_generated_root(const char* p) { std::scoped_lock lock(fs_mutex); generated=p; }
std::string hle_fs_overlay_file(const char* path) {
    auto rel=relative(path);
    std::scoped_lock lock(fs_mutex); return overlay_locked(rel,true);
}
std::string hle_fs_map_path_base(const char* path) {
    const auto rel=relative(path); if (rel.empty()) return {};
    std::scoped_lock lock(fs_mutex);
    if (auto p=overlay_locked(rel,false);!p.empty()) return p;
    return existing(app0,rel);
}
std::string hle_fs_map_path(const char* path) {
    const auto rel=relative(path); if (rel.empty()) return {};
    std::scoped_lock lock(fs_mutex);
    if (auto p=overlay_locked(rel,true);!p.empty()) return p;
    return existing(app0,rel);
}
std::string hle_fs_game_file(const char* path) { return hle_fs_map_path_base(path); }
std::string hle_fs_game_file(const std::string& path) { return hle_fs_game_file(path.c_str()); }

namespace {
bool hook(ElfImage* image,std::uint64_t at,const std::uint8_t* expected,std::size_t n,
    const std::uint8_t* displaced,std::size_t count,void* fn,std::uint64_t id,bool variadic) {
    if (!image || !engine_eboot_supported(image->sha256) || n<14 || n>64 || at<image->mem.slide ||
        n>image->mem.size || at-image->mem.slide>image->mem.size-n) return false;
    auto p=static_cast<std::uint8_t*>(guest_ptr(image->mem,at));
    if (std::memcmp(p,expected,n)) return false;
    auto stub=static_cast<std::uint8_t*>(host_page_alloc(4096,true)); if (!stub) return false;
    thunk_emit_prologue_stub(stub,id,fn,at+n,displaced,count,variadic);
    if (!guest_protect_rwx(&image->mem,at,n)) { host_page_free(stub,4096); return false; }
    std::memset(p,0x90,n); const std::uint8_t jump[]{0xff,0x25,0,0,0,0}; std::memcpy(p,jump,6);
    const auto target=reinterpret_cast<std::uint64_t>(stub); std::memcpy(p+6,&target,8);
    return guest_protect_rx(&image->mem,at,n);
}
}
bool engine_prologue_hook(ElfImage* i,std::uint64_t a,const std::uint8_t* p,std::size_t n,void* f,std::uint64_t id) {
    return hook(i,a,p,n,p,n,f,id,false);
}
bool engine_prologue_hook_exec(ElfImage* i,std::uint64_t a,const std::uint8_t* p,std::size_t n,
    const std::uint8_t* e,std::size_t m,void* f,std::uint64_t id) { return hook(i,a,p,n,e,m,f,id,false); }
bool engine_prologue_hook_variadic(ElfImage* i,std::uint64_t a,const std::uint8_t* p,std::size_t n,void* f,std::uint64_t id) {
    return hook(i,a,p,n,p,n,f,id,true);
}
bool engine_call_site_hook(ElfImage* image,std::uint64_t at,std::uint64_t target,void* fn) {
    if (!image || !engine_eboot_supported(image->sha256) || at<image->mem.slide || at-image->mem.slide+5>image->mem.size) return false;
    auto p=static_cast<std::uint8_t*>(guest_ptr(image->mem,at)); std::int32_t rel=0; std::memcpy(&rel,p+1,4);
    if ((p[0]!=0xe8 && p[0]!=0xe9) || at+5+rel!=target) return false;
    auto stub=static_cast<std::uint8_t*>(exec_near(at)); if (!stub) return false;
    thunk_emit_prologue_stub(stub,0,fn,target,nullptr,0);
    if (!guest_protect_rwx(&image->mem,at,5)) return false;
    rel=std::int32_t(reinterpret_cast<std::uint64_t>(stub)-at-5); std::memcpy(p+1,&rel,4);
    return guest_protect_rx(&image->mem,at,5);
}
