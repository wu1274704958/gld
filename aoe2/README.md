# AoE2 runtime module

`gld_aoe2` loads schema-v2 unit caches produced by `tools/aoe2de_export` and
turns ECS unit render components into compact, AoE2-specific instanced batches.

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
list and an `Aoe2BatchComponent`. Texture/shader/layer changes move only the
affected source, while entity/component destruction uses swap-remove and repairs
the moved member's reverse slot. Cameras opt into the independent AoE2 render
pass through `RegisteredRenderPasses`; the generic `BatchComponent` ABI and its
single-texture use cases have no additional per-instance cost.

The instance ABI is split into an exact 48-byte world stream and a 40-byte
CPU-expanded visual stream. The visual stream stores `vec4 uv`, `vec4
size_and_foot`, and two packed 32-bit material words. Frame and material changes
touch only the visual stream; Transform changes touch only the world stream.
There is no GPU frame-table texture buffer or vertex-shader `texelFetch`: the
CPU-authoritative animation system resolves frame metadata before batching, and
shadow/non-player-colour draws retain their single-texture hot path. Sparse dirty
ranges use partial `glBufferSubData`; dense (at least 75%) or fragmented (more
than eight ranges) edits use one full stream upload. With 30,000 units producing
60,000 main+shadow instances, a fully changing frame uploads about 2.29 MiB of
visual data; updating all 30,000 transforms adds about 1.37 MiB of world data,
for roughly 3.66 MiB total rather than the former roughly 10.1 MiB.

The dirty queue uses entity-indexed masks for constant-time deduplication. Sparse
gameplay edits visit only queued entities; when at least half of drawable sources
are dirty, batching switches to one contiguous ECS-view scan. Both modes preserve
the same CPU-owned animation, transition, freeze and future event-track state.

Detailed performance monitoring is compile-time opt-in and is disabled by
default. Configure with `-DGLD_ENABLE_PERFORMANCE_MONITORING=ON` to compile CPU
timers, per-entity/batch counters, asset-load metrics and the asynchronous AoE2
GPU timer-query ring. FPS remains enabled in both modes. With monitoring disabled,
`aoe2_unit_benchmark` displays and prints only its live `FPS: ...` value, so the
instrumentation itself does not distort the stress result.

CPU animation remains authoritative. Playback time, logical frame, direction,
loop/restart/transition state and the resolved foot position stay on
`Aoe2UnitRender`, leaving a direct extension point for gameplay event tracks.
Typed gameplay setters enqueue precise render dirtiness. Global Transform uses
`patch_transform` plus revisioned, parent-before-child propagation; the AoE2
batcher consumes only `TransformChanges` and queued animation/gameplay changes,
with a low-frequency Debug audit guarding unsupported direct component writes.

The `aoe2_unit_preview` example displays sixteen direction slots. Controls:

- Left/Right: unit
- Up/Down: animation
- Q/E: player colour
- Space: pause
- R: restart animation
- M: normal/mask/subcolour debug views
- F5: rescan the cache root

`aoe2_unit_benchmark` remains interactive by default. Pass
`--duration <seconds>` to print the last HUD sample and close normally, which is
useful for repeatable performance smoke tests and CI runs.
