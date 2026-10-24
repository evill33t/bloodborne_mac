# Native Metal AC profiling, 24 October 2026

Target: native 1920×1080, no frame generation; original game dump and master
saves remain read-only. Every launch uses a separate cloned user directory.
Evidence: `out/native-ac-bottleneck-2026-10-24/`, with gameplay files under
`out/metal-unattended-2026-10-20/ac24-*`.

## Bottleneck evidence

The initial clean AC native run measures 58.35 rendered/displayed FPS, p99
25.00 ms, no missing presentation timestamps or GPU failures. It enables the
existing native SIMD hash and constant mapping specialization, and retains both
the wind amplitude and shadow HTILE corrections. TAA and frame generation are
off for this comparison; dynamic shadows and SSAO remain on.

The asynchronous command/stage/work trace uses the same preset. Its first40s
(preceding the CPU sampler) cover2250 frames, with13.14 renderer submissions per
frame and no requested CPU GPU waits. Observations per frame:

- CPU frame wall17.09ms, plus flip0.67ms; these are sequential producer spans.
- Texture change checks6.99ms (nested within CPU frame wall), approximately
  186MiB/frame. The later CPU sample also finds1964/4954 graphics-thread samples
  in XXH3; sampled wall time is not exclusive CPU utilization.
- Frame handoff0.636ms (included within flip).
- Renderer command GPU interval union13.87ms; sampled render/compute stage
  union11.53ms. These elapsed intervals include dependencies and overlap and
  are not exclusive shader utilization. Uploader/helper coverage is incomplete.

The25-second CPU sample substantially perturbs gameplay (including a1.09s
interval). That portion is excluded from the timestamp analysis and all clean
FPS comparisons. Diagnostic FPS is never used as a before/after result.

## Event-driven presentation candidate

Native ordinary-frame presentation now signals a version0 CFRunLoop source
when the producer publishes a frame. Its existing1ms timer remains a settings
and occlusion watchdog/fallback. Presentation still takes a drawable/semaphore,
queues the shared-queue read, and only then acknowledges borrowed storage.
This changes wake latency, not frame ownership or guest fence semantics.

The notifier captures independently owned state, rather than Presenter::Impl.
The worker invalidates/releases its source under the same mutex used for
cross-thread signaling; producer-retained callbacks become no-ops after
shutdown. A notifier installed after publication also wakes that pending frame.

Apple documents the signal-plus-wake protocol and Core Foundation thread safety:
[CFRunLoopSourceSignal](https://developer.apple.com/documentation/corefoundation/cfrunloopsourcesignal(_:)),
[Run loop management](https://developer.apple.com/library/archive/documentation/Cocoa/Conceptual/Multithreading/RunLoopManagement/RunLoopManagement.html).

`BB_METAL_PRESENT_EVENT_WAKE=0` retains polling for a matched control. Native
arm64 defaults to the event wake; x86 retains its previous default. The
fixture-only `BB_METAL_PRESENT_POLL_MS=250` makes the fallback too slow to pass
the450ms UI-stall regression, proving the new notification actually drives
presentation. Explicit display-link and frame-generation policies remain.

Both runtime builds succeed. Native CTest:29 executed,2 expected private-input
skips. All7 presenter variants pass, including VSync/FG transitions and retained
notifications after destruction. x86: all7 presenter variants plus handoff pass.
The pure handoff fixture additionally passes as arm64 and x86 executables.

A stale native CMake cache referenced Zydis in/tmp. The exact pinned Zydis commit
`a2278f1d254e492f6a6b39f6cb5d1f5d515659dc` and its pinned Zycore submodule
`0b2432ced0884fd152b471d97ecf0258ff4d859f` were archived from existing local Git
objects into `out/deps/`; only build-cache paths changed.

## Matched AC comparisons and gameplay validation

The later light trace (no CPU sampler or stage-counter sampling) measures
0.0896ms per handoff versus the earlier 0.636ms, approximately86% less parked
handoff wall time. Texture checks still take6.47ms inside20.64ms CPU frame
wall; the graphics command producer has only0.13ms idle time per frame. This
points to CPU resource/coherence work as a remaining limit. Renderer GPU
interval union is15.30ms/frame, and there are no requested CPU GPU waits.
These are diagnostics, not clean FPS comparisons.

A clean repeated control sequence with the same event-capable binary measured
41.96 and52.37FPS for polling/no exact cache;46.23FPS for polling/384MiB exact
cache;47.93 and52.49FPS for event/384MiB exact cache. This control drift prevents
an isolated gameplay FPS claim for either switch. The full byte checks remain;
no weaker write journal or skipped payload validation was enabled. A wide NEON
comparison prototype improved its isolated median by about1% and was not adopted.

An optional `BB_METAL_RENDER_THREAD_QOS=1` hint affects only graphics-ring0 and
presentation. Both architectures pass its parser/actual-thread-policy fixture.
Default remains off: the original producer already spent99.7% of its recorded
CPU time on performance cores, and repeated QoS gameplay runs varied widely.
One QoS-off run had a missing presentation timestamp and is excluded from clean
comparisons; no timestamp is imputed.

The final clean route uses native1920×1080, frame generation off, TAA on,
dynamic shadows and SSAO on. It enables SIMD hash, constant mapping,384MiB
exact compare, event wake and the optional QoS hint. It includes stationary
viewing, camera orbit, forward/backward movement and attacks:

| Area | Rendered FPS | Displayed FPS | Generated FPS | p99 | Maximum |
| --- | ---: | ---: | ---: | ---: | ---: |
| Hunter's Dream |68.33|68.35|0|25.00ms|41.66ms|
| Saved arch |48.13|48.15|0|25.01ms|91.67ms|

Rendered/displayed figures are paired in-window one-second counters; exact
all-frame callback averages are68.08 and48.05FPS respectively. Both runs have
zero GPU failures or missing presentation timestamps and all recorded shader
recipes ready before measurement. Post-measurement videos begin only after
freezing benchmark rows. Results establish neither a whole-game60FPS lock nor
an isolated improvement over the drifting controls.

The arch's91.67ms display interval begins with a normal19.23ms CPU submission
interval. Presentation queue latency rises from29.8 to102.2ms; CPU backpressure
arrives two frames later. This does not support labeling it a CPU shader-compile
stall. Subsequent instrumented arch replays have not reproduced that interval;
they are image/timeline diagnostics and are excluded from FPS claims.

## Remaining camera-dependent shadow report

The user reports shadows sliding during camera turns. Do not mark this fixed
from the prior HTILE regression alone. New same-queue captures cover the nearby
ground and right wall as well as the arch, with matching-frame cameras and
independent double-precision depth reprojection (device-Z tolerance2e-7).
Across29 adjacent frame pairs, mean raw shadow-alpha drift is0.276,0.623 and
1.344 levels out of255 respectively. These regions contain substantial shadow
contrast; this is not a constant-white-mask test. Tone-compressed HDR drift is
0.294/0.234/0.306 before host TAA and0.114/0.118/0.147 after it. Final scanout
image drift is0.612/0.579/0.814 levels out of255. This bounds those regions;
it does not prove absence of a visible artifact elsewhere.

The live original shadow program binds nativeD32 and executes Metal
`sample_compare`; an unused generic float-sampler variant is not evidence for
a comparison-filtering fix. The producer's RGB encodes depth, while alpha
contains shadow visibility, so large red-channel changes alone are expected.
The classification capture now optionally includes pre/post host TAA, final
scanout and spaced full-view samples under the same independent256MiB cap.
All new capture modes remain off during ordinary gameplay.

The initial full-view capture has4.27million accepted static-depth samples;
mean raw alpha drift0.54/255, while host TAA lowers tone-compressed HDR drift
from0.689 to0.594/255. A later four-angle camera/movement capture has1.60million
accepted samples and raw alpha drift1.292/255. These do not isolate the user's
reported arch/right-wall artifact. HTILE readiness rejections rise substantially
following travel; additional default-off diagnostics now separate the rejection
sites and CPU-read causes without weakening admission.

## Extended stability failure and correction

The first planned600-second route failed after 504.28 seconds, following
three diagnostic fast-travel requests, movement, attacks and fall/respawn cycles.
It is **not** a completed stability pass. Native bbcpu terminated with status30
at guest0x7005733d44 on `8c 8f e8 00 00 00` (MOV word [RDI+0xe8], CS).
The runtime did not support reading segment-register operands. No GPU wait
timeout was reported before termination. Evidence is retained in
`ac24-final-long-shadow/metrics.json` and `launch.log`.

A synthetic test reproduces the same six-byte stop before the change. The
CPU decoder now retains segment-register identity and its interpreter/JIT
fallback reads per-thread16-bit user-context selectors. Restores update that
metadata while retaining the runtime's separately installed FS/GS bases.
The flat context uses FreeBSD-compatible user selectors, inferred as the
appropriate user-mode ABI model, rather than reading ARM host state. It does
not implement GDT loading, descriptor validation or privilege transitions.
The reference layout and initialization are in FreeBSD9.0
[segments.h](https://raw.githubusercontent.com/freebsd/freebsd-src/releng/9.0/sys/amd64/include/segments.h)
and [exec_setregs](https://raw.githubusercontent.com/freebsd/freebsd-src/releng/9.0/sys/amd64/amd64/machdep.c).

After the correction, interpreter and native JIT fallback tests pass the actual
store, six selector reads,16/32/64-bit destinations, narrow memory guards,
flags, thread isolation and explicit TLS-base preservation. The complete native
suite passes32 executed tests, with two expected private-input skips. The
Rosetta suite passes120 executed tests, with four expected skips. The native audit build and the focused admission/CPU checks also pass. The
private original HTILE GPU fixture was supplied explicitly and passed its
blocked-consumer, full-input-lease regression after rebuilding. The full x86
suite preceded the final audit-only instrumentation; it is not a claim about a
new x86 gameplay replay.

## Analysis-only continuation and remaining work

The AC guard rejected the next launch before creating a game process. The user
selected analysis only, so no further gameplay launches were attempted. The
post-fix travel replay is pending. Its frozen native runtime is
`cpu-fixed-audit-runtime/bb-macos-game`, SHA256
`b1627f8d97f1b1797e465f53847fd14a5a275a3718648ad22a8dd92221b574dd`.
The failed long replay used SHA256
`20eb6777f2ce0b61a4d3ee39d5474f2bd339d368e14828e2892a79e08f981f00`.

The retained long replay ends with 16,368 native clears, 33,819 attempts,
3,017 surface rejections and 14,434 readiness rejections. During its final four
observations, attempts grow by 205 and readiness rejections grow by 205, with
no new native clears. In that fallback, the original metadata RMW dispatch
continues but the native depth-clear substitution does not run. The original
GPU regression establishes that the metadata kernel alone leaves private D32
contents stale. This is a concrete coverage gap in the correction; which
readiness predicate is responsible in gameplay is still unmeasured.

`BB_METAL_SHADOW_HTILE_AUDIT=1` reports default-off counters. The readiness site
indices are modes, missing CP context, stale layout generation, uniform CPU
read, metadata CPU read, allocation, buffer lookup, recognition proof, final
map-generation check, consumer lease and apply-time identity. Uniform/metadata
read counters additionally distinguish unreadable inputs, pending CPU fences,
partial direct mappings, host backing views, unknown GPU writers, pending GPU
writers and callback effects. No check was bypassed.

The next AC replay should target the user's arch/right-wall camera motion after
travel and correlate these counters with atlas and shadow-alpha captures. If
pending same-queue GPU writes block the CPU proof, a candidate is a GPU decision
using the actual protected inputs, followed by a conditional native depth clear.
It must retain the original RMW, the nibble-13 preservation case, uniform
validation, mapping identity and complete consumer leases. A stale binding
instead needs a fresh association proof; updating a generation tag alone would
not establish it. These are analysis proposals, not implemented or validated
shadow fixes.

The final read-only integrity check passes: all 28,779 source files have unchanged
size/mtime inventory, the source eboot and prepared original ELF hashes match,
and all 16 master-save hashes match. `git diff --check` passes. The sliding-shadow
case, post-fix long gameplay stability and a repeatable native 60 FPS lock remain
unresolved.
