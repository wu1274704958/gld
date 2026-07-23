# AoE2DE graphics exporter

Exports selected Age of Empires II: Definitive Edition `.sld` graphics into a
local, versioned `gld` cache. Generated game assets must not be committed.

The SLD decoder under `sld/sld.pyx` is copied/adapted from openage. Keep the
openage copyright/license notice in copied files.

## Build the local SLD extension

```powershell
python -m pip install cython numpy pillow
cmake -S tools\aoe2de_export -B tools\aoe2de_export\build
cmake --build tools\aoe2de_export\build --target aoe2de_export_sld
```

Verify the local module:

```powershell
python -c "import sys; sys.path.insert(0, r'E:\code\gld\tools\aoe2de_export'); from sld.sld import SLD; print('local SLD import ok')"
```

## Output root and resource IDs

`--out` is a cache root, not the final resource directory. Unit export uses the
full unit prefix as its resource ID unless `--name` overrides it:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --unit u_inf_spearman `
  --animations idleA walkA attackA deathA
```

The command above writes:

```text
res/aoe2de_cache/u_inf_spearman/
  manifest.json
  graphics/
    idleA.json
    idleA.png
    idleA_shadow.png
    idleA_playercolor.png
    ...
```

An existing `ROOT/<resource-id>` directory is deleted only after arguments and
the source graphics directory have been validated, then rebuilt from scratch.
Do not point `--out` at a directory whose children are not disposable caches.

Explicit `--graphics` and `--dump-layers` exports require `--name`:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --out "E:\code\gld\res\aoe2de_cache" `
  --name spearman_custom `
  --graphics u_inf_spearman_idleA_x2.sld u_inf_spearman_walkA_x2.sld
```

## Discover units

`--list` aggregates graphics by unit prefix, prints 50 entries per page and
accepts an optional fnmatch pattern:

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
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

## Schema v2

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

## Demo compatibility

The current `aoe2de_spearman` C++ example still reads the old schema and the old
`aoe2de_cache/spearman` path. It must be migrated separately before it can read
`schema_version: 2` resources under `aoe2de_cache/u_inf_spearman`.
