#include "engine/patch_manifest.h"
#include "gpu/metal/bbhost/timing_verify.h"

#include "core/config.h"
#include "core/elf.h"
#include "core/memory.h"
#include "core/portable.h"
#include "engine/addr.h"
#include "gcn/container.h"
#include "hle/fs.h"
#include "host/settings.h"
#include "log.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

struct Site {
    std::string id;
    std::uint64_t address = 0;  // guest VA at the preferred base
    std::vector<std::uint8_t> expect, bytes;
    bool code = true;
};

struct Manifest {
    std::string path, name, eboot, description, option;
    bool enabled = true;
    std::vector<Site> sites;
};

std::string unquote(const std::string& v) {
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front()) return v.substr(1, v.size() - 2);
    return v;
}

bool parse_hex_bytes(const std::string& text, std::vector<std::uint8_t>* out) {
    out->clear();
    std::string s;
    for (char c : text) {
        if (c == ' ' || c == ',' || c == '\t') continue;
        s.push_back(c);
    }
    if (s.rfind("0x", 0) == 0 || s.rfind("0X", 0) == 0) s = s.substr(2);
    if (s.empty() || s.size() % 2) return false;
    for (std::size_t i = 0; i < s.size(); i += 2) {
        char* end = nullptr;
        const std::string pair = s.substr(i, 2);
        const long v = std::strtol(pair.c_str(), &end, 16);
        if (!end || *end) return false;
        out->push_back(static_cast<std::uint8_t>(v));
    }
    return true;
}

bool parse_manifest(const std::string& path, Manifest* m, std::string* err) {
    std::map<std::string, std::string> kv;
    if (!config_parse_toml(path, &kv, err)) return false;
    m->path = path;
    auto get = [&](const std::string& k) {
        auto it = kv.find(k);
        return it == kv.end() ? std::string() : unquote(it->second);
    };
    m->name = get("patch.name");
    m->eboot = get("patch.eboot");
    m->description = get("patch.description");
    m->option = get("patch.option");
    const std::string en = get("patch.enabled");
    m->enabled = en.empty() || en == "true" || en == "1";
    if (m->name.empty() || m->eboot.empty()) {
        *err = "[patch] needs name and eboot";
        return false;
    }
    std::map<std::string, Site> sites;
    for (const auto& [k, v] : kv) {
        if (k.rfind("site.", 0) != 0) continue;
        const std::size_t dot = k.find('.', 5);
        if (dot == std::string::npos) continue;
        Site& s = sites[k.substr(5, dot - 5)];
        s.id = k.substr(5, dot - 5);
        const std::string field = k.substr(dot + 1), val = unquote(v);
        if (field == "address") {
            s.address = std::strtoull(val.c_str(), nullptr, 16);
        } else if (field == "expect") {
            if (!parse_hex_bytes(val, &s.expect)) {
                *err = "site " + s.id + ": bad expect";
                return false;
            }
        } else if (field == "bytes") {
            if (!parse_hex_bytes(val, &s.bytes)) {
                *err = "site " + s.id + ": bad bytes";
                return false;
            }
        } else if (field == "kind") {
            s.code = val != "data";
        }
    }
    for (auto& [id, s] : sites) {
        if (!s.address || s.expect.empty() || s.bytes.size() != s.expect.size()) {
            *err = "site " + id + ": needs address, expect and bytes of the same length";
            return false;
        }
        m->sites.push_back(s);
    }
    if (m->sites.empty()) {
        *err = "no [site.*]";
        return false;
    }
    return true;
}

// The config switches a manifest may name.
bool option_on(const std::string& option, bool* known) {
    *known = true;
    if (option == "startup.skip_intro") return config().skip_intro;
    if (option == "loading.quick_reentry") return config().quick_reentry;
    if (option == "streaming.all_post_processors") return config().all_post_processors;
    if (option == "debug.free_camera") return host_settings().debug_camera;  // F10 > STARTUP, read once
    *known = false;
    return false;
}

void apply(ElfImage* image, const Manifest& m) {
    GuestMemory& mem = image->mem;
    // Every site checked before any is written.
    for (const Site& s : m.sites) {
        if (MacTimingVerify::Conflicts(s.address, s.bytes.size())) {
            host_log("patch: %s refused: site %s overlaps installed bbhost timing at 0x%llx", m.name.c_str(), s.id.c_str(),
                     static_cast<unsigned long long>(s.address));
            return;
        }
        if (s.address < kPreferredGuestSlide) {
            host_log("patch: %s refused: site %s address 0x%llx is below the image", m.name.c_str(), s.id.c_str(),
                     static_cast<unsigned long long>(s.address));
            return;
        }
        const std::uint64_t off = s.address - kPreferredGuestSlide;
        if (off >= mem.size || s.expect.size() > mem.size - off) {
            host_log("patch: %s refused: site %s at 0x%llx is outside the image", m.name.c_str(), s.id.c_str(),
                     static_cast<unsigned long long>(s.address));
            return;
        }
        const auto* p = static_cast<const std::uint8_t*>(guest_ptr(mem, mem.slide + off));
        if (std::memcmp(p, s.expect.data(), s.expect.size()) != 0) {
            // Already applied (the bytes are what the patch writes) is not a
            // refusal: the same file from two directories, or a rerun.
            if (std::memcmp(p, s.bytes.data(), s.bytes.size()) == 0) continue;
            host_log("patch: %s refused: unexpected bytes at 0x%llx (site %s)", m.name.c_str(),
                     static_cast<unsigned long long>(s.address), s.id.c_str());
            return;
        }
    }
    int written = 0;
    for (const Site& s : m.sites) {
        const std::uint64_t at = mem.slide + (s.address - kPreferredGuestSlide);
        auto* p = static_cast<std::uint8_t*>(guest_ptr(mem, at));
        if (std::memcmp(p, s.bytes.data(), s.bytes.size()) == 0) continue;
        const std::uint64_t lo = at & ~0xfffull, hi = (at + s.bytes.size() + 0xfff) & ~0xfffull;
        const bool ok = s.code ? guest_protect_rwx(&mem, lo, hi - lo) : guest_protect_rw(&mem, lo, hi - lo);
        if (!ok) {
            host_log("patch: %s: cannot unprotect 0x%llx (site %s); the rest is left alone", m.name.c_str(),
                     static_cast<unsigned long long>(s.address), s.id.c_str());
            return;
        }
        std::memcpy(p, s.bytes.data(), s.bytes.size());
        if (s.code && !guest_protect_rx(&mem, lo, hi - lo)) {
            host_log("patch: %s: cannot restore the protection of 0x%llx", m.name.c_str(),
                     static_cast<unsigned long long>(s.address));
            std::abort();
        }
        ++written;
    }
    host_log("patch: %s: %d of %zu sites written%s%s", m.name.c_str(), written, m.sites.size(),
             m.description.empty() ? "" : " - ", m.description.c_str());
}

// The directories manifests come from, in order: the package's, a build
// tree's, the mods directory's.
std::vector<std::string> manifest_dirs(const std::string& mods) {
    std::vector<std::string> dirs = {config_exe_dir() + "/patches", config_exe_dir() + "/../patches"};
    if (!mods.empty()) dirs.push_back(mods + "/patches");
    return dirs;
}

// Whether a texture pack (a TPF, inflated) names a texture: its name table
// holds each name NUL-terminated, in UTF-16 or in single bytes.
bool tpf_names(const std::vector<std::uint8_t>& tpf, const std::string& name) {
    std::vector<std::uint8_t> wide, narrow(name.begin(), name.end());
    for (const char c : name) wide.insert(wide.end(), {static_cast<std::uint8_t>(c), 0});
    wide.insert(wide.end(), {0, 0});
    narrow.push_back(0);
    return std::search(tpf.begin(), tpf.end(), wide.begin(), wide.end()) != tpf.end() ||
           std::search(tpf.begin(), tpf.end(), narrow.begin(), narrow.end()) != tpf.end();
}

// The Old Hunters edition looks for the DLC's own files, which came with the
// update that brought it (1.07), so a game folder holding the base game's
// versions has none of them: the edition's title art, MENU_Title_00004 in
// menu/title.tpf.dcx - without it the title movie fills its picture flat
// green (a player's game folder, 2026-10-07) - and the three areas' maps,
// m34-m36 (their lamps are the Old Hunters' headstone's, slots 44-54).
// What is missing, or "" when it is all there; read as the game will read it,
// update folder and mods included.
std::string old_hunters_missing() {
    const std::string pack = hle_fs_map_path("/app0/dvdroot_ps4/menu/title.tpf.dcx");
    std::ifstream in(pack, std::ios::binary);
    if (!in) return "menu/title.tpf.dcx";
    const std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::vector<std::uint8_t> tpf = gcn::dcx_decompress(raw);
    if (!tpf_names(tpf.empty() ? raw : tpf, "MENU_Title_00004")) return "Old Hunters title art (MENU_Title_00004 in menu/title.tpf.dcx)";
    for (const char* map : {"m34_00_00_00", "m35_00_00_00", "m36_00_00_00"}) {
        std::error_code ec;
        if (!std::filesystem::is_directory(hle_fs_map_path(("/app0/dvdroot_ps4/map/" + std::string(map)).c_str()), ec))
            return std::string("map/") + map + " (an Old Hunters area)";
    }
    return {};
}

// By file name, a later directory's file replacing an earlier one's.
std::map<std::string, std::string> manifest_files(const std::vector<std::string>& dirs) {
    std::map<std::string, std::string> files;
    for (const std::string& dir : dirs) {
        std::vector<std::string> names;
        if (!host_list_dir(dir.c_str(), &names)) continue;
        for (const std::string& n : names) {
            if (n.size() > 5 && n.compare(n.size() - 5, 5, ".toml") == 0) files[n] = dir + "/" + n;
        }
    }
    return files;
}

}  // namespace

bool patch_manifest_option_known(const std::string& option) {
    bool known = false;
    option_on(option, &known);
    return known;
}

bool patch_manifest_off(const std::string& name) {
    const std::string v = config_value("patches." + name);
    if (!v.empty()) return v == "false" || v == "0";
    // The Old Hunters was a PC enhancement in v0.2.9, `old_hunters` under
    // [world]: a bbhost.toml (a package's, a player's) that turned it off there
    // keeps it off until [patches] says otherwise.
    if (name == "old-hunters") {
        const std::string w = config_value("world.old_hunters");
        return w == "false" || w == "0";
    }
    return false;
}

std::vector<PatchListing> patch_manifests_list(const std::string& mods) {
    std::vector<PatchListing> out;
    for (const auto& [file, path] : manifest_files(manifest_dirs(mods))) {
        Manifest m;
        PatchListing l;
        l.path = path;
        if (!parse_manifest(path, &m, &l.error)) {
            l.name = file.substr(0, file.size() - 5);
        } else {
            l.name = m.name;
            l.description = m.description;
            l.option = m.option;
            l.eboot = m.eboot;
            l.enabled = m.enabled;
        }
        out.push_back(l);
    }
    return out;
}

void patch_manifests_apply(ElfImage* image) {
    if (const char* e = std::getenv("BBHOST_PATCHES"); e && e[0] == '0') {
        host_log("patch: manifests off (BBHOST_PATCHES=0)");
        return;
    }
    const char* mods = hle_fs_mods_root();
    const std::vector<std::string> dirs = manifest_dirs(mods ? mods : "");
    const std::map<std::string, std::string> files = manifest_files(dirs);
    if (files.empty()) {
        host_log("patch: no manifests (looked in %s, %s%s)", dirs[0].c_str(), dirs[1].c_str(),
                 dirs.size() > 2 ? ", the mods directory" : "");
        return;
    }
    for (const auto& [name, path] : files) {
        Manifest m;
        std::string err;
        if (!parse_manifest(path, &m, &err)) {
            host_log("patch: %s: %s", path.c_str(), err.c_str());
            continue;
        }
        if (m.eboot != image->sha256) {
            host_log("patch: %s skipped: for eboot %.12s..., this is %.12s...", m.name.c_str(), m.eboot.c_str(),
                     image->sha256.c_str());
            continue;
        }
        if (!m.enabled) continue;
        if (patch_manifest_off(m.name)) {
            host_log("patch: %s off ([patches] in bbhost.toml: the setup window's Patches tab)", m.name.c_str());
            continue;
        }
        if (!m.option.empty()) {
            bool known = false;
            const bool on = option_on(m.option, &known);
            if (!known) {
                host_log("patch: %s skipped: unknown option %s", m.name.c_str(), m.option.c_str());
                continue;
            }
            if (!on) continue;
        }
        if (m.name == "old-hunters") {
            if (const std::string missing = old_hunters_missing(); !missing.empty()) {
                host_log("patch: old-hunters skipped - the game folder has no %s. Its files are older than the 1.09 "
                         "update's, which bring The Old Hunters: copy the update's files over the game folder, replacing "
                         "the ones there (or keep them in a CUSA00900-UPDATE folder beside it). The game stays the edition "
                         "without it.",
                         missing.c_str());
                continue;
            }
        }
        apply(image, m);
    }
}
