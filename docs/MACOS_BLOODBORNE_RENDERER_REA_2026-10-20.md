# Bloodborne renderer reconstruction and Metal dependency work

Investigation started against `4fa5d437dd2fbaf7e78ff223b698c849d73835ee`.
Target: native 1920×1080 on the Apple M4 Max, preserving the original game
dump and master saves. This is a partial renderer contract reconstruction,
not a claim that the complete original renderer has been recovered.

## Original renderer evidence

Fresh REA 6.7.0/Hopper analysis recovered the indirect packet writer at
`0x10744d0`, engine wrapper at `0x21699d0`, and light-grid callback at
`0x22b4660`. The analysis-only ELF extractions retain the complete selected
original unwind procedures byte-for-byte at their original addresses. They
omit external procedures/data and are never executed or substituted for the
game. Exact extraction/source hashes, complete REA records, Evidence IDs and
limitations are in
[`fresh-rea-evidence.md`](../out/renderer-rea-2026-10-20/fresh-rea-evidence.md).

The captured render graph contains a six-MRT material/G-buffer pass, depth
and material snapshots, subsequent lighting draws, HDR/effects, post-processing
and UI. Most recurring compute kernels in the selected frame perform copies,
clears or worklist updates. This evidence does not establish compute lighting
or the meanings of all G-buffer channels. The ordered resource/pass ledger is
[`contract-analysis.md`](../out/renderer-rea-2026-10-20/contract-analysis.md).

## Measured dependency problem

The fixed HEAD baseline plus opt-in asynchronous command timestamps reproduces
approximately 20.28 ms/frame inside the indirect-argument CPU drain in its
late diagnostic window. The corresponding renderer CPU frame averages
30.15 ms. These are inclusive blocked wall times: subtracting the drain from
the frame time would incorrectly subtract necessary GPU work too.

With the existing native-indirect experiment enabled, that drain disappears,
but texture-source waits replace it. The first candidate, classifying callback
write effects, enables GPU texture versions during startup but does not improve
the Dream scene: its later source-read path still drains unrelated callbacks.
That unsuccessful intermediate result remains retained for comparison.

The old “GPU busy” statistic sums command-buffer durations. It is not hardware
utilization. New `BB_METAL_COMMAND_TIMELINE_CSV` diagnostics observe existing
submissions without adding command cuts, waits or pixel readbacks. The offline
parser unions their execution spans and records submission gaps; these spans
also are not hardware utilization. Timestamp and callback-lifetime limitations
are documented in
[`timeline-review.md`](../out/renderer-rea-2026-10-20/timeline-review.md).

## Changes under validation

- Keep generic callbacks and arbitrary IRQ handlers unknown. Classify only
  the audited Flip completion as having no direct-pool writes, and IRQ-free
  fence stores by their exact captured physical destination.
- Retain both queued and active effects until each callback returns. Preserve
  the shared-event point, FIFO, real label/status stores and global drains.
- Admit immutable GPU texture copies only with exact mapping, producer,
  readiness and read-lease proofs. Historical uncertainty creates a new
  version; it never validates an old CPU hash baseline.
- At ordinary CPU texture-read boundaries, use the same current per-source
  proof for complete direct backing instead of draining unrelated callbacks.
  Historical invalidation still requires a full content rehash. Private or
  unresolved reads retain conservative callback blocking.
- Replace the prewarm source-recipe directory's volatile GPU registry ID with
  a hashed device-name/architecture/OS identity. Import only bounded, validated
  same-OS legacy source recipes, compile them on the current device, and persist
  successful recipes in the stable namespace. Offline binary archives are
  unchanged. Rejected files also consume the startup read budget.
- Permit native indirect draws with static packing enabled only when the
  decoded pixel shader is outside the exact packing families. Recognized
  material indirect draws and direct-address mode retain their rejection.
- Add an optional physical-page write index, retaining separate write and
  deferred-CPU epochs after bounded history eviction. Queries can return
  conservative same-page overlap; incomplete, invalid or uncertain sources
  still require full checking. Allocation failure returns to conservative
  history handling. The pool extent is supplied by the runtime and bounded.

The Bloodborne bridge now enables native indirect and GPU-source snapshots
before constructing the renderer and installing its mapping-mutation hooks.
Explicit environment overrides are preserved; setting both
`BB_METAL_NATIVE_INDIRECT=0` and `BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT=0`
restores the synchronized comparison route. Generic renderer construction
outside the game retains its prior opt-in configuration. The page index,
read-only specialization and static/direct/SIMD experiments remain optional.

### Intermediate diagnostic results

All rows below use the same stationary Dream lamp and copied save source,
1920×1080, native motion/TAA and AC power. Command, CPU-stage and presentation
timing are enabled, so these are mechanism comparisons rather than clean FPS
claims. CPU-frame values use complete records after ten additional seconds.

| Route | Diagnostic presented FPS | CPU frame wall | Indirect CPU drain | Steady source waits |
| --- | ---: | ---: | ---: | --- |
| HEAD default | 29.32 | 30.15 ms | 20.28 ms/frame | No native coherence journal |
| HEAD native + GPU-source switch | 11.63 | 82.53 ms | None | Many per frame |
| Classified callbacks only | 11.43 | 83.81 ms | None | 17.94/frame in the audited late subset |
| Per-source CPU-read proof | 38.94 | 24.00 ms | None | Zero throughout measurement |

The final row reduces approximately 30.5 commits/frame to 13.1 while still
rehashing approximately 186 MiB/frame. The texture wait counter stays at its
startup value for the entire measured Dream run; all frames have presentation
timestamps and no GPU failure is reported. This establishes a dependency
improvement, not attainment of 60 FPS.

The first default diagnostic has two missing presentation timestamps and
first-use pipeline hitches. Its 583 ms hitch coincides with two blocking CPU
pipeline compilations; the next command is not committed for approximately
503 ms. Captured GPU execution spans do not overlap those compilation waits.
The exact shader-source and descriptor recipes already exist under the prior
boot's cache directory. The cache uses the GPU registry ID, which changed
across reboot on this Mac, leaving those recipes outside the loaded namespace.
See [`control-gap-analysis.md`](../out/renderer-rea-2026-10-20/control-gap-analysis.md).

### Clean comparisons

All of these 90-second runs have complete presentation timestamps, zero GPU
failures and no display interval over 50 ms. They use the same native-1080p
Dream pose/save source and AC power without captures, recordings, command
timelines or frame-work/presentation diagnostics.

| Run | Presented FPS | P99 interval | Meaning |
| --- | ---: | ---: | --- |
| `rea-clean-control` | 29.9831 | 33.338 ms | Fixed HEAD default route |
| `rea-clean-ready` | 35.7038 | 33.333 ms | Per-source callback readiness, 512 records |
| `rea-journal-512` | 39.8965 | 25.006 ms | Combined build, reverse journal queries, 512 records |
| `rea-journal-4096` | 34.8637 | 33.333 ms | Same combined binary, 4,096 records |

The first matched pair establishes a 19.1% gain. The later same-binary capacity
comparison rejects simply increasing retention: it reduces hashing from about
186 to 23.7 MiB/frame, but increases history records examined from roughly
one million to 17.5 million per second. The larger capacity remains optional.
The combined binary is
`311d3663e782761de752a8fad65f206727d7dd0db4fee9c924f3ff92e9c9f546`.

The static-packing live test records attempts but no successful owned snapshot
packing. Its FPS must not be attributed to a shader optimization that did not
execute. Static packing remains disabled by default.

Four subsequent same-binary page-index comparisons do not show a repeatable
gain: OFF/ON pair A measures 39.767/32.667 FPS; pair B measures 35.156/39.771.
All four have complete presentation timestamps and zero GPU failures. The
index consistently reduces hashing to 23.7 MiB/frame, but remains OFF. Its
published-write proof does not replace the normal checks for unjournaled CPU
stores. The full input-hashed comparison and counter limitations are in
[`journal-page-index-ab-analysis.md`](../out/renderer-rea-2026-10-20/journal-page-index-ab-analysis.md).

Retesting read-only VS/PS guest inputs after removing the CPU drains also
shows no repeatable gain: enabled 36.994 and 36.029 FPS, disabled 36.853 FPS.
These three runs use the same frozen native-1080p binary/pose on AC, with no
missing presentation timestamps or GPU failures. Both qualified stage types
execute, but the specialization remains OFF.

## Validation ledger

The fixed baseline is built separately in an isolated worktree. Candidate
executables are copied before subsequent source edits; each phase retains its
binary hash, complete source patch and source hashes in
`out/renderer-rea-2026-10-20/`. Runs use copied users and external prepared
overlays. Instrumented runs are diagnostic, not clean FPS benchmarks.

The callback and per-source wait changes each passed the five focused CPU/GPU
tests: indirect CPU/GPU, texture write journal, Metal integration and GPU sync.
New real-Metal cases check pixels and wait counts for queued/active unrelated,
overlapping and unknown callbacks, real Flip/EventQueue effects, IRQ handlers,
physical aliases, remaps, forgotten history and raw CPU content changes.

One initial fixture failed correctly because constructing VideoOut advanced
the mapping generation after its texture baseline. Moving fixture warm-up
after setup corrected the test without relaxing production mapping checks.

The combined checkpoint builds all targets and passes 84 of 86 registered
tests in 113.39 seconds, with two expected private-input skips and no failures.
Its separate metadata-only cache identity test, 4,096-record GPU fixture,
static/native combination GPU fixture and both original captured material
six-MRT differentials also pass. Cache fixtures exercise old registry IDs,
duplicate recipes, invalid hashes/schema, links, persistence, unchanged legacy
inputs, bounded rejected-input reads and no foreground cache writes.

The subsequent page-index checkpoint also builds all targets and passes
84 of 86 tests, with the same two expected skips. An enabled-index GPU run
then exposed a fixture precondition that depended on eviction losing history.
The ordinary-rehash case now supplies an actual completed generic unknown
callback before pressure and the raw CPU edit, preserving every pixel,
wait, version and full-refresh assertion. Both legacy and explicitly
registered indexed GPU variants pass, along with the indirect CPU and journal
fixtures. This adds one test, bringing the registered suite to 87. Allocation
failure is source-reviewed; an allocator-failure injection has not been run.

A 330-second movement diagnostic completed four lamp transitions through
loaded maps `18010000`, `15000000`, `18000000` and back, with no reported GPU
failure. Its four command requests match actual world-load messages. The
Instruments attachment was accepted but its trace did not finalize within
the bounded timeout; export/salvage yielded no usable GPU tables. That run's
35 missing presentation timestamps, loading intervals and capture overhead
exclude it from clean FPS comparisons. No GPU-stage attribution is claimed.

The next checkpoint passes 88 of 90 registered tests in 111.08 seconds, with
the same two expected private-input skips and no failures. It includes the
metadata-only bound-vertex audit and optional Metal stage-counter profiler.
The profiler is disabled unless a CSV path is supplied. Its actual Metal
fixtures verify sampled/unsampled pixels, bounded pool exhaustion/recovery,
completion lifetime, clock calibration and safe output-path handling.

A clean 90-second replay with the new normal Bloodborne defaults measures
39.009 FPS at native 1920×1080, with p99 33.333 ms, maximum 33.340 ms, no
missing presentation timestamps and no reported GPU errors. The native
indirect and GPU-written texture options are absent from that run's environment,
so this exercises their bridge defaults. The frozen binary SHA-256 is
`58da868414c0e061aeaa8bc91d622d011c65007b229486e093972fe5fab612fe`.
The original controlled result was 29.983 FPS. This is progress, not a 60-FPS
result; current repeated controls also show meaningful run-to-run variation.

Native stage sampling confirms the selected six-MRT material work has
substantial vertex and fragment cost. In a late 50.117-second window, sampled
material vertex/fragment intervals sum to 10.582/9.490 seconds. These intervals
include dependencies and stalls and must not be called exclusive shader cost
or GPU utilization. About 5.9% of selected passes exhausted the fixed sample
pool. Whole-graph sampling exhausted about 89.7% and cannot establish the
remaining graph's budget. All mixed material passes contain multiple shader
identities, so their cost cannot be assigned to one shader. Instrumented FPS
is excluded from clean comparisons. The calibrated, input-hashed analysis is
[`stage-counter-offline-analysis.md`](../out/renderer-rea-2026-10-20/stage-counter-offline-analysis.md).

The same material diagnostic records about 24.08 ms of inclusive CPU frame
work, including 19.30 ms in draw and 14.02 ms in resource preparation. These
scopes overlap. No late `cp_sync_renderer` event remains. This supports
investigating both CPU resource bookkeeping and translated GPU loads rather
than assuming the remaining limit is exclusively GPU throughput.

The frozen default executable also completed a 900-second movement, camera and
attack replay with all four requested lamp transitions, including the return
trip, and no crash or reported GPU error. It submitted 31,433 frames. Its 33
missing presentation timestamps exclude its 34.89-FPS aggregate from clean
performance comparisons. RSS rose from 3,731 to 4,277 MiB while visiting and
reloading multiple areas; this duration does not establish a long-term bound.

That run retained a 137.7-ms CPU draw hitch. Two overlapping library-creation
scopes account for 37.8 and 14.5 ms, and no `pipeline_wait` scope is recorded.
An exact source hash identifies the latter as captured hull shader
`f916abafa71dddf5`. Its source is absent from the post-run compute-recipe
inventory. The translated tessellation preparation path called the device's
compute-pipeline convenience API directly, bypassing `ShaderCompiler` recipe
persistence. It now passes an otherwise-default descriptor through
`ShaderCompiler::ComputePipeline`; native tessellation's early path and the
translated buffers, resources and dispatches are unchanged. This corrects the
cache route but does not remove an unseen shader's first-use work in the
current launch. Live persistence/replay validation follows the candidate runs.

Optional address-only PS and VS root paths retain GPU-time descriptor loads
through the existing guest buffers, with per-root fallback and late consumer
leases. They do not copy descriptor values to CPU snapshots or introduce
overlapping Metal resource arguments. Full six-MRT differentials for both
material programs and full-output differentials for all six captured vertex
programs pass, including queued GPU descriptor/payload/base/format changes.
The real native-indirect fixture also passes with both options enabled. These
checks do not replace the pending actual-renderer post-cut lease integration
test or a positive live performance result. Both options remain OFF.

The optional `BB_METAL_WRITER_PRUNE_EPOCH=1` skips registry compaction while
its shared completion epoch is unchanged. It leaves current content, callback,
CP and mapping checks intact. Conditional completion probes prevent a retained
completed writer from qualifying as a pending producer. Deterministic race,
saturation and lifetime fixtures exercise the same production gate. With this
option enabled, the updated suite passes 93 of 95 tests in 111.42 seconds,
with the same two expected skips. The separate original tessellation capture
fixture also passes its LS/HS and domain-output comparisons.

The next frozen executable is
`62248f2db09d2be8a1112c886f25a8a84dd258dad6c593968584d699b3dd8d06`.
Additional candidate comparisons, live cache replay and final original-file
integrity results will be recorded after the remaining controlled runs complete.
