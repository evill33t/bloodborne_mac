#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Nightwatch launch preparation; game dumps and single-player saves stay read-only."""
import argparse
from datetime import datetime, timezone
import hashlib
import os
from pathlib import Path
import re
import zipfile
from patches import read_settings

ROOT = Path(__file__).resolve().parent.parent
PARTY_XML = ROOT / 'gpu/metal/vendor/nightwatch/patches/Bloodborne-party.xml'
PARTY_PATCHES = (
    'Party: Skip Online/Offline Choice (Online)', 'Party: Bells anywhere',
    'Party: Bells after boss defeated', 'Party: Keep session on map reload',
    'Party: SOS sign timeout 30s', 'Party: Bells without Insight',
    'Party: Fast multiplayer messages', 'Party: Hide bell searching messages',
    'Party: Fast join', 'Party: Fast rejoin', 'Party: Fast bell sign',
)


def mode(settings):
    # Match the tolerant settings loader: invalid entries retain the Off default.
    try:
        value = float(settings.get('nightwatch_mode', '0'))
    except ValueError:
        return 0
    return int(value) if value in (0, 1, 2) else 0


def user_directory(settings, base, cpu='rosetta'):
    base = Path(base).resolve()
    if not mode(settings):
        return base
    if cpu != 'rosetta':
        raise ValueError('Nightwatch currently requires BB_MAC_CPU=rosetta.')
    if mode(settings) == 2 and not re.fullmatch(r'BBP3-[A-Za-z0-9_+-]+', settings.get('nightwatch_code', '')):
        raise ValueError('Nightwatch Join requires the host BBP3 party code in settings.')
    # A distinct user root also isolates co-op side files, network identity and trophies.
    target = base / 'nightwatch'
    if target.is_symlink() or (target.exists() and target.resolve() != target):
        raise ValueError('Nightwatch user directory must not link to single-player saves.')
    return target


def frame_cap(settings):
    try:
        value = float(settings.get('frame_cap', '30'))
    except ValueError:
        return 30
    return int(value) if value in (0, 30, 60, 90, 120, 144, 165, 240) else 30


def snapshot(user):
    """Back up only this mode's existing saves, before the runtime can write them."""
    user = Path(user)
    saves = user / 'savedata'
    if saves.is_symlink():
        raise ValueError('Nightwatch savedata must not link to another save folder.')
    if not saves.is_dir():
        return
    backup = user / 'save_backups'
    backup.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    target = backup / f'nightwatch-{stamp}.zip'
    temp = target.with_suffix('.tmp')
    try:
        with zipfile.ZipFile(temp, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(saves.rglob('*')):
                if path.is_symlink():
                    raise ValueError(f'Nightwatch saves must not contain links: {path}')
                if path.is_file():
                    if not path.resolve().is_relative_to(saves.resolve()):
                        raise ValueError(f'Nightwatch saves must not link to another save folder: {path}')
                    archive.write(path, path.relative_to(user))
        temp.replace(target)
    except BaseException:
        temp.unlink(missing_ok=True)
        raise
    # Preserve all snapshots; removal/restoration is an explicit user operation.


def write_mod_identity(game, source, out):
    """Conservatively compare all loose-file overrides, including cosmetic ones."""
    game, source, out = Path(game), Path(source), Path(out)
    digest = hashlib.blake2b(digest_size=32)
    names = []
    files = (game / 'dvdroot_ps4').rglob('*') if game.resolve() != source.resolve() else ()
    for file in sorted(files):
        if not file.is_file():
            continue
        rel = file.relative_to(game)
        if file.resolve() == (source / rel).resolve():
            continue
        name = rel.as_posix()
        names.append(name)
        encoded = name.encode()
        digest.update(len(encoded).to_bytes(8, 'little') + encoded + file.stat().st_size.to_bytes(8, 'little'))
        with file.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
    content = 'hash ' + digest.hexdigest() + '\n' + ''.join('mod ' + name + '\n' for name in names)
    (out / 'party_mods.txt').write_text(content)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--settings', type=Path, required=True)
    parser.add_argument('--user', type=Path, required=True)
    parser.add_argument('--cpu', default='rosetta')
    args = parser.parse_args()
    settings = read_settings(args.settings)
    try:
        user = user_directory(settings, args.user, args.cpu)
        if mode(settings):
            user.mkdir(mode=0o700, parents=True, exist_ok=True)
            user.chmod(0o700)
            snapshot(user)
        print(user)
    except (ValueError, OSError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
