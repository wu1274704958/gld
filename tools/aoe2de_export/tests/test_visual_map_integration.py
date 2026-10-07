"""Opt-in real DAT/SLD/scenario test; no proprietary fixture is checked in.

Set AOE2_VISUAL_MAP_TEST_ROOT to a local DE install and install requirements-visual-map.txt.
"""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import aoe2de_export as api
import visual_map as vm


@unittest.skipUnless(os.environ.get("AOE2_VISUAL_MAP_TEST_ROOT"), "requires local DE assets (opt-in)")
class VisualMapIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.aoe = Path(os.environ["AOE2_VISUAL_MAP_TEST_ROOT"])
        cls.dat = api.load_dat(api.dat_path_for(cls.aoe))

    def test_real_scenario_roundtrip_and_visual_resource_export(self):
        from AoE2ScenarioParser import settings
        from AoE2ScenarioParser.scenarios.aoe2_de_scenario import AoE2DEScenario
        old = settings.PRINT_STATUS_UPDATES
        settings.PRINT_STATUS_UPDATES = False
        self.addCleanup(setattr, settings, "PRINT_STATUS_UPDATES", old)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = Path(directory)
            scenario = AoE2DEScenario.from_default(scenario_version="1.58")
            scenario.map_manager.map_size = 16
            scenario.map_manager.terrain[1].terrain_id = 6
            scenario.map_manager.terrain[2].terrain_id = 24
            scenario.map_manager.terrain[2].layer = 0
            scenario.map_manager.terrain[2].elevation = 2
            for x, unit_id in enumerate([349, 349, 102, 66, 70, 4, 1613], 1):
                scenario.unit_manager.add_unit(player=0, unit_const=unit_id, x=float(x), y=2.5)
            # This must never be copied into the bundle.
            scenario.trigger_manager.add_trigger("DO_NOT_EXPORT_GAMEPLAY")
            source = root / "fixture.aoe2scenario"
            scenario.write_to_file(str(source))
            args = api.parse_args(["--aoe2", str(self.aoe), "--out", str(root / "cache"),
                                   "--visual-map", str(source), "--name", "fixture"])
            with mock.patch.object(api, "load_dat", return_value=self.dat):
                self.assertEqual(vm.export_visual_map(args, api), 0)
            output = root / "cache/maps/fixture"
            manifest = json.loads((output / "manifest.json").read_text())
            self.assertTrue(manifest["summary"]["complete"])
            # Metadata keeps all placed objects, including visual-filtered ones.
            self.assertEqual(manifest["summary"]["object_count"], 7)
            self.assertEqual(manifest["summary"]["tile_count"], 256)
            objects = json.loads((output / "objects.json").read_text())
            self.assertEqual(objects[0]["appearance"], objects[1]["appearance"])
            graphics = json.loads((output / "graphics.json").read_text())
            self.assertTrue(any(g["particle"] for g in graphics))  # gold shimmer
            self.assertEqual(list(vm.TILE.iter_unpack((output / "terrain.bin").read_bytes()))[2], (24, 0, 2))
            for json_path in output.rglob("*.json"):
                self.assertNotIn("DO_NOT_EXPORT_GAMEPLAY", json_path.read_text(encoding="utf-8"))
            for entry in json.loads((output / "assets.json").read_text()):
                self.assertTrue((output / entry["path"]).is_file())

    def test_town_center_annexes_are_preserved_and_missing_source_is_reported(self):
        from AoE2ScenarioParser import settings
        from AoE2ScenarioParser.scenarios.aoe2_de_scenario import AoE2DEScenario
        old = settings.PRINT_STATUS_UPDATES
        settings.PRINT_STATUS_UPDATES = False
        self.addCleanup(setattr, settings, "PRINT_STATUS_UPDATES", old)
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            root = Path(directory)
            scenario = AoE2DEScenario.from_default(scenario_version="1.58")
            scenario.map_manager.map_size = 16
            scenario.unit_manager.add_unit(player=0, unit_const=109, x=8.0, y=8.0)
            args = api.parse_args(["--aoe2", str(self.aoe), "--out", str(root / "cache"),
                                   "--visual-map", "test.aoe2scenario", "--name", "town",
                                   "--map-allow-incomplete"])
            manifest = vm.write_bundle(args, api, scenario, self.dat, root)
            appearances = json.loads((root / "appearances.json").read_text())
            self.assertTrue(any(a["attachments"] for a in appearances))
            # The local DE install has a DAT reference to s_town_center_extra_x1
            # but no corresponding SLD; never silently declare this complete.
            if not (self.aoe / "resources/_common/drs/graphics/s_town_center_extra_x1.sld").is_file():
                self.assertFalse(manifest["summary"]["complete"])
                self.assertTrue(manifest["summary"]["issues"])

    def test_unsupported_old_scenario_has_actionable_error(self):
        source = self.aoe / "resources/_common/campaign/0_E3_Scenario.aoe2scenario"
        if not source.is_file():
            self.skipTest("optional legacy E3 scenario not installed")
        # The shipped legacy fixture is 1.35, before DE scenario parser support.
        with self.assertRaisesRegex(vm.VisualMapError, "save a copy"):
            vm.load_scenario(source)


if __name__ == "__main__":
    unittest.main()
