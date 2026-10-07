"""Offline, engine-neutral visual map bundles. Never export simulation definitions.

The tile grid and source material recipes are lossless; the PNG is only a bounded
terrain-ID diagnostic, NOT a DE render or a baked Recoil map. Sprite atlases reuse
the existing block-preserving SLD exporter. Engine loading is a separate adapter.
"""
from __future__ import annotations

import hashlib
import json
import math
import re
import shutil
import struct
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any


SCHEMA_VERSION = 2
TILE = struct.Struct("<HhB3x")  # terrain ID, overlay terrain ID (-1 = none), elevation
SCENERY_TYPES = frozenset((10, 15, 20, 80, 90))
MAX_MAP_SIDE = 1024
MAX_OBJECTS = 1_000_000
TERRAIN_LIBRARY_SCHEMA = 1
SOURCE_MAP_SCHEMA = 3


class VisualMapError(ValueError):
    pass


class IncompleteExport(VisualMapError):
    """An already contextualized strict-mode error; do not wrap it at every parent."""


class Report:
    """Aggregate repeated failures (a forest may contain thousands of one tree)."""
    def __init__(self, allow_incomplete: bool):
        self.allow_incomplete = allow_incomplete
        self.issues: Counter[tuple[str, str]] = Counter()
        self.skipped: Counter[str] = Counter()

    def problem(self, code: str, message: str) -> None:
        if not self.allow_incomplete:
            raise IncompleteExport(f"{code}: {message}; use --map-allow-incomplete to inspect a partial export")
        self.issues[code, message] += 1

    def records(self) -> list[dict]:
        return [{"code": code, "message": message, "count": count}
                for (code, message), count in sorted(self.issues.items())]


def checked_int(value: Any, low: int, high: int, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or not low <= value <= high:
        raise VisualMapError(f"invalid {label}: {value!r}; expected integer {low}..{high}")
    return int(value)


def checked_float(value: Any, label: str) -> float:
    value = float(value)
    if not math.isfinite(value):
        raise VisualMapError(f"non-finite {label}")
    return value


def load_scenario(path: Path):
    if path.suffix.lower() != ".aoe2scenario" or not path.is_file():
        raise VisualMapError("--visual-map must name an existing .aoe2scenario; RMS scripts/replays are not layouts")
    try:
        from AoE2ScenarioParser import settings
        from AoE2ScenarioParser.scenarios.aoe2_de_scenario import AoE2DEScenario
    except ImportError as exc:
        raise VisualMapError("install optional dependencies: python -m pip install -r "
                             "tools/aoe2de_export/requirements-visual-map.txt") from exc
    previous = settings.PRINT_STATUS_UPDATES
    # The parser's emoji progress output otherwise fails in a GBK Windows console.
    settings.PRINT_STATUS_UPDATES = False
    try:
        return AoE2DEScenario.from_file(str(path))
    except Exception as exc:
        raise VisualMapError(f"cannot read scenario {path.name}: {exc}. "
                             "For old scenarios, open and save a copy in the current DE editor.") from exc
    finally:
        settings.PRINT_STATUS_UPDATES = previous


def pack_terrain(manager) -> tuple[bytes, set[int], int, int]:
    """One compact grid; source ordering is x + y * width, not an isometric image."""
    width = checked_int(manager.map_width, 1, MAX_MAP_SIDE, "map width")
    height = checked_int(manager.map_height, 1, MAX_MAP_SIDE, "map height")
    if len(manager.terrain) != width * height:
        raise VisualMapError("terrain tile count does not match dimensions")
    data = bytearray(width * height * TILE.size)
    used: set[int] = set()
    for i, tile in enumerate(manager.terrain):
        terrain_id = checked_int(tile.terrain_id, 0, 65535, "terrain ID")
        layer = checked_int(tile.layer, -1, 32767, "overlay terrain ID")
        elevation = checked_int(tile.elevation, 0, 255, "elevation")
        TILE.pack_into(data, i * TILE.size, terrain_id, layer, elevation)
        used.add(terrain_id)
        if layer >= 0:
            used.add(layer)
    return bytes(data), used, width, height


def source_file(root: Path, relative: str) -> Path:
    """Never trust asset names from DAT/scenario to escape the installation tree."""
    root = root.resolve()
    path = (root / relative.replace("\\", "/")).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        raise VisualMapError(f"missing or unsafe resource: {relative}")
    return path


def asset_name(value: str) -> str:
    if not value or not re.fullmatch(r"[A-Za-z0-9_. -]+", value) or value in (".", ".."):
        raise VisualMapError(f"unsafe asset name: {value!r}")
    return value


class AssetCopier:
    """Copy each source once, preserve relative paths, stream hashes without RGBA decode."""
    def __init__(self, root: Path, output: Path):
        self.root = root.resolve()
        self.output = output
        self.records: dict[str, dict] = {}

    def copy(self, relative: str) -> str:
        source = source_file(self.root, relative)
        key = source.relative_to(self.root).as_posix()
        if key not in self.records:
            destination = self.output / "source" / key
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            with destination.open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            self.records[key] = {"path": "source/" + key, "bytes": source.stat().st_size,
                                 "sha256": digest}
        return "source/" + key

    def copy_directory(self, relative: str) -> None:
        directory = (self.root / relative).resolve()
        if not directory.is_relative_to(self.root) or not directory.is_dir():
            raise VisualMapError(f"missing or unsafe resource directory: {relative}")
        for source in sorted(directory.rglob("*")):
            if source.is_file():
                self.copy(source.relative_to(self.root).as_posix())


def export_materials(dat, used: set[int], copier: AssetCopier, report: Report) -> list[dict]:
    """Only visual DAT fields. In particular, no terrain restriction or unit density."""
    terrains = dat.terrain_block.terrains
    records: dict[int, dict] = {}

    def visit(terrain_id: int, stack: frozenset[int]) -> None:
        if terrain_id in stack:
            raise VisualMapError(f"terrain alias cycle at {terrain_id}")
        if terrain_id in records:
            return
        if not 0 <= terrain_id < len(terrains):
            report.problem("missing_terrain", str(terrain_id))
            records[terrain_id] = {"id": terrain_id, "status": "missing"}
            return
        terrain = terrains[terrain_id]
        alias = int(terrain.terrain_to_draw)
        record = {"id": terrain_id, "name": terrain.name, "status": "exported",
                  "draw_as": alias if alias >= 0 else None,
                  "texture": None, "overlay_mask": None,
                  "repeat_tiles": list(terrain.terrain_dimensions),
                  "blend_priority": int(terrain.blend_priority),
                  "blend_type": int(terrain.blend_type),
                  # is_water is a bit field (grass is 32!), never serialize as bool.
                  "water_flags_raw": int(terrain.is_water)}
        records[terrain_id] = record
        if alias >= 0:
            visit(alias, stack | {terrain_id})
        else:
            try:
                record["texture"] = copier.copy(f"terrain/textures/2x/{asset_name(terrain.name_2)}.dds")
            except VisualMapError as exc:
                report.problem("missing_terrain_texture", str(exc))
                record["status"] = "missing"
        if terrain.overlay_mask_name:
            try:
                record["overlay_mask"] = copier.copy(f"terrain/masks/{asset_name(terrain.overlay_mask_name)}")
            except VisualMapError as exc:
                report.problem("missing_terrain_mask", str(exc))
                record["status"] = "missing"

    for terrain_id in sorted(used):
        visit(terrain_id, frozenset())
    # Preserve source recipes instead of inventing an unverified mapping from blend_type.
    for directory in ("terrain/blends", "terrain/colorcorrection_json"):
        try:
            copier.copy_directory(directory)
        except VisualMapError as exc:
            report.problem("missing_visual_support", str(exc))
    # DE terrain water flags occupy the low five bits. These are visual source flags,
    # not an instruction for the consumer to modify physical water/passability.
    if any(record.get("water_flags_raw", 0) & 31 for record in records.values()):
        for directory in ("terrain/water", "terrain/water_json"):
            try:
                copier.copy_directory(directory)
            except VisualMapError as exc:
                report.problem("missing_water_support", str(exc))
    return [records[key] for key in sorted(records)]


def player_civilizations(scenario, overrides: list[str]) -> dict[int, int | None]:
    from AoE2ScenarioParser.datasets.object_support import CivilizationOld
    result: dict[int, int | None] = {0: 0}
    for player in scenario.player_manager.players:
        player_id = int(player.player_id)
        if player_id == 0:
            continue
        # Use architecture override where present; that is the building's visual civ.
        civ = getattr(player, "architecture_set", player.civilization)
        # Random architecture means "use the player's civilization", not Gaia.
        if getattr(civ, "name", "") in ("RANDOM", "MIRROR_RANDOM", "FULL_RANDOM", "CUSTOM_RANDOM"):
            civ = player.civilization
        name = getattr(civ, "name", str(civ))
        if name in ("RANDOM", "MIRROR_RANDOM", "FULL_RANDOM", "CUSTOM_RANDOM"):
            result[player_id] = None
        elif isinstance(civ, int):
            result[player_id] = int(civ)
        else:
            entry = CivilizationOld.__members__.get(name)
            result[player_id] = int(entry) if entry is not None else None
    for override in overrides:
        try:
            player_id, civ = map(int, override.split(":"))
            checked_int(player_id, 0, 8, "player")
            checked_int(civ, 0, 65535, "civ")
        except (ValueError, TypeError) as exc:
            raise VisualMapError("--map-player-civ expects PLAYER:CIV (player 0..8, non-negative DAT civ)") from exc
        result[player_id] = civ
    return result


class GraphicExporter:
    """Deduplicate DAT graphics; keep composite deltas in the visual description."""
    def __init__(self, args, api, dat, output: Path, report: Report, copier: AssetCopier | None = None):
        self.args, self.api, self.dat = args, api, dat
        self.output, self.report = output, report
        self.records: dict[int, dict] = {}
        self.appearances: dict[str, dict] = {}
        self.copier = copier or AssetCopier(args.aoe2 / "resources/_common", output)
        self.decoder = None

    def export_particle(self, name: str) -> dict:
        """Preserve a texture-atlas particle recipe, including Loop and startup delays.

        Do not force it through the existing one-shot effects API or silently omit
        an animated visual such as a gold mine's shimmer.
        """
        name = asset_name(name)
        root = (self.args.aoe2 / "resources/_common/particles").resolve()
        source = source_file(root, f"{name}.json")
        document = json.loads(source.read_text(encoding="utf-8-sig"))
        supported = {"AtlasFile", "ImageFirst", "ImageCount", "ImageAngles", "Scale", "Alpha",
                     "Type", "Timer", "Duration", "DisplayLevel", "StartDelay1", "StartDelay2",
                     "StopMode", "IsPersistent"}
        if not isinstance(document, dict) or not isinstance(document.get("AtlasFile"), str):
            raise VisualMapError(f"particle {name} needs a texture-atlas recipe")
        if set(document) - supported:
            raise VisualMapError(f"particle {name} has unsupported recipe fields: {sorted(set(document) - supported)}")
        atlas = source_file(root, document["AtlasFile"])
        # The companion TexturePacker JSON is needed to reconstruct cropped frames.
        metadata = source_file(root, atlas.with_suffix(".json").relative_to(root).as_posix())
        return {"format": "aoe2de_atlas_recipe", "config": self.copier.copy(f"particles/{name}.json"),
                "atlas": self.copier.copy("particles/" + atlas.relative_to(root).as_posix()),
                "frames": self.copier.copy("particles/" + metadata.relative_to(root).as_posix())}

    def appearance(self, civ: int, unit_id: int, stack: frozenset[str] = frozenset()) -> str:
        """Building annexes are separate units in DAT, but only visual attachments here."""
        key = f"a{civ}_{unit_id}"
        if key in stack:
            raise VisualMapError(f"appearance attachment cycle at {key}")
        if key in self.appearances:
            return key
        if not 0 <= civ < len(self.dat.civs) or not 0 <= unit_id < len(self.dat.civs[civ].units):
            raise VisualMapError(f"missing appearance {key}")
        unit = self.dat.civs[civ].units[unit_id]
        if unit is None:
            raise VisualMapError(f"missing appearance {key}")
        record = {"id": key, "graphic": None, "attachments": []}
        self.appearances[key] = record
        try:
            if unit.standing_graphic[0] >= 0:
                record["graphic"] = self.export(int(unit.standing_graphic[0]))["id"]
            building = getattr(unit, "building", None)
            if building is not None:
                for annex in building.annexes:
                    if annex.unit_id < 0:
                        continue
                    try:
                        child = self.appearance(civ, int(annex.unit_id), stack | {key})
                    except VisualMapError as exc:
                        self.report.problem("appearance_export_failed", str(exc))
                        record["status"] = "partial"
                        continue
                    record["attachments"].append({"appearance": child,
                                                 "offset_tiles": [checked_float(annex.misplacement_x, "annex x"),
                                                                  checked_float(annex.misplacement_y, "annex y")]})
        except IncompleteExport:
            raise
        except VisualMapError as exc:
            record["status"] = "missing"
            self.report.problem("appearance_export_failed", str(exc))
        return key

    def export(self, graphic_id: int, stack: frozenset[int] = frozenset()) -> dict:
        if graphic_id in stack:
            raise VisualMapError(f"graphic delta cycle at {graphic_id}")
        if graphic_id in self.records:
            return self.records[graphic_id]
        if not 0 <= graphic_id < len(self.dat.graphics) or self.dat.graphics[graphic_id] is None:
            raise VisualMapError(f"missing DAT graphic {graphic_id}")
        graphic = self.dat.graphics[graphic_id]
        entry = {"id": graphic_id, "status": "exported", "config": None,
                 "sequence_type": int(graphic.sequence_type),
                 "mirroring_mode": int(graphic.mirroring_mode),
                 "layer_raw": int(getattr(graphic, "layer", 0)),
                 "replay_delay": checked_float(getattr(graphic, "replay_delay", 0.0), "replay delay"),
                 "particle": None, "deltas": []}
        self.records[graphic_id] = entry
        missing_composite_source = None
        try:
            particle = getattr(graphic, "particle_effect_name", "")
            if particle and particle != "None":
                entry["particle"] = self.export_particle(particle)
            if graphic.file_name and graphic.file_name != "None":
                base = re.sub(r"_x[12]$", "", asset_name(graphic.file_name))
                scales = ("x2", "x1") if self.args.scale == "auto" else (self.args.scale,)
                candidates = [self.api.graphics_dir(self.args.aoe2) / f"{base}_{scale}.sld"
                              for scale in scales]
                source = next((path for path in candidates if path.is_file()), None)
                if source is None:
                    if any(delta.graphic_id >= 0 for delta in graphic.deltas):
                        # Missing parent pixels must not discard independently drawable children.
                        missing_composite_source = f"missing SLD for graphic {graphic_id}: {graphic.file_name}"
                    else:
                        raise VisualMapError(f"missing SLD for graphic {graphic_id}: {graphic.file_name}")
            else:
                source = None
            if source is not None:
                source = source_file(self.api.graphics_dir(self.args.aoe2), source.name)
                directions = max(1, int(graphic.angle_count))
                frames = int(graphic.frame_count)
                actual = len(self.api.read_sld_frame_records(source.read_bytes()))
                if frames == 1 and actual > 0 and actual == directions - 1:
                    # Some DE cliffs declare one closing angle with no SLD
                    # frame. Preserve the discrepancy and wrap that angle to
                    # the first exported frame in the runtime pose selector.
                    entry["source_direction_count"] = directions
                    entry["missing_closing_direction"] = directions - 1
                    directions = actual
                if frames <= 0 or actual // directions != frames:
                    raise VisualMapError(f"graphic {graphic_id}: DAT {directions}x{frames}, SLD {actual} frames")
                if actual != directions * frames:
                    entry["unused_trailing_records"] = actual - directions * frames
                duration = checked_float(graphic.frame_duration, "frame duration")
                fps = 1.0 / duration if duration > 0 else self.args.fps
                if self.decoder is None:
                    self.decoder = self.api.load_openage(self.args.openage)
                animation = self.api.export_animation(
                    *self.decoder, source, self.output, f"g{graphic_id}", directions, fps,
                    sampling_mode="timeline",
                    dat_graphic=self.api.UnitAnimationGraphicMetadata(
                        graphic_id, directions, frames, int(graphic.sequence_type), duration, fps))
                entry.update(config=animation["config"], directions=directions,
                             frames_per_direction=frames, source=source.name,
                             pose_mode="variation" if graphic.sequence_type == 6 else
                                       "topology" if graphic.sequence_type == 2 else "direction",
                             animated=frames > 1 and duration > 0)
                if any(status in ("partial", "invalid") for status in animation["layers"].values()):
                    self.report.problem("incomplete_sprite_layer", f"graphic {graphic_id}")
                    entry["status"] = "partial"
            elif entry["particle"] is None and not any(delta.graphic_id >= 0 for delta in graphic.deltas):
                if (getattr(graphic, "slp", None) == -1 and
                        ((graphic.frame_count == 0 and graphic.angle_count == 0)
                         or getattr(graphic, "name", "").upper() == "BLANK")):
                    # DE contains deliberately disabled visual nodes, e.g. LeavesFalling.
                    # This is not a missing file; keep an explicit empty node in the graph.
                    entry["status"] = "empty"
                else:
                    raise VisualMapError(f"graphic {graphic_id} has neither image nor drawable deltas")
            for delta in graphic.deltas:
                if delta.graphic_id < 0:
                    continue
                child = self.export(int(delta.graphic_id), stack | {graphic_id})
                entry["deltas"].append({"graphic": child["id"],
                                         "offset_pixels": [int(delta.offset_x), int(delta.offset_y)],
                                         "display_angle": int(delta.display_angle)})
        except IncompleteExport:
            raise
        except (VisualMapError, self.api.ExportError) as exc:
            entry["status"] = "missing"
            self.report.problem("graphic_export_failed", f"graphic {graphic_id}: {exc}")
        if missing_composite_source:
            entry["status"] = "partial"
            self.report.problem("graphic_export_failed", missing_composite_source)
        return entry


def export_objects(scenario, dat, graphics: GraphicExporter, args, report: Report) -> list[dict]:
    objects = scenario.unit_manager.get_all_units()
    if len(objects) > MAX_OBJECTS:
        raise VisualMapError(f"too many objects: {len(objects)}")
    if args.map_objects == "none":
        report.skipped["object_export_disabled"] = len(objects)
        return []
    civs = player_civilizations(scenario, args.map_player_civ)
    colors = {int(p.player_id): int(p.color) for p in scenario.player_manager.players}
    exported = []
    for obj in objects:
        player, unit_id = int(obj.player), int(obj.unit_const)
        # Classify using Gaia before requesting a random player's visual civ.
        if not 0 <= unit_id < len(dat.civs[0].units) or dat.civs[0].units[unit_id] is None:
            report.problem("missing_unit_appearance", str(unit_id))
            continue
        base_unit = dat.civs[0].units[unit_id]
        if args.map_objects == "scenery" and base_unit.type not in SCENERY_TYPES:
            report.skipped["not_scenery"] += 1
            continue
        if obj.garrisoned_in_id >= 0:
            report.skipped["garrisoned"] += 1
            continue
        if base_unit.standing_graphic[0] < 0:
            # Invisible blockers/placeholders have no visual and must not become gameplay.
            report.skipped["no_visible_graphic"] += 1
            continue
        civ = civs.get(player)
        if civ is None or not 0 <= civ < len(dat.civs):
            report.problem("unresolved_visual_civ", f"player {player}; set --map-player-civ {player}:CIV")
            continue
        unit = dat.civs[civ].units[unit_id] if unit_id < len(dat.civs[civ].units) else None
        if unit is None or unit.standing_graphic[0] < 0:
            report.problem("missing_unit_appearance", f"civ {civ}, unit {unit_id}")
            continue
        appearance = graphics.appearance(civ, unit_id)
        exported.append({"source_reference_id": int(obj.reference_id),
                         "appearance": appearance,
                         "position": [checked_float(obj.x, "object x"), checked_float(obj.y, "object y"),
                                      checked_float(obj.z, "object z")],
                         # Scenery rotation can be a variation index, NOT radians (sequence 6).
                         "rotation_raw": checked_float(obj.rotation, "rotation"),
                         "initial_frame_raw": int(obj.initial_animation_frame),
                         "source_color_index": colors[player],
                         "neutral_color": player == 0})
    return exported


def export_instance_metadata(scenario, dat) -> list[dict]:
    """Losslessly retain placed-object identity/state, independent of visuals.

    This is the source-side placement contract for later synced Feature/Unit
    creation. It intentionally includes buildings and unsupported objects even
    when no renderable graphic, civ, or gameplay mapping exists.
    """
    objects = scenario.unit_manager.get_all_units()
    if len(objects) > MAX_OBJECTS:
        raise VisualMapError(f"too many objects: {len(objects)}")
    records = []
    for obj in objects:
        unit_id = checked_int(int(obj.unit_const), 0, 65535, "source unit ID")
        player = checked_int(int(obj.player), 0, 255, "source player")
        unit = (dat.civs[0].units[unit_id]
                if unit_id < len(dat.civs[0].units) else None)
        category = ("building" if unit is not None and int(unit.type) == 80 else
                    "other")
        clearance = getattr(unit, "clearance_size", (0, 0)) if unit is not None else (0, 0)
        try:
            clearance = [checked_float(v, "source clearance") for v in clearance]
        except (TypeError, ValueError):
            clearance = []
        records.append({
            "source_reference_id": int(obj.reference_id),
            "source_unit_id": unit_id,
            "source_player_id": player,
            "source_status": int(getattr(obj, "status", 0)),
            "position_source_tiles": [checked_float(obj.x, "object x"),
                                      checked_float(obj.y, "object y"),
                                      checked_float(obj.z, "object z")],
            "rotation_raw": checked_float(obj.rotation, "rotation"),
            "initial_frame_raw": int(obj.initial_animation_frame),
            "garrisoned_in_reference_id": int(getattr(obj, "garrisoned_in_id", -1)),
            "category": category,
            "dat": None if unit is None else {
                "name": str(getattr(unit, "name", "")),
                "type": int(unit.type),
                "class_id": int(getattr(unit, "class_", getattr(unit, "class_id", -1))),
                "obstruction_type": int(getattr(unit, "obstruction_type", 0)),
                "clearance_size": clearance,
                "standing_graphic_id": int(unit.standing_graphic[0]),
            },
        })
    records.sort(key=lambda record: (record["source_reference_id"], record["source_unit_id"]))
    return records


def make_preview(output: Path, packed: bytes, width: int, height: int,
                 materials: list[dict], max_size: int) -> None:
    """Bounded diagnostic colors from actual textures, with magenta for missing data."""
    from PIL import Image
    by_id = {m["id"]: m for m in materials}
    colors: dict[int, tuple[int, int, int]] = {}

    def color(terrain_id: int) -> tuple[int, int, int]:
        if terrain_id not in colors:
            material = by_id[terrain_id]
            if material.get("draw_as") is not None:
                colors[terrain_id] = color(material["draw_as"])
            elif material.get("texture"):
                with Image.open(output / material["texture"]) as image:
                    colors[terrain_id] = image.convert("RGB").resize((1, 1)).getpixel((0, 0))
            else:
                colors[terrain_id] = (255, 0, 255)
        return colors[terrain_id]

    pixels = bytearray(width * height * 3)
    for index, (terrain_id, _layer, _elevation) in enumerate(TILE.iter_unpack(packed)):
        pixels[index * 3:index * 3 + 3] = bytes(color(terrain_id))
    image = Image.frombytes("RGB", (width, height), bytes(pixels))
    factor = min(max_size / width, max_size / height)
    image.resize((max(1, int(width * factor)), max(1, int(height * factor))),
                 Image.Resampling.NEAREST).save(output / "terrain-preview.png")


def write_bundle(args, api, scenario, dat, output: Path) -> dict:
    report = Report(args.map_allow_incomplete)
    packed, used, width, height = pack_terrain(scenario.map_manager)
    copier = AssetCopier(args.aoe2 / "resources/_common", output)
    materials = export_materials(dat, used, copier, report)
    instance_metadata = export_instance_metadata(scenario, dat)
    graphics = GraphicExporter(args, api, dat, output, report, copier)
    objects = export_objects(scenario, dat, graphics, args, report)
    (output / "terrain.bin").write_bytes(packed)
    api.write_json(output / "materials.json", materials)
    api.write_json(output / "objects.json", objects)
    api.write_json(output / "instance-metadata.json", instance_metadata)
    api.write_json(output / "graphics.json", [graphics.records[key] for key in sorted(graphics.records)])
    api.write_json(output / "appearances.json", [graphics.appearances[key] for key in sorted(graphics.appearances)])
    api.write_json(output / "assets.json", [copier.records[key] for key in sorted(copier.records)])
    make_preview(output, packed, width, height, materials, args.map_preview_size)
    mood = scenario.map_manager.map_color_mood
    manifest = {
        "schema_version": SCHEMA_VERSION, "kind": "aoe2de_visual_map", "id": args.name,
        "source": {"scenario": args.visual_map.name, "scenario_version": str(scenario.scenario_version)},
        "dimensions": {"width": width, "height": height},
        "coordinates": {"space": "aoe2_scenario", "horizontal_axes": ["x", "y"],
                        "vertical_axis": "z", "horizontal_unit": "source_tile",
                        "elevation": "uint8_source_level", "world_transform": "consumer_defined"},
        "terrain": {"file": "terrain.bin", "stride": TILE.size, "byte_order": "little",
                    "order": "x + y * width", "record": "uint16 terrain; int16 layer; uint8 elevation; uint8 reserved[3]",
                    "materials": "materials.json", "color_mood": getattr(mood, "name", str(mood))},
        "objects": "objects.json", "instance_metadata": "instance-metadata.json",
        "appearances": "appearances.json",
        "graphics": "graphics.json", "assets": "assets.json",
        "preview": {"file": "terrain-preview.png", "mode": "terrain_id_average_color",
                    "includes_objects": False, "includes_elevation": False, "includes_blending": False},
        "scope": {"visual_only": True, "object_selection": args.map_objects,
                  "excluded": ["triggers", "AI", "XS", "player_resources", "victory", "spawn_rules",
                               "unit_defs", "weapon_defs", "collision", "pathfinding", "terrain_unit_generation"]},
        "limitations": ["Source material recipes are exported; Recoil baking is a separate adapter.",
                        "DE terrain transitions and water shaders require a consumer adapter.",
                        "Only initial standing appearances are exported; scenario triggers are never evaluated.",
                        "Atlas particles retain source recipes; terrain-generated trees, age/state/snow overrides are not evaluated.",
                        "Object rotation/variant indices and delta offsets retain source semantics."],
        "summary": {"complete": not report.issues, "tile_count": width * height,
                    "object_count": len(instance_metadata),
                    "visual_object_count": len(objects), "graphic_count": len(graphics.records),
                    "material_count": len(materials), "copied_asset_count": len(copier.records),
                    "skipped_objects": dict(sorted(report.skipped.items())), "issues": report.records()},
    }
    api.write_json(output / "manifest.json", manifest)
    return manifest


def sha256_file(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def terrain_library_path(root: Path, name: str) -> Path:
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_.-]*", name):
        raise VisualMapError(f"invalid terrain library id: {name!r}")
    return root / "terrain-libraries" / name


def export_terrain_library(args, api) -> int:
    """Publish a DAT-indexed, map-independent terrain asset archive directory.

    The DDS and mask bytes are copied without decoding or repacking. Missing
    entries in the DAT are catalogued; a map referencing one is rejected later.
    """
    try:
        api.validate_resource_id(args.name)
        root = args.out.resolve()
        target = terrain_library_path(root, args.name)
        if target.exists():
            raise VisualMapError(f"terrain library already exists: {target}")
        common = args.aoe2.resolve() / "resources/_common"
        dat_path = args.dat or api.dat_path_for(args.aoe2)
        dat = api.load_dat(dat_path)
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".terrain-library-", dir=target.parent) as temporary:
            output = Path(temporary) / "library"
            output.mkdir()
            copier = AssetCopier(common, output)
            copier.copy_directory("terrain")
            report = Report(True)
            materials = export_materials(dat, set(range(len(dat.terrain_block.terrains))), copier, report)
            api.write_json(output / "terrain-ids.json", materials)
            api.write_json(output / "assets.json", [copier.records[key] for key in sorted(copier.records)])
            manifest = {
                "schema_version": TERRAIN_LIBRARY_SCHEMA,
                "kind": "aoe2de_terrain_library",
                "id": args.name,
                "dat_sha256": sha256_file(dat_path),
                "terrain_count": len(materials),
                "terrain_ids_sha256": sha256_file(output / "terrain-ids.json"),
                "assets_sha256": sha256_file(output / "assets.json"),
                "asset_count": len(copier.records),
                "issues": report.records(),
            }
            api.write_json(output / "manifest.json", manifest)
            if target.exists():
                raise VisualMapError(f"terrain library appeared during export: {target}")
            output.rename(target)
        print(f"exported terrain library -> {target}; {manifest['terrain_count']} IDs, "
              f"{manifest['asset_count']} shared assets")
        return 0
    except (VisualMapError, OSError, ValueError) as exc:
        raise SystemExit(f"terrain library export failed: {exc}") from exc


def load_terrain_library(path: Path, dat_path: Path, used: set[int]) -> dict:
    manifest_path = path / "manifest.json"
    if not manifest_path.is_file():
        raise VisualMapError(f"terrain library manifest missing: {manifest_path}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != TERRAIN_LIBRARY_SCHEMA or manifest.get("kind") != "aoe2de_terrain_library":
        raise VisualMapError("unsupported terrain library schema")
    if manifest.get("dat_sha256") != sha256_file(dat_path):
        raise VisualMapError("terrain library was built from a different DAT")
    catalog_path = path / "terrain-ids.json"
    if sha256_file(catalog_path) != manifest.get("terrain_ids_sha256"):
        raise VisualMapError("terrain library ID catalog hash mismatch")
    assets_path = path / "assets.json"
    if sha256_file(assets_path) != manifest.get("assets_sha256"):
        raise VisualMapError("terrain library asset index hash mismatch")
    assets = {record["path"]: record for record in json.loads(assets_path.read_text(encoding="utf-8"))}
    catalog = {record["id"]: record for record in json.loads(catalog_path.read_text(encoding="utf-8"))}
    checked_assets = set()
    for terrain_id in used:
        seen = set()
        current = terrain_id
        while True:
            if current in seen:
                raise VisualMapError(f"terrain alias cycle at {current}")
            seen.add(current)
            record = catalog.get(current)
            if record is None or record.get("status") != "exported":
                raise VisualMapError(f"terrain ID {current} has no complete shared material")
            for key in ("texture", "overlay_mask"):
                asset = record.get(key)
                if asset:
                    resolved = (path / asset).resolve()
                    if not resolved.is_relative_to(path.resolve()) or not resolved.is_file():
                        raise VisualMapError(f"terrain ID {current} missing shared asset: {asset}")
                    indexed = assets.get(asset)
                    if indexed is None:
                        raise VisualMapError(f"terrain ID {current} asset is not indexed: {asset}")
                    if asset not in checked_assets:
                        if sha256_file(resolved) != indexed["sha256"]:
                            raise VisualMapError(f"terrain ID {current} shared asset hash mismatch: {asset}")
                        checked_assets.add(asset)
            alias = record.get("draw_as")
            if alias is None:
                break
            current = alias
    return manifest


def write_compact_bundle(args, api, scenario, dat_path: Path, output: Path) -> dict:
    """Keep per-map source data only; rendering assets live in the shared library."""
    if args.map_objects != "none":
        raise VisualMapError("compact maps require --map-objects none; all object metadata is retained")
    packed, used, width, height = pack_terrain(scenario.map_manager)
    library = args.map_terrain_library.resolve()
    shared = load_terrain_library(library, dat_path, used)
    object_library = None
    if getattr(args, "map_object_library", None) is not None:
        object_library = load_static_object_library(args.map_object_library, dat_path)
    instances = export_instance_metadata(scenario, api.load_dat(dat_path))
    (output / "terrain.bin").write_bytes(packed)
    api.write_json(output / "instance-metadata.json", instances)
    manifest = {
        "schema_version": SOURCE_MAP_SCHEMA,
        "kind": "aoe2de_source_map",
        "id": args.name,
        "source": {"scenario": args.visual_map.name,
                   "scenario_version": str(scenario.scenario_version),
                   "sha256": sha256_file(args.visual_map)},
        "dimensions": {"width": width, "height": height},
        "coordinates": {"space": "aoe2_scenario", "horizontal_axes": ["x", "y"],
                        "vertical_axis": "z", "horizontal_unit": "source_tile",
                        "tile_order": "x + y * width"},
        "terrain": {"file": "terrain.bin", "stride": TILE.size,
                    "record": "uint16 terrain; int16 layer; uint8 elevation; uint8 reserved[3]",
                    "sha256": sha256_file(output / "terrain.bin")},
        "instance_metadata": "instance-metadata.json",
        "instance_metadata_sha256": sha256_file(output / "instance-metadata.json"),
        "terrain_library": {"id": shared["id"],
                            "terrain_ids_sha256": shared["terrain_ids_sha256"],
                            "dat_sha256": shared["dat_sha256"]},
        "summary": {"tile_count": width * height, "object_count": len(instances),
                    "used_terrain_ids": sorted(used)},
    }
    if object_library is not None:
        manifest["object_library"] = {"id": object_library["id"],
                                       "index_sha256": object_library["index_sha256"]}
    api.write_json(output / "manifest.json", manifest)
    return manifest


def load_static_object_library(path: Path, dat_path: Path) -> dict:
    root = path.resolve()
    manifest_path = root / "manifest.json"
    index_path = root / "index.json"
    if not manifest_path.is_file() or not index_path.is_file():
        raise VisualMapError(f"object library manifest or index missing: {root}")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if (manifest.get("schema_version") != 1 or
            manifest.get("kind") != "aoe2de_static_object_library" or
            manifest.get("dat_sha256") != sha256_file(dat_path) or
            manifest.get("index_sha256") != sha256_file(index_path)):
        raise VisualMapError("object library is invalid or uses a different DAT")
    index = json.loads(index_path.read_text(encoding="utf-8"))
    if (index.get("id") != manifest.get("id") or
            index.get("kind") != "aoe2de_static_object_index" or
            index.get("dat_sha256") != manifest.get("dat_sha256") or
            not isinstance(index.get("objects"), list)):
        raise VisualMapError("object library index does not match manifest")
    prefix = index.get("resource_prefix")
    if (not isinstance(prefix, str) or not prefix or len(prefix) > 80 or
            any(char not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"
                for char in prefix)):
        raise VisualMapError("object library has an invalid resource prefix")
    for graphic_id in {record["graphic_id"] for record in index["objects"]}:
        if not isinstance(graphic_id, int) or graphic_id < 0:
            raise VisualMapError("object library has an invalid graphic ID")
        resource = root / "graphics" / f"{prefix}{graphic_id}" / "manifest.json"
        if not resource.is_file():
            raise VisualMapError(f"object library graphic missing: {resource}")
    return manifest


def export_static_object_library(args, api) -> int:
    """Publish DAT-indexed scenery graphics once, for reuse by compact maps."""
    try:
        api.validate_resource_id(args.name)
        target = (args.out.resolve() / "object-libraries" / args.name).resolve()
        if target.exists():
            raise VisualMapError(f"object library already exists: {target}")
        dat_path = args.dat or api.dat_path_for(args.aoe2)
        dat = api.load_dat(dat_path)
        dat_hash = sha256_file(dat_path)
        scenario = load_scenario(args.visual_map)
        instances = export_instance_metadata(scenario, dat)
        selected = {}
        for record in instances:
            unit_id = record["source_unit_id"]
            info = record["dat"]
            if (record["source_player_id"] != 0 or
                    record["garrisoned_in_reference_id"] >= 0 or
                    info is None or info["type"] != 10 or info["standing_graphic_id"] < 0):
                continue
            selected[unit_id] = info["standing_graphic_id"]
        target.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".object-library-", dir=target.parent) as tmp:
            output = Path(tmp) / "library"
            output.mkdir()
            report = Report(False)
            copier = AssetCopier(args.aoe2 / "resources/_common", output)
            graphics = GraphicExporter(args, api, dat, output, report, copier)
            for graphic_id in sorted(set(selected.values())):
                graphics.export(graphic_id)
            prefix = "map_" + dat_hash[:12] + "_g"
            published = []
            for graphic_id, record in sorted(graphics.records.items()):
                if record["status"] != "exported":
                    raise VisualMapError(f"static graphic {graphic_id} is incomplete: {record['status']}")
                if not record.get("config"):
                    if record.get("particle") is not None:
                        # A particle-only DAT child is retained in the index;
                        # it has no sprite frames to turn into an appearance.
                        continue
                    raise VisualMapError(f"static graphic {graphic_id} has no renderable atlas")
                resource_id = prefix + str(graphic_id)
                resource_dir = output / "library-graphics" / resource_id
                atlas_dir = resource_dir / "graphics"
                atlas_dir.mkdir(parents=True)
                for source in (output / "graphics").glob(f"g{graphic_id}.*"):
                    shutil.move(str(source), str(atlas_dir / source.name))
                for source in (output / "graphics").glob(f"g{graphic_id}_*.dds"):
                    shutil.move(str(source), str(atlas_dir / source.name))
                api.write_json(resource_dir / "manifest.json", {
                    "schema_version": 2, "kind": "aoe2de_graphics", "id": resource_id,
                    "discovered_animations": [f"g{graphic_id}"],
                    "animations": {f"g{graphic_id}": {"status": "exported",
                                                     "config": f"graphics/g{graphic_id}.json"}},
                    "map_graphic": {"graphic_id": graphic_id,
                                    "sequence_type": record["sequence_type"],
                                    "deltas": record["deltas"]},
                })
                published.append(resource_id)
            index = {"schema_version": 1, "kind": "aoe2de_static_object_index",
                     "id": args.name, "dat_sha256": dat_hash,
                     "resource_prefix": prefix,
                     "objects": [{"source_unit_id": unit, "graphic_id": graphic}
                                 for unit, graphic in sorted(selected.items())],
                     "graphics": [graphics.records[key] for key in sorted(graphics.records)]}
            api.write_json(output / "index.json", index)
            api.write_json(output / "manifest.json", {
                "schema_version": 1, "kind": "aoe2de_static_object_library",
                "id": args.name, "dat_sha256": dat_hash,
                "index_sha256": sha256_file(output / "index.json"),
                "graphic_count": len(published), "object_type_count": len(selected),
            })
            # Keep the shared library self-contained and publish it atomically.
            # GraphicExporter uses output/graphics for its intermediate files.
            for resource_id in published:
                source = output / "library-graphics" / resource_id
                destination = output / "graphics" / resource_id
                source.rename(destination)
            leftovers = [p for p in (output / "graphics").iterdir() if p.is_file()]
            if leftovers:
                raise VisualMapError(f"unpackaged static graphic output: {leftovers[0]}")
            output.rename(target)
        print(f"exported static object library -> {target}; {len(selected)} types, {len(published)} graphics")
        return 0
    except (VisualMapError, OSError, ValueError) as exc:
        raise SystemExit(f"static object library export failed: {exc}") from exc


def export_visual_map(args, api) -> int:
    """Build in a private sibling directory; publish only a successful new bundle.

    Existing outputs are never removed/overwritten, including failed exports. Use a
    new resource name for revisions. TemporaryDirectory owns only our unique files.
    """
    try:
        api.validate_resource_id(args.name)
        if not 1 <= args.map_preview_size <= 4096:
            raise VisualMapError("--map-preview-size must be 1..4096")
        root = args.out.resolve()
        parent = (root / "maps").resolve()
        target = (parent / args.name).resolve()
        if not parent.is_relative_to(root) or target.parent != parent:
            raise VisualMapError("map output escapes cache root")
        if target.exists():
            raise VisualMapError(f"output already exists (not overwritten): {target}; choose a new --name")
        scenario = load_scenario(args.visual_map)
        print(f"loaded scenario {args.visual_map.name}; reading DAT terrain/object metadata...")
        dat_path = args.dat or api.dat_path_for(args.aoe2)
        dat = None if args.map_terrain_library else api.load_dat(dat_path)
        parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix=".visual-map-", dir=parent) as temporary:
            output = Path(temporary) / "bundle"
            output.mkdir()
            manifest = (write_compact_bundle(args, api, scenario, dat_path, output)
                        if args.map_terrain_library else write_bundle(args, api, scenario, dat, output))
            if target.exists():
                raise VisualMapError(f"output appeared during export: {target}")
            output.rename(target)
        summary = manifest["summary"]
        print(f"exported visual map -> {target}; {summary['tile_count']} tiles, "
              f"{summary['object_count']} metadata objects")
        for issue in summary.get("issues", []):
            print(f"warning: {issue['code']}: {issue['message']} (x{issue['count']})")
        return 0
    except (VisualMapError, OSError, ValueError) as exc:
        raise SystemExit(f"visual map export failed: {exc}") from exc
