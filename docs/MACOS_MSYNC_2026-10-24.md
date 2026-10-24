# Guest msync and exception unwinding, 2026-10-24

The BBPROBE6 gameplay replay successfully queried real module unwind metadata
once (`queries=1`, `unknown=0`, `metadata-unavailable=0`), then stopped at the
previously unsupported POSIX `msync` import, `tZY4+SZNFhA#p#J`, after four travel
requests. Its first argument was `0x71854aa000`, aligned to 4 KiB but not 16 KiB.
This failed replay is evidence of module API coverage, not completed stability.
Its evidence remains under
`out/metal-unattended-2026-10-20/resource24-v6-final-soak/`.

`libunwind` uses `msync(MS_ASYNC)` as a mapped-page validator, with 4 KiB probe
alignment, when its alternative validation method is unavailable. That is a
plausible explanation for this next dependency after `_Unwind_RaiseException`;
the retained native log does not establish the exact guest caller of this
particular msync invocation. See the primary
[libunwind implementation](https://github.com/libunwind/libunwind/blob/v1.1/src/x86_64/Ginit.c).

## Implemented contract

The imported POSIX API now issues a real host `msync` after checked guest-range
normalization. FreeBSD 9 defines guest synchronous mode as 0, asynchronous as
1 and invalidation as 2. It rounds the address down and the covered length up
to kernel pages. The implementation uses the PS4's 16 KiB pages, allowing
valid 4 KiB unwind probes on both native and Rosetta hosts. It translates the
flags explicitly instead of forwarding Darwin's different synchronous flag.
[FreeBSD flag definitions](https://github.com/freebsd/freebsd-src/blob/releng/9.0/sys/sys/mman.h),
[kernel msync](https://github.com/freebsd/freebsd-src/blob/releng/9.0/sys/vm/vm_mmap.c).

As in that FreeBSD implementation, ASYNC plus INVALIDATE is rejected; bits
outside those two are ignored. They are never forwarded as Darwin KILLPAGES
or DEACTIVATE operations, which could discard data. A zero-length request at
an aligned address flushes its current containing VM region. Logical guest
reservations and unmapped gaps return EINVAL, including holes that still have
an anonymous PROT_NONE reservation on the host. Host unmapped-range ENOMEM is
translated to FreeBSD's EINVAL; real I/O/busy errors remain errors. Success
preserves guest errno. No GPU wait or fence semantics are changed.
[FreeBSD region synchronization](https://github.com/freebsd/freebsd-src/blob/releng/9.0/sys/vm/vm_map.c),
[Darwin implementation](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/kern_mman.c).

This supports existing anonymous and file-backed host mappings. It does not
introduce guest file-backed mmap: that separate API still explicitly returns
ENODEV. POSIX synchronization does not prove that memory is readable; a mapped
PROT_NONE object can be valid for msync and still fault if dereferenced. The
Windows branch returns ENOSYS rather than claiming FlushViewOfFile provides
these semantics. The current target and validation are macOS.

## Validation

Native and Rosetta focused CPU fixtures pass. Their observer records the actual
host syscall ranges and flags before forwarding to real `msync`. Cases cover:

- Four 4 KiB offsets within a 16 KiB guest page, plus a probe crossing two pages.
- Anonymous low guest storage, logical flexible mappings and reserved/unmapped
  holes; unchanged payloads after synchronization and ignored flag bits.
- Zero-length containing-region synchronization, overflow and invalid flags.
- Owned temporary shared file mappings and private copy-on-write mappings;
  shared changes reach file reads while private changes do not overwrite them.
- Injected I/O, busy and unmapped errors, preserving actual guest errno values.

The I/O-error injection checks error propagation; it is not a physical storage
failure or power-loss durability test. All test mappings and files are owned
fixtures, and temporary files are unlinked. No original game file or master
save was written or game process launched by these fixtures.

Both runtime probes build, and all 20 loader tests pass in each architecture.
A synthetic x86 guest calls the actual scoped msync NID with an 8 KiB offset
inside a 16 KiB image, a 4 KiB length and ASYNC mode. It reaches the terminal
diagnostic with return value 0 and exactly one recorded guest msync call.
Rosetta tests require the established `ROSETTA_ADVERTISE_AVX=1` environment.
Native module-info and msync CTests both pass; no GPU fixtures were run for this
change. These checks establish probe linkage and CPU behavior; full-game
gameplay validation is separate.

Evidence under `out/metal-resource-2026-10-24/`:
`msync-native-build.log`, `msync-native-loader.log`, `msync-native-fixtures.log`,
`msync-x86-build.log`, `msync-x86-loader.log` and `msync-x86-fixture.log`.
The original throw's successful completion and a full gameplay stability pass
remain unproven until that replay completes.
