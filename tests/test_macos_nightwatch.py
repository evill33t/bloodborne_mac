from paths import ROOT
import hashlib
from pathlib import Path
import tempfile
import unittest
import zipfile

from macos_nightwatch import mode, user_directory, snapshot, write_mod_identity, PARTY_PATCHES
from macos_prepare import settings_patches
from test_patches import SEGMENTS


class NightwatchTests(unittest.TestCase):
    def test_off_and_malformed_modes_leave_the_save_root_alone(self):
        with tempfile.TemporaryDirectory() as tmp:
            for value in ('0', 'off', 'nan', '1.5', '3', '-1'):
                settings = {'nightwatch_mode': value}
                self.assertEqual(mode(settings), 0)
                self.assertEqual(user_directory(settings, tmp, 'bbcpu'), Path(tmp).resolve())
            self.assertEqual(mode({'nightwatch_mode': '1.0'}), 1)

    def test_coop_is_separate_and_native_cpu_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            self.assertEqual(user_directory({'nightwatch_mode': '1'}, tmp), Path(tmp).resolve() / 'nightwatch')
            with self.assertRaises(ValueError):
                user_directory({'nightwatch_mode': '1'}, tmp, 'bbcpu')
            with self.assertRaises(ValueError):
                user_directory({'nightwatch_mode': '2'}, tmp)

    def test_save_symlinks_cannot_reach_the_single_player_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'nightwatch').symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                user_directory({'nightwatch_mode': '1'}, root)
            (root / 'nightwatch').unlink()
            (root / 'savedata').symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                snapshot(root)

    def test_snapshots_preserve_previous_files_and_can_be_read_back(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            save = root / 'savedata/1/CUSA00900/userdata0000'
            save.parent.mkdir(parents=True)
            save.write_bytes(b'character before co-op')
            snapshot(root)
            archives = list((root / 'save_backups').glob('*.zip'))
            self.assertEqual(len(archives), 1)
            with zipfile.ZipFile(archives[0]) as archive:
                self.assertEqual(archive.read('savedata/1/CUSA00900/userdata0000'), save.read_bytes())
            save.write_bytes(b'next session')
            snapshot(root)
            self.assertEqual(len(list((root / 'save_backups').glob('*.zip'))), 2)
            self.assertEqual(save.read_bytes(), b'next session')

    def test_party_patches_are_opt_in_and_do_not_touch_timing_sites(self):
        off, names, _ = settings_patches({}, SEGMENTS, engine_timing=True)
        on, party_names, _ = settings_patches({'nightwatch_mode': '1'}, SEGMENTS, engine_timing=True)
        self.assertFalse(set(names) & set(PARTY_PATCHES))
        self.assertTrue(set(PARTY_PATCHES).issubset(party_names))
        self.assertGreater(len(on), len(off))
        self.assertNotIn('Uncap FPS++', party_names)
        self.assertEqual(settings_patches({'nightwatch_mode': '2'}, SEGMENTS, engine_timing=True)[0], on)

    def test_identity_changes_when_a_mod_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source, game, out = root / 'source', root / 'view', root / 'prepared'
            (source / 'dvdroot_ps4/event').mkdir(parents=True)
            (game / 'dvdroot_ps4/event').mkdir(parents=True)
            out.mkdir()
            (source / 'dvdroot_ps4/event/base').write_bytes(b'original')
            (game / 'dvdroot_ps4/event/base').symlink_to(source / 'dvdroot_ps4/event/base')
            write_mod_identity(game, source, out)
            empty = (out / 'party_mods.txt').read_text()
            replacement = game / 'dvdroot_ps4/event/quest'
            replacement.write_bytes(b'first mod')
            write_mod_identity(game, source, out)
            first = (out / 'party_mods.txt').read_text()
            replacement.write_bytes(b'changed mod')
            write_mod_identity(game, source, out)
            self.assertNotEqual(empty, first)
            self.assertNotEqual(first, (out / 'party_mods.txt').read_text())
            self.assertNotIn('mod dvdroot_ps4/event/base', first)
            self.assertIn('mod dvdroot_ps4/event/quest', first)


if __name__ == '__main__':
    unittest.main()
