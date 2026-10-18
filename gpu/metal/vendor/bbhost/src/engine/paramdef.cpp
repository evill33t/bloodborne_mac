#include "engine/paramdef.h"

#include "gcn/container.h"
#include "hle/fs.h"
#include "log.h"

#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

namespace {

constexpr const char* kArchive = "/app0/dvdroot_ps4/paramdef/paramdef.paramdefbnd.dcx";
constexpr std::uint16_t kFormat64 = 201;   // the 64-bit PARAMDEF this build ships
constexpr std::uint16_t kFieldBytes = 0xd0;

std::once_flag g_once;
std::map<std::string, ParamDefinition> g_defs;  // by param type; written once, under g_once

template <typename T>
bool get(const std::vector<std::uint8_t>& b, std::size_t at, T* out) {
    if (at + sizeof(T) > b.size()) return false;
    std::memcpy(out, b.data() + at, sizeof(T));
    return true;
}

// A fixed-size NUL-terminated byte string.
std::string fixed(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n && at + i < b.size() && b[at + i]; ++i) s.push_back(static_cast<char>(b[at + i]));
    return s;
}

// A fixed-size NUL-terminated UTF-16LE string, as UTF-8.
std::string fixed_wide(const std::vector<std::uint8_t>& b, std::size_t at, std::size_t units) {
    std::string s;
    for (std::size_t i = 0; i < units && at + 2 * i + 1 < b.size(); ++i) {
        const std::uint32_t c = b[at + 2 * i] | (static_cast<std::uint32_t>(b[at + 2 * i + 1]) << 8);
        if (!c) break;
        if (c < 0x80) {
            s.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            s.push_back(static_cast<char>(0xc0 | (c >> 6)));
            s.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {  // the BMP; a surrogate half comes out as its own (invalid) code point
            s.push_back(static_cast<char>(0xe0 | (c >> 12)));
            s.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            s.push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    return s;
}

std::uint32_t element_bytes(const std::string& type) {
    if (type == "s8" || type == "u8" || type == "dummy8" || type == "fixstr") return 1;
    if (type == "s16" || type == "u16" || type == "fixstrW") return 2;
    if (type == "s32" || type == "u32" || type == "f32") return 4;
    return 0;
}

bool parse_one(const std::vector<std::uint8_t>& b, ParamDefinition& d) {
    std::uint16_t count = 0, field_bytes = 0, format = 0;
    std::uint64_t fields_at = 0;
    if (!get(b, 0x08, &count) || !get(b, 0x0a, &field_bytes) || !get(b, 0x2e, &format) || !get(b, 0x30, &fields_at)) return false;
    if (format != kFormat64 || field_bytes != kFieldBytes) return false;
    d.type = fixed(b, 0x0c, 0x20);
    if (d.type.empty() || fields_at + static_cast<std::uint64_t>(count) * field_bytes > b.size()) return false;
    struct Unit {
        bool open = false;
        std::uint32_t offset = 0, bytes = 0, used = 0;
    } unit;
    std::uint32_t offset = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t at = static_cast<std::size_t>(fields_at) + static_cast<std::size_t>(i) * field_bytes;
        ParamField f;
        f.display = fixed_wide(b, at, 0x20);
        std::uint32_t byte_count = 0;
        get(b, at + 0x50, &f.def);
        get(b, at + 0x54, &f.min);
        get(b, at + 0x58, &f.max);
        get(b, at + 0x64, &byte_count);
        // The primitive is the display type; the internal type is often the
        // name of an enum over it ("MAGIC_BOOL", "SP_EFFECT_TYPE").
        f.type = fixed(b, at + 0x40, 8);
        f.enum_type = fixed(b, at + 0x70, 0x20);
        std::string name = fixed(b, at + 0x90, 0x20);
        // "name:bits" is a bitfield, "name[n]" an array.
        std::uint32_t bits = 0;
        if (const std::size_t colon = name.find(':'); colon != std::string::npos) {
            bits = static_cast<std::uint32_t>(std::strtoul(name.c_str() + colon + 1, nullptr, 10));
            name.resize(colon);
        }
        if (const std::size_t bracket = name.find('['); bracket != std::string::npos) {
            f.count = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(name.c_str() + bracket + 1, nullptr, 10)));
            name.resize(bracket);
        }
        while (!name.empty() && name.back() == ' ') name.pop_back();
        f.name = name;
        const std::uint32_t esize = element_bytes(f.type);
        if (bits && esize) {
            // Into the open unit while it has room and is the same width.
            if (!unit.open || unit.bytes != esize || unit.used + bits > esize * 8) {
                if (unit.open) offset += unit.bytes;
                unit = Unit{true, offset, esize, 0};
            }
            f.offset = unit.offset;
            f.bytes = esize;
            f.bit = static_cast<std::uint8_t>(unit.used);
            f.bits = static_cast<std::uint8_t>(bits);
            unit.used += bits;
        } else {
            if (unit.open) {
                offset += unit.bytes;
                unit.open = false;
            }
            f.offset = offset;
            f.bytes = byte_count ? byte_count : esize * f.count;
            offset += f.bytes;
        }
        d.fields.push_back(std::move(f));
    }
    if (unit.open) offset += unit.bytes;
    d.row_bytes = offset;
    return true;
}

void load() {
    const std::string path = hle_fs_map_path(kArchive);
    std::vector<std::uint8_t> raw;
    if (path.empty() || !gcn::read_file(path, raw)) {
        host_log("paramdef: cannot read %s", kArchive);
        return;
    }
    std::string err;
    const std::vector<std::uint8_t> bnd = gcn::dcx_decompress(raw, &err);
    std::vector<gcn::BundleEntry> entries;
    if (!gcn::bnd4_entries(bnd, entries, &err)) {
        host_log("paramdef: %s is not a readable archive (%s)", kArchive, err.c_str());
        return;
    }
    std::size_t bad = 0;
    for (const gcn::BundleEntry& e : entries) {
        ParamDefinition d;
        if (!parse_one(e.data, d)) {
            ++bad;
            continue;
        }
        g_defs[d.type] = std::move(d);
    }
    host_log("paramdef: %zu param definitions from the game's archive%s", g_defs.size(),
             bad ? " (some entries were not the 64-bit format 201 and were left out)" : "");
}

}  // namespace

const ParamField* ParamDefinition::field(const std::string& name) const {
    for (const ParamField& f : fields) {
        if (f.name == name) return &f;
    }
    return nullptr;
}

const ParamDefinition* paramdef_for(const std::string& param_type) {
    std::call_once(g_once, load);
    const auto it = g_defs.find(param_type);
    return it == g_defs.end() ? nullptr : &it->second;
}

std::size_t paramdef_count() { return g_defs.size(); }

namespace {

std::uint32_t load_unit(const std::uint8_t* p, std::uint32_t bytes) {
    std::uint32_t v = 0;
    std::memcpy(&v, p, bytes);  // little-endian, as the tables are
    return v;
}

void store_unit(std::uint8_t* p, std::uint32_t bytes, std::uint32_t v) { std::memcpy(p, &v, bytes); }

}  // namespace

std::string paramdef_read(const ParamField& f, const std::uint8_t* row, std::uint32_t index) {
    const std::uint32_t esize = element_bytes(f.type);
    if (!row || !esize || index >= f.count) return "";
    char buf[64];
    if (f.bits) {
        const std::uint32_t unit = load_unit(row + f.offset, f.bytes);
        const std::uint32_t mask = f.bits >= 32 ? 0xffffffffu : ((1u << f.bits) - 1u);
        std::snprintf(buf, sizeof(buf), "%u", (unit >> f.bit) & mask);
        return buf;
    }
    if (f.type == "fixstr") return std::string(reinterpret_cast<const char*>(row + f.offset), strnlen(reinterpret_cast<const char*>(row + f.offset), f.bytes));
    if (f.type == "fixstrW") return "";
    const std::uint8_t* p = row + f.offset + index * esize;
    if (f.type == "f32") {
        float v;
        std::memcpy(&v, p, 4);
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
    } else if (f.type == "s8") {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(static_cast<std::int8_t>(*p)));
    } else if (f.type == "u8" || f.type == "dummy8") {
        std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(*p));
    } else if (f.type == "s16" || f.type == "u16") {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        if (f.type == "s16") std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(static_cast<std::int16_t>(v)));
        else std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(v));
    } else {
        std::uint32_t v;
        std::memcpy(&v, p, 4);
        if (f.type == "s32") std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(v));
        else std::snprintf(buf, sizeof(buf), "%u", v);
    }
    return buf;
}

bool paramdef_write(const ParamField& f, std::uint8_t* row, const std::string& text, std::uint32_t index) {
    const std::uint32_t esize = element_bytes(f.type);
    if (!row || !esize || index >= f.count || f.type == "fixstr" || f.type == "fixstrW") return false;
    if (f.bits) {
        const std::uint32_t mask = f.bits >= 32 ? 0xffffffffu : ((1u << f.bits) - 1u);
        const std::uint32_t v = static_cast<std::uint32_t>(std::strtoll(text.c_str(), nullptr, 0)) & mask;
        std::uint32_t unit = load_unit(row + f.offset, f.bytes);
        unit = (unit & ~(mask << f.bit)) | (v << f.bit);
        store_unit(row + f.offset, f.bytes, unit);
        return true;
    }
    std::uint8_t* p = row + f.offset + index * esize;
    if (f.type == "f32") {
        const float v = std::strtof(text.c_str(), nullptr);
        std::memcpy(p, &v, 4);
    } else {
        const std::int64_t v = std::strtoll(text.c_str(), nullptr, 0);
        const std::uint32_t u = static_cast<std::uint32_t>(v);
        std::memcpy(p, &u, esize);  // the low bytes, little-endian
    }
    return true;
}
