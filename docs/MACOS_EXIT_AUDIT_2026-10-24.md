# Guest exit audit, 2026-10-24

The `resource24-msync-final-soak` replay stopped after approximately 640.9
seconds of measured gameplay. Its launch log first records
`Runtime: guest requested exit(1)`, then a guest finalizer panics with
`Mutex is not initialized`. The fault at guest RVA `0x20b56d2` is inside a
nested `runtime_finalize` guest callback. The host stack shows `guest_libc_exit`
below that call, followed by the original worker's guest execution and
`host_start`.

The requested exit therefore precedes the mutex cleanup panic. The existing
log does not establish why the original code requested status 1. In particular,
it does not prove that the initial throw was fatal, that a command-buffer
guard prevented unwinding, or that the cleanup panic caused the initial exit.
The finalizer registry already selects callbacks in reverse registration
order and marks each inactive before invocation. Selection is protected by a
mutex, but callback execution is outside that lock; concurrent cleanup remains
a separate possibility rather than an established cause.

## Optional diagnostic

`BB_RUNTIME_EXIT_AUDIT=1` records the immediate exit context before invoking any
finalizer: status, current pthread/name, guest RIP, immediate guest import
return and its image RVA, plus a snapshot of active/registered finalizers.
Existing module-info and msync reports are flushed at that point so a fault
inside a destructor cannot discard the first API coverage evidence. Each
selected finalizer then records its index, callback/RVA, argument, DSO and
thread. Output is limited to 512 callback rows plus one truncation message.

This flag changes diagnostic output only. It preserves callback selection,
order, arguments, inactive marking, exit status and error propagation. It does
not suppress the panic or attempt to repair unwinding. Context comes from the
public bbcpu diagnostic accessors; there is no raw stack walk. Non-translated
probes cannot provide a guest RVA and print the unavailable sentinel instead.

Command-buffer guard cookies are not included: the public accessor filters by
an exact owner callback, and the renderer's owner is private. Passing NULL is
not a wildcard and would misleadingly produce zero. A deliberate read-only
reporting bridge is needed before this diagnostic can report that information.

## Optional original throw observation

On the native ARM translator, the same flag also installs an existing bbcpu
observation probe at the loader's exact bound `vkuuLfhnSZI#q#q` import
(`__cxa_throw`). Installation requires the bound function kind, a complete
54-byte extent inside the first linked module (libc) and an executable segment, and the observed
12-byte entry prologue. Ambiguous targets or a mismatch refuse observation
without affecting the guest. This short prologue guard is not a full binary
identity check. The current original libc export is RVA `0x60510`, size 54.
The observer follows the bound target rather than assuming its aggregate
image RVA or a fixed module placement.

The first 64 observed entries record the exception-object, typeinfo and
destructor argument addresses, the thread, function RVA and immediate return
slot with a readable/unavailable indication. The return slot is read through
the checked macOS memory reader; there is no stack walk. RTTI inspection reads
the typeinfo header and at most 96 name bytes only inside declared readable
guest image segments, using checked reads throughout. Names are escaped to
prevent embedded quotes/control bytes creating misleading log lines; absent,
outside-image or unreadable metadata is explicitly unavailable, while a name
longer than the bound is marked truncated. The name remains compiler RTTI
text, not a demangled class or an interpreted exception payload. Dynamically
allocated typeinfo outside the image is intentionally not inspected.

The callback preserves host and guest errno, never dereferences the exception
object, and leaves all guest instructions, arguments and return slots intact.
It neither replaces `__cxa_throw` nor changes exception propagation. Rosetta
executes x86 code directly, so this bbcpu observation hook does not apply there.
The diagnostic's extra timing cost is not suitable for clean FPS comparisons.
An observed throw does not itself establish an unhandled exception or explain
why the game requested exit status 1.

## Validation

The native full game and runtime probe build successfully. All 22 native
loader tests pass. A synthetic guest uses the real scoped `__cxa_atexit` and
`exit` imports, requests status 1, and checks its two destructors execute once
in reverse registration order through shared guest memory; violations trap.
The audit test additionally checks the exact immediate guest return RVA,
pre-finalizer coverage reports and row order. With the flag disabled, status
and callback behavior remain the same and no audit rows appear. All 22 Rosetta
loader tests also pass with `ROSETTA_ADVERTISE_AVX=1`.

The new diagnostic has not yet established the gameplay failure's root cause.
No gameplay or GPU test was launched for this diagnostic-only change, and the
fixtures use synthetic images and owned temporary files. Original game files
and master saves remain untouched.

Evidence is under `out/metal-resource-2026-10-24/`:
`exit-audit-native-build.log`, `exit-audit-native-loader.log`,
`exit-audit-x86-build.log` and `exit-audit-x86-loader.log`.

Native game SHA-256:
`01a8e6a34842c0df26ddc7c0e1530cb3720c3c9bf901f31ea341f8b085eda0af`.

After adding the throw observer, native game/probe and Rosetta probe builds
pass, with all 27 loader tests passing on each architecture. Owned synthetic
functions prove their expected return, object writes, passed arguments and
errno remain unchanged. The native tests additionally check exact caller RVA,
escaped/unavailable/truncated names, rejection of mismatched prologue/libc
extent/scope and of a lookalike binding into a second module, the 64-event
limit and the disabled flag. Rosetta tests confirm
the same function behavior with no translator audit rows. No original
procedure body is embedded in these tracked fixtures.

Evidence: `throw-audit-{native,x86}-{build,loader}.log` under the same output
directory. No gameplay or GPU test was launched to validate this new observer.
The initiating gameplay exception remains unknown until an observed replay.

Native game SHA-256 with throw observation support:
`7a74c58e44e8a57a74760f343563554e19c4931247c988a576cca16389ddce2b`.
