# bbhost engine and plugins for the Mac port

Source: [droogie/bbhost](https://github.com/droogie/bbhost), commit
[`566184a74f071166299bb656c4af479e5bbe2771`](https://github.com/droogie/bbhost/tree/566184a74f071166299bb656c4af479e5bbe2771).
The selected sources, SDK and four plugins retain bbhost's **GPL-3.0-or-later**
license; see [LICENSE](LICENSE). The combined Mac runtime is GPL-3.0-or-later.
The root project's GPL-2.0-or-later grant permits that combination.

`gpu/metal/bbhost/CMakeLists.txt` lists the compiled modules. This is a selected
engine integration, not bbhost's Vulkan renderer, launcher or updater.
Darwin services, SysV thunks, native game settings, light-grid/LOD controls,
Keychain account storage and the existing THD adapter live in `gpu/metal/bbhost/`.
`thunk.cpp` derives its emitter from upstream `src/core/thunk.cpp`;
`game_menu.cpp` derives its row builders from `src/engine/option_menu.cpp`;
`graphics.cpp` selects the light-grid and model-detail changes from
`src/engine/graphics_patch.cpp`. Their attribution is retained in those files.

[macos-changes.diff](macos-changes.diff) records modifications to the selected
upstream files: Darwin executable paths and dylibs, a verified retail ELF hash,
Mac timing integration/assembly names, native settings captions and rate-independent
60 Hz timers for boss rush/mutators and the randomizer's banner. Config access
is synchronized between Cocoa and game threads, and plugin options persist
before their queued callbacks run. Timing installs before plugin text translation
because both touch camera instructions.

Additional Mac changes use Paleblood's independent limiter reference to verify
bbhost timing in a CPU-only target. `frame_rate.cpp` exposes deterministic replay
and step fixtures, records installed timing ranges, preserves fields for an invalid
stock flip mode, and clears the one-shot notification flag with a missing target.
The latter two corrections were checked against the original 1.09 instructions.
The Mac path in `patch_manifest.cpp` refuses a whole manifest before writing when
it overlaps an installed timing range. Paleblood provenance and its separate
GPL-2.0-or-later reference sources are in `../paleblood/UPSTREAM.md`.

For the optional arm64/bbcpu runtime, the frame-rate compare continuation is
emitted as x86 guest bytes in registered hook storage. Darwin thunks bridge
integer, vector and stack arguments through bbcpu. Native command-writer guards
adapt bloodborne-native's separate runtime; provenance and local CPU changes are
recorded in [bbcpu provenance](../../../../third_party/bbcpu/UPSTREAM.md).

Additional dependencies:

- Monocypher and its Ed25519 module: copied from this bbhost commit, with
  [dual CC0/BSD license](third_party/monocypher/LICENCE.md).
- JSON for Modern C++, 3.12.0: official
  [single header](https://github.com/nlohmann/json/blob/v3.12.0/single_include/nlohmann/json.hpp),
  SHA-256 `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`,
  [MIT license](third_party/nlohmann/LICENSE.MIT), copyright 2013–2025 Niels Lohmann.
- `plugins/debug_menu/font14.bin`: upstream's glyph data made from the
  public-domain X11 k14 and 7x14 fonts. `tools/make_debug_font.py` records
  the source and reproducible format. No original game font is included.

Game movies and message banks are generated locally from the user's dump into
the writable data directory. No original executable or game assets are vendored.

## macOS-only source cleanup (24 October 2026)

Inactive Windows/Linux host branches were removed from the adapted sources.
Darwin behavior, CPU architecture switches, guest ABI interfaces and original
license notices are retained.
