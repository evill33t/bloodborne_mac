# Bloodborne for macOS

An experimental Bloodborne port for **Apple Silicon**, with a **direct Metal renderer**
and a macOS runtime for the original PS4 game code. Graphics commands and GCN shaders
are translated to Metal; the Mac game process does not require Vulkan or MoltenVK.
This repository targets macOS only.

**Status: 24 October 2026.** The game boots, loads saves and supports movement,
combat, audio, controller input and area transitions. Both the default Rosetta
runtime and the optional native arm64 `bbcpu` interpreter/JIT have real gameplay
coverage. Rendering correctness, performance and longer sessions remain active work.

> **No game files are included.** Bring your own decrypted Bloodborne dump with the
> supported **1.09 executable layout**, plus your own DLC dump where required.
> This project is not affiliated with Sony Interactive Entertainment or FromSoftware.

## Current status

| Area | Current state |
| --- | --- |
| CPU execution | Rosetta is the default. Optional `BB_MAC_CPU=bbcpu` runs the host runtime, renderer and plugins as arm64 and translates the game's x86-64 code without Rosetta. Whole-game ahead-of-time recompilation is not enabled. |
| Gameplay | Tested paths include the starting clinic, Hunter's Dream, a saved arch area, movement, attacks, save loading and multiple lamp/area transitions. This is not whole-game compatibility. |
| Rendering | Direct GCN-to-MSL translation, PM4 graphics/compute processing, tessellation, render-target copies, depth comparisons, native motion inputs and corrected surface ownership/clears. Foliage jitter and camera-dependent sliding shadows remain under investigation. |
| Performance | Native 1080p is the main target. Scene-dependent results can exceed 60 rendered FPS, but a repeatable whole-game 60 FPS lock has not been achieved. |
| Presentation | MetalFX Spatial scaling and optional temporal AA. Experimental MetalFX frame interpolation is integrated and gameplay-tested, with separate rendered/displayed counters. |
| Stability | Short gameplay and transition replays pass. Longer native runs exposed missing CPU/runtime behavior; segment-register saves, module unwind queries and guest `msync` now have fixes and focused tests. Completion of the original exception path and a post-fix long-session pass remain unproven. |
| Online | The Hunter's Dream client, server selection and Keychain-backed device sign-in are integrated. Offline is the default; live authentication, summons and cross-client compatibility still need validation. |
| Nightwatch Co-op | Experimental source integration with Host/Join settings, encrypted peer networking, shared progression hooks and isolated saves. Requires Rosetta; live co-op gameplay and Internet/NAT traversal on Mac remain unverified. |

The [24 October resource and shadow report](docs/MACOS_METAL_RESOURCES_2026-10-24.md)
records the latest changes and test boundaries. Earlier clean native 1080p replays
on an M4 Max recorded **68.33 rendered FPS in Hunter's Dream** and **48.13 at the
saved arch**, with frame generation off. Those runs used warmed shader recipes and
opt-in resource/scheduling settings; they are scene samples, not default-setting
benchmarks or an isolated speedup claim. See the
[AC gameplay measurements](docs/MACOS_NATIVE_AC_2026-10-24.md) for the full conditions.

## Features

- **Mac and in-game settings:** a native settings panel plus Mac Display, Mac
  Graphics, Mac Mods and Mac Performance pages in the game's System menu.
- **Display and graphics controls:** render resolutions from 540p to 4K, frame
  caps from 30 through 240 FPS or uncapped, V-sync, fullscreen, texture filtering
  up to 16×, effect switches, sharpening and MetalFX Spatial scaling.
- **Experimental temporal AA:** camera/depth reprojection with validated native
  object-motion inputs, history resets and disocclusion rejection. Moving objects
  can still ghost; this is not temporal upscaling.
- **Experimental 2× MetalFX frame generation:** optional interpolation at native
  1080p, with HUD preservation, loading/camera-cut fallback and separate rendered,
  displayed and generated-frame counters. Requires macOS 26+ and a supported
  MetalFX device, V-sync and the display-link presentation path. It defaults off.
  Generated frames do not advance game simulation or improve its input update rate;
  fast motion can show ghosting, soft foliage and disocclusion artifacts.
- **Shader precompilation:** recorded MSL/pipeline recipes can be compiled before
  launch. New variants may still compile on first use. The live renderer uses
  Metal's driver-managed cache rather than serializing binary archives.
- **Controller, keyboard and mouse:** SDL input, configurable `input.ini` bindings,
  mouse capture and native character-name entry. Mac direct-camera mode currently
  falls back to mouse-to-stick input.
- **Achievements:** all 40 trophies, including The Old Hunters, in a native Mac
  window with progress, filters, unlock dates and queued in-game banners. Existing
  `trophies.log` history is preserved. Runtime/UI fixtures pass; actual gameplay
  trophy unlocks still need live verification.
- **External mods and patches:** replacement files, parameter edits, XML/TOML
  patches and opt-in randomizer, boss rush, mutator and developer-menu plugins.
  The original dump stays read-only; generated images and overlays live separately.
- **Nightwatch Co-op (experimental):** Host/Join controls in Settings, 2–4 hunters,
  Internet/LAN party codes, leave/rejoin, and optional Rot penalties. Co-op saves
  and launch snapshots are separate from single-player data. See setup below.
- **DLC discovery and separate player data:** matching DLC folders can be found
  automatically, with explicit overrides for DLC, saves, settings and build paths.

Frame caps are timing modes, not promised frame rates. Resolution, timing,
plugin and several effect changes require a restart. Plugin gameplay changes can
persist in saves even though the source dump is unchanged.

## Requirements

- An Apple Silicon Mac. Current gameplay reports use an **M4 Max**; other machines
  and areas need their own compatibility and performance checks.
- **macOS 15.4 or newer** for the game runtime. Optional MetalFX frame generation
  requires **macOS 26+** and compatible hardware; recorded tests used macOS 27.
- Xcode command-line tools with a recent macOS SDK, **CMake 3.24+**, and **Python 3**
  (3.11+ for the TOML patch analyzer).
- Rosetta for the default x86-64 runtime. The optional arm64 game executable does
  not need it; its synthetic differential CPU test uses Rosetta for reference results.
- Your decrypted game dump, containing `eboot.bin`, game data and PS4 modules.
  Preparation checks the executable fingerprint: an SFO version string alone does
  not establish compatibility. The tested CUSA00900 dump uses the supported 1.09
  layout, even where its SFO reports 01.00.

Build scripts fetch pinned source dependencies, with archive hashes where specified.
They do not install Rosetta, system packages or game data.

## Build and run

Clone the repository and build from its root:

```sh
git clone https://github.com/evill33t/bloodborne_mac.git
cd bloodborne_mac
bash build.sh --test
```

This builds native Metal development tools in `out/macos/` and the
Rosetta game runtime in `out/macos-runtime/`. Launch with your own paths:

```sh
BB_GAME_DIR=/path/to/GAMES/CUSA00900 \
BB_DLC_DIR=/path/to/DLC/CUSA00900 \
    bash run.sh
```

`BB_GAME_DIR` must contain `eboot.bin`. Omit `BB_DLC_DIR` to discover a matching
`DLC/<title ID>` beside a `GAMES/` directory; set `BB_DLC_DIR=` to disable discovery.
The launcher checks the executable, prepares a separate runtime image and starts
the game. Its default has no diagnostic timeout.

For the optional native arm64 CPU path:

```sh
bash packaging/macos/build-native-runtime.sh --test
BB_MAC_CPU=bbcpu BB_DATA_DIR=out/macos-native-data \
    BB_GAME_DIR=/path/to/GAMES/CUSA00900 bash run.sh
```

This example gives the native run separate settings and saves. Use a copied save
for comparisons. `BB_MAC_CPU=rosetta` selects the default runtime again.
`BB_MAC_CPU=bbcpu bash build.sh --test` builds the tools and native runtime together.

To precompile previously recorded shader recipes without launching the game:

```sh
bash packaging/macos/precompile-shaders.sh --data-dir out/macos-data --threads 4
```

For native caches, prefix that command with `BB_MAC_CPU=bbcpu` and use
`--data-dir out/macos-native-data`. See the
[macOS development guide](packaging/macos/README.md) for runtime-only builds,
cache behavior, diagnostics and detailed controls.

## Controls and player data

| Action | Default control |
| --- | --- |
| Open settings | F1, Insert, Command-comma or L3 + R3 |
| Open achievements | F2, Command-Shift-T or the Mac menu |
| Toggle mouse capture / reload input bindings | F7 / F8 |
| Move / camera | WASD / arrow keys |
| Cross / Circle / Square / Triangle | Space / Shift / E / V |
| D-pad / Options | I/J/K/L / Return |

Settings support keyboard, mouse and controller navigation. Opening settings or
text entry releases mouse capture; the game continues running behind settings.
Return or Cross/A confirms character-name entry; Escape or Circle/B cancels.

### Nightwatch Co-op (experimental)

This is a modified macOS integration of
[Nightwatch Co-op](https://github.com/vladtepes22/NightwatchCoop), pinned at
[`db7c9fb2`](https://github.com/vladtepes22/NightwatchCoop/commit/db7c9fb2f07a007a9bb53d3056a425c887c9d4b3).
It retains the direct Metal renderer and existing Mac controls. The Windows
launcher is not used. **Live multiplayer gameplay has not been verified on Mac.**

1. Build the default Rosetta runtime and launch with `bash run.sh`. Native
   `BB_MAC_CPU=bbcpu` currently does not support Nightwatch's x86 hook callbacks.
2. Open Settings with F1 or Command-comma. Under **Nightwatch Co-op**, choose
   **Host a party** or **Join a party**, set a unique hunter name and the same
   party size (2–4). A joiner pastes the host's full `BBP3-…` code.
3. Disable gameplay plugins, then choose **Apply & restart**. Continue a co-op
   character or create a new one; new characters finish their prologue first.
4. The host copies the **Internet code** or **LAN code** from Settings and shares
   it with invited players. **Leave party** and **Rejoin party** operate on the
   current session. Select **Off** and restart to return to single-player saves
   and the existing private-server option.

Use matching Mac builds, game executables, frame caps, patches and loose-file
mods on every machine. Build, hook and content compatibility checks remain
enabled; compatibility with upstream Windows Nightwatch releases is not claimed.
Nightwatch replaces The Hunter's Dream networking for the run and uses direct
player connections. Internet connections need a reachable host port (default
**9307 TCP/UDP**); Mac UPnP port mapping is not implemented. DHT rendezvous stays
off. A LAN code is for players on the same network or an appropriate VPN.

Co-op uses a `nightwatch/` subfolder of your original `BB_USER_DIR`,
including its own `savedata/`, trophies and party state. Existing solo characters
are not copied automatically. Before each co-op launch, existing co-op saves are
snapshotted under `nightwatch/save_backups/`; snapshots are retained. Shared
progression and NPC choices can persist in co-op saves. Keep all players on the
same frame cap, and treat higher rates as experimental.

Before switching to `BB_MAC_CPU=bbcpu`, turn Nightwatch Off and restart. You can
also disable it before launch by setting `nightwatch_mode=0` in `macos-settings.ini`
(`1` hosts, `2` joins).

Credits: **Nightwatch Co-op by vladtepes22**, **visy's seamless co-op**, and
**Lantern Engine by Awaken (https://github.com/Awakenchan/LanternEngine)**.
This is an unofficial, modified integration, not an official Lantern Engine
release. [Source provenance, notices and port boundaries](gpu/metal/vendor/nightwatch/UPSTREAM.md).

The default writable root is `out/macos-data/`:

| Path or override | Purpose |
| --- | --- |
| `prepared/` | Generated executable, patches and content metadata. |
| `user/` | Game saves and `trophies.log`. |
| `user/nightwatch/` | Isolated Nightwatch saves, trophies, party state and launch snapshots. |
| `macos-settings.ini` | Saved Mac display, graphics and runtime settings. |
| `mods/`, `mods.json`, `bbhost.toml` | External mods, plugin and engine configuration. |
| `BB_DATA_DIR` | Change the complete writable root. |
| `BB_USER_DIR` | Override only the save/trophy directory. |

Source game/DLC files are read-only. Game saves and modded save state are written
to the selected user directory. Keep a copy of existing saves before experiments.

## Known limits and development notes

Foliage animation, fog/shadow appearance during camera motion, unsupported
shader/command paths and long-session stability still need work. The new guarded
shadow-clear corrections default on; experimental texture-journal indexing and
inline-binding suppression stay off because matched gameplay did not establish a
repeatable FPS gain. Cinematic video decoding is not implemented, and the game
skips those videos. Real Metal occlusion queries are not implemented.

Frame generation can raise displayed FPS while lowering rendered FPS. It does
not establish a native 60 FPS simulation, remove all transition stalls or provide
a measured latency improvement. Whole-game AOT loading and the Rosetta checkpoint
continuation provider are not available in the native arm64 path.

Builds and synthetic tests establish their exercised contracts. They do not prove
complete game compatibility, all trophy unlocks, multiplayer or artifact-free
rendering. Current evidence and implementation details are recorded in:

- [macOS build, settings and validation guide](packaging/macos/README.md)
- [Native CPU integration and provenance](docs/MACOS_NATIVE_UPSTREAM_2026-10-22.md)
- [Resource/shadow corrections and latest stability work](docs/MACOS_METAL_RESOURCES_2026-10-24.md)
- [Module unwind metadata](docs/MACOS_MODULE_UNWIND_2026-10-24.md) and [guest msync](docs/MACOS_MSYNC_2026-10-24.md)
- [MetalFX frame-generation gameplay and quality results](docs/MACOS_METALFX_FRAME_GENERATION_2026-10-23.md)
- [Shader cache and precompilation](docs/MACOS_SHADER_CACHE_2026-10-23.md)
- [Experimental Rosetta checkpoints](docs/MACOS_CHECKPOINTS.md)
- [bbhost timing, settings, plugins, mods and online integration](docs/BBHOST_MAC_INTEGRATION.md)
- [External mod formats](docs/MODS.md) and [input configuration](docs/INPUT.md)

The build and launch entry points support macOS only. Legacy Windows/MSYS2,
Linux/Nix/AppImage and desktop-launcher paths, the Vulkan renderer and its
FSR/DLSS integrations, unused submodules, and host-specific compatibility layers
have been removed. Both Rosetta and native arm64 `bbcpu` remain supported Mac
runtime modes; the x86-64 guest ABI and PS4 ELF/module tooling are still required.

The small retained `gpu/shadps4/` subset supplies shared type definitions, GNM
initialization packets and development host shaders. The native shader tools use
glslang and SPIRV-Cross to translate SPIR-V intermediates to MSL. Their pinned
SDK tag and compiler target names contain “Vulkan,” but they do not link a Vulkan
loader or provide a Vulkan rendering backend. Source dependencies are fetched by
CMake; no Git submodule initialization is needed.

After building, run the Python tooling checks with:

```sh
ROSETTA_ADVERTISE_AVX=1 python3 -m unittest discover -s tests
python3 scripts/macos_trophy_catalog.py --check
```

## License

The root project is **GPL-2.0-or-later**; see [LICENSE](LICENSE). As recorded in the
[bbhost notices](gpu/metal/vendor/bbhost/UPSTREAM.md), the combined macOS runtime
incorporating bbhost is **GPL-3.0-or-later**. Selected Nightwatch/Lantern code also
retains the [Lantern attribution and modified-version terms](gpu/metal/vendor/nightwatch/NOTICE).
Third-party components retain their
own licenses and copyright notices; include those when distributing builds.
Game executables, assets, trophy artwork and player data are not distributed here.

## Credits

This port builds on the work below. The tables identify imported code,
adaptations, dependencies and research references. Historical platform names
describe the project's origins; the supported host and renderer are macOS and Metal.

### Port foundations and adapted code

| Repository | What this project uses |
| --- | --- |
| [Supermedo/bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) — Mohammed Albarghouthi / Supermedo and contributors | The Windows branch this fork builds on: loader/runtime, preparation and patch/mod tooling, and the foundation for settings and upstream integrations. |
| [deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc) — bbport | The original native Linux port and HLE/loader foundation. Selected dependency-free PM4 decoder code and tests are also imported at `3c57da4`; input, camera layout, temporal rendering, filtering and cache policies informed Mac adaptations. [Provenance](gpu/metal/vendor/bbport/UPSTREAM.md). |
| [shadps4-emu/shadPS4](https://github.com/shadps4-emu/shadPS4) | PS4 GCN/PM4, GNM initialization, shared types and development host shaders, plus HLE, TLS, module-API and network references used by the selected Metal sources and THD adapter. Original notices remain with the retained code. [GPU source record](gpu/VENDOR.txt), [network source record](gpu/bbnet/SHADNET-PORT.md). |
| [BlackLineInteractive/Driveclub-Port](https://github.com/BlackLineInteractive/Driveclub-Port) | The selected direct Metal backend, GCN-to-MSL compiler, command processor, GNM, VideoOut and event-queue code at `408fd84`, adapted extensively for Bloodborne's resources, shaders, synchronization and presentation. [Provenance and local changes](gpu/metal/vendor/bloodborne/UPSTREAM.md). |
| [droogie/bbhost](https://github.com/droogie/bbhost) | Selected engine timing, game-menu builders, parameter/event/player helpers, mod/patch support, plugin SDK and randomizer/boss-rush/mutator/debug-menu plugins. Its THD network/account/session code supplies the private-server client foundation. Pinned at `566184a`; adapted to Darwin and the existing Metal renderer. [Provenance](gpu/metal/vendor/bbhost/UPSTREAM.md). |
| [vladtepes22/NightwatchCoop](https://github.com/vladtepes22/NightwatchCoop), visy, and Lantern Engine by Awaken (https://github.com/Awakenchan/LanternEngine) | Selected party engine, peer networking, shared campaign rules, byte-checked hooks and co-op patch definitions at `db7c9fb2`. Modified for Darwin, our existing timing callback, native Mac settings and separate saves; no Windows launcher or Vulkan renderer. [Provenance and notices](gpu/metal/vendor/nightwatch/UPSTREAM.md). |
| [bloodborne-native/bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp) | The `bbcpu` x86-64 decoder, interpreter, arm64 JIT, CPU fixtures and generated-library interface at `b9652d7`, with local ABI, TLS, timing and CPU-context changes. The AOT generator and generated game functions are not imported. [Provenance](third_party/bbcpu/UPSTREAM.md). |
| [bloodborne-native/bloodborne-runtime](https://github.com/bloodborne-native/bloodborne-runtime) | Command-writer RVAs, per-buffer recursive-lock/entry guards and resolution-dependent direct-memory sizing, adapted from `6228f43`. Its renderer plan also informed later analysis. [Integration report](docs/MACOS_NATIVE_UPSTREAM_2026-10-22.md). |
| [pyL1nx/bloodborne_pc — Achievement-System-Patch](https://github.com/pyL1nx/bloodborne_pc/tree/Achievement-System-Patch) | The achievement-system foundation: trophy catalog and `trophies.log` persistence format, preserved from [snapshot `64919f7`](https://github.com/pyL1nx/bloodborne_pc/tree/64919f7da13a24bac53578acec5f74f959fe5c4e). Adapted into the shared C runtime, native Mac achievement window and in-game banners. [Achievement implementation notes](packaging/macos/README.md#achievements). |
| [Awakenchan/BloodBorneButPc, now LanternEngine](https://github.com/Awakenchan/LanternEngine) — Awaken | The upstream integration from which the selected bbhost THD client in `gpu/bbnet/thd/` was adapted. The unused shadNet backend has been removed. [Attribution](gpu/bbnet/SHADNET-PORT.md). |
| [Wozzardman/shadp2p](https://github.com/Wozzardman/shadp2p) | Historical foundation of bbport's first online implementation and shadNet server reference. The Mac client currently uses the selected THD integration. |

### Libraries and tools used by the macOS path

| Repository | What this project uses |
| --- | --- |
| [libsdl-org/SDL](https://github.com/libsdl-org/SDL) | SDL3 input, controller and platform services; pinned source builds for both CPU modes. |
| [Thealexbarney/LibAtrac9](https://github.com/Thealexbarney/LibAtrac9) | ATRAC9 game-audio decoding. |
| [Cyan4973/xxHash](https://github.com/Cyan4973/xxHash) | Content hashing and texture-change detection, including the copy carried by the Metal renderer. |
| [zyantific/zydis](https://github.com/zyantific/zydis) and [zyantific/zycore-c](https://github.com/zyantific/zycore-c) | x86 instruction decoding and its support library for the native CPU translator; exact revisions are pinned. |
| [KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) | Compiling GLSL host/development shaders to SPIR-V intermediates. |
| [KhronosGroup/SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) | Translating those SPIR-V intermediates to MSL in the native Metal tools. The production retail GCN shader path uses the adapted direct MSL compiler above. |
| [LoupVaillant/Monocypher](https://github.com/LoupVaillant/Monocypher) | Cryptography and Ed25519 code vendored through bbhost for signature verification. |
| [nlohmann/json](https://github.com/nlohmann/json) | JSON parsing/serialization, including the pinned single header used by the Mac THD adapter. |
| [curl/curl](https://github.com/curl/curl) | System libcurl for HTTP/account services. |
| [madler/zlib](https://github.com/madler/zlib) | Compression support used by the engine integration and inherited networking. |
| [Kitware/CMake](https://github.com/Kitware/CMake), [llvm/llvm-project](https://github.com/llvm/llvm-project) and [python/cpython](https://github.com/python/cpython) | Build configuration, Apple Clang/LLVM compilation and Python preparation, mod, patch and verification tooling. |

The retained development shader headers include AMD's
[FidelityFX FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR) helpers, with
their embedded MIT notices. Production scaling uses MetalFX. The debug-menu font
is generated from the public-domain X11 k14/7x14 fonts;
[bbhost's provenance](gpu/metal/vendor/bbhost/UPSTREAM.md) records that source.

### Research and verification references

| Repository | What informed this port |
| --- | --- |
| [Yharnam-Hunters/Paleblood](https://github.com/Yharnam-Hunters/Paleblood) | Selected function extents, scalar helpers and an adapted CPU-only frame-limiter reference at `af97b90`; differential timing verification and patch-overlap analysis. Production timing stays in bbhost. [Provenance](gpu/metal/vendor/paleblood/UPSTREAM.md). |
| [bloodborne-native/bloodborne-mac](https://github.com/bloodborne-native/bloodborne-mac) | App/runtime integration research that led to the selected CPU/runtime imports above. Its Tauri launcher and graphics backend are not imported. |
| [Mrsuss60/bloodborne_pc_windows_port](https://github.com/Mrsuss60/bloodborne_pc_windows_port) | Frame-pacing, texture-alias and graphics-settings references reviewed at `0a71dd1`; Mac settings and MetalFX presentation are native adaptations. |
| [iOrange/bcdec](https://github.com/iOrange/bcdec) | Independent CPU decoding of captured BC6H textures to verify the cubemap/mip correction. Used only in diagnostic output, not linked into the port. |
| [GPUOpen-Drivers/pal](https://github.com/GPUOpen-Drivers/pal) | AMD depth/HTILE clear-encoding reference used while validating native shadow-clear handling. |
| [OpenOrbis/OpenOrbis-PS4-Toolchain](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain) | ModuleInfoEx ABI layout and kernel function declarations used for guest module-unwind queries. |
| [freebsd/freebsd-src](https://github.com/freebsd/freebsd-src) | User-context segment selectors and guest memory-synchronization flag, range and error semantics. |
| [apple-oss-distributions/xnu](https://github.com/apple-oss-distributions/xnu) | Darwin `msync` implementation reference for translating guest synchronization requests. |
| [libunwind/libunwind](https://github.com/libunwind/libunwind) | Reference for the unwinder's page-validation use of `msync`; it is not a replacement guest unwinder. |

Thanks to **Kyo, Lance McDonald, auser1337, illusion, emoose** and the Bloodborne
patch/modding community for the game patches, and to **Metr1k and The Hunter's Dream
team** for guidance on the inherited online work. Original third-party source
notices and the detailed `UPSTREAM.md` files remain the attribution record for
the imported revisions and local changes.
