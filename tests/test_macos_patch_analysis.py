from paths import ROOT
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

from macos_patch_analysis import analyze, check_selected, loaded_image_sha256, timing_ranges, xml_writes
from macos_prepare import settings_patches
from test_patches import SEGMENTS


class MacPatchAnalysisTests(unittest.TestCase):
    def test_mapping_does_not_treat_paleblood_hooks_as_our_hooks(self):
        result = analyze([('free', 0x01388c60, b'\x90')])
        self.assertEqual(result['writes'][0]['functions'], ['frame_timing_task_run_all_01388c60'])
        self.assertFalse(result['conflicts'])

    def test_half_open_and_spanning_ranges(self):
        end = 0x02434770 + 2077
        self.assertFalse(analyze([('after', end, b'\x90')])['conflicts'])
        self.assertTrue(analyze([('cross', 0x0243476f, b'\x90\x90')])['conflicts'])
        self.assertTrue(analyze([('span', 0x02434760, b'\x90' * 2100)])['conflicts'])

    def test_inventory_uses_real_bbhost_tables_and_cap(self):
        self.assertGreater(len(timing_ranges(120)), 200)
        self.assertEqual(len(timing_ranges(30)), 1)
        for cap in (0, 30, 60, 90, 120, 144, 165, 240):
            with self.subTest(cap=cap):
                self.assertTrue(analyze([('limiter', 0x02434780, b'\x90')], fps=cap)['conflicts'])
        self.assertTrue(analyze([('physics', 0x01bf9b69, b'\x90')], fps=120)['conflicts'])
        self.assertFalse(analyze([('physics', 0x01bf9b69, b'\x90')], fps=30)['conflicts'])

    def test_unsafe_control_flow_by_bytes_even_with_another_name(self):
        for address, data in ((0x02715d71, bytes.fromhex('e881090000')), (0x02483ec1, b'\xc3')):
            result = analyze([('renamed', address, data)], engine=False)
            self.assertEqual(result['conflicts'][0]['kind'], 'unsafe_control_flow')
        self.assertFalse(analyze([('repair', 0x02483ec1, b'\x90')], engine=False)['conflicts'])
        split = [('split', 0x02715d71 + i, bytes([byte])) for i, byte in enumerate(bytes.fromhex('e881090000'))]
        self.assertEqual(analyze(split, engine=False)['conflicts'][0]['kind'], 'unsafe_control_flow')

    def test_conflicting_patches_and_identical_overlaps(self):
        self.assertFalse(analyze([('a', 0x400010, b'\x11\x22'), ('b', 0x400011, b'\x22')])['conflicts'])
        self.assertEqual(analyze([('a', 0x400010, b'\x11\x22'), ('b', 0x400011, b'\x33')])['conflicts'][0]['kind'], 'patch_overlap')

    def test_current_mac_builtins_are_allowed(self):
        for profile in ('original', 'compatibility'):
            writes, _, _ = settings_patches({}, SEGMENTS, profile, engine_timing=True)
            self.assertFalse(check_selected([], writes, SEGMENTS)['conflicts'])

    def test_selected_mod_conflict_refuses_before_compile(self):
        meta = ET.fromstring('<Metadata><Line Type="bytes" Address="0x02434780" Value="90"/></Metadata>')
        with self.assertRaisesRegex(ValueError, 'bbhost frame limiter'):
            check_selected([('user/mod', None, meta)], [], SEGMENTS)
        meta = ET.fromstring('<Metadata><Line Type="bytes" Address="0x400010" Value="90"/></Metadata>')
        self.assertFalse(check_selected([('user/mod', None, meta)], [], SEGMENTS)['conflicts'])

    def test_legacy_xml_maps_and_flags_the_suspect_call(self):
        report = analyze(xml_writes(ROOT / 'patches/Bloodborne.xml', ['Uncap FPS++']))
        self.assertTrue(any(c['address'] == '0x02715d71' and c['kind'] == 'unsafe_control_flow' for c in report['conflicts']))

    def test_loaded_image_identity_ignores_file_packaging(self):
        def elf(offset):
            data = bytearray(offset + 4)
            data[:6] = b'\x7fELF\x02\x01'
            struct.pack_into('<Q', data, 32, 64)
            struct.pack_into('<HH', data, 54, 56, 1)
            struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, offset, 0, 0, 4, 8, 4096)
            data[offset:] = b'test'
            return data
        self.assertEqual(loaded_image_sha256(elf(128)), loaded_image_sha256(elf(256)))
        with self.assertRaises(ValueError):
            loaded_image_sha256(b'bad ELF')
        bad = elf(128)
        struct.pack_into('<Q', bad, 64 + 40, 2**40)
        with self.assertRaises(ValueError):
            loaded_image_sha256(bad)

    def test_cli_machine_readable_and_exit_status(self):
        script = ROOT / 'scripts/macos_patch_analysis.py'
        result = subprocess.run([sys.executable, str(script), '--name', 'Skip Intro', '--json'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(json.loads(result.stdout)['conflicts'])
        result = subprocess.run([sys.executable, str(script), '--name', 'Uncap FPS++', '--json'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertTrue(json.loads(result.stdout)['conflicts'])

    def test_toml_cli_inspects_only_explicit_manifests(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'patch.toml'
            path.write_text('[patch]\nname="safe"\neboot="' + 'a' * 64 + '"\n'
                            '[site.one]\naddress="0x400010"\nexpect="0x90"\nbytes="0x91"\n')
            command = [sys.executable, str(ROOT / 'scripts/macos_patch_analysis.py'), '--manifest', str(path), '--json']
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(len(json.loads(result.stdout)['writes']), 1)
            path.write_text(path.read_text().replace('0x400010', '0x02434780'))
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            path.write_text('[patch]\neboot="wrong"\n[site.one]\n')
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)


if __name__ == '__main__':
    unittest.main()
