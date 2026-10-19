# macOS / direct Metal development

**Experimental: the real game reaches the starting clinic with a controllable hunter.**
The x86-64 Mach-O runtime executes the original game under Rosetta on Apple
Silicon. PS4 GCN shaders and PM4 commands go to a direct Metal backend, with no
Vulkan or MoltenVK runtime. Initial gameplay runs; broader validation is ongoing.

## Build and launch

Use macOS 15.4 or newer with Rosetta, Xcode command-line tools, CMake 3.24+ and
Python 3. No game data or system dependencies are installed by the scripts.

```sh
bash build.sh --test
BB_GAME_DIR=/path/to/GAMES/CUSA00900 \
BB_DLC_DIR=/path/to/DLC/CUSA00900 bash run.sh
```

For the `GAMES/<game folder>` and `DLC/<title ID>` directory layout, the launcher
finds the matching DLC directory automatically using the game's SFO title ID.
This also works when the game folder is named `CUSA00900-backup`. An explicit
`BB_DLC_DIR` overrides discovery and is inherited by settings restarts; an empty
value explicitly disables discovery. The launch log lists the installed add-on
labels, including `SPEXPANSIONDLC03` for The Old Hunters. Saves made with that
expansion require it on subsequent launches.

`build.sh` builds native ARM64 Metal tools in `out/macos/` and the separate
x86-64 runtime in `out/macos-runtime/`. For runtime-only development:

```sh
bash packaging/macos/build-runtime.sh --test
```

The runtime build downloads pinned SDL 3.4.16 and LibAtrac9 source archives with
SHA-256 verification. Native shader tools use pinned glslang and SPIRV-Cross.
The selected direct Metal PS4 renderer is vendored under
`gpu/metal/vendor/driveclub/`; its `UPSTREAM.md` records the source commit and GPL
license. The selected bbhost engine and four native plugins are recorded in
[`gpu/metal/vendor/bbhost/UPSTREAM.md`](../../gpu/metal/vendor/bbhost/UPSTREAM.md).
The combined Mac runtime is GPL-3.0-or-later. Include dependency licenses when
distributing builds.

The launcher checks the dump's executable fingerprint, then writes generated
images to `out/macos-data/prepared/` and separate saves to `out/macos-data/user/`.
It requires the supported 1.09 patch layout, even when SFO APP_VER says 01.00.
The default `original` graphics profile preserves AA, motion blur and depth of
field. Skip Intro is applied only to the generated image. The supported engine
timing integration handles frame pacing and game timing at the selected cap.
`BB_MAC_GRAPHICS_PROFILE=compatibility` additionally disables those three effects
as a diagnostic fallback. Source files are read-only.
`BB_DATA_DIR` changes the generated-data/save root; `BB_USER_DIR` overrides just saves;
`BB_MAC_RUNTIME_DIR` changes the runtime build directory; `BB_BUILD_JOBS` controls
parallel compilation. `BB_TIMEOUT=120` enables a bounded diagnostic session.
The launcher's default has no timeout. Offline remains the default. The Mac
settings panel now includes The Hunter's Dream server selection and device-link
sign-in; live multiplayer validation is pending.

DualSense, keyboard and configured mouse input use the existing SDL runtime. Keyboard defaults:
Space = Cross, Shift = Circle, I/J/K/L = d-pad, WASD = movement, arrows = camera,
Enter = Options. Character-name entry uses a native text field: Return or Cross/A
confirms, Escape or Circle/B cancels, and game input stays neutral during entry.
`BB_IME_TEXT` still overrides it for tests. Mouse look uses the existing `input.ini`
bindings; uncomment `mouse_to_joystick = right` to enable it. F7 toggles capture
and F8 reloads bindings by default. Settings, achievements, text entry and focus
loss release capture and discard pending mouse input. Direct-camera mode currently
uses mouse-to-stick fallback on Mac. Cinematic video decoding is not implemented. AvPlayer init
returns a real failure result, which Bloodborne handles by skipping video.

## In-game settings

The game's **System** menu now contains **Mac Display**, **Mac Graphics**,
**Mac Mods** and **Mac Performance**, backed by the same saved settings.
These cover the display/effect controls below, bundled plugin selection,
light-grid batching and model detail. Native menu edits save immediately;
restart-dependent changes take effect on the next launch. The original Mac menu
and settings panel remain available, including detailed plugin options/actions
and server/account controls. See the
[integration guide](../../docs/BBHOST_MAC_INTEGRATION.md) for formats and limits.

Press **F1**, **Insert**, **Command-comma**, or **L3 + R3** to open settings inside
the game window. Use the mouse/keyboard, or d-pad to navigate, Cross/A to change
or activate, and Circle/B to close. Escape also closes. Game input is captured
while the menu is open, but the game continues running.

- **Frame cap:** 30, 60, 90, **120 FPS**, 144, 165, 240, or uncapped. Changes require
  **Apply & restart** to update the engine's timing mode. These are limits; actual performance depends on the scene
  and hardware. V-sync may impose the display's refresh-rate limit.
- **Render resolution:** 960×540, 1280×720, 1440×810, 1600×900, 1920×1080,
  2560×1440, or 3840×2160. These change actual game rendering and require a restart.
  The launcher automatically adjusts the direct-memory pool above 1080p;
  an explicit `BB_DMEM_MB` overrides automatic sizing.
- **MetalFX Spatial** or bilinear presentation, plus optional extra sharpening.
  MetalFX applies when output is larger than the rendered frame and falls back
  to bilinear if unsupported. It scales the whole frame, including the HUD.
  It does not provide temporal upscaling or frame generation.
- **Texture filtering:** Game default, 2×, 4×, 8× or **16×** (default), applied live.
  Overrides affect linear mipmapped samplers; point, unmipped and comparison
  samplers retain their original filtering.
- **Temporal anti-aliasing (experimental):** an opt-in live setting. A native Metal
  resolve uses scene camera/depth before the game's post-processing and HUD,
  with variance clipping, disocclusion rejection and camera-cut/history resets.
  Verified native object-motion producers now supply half-resolution vectors
  when their current/previous camera, depth, coverage and frame contracts match.
  Unsupported/stale inputs use camera reprojection; moving objects can still
  ghost. `BB_METAL_NATIVE_MOTION=0` selects camera-only comparison. There is no
  projection jitter. It does not provide
  temporal upscaling. Unsupported scene inputs keep the original frame and the
  status reads **Temporal AA waiting for scene**. MetalFX Spatial can be used
  separately for presentation scaling.
- V-sync, fullscreen and FPS title-bar visibility apply live.
- Game AA, motion blur, depth of field, chromatic aberration, ambient occlusion
  and dynamic light shadows require a restart.
- Optional light-grid batching and model detail apply live. Light-grid batching
  defaults off pending Metal performance measurements.
- Randomizer (items, shops, enemies), boss rush, mutators and the developer
  debug menu ship as opt-in Darwin x86-64 plugins. Enabling requires a restart.
- Private-server/account configuration requires a restart. Sign-in credentials
  are kept in Keychain; no password is saved in the settings file.

**Apply & save** writes all choices atomically to
`$BB_DATA_DIR/macos-settings.ini` (default `out/macos-data/macos-settings.ini`).
Every launch restores this file; `BB_MAC_SETTINGS` overrides its path.
**Apply & restart** also relaunches the game through `run.sh`, regenerating the
timing/resolution/effect patches and reloading the last game save. **Restore defaults**
changes the draft; press an Apply button to save it. Closing without applying
discards draft changes. Settings are separate from game saves and Windows INI files.

The new bbhost integration was checked without launching the game: all eight
timing modes installed and the patched frame manager executed against synthetic
state; all four plugin image callbacks passed together at 240 FPS; native assets
generated for 22/22 language bundles. This does not prove high-rate gameplay,
native menu appearance or live multiplayer. Real Metal occlusion queries are
not implemented; current backface culling and conservative HTILE fallback remain.

Development patch-conflict analysis and CPU timing verification are documented in
the [bbhost integration guide](../../docs/BBHOST_MAC_INTEGRATION.md#patch-analysis-and-cpu-timing-verification).

### Pipeline caching and command validation

The game uses Metal's driver-managed pipeline cache, preserving cold first-use
draws. Explicit binary-archive collection and serialization are disabled in the
live renderer: serializing real game compute pipelines crashed inside Metal
under Rosetta. Old files under `$BB_DATA_DIR/metal-cache` are left untouched and
are not loaded by the game. Environment variables cannot re-enable that path.
Binary-archive persistence is restricted to the isolated development fixture.

The renderer also records plain MSL/pipeline recipes outside the game dump and
prewarms exact matches on a background compiler worker at the next launch.
`BB_METAL_SHADER_PREWARM=0` disables this path. GPU label waits now park on
completion notifications, detect invalid mappings and report failures. Supported
1.09 native fence-loop hooks preserve guest comparisons and timeout budgets;
`BB_METAL_PARK_FENCES=0` retains the original polling. See the
[GPU stability and prewarming guide](../../docs/MACOS_GPU_STABILITY.md) for
cache limits, diagnostics and validation. The
[gameplay report](../../docs/MACOS_GPU_GAMEPLAY_VALIDATION.md) records real
captures, performance and stability limits. Opt-in `BB_METAL_CAPTURE_DIR`
diagnostics perform bounded lossless readbacks; disable them for timing runs.
The [stutter follow-up](../../docs/MACOS_STUTTER_PROFILING.md) records the
dedicated presentation loop, controlled UI-stall regression and visible-window
gameplay results.

Reconstructible uploaded textures expire after 20 idle seconds. Under cache
pressure the oldest uploads can leave sooner, with a two-second grace period for
the active scene. Render targets and GPU-owned surface contents are retained.
The default upload budget is one eighth of Metal's recommended working set,
clamped to 256 MiB–2 GiB; `BB_METAL_TEXTURE_CACHE_MB=64..16384` overrides it.

`BB_METAL_CP_VALIDATE=1` compares register-write packets with the independent
bbport decoder without replaying draws or memory writes. Packet-length checks,
bounded render-target hints and indirect-memory checks apply in normal builds.
The decoder and attribution are in
[`gpu/metal/vendor/bbport`](../../gpu/metal/vendor/bbport/UPSTREAM.md).

Focused verification checks that the runtime never calls binary-archive APIs
with either cold or existing caches, plus offline archive hits/corruption recovery, HDR
temporal accumulation/depth rejection and synthetic GCN through the production
scene hooks, plus native text entry, mouse capture and CPU decoder/eviction tests:

```sh
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir out/macos-runtime \
  -R 'darwin-(metal-(quality|pipeline_cache|temporal_aa|temporal-integration|input|integration)|independent-cp|settings)' \
  --output-on-failure
```

These are fixture checks; the gameplay report separately records the tested
scene and settings. Longer sessions and other areas still require validation.

## Achievements


Open **Achievements…** from the Mac application menu, press **Command-Shift-T**
or **F2**, or use the **Achievements…** button in Settings. The native Mac window
shows all 40 trophies, including the six Old Hunters trophies, with progress,
grades, unlock dates, search and All / Earned / Locked filters. It updates as you play.
Game input is captured while the achievement window has keyboard focus.

New unlocks produce a compact black banner at the top left of the game view,
with the trophy's artwork, grade and “You have earned a trophy!” text. There is
no PlayStation logo. Banners queue for 6.5 seconds each, do not intercept input,
and respect Reduce Motion. Artwork is read from the user's
`sce_sys/trophy/trophy00.trp`; missing artwork falls back to a drawn trophy cup.
No game artwork is distributed with the source or build.

The implementation preserves the catalog and `trophies.log` format from
[pyL1nx/bloodborne_pc at 64919f7](https://github.com/pyL1nx/bloodborne_pc/tree/64919f7da13a24bac53578acec5f74f959fe5c4e).
Unlocks are kept beside saves at `$BB_USER_DIR/trophies.log`. Existing entries
retain their earliest timestamps and load silently. Repeated unlock calls do
not create extra entries or banners. Platinum is awarded after the other 33
base-game trophies; DLC trophies are excluded. Unrecorded achievements from old
saves are only added when the game reports an unlock.

The shared C catalog is generated from `launcher/bbport_trophies.py`:

```sh
python3 scripts/macos_trophy_catalog.py --check
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir out/macos-runtime \
    -R 'darwin-achievements' --output-on-failure
```

CPU tests cover persistence, concurrent duplicate calls, platinum, notification
order, write failures and PS4 trophy queries. A hidden-window Cocoa fixture covers
the browser, search/filters, artwork, notification timing, resizing and input
capture; it returns 77 (skipped) if no display is available. The fixture has also
rendered and checked the supplied CUSA00900 artwork read-only. Unlocks from actual
gameplay still need live verification.

## Runtime progress and limits

Verified on an M4 Max with the supplied CUSA00900 dump:

- Original executable and both bundled module initializers execute under Rosetta.
- Darwin guest TLS uses an LDT/FS segment without replacing macOS's GS TLS.
  Guest-visible threads, TLS, image, stacks and GPU labels stay within 40 bits.
- Production memory aliases, files, synchronization, CoreAudio output, ATRAC9
  decoding, SDL controller input, game-data loading and three DLC entitlements
  reach the game's startup paths.
- PM4 graphics/compute submissions, GCN-to-MSL shaders, render targets, fences
  and native window presentation produce the FromSoftware logo, title menu and
  offline setup screens and the clinic. The scene became visible after fixing
  depth/color surface copies, BGRA render-target ordering and viewport scissors.
  The original graphics profile runs at approximately 30 FPS in the clinic on this
  test Mac. This is not a whole-game performance claim.
- The title bar reports game flips per second, updated once per second.
- Typed compute clears update both guest buffers and private Metal targets,
  including neutral motion vectors. This fixes accumulated light trails and
  character ghosting. Native depth comparisons use filtered Metal comparisons.
  Invariant vertex positions preserve depth-prepass matching.
- Depth-bounds lighting snapshots are refreshed after depth writes, clears and
  GPU depth copies. Read-only lighting passes reuse them across attachment
  changes. Shadow/colour address reuse selects the latest writer, and depth
  textures must match the requested extent. A synthetic M4 Max GPU regression
  covers stale-depth rejection, 256 lighting passes without redundant copies,
  clears, depth copies and same/different-size address reuse. These checks do
  not establish an in-game frame-rate increase or eliminate every visual issue.
- Dynamic CPU texture uploads preserve the pixels referenced by earlier queued
  frames. A gated GPU regression verifies two different texture versions without
  waiting for prior GPU reads. Compute work-list tests cover append/consume,
  masked lanes and nonzero GDS allocation bases. Repeated texture binds reuse
  validated layout work within the frame while the guest mapping is unchanged;
  disabled register probes no longer format per-draw diagnostic labels.
- Integer texture uploads can be sampled through normalized and sRGB views.
  This restores the generated face texture and fixes the red player face.
- Quad light volumes execute the game's local, hull and domain shaders through
  Metal tessellation. The current path supports one input/output control point,
  nonindexed or 16/32-bit indexed patches, up to 4096 patches and a single instance.
- Zero compute fills use ordered Metal blits after updating private textures,
  preserving guest-buffer contents while avoiding the translated clear shader.
  Reused texture addresses only select cached color targets of matching extent.
- Runtime/integration checks cover synchronization, guest event-handle
  width, PM4 initialization/shader updates, flip lifecycle, BGRA GPU readback,
  actual GCN rasterization with normal shader-compilation defaults, preservation
  of cold first-use draws, ordered render-target copies, typed clears,
  indexed tessellation, integer-to-sRGB texture views, transparent material
  compositing across color formats, initial fast-clear transmittance, shader
  register-layout changes, queued buffer clears before the first color bind,
  initial micro-tiled sRGB pixels, filtered shadow comparisons,
  persistent settings, Cocoa overlay input/save/restart handling, frame pacing
  and actual MetalFX GPU output.

Remaining limitations include other tessellation layouts, some materials/effects,
cinematic video decoding, longer gameplay/save testing and DLC-area access.
Recognition of DLC entries does not prove DLC gameplay. Existing
Windows FSR/DLSS/frame generation and the Windows launcher are not part of
the Metal port. No packaged/signed app is provided yet.

## Implemented and verified

- A direct Objective-C++ Metal library (`bbmetal`), with native device/queue,
  compute and render pipelines, shared buffers, private textures, upload and
  readback. Submission waits for completion and propagates Metal errors.
- SPIR-V to Metal Shading Language 2.4 translation. SPIR-V is only a compiler
  intermediate; there is **no Vulkan or MoltenVK runtime dependency** in these
  development targets. The game runtime uses the vendored GCN-to-MSL compiler
  and PM4 frontend directly; the SPIR-V tooling is a separate development path.
- Deterministic per-stage remapping of descriptor sets/bindings into Metal buffer,
  texture and sampler slots, fixed-size arrays, combined samplers, reflected
  compute group sizes, and push constants at buffer index 30. Unsupported resource
  categories, auxiliary buffers, dynamic descriptor arrays and slot overflows
  fail explicitly. Specialization constants currently use their SPIR-V defaults.
- 22 existing host-shader variants compile into Metal functions/pipelines: full-screen
  vertex/post-processing, multi-copy, camera motion/debug, TAA/sharpen, upscale
  merge/reactivity, and micro/macro tiling for 8/16/32/64/96/128 bits per pixel.
  Compilation is not proof of equivalent results for every shader.
- GPU results were checked for multi-region buffer copies, full-screen rendering
  with texture/sampler binding, padded-row private-texture readback, and six camera
  motion cases (distant geometry, sky rotation/translation, jitter, and valid/stale
  object motion). Descriptor-set and array fixtures also compile on the GPU.
- The production guest-memory implementation now has a Darwin path: an unlinked
  sparse backing file, aliased direct-memory views, physical/flexible zero-on-reuse,
  anonymous libc `mmap`/`munmap` with guest flags and errno, and collision-safe
  low-address allocation. File-backed `mmap` returns an explicit unsupported
  mapping error. Tests pass as native ARM64 and as
  x86-64 under Rosetta. Darwin's reserved address aperture requires a different
  placement: host-owned guest-visible memory in `[448,512)` GiB, guest mappings in
  `[512,1008)` GiB. All remain representable by the GPU's 40-bit descriptors.
  Explicit guest requests outside that range currently fail; startup and menu
  allocations and the clinic have been exercised with this layout.
- An x86-64 Mach-O CPU probe verifies CPUID features and actually executes a small
  dynamically loaded SysV/AVX function. It passes under Rosetta on the test Mac
  when AVX is advertised for that process.

`Device::ConfigureLayer`/`Present` provide a CAMetalLayer blit/presentation path.
The game runtime uses a separate native window/presenter from the vendored renderer.
Windowed presentation and controller input have been exercised in menus and the clinic.

## Run the checks

```sh
ctest --test-dir out/macos --output-on-failure
MTL_DEBUG_LAYER=1 out/macos/gpu/metal/bb-metal-tests
ROSETTA_ADVERTISE_AVX=1 out/macos/bb-macos-cpu-probe
out/macos/bb-macos-memory-tests-x86_64
```

The GPU executable opens no window. On a headless/restricted host it returns 77
when Metal is inaccessible; CTest records that as **skipped**, not GPU validation.
The CPU helpers intentionally build as x86-64, and running them requires Rosetta
on Apple Silicon. They are not automatically run by CTest and never install Rosetta.
`ROSETTA_ADVERTISE_AVX` above is scoped to that command, not a persistent OS setting.

The existing CUSA00900 dump was inspected read-only. Its `APP_VER` is `01.00` and
`VERSION` is `01.09`; its executable's patch-site fingerprint matches the known
1.09 layout. The existing prepare/link tools accepted its plaintext eboot,
`libc.prx` and `libSceFios2.prx`. Generated inspection/link artifacts live under
`out/` and are ignored by Git. No original game, DLC or save files were modified.

To inspect another dump without preparing or launching it:

```sh
python3 scripts/macos_preflight.py /path/to/CUSA00900 \
    --dlc /path/to/DLC/CUSA00900
```

The report distinguishes the SFO version from the verified patch **layout**.
A matching patch fingerprint only covers the sites used by the built-in FPS and
resolution patches; it does not establish that the entire executable is unmodified.
The upstream Windows launcher's metadata-based version selection is unchanged;
the macOS launcher requires the verified executable patch layout.
The existing host-CPU auto-detection is Windows-specific. For development linking
on this Mac, `BB_LEGACY_CPU=0` was used only after the Rosetta probe verified all
required extensions; this avoids applying the experimental legacy CPU translation
profile merely because the Python process cannot query CPUID. The macOS launcher
advertises AVX for its child process, and the runtime checks the required CPU
features before entering guest code.

To inspect a shader conversion:

```sh
out/macos/gpu/metal/bb-metal-shader compute \
    out/macos/gpu/metal/shaders/camera_motion_comp.spv out/camera_motion.metal
```

## Remaining integration

Extend tessellation to other patch layouts and instancing, and fix
remaining material/effect issues. Linear, perspective and centroid interpolation
are selected from the guest barycentric input layout; a GPU fixture checks
their distinct results with varying clip W. Viewport conversion stays in
homogeneous clip space, with exact full-target transforms and GPU checks for
shadow-atlas viewports and pre-divided inputs.
Character creation, the clinic and initial character reload have been exercised;
broader gameplay, save recovery, DLC areas and longer sessions still need validation.
Implement cinematic decoding, native text entry and packaging.
Central Yharnam live checks on an M4 Max still showed roughly 25-36 FPS at
1920x1080 in the observed views. The regression fixes above have not established
a substantial FPS gain or eliminated the reported camera-dependent building
and shadow flicker; those remain open rendering issues.
Unsupported GPU operations are logged with shader/command diagnostics; the
renderer was originally developed for Driveclub and needs game-specific testing.

For request-only frame/pass captures, create an output directory and launch with
`DC_DUMP_FRAMES=out/captures DC_DUMP_EVERY=0 DC_TRACE_FILE=out/capture-request.txt`.
Write a new integer to the request file for each capture. Add
`DC_DUMP_PASSES=1` for intermediate targets. An optional second hexadecimal word
in the request selects a pixel shader whose input textures are also captured.
An optional hexadecimal instruction PC followed by decimal pixel X and Y
captures that fragment's registers, for example `7 80d3bccf00 574 960 650`.
Append `!` after the shader to arm a capture that waits for its next draw, for
example `8 80d3b74300 !`. It records that shader's inputs and color outputs up to 4096x4096
in the hit frame, then stops tracing. Output attachments use capture indices
16 and above. An optional PC/X/Y probe may follow `!` as well.
`DC_TRACE_CB=<hex address>` traces a particular color target even during loading;
`DC_TRACE_CS=<hex shader address>` does the same for an upload/compute shader.
These diagnostics add overhead and should be omitted during normal play.
`DC_GPU_PROFILE=passes` adds per-pass GPU timings and also introduces substantial
profiling overhead; do not use it for normal-play FPS comparisons.
For exact blood atlas bytes, add `DC_DUMP_RGBA=1`; RGBA8 input and selected output textures up to 4096x4096
are also written as RGBA PAM files, including each exposed mip level.
For a selected pixel shader, `DC_PEEK_PS_CHAIN=2:30:60` reads eight floats from
snapshotted user-data pointer 2, following the pointer at offset `0x30` and
reading at offset `0x60`. Commas separate chains; intermediate offsets follow
guest pointers, and the last offset selects the values to print.

The title-bar FPS counts drawables reported as displayed by Metal, excluding
skipped presentations. `DC_FRAME_TIMES=out/frame-times.csv` optionally records
submission/presentation times and display intervals; once per second the log
also reports median, 95th-percentile and maximum intervals. This uses drawable
callbacks and does not enable GPU counters. Average FPS alone does not measure
frame pacing; a 30 FPS stream can still alternate short and long intervals.
The dedicated presentation thread acknowledges each flip after committing its
presentation read on the shared Metal queue. With V-sync on it uses drawables
provided by `CAMetalDisplayLink`; V-sync-off acquires drawables on that worker.
This keeps later rendering ordered without waiting for the GPU to finish every
frame, and isolates it from UI-event stalls. `BB_METAL_PRESENT_THREAD=0`
restores the UI path; `BB_METAL_DISPLAY_LINK=0` retains the worker with legacy
drawable acquisition. Resize, refresh, V-sync and scaling changes apply live.

`BB_METAL_STUTTER_TRACE=/absolute/output.csv` optionally records slow CPU stages
without pixel readback or forced GPU drains. Most scopes use a 10 ms threshold;
whole UI iterations and frame handoffs use 50 ms. Tracing refuses paths inside
the configured original-game directories. For timing comparisons keep the game
visible, require positive presentation timestamps and check the
`DC_GPU_STATS=1` unavailable-drawable skip counter. Fully occluded windows can
stop receiving display-link updates; after 100 ms the producer may skip a frame
to keep running. Those periods do not provide valid whole-window display FPS.

The Windows reference was reviewed at
[`0a71dd171f15096a01dc0f472aa0bcff12c41c6d`](https://github.com/Mrsuss60/bloodborne_pc_windows_port/tree/0a71dd171f15096a01dc0f472aa0bcff12c41c6d)
for frame pacing, texture alias handling and graphics settings. The macOS overlay
and MetalFX presentation are native implementations; Vulkan barriers, Windows
thread priorities and DLSS hooks are not applicable to this backend.

References: Apple's [Rosetta translation environment](https://developer.apple.com/documentation/apple-silicon/about-the-rosetta-translation-environment),
the [upstream TLS implementation](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/tls.cpp),
and the [direct Metal renderer upstream](https://github.com/BlackLineInteractive/Driveclub-Port).
