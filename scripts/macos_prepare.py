#!/usr/bin/env python3
"""Prepare an isolated macOS boot image; the source dump is read-only."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys

from macos_preflight import inspect_game
from macos_patch_analysis import check_selected
from macos_nightwatch import mode as nightwatch_mode, frame_cap, PARTY_XML, PARTY_PATCHES, write_mod_identity
from patches import (EBOOT_BASE, compile_patches, eboot_segments, resolution_writes, read_settings,
                     EFFECTS, external_patches, external_selection, compile_external)

ROOT = Path(__file__).resolve().parent.parent

RESOLUTIONS = {(960,540),(1280,720),(1440,810),(1600,900),(1920,1080),(2560,1440),(3840,2160)}


ENGINE_ELF_HASHES = {'941f887a562aae054fac35af8cc8f27cf075f3d4cc2e029fb5ae2a663aaa5ae7',
                     'cec1b276e7f9e4db978e57f524f41fbaac594530a3437b002e23f3fab14b4f86'}


def direct_memory_mb(size):
    # bloodborne-native/bloodborne-runtime run.sh (6228f43): target storage
    # grows with scene pixels, not the display size. Round extra storage to
    # 64 MiB; keep the PS4's base pool for scenes at or below 1080p.
    extra = max(0, size[0] * size[1] - 1920 * 1080) * 448
    step = 64 * 1024 * 1024
    return 5056 + (extra + step - 1) // step * 64


def settings_patches(settings, segments, profile='original', *, engine_timing=False):
    # The engine adapter writes its timing sites after import/TLS linking. Keep the
    # legacy XML path available to callers that explicitly prepare without it.
    names = ['Skip Intro'] + ([] if engine_timing else ['Uncap FPS++'])
    keys = ('effect_game_aa','effect_motion_blur','effect_dof',
            'effect_chromatic_aberration','effect_ssao','effect_dynamic_shadows')
    for key in keys:
        value = settings.get(key, '1')
        if profile == 'compatibility' and key in keys[:3]:
            value = '0'
        if value == '0':
            names.append(EFFECTS[key][0])
    xml = ROOT / 'patches/Bloodborne.xml'
    writes = compile_patches(xml, names, '01.09', segments)
    if nightwatch_mode(settings):
        writes += compile_patches(PARTY_XML, PARTY_PATCHES, '01.09', segments)
        names += list(PARTY_PATCHES)
    try:
        size = tuple(int(v) for v in settings.get('render_resolution','1920x1080').split('x'))
    except ValueError:
        size = (1920,1080)
    if size not in RESOLUTIONS:
        size = (1920,1080)
    if size != (1920,1080):
        # The direct Metal path presents the entire game frame, including UI.
        # Keep the game's movie viewport consistent with its render buffers.
        writes += resolution_writes(xml, size, '01.09', segments, ui=size)
    return writes, names, size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--graphics-profile', choices=('compatibility', 'original'),
                        default='original')
    parser.add_argument('--settings', type=Path)
    args = parser.parse_args()
    game, out = args.game.resolve(), args.out.resolve()
    if out == game or game in out.parents:
        parser.error('The generated output must be outside the source game folder')
    report = inspect_game(game, os.environ.get('BB_DLC_DIR'))
    if report['verified_patch_layout'] != '01.09':
        parser.error('This experimental runtime requires the verified 1.09 executable layout')
    out.mkdir(parents=True, exist_ok=True)
    (out / 'macos-preflight.json').write_text(json.dumps(report, indent=2) + '\n')
    for script in ('prepare.py', 'link_libc.py', 'link_modules.py', 'content_profile.py'):
        cmd = [sys.executable, str(ROOT / 'scripts' / script), str(game), '--out', str(out)]
        if script == 'content_profile.py':
            cmd += ['--sku', os.environ.get('BB_CONTENT_SKU', 'full')]
        subprocess.run(cmd, check=True)
    # This path rebuilds on every preparation. Refuse stale/mismatched linker
    # output instead of advertising unwind readiness for a legacy boot image.
    with (out / 'boot-linked.bin').open('rb') as linked:
        if linked.read(8) != b'BBPROBE6':
            parser.error('Prepared boot image lacks BBPROBE6 module unwind metadata')
    elf = (out / 'eboot.elf').read_bytes()
    settings_path = args.settings or out.parent / 'macos-settings.ini'
    engine_timing = report['elf_sha256'] in ENGINE_ELF_HASHES
    if not engine_timing:
        parser.error('Engine features require a supported decrypted ELF hash; no image patches were applied')
    settings = read_settings(settings_path)
    segments = eboot_segments(elf)
    writes, names, size = settings_patches(settings, segments, args.graphics_profile, engine_timing=engine_timing)
    patches_dir=Path(os.environ.get('BB_PATCHES_DIR',str(out.parent/'patches')))
    patches_config=Path(os.environ.get('BB_PATCHES_CONFIG',str(out.parent/'patches.json')))
    selected = external_selection(external_patches(patches_dir), patches_config)
    try:
        fps = frame_cap(settings)
        patch_report = check_selected(selected, writes, segments, fps=fps)
        patch_report['identity'] = report['loaded_image_sha256']
    except (ValueError, TypeError) as error:
        parser.error(str(error))
    (out / 'macos-patch-analysis.json').write_text(json.dumps(patch_report, indent=2) + '\n')
    if nightwatch_mode(settings):
        write_mod_identity(game, Path(os.environ.get('BB_SOURCE_GAME', str(game))), out)
    writes += compile_external(selected, segments)
    blob = struct.pack('<8sQQ', b'BBPATCH2', EBOOT_BASE, len(writes))
    for offset, data in writes:
        blob += struct.pack('<QQ', offset, len(data)) + data
    (out / 'patches.bin').write_bytes(blob)
    if nightwatch_mode(settings):
        identity = hashlib.blake2b(blob + f'frame_cap={fps}\n'.encode(), digest_size=32).hexdigest()
        (out / 'party_patch_hash.txt').write_text(f'hash {identity}\npatch Mac generated patches and {fps} FPS timing\n')
    (out / 'macos-runtime-options.json').write_text(json.dumps({
        'render_width': size[0], 'render_height': size[1],
        'direct_memory_mb': direct_memory_mb(size),
        'variable_timestep': True, 'settings': str(settings_path.resolve()),
        'engine_timing': engine_timing, 'elf_sha256': report['elf_sha256'],
        'nightwatch_mode': nightwatch_mode(settings),
    }, indent=2) + '\n')
    print(f'macOS: prepared verified 1.09 boot image; graphics profile {args.graphics_profile}')
    print('Generated-image patches: ' + ', '.join(names))


if __name__ == '__main__':
    main()
