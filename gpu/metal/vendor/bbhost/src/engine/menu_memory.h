#pragma once

// Room for the port's option pages in Scaleform's memory.
//
// Scaleform runs on two Dantelion heaps the game carves from its MENU heap
// when it starts (0x2359d50): 18 MiB for movies' large blocks and 34 MiB for
// the 4 KiB pages every small block lives in. MENU itself is 82 MiB, from
// SprjMemory's size table (0x4b36d40, row 2 - "Master_ps4_flexible" - entry
// 9). The option movie keeps every section alive at once and only shows one,
// so each section the port adds (engine/menu_assets.cpp adds six) costs
// what a stock one does: in game, where the HUD and the world's movies
// already hold ~13 MiB of pages, opening System took 11 MiB where the stock
// movie takes 4, and opening a section then ran the page heap out - a null
// from the allocator, and the next constructor wrote through it.
//
// This grows the three sizes before the game reads them: the page heap and
// the large-block heap by 64 and 32 MiB, and MENU by their sum, so what MENU
// has spare is unchanged. The memory is flexible memory, mapped when the
// heap is created; a PC has it to spare. Hash-gated like every address
// patch, and each site is checked for the value it replaces.

struct ElfImage;

void menu_memory_install(ElfImage* image);
