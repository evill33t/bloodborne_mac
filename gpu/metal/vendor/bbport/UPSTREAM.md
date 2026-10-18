# bbport command decoder

Selected `gpu/cp/` source and `tests/test_cp_decoder.cpp` from
[deadinside28/bloodborne_pc](https://github.com/deadinside28/bloodborne_pc),
commit `3c57da4198ad62039f63f33bfd205b9916518acf`.

License: GPL-2.0-or-later; see the repository's root LICENSE and the source notices.
The decoder has no Vulkan dependency. The Mac integration uses it for optional
per-packet register validation against the existing direct Metal frontend.
The original decoder unit tests are retained as `upstream_cp_decoder_tests.cpp`.

The Mac input adapter, camera-constant layout, temporal AA, texture filtering and
cache policy also use this project's implementations as references. Those are
adaptations to the existing Darwin/Metal architecture, not its Vulkan renderer.
