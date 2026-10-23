# Metal command scheduling investigation, 2026-10-22

The target remains native 1920×1080 gameplay toward 60 FPS. The Data drive
was reconnected and matched gameplay tests ran on cloned saves. The elapsed
command budget is implemented and tested, but **remains off by default**:
neither 2,000 nor 8,000 us established a repeatable FPS gain. The hash alternative
was also rejected before renderer integration. No 60 FPS result is claimed.
Original dump, original prepared ELF and master saves remain untouched.

Work is paused at the user's request after the completed runs. The later
cross-architecture shader-source import is unfinished: both final builds failed
in its new test fixture, and that change has no passing regression or gameplay
validation yet. No further runs are scheduled today.

## Profiling evidence

The retained `rea21-gpu-full` capture covers 1,258 measured Dream frames.
Its render-stage interval union is 20.137 ms/frame, with substantial time in
mixed geometry/material passes writing six color attachments. Inclusive CPU
draw recording is 18.491 ms/frame, including 7.355 ms of texture validation.
Those CPU scopes nest; they are not added together. See
[the original report](MACOS_BLOODBORNE_RENDERER_REA_2026-10-21.md).

New analysis of that retained command timeline found:

| Observation | Result |
| --- | ---: |
| Successful renderer commands | 16,766 |
| Commands per measured frame | 13.328 |
| Union of command GPU intervals | 23.068 ms/frame |
| Gaps between those intervals | 5.616 ms/frame |
| Gap portion before the following CPU commit | 5.023 ms/frame |
| Commands with more than 5 ms of CPU recording | 1,267 |
| Median recording time of those long commands | 16.631 ms |

Almost every frame contains one long-lived open command buffer. This motivates
publishing completed encoders while the CPU records remaining work. The log
only covers instrumented renderer commands; these gaps do not prove that the
entire GPU was idle. GPU intervals include dependencies and overlap. Last-shader
labels are command endpoints, not attribution of all mixed work. These are
yesterday's diagnostic observations, not a fresh baseline or current clean FPS.

## Opt-in implementation

`BB_METAL_COMMAND_BUDGET_US=2000` adds an elapsed recording-time budget to the
existing 400-work-item limit. It remains **off by default** because gameplay
testing did not establish a repeatable gain. Values 250 through 16,000 microseconds are accepted; missing, zero or
malformed values retain the original policy.

The check runs after a naturally required render-encoder end, before a new
render pass or compute dispatch, using the existing nonblocking commit path.
It never ends an otherwise reusable render pass to meet the budget. Explicit
submits, fences, CPU-read readiness, mapping checks, callback effects and
snapshot ownership retain their existing paths. Upload storage and constant
snapshot leases follow existing commit/completion lifetime rules. Empty command
buffers cannot inherit a previous command's timestamp. `DC_GPU_STATS=1` reports
cumulative budget-triggered commits. No shader, resolution or quality changes.

## GPU tests and synthetic benchmark

The production-renderer fixture checks that a reused pass stays intact; a
naturally ended pass and callback can reach the GPU before an explicit submit;
the disabled control leaves the callback queued; prepared CE snapshots survive;
fresh commands reject stale age; and every pixel in both targets remains correct.

The optional `--benchmark` warms and drains pipelines and targets, then times
16 natural one-color 1920×1080 passes with a requested 3 ms CPU pause per pass.
Presentation and readback are outside the timing. Actual 1920×1080 attachments
and final pixels are verified. The benchmark candidate budget is 1,000 us.

| Mode | CPU recording | Recording through GPU completion | Mean callback latency |
| --- | ---: | ---: | ---: |
| OFF, mean of two runs | 71.307 ms | 73.479 ms | 39.846 ms |
| ON, mean of two runs | 70.183 ms | 70.792 ms | 4.821 ms |

Order: OFF/ON/ON/OFF. Callback latency demonstrates earlier queue progress in
this controlled workload. Completion time includes CPU recording variability
and only two samples per mode. Neither result is gameplay FPS or full-scene
material cost. An initial benchmark accidentally used an 8×8 attachment with
1080p output; it and a stale-binary retry are invalid and excluded.

Before the later shader-cache import changes, the Rosetta build and serial suite with a 2,000 us budget passed:
101 tests passed, two expected private-input skips, zero failures, 122.53 s.
The arm64 game and raster fixture also built; the arm64 raster test passed with
the same 2,000 us budget. These tests do not establish long-session gameplay
stability or visual equivalence in the original scene.
They do not validate the subsequent cross-architecture cache changes.

## Reconnected-drive gameplay comparisons

One frozen Rosetta runtime was used throughout:
`db87c251b0950d97c4f9f69e95a8756a09b950a8566240390499c5afaa07e55f`.
All stationary comparisons use the same validated Dream map `15000000` and
pose `[-9.2, -6.770591, -19.4, -2.356194]`, native 1920×1080, TAA/native motion,
a 120 FPS cap and cloned saves. The existing extended tessellation, vertex
fetch and other experimental CPU/shader options are disabled equally.
No builds, CPU microbenchmarks or other GPU fixtures run during FPS recording.

| Run, 60 seconds each | Budget | Displayed FPS | Zero presentation results | GPU failures |
| --- | ---: | ---: | ---: | ---: |
| `pipe22-off-a` | OFF | 36.9013, excluded by strict harness | 1 | 0 |
| `pipe22-on-a` | 2,000 us | 31.4735 | 0 | 0 |
| `pipe22-on-b` | 2,000 us | 34.2833 | 0 | 0 |
| `pipe22-off-b` | OFF | 34.8346 | 0 | 0 |
| `pipe22-8k-a` | 8,000 us | 37.3899, excluded by strict harness | 1 | 0 |
| `pipe22-8k-b` | 8,000 us | 26.4688 | 0 | 0 |
| `pipe22-off-c` | OFF | 27.0289 | 0 | 0 |

All seven completed their planned stop on AC, with no GPU failure. The
2,000 us candidate adds roughly eight commands/frame (about 21 versus 13).
The less aggressive candidate and closing control both exhibit substantial
session drift. A contemporaneous process snapshot found QEMU, WindowServer,
terminal and system background activity; `pmset` reported no recorded thermal
warning. This is not proof of which process caused the drift. No unrelated
process or OS power setting was changed, and no default is selected from it.

The original harness rejects any zero `presentedTime`, even a single skipped
frame. Such a callback can mean a dropped/not-displayed drawable, as described
by [Apple](https://developer.apple.com/documentation/metal/mtldrawable/presentedtime).
The raw aggregate FPS already counts only displayed frames over the full elapsed
interval, including gaps. The existing strict exclusion policy is retained for
this selection; isolated good intervals are not extracted to claim a gain.
The harness enables slow-scope CPU timers even outside its full profiling mode;
that instrumentation is equal between budget modes and is checked separately.

## CPU timing overhead check

A separate OFF/ON/ON/OFF test changes only slow-scope CPU timing; command budget
stays OFF. All four runs have complete presentation results, no GPU failures,
planned stops, AC power and the same binary/map/pose at native 1080p.

| Slow-scope timers | Displayed FPS |
| --- | ---: |
| OFF A | 28.9090 |
| ON A | 27.7978 |
| ON B | 28.1003 |
| OFF B | 30.7811 |

Disabled mean is 29.8451 versus 27.9491 enabled, about 6.8 percent higher, with
remaining drift between the two controls. Removing diagnostic timers does not
change normal launches, where those timers are already opt-in. Later CPU-mode
comparisons remove these timers and extend the common post-load warm-up from
15 to 45 seconds. The older budget figures remain explicitly timer-instrumented;
they are not presented as fully uninstrumented production FPS.

## CPU-mode experiment: cache mismatch invalidates architecture comparison

The existing optional arm64/bbcpu integration was exercised without changing
its implementation. The native frozen runtime is
`71eee0c0cc377892de4e00d93e135396b1eb0ee6ede24371322a60a1c4b13d4e`.
The first attempt was deliberately stopped because the test-only map probe was
x86-only. A separately built arm64 probe and test-data directory corrected the
harness; the Rosetta probe and original files were not overwritten. That invalid
attempt supplies no FPS result.

The first valid native 60-second smoke run loaded the expected Dream map/pose
and measured 38.6919 FPS with no zero presentation results or GPU failures.
This was not promoted as a CPU-mode benefit. The subsequent timer-free,
45-second-warm-up Rosetta/native/native/Rosetta comparison measured:

| CPU mode | Displayed FPS | p99 | Maximum interval |
| --- | ---: | ---: | ---: |
| Rosetta A | 38.9007 | 33.333 ms | 41.667 ms |
| Native A | 34.4329 | 41.667 ms | 83.331 ms |
| Native B | 32.8051 | 41.667 ms | 50.000 ms |
| Rosetta B | 28.4692 | 41.667 ms | 66.667 ms |

All four completed their planned 60 seconds on AC with complete presentation
results and zero GPU failures. Later log inspection found that native loaded
zero prewarm recipes while Rosetta loaded 945. The native test-data cache was
a symlink, correctly rejected by the existing path-safety guard. The table is
retained as raw diagnostic evidence, but is **not a matched CPU-mode comparison**
and supports no architecture performance conclusion. Substantial control drift
is an additional limitation. The normal launch default remains Rosetta.

## Six-minute native stability diagnostic and shader-cache gap

The frozen arm64 runtime completed six minutes of movement, camera input,
attacks and three successful area transitions, then stopped as planned with
zero GPU failures. RSS grew from 3,930,656 to 4,573,856 KiB; this short run cannot
establish absence of leaks. The instrumented run includes video, loading
transitions, 74 zero presentation results and a 5,075 ms maximum display gap.
It is **not an FPS benchmark** or proof of motion/fog correctness.

The largest dispatch stall was 2,857.880 ms, containing a 2,855.256 ms pipeline
wait: roughly 1,702 ms of shader-library creation and 1,153 ms of pipeline
creation. The compute source was already present in the Rosetta recipe bank.
This native run also loaded zero prewarm recipes because of the test-cache
symlink. This is first-use compilation evidence, not proof of a GPU fence stall.

A second issue exists beyond the test setup: v2 source-recipe namespaces are
architecture-specific, so an ordinary native launch cannot import the current
Rosetta namespace. A bounded alternate-architecture source import was added
for the same GPU name and OS, retaining schema/hash/path/link/read limits and
recompiling source on the current device. Binary archives are not shared.
The standalone precompiler also now selects the configured CPU runtime.

These later changes are **not validated**. Both final builds failed at three
calls in `game_pipeline_cache_tests.mm`: the new alternate architecture is a
`std::string_view`, but the fixture's `Identity` helper accepts `const char*`.
The precompiler targets built; the overall builds failed. No correction,
fixture rerun or gameplay replay was started after the pause request. The
native test cache is now a physical copy, but the old frozen native binary
still lacks the new source import and must not be described as fixed.

## Render-worker priority experiment

A temporary test plugin applied user-initiated QoS overrides to the named GPU
graphics, GPU compute and Metal presentation workers. OFF/ON/ON/OFF stationary
Rosetta runs used the same frozen binary, a physical cache, a 945-ready-recipe
gate, no slow CPU scopes and a 45-second warm-up. Results were 28.3880,
21.4205, 30.6796 and 33.4779 FPS respectively. The first ON run had 17 zero
presentation results and is excluded by the strict comparison policy.
All completed as planned on AC with zero GPU failures. No repeatable benefit
was established, so no production priority change was made. Requested worker
QoS alone does not establish effective priority or physical core placement.

The M4 Max's `MTLDevice.counterSets` inventory exposes only `timestamp` /
`GPUTimestamp`, with stage-boundary sampling. No fragment-invocation, cycle or
utilization counter set is exposed by this public API on this device. Timestamp
intervals must therefore not be described as exclusive ALU activity or proof
that other GPU clients did not run.

## Fresh mixed-stage diagnostic

`pipe22-profile` covers 783 measured frame labels with 117,921 valid render and
56,643 valid ordinary-compute rows. This is an instrumented capture, including
an initial battery-to-AC transition, and is not an FPS benchmark.

| Observation | Measured time per frame |
| --- | ---: |
| Sampled render-stage interval union | 17.911 ms |
| Sampled ordinary-compute interval union | 1.170 ms |
| Union of both sampled kinds | 18.881 ms |
| Union of renderer command GPU intervals | 23.278 ms |
| Gaps between instrumented renderer commands | 8.727 ms |
| Gap portion before following CPU commit | 7.971 ms |
| Inclusive CPU draw recording | 18.440 ms |
| Inclusive texture change checks, nested in draws | 7.584 ms |
| CPU frame wall scope | 28.754 ms |

There are 13.434 renderer commands/frame and 829 commands recorded for more than
5 ms; their median recording time is 16.029 ms. The capture still points toward
both CPU resource validation/recording and GPU geometry/material work. Sampled
stages include dependencies and can overlap; their sums are not utilization or
exclusive ALU time. Ordinary compute is not the dominant sampled stage.
Coverage excludes upload/adaptor/special-helper/LS-HS/blit/presentation work;
startup counter-pool exhaustion is retained in the raw endpoints. Mixed pass
first/last shader labels do not attribute every draw to one program.

A separate 15-second Instruments Metal System Trace reached its recording
limit but exceeded the 90-second finalization timeout. The retained 4.8 GiB
partial trace fails export with `Document Missing Template Error`; no timing or
utilization conclusion is drawn from that unusable capture.

## Rejected hash prototype

The isolated CPU prototype uses the unmodified BSD-licensed
[xxHash v0.8.3 header](https://github.com/Cyan4973/xxHash/blob/v0.8.3/xxhash.h).
It reads every byte and compares the same chunk boundaries. Its explicitly
synthetic 537-allocation working set preserves retained Dream histogram counts
and approximate scanned bytes, totaling 186.11 MiB, with unaligned starts and
short final chunks. It does not reconstruct original texture identities.
Alternating CRC/XXH3/XXH3/CRC trials use the production
`ROSETTA_ADVERTISE_AVX=1` environment. Unchanged warm-working-set means:

| Build | Existing CRC | XXH3 |
| --- | ---: | ---: |
| AVX2 | 6.975 ms | 22.340 ms |
| SSE2 | 6.861 ms | 7.155 ms |

Streaming a 320 MiB eviction buffer also failed to produce a win. First/last
edits to the largest allocation were detected. These are CPU microbenchmarks,
not game FPS or a guaranteed cold-cache experiment. Neither the header nor a
new hash algorithm is added to production. Readiness, alias, mapping, callback
and GPU-writer checks remain intact.

Evidence lives under `out/pipeline-2026-10-22/`; gameplay data is in
`out/metal-unattended-2026-10-20/pipe22-*`. Frozen binary/source identities,
complete logs, raw CSVs, parser scripts and rejected prototype trials are kept.

Final read-only integrity verification passed after the runs: all 28,779 dump
file sizes/mtimes match the baseline, original eboot and prepared ELF hashes
match, and all 16 master save hashes match. Gameplay and owned build/test
processes were stopped before recording the pause checkpoint.
