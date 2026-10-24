#pragma once

#include <cstdint>
#include <cstddef>

// PS4 userland is SysV x86-64. Linux hosts already use that ABI.
// Windows x64 is a different register convention, so HLE entry points that
// the eboot calls must be annotated SysV when building with Clang.
#define GUEST_ABI

using GuestFn = int64_t(GUEST_ABI*)();
using GuestStart = void(GUEST_ABI*)(void* stack, void* atexit_fn);
using GuestInit = void(GUEST_ABI*)();
