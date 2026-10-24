# Native Metal resource and shadow work, 24 October 2026

Target: native 1920x1080, temporal AA, dynamic shadows and SSAO enabled,
frame generation disabled. Original game/DLC files and master saves are read
only. Gameplay runs use separate cloned user directories. Evidence is under
`out/metal-resource-2026-10-24/`; gameplay artifacts are in
`out/metal-unattended-2026-10-20/resource24-*`.

## Original-binary investigation

The installed REA 6.7.0 CLI and existing Hopper provider were used. REA MCP
tools were absent in this agent session; the read-only doctor found Codex
registration aligned and Hopper available. No REA configuration, analysis
engine or game code was installed or changed.

Four queries analyze complete original unwind procedure payloads extracted
from the prepared original ELF, SHA256
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.
The extraction validates original executable PT_LOAD mappings, unwind table
entries and complete byte equality, before and after preparation. Its two
small generic ELF artifacts preserve original RVAs but omit external code,
imports and globals. They are analysis-only artifacts, never game executables.
Exact source file offsets, new file offsets and payload hashes are retained
in `extraction.json` and the adjacent manifests.

| Original RVA | REA Evidence ID | Returned assembly entries |
| --- | --- | ---: |
| `0x1071b70` | `ev_49016ea988e9e70f8e8aded7531a7e39232a132c328ed1d827a46b1eb67e7dde` |345|
| `0x10739b0` | `ev_76a70042107818ef0861b5e73770b3fc58d91c1b4267c2f6a5ab35a900292de5` |86|
| `0x2169ec0` | `ev_76b964bc0fc1a0789dfc1dfdf0cb187e2e08d88c1dc5b2a50f0c2df996b17683` |98|
| `0x1077660` | `ev_a3d9509ac5825561e5e8abfa75decf84b567c0b174a00d258394139da75c5025` |103|

Observed in original assembly: the surface constructor writes descriptor
fields and calls the external allocation helpers; the depth-binding writer
emits separate depth and metadata-related register packets. The engine
synchronization wrapper selects different packet writers by object state;
the included writer has an explicit memory wait sequence. These are static
contracts, not execution counts or evidence that texture data is immutable.
Hopper pseudocode types and array indices are inferred; assembly governs byte
offsets. Hopper does not expose complete function-body ranges through its
public API; extraction completeness is independently checked by the original
unwind payload manifests. External callees remain unresolved here.

Consequently the changes retain complete texture content checks and all
guest submit, fence, callback and retirement semantics. No whole-engine
texture lifetime hook or broad PM4-submit coalescing is claimed.

## Implemented candidates

- `BB_METAL_TEXTURE_JOURNAL_PAGE_INDEX=1`: persistent physical-page write
  history avoids bounded-journal overflow becoming unrelated invalidations.
  New 64-page summaries reduce large range-query cost while retaining exact
  partial edges and separate deferred CPU-effect history. Every first use in
  a frame compares the complete texture payload, including cold textures;
  raw guest CPU stores are not falsely treated as tracked. Existing mapping,
  source-readiness, callback and immutable queued-version guards remain.
- `BB_METAL_TEXTURE_JOURNAL_PAGE_INDEX=2`: bounded page/block summaries speed
  queries only while the original journal history is complete. Once history
  has been dropped, the original unknown-history result and full-validation
  policy remain unchanged. This alternative avoids mode1's extra first-use
  checks on cold textures. It does not assume that raw CPU writes are tracked.
- `BB_METAL_BINDING_CACHE=2`: exact copied-byte keys suppress repeated inline
  stage data and draw constants inside one retained render encoder. All
  encoder boundaries invalidate keys. Modes0/1 retain previous behavior.
  See `MACOS_METAL_INLINE_BINDINGS_2026-10-24.md` for the isolated API benchmark.
- `BB_METAL_SHADOW_HTILE_GPU=1`: when a known pending same-queue GPU writer
  prevents a CPU input read, a GPU decision inspects the original protected
  uniform and all1MiB metadata before the original RMW. A subsequent indirect
  depth-only draw clears native D32 only if count/value match and no word has
  preserved nibble13. The original metadata program always executes. Unknown
  GPU writers, conflicting callbacks, CP writes and mapping changes reject.
- `BB_METAL_SHADOW_HTILE_REBIND=1`: a stale global mapping generation requires
  a fresh association proof, comparing complete metadata1MiB and depth64MiB
  physical spans, runtime VMA lifetime identities, target/texture identity
  and current ownership/layout. Actual consuming-command leases protect
  those complete spans. Metadata/depth remaps and same-physical ABA reuse
  reject; merely rewriting a generation number is insufficient.

The texture and binding candidates remain optional. Both shadow corrections
now default on; setting either corresponding variable to0 restores its old
path. The final-default gameplay validation is recorded below.
Conditional depth draws add GPU work; they are a correctness correction, not
an assumed FPS optimization.

## Reproduction and validation so far

The baseline270-second AC movement replay made three travel requests and
stopped normally with zero GPU failures. Audit counters isolate both gaps:
12,076 native clears/23,179 attempts;3,057 surface rejects;8,046 readiness
rejects comprising5,706 stale associations and2,340 known pending metadata
writers. One presentation timestamp was missing. This diagnostic replay,
which overlapped analysis/fixture work, is excluded from FPS comparisons.

The initial indexed/binding/GPU-fallback diagnostic completed60 seconds with
zero GPU failures or missing presentation timestamps. Fourteen GPU decisions
completed positive native clears. Its CPU texture-check span was7.83ms
within19.08ms producer frame wall. These nested times and the different
diagnostic load do not establish an isolated improvement over older traces.

The combined native build succeeds. CTest has38 executed passes and2 expected
private-input skips. The original HTILE GPU and CPU-lease regressions were
then supplied their private capture explicitly and pass. Native and Rosetta
fixtures cover raw cold texture tail edits, dropped journal history,
same-frame writes, physical aliases, remaps and queued immutable versions.
Independent journal fuzzing and native ASan/UBSan pass. Binding pixels match
with modes0/1/2, including changing user data, mappings, viewport and encoders.
Shadow fixtures execute the original88-byte shader and verify full RMW words,
pending GPU inputs, invalid uniform/count, final-word13, complete input leases,
unrelated mapping recovery, changed depth/metadata backing and ABA rejection.

Independent review added rejection of uniform/metadata input aliases, including
physical aliases. Conditional clears also reject a newer color owner: a GPU
decision that selects zero vertices must not promote stale depth ownership over
a newer color target. These are conservative correctness guards, not proof that
either alias occurred in the live scene.

The final combined native CTest run executes40 checks successfully and skips
the two tests requiring private shader input. Supplied explicitly afterward,
the original HTILE GPU/CPU-lease cases and864 analytic original shadow-projection
cases pass. This run includes the bounded journal and the module API below.

## Interrupted soak and unwind fix

The first planned600-second post-fix movement replay stopped at579.3307 seconds
on the unimplemented `sceKernelGetModuleInfoFromAddr` import. It is a failed
stability interval, not a completed600-second pass. Four travel requests had
occurred. Up to the stop,3,550 GPU conditional clears and34,184 CPU clears were
recorded, with5 binding revalidations, no readiness rejections and no GPU failure
reported.

The queried address belongs to the original libc `_Unwind_RaiseException`.
Fresh REA/Hopper analysis of its extracted original procedure has Evidence ID
`ev_eb1016e19d8d93ea09d72e5bfe4ac591337be0786f35842f8472991f4f6fac63`
(156 returned assembly entries). The extracted ELF preserves the procedure's
original RVA and bytes. External code and data are omitted, so this static
result does not determine the cause or outcome of the throw.

The runtime now returns real validated loaded-module EH information, with
BBPROBE6 metadata derived from original ELF headers. It does not suppress
exceptions or fake successful unwinding. See
[module metadata and its validation](MACOS_MODULE_UNWIND_2026-10-24.md).
An earlier comparison attempt stopped before gameplay because an old frozen
runtime received newly generated BBPROBE6 data. That is a harness version
mismatch, separate from the in-game579-second failure. Subsequent comparisons
freeze the runtime and matching preparation tools together.

## Initial matched performance observation

One clean AC native1080p pair with both shadow fixes enabled measured40.6668
rendered/displayed FPS for journal0/binding0 and31.1359 for journal1/binding2.
No presentation timestamps were missing and no GPU errors were recorded in
either run. This pair is a regression; mode1 stays optional. The reverse pair
was interrupted by the preparation mismatch above, so this does not isolate
which component caused the difference or establish its repeatability.

The later complete frozen-BBPROBE6 ABBA comparison keeps both shadow fixes
enabled in every case. All four are clean60-second AC runs from the same cloned
arch save, after45-second warmup, at native1080p with TAA/shadows/SSAO on and
frame generation off:

| Order | Journal / binding mode | Rendered FPS | Displayed FPS | p99 ms | Maximum ms |
| --- | --- | ---: | ---: | ---: | ---: |
| A1 |0 /0|53.4663|53.4663|25.0044|33.3333|
| B1 |2 /2|43.3404|43.3404|33.3335|75.0001|
| B2 |2 /2|54.3300|54.3469|25.0042|41.6667|
| A2 |0 /0|45.6912|45.7081|25.0060|33.3370|

No presentation timestamps are missing and no GPU failures are recorded. The
small rendered/displayed difference is the paired counter-window boundary;
generated FPS is zero. Means are49.5788 control versus48.8352 candidate
(-1.50%), with much larger within-condition variation. This does not establish
a repeatable speedup or isolate the cause of variation. Both journal variants
and inline binding suppression remain disabled by default.

Independent normalized counter review finds no evidence that coarse page hits
caused extra full texture checks in these runs: full-check deltas track
forgotten-history deltas. The index retains all deque updates while adding
page/block maintenance over5.26MB metadata versus roughly16KB retained records.
This is a hypothesis about overhead, not a measured causal attribution.
The detailed analysis and proposed block-only negative filter are retained in
`out/metal-resource-2026-10-24/texture-index-live-review.md`.

The full-recompiler reference was also reviewed at its current public revision.
Our CPU integration already includes its translator; no newer runtime CPU
implementation exists in the three upstream commits since our pin. The
[assessment](MACOS_RECOMP_REVIEW_2026-10-24.md) separates useful mapping/replay
tools from unproven whole-game AOT performance claims.

## Image validation and stability continuation

The dedicated arch camera replay completes60seconds on AC at native1080p,
TAA/shadows/SSAO enabled and frame generation off, without GPU failures or
missing presentation timestamps. Its raw capture contains20 phase records over
four frames spaced30frames apart; every record's camera matches its frame.
Across4,448,219 accepted static-depth samples, camera-registered mean absolute
drift is0.514/255 for raw shadow alpha,0.533 before host TAA,0.425 after TAA,
and2.603 in final scanout. Final postprocessing/particles/HUD are not all world
registered. Right-side raw-shadow tiles reach1.44/255; the highest3.88/255 tile
is near the character's lower image region. These are bounded observations,
not proof that every camera-dependent artifact is eliminated or an isolated
before/after improvement. Capture/video timings are excluded from clean FPS.

The Hunter's Dream replay completes35seconds with no GPU failure or missing
presentation timestamp. Its current-schema WINDOBS5 parser validates all240
records: all sampled fixed original vertex stages are uniform within each draw;
no sampled W140 variation or sampled wind/view/camera null read is observed.
Two descriptor groups are covered. The first older W140-only parser correctly
rejected this newer format; it is not evidence of a rendering failure.
Videos include a stationary opening and a camera-turn interval. Recording,
shader diagnostics and a concurrent build exclude timing/FPS conclusions.
Sparse fixed-vertex sampling does not prove every vertex or animation speed;
typed matrix mapping-null status remains unobserved.

The later600-second V6 movement attempt fails after546.149seconds to its last
submission, with31,037 submitted frames after the start and no missing
presentation timestamp. All four travel requests occur. The module query
succeeds (`queries=1`, unknown/unavailable=0), then the previously unimplemented
POSIX `msync` import stops the process. It records6,540 positive conditional
clears,4 binding revalidations, no readiness rejection and no reported GPU
failure. This is another failed stability interval, not a completed soak.

The runtime now implements actual host memory synchronization with guest16KiB
rounding, FreeBSD flag/errno translation, reservation/hole checks and real
error propagation. Native/Rosetta syscall fixtures and20 loader tests per
architecture pass, including an actual-NID4KiB-aligned probe. See the
[msync report](MACOS_MSYNC_2026-10-24.md).

The combined default build succeeds. The43-test native suite initially has a
display-update timing assertion in the presentation test and a display-link
skip; both pass in isolation afterward. This yields41 distinct passing native
CTest paths and2 private-input skips. The supplied original HTILE GPU,
GPU-off and lease regressions also pass with the combined default build.

The 720-second movement/travel attempt with binary
`2cc5e1ae0bfc693af3f31de14102729782b158aacc15e7eadacead72dd64c511`
leaves both shadow override variables unset, uses the new defaults, and keeps
journal/binding experiments off. It stops at640.345seconds to last submission,
after35,455 submitted frames, four travel requests and four unpresented frames
at the stop. The guest requests `exit(1)` before a finalizer reports
"Mutex is not initialized." The fault stack is nested under `runtime_finalize`,
so that cleanup panic does not establish the initial reason for exit.
No720-second stability pass is claimed. The last audit records4,579 positive
GPU clears,6 binding revalidations and zero readiness rejections; no Metal
failure is reported before the CPU failure.

REA analyzes the complete original511-byte panic procedure at0x20b55b0, with
Evidence ID
`ev_9ee24e81f0f571b098b5b96c2254cab8041cda0e39dd9324f584fa2320fe2a40`
and123 assembly entries. At0x20b56d2 it deliberately stores0xdeadba to address0.
This identifies the final null fault as the game's panic action, not evidence
of a GPU fault. The initial exit caller remains unknown in that run.

Optional `BB_RUNTIME_EXIT_AUDIT=1` now records original guest RIP/import return,
thread name and module/msync coverage before callbacks, then bounded finalizer
identities. It changes no exit status, order or destructor selection. Both
architecture probes pass22 loader tests including exact actual-NID exit caller,
LIFO/single callback execution and audit-off behavior. See the
[diagnostic note](MACOS_EXIT_AUDIT_2026-10-24.md). The separate audit replay with
binary `01a8e6a34842c0df26ddc7c0e1530cb3720c3c9bf901f31ea341f8b085eda0af`
stopped at 572.23 measured presentation seconds on the next unsupported
import, `_is_signal_return`, before reaching any exit/finalizer audit. Its
argument was the translator's outer return sentinel. The runtime now implements
classification against its empty guest signal-trampoline set, preserving errno
and leaving address validation and unwind behaviour unchanged. Native and
Rosetta loader tests and private original-consumer cases pass; see the
[signal-return report](MACOS_SIGNAL_RETURN_2026-10-24.md). This fixes the missing
API, not the initiating exception. Optional observation of original
`__cxa_throw` is being added to identify that exception during the next replay.
No native60FPS lock is established by the completed matched comparisons above.

The user subsequently moved the sources to the internal SSD. Read-only
preflight and separate preparation pass, and the complete relative-name,
size/mtime inventory matches the previous sources. The new gameplay cohort
keeps the previous evidence intact; see [SSD validation](MACOS_SSD_TESTS_2026-10-24.md).

The original-file inventory/hash verification passes after these changes:
28,779 game files and35 DLC files retain sizes/mtimes, the original eboot and
prepared-original ELF retain their expected SHA256 values, and all16 master
save hashes match the starting snapshot. Test writes use derived outputs and
cloned saves. Evidence is `integrity-final.json` alongside the retained start
snapshot. This is not a full-content hash of every game/DLC file.
