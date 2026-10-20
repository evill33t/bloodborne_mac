# Native 1080p GPU stalls and throughput, 2026-10-20

Target: direct Metal, native 1920×1080, temporal AA and native motion vectors,
with the original game dump and master saves untouched. All gameplay runs use
cloned saves. This continues the fixes recorded in
`MACOS_METAL_REFINEMENT_2026-10-19.md`.

## Fresh profiling

Reference executable SHA-256:
`af4a3f9bcb8ba768672509c77abd5df7343465a4ddeb713bc3e20eadf587be06`.
Artifacts live under `out/metal-refinement-2026-10-19/`.

`gpu-stalls-baseline` takes three stationary per-draw GPU captures. The two
material fragment shaders remain the largest families: `68fcb859536adcd4`
accounts for about 11 ms across 220 draws and `aff190d9945a53e0` about 9.4 ms
across 137 draws. These are intrusive measurements with a command buffer cut
per draw; their roughly 60–67 ms total is not normal gameplay frame time.

`gpu-pass-drop-baseline` takes three pass-level captures in a 90-second replay.
The main material render target costs 8.9 ms across 13 passes in each capture;
total captured GPU time is 23.5–23.7 ms. Lighting costs 2.0–2.7 ms, the next
two-pass target 1.6 ms, and eight typed-fill operations 1.4–1.6 ms. The typed
fills already use native Metal clear/fill paths. Pass cuts still disturb GPU
scheduling, so these timings rank targets rather than establish FPS gains.

The replay presents 4,733 of 4,733 submitted frames, with no Metal errors or
GPU fence timeouts. Excluding the first 20 seconds containing the profiling
captures, the actual display intervals have median 16.667 ms, p99 about
25.002 ms and maximum 41.667 ms. This is a diagnostic run, not a benchmark.

Two 41.667 ms gaps show normal handoff spacing but presentation work waits
50.998 and 57.545 ms on the shared GPU queue; it finishes 9.941 and 16.379 ms
after the target presentation time. The fullscreen presentation pass itself
takes only about 0.319 ms median, at most 0.586 ms. Another 41.667 ms gap has
33.561 ms handoff spacing while the GPU finishes 40.610 ms before its target;
that gap cannot be assigned to late GPU execution. All measured presentation
commits arrive before their CPU deadline. These observations support reducing
render work; they do not justify weakening fences or changing frame ownership.

## Candidate changes

The native vertex fetch candidate adds four exact whole-program identities
whose decoded fetch clusters match the two previously tested layouts. Each
identity retains its own exact VCC prefix literal. The existing opcode,
register, mapping, descriptor, record-boundary and physical-extent guards stay
in force, with the original typed fetches as fallback. The Dream foliage
shader does not enter this path. `BB_METAL_NATIVE_VERTEX_FETCH_EXTENDED`
selects the extension independently of the original two native fetch shaders.

The rejected material constant-load candidate reused a GPU-time address translation for
four exact scalar load instructions in each of the two hot fragment shaders.
It validates the complete 688-byte window, keeps payload reads at their
original instructions and falls back to the original individual loads at
mapping or physical boundaries. It adds no CPU snapshot or GPU binding.
It was tested with `BB_METAL_MATERIAL_SMRD_SPAN=1`, then removed after the
negative performance result below. That switch is not part of the final port.

The extension admits 94 additional draws per stationary capture: 66 for
`24dbf327f8c6d151`, 20 for `9cd63b2566180f01`, six for
`529745c8c9ccb660`, and two for `85043c7598c4ad8c`. Across the three captures,
this adds 26.8% to candidate draw admission and 9.6% to candidate index counts.
Admission counters do not measure actual GPU fast-path hits or time saved.

## Validation

The first clean, diagnostics-off control, `gpu-opt-control-live`, records
52.012877 presented FPS over 90.208 seconds, with all 4,692 submitted frames
displayed. P99 display interval is 25.001462 ms, maximum 33.333417 ms, no
interval exceeds 50 ms, and no GPU failure is recorded. Resolution, temporal
AA and native velocity input checks pass. This is the pre-change executable
identified above, with the same cloned-save starting pose and replay route.

The expanded native vertex fixture passes 418 synthetic GPU comparisons and
418 full-output comparisons using the six private captured shaders. It tests
both guarded fast paths and original fallbacks, record boundaries, mapping
gaps, physical buffer boundaries and ordered GPU changes to source records,
descriptor tables and formats. The CPU proof rejects 32 mutations per shader,
including a prefix transplanted from another known identity. Logs:
`native-fetch-extension-tests.log` and
`native-fetch-extension-captured-tests.log`.

The material span prototype fixture passed 31 raw GPU mapping/extent cases (13
fast paths and 18 fallbacks) and 32 full-fragment states for each original
shader. Six MRT outputs match the original GPU path. Ordered GPU writes to
nested descriptors and payloads remained observable; both fast and fallback
paths were asserted. Independent review identified a validation limitation:
uniform payload values did not distinguish each of the 19 selected words.
The prototype was rejected on performance rather than promoted on this test.
CPU negatives covered changed load instructions, descriptor
clobbers and branch entry inside the reuse interval. Log:
`material-span-captured-tests.log`.

Initial diagnostics-off replay results:

| Run | Extended fetch | Material span | Presented / submitted | Actual presented FPS | P99 / maximum interval |
| --- | --- | --- | --- | --- | --- |
| `gpu-opt-control-live` | Off | Off | 4,692 / 4,692 | 52.012877 | 25.001 / 33.333 ms |
| `gpu-opt-extended-fetch` | On | Off | 4,932 / 4,932 | 54.673415 | 25.000 / 33.333 ms |
| `gpu-opt-control-repeat` | Off | Off | 4,727 / 4,728 | 52.405712 | 25.000 / 41.667 ms |
| `gpu-opt-fetch-and-material` | On | On | 4,825 / 4,825 | 53.502065 | 25.000 / 41.667 ms |
| `gpu-opt-final-fetch-benchmark` | On | Removed | 4,819 / 4,823 | 53.443064 | 25.000 / 58.339 ms |

The first comparison improves presented throughput by 5.12%. The repeated
control uses the same executable as the candidate and still shows lower
throughput, but its one missing presentation timestamp disqualifies it from
the harness's strict all-frames-presented benchmark criterion. Its FPS is
actual displayed throughput, not a claim that all submitted frames displayed.
None reports a GPU error. The later fetch-only repeat has four missing
presentation timestamps and two intervals above 50 ms, so it also fails the
strict benchmark criterion. These initial results suggested a modest throughput
gain, but the later AC comparisons below do not reproduce it consistently.
They do not establish sustained 60 FPS or eliminate dropped frames.

The material span result is 2.14% slower than extended fetch alone. Its
production helper, eligibility proof, option, renderer wiring and test changes
were removed. The experiment is retained only in ignored research artifacts.

Extended vertex fetch is **opt-in** in the final port. Set
`BB_METAL_NATIVE_VERTEX_FETCH_EXTENDED=1` to test the four additional
identities. Leaving it unset retains the original two optimized shaders.
The existing
`BB_METAL_NATIVE_VERTEX_FETCH=0` disables all native vertex fetch paths.

The fetch-only executable SHA-256 is
`a407a1ec30f19aae6a01066c9d700d6b3dff2a6b233621fad2710c00d9d3b3e2`.
The full build passes. CTest registers 71 tests: 69 pass and two expected
private-input tests skip (`native_tessellation` and `native-motion-capture`),
with zero failures in 106.26 seconds. The final native fetch fixture's six
private captured shaders pass another 418 full-output GPU comparisons after
the prototype removal and rebuild. Logs: `gpu-opt-final-build.log`,
`gpu-opt-final-ctest.log`, and `native-fetch-final-captured-tests.log`.

## Transition run and remaining cleanup work

`gpu-opt-long-gameplay` exercises 330 seconds of movement, camera rotation,
combat inputs and four successful lamp transitions across Central Yharnam,
Hunter's Dream and Cathedral Ward. Position samples confirm movement in all
three areas. There is no recorded crash or GPU failure; the three label waits
total 1.7 ms parked with zero timeouts. This run includes GPU pass cuts and
presentation diagnostics, so its aggregate FPS is not a benchmark.

It displays 16,118 of 16,141 submitted frames. All 23 zero-timestamp drawables
finish GPU work 38.6–41.1 ms before the estimated target. The two largest
visible gaps, 1,254 and 1,229 ms, overlap 22 display-link-unavailable fallback
polls in two groups. Their cause is not established; they must not be reported
as GPU-late drops, nor hidden from the actual display-gap data.

The run exposes three texture collection batches of 10.421, 10.517 and
12.086 ms, around 20 seconds after area transitions. The latter two contribute
to 41.667 ms visible intervals. After fallback acknowledgements, renderer and
presenter sequence numbers diverge; these associations use timestamps rather
than assuming frame numbers remain identical. This motivates bounded texture
retirement and separate scan, policy and destruction timings.

Other 108–158 ms display gaps show longer upstream handoff spacing with the
presentation GPU ready early. Recorded scopes explain only part of them;
command-processor dependency timing was disabled in this run. The texture
cleanup change must not be claimed to resolve all of those gaps. Exact data
and limitations: `gpu-opt-long-gameplay/long-run-attribution.md`.

## Bounded texture cleanup

The final cleanup implementation caches each Metal texture's allocated size
when it is created or replaced. Discovery retains the existing two-second
pressure grace and 20-second idle expiry. Instead of destroying the entire
expired set immediately, it inspects at most 32 metadata tickets per Flip.
Version and last-use checks cancel tickets after replacement or reuse, and
live age and memory pressure are checked again before removal. Texture
destruction stays on the renderer thread. No additional Metal ownership or
GPU waits are introduced; render targets are excluded as before.

The cap includes canceled or missing tickets. Pending backlogs can retain
idle textures longer than immediate removal; this is a soft cache budget,
not a hard memory ceiling. `BB_METAL_BOUNDED_TEXTURE_RETIRE=0` restores batch
removal. Both flag values use cached allocation sizes, isolating the deletion
schedule in the comparison. `BB_METAL_TEXTURE_RETIRE_DIAGNOSTICS=1` enables
split scan, selection and retirement timings; ordinary timing thresholds stay
unchanged when it is off.

The rebuilt executable SHA-256 is
`93853eee1626633771d61b05b10ac9d101532865601f82f4bc2f48b8ee435b8d`.
The full build and CTest pass again: 69 passing tests, two expected private-input
skips, zero failures, 109.43 seconds. New CPU fixtures cover bounded completion
and order, budget and grace changes, reuse and replacement, missing/new
entries, stale inspection limits and long gaps. Existing GPU texture coherence
and resource tests also pass. Logs: `texture-retirement-full-build.log` and
`texture-retirement-ctest.log`.

`texture-retirement-control` uses this same executable with bounded cleanup
disabled, with frame-work, presentation and split cleanup diagnostics enabled.
After the Dream transition, collection at 82.342 seconds takes 10.305 ms;
9.887 ms is retirement of 1,101 textures totaling 375,392,256 bytes. Scanning
and selection each remain below 0.4 ms. This establishes batch destruction as
the dominant cost in that event, rather than inferring it from one combined
scope. Power snapshots record battery operation; this is a diagnostic control,
not an FPS benchmark.

`texture-retirement-long` completes another 330-second movement route and four
successful area transitions on this build. It presents 14,645 of 14,647 frames,
with no GPU failures, no unavailable-drawable fallbacks, and three label waits
totaling 1.4 ms parked with zero timeouts. The two zero-timestamp drawables
finish GPU work 40.509 and 41.087 ms before their targets. They remain reported
as dropped presentations, without assigning them to late GPU execution.

Across 272 retirement steps, 6,764 entries are removed and no step exceeds
32 entries. The maximum retirement time is 2.459 ms, during early measurement;
the matching Dream phase peaks at 0.928 ms versus 9.887 ms in the control.
Total measured retirement time in that phase increases from 13.072 to
19.061 ms across more, smaller steps. This is a reduction in the burst size,
not proof of lower total CPU cost or higher FPS.
Scanning/selection peak at 0.476 ms, and no whole collection reaches the
ordinary 10 ms reporting threshold. This verifies the count bound and removes
the measured mass-destruction hitch. The bound is on records, not bytes or
wall time: the largest step releases 130,636,672 bytes, and one expensive
driver release could still exceed these observed timings.

Sampled process RSS starts at 3,741,924 KiB, ends at 3,914,004 KiB and peaks at
4,127,532 KiB. This includes the runtime, shaders and diagnostics; it is not a
proof of a hard Metal allocation ceiling or indefinitely flat memory use.
The longer run changes from battery power to AC while running, so its FPS is
excluded from performance comparisons.

## Remaining stalls

The added command-processor timing explains the larger upstream stalls. In the
100-second control, 3,422 indirect-argument drains accumulate 22.18 seconds of
wait time after entering Dream; nonindexed indirect draws account for 19.09
seconds. These are cumulative wall times and may overlap other work. Removing
the drains without preserving GPU argument producers and base-vertex/start-
instance semantics is unsafe. A native Metal indirect path is the next
architectural target, rather than skipping the dependency checks.

The final long run's largest 183.334 ms visible gap contains a 179.309 ms
renderer frame: draw recording takes 153.409 ms, including 852 texture uploads
costing 114.427 ms, and indirect dependency waiting takes 19.729 ms. Another
158.334 ms gap contains 963 uploads costing 113.160 ms. A later 141.667 ms gap
contains aggregate shader/pipeline variant preparation; the largest individual
pipeline wait is only 2.850 ms. Bounded retirement does not resolve these
streaming and preparation costs. Exact attribution is retained in the control
and long-run analysis artifacts.

## Final AC presentation comparisons

All four clean replays below use the same `93853eee` executable, cloned-save
starting pose, native 1920×1080, temporal AA and native motion vectors. Bounded
texture retirement stays on. Each measurement lasts 90 seconds: 20 stationary,
20 camera orbit, then 50 combat inputs. GPU cuts, frame-work and presentation
diagnostics, screenshots and video are disabled. Start/end snapshots confirm
AC power. All submitted frames receive positive presentation timestamps; none
records a GPU failure.

| Run | Extended fetch | Presented / submitted | Actual presented FPS | P99 / maximum interval |
| --- | --- | --- | --- | --- |
| `gpu-opt-ac-control` | Off | 4,669 / 4,669 | 51.767596 | 25.004 / 41.668 ms |
| `gpu-opt-ac-optimized` | On | 3,016 / 3,016 | 33.430576 | 41.667 / 50.000 ms |
| `gpu-opt-ac-control-repeat` | Off | 4,744 / 4,744 | 52.618582 | 25.000 / 33.333 ms |
| `gpu-opt-ac-optimized-repeat` | On | 4,325 / 4,325 | 47.940060 | 25.002 / 33.333 ms |

The extension therefore fails the throughput acceptance criterion despite
passing GPU equivalence tests. The severe 33.43 FPS result remains unexplained
and is retained in the results. It has no matching reported CPU cleanup or
pipeline stall large enough to account for the sustained slowdown. No measured
GPU-clock data establishes a hardware cause, and background application
activity is not proof of one.

The intervening 45-second stationary diagnostic,
`native-fetch-ac-investigation`, presents all 2,376 frames at 52.673116 FPS
with the extension on. Its three pass captures put the main material target
at about 8.1 ms across 13 passes, versus 8.2–8.4 ms in the earlier long capture.
These cuts disturb scheduling, so this is not a clean benchmark. All 273
matching generated shader files, including the six fetch variants and hot
fragment shaders, are byte-identical between these captures. No shader-source
or cache-key regression was found; candidate admission counters still do not
measure actual GPU helper hits. Audits and source hashes are retained under
`native-fetch-ac-audit/`.

The final renderer consequently leaves the four new identities disabled
unless explicitly enabled, while keeping the original two native fetch paths
and bounded texture retirement on. It also excludes the slower material-span
prototype. This work fixes a measured cleanup burst and improves attribution;
it does not establish an FPS gain from the experimental fetch extension or
meet the sustained 60 FPS target. Streaming uploads, shader-variant preparation
and GPU-produced indirect-argument dependencies remain the next targets.

## Final default build

The final executable SHA-256, after making the extension opt-in, is
`1fb49b673312fe7847e6f0b32f8e14202f93cd9223b68b54c782577b5c83533e`.
The full build succeeds and CTest completes in 104.99 seconds: 69 pass,
two expected private-input skips, zero failures. Logs:
`gpu-opt-release-default-build.log` and `gpu-opt-release-default-ctest.log`.
The only code change from `93853eee` is the extended-fetch option's default;
the bounded cleanup implementation is unchanged.

`gpu-opt-release-default` tests that exact executable with both the extended-
fetch and bounded-retirement switches **unset**, exercising the actual defaults.
The clean 90-second native-1080p AC replay presents 4,612 of 4,614 submissions:
51.135580 actual presented FPS, median 16.667 ms, p99 25.001 ms and maximum
75.000 ms. The two missing presentation timestamps disqualify it from the
harness's strict benchmark criterion. Its actual displayed throughput and
gaps remain reported. There are no GPU failures or unavailable-drawable
fallbacks, and the one label wait parks for 0.9 ms with no timeout. The game
exits through the harness's planned SIGTERM. With detailed presentation timing
off, this run does not establish the cause of the two dropped presentations.

Final integrity verification passes after all runs: all 28,779 original dump
files retain their size/mtime inventory, the original `eboot.bin` SHA-256 remains
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`,
the prepared original ELF remains
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`,
and all 16 master-save file hashes are unchanged. Only cloned saves are used
by the gameplay harness. Evidence: `gpu-opt-final-integrity.log`.

`git diff --check` also passes. No original game-file patches, power-setting
changes or modifications to unrelated running applications were made.
