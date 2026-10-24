cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 13.0 CACHE STRING "Minimum macOS version")
endif()
project(bloodborne_mac LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Bloodborne for macOS requires macOS and Xcode command-line tools.")
endif()
enable_language(OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()
include(CTest)
add_subdirectory(gpu/metal)
find_program(BB_XCRUN xcrun REQUIRED)
# Cross-build CPU fixtures independently of the native ARM64 Metal libraries.
# Running these helpers is explicit, since Rosetta is an optional OS component.
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bb-macos-cpu-probe"
    COMMAND "${BB_XCRUN}" clang -arch x86_64 -std=c11 -O2 -Wall -Wextra -Werror
        "${CMAKE_SOURCE_DIR}/tools/macos_cpu_probe.c" -o "${CMAKE_BINARY_DIR}/bb-macos-cpu-probe"
    DEPENDS tools/macos_cpu_probe.c VERBATIM)
add_custom_target(bb-macos-cpu-probe-build ALL DEPENDS "${CMAKE_BINARY_DIR}/bb-macos-cpu-probe")
if(BUILD_TESTING)
    add_executable(bb-macos-memory-tests tests/test_macos_memory.c src/runtime_errno.c)
    target_compile_features(bb-macos-memory-tests PRIVATE c_std_11)
    target_compile_options(bb-macos-memory-tests PRIVATE -Wall -Wextra -Werror)
    add_test(NAME macos-guest-memory COMMAND bb-macos-memory-tests)
    set_tests_properties(macos-guest-memory PROPERTIES TIMEOUT 30)
    add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bb-macos-memory-tests-x86_64"
        COMMAND "${BB_XCRUN}" clang -arch x86_64 -std=c11 -O2 -Wall -Wextra -Werror
            "${CMAKE_SOURCE_DIR}/tests/test_macos_memory.c" "${CMAKE_SOURCE_DIR}/src/runtime_errno.c"
            -o "${CMAKE_BINARY_DIR}/bb-macos-memory-tests-x86_64"
        DEPENDS tests/test_macos_memory.c src/runtime_memory.c src/runtime_errno.c src/runtime.h src/runtime_gpu_memory.h
            src/checkpoint/memory.h VERBATIM)
    add_custom_target(bb-macos-memory-tests-x86-build ALL
        DEPENDS "${CMAKE_BINARY_DIR}/bb-macos-memory-tests-x86_64")
endif()
message(STATUS "Building native Metal development targets; packaging/macos/runtime builds the x86-64 game process.")
