#pragma once

// What a text box holds while it is typed into, and which characters it
// takes: the editing behind host_text_entry_* (host/window.h), kept apart from
// SDL so it can be tested on its own (tests/text_entry_test.cpp).

#include <cstdint>
#include <string>

// The characters a box takes. All three of the game's text dialogs ask for
// basic Latin (SCE_IME_TYPE_BASIC_LATIN), and on the console that means a
// keyboard of printable ASCII and nothing else: an accented letter from a PC
// layout, or kana an IME composed, is something the game never received from
// it - and nothing the box's font could draw.
enum class TextCharset : int {
    Any,         // anything printable: the port's own fields
    BasicLatin,  // printable ASCII, U+0020-U+007E
    Glyph,       // a chalice glyph: 2-9 and a-z less l and o, letters lowercased
};

// Whether a box of this charset takes ASCII alone - an IME has nothing to
// compose for it.
inline bool text_charset_ascii(TextCharset cs) { return cs != TextCharset::Any; }

// The character `cp` as a box of `cs` takes it: the code point to insert,
// folded where the box has a form of it (an IME's full-width A is A, a
// glyph's A is a), or 0 when the box leaves it out. Control characters - a
// pasted line's end, a tab - are always left out.
std::uint32_t text_entry_accept(TextCharset cs, std::uint32_t cp);

struct TextEntry {
    std::string text;         // UTF-8
    unsigned max_chars = 32;  // characters, not bytes
    TextCharset charset = TextCharset::Any;
    // The starting text is only a suggestion (the name box's player.name):
    // it is shown selected, and the first character typed or pasted replaces
    // it, Backspace or Delete clears it, and Enter on its own accepts it. A
    // caret key keeps it as typed text.
    bool selected = false;

    // The starting text goes through the charset and the length like typing.
    void begin(const char* initial_utf8, unsigned max, TextCharset cs, bool select_initial);
    // Typed or pasted text: each character through the charset, the rest
    // dropped once the box is full.
    void insert(const char* utf8);
    // Backspace: the selected suggestion, else the last character.
    void backspace();
    // Delete: the caret is always at the end, so only a selection goes.
    void erase_selection();
    // A caret key: the suggestion stays, as text typed.
    void keep() { selected = false; }
    unsigned length() const;  // in characters
};
