# Allocation failure investigation, 2026-10-24

The SSD replay first observed a `std::bad_alloc` throw on `GXRenderThread`,
then the original uncaught-exception/debug path. Its immediate throw caller
was aggregate image RVA `0x573ec02`, or original libc RVA `0x5ec02` after
subtracting the linked libc base `0x56e0000`. Four travel requests preceded
the failure; no GPU error was reported before it. This identifies an exception
type and its throw helper, not the failed game allocation request or its cause.
Evidence: `out/metal-unattended-2026-10-20/resource24-ssd-unwind-soak/`.

## Exact original procedures

The immediate return is the end-exclusive boundary of the complete original
helper `0x5ebd0..0x5ec02`, 50 bytes, export NID `eT2UsmTewbU`. For an FDE
lookup of this return PC, the preceding call instruction belongs to that
procedure; querying the end-exclusive PC itself does not. The helper allocates
an eight-byte exception payload, supplies `_ZTISt9bad_alloc` at `0xbe500` and
destructor `0x5f4b0`, then calls `__cxa_throw` at `0x60510`. Its immediate 8 is
not the failed game request size. REA evidence:
`ev_54fc0357b115f07c3cab3a37ca5a8a198b120577f3751af82bc291fff07e596f`.

Default scalar new is the six-byte dispatch export `0x5fc30`, NID
`fJnpuVVBbKk`, through callback slot `0xbe978`. The initial relative relocation
targets the complete 69-byte implementation `0x5fe80..0x5fec5`. It retains
`max(request,1)` in RBX, calls malloc at `0x1f850`, retries through the installed
new-handler, and calls the bad_alloc helper at `0x5feb9` only after malloc
returned NULL and the new-handler was NULL. The next PC is `0x5febe`.
REA evidence:
`ev_afceaaa4583b92111cdaba18d8f20a3259dae88d00e0711ba3498d27ba636fb6`.
The decompiler's inferred `return` type is incomplete; assembly establishes
that a successful allocation result remains in RAX.

Private exact bytes, original RVAs/FDEs, source offsets and SHA-256 manifests
are under `out/metal-resource-2026-10-24/`. The helper's original file offset
is `0x62bd0`, FDE `0xb0334`, procedure SHA-256
`85746a3e5cf1d749da03e5df88cc4d8d622e8d4820193e8acfb5f44a2587784c`.
Default new is file offset `0x63e80`, FDE `0xb1bf4`, SHA-256
`86da526a4b6a939401d1b916f04b5b600e12cc1b16ce842fbfb6be804ee65e70`.
The complete original bodies remain private, outside tracked test source.

## Emergency exception allocation

The observed exception object, libc-relative `0xd1cf0`, matches payload 0 of
the original emergency exception pool exactly. `__cxa_allocate_exception`
at `0x60070` adds a 128-byte header before calling `0x60090`. For the helper's
eight-byte payload that is `calloc(1,136)`. On NULL it retries under the pool
lock; after another NULL it can take one of sixteen 1024-byte static slots
starting at `0xd1c70`, then returns that slot plus 128. Slot 0 produces exactly
the logged address. REA evidence:
`ev_719cee3101f4539a755a365c29280512c20001877446e55a8807e6776e59e81c`.
This is strong correlation with the original fallback path and failure of the
active calloc callback; it does not establish exhausted host RAM or explain
why the engine allocator returned NULL.

## Bound engine allocator

Resolving original eboot relative relocations gives procparam `0x50dc000`,
libc parameters `0x513b320`, and malloc API table `0x513b398`. Its malloc
callback is game RVA `0x20809f0`; calloc is `0x2080cd0`. Original libc startup
copies these into its `0xba190` and `0xba1a0` dispatch slots. The new/delete
table at `0x513b408` initially has zero callbacks, preserving default new.
This establishes the initial binding; the failed run did not record whether
another callback replaced those slots later.

The exact original game malloc wrapper spans `0x20809f0..0x2080b67`, 375
bytes. It preserves request bytes in R14, locks the allocator object reached
through global `0x56a20c0`, calls `0x2085730` with object+`0x28` and that size,
then returns the core result after unlocking. The 399-byte calloc wrapper at
`0x2080cd0..0x2080e5f` computes its count-times-size and calls the same core,
zeroing storage only when the result is non-NULL. These wrapper observations
come from the exact private original-byte disassembly, not a live allocator
capture. The additional core/initializer analysis below establishes initial
capacity, while live free space and fragmentation remain unobserved.

Several other original routines call the same bad_alloc helper. The immediate
helper return will select the path without assuming all bad_alloc throws
come from default new:

| Helper return RVA | Caller path and size evidence |
| --- | --- |
| `0x5febe` | Default new; normalized request retained in RBX. |
| `0x5a7d9` | Reallocation of eight-byte entries; RBX retains entry count. |
| `0x5a940` | Malloc of eight-byte entries; count is at `[R12+0x18]`. |
| `0x62cac` / `0x62d40` | String allocation/fallback paths; capacities remain in R13/R15. |
| `0x70cc6` / `0x70d6a` | Wide-string allocation or size-overflow rejection; a throw can precede an allocator call. |

Only the proven default-new path receives a labeled request size in the new
diagnostic. Other paths retain raw registers and an unavailable request label.

## Optional failure observation

`BB_RUNTIME_EXIT_AUDIT=1` now observes the original bad_alloc helper entry and
the default-new failure label using existing bbcpu probes. Installation is
anchored to the validated bound original throw export in libc module0, and
checks helper/default-new extents, entry signatures and exact relative call
targets. This is a bounded signature guard, not a full binary identity check.
The helper logs its checked immediate return and raw registers. The default
failure label additionally reports normalized RBX request bytes and one
checked original-caller slot at `[RSP+24]`. That fixed displacement follows
the validated `push rbp; push rbx; push rax` frame; it is not a frame-chain walk.
Unreadable or non-guest caller values remain explicitly unavailable.

Both events also read the three current callback slots through checked,
module-bounded readable-image access, logging target pointers and guest RVAs
when recognized. No callback target is invoked by the observer. Host and guest
errno, guest code, registers, stack and allocation behavior remain unchanged.
The first 64 failure events are logged, followed by one truncation message.
Successful allocator calls have no failure-event hook or log. Rosetta direct
x86 execution does not use these translator probes.

Owned synthetic loader fixtures prove observation of the interior conditional
fallthrough label, no event on success, normalized 37 and zero-to-one requests,
the exact fixed-slot origin and immediate helper caller, checked callback
slots, original synthetic return/header writes/errno, mismatched-signature
refusal, event bound and disabled behavior. No original procedure body is
embedded in those tracked fixtures. Gameplay diagnosis with these new hooks
is a separate next step; no allocation-budget expansion or failure suppression
is part of this change.

The combined final native game/probe and Rosetta probe build succeeds. All
29 loader tests pass in each architecture after the final changes. The native
fixture records the interior failure label before helper entry, reports the
correct request37 and origin RVA, and leaves the successful path without an
allocation event. The combined native game includes the separately tested
flexible-memory accounting correction; no gameplay result is claimed here.
Its SHA-256 is
`8895a8eeb20790c4e50a048f19c4ea60329a0c4206594d18cd316e7cb8ffb081`.

Build/test logs in the same private output directory:
`allocation-audit-combined-native-build.log`,
`allocation-audit-combined-x86-probe-build.log`,
`allocation-audit-native-loader.log` and `allocation-audit-x86-loader.log`.
REA records and exact original-byte artifacts are
`rea-libc-{operator-new,badalloc-helper,emergency-allocation}.json`,
`libc-allocation-tiny.json`, `libc-emergency-allocation-tiny.json`,
`libc-allocation-original-bytes.asm.txt`, `eboot-allocator-tiny.json` and
`eboot-allocator-original-bytes.asm.txt`.

## Second SSD replay: a 384-byte vector allocation

The next replay, `resource24-ssd-allocation-soak`, used the frozen `8895a8…`
binary and failed after three travel requests. The default-new failure event
records normalized request 384, game return RVA `0x2a3bbaf`, R15=16 and
R13=`0x80073d29f8`. Current callback slots still selected original malloc
`0x20809f0`, calloc `0x2080cd0`, and linked default new `0x573fe80`. Its helper
entry then records return `0x573febe`, followed by the same emergency-pool
bad_alloc object and original uncaught-exception/debug path. There was no
kernel memory-allocation failure audit row. This does not prove host memory
exhaustion, a specific game leak, or valid heap metadata.

The complete original containing procedure is
`0x2a3bab0..0x2a3bfc9`, 1305 bytes, FDE `0x4d4d7dc`, original ELF file offset
`0x2a3fab0`, SHA-256
`c959637f75e521c11c952fc569b18358af3cd368a1df47c349ca1966616cb3cc`.
REA evidence
`ev_3ba0be8d5baa034239e59e03b334835672aa141ce6c442d4d5a1798dd69f8bfe`
agrees with the independent original-byte disassembly: it inserts/grows a
vector-style sequence of 24-byte records, with begin/end/capacity-end at
header offsets 8/16/24. Growth selects the larger of approximately 1.5 times
old capacity and size plus inserted count. The exact call at `0x2a3bbaa`
allocates capacity times 24, matching observed 16 times 24 =384 bytes.

The successful path copies records, calls scalar delete on the old buffer at
`0x2a3bcdd`, and updates the header. Original GOT relocation names plus NID
hashes confirm scalar new thunk `0x2bbe9a8` and scalar delete `0x2bbe588`.
This establishes a real free edge in the procedure, not its live execution or
the higher-level owner's lifetime. A bounded original-text byte inventory
finds 30 direct CALL candidates into this generic procedure; those candidates
do not identify which owner was executing and do not cover indirect callers.
The tiny extraction omits external data, relocations and indirect state, so
decompiler type/signature guesses remain provisional.

Private evidence: `eboot-allocation-failure-tiny.{elf,json}`,
`eboot-allocation-failure-extraction.json`,
`eboot-allocation-failure-original-bytes.asm.txt`,
`eboot-allocation-failure-callers.json`, and
`rea-eboot-allocation-failure.json` in the same output directory. Original
container SHA-256 before and after extraction is
`d65f0b4f01d59166aed16f8604196d8b7dd805abbf0758b356e8f1354c9429f9`;
parsed original ELF SHA-256 is
`cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86`.

## Fixed standard heap: proved layout and next-run observation

Complete original allocator core `0x2085730..0x2085894` (356 bytes) and
initializer `0x20819f0..0x2081c5e` (622 bytes) were extracted with original
FDEs, source offsets and procedure hashes in `eboot-heap-core-tiny.json`.
The initializer requests/maps `0x7400000` bytes (116 MiB) with 2 MiB
alignment, then constructs the heap and its initial free block. The core
compares request against the free-byte field, rounds/splits blocks and
updates segregated bins. There are no calls/growth edges in its complete
356-byte body. This does not exclude an allocator reset/rebuild elsewhere.
REA provider analysis of these two procedures has not yet been performed;
these are bounded exact-byte disassembly observations.

The standard object is `G=[image+0x56a20c0]`, initially the static RW BSS
object `image+0x56a20c8`; its core heap is `H=G+0x28`. All fields below are
proved eight-byte unsigned scalars:

| Object offset | Recorded field |
| --- | --- |
| `G+0x28` / `H+0` | Bitmap |
| `G+0x30` / `H+8` | Usable arena start |
| `G+0x38` / `H+0x10` | Usable arena end |
| `G+0x40` / `H+0x18` | Free bytes |
| `G+0x48` / `H+0x20` | Counter incremented by allocation; live-count meaning unproved |
| `G+0x460` | Initial usable span |
| `G+0x468` | Mapped base |
| `G+0x470` | Direct-memory physical offset |
| `G+0x478` | Mapped capacity |

Usable initial span can be capacity minus 32 bytes; raw capacity and free
bytes are not required to be identical. No arena-size increase is implemented.

The next optional failure capture reads only these nine scalars twice, with
checked image bounds and RW segment membership. Selection requires the exact
prepared ELF hash above, matching current malloc/calloc callbacks, two small
original instruction/layout guards, and the exact default static BSS object.
The identity is supplied by the trusted launcher from its preparation record;
the observer does not recompute a hash of relocated in-memory code. It
rechecks the object global and callback selection after both samples. Other
images/objects, changed selection, unavailable bytes and guard mismatches
report explicit unavailability.

Rows are labeled `unsynchronized=1`, with `sample_comparison=equal` or
`different-racy`. Even equal reads are not an atomic/coherent snapshot. Raw
values never trigger a corruption diagnosis, metadata repair, allocation
retry, list walk or guest call. The observations remain useful for deciding
whether the small request exceeded recorded free space; sufficient aggregate
free space alone would still not prove fragmentation or valid bins.

At helper entry, only seven known helper returns select a proved original
caller frame. Caller extent/prologue and its direct call to the helper must
match; captured RBP must equal RSP plus the selected displacement minus 8.
One checked saved return is read at fixed helper-entry RSP offsets: 32 for
default new, 240 for `0x5a940`, and 64 for the other classified returns. No
frame chain is followed. At the existing default-new failure site, only the
exact game return `image+0x2a3bbaf` can select one additional saved owner
return at `RSP+168`; this requires the exact eboot identity, full executable
extent, observed game prologue, expected scalar-new CALL target, current
default-new callback and its validated RBP/RSP relationship. Unknowns remain
unavailable.

Finally, helper entry can read one checked 512-byte block starting exactly at
guest RSP and log its 64 words as `uninterpreted=1`. Partial/unmapped/overflow
ranges report unavailable. These private local words are not an inferred
stack trace and no word is followed as a pointer. Existing 64-event limits and
host/guest errno preservation cover every new observation. Successful
allocations and disabled diagnostics do not execute these failure probes.

## Follow-up validation

The owned sparse fixture in `tests/test_allocation_audit.c` exercises identity,
callback/layout/object and segment guards, fixed scalar read failures, two
different samples, selection changes, all seven caller layouts and refusals,
the vector-owner slot/refusals, checked exact-size 512-byte reads, unavailable
and overflow ranges, and errno restoration. It synthesizes virtual memory
cells; no original procedure body is embedded or executed. The shared target
`bb-runtime-allocation-audit-tests` passes natively and under Rosetta. All 29
loader tests also pass in each architecture. The initial Rosetta invocation
omitted its required advertised-AVX environment and stopped at the CPU
capability guard; rerunning with `ROSETTA_ADVERTISE_AVX=1` passes.

Full native game and native/Rosetta probes build successfully. Final native
game SHA-256:
`f3143e3d4183d483a25d2c3af85bbc097d68e8d6c187221e57f4f122cd453cc4`.
Native probe SHA-256:
`5a606e08e4649a52758bb10872239e203a57feb2a9925b19fb51dd1365bc5be9`.
Logs are `heap-audit-{native,x86}-{build,loader,fixture}.log`. This validates
the optional diagnostic mechanism and unchanged synthetic behavior; gameplay
coverage of these new reads is the next replay, and no root-cause fix or
performance improvement is claimed by this diagnostic patch.

## Complete original-core differential

Subsequent private fixtures execute the unchanged 356-byte allocation core and
445-byte free core in owned arenas. Native JIT, native interpreter and Rosetta
pass through both direct entry and an owned guest CALL/RET wrapper. Each of
the six cases performs 109,640 allocation/free attempts; normalized operation
and structural state digest is `68142e2e182426aa` throughout. All 53,699
successful allocations are freed, with 2,242 expected NULL outcomes. Coverage
includes size boundaries, repeated 384-byte allocations, exhaustion,
fragmentation/coalescing and fixed-seed mixed sizes. Bin membership, bitmap,
block chain, counters, bounds, alignment and payload canaries match; both the
1 MiB and 116 MiB arenas return to their initial state.

This finds no serial allocator-core translation discrepancy. It does not
validate lock/vtable wrappers, concurrent heap mutation, heap selection or
actual gameplay allocation lifetime. Manifests, exact original-input hashes,
logs and limitations are in `heap-core-differential-results.json` and
`heap-core-differential-feasibility.md` under the private evidence root.

The next gameplay attempt prewarms shaders but stops before loading the save:
the Mac is locked and activation/presentation guards fail. It supplies no
live heap snapshot and is not another allocation failure. See
[SSD replay status](MACOS_SSD_TESTS_2026-10-24.md).

## Live owner, fragmentation condition and retirement investigation

The unlocked queue-corrected replay fails after 410.902296167 seconds to its
last submission. The same 384-byte request now has validated owner return
`0x2a4877d`. Two checked unsynchronized samples agree on bitmap `0x60`, free
bytes 5,696,288 and live count 552,827 in the original 116 MiB arena. Current
malloc/calloc/new callbacks remain original. The request rounds to a 400-byte
block, requiring bin 8 or higher; bitmap `0x60` contains only bins 5 and 6.
Conditional on valid metadata, external fragmentation explains this immediate
rejection despite aggregate free space. No coherent bin/block traversal was
captured and no host-RAM exhaustion is inferred.

Exact REA owner/factory analysis identifies this allocation as initial
16×24-byte bucket storage in the base constructor of the original named
`ID3D11ShaderResourceView` object. The original factory return `0x11dda46` is
also validated from the fixed captured slot against the complete caller frame
and direct CALL. It is not an arbitrary code-looking stack word. The per-view
construction has four allocations with total rounded extent 752 bytes; the
untyped live count cannot be converted into a number of retained views.

Cleanup at `0x2a48ca0`/`0x2a48d10` actually frees the vector, sentinel,
registration node and view. Original Release `0x2a489a0` uses atomic external
reference decrement, internal refs and a GPU EOP completion sequence to decide
eligibility. The sequence is the qword referenced by image global `0x54b9eb0`,
published from context+`0xc30`; submission methods increment context+`0x3400`
and emit a 64-bit EVENT_WRITE_EOP followed by WAIT_REG_MEM for that label.
This is a guest GPU sequence, not a host frame or wall clock. Current source
handles that selector, but actual target-label progress remains unobserved.

The unchanged 115-byte original Release passes six private differential
cases: JIT, interpreter and Rosetta, each direct and guest-CALL entry. Each
performs 32,043 calls with digest `9784b9217d3affc2`, covering serial unsigned
boundaries/defer decisions and two four-thread atomic-refcount cases. Old-count
returns, cleanup ABI/order, exactly-once eligible cleanup and guards match.
Synthetic cleanup does not actually free the object. Real internal-reference
mutations, cleanup/free execution and GPU progress are outside these tests.

The wrapper audit also finds matching original recursive lock/unlock imports
around malloc/calloc/free. Runtime sync helpers allocate host storage, not
directly from this game arena. Cumulative, non-atomic mutex statistics cannot
prove a leak or broken lock ownership.

Private manifests, all five REA evidence IDs, complete original FDE/hash
records, source references and limits are in `view-owner-rea-ledger.md`,
`heap-wrapper-lock-audit.*`, `queue-ssd-heap-sync-source-review.md` and
`view-release-differential-results.*` under the evidence root. Original game
data and master saves remain unchanged. A narrow read-only counter observation
is the next test; no retirement override or speculative allocator fix is made.
