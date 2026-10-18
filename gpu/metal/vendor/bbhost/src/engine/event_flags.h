// Event flags: the world's state as numbered
// bits - bosses killed, doors opened, NPC steps, and the online state the
// event scripts read (2000-2022, written from the session each frame) - kept
// by SprjEventFlagMan (the singleton slot at Binary Ninja 0x593b100). The
// host's reader and writer for plugins, over the store's layout as
// decomp/events/flag_store.h writes it down (the game's own four flag
// functions are ours there), read without trusting the game's memory: a
// plugin may ask before the store exists. The first read on the main thread
// is checked against the game's own getter (a line either way).
//
// Changes: while watching (a plugin's on_event_flag, or
// BBHOST_EVENT_FLAG_LOG=1 to log each), every flag a setter flips is taken
// from decomp's ring once a frame, on the main thread.
#pragma once

#include <cstddef>
#include <cstdint>

struct ElfImage;

void event_flags_install(ElfImage* image);
// False when the manager or the flag's block does not exist (yet).
bool event_flag_get(std::uint32_t id, bool* value);
bool event_flag_set(std::uint32_t id, bool value);
// Several flags at once, out[i] for ids[i], with one walk of the store's map
// per block they are in - event_flag_get walks it for each, a few dozen
// guarded reads (ReadProcessMemory on Windows) - for a caller that reads a
// handful every frame (engine/rebirth.cpp). False when any could not be read;
// out is then partly written.
bool event_flags_get(const std::uint32_t* ids, std::size_t n, bool* out);
// From the frame-time manager's hook: the changes since the last frame, and
// the one-time check against the game's getter once the manager exists.
void event_flags_tick();
