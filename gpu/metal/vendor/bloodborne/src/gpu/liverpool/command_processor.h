#pragma once

// The guest GPU front end: executes PM4 command streams the game submits.
//
// Submissions are queued and run on a dedicated GPU thread, like the real
// command processor, so the game keeps running while "the GPU" works. Sync
// packets (fences, waits, memory writes, interrupts) are executed here;
// draws and dispatches are handed to a Gpu::Renderer.

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "common/types.h"
#include "gpu/liverpool/pm4.h"
#include "gpu/liverpool/registers.h"
#include "gpu/metal/gpu_execution.h"
#include "gpu/metal/cp_fence_map_cache.h"

namespace Gpu {
class Renderer;
}

namespace Gpu::Liverpool {

class CommandProcessor {
public:
    // Interrupt sources the game can listen to (sceGnmAddEqEvent ids).
    static constexpr u32 kIrqGfxEop = 0x40;

    struct Stats {
        std::atomic<u64> packets{0};
        std::atomic<u64> draws{0};
        std::atomic<u64> dispatches{0};
        std::atomic<u64> submits{0};
        std::atomic<u64> validated_register_packets{0}, register_mismatches{0}, malformed_packets{0};
        std::atomic<u64> label_waits{0},label_parked_us{0},label_timeouts{0};
        std::atomic<u64> native_indirect_draws{0},native_indirect_dispatches{0},indirect_fallbacks{0};
    };

    CommandProcessor();
    ~CommandProcessor();

    // One draw command buffer and the constant command buffer that feeds it
    // (run by the constant engine, CE, alongside the draw engine, DE).
    struct CmdBuffers {
        std::span<const u32> dcb;
        std::span<const u32> ccb;
    };

    // Graphics ring. `after` runs on the GPU thread once the buffers are done
    // (used for flips).
    void SubmitGraphics(std::vector<CmdBuffers> bufs, std::function<void()> after = {});
    // Compute ring `queue` (1-based Gnm vqid).
    void SubmitCompute(u32 queue, std::span<const u32> acb);

    // Blocks until every queued submission has executed.
    void WaitIdle();
    struct CheckpointRing {
        u32 index=0;
        u64 indirect_base=0;
        std::vector<u32> registers;
        bool operator==(const CheckpointRing&) const=default;
    };
    struct CheckpointState {
        u32 version=1,index_size=2,num_instances=1;
        u64 index_base=0,ce_counter=0,de_counter=0;
        std::vector<u32> registers,ce_ram;
        std::vector<CheckpointRing> rings;
        bool operator==(const CheckpointState&) const=default;
    };
    // Guest threads have already parked. Stops new submissions, drains rings,
    // host GPU work and callbacks with one deadline, then exports logical state.
    // Failure reopens admission. Success requires ResumeAfterCheckpoint on all
    // paths; the UI keeps servicing presentation while this worker waits.
    bool PauseForCheckpoint(CheckpointState&,std::chrono::steady_clock::time_point,std::string& error);
    void ResumeAfterCheckpoint();
    // Recreate the frontend before any guest submission. Remains paused until
    // the rest of the restored GPU/runtime resources have been activated.
    bool RestoreCheckpoint(const CheckpointState&,std::string& error);

    void SetInterruptHandler(std::function<void(u32 irq)> handler) { on_irq = std::move(handler); }
    void SetRenderer(Gpu::Renderer* r);
    Gpu::Renderer* GetRenderer() const { return renderer; }
    std::shared_ptr<Gpu::ExecutionState> Execution() const { return execution; }
    // Bounded CPU-side label wait; never infers success from a queued write.
    bool WaitLabel(u64 address,u64 reference,u32 bytes,u32 compare,u64 mask,
        std::chrono::steady_clock::time_point deadline,bool fatal_timeout=false);

    const Stats& GetStats() const { return stats; }
    bool FenceMapCacheEnabled() const {return fence_map_cache.Enabled();}
    bool FenceDestinationIndexEnabled() const {return fence_map_cache.DestinationIndexEnabled();}
    Metal::CpFenceMapCache::Counters FenceMapCacheStats();
    const RegFile& Regs() const { return regs; }

private:
    // Ephemeral diagnostics only. These never enter checkpoint state or replace
    // the guest command spans used for execution.
    struct AuditJob;
    struct AuditState;
    struct Job {
        std::vector<CmdBuffers> buffers;
        std::function<void()> after;
        std::shared_ptr<AuditJob> audit;
    };
    std::unique_ptr<AuditState> audit;
    static thread_local AuditJob* t_audit_job;
    static thread_local u32 t_audit_buffer;
    void InitAudit();
    std::shared_ptr<AuditJob> AuditSubmission(const std::vector<CmdBuffers>&);
    void AuditConsume(AuditJob&,const CmdBuffers&,u32 buffer);
    void AuditFinish(AuditJob&);
    void AuditPacket(std::span<const u32>,u64 offset,int depth,bool constant);
    void AuditDraw(bool indexed,u32 count,u32 offset,u32 first,u32 instances);

    // Constant engine: fills its 48 KB RAM from the CCB and dumps it to
    // memory (descriptor tables) for the DE. It runs ahead of the DE, synced
    // through two counters; here it runs on demand whenever the DE waits.
    struct ConstantEngine {
        std::vector<std::span<const u32>> stack; // CCB + IB_CONST nesting
        u64 ce_counter = 0;
        u64 de_counter = 0;
        std::vector<u32> ram = std::vector<u32>(0xC000 / 4, 0);
    };

    // One hardware ring: the graphics ring or a compute queue. Rings run in
    // parallel, as on the GPU, so a wait on one can be satisfied by another.
    struct Ring {
        u32 index = 0; // 0 = graphics, n = compute vqid n
        std::mutex lock;
        std::condition_variable cv;
        std::condition_variable idle_cv;
        std::deque<Job> jobs;
        bool busy = false;
        RegFile regs;           // compute rings: their own SH state
        u64 indirect_base = 0;  // SET_BASE
        u32 indirect_selector = 0; // optimization admission only; unknown after restore
        std::jthread thread;
    };

    Ring& GetRing(u32 index);
    void Push(u32 ring, Job job);
    void Run(Ring& ring, std::stop_token stop);
    void Execute(std::span<const u32> cmds, bool compute, int depth);
    void Interrupt(u32 irq);
    void SetRegs(u32 index, const u32* values, u32 count);
    void Draw(bool indexed, u32 count, u32 index_offset, u32 first_vertex, u32 instances,bool indirect=false);
    RegFile& CurRegs();
    // Ring and registers of the ring the calling thread executes.
    static thread_local Ring* t_ring;
    static thread_local Liverpool::RegFile* t_regs;
    const u32* IndirectArgs(u32 offset, u32 dwords);
    bool SyncRenderer(u32 reason=0);
    // Runs `fn` once the renderer has executed everything recorded so far.
    void AfterGpu(std::function<void()> fn,u64 write_address=0,std::size_t write_bytes=0,bool write_only=false);
    // AfterGpu for a fence/label write (`data_sel` as in EVENT_WRITE_EOP),
    // remembered as pending until it lands; then `irq` if non-zero.
    void FenceWrite(u32 data_sel, u64 addr, u32 lo, u32 hi, u32 irq);
    // Value a recorded but not yet executed fence write will put at `addr`.
    bool PendingFence(u64 addr, u32* value);
    bool PendingFenceOverlaps(u64 addr,u64 bytes);
    void Fail(const std::string& reason);
    void ClearState();
    // Runs the CE until `done()` holds, it blocks on the DE, or its CCB ends.
    void RunConstantEngine(const std::function<bool()>& done);

    RegFile regs;
    ConstantEngine ce;
    // Draw state set by packets rather than registers.
    u64 index_base = 0;
    u32 index_size = 2;
    u32 num_instances = 1;
    bool warned_indirect = false;
    bool trace = false; // DC_CP_TRACE
    bool validate_registers = false; // BB_METAL_CP_VALIDATE=1, no rendering side effects
    // Flip label requested by the frame being executed; written once it is shown.
    struct FlipLabel {
        u64 addr = 0;
        u32 value = 0;
        bool interrupt = false;
    } flip_label;
    Gpu::Renderer* renderer = nullptr;
    std::shared_ptr<Gpu::ExecutionState> execution=std::make_shared<Gpu::ExecutionState>();
    std::atomic<bool> stopping{false};
    struct CallbackGate {std::mutex lock;bool alive=true;};
    std::shared_ptr<CallbackGate> callback_gate=std::make_shared<CallbackGate>();
    Stats stats;
    std::function<void(u32)> on_irq;

    // The host GPU runs everything recorded so far in order, so a ring that
    // waits for a value one of these writes will produce only waits for work
    // that is already ahead of it: the wait is satisfied without a round trip.
    std::mutex fence_lock;
    Metal::CpFenceMapCache fence_map_cache;
    // This epoch and the table below are protected by fence_lock together.
    Metal::PendingFenceEpoch pending_fence_epoch;
    struct PendingWrite {u32 value;u64 ticket;u32 ring;bool known=true;};
    std::unordered_map<u64, PendingWrite> pending_fences;
    u64 next_fence_ticket=0;

    std::mutex rings_lock;
    std::vector<std::unique_ptr<Ring>> rings;
    std::mutex admission_lock;
    std::condition_variable admission_changed;
    bool checkpoint_paused=false;
};

CommandProcessor& Instance();

} // namespace Gpu::Liverpool
