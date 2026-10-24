// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace Gpu::Metal::WindConsumed {
inline constexpr std::uint64_t ShaderHash=0xe4abfca709b00ef3ull;
inline constexpr std::uint64_t CacheTag=0x57494e444f425335ull; // WINDOBS5: private diagnostic ABI
inline constexpr unsigned Binding=14,MaxFrames=120,DrawsPerFrame=2,SampleMask=63;
inline constexpr std::uint32_t MagicValue=0x57494e44u,SchemaValue=5;
enum Word : std::size_t {
    Magic,Schema,CounterOverflow,WindSamples,ViewSamples,WindNull,ViewNull,
    WindLeafLoMin,WindLeafLoMax,WindLeafHiMin,WindLeafHiMax,
    ViewLeafLoMin,ViewLeafLoMax,ViewLeafHiMin,ViewLeafHiMax,
    VertexMin,VertexMax,InstanceMin,InstanceMax,Reserved,
    WindMin,WindMax=WindMin+4,ViewMin=WindMax+4,ViewMax=ViewMin+16,
    CameraSamples=ViewMax+16,CameraNull,CameraLeafLoMin,CameraLeafLoMax,CameraLeafHiMin,CameraLeafHiMax,
    CameraMin,CameraMax=CameraMin+3,
    MatrixCandidates=CameraMax+3,MatrixSamples,MatrixOtherIndices,MatrixClaimMisses,MatrixRejectedBounds,MatrixSelectedIndex,
    MatrixLeafLoMin,MatrixLeafLoMax,MatrixLeafHiMin,MatrixLeafHiMax,
    MatrixDescriptorMin,MatrixDescriptorMax=MatrixDescriptorMin+4,
    MatrixRowsMin=MatrixDescriptorMax+4,MatrixRowsMax=MatrixRowsMin+3,
    MatrixMin=MatrixRowsMax+3,MatrixMax=MatrixMin+12,
    FixedFetchSamples=MatrixMax+12,FixedMatrixSamples,FixedUndeformedSamples,FixedDeformedSamples,FixedClipSamples,
    FixedFetchMin,FixedFetchMax=FixedFetchMin+36,
    FixedMatrixMin=FixedFetchMax+36,FixedMatrixMax=FixedMatrixMin+19,
    FixedUndeformedMin=FixedMatrixMax+19,FixedUndeformedMax=FixedUndeformedMin+3,
    FixedDeformedMin=FixedUndeformedMax+3,FixedDeformedMax=FixedDeformedMin+3,
    FixedClipMin=FixedDeformedMax+3,FixedClipMax=FixedClipMin+4,Words=FixedClipMax+4
};
enum class Site : unsigned {Wind,View,Camera};
struct SiteLayout {Word samples,null,leaf,minimum,maximum;unsigned count;};
inline constexpr SiteLayout Layout(Site site) noexcept {
    switch(site) {
    case Site::Wind:return {WindSamples,WindNull,WindLeafLoMin,WindMin,WindMax,4};
    case Site::View:return {ViewSamples,ViewNull,ViewLeafLoMin,ViewMin,ViewMax,16};
    case Site::Camera:return {CameraSamples,CameraNull,CameraLeafLoMin,CameraMin,CameraMax,3};
    }
    return {Magic,Magic,Magic,Magic,Magic,0};
}
using Record=std::array<std::uint32_t,Words>;
static_assert(Words==255 && sizeof(Record)==1020);
inline constexpr Record InitialRecord() noexcept {
    Record r{};r[Magic]=MagicValue;r[Schema]=SchemaValue;
    for(auto word:{WindLeafLoMin,WindLeafHiMin,ViewLeafLoMin,ViewLeafHiMin,CameraLeafLoMin,CameraLeafHiMin,VertexMin,InstanceMin})r[word]=~0u;
    for(unsigned k=0;k<4;++k)r[WindMin+k]=~0u;
    for(unsigned k=0;k<16;++k)r[ViewMin+k]=~0u;
    for(unsigned k=0;k<3;++k)r[CameraMin+k]=~0u;
    r[MatrixSelectedIndex]=~0u;r[MatrixLeafLoMin]=r[MatrixLeafHiMin]=~0u;
    for(unsigned k=0;k<4;++k)r[MatrixDescriptorMin+k]=~0u;
    for(unsigned k=0;k<3;++k)r[MatrixRowsMin+k]=~0u;
    for(unsigned k=0;k<12;++k)r[MatrixMin+k]=~0u;
    for(unsigned k=0;k<36;++k)r[FixedFetchMin+k]=~0u;
    for(unsigned k=0;k<19;++k)r[FixedMatrixMin+k]=~0u;
    for(unsigned k=0;k<3;++k)r[FixedUndeformedMin+k]=r[FixedDeformedMin+k]=~0u;
    for(unsigned k=0;k<4;++k)r[FixedClipMin+k]=~0u;
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
    const auto layout=Layout(site);return layout.count ? r[layout.samples] : 0;
}
inline bool Varied(const Record& r,Site site,unsigned component) noexcept {
    const auto layout=Layout(site);
    return Valid(r) && component<layout.count && Samples(r,site)>1 && r[layout.minimum+component]!=r[layout.maximum+component];
}
inline bool SelectedLeafUniform(const Record& r,Site site) noexcept {
    const auto lo=Layout(site).leaf;
    return Valid(r) && Samples(r,site)>0 && r[lo]==r[lo+1] && r[lo+2]==r[lo+3];
}
inline bool MatrixIdentityUniform(const Record& r) noexcept {
    if(!Valid(r) || !r[MatrixSamples] || r[MatrixSelectedIndex]>~0u-2u ||
       r[MatrixLeafLoMin]!=r[MatrixLeafLoMax] || r[MatrixLeafHiMin]!=r[MatrixLeafHiMax])return false;
    for(unsigned k=0;k<4;++k)if(r[MatrixDescriptorMin+k]!=r[MatrixDescriptorMax+k])return false;
    if(r[MatrixLeafLoMin]!=r[MatrixDescriptorMin] || r[MatrixLeafHiMin]!=(r[MatrixDescriptorMin+1]&0xfffu))return false;
    // Only the captured plain float4/stride16 form proves row-index identity
    // independent of tid. Bit15 is captured ignored padding, not swizzling;
    // accept that exact padding variant as well as zero padding. Reject
    // bits8..11 (included by dc_vbase), cache/address swizzling (bits30/31),
    // ADD_TID, index-stride, and all other unknown descriptor variants.
    const auto layout=r[MatrixDescriptorMin+1]&0xffffff00u;
    if((r[MatrixDescriptorMin]&3u) ||
       (layout!=0x00100000u && layout!=0x00108000u) ||
       r[MatrixDescriptorMin+3]!=0x77facu)return false;
    const std::uint64_t base=std::uint64_t(r[MatrixDescriptorMin]) |
        (std::uint64_t(r[MatrixDescriptorMin+1]&0xffu)<<32);
    if(base+(std::uint64_t(r[MatrixSelectedIndex])+3u)*16u>(1ull<<40))return false;
    for(unsigned k=0;k<3;++k)if(r[MatrixRowsMin+k]!=r[MatrixRowsMax+k] ||
        r[MatrixRowsMin+k]!=r[MatrixSelectedIndex]+k || r[MatrixRowsMin+k]>=r[MatrixDescriptorMin+2])return false;
    return true;
}
inline bool MatrixVaried(const Record& r,unsigned component) noexcept {
    return Valid(r) && component<12 && r[MatrixSamples]>1 && r[MatrixMin+component]!=r[MatrixMax+component];
}
} // namespace Gpu::Metal::WindConsumed
