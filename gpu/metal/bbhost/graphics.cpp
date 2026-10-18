// SPDX-License-Identifier: GPL-3.0-or-later
// Light-grid batching and model detail from bbhost engine/graphics_patch.cpp.
// The PS4 engine still chooses visible objects; no occlusion results are fabricated here.
#include "features.h"
#include "core/elf.h"
#include "core/portable.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "host/settings.h"
#include "log.h"
#include <atomic>
#include <algorithm>
#include <mach/mach.h>
#include <mach/mach_vm.h>

namespace MacFeatures {
namespace {
std::atomic<bool> optimized{false};
std::atomic<int> detail{0};
std::uint64_t graphics_slot=0,render_slot=0;
std::uint32_t* divisions=nullptr;
std::uint32_t render_width=1920,render_height=1080;
bool was_optimized=false;
template<class T> bool read(std::uint64_t address,T& v) { return host_read_safe(reinterpret_cast<void*>(address),&v,sizeof(v)); }
template<class T> void write(std::uint64_t address,const T& v) {
    mach_vm_write(mach_task_self(),address,reinterpret_cast<vm_offset_t>(&v),sizeof(v));
}
std::int64_t view_hook(std::uint64_t,const std::uint64_t*) {
    std::uint64_t gm=0,rm=0,settings=0;
    const bool on=optimized.load(std::memory_order_relaxed);
    if ((on || was_optimized) && read(graphics_slot,gm) && gm) {
        const std::uint32_t wanted[]{on ? std::clamp((render_width+3)/4,128u,16384u) : 128u,
            on ? std::clamp((render_height+3)/4,128u,16384u) : 128u};
        std::uint32_t current[2]{};
        if (read(gm+0x78a8,current) && (current[0]!=wanted[0] || current[1]!=wanted[1])) write(gm+0x78a8,wanted);
        divisions[0]=divisions[1]=on ? 1 : 3;
        was_optimized=on;
    }
    if (read(render_slot,rm) && rm && read(rm+0x10,settings) && settings) {
        const auto wanted=detail.load(std::memory_order_relaxed); int current=0;
        if (read(settings+0x530,current) && current!=wanted) write(settings+0x530,wanted);
    }
    return 0;
}
}
void UpdateGraphics(const MacSettings::Values& v) { optimized=v.optimized_lights; detail=v.model_lod; }
void InstallGraphics(ElfImage* image) {
    if (!image || !engine_eboot_supported(image->sha256)) return;
    const auto at=[&](std::uint64_t bn) { return image->mem.slide+bn-0x400000; };
    divisions=static_cast<std::uint32_t*>(guest_ptr(image->mem,at(0x553ae80)));
    if (divisions[0]!=3 || divisions[1]!=3) { host_log("Mac light grid refused: job split differs"); divisions=nullptr; return; }
    if (!guest_protect_rw(&image->mem,at(0x553ae80),8)) return;
    graphics_slot=at(0x5940dd8); render_slot=at(0x5940298);
    auto s=host_settings(); render_width=s.res_width; render_height=s.res_height;
    static constexpr std::uint8_t prologue[]{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,
        0x41,0x54,0x53,0x48,0x81,0xec,0xa8,0x0e,0,0};
    const bool hooked=engine_prologue_hook(image,at(0x269e990),prologue,sizeof(prologue),reinterpret_cast<void*>(&view_hook));
    host_log("Mac graphics: light grid and model detail %s",hooked ? "installed; original culling preserved" : "refused: view prologue differs");
}
}
