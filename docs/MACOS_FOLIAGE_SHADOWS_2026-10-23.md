# Foliage and camera-dependent shadow investigation — 23 October 2026

The reported scenes are Hunter’s Dream flowers/grass and the foggy saved position
by the arch (map 18010000). This investigation keeps the direct Metal backend,
native 1920×1080 render target, and original dump/master saves unchanged.

## Confirmed renderer correction

`BeginPass` used to promote every loaded depth/color attachment to the newest
writer. Reading an older depth attachment could therefore hide a newer R32 color
image at the same guest allocation and make a later texture sample select stale
depth. A real Metal regression reproduced red=102 instead of 255 before the fix.

Writer versions now advance for attachment clears and actual encoded draws that
can write. A depth-writing draw later in a read-only pass becomes authoritative;
stencil-only work leaves the Z allocation’s writer identity and depth-bounds
snapshot revision unchanged. All primitive paths use the same draw boundary.
This preserves frame-generation revision checks. The regression passes after the
fix, including color writes, explicit clears, same-pass depth writes and independent
stencil writes/clears. Logs: `out/foliage-shadows-2026-10-23/alias-before-gpu.log`
and `alias-after-gpu.log`.

This is not established as the arch artifact’s cause. The fresh arch target log
has 377 color and 12 depth addresses with **zero shared base addresses**. The
version tracking remains conservative about possible fragment writes; it does
not prove that pixels survived every test/discard.

## Measured shadow work

Three cloned-save, FG-off/TAA-off diagnostic replays recorded stationary views,
camera orbit and movement/combat, with original settings, SSAO disabled, and the
experimental optimized light grid disabled. Generated-image controls are external
to the original dump. Recording, tracing and GPU stage counters make these runs
unsuitable for clean FPS comparisons. All reached their planned stop with zero
GPU failures. The first capture attempt failed its scene guard because that test
still expected the Dream pose; it is excluded and retained as failed evidence.

| Arch diagnostic | Measured frames | 4096² depth-atlas stage union/frame | Shadow-mask stage sum/frame |
| --- | ---: | ---: | ---: |
| Original settings |1817|0.789ms|0.212ms|
| SSAO off |1381|0.813ms|0.220ms|
| Optimized light grid off |1692|0.863ms|0.226ms|

Stage spans include dependencies and may overlap. These are not exclusive GPU
utilization or causal FPS ablations. They do not support attributing the full
30-to-60 FPS gap to shadow-map rendering. Camera-aligned cascades can require
updates with a fixed light; freezing them would invalidate coverage and moving
caster shadows. No unsafe shadow caching is enabled.

Full-window arch recordings are in
`out/metal-unattended-2026-10-20/visual23-fog2-{original,no-ssao,lightgrid}`.
The SSAO control’s startup log confirms `Disable SSAO`. No setting change from
these recordings is claimed as a visual cure.

An original shadow-mask shader GPU differential passed 864 cases: cascade
boundaries, fixed world positions under camera translation/rotation, projective
W, four PCF taps and LESS/GREATER comparison. Maximum original-versus-independent
native-Metal control error was 0.00048822. The looser scalar-versus-hardware
filtering sanity bound is explicitly separate. Fixture success proves the
exercised arithmetic, not live uniform lifetime or absence of the reported
artifact. Log: `original-shadow-projection-gpu-v2.log`.

## Foliage evidence

The earlier half-task-step wind correction and original CNDMASK modifier fixes
remain. Reimported REA evidence retains the complete original wind updater
(RVA 0x2313870, 11,646 bytes, original instruction SHA256
`7ef49d44de65873820c2bfd76c794be1d903045baa1fb7430a2d77452c3a35bc`).
No original binary is patched on disk by this investigation.

The first actual-consumption probe observed already-loaded four wind and sixteen view-projection
register words from shader `e4abfca709b00ef3`, sampling vertices at mask63 in at most
120 frames and two draws/frame. Both 240-record runs had zero invalid observations,
null reads or within-draw variation: 849,960 selected invocations stationary and
371,761 during camera motion. The stationary first draw’s phase increased smoothly
at about 0.496 raw units/host second with no reversals. Draw identities change under
camera culling, so switching between object phases cannot be interpreted as a
single wind timeline. This evidence does not justify another clock-speed change.

The bounded diagnostic is off by default and uses a separate shader/cache ABI.
It adds no guest-memory rereads, CPU snapshots, command waits or drains. Atomics
perturb timing; stable sparse samples cannot prove all vertices, inputs or frames.
Schema extensions retain the separately loaded camera XYZ and one selected original
object-matrix index, with full descriptor/index identity, so legitimate per-vertex
matrix differences are not misclassified as a race. Matrix mapping status remains
explicitly unobserved.

## Validation status

Final schema fixtures, runtime suites, clean FPS and gameplay results are recorded
below after completion. The remaining foliage jitter and sliding-shadow report
must not be described as fixed solely from source review or fixtures.
