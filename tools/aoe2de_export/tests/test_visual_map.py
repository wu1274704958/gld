from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace as NS
from unittest import mock

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import aoe2de_export as api
import visual_map as vm


def terrain(name="grass", alias=-1, water=32):
    return NS(name=name, name_2=name, terrain_to_draw=alias, overlay_mask_name="",
              terrain_dimensions=(10, 10), blend_priority=100, blend_type=0, is_water=water)


def graphic(name="tree_x1", deltas=(), directions=1, frames=1):
    return NS(file_name=name, angle_count=directions, frame_count=frames, sequence_type=6,
              frame_duration=0.0, mirroring_mode=0, deltas=list(deltas))


def obj(unit_id=0, player=0, **kwargs):
    return NS(unit_const=unit_id, player=player, reference_id=123, x=1.25, y=2.5,
              z=0.0, rotation=11.0, initial_animation_frame=11,
              garrisoned_in_id=kwargs.get("garrison", -1))


def scene(objects=()):
    return NS(scenario_version="1.58",
              map_manager=NS(map_width=2, map_height=1, map_color_mood="summer",
                             terrain=[NS(terrain_id=0, layer=-1, elevation=0),
                                      NS(terrain_id=0, layer=-1, elevation=3)]),
              unit_manager=NS(get_all_units=lambda: list(objects)),
              player_manager=NS(players=[NS(player_id=0, color=8, civilization=0)]))


class VisualMapTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.aoe = self.root / "aoe"
        self.common = self.aoe / "resources/_common"
        for directory in ("terrain/textures/2x", "terrain/blends", "terrain/colorcorrection_json",
                          "terrain/water", "terrain/water_json", "drs/graphics"):
            (self.common / directory).mkdir(parents=True, exist_ok=True)
        Image.new("RGB", (8, 8), (30, 120, 50)).save(self.common / "terrain/textures/2x/grass.dds")
        self.output = self.root / "output"
        self.output.mkdir()
        self.scenario_path = self.root / "test.aoe2scenario"
        self.scenario_path.write_bytes(b"fixture")
        self.args = api.parse_args(["--aoe2", str(self.aoe), "--out", str(self.root / "cache"),
                                    "--visual-map", str(self.scenario_path), "--name", "sample"])
        self.dat = NS(terrain_block=NS(terrains=[terrain()]), graphics=[graphic()],
                      civs=[NS(units=[NS(type=10, standing_graphic=(0, -1))])])

    def test_static_object_library_requires_its_graphics(self):
        dat_path = self.root / "test.dat"
        dat_path.write_bytes(b"dat-fixture")
        library = self.root / "static-library"
        library.mkdir()
        index = {"schema_version": 1, "kind": "aoe2de_static_object_index",
                 "id": "static-library", "dat_sha256": vm.sha256_file(dat_path),
                 "resource_prefix": "map_test_g",
                 "objects": [{"source_unit_id": 351, "graphic_id": 2306}]}
        api.write_json(library / "index.json", index)
        api.write_json(library / "manifest.json", {
            "schema_version": 1, "kind": "aoe2de_static_object_library",
            "id": "static-library", "dat_sha256": vm.sha256_file(dat_path),
            "index_sha256": vm.sha256_file(library / "index.json")})
        with self.assertRaisesRegex(vm.VisualMapError, "graphic missing"):
            vm.load_static_object_library(library, dat_path)
        graphic_dir = library / "graphics/map_test_g2306"
        graphic_dir.mkdir(parents=True)
        api.write_json(graphic_dir / "manifest.json", {"id": "map_test_g2306"})
        self.assertEqual(vm.load_static_object_library(library, dat_path)["id"], "static-library")

    def test_grid_layout_and_layers(self):
        manager = scene().map_manager
        manager.terrain[1].layer = 2
        packed, used, width, height = vm.pack_terrain(manager)
        self.assertEqual((width, height, used), (2, 1, {0, 2}))
        self.assertEqual(len(packed), 16)
        self.assertEqual(list(vm.TILE.iter_unpack(packed)), [(0, -1, 0), (0, 2, 3)])
        self.assertEqual(packed[5:8], b"\0\0\0")

    def test_grid_invalid_sizes_counts_values(self):
        for field, value in [("map_width", 0), ("map_width", 1025), ("map_height", -1)]:
            manager = scene().map_manager
            setattr(manager, field, value)
            with self.assertRaises(vm.VisualMapError):
                vm.pack_terrain(manager)
        manager = scene().map_manager
        manager.terrain.pop()
        with self.assertRaises(vm.VisualMapError):
            vm.pack_terrain(manager)
        for field, value in [("terrain_id", -1), ("layer", -2), ("elevation", 256), ("elevation", 0.5)]:
            manager = scene().map_manager
            setattr(manager.terrain[0], field, value)
            with self.assertRaises(vm.VisualMapError):
                vm.pack_terrain(manager)

    def test_asset_dedup_hash_and_containment(self):
        copier = vm.AssetCopier(self.common, self.output)
        a = copier.copy("terrain/textures/2x/grass.dds")
        self.assertEqual(a, copier.copy("terrain/textures/2x/grass.dds"))
        self.assertEqual(len(copier.records), 1)
        self.assertEqual(len(next(iter(copier.records.values()))["sha256"]), 64)
        with self.assertRaises(vm.VisualMapError):
            copier.copy("../../../test.aoe2scenario")

    def test_material_alias_and_shared_texture(self):
        self.dat.terrain_block.terrains += [terrain(alias=0), terrain()]
        copier = vm.AssetCopier(self.common, self.output)
        records = vm.export_materials(self.dat, {1, 2}, copier, vm.Report(False))
        self.assertEqual([r["id"] for r in records], [0, 1, 2])
        self.assertEqual(records[1]["draw_as"], 0)
        self.assertEqual(len(copier.records), 1)
        self.assertEqual(records[0]["water_flags_raw"], 32)
        self.assertNotIn("passable_terrain", records[0])

    def test_shared_terrain_library_and_compact_map(self):
        dat_path = self.root / "test.dat"
        dat_path.write_bytes(b"fixed DAT version")
        self.args.dat = dat_path
        self.args.terrain_library = True
        self.assertEqual(vm.export_terrain_library(self.args, mock.Mock(load_dat=lambda _: self.dat,
                                                                          validate_resource_id=api.validate_resource_id,
                                                                          write_json=api.write_json,
                                                                          dat_path_for=api.dat_path_for)), 0)
        library = self.root / "cache/terrain-libraries/sample"
        self.assertTrue((library / "source/terrain/textures/2x/grass.dds").is_file())
        self.assertEqual(json.loads((library / "manifest.json").read_text())["terrain_count"], 1)
        self.args.map_terrain_library = library
        self.args.terrain_library = False
        self.args.map_objects = "none"
        with mock.patch.object(vm, "load_scenario", return_value=scene([obj()])), \
             mock.patch.object(api, "load_dat", return_value=self.dat):
            self.assertEqual(vm.export_visual_map(self.args, api), 0)
        compact = self.root / "cache/maps/sample"
        manifest = json.loads((compact / "manifest.json").read_text())
        self.assertEqual(manifest["schema_version"], vm.SOURCE_MAP_SCHEMA)
        self.assertEqual(manifest["summary"]["object_count"], 1)
        self.assertEqual(sorted(path.name for path in compact.iterdir()),
                         ["instance-metadata.json", "manifest.json", "terrain.bin"])
        self.assertNotIn("instantiate_in_v1", (compact / "instance-metadata.json").read_text())

    def test_shared_library_rejects_wrong_dat_or_missing_asset(self):
        dat_path = self.root / "test.dat"
        dat_path.write_bytes(b"A")
        self.args.dat = dat_path
        self.args.terrain_library = True
        fake_api = mock.Mock(load_dat=lambda _: self.dat, validate_resource_id=api.validate_resource_id,
                             write_json=api.write_json, dat_path_for=api.dat_path_for)
        vm.export_terrain_library(self.args, fake_api)
        library = self.root / "cache/terrain-libraries/sample"
        dat_path.write_bytes(b"B")
        with self.assertRaisesRegex(vm.VisualMapError, "different DAT"):
            vm.load_terrain_library(library, dat_path, {0})
        dat_path.write_bytes(b"A")
        (library / "source/terrain/textures/2x/grass.dds").unlink()
        with self.assertRaisesRegex(vm.VisualMapError, "missing shared asset"):
            vm.load_terrain_library(library, dat_path, {0})
        (library / "source/terrain/textures/2x/grass.dds").write_bytes(b"corrupt")
        with self.assertRaisesRegex(vm.VisualMapError, "shared asset hash mismatch"):
            vm.load_terrain_library(library, dat_path, {0})

    def test_material_alias_cycle(self):
        self.dat.terrain_block.terrains = [terrain(alias=1), terrain(alias=0)]
        with self.assertRaisesRegex(vm.VisualMapError, "cycle"):
            vm.export_materials(self.dat, {0}, vm.AssetCopier(self.common, self.output), vm.Report(False))

    def test_missing_texture_strict_and_partial(self):
        self.dat.terrain_block.terrains[0].name_2 = "absent"
        with self.assertRaises(vm.VisualMapError):
            vm.export_materials(self.dat, {0}, vm.AssetCopier(self.common, self.output), vm.Report(False))
        report = vm.Report(True)
        materials = vm.export_materials(self.dat, {0, 999}, vm.AssetCopier(self.common, self.output), report)
        self.assertTrue(report.issues)
        self.assertEqual(materials[0]["status"], "missing")
        packed, _, width, height = vm.pack_terrain(scene().map_manager)
        vm.make_preview(self.output, packed, width, height, materials, 64)
        with Image.open(self.output / "terrain-preview.png") as image:
            self.assertEqual(image.size, (64, 32))
            self.assertEqual(image.getpixel((0, 0)), (255, 0, 255))

    def test_water_support_is_copied(self):
        self.dat.terrain_block.terrains[0].is_water = 4
        (self.common / "terrain/water_json/water_def.json").write_text('{}')
        copier = vm.AssetCopier(self.common, self.output)
        vm.export_materials(self.dat, {0}, copier, vm.Report(False))
        self.assertIn("terrain/water_json/water_def.json", copier.records)

    def test_graphics_deduplicated_and_metadata_preserved(self):
        (self.common / "drs/graphics/tree_x2.sld").write_bytes(b"fake")
        report = vm.Report(False)
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, report)
        record = {"config": "graphics/g0.json", "layers": {"main": "complete", "shadow": "missing"}}
        with mock.patch.object(api, "read_sld_frame_records", return_value=[{}]), \
             mock.patch.object(api, "load_openage", return_value=(None, None)), \
             mock.patch.object(api, "export_animation", return_value=record) as export:
            a = exporter.export(0)
            self.assertIs(a, exporter.export(0))
        export.assert_called_once()
        self.assertEqual(a["pose_mode"], "variation")
        self.assertFalse(a["animated"])

    def test_graphic_frame_mismatch_not_silently_truncated(self):
        (self.common / "drs/graphics/tree_x2.sld").write_bytes(b"fake")
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        with mock.patch.object(api, "read_sld_frame_records", return_value=[{}, {}]), \
             self.assertRaisesRegex(vm.VisualMapError, "DAT 1x1, SLD 2"):
            exporter.export(0)

    def test_graphic_delta_cycle(self):
        delta = NS(graphic_id=0, offset_x=0, offset_y=0, display_angle=-1)
        self.dat.graphics = [graphic(name="", deltas=[delta])]
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        with self.assertRaisesRegex(vm.VisualMapError, "cycle"):
            exporter.export(0)

    def test_composite_missing_parent_keeps_child(self):
        delta = NS(graphic_id=1, offset_x=3, offset_y=4, display_angle=-1)
        self.dat.graphics = [graphic(name="W", deltas=[delta]), graphic()]
        (self.common / "drs/graphics/tree_x2.sld").write_bytes(b"fake")
        report = vm.Report(True)
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, report)
        with mock.patch.object(api, "read_sld_frame_records", return_value=[{}]), \
             mock.patch.object(api, "load_openage", return_value=(None, None)), \
             mock.patch.object(api, "export_animation", return_value={"config": "child.json", "layers": {}}):
            record = exporter.export(0)
        self.assertEqual(record["status"], "partial")
        self.assertEqual(record["deltas"][0]["graphic"], 1)
        self.assertEqual(exporter.records[1]["config"], "child.json")
        self.assertTrue(report.issues)

    def test_blank_node_is_intentionally_empty(self):
        self.dat.graphics = [graphic(name="None")]
        self.dat.graphics[0].name, self.dat.graphics[0].slp = "BLANK", -1
        record = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False)).export(0)
        self.assertEqual(record["status"], "empty")

    def test_trailing_record_preserved_as_explicit_metadata(self):
        self.dat.graphics = [graphic(directions=2)]
        (self.common / "drs/graphics/tree_x2.sld").write_bytes(b"fake")
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        with mock.patch.object(api, "read_sld_frame_records", return_value=[{}, {}, {}]), \
             mock.patch.object(api, "load_openage", return_value=(None, None)), \
             mock.patch.object(api, "export_animation", return_value={"config": "g.json", "layers": {}}):
            record = exporter.export(0)
        self.assertEqual(record["unused_trailing_records"], 1)

    def test_missing_annex_does_not_discard_later_sibling(self):
        annex = lambda i: NS(unit_id=i, misplacement_x=0, misplacement_y=0)
        self.dat.civs[0].units = [NS(standing_graphic=(-1, -1), building=NS(annexes=[annex(9), annex(1)])),
                                  NS(standing_graphic=(-1, -1))]
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(True))
        key = exporter.appearance(0, 0)
        self.assertEqual(exporter.appearances[key]["attachments"][0]["appearance"], "a0_1")

    def test_objects_have_visual_data_only_and_raw_variant(self):
        self.dat.civs[0].units += [NS(type=70, standing_graphic=(0, -1)),
                                   NS(type=10, standing_graphic=(-1, -1))]
        sample = scene([obj(), obj(1), obj(2), obj(garrison=9)])
        graphics = mock.Mock()
        graphics.appearance.return_value = "a0_0"
        report = vm.Report(False)
        with mock.patch.object(vm, "player_civilizations", return_value={0: 0}):
            result = vm.export_objects(sample, self.dat, graphics, self.args, report)
        self.assertEqual(len(result), 1)
        self.assertEqual(result[0]["position"], [1.25, 2.5, 0.0])
        self.assertEqual(result[0]["rotation_raw"], 11)
        self.assertEqual(result[0]["initial_frame_raw"], 11)
        self.assertEqual(set(result[0]), {"source_reference_id", "appearance", "position", "rotation_raw",
                                         "initial_frame_raw", "source_color_index", "neutral_color"})
        self.assertEqual(report.skipped, {"not_scenery": 1, "no_visible_graphic": 1, "garrisoned": 1})

    def test_all_includes_units_but_no_gp(self):
        self.args.map_objects = "all"
        self.dat.civs[0].units[0].type = 70
        graphics = mock.Mock()
        graphics.export.return_value = {"id": 0}
        with mock.patch.object(vm, "player_civilizations", return_value={0: 0}):
            result = vm.export_objects(scene([obj()]), self.dat, graphics, self.args, vm.Report(False))
        self.assertEqual(len(result), 1)
        self.assertNotIn("unit_id", result[0])

    def test_unknown_player_civ_fails_without_inventing_architecture(self):
        sample = scene([obj(player=1)])
        sample.player_manager.players += [NS(player_id=1, color=0)]
        with mock.patch.object(vm, "player_civilizations", return_value={0: 0, 1: None}), \
             self.assertRaisesRegex(vm.VisualMapError, "unresolved_visual_civ"):
            vm.export_objects(sample, self.dat, mock.Mock(), self.args, vm.Report(False))

    def test_nonfinite_position_rejected(self):
        unit = obj()
        unit.x = float("nan")
        graphics = mock.Mock()
        graphics.export.return_value = {"id": 0}
        with mock.patch.object(vm, "player_civilizations", return_value={0: 0}), \
             self.assertRaisesRegex(vm.VisualMapError, "non-finite"):
            vm.export_objects(scene([unit]), self.dat, graphics, self.args, vm.Report(False))

    def test_issues_aggregate(self):
        report = vm.Report(True)
        for _ in range(100):
            report.problem("missing", "tree")
        self.assertEqual(report.records(), [{"code": "missing", "message": "tree", "count": 100}])

    def test_bundle_is_self_contained_and_excludes_gameplay(self):
        self.args.map_objects = "none"
        sample = scene()
        sample.trigger_manager = mock.PropertyMock(side_effect=AssertionError("do not read triggers"))
        manifest = vm.write_bundle(self.args, api, sample, self.dat, self.output)
        self.assertTrue(manifest["summary"]["complete"])
        self.assertEqual(manifest["summary"]["tile_count"], 2)
        self.assertTrue(manifest["scope"]["visual_only"])
        self.assertEqual(json.loads((self.output / "objects.json").read_text()), [])
        self.assertFalse(manifest["preview"]["includes_blending"])
        for entry in json.loads((self.output / "assets.json").read_text()):
            self.assertTrue((self.output / entry["path"]).is_file())

    def test_publish_success(self):
        self.args.map_objects = "none"
        with mock.patch.object(vm, "load_scenario", return_value=scene()), \
             mock.patch.object(api, "load_dat", return_value=self.dat), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(vm.export_visual_map(self.args, api), 0)
        self.assertTrue((self.args.out / "maps/sample/manifest.json").is_file())
        self.assertEqual(list((self.args.out / "maps").glob(".visual-map-*")), [])

    def test_existing_output_never_overwritten(self):
        target = self.args.out / "maps/sample"
        target.mkdir(parents=True)
        (target / "sentinel").write_text("keep")
        with mock.patch.object(vm, "load_scenario") as load, \
             self.assertRaisesRegex(SystemExit, "already exists"):
            vm.export_visual_map(self.args, api)
        load.assert_not_called()
        self.assertEqual((target / "sentinel").read_text(), "keep")

    def test_failure_never_publishes_partial_bundle(self):
        with mock.patch.object(vm, "load_scenario", return_value=scene()), \
             mock.patch.object(api, "load_dat", return_value=self.dat), \
             mock.patch.object(vm, "write_bundle", side_effect=vm.VisualMapError("failed")), \
             contextlib.redirect_stdout(io.StringIO()), self.assertRaises(SystemExit):
            vm.export_visual_map(self.args, api)
        self.assertFalse((self.args.out / "maps/sample").exists())
        self.assertEqual(list((self.args.out / "maps").iterdir()), [])

    def test_cli_exclusive_mode_and_required_name(self):
        with self.assertRaisesRegex(SystemExit, "only one"):
            api.main(["--visual-map", "x.aoe2scenario", "--unit", "u_test"])
        with self.assertRaisesRegex(SystemExit, "requires --out and --name"):
            api.main(["--visual-map", "x.aoe2scenario"])
        with self.assertRaisesRegex(SystemExit, "cannot be combined"):
            api.main(["--visual-map", "x.aoe2scenario", "--dump-layers"])

    def test_output_traversal_rejected(self):
        self.args.name = "../outside"
        with self.assertRaises(SystemExit):
            vm.export_visual_map(self.args, api)

    def test_preview_allocation_bounded(self):
        self.args.map_preview_size = 100000
        with self.assertRaisesRegex(SystemExit, "1..4096"):
            vm.export_visual_map(self.args, api)

    def test_rms_not_accepted_as_map_layout(self):
        with self.assertRaisesRegex(vm.VisualMapError, "RMS"):
            vm.load_scenario(self.root / "Arabia.rms")

    def test_building_annexes_are_shared_visual_attachments(self):
        annex = NS(unit_id=1, misplacement_x=0.5, misplacement_y=-0.25)
        self.dat.civs[0].units[0].building = NS(annexes=[annex])
        self.dat.civs[0].units += [NS(type=80, standing_graphic=(0, -1))]
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        with mock.patch.object(exporter, "export", return_value={"id": 0}):
            self.assertEqual(exporter.appearance(0, 0), "a0_0")
            self.assertEqual(exporter.appearance(0, 0), "a0_0")
        self.assertEqual(len(exporter.appearances), 2)
        self.assertEqual(exporter.appearances["a0_0"]["attachments"],
                         [{"appearance": "a0_1", "offset_tiles": [0.5, -0.25]}])

    def test_building_annex_cycle(self):
        self.dat.civs[0].units[0].building = NS(annexes=[NS(unit_id=0)])
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        with mock.patch.object(exporter, "export", return_value={"id": 0}), \
             self.assertRaisesRegex(vm.VisualMapError, "cycle"):
            exporter.appearance(0, 0)

    def test_particle_only_graphic_preserves_recipe_and_dependencies(self):
        root = self.common / "particles"
        (root / "textures").mkdir(parents=True)
        (root / "gold.json").write_text(json.dumps({"AtlasFile": "textures/gold.dds", "Type": "Loop"}))
        (root / "textures/gold.dds").write_bytes(b"fixture")
        (root / "textures/gold.json").write_text('{}')
        self.dat.graphics[0].file_name = "None"
        self.dat.graphics[0].particle_effect_name = "gold"
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        result = exporter.export(0)
        self.assertEqual(result["status"], "exported")
        self.assertIsNone(result["config"])
        self.assertEqual(result["particle"]["format"], "aoe2de_atlas_recipe")
        self.assertEqual(len(exporter.copier.records), 3)

    def test_unsafe_dat_texture_name_rejected(self):
        self.dat.terrain_block.terrains[0].name_2 = "../../other"
        with self.assertRaisesRegex(vm.VisualMapError, "unsafe asset name"):
            vm.export_materials(self.dat, {0}, vm.AssetCopier(self.common, self.output), vm.Report(False))

    def test_intentionally_disabled_graphic_is_not_a_missing_asset(self):
        self.dat.graphics = [graphic(name="None", directions=0, frames=0)]
        self.dat.graphics[0].slp = -1
        exporter = vm.GraphicExporter(self.args, api, self.dat, self.output, vm.Report(False))
        self.assertEqual(exporter.export(0)["status"], "empty")


if __name__ == "__main__":
    unittest.main()
