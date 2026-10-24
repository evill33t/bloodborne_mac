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
