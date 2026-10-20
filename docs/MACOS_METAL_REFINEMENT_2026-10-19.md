# Metal upload, shader and fog refinement — 2026-10-19

This follow-up retains native 1920×1080 rendering and the direct Metal backend.
All game launches use copied save directories and externally generated runtime
images. No patch is written into the original dump or master save directory.

## Current acceptance status

The fog allocation fix, fast detiler, immutable GPU uploader and guarded
vertex-fetch fusion are enabled by default. Clean matched native-1080p
benchmarks improved from 48.754 to 52.977 FPS (+8.66%), with all submitted
frames presented. Sustained 60 FPS and elimination of first-use shader
stutters are **not** achieved.

The final 330-second default-settings route completed four requested
transitions with no GPU failure or fence timeout. It exposed five previously
unseen pipelines, including a 593.968 ms compilation wait and a 1,349.997 ms
displayed-frame gap. They are now retained for startup prewarming. Their
warmed replay remains pending; this report does not claim them fixed.

The Mac entered clamshell sleep at 22:09:30 local time, after the last gameplay
metrics were saved at 22:07:05. The clean optimized/reference benchmarks were
saved at 21:56:16/21:59:09, respectively, before sleep. Thus the captured
gameplay timings above precede the interruption.

The final 69-test run initially had 65 passes, two expected private-input
skips and two timeouts spanning sleep (609/923 seconds despite 60-second
limits). Both timed-out detiler runs printed 758 byte-exact CPU cases. A
retry passed the CPU fixture in 43.91 seconds; the GPU fixture again timed
out across sleep. Across the run and retry, **66 tests passed, two skipped
and one GPU fixture remains incomplete**. Earlier completed GPU differential
and uploader checks are recorded below. No test timeout was enlarged to
conceal the interruption. The power log confirms Clamshell Sleep followed by
Maintenance Sleep/DarkWake cycles. No power setting was changed.

Final integrity verification passed: all 28,779 original-file sizes and
modification times match the baseline; original eboot and prepared-original
ELF SHA-256 hashes match; all 16 master-save file hashes match. See
`out/metal-refinement-2026-10-19/final-original-integrity.log`.

## Implemented upload improvements

The CPU detiler now computes macro-tile addresses once per 8×8 microtile for
supported one-, two- and four-byte layouts. Other layouts use the original
reference implementation. A 758-case differential covers padding, unaligned
destinations, several dimensions and all supported tile modes.

The Metal uploader snapshots each physical guest allocation into an immutable
buffer, detiles its raw words on the GPU, then blits into the destination
texture. It supports selected four-byte formats plus eight-byte RGBA16Float
and RG32Float. Raw copies preserve half/float bit patterns, channel order and
sRGB bytes. Unsupported layouts fall back before any GPU work is encoded;
an error after partial encoding fails that execution instead of mixing paths.
Resources remain owned by their command buffer until GPU completion. The
uploader adds no command-buffer submission or CPU/GPU wait.

Initial texture-cache chunk hashes now describe that immutable snapshot too.
They no longer reread the guest source after recording its upload. This keeps
the cache baseline tied to the pixels actually uploaded when the guest changes
its source concurrently. Hash scanning still reads every byte; any timing gain
from snapshot locality must be measured rather than assumed.

The GPU fixture covers 121 format/layout/slice/mip cases and queued versions
whose CPU source changes before submission. Production renderer tests also
cover nonuniform texture data, sampler changes, memory-map refresh inside a
pass and encoder boundaries.

The fast CPU detiler and GPU uploader are enabled by default. Diagnostic
overrides are `BB_METAL_FAST_DETILE=0` and `BB_METAL_GPU_TEXTURE_UPLOAD=0`.

Captured cold upload CPU times:

| Resource | Original CPU | Fast CPU | GPU upload |
| --- | ---: | ---: | ---: |
| 64 MiB R32Float | 149.199 ms | 64.994 ms | 30.940 ms |
| 1080p RGBA16Float at `0x80751e0000` | 29.183 ms | unchanged eight-byte path | <10 ms |
| 1080p RGBA16Float at `0x8071a10000` | 26.136 ms | unchanged eight-byte path | <10 ms |

These are matched resources from separate scene loads, not an overall FPS
comparison. Seven matching eight-byte uploads totalled 172.270 ms on the old
CPU path and less than 82.267 ms on the GPU path. Two approximately 21 ms encode
outliers remain; new optional allocation/copy/encoding scopes distinguish
their causes in subsequent captures. CPU encode duration is not GPU duration.
Detailed data and threshold-censor limits are retained in ignored output at
`out/metal-refinement-2026-10-19/texture-upload-timings.md`.

## Experiments and acceptance criteria

`BB_METAL_BINDING_CACHE=1` avoids repeated buffer/texture/sampler identity
bindings inside one encoder. It retains per-draw user data, always refreshes
the memory map and invalidates at encoder boundaries. Depth-bounds texture
bindings remain unconditional. Its production fixture compares actual pixels
with the reference path, including a new guest mapping introduced mid-pass.

Two exact captured material pixel shaders were used to test scalar-load
optimizations. CPU-frozen values remain rejected when snapshot ownership is
unproven. A direct-address variant keeps values GPU-time but showed no clear
material-pass benefit. A SIMD variant proves scalar address provenance, elects
one ordinary lane and preserves helper-thread loads for derivatives. Both
original shaders pass 32-state GPU differential tests across all six game MRTs
and a derivative observation MRT, with GPU-written descriptors and payloads.

The SIMD candidate nevertheless measured **38.995 FPS** in a valid 70-second
native 1080p gameplay benchmark, p99 33.333 ms, maximum 41.667 ms, zero missing
presentation timestamps and zero GPU errors. It stays disabled. GPU equivalence
does not establish a performance improvement. Direct-root and CPU-frozen
variants also stay disabled.

The initial control run was partly backgrounded and is excluded from FPS
comparisons. `timing_usable_for_benchmark` must be true before accepting a run.
The repeated visible 70-second run with binding cache enabled and SIMD disabled
measured **49.343 FPS**, p99 25.005 ms and maximum 33.333 ms, with zero missing
presentation timestamps or GPU errors. This is consistent with the prior
49.663 FPS warm baseline; it does not establish an FPS gain from binding caches.
The cache remains optional.

The guarded native vertex-fetch path targets the two dominant interleaved
layouts, with 32- and 36-byte records. Original descriptor loads and transform
math remain intact. Exact shader/fetch proofs and GPU-time descriptor guards
permit one bounds-checked record translation instead of five or six separate
payload translations and dynamic format decodes. A different layout, unaligned
record, page crossing, mapping gap or physical-buffer boundary retains the
original fetches. No guest values are frozen on the CPU.

Public synthetic and original captured-shader GPU fixtures passed 141 cases:
23 fast-path cases per layout, 45/50 fallback cases, exact raw fetched values
and complete original VS outputs, untouched XYZ/XY destination components,
and descriptor/vertex changes from earlier queued GPU work. The first live
candidate's eight missing presentation timestamps exclude that run from the
final FPS comparison. A repeated 90-second reference with the fog fix measured
50.506 FPS, p99 25.002 ms and maximum 25.006 ms, with every submitted frame
presented and zero GPU errors. No measured CPU stage exceeded 10 ms.

The clean repeat with the guarded vertex fetch enabled passed the presentation
criterion: **52.977 FPS** over 90 seconds, 4,779 of 4,779 frames presented,
p99 25.001 ms, maximum 25.006 ms, zero GPU errors. It uses native 1080p,
TAA and native motion vectors, with no raw capture, recording or extra
presentation timing diagnostics. A matching reference is measured with the
same executable measured **48.754 FPS**, 4,398 of 4,398 frames presented,
p99 25.002 ms and maximum 33.333 ms. The measured gain is **8.66%** in this
route, with no GPU failures or recorded CPU stage over 10 ms in either run.
The phase gains were consistent: stationary 46.952→50.794 FPS (+8.18%),
camera orbit 46.933→51.229 (+9.15%), and attack 50.203→54.551 (+8.66%).
The logged 1,681,937 guarded candidate draws count eligible shaders, not the
number of GPU guards that actually took the fast branch.
Both runs use executable SHA-256
`359f171764b67ca710d7a99f411667505923b89e8496898ab86019934d387d51`.

Guarded vertex-fetch fusion is now enabled by default;
`BB_METAL_NATIVE_VERTEX_FETCH=0` retains the reference. The optional binding
cache, SIMD scalar loads and direct-root variants remain disabled.

## Fog allocation fix

The remaining fog loss is reproducible in the original saved view at faster
rendering rates. A 30 Hz control is smooth; keeping all conversion patches
active but running at 30 Hz is also smooth. Disabling selected conversion code
sites at 60 Hz does not eliminate the fault. A narrow duration-contract fix
restores three original minimum disposal durations to 1/30 second; it is
correct for their native callers but does not fix this fog fault.

Captured shader inputs match their corresponding CPU snapshots in the ranges
actually used by the fog shaders. Submitted fog draws map to renderer draws.
Heavy command-stream snapshots slow the game to 30 Hz and suppress the defect,
so they cannot be used to assert that the fast-rendering case is healthy.

Default-off native diagnostics now join original draw writers to actual
consumed pixel shader `0x80d537b100` and active producer calls. They preserve
original execution and use fixed slots with background output. In the initial
lightweight capture, 1,379 unique packet/writer/producer matches establish
ownership. The same 70 appearance objects enter the producer in each of 25
emission windows; all observed early count, appearance and mode checks pass.
Linked draw writers still decrease every fourth emission window. This locates
the omission after those early checks, rather than proving a depth, culling
or Metal shader failure.

The first native capture and asynchronous image capture had different time
windows. They cannot establish direct image-to-entry correlation. Later
captures must align the trigger and avoid `DC_TRACE_CB`, which forces matching
draws to trace and ends their passes.

The aligned allocation capture pairs 1,846 native inputs/results and observes
70 actual status-9 failures with null output. Their requested sizes fit the
four-MiB resource's declared capacity. Frame 3344 has 49 unique fog object draws
versus 62 in its healthy reference: its 13 missing object identities exactly
match the 13 genuine fog allocation failures in that preparation window. The
pre-particle image is unchanged, while normal fog contribution falls by about
33.7 percent in the measured left region. This establishes a causal allocation
abort. Subsequent bounded snapshots show that all six providers share a fixed
160 MiB arena. Several failures have less than four MiB total free, insufficient
for the resource's four-MiB backing request, while reclamation still progresses.

The fix changes the original creation configuration's capacity from 160 to
320 MiB in loaded process memory. Both backing allocation and arena setup read
that same field. The supported image hash, exact original prologue, original
caller and original capacity are checked before writing its four-byte value.
The original allocator, mapping, disposal and retirement code continues to
execute. The 320 MiB capacity is now the default. The override
`BBHOST_TRANSIENT_GPU_MB=160` preserves the control path; the larger arena
reserves an additional 160 MiB.

The fixed-view fog comparison uses a 60 FPS cap with TAA off to isolate the
allocation effect; the clean FPS benchmarks above use TAA on and a 120 FPS
cap. In the matched 320 MiB capture, all 1,762 completed allocations succeed, with
zero status-9 returns. One final input crosses the diagnostic stop and its
return is unknown. All 26 complete CP frames have 61 fog draws, with 1,423
unique producer/writer/packet links. Peak observed use is 171,871,056 bytes,
which exceeds the original capacity. The 400 before/after HDR buffers contain
200 validated frame pairs with no layout, padding or finite-value errors.

| Region | Worst neighboring-frame fog loss, 160 MiB | 320 MiB |
| --- | ---: | ---: |
| Far city | 26.17% | 0.623% |
| Left fog | 72.47% | 1.469% |
| Arch intrados | 1.830% | 0.860% |

No fixed-capture frame loses more than five percent in these regions after the
fix. This measures sudden contribution loss, allowing natural fog animation
between runs. Increasing the arena relocates the HDR target; diagnostics now
follow the exact shader hash and full-HD half-float target. The old unverified
depth-copy address is omitted after relocation. Detailed proof is in ignored
output at `fog-audit/budget-320-roi/findings.md` and `acceptance.json`.

The 330-second movement test crossed Central Yharnam, Hunter's Dream and
Cathedral Ward, with four successful transitions and movement in all three
areas. Its late diagnostic captured 1,000 completed native allocations: all
succeeded. Peak late arena use was 149.8 MiB, below the earlier 163.9 MiB peak.
Reclamation continued, with 32 free-space increases over four MiB and falling
outstanding counts. All 200 late HDR pairs validated. The moving view cannot
use the fixed-camera fog-loss ratio; its background changes naturally.

The route reported no GPU failures, fence timeouts or unavailable-drawable
skips. Resident memory rose from 3.61 to 4.02 GiB, peaking at 4.09 GiB; this
proves bounded observed native-pool pressure, not globally flat memory.
Video and raw captures exclude that route's aggregate FPS from clean
performance comparisons.

## Pipeline retention and presentation diagnostics

The route exposed first-use pipeline waits of 288.009 and 123.934 ms. The
persistent recipe cache was exactly at its 512-entry count limit, using only
33.2 MiB of the 128 MiB byte budget. Both waited recipes were retained just
after compilation. Without a pre-run inventory, eviction cannot be separated
from a previously unseen source/descriptor variant for those exact waits.
One closely related shader differs in float versus unsigned texture sampling;
those identities must remain distinct.

The cache now retains up to 1,024 recipes, preserving the 128 MiB total and
one-MiB per-recipe limits. A first foreground warm hit queues a bounded
background modification-time update. Background prewarming does not promote
unused entries, repeated hits coalesce, and file contents stay unchanged.
Updates use an opened regular file with symlink refusal and a single-link
check. No filesystem operation runs under the warm-cache lookup lock.
The real Metal fixture passed persistence and reload of 513 descriptors,
coalesced recency updates, retention at the count limit, unchanged JSON bytes
and symlink/hardlink sentinel checks (`prewarm-policy-fixture.log`).
The larger count can retain more driver PSOs; the serialized byte budget
does not bound their driver memory.

Presentation diagnostics distinguish submission deadlines, present-command
GPU execution and actual compositor presentation. They run only when
`BB_METAL_PRESENT_TIMING_CSV` names an output file and are excluded from clean
FPS results. `BB_METAL_DISPLAY_LINK_LATENCY=2` is an optional pacing experiment;
the default remains one frame pending live evidence. Apple's supported latency
values are one and two. The existing command-buffer presentation API follows
[Apple's Metal display-link sample](https://developer.apple.com/documentation/metal/achieving-smooth-frame-rates-with-a-metal-display-link).

The latency-two diagnostic still dropped seven of 4,817 measured drawables.
All seven present commands completed before their target presentation time
and reported no GPU error. A measured in-flight limit of two would be inert: at
every submission only one earlier present command was GPU-incomplete. A
one-frame limit was therefore tested separately. Its initial 31 FPS reading
was startup, not steady gameplay: the measured 70 seconds reached 52.906 FPS,
with four dropped drawables and a 50.000 ms maximum displayed-frame gap.
This capture also contains diagnostics and cannot establish a clean FPS gain.
The one-frame experiment was removed. These experiments do not establish a
pacing fix; the default remains unchanged.

The frame-time logger now retains the last actual display timestamp across a
dropped drawable, so its next interval includes the complete visible gap.
The harness also derives intervals directly from nonzero actual presentation
timestamps. Reanalysis corrects the latency-two maximum from 37.500 to
45.833 ms, and the capture-heavy movement route from 183.334 to 466.667 ms.
The clean reference's 25.006 ms maximum is unchanged. Dropped drawables remain
reported; no timestamps or FPS counts are synthesized.

The capture-free replay used the same area order and four transition
requests, with a different movement cadence because the earlier quality/video
overrides were removed. It recorded no GPU errors, unavailable-drawable skips
or fence timeouts, and only two CPU GPU-fence waits totalling 0.6 ms. Eight
drawables were dropped; its 48.152 aggregate FPS includes loading and is not
a scene benchmark.

Its five cold waits total 1,236.853 ms and correspond to five new retained
recipes: four render pipelines and one compute pipeline. They occur at
measurement time 218.955–220.161 seconds during ordinary movement in
Cathedral Ward, while the world state remains valid. They precede both the
fourth requested warp (240.104 seconds) and a later same-area reload around
316.7 seconds. New vertex code includes real export/constant/matrix changes;
incompatible sources must not reuse an older PSO.

The cache grew from 512 to 517 recipes (35,173,553 bytes), evicting none; 317
older recipe modification times advanced after foreground use. The two
original 288.009/123.934 ms recipes retained identical content but unchanged
modification times. Their usage is unproven in this cadence, so absence of
their renderer keys is not proof those exact stalls were fixed. Detailed
checks are in `final-prewarm-replay-comparison.json` and its Markdown report.
The existing replay helper accepts `--run`, `--before`, `--targets`, `--cache`
and `--output`, but currently handles render recipes. A baseline for the five
new recipes and compute-recipe coverage must still be added before the awake
warmed replay can prove their foreground use. Neither the 594 ms wait nor the
1.35-second gap is claimed resolved.

## Validation boundaries

The canonical build passed after the cache and default-budget changes.
The shared default-budget selection and refusal plan passed 848 CPU checks
(`budget-default-fixture.log`). The expanded CTest run registered 66 tests: 64
passed and two skipped because private native-tessellation/motion capture
inputs are not supplied to public CTest. Original captured-shader CPU and SIMD
GPU differential tests were run separately and passed after the final proof
correction. Logs reside under `out/metal-refinement-2026-10-19/`.
The full 66-test suite was repeated after enabling the upload defaults and
adding the finer upload scopes: again 64 passed, two expected skips, no failures
(`defaults-ctest.log`, 103.68 seconds).
The subsequent snapshot-hash change passed all six targeted CPU/GPU detile,
upload and production coherence tests (`snapshot-hash-tests.log`, 79.69 seconds).
These include old-snapshot hashes and pixels after a pre-submit source change,
and a later edit confined to the final partial chunk of a hot texture.

A single startup fault occurred in the guest's embedded inflate routine during
one timing diagnostic. Its retry completed without the fault. The available
registers do not identify the input asset or establish a cause; no speculative
file-I/O workaround was added, and this is not claimed fixed.

Sustained 60 FPS at native 1080p and removal of every area-transition stall
remain separate acceptance criteria. The fog fix is proven in the saved view;
extended movement and retirement bounds passed in the observed windows,
while cold compilation and the pending warmed replay remain explicit limits. Earlier warm
native-1080p performance and Hunter's Dream cubemap verification are recorded
in `MACOS_METAL_GAMEPLAY_2026-10-19.md`.

## Resume after the Mac is awake

1. Rerun only `darwin-metal-detile` with
   `ctest --test-dir out/macos-runtime -R '^darwin-metal-detile$' --output-on-failure`.
2. Extend the ignored replay helper for compute recipes, save fingerprints and
   modification times for the five `added_names` in
   `final-prewarm-replay-comparison.json`, and snapshot the current cache inventory.
3. Run a new 330-second `movement` replay with final defaults, the same copied
   fog save and `BB_TEST_TRAVEL_INTERVAL=60`; keep the game visible. Do not
   enable quality/video/presentation timing diagnostics. Confirm all 517 recipes
   load and become ready before measurement.
4. Check actual foreground use of each new recipe, all pipeline waits >=10 ms,
   complete nonzero presentation-time gaps, GPU errors/timeouts, memory and
   transition success. A zero wait count without recipe-use evidence is
   insufficient to claim the exact cold stalls resolved.

The final verified executable is
`1fb9cddedfa445fff8489cd22ca4f274facded58c5e62b9c849e17f69514cde1`.
Original game data and master saves must remain untouched; continue using
`run-profile.py` save clones and the existing read-only source paths.
