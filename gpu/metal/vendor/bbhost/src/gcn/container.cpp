#include "gcn/container.h"

#include <zlib.h>

#include <cstdio>
#include <cstring>

namespace gcn {
namespace {

std::uint32_t rd32le(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}
std::uint32_t rd32be(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t rd64le(const std::uint8_t* p) {
    std::uint64_t v;
    std::memcpy(&v, p, 8);
    return v;
}

std::size_t find(const std::vector<std::uint8_t>& b, const char* needle, std::size_t from = 0) {
    const std::size_t n = std::strlen(needle);
    for (std::size_t i = from; i + n <= b.size(); ++i) {
        if (std::memcmp(b.data() + i, needle, n) == 0) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

}  // namespace

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? static_cast<std::size_t>(n) : 0);
    const bool ok = out.empty() || std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

std::vector<std::uint8_t> dcx_decompress(const std::vector<std::uint8_t>& data, std::string* error) {
    if (data.size() < 0x2c || std::memcmp(data.data(), "DCX\0", 4) != 0) {
        return data;
    }
    if (std::memcmp(data.data() + 0x28, "DFLT", 4) != 0) {
        if (error) *error = "unsupported DCX format";
        return {};
    }
    const std::size_t dca = find(data, "DCA");
    if (dca == static_cast<std::size_t>(-1) || dca + 8 > data.size()) {
        if (error) *error = "DCX without DCA";
        return {};
    }
    const std::uint32_t dca_size = rd32be(data.data() + dca + 4);
    const std::size_t comp_off = dca + dca_size;
    // DCS block carries the uncompressed size (big-endian) at +4.
    const std::size_t dcs = find(data, "DCS");
    std::size_t out_size = dcs != static_cast<std::size_t>(-1) ? rd32be(data.data() + dcs + 4) : 0;
    if (out_size == 0) {
        out_size = data.size() * 8;
    }
    std::vector<std::uint8_t> out(out_size);
    uLongf dest_len = out_size;
    const int rc = uncompress(out.data(), &dest_len, data.data() + comp_off, data.size() - comp_off);
    if (rc != Z_OK) {
        if (error) *error = "zlib error " + std::to_string(rc);
        return {};
    }
    out.resize(dest_len);
    return out;
}

std::vector<std::uint8_t> dcx_compress(const std::vector<std::uint8_t>& raw, int level) {
    // The 0x4c-byte DFLT header the game's own files carry ("DCX", "DCS" with
    // the uncompressed and compressed sizes big-endian at 0x1c / 0x20, "DCP"
    // DFLT level 9, "DCA" 8), then the zlib stream.
    static constexpr std::uint8_t kHeader[0x4c] = {
        'D', 'C', 'X', 0, 0, 1, 0, 0, 0, 0, 0, 0x18, 0, 0, 0, 0x24, 0, 0, 0, 0x44, 0, 0, 0, 0x4c,
        'D', 'C', 'S', 0, 0, 0, 0, 0, 0, 0, 0, 0, 'D', 'C', 'P', 0, 'D', 'F', 'L', 'T', 0, 0, 0, 0x20,
        9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 'D', 'C', 'A', 0, 0, 0, 0, 8};
    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> out(sizeof(kHeader) + bound);
    std::memcpy(out.data(), kHeader, sizeof(kHeader));
    if (compress2(out.data() + sizeof(kHeader), &bound, raw.data(), static_cast<uLong>(raw.size()), level) != Z_OK) return {};
    out.resize(sizeof(kHeader) + bound);
    const auto be32 = [&](std::size_t at, std::uint32_t v) {
        out[at] = static_cast<std::uint8_t>(v >> 24);
        out[at + 1] = static_cast<std::uint8_t>(v >> 16);
        out[at + 2] = static_cast<std::uint8_t>(v >> 8);
        out[at + 3] = static_cast<std::uint8_t>(v);
    };
    be32(0x1c, static_cast<std::uint32_t>(raw.size()));
    be32(0x20, static_cast<std::uint32_t>(bound));
    return out;
}

bool bnd4_read(const std::vector<std::uint8_t>& b, Bnd4Archive& out, std::string* error) {
    out = Bnd4Archive{};
    if (b.size() < 0x40 || std::memcmp(b.data(), "BND4", 4) != 0) {
        if (error) *error = "not a BND4";
        return false;
    }
    out.head.assign(b.begin(), b.begin() + 0x40);
    out.unicode = b[0x30] != 0;
    out.entry_size = rd64le(b.data() + 0x20);
    const std::uint32_t n = rd32le(b.data() + 0xc);
    const std::uint64_t table = rd64le(b.data() + 0x10);
    if (out.entry_size < 0x24 || table != 0x40) {
        if (error) *error = "an entry layout this does not write";
        return false;
    }
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint64_t e = table + k * out.entry_size;
        if (e + out.entry_size > b.size()) {
            if (error) *error = "truncated entry table";
            return false;
        }
        const std::uint8_t* p = b.data() + e;
        Bnd4File f;
        f.flags = rd64le(p);
        const std::uint64_t size = rd64le(p + 8);
        const std::uint32_t data_off = rd32le(p + 0x18);
        f.id = rd32le(p + 0x1c);
        const std::uint32_t name_off = rd32le(p + 0x20);
        std::size_t end = name_off;
        if (out.unicode) {
            while (end + 1 < b.size() && (b[end] || b[end + 1])) end += 2;
        } else {
            while (end < b.size() && b[end]) ++end;
        }
        if (name_off > b.size() || end > b.size() || data_off + size > b.size()) {
            if (error) *error = "an entry outside the archive";
            return false;
        }
        f.name.assign(b.begin() + name_off, b.begin() + static_cast<std::ptrdiff_t>(end));
        f.data.assign(b.begin() + data_off, b.begin() + data_off + size);
        out.files.push_back(std::move(f));
    }
    return true;
}

std::vector<std::uint8_t> bnd4_pack(const Bnd4Archive& a) {
    const std::size_t base = 0x40 + a.files.size() * a.entry_size;
    std::vector<std::uint8_t> names;
    std::vector<std::uint64_t> name_off;
    for (const Bnd4File& f : a.files) {
        name_off.push_back(base + names.size());
        names.insert(names.end(), f.name.begin(), f.name.end());
        names.push_back(0);
        if (a.unicode) names.push_back(0);
    }
    const std::size_t data_start = base + names.size();
    std::vector<std::uint8_t> blobs;
    std::vector<std::uint64_t> offs;
    for (const Bnd4File& f : a.files) {
        while ((data_start + blobs.size()) % 0x10) blobs.push_back(0);
        offs.push_back(data_start + blobs.size());
        blobs.insert(blobs.end(), f.data.begin(), f.data.end());
    }
    std::vector<std::uint8_t> out = a.head;
    auto put = [&](std::vector<std::uint8_t>& b, std::size_t at, std::uint64_t v, int n) {
        for (int i = 0; i < n; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (8 * i));
    };
    put(out, 0x28, data_start, 8);
    for (std::size_t k = 0; k < a.files.size(); ++k) {
        std::vector<std::uint8_t> e(a.entry_size, 0);
        put(e, 0, a.files[k].flags, 8);
        put(e, 8, a.files[k].data.size(), 8);
        put(e, 0x10, a.files[k].data.size(), 8);
        put(e, 0x18, offs[k], 4);
        put(e, 0x1c, a.files[k].id, 4);
        put(e, 0x20, name_off[k], 4);
        out.insert(out.end(), e.begin(), e.end());
    }
    out.insert(out.end(), names.begin(), names.end());
    out.insert(out.end(), blobs.begin(), blobs.end());
    return out;
}

bool bnd4_entries(const std::vector<std::uint8_t>& b, std::vector<BundleEntry>& out, std::string* error) {
    if (b.size() < 0x40 || std::memcmp(b.data(), "BND4", 4) != 0) {
        if (error) *error = "not a BND4";
        return false;
    }
    const bool unicode = b[0x30] != 0;
    const std::uint32_t n = rd32le(b.data() + 0xc);
    const std::uint64_t hdr = rd64le(b.data() + 0x10);
    const std::uint64_t esize = rd64le(b.data() + 0x20);
    for (std::uint32_t k = 0; k < n; ++k) {
        const std::uint64_t e = hdr + k * esize;
        if (e + esize > b.size() || esize < 0x20) {
            if (error) *error = "truncated entry table";
            return false;
        }
        const std::uint8_t* p = b.data() + e;
        const std::uint64_t size = rd64le(p + 8);
        const std::uint32_t data_off = rd32le(p + 0x18);
        const std::uint32_t id = rd32le(p + 0x1c);
        const std::uint32_t name_off = rd32le(p + esize - 4);
        BundleEntry entry;
        entry.id = id;
        if (unicode) {
            for (std::size_t i = name_off; i + 1 < b.size(); i += 2) {
                const std::uint16_t c = static_cast<std::uint16_t>(b[i] | (b[i + 1] << 8));
                if (c == 0) break;
                entry.name.push_back(c < 0x80 ? static_cast<char>(c) : '?');
            }
        } else {
            for (std::size_t i = name_off; i < b.size() && b[i]; ++i) {
                entry.name.push_back(static_cast<char>(b[i]));
            }
        }
        if (data_off + size > b.size()) {
            if (error) *error = "entry data out of range: " + entry.name;
            return false;
        }
        entry.data.assign(b.begin() + data_off, b.begin() + data_off + size);
        out.push_back(std::move(entry));
    }
    return true;
}

bool shader_code(const std::vector<std::uint8_t>& container, ShaderCode& out) {
    // ShaderFileHeader: 'Shdr', u16 major, u16 minor, u8 type, u8 headerSizeInDW,
    // then the gnmx stage header of headerSizeInDW dwords (registers, input
    // semantic tables), then the code. The ShaderBinaryInfo footer ('OrbShdr',
    // u8 version, then flags:8 | length:24) gives the code length in bytes; an
    // input-usage-slot table and padding sit between the code and the footer.
    const std::size_t i = find(container, "Shdr");
    if (i == static_cast<std::size_t>(-1) || i + 16 > container.size()) {
        return false;
    }
    const std::size_t j = find(container, "OrbShdr", i);
    if (j == static_cast<std::size_t>(-1) || j + 12 > container.size()) {
        return false;
    }
    const std::size_t header_dw = container[i + 9];
    const std::size_t start = i + 16 + header_dw * 4;
    const std::uint32_t length = rd32le(container.data() + j + 8) >> 8;
    if (start + length > j || length == 0 || (length & 3)) {
        return false;
    }
    out.offset = start;
    out.words.resize(length / 4);
    std::memcpy(out.words.data(), container.data() + start, length);
    out.footer = j;
    out.type = container[i + 8];
    return true;
}

}  // namespace gcn
