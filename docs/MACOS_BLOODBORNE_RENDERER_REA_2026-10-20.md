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

The new behavior remains gated by native indirect, texture coherence and
`BB_METAL_GPU_WRITTEN_TEXTURE_SNAPSHOT=1`. Default flags have not been changed.

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

Final performance, broad tests and original-file integrity results will be
recorded after the remaining controlled runs complete.
