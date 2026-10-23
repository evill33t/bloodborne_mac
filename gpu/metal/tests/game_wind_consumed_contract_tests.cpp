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
    auto r=InitialRecord();assert(r.size()==60 && sizeof(r)==240);
    for(auto site:{Site::Wind,Site::View})assert(!Varied(r,site,0) && !SelectedLeafUniform(r,site));
    r[WindSamples]=r[ViewSamples]=2;
    for(auto start:{WindLeafLoMin,ViewLeafLoMin}) {
        r[start]=r[start+1]=0x12345678;r[start+2]=r[start+3]=0x80;
    }
    for(unsigned k=0;k<4;++k)r[WindMin+k]=r[WindMax+k]=0x7fc12345u+k;
    for(unsigned k=0;k<16;++k)r[ViewMin+k]=r[ViewMax+k]=0x80000000u+k;
    assert(SelectedLeafUniform(r,Site::Wind) && SelectedLeafUniform(r,Site::View));
    assert(!Varied(r,Site::Wind,3) && !Varied(r,Site::View,15));
    ++r[WindMax+3];++r[ViewMax+15];assert(Varied(r,Site::Wind,3) && Varied(r,Site::View,15));
    assert(!Varied(r,Site::Wind,4) && !Varied(r,Site::View,16));
    r[CounterOverflow]=1;assert(!Varied(r,Site::Wind,3) && !SelectedLeafUniform(r,Site::View));
    r[CounterOverflow]=0;r[Schema]=1;assert(!Valid(r));
    std::puts("Consumed wind contract: bounded window/buffers, no-sample/overflow/schema guards and raw-bit ranges passed.");
}
