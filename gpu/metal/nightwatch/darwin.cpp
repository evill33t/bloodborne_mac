// SPDX-License-Identifier: GPL-3.0-or-later
// Darwin glue for Nightwatch. The Mac settings panel owns role changes; upstream
// Windows launcher, diagnostic automation and Vulkan menu pages are not used.
#include "integration.h"
#include "bbport_settings.h"
#include "bbport_game_menu.h"
#include "bbport_game_menu_pages.h"
#include "party/party_flow.h"
#include "party/coop_hooks.h"
#include "party/game_state.h"
#include "party/test_newgame.h"
#include "party/party_async_io.h"
#include "core/portable.h"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <atomic>
#include <pthread.h>

extern "C" void restore_guest_fs() {} // Darwin's guest GS remains installed by the runtime.
extern "C" const char* bbport_build_id() { return BB_NIGHTWATCH_BUILD_ID; }
namespace BbSettings {
#include "party_settings.inc"
const char* UpscalerName(int) { return "direct Metal"; }
Values& Get() { static Values values; return values; }
}
namespace party::flow {
namespace { std::mutex mutex; std::string secret; }
std::string RuntimeCode() { auto p=std::getenv("BB_PARTY_CODE"); return p ? p : ""; }
std::string RuntimePassword() { return {}; }
std::string RuntimeName() { auto p=std::getenv("BB_PARTY_NAME"); return p ? p : "Hunter"; }
std::string RuntimeHostSecret() {
    std::scoped_lock lock(mutex);
    if (secret.empty()) { std::ifstream f(std::filesystem::path(std::getenv("BB_USER_DIR"))/"nightwatch-secret.txt"); f>>secret; }
    return secret;
}
void StoreHostSecret(const std::string& hex) {
    std::scoped_lock lock(mutex); secret=hex;
    const auto path=std::filesystem::path(std::getenv("BB_USER_DIR"))/"nightwatch-secret.txt";
    const auto temp=path.string()+".tmp";
    std::ofstream f(temp); f<<hex<<'\n'; f.close();
    if (f) { std::error_code ec; std::filesystem::permissions(temp,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,ec); std::filesystem::rename(temp,path,ec); }
}
void NoteCharacterInWorld() {}
int CurrentSaveSlot() {
    const auto slot=coop::Guest(0x553b130); std::uint64_t man=0, data=0; int index=-1;
    if (!coop::SafeRead(slot,&man,8) || !man || !coop::SafeRead(man+0x50,&data,8) || !data || !coop::SafeRead(data+2000,&index,4)) return -1;
    return index>=0 && index<10 ? index : -1;
}
}
namespace coop {
bool PrologueSkipBusy() { return false; }
namespace warper { void Tick() {} } // Upstream developer world-warp UI is not selected.
bool NewGameTestRequested() { return false; }
bool NewGameOwnsTitle() { return false; }
void NewGameTestTick(const GameSnapshot&,bool) {}
}

namespace BbEngine { bool FrameTickInstalled() { return false; } }
extern "C" int runtime_win_thread_name(std::uint32_t,char* out,std::size_t size) { return pthread_getname_np(pthread_self(),out,size); }
