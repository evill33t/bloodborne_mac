# bloodborne-native integration, 2026-10-22

The inspected app commit is
[`9d34ad2`](https://github.com/bloodborne-native/bloodborne-mac/tree/9d34ad2e11571ab458781198cd12ef2e2f313a5c),
runtime [`6228f43`](https://github.com/bloodborne-native/bloodborne-runtime/tree/6228f43b2919d1abd1c4185e689048797855e540),
and CPU layer [`b9652d7`](https://github.com/bloodborne-native/bloodborne-recomp/tree/b9652d75edc0a281f06cec0da6ff72e9f5150a70).
The latter two match their repositories' HEADs at inspection. Sources and
licenses are recorded in [bbcpu provenance](../third_party/bbcpu/UPSTREAM.md).

## Integrated

An optional arm64 game process uses bbcpu's x86-64 interpreter and arm64 JIT.
The runtime, SDL, decoders, engine integrations, plugins and renderer all build
for arm64. Graphics continue through our direct Metal renderer. The existing
Rosetta process remains the default while native gameplay is unverified.

The integration includes ELF loading with 4 KiB guest segments on 16 KiB host
pages, unioned page permissions, low guest stacks and TLS, guest thread entry,
once/destructor/exit callbacks, integer/vector/stack argument bridges, nested
callbacks, generated x86 hook storage, frame timing, fence-clock adapters, and
translated alignment-fault handling. Guest x86 storage is not executable on the
host; the translator owns its separate MAP_JIT mappings.

Native command writers use bbcpu entry/return guards and a recursive lock per
buffer. The 336 method RVAs and two compute-job scopes come from upstream.
Supported ELF hashes and expected function prefixes gate installation. This
addresses the upstream race between render and loading writers sharing a command
cursor. It does not establish that every possible writer is covered. The
1024-buffer lock table reports exhaustion and leaves new buffers unguarded.
`BB_CB_GUARD=0` restores unguarded execution for comparison.

Both CPU modes now size direct memory from the configured scene area, retaining
explicit `BB_DMEM_MB` overrides and recalculating automatic values after restart:

| Render resolution | Previous automatic pool | New automatic pool |
| --- | ---: | ---: |
| At or below 1920×1080 | 5056 MiB | 5056 MiB |
| 2560×1440 | 12288 MiB | 5760 MiB |
| 3840×2160 | 12288 MiB | 7744 MiB |

The formula reserves 448 bytes for each additional scene pixel, rounded to
64 MiB steps. These are reservation sizes, not measured physical RAM savings or
FPS gains. Larger scenes and long sessions need gameplay validation.

## Build and select

```sh
bash packaging/macos/build-native-runtime.sh --test
BB_MAC_CPU=bbcpu BB_DATA_DIR=out/macos-native-data \
    BB_GAME_DIR=/path/to/GAMES/CUSA00900 bash run.sh
```

The separate data directory in this example starts with separate settings and
saves. Use a copied save for comparisons. `BB_MAC_NATIVE_RUNTIME_DIR` overrides
`out/macos-native-runtime`; `BB_MAC_CPU=rosetta` selects the existing runtime.
Settings restarts retain the chosen CPU mode. `BB_MAC_CPU=bbcpu bash build.sh`
also selects the native runtime after building the development tools.

The native executable requires no Rosetta. The differential CPU test alone runs
a synthetic x86 reference through Rosetta to obtain expected answers. Both
tested interpreter/JIT executables are arm64. It does not install Rosetta.

The existing checkpoint continuation format contains native x86 registers and
import trampolines. It remains available in the Rosetta build and is not wired
into the arm64 build. Ordinary game saves use the existing save runtime.
The ahead-of-time generator/library loading path is not enabled here.

## Other upstream work assessed

| Upstream addition | Disposition in this port |
| --- | --- |
| Ahead-of-time recompiled game library | Deferred: upstream reports 20–30% higher CPU use than its translator. The published CPU infrastructure is included; no generated game code is copied. |
| Shared Vulkan/Metal renderer and MoltenVK tuning | Our renderer already issues Metal commands directly. Replacing it would discard our established GCN/MSL, resource and synchronization work. |
| Deferred clears and fewer encoder breaks | Our render passes already use deferred clear load actions, grouped tessellation preparation and ordered copies. Further changes need pass-level profiling. |
| GPU-written texture eviction/lighting fix | Our GPU-written color/depth targets are separate from the bounded CPU-upload texture cache. Target alias retirement preserves the rendered data. The Vulkan collector patch does not apply to these containers. |
| Staging-buffer/residency lifetime fixes | Our transient uploads and retired targets already remain alive through GPU completion. MoltenVK residency and 4 GiB buffer-offset workarounds target a different resource path. |
| Dynamic-shadow setting crash | Our effects require a restart rather than re-enabling shadows in a live secondary context. No corresponding live state mutation is introduced. |
| FSR 3.1 and MetalFX | MetalFX Spatial is already integrated directly. Upstream FSR code depends on its Vulkan resources and motion pipeline. Its FPS measurements do not transfer to our renderer. |
| Full command-buffer snapshots | Deferred: our CP also consumes live indirect/GPU-produced streams and has submit/consume auditing. A snapshot policy needs explicit lifetime and mutation validation; native producer guards are integrated. |
| Movie-state/AVPlayer locks | Our AVPlayer currently returns unsupported and skips cinematics. The upstream FFmpeg playback subsystem would require a separate decoder integration. |
| Launcher, settings, input, mods and trophies | Existing Cocoa/SDL and bbhost surfaces are retained. Upstream's Tauri launcher is not a replacement for those integrations. |

## Validation boundary

The arm64 game and diagnostic executables build successfully. Synthetic checks
cover loader contracts, 4000 x86 reference cases through each of the interpreter
and JIT, instruction encodings, fault recovery, TLS/thread entry, nested and mixed
argument callbacks, installed prologue hooks, and concurrent guarded writers.
The native Metal raster, temporal-AA, pipeline-cache and command-bridge fixtures
also run with GPU access. These use synthetic code and shaders, with no game data.
The final native CTest run passed 10/10. Python settings/patch and launcher/mod
checks passed 32 tests, with one platform-specific test skipped.

The Rosetta game and tests build. Its sandbox run passed 55 tests, skipped 42,
and failed four checks at video/Metal initialization. With GPU access, the
affected texture uploader and the pipeline-cache, raster and fence-sync checks
pass. Three window/presenter/overlay checks remain unverified because SDL video
could not initialize in the sandbox; they were not retried by opening windows.
The final focused runtime/checkpoint/timing regression run passed 8/8, and the
separate Rosetta pipeline-cache, raster, fence-sync and texture-upload run passed 4/4.

No game was launched for this integration. Native boot, actual save loading,
gameplay, image quality, whole-game timing, CPU cost and FPS gains are unverified.

### Subsequent gameplay validation

That boundary describes the integration checks above. Later native gameplay
runs booted cloned saves and completed movement, attacks and three area
transitions without GPU failures. The October 22 CPU-mode FPS comparison had
unmatched shader-cache coverage and is invalid for architecture attribution.
The [October 23 follow-up](MACOS_SHADER_CACHE_2026-10-23.md) records the cache
correction, native precompilation and gameplay limits; it does not establish
60 FPS, complete visual correctness or long-session stability.
