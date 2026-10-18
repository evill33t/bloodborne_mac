// The developers' debug font, DbgFont14h.ccm and DbgFont14h.tpf, made from
// font14.bin (tools/make_debug_font.py: the public-domain X11 fonts k14 and
// 7x14). The retail game ships neither file, and the debug menu draws
// nothing without them.
//
// The files are what the game's own font loader reads (the font manager's
// sub_136dc70): the CCM glyph table, version 0x20000, and a TPF of 512x512
// DXT1 pages for the PC platform. Glyphs are drawn as the original debug
// font draws them - white, with a black outline one pixel wide, on
// transparent pixels - so the menu stays readable over any scene.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace debug_font {

struct Files {
    std::vector<std::uint8_t> ccm, tpf;
    int glyphs = 0, pages = 0;
};

namespace detail {

constexpr int kPage = 512;

inline void put16(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    v[at] = static_cast<std::uint8_t>(x);
    v[at + 1] = static_cast<std::uint8_t>(x >> 8);
}
inline void put32(std::vector<std::uint8_t>& v, std::size_t at, std::uint32_t x) {
    put16(v, at, x & 0xffff);
    put16(v, at + 2, x >> 16);
}

// One page's pixels as DXT1 in the original's encoding: every block in the
// three-colour mode, black and white the two colours (index 0 the outline,
// 1 the glyph) and index 3 transparent.
inline void encode_dxt1(const std::vector<std::uint8_t>& px, std::uint8_t* out) {
    for (int by = 0; by < kPage / 4; ++by) {
        for (int bx = 0; bx < kPage / 4; ++bx) {
            std::uint32_t idx = 0;
            bool any = false;
            for (int j = 15; j >= 0; --j) {
                const std::uint8_t p = px[static_cast<std::size_t>((by * 4 + j / 4) * kPage + bx * 4 + j % 4)];
                any = any || p;
                idx = (idx << 2) | (p == 2 ? 1u : p == 1 ? 0u : 3u);
            }
            const std::uint16_t c1 = any ? 0xffff : 0;
            out[0] = out[1] = 0;  // c0: black
            out[2] = out[3] = static_cast<std::uint8_t>(c1);
            std::memcpy(out + 4, &idx, 4);
            out += 8;
        }
    }
}

}  // namespace detail

// font14.bin -> the two files. False, with the reason, when the blob is not
// one tools/make_debug_font.py writes.
inline bool build(const std::uint8_t* blob, std::size_t size, Files* out, std::string* err) {
    using namespace detail;
    if (size < 8 || std::memcmp(blob, "BBF1", 4) != 0) {
        *err = "font14.bin: not a glyph file";
        return false;
    }
    const int count = blob[4] | (blob[5] << 8);
    const int height = blob[6];
    std::size_t bits = 8 + 4 * static_cast<std::size_t>(count);
    if (count == 0 || height == 0 || height + 2 > kPage || bits > size) {
        *err = "font14.bin: a bad header";
        return false;
    }
    struct Glyph {
        std::uint32_t code;
        int width, page, x, y;
        std::size_t bitmap;
    };
    std::vector<Glyph> glyphs(static_cast<std::size_t>(count));
    // The cells, row by row across each page, a pixel apart: as wide and
    // tall as the glyph and its outline.
    const int cell_h = height + 2;
    int page = 0, x = 1, y = 1;
    for (int i = 0; i < count; ++i) {
        Glyph& g = glyphs[static_cast<std::size_t>(i)];
        const std::uint8_t* e = blob + 8 + 4 * i;
        g.code = static_cast<std::uint32_t>(e[0] | (e[1] << 8));
        g.width = e[2];
        if (g.width == 0 || g.width + 2 > kPage - 2 || (i > 0 && g.code <= glyphs[static_cast<std::size_t>(i) - 1].code)) {
            *err = "font14.bin: glyph " + std::to_string(i) + " is out of order or too wide";
            return false;
        }
        g.bitmap = bits;
        bits += static_cast<std::size_t>(height) * static_cast<std::size_t>((g.width + 7) / 8);
        if (bits > size) {
            *err = "font14.bin: cut short";
            return false;
        }
        const int w = g.width + 2;
        if (x + w > kPage - 1) {
            x = 1;
            y += cell_h + 1;
        }
        if (y + cell_h > kPage - 1) {
            ++page;
            x = 1;
            y = 1;
        }
        g.page = page;
        g.x = x;
        g.y = y;
        x += w + 1;
    }
    const int pages = page + 1;
    if (pages > 255) {
        *err = "font14.bin: too many glyphs for one font";
        return false;
    }

    // The pages: 0 transparent, 1 outline, 2 glyph.
    std::vector<std::vector<std::uint8_t>> px(static_cast<std::size_t>(pages),
                                              std::vector<std::uint8_t>(static_cast<std::size_t>(kPage * kPage), 0));
    for (const Glyph& g : glyphs) {
        std::vector<std::uint8_t>& p = px[static_cast<std::size_t>(g.page)];
        const int stride = (g.width + 7) / 8;
        for (int gy = 0; gy < height; ++gy) {
            for (int gx = 0; gx < g.width; ++gx) {
                if (!(blob[g.bitmap + static_cast<std::size_t>(gy * stride + gx / 8)] & (0x80 >> (gx % 8)))) continue;
                const int cx = g.x + 1 + gx, cy = g.y + 1 + gy;
                p[static_cast<std::size_t>(cy * kPage + cx)] = 2;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        std::uint8_t& n = p[static_cast<std::size_t>((cy + dy) * kPage + cx + dx)];
                        if (n == 0) n = 1;
                    }
                }
            }
        }
    }

    // DbgFont14h.ccm: the header, one region per glyph, then the glyphs.
    const std::size_t regions_at = 0x20, glyphs_at = regions_at + 8 * static_cast<std::size_t>(count);
    std::vector<std::uint8_t>& ccm = out->ccm;
    ccm.assign(glyphs_at + 24 * static_cast<std::size_t>(count), 0);
    put32(ccm, 0x00, 0x20000);
    put32(ccm, 0x04, static_cast<std::uint32_t>(ccm.size()));
    put16(ccm, 0x08, static_cast<std::uint32_t>(cell_h));  // the em, the line's height
    put16(ccm, 0x0a, kPage);
    put16(ccm, 0x0c, kPage);
    put16(ccm, 0x0e, static_cast<std::uint32_t>(count));   // regions
    put32(ccm, 0x10, static_cast<std::uint32_t>(count));   // glyphs
    put32(ccm, 0x14, static_cast<std::uint32_t>(regions_at));
    put32(ccm, 0x18, static_cast<std::uint32_t>(glyphs_at));
    ccm[0x1c] = 4;  // as the original has it
    ccm[0x1e] = static_cast<std::uint8_t>(pages);
    for (int i = 0; i < count; ++i) {
        const Glyph& g = glyphs[static_cast<std::size_t>(i)];
        const std::size_t r = regions_at + 8 * static_cast<std::size_t>(i);
        put16(ccm, r + 0, static_cast<std::uint32_t>(g.x));
        put16(ccm, r + 2, static_cast<std::uint32_t>(g.y));
        put16(ccm, r + 4, static_cast<std::uint32_t>(g.x + g.width + 2));
        put16(ccm, r + 6, static_cast<std::uint32_t>(g.y + cell_h));
        const std::size_t e = glyphs_at + 24 * static_cast<std::size_t>(i);
        put32(ccm, e + 0, g.code);
        put32(ccm, e + 4, static_cast<std::uint32_t>(r));  // the region, by its offset in the file
        put16(ccm, e + 8, static_cast<std::uint32_t>(g.page));
        put16(ccm, e + 10, 0);                                         // drawn this far right of the pen
        put16(ccm, e + 12, static_cast<std::uint32_t>(g.width + 2));   // the region's width
        put16(ccm, e + 14, static_cast<std::uint32_t>(g.width + 1));   // the pen's advance: outlines overlap
    }

    // DbgFont14h.tpf: the header, an entry per page, the pages' names, then
    // each page as a DDS file.
    const std::size_t dds_size = 128 + static_cast<std::size_t>(kPage * kPage / 2);
    const std::size_t names_at = 0x10 + 20 * static_cast<std::size_t>(pages);
    const std::size_t name_bytes = 32;  // "DbgFont14h_0000" in UTF-16, and its terminator
    const std::size_t data_at = names_at + name_bytes * static_cast<std::size_t>(pages);
    std::vector<std::uint8_t>& tpf = out->tpf;
    tpf.assign(data_at + dds_size * static_cast<std::size_t>(pages), 0);
    std::memcpy(tpf.data(), "TPF", 4);
    put32(tpf, 0x04, static_cast<std::uint32_t>(dds_size * static_cast<std::size_t>(pages)));
    put32(tpf, 0x08, static_cast<std::uint32_t>(pages));
    tpf[0x0c] = 0;  // platform: PC
    tpf[0x0d] = 3;
    tpf[0x0e] = 1;  // names in UTF-16
    for (int i = 0; i < pages; ++i) {
        const std::size_t e = 0x10 + 20 * static_cast<std::size_t>(i);
        const std::size_t data = data_at + dds_size * static_cast<std::size_t>(i);
        const std::size_t name = names_at + name_bytes * static_cast<std::size_t>(i);
        put32(tpf, e + 0, static_cast<std::uint32_t>(data));
        put32(tpf, e + 4, static_cast<std::uint32_t>(dds_size));
        tpf[e + 8] = 1;   // DXT1
        tpf[e + 9] = 0;   // a 2D texture
        tpf[e + 10] = 1;  // one mip level
        put32(tpf, e + 12, static_cast<std::uint32_t>(name));
        char text[16];
        std::snprintf(text, sizeof(text), "DbgFont14h_%04d", i);
        for (int c = 0; text[c]; ++c) tpf[name + 2 * static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(text[c]);
        // The DDS header the original's pages carry: 512x512, DXT1, one level.
        std::uint8_t* d = tpf.data() + data;
        std::memcpy(d, "DDS ", 4);
        std::vector<std::uint8_t> h(124, 0);
        put32(h, 0, 124);
        put32(h, 4, 0x81007);  // caps, height, width, pixel format, linear size
        put32(h, 8, kPage);
        put32(h, 12, kPage);
        put32(h, 16, kPage * kPage / 2);
        put32(h, 20, 1);
        put32(h, 24, 1);
        put32(h, 72, 32);
        put32(h, 76, 4);  // a FourCC
        std::memcpy(h.data() + 80, "DXT1", 4);
        put32(h, 104, 0x1000);
        std::memcpy(d + 4, h.data(), h.size());
        encode_dxt1(px[static_cast<std::size_t>(i)], d + 128);
    }
    out->glyphs = count;
    out->pages = pages;
    return true;
}

}  // namespace debug_font
