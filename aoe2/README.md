# AoE2 runtime module

`gld_aoe2` loads schema-v2 unit caches produced by `tools/aoe2de_export` and
turns ECS unit render components into the engine's generic instanced batches.

Public entry points are under `aoe2/include/aoe2`:

- `Aoe2Plugin` registers the loader, resource index, animation and batching systems.
- `Aoe2ResourceManager` scans `<cache-root>/*/manifest.json`, lists unit IDs and
  returns asynchronous `Handle<Aoe2UnitAppearance>` values.
- `spawn_aoe2_unit` creates an entity with an `Aoe2SpawnRequest`; the spawn
  system resolves it in place when its appearance becomes available.
- `Aoe2UnitRender` owns active/pending playback state. Animation changes use
  `request_aoe2_animation`: the active frame freezes until every required
  target texture is ready, then texture/UV/foot/frame commit atomically.
  Its entity transform is the unit's
  world-space foot position; per-frame foot offsets remain authoritative in
  `Aoe2UnitAppearance`.

Player colour uses the exporter's `r8_subcolor_alpha_binary` mask. Main atlas,
nearest-filtered mask and the shared 8x8 palette LUT occupy batch texture slots
0, 1 and 2. Shadow and non-player-colour sprites remain single-texture batches.

The schema-v2 cache images remain unchanged on disk. During asynchronous load,
the runtime extracts only the channels consumed by the shaders: shadow source R
is uploaded as `GL_R8`, while player-colour source R+A is packed into `GL_RG8`.
The RG texture swizzles G back to sampled alpha, so shaders continue to read the
mask as `.r` subcolour plus `.a` binary coverage. Main atlases remain RGBA8.

Appearance loading parses all animation metadata but texture residency is lazy.
Only the initial/current animation is requested; animations already visited stay
resident for instant switching back.

Animation hot paths use a stable `AnimationSlot` enum. The default ABI reserves
1..3 for `WalkA/B/C`, 4..6 for `IdleA/B/C`, 7..9 for `AttackA/B/C`, 10 for
`DeathA`, and 11 for `DecayA`. The name mapping is declared by the compile-time
`DefaultAoe2AnimationAbi` template table. Other exported animation names remain
supported: each appearance sorts them by name and assigns local extension slots
starting at `0x100`. Use `extension_animation_count()` and
`extension_animation_names()` to inspect that local extension set; an extension
slot must always be interpreted together with its appearance.

AoE2 batching keeps persistent main/shadow membership. Each unit stores compact
`group_id + instance_index` reverse slots; a stable group owns a parallel member
list and generic `BatchComponent::instances` vector. Texture/shader/layer changes
move only the affected source, while entity/component destruction uses
swap-remove and repairs the moved member's reverse slot. Steady animation frame
changes therefore do not rebuild batch keys or group member vectors.

Instance invalidation is split into frame, transform and material state. Frame
changes update UV plus the frame-derived quad transform, transform-only changes
update only the matrix, and player colour/debug/tint changes update only
material fields. The generic batch uploader accepts optional dirty instance
ranges, merges adjacent ranges and uses partial `glBufferSubData` calls for
sparse edits. Dense (at least 50%) or fragmented (more than eight ranges) edits
fall back to one full upload. Existing generic collectors continue to use
`dirty=true`; they allocate no per-instance dirty metadata and retain the
original full-upload path.

The `aoe2_unit_preview` example displays sixteen direction slots. Controls:

- Left/Right: unit
- Up/Down: animation
- Q/E: player colour
- Space: pause
- R: restart animation
- M: normal/mask/subcolour debug views
- F5: rescan the cache root
