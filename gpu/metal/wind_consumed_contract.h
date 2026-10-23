// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace Gpu::Metal::WindConsumed {
inline constexpr std::uint64_t ShaderHash=0xe4abfca709b00ef3ull;
inline constexpr std::uint64_t CacheTag=0x57494e444f425332ull; // WINDOBS2: private diagnostic ABI
inline constexpr unsigned Binding=14,MaxFrames=120,DrawsPerFrame=2,SampleMask=63;
inline constexpr std::uint32_t MagicValue=0x57494e44u,SchemaValue=2;
enum Word : std::size_t {
    Magic,Schema,CounterOverflow,WindSamples,ViewSamples,WindNull,ViewNull,
    WindLeafLoMin,WindLeafLoMax,WindLeafHiMin,WindLeafHiMax,
    ViewLeafLoMin,ViewLeafLoMax,ViewLeafHiMin,ViewLeafHiMax,
    VertexMin,VertexMax,InstanceMin,InstanceMax,Reserved,
    WindMin,WindMax=WindMin+4,ViewMin=WindMax+4,ViewMax=ViewMin+16,Words=ViewMax+16
};
enum class Site : unsigned {Wind,View};
using Record=std::array<std::uint32_t,Words>;
static_assert(Words==60 && sizeof(Record)==240);
inline constexpr Record InitialRecord() noexcept {
    Record r{};r[Magic]=MagicValue;r[Schema]=SchemaValue;
    for(auto word:{WindLeafLoMin,WindLeafHiMin,ViewLeafLoMin,ViewLeafHiMin,VertexMin,InstanceMin})r[word]=~0u;
    for(unsigned k=0;k<4;++k)r[WindMin+k]=~0u;
    for(unsigned k=0;k<16;++k)r[ViewMin+k]=~0u;
    return r;
}
inline unsigned ConfiguredFrames(const char* value) noexcept {
    if(!value || !*value)return 0;
    unsigned frames=0;
    for(const char* p=value;*p;++p) {
        if(*p<'0' || *p>'9')return 0;
        // Saturate before multiplying again, including arbitrarily long input.
        if(frames<MaxFrames)frames=frames*10+unsigned(*p-'0');
        if(frames>MaxFrames)frames=MaxFrames;
    }
    return frames;
}
struct Window {
    unsigned frames=0,draws=0,total=0;
    std::uint64_t first=~0ull,last=~0ull;
    explicit Window(unsigned requested=0):frames(requested>MaxFrames ? MaxFrames : requested) {}
    bool Request(std::uint64_t frame,bool trace) noexcept {
        if(!frames)return false;
        if(first==~0ull) {if(!trace)return false;first=frame;}
        return frame>=first && frame-first<frames && total<frames*DrawsPerFrame &&
            (last!=frame || draws<DrawsPerFrame);
    }
    bool Reserve(std::uint64_t frame) noexcept {
        if(first==~0ull || !Request(frame,false))return false;
        if(last!=frame) {last=frame;draws=0;}
        ++draws;++total;return true;
    }
};
inline bool Valid(const Record& r) noexcept {
    return r[Magic]==MagicValue && r[Schema]==SchemaValue && !r[CounterOverflow];
}
inline std::uint32_t Samples(const Record& r,Site site) noexcept {
    return r[site==Site::Wind ? WindSamples : ViewSamples];
}
inline bool Varied(const Record& r,Site site,unsigned component) noexcept {
    const auto count=site==Site::Wind ? 4u : 16u;
    const auto lo=site==Site::Wind ? WindMin : ViewMin,hi=site==Site::Wind ? WindMax : ViewMax;
    return Valid(r) && component<count && Samples(r,site)>1 && r[lo+component]!=r[hi+component];
}
inline bool SelectedLeafUniform(const Record& r,Site site) noexcept {
    const auto lo=site==Site::Wind ? WindLeafLoMin : ViewLeafLoMin;
    return Valid(r) && Samples(r,site)>0 && r[lo]==r[lo+1] && r[lo+2]==r[lo+3];
}
} // namespace Gpu::Metal::WindConsumed
