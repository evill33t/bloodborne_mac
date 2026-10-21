#pragma once

// Metal backend for the guest GPU: translates guest shaders to MSL, keeps
// render targets and textures as Metal textures, and records draws.

#include <memory>
#include <span>
#include <vector>

#include "gpu/renderer.h"

namespace Gpu::Metal {

class Device;

class MetalRenderer : public Gpu::Renderer {
public:
    explicit MetalRenderer(Device& device);
    ~MetalRenderer() override;

    void Draw(const Liverpool::RegFile& regs, const DrawCall& draw) override;
    void Dispatch(const Liverpool::RegFile& regs, std::uint32_t x, std::uint32_t y,
                  std::uint32_t z) override;
    bool DrawIndirect(const Liverpool::RegFile&,const DrawCall&,GuestAddr,
        const std::function<bool(GuestAddr,std::size_t)>& ready={}) override;
    bool DispatchIndirect(const Liverpool::RegFile&,GuestAddr,
        const std::function<bool(GuestAddr,std::size_t)>& ready={}) override;
    void SetCpuReadGuard(std::function<bool(GuestAddr,std::size_t)>) override;
    void CpuWrite(GuestAddr,const void*,std::size_t,bool constants) override;
    // Runtime mapping mutations hold this guard from BEFORE the memory write
    // lock until AFTER it unlocks. Failure leaves the renderer unlocked and
    // requires the runtime to leave the mapping unchanged. Same-thread nested
    // snapshot-arena allocation is allowed; End must run on the Begin thread.
    bool BeginGuestMemoryMutation();
    void EndGuestMemoryMutation();
    void OnSubmit() override;
    void WaitIdle() override;
    bool WaitIdleUntil(std::chrono::steady_clock::time_point,std::string& error) override;
    struct CheckpointChunk {
        std::uint64_t id=0,size=0;
        std::function<bool(std::uint64_t,std::span<std::uint8_t>,std::string&)> read;
    };
    class CheckpointCapture {
    public:
        ~CheckpointCapture();
        const std::vector<CheckpointChunk>& Chunks() const;
    private:
        friend class MetalRenderer;
        struct State;
        explicit CheckpointCapture(std::unique_ptr<State>);
        std::unique_ptr<State> state;
    };
    // CPU writers and command-processor admission must be stopped first. The
    // capture retains the registry lock until its streaming sources are read
    // and destroyed. Use only on the coordinator worker, never the UI thread.
    std::unique_ptr<CheckpointCapture> CaptureCheckpoint(std::chrono::steady_clock::time_point,std::string& error);
    // Only on a fresh renderer, after guest memory has been rebuilt. Sources
    // must come from an already verified archive. Publishes targets together
    // after all transfers succeed; discard the fresh process on failure.
    bool RestoreCheckpoint(std::span<const CheckpointChunk>,std::chrono::steady_clock::time_point,std::string& error);
    void Signal(std::function<void()> done) override;
    void NotePendingCpuWrite(GuestAddr address,std::size_t bytes) override;
    void SignalCpuWrite(GuestAddr address,std::size_t bytes,std::function<void()> done) override;
    void SignalNoDirectMemoryWrite(std::function<void()> done) override;
    void SignalCpuWriteOnly(GuestAddr address,std::size_t bytes,std::function<void()> done) override;
    void NoteConstDump(std::uint64_t addr, std::uint32_t bytes) override;
    void Flip(const FlipInfo& flip) override;
    void OnContextRegs(const std::uint32_t* body, std::uint32_t body_dwords,std::size_t available_words) override;
    bool DmaData(std::uint32_t dst_sel, std::uint64_t dst, std::uint32_t src_sel, std::uint64_t src,
                 std::uint32_t bytes) override;
    std::uint8_t* Gds() override;

    // Main thread: the last flipped frame as id<MTLTexture> (void*), or null.
    // It stays valid until two more flips have happened.
    void* LatestFrame() const;
    std::uint64_t FrameCount() const;
    // Diagnostic counter: full-depth copies used by emulated depth bounds.
    std::uint64_t DepthBoundsCopyCount() const;
    // Diagnostic counter: new transient upload buffers, excluding safe reuse.
    std::uint64_t UploadBufferAllocationCount() const;
    std::uint64_t NativeIndirectDrawCount() const;
    std::uint64_t NativeIndirectDispatchCount() const;
    std::uint64_t IndirectCpuWriteWaitCount() const;
    // Read-only opt-in diagnostic: DirectRootRead::Reject index, zero if invalid.
    std::uint64_t DirectSmrdRootProofCount(std::uint32_t result) const;
    // Existing address-only VS proof counters; zero for an invalid result index.
    std::uint64_t DirectVertexRootProofCount(std::uint32_t result) const;
    std::uint64_t TextureGpuSnapshotCount() const;
    std::uint64_t TextureCoherenceWaitCount() const;
    std::uint64_t TextureCoherenceRefreshCount() const;
    // Actual synchronous worker dispatches; first-chunk serial exits excluded.
    std::uint64_t ParallelTextureHashCheckCount() const;
    // Called on the GPU recording thread after committing a flip. The host
    // queues its presentation before returning, preserving shared-queue order.
    void SetFrameReadyCallback(std::function<void(void*)> callback);
    // Main thread; applied by the GPU thread without changing guest state.
    void SetGraphicsOptions(int anisotropy,bool temporal_aa);
    bool TemporalAAActive() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Gpu::Metal
