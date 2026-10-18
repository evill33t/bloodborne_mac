// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "game_scene.h"
#include <memory>

namespace Gpu::Metal {
class TemporalAA {
public:
    explicit TemporalAA(void* device);
    ~TemporalAA();
    void ObserveCamera(const SceneCamera& camera);
    bool HasCamera() const;
    // Same-sized HDR scene color and scene depth; never a completed HUD frame.
    // Returns the resolved RGBA16F texture, or null to preserve original rendering.
    void* Encode(void* command,void* color,void* depth);
    void EndFrame();
    void Invalidate();
    std::uint64_t ResolvedFrames() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
