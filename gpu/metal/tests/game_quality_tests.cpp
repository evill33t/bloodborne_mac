// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpu/metal/game_scene.h"
#include "gpu/metal/texture_cache_policy.h"
#include "gpu/metal/command_validation.h"
#include <cassert>
#include <limits>
#include <cstdio>

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

    RegisterValidation validator;
    std::vector<std::uint32_t> regs(0x10000);
    const std::array<std::uint32_t,4> packet{0xc0026900,0x100,7,8};
    validator.Prepare(packet); assert(validator.Checked());
    regs[0xa100]=7; regs[0xa101]=8; assert(!validator.Mismatch(regs));
    regs[0xa101]^=1; assert(validator.Mismatch(regs)==0xa101); // independent mutation detection
    assert(MinimumPacketBody(0x50)==6 && MinimumPacketBody(0x27)==5);
    puts("Scene camera validation/reprojection, bounded shader metadata, filtering, cache eviction and independent register checks passed");
}
