// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted from bloodborne-native/bloodborne-runtime src/runtime_cmdbuf.c at
// 6228f43b2919d1abd1c4185e689048797855e540, built on bbport and shadPS4.
#include "command_guards.h"
#include "cpu/bbcpu.h"
#include "core/elf.h"
#include "engine/addr.h"
#include "log.h"
#include "src/compat_macos.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace MacCommandGuards {
namespace {
constexpr unsigned Capacity=1024;
std::array<std::atomic<std::uint64_t>,Capacity> keys{};
std::array<std::recursive_mutex,Capacity> locks;
std::atomic<bool> full_reported{false};
std::uint64_t Enter(const std::uint64_t regs[16],std::uint32_t context) {
    const auto owner=regs[7]; // PS4 RDI, not a host ABI register
    if (!owner || owner%8 || owner>UINT64_MAX-0x10) return 0;
    const auto key=owner+(context ? 0x10 : 0);
    unsigned i=unsigned((key*0x9e3779b97f4a7c15ull)>>54);
    for (unsigned n=0;n<Capacity;++n,i=(i+1)%Capacity) {
        auto observed=keys[i].load(std::memory_order_acquire);
        if (!observed) {
            if (keys[i].compare_exchange_strong(observed,key,std::memory_order_acq_rel)) observed=key;
        }
        if (observed!=key) continue;
        locks[i].lock();return i+1;
    }
    if (!full_reported.exchange(true)) host_log("Command-buffer guard table is full; new buffers run unguarded");
    return 0;
}
void Leave(std::uint64_t cookie) { locks.at(cookie-1).unlock(); }
}
void RegisterMethod(std::uint64_t address,bool context) {
    bbcpu_set_guard(address,Enter,Leave,context ? 1 : 0);
}
void Install(ElfImage* image) {
    if (!image || !engine_eboot_supported(image->sha256)) return;
    if (const auto* env=std::getenv("BB_CB_GUARD");env && env[0]=='0') return;
    static constexpr std::uint32_t methods[]{
#include "third_party/bbcpu/command_buffer_offsets.inc"
    };
    constexpr unsigned char prefix[]{0x55,0x48,0x89,0xe5};
    unsigned installed=0,refused=0;
    const auto add=[&](std::uint32_t rva,bool context) {
        unsigned char bytes[sizeof(prefix)]{};
        if (rva>image->mem.size || sizeof(prefix)>image->mem.size-rva ||
            !bb_macos_read_memory(image->mem.slide+rva,bytes,sizeof(bytes)) ||
            std::memcmp(bytes,prefix,sizeof(prefix))) { ++refused;return; }
        RegisterMethod(image->mem.slide+rva,context);++installed;
    };
    for (auto rva:methods) add(rva,false);
    add(0x2168630,true);add(0x26b4ef0,true);
    host_log("Native command buffers: %u writer guards installed, %u refused (BB_CB_GUARD=0: off)",installed,refused);
}
}
