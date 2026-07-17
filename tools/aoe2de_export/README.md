# AoE2DE graphics exporter

Exports selected Age of Empires II: Definitive Edition `.sld` graphics into a
local `gld` cache. Generated game assets must not be committed.

The SLD decoder under `sld/sld.pyx` is copied/adapted from openage:

```text
E:\code\openage\openage\convert\value_object\read\media\sld.pyx
```

Keep the openage copyright/license notice in copied files.

## Build local SLD extension

Install minimal Python dependencies:

```powershell
python -m pip install cython numpy pillow
```

Build the local Cython extension:

```powershell
cmake -S tools\aoe2de_export -B tools\aoe2de_export\build
cmake --build tools\aoe2de_export\build --target aoe2de_export_sld
```

Verify import:

```powershell
python -c "import sys; sys.path.insert(0, r'E:\code\gld\tools\aoe2de_export'); from sld.sld import SLD; print('local SLD import ok')"
```

## List matching graphics

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --list "*spearman*"
```

## Export Spearman preset

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --openage "E:\code\openage" `
  --out "E:\code\gld\res\aoe2de_cache\spearman" `
  --unit spearman `
  --scale auto
```

`--scale auto` prefers HD `_x2.sld` graphics when they are installed and falls
back to `_x1.sld`. Use `--scale x1` or `--scale x2` to force a specific asset
scale.

The exporter uses openage's SLD conversion modules if they are available. If
they are not built/importable, build openage's converter first or provide a
preconverted cache with the documented JSON schema.

## Export explicit graphics

```powershell
python tools\aoe2de_export\aoe2de_export.py `
  --aoe2 "D:\program1\steam\steamapps\common\AoE2DE" `
  --openage "E:\code\openage" `
  --out "E:\code\gld\res\aoe2de_cache\custom" `
  --graphics u_inf_spearman_idleA_x1.sld u_inf_spearman_walkA_x1.sld
```

## Build and run demo

```powershell
cmake --build E:\code\gld\build\copilot-audio-msvc --target aoe2de_spearman -j 6
E:\code\gld\build\copilot-audio-msvc\aoe2de_spearman.exe
```

The demo expects:

```text
E:\code\gld\res\aoe2de_cache\spearman\manifest.json
```
