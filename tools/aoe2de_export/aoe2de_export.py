#!/usr/bin/env python3
"""Generic AoE2DE graphics exporter for gld demos.

This tool writes a local cache from AoE2DE .sld graphics. It intentionally does
not ship or download game assets.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import math
import sys
from pathlib import Path


UNIT_PRESETS = {
    "spearman": {
        "kind": "aoe2de_unit",
        "unit": "spearman",
        "prefix": "u_inf_spearman",
        "animations": {
            "idleA": "idleA",
            "walkA": "walkA",
            "attackA": "attackA",
            "deathA": "deathA",
        },
    },
}


def graphics_dir(aoe2: Path) -> Path:
    return aoe2 / "resources" / "_common" / "drs" / "graphics"


def list_graphics(aoe2: Path, pattern: str) -> int:
    root = graphics_dir(aoe2)
    if not root.is_dir():
        raise SystemExit(f"graphics directory does not exist: {root}")

    for path in sorted(root.glob("*.sld")):
        if fnmatch.fnmatch(path.name, pattern):
            print(path.name)
    return 0


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
    except Exception as exc:  # noqa: BLE001 - surface full converter import problem
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


def pack_frames(frames):
    from PIL import Image

    count = len(frames)
    if count == 0:
        raise ValueError("no frames in SLD")

    widths = [frame.width for frame in frames]
    heights = [frame.height for frame in frames]
    cell_w = max(widths)
    cell_h = max(heights)
    cols = max(1, math.ceil(math.sqrt(count)))
    rows = math.ceil(count / cols)
    atlas = Image.new("RGBA", (cols * cell_w, rows * cell_h), (0, 0, 0, 0))

    metadata = []
    for idx, frame in enumerate(frames):
        img = frame.get_pil_image()
        col = idx % cols
        row = idx // cols
        x = col * cell_w
        y = row * cell_h
        atlas.alpha_composite(img, (x, y))
        hx, hy = frame.hotspot
        metadata.append({
            "x": x,
            "y": y,
            "w": frame.width,
            "h": frame.height,
            "hotspot_x": int(hx),
            "hotspot_y": int(hy),
        })

    return atlas, metadata


def export_sld(SLD, Texture, source: Path, out_dir: Path, name: str, directions: int, fps: float):
    data = source.read_bytes()
    sld = SLD(data)
    texture = Texture(sld)
    frames = texture.frames
    atlas, frame_meta = pack_frames(frames)

    graphics_out = out_dir / "graphics"
    graphics_out.mkdir(parents=True, exist_ok=True)
    image_name = f"{name}.png"
    json_name = f"{name}.json"
    atlas.save(graphics_out / image_name)

    frame_count = len(frame_meta)
    if directions <= 0:
        raise ValueError("directions must be greater than zero")

    frames_per_direction = frame_count // directions
    usable_frame_count = frames_per_direction * directions
    unused_frame_count = frame_count - usable_frame_count
    if unused_frame_count:
        print(
            f"warning: {source.name} has {frame_count} frames; "
            f"using {usable_frame_count} ({directions} directions x {frames_per_direction}), "
            f"ignoring {unused_frame_count} trailing frame(s)"
        )

    frame_meta = frame_meta[:usable_frame_count]
    for idx, item in enumerate(frame_meta):
        item["direction"] = idx // max(frames_per_direction, 1)
        item["frame"] = idx % max(frames_per_direction, 1)

    meta = {
        "image": image_name,
        "source": source.name,
        "atlas_w": atlas.width,
        "atlas_h": atlas.height,
        "direction_count": directions,
        "frames_per_direction": frames_per_direction,
        "frame_order": "direction_major",
        "unused_frame_count": unused_frame_count,
        "fps": fps,
        "start_angle": 270,
        "frames": frame_meta,
    }
    (graphics_out / json_name).write_text(json.dumps(meta, indent=2), encoding="utf-8")
    return f"graphics/{json_name}"


def resolve_scaled_graphic(root: Path, prefix: str, action: str, scale: str) -> Path:
    def candidate(scale_name: str) -> Path:
        return root / f"{prefix}_{action}_{scale_name}.sld"

    if scale == "auto":
        for scale_name in ("x2", "x1"):
            path = candidate(scale_name)
            if path.is_file():
                return path
        raise SystemExit(f"missing graphic for {prefix}_{action} with scale auto (tried x2, x1)")

    path = candidate(scale)
    if not path.is_file():
        raise SystemExit(f"missing source graphic: {path}")
    return path


def export_unit(args) -> int:
    preset = UNIT_PRESETS.get(args.unit)
    if not preset:
        raise SystemExit(f"unknown unit preset: {args.unit}")

    SLD, Texture = load_openage(args.openage)
    root = graphics_dir(args.aoe2)
    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "kind": preset["kind"],
        "unit": preset["unit"],
        "source_root": str(args.aoe2).replace("\\", "/"),
        "scale": args.scale,
        "animations": {},
    }

    for anim, action in preset["animations"].items():
        source = resolve_scaled_graphic(root, preset["prefix"], action, args.scale)
        manifest["animations"][anim] = export_sld(SLD, Texture, source, out_dir, anim, args.directions, args.fps)
        print(f"exported {source.name} -> {manifest['animations'][anim]}")

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return 0


def export_graphics(args) -> int:
    if not args.graphics:
        raise SystemExit("--graphics requires at least one .sld filename")

    SLD, Texture = load_openage(args.openage)
    root = graphics_dir(args.aoe2)
    out_dir = args.out
    out_dir.mkdir(parents=True, exist_ok=True)

    manifest = {
        "kind": "aoe2de_graphics",
        "source_root": str(args.aoe2).replace("\\", "/"),
        "animations": {},
    }

    for filename in args.graphics:
        source = root / filename
        if not source.is_file():
            raise SystemExit(f"missing source graphic: {source}")
        name = Path(filename).stem
        manifest["animations"][name] = export_sld(SLD, Texture, source, out_dir, name, args.directions, args.fps)
        print(f"exported {filename} -> {manifest['animations'][name]}")

    (out_dir / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    return 0


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--aoe2", type=Path, default=Path(r"D:\program1\steam\steamapps\common\AoE2DE"))
    parser.add_argument("--openage", type=Path, default=Path(r"E:\code\openage"))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--list", metavar="PATTERN")
    parser.add_argument("--unit", choices=sorted(UNIT_PRESETS.keys()))
    parser.add_argument("--graphics", nargs="*")
    parser.add_argument("--scale", choices=("x1", "x2", "auto"), default="auto",
                        help="asset scale for unit presets; auto prefers x2 and falls back to x1")
    parser.add_argument("--directions", type=int, default=8)
    parser.add_argument("--fps", type=float, default=12.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.list:
        return list_graphics(args.aoe2, args.list)

    if not args.out:
        raise SystemExit("--out is required for export")

    if args.unit:
        return export_unit(args)

    if args.graphics:
        return export_graphics(args)

    raise SystemExit("choose --list, --unit, or --graphics")


if __name__ == "__main__":
    raise SystemExit(main())
