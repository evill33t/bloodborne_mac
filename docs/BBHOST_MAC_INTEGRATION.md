# bbhost features in the direct Metal Mac runtime

This integrates selected engine features and all four bundled native plugins from
[bbhost commit 566184a](https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771).
The existing direct Metal renderer and macOS settings panel remain in place.
Source provenance and licenses are in
[UPSTREAM.md](../gpu/metal/vendor/bbhost/UPSTREAM.md).

## Settings in the game's System menu

The System menu gains four sections drawn by the game's own menu system:

| Section | Settings |
| --- | --- |
| Mac Display | V-sync, fullscreen, FPS counter, MetalFX, restart, frame cap |
| Mac Graphics | AA, motion blur, depth of field, chromatic aberration, SSAO, dynamic shadows |
| Mac Mods | Randomizer, boss rush, mutators, developer debug menu, sharpening, render resolution |
| Mac Performance | Light-grid batching, model detail |

Changes save to `$BB_DATA_DIR/macos-settings.ini` and use the same state as the
Mac panel. Display presentation, light-grid batching and model detail apply live.
Frame cap, render resolution, effects, online configuration and plugin enablement
require a restart. Mac Display's restart switch asks before reloading the last save.

The macOS **Settings…** menu item, Command-comma, F1, Insert and L3+R3 remain
available. That panel also contains server/account controls and metadata-driven
plugin options/actions, including boss rush Start/Stop. Plugin options save when
edited; plugin enablement takes effect next start. The Mac panel's Restore defaults
and Apply buttons control the regular settings draft.

The port generates the Scaleform movie and language message banks under
`$BB_DATA_DIR/bbhost/menu-assets/`; originals stay untouched. User replacements
take priority over generated assets, so a mod replacing the same menu movie or
message bank must include the Mac sections to display them correctly. Generated
labels currently use English in every supported language bundle.

## Frame rates

Choose **30, 60, 90, 120, 144, 165, 240 FPS, or Uncapped**. Higher caps use bbhost's
variable-step engine mode with the requested limit; the launcher omits the older
XML Uncap patch so the two implementations do not overwrite one another.
The engine integration includes measured time steps, camera/movement smoothing,
cloth corrections above 60, play-time millisecond carry and an adaptive host limiter.
Boss rush and mutator timers use a separate 60 Hz clock at every render rate.

The supported decrypted ELF hashes are:

```
941f887a562aae054fac35af8cc8f27cf075f3d4cc2e029fb5ae2a663aaa5ae7
cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86
```

The launcher verifies the ELF hash and the 1.09 layout. Address-based engine hooks
also check the expected original instructions. SFO's version string alone is
insufficient. A refused timing patch logs the fallback mode.

These settings are caps, not measured performance promises. Fullscreen/V-sync and
the display refresh rate can impose another limit. High-rate gameplay remains
experimental: upstream reports rate-dependent slow poison and intermittent summon
failures. The fixture checks below do not establish whole-game timing correctness.

## Mods without editing the dump

The default writable root is `out/macos-data` (`BB_DATA_DIR` overrides it).
Replacement packs use the existing mod selection and merge machinery:

```
<data>/mods/MyReplacement/dvdroot_ps4/chr/...
<data>/mods/params/EquipParamWeapon.toml
<data>/mods/patches/my-patch.toml
<data>/plugins/my_plugin.dylib
<data>/bbhost.toml
```

`BB_MODS_DIR` overrides the mod root; `BB_MODS_CONFIG` overrides the selection file.
Merged views live in `<data>/mod-views/`, are regenerated at restart and removed
after exit. They link/read original files and place replacements in the view.
`BB_MAC_MOD_OVERLAY` optionally selects a separate loose overlay/param/patch root.

Named param edits use the definitions in the game's own paramdef archive:

```toml
# mods/params/EquipParamWeapon.toml
row.1000000.correctStrength = 250
row.1000000.weight = 0.5
```

Use `row.all.<field>` for every row or `<field>[index]` for an array element.
`BBHOST_PARAM_DUMP=EquipParamWeapon:1000000` logs the actual fields and values.
Definitions whose row size disagrees with the loaded table are refused.
Param edits apply in memory when the table loads.

Byte manifests are TOML data. Set `eboot` to the **full decrypted ELF hash** shown
by preflight, use preferred guest addresses based at `0x400000`, and specify
`expect` and replacement `bytes` of equal length for each `[site.name]`.
Every site's expected bytes must match before any site is written.
`[patches] name = false` in `bbhost.toml` disables a named manifest.
Existing XML patches and their selection file remain supported during preparation.

The build supplies x86-64 Mach-O plugins in `out/macos-runtime/plugins/`:

- **Randomizer:** seeded items, shops and enemies, with its choices exposed in the Mac panel.
- **Boss rush:** order, practice rounds, DLC/chalice choices and Start/Stop actions.
- **Mutators:** speed, gravity, bullet time, one hit, blood drain, enemy HP, echoes and chaos.
- **Debug menu:** the developers' menu, generated debug font and optional English text;
  backtick opens it after enabling and restarting.

Enable them in either settings surface, or in `<data>/bbhost.toml`:

```toml
[plugins]
randomizer = true
boss_rush = false
mutators = false
debug_menu = false

[randomizer]
seed = "my-run"
```

Plugins are opt-in. Their SDK is in `gpu/metal/vendor/bbhost/include/`; third-party
plugins must be built for **Darwin x86-64**, not Linux `.so` or Windows DLLs.
Native plugins execute within the runtime process. Gameplay changes can persist
in game saves even though the original dump stays unchanged. `BBHOST_PLUGINS=0`
disables all native plugins. Session requests carry the active plugin ruleset;
automatic adoption of a differently modded host's world is not integrated.

## The Hunter's Dream private-server client

The Mac panel contains **Play online**, a game/account server origin, a matchmaking
server origin, Sign in and Sign out. This connects the existing THD protocol adapter
to Darwin sockets, system curl and the game HTTP API. It includes asynchronous game
HTTP completion, account state, matchmaking/P2P services and plugin ruleset headers.
It is a client integration; it does not host or deploy a private server.

Save a server change and restart before linking an account. Sign in starts the
server's passwordless Discord device flow and opens its verification page after
checking the page belongs to the selected origin. Tokens are stored in macOS
Keychain, separate from settings and game saves. Sign out removes the local token.
Explicit `BB_ONLINE`, `BB_THD_SERVER`, `BB_THD_GAME_HOST`, `BB_THD_SCHEME` and
`BB_THD_AUTH` environment overrides remain available. For automation, the existing
`BB_THD_ONLINE_ID` and `BB_THD_TOKEN` inputs also work.

Online is off by default. Live authentication, messages, summons, cross-client
compatibility and router traversal still need testing. The Windows shadNet
password/login interface is not part of this Mac backend.

## Graphics and occlusion culling

The optional light-grid setting selects bbhost's larger light tiles and smaller
CPU job split. It defaults off until measured with this Metal backend. Model detail
changes the engine's LOD bias live. Existing MetalFX presentation, graphics effect
switches, tessellation, shadow comparisons and backface culling remain available.

There are three distinct paths in the source:

- The direct Metal renderer implements triangle front/backface culling.
- Its HTILE depth metadata fallback currently reports the full depth range for
  light tiles, keeping them visible. This is conservative light culling.
- The retained shadPS4 Vulkan tree's occlusion results are placeholder counters.
  bbhost has a separate real Vulkan query/predication implementation in
  `src/host/occlusion.cpp`, but it cannot be used directly by Metal. Real Metal
  visibility queries and their guest result/predication path are still needed.

No occlusion performance gain is claimed by this integration.

## Patch analysis and CPU timing verification

The Mac development tools use selected research from
[Paleblood](https://github.com/Yharnam-Hunters/Paleblood/tree/af97b90bb1e88853adfc04b8da2b3d7506516565).
Provenance and GPL notices are in
[`gpu/metal/vendor/paleblood/UPSTREAM.md`](../gpu/metal/vendor/paleblood/UPSTREAM.md).
The renderer and production engine integration remain direct Metal and bbhost.

Map one patch, with this port's potential timing ownership at the selected cap:

```sh
python3 scripts/macos_patch_analysis.py --name 'Uncap FPS++' --fps 120 --json
python3 scripts/macos_patch_analysis.py --name 'Skip Intro' \
  --elf out/macos-data/prepared/eboot.elf
python3 scripts/macos_patch_analysis.py --manifest /path/to/my-patch.toml --fps 60
```

The analyzer is read-only. Exit status is 0 for no conflicts, 1 for conflicts,
and 2 for invalid input or an unsupported supplied ELF. With no `--name`, it maps
the built-in XML's patches independently; repeated `--name` options analyze a
selection together, including disagreements between patches. Identical overlapping
bytes are allowed. TOML inspection requires Python 3.11+. `--elf` checks the
unrelocated loaded-image hash before applying target-specific labels. This avoids
rejecting an equivalent executable solely because its SELF conversion differs.

The 32-function Paleblood catalog is a **partial** address map. Its upstream
verification labels do not describe this Mac port. Only bbhost's replacement
limiter and its timing tables determine engine ownership; other Paleblood hooks
are not treated as installed here. Static ownership is conservative, including
optional edits at the selected frame cap; unmapped addresses are not proven safe.
Arbitrary plugins, extra fixed-tick hooks and other engine edits are outside this
timing inventory. The analyzer also flags the known Uncap call into an initializer
and the early return inside the timed condition-wait path, based on written bytes.

Mac preparation checks selected external XMLs before writing `patches.bin` and
records `macos-patch-analysis.json`. Conflicts stop preparation with the patch
name, address and reason. Runtime TOML manifests separately refuse overlaps with
**successfully installed** bbhost timing edits, before any site in that manifest
is written. The existing executable-hash and expected-byte checks still apply.

The normal runtime test build includes deterministic CPU fixtures and an independent
Paleblood stock limiter reference:

```sh
bash packaging/macos/build-runtime.sh --test
python3 tools/macos_verify_timing.py --report out/macos-runtime/timing-reference.json
python3 tools/macos_verify_timing.py --elf out/macos-data/prepared/eboot.elf \
  --report out/macos-runtime/timing-verification.json
```

The optional ELF run checks its loaded-image hash, maps a private copy with local
relocations and scripted imports, and executes only the original frame limiter.
It never enters game startup, reads saves, starts networking, or initializes a GPU
or window. The original and Paleblood reference must agree on all 0x2c8 state bytes
and scripted clock/sleep counts. bbhost replays the reference's release time and
must agree on those state bytes; its optimized sleep/spin policy intentionally
does not need the same clock-call count. Unscripted calls, exhausted clocks,
process failures and timeouts fail verification. Deliberately corrupting the
result must fail every fixture; this negative control is required by the wrapper.

Twenty synthetic limiter cases cover the five modes, pending changes, resets,
overrides, catch-up, ring wrap, notification clearing, invalid modes and a backwards
clock. Another 82 checks exercise the production task-step clamp, play-time
millisecond carry and camera smoothing at 30/60/90/120/144/165/240 FPS, plus loading
frames with no target. These fixtures identified and corrected two stock-mode
differences: an invalid mode keeps the existing fields, and the 60 Hz notification
flag clears even when its destination is missing. Both match the original 1.09
instructions in the supplied ELF.

This is bounded CPU evidence, not recorded gameplay, a full AI/physics verification,
GPU validation, measured frame pacing, or proof of correctness throughout the game.

## Validation

The x86-64 Mac runtime and all four `.dylib` plugins build. Focused checks cover
settings persistence, read-only replacement precedence, path traversal rejection,
named param bitfields, all-site manifest refusal, timing math and plugin clocks.
The HTTP fixture uses only a temporary loopback server and checks real curl POSTs,
HLE symbol resolution, nonblocking completion and response reads.

The supplied CUSA00900 dump was inspected read-only. Preflight recognized the 1.09
layout, decrypted ELF hash `cec1b276…`, and three supplied DLC entitlements. Asset
generation produced the native menu movie and **22/22** language bundles. A CPU-only
fixture mapped the ELF, installed timing for **30/60/90/120/144/165/240/uncapped**, and
executed only the patched frame manager on synthetic state. All four plugin image
callbacks also passed together at 240, including debug-font/text generation.

```sh
cmake --build out/macos-runtime --parallel
ROSETTA_ADVERTISE_AVX=1 ctest --test-dir out/macos-runtime \
  -R 'darwin-(settings$|bbhost-services|bbhost-http)' --output-on-failure
python3 -m unittest discover -s tests -p 'test_macos_settings.py'
# Optional, read-only game fixture; does not start the game or open a window:
ROSETTA_ADVERTISE_AVX=1 out/macos-runtime/bb-macos-features-tests \
  /path/to/prepared/eboot.elf 240 /path/to/CUSA00900 --plugins
```

The native menu's visual layout, actual high-rate gameplay, plugin runs, live
multiplayer, DLC-area access and Metal performance changes still require gameplay
validation. No original game, DLC or save files were changed by these checks.
