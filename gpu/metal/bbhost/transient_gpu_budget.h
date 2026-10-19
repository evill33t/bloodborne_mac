// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine/addr.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

struct ElfImage;
namespace MacFeatures {
namespace TransientGpuBudget {
constexpr std::uint64_t Rva=0x216b0f0,CallerReturn=0x216545a;
constexpr std::size_t BudgetOffset=0x54,ConfigBytes=0x58;
constexpr std::uint32_t OriginalBytes=160u*1024*1024,ExpandedBytes=320u*1024*1024;
constexpr std::array<std::uint8_t,20> Prologue{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,
    0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xec,0x18,0x4d,0x89,0xcf};
enum class Decision {Refuse,Original,Expanded,AlreadyExpanded};
inline bool SupportedSite(std::string_view hash,std::span<const std::uint8_t> code) {
    return engine_eboot_supported(hash) && code.size()==Prologue.size() &&
        std::equal(code.begin(),code.end(),Prologue.begin());
}
// The runtime uses this exact checked transformation on a local config copy,
// then writes only its budget DWORD. Refusal never changes any config byte.
inline Decision Prepare(std::string_view hash,std::span<const std::uint8_t> code,
                        std::span<std::uint8_t> config,std::uint64_t address,
                        std::uint64_t requested_mb) {
    if(!SupportedSite(hash,code) || config.size()<ConfigBytes || !address || (address&3) ||
       address>std::numeric_limits<std::uint64_t>::max()-ConfigBytes ||
       requested_mb>std::numeric_limits<std::uint32_t>::max()/(1024u*1024u) ||
       (requested_mb!=160 && requested_mb!=320)) return Decision::Refuse;
    std::uint32_t current=0;std::memcpy(&current,config.data()+BudgetOffset,sizeof(current));
    if(requested_mb==160) return current==OriginalBytes ? Decision::Original : Decision::Refuse;
    if(current==ExpandedBytes) return Decision::AlreadyExpanded;
    if(current!=OriginalBytes) return Decision::Refuse;
    const auto target=ExpandedBytes;std::memcpy(config.data()+BudgetOffset,&target,sizeof(target));
    return Decision::Expanded;
}
}
// Default original. BBHOST_TRANSIENT_GPU_MB accepts only160 or320; changes
// loaded config memory before native creation, never the eboot/game files.
void InstallTransientGpuBudget(ElfImage* image);
}
