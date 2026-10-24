# SSD source migration, 24 October 2026

New read-only source roots supplied by the user:

- Game: `/Users/ronnystiftel/ps4/CUSA00900`
- DLC: `/Users/ronnystiftel/ps4/CUSA00900-dlc`

The local launcher is `bash out/run-ssd.sh`. It selects the native arm64
runtime and passes both paths explicitly, including `BB_SOURCE_GAME` so that
settings restarts do not inherit the previous SD-card source. Generated images,
caches, mod views and saves remain under the existing output directories.
The generic repository launcher continues to accept explicit source paths.

Read-only inspection confirms the verified 1.09 executable layout. The source
SFO still reports 01.00, as it did before; no metadata was edited. The new
`eboot.bin` SHA-256 is
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`,
matching the earlier source. The prepared original ELF and libc module also
match the prior hashes. All 28,779 game files and 35 DLC files have the same
relative names, sizes and modification times as the previous inventory.
This is an inventory comparison plus executable/module hashes, not a complete
content hash of every asset. All 16 master-save hashes are unchanged.

The new sources successfully prepare a separate BBPROBE6 image in
`out/metal-resource-2026-10-24/ssd-prepared/`. The preflight lists The Old
Hunters and both Messenger add-ons. Generated content metadata starts with
zero mounted add-ons; actual mounting remains a runtime operation.

Future gameplay uses `out/metal-resource-2026-10-24/run-case-ssd.py`, which
records both source roots and an `internal-ssd` storage cohort. Earlier SD-card
logs and frozen harnesses remain intact. The existing shader cache is retained;
this is not a cold-cache loading comparison. No loading-time or FPS improvement
is inferred from moving the files alone.

Evidence is retained under `out/metal-resource-2026-10-24/`:
`ssd-preflight.json`, `ssd-prepare.log`, `ssd-migration-check.json`,
`ssd-game-paths.json`, and the separate `integrity-ssd-{start,final}.json`
snapshots. The final snapshot is written after the SSD gameplay checks.

## First SSD replay

`resource24-ssd-unwind-soak` used the frozen native binary
`a4fb58eee8668b5d0bce3bba86362e93de486cee5c0d89c94fedebcc2962665a`,
native 1080p, TAA/shadows/SSAO, no frame generation, AC power at both ends,
cloned saves and the existing shader cache. All 956 recipes became ready.
The 780-second plan failed at 581.223 seconds to the last submission after
four lamp-travel requests. It is not a completed stability pass.

The optional original-function observer captured `St9bad_alloc` on
`GXRenderThread`, with immediate throw caller at aggregate image RVA
`0x573ec02`. Original code subsequently reported an uncaught `std::bad_alloc`
and raised debug exception `0xa0020008`. The immediate caller belongs to a
throw helper; its exception-payload size does not identify the failed game
allocation request. The failing request and internal heap remain under
investigation. Global pool usage alone does not prove physical RAM exhaustion.
The later host mutex errors occur during termination, after the guest failure.

The last shadow audit records 6,919 completed positive GPU clears, four binding
revalidations and zero readiness rejections. Recorded renderer and presentation
command rows have no Metal error. One presentation timestamp is missing at
the stop. These observations do not establish complete GPU/helper coverage.
An eight-second own-window clip and two extracted frames show the loaded scene
after travel, with character/camera movement; they are not a quantitative
foliage-jitter or shadow-registration comparison.

The diagnostic opening 90 seconds, before travel, cover 4,440 frame intervals:
texture change checks average 7.702 ms within 11.711 ms of texture resolution
and 20.145 ms of producer CPU wall time. Renderer GPU interval union is
15.061 ms per frame with zero CPU waits requested in those command rows.
CPU spans are nested and GPU intervals include dependencies. This is not a
clean FPS comparison. Source paths are recorded in the run's metadata.

After this failed replay, the separate integrity check passes for all source
inventory entries, executable hashes and 16 master-save hashes. No original
game/DLC files or master saves were modified. Full evidence is in
`ssd-opening-diagnostic.json` and the replay's `launch.log`/`metrics.json`.

## Second SSD replay and narrower allocation evidence

The next frozen native binary,
`8895a8eeb20790c4e50a048f19c4ea60329a0c4206594d18cd316e7cb8ffb081`,
adds failure-only allocation observations and the independently tested net
flexible `MAP_FIXED` accounting fix. The latter leaves quotas unchanged.
Native/Rosetta memory fixtures pass; the combined build passes 29 loader tests
on each architecture. Focused Metal coherence/foliage and msync checks pass
4/4, and the original private HTILE GPU regression also passes.

`resource24-ssd-allocation-soak` fails at 441.977 seconds to last submission,
after 24,278 submitted frame intervals and three travel requests. No frame
timestamp is missing in the recorded partial interval. AC power is reported
at both ends. Heavy CPU/GPU timing traces are disabled, but stability/audit
instrumentation still excludes this run from clean FPS comparisons.

The original default-new failure block reports a normalized 384-byte request,
with game caller RVA `0x2a3bbaf`. Current malloc/calloc callbacks still point
to original game RVAs `0x20809f0`/`0x2080cd0`, and new still selects the
original libc implementation. No kernel allocation-failure audit row precedes
the throw. The observation therefore does not implicate the corrected fixed
mapping admission check.

REA identifies the containing original game function as a vector-style grow
of 24-byte records, requesting sixteen records at this call site. Its successful
path frees the old buffer. That generic helper has numerous callers; the
higher-level owner and failed arena state require further capture. The game
allocator's initializer maps a fixed 116 MiB arena; this static capacity is
distinct from the runtime's larger direct/flexible pools.

A PID/path-checked read-only Mach helper attempted to read two arena counters
from the agent-launched game. macOS returned `task_for_pid` failure 5; no memory
read succeeded. No debugger attach, process suspension, memory write or security
configuration change followed. The next built-in failure snapshot provides
checked local counters and caller context instead.

Both failed SSD replays retain their own cloned saves and evidence. Repeated
source/executable/master-save integrity verification still passes.

## Third attempt: blocked before gameplay

`resource24-ssd-heap-soak` uses diagnostic binary
`f3143e3d4183d483a25d2c3af85bbc097d68e8d6c187221e57f4f122cd453cc4`.
It prewarms all 956 recipes but cannot activate the window or reach the save.
The activation helper reports requested=0/active=0, presentation stays at zero,
and the startup guard stops the process. The macOS console reports
`CGSSessionScreenIsLocked=Yes`. This is an unavailable gameplay test, not a
game crash or an allocator recurrence. The new bounded heap/owner snapshot is
therefore not yet exercised in real gameplay. AC is connected.

The SSD driver now checks that lock state before creating a replay or launching
the game. Its locked-state preflight was tested: it refuses immediately and
creates no replay directory. Existing activation/presentation guards remain.
Unlocking and keeping the game visible requires the user; no security setting
was changed. See `lock-preflight-test.log`.

## Source and CPU work while gameplay is unavailable

The exact original allocation and free cores pass private serial differential
tests under native JIT, interpreter and Rosetta, each through direct entry and
an owned guest CALL/RET wrapper. Each case executes 109,640 attempts with
53,699 successful allocations, 53,699 valid frees and 2,242 NULL outcomes.
Size boundaries, repeated 384-byte requests, exhaustion, fragmentation,
coalescing, mixed-size traces, block/bin invariants and canaries match. Both
1 MiB and 116 MiB arenas return to their initial state. Normalized trace digest
is `68142e2e182426aa` in all six cases. Original procedure bytes remain private
test inputs, outside tracked source.

This does not validate concurrent heap use, public wrapper/lock behavior, heap
selection or actual gameplay allocation lifetimes. It supplies no evidence for
an allocator-core CPU translation correction. Evidence:
`heap-core-differential-results.json` and `heap-core-differential-feasibility.md`.

Two independently reproducible source corrections are implemented: graphics
flip-label retirement belongs to the graphics ring, and user-event pointers
are delivered atomically through `udata`. Neither is attributed to the observed
`bad_alloc` without live evidence. See
[queue ownership and validation](MACOS_QUEUE_OWNERSHIP_2026-10-24.md).

The combined native game builds. Six focused native CPU/integration checks,
two Rosetta integration/flip checks, three native Metal foliage/texture checks,
and the original private HTILE GPU regression pass. The sandbox-only GPU
attempt skips for lack of device access; the device-enabled rerun executes all
three checks successfully. These are fixture results, not a completed gameplay
soak or a new FPS comparison. Source inventory, executable hashes and all 16
master saves still match the starting SSD snapshot.
