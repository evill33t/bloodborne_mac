# Bloodborne macOS: presentation stutter investigation

2026-10-19. Follow-up to the [gameplay validation](MACOS_GPU_GAMEPLAY_VALIDATION.md).
The fix moves presentation off the Mac UI thread and uses Metal display-link
drawables when V-sync is enabled. All changes are in the port; game executables
and original saves are unchanged.

## What the 383 ms frame established

The previous native-motion run's 383.333 ms interval occurred at presentation
sequence 7139, 57.017 seconds into the measurement. Its submission-to-display
delay was only 29.050 ms, while GPU utilization dropped. This points to a gap
before submission rather than 383 ms of GPU execution. That run has no CPU
stack trace, so the exact historical instruction responsible is not established.

New CPU timings covered UI events/status, frame handoff, drawable acquisition,
presentation semaphore waits, texture collection/upload, shader decoding,
pipeline waits and draw/dispatch recording. A replay captured a 183.931 ms
`nextDrawable` call, with the GPU producer waiting for the UI to acknowledge its
frame. An earlier pre-measurement acquisition lasted 1,117.621 ms. The 184 ms
event preceded the sampling window. Sampling the Rosetta process subsequently
introduced its own pauses; the sampled run is excluded from FPS comparisons.

A separate visible-window replay without a sampler caught 43.531 ms in UI event
handling and a 58.333 ms displayed interval. The original architecture requires
that UI thread to queue each presentation before releasing the producer. UI
delays and drawable acquisition can therefore stop subsequent game submission.
No texture-collection scope reached the 10 ms trace threshold in these replays;
the evidence did not justify changing texture eviction.

Apple documents that [`nextDrawable` can wait for an available drawable](https://developer.apple.com/documentation/quartzcore/cametallayer/1478172-nextdrawable?language=objc).
The replacement follows Apple's [dedicated display-link render-loop pattern](https://github.com/apple/game-porting-toolkit/blob/main/game-porting-skills/skills/presenting-metal-drawables/references/render-loop-detail.md):
use the drawable supplied to the display-link callback, and keep the V-sync-off
acquisition loop separate.

## Implemented behavior

`Presenter` owns a presentation thread and its run loop. With V-sync on,
`CAMetalDisplayLink` supplies the drawable; this path never calls `nextDrawable`.
The callback tries the in-flight semaphore without blocking and retains the
pending frame for a later callback if the GPU slots are occupied. With V-sync
off, the existing drawable acquisition runs on the presentation worker.

Presentation reads are committed on the renderer's existing Metal command
queue before the frame handoff is acknowledged. This preserves ordering against
later game rendering and reuses the existing producer-owned storage. There is
no extra frame copy or per-frame GPU drain. The callbacks passed to
`StartFrameLoop` must outlive the presenter; shutdown joins the worker before
draining its GPU submissions.

Resize, refresh rate, V-sync, scaling and sharpening requests cross a mutex and
are applied on that worker. Only one pacing path runs at a time. Display changes
update the requested refresh rate. If macOS supplies no display-link updates
for 100 ms, a pending frame can be skipped to keep the producer moving. This
does not publish a guest GPU fence or manufacture GPU completion. Such periods
are counted and invalidate a whole-window FPS comparison.

`BB_METAL_PRESENT_THREAD=0` restores UI-thread presentation for diagnosis.
`BB_METAL_DISPLAY_LINK=0` retains the worker but uses legacy drawable acquisition.
The latter and V-sync-off can still block that worker in `nextDrawable`.

`BB_METAL_STUTTER_TRACE=/absolute/output.csv` enables the CPU timing scopes.
Most record durations of at least 10 ms; whole UI iterations and frame handoffs
use 50 ms. Times share Metal's presentation clock epoch. Earlier investigation
CSVs used a different steady-clock epoch and must be correlated by frame IDs,
not directly compared to presentation timestamps. Tracing is disabled by
default, does not read pixels or force GPU completion, and refuses output paths
inside either configured original-game directory.

## Tests and gameplay results

The real SDL/Metal window fixture deliberately blocks the main thread for
450 ms. With the new loop, 54 additional frames were displayed and the producer
advanced 54 frames. The legacy control submitted no new presentation commands
and advanced no producer frames during the same pause. Its four delayed display
callbacks belonged to already queued work. The fixture also exercises V-sync
changes, resize/refresh requests and shutdown. This directly proves isolation
from UI stalls; it does not reconstruct the historic 383 ms stack.

Gameplay used the same Central Yharnam save, settings and scripted actions on
the M4 Max/120 Hz machine described in the earlier report: 1920×1080, cap 120,
V-sync and MetalFX Spatial on, native-motion temporal AA enabled. Each run
cloned the original save directory, detected scene rendering and warmed up for
15 seconds. The 90-second measurement consisted of 20 seconds stationary,
20 seconds camera rotation and 50 seconds repeated attacks. No builds, other
game instances, pixel captures or samplers ran during the compared measurements.

| Visible-window run | Confirmed / submitted frames | FPS | Median / p95 / p99 ms | Maximum ms | Intervals >50.01 ms |
| --- | ---: | ---: | --- | ---: | ---: |
| Legacy presentation, `window-baseline` | 3,346 / 3,348 | 37.07 | 25.0 / 33.3 / 33.3 | 58.333 | 2 |
| Dedicated display link, `fixed-visible` | 3,474 / 3,474 | 38.52 | 25.0 / 33.3 / 33.3 | 50.000 | 0 |

The final run measured 90.192 seconds, with zero missing presentation timestamps
and zero unavailable-drawable skips. No instrumented CPU scope reached its
threshold during that measured window. The longest submission gap was 50.069 ms,
versus 117.904 ms in the visible legacy replay. Median submission-to-display
delay was 66.516 ms versus 65.786 ms; these are queue measurements, not end-to-end
input-latency measurements.

Both compared runs had zero logged GPU failures and fence timeouts. The final
native telemetry admitted 3,967 of 3,970 resolved frame inputs; other inputs
used the existing fallback. Process RSS ranged from 3.56 to 3.79 GiB and ended
at 3.75 GiB. Both processes were deliberately stopped after measurement with
SIGTERM (recorded status -15).

The 383 ms event did not recur in either visible 90-second replay. The final
result supports the presentation fix and shows no observed performance
regression in this route. One pair does not establish a general FPS improvement
or elimination of all stutter. Other areas, streaming conditions, cold shaders,
long sessions and input latency remain unmeasured.

An initial changed-build run lost drawable availability when occluded and is
excluded from the comparison. Other runs with zero presentation timestamps
are also excluded. Keep the game visible and inspect both positive timestamps
and unavailable-drawable counters when repeating measurements.

The final runtime build passed 52 CTest entries; the 53rd, requiring the original
ELF, skipped by default and passed separately when supplied that ELF. The new
presentation fixture passed with real displayed frames. Original fence-hook
checks also passed separately. The tested runtime SHA-256 is
`92570bb16be6cd67699ea6615e546cba23d282cb9f64bbeae30a5fb67945e2b0`.

## Original-file integrity and artifacts

After all runs, all 28,779 original source-file size/mtime records matched the
fresh pre-investigation inventory. Original `eboot.bin` SHA-256 remained
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`;
the prepared original ELF remained
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.
All 16 original save-file hashes matched. Other source files were checked by
size/mtime inventory, not a complete content-hash inventory.

Local evidence is under `out/stutter-2026-10-19/`: `window-baseline/` and
`fixed-visible/` contain frame CSVs, metrics, CPU traces and launch logs;
`fixed-visible/trace-analysis.json` checks visibility, trace thresholds, RSS
and the runtime hash. `presenter-direct.log`, `presenter-control.log`,
`final-validation.log`, `native-hook-validation.log`, `fence-validation.log`
and `original-integrity.json` record the other checks. `run-profile.py`
contains the route, isolated-save setup and presentation checks.
