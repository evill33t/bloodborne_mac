#pragma once

// Fixed ticks: a function of the game's that advances its state by one 30 fps
// frame a call - a counter, a ring of the last N frames, "every N frames", a
// pattern stepped once a call - runs as a 30 Hz clock at any frame rate. Its
// entry is hooked (engine_prologue_hook, no code of ours in the image); each
// frame the hook calls the original n = floor(carry + 30 dt) times, the carry
// kept, so a 60 fps frame runs it every other frame and a 20 fps frame runs it
// twice on some frames. The extra calls replay the entry's arguments
// (integers and xmm0-7) through a trampoline of the moved prologue; the last
// goes through the hook's own path. At 30 fps every frame runs it once: the
// game's own behaviour, call for call.
//
// Only for functions whose output is not shown every frame (timers, rings,
// input windows, AI decisions, network cadence): a visible motion stepped at
// 30 Hz would judder at 60.
//
// BBHOST_FIXED_TICK=<addr>:<n>[,...] (a test switch): the functions to run so,
// Binary Ninja addresses, n the bytes of whole instructions at the entry the
// hook moves (14 or more, none rip-relative). At converted rates only.

struct ElfImage;

void frame_tick_install(ElfImage* image);

// Once a frame at a converted rate, with the step the game advances by (s).
void frame_tick_frame(float step);
