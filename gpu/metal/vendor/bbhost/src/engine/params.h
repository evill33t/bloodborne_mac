// The param repository as ours: the game's SoloParamRepository
// (singleton slot at Binary Ninja 0x5940340) keeps 63 slots of resource caps
// (repo + 0x70 + slot * 0x48: a count, then up to eight cap pointers); a
// cap's name is the std::wstring at +0x18 and its file is at
// *(*(cap + 0x70) + 0x70), the .param as read: row count u16 at +0xa, the
// type name at +0xc, format bytes at +0x2d/+0x2e, then the row records at
// +0x40 (0x18 bytes: id u32, data offset u64 at +8, name offset) and the
// rows the offsets point at. The camera's update (0x183ac60) reaches
// LockCamParam this way, through an id index the game builds behind the
// file; ours walks the records.
//
// Once a frame on the main thread (params_tick) the repository is walked:
// every table is named in the log the first time it is seen, our row
// lookup (params_row) serves it, and a row override file from the mods
// overlay, <mods>/params/<Name>.toml, is written into its rows in place -
// no repacking of the archive, and reapplied when the game reloads the
// table (a new buffer).
//
//   [row.100]            # a row by id; [row.all] every row
//   camDistTarget = 6.5  # a field by the game's own name (engine/paramdef.h)
//   pad[2] = 1           # an array field's element
//   f32_0x14 = 60.0      # or <type>_<offset>: f32, i32, u32, i16, u16, i8, u8
//
// BBHOST_PARAM_DUMP="Table:id,..." prints rows by field name.
#pragma once

#include <cstddef>
#include <cstdint>

struct ElfImage;
struct ParamDefinition;

void params_install(ElfImage* image);
// Once a frame from the frame-time manager's hook, on the main thread.
void params_tick();
// The row's data for a table by its repository name ("LockCamParam") and a
// row id, or nullptr when the table is not loaded or has no such row.
// `bytes` receives the row size when the table's records give one.
void* params_row(const char* table, std::uint32_t id, std::size_t* bytes);
// A table's row ids in order: up to `max` into ids (may be null), the row
// count into *count. False when the table is not loaded.
bool params_ids(const char* table, std::uint32_t* ids, std::size_t max, std::size_t* count);
// The table's definition (field names and offsets, engine/paramdef.h) when
// its rows are laid out as it says; nullptr when not, or not loaded.
const ParamDefinition* params_definition(const char* table);
// The exit report: tables seen, overrides applied.
void params_report();
