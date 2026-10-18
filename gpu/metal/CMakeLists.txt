include(FetchContent)
# Immutable SDK releases and archive hashes. Neither dependency needs Vulkan.
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_CPP OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_REFLECT OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_C_API OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_UTIL OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_SKIP_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(spirv_cross
    URL https://github.com/KhronosGroup/SPIRV-Cross/archive/refs/tags/vulkan-sdk-1.4.328.1.tar.gz
    URL_HASH SHA256=5b1149927e40a67396b440711543a3b1f9d004c844ca7293582a72c01cb69756
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(spirv_cross)

find_library(BB_METAL Metal REQUIRED)
find_library(BB_FOUNDATION Foundation REQUIRED)
find_library(BB_QUARTZCORE QuartzCore REQUIRED)
add_library(bbmetal STATIC shader.cpp device.mm)
target_include_directories(bbmetal PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bbmetal PRIVATE spirv-cross-msl PUBLIC
    "${BB_METAL}" "${BB_FOUNDATION}" "${BB_QUARTZCORE}")
target_compile_options(bbmetal PRIVATE -Wall -Wextra -Werror
    "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")

# Build the project's existing GLSL host shaders as compiler intermediates.
# The output is consumed by SPIRV-Cross, never by a Vulkan API.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    URL https://github.com/KhronosGroup/glslang/archive/refs/tags/15.4.0.tar.gz
    URL_HASH SHA256=b16c78e7604b9be9f546ee35ad8b6db6f39bbbbfb19e8d038b6fe2ea5bba4ff4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(glslang)

set(BB_METAL_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
function(bb_metal_host_shader filename name)
    set(output "${BB_METAL_SHADER_DIR}/${name}.spv")
    set(defines ${ARGN})
    list(TRANSFORM defines PREPEND "-D")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${BB_METAL_SHADER_DIR}"
        COMMAND glslang-standalone --target-env vulkan1.2 ${defines}
            -o "${output}" "${CMAKE_CURRENT_SOURCE_DIR}/../shadps4/video_core/host_shaders/${filename}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../shadps4/video_core/host_shaders/${filename}"
        VERBATIM)
    set(BB_METAL_HOST_SHADERS ${BB_METAL_HOST_SHADERS} "${output}" PARENT_SCOPE)
endfunction()
bb_metal_host_shader(fs_tri.vert fs_tri_vert)
bb_metal_host_shader(post_process.frag post_process_frag)
bb_metal_host_shader(buffer_multi_copy.comp buffer_multi_copy_comp)
bb_metal_host_shader(camera_motion.comp camera_motion_comp)
bb_metal_host_shader(camera_motion.comp camera_motion_debug_comp DEBUG_OVERLAY=1)
bb_metal_host_shader(taa.comp taa_comp)
bb_metal_host_shader(taa_sharpen.comp taa_sharpen_comp)
bb_metal_host_shader(taa_sharpen.comp taa_sharpen_ldr_comp LDR_OUTPUT=1)
bb_metal_host_shader(upscale_merge.comp upscale_merge_comp)
bb_metal_host_shader(upscale_reactive.comp upscale_reactive_comp)
foreach(bpp IN ITEMS 8 16 32 64 96 128)
    bb_metal_host_shader(tiling.comp tiling_micro_${bpp}_comp BITS_PER_PIXEL=${bpp} IS_MACRO_TILED=0)
    bb_metal_host_shader(tiling.comp tiling_macro_${bpp}_comp BITS_PER_PIXEL=${bpp} IS_MACRO_TILED=1)
endforeach()
add_custom_target(bb-metal-host-shaders ALL DEPENDS ${BB_METAL_HOST_SHADERS})
list(LENGTH BB_METAL_HOST_SHADERS BB_METAL_HOST_SHADER_COUNT)
string(JOIN "\n" BB_METAL_HOST_MANIFEST ${BB_METAL_HOST_SHADERS})
file(GENERATE OUTPUT "${BB_METAL_SHADER_DIR}/manifest.txt" CONTENT "${BB_METAL_HOST_MANIFEST}\n")

add_executable(bb-metal-shader tools/shader_tool.cpp)
target_link_libraries(bb-metal-shader PRIVATE bbmetal)

if(BUILD_TESTING)
    add_executable(bb-metal-quality-contracts tests/game_quality_tests.cpp vendor/bbport/cp/cp_decoder.cpp)
    add_executable(bb-metal-cp-contracts tests/upstream_cp_decoder_tests.cpp vendor/bbport/cp/cp_decoder.cpp)
    foreach(fixture IN ITEMS quality cp)
        target_include_directories(bb-metal-${fixture}-contracts PRIVATE "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/vendor/bbport/cp")
        target_compile_options(bb-metal-${fixture}-contracts PRIVATE -UNDEBUG -Wall -Wextra -Werror)
        add_test(NAME metal-${fixture}-contracts COMMAND bb-metal-${fixture}-contracts)
    endforeach()
    set(BB_METAL_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/fixtures")
    foreach(fixture IN ITEMS bindings too_many_buffers)
        add_custom_command(OUTPUT "${BB_METAL_FIXTURE_DIR}/${fixture}.spv"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${BB_METAL_FIXTURE_DIR}"
            COMMAND glslang-standalone --target-env vulkan1.2
                -o "${BB_METAL_FIXTURE_DIR}/${fixture}.spv"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/${fixture}.comp"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders/${fixture}.comp" VERBATIM)
        list(APPEND BB_METAL_FIXTURES "${BB_METAL_FIXTURE_DIR}/${fixture}.spv")
    endforeach()
    add_custom_target(bb-metal-fixtures DEPENDS ${BB_METAL_FIXTURES})
    add_executable(bb-metal-tests tests/metal_tests.mm)
    target_compile_options(bb-metal-tests PRIVATE -Wall -Wextra -Werror -fobjc-arc)
    target_link_libraries(bb-metal-tests PRIVATE bbmetal)
    target_compile_definitions(bb-metal-tests PRIVATE BB_METAL_SHADER_DIR="${BB_METAL_SHADER_DIR}"
        BB_METAL_FIXTURE_DIR="${BB_METAL_FIXTURE_DIR}"
        BB_METAL_HOST_SHADER_COUNT=${BB_METAL_HOST_SHADER_COUNT})
    add_dependencies(bb-metal-tests bb-metal-host-shaders bb-metal-fixtures)
    add_test(NAME metal-shader-contracts COMMAND bb-metal-tests --translation-only)
    add_test(NAME metal-gpu COMMAND bb-metal-tests)
    set_tests_properties(metal-gpu PROPERTIES LABELS "gpu" SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
