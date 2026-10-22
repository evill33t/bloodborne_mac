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
#ifndef BB_CPU_TRANSLATOR
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
#endif
namespace MacFeatures {
void InstallGpuFenceWaits(ElfImage* image) {
    const char* enabled=std::getenv("BB_METAL_PARK_FENCES");
    if(!image || !engine_eboot_supported(image->sha256) || (enabled && !std::strcmp(enabled,"0"))) return;
    struct Site {std::uint64_t rva;const unsigned char* bytes;std::size_t size,call;void* replacement;};
    static constexpr unsigned char flush[]{0x49,0x8b,0x06,0x48,0x83,0xf8,0x01,0x74,0x15,0xe8,0x2a,0x21,0x88,0x02,0x44,0x29,0xf8};
    static constexpr unsigned char counter[]{0x41,0xb5,0x01,0x41,0x81,0xfe,0x0f,0x27,0,0,0x77,0x40,0xe8,0xd5,0xd1,0x81,0x02};
    Site sites[]{{0x33c960,flush,sizeof(flush),9,nullptr},
        {0x3a18b2,counter,sizeof(counter),12,nullptr}};
    for(const auto& site:sites) {
        if(site.rva>image->mem.size || site.size>image->mem.size-site.rva ||
            std::memcmp(static_cast<unsigned char*>(image->mem.base)+site.rva,site.bytes,site.size)) {
            host_log("Mac GPU fence parking refused: instructions differ at RVA %#llx",(unsigned long long)site.rva);return;
        }
    }
#ifdef BB_CPU_TRANSLATOR
    // Recreate the x86 register adapters inside the registered guest arena.
    const unsigned char flush_args[]{0x4c,0x89,0xf7,0x4c,0x89,0xfe};
    const unsigned char counter_args[]{0x48,0x89,0xdf,0x48,0x8b,0x75,0xc8,
        0x4c,0x89,0xfa,0x48,0x8b,0x4d,0xc0};
    const unsigned char* args[]{flush_args,counter_args};
    const std::size_t lengths[]{sizeof(flush_args),sizeof(counter_args)};
    const std::uint64_t targets[]{reinterpret_cast<std::uint64_t>(&bbmac_gpu_flush_clock_impl),
        reinterpret_cast<std::uint64_t>(&bbmac_gpu_counter_clock_impl)};
    for (unsigned i=0;i<2;++i) {
        auto* stub=static_cast<unsigned char*>(host_page_alloc(4096,true));
        if (!stub) return;
        std::memcpy(stub,args[i],lengths[i]);
        auto* end=stub+lengths[i];end[0]=0x48;end[1]=0xb8;
        std::memcpy(end+2,&targets[i],8);end[10]=0xff;end[11]=0xe0;
        sites[i].replacement=stub;
    }
#else
    sites[0].replacement=reinterpret_cast<void*>(&bbmac_gpu_flush_clock);
    sites[1].replacement=reinterpret_cast<void*>(&bbmac_gpu_counter_clock);
#endif
    for(const auto& site:sites) {
        const bool installed=RedirectCheckedCall(image,image->mem.slide+site.rva+site.call,
            image->mem.slide+0x2bbea98,site.replacement);
        host_log("Mac GPU fence clock RVA %#llx: %s",(unsigned long long)(site.rva+site.call),installed ? "completion-driven parking installed" : "original polling retained");
    }
}
}
