#include "core/sfo.h"

#include <cstdio>
#include <cstring>
#include <vector>

bool sfo_read(const std::string& path, std::map<std::string, SfoValue>* out, std::string* error) {
    out->clear();
    std::FILE* f = path.empty() ? nullptr : std::fopen(path.c_str(), "rb");
    if (!f) {
        *error = "not found";
        return false;
    }
    std::vector<std::uint8_t> b;
    std::uint8_t buf[4096];
    for (std::size_t n; (n = std::fread(buf, 1, sizeof(buf), f)) > 0;) b.insert(b.end(), buf, buf + n);
    std::fclose(f);
    auto u16 = [&](std::size_t o) -> std::uint32_t { return o + 2 <= b.size() ? b[o] | (b[o + 1] << 8) : 0; };
    auto u32 = [&](std::size_t o) {
        std::uint32_t v = 0;
        if (o + 4 <= b.size()) std::memcpy(&v, &b[o], 4);
        return v;
    };
    if (b.size() < 20 || std::memcmp(b.data(), "\0PSF", 4) != 0) {
        *error = "not an SFO";
        return false;
    }
    // Header: magic, version, key table, data table, entry count; then 16-byte
    // entries (key offset u16, format u16, length, capacity, data offset).
    const std::uint32_t keys = u32(8), data = u32(12), count = u32(16);
    for (std::uint32_t i = 0; i < count && 20 + i * 16 + 16 <= b.size(); ++i) {
        const std::size_t e = 20 + i * 16;
        const std::size_t k = keys + u16(e);
        const std::uint32_t fmt = u16(e + 2), len = u32(e + 4);
        const std::size_t d = data + u32(e + 12);
        if (k >= b.size() || d > b.size() || len > b.size() - d) continue;
        const void* nul = std::memchr(&b[k], 0, b.size() - k);
        if (!nul) continue;
        const std::string key(reinterpret_cast<const char*>(&b[k]), static_cast<const std::uint8_t*>(nul) - &b[k]);
        SfoValue v;
        if (fmt == 0x0404) {  // integer
            v.is_int = true;
            v.num = u32(d);
        } else {  // UTF-8 text (0x0204) or raw bytes (0x0004), up to the first NUL
            const char* s = reinterpret_cast<const char*>(&b[d]);
            const void* z = std::memchr(s, 0, len);
            v.text.assign(s, z ? static_cast<std::size_t>(static_cast<const char*>(z) - s) : len);
        }
        (*out)[key] = std::move(v);
    }
    return true;
}
