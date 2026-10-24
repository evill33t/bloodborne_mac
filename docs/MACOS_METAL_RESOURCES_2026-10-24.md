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

All candidates initially remain optional pending matched gameplay assessment.
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

Long post-fix travel, live shadow/foliage quality and matched clean FPS results
are still pending. No native60FPS lock is established by the current results.
