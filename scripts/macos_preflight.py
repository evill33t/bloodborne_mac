#!/usr/bin/env python3
"""Read-only dump inspection for the macOS port. Never modifies or launches a game."""
import argparse
import hashlib
import json
from pathlib import Path

from patches import BLOODBORNE_IDS, PATCH_FINGERPRINTS, patch_fingerprint
from prepare import inspect_libc, parse_self, sfo
from macos_patch_analysis import loaded_image_sha256, TARGET_IMAGE


def inspect_game(game, dlc=None):
    game = Path(game).resolve()
    metadata = sfo((game / 'sce_sys/param.sfo').read_bytes())
    if metadata.get('TITLE_ID') not in BLOODBORNE_IDS:
        raise ValueError('This folder is not a recognized Bloodborne title ID')
    data = (game / 'eboot.bin').read_bytes()
    elf, header, segments, _, missing = parse_self(data)
    fingerprint = patch_fingerprint(elf)
    binary_version = '01.09' if fingerprint == PATCH_FINGERPRINTS['01.09'] else None
    libc = inspect_libc(game / 'sce_module/libc.prx')
    if not libc['init_env_is_ret']:
        raise ValueError('Bundled libc does not meet the loader _init_env contract')
    # Validate the other module the current loader links, without writing an image.
    parse_self((game / 'sce_module/libSceFios2.prx').read_bytes())
    image_hash = loaded_image_sha256(elf)
    addons = []
    if dlc is not None:
        dlc = Path(dlc)
        if not dlc.is_dir():
            raise ValueError(f'DLC directory does not exist: {dlc}')
        addons = sorted(p.name for p in dlc.iterdir()
                        if p.is_dir() and (p / 'sce_sys').is_dir())
    return {
        'title': metadata.get('TITLE'),
        'title_id': metadata['TITLE_ID'],
        'app_version': metadata.get('APP_VER'),
        'master_version': metadata.get('VERSION'),
        'eboot_sha256': hashlib.sha256(data).hexdigest(),
        'elf_sha256': hashlib.sha256(elf).hexdigest(),
        'loaded_image_sha256': image_hash,
        'paleblood_target_image': image_hash == TARGET_IMAGE,
        'entry': header[4],
        'program_headers': len(segments),
        'unavailable_metadata_headers': missing,
        'patch_site_fingerprint': fingerprint,
        'verified_patch_layout': binary_version,
        'metadata_version_mismatch': bool(binary_version and metadata.get('APP_VER') != binary_version),
        'libc_init_env_verified': True,
        'dlc_labels': addons,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--dlc', type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_game(args.game, args.dlc), ensure_ascii=False, indent=2))
    except (OSError, ValueError, StopIteration, KeyError, IndexError) as error:
        parser.exit(1, f'macOS preflight failed: {error}\n')


if __name__ == '__main__':
    main()
