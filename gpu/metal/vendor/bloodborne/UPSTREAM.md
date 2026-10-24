# Direct Metal renderer source

Selected GPU, shader, GNM, VideoOut and event-queue source from
[BlackLineInteractive/Driveclub-Port](https://github.com/BlackLineInteractive/Driveclub-Port),
commit `408fd84aeb76f237658d25c684cef46bc7d48904`.
Archive SHA-256: `5209de13502d379d6ee9be0005bad2d26f110210a670f9b99a6c29a758b34858`.

License: GNU GPL version 2, included in LICENSE. Original source notices are
preserved. Some GNM code derives from shadPS4 and retains its attribution.
No game data is included.

Local adaptations: runtime-owned physical-pool sizing, low-address tessellation
storage and VideoOut labels, the 64-byte flip-status ABI and lifecycle, corrected
29-dword HS shader command bounds, deterministic pipeline-key padding,
BGRA render-target ordering, viewport-scissor enable handling, shader diagnostics,
and conservative recognition of Gnm copy kernels for ordered GPU texture copies.
Additional adaptations include invariant vertex exports, native filtered depth
comparisons, execution of single-control-point quad LS/HS/domain shaders with
Metal tessellation, and conservative typed-fill recognition that keeps private
render targets coherent with guest buffer clears. Diagnostic frame capture can
be requested live, and the renderer exposes a game-flip counter for the FPS title.
Texture views honor integer-to-normalized/sRGB reinterpretation, including the
generated face-texture compositing path.
Quad tessellation also accepts 16/32-bit indexed patches and up to 4096 patches;
GPU blits initialize its scratch buffers. Exact zero-fill kernels use ordered
blits for guest backing storage as well as private-texture clears. Texture cache
fallbacks reject color targets with a different extent when an address is reused.
Compatible RGBA8 color bindings share their stored pixels across integer,
normalized and sRGB formats. Newly registered fast-clear targets honor their
programmed clear color, including the alpha-one transmittance used by blood
compositing. Shader cache identity includes user/system register layout and
compute resource settings, as well as code bytes and fetch shaders.
First binds of supported 32-bit 2D color surfaces detile their existing guest
bytes on the GPU, after queued buffer writes. This preserves partial-render
inputs and clear alpha without racing a CPU upload. Offset maps are cached by
surface layout. Exact RGBA material captures are available with DC_DUMP_RGBA.
Render pipeline cache misses always wait for compilation before issuing their
draw. Discarding a cold draw can lose guest initialization and stall startup;
normal launches no longer require DC_SYNC_SHADERS to preserve this work.
Depth-only passes retain fragment shaders when DB_SHADER_CONTROL enables
discard, depth/stencil/coverage exports or shader side effects, so alpha-tested
shadow casters preserve their transparent regions. Material RGBA captures
include every exposed mip level.
Depth-bounds snapshots follow actual depth draws, clears and GPU copies, and
remain reusable across read-only lighting passes and attachment switches.
Texture reads and surface copies select the latest colour/depth writer at a
shared address; sampled depth surfaces must also match the descriptor extent.
Transient upload storage is recycled only by GPU completion callbacks, with
an independent lifetime and a 512 MiB idle-pool bound. Local and hull shader
preparation share one compute encoder with a buffer dependency barrier.
Repeated tessellation fixtures verify that completed storage is reused; raster
and queued-frame tests continue to cover the resulting rendered pixels.

Viewport conversion is composed once on the CPU and applied in homogeneous
clip space. Full-target viewports preserve position/depth bits instead of
rounding through a divide by W and a multiply back; GPU fixtures also cover
shadow-atlas quadrants, non-default depth ranges and pre-divided inputs.
Explicit one-shot shader captures can include full-size color attachments and
RGBA8 alpha through 4096x4096, so packed shadow masks can be inspected correctly.

Changed CPU-uploaded textures use separate Metal storage while previous frames
remain queued. Descriptor reuse in a frame skips repeated mip layout and range
queries only while the guest mapping generation is unchanged. Register-probe
labels are formatted only when a probe is active. Compute append/consume
counters use M0's high half and return their pre-reservation value, preserving
the game's subsequent masked-lane index arithmetic.
VideoOut supports live frame limits with deadline-based waits. The integration
outside this directory provides persistent in-game settings and optional MetalFX
Spatial presentation, and presents only newly flipped frames. An optional
per-pass GPU profiler is available for diagnostics.
The integration code outside this directory connects these components to
bbport's existing runtime and keeps event queue handles as guest 32-bit IDs.

Further bbport-informed adaptations retain direct Metal: driver-managed pipeline
caching with cold first-use draws preserved, bounded uploaded-texture caching,
live mipmapped texture filtering and optional pre-HUD camera/depth temporal AA.
The frontend validates packet payload lengths and bounds target-size hints;
optional independent register checks use the separately attributed
[`bbport` decoder](../bbport/UPSTREAM.md). Camera constants and retail shader
metadata are validated before enabling the scene resolve.
Explicit binary-archive collection/serialization is restricted to an isolated
development fixture after a real-game Rosetta crash inside Metal's serializer.

This code was developed for another game. With the local runtime integration,
Bloodborne reaches the starting clinic with movement, audio and controller input.
Unsupported shaders and PM4 paths still need validation and fixes; this is not
whole-game compatibility.

The optional arm64/bbcpu runtime shares these production sources. VideoOut and
CP performance-counter timestamps use its virtual 1.6 GHz guest TSC; x86 builds
retain the original host counter. Source provenance and native validation are in
[the integration report](../../../../docs/MACOS_NATIVE_UPSTREAM_2026-10-22.md).

The port also provides an opt-in elapsed command-recording budget at existing
encoder boundaries. It retains nonblocking commit, fence and snapshot lifetime
paths. Native-1080p comparisons did not establish a repeatable FPS gain, so
it remains disabled by default. See
[the scheduling report](../../../../docs/MACOS_PIPELINE_SCHEDULING_2026-10-22.md).

## macOS-only source cleanup (24 October 2026)

Inactive Windows/Linux host branches were removed from the adapted sources.
Darwin behavior, CPU architecture switches, guest ABI interfaces and original
license notices are retained.

## Queue ownership corrections (24 October 2026)

Graphics flip markers now belong to the graphics ring; concurrent compute jobs
cannot overwrite or retire them. Graphics multi-span/indirect-buffer scope and
presentation, label-store and interrupt ordering are retained. User-event
trigger payloads are published atomically through `udata` and triggering does
not create a missing event. Native/Rosetta CPU regressions and combined native
GPU fixtures pass; visible gameplay and performance validation remain pending.
See [queue validation](../../../../docs/MACOS_QUEUE_OWNERSHIP_2026-10-24.md).
