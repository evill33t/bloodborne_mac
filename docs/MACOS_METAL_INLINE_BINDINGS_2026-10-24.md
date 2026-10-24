# Optional Metal inline binding cache, 2026-10-24

`BB_METAL_BINDING_CACHE=2` extends the existing optional identity cache with
exact-byte suppression of repeated vertex/fragment user data and vertex draw
constants inside one render encoder. Missing/zero still disables the cache;
mode1 retains the previous identity-only behavior. Mode2 remains optional:
synthetic repeated data benefits, while constantly changing data pays comparison
overhead. No gameplay FPS improvement is established by these fixtures.

The renderer copies eligible inline payloads into separate stage/slot keys.
It compares every byte, including changed values at a reused stack address.
Encoder boundaries invalidate the keys; no key survives a render pass end or
command submission. Unretained commands, unsupported lengths and other slots
always bind. An unsupported write invalidates an older cached payload at that
slot. Diagnostic bindings, tessellation inputs, depth bounds, static SMRD data,
mapping checks and GPU-read leases retain their prior paths. No guest memory
is treated as immutable or copied earlier than the existing per-draw binding.

`DC_GPU_STATS=1` reports cumulative inline APIs skipped/intended in mode2,
alongside existing identity-cache counts. This permits an identity-only mode1
versus mode2 comparison using one executable.

## Submission boundary review

Current guest job completion calls `OnSubmit`, handing recorded GPU work and
its fence signals to Metal. An unsatisfied `WAIT_REG_MEM` also submits before
waiting. The existing native trace has approximately13 renderer commands per
frame and no requested CPU GPU waits. It does not justify delaying these
submits: their callbacks can unblock guest command producers. The existing
optional elapsed command budget previously failed to establish a repeatable
gameplay gain. This change preserves all commits, encoder boundaries, fence
callbacks, upload retirement and snapshot release points.

## Validation

The native production fixture passes with cache modes0,1 and2. Consecutive
translated-GCN draws cover repeated and changed user data, texture/sampler
changes, a new guest mapping mid-pass and a new render encoder. Additional
degenerate viewport draws would visibly overwrite expected stripes if stale
draw constants were reused. Every output pixel matches the expected image.
The CPU fixture covers per-stage/slot isolation, content changes at the same
address, identical content at another address, length changes, unsupported
writes, inactive epochs and epoch wrap. All four native CTests pass with GPU
access outside the sandbox; sandbox-only GPU attempts skipped because Metal
reported no device.

The optional `bb-native-metal-binding_cache-tests --benchmark` measures only
CPU calls to the actual Metal encoder. It has no draws, texture validation or
presentation and is not a gameplay performance test. Both modes are warmed
with100,000 iterations; each pattern then runs OFF/ON/ON/OFF twice. Each
iteration attempts three inline bindings:

| Payload pattern | OFF median | ON median | APIs skipped |
| --- | ---: | ---: | ---: |
| Vertex UD changes every16 iterations, fragment UD every8, fixed viewport |2.0003ms|0.6548ms|281,249/300,000|
| All three payloads change every iteration |2.0178ms|2.3782ms|0/300,000|

The repeated pattern removes93.75% of calls, whereas the all-changing pattern
adds approximately17.9% to this isolated operation. Savings per100,000 attempted
draw-state triples must not be presented as per-frame savings. Actual scene
reuse rates and matched gameplay performance decide whether mode2 is useful.

Evidence: `out/metal-inline-bindings-2026-10-24/native-binding-tests.log` and
`native-encoding-benchmark.log`. The native fixture target and its dependencies
built successfully. These results do not establish long-session gameplay
stability, a60FPS lock or validation of other concurrent renderer changes.
Original game files and master saves were not opened for writing by this work.
