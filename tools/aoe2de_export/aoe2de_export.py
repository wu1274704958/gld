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


SCHEMA_VERSION = 2
GRAPHIC_NAME_RE = re.compile(
    r"^(?P<prefix>.+)_(?P<action>[A-Za-z0-9]+)_(?P<scale>x[12])\.sld$"
)
RESOURCE_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
DEFAULT_DIRECTIONS = 16
DEFAULT_FPS = 30.0
LIST_PAGE_SIZE = 50

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


def positive_finite_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("must be a number") from exc
    if not math.isfinite(parsed) or parsed <= 0:
        raise argparse.ArgumentTypeError("must be finite and greater than zero")
    return parsed


def validate_playercolor_args(args) -> None:
    if not 0 <= args.playercolor_alpha_min <= 255:
        raise SystemExit("--playercolor-alpha-min must be in range 0..255")
    if not math.isfinite(args.playercolor_luma_min) or args.playercolor_luma_min < 0:
        raise SystemExit("--playercolor-luma-min must be finite and non-negative")
    if not math.isfinite(args.playercolor_sat_max) or args.playercolor_sat_max < 0:
        raise SystemExit("--playercolor-sat-max must be finite and non-negative")


def validate_resource_id(resource_id: str) -> str:
    if resource_id in {".", ".."} or not RESOURCE_ID_RE.fullmatch(resource_id):
        raise SystemExit(
            "resource id must be one path component containing only letters, "
            "digits, '.', '_' or '-'"
        )
    return resource_id


def target_directory(root: Path, resource_id: str) -> Path:
    resource_id = validate_resource_id(resource_id)
    resolved_root = root.resolve()
    target = (resolved_root / resource_id).resolve()
    if target.parent != resolved_root:
        raise SystemExit(f"unsafe output target outside root: {target}")
    return target


def clean_target(root: Path, resource_id: str) -> Path:
    target = target_directory(root, resource_id)
    root.mkdir(parents=True, exist_ok=True)
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
                layout: AtlasLayout | None = None):
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


def is_diffuse_neutral_teamcolor(pixel: tuple[int, int, int, int], luma_min: float,
                                 sat_max: float, alpha_min: int) -> bool:
    r, g, b, a = pixel
    if a <= alpha_min:
        return False
    luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
    saturation = max(r, g, b) - min(r, g, b)
    return luma > luma_min and saturation < sat_max


def bake_playercolor_index_mask(mask, base, rule: str = "raw",
                                luma_min: float = 120.0, sat_max: float = 8.0,
                                alpha_min: int = 8):
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
            diffuse_px = base_px[x, y]
            if diffuse_px[3] <= alpha_min:
                continue
            raw_strength = max(mask_px[x, y][:3])
            neutral_hit = is_diffuse_neutral_teamcolor(
                diffuse_px, luma_min, sat_max, alpha_min
            )
            raw_hit = raw_strength > 8
            if rule == "raw":
                hit = raw_hit
            elif rule == "diffuse-neutral":
                hit = neutral_hit
            elif rule == "hybrid":
                hit = neutral_hit or raw_hit
            else:
                raise ExportError(f"unknown player-color rule: {rule}")
            if not hit:
                continue
            if neutral_hit and not raw_hit:
                strength = max(0, min(255, int(
                    0.2126 * diffuse_px[0] + 0.7152 * diffuse_px[1] +
                    0.0722 * diffuse_px[2]
                )))
            else:
                strength = raw_strength
            sub = max(0, min(7, int(round((strength / 255.0) * 7.0))))
            out_px[x, y] = (sub, 0, 0, 255)
    return out


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
                     directions: int, fps: float, playercolor_rule: str,
                     luma_min: float, sat_max: float, alpha_min: int):
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
                        f"source frame {record['frame_index']} size/foot differs "
                        "from main; same-UV sampling would be unsafe"
                    )
                player_frames.append(player_frame)

            raw_player_atlas, player_meta, _ = pack_frames(
                player_frames, frames_per_direction, layout=main_layout
            )
            player_atlas = bake_playercolor_index_mask(
                raw_player_atlas, main_atlas, playercolor_rule,
                luma_min, sat_max, alpha_min
            )
            if player_atlas.size != main_atlas.size:
                raise ExportError("baked player-color atlas does not match main atlas")
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
            layers["player_color"] = image_layer_record(
                player_status, presence_counts["player_color"], player_image,
                player_atlas, player_meta, missing_player
            )
        except Exception as exc:  # noqa: BLE001
            message = f"{source.name} player-color layer is invalid: {exc}"
            warnings.append(warning("invalid_player_color", message))
            layers["player_color"] = empty_layer_record(
                "invalid", presence_counts["player_color"], warning=message
            )

    parsed = parse_graphic_name(source.name)
    actual_scale = parsed[2] if parsed else None
    config = {
        "schema_version": SCHEMA_VERSION,
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
            "rule": args.playercolor_rule,
            "luma_min": args.playercolor_luma_min,
            "saturation_max": args.playercolor_sat_max,
            "alpha_min": args.playercolor_alpha_min,
            "format": "r8_subcolor_alpha_binary",
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
                args.directions, args.fps, args.playercolor_rule,
                args.playercolor_luma_min, args.playercolor_sat_max,
                args.playercolor_alpha_min,
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

    out_dir = clean_target(args.out, resource_id)
    manifest = {
        "schema_version": SCHEMA_VERSION,
        "kind": "aoe2de_unit",
        "id": resource_id,
        "unit": args.unit,
        "source_root": str(args.aoe2).replace("\\", "/"),
        "export_settings": manifest_settings(args),
        "requested_animations": requested,
        "discovered_animations": discovered,
        "missing_animations": missing,
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
    out_dir = clean_target(args.out, args.name)
    manifest = {
        "schema_version": SCHEMA_VERSION,
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
    out_dir = clean_target(args.out, args.name)
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
        default=Path(r"D:\program1\steam\steamapps\common\AoE2DE")
    )
    parser.add_argument("--openage", type=Path, default=Path(r"E:\code\openage"))
    parser.add_argument("--out", type=Path, help="cache root directory")
    parser.add_argument("--name", help="resource id under --out")
    parser.add_argument("--list", nargs="?", const="*", metavar="PATTERN")
    parser.add_argument("--page", type=positive_int, default=1)
    parser.add_argument("--unit", metavar="PREFIX")
    parser.add_argument("--animations", nargs="*")
    parser.add_argument("--graphics", nargs="*")
    parser.add_argument("--scale", choices=("x1", "x2", "auto"), default="auto")
    parser.add_argument(
        "--playercolor-rule", choices=("raw", "diffuse-neutral", "hybrid"),
        default="raw"
    )
    parser.add_argument("--playercolor-luma-min", type=float, default=120.0)
    parser.add_argument("--playercolor-sat-max", type=float, default=8.0)
    parser.add_argument("--playercolor-alpha-min", type=int, default=8)
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
