// The game's functions as our source (docs/decomp.md): a decompiled
// function takes the game's place at its entry. The whole instructions there
// become a jump to ours, and a trampoline - those instructions, re-aimed to
// run from there (decomp/insn.h), then a jump back - keeps the game's own
// version callable, for a compare run and for ours to lean on. Every
// function is registered here: one list, switched off by name, reported at
// exit.
//
// Each area of the engine keeps its functions in a folder, decomp/<area>/,
// and adds them with decomp_add() from its decomp_<name>_add(), which
// decomp/areas.cpp calls before decomp_install() runs (hle/runtime.cpp).
#pragma once

#include "guest_abi.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

struct ElfImage;

enum class DecompKind {
    // Entered straight from the game's code, on its thread as the game left
    // it: its FS base, its stack. Ours touches the game's memory and its own
    // globals and nothing of the host's - no thread-locals (FS is the
    // game's), locks, logging or allocation - and is built without the stack
    // protector, whose canary lives behind FS (DECOMP_LEAF). It costs what
    // the game's version cost.
    Leaf,
    // Through the FS-switching thunk (core/thunk.h), like an HLE entry: ours
    // may do anything host code does.
    Hosted,
    // Hosted, and handed the guest's rsp and rbp after its own three
    // arguments (thunk_wrap_capture_frame): *rsp is the return address, for a
    // function that must know which of its callers it is serving.
    HostedFrame,
};

// What a compare run counts, from any thread.
struct DecompCompare {
    std::atomic<std::uint64_t> calls{0}, differ{0};
};

struct DecompFunction {
    const char* name;           // the game's name where the binary gives one, else sub_<address>
    const char* area;           // its folder: events, player, items, talk, chalice, camera, ...
    std::uint64_t bn;           // the entry, as Binary Ninja addresses it
    const std::uint8_t* entry;  // the whole instructions expected there: at least 5 bytes, none a
    std::size_t entry_len;      //   branch target (rip-relative ones and relative branches are re-aimed)
    void* ours;                 // GUEST_ABI, the game's signature
    DecompKind kind;
    void** original = nullptr;  // receives the trampoline into the game's version
    // A Hosted stand-in for compare runs (BBHOST_DECOMP_COMPARE=1): the game's
    // version does the work and ours has to agree with it, into `counts`.
    void* compare = nullptr;
    DecompCompare* counts = nullptr;
    // A line of its own at exit (what it did), when placed.
    void (*report)() = nullptr;
    // For one whose body ours mirrors as a whole: the body at the entry is
    // still the one it was written against (patches it knows of aside), or
    // it is refused like a changed entry.
    bool (*body_ok)(const std::uint8_t* entry) = nullptr;
};

void decomp_add(const DecompFunction& fn);

// Places every function added, once the eboot's SHA-256 is 1.09's:
//   BBHOST_DECOMP=0              none: the game's code throughout
//   BBHOST_DECOMP_OFF=name,...   these stay the game's
//   BBHOST_DECOMP_COMPARE=1      those with a compare run it instead of ours
void decomp_install(ElfImage* image);
bool decomp_comparing();
// Whether a function on the list took the game's place, by its name: 0 not,
// 1 ours, 2 its compare stand-in. After decomp_install.
int decomp_placed(const char* name);
// The exit report: what is ours, and what a compare run found.
void decomp_report();

// A Binary Ninja address as the guest sees it, once decomp_install has run.
std::uint64_t decomp_guest(std::uint64_t bn);
// The same for a leaf, which calls nothing of the host's: decomp_install
// fixes the slide before any of ours can run.
extern std::uint64_t g_decomp_slide;
inline std::uint64_t decomp_leaf_guest(std::uint64_t bn) { return g_decomp_slide + (bn - 0x400000); }

// Every area's functions added to the list (decomp/areas.cpp): hle/runtime.cpp
// calls it once, before decomp_install.
void decomp_add_areas();

// A leaf's entry points. The file holding them is built with
// -fno-stack-protector as well (CMakeLists.txt), for helpers not inlined.
#define DECOMP_LEAF GUEST_ABI __attribute__((no_stack_protector))
