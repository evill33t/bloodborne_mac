// SPDX-License-Identifier: GPL-3.0-or-later
// Native game menus adapted from bbhost engine/option_menu.cpp, commit 566184a.
#include "features.h"
#include "core/elf.h"
#include "core/thunk.h"
#include "core/portable.h"
#include "core/config.h"
#include "engine/addr.h"
#include "engine/graphics_patch.h"
#include "engine/debug_menu.h"
#include "host/plugins.h"
#include "host/window.h"
#include "hle/hle.h"
#include "log.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <atomic>
namespace MacFeatures {
namespace { std::atomic<void (*)()> extra_game_tick{nullptr}; }
void SetGameTick(void (*tick)()) { extra_game_tick.store(tick,std::memory_order_release); }
namespace {
constexpr std::uint64_t kOpenSection = 0x1f20900;         // sub_1f20900
constexpr std::uint64_t kScratchInit = 0x208af20;         // the handler's scratch block
constexpr std::uint64_t kAddSliderRow = 0x1f2ac00;        // a slider row
constexpr std::uint64_t kAddChoiceRow = 0x1f2a100;        // a choice row
constexpr std::uint64_t kBuildOnOff = 0x1f2b3b0;          // its two-entry On/Off list
// The pick-list row - what Language's "Text" uses, opening a sub-menu to
// choose from. Its list is a different, larger container than the On/Off one
// (that one panics above two entries), appended one entry at a time, and its
// entries are keyed by an int32 rather than a byte. That is what a frame cap
// needs.
constexpr std::uint64_t kAddListRow = 0x1f29370;
constexpr std::uint64_t kListAppend = 0x1f1c0c0;
constexpr std::uint64_t kWstrCpy = 0x2d67040;             // builds a wstring in place
// The game's own two-entry Normal / Reversed list, as Controls' Camera X-Axis
// builds it: message 0x9d76 with the value 1, 0x9d77 with 0.
constexpr std::uint64_t kBuildNormalReversed = 0x1f29ee0;
// Appends one 0x48-byte entry to a choice row's list (the On/Off container,
// two entries at most, count at +0x98).
constexpr std::uint64_t kChoiceAppend = 0x1f2c200;
// A list component's redraw: the visible range, through its item provider.
// The providers draw each item from the raw UTF-16 pointer it was built with,
// so text the port owns can be changed and shown again with this alone.
constexpr std::uint64_t kListRedraw = 0x1ed06e0;
constexpr std::uint64_t kListVTable = 0x5799fb0;
// The dialog's row layout: the rows list's count from the record vector, its
// providers, the cursor. sub_1f20900 ends with it, after appending Defaults.
constexpr std::uint64_t kLayoutRows = 0x1f28de0;
constexpr std::uint32_t kDefaultsRowId = 0x9d8a;
// sub_1f20900 will only open a sprite whose name is in this six-entry table,
// and slots [6] and [7] after it are unreferenced zero padding before a
// vtable. The loop over it binds each name's sprite and keeps the one that
// matches the name it was asked for, so the table is really the list of
// names it accepts. Slot [6] therefore holds a pointer to **g_open_name**,
// which every port opener fills in just before its call, and the loop's
// `cmp rdx, 6` becomes a 7 - one byte. Four sections, one slot, and [7] stays
// the zero it was.
constexpr std::uint64_t kSectionNameTable = 0x57398b0;
constexpr std::uint64_t kSectionCountImm = 0x1f20e21;     // the 6 in cmp rdx, 6
char g_open_name[32] = "PCSetting";
// The sections the generated movie adds (engine/menu_assets.h), each a
// clone of ControllSetting with as many widget slots as it has rows.
const char kOurSectionName[] = "PCSetting";
// The second one. A section does not scroll, so more settings than its slots
// means another section - which is how the game does it (Environment,
// Brightness, Network and Language are four sections, not one long list). The
// name table had room: slots [6] and [7] were both unreferenced padding.
//
// This clone keeps only as many slots as it has rows. sub_1f20900 appends a
// "Defaults" row of its own (message 0x9d8a) after the handler returns, and it
// lands in the first slot the handler did not fill - which is how a fourth row
// reading "Defaults" appeared under three settings. With no spare slot it has
// nowhere to draw, which is also why PCSetting, whose six rows fill all six of
// its slots, never showed one.
const char kOurGraphicsName[] = "PCGraphics";
// The mouse: camera, sensitivity, the two axes, the pointer in menus.
const char kOurControlsName[] = "PCControls";
// The key bindings: a page row and seven actions, relabelled in place when the
// page changes (see "The Key Bindings screen" below).
const char kOurKeysName[] = "PCKeys";
// The post-processing the player sees rather than pays for: motion blur, depth
// of field, chromatic aberration, bloom, vignette. Split from PCGraphics when
// the two together came to nine rows, two more than a section's slots.
const char kOurEffectsName[] = "PCEffects";
// The follow camera: field of view, distance, height.
const char kOurCameraName[] = "PCCamera";
// The Steam Deck's frame rate and model detail - on a Deck only (host_steam_deck).
const char kOurDeckName[] = "PCDeck";
// The PC enhancements: the port's additions to the game itself - The Old
// Hunters, the mirror, the rebirth, five players. Each is read once at start,
// which its line help says.
const char kOurEnhanceName[] = "PCEnhance";

// The overlay asset that has to supply that sprite. Opening a section whose
// sprite the movie does not define is not something to find out at runtime, so
// the install refuses when this file is not in the overlay.
const char kOurMovie[] = "/dvdroot_ps4/menu/optionsetting.gfx";
// And the bundle carrying the port's message ids. Without it every row would
// ask for text that is not there.
// The port's message ids are in every language's menu bundle the overlay
// has (engine/menu_assets.cpp); the game reads the one its region and
// language pick, so any of them will do for the check.
const char* const kMessageLanguages[] = {"engus", "enggb", "frafr", "deude", "itait", "spaes", "spaar", "porbr", "porpt", "nldnl",
                                         "rusru", "polpl", "dandk", "finfi", "norno", "swese", "turtr", "jpnjp", "japanese", "korkr",
                                         "zhocn", "zhotw"};

constexpr std::uint64_t kMsgRepository = 0x1ee8cc0;       // call_msg_repository
// The two message groups a row reads, which are also the bundle entry ids:
// group 200 is SP_メニューテキスト.fmg and 201 its line help.
constexpr std::uint32_t kMsgMenuText = 0xc8, kMsgLineHelp = 0xc9;

// The System command list: the row adder, the call that finalises the builder,
// and the alignment padding the redirect lands in.
constexpr std::uint64_t kAddCommandRow = 0x1f4e2a0;       // sub_1f4e2a0
// "Exit Game" is built by a *different* adder - three arguments, no flag - and
// is the one row the port has to move rather than follow. sub_1fb3ad0 adds it
// fifth of six and pops it again unless its third argument is set, so it
// survives in game and not at the title; either way the port's rows are
// appended after it, which left Exit Game in the middle of the list.
constexpr std::uint64_t kAddExitRow = 0x1f4c9a0;          // sub_1f4c9a0
constexpr std::uint64_t kExitGameOpener = 0x200c9f0;      // sub_200c9f0
constexpr std::uint32_t kExitGameRowId = 0x1adb4;
// The command builder's row vector: begin at +0x68, end at +0x70, 0x180 bytes
// a row. sub_1fb3ad0 pops a row by calling slot 1 of its vtable and dropping
// the end pointer, and that is exactly what pop_last_row does here.
constexpr std::size_t kBuilderBegin = 0x68, kBuilderEnd = 0x70;
constexpr std::size_t kBuilderRowStride = 0x180;
constexpr std::uint64_t kSystemFinalize = 0x1fea630;      // sub_1fea630
constexpr std::uint64_t kSystemFinalizeCall = 0x1fb4978;  // the one call to it
// 16 bytes of alignment nops between sub_1fb4be0's ret and sub_1fb4c70. A
// call cannot reach a host address in 5 bytes, so the redirect points here and
// this holds `movabs r11, host; jmp r11`. Both addresses are on one page.
constexpr std::uint64_t kTrampolinePad = 0x1fb4c60;
constexpr std::uint8_t kPadBytes[] = {0x90, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x2e,
                                      0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00};

// Opening a section is a two-step dance through std::function, and the port
// has to build one of each: `sub_1fb4c70(out, ctx, f)` starts the menu flow
// that loads the OptionSetting movie and then calls `f(root, params)`.
constexpr std::uint64_t kOpenSectionFlow = 0x1fb4c70;     // sub_1fb4c70
constexpr std::uint64_t kRowFunctorVTable = 0x573d120;    // Dlg*(Dlg*, Ctx)
constexpr std::uint64_t kOpenerFunctorVTable = 0x5743880; // int64(root, params)


std::uint64_t g_slide=0;
bool installed=false,seeded=false;
std::uint64_t guest(std::uint64_t bn) { return g_slide+bn-kPreferredGuestSlide; }
void* guest_fn(std::uint64_t bn) { return reinterpret_cast<void*>(guest(bn)); }
struct GuestFunction {
    alignas(16) std::uint8_t buf[0x28]{};

    GuestFunction(std::uint64_t vtable_bn, void* fn) {
        const std::uint64_t vt = guest(vtable_bn);
        void* self = &buf[0];
        std::memcpy(&buf[0x00], &vt, sizeof(vt));
        std::memcpy(&buf[0x08], &fn, sizeof(fn));
        std::memcpy(&buf[0x20], &self, sizeof(self));
    }
};

// A caption pair: two objects 0x40 apart, the label in the first and the line
// help in the second, which is the shape every row builder reads. Within each,
// +0x00 is the raw UTF-16 pointer the row draws, +0x08 a wstring the command
// row copies, +0x38 a flag.
//
// The port used to build one of these from an arbitrary message id and then
// store a pointer to its own string literal over the text. It drew, but the
// flags beside it were the placeholder's, and a section title - which the
// movie carries as a `StaticText_<id>` placement, the instance name being the
// id - could not be done that way at all. So the port has message ids of its
// own now (tools/pc_option_messages.tsv, written into the overlay's
// menu.msgbnd.dcx by engine/menu_assets.cpp) and asks for them exactly as the game
// does.
struct Captions {
    alignas(16) std::uint8_t buf[0x80]{};

    explicit Captions(std::uint32_t id) {
        hle_call_guest<std::int64_t>(guest_fn(kMsgRepository), &buf[0], kMsgMenuText, id);
        hle_call_guest<std::int64_t>(guest_fn(kMsgRepository), &buf[0x40], kMsgLineHelp, id);
    }

    // The repository's text is copied into a wstring beside the pointer, and a
    // caption long enough to leave the SSO buffer put it on the heap. Every
    // builder that makes one of these frees it the same way, so this does too:
    // one leak per menu open is still a leak.
    ~Captions() {
        free_wstring(&buf[0x08]);
        free_wstring(&buf[0x48]);
    }

    Captions(const Captions&) = delete;
    Captions& operator=(const Captions&) = delete;

    // MSVC's _String_val: the union at +0x00 is either eight wchars or the
    // heap pointer, the length is at +0x10, the capacity at +0x18 and the DL
    // allocator at +0x20. Capacity below 8 is the small-string buffer.
    static void free_wstring(void* ws) {
        auto* p = static_cast<std::uint8_t*>(ws);
        std::uint64_t cap = 0, ptr = 0, alloc = 0;
        std::memcpy(&cap, p + 0x18, sizeof(cap));
        if (cap < 8) {
            return;
        }
        std::memcpy(&ptr, p, sizeof(ptr));
        std::memcpy(&alloc, p + 0x20, sizeof(alloc));
        if (!ptr || !alloc) {
            return;
        }
        std::uint64_t vtable = 0;
        std::memcpy(&vtable, reinterpret_cast<void*>(static_cast<std::uintptr_t>(alloc)),
                    sizeof(vtable));
        std::uint64_t fn = 0;
        std::memcpy(&fn, reinterpret_cast<void*>(static_cast<std::uintptr_t>(vtable + 0x70)),
                    sizeof(fn));
        hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(fn)),
                                     alloc, ptr);
    }
};

// The repository's own text for a message id: the caption object's +0x00,
// which the repository owns and which outlives anything the port holds.
const char16_t* message_text(std::uint32_t id) {
    Captions c(id);
    const char16_t* t = nullptr;
    std::memcpy(&t, &c.buf[0], sizeof(t));
    return t;
}

// A list entry, the 0x48 bytes every list the rows take is made of: the
// value at +0x00 (a byte in a choice row's list, an int32 in a pick list's),
// the raw UTF-16 pointer that is drawn at +0x08, a wstring copy of it at
// +0x10 and a flag at +0x40. The containers deep-copy an entry, so the
// caller's copy, and later the container's, own a heap buffer for any text
// of eight characters or more, and free it the way the game's own handlers
// do: the buffer pointer at +0x18, its capacity at +0x30, the allocator at
// +0x38 - which is Captions::free_wstring's shape from +0x18.
void make_entry(std::uint8_t (&entry)[0x48], std::int32_t value, const char16_t* text) {
    // A message the bundle does not have (a player's own older menu.msgbnd in
    // paths.mods wins over the one made at start) gives no text, and the
    // game's wstring copy would read through null: an empty entry instead.
    if (!text) text = u"";
    std::memset(entry, 0, sizeof(entry));
    std::memcpy(&entry[0], &value, sizeof(value));
    std::memcpy(&entry[8], &text, sizeof(text));
    hle_call_guest<std::int64_t>(guest_fn(kWstrCpy), &entry[0x10], text);
    entry[0x40] = 1;
}

void free_entries(std::uint8_t* first, std::uint64_t count) {
    for (std::uint64_t k = 0; k < count; ++k) {
        Captions::free_wstring(first + k * 0x48 + 0x18);
    }
}

// Every builder takes the value its row's **Defaults** restores. The widgets
// copy it when they are built - a choice row keeps the byte at +0x368 beside
// its value pointer, a pick list the int32 at +0x3d4 - and the game's own
// Defaults row (sub_1f24850: "restore defaults?", then sub_1f1a2d0 calls every
// widget's slot +0x20) writes it back through the value pointer. So each row
// is built with what its setting ships with (host_opt_default_*), and the poll
// sees the change like any other.

// The game's slider: a byte from 0 to 10 in steps of 1 (sub_1f2ac00 builds it
// with those bounds), as Controls' Camera Sensitivity uses it.
void add_slider_row(void* dialog, std::uint8_t* value, std::uint32_t id, std::uint8_t def) {
    Captions c(id);
    hle_call_guest<std::int64_t>(guest_fn(kAddSliderRow), dialog, &c.buf[0], value, &def);
}

// An On/Off row. sub_1f2b3b0 builds the two-entry list from the game's own
// "on" and "off" messages, so the port does not have to construct a choice
// entry - the one structure here it would otherwise have to guess.
void add_toggle_row(void* dialog, void* value, std::uint32_t id, std::uint8_t def) {
    Captions c(id);
    alignas(16) std::uint8_t list[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kBuildOnOff), &list);
    hle_call_guest<std::int64_t>(guest_fn(kAddChoiceRow), dialog, &c.buf[0], value, &list, &def);
    std::uint64_t n = 0;
    std::memcpy(&n, &list[0x98], sizeof(n));
    free_entries(list, n);
}

// Normal / Reversed, from the game's own list: 1 is Normal, 0 Reversed.
[[maybe_unused]] void add_axis_row(void* dialog, std::uint8_t* value, std::uint32_t id, std::uint8_t def) {
    Captions c(id);
    alignas(16) std::uint8_t list[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kBuildNormalReversed), &list);
    hle_call_guest<std::int64_t>(guest_fn(kAddChoiceRow), dialog, &c.buf[0], value, &list, &def);
    std::uint64_t n = 0;
    std::memcpy(&n, &list[0x98], sizeof(n));
    free_entries(list, n);
}

// A row that shows text the port owns and changes: a choice row whose list is
// one entry, drawn from `text`, with the caption and the line help pointed at
// the port's own buffers too. With one entry Left and Right have nothing to
// step to; what the row does is up to the port (a key binding: Circle starts
// a capture). The captions come from `id` for their flags, and the three
// pointers are replaced - every provider draws the raw pointer when there is
// one (sub_1f263b0 for a choice list's items, the dialog's for its rows).
[[maybe_unused]] void add_text_row(void* dialog, std::uint8_t* value, std::uint32_t id, const char16_t* label, const char16_t* help,
                  const char16_t* text) {
    Captions c(id);
    std::memcpy(&c.buf[0x00], &label, sizeof(label));
    std::memcpy(&c.buf[0x40], &help, sizeof(help));
    alignas(16) std::uint8_t list[0x100]{};
    alignas(16) std::uint8_t entry[0x48];
    make_entry(entry, 0, text);
    hle_call_guest<std::int64_t>(guest_fn(kChoiceAppend), &list, &entry);
    free_entries(entry, 1);
    *value = 0;
    std::uint8_t def = 0;  // its only entry; the Key Bindings screen has no Defaults row
    hle_call_guest<std::int64_t>(guest_fn(kAddChoiceRow), dialog, &c.buf[0], value, &list, &def);
    free_entries(list, 1);
}

// One choice in a pick-list: the value the row stores and the message id whose
// text it shows.
struct Choice {
    std::int32_t value;
    std::uint32_t id;
};

// A pick-list row. Its entries are the 0x48-byte shape above with an int32
// value, which is the same shape the game builds for Language.
void add_list_row(void* dialog, void* value, std::uint32_t id, const Choice* choices, int count, std::int32_t def) {
    Captions c(id);
    // The container is an inline array plus a count at +0x908; zeroing it is
    // the whole of its initialisation.
    alignas(16) std::uint8_t list[0x1000]{};
    for (int i = 0; i < count; ++i) {
        alignas(16) std::uint8_t entry[0x48];
        make_entry(entry, choices[i].value, message_text(choices[i].id));
        hle_call_guest<std::int64_t>(guest_fn(kListAppend), &list, &entry);
        free_entries(entry, 1);
    }
    hle_call_guest<std::int64_t>(guest_fn(kAddListRow), dialog, &c.buf[0], value, &list, &def);
    free_entries(list, static_cast<std::uint64_t>(count));
}

void* row_open(void* out, void* ctx, void* opener) {
    GuestFunction f(kOpenerFunctorVTable, opener);
    hle_call_guest<std::int64_t>(guest_fn(kOpenSectionFlow), out, ctx, f.buf);
    return out;
}
bool row_is_exit_game(const std::uint8_t* row) {
    const std::uint64_t want = guest(kExitGameOpener);
    for (std::size_t i = 0; i + sizeof(std::uint64_t) <= kBuilderRowStride;
         i += sizeof(std::uint64_t)) {
        std::uint64_t v = 0;
        std::memcpy(&v, row + i, sizeof(v));
        if (v == want) {
            return true;
        }
    }
    return false;
}

std::uint8_t* builder_last_row(void* builder) {
    std::uint64_t begin = 0, end = 0;
    auto* b = static_cast<std::uint8_t*>(builder);
    std::memcpy(&begin, b + kBuilderBegin, sizeof(begin));
    std::memcpy(&end, b + kBuilderEnd, sizeof(end));
    if (!begin || end <= begin || (end - begin) % kBuilderRowStride != 0) {
        return nullptr;
    }
    return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(end - kBuilderRowStride));
}

// The game's own pop, replayed: slot 1 of the row's vtable, then drop the end
// pointer by one row.
void pop_last_row(void* builder) {
    std::uint8_t* row = builder_last_row(builder);
    if (!row) {
        return;
    }
    std::uint64_t vt = 0, dtor = 0;
    std::memcpy(&vt, row, sizeof(vt));
    std::memcpy(&dtor, reinterpret_cast<void*>(static_cast<std::uintptr_t>(vt + 8)), sizeof(dtor));
    hle_call_guest<std::int64_t>(reinterpret_cast<void*>(static_cast<std::uintptr_t>(dtor)), row);
    std::uint64_t end = 0;
    auto* b = static_cast<std::uint8_t*>(builder);
    std::memcpy(&end, b + kBuilderEnd, sizeof(end));
    end -= kBuilderRowStride;
    std::memcpy(b + kBuilderEnd, &end, sizeof(end));
}


std::uint8_t display[5]{},last_display[5]{},effects[6]{},last_effects[6]{},plugins[4]{},last_plugins[4]{},sharpness=0,last_sharpness=0;
std::int32_t cap_value=30,last_cap=30,resolution_value=4,last_resolution=4;
std::uint8_t lights=0,last_lights=0,frame_generation=0,last_frame_generation=0;
std::int32_t lod=0,last_lod=0;
const Choice lods[]{{-2,126071},{0,126072},{1,126073},{2,126074}};
const char* plugin_names[]{"randomizer","boss_rush","mutators","debug_menu"};
const Choice caps[]{{30,126030},{60,126031},{90,126032},{120,126033},{144,126034},{165,126035},{240,126036},{0,126037}};
const Choice sizes[]{{0,126040},{1,126041},{2,126042},{3,126043},{4,126044},{5,126045},{6,126046}};
void seed() {
    const auto v=Settings();
    const bool ds[]{v.vsync,v.fullscreen,v.show_fps,v.metalfx,false};
    const bool es[]{v.game_aa,v.motion_blur,v.dof,v.chromatic_aberration,v.ssao,v.dynamic_shadows};
    for (int k=0;k<5;++k) display[k]=last_display[k]=ds[k];
    for (int k=0;k<6;++k) effects[k]=last_effects[k]=es[k];
    for (int k=0;k<4;++k) plugins[k]=last_plugins[k]=config_value(std::string("plugins.")+plugin_names[k])=="true";
    sharpness=last_sharpness=std::clamp(int(v.sharpness*10+.5f),0,10);
    cap_value=last_cap=v.frame_cap;
    for (int k=0;k<MacSettings::ResolutionCount;++k) if (v.render_width==MacSettings::Widths[k]) resolution_value=k;
    last_resolution=resolution_value; seeded=true;
    lights=last_lights=v.optimized_lights; lod=last_lod=v.model_lod;
    frame_generation=last_frame_generation=v.frame_generation;
}
GUEST_ABI void display_handler(void* dialog,void*) {
    seed(); alignas(16) std::uint8_t scratch[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kScratchInit),scratch);
    for (int k=0;k<5;++k) add_toggle_row(dialog,&display[k],126000+k,k==0 || k==2);
    add_list_row(dialog,&cap_value,126005,caps,std::size(caps),30);
}
GUEST_ABI void effects_handler(void* dialog,void*) {
    seed(); alignas(16) std::uint8_t scratch[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kScratchInit),scratch);
    for (int k=0;k<6;++k) add_toggle_row(dialog,&effects[k],126010+k,1);
}
GUEST_ABI void mods_handler(void* dialog,void*) {
    seed(); alignas(16) std::uint8_t scratch[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kScratchInit),scratch);
    for (int k=0;k<4;++k) add_toggle_row(dialog,&plugins[k],126060+k,0);
    add_slider_row(dialog,&sharpness,126064,0);
    add_list_row(dialog,&resolution_value,126065,sizes,std::size(sizes),4);
}
GUEST_ABI void performance_handler(void* dialog,void*) {
    seed(); alignas(16) std::uint8_t scratch[0x100]{};
    hle_call_guest<std::int64_t>(guest_fn(kScratchInit),scratch);
    add_toggle_row(dialog,&lights,126070,0);
    add_toggle_row(dialog,&frame_generation,126076,0);
    add_list_row(dialog,&lod,126075,lods,std::size(lods),0);
}
// Our sections save each edit immediately. Remove the engine's appended
// Defaults row so it cannot scroll captions away from their fixed widgets.
void drop_defaults_row(std::uint8_t* dialog) {
    constexpr std::size_t begin_at=0xe58,end_at=0xe60,stride=0x90,widget_at=0x80;
    std::uint64_t begin=0,end=0;
    std::memcpy(&begin,dialog+begin_at,8); std::memcpy(&end,dialog+end_at,8);
    if (!begin || end<begin+stride) return;
    auto* row=reinterpret_cast<std::uint8_t*>(end-stride);
    const char16_t* label=nullptr; std::memcpy(&label,row,8);
    if (label!=message_text(kDefaultsRowId)) return;
    Captions::free_wstring(row+0x08); Captions::free_wstring(row+0x48);
    std::uint64_t widget=0; std::memcpy(&widget,row+widget_at,8);
    if (widget) {
        auto* w=reinterpret_cast<std::uint8_t*>(widget);
        std::int32_t refs=0; std::memcpy(&refs,w+8,4);
        const auto left=refs-1; std::memcpy(w+8,&left,4);
        if (refs==1) {
            std::uint64_t vt=0,dtor=0; std::memcpy(&vt,w,8);
            std::memcpy(&dtor,reinterpret_cast<void*>(vt),8);
            hle_call_guest<std::int64_t>(reinterpret_cast<void*>(dtor),w);
        }
    }
    end-=stride; std::memcpy(dialog+end_at,&end,8);
    hle_call_guest<std::int64_t>(guest_fn(kLayoutRows),dialog,0);
}
std::int64_t open_section(void* root,void* params,const char* name,void* handler) {
    std::snprintf(g_open_name,sizeof(g_open_name),"%s",name);
    const auto dialog=hle_call_guest<std::int64_t>(guest_fn(kOpenSection),root,params,g_open_name,handler,0,0);
    if (dialog) drop_defaults_row(reinterpret_cast<std::uint8_t*>(dialog));
    return dialog;
}
GUEST_ABI std::int64_t display_open(void* root,void* params) { return open_section(root,params,"PCSetting",reinterpret_cast<void*>(&display_handler)); }
GUEST_ABI std::int64_t effects_open(void* root,void* params) { return open_section(root,params,"PCGraphics",reinterpret_cast<void*>(&effects_handler)); }
GUEST_ABI std::int64_t mods_open(void* root,void* params) { return open_section(root,params,"PCControls",reinterpret_cast<void*>(&mods_handler)); }
GUEST_ABI std::int64_t performance_open(void* root,void* params) { return open_section(root,params,"PCDeck",reinterpret_cast<void*>(&performance_handler)); }
GUEST_ABI void* display_row(void* out,void* ctx) { return row_open(out,ctx,reinterpret_cast<void*>(&display_open)); }
GUEST_ABI void* effects_row(void* out,void* ctx) { return row_open(out,ctx,reinterpret_cast<void*>(&effects_open)); }
GUEST_ABI void* mods_row(void* out,void* ctx) { return row_open(out,ctx,reinterpret_cast<void*>(&mods_open)); }
GUEST_ABI void* performance_row(void* out,void* ctx) { return row_open(out,ctx,reinterpret_cast<void*>(&performance_open)); }
GUEST_ABI void* pc_system_finalize(void* out,void* builder,void* arg3) {
    auto last=builder_last_row(builder); const bool move=last && row_is_exit_game(last);
    if (move) pop_last_row(builder);
    void* handlers[]{reinterpret_cast<void*>(&display_row),reinterpret_cast<void*>(&effects_row),reinterpret_cast<void*>(&mods_row),reinterpret_cast<void*>(&performance_row)};
    const std::uint32_t ids[]{110006,110007,110008,110012}; std::int64_t flag=0;
    for (int k=0;k<4;++k) {
        Captions c(ids[k]); GuestFunction f(kRowFunctorVTable,handlers[k]);
        hle_call_guest<std::int64_t>(guest_fn(kAddCommandRow),builder,c.buf,f.buf,&flag);
    }
    if (move) {
        Captions c(kExitGameRowId); GuestFunction f(kRowFunctorVTable,guest_fn(kExitGameOpener));
        hle_call_guest<std::int64_t>(guest_fn(kAddExitRow),builder,c.buf,f.buf);
    }
    hle_call_guest<std::int64_t>(guest_fn(kSystemFinalize),out,builder,arg3); return out;
}
bool patch_section_table(ElfImage* image) {
    const std::uint64_t slot = guest(kSectionNameTable) + 6 * 8;
    const std::uint64_t imm = guest(kSectionCountImm);
    auto* p = static_cast<std::uint8_t*>(guest_ptr(image->mem, slot));
    auto* c = static_cast<std::uint8_t*>(guest_ptr(image->mem, imm));
    std::uint64_t existing = 0, existing2 = 0;
    std::memcpy(&existing, p, 8);
    std::memcpy(&existing2, p + 8, 8);
    if (existing != 0 || existing2 != 0 || *c != 6) {
        host_log("pc-options: refused, section table slot 0x%llx is %llu and the bound is %u",
                 static_cast<unsigned long long>(slot), static_cast<unsigned long long>(existing),
                 *c);
        return false;
    }
    if (!guest_protect_rw(&image->mem, slot & ~0xfffull, 0x1000)) {
        host_log("pc-options: cannot unprotect the section table");
        return false;
    }
    const std::uint64_t name = reinterpret_cast<std::uint64_t>(g_open_name);
    std::memcpy(p, &name, 8);
    if (!guest_protect_rwx(&image->mem, imm & ~0xfffull, 0x1000)) {
        host_log("pc-options: cannot unprotect the section loop");
        return false;
    }
    *c = 7;  // one more name in the table, so the loop walks seven
    guest_protect_rx(&image->mem, imm & ~0xfffull, 0x1000);
    return true;
}

bool patch_system_row(ElfImage* image) {
    const std::uint64_t call = guest(kSystemFinalizeCall);
    const std::uint64_t pad = guest(kTrampolinePad);
    auto* cp = static_cast<std::uint8_t*>(guest_ptr(image->mem, call));
    auto* pp = static_cast<std::uint8_t*>(guest_ptr(image->mem, pad));
    std::int32_t rel = 0;
    std::memcpy(&rel, cp + 1, sizeof(rel));
    if (cp[0] != 0xe8 || call + 5 + static_cast<std::uint64_t>(rel) != guest(kSystemFinalize)) {
        host_log("pc-options: refused, 0x%llx is not the call to sub_1fea630",
                 static_cast<unsigned long long>(kSystemFinalizeCall));
        return false;
    }
    if (std::memcmp(pp, kPadBytes, sizeof(kPadBytes)) != 0) {
        host_log("pc-options: refused, 0x%llx is not the alignment padding",
                 static_cast<unsigned long long>(kTrampolinePad));
        return false;
    }
    if (!guest_protect_rwx(&image->mem, call & ~0xfffull, 0x1000)) {
        host_log("pc-options: cannot unprotect the System command list");
        return false;
    }
    const std::uint64_t target =
        reinterpret_cast<std::uint64_t>(hle_wrap_fn(reinterpret_cast<void*>(&pc_system_finalize)));
    std::uint8_t* t = pp;
    *t++ = 0x49;
    *t++ = 0xbb;  // movabs r11, target
    std::memcpy(t, &target, 8);
    t += 8;
    *t++ = 0x41;
    *t++ = 0xff;
    *t++ = 0xe3;  // jmp r11
    const std::int32_t to_pad = static_cast<std::int32_t>(static_cast<std::int64_t>(pad) -
                                                         static_cast<std::int64_t>(call + 5));
    std::memcpy(cp + 1, &to_pad, sizeof(to_pad));
    guest_protect_rx(&image->mem, call & ~0xfffull, 0x1000);
    return true;
}


} // namespace
void InstallGameMenu(ElfImage* image) {
    if (!image || !engine_eboot_supported(image->sha256)) return;
    g_slide=image->mem.slide;
    installed=patch_section_table(image) && patch_system_row(image);
    host_log("Mac System menu: %s",installed ? "Display, Graphics, Mods and Performance added" : "unexpected code; native panel remains available");
}
void PollMenu() {
    if (auto tick=extra_game_tick.load(std::memory_order_acquire)) tick();
    if (TakeDebugToggle()) debug_menu_toggle();
    if (!installed || !seeded) return;
    auto v=Settings(); bool changed=false;
    bool* ds[]{&v.vsync,&v.fullscreen,&v.show_fps,&v.metalfx};
    bool* es[]{&v.game_aa,&v.motion_blur,&v.dof,&v.chromatic_aberration,&v.ssao,&v.dynamic_shadows};
    for (int k=0;k<4;++k) if (display[k]!=last_display[k]) { *ds[k]=display[k]!=0; changed=true; }
    for (int k=0;k<6;++k) if (effects[k]!=last_effects[k]) { *es[k]=effects[k]!=0; changed=true; }
    if (sharpness!=last_sharpness) { v.sharpness=float(sharpness)/10; changed=true; }
    if (cap_value!=last_cap) { v.frame_cap=cap_value; changed=true; }
    if (lights!=last_lights) { v.optimized_lights=lights!=0; changed=true; }
    if (lod!=last_lod) { v.model_lod=lod; changed=true; }
    if (frame_generation!=last_frame_generation) {v.frame_generation=frame_generation!=0;changed=true;}
    if (resolution_value!=last_resolution) {
        const auto k=std::clamp(int(resolution_value),0,MacSettings::ResolutionCount-1);
        v.render_width=MacSettings::Widths[k]; v.render_height=MacSettings::Heights[k]; changed=true;
    }
    if (changed && !RequestSettings(v)) { seed(); return; }
    if (display[4]) { RequestRestart(); display[4]=0; }
    for (int k=0;k<4;++k) if (plugins[k]!=last_plugins[k]) {
        plugins_set_enabled(plugin_names[k],plugins[k]!=0);
        host_message_show("Plugin selection saved. Restart to apply; plugin options are in the Mac settings panel.",8);
    }
    std::copy(std::begin(display),std::end(display),last_display);
    std::copy(std::begin(effects),std::end(effects),last_effects);
    std::copy(std::begin(plugins),std::end(plugins),last_plugins);
    last_cap=cap_value; last_resolution=resolution_value; last_sharpness=sharpness;
    last_lights=lights; last_lod=lod;last_frame_generation=frame_generation;
}
} // namespace MacFeatures
