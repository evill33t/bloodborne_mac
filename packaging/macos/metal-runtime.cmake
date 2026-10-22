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
    "${BB_ROOT}/gpu/metal/vendor/bbport/cp/cp_decoder.cpp"
    "${BB_ROOT}/gpu/metal/raw_texture_snapshot.mm"
    "${BB_ROOT}/gpu/metal/gpu_texture_upload.mm"
    "${BB_ROOT}/gpu/metal/frontend_snapshot.cpp"
    "${BB_ROOT}/gpu/metal/game_bridge.mm")
target_include_directories(bb-ps4-metal PRIVATE "${BB_PS4_METAL}" "${BB_ROOT}")
target_compile_options(bb-ps4-metal PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_link_libraries(bb-ps4-metal PUBLIC SDL3::SDL3-static bbmac-network bbmac-features
    "-framework Metal" "-framework MetalFX" "-framework AppKit" "-framework Foundation" "-framework QuartzCore")
