# Selected engine and plugin code from bbhost; the renderer remains direct Metal.
set(BB_FEATURE_VENDOR "${BB_ROOT}/gpu/metal/vendor/bbhost")
set(BB_FEATURE_SRC "${BB_FEATURE_VENDOR}/src")
set(BB_FEATURE_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${BB_FEATURE_GEN}")
file(WRITE "${BB_FEATURE_GEN}/bbhost_version.h" "#pragma once\n#define BBHOST_VERSION \"bloodborne-mac-bbhost-566184a\"\n")
file(READ "${BB_FEATURE_VENDOR}/tools/pc_option_messages.tsv" BB_MAC_MESSAGES)
file(WRITE "${BB_FEATURE_GEN}/pc_option_messages.inc" "R\"tsv(${BB_MAC_MESSAGES})tsv\"\n")
add_library(bbmac-features STATIC
    platform.cpp thunk.cpp features.cpp game_menu.cpp futex.cpp graphics.cpp gpu_fences.cpp motion_capture.cpp fog_draw_capture.cpp
    "${BB_FEATURE_SRC}/core/config.cpp" "${BB_FEATURE_SRC}/core/sfo.cpp"
    "${BB_FEATURE_SRC}/engine/frame_rate.cpp" "${BB_FEATURE_SRC}/engine/frame_tick.cpp"
    "${BB_FEATURE_SRC}/engine/params.cpp" "${BB_FEATURE_SRC}/engine/paramdef.cpp"
    "${BB_FEATURE_SRC}/engine/event_flags.cpp" "${BB_FEATURE_SRC}/engine/player_data.cpp"
    "${BB_FEATURE_SRC}/engine/world_chr.cpp" "${BB_FEATURE_SRC}/engine/lua_events.cpp"
    "${BB_FEATURE_SRC}/engine/image_text.cpp" "${BB_FEATURE_SRC}/engine/debug_menu.cpp"
    "${BB_FEATURE_SRC}/engine/patch_manifest.cpp" "${BB_FEATURE_SRC}/engine/menu_assets.cpp"
    "${BB_FEATURE_SRC}/engine/menu_memory.cpp"
    "${BB_FEATURE_SRC}/decomp/events/flag_store.cpp"
    "${BB_FEATURE_SRC}/host/plugins.cpp" "${BB_FEATURE_SRC}/gcn/container.cpp"
    "${BB_FEATURE_SRC}/hle/http.cpp"
    "${BB_FEATURE_SRC}/net/http.cpp" "${BB_FEATURE_SRC}/net/account.cpp" "${BB_FEATURE_SRC}/replay/json.cpp"
    signature.cpp
    "${BB_FEATURE_VENDOR}/third_party/monocypher/monocypher.c"
    "${BB_FEATURE_VENDOR}/third_party/monocypher/monocypher-ed25519.c")
target_include_directories(bbmac-features PUBLIC "${BB_ROOT}" "${BB_FEATURE_SRC}" "${BB_FEATURE_VENDOR}/include"
    PRIVATE "${BB_FEATURE_GEN}" "${BB_FEATURE_VENDOR}/third_party/monocypher")
target_link_libraries(bbmac-features PUBLIC SDL3::SDL3-static "-lz")
target_compile_definitions(bbmac-features PRIVATE BBHOST_HAVE_CURL)
target_link_libraries(bbmac-features PUBLIC "-lcurl")
target_compile_options(bbmac-features PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-format)
set_source_files_properties("${BB_FEATURE_SRC}/engine/frame_rate.cpp"
    PROPERTIES COMPILE_OPTIONS "-ffp-contract=off;-fno-fast-math")
set_source_files_properties(fog_draw_capture.cpp PROPERTIES COMPILE_OPTIONS "-fno-omit-frame-pointer")

file(GLOB BB_MAC_THD_SOURCES "${BB_ROOT}/gpu/bbnet/thd/*.cpp")
add_library(bbmac-network STATIC network.cpp account.mm ${BB_MAC_THD_SOURCES})
target_compile_options(bbmac-network PRIVATE "$<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>")
target_include_directories(bbmac-network PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/network_compat"
    "${BB_ROOT}/gpu/shadps4" "${BB_FEATURE_VENDOR}/third_party")
target_link_libraries(bbmac-network PUBLIC bbmac-features "-lcurl" "-framework Security" "-framework Foundation")

foreach(name IN ITEMS randomizer boss_rush mutators debug_menu)
    add_library(bb-plugin-${name} MODULE "${BB_FEATURE_VENDOR}/plugins/${name}/${name}.cpp")
    target_include_directories(bb-plugin-${name} PRIVATE "${BB_FEATURE_VENDOR}/include" "${BB_FEATURE_GEN}")
    set_target_properties(bb-plugin-${name} PROPERTIES PREFIX "" SUFFIX ".dylib" OUTPUT_NAME "${name}"
        CXX_VISIBILITY_PRESET hidden LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
endforeach()
file(READ "${BB_FEATURE_VENDOR}/plugins/debug_menu/font14.bin" BB_FONT14_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," BB_FONT14_BYTES "${BB_FONT14_HEX}")
file(WRITE "${BB_FEATURE_GEN}/debug_font14.inc" "${BB_FONT14_BYTES}\n")
file(READ "${BB_FEATURE_VENDOR}/plugins/debug_menu/strings_en.tsv" BB_DEBUG_MENU_EN)
file(WRITE "${BB_FEATURE_GEN}/debug_menu_strings_en.inc" "R\"tsv(${BB_DEBUG_MENU_EN})tsv\"\n")
