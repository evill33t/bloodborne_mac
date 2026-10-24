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
