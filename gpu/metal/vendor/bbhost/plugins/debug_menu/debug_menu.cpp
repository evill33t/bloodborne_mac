// Debug Menu: an official bbhost plugin (docs/plugins.md) - the developers'
// debug menu, which the game still carries but leaves off.
//
//   [plugins]
//   debug_menu = true
//
// With it on, bbhost restores the menu at the next start (engine/debug_menu.h)
// and the Debug Menu key (`) opens and closes it. The menu draws with a debug
// font the game does not ship; this plugin makes it (debug_font.hpp) from the
// public-domain X11 fonts k14 and 7x14 and hands it to the game through its
// file overlay, so nothing has to be downloaded. A DbgFont14h.ccm and .tpf in
// the player's mods folder still win.
//
// It is a gameplay plugin: the menu can hand out items and change the rules,
// so its sessions announce a ruleset of their own and the server keeps their
// messages, bloodstains and statistics apart from other players'.
//
// Its text is the developers' Japanese, or English (the "text" setting,
// English unless set): strings_en.tsv, built in, names each string by its
// address and a hash of the game's own text there, and the host points the
// game's references to that string at the English (replace_text, plugin API
// 11). A row whose hash does not match the eboot is left out.
#include "bbhost/sdk.hpp"

#include "debug_font.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

// font14.bin (tools/make_debug_font.py), built in (CMakeLists.txt).
const std::uint8_t kFont14[] = {
#include "debug_font14.inc"
};

// strings_en.tsv, built in.
const char kEnglish[] =
#include "debug_menu_strings_en.inc"
    ;

bb::Plugin g;

// 64-bit FNV-1a over the UTF-16 bytes, as tools/debug_menu_strings.py has it.
std::uint64_t fnv1a(const std::vector<std::uint16_t>& s) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (const std::uint16_t c : s) {
        h = (h ^ (c & 0xff)) * 0x100000001b3ull;
        h = (h ^ (c >> 8)) * 0x100000001b3ull;
    }
    return h;
}

// The game's string at a Binary Ninja address, without its terminator.
bool game_text(std::uint64_t bn, std::vector<std::uint16_t>* out) {
    out->clear();
    std::uint16_t chunk[64];
    for (int n = 0; n < 32; ++n) {
        if (g.api->read(bn + 128 * static_cast<std::uint64_t>(n), chunk, sizeof(chunk)) != 0) return false;
        for (const std::uint16_t c : chunk) {
            if (!c) return true;
            out->push_back(c);
        }
    }
    return false;
}

// A table field, its escapes undone, as UTF-16.
std::vector<std::uint16_t> utf16(const std::string& field) {
    std::string s;
    for (std::size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 1 < field.size()) {
            const char e = field[++i];
            s += e == 't' ? '\t' : e == 'n' ? '\n' : e == 'r' ? '\r' : e;
        } else {
            s += field[i];
        }
    }
    std::vector<std::uint16_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = c;
        int extra = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        if (extra) cp = c & (0x3f >> extra);
        for (int k = 1; k <= extra && i + static_cast<std::size_t>(k) < s.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3f);
        i += static_cast<std::size_t>(extra) + 1;
        if (cp < 0x10000) out.push_back(static_cast<std::uint16_t>(cp));
    }
    out.push_back(0);
    return out;
}

// The table's rows that match this eboot, handed to the host in one call.
void english() {
    if (g.api->version < 11 || g.api->size < offsetof(BbHostApi, replace_text) + sizeof(void*)) {
        g.log("debug_menu: this bbhost has no replace_text (plugin API 11); the menu stays in Japanese");
        return;
    }
    std::vector<std::vector<std::uint16_t>> texts;
    std::vector<BbText> list;
    std::size_t rows = 0, stale = 0;
    std::vector<std::uint16_t> have;
    const std::string table = kEnglish;
    for (std::size_t at = 0; at < table.size();) {
        std::size_t end = table.find('\n', at);
        if (end == std::string::npos) end = table.size();
        const std::string line = table.substr(at, end - at);
        at = end + 1;
        if (line.empty() || line[0] == '#') continue;
        const std::size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos) continue;
        ++rows;
        const std::uint64_t bn = std::strtoull(line.substr(0, t1).c_str(), nullptr, 16);
        const std::uint64_t hash = std::strtoull(line.substr(t1 + 1, t2 - t1 - 1).c_str(), nullptr, 16);
        if (!game_text(bn, &have) || fnv1a(have) != hash) {
            ++stale;
            continue;
        }
        texts.push_back(utf16(line.substr(t2 + 1)));
        list.push_back({bn, nullptr});
    }
    for (std::size_t i = 0; i < list.size(); ++i) list[i].text = texts[i].data();
    const int moved = g.api->replace_text(list.data(), list.size());
    g.log("debug_menu: English for %zu of the table's %zu strings (%d references)%s", list.size(), rows, moved,
          stale ? "; the rest do not match this eboot" : "");
}

}  // namespace

extern "C" BB_PLUGIN_EXPORT const BbPluginInfo bb_plugin_info = {
    BB_PLUGIN_API_VERSION,
    BB_PLUGIN_OPT_IN | BB_PLUGIN_GAMEPLAY,
    "debug_menu",
    "Debug Menu",
    "1.0.0",
    "bbhost",
    "The developers' debug menu, still in the game: ` opens and closes it (from the next start). Cheating against "
    "other players with it gets an account banned; trying it out online with friends is fine.",
};

extern "C" BB_PLUGIN_EXPORT const BbOption bb_plugin_options[] = {
    {BB_OPT_CHOICE, "text", "Menu text", "English, or the developers' original Japanese.", "English", 0, 0, "English|Japanese"},
    {BB_OPT_HEADING, nullptr, "Playing online",
     "Using the debug menu to cheat against other players - in their worlds, in invasions or on the leaderboards - "
     "gets your account banned. Trying it out online with friends is fine."},
    {BB_OPT_HEADING, nullptr, "Using it",
     "` (the key left of 1) opens and closes it, and Key Bindings can move it. The arrows move, Enter opens an entry and "
     "Backspace goes back. Sessions with it on keep their messages, bloodstains and statistics apart from other "
     "players'."},
    {BB_OPT_END}};

extern "C" BB_PLUGIN_EXPORT int bb_plugin_image(const BbHostApi* api) {
    if (!g.attach(api, 9)) return 1;
    if (!api->eboot_is_109()) return 1;
    debug_font::Files font;
    std::string err;
    if (!debug_font::build(kFont14, sizeof(kFont14), &font, &err)) {
        g.log("debug_menu: %s", err.c_str());
        return 1;
    }
    if (api->overlay_file("debug_menu", "/dvdroot_ps4/adhoc/font/DbgFont14h.ccm", font.ccm.data(), font.ccm.size()) != 0 ||
        api->overlay_file("debug_menu", "/dvdroot_ps4/adhoc/font/DbgFont14h.tpf", font.tpf.data(), font.tpf.size()) != 0) {
        g.log("debug_menu: the debug font could not be written to the overlay");
        return 1;
    }
    g.log("debug_menu: the debug font made (%d glyphs on %d pages); bbhost restores the menu", font.glyphs, font.pages);
    const char* text = api->config("debug_menu.text");
    if (!text || !*text || std::string(text) == "English") english();
    return 0;
}
