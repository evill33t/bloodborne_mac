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
