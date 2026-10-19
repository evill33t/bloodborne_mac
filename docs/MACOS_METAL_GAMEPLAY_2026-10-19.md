# Bloodborne-focused Metal work and extended gameplay validation

Date: 2026-10-19. Target: Apple Silicon, direct Metal, native 1920×1080.
All launches use a copied user directory and separate prepared data/cache under
`out/long-gameplay-2026-10-19/`. The original dump and master saves are excluded
from writes. The test plugin records position and map IDs and invokes existing
native lamp travel; it does not change health, progression or unlock flags.

## Backend changes

The captured light-volume local and hull shaders prepare one control point per
patch and constant tessellation factors. The previous translation cleared
64 KiB of emulated LDS per patch and ran separate local/hull compute encoders
before each draw. The new Metal domain wrapper fetches the checked attributes
directly into a small local payload and uses one immutable constant factor
record. This removes those compute encoders and their render-pass boundaries.
Hardware tessellation, domain arithmetic, winding, partitioning, draw order and
guest fence dependencies are retained.

Selection requires exact compound GCN hashes, including inline fetch code and
system-input metadata: two local layouts, two hull programs and four domain
programs. Unsupported programs, layouts and attachment-feedback cases use the
original translation. `BB_METAL_NATIVE_TESSELLATION=0` is the control switch.
`DC_GPU_STATS=1` reports native and translated preparation draw counts.

Fragment generation now proves when its inserted discard checks are unreachable.
A forward control-flow analysis tracks saved scalar/EXEC constants, keeps both
arms of dynamic branches, and requires full EXEC at every export and an export
on every exit. Unknown restores, masked exports, unsupported flow and loops
keep the old checks. This permits Metal's normal depth rejection optimization;
it does not force early tests, change depth writes or remove depth-bounds kills.
`BB_METAL_OPAQUE_EXPORTS=0` retains the original generated checks. Both dominant
captured G-buffer programs pass the proof; a masked captured program fails it.

The canonical typed RGBA8 fill now also handles finite nonzero values through
the existing contiguous Metal fill kernel. It retains the private attachment
clear and the original descriptor, coverage and CPU/GPU ownership checks.
Backing bytes use the original conversion's clamp and ties-to-even rules.
`BB_METAL_TYPED_FILL=0` restores the translated backing kernel.

Shader recipe prewarming previously stopped learning at 64 entries. The extended
route exposed first-visit pipeline waits of 2,635 ms in Hunter's Dream and
625 ms in a later area. The cache now retains/learns up to 512 recipes and
128 MiB, with a 1 MiB limit per recipe and bounded queued inputs/writes. Oldest
generated entries can be replaced as new areas are visited. Recent entries warm
first; foreground compilation keeps priority. This improves learned routes on
later launches; an unseen shader still requires its first compilation.
Production Metal binary archives remain disabled.

Constant snapshot allocations now carry explicit lifetime leases. All stages of
a draw reserve their snapshots before creating encoders, and the allocations
remain pinned across diagnostic commits. Reused backing memory must belong to
the current command-buffer epoch; capacity drains recheck the whole batch and
oversized batches fail closed. This fixes an independently demonstrated arena
lifetime hazard. It is not evidence that every nested guest constant payload is
immutable or that this hazard explained every reported visual artifact.

Requested-frame profiling is available as `DC_GPU_PROFILE=trace` (pass costs)
or `trace-draws` (draw costs), triggered by a changed `DC_TRACE_FILE` token.
These modes split GPU work and must be excluded from FPS measurements. Raw
captures and screen recording are likewise diagnostic runs. Shader captures now
include the fetch-code sidecar needed to reconstruct complete vertex programs.

## Evidence and validation

Artifact root: `out/long-gameplay-2026-10-19/` (ignored local outputs, including
proprietary shader captures; they are not embedded in source or this document).

- `native-tess-equivalence.log`: 1,440 preparation cases, including 1,260
  executions against original captured local/hull shaders and 180 sparse hull
  oracle cases; 144 domain cases using the four actual captured programs.
  Finite/infinite output bits match exactly; paired NaNs ignore payload bits.
  Tests cover layouts, factor variants, indices, base offsets, swizzles,
  normalized/integer formats, unmapped reads and range canaries.
- `fill-fast-fixture.log` and `fill-gcn-fixture.log`: exact backing-byte and
  canary checks for the new and original fill paths, including rounding cases.
- `cache-validation.log`: more than 64 distinct pipelines persist/reload and
  produce exact warm hits; count/byte eviction and symlink protection pass.
- `ctest-final.log`: 54 runtime/Metal tests pass. Two fixtures require private
  inputs and skip by default; run them separately with local captures/original
  ELF before claiming those paths verified.

## Fog, arch and distant-wall investigation

The user identified the fog, distant walls and arch in the original Central
Yharnam saved view. `fog-depth-original/` and `fog-depth-native/` contain paired
raw Depth32 and HDR captures plus window-only recordings with temporal AA off.
Camera matrices are constant within each run. Sampled arch/wall/fog depth is
bit-identical between the four captured frames, while HDR color varies. The
variation persists with native preparation. This rules out main scene-depth
instability and temporal history as explanations for the sampled color changes;
it does not establish correct lighting or fog, nor exclude other flicker.

The native preparation optimization is not a confirmed fog-flicker fix.

Later window recordings still contain isolated fog dips. A bounded asynchronous
capture records 200 exact before/after-particle HDR regions and the sampled
depth copy without capture-induced GPU waits. Its 600 payloads parse correctly;
small residual changes cluster at frame phase modulo four. This locates a
remaining particle-path question but does not distinguish guest cadence from a
renderer defect. Neither the synchronous nor asynchronous capture proves all
fog flicker resolved.

## Hunter's Dream character and foliage glow

The reproduced green/yellow/red glow originates in finite material-buffer RGB,
before deferred lighting and bloom. A control disables temporal AA, native
tessellation preparation, typed fills and opaque-export optimization together;
the artifact persists. Four character shader families sample the same BC6H
environment cubemap. The initial float material capture contains green=1,855
on the left weapon and green=2,462 on the right-hand weapon region.

The actual T# has POW2_PAD set (`word3=0xb6d703ac`), depth=0, six visible cube
faces and eight mip levels. AMD's physical layer count is computed from DEPTH,
multiplied by six for cubes, then rounded up to a power of two. Thus this image
has eight physical slices per mip. The upload had incorrectly used the six
visible faces as the physical stride. Mip 0 was valid, while mip 1 started at
`0x18000` rather than `0x20000`; the old chain size was 159,744 rather than
212,992 bytes. Decoding those wrong blocks produces green up to 63,040.

The fix separates the physical layer count from the view range and uses the
same helper for uploaded textures and render-target mip aliases. Visible Metal
faces remain six. Partial array views likewise retain the full physical stride.
Volume texture handling stays on its existing separate path.

- `cube-regression-before.log`: the production upload GPU fixture fails at the
  first padded cube's mip 1 with the original implementation.
- `cube-regression-after.log`: padded/unpadded cubes and a three-layer view of
  an eight-layer array pass all 75 face/mip samples and 19,200 component checks,
  alongside the existing queued-upload lifetime test. Exact BC6H offsets and
  cube-array physical layer counts are also checked.
- The expanded fixture additionally reproduced a pre-existing view bug:
  uploaded textures ignored nonzero BASE_ARRAY (`array-view-before.log`).
  The view now rebases both its slice and mip ranges. Final coverage includes
  cube arrays, nonzero base slices, combined base slice/mip selection and
  inherited BGR/constant-alpha swizzles: 144 face/mip samples and 36,864 output
  component checks. It does not depend on proprietary shader binaries.
- `dream-cube-probe/`: original upload bytes, detiled faces, raw material/HDR
  captures and shader probes. The independent CPU decoder and Metal agree
  bit-for-bit on all decoded RGB components, isolating the erroneous source
  offsets rather than a Metal BC6H decoder difference.
- `dream-cube-fixed/`: corrected 212,992-byte cube captures. All three captured
  environment cubes now decode without the spurious green outliers. The Dream
  cube's corrected mips also match the independent CPU decoder bit-for-bit on
  Metal. All four live HDR captures contain zero strongly green pixels
  (`G>1 && G>10*max(R,B)`), versus 1,173–1,300 in eight pre-fix frames. Peak green
  falls from 4,000–6,272 to 3.027–3.422; the remaining bright pixels are ordinary
  scene lighting. The character material itself peaks at RGB approximately
  `(0.02400,0.02441,0.02409)`. All captured components are finite.
- Independent review of the four displayed frames confirms neon weapon,
  hip and foliage cores/bloom are gone while candle lighting and normal weapon
  shading remain. Camera matrices match the pre-fix captures exactly. All 442
  decoded frames of the fixed eight-second video contain zero bright-green
  threshold pixels in the sampled character and foliage regions; every frame
  of the corresponding pre-fix video contains them. This verifies the sampled
  Dream scene, rather than whole-game visual correctness.
  `graphics-audit/dream-glow/live-cube-fix/` retains the numeric
  comparison and image contact sheet. Diagnostics disable temporal AA and the
  three optional optimizations for a causal comparison; a separate post-fix
  movement run uses the normal settings.

The analysis-only CPU decoder is [bcdec](https://github.com/iOrange/bcdec),
downloaded into ignored diagnostic output, not added as a port dependency.
Header SHA-256: `134520764d96f70a27db814173616c89ad85db95fbd3328417c7a8c77e7ca189`.

## REA findings and boundaries

Fresh REA CLI/Hopper decompilation of RVA `0xea4410` confirms a ceil-divided
spatial grid with 40-byte cell allocations. The analysis-only ELF preserves the
two selected original routines at their RVAs; external callees/globals are
absent and the artifact must never be executed. Source SHA-256 is
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`;
analysis artifact SHA-256 is
`db709addbbb114b2eee801826b36813ef692be3fe7319f2307427a747a5635a1`.
REA evidence ID:
`ev_183e480c41d9d7c70a55f994fbc61157f7d5d9c063a7d54743ac93541f6a362b`.
The existing MCP native session retained a cleanup failure; the fresh CLI job
completed and cleaned up without changing REA/client configuration.

The original light-grid setting averaged 38.47 FPS versus 38.52 FPS with the
existing optimized setting over the earlier matching 90-second route. This
does not support a useful FPS gain from changing cell size, so the setting's
behavior is retained. No speculative per-tile HTILE depth-extrema work was
enabled. See the [earlier capture boundaries](MACOS_GPU_GAMEPLAY_VALIDATION.md).

## Gameplay and performance results

The clean same-build comparison used two 90-second runs: 20 seconds stationary,
20 seconds camera rotation, then repeated attacks. Temporal AA/native motion,
the original effects and native 1920×1080 rendering were enabled; cap 120,
vsync on, Apple M4 Max. No profiler, capture or sampler ran during these runs.

| Preparation | Presented FPS | p99 | Maximum | Missing presentation timestamps |
| --- | ---: | ---: | ---: | ---: |
| Original translated local/hull | 38.78 | 33.33 ms | 58.33 ms | 0 |
| Fused Metal local/hull | 49.87 | 25.00 ms | 41.67 ms | 0 |

This is a 28.6% gain. Both runs had zero GPU errors and zero unavailable-drawable
skips. Executable SHA-256:
`9e5a63823d04def16d55d071218e6d12c3f752a8b14239890d4b51e6ed6d276f`.
These runs precede the opaque-export, snapshot-arena and cubemap-layout changes.
A later final-build 90-second run measured 46.50 FPS but includes 1,583 ms of
cold pipeline waits; it is not a clean comparison with the warm pair above.
60 FPS at native 1080p has not yet been demonstrated.

The extended final route (`long-movement-final/analysis.json`) ran for 12.5
minutes with movement, attacks and five successful lamp transitions across
Central Yharnam, Cathedral Ward and Hunter's Dream. There were zero GPU failures
and zero GPU fence timeouts. The longest recorded first-use pipeline wait was
387 ms; 56 such waits totalled 8,483 ms as new areas/shaders appeared. This is
evidence of cold shader compilation, not identification of the historical
383 ms UI stall's exact stack. The run ended with the planned SIGTERM.

The whole-route 35.49 presented FPS includes loading and about 32 seconds of
window occlusion. It must not be used as a comparable gameplay benchmark.
This long run precedes the cubemap fix; post-fix verification is separate.

The post-cubemap-fix normal-settings route (`cubemap-fixed-movement/analysis.json`)
ran eight minutes with four successful lamp transitions through the same three
areas. Actual player positions change in every area. It recorded zero GPU
failures, zero fence timeouts and zero unavailable-drawable skips; all five CPU
GPU waits totalled 58.9 ms including the 56.9 ms startup wait. No measured
pipeline wait exceeded the 10 ms logging threshold on this learned replay.
Nine draw-recording spikes remain, maximum 180.1 ms, plus texture uploads up to
32.9 ms. They are remaining investigation targets. Whole-route 40.92 FPS and
175 ms maximum displayed interval include loading and are not the clean FPS
comparison. Four presentation timestamps were missing. The run ended with the
planned SIGTERM. This route precedes the separate nonzero view-base follow-up;
the final view behavior is covered by the expanded GPU fixture and subsequent
final-build game runs.

The cubemap-fix executable SHA-256 is
`016da60fdce425b800054a6afe50b069e6a4f6ec6d629845e492158bd925e8a9`.
`cubemap-full-build.log` records the full rebuild;
`ctest-cubemap-final.log` records 54 passes and two expected private-input skips
across 56 registered tests, including the expanded production upload fixture.

The final executable, including nonzero array-view bases and the additional
cold-path CPU trace scopes, is
`b818e3fa493b8944500ecda55cce80b761c04a759f11cb8d960250c0b5953961`.
`final-texture-build.log` and `ctest-texture-final.log` record the successful
full build and 54 passes/two private-input skips. The final production upload
fixture covers all 144 samples described above.

`final-texture-1080p-benchmark/` repeats the same clean 90-second route with
normal TAA/native motion, native tessellation preparation, typed fills and
opaque-export proof enabled. It contains no GPU profiling, captures or recording.
All 4,479 submitted frames have presentation timestamps over 90.187 seconds:
**49.663 FPS**, p95 **25.000 ms**, p99 **25.001 ms**, maximum **41.667 ms**.
There are zero GPU failures, zero unavailable-drawable skips and zero GPU fence
timeouts. No CPU stage reached its 10 ms logging threshold during measurement.
This is consistent with the earlier 49.87 FPS optimized result; it does not
demonstrate sustained native 1080p/60 FPS. The historical 383 ms stall and the
remaining area-transition draw-recording spikes are not claimed fully resolved.

Final integrity verification (`original-integrity.json`) confirms the 28,779-file
source size/mtime inventory is unchanged, the original `eboot.bin` and prepared
original ELF SHA-256 values are unchanged, and all 16 master save hashes match
the starting inventory. The original dump and master saves were not used as
writable runtime test directories.
