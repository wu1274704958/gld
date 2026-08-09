# AoE gameplay units

`gld_aoe_gameplay` is a renderer-independent ECS module for deterministic
AoE-style unit state. A gameplay entity owns a logical `AoePosition`, health,
level, typed armor and attacks, collider, movement state, facing, and integer-tick
action state. It does not include or depend on the AoE2 presentation module. A
renderer or game scene decides how logical positions map to world/screen
transforms.

## Definition format

Definitions are JSON files below `res/aoe_units`:

```json
{
  "schema_version": 2,
  "kind": "aoe_gameplay_unit",
  "id": "archer",
  "tags": ["archer", "ranged"],
  "level": 1,
  "max_hp": 30.0,
  "armor": [{"class_id": 3, "amount": 0.0}],
  "collision": {"radius_x": 0.2, "radius_y": 0.2, "height": 2.0},
  "movement": {"speed": 0.96},
  "target_acquisition": {
    "strategy_id": "nearest_enemy",
    "radius": 6.0
  },
  "lifecycle": {
    "death_duration_seconds": 1.0,
    "disappear_duration_seconds": 0.0
  },
  "attack": {
    "mode": "projectile",
    "damage": [{"class_id": 3, "amount": 4.0}],
    "range": 4.0,
    "release_seconds": 0.5,
    "animation_duration_seconds": 1.0,
    "cooldown_seconds": 2.0,
    "critical_chance": 0.1,
    "critical_multiplier": 2.0,
    "projectile_id": "arrow",
    "projectile_launch_offset": {"x": 0.0, "y": 0.5, "z": 1.5}
  },
  "presentation": {
    "backend": "aoe2",
    "resource_id": "u_arc_archer",
    "default_player_color": 1,
    "animations": {
      "idle": "idleA",
      "moving": "idleA",
      "attack": "attackA",
      "death": "idleA"
    }
  }
}
```

Armor and damage use explicit class IDs. Damage is resolved per class as
`max(0, attack - armor)`, then critical multipliers are applied. A positive
authored attack deals at least one damage. Melee damage resolves on the release
tick. Projectile attacks use `AoeProjectileRegistry`: release invokes the
statically bound logic type, while damage resolves only when its gameplay
projectile collides. The built-in `arrow` continuously seeks the target's live
incarnation, follows a deterministic arc, and expires on stale/dead targets or
after five seconds. Projectile logic remains renderer-independent.

`projectile_launch_offset` stores the DAT sprite-space displacement. Its X/Y
components rotate with the attacker's locked facing slot using
`pi - 2*pi*direction/direction_count`; positive Z is elevation. This keeps the
gameplay launch point attached to the same discrete attack pose being rendered.

Schema 1 definitions remain loadable and keep their old permanent terminal-death
behavior. Schema 2 adds movement and the timed `death -> disappear -> recycle`
lifecycle. A positive disappear duration requires a `disappear` animation mapping.

Timing must satisfy `release_seconds <= animation_duration_seconds <=
cooldown_seconds`. The loader rejects non-finite values, invalid crit ranges,
duplicate/negative class IDs, invalid colliders, invalid movement/lifecycle
values, and projectile attacks without a projectile ID.

`target_acquisition` is optional. Its defaults are `nearest_enemy`, a
surface-to-surface discovery radius of 6 gameplay units, and a disengage radius
of 9. Strategies use the closed `AoeTargetAcquisitionType` enum and
`AoeTargetAcquisitionBinding<Type>` template mapping, so dispatch has no runtime
string registry. The built-in strategy keeps a valid locked target and otherwise
selects the closest live unit with a different instance-level `AoeTeam`; player
color remains presentation-only.

## Fixed-clock authority

`AoeGameplayPlugin` runs an accumulator-driven fixed clock (30 Hz and at most 8
catch-up ticks by default). Actions store integer ticks and emit
`AttackStarted`, `AttackReleased`, `AttackFinished`, `DamageApplied`,
`DeathStarted`, `DisappearStarted`, `RecycleRequested`, and projectile
spawn/hit/miss/failure events. Crit rolls use
deterministic per-instance state. Render readiness, render animation callbacks,
and render frame rate never affect these events.

Commands are queued through `request_aoe_attack(attacker, target)`,
`request_aoe_attack_move(unit, destination)`,
`request_aoe_move`, `request_aoe_stop`, `set_aoe_unit_facing`,
`set_aoe_unit_health`, and `set_aoe_unit_level`. Attack orders persist:
navigation moves the attacker into surface-to-surface range, then combat repeats
attack cycles until the target dies or another command replaces the order. Target
references include an instance ID so a stale order cannot attack a recycled
entity's new incarnation.

Attack Move discovers and pursues enemies inside the wider acquisition radius,
while the collider surface-to-surface attack range decides when combat can
start. A valid locked target is normally retained instead of being replaced by
each newly discovered enemy. If movement toward that target has stalled for at
least one fixed tick while it remains outside weapon range, however, an
alternative enemy already inside the unit's real weapon range may preempt it
immediately. This in-range check uses the unit's own acquisition strategy rather
than a Squad's unfiltered shared candidate list. If no such alternative exists,
the current target, path, and normal blocked-repath behavior are unchanged. The
retained Attack Move destination resumes after combat; an opportunistic target's
death performs ordinary acquisition and does not restore a suspended target.
Explicit AttackTarget remains the command for pursuing only a selected enemy.
AttackTarget, MoveTo, Stop, or a new Attack Move replaces the previous command.
`AoeUnitSpawnOptions::team_id` assigns gameplay
affiliation and is reset when a pooled entity is reused.

The fixed pipeline synchronizes map obstacles first, then processes commands,
Squad traffic/control, Attack Move acquisition, navigation, movement intent,
local avoidance, global motion planning and safety, movement, combat,
projectile, and lifecycle.
Navigation writes `AoeNavigationPath`; movement only consumes waypoints. This is
the extension boundary for pathfinding, obstacle avoidance, and steering.
Range uses the support radii of both elliptical colliders rather than center
distance alone.

## Logical maps and navigation

`AoeLogicMap` is a finite, renderer-independent rectangular grid. Gameplay X/Y
positions remain `glm::vec2`; terrain elevation is queried separately with
`sample_height(position)`. Height values live on the `(width + 1) * (height +
1)` grid vertices and sampling is bilinear. Points on or beyond the exclusive
maximum map edge, and all other out-of-bounds points, return `std::nullopt`.
This version stores height for later combat rules but does not apply slope or
high-ground bonuses.

A map can be installed programmatically:

```cpp
AoeMapDefinition definition;
definition.id = "arena";
definition.origin = {-16.f, -16.f};
definition.tile_size = 1.f;
definition.width = 32;
definition.height = 32;
definition.heights.assign(33 * 33, 0.f);

AoeStaticObstacleDesc keep;
keep.source_id = "keep_01";
keep.shape = AoeStaticObstacleShape::Aabb;
keep.center = {0.f, 0.f};
keep.half_extents = {2.f, 2.f};
definition.static_obstacles.push_back(keep);

world.add_resource<AoeLogicMap>(definition);
```

The same data can be loaded as a schema-1 asset (see
`res/aoe_maps/example.json`):

```json
{
  "schema_version": 1,
  "kind": "aoe_logic_map",
  "id": "example",
  "origin": {"x": 0.0, "y": 0.0},
  "tile_size": 1.0,
  "width": 4,
  "height": 4,
  "heights": [
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    0, 0, 1, 1, 0,
    0, 0, 1, 1, 0,
    0, 0, 0, 0, 0
  ],
  "static_obstacles": [
    {
      "id": "building_01",
      "shape": "aabb",
      "center": {"x": 2.0, "y": 1.0},
      "half_extents": {"x": 0.5, "y": 0.5}
    },
    {
      "id": "tree_01",
      "shape": "circle",
      "center": {"x": 1.0, "y": 3.0},
      "radius": 0.35
    }
  ]
}
```

Static obstacles support axis-aligned AABBs and circles. Runtime building-like
entities can own `AoeMapStaticObstacle` plus `AoePosition`; the fixed map system
adds, updates, and removes their stable obstacle records automatically. Every
static change increments `static_revision()`, invalidating paths that were
planned against older geometry.

All active gameplay units are dynamic obstacles. A reusable uniform-grid index
is rebuilt once per fixed tick from `AoePosition`, `AoeCollider`, and
`AoeGameplayIdentity`; pooled and recycle-pending units are excluded. Its
two-pass count/prefix/fill build keeps cell ranges and memberships contiguous,
and generation marks deduplicate multi-cell candidates without per-query sets.
Movement performs swept elliptical collision against the retained nearest
candidates instead of querying the grid twice or checking every unit. Members
of the same squad ignore formation followers during route planning even when
the squad is globally `Engaging`; only members with an independent active
attack approach leave that shared flow. Final swept collision remains active,
so this planning rule does not permit overlap.

The built-in deterministic `grid_astar` uses eight neighbors, prevents diagonal
corner cutting, respects elliptical clearance, and simplifies the result only
across collision-free line-of-sight segments. A deterministic local steering
layer keeps a fixed top-eight set from the dynamic grid without sorting every
query result. No-threat movement uses one straight feeler. Threatened movement
samples a deterministic forward fan through 45 degrees, widening through 90
degrees after sustained low progress; no backward candidate is enabled yet.
Threatened results are cached for two staggered ticks unless contact is imminent.
The chosen avoidance
side receives a six-tick switching margin, and sprite facing must remain in a
new sector for two fixed ticks. Persistent velocity is constrained by
acceleration and turn rate, while swept ellipse tests remain the final
anti-penetration authority. Transient contacts steer locally; sustained
lack of forward progress triggers a dynamic A* replan after the configured
blocked threshold. A failed optional dynamic replan retains a usable static
route and continues local steering instead of stopping the unit. Failed replans
retry after a three-tick cooldown with a small deterministic per-entity offset.
Static no-path results wait until the map
revision changes. `AoeNavigationEvent` reports `Ready`, `Blocked`, and `NoPath`.
Without an `AoeLogicMap` resource, navigation deliberately falls back to the
old direct waypoint behavior. The default `AoeGameplayPlugin` uses
`AoeFullLocalAvoidancePlugin`; its `AoeLocalAvoidanceScratch` retains neighbor
storage capacity across fixed ticks, and `AoeGameplayDiagnostics` exposes fast,
full, cached and imminent solve counts plus side/facing changes and movement
timing.

Local avoidance is selected statically when the gameplay plugin is composed.
`AoePassThroughLocalAvoidancePlugin` forwards path-following velocity directly to
global motion planning without dynamic-neighbor collection, a local steering
solve, or full-plugin per-unit state. GPU/CPU global coordination and final
static/dynamic collision safety remain active, so this combination removes only
the local layer rather than permitting unit penetration.

Global motion is another required static phase. The default
`AoeDefaultGlobalMotionPlugin` retains the late-bound `gpu_image` planner and
the headless `cpu_unit_flow` fallback. `AoePassThroughGlobalMotionPlugin` is a
performance-floor backend: it preserves acceleration limiting and final static
obstacle safety, but deliberately omits dynamic unit coordination and dynamic
pair safety. Static phases cannot be switched at runtime; select the plugin
types when composing the application or benchmark.

Squad AttackMove engagement is also selected statically. The full plugin owns
automatic target acquisition and member target maintenance. The pass-through
plugin is empty, so AttackMove continues as formation travel while explicit
Squad AttackTarget remains available.

For a Global Motion A/B, keep the other phases fixed and change only the global
motion plugin before rebuilding the same executable:

```cpp
using ProductionGameplay = AoeGameplayDef<
    AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
    AoeFullLocalAvoidancePlugin, AoeDefaultGlobalMotionPlugin>;
using GlobalMotionFloorGameplay = AoeGameplayDef<
    AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
    AoeFullLocalAvoidancePlugin, AoePassThroughGlobalMotionPlugin>;
```

The squad preview's `SquadGameplayDef` is the intended benchmark edit point.
Its GPU plugin is installed only when the selected Global Motion phase uses the
runtime planner registry. Restore `AoeDefaultGlobalMotionPlugin` after measuring;
the pass-through result is a cost floor, not a behavior-equivalent production
configuration.

`AoeLocomotionState` exposes actual velocity, actual speed, previous velocity,
cumulative travelled distance, and `effective_max_speed`. The latter is the
base movement speed after persistent gameplay limits such as a mixed squad's
slowest-member cap, but before transient arrive, steering, and collision
reductions. The AoE2 presentation bridge advances Move loops by
`distance_delta / effective_max_speed`; reaching the commanded squad speed is
therefore 1x playback, while genuinely blocked or locally slowed units still
animate proportionally. Attack Move target scans that find no enemy preserve
formation velocity instead of restarting acceleration every fixed tick. Idle
remains fixed-clock-driven, while attack/death/disappear timing continues to use
action ticks.

Custom single-unit or squad planners use the same static registry interface:

```cpp
struct MyPathfinder {
    static AoePathResult find(EcsWorld& world,
                              const AoePathRequest& request);
};

auto& paths = world.resource_or_add<AoePathfinderRegistry>();
paths.bind<MyPathfinder>("my_pathfinder");
auto& settings = world.resource_or_add<AoeNavigationSettings>();
settings.unit_pathfinder_id = "my_pathfinder";
settings.squad_pathfinder_id = "my_pathfinder";
```

Local avoidance is extended by defining a static phase plugin and composing a
gameplay definition with it:

```cpp
struct MyLocalAvoidancePlugin {
    using phase = AoeLocalAvoidancePhase;
    static constexpr std::string_view name = "my_local_avoidance";

    static void install(App& app);
    static void fixed_tick(EcsWorld& world, std::uint64_t tick);
};

using MyGameplay = AoeGameplayDef<
    AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
    MyLocalAvoidancePlugin, AoeDefaultGlobalMotionPlugin>;
app.add_plugin(MyGameplay{"aoe_units"});
```

Squads plan a static-obstacle guide for a moving virtual anchor. Each member
independently plans toward its moving formation slot, so members can split
around an obstacle and gradually converge again. A valid guide keeps advancing
even when individual members temporarily fall behind or cannot reach a moving
slot; `squad_leash` is retained as a diagnostic threshold rather than a hard
stop. The anchor enters `Blocked` only when its own guide has no path. This
release intentionally has no map rendering, flow fields, full ORCA solver,
formation shrinking, or squad-wide shared path.

## Squads and formations

A squad is its own ECS entity. It owns the squad center, team, current order,
formation, shared target-acquisition settings, spawn result, and stable
references to its member incarnations. Composition is supplied by C++ when the
squad is spawned; it is deliberately not another JSON asset:

```cpp
AoeSquadSpawnOptions options;
options.composition = {
    {"camel_scout", 3, 1},
    {"archer", 5, 1},
};
options.center = {-5.f, 0.f};
options.forward = {1.f, 0.f};
options.formation = AoeFormationType::Skirmish;
options.team_id = 1;

const entt::entity squad = spawn_aoe_gameplay_squad(world, options);
request_aoe_squad_attack_move(world, squad, {8.f, 0.f});
```

Member definitions load through the normal asynchronous unit spawn path. The
squad reports `Pending`, `Ready`, `Partial`, `Failed`, or `Empty`; a bad member
definition does not discard members that loaded successfully. Member references
include gameplay instance IDs, so a recycled entity cannot silently become a
member of its former squad.

Formation implementations are static logic types registered by enum:

```cpp
using MySkirmishFormation = AoeSquareFormation<
    AoeFormationTagPriority<"spearman", 300>,
    AoeFormationTagPriority<"cavalry", 200>,
    AoeFormationTagPriority<"scout", 100>,
    AoeFormationTagPriority<"archer", -100>>;

registry.bind<AoeFormationType::Skirmish, MySkirmishFormation>();
```

Unit `tags` are runtime JSON strings, while the priority table is checked at
compile time. Every matching rule contributes to a member's score: a unit tagged
`["scout", "cavalry"]` scores 300 in the example above. Higher scores occupy
front slots first. Equal scores keep spawn-ordinal order, and unmatched tags
score zero. The built-in `DefaultSkirmishFormation` uses the rule table above
and produces a collision-aware square grid.

`request_aoe_squad_move`, `request_aoe_squad_attack`,
`request_aoe_squad_attack_move`, and `request_aoe_squad_stop` apply orders to
the whole squad. Squad movement is capped to the slowest living member. Attack
Move builds one enemy set shared by the whole squad, then lets each capable
member select its own nearest enemy with the strategy configured by the squad.
Targets are not claimed: several members may lock the same enemy, and the squad
anchor pauses while any shared engagement remains. Members leave their travel
slots, use deterministic target-relative approach directions, and form an inner
melee ring or an outer
projectile ring derived from their attack range. A dead target is replaced only
for the members that owned it; after no target remains the squad rebuilds its
formation while resuming the original destination. Explicit Squad AttackTarget
keeps focus-fire semantics while still spreading approach directions.
As with a standalone Attack Move unit, a stalled member may immediately replace
an out-of-range locked target with an enemy already inside that member's weapon
range. This does not change the Squad order or resume anchor movement while an
engagement remains. When an Attack Move anchor reaches its final destination,
the Squad performs at most one terminal nearest-slot rematch if members have not
arrived. Assignment is minimized independently inside each formation-priority
group, so front/back role ordering is preserved and members do not cross toward
slots already occupied by same-role Squad mates. Regular MoveTo does not perform
this terminal rematch.
Issuing an order-bearing unit command directly to a member
automatically detaches that member. `set_aoe_squad_formation` changes formation;
`disband_aoe_gameplay_squad` detaches all living members and destroys only the
squad entity.

Moving squads run a deterministic traffic-coordination layer before member
steering. Same-direction squads follow the leading flow, head-on squads negotiate
complementary right-hand lanes, and crossing squads use a stable entity priority
for yielding. The decision supplies a shared speed scale and lateral corridor to
all formation followers. A slot that temporarily falls inside static geometry is
resolved to a bounded elastic target biased along that corridor; the original
slot remains intact and is recovered after it becomes valid again.

Run `aoe_gameplay_squad_preview` for the mapped two-squad stress demo. It
installs a `192 x 112` logic map with two AABB barriers and a circular obstacle,
so both virtual anchors and their members use `grid_astar` instead of the direct
fallback. Keys 1 through 6 rebuild both sides with a total of
128/512/2,000/5,000/10,000/20,000 mixed Camel Scouts and Archers; the default
is 128 total, while the maximum preset contains 10,000 units per side. The main
camera automatically fits the complete isometric map and updates after a window
resize. Space reissues mutual Attack Move, S stops both squads, and R respawns
the current preset. G toggles map boundaries and obstacles, N toggles
anchor/member paths and formation slots, P toggles rendered SLD foot markers, C
toggles the interpolated gameplay collision ellipses, and L traces one blue
Archer to the console. The HUD and profile CSV identify the statically selected
local-avoidance backend. Collision, navigation, foot, and trace
diagnostics are disabled when comparing performance because they add
intentional debug work. F5 rescans unit definitions and respawns, and Escape
exits. The stress preview disables VSync by default so its FPS reflects actual
throughput; V toggles VSync at runtime. Set `GLD_AOE_STRESS_PRESET=1|2|3|4|5|6` to
select the initial load and `GLD_AOE_PROFILE=1` to append HUD snapshots to
`aoe_gameplay_squad_profile.log`. The aggregate HUD reports squad state plus fixed-clock, dynamic-index,
steering-tier, live entity category/high-water, AoE2 batch, upload, and render
timing diagnostics without listing every member. `aoe_map_benchmark` also runs a
headless 30,000-unit, 30-tick crowd workload so rendering cost cannot be
mistaken for gameplay steering cost.

The 20,000-unit preset is a pressure-test tier: it must load, render, accept
Attack Move, and continue publishing diagnostics, but it does not promise an
interactive frame rate during large-scale acquisition, combat, or terminal
formation rematching. Keep the optional Gizmo and motion trace switches disabled
when recording its baseline.

With `GLD_ENABLE_PERFORMANCE_MONITORING=ON`, set
`GLD_AOE_SYSTEM_PROFILE=<csv-path>` to make the squad preview wait for the
default scene to become resident, warm up for three seconds, capture system
timings for 15 seconds, write one buffered CSV, and exit. Override the capture
length with `GLD_AOE_SYSTEM_PROFILE_SECONDS=<seconds>`. The trace splits every
gameplay fixed-tick phase, the AoE2 spawn/animation/batch systems, presentation
bridge, transform, render, GPU and present work. The HUD's `render_fps` and
frame-time statistics use wall-clock windows; `fixed_hz` and `pose_change_hz`
separately report simulation and atlas-pose cadence.
Unit presentation interpolates between the previous and current authoritative
30 Hz positions with one fixed-tick of latency; movement, collision, targeting,
combat, and projectiles continue to use current gameplay positions without
interpolation.

## AoE2 presentation bridge

`gld_aoe2_gameplay_bridge` is the optional renderer-specific adapter. It creates
a separate AoE2 child entity, attaches it to the gameplay parent, selects the
configured semantic animation, and synchronizes facing, player color, and a
fixed-clock presentation cursor. Direction changes and transitions between
`idle` and `moving` preserve that cursor, so switching an atlas does not jump
back to frame zero. Entering `attack`/`critical_attack`, `death`, or `disappear`
starts the authored action immediately at frame zero; returning from an attack
to locomotion retains the attack-end cursor. Critical attacks prefer
`critical_attack` and fall back to `attack`. Dying and disappearing select
non-looping `death` and `disappear` animations. Damage, projectile release, and
lifecycle timing remain authoritative gameplay fixed-clock events and do not
depend on render loading or frame selection. The bridge removes the render
child before the gameplay entity enters the pool.

AoE2 SLD direction 0 represents screen-facing `(1, 0)`, and its slots increase
clockwise. Gameplay first projects a logical map delta through the same 2:1
isometric basis into screen-facing space, then measures the signed angle from
the explicit `(1, 0)` reference and selects the nearest direction sector. The
AoE2 bridge forwards that slot without another rotation. The combat
preview also derives sprite depth from `x + y` and enables depth test/write on
its AoE2 pass, so units lower on the isometric screen correctly cover units
behind them without breaking instanced batching.

Presentation load failures are recorded in `AoePresentationError`; gameplay keeps
advancing. Destroyed children are recreated, and children whose gameplay owner no
longer exists are cleaned up.

The bridge also maps gameplay projectile `arrow` to the standalone AoE2 graphic
`p_arrow/p_arrow_x2`. Its 32 direction groups encode horizontal facing and its
11 frames encode pitch from up through horizontal to down; they are selected
from trajectory velocity rather than played as a looping animation.

Run `aoe_gameplay_unit_preview` for the combat demo. It creates a blue player and
a passive red Camel Scout at a random logical map position, then projects both
with the AoE isometric mapping. Controls are Left/Right player definition, Space
attack the enemy, M Attack Move through and beyond the enemy, A/D facing, R reuse
the enemy after it reaches the pool, F5 rescan definitions, and Escape quit.

## GPU image global motion planner

`AoeNavigationSettings::global_motion_planner_id` defaults to `gpu_image`.
Windowed gameplay examples register `AoeGpuMotionPlugin`; renderer-free tests
and machines without OpenGL 4.3 automatically execute `cpu_unit_flow` for that
fixed tick. `AoeGlobalMotionPlannerDiagnostics` records the requested/active
backend, failures, fallback reason, and authoritative safety corrections.

The GPU world state uses two ping-pong `GL_RG16UI` images at 16 pixels per world
unit. R16 stores a reusable unit handle or a reserved static-obstacle ID; G16
stores an 8-bit direction and 8-bit speed. Exact float positions, radii,
generation values, intentions, and final velocities remain in SSBOs. Solve
builds one shared `RGBA16F` field with mips, scores candidate directions,
atomically reserves a temporary `R32UI` image, validates the complete rigid
footprint, propagates same-direction dependencies for 32 passes, writes the
complete next image, and reads the compact decision SSBO in the same gameplay
fixed tick. The images are swapped only after commit and readback.

Detailed fixed-tick map dumps are compiled only when
`GLD_ENABLE_PERFORMANCE_MONITORING=ON`. Set the output directory before running
either gameplay preview:

```powershell
$env:GLD_AOE_GPU_MAP_DUMP = "build/profile/gpu_map"
```

Every fixed tick produces `tick_<n>.rg16ui.bin`,
`tick_<n>_occupancy.png`, and `tick_<n>_vector.png`. Readback uses a three-PBO
ring; a full ring waits instead of dropping a fixed tick, while Raw/PNG encoding
runs on a background writer.
