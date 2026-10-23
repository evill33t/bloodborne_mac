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
Schema 4 retains the separately loaded camera XYZ and one selected original
object-matrix index, with full descriptor/index identity. Both fresh 240-record
runs found zero within-draw variation in all four observed groups. There were
849,960 stationary and 373,625 camera-motion selected invocations for each group.
All 480 selected matrices had the known plain stride16/float4 descriptor form and
nonzero data. The exact captured ignored padding bit 15 is admitted separately
from true cache/address-swizzle flags; ADD_TID and unknown forms fail closed.
Other matrix indices are counted separately, avoiding a false race finding from
legitimate per-index differences. Matrix mapping status remains explicitly
unobserved. These observations do not establish every input or frame as correct.

The original shader math review found no further concrete translation defect in
its 16 sine/32 fract operations, authored clamp modifiers, scalar branch conditions
or matrix addressing for the captured plain descriptors. This does not establish
bit-exact PS4 arithmetic. No additional clock or snapshot workaround is enabled.

## Validation status

Both final x86-64/Rosetta and arm64 builds passed. The broad runtime suite passed
112 executed tests with 3 private-input skips; the arm64 suite passed 20 with one
private-input skip. The original shadow shader differential was supplied its
private input and passed separately. Final focused depth-bounds/frame-generation/
wind tests passed 8/8, the Python protocol suite passed 15/15, and the final ARM64
atomic-helper/lifecycle/matrix-subgroup GPU fixture plus exact original instrumented
VS compile passed. The CPU emitter fixture verifies the normal OFF source remains
byte-identical after stripping diagnostic plumbing.

Only the depth/color alias regression is proven fixed. The reported foliage
jitter and sliding shadows remain unresolved; none of the above fixtures or sparse
observations is presented as a visual cure. No 60 FPS improvement is claimed from
instrumented captures. The final uninstrumented replay results follow below.


## Final clean replays and pause

Three 90-second Rosetta gameplay replays completed, on AC power at native
1920×1080, TAA/FG off, all shader probes/tracing/stage counters/recordings off,
and all 951 loaded prewarm recipes ready. Routes included stationary viewing,
camera orbit, forward/backward movement and attacks. Each run stopped as planned
with **zero GPU failures and zero missing presentation timestamps**.

| Replay | Rendered FPS | Displayed FPS | p99 presentation interval | Worst interval |
| --- | ---: | ---: | ---: | ---: |
| Fog baseline | 24.48 | 24.49 | 58.33ms | 91.67ms |
| Fog with alias fix | 31.36 | 31.37 | 33.34ms | 41.67ms |
| Dream with alias fix | 38.28 | 38.28 | 41.67ms | 50.00ms |

Rendered/displayed rates use in-window periodic counters; the complete per-frame
presentation stream averages 24.47, 31.35 and 38.19 FPS respectively, with slightly
different timestamp windows. Generated-frame count is zero in every run.

QEMU used approximately one CPU core at preflight and again after the matched fog
pair. These are shared-load observations from one pair, not an isolated causal
speedup or evidence of sustained 60 FPS. The scene and combat paths can diverge.
The fixed fog run’s stationary/camera/attack phases measured 33.85/31.32/30.07 FPS
versus 31.15/22.74/21.71 in the baseline. These short replays do not establish
long-session stability or whole-game visual correctness.

Authoritative checked metrics: `clean-validation-results-checked.json`, with
per-phase results, physical output extents, binary identities and diagnostic flags.
The copied driver’s summary label erroneously said diagnostic=true for these
clean runs; the underlying per-run metrics and all actual environment flags show
otherwise. The checked summary derives its flag from those actual metrics, and
the reusable driver’s reporting label has been corrected. Raw logs are retained.

The final original-file check passed: all 28,779 dump file size/mtime entries,
original eboot hash, prepared original ELF hash and 16 master-save hashes match
their baselines. `original-integrity-final.json` records the exact scope and
hashes. `owned-processes-final.json` confirms no owned game process remains.

Paused at the user’s request after this validation round. The depth/color alias
fix is validated; the reported foliage jitter/sliding shadows and the 60 FPS goal
remain open. No further optimization or unattended gameplay is running.


## Saved state for the next round

Source checkpoint: `b4575f6` (`Expand depth alias and consumed shader diagnostics`).
Final frozen gameplay binaries are in `out/foliage-shadows-2026-10-23/rosetta-final2`
and `native-final2`, with adjacent SHA256 manifests. Gameplay validation here uses
Rosetta; ARM64 build and fixture results are separate from ARM64 gameplay proof.

The diagnostic remains opt-in:

```sh
BB_METAL_WIND_CONSUMED_FRAMES=120
BB_METAL_WIND_CONSUMED_PASS=color
BB_METAL_WIND_CONSUMED_DIR=/absolute/external/capture/directory
```

Arm it with the existing bounded trace trigger. Each draw owns a separate private
480-byte record; completion owns its resources and metadata. The protocol parser
is `tools/analyze_wind_consumed.py`; offline mutation tests are
`gpu/metal/tests/test_analyze_wind_consumed.py`. Raw schema4 evidence is in
`out/metal-unattended-2026-10-20/visual23-consumed4-{stationary,camera}/consumed`.
The normal shader ABI is unchanged when this diagnostic is OFF.

Resume by separating the actual shadow-mask factor from AO, lighting and fog at
the reported arch during camera motion, rather than assuming every dark patch is
a static-light shadow. The existing controls did not establish the cause. For
Dream foliage, retain the correct clock and use stable frame pacing plus actual
vertex/output evidence before another animation workaround. Do not treat sparse
negative race observations as complete proof of input lifetime. Neither a broad
renderer rewrite nor new default performance flags were added in this round.
