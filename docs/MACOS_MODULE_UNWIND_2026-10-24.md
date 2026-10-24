# Native module unwind metadata, 2026-10-24

The later 600-second gameplay soak stopped near 590 seconds on the previously
unimplemented `sceKernelGetModuleInfoFromAddr` import (`f7KBOafysXo#p#J`). It
did not finish its planned stability interval. The queried guest address
`0x70057336fa` belongs to linked `libc.prx`, at offset `0x536fa`. Parent analysis
identifies the enclosing export at `0x536b0` as `_Unwind_RaiseException` by its
exact NID. The address follows the native `getcontext` call that previously
exposed the unsupported segment-register save. This is evidence of C++ exception
unwinding, not proof that the underlying throw is fatal or caused by Metal.

The runtime now implements the actual module-information query. No exception
handler, terminate path or guest error is suppressed. A gameplay replay must
still determine whether the game catches the original exception successfully.

## Prepared metadata and runtime contract

The loaded BBPROBE5 image contains relocated code/data, not ELF headers. Its
small module table lacks EH frame locations. The linker therefore emits
BBPROBE6, retaining the previous module/binding sections and adding a count
followed by 424-byte ModuleInfoEx records before the load-segment table.
Addresses in these serialized records are image RVAs. IDs are deterministic
load-order IDs, starting with 0 for the main executable; host libraries are not
reported as guest modules.

The descriptions come from the already parsed original ELF program headers.
EH header locations come from `PT_GNU_EH_FRAME`; its encoded pointer supplies
the EH frame address. A bounded record-length walk establishes the frame
extent through its zero terminator, or the bounded section end when no
terminator is present. Unknown pointer encodings, unmapped pointers, truncated
records and oversized metadata fail preparation instead of inventing tables.
Known modules without an EH header report its absence explicitly.

The loader validates table counts, ABI sizes, names/IDs, exact per-image load
segments, TLS consistency and contained EH/init ranges. It rebases the records
once and installs a validated immutable registry before guest initialization.
Malformed registration is transactional and leaves an existing registry intact.
`macos_prepare.py` regenerates output and explicitly verifies BBPROBE6; it cannot
silently accept a cached BBPROBE5 as unwind-ready. Existing boot formats 1–5
remain loadable, but the query returns ENOSYS and an explicit diagnostic when
their metadata is unavailable. Older frozen executables cannot load BBPROBE6;
comparisons must freeze matching preparation scripts or keep separate datasets.

The imported API accepts flags 0–2 and output sizes at least 424 bytes, returns
the exact record for an address in one of its loaded segments, and leaves a
larger output tail untouched. Unknown addresses return ESRCH; invalid flags or
short output size return EINVAL; inaccessible/read-only output returns EFAULT.
VM copies honor native permissions, and writes additionally honor original
guest ELF permissions when 4 KiB guest segments share a 16 KiB native page. The
function does not treat allocations, host code or aggregate image padding as
loaded guest modules. These are the implemented, tested semantics; they do not
claim a complete console kernel or every undocumented flag behavior.

The ABI layout and signatures are described by
[OpenOrbis kernel types](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/_types/kernel.h)
and [libkernel declarations](https://github.com/OpenOrbis/OpenOrbis-PS4-Toolchain/blob/master/include/orbis/libkernel.h).
The accepted flag range and module lookup behavior were compared with
[shadPS4 process services](https://github.com/shadps4-emu/shadPS4/blob/main/src/core/libraries/kernel/process.cpp).

## Validation and limits

The native full game and runtime probe build successfully. The isolated C API
fixture passes exact ABI/TLS/EH contents, end-exclusive segment boundaries,
flags and size errors, native VM permissions, guest-only read-only permissions,
invalid pointers, guarded output tails, transactional readiness and 8,000
concurrent queries. Five synthetic Python ELF tests pass pointer encodings,
exact frame extent, packed offsets and malformed header/range rejection.

All 19 native loader tests pass. New synthetic x86 guests call the real scoped
NID through the native bridge and verify the rebased EH pointer and module ID.
They also test legacy readiness, read-only output and malformed metadata counts,
truncation, ownership ranges and TLS. These fixtures use owned temporary images;
they neither launch Bloodborne nor write original game files or master saves.

Evidence is retained under `out/metal-resource-2026-10-24/` in
`module-info-native-build.log`, `module-info-native-loader.log` and
`module-info-cpu.log`. A post-fix original-game unwind replay, long gameplay
stability and the Rosetta-specific build/test remain separate required checks.
