// SPDX-License-Identifier: GPL-3.0-or-later
// Cooperative clocks at the two verified 1.09 GPU-fence polling call sites.
// The original comparisons, flush calls, return values and timeout logic run.
#include "core/elf.h"
#include "engine/addr.h"
#include "core/portable.h"
#include "log.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" std::uint64_t runtime_process_time_us();
extern "C" int bbmetal_wait_gpu_label(std::uint64_t,std::uint64_t,unsigned,std::uint64_t);
namespace MacFeatures {
bool RedirectCheckedCall(ElfImage*,std::uint64_t,std::uint64_t,void*);
}
extern "C" std::uint64_t bbmac_gpu_flush_clock_impl(std::uint64_t label,std::uint64_t start) {
    const auto now=runtime_process_time_us();
    const auto elapsed=std::uint32_t(now)-std::uint32_t(start);
    if(elapsed<100000) bbmetal_wait_gpu_label(label,1,3,std::min<std::uint64_t>(250,100000-elapsed));
    return runtime_process_time_us();
}
extern "C" std::uint64_t bbmac_gpu_counter_clock_impl(std::uint64_t object,std::uint64_t target,
    std::uint64_t first,std::uint64_t start) {
    const auto now=runtime_process_time_us();
    const auto elapsed=std::uint32_t(now)-std::uint32_t(start);
    std::uint64_t label=0;
    // The first iteration must still invoke the guest's flush before parking.
    if(!(first&1) && elapsed<10000 && object<=UINT64_MAX-0xd8 &&
        host_read_safe(reinterpret_cast<void*>(object+0xd8),&label,sizeof(label)))
        bbmetal_wait_gpu_label(label,target,5,std::min<std::uint64_t>(250,10000-elapsed));
    return runtime_process_time_us();
}
extern "C" void bbmac_gpu_flush_clock();
extern "C" void bbmac_gpu_counter_clock();
// These replace a no-argument clock CALL, not a function entry. All supplied
// registers/stack slots were verified in the original instruction trace.
asm(R"(
.text
.globl _bbmac_gpu_flush_clock
_bbmac_gpu_flush_clock:
    mov %r14,%rdi
    mov %r15,%rsi
    jmp _bbmac_gpu_flush_clock_impl
.globl _bbmac_gpu_counter_clock
_bbmac_gpu_counter_clock:
    mov %rbx,%rdi
    mov -0x38(%rbp),%rsi
    mov %r15,%rdx
    mov -0x40(%rbp),%rcx
    jmp _bbmac_gpu_counter_clock_impl
)");
namespace MacFeatures {
void InstallGpuFenceWaits(ElfImage* image) {
    const char* enabled=std::getenv("BB_METAL_PARK_FENCES");
    if(!image || !engine_eboot_supported(image->sha256) || (enabled && !std::strcmp(enabled,"0"))) return;
    struct Site {std::uint64_t rva;const unsigned char* bytes;std::size_t size,call;void* replacement;};
    static constexpr unsigned char flush[]{0x49,0x8b,0x06,0x48,0x83,0xf8,0x01,0x74,0x15,0xe8,0x2a,0x21,0x88,0x02,0x44,0x29,0xf8};
    static constexpr unsigned char counter[]{0x41,0xb5,0x01,0x41,0x81,0xfe,0x0f,0x27,0,0,0x77,0x40,0xe8,0xd5,0xd1,0x81,0x02};
    const Site sites[]{{0x33c960,flush,sizeof(flush),9,reinterpret_cast<void*>(&bbmac_gpu_flush_clock)},
        {0x3a18b2,counter,sizeof(counter),12,reinterpret_cast<void*>(&bbmac_gpu_counter_clock)}};
    for(const auto& site:sites) {
        if(site.rva>image->mem.size || site.size>image->mem.size-site.rva ||
            std::memcmp(static_cast<unsigned char*>(image->mem.base)+site.rva,site.bytes,site.size)) {
            host_log("Mac GPU fence parking refused: instructions differ at RVA %#llx",(unsigned long long)site.rva);return;
        }
    }
    for(const auto& site:sites) {
        const bool installed=RedirectCheckedCall(image,image->mem.slide+site.rva+site.call,
            image->mem.slide+0x2bbea98,site.replacement);
        host_log("Mac GPU fence clock RVA %#llx: %s",(unsigned long long)(site.rva+site.call),installed ? "completion-driven parking installed" : "original polling retained");
    }
}
}
