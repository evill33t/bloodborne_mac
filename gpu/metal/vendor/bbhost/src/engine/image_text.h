#pragma once

// Text in the game's image replaced by other text (the plugin API's
// replace_text, version 11): every reference to a string - each rip-relative
// lea that loads its address, each relocated pointer to it in the image's
// data - is pointed at a copy of the new text in the image's tail
// (core/elf.h). The string itself stays as it was, so whatever measures or
// compares it in place sees no change.
//
// A string the code reaches some other way (an offset added to another
// address) keeps its text. Run before the game does: code pages are written
// in place.

#include <cstddef>
#include <cstdint>
#include <string>

struct ElfImage;

struct ImageText {
    std::uint64_t at = 0;  // the game's string, by its ELF address
    std::u16string text;   // what its references name instead
};

struct ImageTextResult {
    std::size_t strings = 0;       // strings with a reference moved
    std::size_t leas = 0;          // instructions changed
    std::size_t pointers = 0;      // pointers changed
    std::size_t unreferenced = 0;  // strings nothing was found to name
    std::size_t rejected = 0;      // not in the image's read-only data, empty, or no room left
};

ImageTextResult image_replace_text(ElfImage* image, const ImageText* items, std::size_t count);
