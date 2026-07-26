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

`target_acquisition` is optional. Its defaults are `nearest_enemy` and a
surface-to-surface radius of 6 gameplay units. Strategies are static logic types
registered with `AoeTargetAcquisitionRegistry::bind<T>(id)`. The built-in
strategy selects the closest live unit with a different instance-level
`AoeTeam`; player color remains presentation-only.

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

Attack Move searches while travelling, pauses at an acquired enemy and reuses
the persistent attack behavior, then resumes its original destination after the
target dies. An explicit AttackTarget, MoveTo, Stop, or a new Attack Move
replaces the previous command. `AoeUnitSpawnOptions::team_id` assigns gameplay
affiliation and is reset when a pooled entity is reused.

The fixed pipeline is commands, Attack Move acquisition, navigation, movement,
combat, projectile, then lifecycle.
Navigation writes `AoeNavigationPath`; movement only consumes waypoints. This is
the extension boundary for later pathfinding, obstacle avoidance, and steering.
Range uses the support radii of both elliptical colliders rather than center
distance alone.

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
Move acquires one shared enemy, gives every capable member the same focus-fire
target, and immediately chains to another enemy in range when that target dies.
Only after no target remains does the squad resume its original destination;
survivors recover their formation while moving instead of waiting to regroup.
Issuing an order-bearing unit command directly to a member
automatically detaches that member. `set_aoe_squad_formation` changes formation;
`disband_aoe_gameplay_squad` detaches all living members and destroys only the
squad entity.

Run `aoe_gameplay_squad_preview` for the two-squad demo. Each side contains
three Camel Scouts and five Archers and automatically Attack Moves toward the
other side. Space reissues mutual Attack Move, S stops both squads, R respawns
the battle, P toggles white crosses at each rendered unit's SLD foot/world
origin, L traces the lowest-ordinal live blue Archer's fixed-tick history,
interpolated logical foot, raw SLD hotspot, and final render origin to the
console, F5 rescans unit definitions and respawns, and Escape exits. The HUD
shows squad spawn/phase state plus every live member's tags, summed priority,
and assigned slot. Unit presentation interpolates between the previous and
current authoritative 30 Hz positions with one fixed-tick of latency; movement,
collision, targeting, combat, and projectiles continue to use current gameplay
positions without interpolation.

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
