# Bloodborne 1.09 executable: macOS graphics, performance and stability audit

Date: 2026-10-18. Scope: static executable research correlated with the current
direct Metal port. No game launch, save changes, source-dump writes, or runtime
patches were performed. No FPS improvement or gameplay fix is claimed.

This document records the investigation before implementation. Subsequent
port changes and tests are documented in
[macOS GPU stability and prewarming](MACOS_GPU_STABILITY.md).
The later [gameplay validation](MACOS_GPU_GAMEPLAY_VALIDATION.md) records native
motion-vector contracts and explains why the captured HTILE traffic supports a
metadata-state fix rather than an accurate depth-extrema optimization.

## Result and implementation order

The executable gives us concrete places to investigate CPU/GPU stalls and an
existing application-supplied motion-vector path. The highest-value sequence is:

1. Instrument GPU completion and make command failures and unsatisfied waits
   explicit. Preserve the existing deferred completion protocol.
2. Measure the native CPU fence loops and replace expensive polling with
   completion-driven parking where the original contract permits it. Correct
   GPU-produced indirect argument handling at the same time.
3. Replace full-range HTILE stand-ins with conservative depth bounds; measure
   spatial light-grid size separately from CPU job divisions.
4. Inspect the native external velocity texture in a real scene. Reuse it for
   object-aware temporal AA only if its contents, coverage and timing are valid.
5. Prewarm observed shader/pipeline combinations during loading and improve
   texture write tracking, retaining the existing cache and lifetime safeguards.

These are prioritized research and implementation targets, not installed fixes.

## Target identity and evidence boundaries

The original input was read from
`/Volumes/Data/Systems/PS4/GAMES/CUSA00900-backup/eboot.bin`:

| Artifact | SHA-256 |
| --- | --- |
| Original plaintext SELF, 93,825,704 bytes | `d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9` |
| Decoded original ELF | `cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86` |
| Normalized loaded image, existing preflight | `071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a` |

Re-extracting that SELF with the repository's `parse_self` produced bytes
identical to `out/macos-data/prepared/eboot.elf`. The SFO declares APP_VER 01.00
but master VERSION 01.09; the current preflight recognizes the 1.09 code layout.

REA 6.7.0 opened the decoded ELF using Hopper. Its initial inventory reported
1,721 procedures and zero typed strings:
`ev_38478bea6dc765131a5ce16dddc30f2bdc3ad1f3c5d6ac6f0686903377c3c453`.
That import was insufficient for broad conclusions. Its negative seed lookup
`ev_bd0b58591fffbd830bc873d0bcf24d3e14c556563825982b4b026a38779beb88`
and empty string search
`ev_4fc318913e27ea2c82bcc2b321cd8714f9af74671d604424bd268b5729aa6ff1`
are coverage limitations, not evidence that graphics functions or strings are
absent.

The original unwind header contains 162,959 entries. Each was checked against
its CIE/FDE, function address, range and executable file mapping. These are
unwind ranges, not 162,959 independently understood or verified functions.
Analysis-only ELF copies add function symbols from those ranges and normalize
the ELF type/OSABI. Their two file-backed PT_LOAD segments were compared byte
for byte with the original and remain identical. Never run or substitute these
analysis copies for the prepared game executable.

The full symbol-seeded Hopper import exceeded REA's 120-second startup limit.
The findings below therefore distinguish REA's inventory from focused offline
LLVM instruction and original-relocation evidence, plus live source inspection.
They are not presented as a complete REA decompilation or runtime trace.
REA also retained a cleanup-incomplete marker after the timed-out import. The
Hopper processes started by this investigation were terminated and verified exited;
the retained REA connection needs reconnection before another native analysis.
No REA/client configuration or analysis engine installation was changed.

Unless explicitly stated otherwise, executable addresses below are **ELF RVAs**.
Existing bbhost Binary Ninja preferred addresses are RVA + `0x400000`; host
runtime pointers and Ghidra addresses are different coordinate systems.

## 1. Concrete CPU fence polling can waste a substantial part of a frame

**Observed in the executable:** RVA `0x33c850` allocates a label, initializes it,
emits a 64-bit EOP write of `1` at `0x33c93f`, and submits commands at
`0x33c948`. Its loop at `0x33c954` through `0x33c97c` repeatedly reads the label
and calls `sceKernelGetProcessTime` until completion or 100,000 microseconds.
There is no sleep or pause instruction in this loop.

A second fence object uses a counter at `this+0xd0` and a label pointer at
`this+0xd8`. Original relative relocations establish its vtable at `0x51bbeb0`:

| Slot | Target RVA | Observed behavior |
| --- | --- | --- |
| `+0x28` | `0x3a17c0` | Flushes through the 100 ms polling routine |
| `+0x30` | `0x3a1840` | Tests label value against the requested counter |
| `+0x38` | `0x3a1850` | Polls the pending check against a nominal 10 ms budget |
| `+0x40` | `0x3a17e0` | Increments the counter and emits its 64-bit EOP write |

The first pending result in `0x3a1850` invokes the flush method. Its nominal
10 ms test therefore does not strictly bound the whole call: the nested flush
can itself poll for 100 ms.

**Port implication:** measure call frequency, actual wait duration and calling
thread, then use the existing Metal completion notification to park a waiting
CPU thread when appropriate. Preserve label width, counter ordering, timeout
semantics, allocation lifetime and wakeup races. A hash/expected-byte-guarded
engine hook could avoid repeated guest polling under Rosetta without removing
the GPU dependency. Short waits may still benefit from a bounded spin.

**Unknown:** these budgets are worst-case code paths, not measured delays on
every frame. No aggregate frame-time cost was measured.

Evidence: `out/rea-eboot-2026-10-18/audit-fence-flip-clock-evidence.json` and its
companion LLVM instruction listing.

## 2. GPU synchronization and indirect arguments need explicit ownership

**Observed in the executable:** the indirect draw writer at `0x10744d0` emits
`DRAW_INDIRECT` (`0xc0032400` plus a possible predicate bit). The engine wrapper
`0x21699d0` calls it at `0x2169a89`. The indirect dispatch writer at `0x1074de0`
emits `DISPATCH_INDIRECT` (`0xc0011602` plus a possible predicate bit), with
engine callers including `0x2169640` and `0x21a2320`.

LLVM-verified direct calls to the memory-wait writer `0x1077b30` occur at 24
sites. The generic wait writer `0x1075c10` is called twice in `0x26bc320`.
This establishes engine-side paths; it does not prove every path runs in a
particular gameplay scene or that every argument buffer is GPU-produced.

**Observed in the port:**

- `gpu/metal/vendor/bloodborne/src/gpu/liverpool/command_processor.cpp:651`
  handles `WAIT_REG_MEM` with up to 1,000,000 `sched_yield()` iterations, then
  logs and continues even when its predicate remains unsatisfied.
- The pending-fence shortcut at line 662 can avoid a CPU wait for ordered GPU
  work. Indirect argument handling at lines 722–752 immediately dereferences
  ordinary CPU pointers. GPU queue order alone does not make an unfinished GPU
  producer's results visible to that CPU read.
- Deferred `WriteFence` and flip-label callbacks dereference packet addresses
  without the mapping ownership checks used by several other packet paths
  (lines 50–62, 267–275 and 309–335).

**Recommended change:** keep GPU-generated indirect argument reads on the GPU
where possible; otherwise resolve the producing dependency before CPU access.
Replace iteration-based waits with a bounded, observable completion protocol.
Validate asynchronous destination ranges and mapping generations/lifetimes.
Do not silently treat timeout as successful execution.

The renderer already defers completion through `MTLSharedEvent` callbacks
(`mtl_renderer.mm:1151`) and delays flip completion until host GPU work finishes
(`hle/gnmdriver/gnmdriver.cpp:352`). Retain those safeguards.

## 3. GPU errors need to reach the waiting guest

`gpu/metal/vendor/bloodborne/src/gpu/metal/mtl_renderer.mm:983` installs upload,
arena-lifetime and statistics callbacks in normal `Commit()` but no durable
command-status/error handling. `WaitLast()` at line 1191 waits and clears the
command without inspecting its failure status. Checkpoint draining at line
4239 already checks `MTLCommandBufferStatusError`.

Together with the executable's label polling, this is a concrete stability
concern: a failed command can prevent an expected completion signal and make
the later stall or rendering corruption hard to diagnose.

Record the submission ID, shader/pass identifiers, Metal error and first
unsatisfied guest dependency. Wake waiters into a controlled failure state;
never publish successful guest fence completion for failed work. No such
failure was reproduced during this static audit.

## 4. Depth-based light rejection and light-grid size are separate optimizations

**Observed in the executable:** `0x10739b0` writes the depth target's HTILE base
and surface registers separately from depth image bases. The HTILE allocation
calculator at `0x109e570` and the descriptor slice-size reader at `0x1072630`
are also present and have engine callers. Allocation geometry is not the full
per-tile compression/quantization contract.

**Observed in the renderer:** `mtl_renderer.mm:3821` fills HTILE reads with
`0xFFFC000F`, the full depth range, because actual depth lives in Metal textures.
That conservatively avoids removing visible lights but loses depth rejection.

Use GPU depth reduction to derive conservative per-tile bounds, retaining the
full-range fallback when the format, layout, revision or sample semantics are
unknown. Verify every consumer's expected packing before writing guest-visible
metadata. Compare depth and lighting images and count rejected lights; measure
the reduction cost as well as any lighting savings.

The existing "Optimize light grid" setting changes more than job scheduling:

- View `0x229e990` calls main pipeline `0x22a0a30`.
- At `0x22a5d22` and `0x22a5d2a`, the pipeline loads tile dimensions from graphics
  manager `+0x78a8/+0x78ac` and ceil-divides viewport dimensions by them.
- Globals `0x513ae80/+4` control job divisions; the work callback is `0x22b4660`.
- The callback passes the tile dimensions to `0xea4410`, which computes the cell
  grid and allocates 40-byte cell records (`0xea44ee` through `0xea4536`).

`gpu/metal/bbhost/graphics.cpp:27` increases spatial tile sizes and reduces
divisions from `3×3` to `1×1`. Larger cells can reduce CPU construction and
scheduling costs while increasing lights processed per cell and GPU fragment
work. Keep its current opt-in status and measure CPU/GPU effects separately.
Do not conflate it with restoring HTILE depth rejection.

## 5. A native external velocity path may improve temporal image quality

**Observed in game code:** function `0x21d5f80` reads a candidate texture from
configuration `+0x388`. The registration path requires `+0x1dc != 0`,
`+0x1e0 == 2`, and a nonnull texture (`0x21d7e5b` through `0x21d7e80`). It extracts
the resource descriptor and dimensions, supplies mode 1 at `0x21d7f21`, and
calls `0xbb5430` at `0x21d7f45`.

The native diagnostic string identifies that wrapper as
`SetMotionBlurVelocitySource`. The instruction chain is:

`0x21d5f80 → 0xbb5430 → 0xbda460 → 0xbd3c50`.

The final setter stores mode/format/texture at `+0xa28/+0xa2c/+0xa30`, a flag at
`+0xa38`, and supplied scale floats at `+0xa14`. Comparison instructions next
to diagnostic strings in `0xbe0450` identify mode 1 as
`AUTOGEN_APPOVERWRITE`, with mode 0 `AUTOGEN` and mode 2 `APPGEN`.
Mode 1 keeps both an internal generated texture and the application texture;
`0xbe1c50` subsequently reads both.

**Port implication:** our current Metal TAA uses camera/depth reprojection
(`gpu/metal/temporal_aa.mm:16`), without per-object velocity. The native texture
could provide a useful existing source for motion from moving characters and
objects, reducing the need to reconstruct every transform separately.

**Required validation:** inspect this exact resource in a real GPU capture with
a stationary camera and independently moving character. Establish whether the
gate is active, whether vectors are valid, their units/sign/jitter convention,
resolution and frame association, and coverage of skinning, cloth, foliage and
particles. Motion-blur input availability does not establish TAA-ready vectors.
Never feed an unvalidated texture directly into a temporal upscaler.

The current TAA already runs before tone mapping and HUD and has depth/history
rejection. MetalFX is currently spatial. The retained Vulkan object-motion
implementation is a reference, not evidence of direct Metal integration.

## 6. Query support needs correctness work before an occlusion speed claim

YEBIS query routines are present. At `0xc75370`, a result fetch stores a bounded
nonnegative occlusion result at object `+0x2c`. Routine `0xc75480` retries a
virtual result operation while it returns pending (`1`) at `0xc75500` through
`0xc75511`; that loop has no local time limit or sleep.

The native writer `0x1077660` emits paired `EVENT_WRITE` packets with event words
`0x138` and `0x139`. It is called by GX paths including `0x2169ec0` and
`0x216a070`. The active command processor handles EOP/EOS but has no ordinary
`EventWrite` case; unknown commands fall through at
`command_processor.cpp:757`. The draw writer also carries a predicate bit,
while active header handling does not implement general predication.

Implement/query-test the exact event/result protocol and its completion and
fallback behavior before making these paths authoritative. The located YEBIS
strings include luminance and post-processing usage. They do **not** prove
whole-world geometry occlusion culling is active or that Metal visibility queries
will yield a large FPS gain. The separate bbport conditional-execution decoder
does not establish support in the live Metal command processor.

## 7. First-use pipelines and texture coherence remain practical targets

Graphics and compute first-use paths call `WaitCompiled`
(`mtl_renderer.mm:3262`, `:3896`; blocking implementation `:1335`). Worker threads
already exist. Capture actual shader/state combinations and prewarm versioned
pipeline descriptions during loading; keep first-use draw correctness and bound
memory. Do not restore live Metal binary-archive serialization: the current
driver-managed policy intentionally avoids a previously observed Rosetta/Metal
serializer crash (`mtl_compiler.mm:16`).

Texture validation checks one 64 KiB chunk of a cold texture per used frame,
then hashes every chunk per used frame once that texture becomes hot
(`mtl_renderer.mm:1428`). Same-frame hits can bypass repeated content validation
(`:1667`). Changed textures are detiled/uploaded across their described mip and
layer layout. This creates two different targets: delayed visibility of small
writes to cold resources and CPU cost of fully checking dynamic resources.

Investigate engine upload/streaming boundaries and range-based invalidation;
distinguish mapping-generation changes from actual content-write dirtiness.
Use partial uploads or GPU detiling where measurements justify them. Preserve
immutable queued texture versions (`:1705`), upload lifetimes, mapping checks and
the existing reconstructible-upload eviction budget (`:422`). This audit did not
measure a leak or quantify hashing/detiling cost in gameplay.

## Lower-priority or already-addressed topics

- VideoOut's host `process_time` epoch differs from `sceKernelGetProcessTime`.
  However, the inspected game caller `0x26d6e50` only consumes flip-status
  `flip_arg` at offset `+0x18`; when behind it blocks on `sceKernelWaitEqueue`.
  That path does not use the process-time field. Unifying timestamps is ABI
  cleanup with unproven pacing impact.
- `sceGnmSubmitDone` waits all CPU command-processor rings. Measure actual
  caller dependencies before loosening this synchronization.
- The existing timing implementation already adapts sleep/spin behavior and
  includes camera/task/cloth/play-time corrections and patch-conflict guards.
  The 100 ms fence polling identified here is a different path from that frame
  limiter. Raising the frame cap alone is not a correctness fix.
- Guest affinity/priority are bookkeeping, and Fios2 remains an original linked
  guest module. Neither fact identifies the current bottleneck by itself.

## Validation plan and reusable artifacts

Use a fixed save, camera route, resolution/profile and frame cap; compare repeated
runs after controlled warmup. Record presented-frame median/p95/p99, CPU fence
wait duration and frequency, GPU pass time, draw/dispatch counts, cache misses,
hashing volume, command commits, peak memory and errors. Compare images for
lighting, disocclusion, moving objects and HUD. Include long-session streaming,
scene changes and suspend/checkpoint restoration when changing resource lifetime.

The port already has useful instrumentation: `DC_GPU_STATS`
(`mtl_renderer.mm:4017`) and `DC_FRAME_TIMES` (`gpu/metal/game_bridge.mm:362`).
No new profiling run or live gameplay capture was performed for this report.

Local artifacts are under `out/rea-eboot-2026-10-18/`:

- `source-identity.json`: current original SELF fingerprint and extraction match.
- `prepare_analysis_elf.py` and `eboot-analysis.json`: checked unwind recovery and
  exact analysis-copy changes, including unchanged PT_LOAD digests.
- `original-import-evidence.json`: exported REA inventory and bounded seed checks.
- `rea-full-import-result.json`: exact REA timeout and retained-cleanup diagnostic.
- `confirmed-calls.json`, `asm-*.txt`, `caller-*.txt`: LLVM-verified static sites.
- `audit-fence-flip-clock-evidence.json` and `audit-fence-flip-clock-llvm.txt`:
  native fence loops, original vtable relocations and flip-status consumer.
- `audit-graphics-evidence.json` and `audit-graphics-llvm.txt`: native velocity
  registration and spatial light-grid consumers.
- `graphics-strings.json`, `string-xref-candidates.json` and
  `packet-writer-candidates.json`: discovery aids; raw candidates alone do not
  establish instruction boundaries, execution or feature enablement.

All game-dependent outputs remain local under the ignored output directory.
Only this research report is added to repository documentation.
