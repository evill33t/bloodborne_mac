# macOS GPU stability, native motion and pipeline prewarming

Implemented 2026-10-18–19, following the
[executable audit](REA_EBOOT_GRAPHICS_PERFORMANCE.md). Changes are in the port
and its in-memory hooks. The original game dump and prepared ELF stay untouched.

## Stability and synchronization

Metal submissions carry submission/frame identifiers and the most recent
vertex, fragment and compute shader addresses. A failed command records the
first error, wakes CPU waiters and stops further draw/dispatch/fence publication.
The Mac UI reports the error and ends the process; relaunch is required.
An error cannot be cleared by a later successful submission.

`WAIT_REG_MEM` now parks on completion notifications, with a 250 microsecond
maximum interval between checks for labels written directly by the CPU. Its
five-second deadline becomes an explicit error instead of continuing after an
unsatisfied wait. The pending-fence shortcut remains limited to ordered work
on the same ring; it is never used as proof that CPU-readable data is ready.

GPU-produced indirect draw/dispatch arguments require a successful GPU drain
before the CPU reads them. Label and argument accesses retain runtime mapping
identities, reject unmap/reuse, and use locked backing-memory aliases. Permanent
loader/host labels use checked Mach copies. Misaligned or invalid destinations
fail without an unchecked pointer write. Ticketed pending writes and shared
callback state protect label replacement and frontend/renderer destruction.

CPU dependencies use a ten-second GPU-drain deadline. Checkpoint drains retain
their caller-supplied deadline. Explicit synchronous debug waits and renderer
destruction still use Metal's synchronous completion API.

## Native Bloodborne fence loops

On supported 1.09 executable hashes, the port verifies both original instruction
sequences before installing either call-site hook. It redirects the clock calls
at ELF RVAs `0x33c969` and `0x3a18be` in memory. A changed instruction sequence
refuses installation. Other unsupported executables retain their original code.

The hooks park for at most 250 microseconds at a time. The original comparisons,
first-iteration flush, return values, clock and 100 ms/10 ms timeout arithmetic
remain in the guest. They never write a label to manufacture completion.
Set `BB_METAL_PARK_FENCES=0` before launching to retain the original polling.

With `DC_GPU_STATS=1`, logs include cumulative parked waits, parked microseconds
and fatal label timeouts. These totals can overlap across threads and are not
whole-frame timing measurements.

## Graphics and stutter

Presentation now runs on a dedicated thread. With V-sync enabled,
`CAMetalDisplayLink` supplies drawables without a blocking `nextDrawable` call
on the UI thread. The frame producer is acknowledged only after its presentation
read is committed on the same Metal queue. Resize, refresh, V-sync and scaling
settings are applied on the presentation worker; its callbacks must outlive the
presenter. Teardown joins the worker before draining its GPU submissions.

A missing display-link update for 100 ms permits a pending frame to be skipped
so an occluded window does not strand the producer. This does not signal guest
GPU completion. Skips are counted in `DC_GPU_STATS`; exclude those periods from
display-FPS comparisons. V-sync-off retains drawable acquisition on the worker.
`BB_METAL_PRESENT_THREAD=0` restores the original UI path;
`BB_METAL_DISPLAY_LINK=0` keeps the worker with legacy acquisition.
`BB_METAL_STUTTER_TRACE=/absolute/output.csv` enables slow CPU-stage timings
without GPU readbacks or forced drains. See the
[stutter report](MACOS_STUTTER_PROFILING.md) for attribution limits, the 450 ms
UI-stall regression and valid 90-second gameplay comparisons.

The existing optional temporal AA reduces history weight when current and
reprojected HDR colors differ substantially. This makes independently changing
surfaces less dependent on camera-only history while retaining stable-scene
accumulation, neighborhood bounds, depth rejection, camera-cut handling and
current-frame alpha.

When experimental temporal AA is enabled, the renderer can now use the game's
native half-resolution object-velocity texture. Its contract was checked against
original GCN and lossless gameplay captures. Exact producer shader hashes,
current/previous rendered camera matrices, scale constants, mapping generation,
neutral clear and subsequent dilation must all agree in the same renderer frame.
Any unknown writer invalidates the whole candidate. Missing/stale/unsupported
inputs fall back to camera reprojection.

The verified UNORM producer stores NDC displacement in RG, view depth as
`z/(z+10)` in B and coverage in A. TAA decodes the producer's actual scale;
it does not reuse YEBIS's separate motion-blur scaling or merged blur texture.
Nearest samples require full coverage and compatible depth. Saturated vectors,
partial coverage and differences within one quantization step retain precise
camera reprojection. Existing disocclusion rejection, HDR clipping and alpha
preservation remain. Set `BB_METAL_NATIVE_MOTION=0` to compare camera-only TAA.
Temporal AA remains opt-in; this does not add projection jitter or upscaling.

HTILE fallback now follows actual buffer-instruction descriptor provenance,
including pointer-table bindings. It excludes resources written by the same
shader and fills each read-only metadata base once per dispatch. This preserves
the native shadow state-conversion kernel's low metadata bits and avoids
unrelated overlapping user-data descriptors. Supported read-only depth consumers
still receive conservative full-range bounds. The captured scene did not expose
a depth-extrema consumer; accurate per-tile swizzled/quantized HTILE bounds are
not implemented. See the [capture findings](MACOS_GPU_GAMEPLAY_VALIDATION.md).

The live shader compiler records exact generated MSL and pipeline descriptor
recipes under:

```text
$BB_DATA_DIR/metal-cache/prewarm-v1/<device-registry-id>-<OS-version>/
```

Recipes warm on one compiler worker at the next launch. All workers prefer
first-use work; the remaining three renderer compiler workers do not consume
background recipes. Disk writes also run in the background and pending writes
flush at compiler shutdown. Warm pipeline lookup never waits for a disk write.
Cold first-use draws are preserved. A pipeline is reused only when its current
source and serialized descriptor match exactly; function caching uses the MSL
source hash rather than a reusable guest address/key.

The cache admits up to 64 pipelines. Each JSON recipe is at most 1 MiB, and
startup loads at most 64 files/32 MiB. Corrupt, incompatible and symlinked files
are ignored; successful compilation can replace a corrupt recipe atomically.
Cache roots resolving inside `BB_GAME_DIR` or `BB_SOURCE_GAME` are refused.
Set `BB_METAL_SHADER_PREWARM=0` to disable recording and prewarming.
`DC_GPU_STATS=1` reports recipe loads, ready pipelines and warm pipeline hits.

Production Metal binary-archive creation/serialization remains disabled because
that path previously crashed inside Metal under Rosetta. Existing `.metalarc`
files are not loaded by the live renderer. The new recipes are plain compiler
inputs, not serialized Metal archives.

## Validation and remaining work

The full runtime build and 53 CTest cases completed on this Mac: 52 passed and
the original-ELF motion-hook fixture skipped by default. That fixture passed
separately with the supported original ELF. The suite includes actual
Metal pipeline, HDR temporal AA, raster, shadows, texture coherence and renderer
checkpoint fixtures. New synchronization cases cover mapping reuse, parked
wakeups/timeouts, injected errors, indirect GPU producers and late callbacks.
Pipeline cases cover exact descriptor/source matches, changed descriptors,
corruption recovery, archive isolation, opt-out and game-directory protection.
Native-motion GPU fixtures cover signed/subpixel object displacement, coverage,
depth, quantization, invalid formats/frame IDs, reset, HDR and alpha. Resource
fixtures distinguish read-only descriptors from stores, atomics, RMW resources,
pointer tables and unknown provenance.
The real-window presentation fixture verifies displayed-frame and producer
progress during a 450 ms UI pause, settings transitions and shutdown. It returns
skip status 77 if onscreen presentation timestamps are unavailable.

```sh
cmake --build out/macos-runtime -j 6
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir out/macos-runtime --output-on-failure
ROSETTA_ADVERTISE_AVX=1 out/macos-runtime/bb-gpu-sync-tests \
  --elf out/macos-data/prepared/eboot.elf
ROSETTA_ADVERTISE_AVX=1 out/macos-runtime/bb-native-motion-capture-tests \
  out/macos-data/prepared/eboot.elf
```

The ELF fixture reads the supported original, maps a private copy and exercises
both installed clock trampolines with their verified register/stack layouts.
It also checks unsupported hashes and instruction mismatch refusal. No game
process is launched and no executable on disk is patched by that fixture.

Actual gameplay, lossless capture findings, performance measurements and their
limits are recorded in [gameplay validation](MACOS_GPU_GAMEPLAY_VALIDATION.md).
Hardware GPU faults were not deliberately induced; failure propagation was
checked with injected errors. Spatial light-grid tuning, broader texture write
tracking and longer sessions across additional areas remain separate work.

For measurement, use the same save, route, settings and resolution for baseline
and changed builds. Compare cold/warm launches, frame-time median/p95/p99, CPU
fence-wait totals, pipeline hits and visual captures. Toggle the two environment
variables above independently to isolate fence parking from prewarming.

## Opt-in native capture

`BB_METAL_CAPTURE_DIR=/absolute/output/directory` enables checked, in-memory
observation hooks for original native velocity registration. It records at most
256 JSONL observations and preserves the original functions. Only supported
executable hashes with both exact prologues are accepted. Output roots resolving
inside either game directory are refused.

With `DC_TRACE_FILE=/absolute/request.txt`, change the file's integer token to
capture one renderer frame. `DC_DUMP_FRAMES` or `DC_DUMP_SHADERS` selects the
separate stage/resource diagnostic output directory. Raw native velocity, scene
depth, pre-resolve HDR and resolved HDR retain exact formats and row layouts in
JSON/bin pairs, capped at 64 textures / 512 MiB per process. The resource audit
records descriptor overlaps, not dynamic lane execution. Captures deliberately
drain the GPU and must be disabled during performance comparisons.
