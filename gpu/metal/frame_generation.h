// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "temporal_aa.h"
#include <memory>

namespace Gpu::Metal {
// Private snapshots only. Keeping the packet alive pins its texture pool slot;
// GPU completion handlers also retain it until every queued read has finished.
struct GeneratedFrame {
    void* real=nullptr;
    void* generated=nullptr;
    void* scene=nullptr;
    void* motion=nullptr;
    void* depth=nullptr;
    std::uint64_t source_frame=0;
    double interval=0;
    void* ready_event=nullptr;
    std::uint64_t ready_value=0;
    std::shared_ptr<void> storage;
};
class FrameGeneration {
public:
    explicit FrameGeneration(void* device);
    ~FrameGeneration();
    static bool Supported(void* device);
    void Invalidate();
    bool Scene(void* command,void* depth,const SceneCamera&,std::uint64_t frame,const MotionInput*);
    bool Hud(void* command,void* color,std::uint64_t frame);
    bool NeedsHud(std::uint64_t frame) const;
    bool DisplayCopy(void* command,void* destination,void* source,void* lut,void* sampler,std::uint64_t frame);
    void Draw(void* destination,std::uint64_t revision,bool display_copy);
    std::shared_ptr<const GeneratedFrame> Finish(void* command,void* display,void* real,
                                               std::uint64_t revision,std::uint64_t frame);
    std::shared_ptr<const GeneratedFrame> RealFrame(void* command,void* real,std::uint64_t frame);
    const char* Status() const;
    std::uint64_t Generated() const;
    void Report() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
