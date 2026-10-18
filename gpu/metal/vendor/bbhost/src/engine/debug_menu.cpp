#include "engine/debug_menu.h"

#include "core/elf.h"
#include "engine/addr.h"
#include "hle/fs.h"
#include "host/plugins.h"
#include "log.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

// Binary Ninja addresses, which are the shadPS4 patch list's unchanged (see
// engine/graphics_patch.cpp). Each site is checked byte for byte before any is
// written, and either all of them are applied or none.
struct Site {
    std::uint64_t at;
    const char* what;
    std::size_t len;
    std::uint8_t expect[29];
    std::uint8_t write[29];
};

const Site kSites[] = {
    // sub_136d8e0: `je` -> `jne` around loading adhoc:/FontShader/debugFont_*,
    // which the disc does not have.
    {0x136d90d, "font shaders skipped", 1, {0x74}, {0x75}},
    // sub_2199e20's allocation-failure block, rewritten into the font load:
    //   lea rdx, [rip -> 0x4d3b738]  "adhoc:/font/DbgFont14h.ccm"
    {0x2199eee, "font load: .ccm path", 8,
     {0x49, 0xc7, 0x04, 0x24, 0x00, 0x00, 0x00, 0x00},
     {0x48, 0x8d, 0x15, 0x43, 0x18, 0xba, 0x02, 0x90}},
    //   lea rcx, [rip -> 0x4d3b7c1]  "adhoc:/font/DbgFont14h.tpf" (the lea at
    //   0x2199ef6, its register and displacement)
    {0x2199ef8, "font load: .tpf path", 3, {0x3d, 0x6c, 0x14}, {0x0d, 0xc4, 0x18}},
    //   mov rdi, rbx ; xor esi, esi ; call sub_136dc70 ; mov rdi, r14 ;
    //   jmp 0x2199f26 (back into the function's own tail)
    {0x2199efe, "font load: call sub_136dc70", 29,
     {0x8d, 0x15, 0xb9, 0x14, 0xba, 0x02, 0x48, 0x8d, 0x0d, 0x8d, 0xb5, 0xb9, 0x02, 0xbe, 0xb1,
      0x00, 0x00, 0x00, 0x31, 0xc0, 0xe8, 0x99, 0xb6, 0x31, 0x00, 0x49, 0x8b, 0x1c, 0x24},
     {0x89, 0xdf, 0x90, 0x90, 0x90, 0x90, 0x31, 0xf6, 0x90, 0x90, 0x90, 0x90, 0x90, 0xe8, 0x60,
      0x3d, 0x1d, 0xff, 0x4c, 0x89, 0xf7, 0x90, 0x90, 0x90, 0x90, 0xeb, 0x0d, 0x90, 0x90}},
    // ...entered from the tail: `mov rdi, r14` -> `jmp 0x2199eee`
    {0x2199f23, "font load: entry", 3, {0x4c, 0x89, 0xf7}, {0xeb, 0xc9, 0x90}},
    // sub_23abe10: the two state setters swapped (each call's rel32 low byte).
    {0x23abefa, "state flags: first call", 1, {0x52}, {0x62}},
    {0x23abf07, "state flags: second call", 1, {0x55}, {0x45}},
    // SprjInitStuff_mb: the debug object built enabled (`mov ecx, 0` -> 1).
    {0x2418059, "debug system on", 1, {0x00}, {0x01}},
};

// The two font paths, written as UTF-16 over two assert-message strings - the
// source file names DL_PANIC prints for the disk file device - which the
// re-aimed `lea`s above point at. Each original is checked to be that path and
// long enough to hold the new one.
struct Text {
    std::uint64_t at;
    const char16_t* text;
};
const Text kTexts[] = {
    {0x4d3b738, u"adhoc:/font/DbgFont14h.ccm"},
    {0x4d3b7c1, u"adhoc:/font/DbgFont14h.tpf"},
};
const char kOriginalPrefix[] = "N:\\SPRJ\\Source\\Library\\Dantelion2\\";

// Where adhoc:/font resolves: the game mounts adhoc: on dvdroot_ps4/adhoc, and
// the file HLE looks in the asset overlay first.
const char* const kFonts[] = {"/app0/dvdroot_ps4/adhoc/font/DbgFont14h.ccm",
                              "/app0/dvdroot_ps4/adhoc/font/DbgFont14h.tpf"};

bool g_active = false;
std::uint64_t g_slide = 0;
constexpr std::uint64_t kMenuManager = 0x58b3570;  // FD4DebugMenu*, set by SprjInitStuff_mb
constexpr std::size_t kMenuMode = 0x30;

std::size_t u16len(const char16_t* s) {
    std::size_t n = 0;
    while (s[n]) ++n;
    return n;
}

}  // namespace

bool debug_menu_active() { return g_active; }

// The manager's mode, or nullptr before SprjInitStuff_mb has built it.
volatile std::int32_t* menu_mode() {
    if (!g_active) return nullptr;
    std::uint64_t mgr = 0;
    std::memcpy(&mgr, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(g_slide + (kMenuManager - kPreferredGuestSlide))),
                sizeof(mgr));
    return mgr ? reinterpret_cast<volatile std::int32_t*>(static_cast<std::uintptr_t>(mgr + kMenuMode)) : nullptr;
}

bool debug_menu_open() {
    const volatile std::int32_t* mode = menu_mode();
    return mode && *mode == 1;
}

void debug_menu_toggle() {
    if (!g_active) return;
    volatile std::int32_t* mode = menu_mode();
    if (!mode) {
        host_log("debug-menu: not built yet");
        return;
    }
    const std::int32_t was = *mode;
    *mode = was == 0 ? 1 : 0;
    host_log("debug-menu: %s (mode %d -> %d)", was == 0 ? "opened" : "closed", was, was == 0 ? 1 : 0);
}

void debug_menu_install(ElfImage* image) {
    if (!plugins_active("debug_menu")) {
        return;
    }
    for (const char* f : kFonts) {
        const std::string path = hle_fs_map_path(f);
        std::FILE* fp = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
        if (!fp) {
            host_log("debug-menu: off - %s is missing (%s); the game crashes at boot without its font", f,
                     path.empty() ? "no mapping" : path.c_str());
            return;
        }
        std::fclose(fp);
    }
    const std::uint64_t slide = image->mem.slide;
    g_slide = slide;
    auto at = [&](std::uint64_t bn) { return slide + (bn - kPreferredGuestSlide); };
    for (const Site& s : kSites) {
        const auto* p = static_cast<const std::uint8_t*>(guest_ptr(image->mem, at(s.at)));
        if (std::memcmp(p, s.expect, s.len) != 0) {
            host_log("debug-menu: refused - 0x%llx (%s) is not the expected code",
                     static_cast<unsigned long long>(s.at), s.what);
            return;
        }
    }
    for (const Text& t : kTexts) {
        const auto* p = static_cast<const char*>(guest_ptr(image->mem, at(t.at)));
        const std::size_t need = (u16len(t.text) + 1) * 2;
        if (std::strncmp(p, kOriginalPrefix, sizeof(kOriginalPrefix) - 1) != 0 || strnlen(p, need + 1) < need) {
            host_log("debug-menu: refused - 0x%llx is not the assert string the patch overwrites",
                     static_cast<unsigned long long>(t.at));
            return;
        }
    }
    for (const Site& s : kSites) {
        const std::uint64_t va = at(s.at);
        if (!guest_protect_rwx(&image->mem, va & ~0xfffull, ((va + s.len + 0xfff) & ~0xfffull) - (va & ~0xfffull))) {
            host_log("debug-menu: cannot unprotect 0x%llx; the image may be half patched",
                     static_cast<unsigned long long>(s.at));
            return;
        }
        std::memcpy(guest_ptr(image->mem, va), s.write, s.len);
        guest_protect_rx(&image->mem, va & ~0xfffull, ((va + s.len + 0xfff) & ~0xfffull) - (va & ~0xfffull));
    }
    for (const Text& t : kTexts) {
        const std::uint64_t va = at(t.at);
        const std::size_t bytes = (u16len(t.text) + 1) * 2;
        if (!guest_protect_rw(&image->mem, va & ~0xfffull, ((va + bytes + 0xfff) & ~0xfffull) - (va & ~0xfffull))) {
            host_log("debug-menu: cannot unprotect the string at 0x%llx", static_cast<unsigned long long>(t.at));
            return;
        }
        std::memcpy(guest_ptr(image->mem, va), t.text, bytes);
    }
    g_active = true;
    host_log("debug-menu: on - the debug font loads from adhoc:/font; the Debug Menu key opens it, and so "
             "does the left of the touchpad");
}
