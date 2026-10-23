# Shader-cache recovery and native renderer profiling, 2026-10-23

The original dump, original prepared ELF and master saves remain read-only.
Gameplay uses cloned saves and isolated physical cache directories. The target
is native 1920×1080 with TAA/native motion and a 120 FPS cap; sustained 60 FPS
has not been established.

## Compile correction and regression results

The new alternate-architecture cache test passed a `std::string_view` to its
`Identity` helper, which accepted `const char*`. The helper now accepts string
views, matching the production identity API without requiring null termination.
Both complete runtime builds pass.

- Rosetta serial suite: 101 passed, two expected private-input skips, no failures
  (140.14 seconds).
- Native serial suite: 11 passed, no failures (8.74 seconds).
- Both suites execute the actual Metal pipeline-cache fixture, including
  alternate-architecture import, two launches, exact descriptor reuse,
  mismatched GPU/OS rejection, source preservation and symlink rejection.
- Test assertions remain enabled with `-UNDEBUG`.

These checks validate source import and compilation, not every game's pipeline
variant or visual correctness. Rosetta remains the ordinary launch default.

## Actual native precompilation

The native standalone precompiler imported 945 copied Rosetta recipes,
compiled all 945 on the M4 Max in 27.58 seconds, and persisted exact copies in
the native namespace. A second native run loaded those 945 current-namespace
recipes and completed in 0.45 seconds. All copied source hashes and mtimes
remained unchanged. The driver's managed compiler cache may already contain
work; neither time is presented as a cold-driver-cache benchmark.

The live game subsequently reported 945 loaded and ready pipelines, with
foreground warm hits. The existing no-symlink guard was preserved. No explicit
Metal binary archive was created or loaded. See the
[precompiler instructions](MACOS_GPU_STABILITY.md).

## Six-minute gameplay diagnostic

Frozen native runtime:
`a1be3eebf0cf30e18178cc3c5b383d268fc82298cfdb76b7e6a08bc5b074563c`.
The run used AC power, native 1080p, cloned saves, movement/camera/attacks and
three successful transitions:
`18010000 → 15000000 → 18010000 → 18000000`.
It stopped as planned with zero GPU failures. RSS started at 3,776,272 KiB,
ended at 4,120,032 KiB and peaked at 4,189,312 KiB; this is not a leak test.

Yesterday's 2.855-second pipeline wait did not recur. Four additional pipeline
recipes were recorded during travel; a new variant caused a 339.704 ms pipeline
wait, including 186.083 ms library creation and 153.453 ms pipeline creation.
After shutdown all 949 recipes compiled successfully in the standalone tool.
Recorded precompilation cannot predict previously unseen variants.

There are still hitches: the largest measured draw/resource scope was about
959 ms, and UI event handling reached about 1,088 ms. CPU sampling, detailed
CPU/command traces, transitions and 632 missing presentation results invalidate
an FPS comparison. The largest displayed interval was 1,325 ms. No frame-time
or stability claim is made by selecting only the good portions of this run.

## Next measured CPU target

The native CPU sample contains 4,800 graphics-command-thread observations;
2,530 are under `TextureChanged`, about 52.7 percent. These are sampled stacks,
not exclusive CPU cycles, and include scheduling effects. This sample is from
the initial area before the first transition. The renderer already emits
hardware ARM CRC instructions; the cost is not a missing CRC build feature.

The next comparison uses the existing exact two-worker chunk scanner against
serial scanning, on the same frozen native executable. Both modes preserve
whole-source readiness, mapping/callback/GPU-writer checks, per-chunk CRCs,
immutable upload versions and synchronous completion. Both enable the same
lightweight size histogram and disable slow timing scopes. All recorded
pipelines must be ready before a common 45-second warm-up. No build, compiler,
screen recording or other GPU fixture runs during clean FPS measurements.
Results are retained under `out/pipeline-2026-10-23/`; a default change requires
a repeatable benefit and appropriate stability checks.

The completed serial/two-worker/two-worker/serial comparison measured:

| Scanner | Displayed FPS | Missing presentation results | GPU failures |
| --- | ---: | ---: | ---: |
| Serial A | 38.0216 | 0 | 0 |
| Two workers A | 29.7672, excluded | 8 | 0 |
| Two workers B | 29.0254 | 0 | 0 |
| Serial B | 30.5052 | 0 | 0 |

All ran on AC with complete prewarm coverage, planned stops and the same frozen
native executable. The controls drift substantially; no repeatable worker
benefit was established. Parallel scanning remains off by default. A later
read-only `pmset` query reported no recorded thermal/performance warning; that
does not identify the cause of the drift.

## Opt-in constant mapping metadata

`BB_METAL_CONSTANT_MEMORY_MAP=1` changes the generated `DcMap` header/page-table
access to Metal's `constant` address space. `UpdateMemMap` already creates a
fresh, separate buffer for each mapping version. Guest payload buffers remain
`device` resources with their original access, bindings, page arithmetic,
readiness checks and command ownership. No payload snapshot, CPU read, command
cut or fence is added. Unknown source layouts remain unchanged. The default
is OFF while gameplay performance is evaluated.

The isolated original-material test compares all six outputs, GPU-written
payloads and descriptors across sixteen states per shader. Both original
materials pass exact finite-word comparison, allowing paired NaN payloads as
in the existing fixture. Device payload arguments use disjoint buffers. A
256 KiB mapping table and a 256 MiB virtual-address bias exercise table reads
beyond 64 KiB without allocating a large guest pool.

With production Metal 3.0, safe math and invariance options, an isolated
OFF/ON/ON/OFF synthetic benchmark draws eight full-screen 1920×1080 triangles
to six RGBA16Float targets per command. Six measured commands per mode give:

| Original material | Device-map GPU interval | Constant-map GPU interval |
| --- | ---: | ---: |
| `68fcb859536adcd4` | 0.4666 ms | 0.3283 ms |
| `aff190d9945a53e0` | 1.0164 ms | 0.2944 ms |

Uniforms and textures are synthetic; this is not game FPS. These intervals
include render/store and scheduling, not exclusive shader cycles. An earlier
timing pass overlapped precompilation and is explicitly excluded; the final
pass ran after all compilation and other GPU tests had ended.

The first 949-variant precompile attempt caught an additional device-space
table cast in the native vertex-fetch helper: 903 pipelines compiled, 46 did
not. That candidate was never launched in gameplay. The helper now qualifies
that second lookup consistently and rejects unknown integer-cast layouts.
Synthetic GPU coverage explicitly includes the actual native-fetch helper;
its identity-gated optimization is not falsely claimed to run on a synthetic
shader identity. The corrected 949-variant bank compiles completely.

After correction, the serial enabled-mode Rosetta suite passed 102 tests with
two expected private-input skips (135.60 seconds), and native passed 11/11
(6.76 seconds). Full original-material GPU differentials also pass using the
production helper. The new frozen native executable is
`9754637feca43c619e522e06330c65ead854ce297063c0f03bfab2cfd5744497`.
Gameplay comparisons use that same executable in both modes, separate physical
banks containing corresponding 949 variants, and all-ready gates.

The matched stationary Dream comparison used native 1920×1080, native motion,
45 seconds of warmup, 60 seconds of measurement, AC power and no profiling or
screen recording. All four processes reached their planned stops with zero
reported GPU failures:

| Constant map | Presented FPS | Missing presentation timestamps | Selection |
| --- | ---: | ---: | --- |
| OFF A | 28.9291 | 3 | Excluded |
| ON A | 36.1928 | 0 | Complete presentation evidence |
| ON B | 40.4879 | 0 | Complete presentation evidence |
| OFF B | 34.4258 | 5 | Excluded |

The two candidate runs had p99 intervals of 41.6653 and 33.3334 ms. Neither
control meets the pre-existing complete-presentation gate. Consequently these
results establish neither a matched percentage gain nor 60 FPS. The option
remains OFF by default; the isolated material improvement is not substituted
for whole-game performance evidence. The experimental ON bank was derived
into a separate directory using the production source transform, then compiled
with the ordinary precompiler. Automatic conversion of existing recipes when
enabling this option is not implemented in the production precompiler.

A four-minute candidate diagnostic then completed movement, camera motion,
attacks and three successful transitions through
`18010000 → 15000000 → 18010000 → 18000000`. All 949 known recipes were ready,
with 526 warm foreground hits by the end. The runtime transformed 1,008 shader
sources with zero layout fallbacks. It stopped as planned with zero reported
GPU failures. No pipeline/library wait crossed the measured CPU-stall trace
threshold during the replay. This does not prove zero compiler work or attribute
the result to the constant-map change: the learned variants were now prewarmed.

The largest thresholded measured CPU scope was a 37.121 ms UI event scope; a
texture-source wait reached 19.567 ms. There were still 24 missing presentation
timestamps and a 125 ms displayed interval. Detailed tracing, three transitions
and eight seconds of screen recording make this a stability diagnostic rather
than an FPS comparison. RSS started at 3,865,984 KiB, ended at 4,086,672 KiB and
peaked at 4,199,248 KiB; this short replay does not establish absence of leaks.
The retained Dream contact sheet shows the character, foliage, fog and workshop
without a gross colored glow in the sampled frames. It cannot establish that
all fog flicker or foliage timing artifacts are fixed.

## Frame-generation feasibility

A native capability query on this Mac reports `Apple M4 Max`,
`MTLFXFrameInterpolatorDescriptor.supportsDevice = true` and
`supportsMetal4FX = true`. The installed SDK declares this API for macOS 26+;
the current host is macOS 27. This is a support query, not a generated-frame
or performance test. See [Apple's frame-interpolator API](https://developer.apple.com/documentation/metalfx/mtlfxframeinterpolatordescriptor).

The port already captures scene depth, camera state and native motion input.
MetalFX expects current-to-previous motion in pixel units, while this port's
native input is biased/normalized and can fall back to camera motion. An
integration needs explicit conversion, matching frame identities, history
resets, HUD handling, retained frame resources and presentation pacing.
Generated presentations must not advance guest simulation/frame timing a
second time, particularly the corrected wind clock. Rendered and generated
frame counters must remain separate. Interpolation adds GPU work, so an exact
doubling of displayed FPS is not guaranteed. No frame-generation game mode
has been implemented or gameplay-tested in this investigation.

The follow-up offscreen probe created the real native MetalFX interpolator at
1920×1080 using RGBA16Float color, R32Float depth and RG16Float pixel motion.
Twenty synthetic GPU commands completed without a reported GPU error, including
two history resets. Readback checked every output component for finite,
non-poisoned values and preserved foreground/background regions. An object
edge at X=712 in the previous frame and X=716 in the current frame appeared at
X=714 in the generated output. This verifies interpolation rather than simply
copying the current frame.

After four warmup commands the median GPU interval was 1.443 ms for synthetic
input generation plus interpolation, measured across sixteen commands with
gameplay, captures and other GPU fixtures stopped. This is a synthetic cost,
not an in-game budget or displayed-FPS prediction. HUD behavior, actual motion
conversion, camera cuts, foliage, resource retention, pacing and input latency
are untested. Validation-environment attempts also completed, but the device
still reported the native `AGXG16CDevice` class; debug-device wrapping was not
confirmed, so these are not claimed as Metal API validation passes.

## Static recompilation assessment

Static recompilation can reduce guest CPU execution overhead when that work
limits the frame. REA supplies binary analysis and reconstruction evidence; it
does not automatically produce an optimized, equivalent native game binary.
The existing [Bloodborne static recompiler](https://github.com/bloodborne-native/bloodborne-recomp)
currently reports 20–30 percent more CPU time for its generated library than
its translator. That is the upstream project's measurement, not this port's.

This port already has an optional arm64 host renderer and bbcpu JIT. Its
ahead-of-time generator/loading path is not enabled. The sampled texture-check
cost is native renderer code, which recompiling the game executable would not
replace. The available generated-library interface also notes that direct
calls between generated functions bypass the translator's entry/return guards;
the existing command-writer synchronization and patched-function exclusions
must be preserved in any future experiment.

A fresh REA/Hopper query inspected original writer RVA `0x10744d0` using the
previously verified 246-byte original unwind extraction, analysis artifact
SHA-256 `def3a5a1963780ba68c65998f3b37275cc6118a28054e2cbbfa88df5103111cf`.
Evidence ID:
`ev_0cc58c896a6806320d89a6669d31139bba8de94178d9c939f5e36029f3041c16`.
The assembly retains capacity checks, an indirect refill callback, a selector
table load and a subsequent helper call. The tiny artifact omits the table and
helper body. This supports the need to preserve those contracts in a selective
native rewrite; it proves neither call frequency nor a performance gain.
The analysis artifact was never executed or substituted into the game.
The owned REA target was closed after retaining the full evidence record.

## Final state

The compile correction and cross-architecture precompilation are validated.
The constant-map path is corrected, numerically tested and available as an
experimental option; it remains OFF by default because matched whole-game FPS
benefit has not been established. Parallel texture scanning also remains OFF.
Rosetta remains the ordinary launch default. Frame generation has passed a
native offscreen feasibility test and is not yet integrated into gameplay.
Native 1080p 60 FPS remains the performance goal, not an achieved result.

After all gameplay runs, the original dump's 28,779-file size/mtime inventory,
original `eboot.bin` hash, original prepared ELF hash and all sixteen master-save
hashes matched the retained baseline. Owned gameplay processes stopped normally.

Evidence: `out/pipeline-2026-10-23/`, including runtime identities, complete
build/test/precompile logs, source-import verification, the CPU sample summary,
and the six-minute stability summary. Raw gameplay files are retained under
`out/metal-unattended-2026-10-20/pipe23-*`.
