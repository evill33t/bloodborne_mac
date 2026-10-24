# bbcpu source provenance

Selected CPU sources from
[bloodborne-native/bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp/tree/b9652d75edc0a281f06cec0da6ff72e9f5150a70),
commit `b9652d75edc0a281f06cec0da6ff72e9f5150a70` (2026-10-22 snapshot).
License: GPL-2.0-or-later; see [LICENSE](LICENSE). The project credits bbport
by deadinside28 and shadPS4. Original notices are retained.

Imported files are `src/cpu/`, the generated-library interface `src/recomp/rc.h`,
and six synthetic CPU test sources. `upstream-sha256.json` records their original
SHA-256 values; `local-changes.diff` records our source adaptations. No game
executable, generated game functions, game captures or player data are included.

Local changes:

- CMake integration with Zydis 4.1.1 pinned to
  `a2278f1d254e492f6a6b39f6cb5d1f5d515659dc`; its submodule pins Zycore to
  `0b2432ced0884fd152b471d97ecf0258ff4d859f`. These dependencies retain their
  own licenses in the fetched sources.
- `bbcpu_call_frame` bridges bbhost's six integer, eight vector and four stack
  arguments, including return values and nested callback state.
- 128 guest code ranges accommodate the loader's 64-segment limit, traps and
  generated x86 hooks. Hooks occupy one nonexecutable host arena.
- Our runtime and Metal code use the same virtual 1.6 GHz guest TSC.
- Decoded segment operands retain their own register kind. Interpreter/JIT
  fallback context saves read per-thread 16-bit selectors instead of terminating
  on MOV from CS. Flat user-context defaults follow the FreeBSD AMD64 selector
  layout (CS0x43, SS/DS/ES0x3b, FS0x13, GS0x1b). Selector restores retain metadata;
  they do not install GDT descriptors or replace the runtime's explicit FS/GS
  bases. This is a user-mode context model, not privileged segmentation.
  The exact observed six-byte store, register/memory widths, flags, thread
  isolation and TLS are covered in both execution modes.

`command_buffer_offsets.inc` contains 336 command-writer RVAs selected from
`gpu/shim/bbport_gnm_table.inc` in
[bloodborne-native/bloodborne-runtime](https://github.com/bloodborne-native/bloodborne-runtime/tree/6228f43b2919d1abd1c4185e689048797855e540),
commit `6228f43b2919d1abd1c4185e689048797855e540`. The per-buffer recursive-lock
algorithm in `gpu/metal/bbhost/command_guards.cpp` adapts that repository's
`src/runtime_cmdbuf.c`. Installation requires our supported ELF hash and the
expected function prefix; a mismatch is refused. `BB_CB_GUARD=0` disables it.
This metadata contains addresses, with no generated game function bodies.

The same runtime's `run.sh` supplied the scene-memory sizing formula in
`scripts/macos_prepare.py`: 448 bytes per additional scene pixel above 1080p,
rounded to 64 MiB over the 5056 MiB base pool.

The parent app inspected was
[bloodborne-native/bloodborne-mac](https://github.com/bloodborne-native/bloodborne-mac/tree/9d34ad2e11571ab458781198cd12ef2e2f313a5c),
commit `9d34ad2e11571ab458781198cd12ef2e2f313a5c`. Runtime and recompiler HEADs
were also checked directly and matched the above pins.

The ahead-of-time generator is not imported. Upstream currently measures
20–30% higher CPU use for its recompiled library than for bbcpu. Our executable
uses the translator; loading external generated libraries is not enabled.
