# Shared direct Metal production sources for the Rosetta and native CPU runtimes.
set(BB_PS4_METAL "${BB_ROOT}/gpu/metal/vendor/driveclub/src")
add_subdirectory("${BB_ROOT}/gpu/metal/bbhost" bbhost-features)
add_library(bb-ps4-metal STATIC
    "${BB_PS4_METAL}/gpu/liverpool/command_processor.cpp"
    "${BB_PS4_METAL}/gpu/liverpool/tiling.cpp"
    "${BB_PS4_METAL}/gpu/shader/gcn_decode.cpp"
    "${BB_PS4_METAL}/gpu/shader/program.cpp"
    "${BB_PS4_METAL}/gpu/shader/msl.cpp"
    "${BB_PS4_METAL}/gpu/metal/mtl_device.mm"
    "${BB_PS4_METAL}/gpu/metal/mtl_compiler.mm"
    "${BB_PS4_METAL}/gpu/metal/mtl_renderer.mm"
    "${BB_PS4_METAL}/gpu/metal/mtl_presenter.mm"
    "${BB_PS4_METAL}/hle/gnmdriver/gnmdriver.cpp"
    "${BB_PS4_METAL}/hle/video_out/video_out.cpp"
    "${BB_PS4_METAL}/hle/kernel/sync.cpp"
    "${BB_ROOT}/gpu/metal/game_settings.cpp"
    "${BB_ROOT}/gpu/metal/game_overlay.mm"
    "${BB_ROOT}/gpu/metal/game_achievements.mm"
    "${BB_ROOT}/gpu/metal/game_input.mm"
    "${BB_ROOT}/gpu/metal/spatial_upscaler.mm"
    "${BB_ROOT}/gpu/metal/temporal_aa.mm"
    "${BB_ROOT}/gpu/metal/frame_generation.mm"
    "${BB_ROOT}/gpu/metal/vendor/bbport/cp/cp_decoder.cpp"
    "${BB_ROOT}/gpu/metal/raw_texture_snapshot.mm"
    "${BB_ROOT}/gpu/metal/gpu_texture_upload.mm"
    "${BB_ROOT}/gpu/metal/frontend_snapshot.cpp"
    "${BB_ROOT}/gpu/metal/game_bridge.mm")
target_include_directories(bb-ps4-metal PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
target_compile_options(bb-ps4-metal PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(bb-ps4-metal PUBLIC SDL3::SDL3-static bbmac-network bbmac-features
    "-framework Metal" "-framework MetalFX" "-framework AppKit" "-framework Foundation" "-framework QuartzCore")

# Source recipes are recompiled by the selected host runtime; no game is run.
add_executable(bb-macos-precompile-shaders "${BB_ROOT}/gpu/metal/precompile_main.mm")
target_include_directories(bb-macos-precompile-shaders PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
target_compile_options(bb-macos-precompile-shaders PRIVATE -Wall -Wextra -Werror -fobjc-arc)
target_link_libraries(bb-macos-precompile-shaders PRIVATE bb-ps4-metal bbruntime)

if(BUILD_TESTING OR NOT DEFINED BUILD_TESTING)
    add_executable(bb-metal-original-shadow-projection-tests "${BB_ROOT}/gpu/metal/tests/game_original_shadow_projection_tests.mm")
    target_include_directories(bb-metal-original-shadow-projection-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-original-shadow-projection-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-metal-original-shadow-projection-tests PRIVATE bb-ps4-metal bbruntime)
    add_test(NAME metal-original-shadow-projection COMMAND bb-metal-original-shadow-projection-tests)
    set_tests_properties(metal-original-shadow-projection PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77)
    # Optional exact original CS input; shared renderer fixture on both hosts.
    add_executable(bb-metal-shadow-htile-clear-tests "${BB_ROOT}/gpu/metal/tests/game_shadow_htile_clear_tests.mm")
    target_include_directories(bb-metal-shadow-htile-clear-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-shadow-htile-clear-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-metal-shadow-htile-clear-tests PRIVATE bbruntime bb-ps4-metal)
    if(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
        target_link_options(bb-metal-shadow-htile-clear-tests PRIVATE
            -Wl,-ld_classic,-no_pie,-no_fixup_chains,-no_huge,-pagezero_size,0x4000,-segaddr,TCB_SPACE,0x4000,-image_base,0x700000000000)
    endif()
    add_test(NAME metal-shadow-htile-clear COMMAND bb-metal-shadow-htile-clear-tests)
    set_tests_properties(metal-shadow-htile-clear PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 LABELS "gpu;private" RUN_SERIAL TRUE)
    add_executable(bb-metal-shadow-htile-clear-contract-tests "${BB_ROOT}/gpu/metal/tests/game_shadow_htile_clear_contract_tests.cpp")
    target_include_directories(bb-metal-shadow-htile-clear-contract-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-shadow-htile-clear-contract-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME metal-shadow-htile-clear-contract COMMAND bb-metal-shadow-htile-clear-contract-tests)
    set_tests_properties(metal-shadow-htile-clear-contract PROPERTIES TIMEOUT 10)
    add_executable(bb-metal-wind-consumed-contract-tests "${BB_ROOT}/gpu/metal/tests/game_wind_consumed_contract_tests.cpp")
    target_include_directories(bb-metal-wind-consumed-contract-tests PRIVATE "${BB_ROOT}")
    target_compile_options(bb-metal-wind-consumed-contract-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME metal-wind-consumed-contract COMMAND bb-metal-wind-consumed-contract-tests)
    set_tests_properties(metal-wind-consumed-contract PROPERTIES TIMEOUT 10)
    add_executable(bb-metal-wind-consumed-shader-tests "${BB_ROOT}/gpu/metal/tests/game_wind_consumed_shader_tests.cpp")
    target_include_directories(bb-metal-wind-consumed-shader-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-wind-consumed-shader-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(bb-metal-wind-consumed-shader-tests PRIVATE bb-ps4-metal bbruntime)
    add_test(NAME metal-wind-consumed-shader COMMAND bb-metal-wind-consumed-shader-tests)
    set_tests_properties(metal-wind-consumed-shader PROPERTIES TIMEOUT 30)
    add_executable(bb-metal-wind-consumed-gpu-tests "${BB_ROOT}/gpu/metal/tests/game_wind_consumed_gpu_tests.mm")
    target_include_directories(bb-metal-wind-consumed-gpu-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-wind-consumed-gpu-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-metal-wind-consumed-gpu-tests PRIVATE bb-ps4-metal bbruntime)
    add_test(NAME metal-wind-consumed-gpu COMMAND bb-metal-wind-consumed-gpu-tests)
    set_tests_properties(metal-wind-consumed-gpu PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
    add_executable(bb-metal-content-hash-tests "${BB_ROOT}/gpu/metal/tests/game_content_hash_tests.cpp")
    target_include_directories(bb-metal-content-hash-tests PRIVATE "${BB_PS4_METAL}")
    target_compile_options(bb-metal-content-hash-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME metal-content-hash-crc COMMAND bb-metal-content-hash-tests)
    set_tests_properties(metal-content-hash-crc PROPERTIES TIMEOUT 60 ENVIRONMENT "BB_METAL_NATIVE_SIMD_HASH=0")
    add_test(NAME metal-content-hash-simd COMMAND bb-metal-content-hash-tests)
    set_tests_properties(metal-content-hash-simd PROPERTIES TIMEOUT 60 ENVIRONMENT "BB_METAL_NATIVE_SIMD_HASH=1")
    add_executable(bb-metal-exact-texture-compare-tests "${BB_ROOT}/gpu/metal/tests/game_exact_texture_compare_tests.cpp")
    add_executable(bb-metal-render-thread-qos-tests "${BB_ROOT}/gpu/metal/tests/game_render_thread_qos_tests.cpp")
    target_include_directories(bb-metal-render-thread-qos-tests PRIVATE "${BB_ROOT}")
    target_compile_options(bb-metal-render-thread-qos-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME metal-render-thread-qos COMMAND bb-metal-render-thread-qos-tests)
    target_include_directories(bb-metal-exact-texture-compare-tests PRIVATE "${BB_ROOT}")
    target_compile_options(bb-metal-exact-texture-compare-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    add_test(NAME metal-exact-texture-compare COMMAND bb-metal-exact-texture-compare-tests)
    set_tests_properties(metal-exact-texture-compare PROPERTIES TIMEOUT 60)
    add_executable(bb-metal-prewarm-variant-tests "${BB_ROOT}/gpu/metal/tests/game_prewarm_variant_tests.mm")
    target_include_directories(bb-metal-prewarm-variant-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-metal-prewarm-variant-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-metal-prewarm-variant-tests PRIVATE bb-ps4-metal bbruntime)
    add_test(NAME metal-prewarm-variant COMMAND bb-metal-prewarm-variant-tests)
    set_tests_properties(metal-prewarm-variant PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77)
    add_executable(bb-frame-generation-tests "${BB_ROOT}/gpu/metal/tests/game_frame_generation_tests.mm")
    target_include_directories(bb-frame-generation-tests PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
    target_compile_options(bb-frame-generation-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-frame-generation-tests PRIVATE bb-ps4-metal)
    add_test(NAME metal-frame-generation COMMAND bb-frame-generation-tests)
    set_tests_properties(metal-frame-generation PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77)
    add_test(NAME metal-frame-generation-stale COMMAND bb-frame-generation-tests --stale)
    set_tests_properties(metal-frame-generation-stale PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77)
    add_test(NAME metal-frame-generation-swizzle COMMAND bb-frame-generation-tests --swizzle)
    set_tests_properties(metal-frame-generation-swizzle PROPERTIES TIMEOUT 60 LABELS "gpu" SKIP_RETURN_CODE 77)
endif()
