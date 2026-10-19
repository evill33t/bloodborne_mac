// SPDX-License-Identifier: GPL-3.0-or-later
#include "transient_gpu_budget.h"
#include "core/elf.h"
#include "core/portable.h"
#include "engine/graphics_patch.h"
#include "guest_abi.h"
#include "log.h"
#include <atomic>
#include <cstdlib>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <string>

namespace MacFeatures {
namespace {
struct BudgetState {std::uint64_t slide=0;std::string hash;std::atomic<unsigned> reports{0};};
// Installation happens before any guest entry; immutable thereafter apart
// from the bounded log counter. Hook/state remain process lifetime.
BudgetState budget;
bool installed=false;
__attribute__((noinline)) GUEST_ABI std::int64_t ApplyBudget(std::uint64_t,const std::uint64_t* saved) noexcept {
    using namespace TransientGpuBudget;
    if(!saved) return 0;
    const auto config_address=saved[2]; // Original RCX: native creation config.
    std::uint64_t return_ip=0;
    if(!host_read_safe(saved+6,&return_ip,sizeof(return_ip)) || return_ip!=budget.slide+CallerReturn ||
       !config_address || config_address>UINT64_MAX-ConfigBytes) return 0;
    std::array<std::uint8_t,ConfigBytes> config{};
    if(!host_read_safe(reinterpret_cast<const void*>(config_address),config.data(),config.size())) return 0;
    const auto result=Prepare(budget.hash,Prologue,config,config_address,320);
    if(result!=Decision::Expanded) return 0;
    // Native216b0f0 uses config+54 for BOTH backing allocation (216b20e) and
    // arena initialization (216b26d). Write the checked field once before
    // either read. No flags, retirement state, mapping or draw data change.
    const auto status=mach_vm_write(mach_task_self(),config_address+BudgetOffset,
        reinterpret_cast<vm_offset_t>(config.data()+BudgetOffset),sizeof(std::uint32_t));
    std::uint32_t actual=0;
    const bool verified=status==KERN_SUCCESS && host_read_safe(
        reinterpret_cast<const void*>(config_address+BudgetOffset),&actual,sizeof(actual)) && actual==ExpandedBytes;
    if(budget.reports.fetch_add(1,std::memory_order_relaxed)<8)
        host_log("Native transient GPU pool: config=%p budget160->320MiB %s",
            reinterpret_cast<void*>(config_address),verified?"verified":"write/readback failed");
    return 0; // Execute exact displaced prologue and original creation.
}
}
void InstallTransientGpuBudget(ElfImage* image) {
    using namespace TransientGpuBudget;
    const char* option=std::getenv("BBHOST_TRANSIENT_GPU_MB");
    if(!option || !*option || !std::strcmp(option,"160")) return;
    if(std::strcmp(option,"320")) {host_log("Native transient GPU budget refused: only160/320MiB accepted");return;}
    if(installed || !image || Rva>image->mem.size || Prologue.size()>image->mem.size-Rva ||
       image->mem.slide>UINT64_MAX-Rva-Prologue.size()) return;
    std::array<std::uint8_t,Prologue.size()> code{};
    const auto at=image->mem.slide+Rva;
    if(!host_read_safe(reinterpret_cast<const void*>(at),code.data(),code.size()) || !SupportedSite(image->sha256,code)) {
        host_log("Native transient GPU budget refused: unsupported hash or original creation bytes");return;
    }
    budget.slide=image->mem.slide;budget.hash=image->sha256;
    installed=engine_prologue_hook(image,at,Prologue.data(),Prologue.size(),reinterpret_cast<void*>(&ApplyBudget));
    if(!installed) {
        // The generic hook may fail its final RX protection after copying
        // the jump. Restore original bytes before declaring this refused.
        if(guest_protect_rwx(&image->mem,at,Prologue.size())) {
            std::memcpy(guest_ptr(image->mem,at),Prologue.data(),Prologue.size());
            if(!guest_protect_rx(&image->mem,at,Prologue.size()))
                host_log("Native transient GPU budget: original-byte RX restore failed");
        } else host_log("Native transient GPU budget: original-byte restore protection failed");
    }
    host_log("Native transient GPU budget320MiB experiment %s; original retirement preserved",
        installed?"installed":"refused: hook unavailable");
}
}
