# Bloodborne renderer investigation — 2026-10-21

Target: stable native 1920×1080 gameplay toward 60 FPS. The original dump,
prepared original ELF and master saves remain read-only. Gameplay tests use
separate cloned user directories. No sustained 60 FPS result is established.

## Original wind update

REA/Hopper analyzed the complete original unwind procedure at ELF RVA
`0x2313870` (11,646 bytes, ending exclusively at `0x23165ee`). The source's
preferred/Binary Ninja address `0x2713870` includes a `0x400000` base; it is
not an original ELF RVA. The analysis ELF retains exact original instruction
bytes and RVAs with synthetic headers and omits other functions and data. It
is never executed or substituted into the game.

Original procedure SHA-256:
`7ef49d44de65873820c2bfd76c794be1d903045baa1fb7430a2d77452c3a35bc`.
REA evidence: `ev_ccbae12a866d8c91eca8cd603529837747d71cd88985f0f7548dc6c17176621e`.
Evidence, original-byte extraction manifest and assembly are retained under
`out/renderer-rea-2026-10-21/`. Hopper's decompilation was oversized; assembly
and the original unwind extent support the findings, not inferred C types.
The owned REA target was closed after export.

All four observed timing loads are already covered by runtime frame-time
redirects: original RVAs `0x2313d8d`, `0x2313dad`, `0x2313db7`, `0x2315d71`.
They cover the device-mode and object-presence branches. No missing fixed
1/60 load was found in this procedure. Its interaction clocks have not yet
been linked to fields of the wind shader's constant buffer, so this is not
proof of correct displayed foliage motion or original PS4 sway speed.

The earlier shader fix remains present: the sixteen modified `V_CNDMASK_B32`
selections in Dream wind VS `e4abfca709b00ef3` preserve ABS/NEG operand bits.
Changing the authored wind speed without a reproduced timing defect would
hide rather than explain the remaining jitter.

## Fence mapping metadata cache

`BB_METAL_CP_FENCE_MAP_CACHE=1` is an opt-in CPU optimization. Pending fence
queries previously copied the complete direct mapping table for every read.
The new cache retains only a validated immutable sorted mapping snapshot and
uses binary lookup. It still checks each current pending destination and
returns conservative overlap on private, partial, ambiguous or racing spans.
It never caches fence values, destinations, payloads or readiness results.

The existing fence lock covers each query. The runtime's odd/even generation
is checked before/after snapshot construction and after every query, including
reuse. Aliases, physical zero, both halves of eight-byte fences, remaps,
generation regression/exhaustion and allocation failures are covered. The
disabled implementation remains unchanged. Reporting copies counters under
the same fence lock once per reporting interval; no renderer lock is held.

Independent isolated review passed its CPU fixture, including 2,000 queries
against a separate legacy lookup oracle. Full serial CTest with the cache
enabled: 94 passed, two expected private-input skips, zero failures, 116.31 s.
The actual pixel and vertex root renderer fixtures passed both before and
after enabling the cache. Those exercise queued callbacks, physical alias
writes and runtime remaps after a natural command cut, while the actual
consumer command remains unsubmitted; mutations wait for that consumer.

Clean OFF/ON/OFF/ON stationary Dream comparisons use one frozen runtime,
SHA-256 `e81213eb31bd7e539d0c26fca545913afe5e19efc3b7e465fe14bc26a2873528`,
with the other experimental root, binding, pruning and journal options off.
The four 90-second runs measured OFF 39.8374 / ON 39.7967 / OFF 39.8595 /
ON 39.8300 displayed FPS. All were on AC at native 1080p with zero missing
presentation timestamps and zero GPU failures. The means are 39.8485 versus
39.8134 FPS (-0.088%); this is neutral. The cache remains off by default.

The opt-in current-pending destination index additionally versions metadata
on every insertion/overwrite and actual ticket-matched retirement, under the
same fence lock. It retains physical four-byte destination intervals, not
readiness results. Both map and pending epochs must agree, including a final
even/equal map check. Private/partial/racing/exhausted/failed proofs remain
conservative. The full suite with both flags enabled passed 96 tests with two
expected private-input skips in 113.59 seconds; actual PS/VS alias, remap,
callback and post-cut consumer fixtures also passed. The isolated stable-epoch
CPU microbenchmark is not an FPS result. Live comparisons are recorded below.

## Foliage input diagnostic

A bounded, opt-in diagnostic captures the prepared VS UD6 descriptor table
and the exact declared wind (656 B) and view (864 B) leaves. A dedicated
`BB_METAL_WIND_CAPTURE_DIR` leaves the production texture fast path enabled.
It requires current mapping generation, CP readiness, callback and GPU-writer
proofs before CPU reads. Prepared arena roots additionally require their
current snapshot owner. Direct roots must be wholly outside the arena and
cannot physically alias its backing. Failed proof records a reason and never
requests a drain. Same-queue GPU copies hold source read leases; the report
requires the actual draw to be encoded on the same command. Late cuts and
skipped consumers remain incomplete observations. Raw CPU writes are not
instrumented by these leases and can therefore be observed as differing data.

The initial four runs rejected all 960 rows for missing prepared root
ownership, without copying any payload. The live shader still contained all
sixteen corrected CNDMASK selections. The stricter direct-root observer then
captured 240 complete same-command draws each with TAA on and off: 720
descriptor/wind/view input pairs in each run, no denied proofs and no CPU/GPU
mismatches. The active phase field at byte 140 progressed monotonically at
0.993524 and 0.993334 raw units per host second respectively, without resets,
reversals or repeated per-frame values. Other seven phase drivers were zero.
No source aliases were found. This does not reproduce a doubled wind clock
or mutable-input corruption in the sampled view. The original writer's units
and later actual vertex reads remain unproven. Jitter is not declared fixed.

Captures split passes and are diagnostic only. Identical CPU/GPU copies do
not prove identical later vertex reads; different bytes do not alone prove
an invalid writer. The synchronized/native-indirect-off control rejected all
240 observations for pending callback effects, without reading the payload.
Its video is a visual control only; no phase-speed comparison is supported.

## CPU work chosen for optimization

The instrumented Dream interval before video/sampling contained 178 complete
draw frames: 18.883 ms inclusive draw recording, 7.283 ms texture checks,
0.122 ms pipeline setters, 0.117 ms fixed setters, 0.501 ms resource binding
and 0.530 ms draw encoding per frame. These nested timings are not additive;
instrumentation is not clean FPS evidence. Video/sampling later increased API
waits and was excluded from this window. The fixed-state cache prototype
stays isolated because its measured opportunity is small.

Large full CPU texture checks have an opt-in two/four-worker candidate using
the unchanged per-chunk CRC function. A serial first chunk preserves the
earliest changed-input exit. Remaining workers only read the protected source
and expected chunk hashes, keep private counters and synchronously join before
CP recording continues. The renderer owner records actual bytes; all existing
source/mapping/callback/writer checks and immutable upload versions remain.
Inputs below 1 MiB and the default disabled path remain serial. Public CPU
fixtures and isolated throughput results are retained under `hash-review/`.
The actual renderer fixture dispatched two worker scans, preserved queued
old/new texture pixels and covered a changed final partial chunk. Full serial
CTest passed 98 tests with two expected private-input skips, zero failures,
in 115.32 seconds.

## CPU candidate gameplay comparisons

One frozen runtime (`e87be18ca92e18e183e5ba6190062c84b4f4f28c26c8d8b3b06c279b2e33f44b`)
ran 60-second native 1080p stationary Dream comparisons on AC power. Each run
used the same cloned Dream save and validated the initial map and pose. The
texture-size histogram was enabled equally; no GPU/CPU timeline, shader dump,
screen recording or capture was enabled. These runs form their own comparison;
the earlier 39.8 FPS runs use an older save chronology.

| Candidate | Displayed FPS | Missing presentation timestamps | GPU failures |
| --- | ---: | ---: | ---: |
| Control A | 37.2109 | 0 | 0 |
| Destination index | excluded | 1 | 0 |
| Four-worker texture hash | 37.3399 | 0 | 0 |
| Index plus four-worker hash | 37.3170 | 0 | 0 |
| Both plus direct pixel roots | 31.5413 | 0 | 0 |
| Control B | excluded | 1 | 0 |

Every run stopped as planned. The clean CPU candidates differ by less than
0.4 percent from Control A, without a clean closing control. No repeatable
FPS improvement is established. Both caches and parallel hashing remain off
by default, as do direct pixel roots, which regressed in this comparison.

## GPU timing coverage

The retained all-passes capture recorded about 155 natural render encoders
and 101 compute dispatches per complete Dream frame. The old 16-slot pool
captured at most 16 render encoders per frame and exhausted on approximately
90 percent of selected encoders. Its vertex/fragment durations cannot explain
the whole frame. Ordinary compute dispatches were not sampled at all. Stage
durations include pipeline dependencies and are not exclusive shader ALU time;
overlapping intervals must not be added into a GPU-utilization percentage.

A bounded diagnostic pool extension is under validation. Its initial approach
of allocating one counter buffer per slot hit the device's allocation limit
at 256 buffers; the GPU test correctly failed. The replacement uses separate
sample ranges within one buffer and retains each range through asynchronous
resolution. Its CPU/GPU tests passed (including 256 distinct ranges, bounded
exhaustion/recovery, cancellation and HDR pixels), in 0.56 seconds. Normal
rendering and default diagnostic capacity remain unchanged.

The expanded capture used frozen runtime
`4d597aea84f25faa6df33c4a83f671f171985fe0a079d40f818ad68e105ed233`.
It retained 195,640 valid render-stage rows over 1,258 measured frame labels,
about 155.52 rows/frame. The asynchronous counter endpoints record only 61
exhaustions (under 0.1 percent), no attachment/encoding/invalid-result errors,
and no GPU failures. This establishes much wider render coverage, while
ordinary compute, tessellation preparation, adapters, uploader, blits and
presentation still have no stage samples in this capture. An unused requested
compute-counter environment option in the manifest does not imply support.

The calibrated union of sampled render stages is 20.137 ms/frame; the union
of command GPU intervals is 23.068 ms/frame. Neither is exclusive ALU time.
The inclusive CPU frame wall is 26.724 ms/frame, draw recording 18.491 ms and
texture checks 7.355 ms (nested values, not additive). Mixed six-MRT geometry
and material passes account for a substantial part of sampled stage time;
their first shader identity cannot stand in for every shader in the pass.
These instrumented values identify work to investigate, not clean FPS.

An independent retained-state/GCN/MSL screen found no justified forced early-Z
change: the current opaque-export proof already removes artificial kills in
observed depth-tested non-KILL shaders, and none of 691 traced draws sets the
original forced depth-before-shader flag. Generic writable parameter spelling
alone does not prove that Metal disabled early depth testing.

## Native tessellation coverage

The new strict opt-in `BB_METAL_NATIVE_TESSELLATION_EXTENDED=1` admits only
LS `030976bf71b6a530` / HS `f9f5fc457bc359fd` / DS `90b8fb9c35aa6679`.
The original DS has 14 read-only LDS pairs (28 offsets through byte 116),
compatible with the existing 128-byte sparse LS reconstruction and factor-one
HS. Current-attachment feedback still forces translated preparation. In the
retained traced frame, the new gate covers at most five additional draws
before runtime alias checks. The frequently seen `f916abafa71dddf5` hull is
already supported; another missing hull requires inner factor two and stays
excluded.

The private original-shader GPU differential passed 1,440 preparation cases
and 189 domain cases, comparing 339,552 output components. Finite/infinite
outputs match bit for bit; paired NaN payloads are ignored. The admitted
128-byte/factor-one DS90 cases independently produce finite nonzero-w clip
positions and meaningful parameters for every index mode, with numerical UV,
patch and camera/fade variation. This is shader-equivalence evidence, not
gameplay performance or complete renderer admission proof.

## Rejected exact-byte texture comparison

A private prototype replaced repeated full CRC scans with `memcmp` against
a bounded private copy, seeded only from immutable uploader provenance and
copy-then-hash verification. Independent source review found no readiness,
mapping/version, lifetime or 256 MiB payload-budget blocker. Its CPU fixture
passed protected tails, mutation/identity rejection and budget lifetime cases.

Realistic mixed working sets rejected its performance: with production
`ROSETTA_ADVERTISE_AVX=1`, the captured-bin 186.11 MiB unchanged warm pass
cost 6.24 ms for CRC versus 9.73 ms for byte comparison; after a 320 MiB
eviction read, 6.47 versus 9.79 ms. A 190 MiB distribution spanning 1 KiB to
16 MiB and first/last edits also regressed. These are CPU microbenchmarks,
not game FPS. The unbuilt renderer integration and header were removed from
main; prototype, fixtures and evidence remain isolated in `out/`. No gameplay
benefit is claimed and no live test of this rejected candidate was needed.

An independent protection audit also rejected skipping checks on presumed
CPU-read-only textures. The exported direct-map inventory drops protection
metadata, the bridge fabricates `prot=3`, GPU-write mappings grant host CPU
write, and a permanent writable physical-pool alias is not in that inventory.
Internal requested permissions do not prove absence of effective writable
aliases. No texture content/readiness checks were relaxed.
