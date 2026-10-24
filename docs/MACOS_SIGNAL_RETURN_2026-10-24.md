# Guest signal-return classification, 2026-10-24

The exit-audit replay stopped at 572.23 measured presentation seconds after
four travel requests, before any guest exit or finalizer audit. The next
unsupported import was `_is_signal_return`, NID `crb5j7mkk1c#p#J`, index 728.
The process returned 20. Module-info queries reached 29, with two unknown
addresses and zero unavailable-metadata results; msync was called twice.
Evidence: `out/metal-unattended-2026-10-20/resource24-exit-audit-soak/`.

Its argument `0x700000000010` is exactly the translator's depth-one return
sentinel (`0x0000700000000000 + depth*16` in `cpu.c`). It is an outer
host-to-guest return boundary, not an address in the game image, whose base
is `0x7000000000`. It is also distinct from the command-guard return marker
`0xbb6a7d0000000000`. Reaching this boundary does not explain the initiating
exception or prove that the prior finalizer panic was fixed.

## Binary evidence

The original libc imports this NID from libkernel and places its GOT entry at
RVA `0xb8828`, reached through PLT RVA `0x520`. One exact direct call to that
PLT occurs at original libc RVA `0x54530`, inside the complete 90-byte
procedure `0x54510..0x5456a`. Its input is the cursor's PC at offset `0x18`.
Return 2 selects cursor kind 3; return 1 selects kind 2; other results select
kind 0. The original libkernel implementation and names for the two special
classes are unavailable; no broader positive classification ABI is claimed.

REA/Hopper evidence
`ev_6a61037b4378eec217323bc69ad232a5fc70acbf96f340c58d6dee9d57a690b2`
confirms the consumer's argument and result handling. Its private tiny ELF
preserves the complete original procedure at its original RVA. The original
54-byte `__cxa_throw` export is separately identified by NID `vkuuLfhnSZI`,
RVA `0x60510`; its initial prologue saves RBP and initializes the exception
header. Identifying that export does not identify a particular runtime throw.

## Implemented predicate and boundary

The runtime records guest signal handlers but does not deliver host signals
to guest code or install guest signal-return trampolines. Consequently, its
guest trampoline set is empty. `_is_signal_return` now returns 0 for membership
in that empty set. It does not dereference the PC, validate mapped memory,
alter errno, synthesize a signal frame, suppress a throw or change unwinding.
Host fault handlers, ordinary guest code and translator return sentinels do
not belong to that guest trampoline set. Adding guest signal delivery must
also implement classification for the actual installed trampoline addresses.
Unknown/unmapped address validation remains the responsibility of the existing
module and memory APIs, with their distinct error results.

## Validation and limits

Native game/probe and Rosetta probe builds pass. All 23 loader tests pass in
each architecture. Owned synthetic callers use the actual predicate and
`__error` NIDs to check zero classification and unchanged errno for known
guest code, null, 1, two return sentinels, an unmapped PC, the command-guard
marker and the maximum 64-bit value. No test dereferences those PC values.

A private external-input fixture under `out/metal-resource-2026-10-24/` also
executes the unchanged original 90-byte consumer, its 36-byte address-space
helper and original PLT at preserved RVAs. It supplies owned cursor storage
and a zero initialization dword, exercising the helper's actual zero-state
path. All eight PC cases return 0 and store cursor kind 0
on native and Rosetta. Original procedure bodies stay in private analysis
artifacts, outside tracked test source. Before/after source SHA-256 remains
`6e84e6dc84305a0fcf43563ff454a2866208ae4d61f1a4ece1ba717cdc1f1170`.

These fixtures cover the predicate and its original consumer; they do not
establish successful exception handling or long gameplay stability. No game
process or GPU test was launched for this API change. Original game files and
master saves remain untouched.

Evidence files under `out/metal-resource-2026-10-24/`:
`signal-return-{native,x86}-{build,loader}.log`,
`signal-return-{native,x86}-original.json`,
`test_original_signal_consumer.py`, `libc-signal-classifier-tiny.json` and
`rea-libc-signal-classifier.json`.
