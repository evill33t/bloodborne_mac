# Wind amplitude and shadow-atlas clear correction

This resumes the foliage/shadow investigation in `MACOS_FOLIAGE_SHADOWS_2026-10-23.md`.
The original dump and master saves remain read-only inputs. Gameplay uses cloned
saves, direct Metal, native 1920×1080, and no generated frames for measurements.
Evidence is retained under `out/renderer-artifacts-resume-2026-10-23/`.

## Foliage: timestep-dependent bend amplitude

REA assembly and pseudocode cover the complete original wind updater at RVA
0x2313870 (11,646 bytes, original instruction SHA256
`7ef49d44de65873820c2bfd76c794be1d903045baa1fb7430a2d77452c3a35bc`).
The analysis-only miniature ELF omits other code/data/imports and is never used
to run the game. Exported evidence and the closed analysis snapshot are retained.

The XYZ product at RVA 0x2314a66 reaches the two published wind-vector banks.
Its factor follows the variable half-task-step. Actual original VS observations
showed the same stationary vertex attributes, object matrix and undeformed
position while this vector's amplitude changed with frame time: a change from
1/80 to 1/60 produced an approximately 4/3 amplitude jump. This is displacement
amplitude, rather than an accumulated position. The separate phase/fade clock
already has the correct half-step behavior and must not be halved again.

The host now rewrites this one product in its owned loaded image to use an
immutable float4(1/60), the original 30 FPS factor. Phase/fades and the independent
scalar product at RVA 0x2314ae7 retain their existing factors. Admission checks the
exact original 14-byte context, owned tail capacity, and RIP displacement range.
Unknown contexts retain the previous behavior. `BBHOST_WIND_VECTOR_FIXED=0`
selects the old variable-amplitude control for comparisons. No original file is
patched on disk.

The timing fixture executes the emitted x86 instruction in 21 cases, including
variable timesteps, original 1/60 equivalence, and unchanged phase source xmm14.
The bounded actual-consumption diagnostic now also observes one fixed original
vertex through fetched attributes, matrix, undeformed/deformed world position
and clip output. Its default-OFF shader source remains unchanged. Its private
full-original-VS GPU oracle passed 212 cases, with maximum absolute error
0.000263893408.

The first live comparison observed constant fixed XYZ in all 240 records versus
74 patterns per geometric identity in the control. However, the fixed capture
was entirely at the timestep ceiling, and the selected endpoints were outside
the clip volume. Those records alone do not prove visible foliage is completely
fixed or isolate variable-timestep behavior. Diagnostic videos/probes also
invalidate clean FPS comparisons. See `wind-vector-stationary-comparison.json`.

A subsequent deliberately variable-step test does discriminate the two paths.
An isolated, opt-in diagnostic plugin alternates time scale 1.0/.75 every eight
loaded Dream frames through the existing plugin API. It has private runtime/data
copies, explicit path/marker admission and an automatic frame limit; normal
plugins/config/master saves are unchanged. This is slow-motion testing, not an
FPS comparison or a production timing change.

Both runs produced 240 valid, within-draw-uniform observations of the same two
actual geometric identities. The control consumed phase increments
0.00923157–0.01666641 and 61 different XYZ vectors (X/phase-delta correlation
0.999999996786). The correction consumed increments 0.00908661–0.01666641 while
all 240 XYZ vectors remained bit-identical at the authored factor. The variable
increments exceed the float32 quantization gate, so this proves actual shader
consumption separates amplitude from the changing clock. Both stopped as
planned with no GPU errors. Selected endpoints remain offscreen: input/output
evidence does not establish all visible foliage pixels or remove presentation
stutter. Reports: `wind-vector-varstep-stationary-comparison.json` and
`wind-varstep-isolation-check.json`.

## Shadows: original HTILE RMW did not clear native depth

The arch investigation separated the shadow-mask alpha from AO and fog, and
verified actual screen/depth reconstruction, fog UVs, selected cascade transforms
and caster scissors. The cascade basis remained stable for the measured world
points. The captured atlas instead accumulated LESS depth: between successive
camera-motion captures, 29,321 then 51,861 downsampled pixels decreased and **none
increased** across all four atlas quadrants.

The original 88-byte metadata shader (hash `595f38bd3818a17b`, SHA256
`2e7cc32511685e42091fc7b1502a75bb8049d79d89053f3e698abff772bc0a66`)
conditionally replaces every HTILE dword except those whose low nibble is 13.
Actual scalar probes supplied count 0x40000 and replacement 0xfffffff0. The prior
store-only clear recognizer correctly rejected this read-modify-write shader,
but no equivalent clear reached the native D32 atlas.

The new exact recognizer admits only the captured program/register ABI, direct
4096×64 dispatch, plain complete descriptors, uniquely bound initialized 4096²
D32 target, clear depth 1, stable mappings, full CPU-read proofs and all three
native coherence modes. It inspects the entire 1 MiB metadata bank and rejects
the native clear if **any** low nibble is 13. The meaning of that preserved state
is unknown. It always retains the original compute dispatch.

Both the 8-byte scalar leaf and complete metadata bank receive read leases on
the actual consuming command after encoder setup. Existing physical-alias writes
and mapping mutations wait for consumption; capacity/readiness changes discard
only the native-clear plan. The next native pass applies the clear in queue
order and advances depth identity when it actually clears. This does not extend
the existing lease contract to unmarked raw guest CPU stores.

AMD PAL's GFX9 depth-only clear encoding independently provides context for the
observed replacement word, but is not an exact PS4 specification:
[GetClearValue](https://github.com/GPUOpen-Drivers/pal/blob/dev/src/core/hw/gfxip/gfx9/gfx9MaskRam.cpp).
The actual original shader and captured inputs govern admission.

The real Metal fixture reproduced the failure before the correction: a farther
.8 draw failed LESS against stale .2. After the correction the actual native
4096² atlas contains .8 at all nine probes, including quadrant and edge probes.
All 262,144 original RMW outputs and out-of-range guards are checked. Separate
cases reject a last-word nibble13 and disabled proof modes. A blocked GPU queue
plus physical-alias CPU writes verifies both complete input leases belong to
the actual consumer. Logs: `htile-gpu-before.log`, `htile-gpu-after-lease.log`,
`htile-gpu-proof-off.log`.

The corrected live arch replay completed without GPU errors, and the clear
activated each shadow pass after the atlas loaded. Across 120 camera-motion
frames, independently reprojected depth-matched samples improved as follows
(device-Z acceptance tolerance 2e-7):

| Region | Mean shadow-alpha drift before / after (0–255) | Samples changing by >32 before / after |
| --- | ---: | ---: |
| Distant walls | 20.666 / 0.691 | 12.95% / 0.346% |
| Arch intrados | 21.928 / 0.868 | 16.95% / 0.347% |

The corrected mask still spans 0–255 and contains shadowed pixels in both regions;
this is not a uniform-mask result. AO preserves mask alpha, and cameras match
their captured frames. These separate launches share the cloned initial pose and
camera route, but differ in phase timing; they are visual/correctness evidence,
not a clean performance comparison. See `shadow-fix-reprojection-comparison.json`
and `shadow-fixed-alpha-coverage.json`. Other areas and whole-game shadows remain
outside this capture's coverage.

Two full native D32 atlases additionally show 501,064 increasing depth pixels,
419,098 decreasing pixels and 24,151 pixels returning to clear depth1 across the
three active quadrants. The first quadrant remains1. This replaces the old
monotonic accumulation; `atlas-after-clear-transitions.json` retains per-quadrant
counts. `arch-fixed-internal.png` is an unoccluded native1080p framebuffer image.

Both runtime builds pass. The x86 suite executed 113 tests with four skips; the
arm64 suite executed 21 with two skips. The private original HTILE shader was
supplied separately to the real GPU regression above; default CTest intentionally
skips that fixture when its private input is absent. The Python consumed-wind
protocol tests pass 20/20.

## Performance work

The retained arch CPU sample spends 103/241 graphics-thread samples in texture
hash validation and hashes about 288 MiB/frame. These are sampled wall-time
observations, not an exclusive CPU-time measurement. A bit-exact CRC unroll
prototype passed its correctness checks but failed the representative 288 MiB
benchmark (warm median 10.244 ms versus 10.116 ms; eviction 10.168 versus
9.923 ms), so production hashing is unchanged. No 60 FPS gain is claimed from
the instrumented captures or this rejected prototype.

An additional opt-in, `BB_METAL_EXACT_TEXTURE_COMPARE_MB=384`, retains the exact
immutable CPU upload bytes and compares them with bounds-safe SSE2/NEON equality.
Every unchanged byte in the requested range participates. It preserves the
existing hot/full or cold rotating-chunk policy, source-readiness guards and
CRC fallback; it does not infer that unmarked CPU stores are absent. Identity
includes descriptor, address, extent, upload version and mapping generation.

Only successful CPU GPU-detile uploads publish a handle, backed by a dedicated
sealed allocation. GPU-source uploads, CPU fallbacks, failures, unsupported sizes,
unknown allocation charges and allocation failure retain the original path.
The LRU charges the whole backing allocation and rejects excess. Metal can retain
evicted buffers until GPU completion, so this budget covers cache retention,
not total in-flight residency. The option is OFF by default.

The representative isolated 288 MiB x86 comparison measured approximately
8.08 ms for SSE2 equality versus 10.78 ms for CRC, but used only two samples per
case alongside diagnostic gameplay. libc memcmp was slower (16.15 ms); it is not
used for the full vector loop. This is a candidate, not a gameplay speedup.

The actual renderer GPU regression verifies cold-chunk and full-hot cache hits,
unmarked edits in the first/final partial chunks, immutable queued frames,
same-VA remapping and an unmarked tail edit through a physical alias. It passes:
`exact-coherence-gpu-v2.log`. The initial fixture incorrectly edited chunk0 after
advancing the cold cursor to chunk1; correcting the fixture to target chunk1
preserved the existing policy and required no renderer change. Pure tests pass
on x86/Rosetta and arm64, including guarded memory tails, identity changes,
allocation rollback and entry/budget pressure.

Four clean 60-second Dream stationary comparisons (OFF/ON/ON/OFF, native1080p,
FG/TAA off, all957 prewarm recipes ready) completed without GPU errors, skipped
frames or missing presentation timestamps. In-window rendered/displayed FPS:

| Exact comparison | Rendered FPS | Displayed FPS | p99 interval |
| --- | ---: | ---: | ---: |
| OFF A | 39.881 | 39.881 | 25.091ms |
| ON A | 39.921 | 39.921 | 25.003ms |
| ON B | 39.946 | 39.946 | 25.003ms |
| OFF B | 39.861 | 39.878 | 25.004ms |

The approximately0.16% mean difference is not a meaningful FPS improvement.
Actual retained coverage is substantial: about209–213MiB and294–295 entries;
hashing fell from about186MiB/frame to7–11MiB/frame. This rules out interpreting
the sampled hash fraction as a removable, serial frame-time fraction. The feature
therefore stays OFF by default. These are battery runs, with state of charge
falling42%→26%, not AC-normalized results. Window recordings began only after
freezing measured, already-presented frame rows; their sequence markers and
in-window counters exclude recording overhead. Checked metrics are in
`exact-clean-abba-checked.json`.

Source and timestamps establish a remaining display-link admission gate:
`Flip` commits world work then waits for the presenter to acknowledge the frame;
the display-link path acknowledges after a callback obtains a drawable and
commits presentation. In the first OFF/ON runs2370/2400 and2384/2401 intervals
respectively span exactly three120Hz ticks. This identifies quantization, not
whether the underlying GPU work could meet16.67ms. The existing
`BB_METAL_DISPLAY_LINK=0` worker control preserves V-sync and same-queue commit
before acknowledgement, and is being compared separately. No ownership gate
is removed. `BB_METAL_WORLD_FRAME_LIMIT=2` adds a later GPU-completion bound and
does not remove this display-link gate.

The final x86 suite executes115 tests with four private/platform skips; arm64
executes22 with two private-input skips. The final native original-CS HTILE
read-lease fixture passes separately (`htile-native-final-lease.log`).

## Worker pacing follow-up, 24 October

The safe existing worker acquisition control completed three additional clean
60-second Dream stationary runs. FG/TAA remain off and output is native1080p.
Every run has zero GPU errors, skips and missing presentation timestamps:

| Worker preset | Rendered FPS | Displayed FPS | p99 interval |
| --- | ---: | ---: | ---: |
| Rosetta, exact comparison384MiB | 44.127 | 44.127 | 25.003ms |
| Native CPU, SIMD hash and constant-memory map | 54.354 | 54.371 | 25.004ms |
| Rosetta, original CRC | 40.936 | 40.919 | 33.333ms |

These separate launches run on falling battery charge26%→10%; the last reports
an early battery warning. They are not a matched AC causal estimate, and the
native preset also changes CPU/hash/constant-map options. None reaches sustained
60 rendered FPS. The CRC worker control improves mean FPS slightly but has a
worse tail than the display-link baseline. Checked counters and full conditions
are in `legacy-pump-checked.json`.

The final policy therefore changes the **native ARM64 CPU runtime's** ordinary
frame default to worker acquisition. Rosetta retains its display-link default.
Generated pairs use the display link on both CPU paths. Existing explicit
`BB_METAL_DISPLAY_LINK=0/1` overrides remain authoritative. Frame ownership,
same-queue presentation commit, semaphore release and guest-label order remain
unchanged. This is not an early acknowledgement or an unbounded frame queue.

Final presenter fixtures pass5/5 on arm64 and5/5 on Rosetta, covering each
CPU-specific default, explicit overrides, latency two, UI stalls, FG off/on,
V-sync transitions, ordered real/generated pairs and shutdown. The final frozen
game SHA256 values are in `final-runtime-identity.json` and
`final-native-runtime-identity.json`.

The best measured preset remains opt-in because the native CPU and constant-map
paths are experimental beyond the exercised scenes:

```sh
BB_MAC_CPU=bbcpu BB_METAL_NATIVE_SIMD_HASH=1 BB_METAL_CONSTANT_MEMORY_MAP=1 \
  bash packaging/macos/run.sh
```

Leave `BB_METAL_DISPLAY_LINK` unset to allow automatic FG scheduling. The measured
rates above select native1080p, FG off and TAA off in the existing settings. The
final movement/transition stability replay separately exercises TAA on; it is
not a clean FPS comparison.

## Final stability and preservation

The rebuilt native runtime completed a120-second movement/combat replay starting
at the saved arch pose, then travelling to Hunter's Dream at60seconds. Actual
loaded-state rows confirm both maps18010000 and15000000 and subsequent movement.
Native1920×1080, TAA on, FG off, SIMD hash and constant-memory map were enabled;
exact texture comparison remained off and the display-link override was unset,
so this exercises the new native pacing default. It stopped as planned with
zero GPU failures and active native motion inputs. Prewarming overlapped this
short stability replay, so its FPS is deliberately excluded. This is not a
long-session or whole-game stability claim. See `final-stability-checked.json`.

Battery charge fell10%→2% during the final replay; no more gameplay or profiling
is running. `owned-game-processes-final.txt` is empty. The final preservation
check passes all28,779 original dump size/mtime entries, the original eboot and
prepared-ELF hashes, and all16 master-save hashes (`original-integrity-final.json`).
REA evidence is exported and the analysis session is closed. No commit or
original-game-file modification was made.
