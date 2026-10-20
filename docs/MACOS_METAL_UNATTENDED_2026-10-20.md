# Metal optimization follow-up, 2026-10-20

Continues `MACOS_METAL_INDIRECT_STREAMING_2026-10-20.md` during the authorized
unattended test period. Target: native 1920×1080, TAA and native motion vectors,
direct Metal. Original game data and master saves are excluded from test
writes; each run clones the same save source. Work is in progress until the
final validation section is populated.

Evidence directory: `out/metal-unattended-2026-10-20/`.
Frozen baseline executable:
`63f5b5a289fe4e17d4380f1ecdc4ff02d9a8fda0637d63edd322b90932148e52`.

## Questions and current evidence

| Question | Evidence and current limit |
| --- | --- |
| Why did required pipelines stall for 285/184 ms? | Retained recipe/source comparison identifies new typed fragment variants; all 536 older recipes were already ready. Existing exact-source function caching shares duplicate stage sources. |
| Does persistence remove those two waits on replay? | The frozen baseline's second 330-second replay loads all 539 recipes and records no `pipeline_wait` at the 10 ms logging threshold. Other display stalls remain. |
| Can the remaining compute writer have a bounded footprint? | The 26ec shader now passes the bounded vector/EXEC proof, public GPU admission fixtures and the live Dream capture with zero unknown writer tickets. A separate worklist copy shader still conservatively covers about 2 GB; its actual job extents are under investigation. |
| Are guest-pool inputs overdeclared writable? | Metal reflection reports read/write for a translated read-only kernel's mutable `gm0/gm1`, but read-only for a const input. This metadata does not establish scheduling cost or FPS gain. |
| Is inherited validation changing performance? | Explicit 1/0 probes both return `AGXG16CDevice`; wrapper activation is not established. Matched AC runs measure 29.836/28.664 FPS respectively, so disabling the environment flag has no demonstrated benefit. |
| Do original-file checks still pass after the new runs? | Final checks are pending; the baseline inventory/hash evidence is retained. |

## Warm-cache baseline

`warm-baseline-stress` uses the frozen executable and all four earlier
experimental options, repeating 330 seconds of movement, camera rotation,
attacks and four area transitions. It starts and ends on AC, at 85–89% charge.
All four requested transitions complete. The process remains alive until the
planned SIGTERM, with zero GPU failures.

It presents 15,255 of 15,256 submissions, p99 33.333 ms and maximum display gap
191.667 ms. The aggregate 46.125 FPS combines multiple scenes and loading and
has one missing presentation timestamp; it is not a clean stationary FPS
comparison. The previous cold replay also changed power source. Do not treat
the difference between their aggregate FPS values as a source-code speedup.

No `pipeline_wait` reaches the trace's 10 ms threshold during measurement.
This supports the existing recipe persistence for the two previously new
variants; it does not imply zero compilation cost for every future variant.
The largest remaining CPU draw-record span is 90.014 ms at replay time
310.2 seconds. Source recipe evidence is retained under the earlier
`pipeline-stall-audit/` directory, with immutable recipe copies and exact diffs.

## Bounded compute offsets

The new code-only proof bounds complete uint byte offsets, retaining inactive
VGPR values across EXEC changes. It uses exact dispatch threadgroup dimensions,
independent descriptor reaching-definition proof and the existing descriptor,
CPU-write lifetime, physical alias and mapped-footprint guards. Unsupported
control/definitions, overflow, cycles or hard limits retain fallback. Each
program caches at most eight validated threadgroup configurations.

The captured `26ec0808a6cd92dc` shader has 2,045 decoded instructions and 25
buffer writers. With 64×1×1 threads, the first four atomic byte offsets are
bounded by 1008/1012/1016/1020. The production private-input fixture verifies
the complete program hash and passes with 15 queries and 10,808 node visits.
Retail bytes remain outside source fixtures.

The first GPU fixture also exposed an existing cache issue: a raw GPU write
changes guest memory to 23, but a later same-frame draw can reuse the earlier
black uploaded texture. That is separate from the correctly rejected native
admission and must not be hidden by rewriting the fixture's guest pixels.
Cross-frame dimension testing and a separate same-frame coherence regression
now pass. A bounded physical-write journal preserves invalidation across frame
boundaries and prevents both same-frame reuse shortcuts from hiding relevant
GPU or queued CPU fence writes. Mapping changes, forgotten history and unknown
writes force conservative revalidation. The pending-source drain has a bounded
timeout and waits for queued CPU callbacks as well as GPU completion.

The GPU fixture verifies the actual pixel after the write, including physical
aliases, queued EOP fences, host DMA, changes outside the first hash chunk,
history eviction and remapping to an already observed pending fence. The
journal is enabled with the experimental native indirect path; that path
remains off by default. This covers uploaded texture entries, not every
render-target or mip-alias coherence question.

## Exact worklist extents

The final tested refinement covers only the complete 15-instruction
`7cf42796921ac73e` copy kernel. It independently matches every decoded field,
the full shader hash, compute ABI and known paired resource layouts. Only a
direct 64×1×1 dispatch with 1–2,048 X groups and one Y/Z group qualifies. Each
group loads a complete 12-byte `{destination, source, inclusive last lane}`
job, bounding the table to 24 KiB. The whole table must have stable CPU/CP visibility and contiguous GPU
backing; a physical read lease protects it on the actual producer command.

The bound includes lane zero for a zero limit, clips indices using the real
output descriptor count and rejects uint addition wrap before clipping. It
merges adjacent output intervals and rejects worklist self-modification,
unmapped outputs and lease-capacity overflow. Even an empty output set retains
the input lease. Indirect, oversized, pending or unknown worklists preserve
the existing conservative writer; mutable values are never cached by shader.
The first 512-job checkpoint missed observed 622/1,024/1,112-job batches and
one known descriptor layout pair. The final subset covers these captures;
the global 64-physical-range and 512-read-ticket limits are unchanged.

CPU and real GPU fixtures pass for affected rejection, unrelated admission,
physical aliases, list overwrite/unmap, empty outputs, prior GPU/CPU writers,
missing records and wrapping indices. The retained original shader passes
the same production semantic matcher. This proves the bounded subset, not
that the live game's lists will always be ready or that FPS improves.
The 1,112-job GPU case verifies the final job's real output at index 74,840,
as well as affected rejection, unrelated admission and the centre texture
pixel. A 2,049-job list and wrap in the final 2,048th record are rejected.

## Pipeline diagnostics

Optional stutter tracing now separates queue delay, vertex/fragment/compute
function acquisition, source-library compilation and pipeline creation. It
captures requesting frame, pipeline key, shader address and exact-source hash
prefix without reading mutable renderer state from compiler workers. Cache
policy, draw ordering and scheduling are unchanged.

The existing cache fixture additionally counts actual source-library creation
for 513 descriptor variants and reload. It proves existing exact-source
sharing, rather than assuming repeated recipe references compile repeatedly.

## Resource access experiment

The standalone reflection probe reports `MTLArgumentAccessReadWrite` for a
mutable input that is only loaded, including through the production `DcMem`
helpers, and `MTLArgumentAccessReadOnly` for a const input. Writable controls
remain read/write. This motivates an opt-in const guest-memory specialization
for fully supported, writer-free ordinary VS/PS programs. It remains off by
default. CPU qualification and public GPU differential/reflection fixtures
pass, including 5,376 raw-format/address/map cases. Two captured hot fragment
shaders also pass six-render-target differential tests over 16 states each,
with ordered GPU-written payloads and descriptor-base replacement. The default
prelude remains byte-identical. The first clean 90-second AC comparison
measures 29.861 FPS for the control and 29.265 FPS with specialization; this
does not demonstrate a throughput gain. The current live scope qualifies
fragment bindings but no vertex bindings in checkpoint `24ac6aa8`.

Checkpoint `7a7c1655` additionally validates the exact inline fetch segment and
its call/resume structure, independently checking every main/fetch opcode.
Native fetch requires its existing exact proof as well. Its const helper
changes only two pointer qualifiers; the writable helper remains byte-identical
(2,909 bytes, SHA256
`47738e22acc89c215547d077a00ae47cb8401d2350538859ab359d48d25fc922`).
Public GPU raw cases expand to 7,168. Both inline and native fetch modes pass
complete-output differentials for all six retained original vertex shaders,
including queued record, descriptor-base and descriptor-format producers and
actual read/write versus read-only Metal argument reflection. Rectangles,
points, tessellation, probes and other experimental SMRD contexts remain
outside this experiment. Live admission and throughput still need measuring.

Apple documents automatic synchronization of tracked read/write resource
conflicts. Reflection by itself does not prove false serialization or a gain
from changing declarations. References:
[resource synchronization](https://developer.apple.com/documentation/metal/resource-synchronization),
[API validation](https://developer.apple.com/documentation/xcode/validating-your-apps-metal-api-usage).

REA's native provider reports retained Hopper cleanup failure for the earlier
analysis session. Closing the owned session also returns `cleanup_incomplete`;
no new native REA result is claimed. Shader conclusions above use the retained
original captures and the port's GCN decoder/source, with source review.

## Final validation

Checkpoint `24ac6aa8769ac17dbf14b3e05649c91b70b16eb92ee424c6bc409b97ea573110`
builds successfully. Its complete CTest run passes 82 tests, with two expected
private-input skips and zero failures. Separate private captured-shader tests
pass for the offset proof and both read-only fragment material variants.

The expanded checkpoint
`7a7c165598b04f551db9a467e629b2ecb9d300188eb9cf747f1ba3c78fc92414`
also builds successfully. All nine focused tests pass; the complete suite
passes 84 tests with two expected private-input skips and zero failures in
111.94 seconds. Six separate private-input fixture modes pass, covering 7cf,
26ec, six original vertex programs and both six-MRT fragment programs. The
executable is frozen under `refined-runtime/` for subsequent gameplay runs.

Final expanded executable:
`f37f9919f7f81b05e78abff799e0d87a932a889f88704a07ec515598c35503fd`.
Its changed worklist CPU/GPU fixtures and private 7cf matcher pass. The full
86-test suite passes 84 tests with the same two expected private-input skips
and zero failures in 109.33 seconds. Read-only shader source is unchanged
from the independently reviewed and privately tested `7a7c1655` checkpoint.

Final matched native-1080p runs, extended movement/transition testing,
original-file integrity and default-policy decisions are still in progress.

### First guarded checkpoint comparison

Four sequential 90-second AC measurements use the frozen checkpoint, identical
cloned Dream save/pose, native 1920×1080, TAA and native motion vectors. Captures,
GPU cuts, frame-work/presentation diagnostics and optional indirect traces are
disabled. Streaming batching, mapped snapshots and the world-frame limiter are
off. `MTL_DEBUG_LAYER=0` is the same for every row. All four measurements have
zero missing presentation timestamps and zero GPU failures.

| Native indirect | Read-only guest inputs | Presented FPS | p99 interval | Largest gap |
| --- | --- | --- | --- | --- |
| Off | Off | 29.861 | 41.667 ms | 58.333 ms |
| Off | On | 29.265 | 41.667 ms | 50.000 ms |
| On | Off | 9.912 | 125.000 ms | 133.334 ms |
| On | On | 9.973 | 125.000 ms | 133.334 ms |

The native rows include the required texture coherence journal. This is a
clear regression and is not a candidate for default enablement. The next
investigation targets the real `7cf42796921ac73e` worklist copy shader: its
descriptor-wide conservative writer covers about 2 GB and invalidates many
unrelated uploaded textures. Only stable, bounded, exact-context worklists
can justify a smaller footprint; unknown or mutable inputs retain fallback.
