// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <string_view>

namespace Gpu::Metal::ConstantMemoryMap {
inline bool Configured(const char* value) noexcept {
    return value && value[0]=='1' && !value[1];
}
// UpdateMemMap allocates a fresh, separate Metal buffer for each version.
// Only that immutable header/page table changes address space. Guest memory
// pointers, stores, bindings, page arithmetic and command lifetimes stay intact.
// Admit only the complete generated ABI; a changed/foreign layout stays as-is.
inline bool Apply(std::string& source) {
    const auto count=[&](std::string_view text) {
        std::size_t found=0,at=0;
        while((at=source.find(text,at))!=std::string::npos){++found;at+=text.size();}
        return found;
    };
    constexpr std::string_view types[]{
        "device const DcMap* map;",
        "device const DcMap& gmap [[buffer(3)]]",
        "((device const int*)(m.map + 1))"};
    constexpr std::string_view replacements[]{
        "constant DcMap* map;",
        "constant DcMap& gmap [[buffer(3)]]",
        "((constant int*)(m.map + 1))"};
    if(count("device const DcMap")!=2)return false;
    for(const auto text:types)if(count(text)!=1)return false;
    // The optional native vertex-fetch helper has its own page lookup.
    constexpr std::string_view fetch_cast="((device const int*)(m.map+1))";
    const auto fetch_count=count(fetch_cast);
    if(fetch_count>1 || count("(device const int*)")!=1+fetch_count)return false;
    // Every replacement is shorter, and all markers were checked before edits.
    for(unsigned i=0;i<3;++i)source.replace(source.find(types[i]),types[i].size(),replacements[i]);
    if(fetch_count)source.replace(source.find(fetch_cast),fetch_cast.size(),"((constant int*)(m.map+1))");
    return true;
}
} // namespace Gpu::Metal::ConstantMemoryMap
