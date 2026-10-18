#!/usr/bin/env python3
"""Builds plugins/debug_menu/font14.bin, the glyphs the Debug Menu plugin
draws the developers' debug font from (plugins/debug_menu/debug_font.hpp),
out of two X11 bitmap fonts that are in the public domain ("Public domain
font. Share and enjoy."): k14 (JIS X 0208, 14 dots, full width) and 7x14
(ISO 10646, 7 dots, half width), as X.Org's font-misc-misc installs them.

    tools/make_debug_font.py [FONTDIR] [OUT]
    FONTDIR: where k14.pcf.gz and 7x14.pcf.gz are (default /usr/share/fonts/misc)

font14.bin, little-endian:
    "BBF1", u16 glyph count, u8 cell height, u8 ascent (the baseline)
    per glyph, sorted by code: u16 code (UTF-16), u8 width, u8 0
    then each glyph's bitmap in that order: height rows of ceil(width/8)
    bytes, the leftmost pixel in a byte's high bit.

A JIS X 0208 character goes in under its JIS standard code point and, where
Microsoft's mapping (CP932, what the game's strings use) differs, under that
one as well: 0x2141 is both U+301C and U+FF5E.
"""
import gzip
import os
import struct
import sys

PCF_PROPERTIES, PCF_ACCELERATORS, PCF_METRICS, PCF_BITMAPS = 1 << 0, 1 << 1, 1 << 2, 1 << 3
PCF_BDF_ENCODINGS, PCF_BDF_ACCELERATORS = 1 << 5, 1 << 8


class Pcf:
    """The tables of a PCF font this needs: metrics, bitmaps, encodings."""

    def __init__(self, path):
        d = gzip.open(path).read() if path.endswith('.gz') else open(path, 'rb').read()
        if d[:4] != b'\x01fcp':
            raise ValueError(path + ': not a PCF font')
        n, = struct.unpack_from('<I', d, 4)
        self.tables = {}
        for i in range(n):
            typ, fmt, size, off = struct.unpack_from('<IIII', d, 8 + 16 * i)
            self.tables[typ] = (fmt, off)
        self.d = d
        self.props = self._properties()
        self.ascent, self.descent = self._accelerators()
        self.metrics = self._metrics()
        self.bitmaps = self._bitmaps()
        self.encoding = self._encodings()

    def _fmt(self, typ):
        fmt, off = self.tables[typ]
        own, = struct.unpack_from('<I', self.d, off)  # each table repeats its format, in little-endian
        endian = '>' if own & 4 else '<'
        return own, endian, off + 4

    def _properties(self):
        fmt, e, at = self._fmt(PCF_PROPERTIES)
        n, = struct.unpack_from(e + 'i', self.d, at)
        at += 4
        entries = [struct.unpack_from(e + 'iBi', self.d, at + 9 * i) for i in range(n)]
        at += 9 * n
        at += (4 - n % 4) % 4 if n % 4 else 0
        size, = struct.unpack_from(e + 'i', self.d, at)
        strings = self.d[at + 4:at + 4 + size]
        out = {}
        for name_off, is_string, value in entries:
            name = strings[name_off:strings.index(b'\0', name_off)].decode('latin-1')
            out[name] = strings[value:strings.index(b'\0', value)].decode('latin-1') if is_string else value
        return out

    def _accelerators(self):
        # fontAscent and fontDescent, after eight flag bytes.
        typ = PCF_BDF_ACCELERATORS if PCF_BDF_ACCELERATORS in self.tables else PCF_ACCELERATORS
        fmt, e, at = self._fmt(typ)
        return struct.unpack_from(e + 'ii', self.d, at + 8)

    def _metrics(self):
        fmt, e, at = self._fmt(PCF_METRICS)
        out = []
        if fmt & 0x100:  # compressed: five bytes, each biased by 0x80
            n, = struct.unpack_from(e + 'h', self.d, at)
            at += 2
            for i in range(n):
                lsb, rsb, w, asc, desc = (b - 0x80 for b in self.d[at + 5 * i:at + 5 * i + 5])
                out.append((lsb, rsb, w, asc, desc))
        else:
            n, = struct.unpack_from(e + 'i', self.d, at)
            at += 4
            for i in range(n):
                lsb, rsb, w, asc, desc, _attr = struct.unpack_from(e + 'hhhhhH', self.d, at + 12 * i)
                out.append((lsb, rsb, w, asc, desc))
        return out

    def _bitmaps(self):
        fmt, e, at = self._fmt(PCF_BITMAPS)
        n, = struct.unpack_from(e + 'i', self.d, at)
        offsets = struct.unpack_from(e + '%di' % n, self.d, at + 4)
        sizes = struct.unpack_from(e + '4i', self.d, at + 4 + 4 * n)
        data = self.d[at + 4 + 4 * n + 16:at + 4 + 4 * n + 16 + sizes[fmt & 3]]
        pad = 1 << (fmt & 3)
        msbit = bool(fmt & 8)
        unit = 1 << ((fmt >> 4) & 3)
        msbyte = bool(fmt & 4)
        out = []
        for i in range(n):
            lsb, rsb, w, asc, desc = self.metrics[i]
            width, height = rsb - lsb, asc + desc
            stride = ((width + 7) // 8 + pad - 1) // pad * pad
            rows = []
            for y in range(height):
                row = bytearray(data[offsets[i] + y * stride:offsets[i] + (y + 1) * stride])
                if unit > 1 and msbyte != msbit:  # bytes within a scan unit run the other way
                    for k in range(0, len(row), unit):
                        row[k:k + unit] = row[k:k + unit][::-1]
                bits = []
                for x in range(width):
                    b = row[x // 8]
                    bits.append((b >> (7 - x % 8)) & 1 if msbit else (b >> (x % 8)) & 1)
                rows.append(bits)
            out.append(rows)
        return out

    def _encodings(self):
        fmt, e, at = self._fmt(PCF_BDF_ENCODINGS)
        min2, max2, min1, max1, default = struct.unpack_from(e + '5h', self.d, at)
        n = (max2 - min2 + 1) * (max1 - min1 + 1)
        idx = struct.unpack_from(e + '%dH' % n, self.d, at + 10)
        out = {}
        for b1 in range(min1, max1 + 1):
            for b2 in range(min2, max2 + 1):
                g = idx[(b1 - min1) * (max2 - min2 + 1) + (b2 - min2)]
                if g != 0xffff:
                    out[(b1 << 8) | b2] = g
        return out

    def cell(self, glyph, height, ascent):
        """The glyph in a cell as wide as its advance and `height` tall."""
        lsb, rsb, w, asc, desc = self.metrics[glyph]
        rows = [[0] * w for _ in range(height)]
        for y, bits in enumerate(self.bitmaps[glyph]):
            cy = ascent - asc + y
            for x, b in enumerate(bits):
                cx = lsb + x
                if b and 0 <= cy < height and 0 <= cx < w:
                    rows[cy][cx] = 1
        return w, rows


def jis_unicode(code):
    """The code points of a JIS X 0208 code: JIS's own, and CP932's when it differs."""
    b1, b2 = code >> 8, code & 0xff
    out = []
    try:
        out.append(bytes([b1 | 0x80, b2 | 0x80]).decode('euc_jp'))
    except UnicodeDecodeError:
        pass
    # JIS to Shift JIS, for CP932's reading of the same character.
    s1 = (b1 + 1) // 2 + (0x70 if b1 <= 0x5e else 0xb0)
    s2 = b2 + (0x1f if b1 % 2 else 0x7e)
    if s2 >= 0x7f and b1 % 2:
        s2 += 1
    try:
        ch = bytes([s1, s2]).decode('cp932')
        if ch not in out:
            out.append(ch)
    except UnicodeDecodeError:
        pass
    return [c for c in out if len(c) == 1 and ord(c) < 0x10000]


def main():
    fontdir = sys.argv[1] if len(sys.argv) > 1 else '/usr/share/fonts/misc'
    out_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'plugins', 'debug_menu', 'font14.bin')
    k14 = Pcf(os.path.join(fontdir, 'k14.pcf.gz'))
    half = Pcf(os.path.join(fontdir, '7x14.pcf.gz'))
    for f, want in ((k14, 'JISX0208'), (half, 'ISO10646')):
        if not str(f.props.get('CHARSET_REGISTRY', '')).upper().startswith(want):
            raise SystemExit('unexpected encoding %s' % f.props.get('CHARSET_REGISTRY'))
    # One baseline for both: the taller ascent above it, the deeper descent below.
    ascent = max(k14.ascent, half.ascent)
    height = ascent + max(k14.descent, half.descent)
    glyphs = {}
    # Half width: ASCII, Latin-1 and the half-width katakana, from 7x14.
    for code in list(range(0x20, 0x7f)) + list(range(0xa0, 0x100)) + list(range(0xff61, 0xffa0)):
        if code in half.encoding:
            glyphs[code] = half.cell(half.encoding[code], height, ascent)
    # Full width: all of JIS X 0208, from k14.
    for code, g in k14.encoding.items():
        for ch in jis_unicode(code):
            cp = ord(ch)
            if cp >= 0x20 and not (0x20 <= cp < 0x7f) and cp not in glyphs:
                glyphs[cp] = k14.cell(g, height, ascent)
    codes = sorted(glyphs)
    blob = bytearray(b'BBF1' + struct.pack('<HBB', len(codes), height, ascent))
    for cp in codes:
        blob += struct.pack('<HBB', cp, glyphs[cp][0], 0)
    for cp in codes:
        w, rows = glyphs[cp]
        for bits in rows:
            row = bytearray((w + 7) // 8)
            for x, b in enumerate(bits):
                if b:
                    row[x // 8] |= 0x80 >> (x % 8)
            blob += row
    with open(out_path, 'wb') as f:
        f.write(blob)
    print('%s: %d glyphs (%d half width), %d bytes' % (out_path, len(codes), sum(1 for c in codes if glyphs[c][0] < 10), len(blob)))


if __name__ == '__main__':
    main()
