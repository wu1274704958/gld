from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

import numpy
from PIL import Image


TOOL_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_DIR))

import aoe2de_export as exporter  # noqa: E402


class FakeDecodedFrame:
    def __init__(self, layer: int, record: dict[str, int], player_mismatch: bool = False):
        self.source_ordinal = record["ordinal"]
        self.source_frame_index = record["frame_index"]
        if layer == 1:
            self.width, self.height = 3, 2
            self.hotspot = (1, 1)
            color = (80, 0, 0, 255)
        else:
            self.width, self.height = 2, 3
            self.hotspot = (1, 2)
            color = (140, 140, 140, 255) if layer == 0 else (255, 0, 0, 255)
            if layer == 4 and player_mismatch:
                self.width = 3
        self._image = Image.new("RGBA", (self.width, self.height), color)

    def get_pil_image(self):
        return self._image


class FakeSLD:
    def __init__(self, data: bytes):
        self.records = exporter.read_sld_frame_records(data)


class FakeTexture:
    player_mismatch = False

    def __init__(self, sld: FakeSLD, layer: int = 0):
        bit = 1 << layer
        self.frames = [
            FakeDecodedFrame(layer, record, self.player_mismatch)
            for record in sld.records
            if record["frame_type"] & bit
        ]


def fake_dat(prefix: str = "u_test"):
    combat = SimpleNamespace(
        projectile_unit_id=3, frame_delay=5,
        graphic_displacement=(0.0, 0.16, 0.8), accuracy_percent=80,
        accuracy_dispersion=0.5, min_range=0.0, max_range=4.0,
        reload_time=2.0, blast_width=0.0, blast_attack_level=0,
        attack_graphic=1, attack_graphic_2=-1,
    )
    creatable = SimpleNamespace(
        secondary_projectile_unit=-1, total_projectiles=1.0,
        max_total_projectiles=1, projectile_spawning_area=(0.0, 0.0, 0.0),
    )
    unit = SimpleNamespace(
        id=4, name="TEST", type=70, standing_graphic=(0, -1),
        dying_graphic=-1, undead_graphic=-1, dead_fish=None,
        damage_graphics=[], collision_size_x=0.2, collision_size_y=0.2,
        collision_size_z=0.8, outline_size_x=0.3, outline_size_y=0.4,
        outline_size_z=0.9, type_50=combat, creatable=creatable,
    )
    graphics = [
        SimpleNamespace(file_name=f"{prefix}_idleA_x2"),
        SimpleNamespace(file_name=f"{prefix}_attackA_x2"),
    ]
    return SimpleNamespace(
        graphics=graphics,
        civs=[SimpleNamespace(units=[None, None, None, None, unit])],
    )


def make_sld(frame_types: list[int]) -> bytes:
    data = bytearray(exporter.SLD_HEADER.pack(
        b"SLD0", 1, len(frame_types), 0, 0, 0
    ))
    for index, frame_type in enumerate(frame_types):
        data.extend(exporter.SLD_FRAME_HEADER.pack(
            16, 16, 8, 12, frame_type, 0, index
        ))
        for bit in exporter.LAYER_BITS.values():
            if frame_type & bit:
                data.extend(exporter.SLD_LAYER_LENGTH.pack(4))
        while len(data) % 4:
            data.append(0)
    return bytes(data)


class ExporterTests(unittest.TestCase):
    def tearDown(self):
        FakeTexture.player_mismatch = False

    def make_tree(self, root: Path, frame_types: list[int], action: str = "idleA"):
        aoe2 = root / "aoe2"
        graphics = exporter.graphics_dir(aoe2)
        graphics.mkdir(parents=True)
        source = graphics / f"u_test_{action}_x2.sld"
        source.write_bytes(make_sld(frame_types))
        return aoe2, source

    def run_fake(self, argv: list[str]):
        stdout = io.StringIO()
        with mock.patch.object(exporter, "load_openage", return_value=(FakeSLD, FakeTexture)), \
             mock.patch.object(exporter, "load_dat", return_value=fake_dat()):
            with contextlib.redirect_stdout(stdout):
                result = exporter.main(argv)
        return result, stdout.getvalue()

    def test_cli_defaults_and_positive_custom_values(self):
        defaults = exporter.parse_args([])
        self.assertEqual(16, defaults.directions)
        self.assertEqual(30.0, defaults.fps)
        custom = exporter.parse_args(["--directions", "32", "--fps", "24.5"])
        self.assertEqual(32, custom.directions)
        self.assertEqual(24.5, custom.fps)

    def test_invalid_numeric_values_are_rejected(self):
        for argv in (["--directions", "0"], ["--fps", "0"], ["--fps", "nan"]):
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit):
                    exporter.parse_args(list(argv))

    def test_root_layout_cleanup_schema_layers_and_foot(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, _ = self.make_tree(root, [0x17] * 4)
            out_root = root / "cache"
            old_target = out_root / "u_test"
            old_target.mkdir(parents=True)
            (old_target / "stale.txt").write_text("old", encoding="utf-8")

            result, _stdout = self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "2",
            ])
            self.assertEqual(0, result)
            self.assertFalse((old_target / "stale.txt").exists())
            manifest = json.loads((old_target / "manifest.json").read_text())
            self.assertEqual(3, manifest["schema_version"])
            self.assertEqual("graphic_unique", manifest["dat"]["mapping_source"])
            self.assertEqual(4, manifest["dat"]["unit_id"])
            self.assertEqual(
                {"x": 0.0, "y": 0.16, "z": 0.8},
                manifest["dat"]["combat"]["weapon_offset"],
            )
            self.assertTrue(manifest["summary"]["complete"])
            record = manifest["animations"]["idleA"]
            self.assertEqual("exported", record["status"])
            self.assertEqual("complete", record["layers"]["main"])
            self.assertEqual("complete", record["layers"]["shadow"])
            self.assertEqual("unsupported", record["layers"]["outline"])
            self.assertEqual("missing", record["layers"]["damage"])
            self.assertEqual("complete", record["layers"]["player_color"])

            config = json.loads((old_target / record["config"]).read_text())
            frame = config["layers"]["main"]["frames"][0]
            self.assertEqual(
                {"x": 1, "y": 2, "space": "frame_pixels_top_left"},
                frame["foot"],
            )
            self.assertEqual(
                config["layers"]["main"]["atlas_w"],
                config["layers"]["player_color"]["atlas_w"],
            )
            mask = Image.open(old_target / "graphics" / "idleA_playercolor.png").convert("RGBA")
            pixels = numpy.asarray(mask)
            self.assertTrue(numpy.all(pixels[:, :, 0] <= 7))
            self.assertTrue(numpy.all(pixels[:, :, 1:3] == 0))
            self.assertTrue(set(numpy.unique(pixels[:, :, 3])).issubset({0, 255}))

    def test_remainder_is_dropped_before_packing_and_warned(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, _ = self.make_tree(root, [0x17] * 5)
            out_root = root / "cache"
            result, stdout = self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "2",
            ])
            self.assertEqual(0, result)
            self.assertIn("dropping 1 trailing frame", stdout)
            config = json.loads(
                (out_root / "u_test" / "graphics" / "idleA.json").read_text()
            )
            self.assertEqual(5, config["source_frame_count"])
            self.assertEqual(4, config["exported_frame_count"])
            self.assertEqual(2, config["frames_per_direction"])
            self.assertEqual([4], config["unused_source_frames"])
            self.assertEqual(4, len(config["layers"]["main"]["frames"]))
            self.assertEqual("direction_remainder", config["warnings"][0]["code"])

    def test_partial_shadow_gets_transparent_placeholder(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            # Frame 1 has main, outline and player-color, but no shadow.
            aoe2, _ = self.make_tree(root, [0x17, 0x15, 0x17, 0x17])
            out_root = root / "cache"
            self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "2",
            ])
            config = json.loads(
                (out_root / "u_test" / "graphics" / "idleA.json").read_text()
            )
            shadow = config["layers"]["shadow"]
            self.assertEqual("partial", shadow["status"])
            self.assertEqual([1], shadow["missing_source_frames"])
            self.assertFalse(shadow["frames"][1]["present"])

    def test_partial_player_color_uses_main_layout_placeholder(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            # Frame 1 has main, shadow and outline, but no player-color.
            aoe2, _ = self.make_tree(root, [0x17, 0x07, 0x17, 0x17])
            out_root = root / "cache"
            self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "2",
            ])
            config = json.loads(
                (out_root / "u_test" / "graphics" / "idleA.json").read_text()
            )
            player = config["layers"]["player_color"]
            main = config["layers"]["main"]
            self.assertEqual("partial", player["status"])
            self.assertEqual([1], player["missing_source_frames"])
            self.assertFalse(player["frames"][1]["present"])
            self.assertEqual(main["atlas_w"], player["atlas_w"])
            self.assertEqual(main["atlas_h"], player["atlas_h"])

    def test_misaligned_player_color_is_invalid_and_image_is_removed(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, _ = self.make_tree(root, [0x17] * 4)
            out_root = root / "cache"
            FakeTexture.player_mismatch = True
            self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "2",
            ])
            config = json.loads(
                (out_root / "u_test" / "graphics" / "idleA.json").read_text()
            )
            self.assertEqual("invalid", config["layers"]["player_color"]["status"])
            self.assertFalse(
                (out_root / "u_test" / "graphics" / "idleA_playercolor.png").exists()
            )

    def test_more_directions_than_frames_marks_animation_invalid(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, _ = self.make_tree(root, [0x17] * 4)
            out_root = root / "cache"
            result, _stdout = self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA",
                "--directions", "8",
            ])
            self.assertEqual(0, result)
            manifest = json.loads(
                (out_root / "u_test" / "manifest.json").read_text()
            )
            self.assertEqual("invalid", manifest["animations"]["idleA"]["status"])
            self.assertFalse(manifest["summary"]["complete"])

    def test_missing_requested_animation_is_recorded_and_returns_success(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, _ = self.make_tree(root, [0x17] * 4)
            out_root = root / "cache"
            result, stdout = self.run_fake([
                "--aoe2", str(aoe2), "--out", str(out_root),
                "--unit", "u_test", "--animations", "idleA", "danceA",
                "--directions", "2",
            ])
            self.assertEqual(0, result)
            self.assertIn("danceA", stdout)
            manifest = json.loads(
                (out_root / "u_test" / "manifest.json").read_text()
            )
            self.assertFalse(manifest["summary"]["complete"])
            self.assertEqual(["danceA"], manifest["missing_animations"])
            self.assertEqual("missing_source", manifest["animations"]["danceA"]["status"])

    def test_graphics_requires_name(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, source = self.make_tree(root, [0x17] * 4)
            with self.assertRaisesRegex(SystemExit, "--name is required"):
                exporter.main([
                    "--aoe2", str(aoe2), "--out", str(root / "cache"),
                    "--graphics", source.name,
                ])

    def test_dat_mapping_priority_explicit_map_and_unique(self):
        dat = fake_dat()
        unique = exporter.resolve_dat_unit(dat, "u_test", 0, None, {})
        self.assertEqual((0, 4, "graphic_unique"),
                         (unique.civ_id, unique.unit_id, unique.mapping_source))
        mapped = exporter.resolve_dat_unit(
            dat, "shared_special", 0, None,
            {"shared_special": {"civ_id": 0, "unit_id": 4}},
        )
        self.assertEqual("map", mapped.mapping_source)
        explicit = exporter.resolve_dat_unit(
            dat, "shared_special", 0, 4,
            {"shared_special": {"civ_id": 0, "unit_id": 999}},
        )
        self.assertEqual((4, "explicit"), (explicit.unit_id, explicit.mapping_source))

    def test_dat_mapping_zero_and_ambiguous_candidates_are_diagnostic(self):
        dat = fake_dat()
        with self.assertRaisesRegex(SystemExit, "0 candidate"):
            exporter.resolve_dat_unit(dat, "missing", 0, None, {})
        duplicate = SimpleNamespace(**vars(dat.civs[0].units[4]))
        duplicate.name = "OTHER"
        dat.civs[0].units.append(duplicate)
        with self.assertRaisesRegex(SystemExit, "2 candidate") as raised:
            exporter.resolve_dat_unit(dat, "u_test", 0, None, {})
        message = str(raised.exception)
        self.assertIn("unit=4", message)
        self.assertIn("unit=5", message)
        self.assertIn("u_test_idleA_x2", message)

    def test_dat_metadata_serializes_combat_creatable_and_sentinels(self):
        dat = fake_dat()
        match = exporter.resolve_dat_unit(dat, "u_test", 0, None, {})
        metadata = exporter.serialize_dat_metadata(
            Path("aoe2/resources/_common/dat/empires2_x2_p1.dat"),
            Path("aoe2"), match,
        )
        self.assertEqual({"x": 0.2, "y": 0.2, "z": 0.8},
                         metadata["collision_size"])
        self.assertEqual({"x": 0.3, "y": 0.4, "z": 0.9},
                         metadata["outline_size"])
        combat = metadata["combat"]
        self.assertEqual(-1, combat["secondary_projectile_unit_id"])
        self.assertEqual(1, combat["projectile_min_count"])
        self.assertEqual(
            {"width": 0.0, "length": 0.0, "randomness": 0.0},
            combat["projectile_spawning_area"],
        )
        dat.civs[0].units[4].type_50 = None
        dat.civs[0].units[4].creatable = None
        without_optional = exporter.serialize_dat_metadata(
            Path("external.dat"), Path("aoe2"), match,
        )
        self.assertNotIn("combat", without_optional)
        self.assertNotIn("creatable", without_optional)

    def test_dat_metadata_rejects_invalid_outline(self):
        dat = fake_dat()
        match = exporter.resolve_dat_unit(dat, "u_test", 0, None, {})
        for attribute, value in (("outline_size_x", -0.1),
                                 ("outline_size_y", float("nan")),
                                 ("outline_size_z", float("inf"))):
            unit = dat.civs[0].units[4]
            original = getattr(unit, attribute)
            setattr(unit, attribute, value)
            with self.assertRaisesRegex(exporter.ExportError,
                                        "finite and non-negative"):
                exporter.serialize_dat_metadata(Path("test.dat"), Path("."), match)
            setattr(unit, attribute, original)

    def test_graphics_export_never_loads_dat(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            aoe2, source = self.make_tree(root, [0x17] * 4)
            with mock.patch.object(exporter, "load_openage",
                                   return_value=(FakeSLD, FakeTexture)), \
                 mock.patch.object(exporter, "load_dat",
                                   side_effect=AssertionError("DAT was loaded")):
                result = exporter.main([
                    "--aoe2", str(aoe2), "--out", str(root / "cache"),
                    "--name", "graphic_only", "--graphics", source.name,
                    "--directions", "2",
                ])
            self.assertEqual(0, result)
            manifest = json.loads(
                (root / "cache" / "graphic_only" / "manifest.json").read_text()
            )
            self.assertEqual(2, manifest["schema_version"])
            self.assertNotIn("dat", manifest)

    def test_missing_genieutils_has_actionable_install_hint(self):
        original_import = __import__

        def import_without_genieutils(name, *args, **kwargs):
            if name.startswith("genieutils"):
                raise ImportError("not installed")
            return original_import(name, *args, **kwargs)

        with mock.patch("builtins.__import__", side_effect=import_without_genieutils):
            with self.assertRaisesRegex(
                    SystemExit, r"python -m pip install genieutils-py"):
                exporter.load_dat(Path("unused.dat"))


if __name__ == "__main__":
    unittest.main()
