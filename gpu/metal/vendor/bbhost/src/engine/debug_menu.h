#pragma once

// The developers' debug menu, restored the way the shadPS4 community patch
// "Restore Debug Menu" (Whitehawkx, auser1337; Bloodborne 1.09) does it, when
// the Debug Menu plugin (plugins/debug_menu) is on - read once, at start.
//
// The shipped game keeps the debug system and its menu and leaves them off:
//
//   - SprjInitStuff_mb builds the debug object at 0x58b3570 with a `false`
//     (`mov ecx, 0` at 0x2418058) - the patch passes true;
//   - sub_23abe10 then sets that object's two state flags through
//     sub_13b5350 / sub_13b5360 with 0 and 1 - the patch swaps the two calls
//     (the low byte of each rel32), so they are set 1 and 0;
//   - the font manager (sub_2199e20) never loads a debug font. The patch
//     reroutes the tail of that function through its allocation-failure block,
//     which it rewrites into a call to sub_136dc70(manager, 0,
//     "adhoc:/font/DbgFont14h.ccm", "adhoc:/font/DbgFont14h.tpf") - the two
//     paths written as UTF-16 over two assert-message strings its `lea`s are
//     re-aimed at - and flips the `je` in sub_136d8e0 so the debug font
//     shaders, which are not on the disc, are not asked for.
//
// The fonts are not on the disc either: DbgFont14h.ccm and .tpf have to be in
// dvdroot_ps4/adhoc/font, and the game crashes at boot without them. The
// plugin makes both (plugins/debug_menu/debug_font.hpp) into its file
// overlay, where a copy in the player's mods folder wins; the patch still
// refuses when they are missing.
//
// With the patch the menu opens on the touchpad's left side, which is also
// Gestures, so the two opened together. The Debug Menu key (`) does not press
// the touchpad: it toggles the menu itself (debug_menu_toggle).

#include <cstdint>

struct ElfImage;

// After the plugins' image phase (main.cpp): the plugin's font is written by
// then.
void debug_menu_install(ElfImage* image);

// Whether this run's image has the menu patched in.
bool debug_menu_active();

// Opens the menu if it is closed and closes it otherwise. FD4DebugMenu's
// update (sub_13a18f0) keeps its mode in an int at +0x30 of the manager the
// global at 0x58b3570 points to - 0 closed, 1 open and taking input, and the
// touchpad cycles 0 -> 1 -> 2 -> 3 - and derives everything else from it each
// frame, so writing it is the whole of opening or closing. Called on the
// game's pad-read thread.
void debug_menu_toggle();

// Open and taking input (mode 1). While it is, the game ignores the player's
// pad - the character stands still - and the menu reads it instead, with the
// Japanese convention whatever the region: Circle opens a node, Cross goes
// back, the d-pad moves. So Enter and Backspace work it as they do any menu
// (engine/menu_pointer.h's menu_confirm_button), in the world as well.
bool debug_menu_open();
