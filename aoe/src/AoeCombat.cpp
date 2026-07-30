#include <aoe/AoeGameplay.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;
std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}
float random01(AoeGameplayIdentity& identity) {
    identity.rng_state = mix64(identity.rng_state);
    return static_cast<float>((identity.rng_state >> 40) * (1.0 / 16777216.0));
}

std::uint64_t seconds_to_ticks(float seconds, const AoeGameplaySettings& settings) {
    if (seconds <= 0.f) return 0;
    const double ticks = static_cast<double>(seconds) / settings.fixed_dt;
    const double tolerance = 1e-6 * std::max(1.0, std::abs(ticks));
    return static_cast<std::uint64_t>(std::ceil(ticks - tolerance));
}

void emit_event(EcsWorld& world, entt::entity unit, AoeActionEventType type,
                const AoeActionState& state, std::uint64_t tick,
                entt::entity target = entt::null, float amount = 0.f) {
    world.resource_or_add<Events<AoeActionEvent>>().emit(
        AoeActionEvent{unit, target, type, tick, state.sequence, state.critical, amount});
    ++world.resource_or_add<AoeGameplayDiagnostics>().events_emitted;
}

void reject(EcsWorld& world) {
    ++world.resource_or_add<AoeGameplayDiagnostics>().commands_rejected;
}

bool is_terminal(UnitState state) {
    return state == UnitState::Dying || state == UnitState::Disappearing;
}

bool target_valid(const entt::registry& reg, const AoeUnitTarget& target) {
    if (target.entity == entt::null || !reg.valid(target.entity) ||
        reg.any_of<AoePooledUnit, AoeRecyclePending>(target.entity)) return false;
    const auto* identity = reg.try_get<AoeGameplayIdentity>(target.entity);
    const auto* health = reg.try_get<AoeHealth>(target.entity);
    const auto* state = reg.try_get<AoeActionState>(target.entity);
    return identity && identity->instance_id == target.instance_id && health && health->current > 0.f &&
           state && !is_terminal(state->state) &&
           reg.all_of<AoePosition, AoeCollider, AoeUnitDefinitionRef>(target.entity);
}

int facing_direction_toward(const AoeFacing& facing, glm::vec2 delta) {
    if (facing.direction_count <= 0 ||
        glm::dot(delta, delta) <= Epsilon * Epsilon) return facing.direction;

    // Gameplay positions use AoE map X/Y axes, while SLD directions are
    // screen-facing sectors. Project the delta through the same 2:1 isometric
    // basis first (screen Y points down), then measure from SLD direction zero
    // at screen-space +X and select the nearest clockwise sector.
    const glm::vec2 facing_delta{
        delta.x - delta.y,
        (delta.x + delta.y) * .5f};
    const glm::vec2 reference{1.f, 0.f};
    const glm::vec2 direction = glm::normalize(facing_delta);
    const float cross = reference.x * direction.y - reference.y * direction.x;
    const float dot = std::clamp(glm::dot(reference, direction), -1.f, 1.f);
    float angle = std::atan2(cross, dot);
    const float full_turn = 2.f * glm::pi<float>();
    angle = std::fmod(angle, full_turn);
    if (angle < 0.f) angle += full_turn;

    const float sector_width = full_turn / static_cast<float>(facing.direction_count);
    const int sector = static_cast<int>(
        std::floor((angle + sector_width * .5f) / sector_width));
    return sector % facing.direction_count;
}

void set_facing_toward(AoeFacing& facing, glm::vec2 delta) {
    facing.direction = facing_direction_toward(facing, delta);
}

void set_locomotion_facing(
    AoeFacing& facing, AoeLocomotionState& locomotion, glm::vec2 delta,
    const AoeNavigationSettings& settings, AoeGameplayDiagnostics& diagnostics) {
    const int desired = facing_direction_toward(facing, delta);
    if (desired == facing.direction) {
        locomotion.pending_facing_direction = -1;
        locomotion.pending_facing_ticks = 0;
        return;
    }
    if (locomotion.pending_facing_direction != desired) {
        locomotion.pending_facing_direction = desired;
        locomotion.pending_facing_ticks = 1;
    } else if (locomotion.pending_facing_ticks <
               std::numeric_limits<std::uint8_t>::max()) {
        ++locomotion.pending_facing_ticks;
    }
    const auto stable_ticks = std::max(
        1u, settings.steering_facing_stable_ticks);
    if (locomotion.pending_facing_ticks < stable_ticks) {
        ++diagnostics.facing_changes_suppressed;
        return;
    }
    facing.direction = desired;
    locomotion.pending_facing_direction = -1;
    locomotion.pending_facing_ticks = 0;
    ++diagnostics.facing_changes_committed;
}

void clear_active_engagement(entt::registry& reg, entt::entity entity) {
    reg.remove<AoeAttackOrder>(entity);
    reg.remove<AoeEngagementApproach>(entity);
    reg.remove<AoeMoveGoal>(entity);
    reg.remove<AoeNavigationPath>(entity);
}

void cancel_orders(entt::registry& reg, entt::entity entity) {
    clear_active_engagement(reg, entity);
    reg.remove<AoeAttackMoveOrder>(entity);
}

void set_idle_if_active(entt::registry& reg, entt::entity entity,
                        std::uint64_t tick) {
    auto& state = reg.get<AoeActionState>(entity);
    if (is_terminal(state.state)) return;
    state.state = UnitState::Idle;
    state.state_started_tick = tick;
    state.critical = false;
    state.release_emitted = false;
    if (auto* locomotion = reg.try_get<AoeLocomotionState>(entity)) {
        locomotion->velocity = {0.f, 0.f};
        locomotion->actual_speed = 0.f;
    }
}

void begin_death(EcsWorld& world, entt::entity entity, std::uint64_t tick) {
    auto& reg = world.reg();
    auto* state = reg.try_get<AoeActionState>(entity);
    if (!state || is_terminal(state->state)) return;
    cancel_orders(reg, entity);
    state->state = UnitState::Dying;
    state->critical = false;
    state->release_emitted = false;
    state->state_started_tick = tick;
    ++state->sequence;
    emit_event(world, entity, AoeActionEventType::DeathStarted, *state, tick);
}

float armor_for(const AoeUnitDefinition& definition, int class_id) {
    for (const auto& armor : definition.armor)
        if (armor.class_id == class_id) return armor.amount;
    return 0.f;
}

float damage_for(const AttackDefinition& attack, const AoeUnitDefinition& target,
                 bool critical) {
    float total = 0.f;
    bool has_positive = false;
    for (const auto& damage : attack.damage) {
        has_positive = has_positive || damage.amount > 0.f;
        total += std::max(0.f, damage.amount - armor_for(target, damage.class_id));
    }
    if (critical) total *= attack.critical_multiplier;
    return has_positive ? std::max(1.f, total) : 0.f;
}

float damage_for_payload(const std::vector<TypedAmount>& damage,
                         float critical_multiplier,
                         const AoeUnitDefinition& target, bool critical) {
    float total = 0.f;
    bool has_positive = false;
    for (const auto& value : damage) {
        has_positive = has_positive || value.amount > 0.f;
        total += std::max(0.f, value.amount - armor_for(target, value.class_id));
    }
    if (critical) total *= critical_multiplier;
    return has_positive ? std::max(1.f, total) : 0.f;
}

void emit_projectile_event(EcsWorld& world, AoeActionEventType type,
                           const AoeProjectile& projectile,
                           entt::entity projectile_entity, std::uint64_t tick,
                           AoeProjectileMissReason reason = AoeProjectileMissReason::None,
                           float amount = 0.f) {
    AoeActionEvent event;
    event.unit = projectile.attacker;
    event.target = projectile.target.entity;
    event.type = type;
    event.tick = tick;
    event.sequence = projectile.attack_sequence;
    event.critical = projectile.critical;
    event.amount = amount;
    event.projectile = projectile_entity;
    event.projectile_id = projectile.id;
    event.projectile_reason = reason;
    world.resource_or_add<Events<AoeActionEvent>>().emit(std::move(event));
    ++world.resource_or_add<AoeGameplayDiagnostics>().events_emitted;
}

glm::vec3 projectile_launch_position(const AoePosition& source,
                                     const AoeCollider& source_collider,
                                     const AoeFacing& facing,
                                     const AttackDefinition& attack) {
    glm::vec3 local = attack.projectile_launch_offset.value_or(
        glm::vec3{0.f, 0.f, source_collider.height * .75f});
    if (attack.projectile_launch_offset && facing.direction_count > 0) {
        // DAT graphic_displacement uses the sprite-facing coordinate basis.
        // SLD direction zero faces opposite raw DAT +Y, and direction slots
        // advance clockwise. Rotate with the already locked render-facing slot
        // so the gameplay launch socket stays attached to the attack sprite.
        const float angle = glm::pi<float>() - glm::two_pi<float>() *
            static_cast<float>(facing.direction) /
            static_cast<float>(facing.direction_count);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        local = {local.x * c - local.y * s,
                 local.x * s + local.y * c, local.z};
    }
    return {source.value.x + local.x, source.value.y + local.y, local.z};
}

} // namespace

void combat_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& settings = world.resource<AoeGameplaySettings>();
    std::vector<entt::entity> clear;
    std::vector<entt::entity> deaths;
    for (auto entity : reg.view<AoeAttackOrder, AoeActionState, AoeUnitDefinitionRef,
                                AoeGameplayIdentity, AoePosition, AoeCollider, AoeFacing>()) {
        auto& order = reg.get<AoeAttackOrder>(entity);
        auto& state = reg.get<AoeActionState>(entity);
        const auto* definition = reg.get<AoeUnitDefinitionRef>(entity).value.get();
        if (!definition || !definition->attack || !target_valid(reg, order.target)) {
            clear.push_back(entity); continue;
        }
        const auto& attack = *definition->attack;
        const float gap = aoe_surface_gap(reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
            reg.get<AoePosition>(order.target.entity), reg.get<AoeCollider>(order.target.entity));
        if (state.state == UnitState::Attacking) {
            if (!state.release_emitted && tick >= state.release_tick) {
                state.release_emitted = true;
                emit_event(world, entity, AoeActionEventType::AttackReleased, state, tick,
                           order.target.entity);
                if (gap <= attack.range + Epsilon && target_valid(reg, order.target)) {
                    if (attack.mode == AttackMode::Projectile) {
                        AoeProjectileSpawnContext context;
                        context.attacker = entity;
                        context.target = order.target;
                        context.attack_sequence = state.sequence;
                        context.critical = state.critical;
                        context.critical_multiplier = attack.critical_multiplier;
                        context.damage = attack.damage;
                        context.launch_position = projectile_launch_position(
                            reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
                            reg.get<AoeFacing>(entity), attack);
                        if (const auto* presentation =
                                reg.try_get<AoePresentationOptions>(entity))
                            context.layers = presentation->layers;
                        auto& registry = world.resource_or_add<AoeProjectileRegistry>();
                        const auto projectile_entity = registry.spawn(
                            attack.projectile_id, world, context);
                        if (projectile_entity != entt::null && reg.valid(projectile_entity) &&
                            reg.all_of<AoeProjectile>(projectile_entity)) {
                            const auto& projectile = reg.get<AoeProjectile>(projectile_entity);
                            emit_projectile_event(world, AoeActionEventType::ProjectileSpawned,
                                projectile, projectile_entity, tick);
                            ++world.resource<AoeGameplayDiagnostics>().projectiles_spawned;
                        } else {
                            AoeProjectile failed;
                            failed.id = attack.projectile_id;
                            failed.attacker = entity;
                            failed.target = order.target;
                            failed.attack_sequence = state.sequence;
                            failed.critical = state.critical;
                            emit_projectile_event(world,
                                AoeActionEventType::ProjectileSpawnFailed, failed,
                                entt::null, tick, AoeProjectileMissReason::UnknownLogic);
                            ++world.resource<AoeGameplayDiagnostics>().projectiles_failed;
                        }
                    } else {
                        auto& target_health = reg.get<AoeHealth>(order.target.entity);
                        const auto* target_definition =
                            reg.get<AoeUnitDefinitionRef>(order.target.entity).value.get();
                        const float amount = target_definition
                            ? damage_for(attack, *target_definition, state.critical) : 0.f;
                        target_health.current = std::max(0.f, target_health.current - amount);
                        emit_event(world, entity, AoeActionEventType::DamageApplied, state, tick,
                                   order.target.entity, amount);
                        ++world.resource<AoeGameplayDiagnostics>().damage_events;
                        if (target_health.current <= 0.f)
                            deaths.push_back(order.target.entity);
                    }
                }
            }
            if (tick >= state.finish_tick) {
                emit_event(world, entity, AoeActionEventType::AttackFinished, state, tick,
                           order.target.entity);
                state.state = UnitState::Idle;
                state.critical = false;
                state.release_emitted = false;
                state.state_started_tick = tick;
            }
            continue;
        }
        if (gap <= attack.range + Epsilon && tick >= state.ready_tick) {
            set_facing_toward(reg.get<AoeFacing>(entity),
                reg.get<AoePosition>(order.target.entity).value -
                reg.get<AoePosition>(entity).value);
            reg.remove<AoeMoveGoal>(entity);
            reg.remove<AoeNavigationPath>(entity);
            state.state = UnitState::Attacking;
            state.state_started_tick = tick;
            state.release_tick = tick + seconds_to_ticks(attack.release_seconds, settings);
            state.finish_tick = tick + seconds_to_ticks(attack.animation_duration_seconds, settings);
            state.ready_tick = tick + seconds_to_ticks(attack.cooldown_seconds, settings);
            state.release_emitted = false;
            state.critical = random01(reg.get<AoeGameplayIdentity>(entity)) < attack.critical_chance;
            ++state.sequence;
            ++world.resource<AoeGameplayDiagnostics>().attacks_started;
            emit_event(world, entity, AoeActionEventType::AttackStarted, state, tick,
                       order.target.entity);
        }
    }
    std::sort(deaths.begin(), deaths.end());
    deaths.erase(std::unique(deaths.begin(), deaths.end()), deaths.end());
    for (auto entity : deaths) if (reg.valid(entity)) begin_death(world, entity, tick);
    for (auto entity : clear) {
        if (!reg.valid(entity)) continue;
        if (reg.all_of<AoeAttackMoveOrder>(entity))
            clear_active_engagement(reg, entity);
        else cancel_orders(reg, entity);
        set_idle_if_active(reg, entity, tick);
    }
}

void AoeProjectileRegistry::bind_erased(std::string id, SpawnFn function) {
    if (id.empty() || !function)
        throw std::invalid_argument("projectile binding requires a non-empty id and function");
    if (!entries_.emplace(std::move(id), function).second)
        throw std::invalid_argument("duplicate projectile binding");
}

bool AoeProjectileRegistry::contains(std::string_view id) const {
    return entries_.find(std::string(id)) != entries_.end();
}

entt::entity AoeProjectileRegistry::spawn(
    std::string_view id, EcsWorld& world,
    const AoeProjectileSpawnContext& context) const {
    const auto it = entries_.find(std::string(id));
    return it == entries_.end() ? entt::null : it->second(world, context);
}

entt::entity ArrowProjectileLogic::spawn(
    EcsWorld& world, const AoeProjectileSpawnContext& context) {
    const auto entity = world.spawn();
    const auto tick = world.resource_or_add<AoeGameplayClock>().tick;
    const auto& settings = world.resource<AoeGameplaySettings>();
    AoeProjectile projectile;
    projectile.id = "arrow";
    projectile.attacker = context.attacker;
    projectile.target = context.target;
    projectile.attack_sequence = context.attack_sequence;
    projectile.critical = context.critical;
    projectile.critical_multiplier = context.critical_multiplier;
    projectile.launch_position = context.launch_position;
    projectile.previous_position = context.launch_position;
    projectile.position = context.launch_position;
    projectile.layers = context.layers;
    projectile.damage = context.damage;
    projectile.speed = Speed;
    projectile.arc_height = ArcHeight;
    projectile.collision_radius = CollisionRadius;
    projectile.spawn_tick = tick;
    projectile.expire_tick = tick + seconds_to_ticks(MaxLifetimeSeconds, settings);
    world.reg().emplace<AoeProjectile>(entity, std::move(projectile));
    world.reg().emplace<Transform>(entity, Transform{});
    return entity;
}

namespace {
bool point_in_expanded_ellipse(glm::vec2 point, const AoePosition& center,
                               const AoeCollider& collider, float radius) {
    const float rx = collider.radius_x + radius;
    const float ry = collider.radius_y + radius;
    const glm::vec2 delta = point - center.value;
    return delta.x * delta.x / (rx * rx) + delta.y * delta.y / (ry * ry) <= 1.f;
}

bool swept_projectile_hits(const glm::vec3& from, const glm::vec3& to,
                           const AoePosition& center, const AoeCollider& collider,
                           float radius) {
    const auto height_inside = [&](float z) {
        return z >= -radius && z <= collider.height + radius;
    };
    if (point_in_expanded_ellipse(glm::vec2(to), center, collider, radius) &&
        height_inside(to.z)) return true;

    const float rx = collider.radius_x + radius;
    const float ry = collider.radius_y + radius;
    const glm::vec2 start = glm::vec2(from) - center.value;
    const glm::vec2 delta = glm::vec2(to - from);
    const float a = delta.x * delta.x / (rx * rx) +
                    delta.y * delta.y / (ry * ry);
    const float b = 2.f * (start.x * delta.x / (rx * rx) +
                           start.y * delta.y / (ry * ry));
    const float c = start.x * start.x / (rx * rx) +
                    start.y * start.y / (ry * ry) - 1.f;
    if (a <= Epsilon) {
        if (c > 0.f) return false;
        const float low = std::min(from.z, to.z);
        const float high = std::max(from.z, to.z);
        return high >= -radius && low <= collider.height + radius;
    }
    const float discriminant = b * b - 4.f * a * c;
    if (discriminant < 0.f) return false;
    const float root = std::sqrt(std::max(0.f, discriminant));
    const float enter = (-b - root) / (2.f * a);
    const float leave = (-b + root) / (2.f * a);
    const float raw_begin = std::min(enter, leave);
    const float raw_end = std::max(enter, leave);
    if (raw_end < 0.f || raw_begin > 1.f) return false;
    const float begin = std::clamp(raw_begin, 0.f, 1.f);
    const float end = std::clamp(raw_end, 0.f, 1.f);
    const float z0 = from.z + (to.z - from.z) * begin;
    const float z1 = from.z + (to.z - from.z) * end;
    return std::max(z0, z1) >= -radius &&
           std::min(z0, z1) <= collider.height + radius;
}

AoeProjectileMissReason projectile_target_reason(const entt::registry& reg,
                                                  const AoeUnitTarget& target) {
    if (target.entity == entt::null || !reg.valid(target.entity))
        return AoeProjectileMissReason::TargetInvalid;
    if (reg.any_of<AoePooledUnit, AoeRecyclePending>(target.entity))
        return AoeProjectileMissReason::TargetRecycled;
    const auto* identity = reg.try_get<AoeGameplayIdentity>(target.entity);
    if (!identity || identity->instance_id != target.instance_id)
        return AoeProjectileMissReason::TargetRecycled;
    const auto* health = reg.try_get<AoeHealth>(target.entity);
    const auto* state = reg.try_get<AoeActionState>(target.entity);
    if (!health || health->current <= 0.f || !state || is_terminal(state->state))
        return AoeProjectileMissReason::TargetDead;
    if (!reg.all_of<AoePosition, AoeCollider, AoeUnitDefinitionRef>(target.entity))
        return AoeProjectileMissReason::TargetInvalid;
    return AoeProjectileMissReason::None;
}
} // namespace

void aoe_projectile_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
    std::vector<entt::entity> finished;
    std::vector<entt::entity> deaths;
    for (const auto entity : reg.view<AoeProjectile>()) {
        auto& projectile = reg.get<AoeProjectile>(entity);
        if (tick <= projectile.spawn_tick) continue;
        auto reason = projectile_target_reason(reg, projectile.target);
        if (reason == AoeProjectileMissReason::None && tick >= projectile.expire_tick)
            reason = AoeProjectileMissReason::Expired;
        if (reason != AoeProjectileMissReason::None) {
            emit_projectile_event(world, AoeActionEventType::ProjectileMiss,
                                  projectile, entity, tick, reason);
            ++world.resource<AoeGameplayDiagnostics>().projectiles_missed;
            finished.push_back(entity);
            continue;
        }

        const auto& target_position = reg.get<AoePosition>(projectile.target.entity);
        const auto& target_collider = reg.get<AoeCollider>(projectile.target.entity);
        projectile.previous_position = projectile.position;
        const glm::vec2 current_ground{projectile.position.x, projectile.position.y};
        const glm::vec2 delta = target_position.value - current_ground;
        const float remaining = glm::length(delta);
        const float step = std::min(remaining, projectile.speed * dt);
        glm::vec2 next_ground = current_ground;
        if (remaining > Epsilon) next_ground += delta / remaining * step;
        projectile.travelled += step;
        const float after_remaining = glm::length(target_position.value - next_ground);
        const float denominator = projectile.travelled + after_remaining;
        const float candidate = denominator > Epsilon
            ? projectile.travelled / denominator : 1.f;
        projectile.progress = std::max(projectile.progress,
                                       std::clamp(candidate, 0.f, 1.f));
        const float target_z = target_collider.height * .5f;
        const float p = projectile.progress;
        projectile.position = {
            next_ground.x, next_ground.y,
            projectile.launch_position.z +
                (target_z - projectile.launch_position.z) * p +
                4.f * projectile.arc_height * p * (1.f - p)};
        projectile.velocity = (projectile.position - projectile.previous_position) / dt;

        if (!swept_projectile_hits(projectile.previous_position, projectile.position,
                                   target_position, target_collider,
                                   projectile.collision_radius)) continue;

        auto& health = reg.get<AoeHealth>(projectile.target.entity);
        const auto* target_definition =
            reg.get<AoeUnitDefinitionRef>(projectile.target.entity).value.get();
        const float amount = target_definition
            ? damage_for_payload(projectile.damage, projectile.critical_multiplier,
                                 *target_definition, projectile.critical)
            : 0.f;
        health.current = std::max(0.f, health.current - amount);
        emit_projectile_event(world, AoeActionEventType::ProjectileHit,
                              projectile, entity, tick,
                              AoeProjectileMissReason::None, amount);
        emit_projectile_event(world, AoeActionEventType::DamageApplied,
                              projectile, entity, tick,
                              AoeProjectileMissReason::None, amount);
        auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
        ++diagnostics.projectiles_hit;
        ++diagnostics.damage_events;
        if (health.current <= 0.f) deaths.push_back(projectile.target.entity);
        finished.push_back(entity);
    }
    std::sort(deaths.begin(), deaths.end());
    deaths.erase(std::unique(deaths.begin(), deaths.end()), deaths.end());
    for (const auto entity : deaths)
        if (reg.valid(entity)) begin_death(world, entity, tick);
    for (const auto entity : finished)
        if (reg.valid(entity)) reg.destroy(entity);
}


} // namespace gld::ecs::aoe

