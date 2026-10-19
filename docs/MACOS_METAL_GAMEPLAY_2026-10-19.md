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
- `ctest-final.log`: 52 runtime/Metal tests pass. Two fixtures require private
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

## Results still being collected

The first clean native-preparation benchmark averaged 49.87 FPS over 90 seconds,
with p99 25 ms, maximum 41.67 ms, no missing presentation timestamps and no GPU
errors. The same-build control, extended final gameplay test and original-file
integrity results will be recorded here after completion. 60 FPS at native
1080p has not yet been demonstrated.
