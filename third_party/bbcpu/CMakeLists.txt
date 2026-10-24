# CPU layer from bloodborne-native/bloodborne-recomp; see UPSTREAM.md.
include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG a2278f1d254e492f6a6b39f6cb5d1f5d515659dc
    GIT_SUBMODULES dependencies/zycore)
FetchContent_MakeAvailable(zydis)
enable_language(ASM)
file(GLOB BB_CPU_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cpu/*.c")
add_library(bbcpu STATIC ${BB_CPU_SOURCES} src/cpu/hostcall.S)
target_include_directories(bbcpu PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(bbcpu PUBLIC Zydis)
target_compile_features(bbcpu PRIVATE c_std_11)
target_compile_options(bbcpu PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)

if(BUILD_TESTING AND CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
    add_executable(bb-cpu-a64-tests tests/test_a64.c)
    add_test(NAME native-cpu-encodings COMMAND bb-cpu-a64-tests)
    foreach(fixture IN ITEMS jit jit_fault)
        add_executable(bb-cpu-${fixture}-tests tests/test_${fixture}.c)
        target_compile_options(bb-cpu-${fixture}-tests PRIVATE -UNDEBUG)
        target_link_libraries(bb-cpu-${fixture}-tests PRIVATE bbcpu)
    endforeach()
    add_test(NAME native-cpu-faults COMMAND bb-cpu-jit_fault-tests)
    set_tests_properties(native-cpu-faults PROPERTIES TIMEOUT 45)
    add_executable(bb-cpu-segment-tests tests/test_segments.c)
    target_compile_options(bb-cpu-segment-tests PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(bb-cpu-segment-tests PRIVATE bbcpu)
    foreach(mode IN ITEMS 0 1)
        add_test(NAME native-cpu-segments-${mode} COMMAND bb-cpu-segment-tests)
        set_tests_properties(native-cpu-segments-${mode} PROPERTIES ENVIRONMENT "BB_JIT=${mode}" TIMEOUT 15)
    endforeach()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_program(BB_CPU_CLANG clang REQUIRED)
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/jit-guest"
        COMMAND "${BB_CPU_CLANG}" -arch x86_64 -O2 -mavx -msse4.1 -mpopcnt
            -fno-builtin -fno-stack-protector -Wl,-no_pie -Wl,-image_base,0x30000000000
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/jit_guest.c" -o "${CMAKE_CURRENT_BINARY_DIR}/jit-guest"
        DEPENDS tests/jit_guest.c VERBATIM)
    add_custom_target(bb-cpu-reference ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/jit-guest")
    add_test(NAME native-cpu-differential COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../tests/test_native_cpu.py"
        "${CMAKE_CURRENT_BINARY_DIR}/jit-guest" "$<TARGET_FILE:bb-cpu-jit-tests>")
    set_tests_properties(native-cpu-differential PROPERTIES TIMEOUT 120)
endif()
