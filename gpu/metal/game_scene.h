// SPDX-License-Identifier: GPL-2.0-or-later
// Bloodborne scene-constant layout and reprojection from deadinside28/bloodborne_pc.
#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace Gpu::Metal {
struct SceneCamera {
    std::array<float,12> view{}, inverse{};
    std::array<float,4> projection{};
    unsigned width=0,height=0;
};
inline std::array<float,12> Reprojection(const SceneCamera& previous,const SceneCamera& current) {
    std::array<float,12> out{};
    for (int row=0;row<3;++row) for (int col=0;col<4;++col) {
        float v=col==3 ? previous.view[row*4+3] : 0;
        for (int k=0;k<3;++k) v+=previous.view[row*4+k]*current.inverse[k*4+col];
        out[row*4+col]=v;
    }
    return out;
}
inline std::optional<SceneCamera> ReadSceneCamera(std::span<const float> data,float viewport_y) {
    if (data.size()!=216 || data[0]!=3000 || !std::isfinite(data[1]) ||
        std::abs(data[0]*data[1]-1)>1e-3f ||
        !std::isfinite(data[4]) || !std::isfinite(data[5]) ||
        data[4]<64 || data[5]<64 || data[4]>8192 || data[5]>8192) return {};
    SceneCamera camera;
    std::copy_n(data.data()+8,12,camera.view.begin());
    std::copy_n(data.data()+180,12,camera.inverse.begin());
    camera.projection={data[52],data[57],data[62],data[63]};
    for (float v:camera.view) if (!std::isfinite(v)) return {};
    for (float v:camera.inverse) if (!std::isfinite(v)) return {};
    for (float v:camera.projection) if (!std::isfinite(v)) return {};
    if (std::abs(camera.projection[0])<1e-4 || std::abs(camera.projection[1])<1e-4 ||
        camera.projection[2]<0.5f || camera.projection[2]>1.1f || camera.projection[3]>=0) return {};
    const auto identity=Reprojection(camera,camera);
    for (unsigned i=0;i<12;++i) if (std::abs(identity[i]-(i==0 || i==5 || i==10 ? 1.f : 0.f))>0.02f) return {};
    // The G-buffer's actual viewport determines texture Y, not a guessed sign
    // from a projection matrix that the game can reuse for another pass.
    if (viewport_y && std::isfinite(viewport_y))
        camera.projection[1]=std::copysign(std::abs(camera.projection[1]),-viewport_y);
    camera.width=unsigned(data[4]); camera.height=unsigned(data[5]);
    return camera;
}
// Bounded search over mapped bytes only. Retail shader IDs come from OrbShdr,
// not our MSL cache hash; synthetic/unknown shaders return no ID.
inline std::optional<std::uint64_t> RetailShaderID(std::span<const std::uint32_t> code) {
    const auto bytes=std::as_bytes(code);
    constexpr char signature[]="OrbShdr";
    if (code.size()<2 || code[0]!=0xbeeb03ffu) return {};
    const std::uint64_t offset=(std::uint64_t(code[1])+1)*8;
    if (offset>bytes.size() || bytes.size()-offset<32 ||
        std::memcmp(bytes.data()+offset,signature,7)) return {};
    std::uint64_t id=0; std::memcpy(&id,bytes.data()+offset+16,8);
    return id;
}
}
