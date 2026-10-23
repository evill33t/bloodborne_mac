# Optional native 1080p MetalFX frame generation

The optional `frame_generation` setting adds intermediate display frames using
native MetalFX. It defaults to OFF. Rendered/simulation frames and generated
presentations have independent counters. The original game dump, original
prepared ELF and master saves remain read-only; gameplay validation uses cloned
saves, isolated settings and copied shader caches.

## Controls and compatibility

- Mac Settings / F1 / L3 + R3: **2× MetalFX frame generation (experimental)**.
- Native game System → Performance: **2x MetalFX frame generation**.
- Settings file: `frame_generation=1`, `render_resolution=1920x1080`, `vsync=1`.
- Requires macOS 26+, a supported MetalFX device, the dedicated presentation
  thread and display link. This Mac's M4 Max supports the native API.
- Changes apply live. A pending render-resolution change still requires the
  existing restart. Unsupported modes preserve ordinary real-frame presentation.
- The frame cap continues to control rendered frames. Input, simulation, wind
  animation and guest flip/fence clocks do not advance for generated frames.

Rendering and interpolation both remain 1920×1080. Window scaling is independent;
the performance harness explicitly requests a physical 1920×1080 window and
records its actual pixel dimensions, preventing Retina scaling from silently
adding another output-resolution workload.

The first output-size pilots exposed a hidden settings-overlay layout constraint
that enlarged the window to 1920×1264. Its preferred panel dimensions now yield
to the game view. A Cocoa regression reproduced the original height expansion
and passes after the correction, including scrolling, save/restart and input
handling. Both incorrectly sized pilots are excluded from the 1080p comparison.

## Input provenance and image handling

The direct Metal renderer observes the verified retail scene boundary, snapshots
scene depth and current-to-previous motion, and snapshots the LDR scene before
Scaleform draws the HUD. Native RGBA8 motion is converted to full-resolution
RG16Float pixel motion with the existing camera/depth/alpha validation; pixels
without a valid object vector use camera reprojection.

The actual retail display copy is recognized by its translated shader identities
(`601fd916b9e41328` / `1c472a7564e37de2`), procedural full-screen rectangle,
viewport, complete scissor, target, resource shape and write revision. The live
Metal trace establishes that its source scene belongs to the same emulated flip
epoch. A matching texture address from an earlier epoch is rejected, since the
game can overwrite a reused UI texture while a fresh scene observation is absent.

The HUD-free snapshot follows the actual bound texture-view format **and
component swizzle**, then the display pass's gamma LUT and sampler. This matches
the completed frame's color convention. MetalFX interpolates only HUD-free
images. A separate pass restores current HUD pixels, including a two-pixel
boundary, wherever the completed image differs from the HUD-free image by more
than the allowed UNORM rounding tolerance.

Known invalid inputs, scene cuts, projection changes and long source gaps reset
history. The reset encode and three consecutive priming encodes present only
real frames. The conservative warmup follows a cold-start Rosetta GPU test that
caught an additional current-frame copy. Warmup copies are not counted as
generated frames.

## Presentation and lifetime

Snapshots use a pool of at most eight slots. Shared ownership and GPU completion
leases prevent reuse while a snapshot is queued or being displayed. Exhaustion
falls back without a CPU GPU-wait loop. Loading/menu frames can use owned real
snapshots without valid interpolation history.

Committed snapshots carry a Metal shared-event readiness value. The producer
can resume once the presenter retains the private packet. The display callback
checks readiness before reserving a presentation, and a separate Metal queue
waits on the same event to establish cross-queue visibility. It presents the
generated midpoint followed by its real frame, spaced on display-link ticks.
A second event preserves presentation order when V-sync or frame generation is
toggled and commands move between queues. Hidden windows retain the existing
bounded guest-unblocking behavior.

The title and status show **rendered / displayed FPS** when requested. Optional
`BB_METAL_FRAMEGEN_STATS` CSV counters record rendered, displayed and generated
displayed totals independently, with actual output dimensions. `DC_FRAME_TIMES`
adds numeric generated/source-frame columns. Display counts use real drawable
presentation callbacks, not GPU completion or an assumed multiplication.

## Validation and retained failures

Native and Rosetta production GPU fixtures check intermediate moving-edge
positions, camera and native object motion, exact HUD pixels, non-identity
component swizzles, stale-frame/revision rejection, retained snapshot content,
history invalidation and owned real-frame fallback. The real-window fixture
checks generated/real presentation counters, source ordering, UI-stall isolation,
settings changes and shutdown. Existing settings tests cover the opt-in field's
default, save/load roundtrip and live application.

The first pilots are retained as rejected evidence:

- `fg23-pilot-v8` and `v9`: display-copy epoch admission prevented generation.
- `fg23-pilot-v11`: a missing source-view swizzle made the HUD mask cover 99.99%
  of the image and overwrite almost all interpolation. Its apparent doubled
  presentation count is **not** accepted as image-quality evidence.
- `fg23-pilot-v13`: corrected image handling and separate presentation queue;
  includes detailed captures and recording, so its FPS is diagnostic only.

Three corrected v13 capture triplets have HUD-mask coverage of 2.21–2.26%, exact
masked channels, finite motion buffers and 19.0–86.5% of pixels changed from the
current real frame. Previous/current/generated images are retained. Sampled
character motion shows some interpolation softness around fast-moving geometry;
the option remains experimental and is not claimed artifact-free. These captures
do not provide ground-truth midpoint images or an input-latency measurement.

Native 60 rendered FPS remains separate from 60 displayed FPS.

The v17 native suite passed 14/14 tests in 10.58 seconds. The v17 Rosetta suite
passed 106 tests, with two explicitly skipped hardware/capture fixtures, in
134.12 seconds. The final v19 build additionally includes the tested overlay
constraint correction and the test-only physical-window sizing adjustment;
the interpolation and presentation algorithms are unchanged from v17.

### Normal launcher performance at native 1080p

The normal Rosetta launcher also has a clean OFF/ON pair using the same frozen
Rosetta v19 executable, shader bank and cloned Hunter's Dream pose. Each has a
45-second warmup followed by 60 seconds of stationary measurement, AC power at
100%, no capture/profiling, and verified 1920×1080 rendering and output.

| Setting | Rendered FPS | Displayed FPS | Generated displayed FPS | Display p99 | Maximum display gap |
| --- | ---: | ---: | ---: | ---: | ---: |
| Off | 32.49 | 32.47 | 0 | 33.33 ms | 41.67 ms |
| On | 29.44 | 58.89 | 29.45 | 25.00 ms | 41.67 ms |

Both runs pass the strict timing selection: no missing presentation timestamps,
unavailable-drawable skips or GPU failures. Rates are counter deltas over about
59.1 seconds inside the measured window; one-frame boundary differences explain
small departures from an exact 2:1 displayed/rendered ratio. The ON run displays
about twice its own rendered rate, and about 1.81 times the OFF displayed rate.
The interpolation cost lowers rendered FPS. One accepted pair is not a broad
performance guarantee; this does not reach a sustained 60 displayed FPS or
60 rendered FPS.

### Native CPU mode at 1080p

The four final stationary Hunter's Dream runs use the native `bbcpu` mode,
AC power at 100%, the same frozen v19 executable, native 1920×1080 rendering
and a verified physical 1920×1080 output. Each measured 60 seconds after a
45-second warmup and all-ready shader gate. Rates below are deltas of actual
flip/presentation counters over approximately 59 seconds inside that window.

| Run | Setting | Rendered FPS | Displayed FPS | Generated displayed FPS | Strict timing selection |
| --- | --- | ---: | ---: | ---: | --- |
| OFF A | Off | 38.85 | 38.85 | 0 | Excluded: one missing presentation timestamp |
| ON A | On | 29.76 | 59.54 | 29.76 | Excluded: one missing presentation timestamp |
| ON B | On | 28.65 | 57.32 | 28.65 | Accepted |
| OFF B | Off | 34.26 | 34.26 | 0 | Accepted |

The accepted ON B / OFF B samples have displayed p99 intervals of 25.00 /
33.33 ms and maxima of 41.67 / 50.00 ms. Neither has unavailable-drawable
skips or GPU failures. The excluded runs remain visible above to show variation;
they are not used as strict timing evidence. The accepted ON sample displays
about twice its own rendered rate, but interpolation reduces the rendered rate
relative to OFF. This is not a claim of 60 rendered FPS, a stable 60 displayed
FPS floor, or a latency improvement. Only one accepted pair is available.

### Movement, transitions and image quality

The final native replay completed 300 seconds of movement, camera turns and
combat with three successful lamp transitions: `18010000 → 15000000 →
18010000 → 18000000`. Loaded-map observations occurred approximately 78.25,
152.82 and 228.00 seconds after the measurement anchor. Interpolation fell
back during loading and resumed afterward. The final state reports 8,614
generated encodes, four history resets, three allocated slots and zero slot
rejections. The process reached its planned stop with no GPU failures. RSS
peaked at 4,649.9 MiB and ended at 4,233.3 MiB; this is a short stability run,
not a long-duration memory-leak proof.

This replay includes readback captures, recording and CPU timing scopes and is
excluded from FPS comparisons. It had eight presentations without timestamps
and a maximum observed display gap of 116.67 ms. Frame generation does not
eliminate all transition stalls or dropped presentations.

The three final native capture pairs are consecutive real source frames. Their
HUD masks cover 2.23–2.27% of the image, and all masked channels match the real
HUD byte-for-byte. Motion and depth buffers are finite. Generated images change
28.1–97.6% of pixels relative to the current real frame. The stationary foliage
and character crops retain recognizable geometry and HUD placement. Rapid
camera motion exposes blocky disocclusion edges and foliage softening; fast
character/weapon motion can show ghosting. These are remaining quality limits,
not a passed claim of artifact-free interpolation or corrected legacy foliage
animation. Motion blur and chromatic aberration were enabled in the game in all
matched runs, and their blur/fringing is also visible in real source frames.

An eight-second gameplay recording and previous/generated/current crops are
retained in `quality-final/` and the raw replay directory. The recorded window
includes decorations and shadow, so its video dimensions exceed the separately
verified 1920×1080 game content.

The normal Rosetta runtime completed a separate 65-second stationary/camera/
combat replay with native 1080p output, planned stop and no GPU failures. Its
three captures have consecutive sources, finite depth/motion, 2.26–2.28% HUD
coverage and exact HUD channels. They change 23.5–94.6% of pixels relative to
the current real source. Captures and recording exclude this replay's FPS from
performance selection; it had two presentations without timestamps.

Its first pilot stopped at the saved-position gate because the isolated
test-data copy contained an ARM-only gameplay probe. The rejected pilot is
retained; an x86 build of the same probe is used for the successful repeat.
This was a test-fixture architecture mismatch.

### Original-file preservation

Final verification finds all 28,779 original game files unchanged by size/mtime
inventory, the original `eboot.bin` and prepared original ELF hashes unchanged,
and all 16 master-save file hashes unchanged. Test saves, settings, plugins,
prepared runtime images, captures and shader banks live outside the dump.

Original executable SHA-256:
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`.
Original prepared ELF SHA-256:
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.

### Tested artifacts

Tests ran on M4 Max, macOS 27.0 (26A428). The API is guarded for macOS 26+;
macOS 26 itself was not tested here. Frozen final runtime SHA-256 values:

- Native `bbcpu`: `5886b4f625a5157373a4eca2dcef902c2837856610bf70027870539d9811066b`.
- Normal Rosetta: `d7485a4ee16dfccf396e2fca25b95d0553a2dc1e58481c02f20146688a60fd32`.

## Evidence and reproduction

Evidence directory: `out/framegen-2026-10-23/`. Raw gameplay directories:
`out/metal-unattended-2026-10-20/fg23-*`. The benchmark uses one frozen native
executable, corresponding copied recipe banks, all-ready prewarm gates, a common
45-second warmup and OFF/ON/ON/OFF order. No builds, captures, profiling or other
GPU fixtures run during the clean measurements.

The opt-in quality capture uses `BB_METAL_FRAMEGEN_CAPTURE` and
`BB_METAL_FRAMEGEN_CAPTURE_TRIGGER`; it stores bounded raw previous/current/
generated/HUD-free color, motion and depth inputs with frame metadata. These
diagnostic runs are excluded from performance selection.

Design reference: Apple's [MetalFX frame interpolation and UI/pacing guidance](https://developer.apple.com/videos/play/wwdc2025/211/).
