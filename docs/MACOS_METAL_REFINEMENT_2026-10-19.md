# Metal upload, shader and fog refinement — 2026-10-19

This follow-up retains native 1920×1080 rendering and the direct Metal backend.
All game launches use copied save directories and externally generated runtime
images. No patch is written into the original dump or master save directory.

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
execute. The override `BBHOST_TRANSIENT_GPU_MB=160` preserves the control path;
the larger arena reserves an additional 160 MiB.

In the matched 320 MiB capture, all 1,762 completed allocations succeed, with
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

## Validation boundaries

The canonical build passed. The expanded CTest run registered 66 tests: 64
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
extended movement and retirement bounds are validated separately. Earlier warm
native-1080p performance and Hunter's Dream cubemap verification are recorded
in `MACOS_METAL_GAMEPLAY_2026-10-19.md`.
