# Bloodborne macOS: captured motion, HTILE and gameplay validation

2026-10-18–19. Follow-up to the [static executable audit](REA_EBOOT_GRAPHICS_PERFORMANCE.md)
and [GPU implementation notes](MACOS_GPU_STABILITY.md). The port was actually
launched and controlled for these checks. Original game files were not patched.

The subsequent [stutter investigation](MACOS_STUTTER_PROFILING.md) profiles the
383 ms interval below, implements independent display-link presentation and
records the new 90-second gameplay and UI-stall regression results. This report
retains the earlier build's measurements.

## Environment and procedure

Apple M4 Max, macOS 27.0 (26A428), direct Metal renderer, x86-64 runtime under
Rosetta, 120 Hz display. Render settings: 1920×1080, cap 120, V-sync on, MetalFX
Spatial on, original game effects/AA, model detail 0, optimized lights on,
offline. Experimental temporal AA was enabled only in each test's copied
settings; the user's settings were not changed.

Each launch cloned `out/macos-data/user` into its own output directory and used
`BB_USER_DIR` to isolate saves. The same Central Yharnam save and starting view
were used. The launcher used the original CUSA00900-backup game directory and
the existing prepared ELF. An explicit DLC path was supplied, but the launcher
reported zero mounted add-ons; these runs do not establish DLC support.

Artifacts are local under `out/gameplay-2026-10-18/`. The directory date is the
start of the investigation; final runs continued after midnight. Captures
deliberately drain the GPU. Their timings must not be used as a performance
baseline. Performance runs disable raw, shader and framebuffer capture.

## Native object-motion contract

Eight lossless scene captures covered stationary frames, horizontal/vertical
camera input, stopping the camera and character attacks. They retain exact
native RGBA8 UNORM velocity, scene depth and RGBA16Float pre-tone-map HDR,
original GCN, generated MSL, resource bindings and constant blocks. Native
registration was observed through checked in-memory hooks at original RVAs
`0xbb5430` and `0xbd3c50`; both original functions still execute.

The active mode was application overwrite, velocity enum 1. The external native
surface was 960×540 at `0x806a248000`, with filtering through `0x806a488000`.
Its clear is `(127,127,255,0)`. Verified fragment producers
`ba34012ce43413a9` and `dc9a11395024c0e9` encode:

```text
delta = currentClip.xy/currentClip.w - previousClip.xy/previousClip.w
RG = saturate(0.5 + 0.5 * producerScale * delta)
B  = currentViewZ / (currentViewZ + 10)
A  = 1
previousUV = currentUV + (RG - 0.5) * (-1/scale.x, +1/scale.y)
```

Captured producer scale was `(4.444444656, -2.5)`, yielding UV decode
`(-0.225, -0.4)`. The registration's `(0.1,0.1)` and YEBIS consumer's separate
blur constants are not the TAA decode. The merged/clamped blur output is not
used as a motion-vector source.

Five exact vertex shader families were audited for separate current/previous
rigid or skinned transforms and VP matrices. At runtime both VP matrices must
match consecutive observed rendered cameras. Unknown writers invalidate the
entire candidate; mapping generation and frame IDs must agree. The verified
dilation pass propagates this provenance only through its checked contract.
That contract includes the actual four clip-space corners and screen-identity
UVs, strip topology, viewport, write masks, source swizzle/mip and sampler.
An uncleared ping-pong destination requires a full overwrite; a partial scissor
requires an already valid current destination.

The stationary clear covered about 95.22% of native texels; most scene geometry
therefore still needs camera reprojection. Covered pixels can belong to another
surface after dilation: about 8.8% of full-alpha texels in one stationary capture
failed depth ownership. TAA consequently requires full alpha, compatible B depth,
finite nonsaturated RG and motion beyond one quantization step. Partial alpha,
rounding-only residuals and rejected inputs keep camera motion.

GPU fixtures compare history against known signed/subpixel object translations.
Mean absolute error fell 85–98% relative to camera-only history in these
synthetic cases. This is fixture evidence for decoding/reprojection, not a
whole-game image-quality percentage. Fixtures also cover history gaps/resets,
bad formats/extents, signed scales, coverage, depth mismatch, saturation, HDR
and alpha. Native and Rosetta Metal fixture runs passed.

Evidence: `calibration/scenarios.json`, `calibration/raw/`,
`calibration/shaders/`, and `calibration/native-velocity-vs-contracts.json`.

## HTILE finding and implemented correction

The eight frames contain 15,566 stage/dispatch records and 227,524 buffer-read
instruction instances across 174 shader hashes. There were no resolved graphics
HTILE overlaps and two compute overlaps per frame:

* `595f38bd3818a17b` reads/modifies/writes shadow metadata state bits. Its
  low-bit comparison/replacement is not a depth-min/max unpack.
* `0bd0f61071180d37` is the verified raw surface-copy family. Its descriptor
  spans an adjacent metadata allocation; the port substitutes an ordered
  Metal texture copy.

The 397 unresolved instruction instances reduce to 35 distinct sites in
vertex bone/index and material/fog constant paths. Their instruction flows were
examined; none established a depth-extrema consumer. Descriptor overlap records
are not traces of executed lanes, and direct pointer loads/unobserved areas are
not fully covered. This is not proof that the entire game lacks HTILE culling.

The original allocation helper at RVA `0x109e570` establishes allocation sizes
and 64/512-pixel alignment. It does not establish the per-tile swizzle or depth
quantization. Generating guessed depth bounds would not be justified by this
capture set.

The implemented correction restricts conservative full-range substitution to
actual read-only metadata descriptors. It excludes read/modify/write and atomic
resources, including differently described overlapping write ranges; unknown
writers refuse substitution. Repeated read sites fill a base only once. This
preserves native shadow metadata state and removes redundant fills without
inventing an HTILE encoding. Accurate per-tile bounds remain unimplemented.
The inherited metadata registry also lacks mapping-generation tags; this change
does not establish correctness for all metadata address-reuse cases.

Evidence: `calibration/htile-audit-summary.json`, original `.gcn` dumps and
their stage/resource JSON records.

## Performance and stability

The final native/camera runs used the same runtime SHA-256:
`0939f2f80798534e32c862f9f9590e4cb108a5d39c56bf9a6cc0020050ecc613`.
Each measured 60 seconds after scene detection and a 15-second warmup, with
20 seconds stationary, 20 seconds camera input and 20 seconds repeated attacks.
No tests, builds or other game instances ran concurrently.

| Final-build run | Presented frames | FPS | Median / p95 / p99 ms | Maximum ms | Intervals >50.01 ms | GPU failures |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| Native object motion | 2,249 | 37.39 | 25.0 / 33.3 / 33.3 | 383.3 | 1 | 0 |
| Camera-only TAA | 1,576 | 26.20 | 41.7 / 50.0 / 50.0 | 50.006 | 0 | 0 |

All native samples have valid presentation timestamps. One of 1,577 camera-run
submissions has no presentation timestamp and is excluded from its presented
frame count. Frame-time percentiles exclude zero/unconfirmed intervals.

These are observations, **not evidence of an FPS gain from native motion**.
An earlier camera-only run on the preceding build averaged 37.84 FPS. View and
streaming equivalence were not independently established, and draw counts
varied despite the same save/settings/scripted input. A single pair cannot
separate those differences from thermal/background load or shader behavior.
Native reprojection still had one large stall; stutter is not eliminated.

Final native telemetry admitted 2,815 of 2,816 resolved frame inputs. The camera
run admitted none, as requested. The prewarmer loaded 64 recipes, prepared all
64 and reported 95 pipeline hits in both runs. That proves real cache use,
not a measured cold/warm stutter reduction. GPU CPU-wait telemetry reported no
timeouts. Both benchmark processes were deliberately terminated after collecting
their windows (`SIGTERM`, recorded status -15), rather than crashing.

The five-minute native-motion soak completed with 7,315 frame submissions and
no crash, GPU error or fence timeout. It repeated camera turns, attacks and
two-second forward/backward walks. The process was deliberately stopped after
the window. Native telemetry admitted 7,818 of 7,859 resolved frame inputs;
the remaining frames used the fallback. Process RSS ranged from 3.84 to 4.21 GiB,
starting at 4.04 and ending at 4.18 GiB. This short run does not establish the
absence of a memory leak or account for all GPU memory.

Only 6,036 submissions have confirmed presentation timestamps; the last roughly
100 seconds have zero presentation timestamps despite continuing GPU render
telemetry and successful requested captures. Consequently the soak has no valid
whole-window display-FPS result. Intrusive readbacks would exclude it from the
benchmark comparison regardless. The initially generated negative FPS was a
diagnostic-script accounting error and was replaced with null, preserving the
raw CSV and original metrics separately.

Seven trace frames produced 28 lossless planes (about 291 MiB) and seven display
images. Every captured HDR/resolved-HDR value was finite, and alpha matched
exactly in all seven pairs. Geometry, fog, lighting, character and HUD were
visually inspected during stationary and action/camera states. Static captures
do not establish the absence of every temporal artifact. These captures are in
`soak-bounded/raw/`, `soak-bounded/frames/` and `soak-bounded/capture-summary.json`.

After the final run, all 28,779 original source-file size/mtime records matched
the pre-test inventory. Original `eboot.bin` SHA-256 remained
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`;
the prepared original ELF remained
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.
All 16 original save-file hashes also matched. See `original-integrity.json`.
All gameplay writes were confined to the cloned test saves/generated data.

The full final build completed 52 CTest entries: 51 passed and one original-ELF
fixture skipped by default. Supplying the supported original ELF separately
passed that motion-hook fixture and the original fence-clock trampoline checks.
The fixtures map private images and leave the original executable untouched.
Failure-injection tests intentionally emit error logs; live game runs are
checked separately for GPU errors and unsatisfied waits.

Artifacts: `final-native/metrics.json`, `final-camera/metrics.json`, each run's
`frame-times.csv` / `launch.log`, `final-validation.log`,
`final-native-hook-validation.log` and `final-fence-validation.log`. Earlier
preflight native runs with zero admitted inputs are excluded. An initial
diagnostic soak used per-frame framebuffer dumping and was stopped; surplus
generated PPMs were removed. Its timings are excluded. The restarted
`soak-bounded/` uses `DC_DUMP_EVERY=0` and only seven requested trace frames.

## Repeat the checks

Build and run the suite as documented in the GPU implementation notes. For
native/camera comparisons, enable `temporal_aa=1` in separate settings and
toggle only `BB_METAL_NATIVE_MOTION=0/1`. Keep resolution, save, route and display
settings fixed; disable `BB_METAL_CAPTURE_DIR` and all `DC_DUMP_*` diagnostics.
Use `DC_FRAME_TIMES` for presented intervals and `DC_GPU_STATS=1` for native
admission, prewarm and GPU failure telemetry. A nonzero admission counter counts
accepted frame inputs, not pixels which passed every native-vector gate.

Only the tested area, actions, machine and session lengths are established.
No hardware GPU fault was induced. Injection fixtures establish error delivery;
they do not prove driver recovery or whole-game/long-session stability.
