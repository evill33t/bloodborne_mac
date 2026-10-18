// SPDX-License-Identifier: GPL-3.0-or-later
#include "features.h"
#include "core/elf.h"
#include "engine/addr.h"
#include "engine/params.h"
#include "engine/event_flags.h"
#include "engine/player_data.h"
#include "engine/world_chr.h"
#include "engine/lua_events.h"
#include "engine/frame_rate.h"
#include "engine/debug_menu.h"
#include "engine/patch_manifest.h"
#include "engine/menu_assets.h"
#include "engine/menu_memory.h"
#include "host/plugins.h"
#include "decomp/decomp.h"
#include "core/portable.h"
#include <cstring>
#include "log.h"
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>
#include <atomic>
void decomp_flag_store_add();

namespace MacFeatures {
void InstallGameMenu(ElfImage* image);
void InstallGraphics(ElfImage* image);
void InstallGpuFenceWaits(ElfImage* image);
namespace { ElfImage game_image; std::atomic<bool> debug_toggle{false}; }
void RequestDebugToggle() { debug_toggle=true; }
bool TakeDebugToggle() { return debug_toggle.exchange(false); }
void BindImage(unsigned char* image,std::uint64_t size) {
    size=(size+getpagesize()-1)&~std::uint64_t(getpagesize()-1);
    game_image={}; game_image.mem={image,std::size_t(size+ImageTailSize),reinterpret_cast<std::uint64_t>(image)};
    game_image.tail_va=size; game_image.tail_size=ImageTailSize;
    if (auto p=std::getenv("BB_MAC_EBOOT_SHA256")) game_image.sha256=p;
}
void AddSegment(std::uint64_t at,std::uint64_t size,std::uint32_t flags) {
    game_image.segments.push_back({at,size,size,flags});
}
void AddRelative(std::uint64_t at,std::uint64_t target) {
    if (at<=UINT32_MAX && target<=UINT32_MAX) game_image.relative.emplace_back(at,target);
}
void Install() {
    if (!engine_eboot_supported(game_image.sha256)) {
        host_log("Mac engine features: unsupported ELF hash %s; address-based features disabled",game_image.sha256.c_str()); return;
    }
    auto tail=static_cast<unsigned char*>(game_image.mem.base)+game_image.tail_va;
    if (mprotect(tail,ImageTailSize,PROT_READ|PROT_WRITE)) throw std::runtime_error("Cannot prepare the engine's data tail");
    params_install(&game_image); event_flags_install(&game_image); player_data_install(&game_image);
    extern void InstallFlagStore(ElfImage*);
    InstallFlagStore(&game_image);
    lua_events_install(&game_image); world_chr_install(&game_image);
    // Debug text redirects LEA instructions inside the camera patch sites.
    // Validate/install timing first, as upstream's graphics pass does.
    frame_rate_install(&game_image);
    InstallGpuFenceWaits(&game_image);
    plugins_image(&game_image);
    patch_manifests_apply(&game_image);
    debug_menu_install(&game_image);
    InstallGraphics(&game_image);
    if (menu_assets_ensure()) { menu_memory_install(&game_image); InstallGameMenu(&game_image); }
}

void InstallFlagStore(ElfImage* image);
}
namespace { ElfImage* flag_image; }
std::uint64_t g_decomp_slide=0;
// Only the four checked event-flag leaves are selected, for plugin flag callbacks.
void decomp_add(const DecompFunction& f) {
    if (!flag_image || f.entry_len<14 || f.entry_len>64 || f.bn<0x400000) return;
    const auto offset=f.bn-0x400000;
    if (offset+f.entry_len>flag_image->mem.size) return;
    auto p=static_cast<unsigned char*>(flag_image->mem.base)+offset;
    if (std::memcmp(p,f.entry,f.entry_len)) { host_log("event-flag hook %s refused: unexpected code",f.name); return; }
    auto trampoline=static_cast<unsigned char*>(host_page_alloc(4096,true)); if (!trampoline) return;
    std::memcpy(trampoline,p,f.entry_len);
    const unsigned char jump[]{0xff,0x25,0,0,0,0};
    std::memcpy(trampoline+f.entry_len,jump,6);
    const auto resume=reinterpret_cast<std::uint64_t>(p+f.entry_len);
    std::memcpy(trampoline+f.entry_len+6,&resume,8);
    const auto at=reinterpret_cast<std::uint64_t>(p);
    if (!guest_protect_rwx(&flag_image->mem,at,f.entry_len)) return;
    std::memset(p,0x90,f.entry_len); std::memcpy(p,jump,6); std::memcpy(p+6,&f.ours,8);
    guest_protect_rx(&flag_image->mem,at,f.entry_len);
    if (f.original) *f.original=trampoline;
}
namespace MacFeatures {
void InstallFlagStore(ElfImage* image) {
    flag_image=image; g_decomp_slide=image->mem.slide;
    ::decomp_flag_store_add();
}
}
extern "C" void bbmac_features_segment(std::uint64_t a,std::uint64_t n,std::uint32_t f) { MacFeatures::AddSegment(a,n,f); }
extern "C" void bbmac_features_relative(std::uint64_t a,std::uint64_t v) { MacFeatures::AddRelative(a,v); }
extern "C" void bbmac_features_install() { MacFeatures::Install(); }
