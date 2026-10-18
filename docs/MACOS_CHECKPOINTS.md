# Full playable checkpoints: implementation status

The requested feature is a full playable checkpoint that survives a process
restart and preserves the hunter, enemies, combat state and blood stains.
**Saving or restoring that checkpoint is not implemented.** There are no new
checkpoint controls, hotkeys or automatic memory writes in the running game.

The production loader/thread runtime now has an opt-in checkpoint path
(`BB_CHECKPOINT_ENABLE=1`). It separates guest stacks from native pthread
bootstrap stacks, wraps resolved HLE imports, recreates guest TCB/LDT bindings
and retains logical synchronization state. Fresh processes resume a synthetic
guest through these production runtime providers. **The complete Bloodborne
save/restore path is still absent.** Files, clocks, services/audio, the complete
memory image and GPU state are not yet rebuilt together.

Live CPU audits froze and resumed 45 threads at startup and 60 threads in the
loaded DLC-dependent save. They did not save a file or restore an in-game
checkpoint. An optional
`BB_CHECKPOINT_AUDIT_FILE` watches integer request tokens and writes CPU audit
results to `<request>.result`; the UI continues servicing presentation. The
coordinator also checks that every unfinished thread metadata record has one
captured continuation and rejects creation/cleanup/untracked-callback races.

Current implementation:

| Component | Implemented proof | Still required for Bloodborne |
| --- | --- | --- |
| `src/checkpoint/cpu_x86_64.*`, `runtime.*`, `thread.h`, `src/runtime_thread.c` | Restores GPRs, RFLAGS, x87, SSE/MXCSR and AVX YMM0-15 through fresh production guest pthreads; stable identities, stacks, TCBs, dynamic module TLS, errno pointers and thread attributes survive restart | Connect main/game restore boot, guest image/low allocation ownership and the complete provider manifest |
| `src/checkpoint/import_proxy.*`, `src/probe.c` | Loader imports use rebound proxies in checkpoint mode; guest-only continuations before/after HLE, nested callback suppression, explicit replay and pthread_exit cleanup | Cover generated engine hooks/host callbacks and other nonlocal transfers; reject unsupported crossings |
| `src/checkpoint/coordinator.*`, `runtime.*` | Real guest registration, consistent rendezvous, timeout recovery, thread/continuation consistency checks and automatic resume | Coordinate GPU, files, clocks, audio and service providers at one complete boundary |
| `src/checkpoint/semaphore.*`, `synchronization.*`, `rwlock.*` | Production guest ABI uses logical counts, mutex/read/write ownership and recursion, conditions and pending waits; native waits detach/replay without losing requests; pending results/timeouts survive restart | Cover the remaining blocking service/event operations and cross-check all owner/waiter IDs in the complete runtime manifest |
| `src/runtime_memory.c`, `src/checkpoint/memory.h` | Exports/restores the production direct/flexible/reserved VMA layout, physical allocation slots and backing bytes; preserves aliases, page protections and flexible allocation bitmap | Coordinate with stopped CPU/GPU writers; capture image/stack/TLS low mappings separately |
| `src/checkpoint/archive.*` | Versioned checked sections, atomic replacement, bounded streaming writes/verification using 1 MiB scratch, indexed reads, mutation detection; failure preserves previous output | Enforce a complete game-state manifest and connect streaming sources to the actual stopped guest/GPU state |
| `gpu/metal/raw_texture_snapshot.*`, `renderer_checkpoint.inc`, `frontend_snapshot.*`, `game_checkpoint.inc` | Captures the renderer registry, all initialized target mip/slice/depth/stencil planes, pending clears, write ordering, aliases, GDS and constant snapshots; restores a renderer in two fresh processes and continues rendering; live runtime hook coordinates CPU parking, CP draining and renderer locking | Capture video/GNM/event registries and clocks; include these chunks in the complete game manifest and restore boot |

The import replay path is explicitly opt-in per operation. An adapter may use it
only after unwinding without observable side effects, with inputs in argument
registers. It never rewinds an arbitrary HLE function with already-issued work.
Nested host callbacks cannot request replay. Observer failures disable checkpoint
observation while allowing ordinary import calls to continue.

`gpu/metal/material_snapshot.{h,cpp}` stores the lossless stain mip bytes, shader
input words and diagnostic memory blocks. It checks game identity, dimensions,
formats, lengths and checksum, and retains a `.previous` copy on overwrite. Its
memory blocks are inspection data, never addresses at which its loader writes.
The original small RGBA8 transfer helper remains available for material tests.
The production raw transfer provider additionally covers the renderer's
uncompressed integer/normalized/float colour formats, depth and stencil,
2D/array/3D textures, and each mip/slice. The raw helper also tests cube arrays.
Unsupported layouts fail before publication. These remain GPU components, not
a playable game checkpoint.

The checkpoint CPU/import/coordinator library and logical synchronization
adapters are linked into the production runtime. They are enabled only by the
explicit environment flag; normal launches keep their established thread and
synchronization path. The renderer provider is connected to the opt-in live
coordinator through `src/checkpoint/graphics.h`. The production memory provider
is compiled with the runtime but no game save/load path invokes it. The flexible-memory split fix below also applies to normal launches.

The command processor now has bounded submission draining, logical frontend
register/CE/ring state export and reconstruction on new native ring threads.
Metal draining waits for command completion and fence callbacks with a deadline.
Timeout reopens submission and preserves the previous output. The runtime
parks guest CPUs first, drains CP/GPU work while HLE synchronization still
accepts fence callbacks, then pauses/exports the logical kernel providers.
Failure and thaw unlock targets and reopen GPU submission before guest CPUs
resume. This is tested with the actual PM4 frontend and ordinary Metal
rendering. Video output, event listeners, GPU clocks and the complete game
restore boot are still required.

Renderer chunks are streaming sources held by the frozen token. They contain
checked little-endian metadata and lossless raw texture planes; the transfer
uses one plane at a time, with a deadline and no sampling or colour conversion.
Private textures, clear flags, target variant order, mip aliases, GDS and CE
constant snapshots are authoritative. Derived shader/pipeline/upload/view and
presentation caches rebuild. Guest-backed writable buffers and the CE arena
belong to the guest memory provider. Renderer restore validates the complete
chunk manifest before allocation and publishes new registries only after all
transfers succeed. It requires a fresh renderer with restored guest memory.
A failed fresh-process restore must never activate guest execution.

## Memory split correction

While implementing the memory provider, a regression fixture demonstrated that
splitting a flexible VMA retained the original physical offset in the right-hand
pieces. Releasing its middle page could therefore discard/reuse the first page
while that first page was still mapped. `split_at` now advances the physical
offset for flexible mappings as well as direct mappings. The test failed before
the correction and passes afterward, including live-neighbor contents and reuse
of the released page. This is memory-fixture proof; no gameplay/graphics benefit
has been established.

## Runtime findings

These findings were checked against this checkout and the prepared 1.09 ELF at
`out/macos-data/prepared/eboot.elf` on 2026-10-18.

| State | Current implementation | Requirement for restore |
| --- | --- | --- |
| CPU execution | Default mode uses the established pthread path; checkpoint mode switches to separate guest stacks and serializable continuations with new native bootstrap/exit recovery frames | Integrate the complete game restore boot and every host callback boundary |
| TLS | `src/compat_macos.c`: guest FS uses per-thread LDT slots with host pthread-specific allocation bookkeeping | Recreate LDT entries and bind each restored guest thread to its saved TCB |
| Mutexes and conditions | Checkpoint mode uses logical handle/owner/recursion/condition wait records, with native CVs used only to wake callers | Include and validate the provider in the complete game bundle |
| Semaphores | Checkpoint mode retains logical requests while their native callers unwind/replay; counts/grants/deadlines are independent of native stacks | Include and validate the provider in the complete game bundle |
| Read/write locks | Checkpoint mode records stable guest owners/read recursion and pending reader/writer waits | Include and validate the provider in the complete game bundle |
| Guest memory | `src/runtime_memory.c`: flexible VMAs, physical direct-memory blocks and aliased mappings | Save allocation metadata, protections and contents; restore aliases to the same physical bytes exactly once |
| Files and saves | `src/runtime_file.c`: host descriptors, directory cursors, mount table and pending atomic-save temporary files | Reopen files and restore positions/cursors; use checkpoint-owned writable save storage and preserve pending-save semantics |
| Audio and services | Native output queues, AJM instances, guest callback state and HLE service registries | Export logical state and recreate host resources; reject incomplete captures |
| GPU | `mtl_renderer.mm`: private Metal targets and buffers can contain newer data than guest RAM | Drain a complete submission boundary, capture authoritative resource contents and metadata, recreate views/aliases and invalidate derived caches |
| Presentation | `game_bridge.mm`: renderer waits for the UI's frame acknowledgement | Keep the UI servicing the handoff while quiescing; avoid waiting for a renderer lock on the UI thread |

Copying guest VMAs alone would leave stale thread handles, mutex ownership,
semaphore waiters and GPU contents. Copying the containing host allocations does
not make those resources valid in a fresh process. Checkpoints must serialize
their logical state instead.

`fork()` is not a usable process-checkpoint shortcut for this multithreaded
Cocoa/Metal runtime. Apple's threading documentation requires an `exec` after
`fork` for this environment; continuing in the forked child does not provide a
supported way to resume these framework resources:
https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/AboutThreads/AboutThreads.html

## The native world-transition serializer is insufficient

The bbhost layout headers name native transition serializers. Disassembly of the
prepared ELF establishes a narrower contract than a full world save:

* RVA `0x191bd10` (`WorldChrMan` transition serialization) emits the `CHR ` tag
  with version `0x15091700`, iterates character areas and calls `0x190e750`.
* RVA `0x190e750` emits a 12-byte area record followed by
  `(character_count + 7) / 8` bytes. It sets one occupancy bit per character
  based on that character's runtime state. It does not serialize per-character
  transforms, health, AI, animation, controller modules or blood atlases.
* RVA `0x191bdd0` consumes that transition format through `GameStateMan`.
  Invoking it cannot restore the missing state.

These routines must not be exposed as full checkpoint save/load. Calling
SummonedMapReload or replacing its buffers would introduce a transition without
meeting the requested preservation contract.

## Required implementation sequence

1. Finish coverage of generated engine hooks, host callbacks and remaining
   service/event waiters. The production guest thread and synchronization
   boundaries are connected. Quiescing must time out and resume every provider
   on failure; arbitrary host-thread suspension is not enough.
2. Connect the production memory and thread/synchronization providers to the
   complete bundle; add logical file, clock, audio and service export/import.
   Never load raw pthread objects, host pointers or jump buffers from a file.
3. Complete the GPU bundle: target/depth/stencil/GDS/CE and frontend providers
   now exist, but video output, event listeners, GPU clocks, guest-backed
   writable buffers and complete restore activation must be coordinated.
   Stain maps are part of this state, not a colour-only patch.
4. Write one versioned checkpoint bundle with a manifest, per-section size and
   checksum, prepared guest/mod identity, runtime schema, memory configuration
   and renderer resource schema. Validate every section before activating a
   restore. Renderer implementation changes may be allowed only when the
   serialized resource schema remains compatible; host code addresses must not
   be part of the persisted execution state.
5. Restore into a fresh process and checkpoint-owned save directory. Rebuild
   the host resources before releasing any guest thread. Keep ordinary saves,
   settings, trophy history, source game and DLC untouched by restore trials.
6. Only then expose save/load in the in-game menu. Unsupported/incomplete
   runtime state must produce an explicit failure instead of a partial success.

## Acceptance and validation

Run the checkpoint fixtures with:

```sh
cmake -S packaging/macos/runtime -B out/macos-runtime -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out/macos-runtime --parallel 6
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir out/macos-runtime -R darwin-checkpoint --output-on-failure
```

The process fixture launches a synthetic guest with fixed guest code/data/stack
addresses and newly bound host imports. Separate fresh processes restore its
direct CPU continuation, a before-import continuation and an after-import
continuation, twice each. It verifies register/FP state, stack locals and continued
execution on restored guest memory. No host call stack is loaded from the file.

Other fixtures restore pending semaphore waits and the production VM provider
in separate processes, test thread rendezvous/timeouts, exercise invalid file/
CPU/layout rejection, and verify a real private Metal texture round trip through
disk. The synthetic import tests include replay, native callback nesting, native
thread-local isolation, callback failure, stack arguments, SysV AL for variadic
calls, AVX arguments and aggregate/x87 results. The GPU test requires a Metal
device and otherwise returns the explicit skip code 77.

`darwin-checkpoint-runtime-process` uses the production guest ABI with five
guest threads. It saves, exits and restores twice into new processes/native
pthreads. It verifies stack/TCB addresses and identities, dynamic libc TLS,
stable errno pointers, thread attributes, recursive mutex ownership, and
pending mutex/condition/semaphore/read-write-lock waits. Workers remain gated
until all tested providers are activated. The fixture also creates/destroys
100,000 temporary mutex attributes before capture: the former 65,536-lifetime-ID
limit was reproduced returning `ENOMEM` with zero live objects. Logical ID
lifetime and bounded live-record limits are now separate.

The streaming archive tests check cross-chunk reads, failed-provider atomicity,
64-bit sizes without allocating those payloads, malformed inputs and mutation
of an already verified inode. PM4 tests reconstruct frontend state on new ring
threads, reject bad state before activation, block new submissions while paused
and recover admission after a deliberately blocked-ring timeout.

These tests prove the tested production integrations and synthetic guest
restarts. They do **not** prove complete Bloodborne HLE coverage, complete GPU
resource coverage or a playable game checkpoint.

Validation on 2026-10-18: the complete runtime build and CTest suite passed
39/39, including raw Metal transfers and two fresh-process renderer restores
with a real device. The renderer fixture compares every saved byte and logical
metadata record before continuing a red target and a pending green fast clear;
its original process deliberately changes the live state after writing the
archive. The CPU runtime fixture rejects a simulated GPU timeout, resumes all
guest threads and checks HLE callback ordering around a successful freeze.
The flexible-memory regression was reproduced against the previous implementation before applying
the correction.

Live trials use copied saves/settings under `out/macos-checkpoint-boot-trial`,
the clean game dump and `BB_DLC_DIR=/Volumes/Data/Systems/PS4/DLC/CUSA00900`.
The provided save requires that DLC; do not omit it on a diagnostic restart.
The local helper `out/macos-checkpoint-trial-launch.py` checks its presence,
keeps normal saves out of the trial and removes GPU diagnostic flags. The first
loading trial exposed the temporary-attribute ID exhaustion above. A CPU
rendezvous or a combined CPU/GPU boundary is not evidence of an in-game
checkpoint restore. Audit output reports GPU chunks as available but not read;
it does not write a save file. The combined live audit passed twice with 60
guest threads and 567 GPU chunks (718,902,572 bytes available, not read).
`--normal` on the local trial helper keeps the same DLC/save-copy configuration
with checkpoint instrumentation disabled for ordinary play.

A full checkpoint requires a fresh-process test with a hunter carrying visible
blood stains, a damaged enemy at a recorded position, and active world objects.
After restore, verify their position/health/state and stain texture bytes, then
continue combat and save normally. Repeat the restore after renderer changes
and verify frame timing and resource aliases. Also cover blocked HLE waiters,
pending file writes, audio callbacks, corrupt/incompatible files and failed
quiescing. No such playable restore has been performed yet.
