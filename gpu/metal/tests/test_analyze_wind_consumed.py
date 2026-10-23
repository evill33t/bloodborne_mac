#!/usr/bin/env python3
"""Offline protocol fixtures only; no game, guest memory or GPU work."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[3] / "tools/analyze_wind_consumed.py"
SPEC = importlib.util.spec_from_file_location("analyze_wind_consumed", MODULE)
ANALYZER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ANALYZER)


class ConsumedProtocolTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="wind-consumed-parser-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "wind_consumed_f100_draw1.json"

    def fixture(self, schema=4):
        words = [0] * {2: 60, 3: 72, 4: 120}[schema]
        words[:2] = [0x57494E44, schema]
        words[3:5] = [4, 4]
        for leaf in (7, 11):
            words[leaf:leaf+4] = [0x12340000, 0x12340000, 0x80, 0x80]
        for index in range(4):
            words[20+index] = words[24+index] = 0x7FC12300 + index
        for index in range(16):
            words[28+index] = words[44+index] = 0x80000000 + index
        sites = [
            {"kind": "wind", "original_pc": 0x001C, "byte_offset": 128, "words": 4, "first_sgpr": 12},
            {"kind": "view_projection", "original_pc": 0x14EC, "byte_offset": 800, "words": 16, "first_sgpr": 8},
        ]
        if schema >= 3:
            words[60] = 4
            words[62:66] = [0x12340000, 0x12340000, 0x80, 0x80]
            for index in range(3):
                words[66+index] = words[69+index] = 0xFFC12300 + index
            sites.append({"kind": "camera", "original_pcs": [0x1500, 0x14F8, 0x14E8],
                          "byte_offsets": [732, 748, 764], "sgprs": [0, 5, 4], "words": 3, "recorded_after_pc": 0x1500})
        if schema >= 4:
            words[72:78] = [4, 2, 2, 0, 0, 3]
            words[78:82] = [0x71234000, 0x71234000, 0x80, 0x80]
            words[82:86] = words[86:90] = [0x71234000, 0x108080, 6, 0x77FAC]
            words[90:93] = words[93:96] = [3, 4, 5]
            for index in range(12):
                words[96+index] = words[108+index] = 0
            sites.append({"kind": "object_matrix", "original_pcs": [0x00B4, 0x00BC, 0x00C4],
                          "first_vgprs": [23, 27, 31], "descriptor_sgprs": [16, 17, 18, 19], "index_vgprs": [0, 1, 2],
                          "words": 12, "recorded_after_pc": 0x00C4, "selection": "one_sampled_original_row0_index", "mapping_null_status": "unobserved"})
        meta = {"complete": True, "gpu_status": 4, "record_bytes": len(words) * 4,
                "schema": schema, "shader_hash": "e4abfca709b00ef3", "sample_mask": 63, "sites": sites,
                "frame": 100, "window_first_frame": 100, "window_frames": 1, "record_slot": 0, "draw": 1}
        return words, meta

    def analyze(self, words, meta):
        self.path.write_text(json.dumps({**meta, "record_words": words}))
        self.path.with_suffix(".bin").write_bytes(struct.pack(f"<{len(words)}I", *words))
        return ANALYZER.analyze(self.root)

    def test_legacy_does_not_claim_camera_observation(self):
        result = self.analyze(*self.fixture(2))
        self.assertEqual(result["invalid_records"], 0)
        self.assertEqual(result["wind"]["eligible_observations"], 1)
        self.assertEqual(result["camera"]["recorded_observations"], 0)
        self.assertEqual(result["camera"]["eligible_observations"], 0)

    def test_equal_nan_and_signed_words_preserved(self):
        result = self.analyze(*self.fixture())
        for name in ("wind", "view_projection", "camera"):
            self.assertEqual(result[name]["eligible_observations"], 1)
            self.assertEqual(result[name]["varied_draws"], 0)

    def test_camera_only_variation(self):
        words, meta = self.fixture()
        words[71] += 1
        result = self.analyze(words, meta)
        self.assertEqual(result["camera"]["varied_draws_with_uniform_leaf"], 1)
        self.assertEqual(result["wind"]["varied_draws"], 0)
        self.assertEqual(result["view_projection"]["varied_draws"], 0)
        self.assertEqual(result["observations"][0]["camera"]["varied_components"], [2])

    def test_schema3_matrix_is_unobserved(self):
        result = self.analyze(*self.fixture(3))
        self.assertEqual(result["camera"]["eligible_observations"], 1)
        self.assertEqual(result["object_matrix"]["recorded_observations"], 0)
        self.assertEqual(result["object_matrix"]["eligible_observations"], 0)

    def test_stable_zero_matrix_does_not_prove_mapping(self):
        result = self.analyze(*self.fixture())
        self.assertEqual(result["object_matrix"]["eligible_observations"], 1)
        self.assertEqual(result["object_matrix"]["varied_draws"], 0)
        self.assertFalse(result["object_matrix"]["mapped_input_proven"])
        self.assertEqual(result["object_matrix"]["mapping_null_status"], "unobserved")

    def test_matrix_captured_padding_and_zero_padding_are_known(self):
        for word1 in (0x00108080, 0x00100080):
            with self.subTest(word1=word1):
                words, meta = self.fixture()
                words[83] = words[87] = word1
                words[119] += 1
                result = self.analyze(words, meta)
                self.assertEqual(result["object_matrix"]["varied_draws_with_uniform_identity"], 1)
                self.assertTrue(result["observations"][0]["object_matrix"]["plain_addressing_known"])

    def test_matrix_within_index_variation_and_identity(self):
        words, meta = self.fixture()
        words[119] = 0x7FC12345
        result = self.analyze(words, meta)
        self.assertEqual(result["object_matrix"]["varied_draws_with_uniform_identity"], 1)
        self.assertEqual(result["observations"][0]["object_matrix"]["varied_components"], [11])
        words[89] += 1  # descriptor flags changed, so full identity is unproven
        result = self.analyze(words, meta)
        self.assertEqual(result["object_matrix"]["varied_draws"], 1)
        self.assertEqual(result["object_matrix"]["varied_draws_with_uniform_identity"], 0)

    def test_matrix_counter_and_index_violations_reject(self):
        for offset in (72, 90, 93):
            with self.subTest(offset=offset):
                words, meta = self.fixture()
                words[offset] += 1
                self.assertEqual(self.analyze(words, meta)["object_matrix"]["eligible_observations"], 0)

    def test_matrix_spurious_claim_misses_do_not_claim_missing_coverage(self):
        words, meta = self.fixture()
        words[72:78] = [4, 1, 2, 1, 0, 3]
        result = self.analyze(words, meta)
        self.assertEqual(result["object_matrix"]["eligible_observations"], 1)
        self.assertEqual(result["object_matrix"]["selected_invocations"], 1)
        self.assertEqual(result["object_matrix"]["varied_draws"], 0)
        words[72:78] = [4, 0, 0, 4, 0, 0xFFFFFFFF]
        result = self.analyze(words, meta)
        self.assertEqual(result["object_matrix"]["eligible_observations"], 0)
        self.assertTrue(result["observations"][0]["object_matrix"]["accounting_valid"])

    def test_matrix_unknown_address_forms_do_not_prove_uniform_identity(self):
        descriptors = [
            [0x71234000, 0x108080 | (1 << bit), 6, 0x77FAC] for bit in (31, 30, 8, 9, 10, 11, 12, 14)
        ] + [[0x71234000, stride | 0x80, 6, 0x77FAC] for stride in (0, 32 << 16)] + [
            [0x71234000, 0x108080, 6, 0x77FAC ^ (1 << bit)] for bit in (0, 19, 21, 23, 30)
        ] + [[0x71234001, 0x100080, 6, 0x77FAC], [0xFFFFFFF0, 0x1000FF, 6, 0x77FAC]]
        for descriptor in descriptors:
            with self.subTest(descriptor=descriptor):
                words, meta = self.fixture()
                words[82:86] = words[86:90] = descriptor
                words[78:82] = [descriptor[0], descriptor[0], descriptor[1] & 0xFFF, descriptor[1] & 0xFFF]
                words[119] += 1
                result = self.analyze(words, meta)
                self.assertEqual(result["object_matrix"]["varied_draws"], 1)
                self.assertEqual(result["object_matrix"]["varied_draws_with_uniform_identity"], 0)
                self.assertFalse(result["observations"][0]["object_matrix"]["plain_addressing_known"])

    def test_camera_base_variation_is_distinct(self):
        words, meta = self.fixture()
        words[69] += 1
        words[63] += 1
        result = self.analyze(words, meta)
        self.assertEqual(result["camera"]["varied_draws"], 1)
        self.assertEqual(result["camera"]["varied_draws_with_uniform_leaf"], 0)

    def test_camera_null_and_zero_samples_do_not_count(self):
        for samples, null in ((0, 0), (4, 1)):
            with self.subTest(samples=samples, null=null):
                words, meta = self.fixture()
                words[60], words[61] = samples, null
                result = self.analyze(words, meta)
                self.assertEqual(result["camera"]["eligible_observations"], 0)
                self.assertEqual(result["wind"]["eligible_observations"], 1)

    def test_global_failures_reject_all_sites(self):
        for key, value in (("complete", False), ("gpu_status", 5), ("record_bytes", 240),
                           ("schema", 9), ("schema", []), ("frame", 99)):
            with self.subTest(key=key, value=value):
                words, meta = self.fixture()
                meta[key] = value
                result = self.analyze(words, meta)
                self.assertEqual(result["invalid_records"], 1)
                self.assertTrue(all(result[name]["eligible_observations"] == 0 for name in ("wind", "view_projection", "camera")))

    def test_overflow_and_wrong_camera_site_rejected(self):
        words, meta = self.fixture()
        words[2] = 1
        self.assertEqual(self.analyze(words, meta)["camera"]["eligible_observations"], 0)
        words, meta = self.fixture()
        meta["sites"][2]["original_pcs"][0] = 0x1504
        self.assertEqual(self.analyze(words, meta)["invalid_records"], 1)

    def test_nonobject_metadata_rejected(self):
        self.path.write_text("[]")
        self.assertEqual(ANALYZER.analyze(self.root)["invalid_records"], 1)


if __name__ == "__main__":
    unittest.main()
