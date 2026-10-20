# Metal texture streaming and indirect execution, 2026-10-20

Target: native 1920×1080, temporal AA and native motion vectors, direct Metal.
Original game files and master saves remain outside the implementation and
test writes; gameplay uses cloned saves.

This follows `MACOS_METAL_GPU_STALLS_2026-10-20.md`. The starting executable is
`1fb49b673312fe7847e6f0b32f8e14202f93cd9223b68b54c782577b5c83533e`.
Research, captures and test logs live in
`out/metal-indirect-streaming-2026-10-20/`.

## Investigation questions

- Which texture-upload stages cause the transition bursts, and can Metal
  encode the same work with fewer dispatches while retaining immutable source
  snapshots, hashes, exact mip/slice coverage and queued-frame coherence?
- Can GPU-generated indirect arguments reach native Metal execution without
  CPU draining, while preserving guest register writes, argument lifetimes,
  producer ordering and zero-count semantics?
- Which indirect cases require the existing drained fallback?
- Do GPU fixtures, native-1080p replays and movement/area-transition tests
  demonstrate correctness and a repeatable performance benefit?
- Do original-file and master-save integrity checks still pass afterward?

GPU timings with instrumentation are diagnostic measurements. Clean actual
presentation intervals, including missing presentations, determine throughput;
submitted-frame counts alone do not establish FPS.

## Fresh control evidence

`stream-indirect-control` runs 110 seconds of movement with a Dream transition
at 60 seconds. CPU-stage and presentation diagnostics are enabled; command
auditing and shader dumps trigger after the transition, without GPU cuts.
It displays 4,724 of 4,726 submissions with no GPU failures or fence timeouts.
The maximum display gap is 133.333 ms. This is not a clean FPS benchmark.

Its two largest CPU frames combine streaming and indirect dependencies.
Frame 7864 has 483 uploads costing 58.712 ms and a 37.469 ms indirect drain;
frame 7880 has 164 uploads costing 66.424 ms and 22.303 ms of indirect waiting.
Across these two frames, GPU-upload source copying costs 41.464 ms and hashing
44.605 ms. Avoiding Mach calls can remove their overhead, not all copying.

`dream-indirect-control` separately loads Dream before measurement and captures
steady gameplay. In its 20–45 second interval, 727 complete CPU frames have
one indirect draw and one indirect dispatch each. Their argument drains average
21.856 ms per frame: 19.195 ms for draw arguments and 2.681 ms for dispatch.
Mean CPU frame wall is 32.238 ms, with no uploads or shader/pipeline setup in
this interval. These are wall-time waits for real GPU work, not an equivalent
amount of removable GPU execution or a prediction of FPS gain.

All 17 captured steady-Dream indirect draw packets have raw words
`c0032400 0 0 0 2`. Both shader-register destinations are zero, allowing a
guarded native path without deferring arbitrary guest register writes.
The captured graphics dispatch uses SET_BASE selector 1, an argument offset
and initiator 1; other packet forms require separate validation.
Exact command streams, generated shaders and analysis are retained in the
control directories and `independent-control-analysis.json`.

## Implemented candidates and correctness

The native indirect path admits the observed graphics SET_BASE(1) packet forms.
Nonindexed draws require zero SH register destinations, standard Metal topology,
and exact existing initialized attachments with no pending clears or mip-parent
aliases. Indexed draws, other packet forms, register-writing draws and special
count-dependent compute paths retain the drained fallback. Unknown draw counts
never establish native-motion provenance. Indirect instance count zero remains
zero, independently of NUM_INSTANCES.

GPU-generated draw arguments are adapted on the GPU into a dedicated immutable
Metal argument buffer, preserving the existing port's vertex/instance ABI.
Generic dispatches GPU-copy their three argument words into a separate immutable
buffer before consumption, including when the shader overwrites its argument
source. Argument reads are tracked until completion. CP writes check physical overlap, including
different guest aliases. Pending CPU fence callbacks also participate in
admission, because queue order alone does not order a later CPU callback store.

Paired runtime mutation hooks close admission before mapping changes, drain
pending argument reads, and retain the renderer lock until the VMA write lock
has been released. A failed bounded drain refuses the mapping change. Hook
registration belongs to startup/teardown with writers stopped. Registration
leases preserve callback lifetime; nested mappings share the outer guard.
The renderer metadata lock is recursive for lazy arena allocations inside an
already locked renderer operation; GPU queue ownership is unchanged.

Native admission also proves that CPU-resolved image/sampler descriptors,
texture upload bytes and constant snapshots have no outstanding GPU or CPU
fence writer. Queued buffer writers carry bounded physical-range completion
tickets; unknown extents conservatively reject CPU resource reads. Mutable
table-loaded writer descriptors remain unknown even if their current address
can be read. Direct user-data words have immutable Metal `setBytes` ownership.
Array/3D upload cases and uncertain mip aliases retain the drained path.
This closes a dependency that simply removing argument waits would miss.

The optional world-frame limit waits on the producer after presentation has
been committed and acknowledged. It allows at most two unfinished world frames,
without draining the whole GPU or waiting inside the presentation callback.
It retains the existing three presentation slots. The limit is experimental,
bounded by a ten-second failure deadline, and disabled by default.

Texture mip batching packs validated mip/slice jobs into one compute dispatch
per eligible upload, retaining exact copies, hashes and blits. The separate
mapped-snapshot option validates the full readable runtime range under its
read lock and copies via the backing alias, including physical discontinuities.
It writes no destination bytes on rejection and retains the checked Mach-copy
fallback for other sources. Neither option defers a source snapshot or changes
queued texture-version ownership.

The first `d3831453` build passes eight focused tests and the 78-test suite.
After adding the resource-producer guard, immutable dispatch snapshot and
world-frame fixture, `538dcfe9` passes ten focused tests and the 80-test suite:
78 pass, two expected private-input skips, zero failures in 113.60 seconds.
Logs: `resource-guard-build-2.log`, `resource-guard-focused-tests-2.log`
and `resource-guard-full-ctest.log`. Later validation below identifies the
final executable separately.

Native fixtures exercise production PM4 decoding, translated compute and real
Metal draws: GPU-produced argument versions, zero X/Y/Z counts, nonunit 3D
group IDs, CP writes through physical aliases, pending CPU fence writes,
blocked-queue unmapping and recursive lazy arena allocation. Snapshot fixtures
test whole-range validation, permissions, gaps, aliases, remapping stress,
nested/refused mutations and callback lifetime. All four batching/mapped-copy
combinations pass exact raw texture and hash comparisons, including queued
versions, all 21 supported formats, 512 jobs and metadata-page rollover.

## Streaming measurements

Both candidates remain experimental until timing supports enabling them.
The combined run `stream-upload-candidate` produces mixed results. For an
exact 862-upload/188.353 MiB startup cohort, encoding changes from 112.357 to
134.830 ms. An exact 683-upload/252.761 MiB Dream cohort improves from 125.064
to 116.173 ms. Whole-trace encoding cost per GiB worsens by 4.97%; texture mix
and timing differ, so this normalization is not causal proof of a regression.

`stream-batch-only` disables mapped copying. Its exact startup cohort takes
121.782 ms. Removing measured copy/hash/allocation time leaves 35.581 ms of
other work versus 37.336 ms in the control, a small API-side saving. Another
exact 185-upload/48.342 MiB cohort changes from 33.783 to 31.365 ms, mostly
explained by copy/hash variation. Dream upload quantities differ, preventing
a direct comparison of its main burst. A general streaming speedup is not
established; lower dispatch counts alone are insufficient.

Copy and hash timings must be considered together: mapped copying can warm
the snapshot and shift page/cache cost into the copy scope. The combined
startup copy/hash total worsens from 74.287 to 91.346 ms. Detailed cohorts and
limitations are retained in `upload-streaming-analysis.json` and its script.

## Native indirect diagnostic

`dream-native-diagnostic` enables only native indirect, with both streaming
options off. The 45-second run takes one intrusive quality capture at 20 seconds.
The inspected native-1080p frame is `dream-native-diagnostic/dream-native.png`;
a still image is not proof of flicker-free animation or pixel-identical frames.

In the steady 25–45 second window, all 851 presentations have timestamps and
no GPU error is recorded. The two per-frame indirect argument drains disappear.
Mean CPU frame wall falls from 32.238 to 19.042 ms; draw/dispatch recording
remains about 8 ms. Actual counter deltas match one native draw and one native
dispatch per resolved frame. There are no argument-protection drains in this
window, with six pending reads reported. Useful recording work still exists;
removing 21.9 ms of waits does not imply that entire amount becomes FPS gain.

Presentation commit-to-GPU-start averages 41.808 ms, versus 0.213 ms before;
submitted-to-display averages 62.700 versus 41.780 ms. The fullscreen presenter
GPU work remains around 0.3 ms. This is an increased queue of world GPU work,
with CPU presentation commits still preceding their deadlines. This diagnostic
throughput increase and latency tradeoff require clean validation.
All three whole-run missing timestamps cluster around the quality capture;
they remain in whole-run metrics. Detailed evidence:
`independent-dream-native-comparison.json`.

## Clean validation of the first prototype

The same `d3831453` executable, AC power, identical cloned Dream save/pose,
native 1080p and 90-second stationary route produce:

| Native indirect | Actual presented FPS | Missing presentations | GPU failures |
| --- | ---: | ---: | ---: |
| Off | 29.920 | 0 | 0 |
| On | 28.967 | 0 | 0 |

Both measurements meet the harness's clean timing criteria. The apparent
42-FPS diagnostic gain does **not** reproduce. Native execution must remain
experimental; eliminating CPU argument waits alone is not a verified FPS gain.
Evidence: `native-pair.json` and `dream-clean-pair-review.json`.

`dream-native-gpu-investigation` then uses continuous pass cuts, rather than
the intended trace-only cuts. Its 39.609 FPS is diagnostic only. The cuts raise
command commits from roughly 13 to 267 per frame and change queue behavior.
The top sampled duration families are main MRT (11.84 ms summed per frame)
and lighting (7.81 ms); overlapping command durations cannot establish critical
GPU execution time or explain the clean regression causally. The native pair
is point-light VS `c0ad4a71b34d5ee5`/PS `6d7fab94bbe631d4` and compute
`aaf9b6ea6e525713`. Trace dispatch groups are placeholders until actual completed
argument words are sampled. Details: `dream-native-gpu-profile-analysis.md`.

## Guarded-path investigation

The first conservative resource guard (`a868b175`) measures 29.741 FPS with
native indirect off, with no missing presentations. Its native-on run measures
20.388 FPS, with three missing presentations and no GPU failures. This second
run fails the harness's strict clean-presentation criterion. More importantly,
the native draw count remains zero and the dispatch count stops increasing in
Dream: unknown writer tickets reject the intended optimization. These results
do not justify enabling the path. Full comparison evidence is retained in
`guarded-native-comparison.json`.

Review identified avoidable work in this conservative version: scanning all
instructions of every shader to discover stores, including shaders with no
reflected buffer writes. Storage images bind private Metal textures rather
than guest no-copy buffers and can be distinguished from guest buffer writes.
Later validation must cover both the narrower classification and the remaining
fallback cases, including GPU-written shader code and zero-count target aliasing.

The queue-limited guarded run finishes at 28.962 FPS with no missing
presentations or GPU failures, but still never admits the intended Dream native
draw/dispatch pair. It does not establish a native-execution improvement.

## Revised proof and recording cost

The revised executable is
`d6e956c1bd556408a4ca3eb8644afd3b71620ff1899f1e6406abb227795f8df4`.
Shaders with neither reflected buffer stores nor unsupported instructions skip
writer scanning. Storage-image stores are separately proved to write private
Metal texture allocations/views; raw buffer stores in the same shader still
require physical-range tracking.

Native execution requires an already cached program. Complete code and fetch
byte spans are checked against pending producers before comparison; cold or
mutated code falls back without decoding. A wholly CPU-readable span outside
all DirectMappings cannot alias the exposed direct-memory pool: low mappings
are anonymous private pages, and flexible backing starts above that pool.
Partial DirectMapping overlap and the host pool backing view remain rejected.
Pending CP fence checks precede this negative-alias proof.

Zero-count draws also reject cross color/depth attachment aliases. Sampled
private targets must match the selected latest writer and use mip zero without
growth or child-surface migration. Optional static/direct SMRD CPU packing
retains drained fallback.

The expanded GPU fixture covers these boundaries with real producers and
pixels. Its first run caught an outdated expectation: a mapped shader is now
itself a CPU resource, so an unknown buffer writer must reject even a shader
without sampled images. The corrected test verifies fallback output and
separately proves admission for independent anonymous code. The revised
indirect CPU/GPU tests pass; other focused memory/streaming/queue tests pass.
Evidence: `native-proof-full-build.log`, `native-proof-focused-tests.log`
and `native-proof-indirect-tests-2.log`.

## Descriptor lifetime checkpoint

The descriptor-lifetime candidate executable is
`f99b45acfbf69767d162fbc5c0aae9180533b881c734ea0f01e02dd5046b0bbc`.
Pointer-loaded producer descriptors can establish a precise write range only
after their complete words are stable against earlier GPU/fence writes and
their future CPU-read lifetime is protected. Coalesced guest/physical spans
share a completion ticket on the actual producer command. Later CP writes
through the same address or a physical alias, and runtime mapping mutations,
wait for those reads. The registry allows at most 64 spans per producer and
512 outstanding spans; overflow retains unknown-writer fallback. Zero-record
tables also retain protection so a later CPU write cannot activate them early.

Unaligned descriptor roots, self-modified tables, uncertain extents and
two-stage writers remain conservative. Linear scalar-register provenance
cannot establish definitions across branches, so branched buffer-writing
programs also remain unknown at this checkpoint. The fixture includes a branch that selects
runtime descriptor A while linear analysis predicts B, and proves the guard
does not incorrectly admit the affected resource.

Actual translated LS/HS programs now use their reflected writes and patched
user data rather than a blanket unknown ticket. Their separate LDS/factor
outputs do not alias guest memory. Empty-store stages no longer block native
setup. The capture audit attributes 7.534 of 8.796 unknown tickets per frame
to the former blanket. The actual compute shader `aaf9b6ea6e525713` contains
220 branches and 22 pointer-loaded stores, so this checkpoint leaves it unproved.
Tiny branched direct-UD kernels may support a future register-preservation
proof; this checkpoint does not assume one.

Ten focused tests pass in 8.71 seconds, including producer-table same-address
and alias overwrites, alias unmapping, zero-record activation, unaligned roots,
self-modification and capacity exhaustion. Independent source review found no
remaining blocker in these guards. Evidence: `descriptor-proof-build.log`,
`descriptor-proof-focused-tests.log`, `unknown-producer-analysis.md`.

Its unprofiled AC measurements are 29.778 FPS off (one missing presentation),
22.618 FPS on, and 27.835 FPS on with limit two (both zero missing).
No GPU failures occur. No native Dream draw is admitted in this version.
These are negative results, not evidence to enable the path.

## Bounded control-flow proof

The next executable is
`63f5b5a289fe4e17d4380f1ecdc4ff02d9a8fda0637d63edd322b90932148e52`.
`native_writer_proof.h` independently proves reaching definitions for each
store's descriptor words. Reverse walks cover both branch arms, entry paths,
alternate definitions and loop backedges. Pointer descriptors require one
exact X4 load, with original entry root words at that load. Direct descriptors
require unchanged original user-data words. Indirect control flow, inlined
fetches, incomplete graphs, wider/mixed loads and uncertain scalar writes fail
closed. This check applies to every buffer-writing program, including straight
line code where the old linear provenance can miss a scalar clobber.

The check caches per program and caps instructions at 8,192, stores at 256,
queries at 1,024 and visited nodes at 1,048,576. Runtime source-footprint,
producer, CPU-write lifetime, self-alias and write-extent guards still apply.
No game code or shader instruction is patched for this proof.

Public adversarial CPU fixtures pass. The production C++ helper also accepts
the captured retail inputs with hash verification: `aaf9b6ea6e525713` has
22 stores, 92 queries and 20,728 visits; the two small kernels each have one
store and 52/68 visits. Retail bytes remain in ignored `out/` captures, outside
source fixtures. All 11 focused CPU/GPU tests pass in 6.57 seconds.
Evidence: `flow-proof-private-*.log`, `flow-proof-focused-tests.log` and
`flow-proof-full-build.log`.

`dream-flow-quality` confirms native indirect compute in live Dream gameplay;
completed immutable arguments include 45–52 groups in X and one in Y/Z in its
late window. Native draw remains on fallback: shader `26ec0808a6cd92dc` has an
unproved dynamic indexed byte-offset write, so it still creates an unknown
writer ticket. That safety restriction must not be silently dropped to obtain
a faster number. One captured image is a correctness spot check, not proof of
flicker-free animation. The run records zero GPU failures.

This quality run is on battery at 98–96% and includes a raw capture; its FPS
is not comparable to the earlier AC measurements. Two later AC comparison
startups refuse to run because the unrestricted power check reports battery.
The sandboxed `pmset` query misleadingly labels that condition AC; future
power validation uses the same unrestricted environment as the game.
