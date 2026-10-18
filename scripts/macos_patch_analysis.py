#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Read-only patch mapping and bbhost timing conflict analysis for Bloodborne 1.09.

Inspired by Paleblood's patch_overlap.py; see gpu/metal/vendor/paleblood/UPSTREAM.md.
Exit 0: no conflicts, 1: conflicts, 2: invalid input/unsupported executable.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

from patches import EBOOT_BASE, encode

ROOT = Path(__file__).resolve().parent.parent
REFERENCE = ROOT / 'gpu/metal/vendor/paleblood'
ENGINE = ROOT / 'gpu/metal/vendor/bbhost/src/engine'
TARGET_IMAGE = '071df19c8880086d97182dbc057bc8cb37badaca57d9112683836b24a0444c0a'


def loaded_image_sha256(elf):
    """Hash PT_LOAD/RELRO contents before relocation, independent of ELF packaging."""
    if len(elf) < 64 or elf[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 image')
    phoff, = struct.unpack_from('<Q', elf, 32)
    stride, count = struct.unpack_from('<HH', elf, 54)
    if stride != 56 or not count or phoff + stride * count > len(elf):
        raise ValueError('Invalid ELF program headers')
    loads = []
    for i in range(count):
        kind, _, off, va, _, files, memory, _ = struct.unpack_from('<IIQQQQQQ', elf, phoff + i * stride)
        if kind not in (1, 0x61000010):
            continue
        if files > memory or off + files > len(elf) or va + memory > 1024 ** 3:
            raise ValueError('Invalid ELF load segment')
        loads.append((off, va, files, memory))
    if not loads:
        raise ValueError('ELF has no loadable segments')
    image = bytearray(max(va + memory for _, va, _, memory in loads))
    for off, va, files, _ in loads:
        image[va:va + files] = elf[off:off + files]
    return hashlib.sha256(image).hexdigest()


def functions():
    with (REFERENCE / 'functions.csv').open() as f:
        return [dict(address=int(row['address'], 16), size=int(row['size']), name=row['name'])
                for row in csv.DictReader(f)]


def overlaps(a, n, b, m):
    return n > 0 and m > 0 and a < b + m and b < a + n


def timing_ranges(fps=120):
    """Conservative ownership from the compiled bbhost tables, never upstream hooks.

    These are potential writes at the selected cap, including optional camera/cloth
    paths. The runtime additionally records actual installed edits for TOML patches.
    """
    result = [(0x02434770, 2077, 'bbhost frame limiter (replaced body)')]
    if fps == 30:
        return result
    source = (ENGINE / 'frame_rate.cpp').read_text()

    def table(name):
        match = re.search(r'constexpr \w+ ' + name + r'\[\] = \{(.*?)\n\};', source, re.S)
        if not match:
            raise ValueError(f'Cannot inventory bbhost timing table {name}')
        return re.sub(r'//[^\n]*', '', match[1])

    for name, size in (('kConstants', 4), ('kRated', 4), ('kCounts', None), ('kBytes', None)):
        rows = re.findall(r'\{\s*(0x[0-9a-fA-F]+)\s*,\s*([^,\n]+)', table(name))
        if not rows:
            raise ValueError(f'Empty bbhost timing table {name}')
        for address, width in rows:
            result.append((int(address, 16), size or int(width, 0), f'bbhost {name}'))
    texts = [('kFrameTimeSites', table('kFrameTimeSites')),
             ('kSixtySites', (ENGINE / 'sixty_fps_sites.inc').read_text())]
    if fps == 0 or fps > 60:
        texts.append(('kClothSites', (ENGINE / 'ninety_fps_sites.inc').read_text()))
        result.append((0x04d3bd00, 16, 'bbhost cloth scale'))
    for name, text in texts:
        rows = re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*"([0-9a-fA-F]+)",\s*"([0-9a-fA-F]+)"', text)
        if not rows:
            raise ValueError(f'Cannot inventory bbhost timing sites {name}')
        for address, before, after in rows:
            if len(before) != len(after) or len(before) % 2:
                raise ValueError(f'Invalid bbhost timing site {address}')
            result.append((int(address, 16), len(after) // 2, f'bbhost {name}'))
    dt = (ENGINE / 'frame_rate_dt_loads.inc').read_text()
    rows = re.findall(r'\{\s*(0x[0-9a-fA-F]+),\s*(\d+),\s*\d+,', dt)
    if not rows:
        raise ValueError('Cannot inventory bbhost frame-time loads')
    result.extend((int(address, 16), int(size), 'bbhost frame-time load') for address, size in rows)
    result.extend([(0x02418e3d, 19, 'bbhost task step'), (0x01d419d6, 13, 'bbhost play-time carry')])
    return sorted(set(result))


def xml_writes(path, names=None):
    writes, found = [], set()
    for meta in ET.parse(path).getroot().iter('Metadata'):
        name = meta.get('Name', '?')
        if meta.get('AppVer') != '01.09' or meta.get('AppElf', 'eboot.bin') != 'eboot.bin':
            continue
        if names is not None and name not in names:
            continue
        found.add(name)
        for line in meta.iter('Line'):
            writes.append((name, int(line.get('Address') or '', 0), encode(line)))
    if names is not None and set(names) - found:
        raise ValueError(f'Unknown 1.09 patches: {sorted(set(names) - found)}')
    return writes


def analyze(writes, *, fps=120, engine=True, compare_patches=True):
    writes = list(writes)
    catalog, owned, mapped, conflicts, seen = functions(), timing_ranges(fps) if engine else [], [], [], {}
    for name, address, data in writes:
        if address < EBOOT_BASE or not data:
            raise ValueError(f'{name}: invalid patch address/empty data')
        labels = [f['name'] for f in catalog if overlaps(address, len(data), f['address'], f['size'])]
        mapped.append(dict(patch=name, address=f'0x{address:08x}', size=len(data), functions=labels))
        owners = sorted({label for start, size, label in owned if overlaps(address, len(data), start, size)})
        if owners:
            conflicts.append(dict(patch=name, address=f'0x{address:08x}', kind='engine_overlap', detail=', '.join(owners)))
        for i, byte in enumerate(data):
            previous = seen.get(address + i)
            if compare_patches and previous and previous[0] != name and previous[1] != byte:
                conflicts.append(dict(patch=name, address=f'0x{address + i:08x}', kind='patch_overlap', detail=f'Different bytes from {previous[0]}'))
                break
        for i, byte in enumerate(data):
            seen[address + i] = name, byte
    # Detect control-flow edits even when their bytes span several XML lines.
    by_patch = {}
    for name, address, data in writes:
        sparse = by_patch.setdefault(name, {})
        sparse.update((address + i, byte) for i, byte in enumerate(data))
    for name, sparse in by_patch.items():
        for start, pattern, reason in (
                (0x02715d71, b'\xe8\x81\x09\x00\x00', 'Uncap call targets the middle of an initializer'),
                (0x02483ec1, b'\xc3', 'Early return inside the timed condition-wait path')):
            if all(sparse.get(start + i) == byte for i, byte in enumerate(pattern)):
                conflicts.append(dict(patch=name, address=f'0x{start:08x}', kind='unsafe_control_flow', detail=reason))
    return dict(schema=1, catalog='Paleblood af97b90 (32 tracked functions; partial map)',
                identity='not_checked', frame_cap=fps, engine_timing=engine, writes=mapped, conflicts=conflicts)


def check_selected(selected, builtin_writes, segments, *, fps=120):
    """Validate all selected XMLs before compilation; never silently omit a mod."""
    writes = [('Mac built-ins', EBOOT_BASE + off, data) for off, data in builtin_writes]
    for key, _, meta in selected:
        for line in meta.iter('Line'):
            address, data = int(line.get('Address') or '', 0), encode(line)
            if not any(start <= address - EBOOT_BASE and address - EBOOT_BASE + len(data) <= end for start, end in segments):
                raise ValueError(f'{key}: address {address:#x} is outside the eboot')
            writes.append((key, address, data))
    report = analyze(writes, fps=fps)
    if report['conflicts']:
        details = '; '.join(f"{c['patch']} at {c['address']}: {c['detail']}" for c in report['conflicts'][:6])
        raise ValueError(f'Conflicting Mac patches: {details}')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xml', type=Path, default=ROOT / 'patches/Bloodborne.xml')
    parser.add_argument('--name', action='append', help='Analyze selected patches together; default maps each independently')
    parser.add_argument('--manifest', type=Path, action='append', default=[], help='Also inspect bbhost TOML manifests')
    parser.add_argument('--fps', type=int, choices=(0, 30, 60, 90, 120, 144, 165, 240), default=120, help='0 = uncapped')
    parser.add_argument('--no-engine', action='store_true')
    parser.add_argument('--elf', type=Path, help='Require the normalized Paleblood 1.09 target image')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    try:
        identity = None
        if args.elf:
            identity = loaded_image_sha256(args.elf.read_bytes())
            if identity != TARGET_IMAGE:
                raise ValueError('Unsupported loaded image; refusing target-specific analysis')
        writes = xml_writes(args.xml, args.name) if args.name or not args.manifest else []
        for path in args.manifest:
            try:
                import tomllib
            except ImportError as error:
                raise ValueError('TOML analysis requires Python 3.11 or later') from error
            manifest = tomllib.loads(path.read_text())
            if not isinstance(manifest.get('patch'), dict) or not isinstance(manifest.get('site'), dict):
                raise ValueError(f'{path.name}: expected [patch] and [site.*] tables')
            if not re.fullmatch(r'[0-9a-fA-F]{64}', manifest.get('patch', {}).get('eboot', '')):
                raise ValueError(f'{path.name}: missing/invalid [patch] eboot SHA-256')
            if args.elf and manifest['patch']['eboot'].lower() != hashlib.sha256(args.elf.read_bytes()).hexdigest():
                raise ValueError(f'{path.name}: manifest executable hash does not match --elf')
            if not manifest.get('site'):
                raise ValueError(f'{path.name}: no patch sites')
            for site in manifest.get('site', {}).values():
                address = site['address']
                def hex_bytes(value):
                    return bytes.fromhex(value.removeprefix('0x').removeprefix('0X').replace(',', ' '))
                data, expected = hex_bytes(site['bytes']), hex_bytes(site['expect'])
                if not data or len(data) != len(expected):
                    raise ValueError(f'{path.name}: expect and bytes must have equal nonzero length')
                writes.append((path.name, int(address, 16) if isinstance(address, str) else address,
                               data))
        report = analyze(writes, fps=args.fps, engine=not args.no_engine, compare_patches=bool(args.name or args.manifest))
        if identity:
            report['identity'] = identity
        if args.json:
            print(json.dumps(report, indent=2))
        else:
            for row in report['writes']:
                print(f"{row['address']} +{row['size']:<3} {', '.join(row['functions']) or 'unmapped'} [{row['patch']}]")
            for row in report['conflicts']:
                print(f"CONFLICT {row['patch']} {row['address']}: {row['detail']}")
            print(f"{len(writes)} writes, {len(report['conflicts'])} conflicts; function map is partial")
        return int(bool(report['conflicts']))
    except (OSError, ValueError, KeyError, TypeError, ET.ParseError) as error:
        parser.exit(2, f'Patch analysis: {error}\n')


if __name__ == '__main__':
    raise SystemExit(main())
