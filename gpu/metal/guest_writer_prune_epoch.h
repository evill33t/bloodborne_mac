// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

namespace Gpu::Metal {

// Renderer-thread compaction gate. Completion blocks retain only CompletionState
// and their existing flag, never this gate, the registry, or renderer state.
// This skips erase_if work only; retained flags must still be checked before a
// positive pending-writer witness or a CPU-read rejection uses a retained item.
class GuestWriterPruneEpoch {
public:
    using Flag=std::shared_ptr<std::atomic<bool>>;
    struct CompletionState {
        explicit CompletionState(std::uint64_t initial=0):epoch(initial) {}
        std::atomic<std::uint64_t> epoch;
        void Advance() noexcept {
            auto before=epoch.load(std::memory_order_relaxed);
            while(before!=Maximum && !epoch.compare_exchange_weak(before,before+1,
                    std::memory_order_release,std::memory_order_relaxed)) {}
        }
    };
    using State=std::shared_ptr<CompletionState>;
    enum class Use:unsigned {CpuReject,GpuUnknown,GpuWitness,Count};
    struct Counters {
        std::uint64_t calls=0,scans=0,skipped=0,examined=0,removed=0,published=0;
        std::uint64_t conditional_checks=0;
        std::array<std::uint64_t,unsigned(Use::Count)> completed_ignored{};
    };
    static constexpr auto Maximum=std::numeric_limits<std::uint64_t>::max();

    // Strict opt-in. Construction reads the environment once in the renderer.
    static bool ConfiguredEnabled(const char* value) noexcept {
        return value && std::strcmp(value,"1")==0;
    }
    // The initial epoch parameter permits deterministic saturation fixtures.
    explicit GuestWriterPruneEpoch(bool enabled=false,std::uint64_t initial=0)
        :state(enabled ? std::make_shared<CompletionState>(initial) : nullptr) {}
    bool Enabled() const noexcept {return bool(state);}
    const State& SharedState() const noexcept {return state;}
    const Counters& Statistics() const noexcept {return counters;}
    std::uint64_t CompletionEpoch() const noexcept {
        return state ? state->epoch.load(std::memory_order_acquire) : 0;
    }

    static void Complete(const Flag& flag,const State& state) noexcept {
        flag->store(true,std::memory_order_release);
        if(state)state->Advance(); // publish AFTER the completed flag
    }
    void Published() noexcept {if(state)Increment(counters.published);}

    // Called only where this writer would reject a CPU read, reject unknown
    // GPU work, or positively witness a pending overlapping GPU producer.
    bool RetainedCompleted(const Flag& flag,Use use) noexcept {
        if(!state)return false; // legacy path has no extra atomic flag read
        Increment(counters.conditional_checks);
        if(!flag->load(std::memory_order_acquire))return false;
        Increment(counters.completed_ignored[unsigned(use)]);
        return true;
    }

    template<class Writers> void Prune(Writers& writers) {
        Prune(writers,[](const auto& writer) {
            return writer.completed->load(std::memory_order_acquire);
        });
    }
    // The accessor overload lets a fixture interleave an actual completion
    // while the actual production gate/erase_if transaction is executing.
    template<class Writers,class IsComplete> void Prune(Writers& writers,IsComplete is_complete) {
        if(!state) {std::erase_if(writers,is_complete);return;}
        Increment(counters.calls);
        const auto before=state->epoch.load(std::memory_order_acquire);
        if(before!=Maximum && scanned && before==last_scan_epoch) {
            Increment(counters.skipped);return;
        }
        Increment(counters.scans);
        std::erase_if(writers,[&](const auto& writer) {
            Increment(counters.examined);
            const bool complete=is_complete(writer);
            if(complete)Increment(counters.removed);
            return complete;
        });
        // Never reload after scanning: a completion of an already inspected
        // item must leave a different epoch and force the following scan.
        last_scan_epoch=before;
        scanned=true;
    }

private:
    static void Increment(std::uint64_t& value) noexcept {if(value!=Maximum)++value;}
    State state;
    Counters counters;
    std::uint64_t last_scan_epoch=0;
    bool scanned=false;
};

} // namespace Gpu::Metal
