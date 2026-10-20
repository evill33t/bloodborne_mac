// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/game_scene.h"
#include "gpu/metal/texture_cache_policy.h"
#include "gpu/metal/command_validation.h"
#include <cassert>
#include <limits>
#include <cstdio>

namespace {
using namespace Gpu::Metal;
struct RetirementFixture {
    std::vector<CachedTexture> live;
    long double Bytes() const {
        long double result=0;for(const auto& e:live)result+=e.bytes;return result;
    }
    CachedTexture& Entry(std::uint64_t key) {
        const auto i=std::find_if(live.begin(),live.end(),[&](const auto& e){return e.key==key;});
        assert(i!=live.end());return *i;
    }
    void Erase(std::uint64_t key) {
        const auto i=std::find_if(live.begin(),live.end(),[&](const auto& e){return e.key==key;});
        assert(i!=live.end());live.erase(i);
    }
    std::vector<TextureRetirementTicket> Discover(std::uint64_t budget,double now) {
        std::vector<TextureRetirementTicket> result;
        for(const auto key:ExpiredTextures(live,budget,now)) {
            const auto& e=Entry(key);result.push_back({key,e.version,e.last_used});
        }
        return result;
    }
    std::vector<std::uint64_t> Drain(std::span<const TextureRetirementTicket> tickets,
            std::size_t& cursor,std::uint64_t budget,double now,std::size_t limit=32) {
        const auto result=TextureRetirementBatch(tickets,cursor,[&](std::uint64_t key)->std::optional<CachedTexture> {
            const auto i=std::find_if(live.begin(),live.end(),[&](const auto& e){return e.key==key;});
            if(i==live.end())return {};return *i;
        },budget,Bytes(),now,limit);
        for(const auto key:result)Erase(key);
        return result;
    }
};
void RetirementTests() {
    // Mass idle expiry keeps oldest-first order and completes in bounded steps.
    RetirementFixture idle;
    for(std::uint64_t key=1;key<=70;++key)idle.live.push_back({key,100,double(key)/1000,1});
    auto tickets=idle.Discover(100000,25);std::size_t cursor=0;
    assert(tickets.size()==70);
    auto batch=idle.Drain(tickets,cursor,100000,25);
    assert(batch.size()==32 && batch.front()==1 && batch.back()==32 && cursor==32);
    idle.live.push_back({1000,500,25,1}); // a new entry has no old retirement ticket
    batch=idle.Drain(tickets,cursor,100000,25.02);
    assert(batch.size()==32 && batch.front()==33 && batch.back()==64 && cursor==64);
    batch=idle.Drain(tickets,cursor,100000,25.04);
    assert(batch.size()==6 && batch.front()==65 && batch.back()==70 && cursor==70);
    assert(idle.live.size()==1 && idle.Entry(1000).bytes==500 && idle.Bytes()==500);
    assert(idle.Drain(tickets,cursor,100,27.1).empty());
    tickets=idle.Discover(100,27.1);cursor=0;
    assert((idle.Drain(tickets,cursor,100,27.1)==std::vector<std::uint64_t>{1000}) && idle.live.empty());

    // A long frame gap does not revive a ticket canceled by reuse/replacement.
    RetirementFixture changed{{{1,100,0,1},{2,100,1,1},{3,100,2,1},{4,100,3,1}}};
    tickets=changed.Discover(10000,25);cursor=0;
    changed.Entry(1).last_used=24;
    changed.Entry(2).version=2;changed.Entry(2).bytes=200;
    changed.Erase(3);changed.live.push_back({5,300,0,1});
    assert((changed.Drain(tickets,cursor,10000,50)==std::vector<std::uint64_t>{4}));
    assert(changed.live.size()==3 && changed.Bytes()==600);
    tickets=changed.Discover(10000,50);cursor=0;
    assert(changed.Drain(tickets,cursor,10000,50).size()==3 && changed.live.empty());

    // Recheck live pressure: another removal can resolve the original excess.
    RetirementFixture pressure{{{1,100,22,1},{2,100,22.1,1},{3,100,22.2,1},{4,100,24.9,1}}};
    tickets=pressure.Discover(200,25);cursor=0;
    assert(tickets.size()==2);
    assert((pressure.Drain(tickets,cursor,200,25,1)==std::vector<std::uint64_t>{1}));
    pressure.Erase(3);assert(pressure.Bytes()==200);
    assert(pressure.Drain(tickets,cursor,200,25).empty() && pressure.live.size()==2);
    pressure.live.push_back({5,100,24.9,1});
    assert(pressure.Drain(tickets,cursor,200,25).empty()); // discovery owns admission
    tickets=pressure.Discover(200,25);cursor=0;
    assert((pressure.Drain(tickets,cursor,200,25)==std::vector<std::uint64_t>{2}));
    assert(pressure.Discover(1,25).empty()); // two-second grace under pressure
    tickets=pressure.Discover(1,27.1);cursor=0;
    assert(pressure.Drain(tickets,cursor,1,27.1).size()==2 && pressure.live.empty());

    // Stale records also consume the inspection cap; a missing/reused queue
    // cannot cause a single frame to walk thousands of entries looking for32.
    RetirementFixture stale{{{65,123,0,1}}};tickets.clear();
    for(std::uint64_t key=1;key<=65;++key)tickets.push_back({key,1,0});
    cursor=0;
    assert(stale.Drain(tickets,cursor,1000,1000).empty() && cursor==32);
    assert(stale.Drain(tickets,cursor,1000,1000).empty() && cursor==64);
    assert((stale.Drain(tickets,cursor,1000,1000)==std::vector<std::uint64_t>{65}) && cursor==65);
    assert(stale.live.empty());

    RetirementFixture grace{{{1,100,25,1}}};tickets={{1,1,25}};cursor=0;
    assert(grace.Drain(tickets,cursor,0,24).empty()); // no negative-idle expiry
    cursor=0;assert(grace.Drain(tickets,cursor,0,30,0).empty() && cursor==0);
    cursor=100;assert(grace.Drain(tickets,cursor,0,30).empty() && cursor==100);
    assert(grace.live.size()==1);
    std::puts("Texture retirement: bounded oldest-first completion, live budget/grace, reactivation, replacement, missing/new entries and long gaps passed");
}
}

int main() {
    using namespace Gpu::Metal;
    std::array<float,216> constants{};
    constants[0]=3000; constants[1]=1.f/3000; constants[4]=1920; constants[5]=1080;
    for (unsigned i:{0u,5u,10u}) constants[8+i]=constants[180+i]=1;
    constants[52]=1.5; constants[57]=2; constants[62]=1; constants[63]=-.1f;
    auto c=ReadSceneCamera(constants,-540); assert(c && c->projection[1]==2);
    auto flipped=ReadSceneCamera(constants,540); assert(flipped && flipped->projection[1]==-2);
    auto previous=*c,now=*c;
    now.inverse[3]=3; now.view[3]=-3;
    const auto transform=Reprojection(previous,now);
    assert(transform[3]==3 && transform[0]==1 && transform[5]==1 && transform[10]==1);
    auto bad=constants; bad[180]=2; assert(!ReadSceneCamera(bad,-540));
    bad=constants; bad[8]=std::numeric_limits<float>::quiet_NaN(); assert(!ReadSceneCamera(bad,-540));
    bad=constants; bad[52]=0; assert(!ReadSceneCamera(bad,-540));
    bad=constants; bad[5]=std::numeric_limits<float>::infinity(); assert(!ReadSceneCamera(bad,-540));
    assert(!ReadSceneCamera(std::span(constants).first(100),-540));
    std::array<std::uint32_t,16> binary{};
    binary[0]=0xbeeb03ff; binary[1]=3; // metadata at byte 32
    std::memcpy(reinterpret_cast<char*>(binary.data())+32,"OrbShdr",7);
    const std::uint64_t id=0x9a9cf8a9;
    std::memcpy(reinterpret_cast<char*>(binary.data())+48,&id,8);
    assert(RetailShaderID(binary)==id);
    assert(!RetailShaderID(std::span(binary).first(15)));
    binary[1]=std::numeric_limits<std::uint32_t>::max(); assert(!RetailShaderID(binary));
    binary[0]=0; assert(!RetailShaderID(binary)); // unrelated trailing metadata cannot identify a synthetic shader

    assert(SceneAnisotropy(16,1,1,1,2,0)==16);
    for (unsigned point:{0u,2u}) assert(SceneAnisotropy(16,1,point,1,2,0)==1);
    assert(SceneAnisotropy(16,1,1,1,0,0)==1);
    assert(SceneAnisotropy(16,1,1,1,2,3)==1);
    assert(SceneAnisotropy(0,4,1,1,2,0)==4);
    assert(SceneAnisotropy(2,8,3,3,2,0)==2);
    const std::vector<CachedTexture> entries{{1,100,0},{2,100,18},{3,100,19.9},{4,100,20}};
    assert((ExpiredTextures(entries,1000,20)==std::vector<std::uint64_t>{1}));
    assert((ExpiredTextures(entries,200,20)==std::vector<std::uint64_t>{1,2}));
    assert((ExpiredTextures(entries,1,20)==std::vector<std::uint64_t>{1,2})); // retain current scene, even under pressure
    assert(ExpiredTextures({},1,20).empty());
    RetirementTests();

    RegisterValidation validator;
    std::vector<std::uint32_t> regs(0x10000);
    const std::array<std::uint32_t,4> packet{0xc0026900,0x100,7,8};
    validator.Prepare(packet); assert(validator.Checked());
    regs[0xa100]=7; regs[0xa101]=8; assert(!validator.Mismatch(regs));
    regs[0xa101]^=1; assert(validator.Mismatch(regs)==0xa101); // independent mutation detection
    assert(MinimumPacketBody(0x50)==6 && MinimumPacketBody(0x27)==5);
    puts("Scene camera validation/reprojection, bounded shader metadata, filtering, cache eviction and independent register checks passed");
}
