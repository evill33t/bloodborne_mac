# bloodborne-recomp assessment, 24 October 2026

The repository can help this port's analysis and validation. Its CPU core is
already integrated. Enabling whole-game ahead-of-time C recompilation has no
demonstrated performance advantage for our build and does not remove the
resource tracking or PS4 command processing in our direct Metal renderer.

## Reviewed identity

Public upstream HEAD was queried directly on 24 October:
[`ef153b8`](https://github.com/bloodborne-native/bloodborne-recomp/tree/ef153b8d35768aff86fa1643c12e7d0340b16501),
dated 22 October. Our imported CPU sources are pinned to
[`b9652d7`](https://github.com/bloodborne-native/bloodborne-recomp/tree/b9652d75edc0a281f06cec0da6ff72e9f5150a70);
see [source provenance](../third_party/bbcpu/UPSTREAM.md).
The GitHub compare API reports three later commits changing only
`.github/workflows/macos.yml`, `CLAUDE.md`, and `tools/cpu/test_jit.sh`.
There is no newer CPU implementation in that comparison. The complete response
and selected public tool sources are retained locally under
`out/metal-resource-2026-10-24/recomp-review/`.

This was ordinary source review. No REA binary execution was needed to establish
these repository facts, and no upstream scripts were executed or installed.

## What is already present

Our native `BB_MAC_CPU=bbcpu` path builds the upstream decoder, interpreter,
arm64 translator, probes and recorder, with our runtime/ABI adaptations. The
generated-library support source is included in the CPU library, but our
runtime does not call `bbcpu_recomp_load`; merely setting `BB_RECOMP_LIB` does
not enable AOT here. The generator, scanner and replay executables have not
been imported into the production build.

## Useful next applications

| Component | Use for this port | Integration boundary |
| --- | --- | --- |
| `tools/recomp/scan.c` and relocation pointers | Extend the unwind-derived function map with decoded direct-call and code-pointer reachability; locate renderer producers for focused REA investigation. | Generate from our exact local executable. Upstream targets CUSA03173 1.09; our CUSA00900 image and installed hooks require independent byte/hash validation. |
| `sample_guest.py` and `profile.py` | Attribute macOS samples through `BB_JIT_MAP` to guest functions and broad engine/library groups. | Adapt the image-base log parser to our loader; keep host Metal costs separate and report unmapped samples. Library labels are approximate. |
| Recorder and replay tools | Check registers, memory effects and call boundaries before replacing a proven expensive guest routine with native code. | Independently replay the original first. Time reads and concurrently changed memory can prevent deterministic replay. The existing runtime's memory mappings and traps must be represented. |
| Generated C and instruction fuzzing | An optional experiment for specific CPU-bound routines once a profile identifies them. | Preserve fallbacks, patched-code hashes, callback ABI, exception/longjmp behavior and command-writer guards. Direct C-to-C calls can bypass entry hooks. |

The source documents these tools and their limitations in
[Recompilation](https://github.com/bloodborne-native/bloodborne-recomp/blob/ef153b8d35768aff86fa1643c12e7d0340b16501/docs/RECOMPILATION.md).
Whole-game coverage is not equivalent to whole-game behavioural verification:
the published call-by-call checks cover a much smaller set of functions.

## Performance implications

The current upstream README reports 20–30% more CPU time with the full
recompiled library than with the translator. Its detailed progress table
reports 55.2 FPS versus approximately 60 in its own comparison. Those are
upstream results with their scenes, hardware and pipeline, not predictions
for this M4 Max build. The smaller hot-function experiments reported roughly
translator-equivalent throughput, which also does not establish a local gain.

AOT replaces execution of guest x86 instructions. It leaves guest PM4 writing,
our packet processing, texture coherence, shader execution, Metal encoder
boundaries and fences in place. Shader precompilation is a separate mechanism.
Any FPS benefit therefore depends on the CPU portion actually limiting the
frame; it cannot be inferred from the word “native.”

The related runtime's
[native renderer plan](https://github.com/bloodborne-native/bloodborne-runtime/blob/1c8f8aa1dd6dbf3588d17d979a6d549cdb787fa5/docs/NATIVE_RENDERER.md)
is useful as a hypothesis source for encoder breaks and resource objects.
Its measured MoltenVK costs do not transfer directly to our existing Metal
backend. Its engine-level render boundary and texture-lifecycle plan are not
a ready replacement we can import to remove all emulation work.

The runtime revision was checked directly at `1c8f8aa` on24 October; older
cached web copies of that document substantially predate its adapter migration.
Its current table puts the experimental Metal work at50%, texture objects and
the engine boundary at0%. It reports61.7/65.5 FPS for Metal versus58.0/58.1 for
MoltenVK on an M3 Pro, using1080p **output with FSR Balanced**. Those numbers
are not native1080p renderer measurements. The table's recompiled-library run
is51.7–57.3 FPS, again not evidence that enabling AOT raises this port's FPS.
The document still describes transitional code as well as newer adapter steps;
inspect the selected implementation before adapting any individual technique.

For the current round, prioritize matched texture-tracking measurements,
correct shadow input ownership, and measured encoder/submission stalls.
Use the recompiler's function mapping and replay methodology to make future
engine-level replacements evidence-based. Keep whole-game AOT optional until
it passes our exact-image correctness and native 1080p performance comparisons.
No original game data was changed by this review.
