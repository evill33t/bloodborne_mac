# Nightwatch Co-op source integration

Repository: https://github.com/vladtepes22/NightwatchCoop

Pinned revision: `db7c9fb2f07a007a9bb53d3056a425c887c9d4b3` (retrieved 24 October 2026).

Selected from `engine/gpu/shim/party/`, `engine/gpu/shim/net/`, their supporting
headers and `engine/gpu/shim/engine/engine_hooks.*`. `patches/Bloodborne-party.xml`
contains only the eleven 1.09 `Party:` entries from upstream's
`engine/patches/Bloodborne.xml`. Five upstream regression fixtures are retained
under `tests/`. Files preserve their original SPDX notices. Unused launcher,
renderer, menu-page and developer world-warp implementations are excluded.

## Attribution and license

Nightwatch Co-op by vladtepes22; seamless co-op by visy, incorporating work from
visy/bloodborne_pc_windows_port. Nightwatch's README identifies co-op v0.7.4.

**Lantern Engine by Awaken (https://github.com/Awakenchan/LanternEngine)**.
This is a modified, unofficial macOS integration, not an official Lantern Engine
release. Keep this attribution in distributed source, README and settings.
The selected code is GPL-3.0-or-later or GPL-2.0-or-later as marked; retain
[LICENSE](LICENSE) and the additional attribution/modified-version terms in
[NOTICE](NOTICE). Monocypher is reused from the existing attributed bbhost copy.

## Local changes and adapters

- `gpu/metal/nightwatch/` owns Mac settings/environment configuration, code
  validation, lifecycle, game messages and the small platform/settings adapters.
- The default Rosetta runtime builds the party engine. Native arm64/bbcpu builds
  retain the settings schema and expose its current unavailability; raw x86
  callbacks cannot execute as arm64 host functions.
- The existing bbhost frame callback calls `coop::CoopTick()` once. Nightwatch's
  second SprjFlipper hook is disabled with `BB_COOP_EXTERNAL_TICK`; the bbhost
  decompiled flag-writer replacement is skipped in co-op so progression hooks see
  original writes. Gameplay plugins must be disabled for co-op.
- Darwin uses bounded mmap hints without Linux MAP_FIXED_NOREPLACE, checked
  Mach page permissions for code writes, per-socket SO_NOSIGPIPE, pthread QoS and
  thread names. Mach-O assembly references use the required underscore prefix.
- Windows-only optional heap/debug-register experiments are excluded. Windows
  UPnP remains unavailable on Mac. DHT rendezvous is forced off.
- Guest reads/writes use the runtime's fault-recovering reader/writer. The pad
  director feeds the existing input sampler, respecting settings input capture.
- Save isolation and pre-launch ZIP snapshots belong to
  `scripts/macos_nightwatch.py`; the upstream Windows save/launcher flow is not
  used. No solo saves are automatically imported. Source game files stay read-only.
- Existing System menus are retained. A shared message lookup supplies co-op
  travel prompts and upstream NPC/chalice message providers; test automation and
  the Windows menu action/relaunch flow are not selected. Vulkan party name-tag,
  ping/depth-probe rendering and the upstream mirror/world-warp UI are not ported.
- Peer build identity hashes the selected fork source/adapters. All prepared
  patches and loose-file overrides are compared conservatively. Cross-play with
  upstream Windows Nightwatch is not advertised; matching Mac builds are required.

## Validation boundary

Builds, settings/UI fixtures, launch/save isolation tests, loopback encrypted
network tests and pure progression/travel rules are distinct from live game
validation. No claim is made here of two-Mac gameplay, shared campaign completion,
Internet/NAT traversal, Windows cross-play or native arm64 co-op. See the root
README and task validation logs under `out/nightwatch-*` for the executed checks.
