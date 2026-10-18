// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "cpu_x86_64.h"
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <span>

namespace Bb::Checkpoint {
enum class BoundaryKind {BeforeImport,AfterImport};
struct ImportProxy {
    void* target=nullptr;
    std::uint64_t identity=0;
    void* code=nullptr;
    void (*observe)(void* context,BoundaryKind,const ImportProxy&,const BbCheckpointCpu&)=nullptr;
    void* context=nullptr;
    // Opt in only for a blocking API whose adapter can unwind without
    // observable side effects and whose inputs fit in the argument registers.
    bool replayable=false;
    // Explicit nonblocking, non-callback leaf APIs can tail-call their target
    // without saving a context while no rendezvous is requested. During a
    // request the identical guest stub takes the fully preserving path.
    const std::atomic<bool>* requested=nullptr;
    bool leaf=false;
    bool guest_only=false;
};
// The descriptor and code must outlive all calls through the proxy. Caller
// supplies writable code storage, then makes it executable. No live code is
// patched here. Only outermost crossings notify: nested guest callbacks retain
// a native host frame and cannot provide a portable checkpoint continuation.
bool EmitImportProxy(std::span<std::uint8_t> code,ImportProxy&);
bool ImportProxiesHealthy();
std::size_t ImportDepth();
// A checkpoint-aware blocking adapter can finish unwinding its native frame,
// then reenter at BeforeImport with the original guest argument/FP registers.
// Returns false for nested callbacks, unsupported imports or a failed bridge.
bool ReplayCurrentImport();
bool CanReplayCurrentImport();
// A guest pthread_exit transfers to a newly established native bootstrap. Its
// abandoned import frames must not suppress every subsequent boundary.
void AbandonCurrentImports();
bool CurrentImportEntry(BbCheckpointCpu& cpu);
// Host hooks that call guest code without going through an import retain
// native frames above it. Such callbacks must drain before capture.
void EnterNativeGuestCallback();
void LeaveNativeGuestCallback();
}
