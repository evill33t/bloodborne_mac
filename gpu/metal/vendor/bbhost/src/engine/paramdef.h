#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The game's own param definitions: `dvdroot_ps4/paramdef/paramdef.paramdefbnd.dcx`
// (through the mods overlay first), one PARAMDEF per param type - the field
// names, types and defaults the tables were built from. With them a param
// row is read and written by field name ("attackBasePhysics") instead of by
// offset.
//
// The files are the 64-bit PARAMDEF of format 201: a header with the field
// count, the size of a field definition (0xd0) and the param type name
// ("LOCK_CAM_PARAM_ST" - what a param file names at +0xc), then one fixed
// definition per field: its display name (UTF-16), display type (the
// primitive: s8 u8 s16 u16 s32 u32 f32 dummy8 fixstr fixstrW) and format,
// default, minimum, maximum and step, byte count, a description offset, the
// internal type (the primitive again, or the name of an enum over it:
// "MAGIC_BOOL", "SP_EFFECT_TYPE") and the internal name, which may carry a bit
// width ("flag:1") or a length ("pad[4]"). Rows are the fields in order,
// bitfields packed LSB first into units of their primitive's size while they
// fit. Checked against the live tables: a definition whose row size is not
// the table's is not used (engine/params.cpp).

struct ParamField {
    std::string name;         // internal name, without the bit width or array length
    std::string type;         // the primitive (the display type)
    std::string enum_type;    // the internal type: the primitive, or an enum's name
    std::string display;      // display name, UTF-8
    std::uint32_t offset = 0; // byte offset in the row (of the unit, for a bitfield)
    std::uint32_t bytes = 0;  // storage bytes: the element size times the count, or the bitfield's unit
    std::uint32_t count = 1;  // array length
    std::uint8_t bit = 0;     // a bitfield's first bit in its unit
    std::uint8_t bits = 0;    // a bitfield's width; 0 when the field is not one
    float def = 0.0f, min = 0.0f, max = 0.0f;
};

struct ParamDefinition {
    std::string type;          // the param type name
    std::uint32_t row_bytes = 0;
    std::vector<ParamField> fields;
    const ParamField* field(const std::string& name) const;
};

// The definition for a param type, loaded on first use; nullptr when the
// archive has none for it (or could not be read).
const ParamDefinition* paramdef_for(const std::string& param_type);
// How many definitions were loaded (0 before the first paramdef_for).
std::size_t paramdef_count();

// Reads a field of a row as text (a number; a fixstr as its bytes); empty
// when the field cannot be read.
std::string paramdef_read(const ParamField& f, const std::uint8_t* row, std::uint32_t index = 0);
// Writes a field of a row from text (a bitfield keeps its unit's other bits).
// False when the type is not writable (fixstr, fixstrW) or the index is out
// of range.
bool paramdef_write(const ParamField& f, std::uint8_t* row, const std::string& text, std::uint32_t index = 0);
