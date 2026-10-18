from paths import ROOT
import unittest
from macos_prepare import settings_patches, RESOLUTIONS
from patches import SCENE_WIDTH, SCENE_HEIGHT, UI_WIDTH, UI_HEIGHT
from test_patches import immediate, SEGMENTS


class MacSettingsTests(unittest.TestCase):
    def test_engine_timing_does_not_overlay_the_legacy_uncap_patch(self):
        writes, names, _ = settings_patches({}, SEGMENTS, engine_timing=True)
        self.assertEqual(names, ['Skip Intro'])
        self.assertNotIn(0x02434883-0x400000, dict(writes))

    def test_default_preserves_effects_and_enables_variable_timestep(self):
        writes, names, size = settings_patches({}, SEGMENTS)
        self.assertEqual(names, ['Skip Intro', 'Uncap FPS++'])
        self.assertEqual(size, (1920,1080))
        self.assertGreater(len(writes), 100)

    def test_all_render_sizes_patch_actual_scene_and_ui_viewports(self):
        for size in RESOLUTIONS - {(1920,1080)}:
            with self.subTest(size=size):
                writes, _, actual = settings_patches({'render_resolution':f'{size[0]}x{size[1]}'},SEGMENTS)
                self.assertEqual(actual,size)
                self.assertEqual(immediate(writes,SCENE_WIDTH)[1],size[0])
                self.assertEqual(immediate(writes,SCENE_HEIGHT)[1],size[1])
                self.assertEqual(immediate(writes,UI_WIDTH)[1],size[0])
                self.assertEqual(immediate(writes,UI_HEIGHT)[1],size[1])

    def test_effects_and_compatibility_are_explicit(self):
        _, names, _ = settings_patches({'effect_chromatic_aberration':'0','effect_game_aa':'garbage'},SEGMENTS)
        self.assertIn('Disable Chromatic Aberration',names)
        self.assertNotIn('Disable AA',names)
        _, names, _ = settings_patches({},SEGMENTS,'compatibility')
        self.assertIn('Disable AA',names)
        self.assertIn('Disable DoF',names)
        self.assertIn('Disable Motion Blur (perf increase)',names)

    def test_invalid_resolutions_keep_native(self):
        for value in ('garbage','4x4','1920x1080x1','0x0'):
            self.assertEqual(settings_patches({'render_resolution':value},SEGMENTS)[2],(1920,1080))


if __name__ == '__main__':
    unittest.main()
