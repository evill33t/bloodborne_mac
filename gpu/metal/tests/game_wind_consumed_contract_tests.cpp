// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/wind_consumed_contract.h"
#include <array>
#include <cassert>
#include <cstdio>
using namespace Gpu::Metal::WindConsumed;
int main() {
    assert(ConfiguredFrames("1")==1 && ConfiguredFrames("120")==120);
    assert(ConfiguredFrames("999999999999999999999")==120);
    for(const char* value:std::array<const char*,8>{nullptr,"","0","+1","-1"," 1","1 ","1x"})assert(!ConfiguredFrames(value));
    Window window(2);assert(!window.Request(100,false));assert(window.Request(100,true));
    assert(window.Reserve(100) && window.Reserve(100) && !window.Reserve(100));
    assert(window.Request(101,false));assert(window.Reserve(101) && window.Reserve(101));
    assert(!window.Request(102,true) && !window.Reserve(102) && window.total==4);
    Window empty;assert(!empty.Request(1,true) && !empty.Reserve(1));
    Window compile(120);assert(compile.Request(200,true));
    assert(compile.Request(201,false) && !compile.total && compile.first==200);
    assert(!compile.Request(320,true) && !compile.Request(199,true));
    auto r=InitialRecord();assert(SchemaValue==5 && r.size()==255 && sizeof(r)==1020);
    for(const auto counter:{FixedFetchSamples,FixedMatrixSamples,FixedUndeformedSamples,FixedDeformedSamples,FixedClipSamples})assert(!r[counter]);
    for(const auto& [minimum,maximum,count]:{std::array<unsigned,3>{FixedFetchMin,FixedFetchMax,36},
            std::array<unsigned,3>{FixedMatrixMin,FixedMatrixMax,19},
            std::array<unsigned,3>{FixedUndeformedMin,FixedUndeformedMax,3},
            std::array<unsigned,3>{FixedDeformedMin,FixedDeformedMax,3},
            std::array<unsigned,3>{FixedClipMin,FixedClipMax,4}}) {
        assert(minimum+count==maximum && maximum+count<=Words);
        for(unsigned k=0;k<count;++k)assert(r[minimum+k]==~0u && !r[maximum+k]);
    }
    assert(!MatrixIdentityUniform(r) && !MatrixVaried(r,0) && r[MatrixSelectedIndex]==~0u);
    for(auto site:{Site::Wind,Site::View,Site::Camera})assert(!Varied(r,site,0) && !SelectedLeafUniform(r,site));
    r[WindSamples]=r[ViewSamples]=r[CameraSamples]=2;
    for(auto start:{WindLeafLoMin,ViewLeafLoMin,CameraLeafLoMin}) {
        r[start]=r[start+1]=0x12345678;r[start+2]=r[start+3]=0x80;
    }
    for(unsigned k=0;k<4;++k)r[WindMin+k]=r[WindMax+k]=0x7fc12345u+k;
    for(unsigned k=0;k<16;++k)r[ViewMin+k]=r[ViewMax+k]=0x80000000u+k;
    for(unsigned k=0;k<3;++k)r[CameraMin+k]=r[CameraMax+k]=0xffc12345u+k;
    assert(SelectedLeafUniform(r,Site::Wind) && SelectedLeafUniform(r,Site::View));
    assert(!Varied(r,Site::Wind,3) && !Varied(r,Site::View,15));
    ++r[WindMax+3];++r[ViewMax+15];assert(Varied(r,Site::Wind,3) && Varied(r,Site::View,15));
    assert(!Varied(r,Site::Wind,4) && !Varied(r,Site::View,16));
    assert(SelectedLeafUniform(r,Site::Camera) && !Varied(r,Site::Camera,2));
    ++r[CameraMax+1];assert(Varied(r,Site::Camera,1) && !Varied(r,Site::Camera,0) && !Varied(r,Site::Camera,3));
    assert(Samples(r,static_cast<Site>(99))==0 && !SelectedLeafUniform(r,static_cast<Site>(99)));
    r[MatrixCandidates]=4;r[MatrixSamples]=2;r[MatrixOtherIndices]=2;r[MatrixSelectedIndex]=3;
    r[MatrixLeafLoMin]=r[MatrixLeafLoMax]=0x12340000;r[MatrixLeafHiMin]=r[MatrixLeafHiMax]=0x80;
    const std::array<std::uint32_t,4> descriptor{0x12340000,0x108080,6,0x77fac};
    for(unsigned k=0;k<4;++k)r[MatrixDescriptorMin+k]=r[MatrixDescriptorMax+k]=descriptor[k];
    for(unsigned k=0;k<3;++k)r[MatrixRowsMin+k]=r[MatrixRowsMax+k]=3+k;
    for(unsigned k=0;k<12;++k)r[MatrixMin+k]=r[MatrixMax+k]=0x7fc12300+k;
    assert(MatrixIdentityUniform(r) && !MatrixVaried(r,11));
    ++r[MatrixMax+11];assert(MatrixVaried(r,11) && MatrixIdentityUniform(r) && !MatrixVaried(r,12));
    ++r[MatrixDescriptorMax+2];assert(!MatrixIdentityUniform(r));--r[MatrixDescriptorMax+2];
    ++r[MatrixRowsMax];assert(!MatrixIdentityUniform(r));--r[MatrixRowsMax];
    const auto matrix=r;
    r[MatrixDescriptorMin+1]=r[MatrixDescriptorMax+1]=0x100080u;
    assert(MatrixIdentityUniform(r)); // zero padding and captured bit15 both work
    for(const auto word1:std::array<std::uint32_t,10>{0x108080u|(1u<<31),0x108080u|(1u<<30),
            0x108080u|(1u<<8),0x108080u|(1u<<9),0x108080u|(1u<<10),0x108080u|(1u<<11),
            0x108080u|(1u<<12),0x108080u|(1u<<14),0x200080u,0x80u}) {
        r=matrix;r[MatrixDescriptorMin+1]=r[MatrixDescriptorMax+1]=word1;
        r[MatrixLeafHiMin]=r[MatrixLeafHiMax]=word1&0xfffu;
        assert(!MatrixIdentityUniform(r));
    }
    for(const auto bit:std::array<unsigned,5>{0,19,21,23,30}) {
        r=matrix;r[MatrixDescriptorMin+3]^=1u<<bit;r[MatrixDescriptorMax+3]=r[MatrixDescriptorMin+3];
        assert(!MatrixIdentityUniform(r));
    }
    r=matrix;r[MatrixDescriptorMin]=r[MatrixDescriptorMax]=r[MatrixLeafLoMin]=r[MatrixLeafLoMax]=0x12340001;
    assert(!MatrixIdentityUniform(r));
    r=matrix;r[MatrixDescriptorMin]=r[MatrixDescriptorMax]=r[MatrixLeafLoMin]=r[MatrixLeafLoMax]=0xfffffff0u;
    r[MatrixDescriptorMin+1]=r[MatrixDescriptorMax+1]=0x1000ffu;r[MatrixLeafHiMin]=r[MatrixLeafHiMax]=0xffu;
    assert(!MatrixIdentityUniform(r));
    r=matrix;
    r[CounterOverflow]=1;assert(!Varied(r,Site::Wind,3) && !SelectedLeafUniform(r,Site::View));
    assert(!Varied(r,Site::Camera,1) && !SelectedLeafUniform(r,Site::Camera));
    assert(!MatrixIdentityUniform(r) && !MatrixVaried(r,11));
    r[CounterOverflow]=0;r[Schema]=3;assert(!Valid(r));
    std::puts("Consumed wind contract: bounded window/buffers, no-sample/overflow/schema guards and raw-bit ranges passed.");
}
