// SPDX-License-Identifier: GPL-3.0-or-later
// Shared game message lookup: preserve the Mac System menu and Nightwatch's NPC,
// lamp and travel dialogue. Adapted from Nightwatch bbport_game_menu.cpp.
#include "../vendor/nightwatch/gpu/shim/bbport_game_menu.h"
#include "../vendor/nightwatch/gpu/shim/bbport_game_menu_pages.h"
#include "../vendor/nightwatch/gpu/shim/party/coop_hooks.h"
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>
#include <string>
namespace BbGameMenu {
namespace {
using Lookup=const char16_t*(*)(void*,std::uint32_t,std::uint32_t,std::uint32_t);
Lookup original=nullptr;
std::atomic<void*> repository{nullptr};
std::atomic<std::uint32_t> table{0};
std::atomic<TextOverrideFn> override_text{nullptr};
std::atomic<ImePresetFn> ime{nullptr};
const char16_t* Custom(std::uint32_t id) {
    switch(id) {
    case 127000: return u"Travel with the party?";
    case 127001: return u"Yes";
    case 127002: return u"No";
    case 127004: return u"Start the ending with the party?";
    case 127005: return u"Gather at the host's lamp?";
    case 127006: return u"Travel with your companions?";
    default: return nullptr;
    }
}
const char16_t* LookupHook(void* repo,std::uint32_t page,std::uint32_t category,std::uint32_t id) {
    repository=repo; table=page;
    if(auto text=Custom(id)) return text;
    const auto text=original(repo,page,category,id);
    if(auto fn=override_text.load()) if(auto replacement=fn(category,id,text)) return replacement;
    return text;
}
}
void InstallNightwatchMessages() {
    void* trampoline=nullptr;
    if (!coop::ReplacePrologue(0xf6f340,{0x89,0xf0,0x48,0x8b,0x77,0x08,0x48,0x8b,0x34,0xc6},
            reinterpret_cast<void*>(&LookupHook),&trampoline,"Mac Nightwatch message lookup"))
        throw std::runtime_error("Nightwatch could not install the game message lookup.");
    original=reinterpret_cast<Lookup>(trampoline);
}
void TextActivity(std::uint64_t* seq,std::int64_t* ms) { if(seq)*seq=0; if(ms)*ms=0; }
std::int64_t TextSeenMs(const char*) { return 0; } // Diagnostic menu scripts are not selected.
void RecentTexts(std::int64_t,void (*)(const char16_t*,std::int64_t,void*),void*) {}
std::int64_t LastLookupMs(std::uint32_t) { return 0; }
int RecentLookups(std::int64_t,std::uint32_t*,int) { return 0; }
const char16_t* GameText(std::uint32_t category,std::uint32_t id) {
    const auto repo=repository.load();
    return repo && original ? original(repo,table.load(),category,id) : nullptr;
}
void SetTextOverride(TextOverrideFn fn) { override_text=fn; }
void SetImePreset(ImePresetFn fn) { ime=fn; }
const char* ImePreset(std::uint32_t limit) { auto fn=ime.load(); return fn ? fn(limit) : nullptr; }
namespace Pages {
u32 SplitTravelText(int which) { return 127000+which; }
u32 StoryNoticeText(u32 message) { return message; }
u32 EndingQuestionText() { return 127004; }
u32 GatherQuestionText() { return 127005; }
u32 TravelVoteQuestionText() { return 127006; }
void SetTravelVoteQuestion(const std::string&,const std::u16string&) {}
}
}
