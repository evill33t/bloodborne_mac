#pragma once

#include <cstdint>
#include <cstddef>

// PS4 userland uses SysV x86-64, supplied by Rosetta or the bbcpu call bridge.
#define GUEST_ABI

using GuestFn = int64_t(GUEST_ABI*)();
using GuestStart = void(GUEST_ABI*)(void* stack, void* atexit_fn);
using GuestInit = void(GUEST_ABI*)();
