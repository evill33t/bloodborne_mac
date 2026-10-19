#pragma once

// Presents finished frames to a CAMetalLayer. Up to kFramesInFlight frames
// are queued on the GPU so the CPU never waits on the GPU unless it is a
// full three frames ahead.

#include <memory>
#include <cstdint>
#include <vector>
#include <functional>

namespace Gpu::Metal {

class Device;

struct PresenterConfig {
    bool vsync = true;
    float refresh_rate = 60;
};

struct PresentedFrame {
    std::uint64_t sequence;
    double submitted_time;
    double presented_time; // Metal reports zero when the drawable was skipped.
};

class Presenter {
public:
    static constexpr int kFramesInFlight = 3;

    // `layer` is a CAMetalLayer* (e.g. from SDL_Metal_GetLayer).
    Presenter(Device& device, void* layer, const PresenterConfig& config);
    ~Presenter();

    Presenter(const Presenter&) = delete;
    Presenter& operator=(const Presenter&) = delete;

    void Resize(int width_px, int height_px);
    void SetVsync(bool enabled);
    void SetRefreshRate(float hz);
    void SetUpscaling(bool enabled,float sharpness);
    bool SupportsMetalFX() const;
    const char* UpscalingStatus() const;
    std::uint64_t PresentedFrameCount() const;
    std::vector<PresentedFrame> TakePresentations();

    // Pending storage stays owned by the producer until queued() is called
    // after the presentation command is enqueued on the shared Metal queue.
    // Callbacks must outlive this presenter. UI setters remain nonblocking.
    void StartFrameLoop(std::function<void*()> pending,std::function<void()> queued);
    bool HasFrameLoop() const;
    std::uint64_t SkippedFrameCount() const;
    std::uint64_t DisplayLinkUpdateCount() const;
    std::uint64_t SubmittedFrameCount() const;

    // Placeholder frame: clears the drawable.
    void PresentClear(float r, float g, float b);

    // Draws a finished frame (id<MTLTexture> as void*) scaled to the drawable.
    void PresentTexture(void* texture_handle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Gpu::Metal
