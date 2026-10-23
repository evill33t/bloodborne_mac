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

Evidence: `out/pipeline-2026-10-23/`, including runtime identities, complete
build/test/precompile logs, source-import verification, the CPU sample summary,
and the six-minute stability summary. Raw gameplay files are retained under
`out/metal-unattended-2026-10-20/pipe23-*`.
