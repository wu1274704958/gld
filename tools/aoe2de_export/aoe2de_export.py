#!/usr/bin/env python3
"""Export local AoE2DE SLD graphics into a versioned gld cache."""

from __future__ import annotations

import argparse
import fnmatch
import json
import math
import re
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from struct import Struct
from typing import Any


GRAPHICS_SCHEMA_VERSION = 2
UNIT_SCHEMA_VERSION = 3
GRAPHIC_NAME_RE = re.compile(
    r"^(?P<prefix>.+)_(?P<action>[A-Za-z0-9]+)_(?P<scale>x[12])\.sld$"
)
RESOURCE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
DEFAULT_DIRECTIONS = 16
DEFAULT_FPS = 30.0
LIST_PAGE_SIZE = 50
DEFAULT_DAT_RELATIVE = Path("resources/_common/dat/empires2_x2_p1.dat")
DEFAULT_UNIT_MAP = Path(__file__).resolve().with_name("unit_dat_map.json")
PLAYERCOLOR_FORMAT = "rgba8_bc4_decoded"
PLAYERCOLOR_DIFFUSE_ALPHA_MIN = 8
PLAYERCOLOR_TRANSITION_RAW_MIN = 112
PLAYERCOLOR_STRONG_RAW_MIN = 128
# Retained only for the isolated diagnostic helper below; production export no
# longer invokes the legacy consensus filter.
PLAYERCOLOR_TEMPORAL_FILTER_VERSION = 1
PLAYERCOLOR_BASE_RGB_TOLERANCE = 32

SLD_HEADER = Struct("<4s4HI")
SLD_FRAME_HEADER = Struct("<4H2BH")
SLD_LAYER_LENGTH = Struct("<I")

LAYER_BITS = {
    "main": 0x01,
    "shadow": 0x02,
    "outline": 0x04,
    "damage": 0x08,
    "player_color": 0x10,
}
DECODED_LAYERS = {
    "main": 0,
    "shadow": 1,
    "player_color": 4,
}


class ExportError(ValueError):
    """An individual animation cannot be exported safely."""


@dataclass(frozen=True)
class UnitMatch:
    civ_id: int
    unit_id: int
    unit: Any
    graphics: tuple[tuple[int, str], ...]
    mapping_source: str


@dataclass
class ExportFrame:
    image: Any
    width: int
    height: int
    foot: tuple[int, int]
    source_ordinal: int
    source_frame_index: int
    present: bool = True


@dataclass(frozen=True)
class AtlasLayout:
    cols: int
    rows: int
    cell_w: int
    cell_h: int

    @property
    def width(self) -> int:
        return self.cols * self.cell_w

    @property
    def height(self) -> int:
        return self.rows * self.cell_h


def graphics_dir(aoe2: Path) -> Path:
    return aoe2 / "resources" / "_common" / "drs" / "graphics"


def dat_path_for(aoe2: Path) -> Path:
    return aoe2 / DEFAULT_DAT_RELATIVE


def load_dat(path: Path):
    try:
        from genieutils.datfile import DatFile
    except ImportError as exc:
        raise SystemExit(
            "AoE2 DAT export requires genieutils-py. Install it with:\n"
            "  python -m pip install genieutils-py"
        ) from exc
    if not path.is_file():
        raise SystemExit(f"DAT file does not exist: {path}")
    try:
        return DatFile.parse(path)
    except Exception as exc:  # noqa: BLE001
        raise SystemExit(f"failed to parse DAT file {path}: {exc}") from exc


def load_unit_map(path: Path) -> dict[str, dict[str, int]]:
    if not path.is_file():
        return {}
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001
        raise SystemExit(f"failed to read unit map {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise SystemExit(f"unit map root must be a JSON object: {path}")
    result: dict[str, dict[str, int]] = {}
    for prefix, entry in value.items():
        if (not isinstance(prefix, str) or not isinstance(entry, dict) or
                set(entry) != {"civ_id", "unit_id"} or
                not all(isinstance(entry[key], int) and entry[key] >= 0
                        for key in ("civ_id", "unit_id"))):
            raise SystemExit(
                f"invalid unit map entry for {prefix!r}; expected "
                '{"civ_id": non-negative int, "unit_id": non-negative int}'
            )
        result[prefix] = entry
    return result


def graphic_prefix(file_name: str) -> str | None:
    """Return the SLD unit prefix from DAT filenames with or without .sld."""
    name = Path(str(file_name).replace("\\", "/")).name
    if name.lower().endswith(".sld"):
        name = name[:-4]
    match = re.match(r"^(?P<prefix>.+)_[^_]+_x[12]$", name, re.IGNORECASE)
    return match.group("prefix") if match else None


def _graphic_id_values(unit: Any) -> list[int]:
    values: list[int] = []
    for name in ("standing_graphic", "dying_graphic", "undead_graphic"):
        value = getattr(unit, name, None)
        if isinstance(value, (tuple, list)):
            values.extend(item for item in value if isinstance(item, int))
        elif isinstance(value, int):
            values.append(value)
    moving = getattr(unit, "dead_fish", None)
    if moving is not None:
        for name in ("walking_graphic", "running_graphic"):
            value = getattr(moving, name, None)
            if isinstance(value, int):
                values.append(value)
    combat = getattr(unit, "type_50", None)
    if combat is not None:
        for name in ("attack_graphic", "attack_graphic_2"):
            value = getattr(combat, name, None)
            if isinstance(value, int):
                values.append(value)
    for damage in getattr(unit, "damage_graphics", ()) or ():
        for name in ("graphic_id", "graphic"):
            value = getattr(damage, name, None)
            if isinstance(value, int):
                values.append(value)
                break
    return list(dict.fromkeys(value for value in values if value >= 0))


def referenced_graphics(dat: Any, unit: Any) -> tuple[tuple[int, str], ...]:
    result: list[tuple[int, str]] = []
    graphics = getattr(dat, "graphics", ())
    for graphic_id in _graphic_id_values(unit):
        if graphic_id >= len(graphics):
            continue
        file_name = str(getattr(graphics[graphic_id], "file_name", ""))
        if file_name:
            result.append((graphic_id, file_name))
    return tuple(result)


def get_dat_unit(dat: Any, civ_id: int, unit_id: int) -> Any:
    civs = getattr(dat, "civs", ())
    if civ_id < 0 or civ_id >= len(civs):
        raise SystemExit(f"DAT civ id {civ_id} is out of range 0..{len(civs) - 1}")
    units = getattr(civs[civ_id], "units", ())
    if unit_id < 0 or unit_id >= len(units):
        raise SystemExit(
            f"DAT unit id {unit_id} is out of range for civ {civ_id} "
            f"(0..{len(units) - 1})"
        )
    unit = units[unit_id]
    if unit is None:
        raise SystemExit(f"DAT unit {unit_id} does not exist for civ {civ_id}")
    return unit


def _match_warning(prefix: str, match: UnitMatch) -> None:
    if any(graphic_prefix(name) == prefix for _id, name in match.graphics):
        return
    names = ", ".join(name for _id, name in match.graphics) or "no referenced graphics"
    print(
        f"warning: DAT civ {match.civ_id} unit {match.unit_id} "
        f"({getattr(match.unit, 'name', '')}) does not reference graphic prefix "
        f"'{prefix}' ({names}); continuing because {match.mapping_source} mapping was requested"
    )


def resolve_dat_unit(dat: Any, prefix: str, civ_id: int,
                     unit_id: int | None, unit_map: dict[str, dict[str, int]]) -> UnitMatch:
    if unit_id is not None:
        unit = get_dat_unit(dat, civ_id, unit_id)
        match = UnitMatch(civ_id, unit_id, unit, referenced_graphics(dat, unit), "explicit")
        _match_warning(prefix, match)
        return match

    if prefix in unit_map:
        entry = unit_map[prefix]
        mapped_civ, mapped_unit = entry["civ_id"], entry["unit_id"]
        unit = get_dat_unit(dat, mapped_civ, mapped_unit)
        match = UnitMatch(mapped_civ, mapped_unit, unit,
                          referenced_graphics(dat, unit), "map")
        _match_warning(prefix, match)
        return match

    civs = getattr(dat, "civs", ())
    if civ_id < 0 or civ_id >= len(civs):
        raise SystemExit(f"DAT civ id {civ_id} is out of range 0..{len(civs) - 1}")
    candidates: list[UnitMatch] = []
    for index, unit in enumerate(getattr(civs[civ_id], "units", ())):
        if unit is None:
            continue
        graphics = referenced_graphics(dat, unit)
        matched = tuple(item for item in graphics if graphic_prefix(item[1]) == prefix)
        if matched:
            candidates.append(UnitMatch(civ_id, index, unit, matched, "graphic_unique"))
    if len(candidates) == 1:
        return candidates[0]

    lines = [
        f"cannot uniquely map graphic prefix '{prefix}' to a gameplay unit "
        f"in civ {civ_id}: {len(candidates)} candidate(s)"
    ]
    for candidate in candidates:
        graphics = ", ".join(
            f"{graphic_id}:{name}" for graphic_id, name in candidate.graphics
        )
        lines.append(
            f"  civ={candidate.civ_id} unit={candidate.unit_id} "
            f"name={getattr(candidate.unit, 'name', '')!r} graphics=[{graphics}]"
        )
    lines.append("Use --unit-id (and --civ-id) or add an entry to --unit-map.")
    raise SystemExit("\n".join(lines))


def _number(value: Any) -> int | float:
    return value if isinstance(value, int) and not isinstance(value, bool) else float(value)


def _non_negative_finite(value: Any, name: str) -> float:
    result = float(value)
    if not math.isfinite(result) or result < 0.0:
        raise ExportError(f"{name} must be finite and non-negative, got {value!r}")
    return result


def serialize_dat_metadata(dat_path: Path, aoe2: Path, match: UnitMatch) -> dict[str, Any]:
    unit = match.unit
    source: str
    try:
        source = dat_path.resolve().relative_to(aoe2.resolve()).as_posix()
    except ValueError:
        source = dat_path.as_posix()
    value: dict[str, Any] = {
        "source": source,
        "civ_id": match.civ_id,
        "unit_id": match.unit_id,
        "unit_type": int(getattr(unit, "type")),
        "mapping_source": match.mapping_source,
        "collision_size": {
            "x": float(getattr(unit, "collision_size_x")),
            "y": float(getattr(unit, "collision_size_y")),
            "z": float(getattr(unit, "collision_size_z")),
        },
        "outline_size": {
            "x": _non_negative_finite(getattr(unit, "outline_size_x"),
                                       "DAT outline_size_x"),
            "y": _non_negative_finite(getattr(unit, "outline_size_y"),
                                       "DAT outline_size_y"),
            "z": _non_negative_finite(getattr(unit, "outline_size_z"),
                                       "DAT outline_size_z"),
        },
    }
    combat = getattr(unit, "type_50", None)
    creatable = getattr(unit, "creatable", None)
    if combat is not None:
        displacement = getattr(combat, "graphic_displacement")
        record: dict[str, Any] = {
            "projectile_unit_id": int(getattr(combat, "projectile_unit_id")),
            "frame_delay": int(getattr(combat, "frame_delay")),
            "weapon_offset": {
                "x": float(displacement[0]), "y": float(displacement[1]),
                "z": float(displacement[2]),
            },
            "accuracy_percent": int(getattr(combat, "accuracy_percent")),
            "accuracy_dispersion": float(getattr(combat, "accuracy_dispersion")),
            "min_range": float(getattr(combat, "min_range")),
            "max_range": float(getattr(combat, "max_range")),
            "reload_time": float(getattr(combat, "reload_time")),
            "blast_width": float(getattr(combat, "blast_width")),
            "blast_attack_level": int(getattr(combat, "blast_attack_level")),
            "attack_graphic_id": int(getattr(combat, "attack_graphic")),
        }
        if creatable is not None:
            area = getattr(creatable, "projectile_spawning_area")
            record.update({
                "secondary_projectile_unit_id": int(
                    getattr(creatable, "secondary_projectile_unit")),
                "projectile_min_count": _number(getattr(creatable, "total_projectiles")),
                "projectile_max_count": int(getattr(creatable, "max_total_projectiles")),
                "projectile_spawning_area": {
                    "width": float(area[0]), "length": float(area[1]),
                    "randomness": float(area[2]),
                },
            })
        value["combat"] = record
    elif creatable is not None:
        area = getattr(creatable, "projectile_spawning_area")
        value["creatable"] = {
            "secondary_projectile_unit_id": int(
                getattr(creatable, "secondary_projectile_unit")),
            "projectile_min_count": _number(getattr(creatable, "total_projectiles")),
            "projectile_max_count": int(getattr(creatable, "max_total_projectiles")),
            "projectile_spawning_area": {
                "width": float(area[0]), "length": float(area[1]),
                "randomness": float(area[2]),
            },
        }
    return value


def parse_graphic_name(name: str) -> tuple[str, str, str] | None:
    match = GRAPHIC_NAME_RE.match(name)
    if not match:
        return None
    return match.group("prefix"), match.group("action"), match.group("scale")


def discover_units(root: Path, pattern: str) -> dict[str, set[str]]:
    units: dict[str, set[str]] = {}
    for path in root.glob("*.sld"):
        parsed = parse_graphic_name(path.name)
        if not parsed:
            continue
        prefix, action, _scale = parsed
        if fnmatch.fnmatch(prefix, pattern):
            units.setdefault(prefix, set()).add(action)
    return units


def discover_unit_actions(root: Path, prefix: str, scale: str) -> list[str]:
    scales = ("x2", "x1") if scale == "auto" else (scale,)
    actions: set[str] = set()
    for path in root.glob(f"{prefix}_*.sld"):
        parsed = parse_graphic_name(path.name)
        if not parsed:
            continue
        found_prefix, action, found_scale = parsed
        if found_prefix == prefix and found_scale in scales:
            actions.add(action)
    return sorted(actions)


def print_unit_grid(units: list[str], page: int) -> int:
    total = len(units)
    if total == 0:
        print("no exportable units matched the pattern")
        return 0

    total_pages = max(1, math.ceil(total / LIST_PAGE_SIZE))
    if page < 1 or page > total_pages:
        raise SystemExit(f"page {page} out of range (1..{total_pages})")

    start = (page - 1) * LIST_PAGE_SIZE
    end = min(start + LIST_PAGE_SIZE, total)
    page_items = units[start:end]
    col_width = max(len(name) for name in page_items) + 2
    columns = max(1, 100 // col_width)
    rows = math.ceil(len(page_items) / columns)

    for row in range(rows):
        cells = []
        for col in range(columns):
            idx = col * rows + row
            if idx < len(page_items):
                cells.append(page_items[idx].ljust(col_width))
        print("".join(cells).rstrip())

    print(f"-- Page {page}/{total_pages} "
          f"(showing {start + 1}-{end} of {total} units) --")
    return 0


def list_units(aoe2: Path, pattern: str, page: int) -> int:
    root = graphics_dir(aoe2)
    if not root.is_dir():
        raise SystemExit(f"graphics directory does not exist: {root}")
    return print_unit_grid(sorted(discover_units(root, pattern)), page)


def load_openage(openage_path: Path):
    tool_dir = Path(__file__).resolve().parent
    sys.path.insert(0, str(tool_dir))

    try:
        from sld.sld import SLD
        from sld.texture import Texture
        return SLD, Texture
    except Exception as local_exc:  # noqa: BLE001
        local_error = local_exc

    if openage_path:
        sys.path.insert(0, str(openage_path))

    try:
        from openage.convert.value_object.read.media.sld import SLD
        from openage.convert.entity_object.export.texture import Texture
    except Exception as exc:  # noqa: BLE001
        raise SystemExit(
            "Local SLD extension is not built, and openage fallback import failed.\n"
            "Build the local extension:\n"
            "  python -m pip install cython numpy pillow\n"
            "  cmake -S tools\\aoe2de_export -B tools\\aoe2de_export\\build\n"
            "  cmake --build tools\\aoe2de_export\\build --target aoe2de_export_sld\n"
            f"Local import error: {local_error}\n"
            f"openage fallback error: {exc}"
        ) from exc
    return SLD, Texture


def positive_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed <= 0:
        raise argparse.ArgumentTypeError("must be greater than zero")
    return parsed


def non_negative_int(value: str) -> int:
    try:
        parsed = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be an integer") from exc
    if parsed < 0:
        raise argparse.ArgumentTypeError("must be non-negative")
    return parsed


def positive_finite_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return parsed


def validate_playercolor_args(args) -> None:
    if args.playercolor_temporal_filter != "off":
        raise SystemExit(
            "consensus3 is not supported for rgba8_bc4_decoded; "
            "use --playercolor-temporal-filter off"
        )


def validate_resource_id(resource_id: str) -> str:
    if resource_id in {".", ".."} or not RESOURCE_ID_RE.fullmatch(resource_id):
        raise SystemExit(
            "resource id must be one path component containing only letters, "
            "digits, '.', '_' or '-'"
        )
    return resource_id


def target_directory(root: Path, category: str, resource_id: str) -> Path:
    resource_id = validate_resource_id(resource_id)
    if category not in {"units", "graphics"}:
        raise SystemExit(f"invalid cache category: {category}")
    resolved_root = root.resolve()
    category_root = (resolved_root / category).resolve()
    target = (category_root / resource_id).resolve()
    if category_root.parent != resolved_root or target.parent != category_root:
        raise SystemExit(f"unsafe output target outside root: {target}")
    return target


def clean_target(root: Path, category: str, resource_id: str) -> Path:
    target = target_directory(root, category, resource_id)
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if not target.is_dir():
            raise SystemExit(f"output target exists and is not a directory: {target}")
        shutil.rmtree(target)
    target.mkdir(parents=True)
    return target


def write_json(path: Path, value: Any) -> None:
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def read_sld_frame_records(data: bytes) -> list[dict[str, int]]:
    """Read physical frame metadata without decoding image blocks."""
    if len(data) < SLD_HEADER.size:
        raise ExportError("SLD header is truncated")
    _signature, _version, frame_count, _u1, _u2, _u3 = SLD_HEADER.unpack_from(data)
    offset = SLD_HEADER.size
    records: list[dict[str, int]] = []

    for ordinal in range(frame_count):
        if offset + SLD_FRAME_HEADER.size > len(data):
            raise ExportError(f"SLD frame header {ordinal} is truncated")
        values = SLD_FRAME_HEADER.unpack_from(data, offset)
        offset += SLD_FRAME_HEADER.size
        frame_type = int(values[4])
        frame_index = int(values[6])
        records.append({
            "ordinal": ordinal,
            "frame_index": frame_index,
            "frame_type": frame_type,
        })

        for bit in LAYER_BITS.values():
            if not frame_type & bit:
                continue
            if offset + SLD_LAYER_LENGTH.size > len(data):
                raise ExportError(
                    f"SLD layer length for physical frame {ordinal} is truncated"
                )
            layer_length = SLD_LAYER_LENGTH.unpack_from(data, offset)[0]
            if layer_length < SLD_LAYER_LENGTH.size:
                raise ExportError(
                    f"SLD physical frame {ordinal} has invalid layer length {layer_length}"
                )
            offset += layer_length
            offset += (4 - offset) % 4
            if offset > len(data):
                raise ExportError(f"SLD physical frame {ordinal} layer exceeds file size")
    return records


def resolve_scaled_graphic(root: Path, prefix: str, action: str, scale: str) -> Path | None:
    scales = ("x2", "x1") if scale == "auto" else (scale,)
    for scale_name in scales:
        path = root / f"{prefix}_{action}_{scale_name}.sld"
        if path.is_file():
            return path
    return None


def decoded_layer_map(Texture, sld, records: list[dict[str, int]], layer_name: str):
    layer = DECODED_LAYERS[layer_name]
    expected = [record for record in records if record["frame_type"] & LAYER_BITS[layer_name]]
    texture = Texture(sld, layer=layer)
    frames = list(texture.frames)
    if len(frames) != len(expected):
        raise ExportError(
            f"{layer_name} decoded frame count {len(frames)} differs from "
            f"SLD header count {len(expected)}"
        )
    return {record["ordinal"]: frame for record, frame in zip(expected, frames)}


def export_frame(frame, record: dict[str, int], *, present: bool = True) -> ExportFrame:
    image = frame.get_pil_image().convert("RGBA")
    hotspot = tuple(int(value) for value in frame.hotspot)
    return ExportFrame(
        image=image,
        width=int(frame.width),
        height=int(frame.height),
        foot=(hotspot[0], hotspot[1]),
        source_ordinal=record["ordinal"],
        source_frame_index=record["frame_index"],
        present=present,
    )


def transparent_frame(record: dict[str, int], width: int, height: int,
                      foot: tuple[int, int]) -> ExportFrame:
    from PIL import Image

    return ExportFrame(
        image=Image.new("RGBA", (width, height), (0, 0, 0, 0)),
        width=width,
        height=height,
        foot=foot,
        source_ordinal=record["ordinal"],
        source_frame_index=record["frame_index"],
        present=False,
    )


def choose_layout(frames: list[ExportFrame]) -> AtlasLayout:
    if not frames:
        raise ExportError("cannot pack an empty frame list")
    cell_w = max(frame.width for frame in frames)
    cell_h = max(frame.height for frame in frames)
    if cell_w <= 0 or cell_h <= 0:
        raise ExportError("frame dimensions must be greater than zero")
    cols = max(1, math.ceil(math.sqrt(len(frames))))
    rows = math.ceil(len(frames) / cols)
    return AtlasLayout(cols, rows, cell_w, cell_h)


def pack_frames(frames: list[ExportFrame], frames_per_direction: int,
                layout: AtlasLayout | None = None, *, preserve_pixels: bool = False):
    from PIL import Image

    layout = layout or choose_layout(frames)
    if len(frames) > layout.cols * layout.rows:
        raise ExportError("forced atlas layout has insufficient cells")
    if any(frame.width > layout.cell_w or frame.height > layout.cell_h for frame in frames):
        raise ExportError("frame exceeds forced atlas cell dimensions")

    atlas = Image.new("RGBA", (layout.width, layout.height), (0, 0, 0, 0))
    metadata = []
    for idx, frame in enumerate(frames):
        col = idx % layout.cols
        row = idx // layout.cols
        x = col * layout.cell_w
        y = row * layout.cell_h
        if preserve_pixels:
            atlas.paste(frame.image, (x, y))
        else:
            atlas.alpha_composite(frame.image, (x, y))
        metadata.append({
            "source_ordinal": frame.source_ordinal,
            "source_frame_index": frame.source_frame_index,
            "direction": idx // frames_per_direction,
            "frame": idx % frames_per_direction,
            "present": frame.present,
            "x": x,
            "y": y,
            "w": frame.width,
            "h": frame.height,
            "foot": {
                "x": frame.foot[0],
                "y": frame.foot[1],
                "space": "frame_pixels_top_left",
            },
        })
    return atlas, metadata, layout


def empty_layer_record(status: str, source_count: int, missing: list[int] | None = None,
                       warning: str | None = None) -> dict[str, Any]:
    value = {
        "status": status,
        "source_present_frame_count": source_count,
        "exported_frame_count": 0,
        "missing_source_frames": missing or [],
        "image": None,
        "atlas_w": 0,
        "atlas_h": 0,
        "frames": [],
    }
    if warning:
        value["warning"] = warning
    return value


def image_layer_record(status: str, source_count: int, image_name: str, atlas,
                       metadata: list[dict], missing: list[int]) -> dict[str, Any]:
    return {
        "status": status,
        "source_present_frame_count": source_count,
        "exported_frame_count": len(metadata),
        "missing_source_frames": missing,
        "image": image_name,
        "atlas_w": atlas.width,
        "atlas_h": atlas.height,
        "frames": metadata,
    }


def bake_playercolor_index_mask(mask, base):
    """Encode SLD layer 4 as R8: zero is absent; one..128 are palette indices."""
    from PIL import Image

    mask = mask.convert("RGBA")
    base = base.convert("RGBA")
    if mask.size != base.size:
        raise ExportError(
            f"player-color atlas size {mask.size} differs from main atlas {base.size}"
        )
    out = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    mask_px = mask.load()
    base_px = base.load()
    out_px = out.load()

    for y in range(mask.height):
        for x in range(mask.width):
            # SLD stores the meaningful 128 player-color entries in the upper
            # half of the decoded BC4 channel, in reverse palette order:
            # 255 -> palette index 0 and 128 -> palette index 127.  The lower
            # half is outside the runtime player-color palette, except for a
            # narrow transition band recovered across adjacent frames below.
            # Index zero is valid, hence the +1 encoding in the exported R8.
            diffuse = base_px[x, y]
            if diffuse[3] <= PLAYERCOLOR_DIFFUSE_ALPHA_MIN:
                continue
            raw_value = mask_px[x, y][0]
            if mask_px[x, y][3] == 0 or raw_value < PLAYERCOLOR_STRONG_RAW_MIN:
                continue
            palette_index = 255 - raw_value
            out_px[x, y] = (palette_index + 1, 0, 0, 255)
    return out


def _frame_with_image(frame: ExportFrame, image) -> ExportFrame:
    return ExportFrame(
        image=image,
        width=frame.width,
        height=frame.height,
        foot=frame.foot,
        source_ordinal=frame.source_ordinal,
        source_frame_index=frame.source_frame_index,
        present=frame.present,
    )


def bake_playercolor_frames(player_frames: list[ExportFrame],
                            main_frames: list[ExportFrame],
                            frames_per_direction: int,
                            animation_name: str):
    """Bake strong indices and recover only temporally supported BC4 edges."""
    import numpy
    from PIL import Image

    if len(player_frames) != len(main_frames):
        raise ExportError("player-color and main frame counts differ")
    if frames_per_direction <= 0 or len(player_frames) % frames_per_direction:
        raise ExportError("invalid frame count for player-color transition recovery")
    output = [
        _frame_with_image(player, bake_playercolor_index_mask(
            player.image, main.image
        ))
        for player, main in zip(player_frames, main_frames)
    ]
    stats = {
        "mode": "aligned_transition_consensus",
        "transition_raw_min": PLAYERCOLOR_TRANSITION_RAW_MIN,
        "strong_raw_min": PLAYERCOLOR_STRONG_RAW_MIN,
        "base_rgb_tolerance": PLAYERCOLOR_BASE_RGB_TOLERANCE,
        "pixels_recovered": 0,
    }
    if frames_per_direction < 3:
        return output, stats

    looping = is_looping_animation(animation_name)
    direction_count = len(player_frames) // frames_per_direction
    for direction in range(direction_count):
        start = direction * frames_per_direction
        for local_index in range(frames_per_direction):
            if not looping and local_index in (0, frames_per_direction - 1):
                continue
            previous_local = (local_index - 1) % frames_per_direction
            following_local = (local_index + 1) % frames_per_direction
            current_index = start + local_index
            previous = player_frames[start + previous_local]
            current = player_frames[current_index]
            following = player_frames[start + following_local]
            if not (previous.present and current.present and following.present):
                continue

            current_raw = numpy.asarray(
                current.image.convert("RGBA"), dtype=numpy.uint8
            )
            current_main = numpy.asarray(
                main_frames[current_index].image.convert("RGBA"), dtype=numpy.uint8
            )
            previous_raw, previous_valid = _sample_aligned(previous, current)
            following_raw, following_valid = _sample_aligned(following, current)
            previous_main, previous_main_valid = _sample_aligned(
                main_frames[start + previous_local], main_frames[current_index]
            )
            following_main, following_main_valid = _sample_aligned(
                main_frames[start + following_local], main_frames[current_index]
            )

            transition = (
                (current_raw[:, :, 3] > 0) &
                (current_raw[:, :, 0] >= PLAYERCOLOR_TRANSITION_RAW_MIN) &
                (current_raw[:, :, 0] < PLAYERCOLOR_STRONG_RAW_MIN) &
                (current_main[:, :, 3] > PLAYERCOLOR_DIFFUSE_ALPHA_MIN)
            )
            previous_support = (
                (previous_raw[:, :, 3] > 0) &
                (previous_raw[:, :, 0] >= PLAYERCOLOR_TRANSITION_RAW_MIN)
            )
            following_support = (
                (following_raw[:, :, 3] > 0) &
                (following_raw[:, :, 0] >= PLAYERCOLOR_TRANSITION_RAW_MIN)
            )
            current_rgb = current_main[:, :, :3].astype(numpy.int16)
            previous_rgb = previous_main[:, :, :3].astype(numpy.int16)
            following_rgb = following_main[:, :, :3].astype(numpy.int16)
            stable_base = (
                numpy.max(numpy.abs(previous_rgb - current_rgb), axis=2)
                <= PLAYERCOLOR_BASE_RGB_TOLERANCE
            ) & (
                numpy.max(numpy.abs(following_rgb - current_rgb), axis=2)
                <= PLAYERCOLOR_BASE_RGB_TOLERANCE
            )
            recovered = (
                transition & previous_valid & following_valid &
                previous_main_valid & following_main_valid &
                (previous_main[:, :, 3] > PLAYERCOLOR_DIFFUSE_ALPHA_MIN) &
                (following_main[:, :, 3] > PLAYERCOLOR_DIFFUSE_ALPHA_MIN) &
                previous_support & following_support & stable_base
            )
            recovered_count = int(numpy.count_nonzero(recovered))
            if recovered_count == 0:
                continue
            baked = numpy.asarray(
                output[current_index].image.convert("RGBA"), dtype=numpy.uint8
            ).copy()
            # Transition values map beyond the 128-entry palette. Clamp them
            # to its last entry while preserving coverage in the R8 encoding.
            baked[recovered] = (128, 0, 0, 255)
            output[current_index] = _frame_with_image(
                current, Image.fromarray(baked, "RGBA")
            )
            stats["pixels_recovered"] += recovered_count
    return output, stats


def is_looping_animation(name: str) -> bool:
    lowered = name.lower()
    return lowered.startswith(("idle", "walk", "attack"))


def _sample_aligned(frame: ExportFrame, current: ExportFrame):
    """Sample frame into current's pixel grid after aligning their foot points."""
    import numpy

    source = numpy.asarray(frame.image.convert("RGBA"), dtype=numpy.uint8)
    result = numpy.zeros((current.height, current.width, 4), dtype=numpy.uint8)
    valid = numpy.zeros((current.height, current.width), dtype=bool)
    delta_x = frame.foot[0] - current.foot[0]
    delta_y = frame.foot[1] - current.foot[1]
    current_x0 = max(0, -delta_x)
    current_y0 = max(0, -delta_y)
    current_x1 = min(current.width, frame.width - delta_x)
    current_y1 = min(current.height, frame.height - delta_y)
    if current_x0 >= current_x1 or current_y0 >= current_y1:
        return result, valid
    source_x0 = current_x0 + delta_x
    source_y0 = current_y0 + delta_y
    source_x1 = current_x1 + delta_x
    source_y1 = current_y1 + delta_y
    result[current_y0:current_y1, current_x0:current_x1] = source[
        source_y0:source_y1, source_x0:source_x1
    ]
    valid[current_y0:current_y1, current_x0:current_x1] = True
    return result, valid


def stabilize_playercolor_frames(player_frames: list[ExportFrame],
                                 main_frames: list[ExportFrame],
                                 frames_per_direction: int, name: str,
                                 mode: str, alpha_min: int):
    """Remove isolated mask/shade pops without blending moving sprite pixels."""
    import numpy
    from PIL import Image

    if len(player_frames) != len(main_frames):
        raise ExportError("player-color and main frame counts differ")
    if mode == "off":
        return player_frames, {
            "mode": mode, "version": PLAYERCOLOR_TEMPORAL_FILTER_VERSION,
            "coverage_pixels_changed": 0, "shade_pixels_changed": 0,
        }
    if mode != "consensus3":
        raise ExportError(f"unknown player-color temporal filter: {mode}")
    if frames_per_direction <= 0 or len(player_frames) % frames_per_direction:
        raise ExportError("invalid frame count for player-color temporal filter")

    looping = is_looping_animation(name)
    output = list(player_frames)
    coverage_changed = 0
    shade_changed = 0
    direction_count = len(player_frames) // frames_per_direction
    if frames_per_direction < 3:
        return output, {
            "mode": mode, "version": PLAYERCOLOR_TEMPORAL_FILTER_VERSION,
            "coverage_pixels_changed": 0, "shade_pixels_changed": 0,
        }

    for direction in range(direction_count):
        start = direction * frames_per_direction
        for local_index in range(frames_per_direction):
            if not looping and local_index in (0, frames_per_direction - 1):
                continue
            previous_local = (local_index - 1) % frames_per_direction
            next_local = (local_index + 1) % frames_per_direction
            indices = (
                start + previous_local,
                start + local_index,
                start + next_local,
            )
            previous, current, following = [player_frames[index] for index in indices]
            if not (previous.present and current.present and following.present):
                continue
            previous_main, current_main, following_main = [
                main_frames[index] for index in indices
            ]

            current_mask = numpy.asarray(
                current.image.convert("RGBA"), dtype=numpy.uint8
            ).copy()
            current_base = numpy.asarray(
                current_main.image.convert("RGBA"), dtype=numpy.uint8
            )
            previous_mask, previous_mask_valid = _sample_aligned(previous, current)
            following_mask, following_mask_valid = _sample_aligned(following, current)
            previous_base, previous_base_valid = _sample_aligned(
                previous_main, current_main
            )
            following_base, following_base_valid = _sample_aligned(
                following_main, current_main
            )

            valid = (previous_mask_valid & following_mask_valid &
                     previous_base_valid & following_base_valid)
            opaque = ((current_base[:, :, 3] > alpha_min) &
                      (previous_base[:, :, 3] > alpha_min) &
                      (following_base[:, :, 3] > alpha_min))
            current_rgb = current_base[:, :, :3].astype(numpy.int16)
            previous_rgb = previous_base[:, :, :3].astype(numpy.int16)
            following_rgb = following_base[:, :, :3].astype(numpy.int16)
            stable_base = (
                numpy.max(numpy.abs(previous_rgb - current_rgb), axis=2)
                <= PLAYERCOLOR_BASE_RGB_TOLERANCE
            ) & (
                numpy.max(numpy.abs(following_rgb - current_rgb), axis=2)
                <= PLAYERCOLOR_BASE_RGB_TOLERANCE
            )
            eligible = valid & opaque & stable_base

            previous_hit = previous_mask[:, :, 3] == 255
            current_hit = current_mask[:, :, 3] == 255
            following_hit = following_mask[:, :, 3] == 255
            neighbor_coverage_agrees = previous_hit == following_hit
            coverage_fix = eligible & neighbor_coverage_agrees & (
                current_hit != previous_hit
            )
            add = coverage_fix & previous_hit
            remove = coverage_fix & ~previous_hit
            neighbor_shade_difference = numpy.abs(
                previous_mask[:, :, 0].astype(numpy.int16) -
                following_mask[:, :, 0].astype(numpy.int16)
            )
            agreed_shade = numpy.rint((
                previous_mask[:, :, 0].astype(numpy.float32) +
                following_mask[:, :, 0].astype(numpy.float32)
            ) * 0.5).astype(numpy.uint8)
            current_shade_difference = numpy.abs(
                current_mask[:, :, 0].astype(numpy.int16) -
                agreed_shade.astype(numpy.int16)
            )
            shade_fix = (eligible & previous_hit & current_hit & following_hit &
                         (neighbor_shade_difference <= 1) &
                         (current_shade_difference > 1))

            current_mask[add, 0] = agreed_shade[add]
            current_mask[add, 1:3] = 0
            current_mask[add, 3] = 255
            current_mask[remove] = 0
            current_mask[shade_fix, 0] = agreed_shade[shade_fix]
            coverage_changed += int(numpy.count_nonzero(coverage_fix))
            shade_changed += int(numpy.count_nonzero(shade_fix))
            output[indices[1]] = _frame_with_image(
                current, Image.fromarray(current_mask, "RGBA")
            )

    return output, {
        "mode": mode,
        "version": PLAYERCOLOR_TEMPORAL_FILTER_VERSION,
        "base_rgb_tolerance": PLAYERCOLOR_BASE_RGB_TOLERANCE,
        "coverage_pixels_changed": coverage_changed,
        "shade_pixels_changed": shade_changed,
    }


def _aligned_pair_arrays(first: ExportFrame, second: ExportFrame):
    import numpy

    min_x = min(-first.foot[0], -second.foot[0])
    min_y = min(-first.foot[1], -second.foot[1])
    max_x = max(first.width - first.foot[0], second.width - second.foot[0])
    max_y = max(first.height - first.foot[1], second.height - second.foot[1])
    shape = (max_y - min_y, max_x - min_x, 4)
    first_canvas = numpy.zeros(shape, dtype=numpy.uint8)
    second_canvas = numpy.zeros(shape, dtype=numpy.uint8)
    for frame, canvas in ((first, first_canvas), (second, second_canvas)):
        x = -frame.foot[0] - min_x
        y = -frame.foot[1] - min_y
        canvas[y:y + frame.height, x:x + frame.width] = numpy.asarray(
            frame.image.convert("RGBA"), dtype=numpy.uint8
        )
    return first_canvas, second_canvas


def _mask_pair_metrics(first_mask: ExportFrame, second_mask: ExportFrame,
                       first_main: ExportFrame, second_main: ExportFrame):
    import numpy

    mask_a, mask_b = _aligned_pair_arrays(first_mask, second_mask)
    main_a, main_b = _aligned_pair_arrays(first_main, second_main)
    hit_a = mask_a[:, :, 3] == 255
    hit_b = mask_b[:, :, 3] == 255
    body_a = main_a[:, :, 3] > 8
    body_b = main_b[:, :, 3] > 8

    def iou(first, second) -> float:
        union = int(numpy.count_nonzero(first | second))
        if union == 0:
            return 1.0
        return float(numpy.count_nonzero(first & second) / union)

    area_a = int(numpy.count_nonzero(hit_a))
    area_b = int(numpy.count_nonzero(hit_b))
    area_change = abs(area_b - area_a) / max(1, area_a, area_b)
    if area_a and area_b:
        y_a, x_a = numpy.nonzero(hit_a)
        y_b, x_b = numpy.nonzero(hit_b)
        centroid_jump = math.hypot(
            float(x_b.mean() - x_a.mean()), float(y_b.mean() - y_a.mean())
        )
    else:
        centroid_jump = 0.0 if area_a == area_b else float("inf")
    overlap = hit_a & hit_b
    overlap_count = int(numpy.count_nonzero(overlap))
    shade_change = 0.0
    if overlap_count:
        shade_change = float(numpy.count_nonzero(
            mask_a[:, :, 0][overlap] != mask_b[:, :, 0][overlap]
        ) / overlap_count)
    return {
        "body_iou": round(iou(body_a, body_b), 6),
        "mask_iou": round(iou(hit_a, hit_b), 6),
        "mask_area_change_ratio": round(area_change, 6),
        "mask_centroid_jump_pixels": (
            round(centroid_jump, 6) if math.isfinite(centroid_jump) else None
        ),
        "subcolor_change_ratio": round(shade_change, 6),
    }


def write_playercolor_diagnostics(root: Path, out_dir: Path, name: str,
                                  before: list[ExportFrame],
                                  after: list[ExportFrame],
                                  main_frames: list[ExportFrame],
                                  frames_per_direction: int,
                                  main_layout: AtlasLayout):
    import numpy
    from PIL import Image

    target = root / out_dir.parent.name / out_dir.name / name
    target.mkdir(parents=True, exist_ok=True)
    before_atlas, _, _ = pack_frames(
        before, frames_per_direction, layout=main_layout
    )
    after_atlas, _, _ = pack_frames(
        after, frames_per_direction, layout=main_layout
    )
    before_atlas.save(target / "before.png")
    after_atlas.save(target / "after.png")

    difference_frames = []
    for old, new in zip(before, after):
        old_pixels = numpy.asarray(old.image.convert("RGBA"), dtype=numpy.uint8)
        new_pixels = numpy.asarray(new.image.convert("RGBA"), dtype=numpy.uint8)
        old_hit = old_pixels[:, :, 3] == 255
        new_hit = new_pixels[:, :, 3] == 255
        coverage_changed = old_hit != new_hit
        shade_changed = old_hit & new_hit & (
            old_pixels[:, :, 0] != new_pixels[:, :, 0]
        )
        diff = numpy.zeros_like(old_pixels)
        diff[coverage_changed] = (255, 0, 255, 255)
        diff[shade_changed] = (255, 255, 0, 255)
        difference_frames.append(_frame_with_image(
            old, Image.fromarray(diff, "RGBA")
        ))
    difference_atlas, _, _ = pack_frames(
        difference_frames, frames_per_direction, layout=main_layout
    )
    difference_atlas.save(target / "difference.png")

    direction_count = len(before) // frames_per_direction
    looping = is_looping_animation(name)
    pairs = []
    flagged_source_frames: list[int] = []
    for direction in range(direction_count):
        start = direction * frames_per_direction
        pair_count = frames_per_direction if looping else frames_per_direction - 1
        for local_index in range(max(0, pair_count)):
            next_local = (local_index + 1) % frames_per_direction
            first_index = start + local_index
            second_index = start + next_local
            raw = _mask_pair_metrics(
                before[first_index], before[second_index],
                main_frames[first_index], main_frames[second_index],
            )
            stabilized = _mask_pair_metrics(
                after[first_index], after[second_index],
                main_frames[first_index], main_frames[second_index],
            )
            flagged = raw["body_iou"] >= 0.95 and raw["mask_iou"] < 0.8
            if flagged:
                flagged_source_frames.extend((
                    before[first_index].source_frame_index,
                    before[second_index].source_frame_index,
                ))
            pairs.append({
                "direction": direction,
                "from_frame": local_index,
                "to_frame": next_local,
                "from_source_frame": before[first_index].source_frame_index,
                "to_source_frame": before[second_index].source_frame_index,
                "flagged_stable_body_mask_jump": flagged,
                "before": raw,
                "after": stabilized,
            })
    report = {
        "animation": name,
        "frames_per_direction": frames_per_direction,
        "direction_count": direction_count,
        "looping": looping,
        "thresholds": {"stable_body_iou_min": 0.95, "mask_iou_max": 0.8},
        "flagged_pair_count": sum(
            pair["flagged_stable_body_mask_jump"] for pair in pairs
        ),
        "pairs": pairs,
    }
    write_json(target / "metrics.json", report)
    return sorted(set(flagged_source_frames))


def layer_presence(records: list[dict[str, int]], layer_name: str) -> list[dict[str, int]]:
    bit = LAYER_BITS[layer_name]
    return [record for record in records if record["frame_type"] & bit]


def warning(code: str, message: str, source_frames: list[int] | None = None) -> dict[str, Any]:
    value: dict[str, Any] = {"code": code, "message": message}
    if source_frames is not None:
        value["source_frames"] = source_frames
    print(f"warning: {message}")
    return value


def export_animation(SLD, Texture, source: Path, out_dir: Path, name: str,
                     directions: int, fps: float):
    data = source.read_bytes()
    records = read_sld_frame_records(data)
    source_frame_count = len(records)
    frames_per_direction = source_frame_count // directions
    if frames_per_direction <= 0:
        raise ExportError(
            f"{source.name} has {source_frame_count} frames, fewer than "
            f"the requested {directions} directions"
        )

    usable_count = frames_per_direction * directions
    usable_records = records[:usable_count]
    unused_records = records[usable_count:]
    warnings: list[dict[str, Any]] = []
    if unused_records:
        unused_indices = [record["frame_index"] for record in unused_records]
        warnings.append(warning(
            "direction_remainder",
            f"{source.name} has {source_frame_count} frames; using {usable_count} "
            f"({directions} directions x {frames_per_direction}) and dropping "
            f"{len(unused_records)} trailing frame(s): {unused_indices}",
            unused_indices,
        ))

    sld = SLD(data)
    graphics_out = out_dir / "graphics"
    graphics_out.mkdir(parents=True, exist_ok=True)

    presence_counts = {
        layer_name: len(layer_presence(records, layer_name))
        for layer_name in LAYER_BITS
    }

    main_map = decoded_layer_map(Texture, sld, records, "main")
    missing_main = [
        record["frame_index"] for record in usable_records
        if record["ordinal"] not in main_map
    ]
    if missing_main:
        raise ExportError(
            f"{source.name} main layer is missing usable source frames {missing_main}"
        )
    main_frames = [
        export_frame(main_map[record["ordinal"]], record)
        for record in usable_records
    ]
    main_atlas, main_meta, main_layout = pack_frames(
        main_frames, frames_per_direction
    )
    main_image = f"{name}.png"
    main_atlas.save(graphics_out / main_image)
    layers: dict[str, dict[str, Any]] = {
        "main": image_layer_record(
            "complete", presence_counts["main"], main_image,
            main_atlas, main_meta, []
        )
    }

    # Shadow has its own crop and UV layout, but shares physical frame order.
    if presence_counts["shadow"] == 0:
        layers["shadow"] = empty_layer_record("missing", 0)
    else:
        try:
            shadow_map = decoded_layer_map(Texture, sld, records, "shadow")
            missing_shadow = [
                record["frame_index"] for record in usable_records
                if record["ordinal"] not in shadow_map
            ]
            shadow_frames = []
            for record in usable_records:
                frame = shadow_map.get(record["ordinal"])
                if frame is None:
                    shadow_frames.append(transparent_frame(record, 1, 1, (0, 0)))
                else:
                    shadow_frames.append(export_frame(frame, record))
            shadow_atlas, shadow_meta, _ = pack_frames(
                shadow_frames, frames_per_direction
            )
            shadow_image = f"{name}_shadow.png"
            shadow_atlas.save(graphics_out / shadow_image)
            shadow_status = "partial" if missing_shadow else "complete"
            if missing_shadow:
                warnings.append(warning(
                    "partial_shadow",
                    f"{source.name} shadow layer is missing {len(missing_shadow)} "
                    f"usable frame(s); transparent placeholders were inserted",
                    missing_shadow,
                ))
            layers["shadow"] = image_layer_record(
                shadow_status, presence_counts["shadow"], shadow_image,
                shadow_atlas, shadow_meta, missing_shadow
            )
        except Exception as exc:  # noqa: BLE001
            message = f"{source.name} shadow layer is invalid: {exc}"
            warnings.append(warning("invalid_shadow", message))
            layers["shadow"] = empty_layer_record(
                "invalid", presence_counts["shadow"], warning=message
            )

    for layer_name in ("outline", "damage"):
        count = presence_counts[layer_name]
        if count:
            layers[layer_name] = empty_layer_record("unsupported", count)
            warnings.append(warning(
                f"unsupported_{layer_name}",
                f"{source.name} contains {count} {layer_name} frame(s), but "
                f"that layer is not exported",
            ))
        else:
            layers[layer_name] = empty_layer_record("missing", 0)

    # Player-color must use exactly the main atlas layout and UVs.
    if presence_counts["player_color"] == 0:
        layers["player_color"] = empty_layer_record("missing", 0)
    else:
        try:
            player_map = decoded_layer_map(Texture, sld, records, "player_color")
            missing_player = []
            player_frames = []
            for record, main_frame in zip(usable_records, main_frames):
                frame = player_map.get(record["ordinal"])
                if frame is None:
                    missing_player.append(record["frame_index"])
                    player_frames.append(transparent_frame(
                        record, main_frame.width, main_frame.height, main_frame.foot
                    ))
                    continue
                player_frame = export_frame(frame, record)
                if ((player_frame.width, player_frame.height) !=
                        (main_frame.width, main_frame.height) or
                        player_frame.foot != main_frame.foot):
                    raise ExportError(
                        f"source frame {record['frame_index']} geometry differs "
                        "from main; SLD mask layers must inherit main geometry"
                    )
                player_frames.append(player_frame)

            player_atlas, player_meta, _ = pack_frames(
                player_frames, frames_per_direction, layout=main_layout,
                preserve_pixels=True
            )
            if player_atlas.size != main_atlas.size:
                raise ExportError("player-color atlas does not match main atlas")
            player_image = f"{name}_playercolor.png"
            player_atlas.save(graphics_out / player_image)
            player_status = "partial" if missing_player else "complete"
            if missing_player:
                warnings.append(warning(
                    "partial_player_color",
                    f"{source.name} player-color layer is missing "
                    f"{len(missing_player)} usable frame(s); transparent "
                    "placeholders were inserted",
                    missing_player,
                ))
            player_layer = image_layer_record(
                player_status, presence_counts["player_color"], player_image,
                player_atlas, player_meta, missing_player
            )
            layers["player_color"] = player_layer
        except Exception as exc:  # noqa: BLE001
            message = f"{source.name} player-color layer is invalid: {exc}"
            warnings.append(warning("invalid_player_color", message))
            layers["player_color"] = empty_layer_record(
                "invalid", presence_counts["player_color"], warning=message
            )

    parsed = parse_graphic_name(source.name)
    actual_scale = parsed[2] if parsed else None
    config = {
        "schema_version": GRAPHICS_SCHEMA_VERSION,
        "name": name,
        "source": source.name,
        "scale": actual_scale,
        "source_frame_count": source_frame_count,
        "exported_frame_count": usable_count,
        "direction_count": directions,
        "frames_per_direction": frames_per_direction,
        "fps": fps,
        "frame_order": "direction_major",
        "unused_source_frames": [
            record["frame_index"] for record in unused_records
        ],
        "warnings": warnings,
        "layers": layers,
    }
    config_name = f"{name}.json"
    write_json(graphics_out / config_name, config)
    layer_summary = {
        layer_name: layer["status"] for layer_name, layer in layers.items()
    }
    return {
        "status": "exported",
        "source": source.name,
        "scale": actual_scale,
        "config": f"graphics/{config_name}",
        "layers": layer_summary,
        "warning_count": len(warnings),
    }


def manifest_settings(args) -> dict[str, Any]:
    return {
        "scale": args.scale,
        "directions": args.directions,
        "fps": args.fps,
        "player_color": {
            "format": PLAYERCOLOR_FORMAT,
        },
    }


def finish_manifest(manifest: dict[str, Any]) -> None:
    records = list(manifest["animations"].values())
    exported = sum(record["status"] == "exported" for record in records)
    missing = sum(record["status"] == "missing_source" for record in records)
    invalid = sum(record["status"] == "invalid" for record in records)
    warning_count = sum(int(record.get("warning_count", 0)) for record in records)
    manifest["summary"] = {
        "complete": missing == 0 and invalid == 0,
        "exported_animation_count": exported,
        "missing_animation_count": missing,
        "invalid_animation_count": invalid,
        "warning_count": warning_count,
    }


def invalid_animation_record(source: Path, message: str) -> dict[str, Any]:
    print(f"warning: {message}")
    return {
        "status": "invalid",
        "source": source.name,
        "scale": parse_graphic_name(source.name)[2] if parse_graphic_name(source.name) else None,
        "config": None,
        "layers": {"main": "invalid"},
        "warning_count": 1,
        "error": message,
    }


def run_exports(args, manifest: dict[str, Any], sources: dict[str, Path],
                out_dir: Path) -> None:
    if not sources:
        return
    SLD, Texture = load_openage(args.openage)
    for name, source in sources.items():
        try:
            record = export_animation(
                SLD, Texture, source, out_dir, name,
                args.directions, args.fps,
            )
            manifest["animations"][name] = record
            print(f"exported {source.name} -> {record['config']}")
        except Exception as exc:  # noqa: BLE001
            manifest["animations"][name] = invalid_animation_record(
                source, f"failed to export {source.name}: {exc}"
            )


def export_unit(args) -> int:
    root = graphics_dir(args.aoe2)
    if not root.is_dir():
        raise SystemExit(f"graphics directory does not exist: {root}")
    resource_id = args.name or args.unit
    validate_resource_id(resource_id)
    discovered = discover_unit_actions(root, args.unit, args.scale)
    if args.animations is None:
        if not discovered:
            raise SystemExit(
                f"no graphics found for unit prefix '{args.unit}' "
                f"(scale {args.scale}) under {root}"
            )
        requested = discovered
    else:
        requested = list(dict.fromkeys(args.animations))

    sources: dict[str, Path] = {}
    missing: list[str] = []
    for action in requested:
        source = resolve_scaled_graphic(root, args.unit, action, args.scale)
        if source is None:
            missing.append(action)
        else:
            sources[action] = source

    dat_path = args.dat or dat_path_for(args.aoe2)
    dat = load_dat(dat_path)
    unit_map = load_unit_map(args.unit_map)
    dat_match = resolve_dat_unit(dat, args.unit, args.civ_id, args.unit_id, unit_map)
    dat_metadata = serialize_dat_metadata(dat_path, args.aoe2, dat_match)

    out_dir = clean_target(args.out, "units", resource_id)
    manifest = {
        "schema_version": UNIT_SCHEMA_VERSION,
        "kind": "aoe2de_unit",
        "id": resource_id,
        "unit": args.unit,
        "source_root": str(args.aoe2).replace("\\", "/"),
        "export_settings": manifest_settings(args),
        "requested_animations": requested,
        "discovered_animations": discovered,
        "missing_animations": missing,
        "dat": dat_metadata,
        "animations": {},
    }
    for action in missing:
        message = f"requested animation '{action}' is missing for unit '{args.unit}'"
        print(f"warning: {message}")
        manifest["animations"][action] = {
            "status": "missing_source",
            "source": None,
            "scale": None,
            "config": None,
            "layers": None,
            "warning_count": 1,
            "error": message,
        }
    run_exports(args, manifest, sources, out_dir)
    finish_manifest(manifest)
    write_json(out_dir / "manifest.json", manifest)
    return 0


def export_graphics(args) -> int:
    if not args.graphics:
        raise SystemExit("--graphics requires at least one .sld filename")
    if not args.name:
        raise SystemExit("--name is required with --graphics")
    root = graphics_dir(args.aoe2)
    if not root.is_dir():
        raise SystemExit(f"graphics directory does not exist: {root}")
    validate_resource_id(args.name)

    requested = [Path(filename).stem for filename in args.graphics]
    if len(set(requested)) != len(requested):
        raise SystemExit("--graphics contains duplicate output names")
    sources = {
        Path(filename).stem: root / filename
        for filename in args.graphics
        if (root / filename).is_file()
    }
    missing = [
        Path(filename).stem for filename in args.graphics
        if not (root / filename).is_file()
    ]
    out_dir = clean_target(args.out, "graphics", args.name)
    manifest = {
        "schema_version": GRAPHICS_SCHEMA_VERSION,
        "kind": "aoe2de_graphics",
        "id": args.name,
        "unit": None,
        "source_root": str(args.aoe2).replace("\\", "/"),
        "export_settings": manifest_settings(args),
        "requested_animations": requested,
        "discovered_animations": sorted(sources),
        "missing_animations": missing,
        "animations": {},
    }
    for name in missing:
        message = f"requested graphic '{name}' is missing"
        print(f"warning: {message}")
        manifest["animations"][name] = {
            "status": "missing_source",
            "source": None,
            "scale": None,
            "config": None,
            "layers": None,
            "warning_count": 1,
            "error": message,
        }
    run_exports(args, manifest, sources, out_dir)
    finish_manifest(manifest)
    write_json(out_dir / "manifest.json", manifest)
    return 0


def dump_sld_layers(args) -> int:
    if not args.graphics:
        raise SystemExit("--dump-layers requires --graphics with at least one .sld filename")
    if not args.name:
        raise SystemExit("--name is required with --dump-layers")
    root = graphics_dir(args.aoe2)
    sources = [root / filename for filename in args.graphics]
    missing = [path for path in sources if not path.is_file()]
    if missing:
        raise SystemExit(f"missing source graphic: {missing[0]}")
    out_dir = clean_target(args.out, "graphics", args.name)
    layers_out = out_dir / "layers"
    layers_out.mkdir()
    SLD, Texture = load_openage(args.openage)

    for source in sources:
        sld = SLD(source.read_bytes())
        for layer in range(5):
            try:
                texture = Texture(sld, layer=layer)
            except Exception as exc:  # noqa: BLE001
                print(f"warning: {source.name}: layer {layer} cannot be decoded: {exc}")
                continue
            frames = []
            for idx, frame in enumerate(texture.frames):
                record = {
                    "ordinal": getattr(frame, "source_ordinal", idx),
                    "frame_index": getattr(frame, "source_frame_index", idx),
                }
                frames.append(export_frame(frame, record))
            if not frames:
                print(f"{source.name}: layer {layer} empty")
                continue
            atlas, _metadata, _layout = pack_frames(frames, max(1, len(frames)))
            path = layers_out / f"{source.stem}_layer{layer}.png"
            atlas.save(path)
            print(f"{source.name}: layer {layer} -> {path}")
    return 0


def parse_args(argv: list[str] | None = None):
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--aoe2", type=Path,
        default=Path(r"F:\SteamLibrary\steamapps\common\AoE2DE")
    )
    parser.add_argument("--openage", type=Path, default=Path(r"E:\code\openage"))
    parser.add_argument(
        "--dat", type=Path,
        help="AoE2 gameplay DAT (defaults to resources/_common/dat under --aoe2)"
    )
    parser.add_argument("--civ-id", type=non_negative_int, default=0)
    parser.add_argument("--unit-id", type=non_negative_int)
    parser.add_argument("--unit-map", type=Path, default=DEFAULT_UNIT_MAP)
    parser.add_argument("--out", type=Path, help="cache root directory")
    parser.add_argument("--name", help="resource id under --out")
    parser.add_argument("--list", nargs="?", const="*", metavar="PATTERN")
    parser.add_argument("--page", type=positive_int, default=1)
    parser.add_argument("--unit", metavar="PREFIX")
    parser.add_argument("--animations", nargs="*")
    parser.add_argument("--graphics", nargs="*")
    parser.add_argument("--scale", choices=("x1", "x2", "auto"), default="auto")
    parser.add_argument(
        "--playercolor-temporal-filter", choices=("off",), default="off",
        help="reserved for future index-aware filtering; currently always off",
    )
    parser.add_argument("--directions", type=positive_int, default=DEFAULT_DIRECTIONS)
    parser.add_argument("--fps", type=positive_finite_float, default=DEFAULT_FPS)
    parser.add_argument("--dump-layers", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.list is not None:
        return list_units(args.aoe2, args.list, args.page)
    if not args.out:
        raise SystemExit("--out is required for export")
    validate_playercolor_args(args)
    if args.dump_layers:
        return dump_sld_layers(args)
    if args.unit:
        return export_unit(args)
    if args.graphics:
        return export_graphics(args)
    raise SystemExit("choose --list, --unit, or --graphics")


if __name__ == "__main__":
    raise SystemExit(main())
