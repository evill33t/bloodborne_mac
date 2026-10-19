// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "game_scene.h"
#include <memory>

namespace Gpu::Metal {
struct MotionInput {
    void* texture=nullptr;
    std::uint64_t current_frame=0,previous_frame=0;
    // Previous UV = current UV + (native RG - .5) * uv_scale.
    std::array<float,2> uv_scale{};
};
class TemporalAA {
public:
    explicit TemporalAA(void* device);
    ~TemporalAA();
    void ObserveCamera(const SceneCamera& camera);
    bool HasCamera() const;
    // Same-sized HDR scene color and scene depth; never a completed HUD frame.
    // Returns the resolved RGBA16F texture, or null to preserve original rendering.
    void* Encode(void* command,void* color,void* depth,std::uint64_t frame=0,
                 const MotionInput* motion=nullptr);
    void EndFrame();
    void Invalidate();
    std::uint64_t ResolvedFrames() const;
    // Counts admitted frame inputs; individual pixels can still use the camera.
    std::uint64_t NativeMotionFrames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
