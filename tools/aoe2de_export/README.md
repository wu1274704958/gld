# AoE2DE graphics and unit metadata exporter

Exports selected Age of Empires II: Definitive Edition `.sld` graphics into a
local, versioned `gld` cache. Unit exports also read the gameplay DAT and emit
collision/combat metadata. Generated game assets must not be committed.

The SLD decoder under `sld/sld.pyx` is copied/adapted from openage. Keep the
openage copyright/license notice in copied files.

## Build the local SLD extension

```powershell
python -m pip install cython numpy pillow genieutils-py
cmake -S tools\aoe2de_export -B tools\aoe2de_export\build
cmake --build tools\aoe2de_export\build --config Release --target aoe2de_export_sld
```

`Release` is recommended for normal use. On Windows, the CMake target also
keeps the regular CPython `/MD` runtime when built as `Debug`, because standard
Python installations do not provide the debug-only `pythonXY_d.lib`.

Verify the local module:

```powershell
python -c "import sys; sys.path.insert(0, r'E:\code\gld\tools\aoe2de_export'); from sld.sld import SLD; print('local SLD import ok')"
```

## Output root and resource IDs

`--out` is a cache root, not the final resource directory. Unit export uses the
full unit prefix as its resource ID unless `--name` overrides it:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --unit u_arc_archer `
  --animations idleA walkA attackA deathA
```

The command above writes:

```text
res/aoe2de_cache/units/u_arc_archer/
  manifest.json
  graphics/
    idleA.json
    idleA.png
    idleA_shadow.png
    idleA_playercolor.png
    ...
```

Unit exports write `ROOT/units/<resource-id>`; standalone `--graphics` and
`--dump-layers` exports write `ROOT/graphics/<resource-id>`. An existing
resource directory inside its category is deleted only after arguments,
source graphics, DAT parsing and unit mapping have been validated, then rebuilt
from scratch.
Do not point `--out` at a directory whose children are not disposable caches.

Explicit `--graphics` and `--dump-layers` exports require `--name`:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --name spearman_custom `
  --graphics u_inf_spearman_idleA_x2.sld u_inf_spearman_walkA_x2.sld
```

The gameplay arrow presentation is exported as a standalone graphic:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --name p_arrow --graphics p_arrow_x2.sld --directions 32 --fps 30
```

This produces `graphics/p_arrow`. The SLD contains 32 horizontal directions
with 11 pitch poses per direction and an embedded shadow layer.

## Discover units

`--list` aggregates graphics by unit prefix, prints 50 entries per page and
accepts an optional fnmatch pattern:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --list "u_inf_*" --page 1
```

## Directions, FPS and missing animations

- `--directions` accepts any positive integer and defaults to `16`.
- `--fps` accepts any positive finite number and defaults to `30`.
- `--scale auto` prefers `_x2.sld` and falls back to `_x1.sld` per animation.
- Requested animations that do not exist are recorded as `missing_source`; the
  remaining animations are still exported and the command returns success.

Frames are expected in direction-major order. If the source frame count is not
divisible by `--directions`, trailing frames are excluded before atlas packing.
The command prints a warning, and the animation config records their original
SLD frame indexes in `unused_source_frames`.

## DAT selection and schema v3

`--unit` exports require `genieutils-py`. `--dat` defaults to
`resources/_common/dat/empires2_x2_p1.dat` below `--aoe2`; `--civ-id` defaults
to Gaia (`0`). Unit selection is deterministic, in this order:

1. `--unit-id` explicitly selects the unit in `--civ-id`.
2. `--unit-map` is checked (default: `unit_dat_map.json` next to the exporter).
3. The selected civilization is scanned for exactly one unit whose standing,
   walking, attack or death Graphic filename has the requested SLD prefix.

Zero or multiple matches stop before output and list all candidates. Add an
explicit ID or a stable mapping entry when a Graphic is shared. Explicit/map
selections that do not reference the prefix are permitted with a warning:

```json
{
  "u_arc_archer": {"civ_id": 0, "unit_id": 4},
  "u_cam_camel_scout": {"civ_id": 0, "unit_id": 448}
}
```

Example with an explicit mapping override:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "F:\SteamLibrary\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --unit u_arc_archer --civ-id 0 --unit-id 4 `
  --animations attackA idleA walkA deathA
```

Unit manifests use `schema_version: 3`. Animation configs and independent
`--graphics` manifests remain schema 2 because their graphical representation
did not change. The new `dat` object records source/civ/unit/type and
`mapping_source`, raw DAT `collision_size`, optional `outline_size`, and
optional combat data:

```json
"dat": {
  "source": "resources/_common/dat/empires2_x2_p1.dat",
  "civ_id": 0,
  "unit_id": 4,
  "unit_type": 70,
  "mapping_source": "map",
  "collision_size": {"x": 0.2, "y": 0.2, "z": 2.0},
  "outline_size": {"x": 0.2, "y": 0.2, "z": 2.0},
  "combat": {
    "projectile_unit_id": 363,
    "secondary_projectile_unit_id": 1930,
    "frame_delay": 15,
    "weapon_offset": {"x": 0.0, "y": 0.5, "z": 1.5},
    "accuracy_percent": 80,
    "accuracy_dispersion": 0.33,
    "min_range": 0.0,
    "max_range": 4.0,
    "reload_time": 2.0,
    "blast_width": 0.0,
    "blast_attack_level": 3,
    "attack_graphic_id": 627,
    "projectile_min_count": 1.0,
    "projectile_max_count": 1,
    "projectile_spawning_area": {"width": 2.0, "length": 2.0, "randomness": 99.0}
  }
}
```

Collision values come only from DAT `collision_size_x/y/z`; X/Y are gameplay
ground-plane radii and Z is height. The optional `outline_size` comes directly
from DAT `outline_size_x/y/z` and describes selection/outline extents: it does
not replace gameplay collision and is not a per-pixel Sprite bounding box.
Existing schema-3 caches without this additive field remain valid. Weapon offset is
Combat `graphic_displacement`. Combat/type-50 supplies projectile, timing,
accuracy/range/reload/blast and attack Graphic fields. Creatable supplies the
secondary projectile, counts and spawn area. Optional source sections are
omitted, and legal DAT `-1` IDs are preserved.

`--graphics` is deliberately graphics-only: it neither imports
`genieutils-py` nor opens a DAT and never invents gameplay metadata.

## Graphics schema v2

`manifest.json` has `schema_version: 2` and records requested, discovered and
missing animations. Each animation record has one of these statuses:

- `exported`
- `missing_source`
- `invalid`

Each exported animation config records all five SLD layers. Layer statuses are:

- `complete`: all usable physical frames were exported
- `partial`: missing auxiliary frames were replaced with transparent frames
- `missing`: the source has no such layer
- `unsupported`: the source declares the layer but this exporter does not emit it
- `invalid`: the layer could not be decoded or aligned safely

Main, shadow and player-color layers are aligned by physical SLD frame ordinal.
Shadow has an independent atlas layout. Player-color always uses the exact main
atlas layout and UVs; an incompatible mask is rejected instead of being sampled
incorrectly.

Every exported frame includes a semantic foot point:

```json
"foot": {
  "x": 123,
  "y": 178,
  "space": "frame_pixels_top_left"
}
```

The point is the SLD layer hotspot relative to the cropped frame's top-left.
At runtime, place that point at the unit's map/world position.

Player-color output is an RGBA mask whose red channel is `0..7`, green/blue are
zero, and alpha is binary. The default `raw` rule uses the decoded SLD layer 4;
`diffuse-neutral` and `hybrid` are opt-in diagnostics/fallbacks.
