#pragma once

// Backend-agnostic boundary between the guest GPU frontend (PM4 command
// processor for the PS4 "Liverpool" GPU) and a host graphics API.
//
// The frontend decodes command buffers the game submits through
// sceGnmSubmitCommandBuffers / sceGnmDingDong, tracks register state, and
// calls into this interface only for work that touches the host GPU.
// Backend: direct Metal on macOS (src/gpu/metal).

#include <cstdint>
#include <cstddef>
#include <functional>
#include <chrono>
#include <string>
#include <memory>
#include <cstring>
#include <utility>
#include "gpu/metal/gpu_execution.h"

namespace Gpu {

namespace Liverpool {
class RegFile;
}

using GuestAddr = std::uint64_t;

// Per-draw state that comes from draw packets rather than registers.
struct DrawCall {
    bool indexed = false;
    std::uint32_t count = 0;         // indices or vertices
    std::uint32_t instances = 1;
    GuestAddr index_base = 0;        // INDEX_BASE packet
    std::uint32_t index_offset = 0;  // in indices (DRAW_INDEX_OFFSET_2)
    std::uint32_t index_size = 2;    // bytes per index: 2 or 4
    std::uint32_t first_vertex = 0;  // non-indexed draws (indirect)
};

// What sceVideoOutSubmitFlip asks to show.
struct FlipInfo {
    GuestAddr address = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t pitch = 0; // pixels
    std::uint32_t pixel_format = 0;
    std::uint32_t tiling_mode = 0;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    std::shared_ptr<ExecutionState> Execution() const { return execution; }

    // Draws using the current guest register state.
    virtual void Draw(const Liverpool::RegFile& regs, const DrawCall& draw) = 0;
    virtual void Dispatch(const Liverpool::RegFile& regs, std::uint32_t x, std::uint32_t y,
                          std::uint32_t z) = 0;

    // Return false without recording work when the backend cannot preserve
    // this packet's semantics. The CP then drains and snapshots as before.
    // `ready` is checked under the backend's admission lock. A queued CPU
    // fence-store must not race between this check and GPU command admission.
    // Check every CPU-resolved descriptor/payload span, not just arguments.
    virtual bool DrawIndirect(const Liverpool::RegFile&,const DrawCall&,GuestAddr,
        const std::function<bool(GuestAddr,std::size_t)>& ready={}) {(void)ready;return false;}
    virtual bool DispatchIndirect(const Liverpool::RegFile&,GuestAddr,
        const std::function<bool(GuestAddr,std::size_t)>& ready={}) {(void)ready;return false;}
    // Frontend-owned deferred CPU stores also protect descriptor/payload reads
    // during ordinary producer recording. Invoke only under admission locking;
    // the frontend clears this callback after joining its recording threads.
    virtual void SetCpuReadGuard(std::function<bool(GuestAddr,std::size_t)>) {}
    // A deferred host fence-store is announced before its callback can run.
    // Backends with CPU texture caches pair that marker and Signal atomically.
    virtual void NotePendingCpuWrite(GuestAddr,std::size_t) {}
    virtual void SignalCpuWrite(GuestAddr,std::size_t,std::function<void()> done) {Signal(std::move(done));}
    // Trusted narrower effects, used only by audited host closures. The first
    // performs no writes to the exposed direct-memory pool or its aliases and
    // invokes no arbitrary/guest code. The second confines all such writes to
    // exactly the declared destination. Unknown callbacks keep Signal (or
    // SignalCpuWrite for journaling). Neither changes execution/FIFO ordering.
    virtual void SignalNoDirectMemoryWrite(std::function<void()> done) {Signal(std::move(done));}
    virtual void SignalCpuWriteOnly(GuestAddr address,std::size_t bytes,std::function<void()> done) {
        SignalCpuWrite(address,bytes,std::move(done));
    }

    // CP-side writes must not pass an earlier GPU indirect read of the same
    // bytes. Backends serialize admission and the write together; this also
    // keeps CE/WRITE_DATA descriptor snapshot bookkeeping in the same lock.
    virtual void CpuWrite(GuestAddr address,const void* source,std::size_t bytes,bool constants) {
        std::memcpy(reinterpret_cast<void*>(address),source,bytes);
        if(constants) NoteConstDump(address,static_cast<std::uint32_t>(bytes));
    }

    // End of one guest submission: hand recorded work to the host GPU.
    virtual void OnSubmit() = 0;

    // Blocks until all recorded work has executed (the CP then touches
    // memory that work uses).
    virtual void WaitIdle() = 0;
    // Checkpoint worker only; submission has been stopped. Never substitute
    // an unbounded WaitIdle when the backend cannot provide a timeout.
    virtual bool WaitIdleUntil(std::chrono::steady_clock::time_point,std::string& error) {
        error="Renderer does not support bounded checkpoint draining.";return false;
    }

    // Runs `done` on another thread once all work recorded so far has
    // executed; calls complete in order. Fences, labels and their interrupts
    // go through this, so the guest sees GPU progress without the CP waiting.
    virtual void Signal(std::function<void()> done) = 0;

    // The constant engine wrote `bytes` of resource table at guest `addr`.
    // The guest reuses that memory quickly, so the renderer snapshots dumps
    // that draws point at instead of the CP waiting for the GPU.
    virtual void NoteConstDump(std::uint64_t addr, std::uint32_t bytes) = 0;

    // Called on the GPU thread once the frame's command buffers are done.
    virtual void Flip(const FlipInfo& flip) = 0;

    // A SET_CONTEXT_REG packet body (`body[0]` = register offset), read in
    // place in the command buffer: the Gnm driver puts render target size
    // hints in the NOP packet right after it.
    virtual void OnContextRegs(const std::uint32_t*, std::uint32_t, std::size_t) {}

    // CP DMA (DMA_DATA). sel: 0 memory, 1 GDS, 2 immediate (`src` is the
    // value; source only). Return true if the backend did the copy on the
    // GPU; false lets the CP do it on the CPU.
    virtual bool DmaData(std::uint32_t, std::uint64_t, std::uint32_t,
                         std::uint64_t, std::uint32_t) {
        return false;
    }

    // The 64 KB global data share, shared by shaders and CP DMA (call WaitIdle
    // before reading what shaders wrote).
    virtual std::uint8_t* Gds() = 0;
private:
    std::shared_ptr<ExecutionState> execution=std::make_shared<ExecutionState>();
};

} // namespace Gpu
