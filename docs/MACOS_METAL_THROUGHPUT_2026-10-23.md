# Metal throughput optimization, 2026-10-23

Goal: improve rendered gameplay FPS at native 1920×1080. Frame generation is
OFF in throughput measurements. The game dump, prepared original ELF and master
saves stay read-only; all gameplay uses cloned saves and isolated cache banks.

Result: selected-variant precompilation and opt-in native SIMD texture validation
are implemented. Static native ON runs reach 41.07–48.81 rendered FPS versus
39.79–40.16 controls. A separate unprofiled movement replay averages 52.91
rendered/displayed FPS. Sustained 60 FPS is not achieved; both switches stay
opt-in because the static candidate results vary substantially.

## Fresh profile

`metal23-profile` uses the frozen normal Rosetta runtime at native 1080p. The
first forty measured seconds precede CPU sampling and contain 1,461 frame
labels. Inclusive CPU categories are nested and cannot be added together.

| Observation | Time per frame |
| --- | ---: |
| CPU draw recording | 19.12 ms |
| Resource resolution, within draws | 13.63 ms |
| Texture resolution, within resource work | 12.64 ms |
| Texture content checks, within texture work | 7.44 ms |
| Sampled render-stage interval union | 16.90 ms |
| Sampled ordinary-compute interval union | 1.07 ms |
| Renderer command GPU interval union | 22.90 ms |
| Gaps between instrumented renderer commands | 4.47 ms |

Mixed geometry/material passes remain expensive. Hardware timestamp spans
include dependencies and overlap; they are not exclusive shader execution or
GPU utilization. Command coverage does not include every presentation/upload
queue. The diagnostic FPS is excluded from clean comparisons.

## Native SIMD texture validation

`BB_METAL_NATIVE_SIMD_HASH=1` selects the unmodified xxHash v0.8.3 NEON XXH3
detector on Apple arm64. Other architectures retain the existing detector.
The choice is fixed once per process so immutable upload snapshots and later
comparisons agree. All bytes and existing chunk boundaries are covered; mapping,
callback, GPU-writer readiness and immutable upload ownership are unchanged.
No presumed read-only texture bypass or page-protection relaxation is added.
The detector is nonpersistent and does not change game data or shader recipes.

An isolated 537-allocation working set reconstructed from the retained texture
histogram contains 186.11 MiB. On arm64/M4, unchanged warm scans average 6.38 ms
with CRC and 4.30 ms with NEON XXH3; after a 320 MiB eviction read they average
6.40 and 4.28 ms. First/last edits remain detected. These are CPU throughput
observations, not gameplay FPS. The earlier x86 XXH3 prototype regressed; it is
not enabled for Rosetta.

Production tests check every byte of a 64 KiB chunk, unaligned/short inputs,
paired changes, protected allocation tails and NEON output against xxHash's
scalar accumulator. Actual uploader/renderer GPU tests also run with the
native detector enabled. Third-party attribution and the complete BSD license
are retained alongside the unchanged header.

## Precompile the selected mapping variant

`BB_METAL_CONSTANT_MEMORY_MAP=1` already specializes immutable Metal mapping
metadata into the constant address space. The standalone precompiler and
startup loader now derive that same source variant after checking the original
recipe's hash/schema. Exact selected source plus descriptor still gates a warm
pipeline hit. Unknown layouts stay unchanged; guest payload buffers retain
their access and ordering.

Derived recipes have a separate GPU/OS/architecture/variant namespace, retaining
the existing per-namespace count/byte bounds. This prevents a full selected bank
from evicting its ordinary source bank. OFF loads the ordinary namespace; ON can
import and transform ordinary sources. It never shares compiled binary archives.

The initial v2 bulk check caught a real issue: deriving into the ordinary
namespace invoked its existing 1,024-file LRU limit and evicted 876 of 950 copied
source inputs. That candidate and its interrupted pilot are rejected evidence.
The fix isolates namespaces. Its regression checks actual GPU output, warm hits,
separate destination directories, source bytes/mtime preservation, deduplication
on a second launch and ordinary-mode reuse after switching OFF.

The retained original-material differential also passes all six outputs for
both material programs, across sixteen states each, with GPU-written payloads
and descriptor/mapping changes. Finite words compare exactly; paired NaN payloads
are treated as in the existing fixture. Synthetic material timing is not game FPS.

## Validation and gameplay results

The final v3 native suite passes 17/17 tests with both candidate switches enabled
(10.47 seconds). Final Rosetta v3 passes 109 tests with two expected skips
(137.73 seconds). Both complete builds pass. The bulk v3 precompiler compiles
950/950 selected recipes in 19.90 seconds and preserves every one of the 950
ordinary input files' bytes and mtimes. Clean gameplay comparisons and the movement/transition replay are recorded below.

Evidence: `out/metal-throughput-2026-10-23/`. Raw gameplay files:
`out/metal-unattended-2026-10-20/metal23-*`. Candidate runs use one frozen
executable, matching physical cache banks, an all-ready pipeline gate, a
45-second warmup and OFF/ON/ON/OFF order. Clean measurements have no profiler,
capture, recording, slow CPU timing scopes, concurrent builds or GPU tests.

## Comparison controls and rejected evidence

The first native OFF/ON/ON/OFF cohort ran while QEMU and Ghostty were busy.
Its rendered/displayed rates were approximately 40.2, 35.7, 35.4 and 30.5 FPS.
The closing control also changed output height to 1082, with two transient
1730-pixel-wide rows, and fails the strict native-1080p gate. Those shared-load
results do not establish a causal gain or regression. The later isolation runs
(mapping only 47.8 FPS; SIMD only 29.3 FPS) share that workload limitation.

A separate combined diagnostic replay reached about 55 presented FPS, while
measured CPU draw recording averaged 12.34 ms and texture checks 5.27 ms.
It includes instrumentation and one missing presentation timestamp, so it is
not clean FPS evidence. The earlier synthetic checksum measurements also are
not used as game frame rates.

After the user paused the heavy workloads, a read-only process snapshot showed
QEMU at 3% CPU and Ghostty absent. The new low-load cohorts retain separate run
names, all results and the same strict resolution/presentation gates. No other
build, precompiler, capture or GPU fixture runs alongside their measurements.

## Low-load Rosetta comparison

All four runs pass the exact native-1080p, AC-power, runtime-identity and
presentation gates. Generated frames are zero in every run.

| Run | Mapping | Rendered FPS | Displayed FPS | p99 display interval |
| --- | --- | ---: | ---: | ---: |
| metal23-rosetta-off-a | OFF | 39.96 | 39.94 | 25.00 ms |
| metal23-rosetta-on-a | ON | 39.98 | 40.00 | 25.00 ms |
| metal23-rosetta-on-b | ON | 39.97 | 39.97 | 25.00 ms |
| metal23-rosetta-off-b | OFF | 37.63 | 37.63 | 33.33 ms |

All have zero GPU failures, missing presentation timestamps and drawable skips.
Both ON runs are around 40 FPS, but so is the opening OFF run. The closing
control drops to 37.6 FPS. This does not establish a repeatable whole-game gain
from mapping specialization alone, so it remains opt-in. Lower inclusive GPU
timestamps are not presented as GPU utilization or proof of a frame-rate gain.

## Low-load native arm64 comparison

Both switches toggle together in this cohort. All four runs pass the same
strict gates; no generated frames, GPU failures, missing timestamps or drawable
skips occur. Rendered and displayed FPS use actual counter deltas.

| Run | Mapping / SIMD | Rendered FPS | Displayed FPS | p99 display interval |
| --- | --- | ---: | ---: | ---: |
| metal23-lowload-off-a | OFF / OFF | 39.79 | 39.79 | 33.33 ms |
| metal23-lowload-on-a | ON / ON | 41.07 | 41.07 | 33.33 ms |
| metal23-lowload-on-b | ON / ON | 48.81 | 48.83 | 25.00 ms |
| metal23-lowload-off-b | OFF / OFF | 40.16 | 40.16 | 25.00 ms |

The closing control's largest display interval is 91.67 ms; the other maxima
are 33.33, 50.00 and 33.33 ms in order. No profiler or recording ran in these
windows. Photo analysis started during this cohort: a retained process snapshot
shows `photoanalysisd` at 91.5% CPU. Its GPU work is not measured here, and it is
not proven to cause the frame-rate variation.

The ON runs improve over the approximately 40 FPS controls, but span 41.07 to
48.81 rendered FPS. This is encouraging rather than a stable 60 FPS result.
Neither option becomes a default on this evidence. Rosetta remains the default
CPU mode; the optional native runtime is not selected silently. All eight
low-load run results, including both controls and the slower ON run, are kept.

## Using the tested options

Build the native runtime, then set `BB_MAC_CPU=bbcpu`,
`BB_METAL_CONSTANT_MEMORY_MAP=1` and `BB_METAL_NATIVE_SIMD_HASH=1` when launching.
Use the same CPU mode and mapping switch with `precompile-shaders.sh` and the
same external data root. The SIMD switch does not affect shader recipes.
Explicit `0` selects either ordinary path. These options neither enable frame
generation nor reduce render resolution. Both remain off when not requested.

## Movement, transitions and visual check

`metal23-native-stability` completes the planned 300-second replay with both
switches ON, native 1080p, FG OFF, movement, camera orbit and attacks. The probe
records 226 distinct player positions and the complete map sequence:

`18010000 → 15000000 → 18010000 → 18000000`

The three requested warps load at measured seconds 77.76, 152.82 and 227.70.
There are no GPU failures or unexpected process exits. RSS ranges from
3963 to 4961 MiB as areas and camera views change; this is a short test, not
long-duration leak proof.

Its FPS is excluded: it has CPU timing scopes, an eight-second screen recording,
a 25-second CPU sample and six missing presentation timestamps. The largest
941.67 ms display gap begins at 115.57 seconds, as the sampler attaches at
115 seconds. UI and texture-check scopes simultaneously span approximately
832 ms. This strongly suggests process-wide observer interference, rather than
an isolated 832 ms texture scan; it is not claimed as a fixed game stall.
Two missing timestamps coincide with area travel, two with recording startup,
and two with CPU sampling. Correlation does not prove every missing callback's
cause.

The native CPU sample still finds 1302 of 4127 graphics-thread observations in
XXH3 content checks, within texture/resource resolution. These are sampled
thread stacks, not utilization or exclusive durations. An eight-second Dream
window after sampling averages 11.84 ms CPU frame work, including 7.91 ms draw
recording and 3.40 ms content checks; camera/visibility differ from the static
comparison, so those numbers are not a matched performance claim.

The retained Dream recording shows the HUD, character, foliage, stairway and
fog while moving and attacking. Two extracted frames were inspected. This
spot check and the exact original-material GPU differential are bounded visual
coverage; they do not prove absence of all fog/foliage flicker or wind jitter.

`metal23-native-clean-movement` then repeats 90 seconds of Dream movement,
camera orbit and attacks with no profiler, recording, capture, work CSV or slow
CPU scopes. It records 101 distinct player positions. All output counters are
1920×1080, both power snapshots are AC, and FG/generated counts remain zero.

| Observation | Clean movement replay |
| --- | ---: |
| Rendered / displayed counter rate | 52.91 / 52.91 FPS |
| Presented callback rate | 52.78 FPS |
| p99 display interval | 25.00 ms |
| Maximum display interval | 50.00 ms |
| Missing presentation timestamps | 0 |
| GPU failures | 0 |

The approximately 942 ms gap does not recur without the profiler. The camera
moves through different views, so this 52.91 FPS result is not compared directly
with the stationary OFF controls. All owned game, recording and sampling
processes have stopped.

## Original-file integrity and remaining limit

The final verifier passes after all replays: the size/mtime inventory for all
28,779 original dump files is unchanged, both original executable hashes match,
and all 16 master-save hashes are unchanged. Only cloned player data and
external generated/cache files were written.

The measured original `eboot.bin` SHA-256 is
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`;
the prepared original ELF remains
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.

The baseline shows substantial CPU texture validation/resource work as well as
GPU material cost. Reducing one does not guarantee a whole-frame gain. The
native options improve the tested throughput range but do not yet supply the
16.67 ms frame budget consistently. No resolution, fidelity or safety checks
were relaxed to claim 60 FPS.
