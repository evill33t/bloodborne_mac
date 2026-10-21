// SPDX-License-Identifier: GPL-2.0-or-later
// Public metadata/denied-read tests: no Metal or private game bytes required.
#include "gpu/metal/wind_capture_contract.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace Gpu::Metal::WindCapture;
struct Guard {
    Reject blocked=Reject::None;
    unsigned stable_calls=0,fail_stable_at=0,reads=0;
    std::vector<Reject> calls;
    bool Pass(Reject r) {calls.push_back(r);return blocked!=r;}
    bool HasPredicate(){return Pass(Reject::MissingGuard);}
    bool GenerationStable(){return Pass(Reject::MapGeneration) && ++stable_calls!=fail_stable_at;}
    bool Mapping(Range){return Pass(Reject::Mapping);}
    bool CpuReady(Range){return Pass(Reject::CpuPending);}
    bool CallbacksReady(Range){return Pass(Reject::Callback);}
    bool WritersReady(Range){return Pass(Reject::Writer);}
    Reject Copy(Range r) {auto reason=Ready(r,*this);if(reason==Reject::None)++reads;return reason;}
};
int main() {
    for(const char* value:{static_cast<const char*>(nullptr),"","0","00","-1","+1"," 1","1 ","1x","x1"})
        assert(ConfiguredFrames(value)==0);
    assert(ConfiguredFrames("1")==1 && ConfiguredFrames("060")==60);
    assert(ConfiguredFrames("120")==120 && ConfiguredFrames("121")==120);
    assert(ConfiguredFrames("999999999999999999999999999999999")==120);
    assert(!NeedsMappingHooks(nullptr,nullptr) && !NeedsMappingHooks("0","0"));
    assert(!NeedsMappingHooks("0","-1") && !NeedsMappingHooks("01",nullptr));
    assert(NeedsMappingHooks("1",nullptr) && NeedsMappingHooks("0","60"));
    assert(OutsideArena({0x1000,48},0x2000,0x1000));
    assert(OutsideArena({0x3000,48},0x2000,0x1000));
    assert(OutsideArena({0x1fd0,48},0x2000,0x1000));
    assert(!OutsideArena({0x1fd4,48},0x2000,0x1000));
    assert(!OutsideArena({0x2000,48},0x2000,0x1000));
    assert(!OutsideArena({0x2ff0,48},0x2000,0x1000));
    assert(!OutsideArena({~U64(0)-16,48},0x2000,0x1000));
    assert(!OutsideArena({0x1000,48},~U64(0)-16,48));
    assert(OutsideArena({0x1000,48},0,0x1000));
    assert(StableGeneration(2,2));
    assert(!StableGeneration(3,3) && !StableGeneration(2,4) && !StableGeneration(2,3));
    Range range;
    assert(Decode({0x47568b20,0x80,656,0x12345678},656,range));
    assert(range.address==0x8047568b20ull && range.bytes==656);
    assert(Decode({0x47567a20,0x100080,54,0xfedcba98},864,range));
    assert(range.address==0x8047567a20ull && range.bytes==864);
    assert(!Decode({0x47568b20,0x80,864,0},656,range) && !range.bytes);
    assert(!Decode({0x47568b21,0x80,656,0},656,range));
    assert(!Decode({0xfffffff0,0xff,656,0},656,range));
    assert(!Decode({0x47568b20,0x180,656,0},656,range));
    assert(!Decode({0x47568b20,0x80,0,0},656,range));
    assert(!Decode({0,0,656,0},656,range));
    range={0x8047568b20ull,656};
    Guard accepted;assert(accepted.Copy(range)==Reject::None && accepted.reads==1);
    assert(accepted.calls.size()==8 && accepted.stable_calls==3);
    for(unsigned r=1;r<unsigned(Reject::Count);++r) {
        Guard guard;guard.blocked=Reject(r);
        assert(guard.Copy(range)==Reject(r) && !guard.reads);
        assert(guard.calls.back()==Reject(r));
    }
    for(unsigned at=1;at<=3;++at) {
        Guard guard;guard.fail_stable_at=at;
        assert(guard.Copy(range)==Reject::MapGeneration && !guard.reads);
    }
    std::puts("Wind capture contract: exact wind/view extents; every denied guard prevents payload read; mapping races rejected.");
}
