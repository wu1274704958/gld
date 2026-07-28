#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>
#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe {
namespace {
using json = nlohmann::json;
constexpr float Epsilon = 1e-5f;
void squad_traffic_tick(EcsWorld& world, std::uint64_t tick);
void movement_intent_tick(EcsWorld& world, std::uint64_t tick);
void local_avoidance_intent_tick(EcsWorld& world, std::uint64_t tick);
void unit_flow_tick(EcsWorld& world, std::uint64_t tick);
void global_motion_safety_tick(EcsWorld& world, std::uint64_t tick);

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
#define GLD_AOE_GAMEPLAY_PHASE(world_value, field_name, function) do { \
    const auto phase_started = std::chrono::steady_clock::now(); \
    function(); \
    (world_value).resource_or_add<AoeGameplayPerformanceDiagnostics>().field_name += \
        std::chrono::duration<double, std::milli>( \
            std::chrono::steady_clock::now() - phase_started).count(); \
} while (false)
#else
#define GLD_AOE_GAMEPLAY_PHASE(world_value, field_name, function) do { function(); } while (false)
#endif

float steering_dynamic_safe_fraction(
    glm::vec2 from, glm::vec2 to, glm::vec2 radii,
    std::span<const AoeSteeringNeighbor> neighbors) {
    float result = 1.f;
    const glm::vec2 delta = to - from;
    for (const auto& neighbor : neighbors) {
        const glm::vec2 combined = radii + neighbor.radii;
        if (!(combined.x > 0.f) || !(combined.y > 0.f)) continue;
        const glm::vec2 start = from - neighbor.position;
        const float rx2 = combined.x * combined.x;
        const float ry2 = combined.y * combined.y;
        const float a = delta.x * delta.x / rx2 +
                        delta.y * delta.y / ry2;
        const float b = 2.f * (start.x * delta.x / rx2 +
                               start.y * delta.y / ry2);
        const float c = start.x * start.x / rx2 +
                        start.y * start.y / ry2 - 1.f;
        float enter = 1.f;
        if (c <= 0.f) enter = b >= -Epsilon ? 1.f : 0.f;
        else if (a > Epsilon) {
            const float discriminant = b * b - 4.f * a * c;
            if (discriminant >= 0.f) {
                const float root = std::sqrt(std::max(0.f, discriminant));
                const float value = (-b - root) / (2.f * a);
                if (value >= 0.f && value <= 1.f) enter = value;
            }
        }
        if (enter < 1.f)
            result = std::min(result, std::max(0.f, enter - .0001f));
    }
    return result;
}

struct AStarOpenNode {
    std::size_t index = 0;
    float cost = 0.f;
    float estimate = 0.f;
};

struct AStarOpenCompare {
    bool operator()(const AStarOpenNode& a, const AStarOpenNode& b) const {
        if (std::abs(a.estimate - b.estimate) > Epsilon)
            return a.estimate > b.estimate;
        if (std::abs(a.cost - b.cost) > Epsilon) return a.cost > b.cost;
        return a.index > b.index;
    }
};

struct AStarWorkspace {
    std::vector<float> costs;
    std::vector<std::size_t> parents;
    std::vector<std::uint32_t> generations;
    std::vector<std::uint32_t> closed;
    std::vector<AStarOpenNode> open;
    std::uint32_t generation = 0;

    void begin(std::size_t count) {
        if (costs.size() < count) {
            costs.resize(count);
            parents.resize(count);
            generations.resize(count, 0);
            closed.resize(count, 0);
        }
        if (++generation == 0) {
            std::fill(generations.begin(), generations.end(), 0);
            std::fill(closed.begin(), closed.end(), 0);
            generation = 1;
        }
        open.clear();
    }
};

float finite_number(const json& value, const char* name) {
    const double number = value.get<double>();
    if (!std::isfinite(number) || number < -std::numeric_limits<float>::max() ||
        number > std::numeric_limits<float>::max())
        throw std::runtime_error(std::string(name) + " must be finite");
    return static_cast<float>(number);
}

float non_negative(const json& value, const char* name) {
    const float number = finite_number(value, name);
    if (number < 0.f) throw std::runtime_error(std::string(name) + " must be non-negative");
    return number;
}

AoeTargetAcquisitionType target_acquisition_type(
    std::string_view name) {
    if (name == "nearest_enemy")
        return AoeTargetAcquisitionType::NearestEnemy;
    throw std::runtime_error(
        "target_acquisition.strategy_id must be nearest_enemy");
}

std::vector<TypedAmount> typed_amounts(const json& values, const char* name) {
    if (!values.is_array()) throw std::runtime_error(std::string(name) + " must be an array");
    std::vector<TypedAmount> result;
    std::unordered_set<int> ids;
    for (const auto& value : values) {
        TypedAmount item;
        item.class_id = value.at("class_id").get<int>();
        item.amount = non_negative(value.at("amount"), name);
        if (item.class_id < 0 || !ids.insert(item.class_id).second)
            throw std::runtime_error(std::string(name) + " has invalid/duplicate class_id");
        result.push_back(item);
    }
    return result;
}

std::shared_ptr<AoeUnitDefinition> parse_definition(const json& source) {
    const int schema = source.at("schema_version").get<int>();
    if ((schema != 1 && schema != 2) ||
        source.at("kind").get<std::string>() != "aoe_gameplay_unit")
        throw std::runtime_error("unsupported gameplay unit schema/kind");
    auto result = std::make_shared<AoeUnitDefinition>();
    result->id = source.at("id").get<std::string>();
    if (result->id.empty()) throw std::runtime_error("unit id must not be empty");
    const auto level = source.at("level").get<std::int64_t>();
    if (level < 1 || level > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("level must be at least one");
    result->level = static_cast<std::uint32_t>(level);
    result->max_hp = finite_number(source.at("max_hp"), "max_hp");
    if (result->max_hp <= 0.f) throw std::runtime_error("max_hp must be positive");
    result->armor = typed_amounts(source.at("armor"), "armor");
    if (source.contains("tags")) {
        const auto& tags = source.at("tags");
        if (!tags.is_array()) throw std::runtime_error("tags must be an array");
        std::unordered_set<std::string> unique;
        for (const auto& tag_source : tags) {
            const std::string tag = tag_source.get<std::string>();
            if (tag.empty() || !unique.insert(tag).second)
                throw std::runtime_error("tags must be non-empty and unique");
            result->tags.push_back(tag);
        }
    }

    const auto& collision = source.at("collision");
    result->collision.radius_x = finite_number(collision.at("radius_x"), "collision.radius_x");
    result->collision.radius_y = finite_number(collision.at("radius_y"), "collision.radius_y");
    result->collision.height = finite_number(collision.at("height"), "collision.height");
    if (result->collision.radius_x <= 0.f || result->collision.radius_y <= 0.f ||
        result->collision.height <= 0.f)
        throw std::runtime_error("collision dimensions must be positive");

    if (schema == 2) {
        result->lifecycle.recycle_after_death = true;
        result->movement.speed = finite_number(
            source.at("movement").at("speed"), "movement.speed");
        if (result->movement.speed <= 0.f)
            throw std::runtime_error("movement.speed must be positive");
        const auto& lifecycle = source.at("lifecycle");
        result->lifecycle.death_duration_seconds = non_negative(
            lifecycle.at("death_duration_seconds"), "lifecycle.death_duration_seconds");
        result->lifecycle.disappear_duration_seconds = non_negative(
            lifecycle.at("disappear_duration_seconds"), "lifecycle.disappear_duration_seconds");
    }

    if (source.contains("target_acquisition")) {
        const auto& acquisition = source.at("target_acquisition");
        if (!acquisition.is_object())
            throw std::runtime_error("target_acquisition must be an object");
        result->target_acquisition.strategy = target_acquisition_type(
            acquisition.value("strategy_id", std::string(
                aoe_target_acquisition_name(
                    result->target_acquisition.strategy))));
        result->target_acquisition.radius = acquisition.contains("radius")
            ? non_negative(acquisition.at("radius"), "target_acquisition.radius")
            : result->target_acquisition.radius;
        result->target_acquisition.disengage_radius =
            acquisition.contains("disengage_radius")
                ? non_negative(acquisition.at("disengage_radius"),
                               "target_acquisition.disengage_radius")
                : result->target_acquisition.disengage_radius;
        if (result->target_acquisition.disengage_radius <
            result->target_acquisition.radius)
            throw std::runtime_error(
                "target_acquisition.disengage_radius must be at least radius");
    }

    if (source.contains("attack")) {
        const auto& attack = source.at("attack");
        AttackDefinition value;
        const std::string mode = attack.at("mode").get<std::string>();
        if (mode == "melee") value.mode = AttackMode::Melee;
        else if (mode == "projectile") value.mode = AttackMode::Projectile;
        else throw std::runtime_error("attack.mode must be melee or projectile");
        value.damage = typed_amounts(attack.at("damage"), "attack.damage");
        value.range = non_negative(attack.at("range"), "attack.range");
        value.release_seconds = non_negative(attack.at("release_seconds"), "attack.release_seconds");
        value.animation_duration_seconds = non_negative(
            attack.at("animation_duration_seconds"), "attack.animation_duration_seconds");
        value.cooldown_seconds = non_negative(
            attack.at("cooldown_seconds"), "attack.cooldown_seconds");
        value.critical_chance = finite_number(attack.at("critical_chance"), "attack.critical_chance");
        value.critical_multiplier = finite_number(
            attack.at("critical_multiplier"), "attack.critical_multiplier");
        value.projectile_id = attack.value("projectile_id", std::string{});
        if (attack.contains("projectile_launch_offset")) {
            const auto& offset = attack.at("projectile_launch_offset");
            if (!offset.is_object() || !offset.contains("x") ||
                !offset.contains("y") || !offset.contains("z"))
                throw std::runtime_error(
                    "attack.projectile_launch_offset must contain x/y/z");
            value.projectile_launch_offset = glm::vec3{
                finite_number(offset.at("x"), "attack.projectile_launch_offset.x"),
                finite_number(offset.at("y"), "attack.projectile_launch_offset.y"),
                finite_number(offset.at("z"), "attack.projectile_launch_offset.z")};
            if (value.projectile_launch_offset->z < 0.f)
                throw std::runtime_error(
                    "attack.projectile_launch_offset.z must be non-negative");
        }
        if (value.release_seconds > value.animation_duration_seconds ||
            value.animation_duration_seconds > value.cooldown_seconds)
            throw std::runtime_error("attack timing must satisfy release <= duration <= cooldown");
        if (value.critical_chance < 0.f || value.critical_chance > 1.f)
            throw std::runtime_error("critical_chance must be in 0..1");
        if (value.critical_multiplier < 1.f)
            throw std::runtime_error("critical_multiplier must be at least one");
        if (value.mode == AttackMode::Projectile && value.projectile_id.empty())
            throw std::runtime_error("projectile attack requires projectile_id");
        result->attack = std::move(value);
    }

    const auto& presentation = source.at("presentation");
    result->presentation.backend = presentation.at("backend").get<std::string>();
    result->presentation.resource_id = presentation.at("resource_id").get<std::string>();
    result->presentation.default_player_color = presentation.value("default_player_color", 1);
    if (result->presentation.backend.empty() || result->presentation.resource_id.empty() ||
        result->presentation.default_player_color < 1 ||
        result->presentation.default_player_color > 8)
        throw std::runtime_error("invalid presentation backend/resource/player color");
    const auto& animations = presentation.at("animations");
    if (!animations.is_object()) throw std::runtime_error("presentation.animations must be an object");
    for (auto it = animations.begin(); it != animations.end(); ++it) {
        const std::string name = it.value().get<std::string>();
        if (name.empty()) throw std::runtime_error("animation names must not be empty");
        result->presentation.animations[it.key()] = name;
    }
    if (result->presentation.animation("idle").empty())
        throw std::runtime_error("presentation idle animation is required");
    if (result->attack && result->presentation.animation("attack").empty())
        throw std::runtime_error("presentation attack animation is required");
    if (schema == 2 && result->lifecycle.death_duration_seconds > 0.f &&
        result->presentation.animation("death").empty())
        throw std::runtime_error("schema 2 lifecycle requires death animation");
    if (schema == 2 && result->lifecycle.disappear_duration_seconds > 0.f &&
        result->presentation.animation("disappear").empty())
        throw std::runtime_error("schema 2 disappear lifecycle requires disappear animation");
    return result;
}

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

bool squad_member_valid(const entt::registry& reg, const AoeUnitTarget& member) {
    if (!target_valid(reg, member)) return false;
    return reg.all_of<AoeMovement, AoeFacing, AoeTeam>(member.entity);
}

void detach_squad_member(entt::registry& reg, entt::entity entity) {
    const auto* membership = reg.try_get<AoeSquadMember>(entity);
    if (!membership) return;
    const auto squad = membership->squad;
    if (reg.valid(squad)) {
        if (auto* members = reg.try_get<AoeSquadMembers>(squad))
            std::erase_if(members->active,
                [entity](const AoeUnitTarget& value) { return value.entity == entity; });
        if (auto* formation = reg.try_get<AoeSquadFormation>(squad)) {
            formation->dirty = true;
            std::erase_if(formation->slots,
                [entity](const AoeFormationSlot& slot) {
                    return slot.unit.entity == entity;
                });
        }
    }
    reg.remove<AoeSquadMember>(entity);
    reg.remove<AoeSquadMoveSpeedLimit>(entity);
}

bool is_order_command(AoeCommandType type) {
    return type == AoeCommandType::AttackTarget ||
           type == AoeCommandType::AttackMove ||
           type == AoeCommandType::MoveTo ||
           type == AoeCommandType::Stop;
}

void reset_member_action(entt::registry& reg, entt::entity entity,
                         std::uint64_t tick,
                         bool reset_locomotion = true) {
    auto* state = reg.try_get<AoeActionState>(entity);
    if (!state || is_terminal(state->state)) return;
    const bool changed = state->state != UnitState::Idle;
    state->state = UnitState::Idle;
    state->state_started_tick = tick;
    state->critical = false;
    state->release_emitted = false;
    if (reset_locomotion) {
        if (auto* locomotion = reg.try_get<AoeLocomotionState>(entity)) {
            locomotion->velocity = {0.f, 0.f};
            locomotion->actual_speed = 0.f;
        }
    }
    if (changed) ++state->sequence;
}

void stop_squad_member(entt::registry& reg, entt::entity entity,
                       std::uint64_t tick, bool remove_limit = true) {
    cancel_orders(reg, entity);
    if (remove_limit) reg.remove<AoeSquadMoveSpeedLimit>(entity);
    reset_member_action(reg, entity, tick);
}

void assign_engagement_approach(entt::registry& reg, entt::entity entity,
                                const AoeUnitTarget& target,
                                const AttackDefinition& attack) {
    const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
    const double seed = static_cast<double>(identity ? identity->instance_id : 0);
    const double turn = std::fmod(seed * 0.6180339887498948482, 1.0);
    const float angle = static_cast<float>(turn * glm::two_pi<double>());
    const float range_factor = attack.mode == AttackMode::Projectile ? .8f : .5f;
    reg.emplace_or_replace<AoeEngagementApproach>(entity,
        AoeEngagementApproach{
            target, {std::cos(angle), std::sin(angle)},
            attack.range * range_factor, target.instance_id});
}

void attack_with_squad_member(entt::registry& reg, entt::entity entity,
                              const AoeUnitTarget& target,
                              std::uint64_t tick) {
    cancel_orders(reg, entity);
    reg.remove<AoeSquadMoveSpeedLimit>(entity);
    const auto* reference = reg.try_get<AoeUnitDefinitionRef>(entity);
    const auto* definition = reference ? reference->value.get() : nullptr;
    if (!definition || !definition->attack) {
        reset_member_action(reg, entity, tick);
        return;
    }
    reg.emplace_or_replace<AoeAttackOrder>(entity, AoeAttackOrder{target});
    assign_engagement_approach(reg, entity, target, *definition->attack);
    reset_member_action(reg, entity, tick);
}

glm::vec2 squad_slot_world(const AoePosition& center,
                           const AoeSquadFormation& formation,
                           const AoeFormationSlot& slot) {
    const glm::vec2 forward = glm::normalize(formation.forward);
    const glm::vec2 right{forward.y, -forward.x};
    return center.value + right * slot.local_offset.x +
           forward * slot.local_offset.y;
}

bool navigation_destination_valid(const AoeLogicMap* map,
                                  glm::vec2 destination,
                                  glm::vec2 clearance) {
    if (!map || !map->valid()) return true;
    if (!map->contains(destination, clearance) ||
        map->position_blocked(destination, clearance))
        return false;
    const auto cell = map->world_to_cell(destination);
    return cell && map->cell_traversable(cell->x, cell->y, clearance);
}

glm::vec2 attack_approach_destination(EcsWorld& world,
                                      entt::entity entity,
                                      const AoeUnitTarget& target) {
    const auto& reg = world.reg();
    const auto& target_position = reg.get<AoePosition>(target.entity);
    const auto* approach = reg.try_get<AoeEngagementApproach>(entity);
    if (!approach || approach->target.entity != target.entity ||
        approach->target.instance_id != target.instance_id)
        return target_position.value;
    glm::vec2 direction = approach->direction;
    if (glm::length(direction) <= Epsilon) direction = {1.f, 0.f};
    else direction = glm::normalize(direction);
    const auto& collider = reg.get<AoeCollider>(entity);
    const auto& target_collider = reg.get<AoeCollider>(target.entity);
    const float radius = aoe_collider_support_radius(target_collider, direction) +
                         aoe_collider_support_radius(collider, -direction) +
                         approach->desired_gap;
    const glm::vec2 clearance{collider.radius_x, collider.radius_y};
    const auto* map = world.try_resource<AoeLogicMap>();
    const glm::vec2 desired = target_position.value + direction * radius;
    if (navigation_destination_valid(map, desired, clearance))
        return desired;

    // Preserve the assigned radial distance, but search deterministic nearby
    // angles when the preferred attack point lies outside the map or in a
    // static obstacle. Alternating sides avoids a global directional bias.
    constexpr float AngleStep = glm::pi<float>() / 8.f;
    const float base_angle = std::atan2(direction.y, direction.x);
    for (int step = 1; step <= 8; ++step)
        for (const int sign : {1, -1}) {
            const float angle = base_angle + sign * step * AngleStep;
            const glm::vec2 candidate = target_position.value +
                glm::vec2(std::cos(angle), std::sin(angle)) * radius;
            if (navigation_destination_valid(map, candidate, clearance))
                return candidate;
        }
    return navigation_destination_valid(map, target_position.value, clearance)
        ? target_position.value : desired;
}

entt::entity ignored_formation_squad(const entt::registry& reg,
                                     entt::entity entity) {
    const auto* member = reg.try_get<AoeSquadMember>(entity);
    if (!member || !reg.valid(member->squad)) return entt::null;
    // A squad may be globally Engaging while some members still follow their
    // formation slots. Only independently approaching attackers stop ignoring
    // their formation flow; a global phase switch must not turn every follower
    // into a dynamic wall for every other follower.
    const auto* attack = reg.try_get<AoeAttackOrder>(entity);
    return attack && target_valid(reg, attack->target)
        ? entt::null : member->squad;
}

glm::vec2 squad_centroid(const entt::registry& reg,
                         const AoeSquadMembers& members,
                         glm::vec2 fallback) {
    glm::vec2 sum{0.f};
    std::size_t count = 0;
    for (const auto& member : members.active)
        if (squad_member_valid(reg, member)) {
            sum += reg.get<AoePosition>(member.entity).value;
            ++count;
        }
    return count ? sum / static_cast<float>(count) : fallback;
}

void clear_squad_member_orders(entt::registry& reg,
                               const AoeSquadMembers& members,
                               std::uint64_t tick) {
    for (const auto& member : members.active)
        if (squad_member_valid(reg, member))
            stop_squad_member(reg, member.entity, tick);
}

bool squad_slots_arrived(const entt::registry& reg, entt::entity squad,
                         float tolerance = .05f) {
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit)) continue;
        if (glm::length(reg.get<AoePosition>(slot.unit.entity).value -
                        squad_slot_world(center, formation, slot)) > tolerance)
            return false;
    }
    return true;
}

bool rebuild_squad_layout(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    auto& formation = reg.get<AoeSquadFormation>(squad);
    auto& members = reg.get<AoeSquadMembers>(squad);
    auto* formations = world.try_resource<AoeFormationRegistry>();
    if (!formations || !formations->contains(formation.type)) return false;
    AoeFormationContext context;
    context.spacing = formation.spacing;
    for (const auto& member : members.active) {
        if (!squad_member_valid(reg, member)) continue;
        const auto* definition = reg.get<AoeUnitDefinitionRef>(member.entity).value.get();
        const auto& membership = reg.get<AoeSquadMember>(member.entity);
        const auto& collider = reg.get<AoeCollider>(member.entity);
        context.members.push_back({member, membership.ordinal,
            definition ? definition->tags : std::vector<std::string>{},
            {collider.radius_x, collider.radius_y}});
    }
    auto slots = formations->layout(formation.type, context);
    if (!context.members.empty() && slots.empty()) return false;
    formation.slots = std::move(slots);
    formation.dirty = false;

    float bound = 0.f;
    float height = 0.f;
    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit)) continue;
        const auto& collider = reg.get<AoeCollider>(slot.unit.entity);
        bound = std::max(bound, glm::length(slot.local_offset) +
            std::max(collider.radius_x, collider.radius_y));
        height = std::max(height, collider.height);
        if (formation.teleport_on_next_layout) {
            const glm::vec2 position = squad_slot_world(
                reg.get<AoePosition>(squad), formation, slot);
            reg.get<AoePosition>(slot.unit.entity).value = position;
            reg.get_or_emplace<AoePositionHistory>(
                slot.unit.entity).previous = position;
            set_facing_toward(reg.get<AoeFacing>(slot.unit.entity), formation.forward);
        }
    }
    reg.emplace_or_replace<AoeCollider>(squad,
        AoeCollider{bound, bound, std::max(height, Epsilon)});
    formation.teleport_on_next_layout = false;
    return true;
}

void handle_squad_layout_failure(EcsWorld& world, entt::entity squad,
                                 std::uint64_t tick) {
    auto& reg = world.reg();
    auto& formation = reg.get<AoeSquadFormation>(squad);
    auto& spawn = reg.get<AoeSquadSpawnState>(squad);
    auto& state = reg.get<AoeSquadState>(squad);
    auto& order = reg.get<AoeSquadOrder>(squad);
    const auto& members = reg.get<AoeSquadMembers>(squad);
    formation.dirty = false;
    reject(world);

    if (!formation.slots.empty()) {
        constexpr std::string_view message =
            "formation layout rejected; retaining previous slots";
        if (std::find(spawn.errors.begin(), spawn.errors.end(), message) ==
            spawn.errors.end())
            spawn.errors.emplace_back(message);
        return;
    }

    constexpr std::string_view message = "formation layout failed";
    if (std::find(spawn.errors.begin(), spawn.errors.end(), message) ==
        spawn.errors.end())
        spawn.errors.emplace_back(message);
    clear_squad_member_orders(reg, members, tick);
    reg.remove<AoeNavigationPath>(squad);
    order = {};
    spawn.status = AoeSquadSpawnStatus::Failed;
    state.phase = AoeSquadPhase::Failed;
}

bool slot_destination_valid(const AoeLogicMap* map, glm::vec2 destination,
                            glm::vec2 clearance) {
    return navigation_destination_valid(map, destination, clearance);
}

glm::vec2 resolve_elastic_slot_destination(
    EcsWorld& world, entt::entity squad, entt::entity entity,
    const AoeFormationSlot& slot, glm::vec2 original) {
    auto& reg = world.reg();
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    const auto& center = reg.get<AoePosition>(squad);
    const auto& collider = reg.get<AoeCollider>(entity);
    const glm::vec2 clearance{collider.radius_x, collider.radius_y};
    glm::vec2 forward = formation.forward;
    if (glm::length(forward) <= Epsilon) forward = {1.f, 0.f};
    else forward = glm::normalize(forward);
    const glm::vec2 right{forward.y, -forward.x};
    const auto* traffic = reg.try_get<AoeSquadTrafficState>(squad);
    const float lateral = traffic ? traffic->lateral_offset : 0.f;
    const glm::vec2 desired = original + right * lateral;
    auto& follow = reg.get_or_emplace<AoeSquadSlotFollowState>(entity);
    follow.original_destination = original;

    if (slot_destination_valid(map, desired, clearance)) {
        if (follow.elastic) {
            const auto& settings = world.resource_or_add<AoeNavigationSettings>();
            if (++follow.recovery_ticks < settings.formation_slot_recovery_ticks)
                return follow.navigation_destination;
        }
        follow.elastic = false;
        follow.recovery_ticks = 0;
        follow.navigation_destination = desired;
        return desired;
    }

    // Keep slot ordering, first looking ahead along the common squad flow,
    // then progressively compressing only the lateral formation offset.
    // The fixed candidate set keeps the hot path allocation-free.
    const float probe = std::max(.5f, std::max(clearance.x, clearance.y) * 2.f);
    const std::array<glm::vec2, 10> candidates{{
        desired + forward * probe,
        desired + forward * probe * 2.f,
        desired + forward * probe * 3.f,
        center.value + right * (slot.local_offset.x * .75f + lateral) +
            forward * slot.local_offset.y,
        center.value + right * (slot.local_offset.x * .5f + lateral) +
            forward * slot.local_offset.y,
        center.value + right * (slot.local_offset.x * .25f + lateral) +
            forward * slot.local_offset.y,
        center.value + right * lateral + forward * slot.local_offset.y,
        desired - forward * probe,
        center.value + right * lateral,
        center.value
    }};
    for (const auto candidate : candidates)
        if (slot_destination_valid(map, candidate, clearance)) {
            follow.elastic = true;
            follow.recovery_ticks = 0;
            follow.navigation_destination = candidate;
            ++world.resource_or_add<AoeGameplayDiagnostics>().elastic_slot_uses;
            return candidate;
        }

    // Keeping the current valid position is preferable to repeatedly asking
    // A* for an invalid original slot. The formation will retry its desired
    // slot on subsequent fixed ticks as the squad center moves.
    const glm::vec2 current = reg.get<AoePosition>(entity).value;
    if (slot_destination_valid(map, current, clearance)) {
        follow.elastic = true;
        follow.recovery_ticks = 0;
        follow.navigation_destination = current;
        ++world.resource_or_add<AoeGameplayDiagnostics>().elastic_slot_uses;
        return current;
    }

    follow.elastic = true;
    follow.recovery_ticks = 0;
    follow.navigation_destination = original;
    ++world.resource_or_add<AoeGameplayDiagnostics>().elastic_slot_uses;
    return original;
}

void drive_squad_slots(EcsWorld& world, entt::entity squad,
                       float speed_limit, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    const auto* traffic = reg.try_get<AoeSquadTrafficState>(squad);
    const float traffic_speed = traffic
        ? std::clamp(traffic->speed_scale, 0.f, 1.f) : 1.f;
    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit)) continue;
        const auto entity = slot.unit.entity;
        if (const auto* attack = reg.try_get<AoeAttackOrder>(entity);
            attack && target_valid(reg, attack->target))
            continue;
        const glm::vec2 original = squad_slot_world(center, formation, slot);
        const glm::vec2 destination = resolve_elastic_slot_destination(
            world, squad, entity, slot, original);
        reg.remove<AoeAttackOrder, AoeAttackMoveOrder>(entity);
        reg.emplace_or_replace<AoeSquadMoveSpeedLimit>(entity,
            AoeSquadMoveSpeedLimit{speed_limit * traffic_speed});
        auto& goal = reg.emplace_or_replace<AoeMoveGoal>(entity,
            AoeMoveGoal{destination, 0.f, {}});
        (void)goal;
        auto* path = reg.try_get<AoeNavigationPath>(entity);
        if (!path || path->waypoints.empty())
            reg.emplace_or_replace<AoeNavigationPath>(entity,
                AoeNavigationPath{{destination}, 0});
        else
            path->current = std::min(path->current, path->waypoints.size() - 1);
        auto& state = reg.get<AoeActionState>(entity);
        if (state.state == UnitState::Attacking) reset_member_action(reg, entity, tick);
    }
}

void apply_command(EcsWorld& world, const AoeGameplayCommand& command, std::uint64_t tick) {
    auto& reg = world.reg();
    if (!reg.valid(command.unit) || reg.any_of<AoePooledUnit, AoeRecyclePending>(command.unit)) {
        reject(world); return;
    }
    if (is_order_command(command.type) && reg.all_of<AoeSquadMember>(command.unit))
        detach_squad_member(reg, command.unit);
    auto* state = reg.try_get<AoeActionState>(command.unit);
    if (!state) { reject(world); return; }
    if (command.type == AoeCommandType::SetHealth) {
        auto* health = reg.try_get<AoeHealth>(command.unit);
        if (!health) { reject(world); return; }
        health->current = std::clamp(command.number, 0.f, health->maximum);
        if (health->current <= 0.f) begin_death(world, command.unit, tick);
        return;
    }
    if (is_terminal(state->state)) { reject(world); return; }
    if (command.type == AoeCommandType::SetLevel) {
        reg.get<AoeLevel>(command.unit).value = static_cast<std::uint32_t>(command.integer);
        return;
    }
    if (command.type == AoeCommandType::SetFacing) {
        auto& facing = reg.get<AoeFacing>(command.unit);
        facing.direction_count = command.integer2;
        facing.direction = static_cast<int>(((command.integer % command.integer2) + command.integer2) % command.integer2);
        return;
    }
    if (command.type == AoeCommandType::Stop) {
        cancel_orders(reg, command.unit);
        state->state = UnitState::Idle;
        state->state_started_tick = tick;
        state->critical = false;
        state->release_emitted = false;
        ++state->sequence;
        return;
    }
    if (command.type == AoeCommandType::MoveTo) {
        cancel_orders(reg, command.unit);
        reg.emplace_or_replace<AoeMoveGoal>(command.unit,
            AoeMoveGoal{command.position, 0.f, {}});
        reg.emplace_or_replace<AoeNavigationPath>(command.unit,
            AoeNavigationPath{{command.position}, 0});
        state->state = UnitState::Moving;
        state->state_started_tick = tick;
        state->critical = false;
        state->release_emitted = false;
        ++state->sequence;
        return;
    }
    if (command.type == AoeCommandType::AttackMove) {
        const auto* reference = reg.try_get<AoeUnitDefinitionRef>(command.unit);
        const auto* definition = reference ? reference->value.get() : nullptr;
        if (!definition || !definition->attack ||
            !reg.all_of<AoePosition, AoeCollider, AoeMovement, AoeFacing, AoeTeam>(
                command.unit)) {
            reject(world); return;
        }
        cancel_orders(reg, command.unit);
        reg.emplace_or_replace<AoeAttackMoveOrder>(command.unit,
            AoeAttackMoveOrder{command.position});
        reg.emplace_or_replace<AoeMoveGoal>(command.unit,
            AoeMoveGoal{command.position, 0.f, {}});
        reg.emplace_or_replace<AoeNavigationPath>(command.unit,
            AoeNavigationPath{{command.position}, 0});
        state->state = UnitState::Moving;
        state->state_started_tick = tick;
        state->critical = false;
        state->release_emitted = false;
        ++state->sequence;
        return;
    }
    if (command.type != AoeCommandType::AttackTarget || command.unit == command.target.entity ||
        !target_valid(reg, command.target)) {
        reject(world); return;
    }
    const auto* reference = reg.try_get<AoeUnitDefinitionRef>(command.unit);
    const auto* definition = reference ? reference->value.get() : nullptr;
    if (!definition || !definition->attack ||
        !reg.all_of<AoePosition, AoeCollider, AoeMovement, AoeFacing>(command.unit)) {
        reject(world); return;
    }
    attack_with_squad_member(reg, command.unit, command.target, tick);
}

void attack_move_acquisition_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    std::vector<entt::entity> finish;
    for (const auto entity : reg.view<AoeAttackMoveOrder, AoePosition, AoeCollider,
                                      AoeActionState, AoeUnitDefinitionRef,
                                      AoeTeam>(entt::exclude<AoePooledUnit,
                                                            AoeRecyclePending>)) {
        auto& state = reg.get<AoeActionState>(entity);
        if (is_terminal(state.state)) continue;
        const auto* definition = reg.get<AoeUnitDefinitionRef>(entity).value.get();
        if (!definition || !definition->attack) {
            finish.push_back(entity);
            reject(world);
            continue;
        }

        std::optional<AoeUnitTarget> excluded;
        bool keep_if_no_replacement = false;
        if (const auto* attack = reg.try_get<AoeAttackOrder>(entity)) {
            if (target_valid(reg, attack->target)) {
                auto* approach = reg.try_get<AoeEngagementApproach>(entity);
                if (!approach ||
                    approach->target.entity != attack->target.entity ||
                    approach->target.instance_id != attack->target.instance_id) {
                    assign_engagement_approach(reg, entity, attack->target,
                                               *definition->attack);
                    approach = reg.try_get<AoeEngagementApproach>(entity);
                }
                if (state.state == UnitState::Attacking) {
                    if (approach) approach->unreachable_ticks = 0;
                    continue;
                }
                const float gap = aoe_surface_gap(
                    reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
                    reg.get<AoePosition>(attack->target.entity),
                    reg.get<AoeCollider>(attack->target.entity));
                if (gap <= definition->target_acquisition.disengage_radius +
                               Epsilon) {
                    const auto* path = reg.try_get<AoeNavigationPath>(entity);
                    if (gap <= definition->attack->range + Epsilon ||
                        !path || !path->no_path) {
                        if (approach) approach->unreachable_ticks = 0;
                        continue;
                    }
                    if (approach && approach->unreachable_ticks <
                                        std::numeric_limits<std::uint32_t>::max())
                        ++approach->unreachable_ticks;
                    if (!approach || approach->unreachable_ticks <
                            std::max(1u, navigation.blocked_repath_ticks))
                        continue;
                    excluded = attack->target;
                    keep_if_no_replacement = true;
                }
            }
            if (!keep_if_no_replacement) {
                clear_active_engagement(reg, entity);
                set_idle_if_active(reg, entity, tick);
            }
        }

        const auto& position = reg.get<AoePosition>(entity);
        const auto& acquisition = definition->target_acquisition;
        const std::span<const AoeUnitTarget> excluded_span = excluded
            ? std::span<const AoeUnitTarget>(&*excluded, 1)
            : std::span<const AoeUnitTarget>{};
        auto target = dispatch_aoe_target(acquisition.strategy, world,
            AoeTargetAcquisitionContext{
                .seeker = entity,
                .origin = position.value,
                .radius = acquisition.radius,
                .seeker_team = reg.get<AoeTeam>(entity).id,
                .excluded = excluded_span});
        if (target && (target->entity == entity || !target_valid(reg, *target)))
            target.reset();
        if (target) {
            if (keep_if_no_replacement)
                clear_active_engagement(reg, entity);
            reg.remove<AoeMoveGoal>(entity);
            reg.remove<AoeNavigationPath>(entity);
            reg.emplace_or_replace<AoeAttackOrder>(entity,
                AoeAttackOrder{*target});
            assign_engagement_approach(reg, entity, *target,
                                       *definition->attack);
            set_idle_if_active(reg, entity, tick);
            continue;
        }
        if (keep_if_no_replacement) continue;

        const auto destination = reg.get<AoeAttackMoveOrder>(entity).destination;
        if (glm::length(destination - position.value) <= Epsilon) {
            finish.push_back(entity);
            continue;
        }
        const auto* goal = reg.try_get<AoeMoveGoal>(entity);
        if (!goal || goal->target.entity != entt::null ||
            glm::length(goal->destination - destination) > Epsilon ||
            !reg.all_of<AoeNavigationPath>(entity)) {
            reg.emplace_or_replace<AoeMoveGoal>(entity,
                AoeMoveGoal{destination, 0.f, {}});
            reg.emplace_or_replace<AoeNavigationPath>(entity,
                AoeNavigationPath{{destination}, 0});
        }
    }
    for (const auto entity : finish) {
        if (!reg.valid(entity)) continue;
        cancel_orders(reg, entity);
        set_idle_if_active(reg, entity, tick);
    }
}

void squad_spawn_resolution_tick(EcsWorld& world) {
    auto& reg = world.reg();
    std::vector<entt::entity> failed_entities;
    for (const auto squad : reg.view<AoeSquadMembers, AoeSquadSpawnState,
                                     AoeSquadFormation, AoeSquadState>()) {
        auto& members = reg.get<AoeSquadMembers>(squad);
        auto& spawn = reg.get<AoeSquadSpawnState>(squad);
        if (spawn.status != AoeSquadSpawnStatus::Pending) continue;
        std::vector<AoeSquadPendingMember> still_pending;
        for (auto& pending : members.pending) {
            if (!reg.valid(pending.entity)) {
                ++spawn.failed;
                spawn.errors.push_back(pending.definition_id + ": entity destroyed");
                continue;
            }
            if (const auto* error = reg.try_get<AoeGameplaySpawnError>(pending.entity)) {
                ++spawn.failed;
                spawn.errors.push_back(pending.definition_id + ": " + error->message);
                failed_entities.push_back(pending.entity);
                continue;
            }
            const auto* identity = reg.try_get<AoeGameplayIdentity>(pending.entity);
            if (!identity || reg.all_of<AoeGameplaySpawnRequest>(pending.entity)) {
                still_pending.push_back(std::move(pending));
                continue;
            }
            const AoeUnitTarget target{pending.entity, identity->instance_id};
            members.active.push_back(target);
            reg.emplace_or_replace<AoeSquadMember>(pending.entity,
                AoeSquadMember{squad, pending.ordinal});
            ++spawn.succeeded;
        }
        members.pending = std::move(still_pending);
        if (!members.pending.empty()) continue;
        if (!spawn.succeeded) {
            spawn.status = AoeSquadSpawnStatus::Failed;
            reg.get<AoeSquadState>(squad).phase = AoeSquadPhase::Failed;
        } else {
            spawn.status = spawn.failed ? AoeSquadSpawnStatus::Partial
                                        : AoeSquadSpawnStatus::Ready;
            reg.get<AoeSquadFormation>(squad).dirty = true;
            reg.get<AoeSquadState>(squad).phase = AoeSquadPhase::Forming;
        }
    }
    std::sort(failed_entities.begin(), failed_entities.end());
    failed_entities.erase(std::unique(failed_entities.begin(), failed_entities.end()),
                          failed_entities.end());
    for (const auto entity : failed_entities)
        if (reg.valid(entity)) reg.destroy(entity);
}

void squad_membership_cleanup_tick(EcsWorld& world) {
    auto& reg = world.reg();
    for (const auto squad : reg.view<AoeSquadMembers, AoeSquadSpawnState,
                                     AoeSquadFormation, AoeSquadState>()) {
        auto& members = reg.get<AoeSquadMembers>(squad);
        auto& formation = reg.get<AoeSquadFormation>(squad);
        const auto old_size = members.active.size();
        std::erase_if(members.active, [&](const AoeUnitTarget& member) {
            const auto* membership = reg.valid(member.entity)
                ? reg.try_get<AoeSquadMember>(member.entity) : nullptr;
            const bool remove = !squad_member_valid(reg, member) ||
                !membership || membership->squad != squad;
            if (remove && reg.valid(member.entity)) {
                reg.remove<AoeSquadMember>(member.entity);
                reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
            }
            return remove;
        });
        if (members.active.size() != old_size) formation.dirty = true;
        if (members.active.empty() && members.pending.empty() &&
            reg.get<AoeSquadSpawnState>(squad).status != AoeSquadSpawnStatus::Failed) {
            reg.get<AoeSquadSpawnState>(squad).status = AoeSquadSpawnStatus::Empty;
            reg.get<AoeSquadState>(squad).phase = AoeSquadPhase::Empty;
            reg.get<AoeSquadOrder>(squad) = {};
            formation.slots.clear();
        }
    }
}

void apply_squad_command(EcsWorld& world, const AoeSquadCommand& command,
                         std::uint64_t tick) {
    auto& reg = world.reg();
    if (!reg.valid(command.squad) ||
        !reg.all_of<AoeSquadMembers, AoeSquadSpawnState, AoeSquadFormation,
                    AoeSquadOrder, AoeSquadState, AoePosition>(command.squad)) {
        reject(world); return;
    }
    auto& spawn = reg.get<AoeSquadSpawnState>(command.squad);
    if (spawn.status == AoeSquadSpawnStatus::Failed ||
        spawn.status == AoeSquadSpawnStatus::Empty) {
        reject(world); return;
    }
    auto& members = reg.get<AoeSquadMembers>(command.squad);
    auto& formation = reg.get<AoeSquadFormation>(command.squad);
    auto& order = reg.get<AoeSquadOrder>(command.squad);
    auto& state = reg.get<AoeSquadState>(command.squad);
    if (command.type == AoeSquadCommandType::SetFormation) {
        const auto* registry = world.try_resource<AoeFormationRegistry>();
        if (!registry || !registry->contains(command.formation)) {
            reject(world); return;
        }
        formation.type = command.formation;
        formation.dirty = true;
        if (state.phase != AoeSquadPhase::Engaging)
            state.phase = AoeSquadPhase::Regrouping;
        return;
    }
    clear_squad_member_orders(reg, members, tick);
    reg.remove<AoeNavigationPath>(command.squad);
    if (command.type == AoeSquadCommandType::Stop) {
        reg.get<AoePosition>(command.squad).value = squad_centroid(
            reg, members, reg.get<AoePosition>(command.squad).value);
        order = {};
        state.phase = AoeSquadPhase::Idle;
        return;
    }
    if (command.type == AoeSquadCommandType::AttackTarget) {
        if (!target_valid(reg, command.target)) { reject(world); return; }
        order = {AoeSquadOrderType::AttackTarget, {}, command.target};
        state.phase = AoeSquadPhase::Engaging;
        return;
    }
    const auto& center = reg.get<AoePosition>(command.squad);
    const glm::vec2 delta = command.position - center.value;
    if (glm::length(delta) > Epsilon) formation.forward = glm::normalize(delta);
    order = {command.type == AoeSquadCommandType::AttackMove
                 ? AoeSquadOrderType::AttackMove : AoeSquadOrderType::MoveTo,
             command.position, {}};
    state.phase = AoeSquadPhase::Regrouping;
}

void squad_command_tick(EcsWorld& world, std::uint64_t tick) {
    auto* queue = world.try_resource<AoeSquadCommands>();
    if (!queue) return;
    auto commands = std::move(queue->queue);
    queue->queue.clear();
    for (const auto& command : commands) {
        if (world.reg().valid(command.squad)) {
            if (const auto* spawn = world.reg().try_get<AoeSquadSpawnState>(command.squad);
                spawn && spawn->status == AoeSquadSpawnStatus::Pending) {
                queue->queue.push_back(command);
                continue;
            }
        }
        apply_squad_command(world, command, tick);
    }
}

void engage_squad_target(EcsWorld& world, entt::entity squad,
                         const AoeUnitTarget& target, std::uint64_t tick) {
    auto& reg = world.reg();
    auto& members = reg.get<AoeSquadMembers>(squad);
    for (const auto& member : members.active) {
        if (!squad_member_valid(reg, member)) continue;
        const auto* current = reg.try_get<AoeAttackOrder>(member.entity);
        if (current && current->target.entity == target.entity &&
            current->target.instance_id == target.instance_id) continue;
        attack_with_squad_member(reg, member.entity, target, tick);
    }
    reg.get<AoeSquadState>(squad).phase = AoeSquadPhase::Engaging;
}

bool target_within_squad_radius(
    const entt::registry& reg, const AoeSquadMembers& members,
    const AoeUnitTarget& target, float radius) {
    if (!target_valid(reg, target) ||
        !reg.all_of<AoePosition, AoeCollider>(target.entity))
        return false;
    for (const auto& member : members.active) {
        if (!squad_member_valid(reg, member) ||
            !reg.all_of<AoePosition, AoeCollider>(member.entity))
            continue;
        if (aoe_surface_gap(
                reg.get<AoePosition>(member.entity),
                reg.get<AoeCollider>(member.entity),
                reg.get<AoePosition>(target.entity),
                reg.get<AoeCollider>(target.entity)) <= radius + Epsilon)
            return true;
    }
    return false;
}

std::vector<AoeUnitTarget> collect_squad_targets(
    EcsWorld& world, const AoeSquadMembers& members,
    std::uint32_t seeker_team, float radius) {
    auto& reg = world.reg();
    std::vector<AoeUnitTarget> result;
    glm::vec2 low{std::numeric_limits<float>::infinity()};
    glm::vec2 high{-std::numeric_limits<float>::infinity()};
    bool have_member = false;
    for (const auto& member : members.active) {
        if (!squad_member_valid(reg, member) ||
            !reg.all_of<AoePosition, AoeCollider>(member.entity))
            continue;
        const auto& position = reg.get<AoePosition>(member.entity);
        const auto& collider = reg.get<AoeCollider>(member.entity);
        const glm::vec2 radii{collider.radius_x, collider.radius_y};
        low = glm::min(low, position.value - radii);
        high = glm::max(high, position.value + radii);
        have_member = true;
    }
    if (!have_member) return result;

    const auto consider = [&](entt::entity entity,
                              std::uint64_t instance_id) {
        if (!reg.valid(entity) ||
            !reg.all_of<AoeTeam, AoePosition, AoeCollider, AoeHealth,
                        AoeActionState, AoeGameplayIdentity,
                        AoeUnitDefinitionRef>(entity) ||
            reg.get<AoeTeam>(entity).id == seeker_team)
            return;
        const AoeUnitTarget target{entity, instance_id};
        if (!target_valid(reg, target) ||
            !target_within_squad_radius(reg, members, target, radius))
            return;
        result.push_back(target);
    };

    const auto* map = world.try_resource<AoeLogicMap>();
    const auto* dynamic = world.try_resource<AoeDynamicObstacleIndex>();
    if (map && map->valid() && dynamic) {
        dynamic->query(*map, low - glm::vec2(radius),
                       high + glm::vec2(radius),
            [&](const AoeDynamicObstacleEntry& entry) {
                consider(entry.entity, entry.instance_id);
            });
    } else {
        for (const auto entity : reg.view<
                 AoeTeam, AoePosition, AoeCollider, AoeHealth,
                 AoeActionState, AoeGameplayIdentity, AoeUnitDefinitionRef>(
                 entt::exclude<AoePooledUnit, AoeRecyclePending>))
            consider(entity,
                reg.get<AoeGameplayIdentity>(entity).instance_id);
    }
    return result;
}

std::optional<AoeUnitTarget> select_member_target(
    EcsWorld& world, entt::entity entity,
    AoeTargetAcquisitionType acquisition,
    std::span<const AoeUnitTarget> candidates,
    std::span<const AoeUnitTarget> excluded = {}) {
    auto& reg = world.reg();
    const auto* reference = reg.try_get<AoeUnitDefinitionRef>(entity);
    const auto* definition = reference ? reference->value.get() : nullptr;
    if (!definition || !definition->attack ||
        !reg.all_of<AoePosition, AoeTeam>(entity))
        return std::nullopt;
    auto target = dispatch_aoe_target(acquisition, world,
        AoeTargetAcquisitionContext{
            .seeker = entity,
            .origin = reg.get<AoePosition>(entity).value,
            .seeker_team = reg.get<AoeTeam>(entity).id,
            .excluded = excluded,
            .candidates = candidates,
            .use_candidates = true});
    if (!target || !target_valid(reg, *target)) return std::nullopt;
    return target;
}

bool update_squad_attack_move_engagement(
    EcsWorld& world, entt::entity squad, std::uint64_t tick) {
    auto& reg = world.reg();
    auto& members = reg.get<AoeSquadMembers>(squad);
    const auto& settings = reg.get<AoeSquadCombatSettings>(squad);
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    const auto candidates = collect_squad_targets(
        world, members, reg.get<AoeTeam>(squad).id,
        settings.acquisition_radius);
    bool active = false;
    for (const auto& member : members.active) {
        if (!squad_member_valid(reg, member)) continue;
        if (const auto* current = reg.try_get<AoeAttackOrder>(member.entity);
            current && target_valid(reg, current->target)) {
            const auto& action = reg.get<AoeActionState>(member.entity);
            const auto* definition = reg.get<AoeUnitDefinitionRef>(
                member.entity).value.get();
            if (definition && definition->attack &&
                (action.state == UnitState::Attacking ||
                 target_within_squad_radius(
                     reg, members, current->target,
                     settings.disengage_radius))) {
                auto* approach = reg.try_get<AoeEngagementApproach>(
                    member.entity);
                if (!approach ||
                    approach->target.entity != current->target.entity ||
                    approach->target.instance_id !=
                        current->target.instance_id) {
                    assign_engagement_approach(reg, member.entity,
                                               current->target,
                                               *definition->attack);
                    approach = reg.try_get<AoeEngagementApproach>(
                        member.entity);
                }
                const auto* path = reg.try_get<AoeNavigationPath>(
                    member.entity);
                const bool unreachable = action.state != UnitState::Attacking &&
                    path && path->no_path;
                if (approach) {
                    if (unreachable && approach->unreachable_ticks <
                                           std::numeric_limits<std::uint32_t>::max())
                        ++approach->unreachable_ticks;
                    else if (!unreachable)
                        approach->unreachable_ticks = 0;
                }
                const bool replace_unreachable = approach && unreachable &&
                    approach->unreachable_ticks >=
                        std::max(1u, navigation.blocked_repath_ticks);
                if (replace_unreachable) {
                    const std::array excluded{current->target};
                    auto replacement = select_member_target(
                        world, member.entity, settings.acquisition_strategy,
                        candidates, excluded);
                    if (replacement) {
                        clear_active_engagement(reg, member.entity);
                        attack_with_squad_member(
                            reg, member.entity, *replacement, tick);
                    }
                }
                active = true;
                continue;
            }
        }
        clear_active_engagement(reg, member.entity);
        auto target = select_member_target(
            world, member.entity, settings.acquisition_strategy, candidates);
        if (!target || !target_valid(reg, *target)) {
            if (reg.get<AoeActionState>(member.entity).state ==
                UnitState::Attacking)
                reset_member_action(reg, member.entity, tick, false);
            continue;
        }
        attack_with_squad_member(reg, member.entity, *target, tick);
        active = true;
    }
    if (active)
        reg.get<AoeSquadState>(squad).phase = AoeSquadPhase::Engaging;
    return active;
}

void squad_control_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
    for (const auto squad : reg.view<AoeSquadMembers, AoeSquadSpawnState,
                                     AoeSquadFormation, AoeSquadCombatSettings,
                                     AoeSquadOrder, AoeSquadState, AoePosition,
                                     AoeCollider, AoeTeam>()) {
        auto& spawn = reg.get<AoeSquadSpawnState>(squad);
        if (spawn.status == AoeSquadSpawnStatus::Pending ||
            spawn.status == AoeSquadSpawnStatus::Failed ||
            spawn.status == AoeSquadSpawnStatus::Empty) continue;
        auto& members = reg.get<AoeSquadMembers>(squad);
        auto& formation = reg.get<AoeSquadFormation>(squad);
        auto& order = reg.get<AoeSquadOrder>(squad);
        auto& state = reg.get<AoeSquadState>(squad);
        auto& center = reg.get<AoePosition>(squad);
        bool acquisition_attempted = false;
        bool attack_move_active = false;

        if (formation.dirty && state.phase != AoeSquadPhase::Engaging) {
            if (!rebuild_squad_layout(world, squad)) {
                handle_squad_layout_failure(world, squad, tick);
                continue;
            }
            if (state.phase == AoeSquadPhase::Forming)
                state.phase = AoeSquadPhase::Idle;
        }

        state.movement_speed = std::numeric_limits<float>::infinity();
        for (const auto& member : members.active)
            if (squad_member_valid(reg, member))
                state.movement_speed = std::min(state.movement_speed,
                    reg.get<AoeMovement>(member.entity).speed);
        if (!std::isfinite(state.movement_speed)) state.movement_speed = 0.f;

        if (order.type == AoeSquadOrderType::AttackMove &&
            state.phase == AoeSquadPhase::Engaging) {
            acquisition_attempted = true;
            if (update_squad_attack_move_engagement(
                    world, squad, tick)) {
                order.target = {};
                attack_move_active = true;
            } else {
                clear_squad_member_orders(reg, members, tick);
                order.target = {};
                formation.dirty = true;
                state.phase = AoeSquadPhase::Moving;
            }
        }

        if (order.target.entity != entt::null && !target_valid(reg, order.target)) {
            clear_squad_member_orders(reg, members, tick);
            order.target = {};
            center.value = squad_centroid(reg, members, center.value);
            formation.dirty = true;
            reg.remove<AoeNavigationPath>(squad);
            if (order.type == AoeSquadOrderType::AttackTarget) {
                order.type = AoeSquadOrderType::Idle;
                state.phase = AoeSquadPhase::Regrouping;
            } else if (order.type == AoeSquadOrderType::AttackMove) {
                acquisition_attempted = true;
                if (update_squad_attack_move_engagement(
                        world, squad, tick))
                    attack_move_active = true;
                const glm::vec2 delta = order.destination - center.value;
                if (glm::length(delta) > Epsilon)
                    formation.forward = glm::normalize(delta);
                state.phase = AoeSquadPhase::Moving;
            } else {
                state.phase = AoeSquadPhase::Regrouping;
            }
        }

        if (order.target.entity != entt::null) {
            engage_squad_target(world, squad, order.target, tick);
            continue;
        }

        if (formation.dirty) {
            if (!rebuild_squad_layout(world, squad)) {
                handle_squad_layout_failure(world, squad, tick);
                continue;
            }
        }

        if (state.phase == AoeSquadPhase::Regrouping) {
            drive_squad_slots(world, squad, state.movement_speed, tick);
            if (!squad_slots_arrived(reg, squad)) continue;
            state.phase = (order.type == AoeSquadOrderType::MoveTo ||
                           order.type == AoeSquadOrderType::AttackMove)
                ? AoeSquadPhase::Moving : AoeSquadPhase::Idle;
        }

        if (order.type == AoeSquadOrderType::AttackMove &&
            !acquisition_attempted) {
            order.target = {};
            if (update_squad_attack_move_engagement(
                    world, squad, tick))
                attack_move_active = true;
        }

        // Shared squad awareness pauses the Attack Move anchor as soon as the
        // squad engages. Attackers approach their targets independently while
        // any non-combat member holds its current formation slot.
        if (attack_move_active) {
            drive_squad_slots(world, squad, state.movement_speed, tick);
            continue;
        }

        if (order.type == AoeSquadOrderType::MoveTo ||
            order.type == AoeSquadOrderType::AttackMove) {
            auto* guide = reg.try_get<AoeNavigationPath>(squad);
            const auto* map = world.try_resource<AoeLogicMap>();
            const std::uint64_t revision = map ? map->static_revision() : 0;
            const bool needs_guide = !guide ||
                (!guide->no_path && (guide->waypoints.empty() ||
                                     guide->current >= guide->waypoints.size())) ||
                glm::length(guide->requested_goal - order.destination) > Epsilon ||
                guide->map_revision != revision;
            if (needs_guide) {
                const std::uint64_t next_sequence = guide
                    ? guide->request_sequence + 1 : 1;
                glm::vec2 clearance{0.f};
                bool have_member = false;
                for (const auto& member : members.active) {
                    if (!squad_member_valid(reg, member)) continue;
                    const auto& collider = reg.get<AoeCollider>(member.entity);
                    const glm::vec2 radii{collider.radius_x, collider.radius_y};
                    clearance = have_member ? glm::min(clearance, radii) : radii;
                    have_member = true;
                }
                auto& registry = world.resource_or_add<AoePathfinderRegistry>();
                if (!registry.contains("direct"))
                    registry.bind<DirectPathfinderLogic>("direct");
                if (!registry.contains("grid_astar"))
                    registry.bind<GridAStarPathfinderLogic>("grid_astar");
                const auto& navigation =
                    world.resource_or_add<AoeNavigationSettings>();
                const auto result = registry.find(
                    map ? navigation.squad_pathfinder_id : "direct", world,
                    {center.value, order.destination, clearance,
                     squad, squad, entt::null, false});
                auto& value = reg.emplace_or_replace<AoeNavigationPath>(squad);
                value.requested_goal = order.destination;
                value.map_revision = result.map_revision;
                value.request_sequence = next_sequence;
                value.last_repath_tick = tick;
                value.waypoints = result.waypoints;
                value.current = 0;
                value.no_path = result.status != AoePathStatus::Ready ||
                                value.waypoints.empty();
                guide = &value;
                world.resource_or_add<Events<AoeNavigationEvent>>().emit(
                    squad, value.request_sequence,
                    value.no_path ? AoeNavigationEventStatus::NoPath
                                  : AoeNavigationEventStatus::Ready,
                    tick);
            }
            if (guide->no_path) {
                state.phase = AoeSquadPhase::Blocked;
                drive_squad_slots(world, squad, state.movement_speed, tick);
                continue;
            }

            // The guide represents the squad's intent, not a hard tether.
            // Individual members may temporarily fall behind while detouring;
            // freezing the anchor made every other slot stop as well and left
            // the slowest member unable to catch a moving slot.
            if (state.movement_speed > 0.f) {
                const auto* traffic = reg.try_get<AoeSquadTrafficState>(squad);
                const float traffic_speed_scale = traffic
                    ? std::clamp(traffic->speed_scale, 0.f, 1.f) : 1.f;
                while (guide->current < guide->waypoints.size()) {
                    const glm::vec2 delta = guide->waypoints[guide->current] -
                                            center.value;
                    const float distance = glm::length(delta);
                    if (distance <= Epsilon) { ++guide->current; continue; }
                    formation.forward = delta / distance;
                    center.value += formation.forward * std::min(
                        distance, state.movement_speed * traffic_speed_scale * dt);
                    break;
                }
            }
            state.phase = attack_move_active
                ? AoeSquadPhase::Engaging : AoeSquadPhase::Moving;
            drive_squad_slots(world, squad, state.movement_speed, tick);
            if (guide->current >= guide->waypoints.size() &&
                glm::length(order.destination - center.value) <= Epsilon &&
                squad_slots_arrived(reg, squad)) {
                clear_squad_member_orders(reg, members, tick);
                reg.remove<AoeNavigationPath>(squad);
                order = {};
                state.phase = AoeSquadPhase::Idle;
            }
            continue;
        }

        if (state.phase == AoeSquadPhase::Idle) {
            for (const auto& member : members.active)
                if (squad_member_valid(reg, member))
                    reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
        }
    }
}

void command_tick(EcsWorld& world, std::uint64_t tick) {
    auto commands = std::move(world.resource<AoeGameplayCommands>().queue);
    world.resource<AoeGameplayCommands>().queue.clear();
    for (const auto& command : commands) {
        const bool unit_pending = world.reg().valid(command.unit) &&
            world.reg().all_of<AoeGameplaySpawnRequest>(command.unit) &&
            !world.reg().all_of<AoeActionState>(command.unit);
        const bool target_pending = command.type == AoeCommandType::AttackTarget &&
            world.reg().valid(command.target.entity) &&
            world.reg().all_of<AoeGameplaySpawnRequest>(command.target.entity) &&
            !world.reg().all_of<AoeGameplayIdentity>(command.target.entity);
        if (unit_pending || target_pending) {
            world.resource<AoeGameplayCommands>().queue.push_back(command);
            continue;
        }
        apply_command(world, command, tick);
    }
}

bool plan_navigation_path(EcsWorld& world, entt::entity entity,
                          glm::vec2 goal, bool include_dynamic,
                          entt::entity ignored_dynamic_target,
                          std::uint64_t tick) {
    auto& reg = world.reg();
    if (!reg.all_of<AoePosition, AoeCollider>(entity)) return false;
    auto& path = reg.get_or_emplace<AoeNavigationPath>(entity);
    const auto& collider = reg.get<AoeCollider>(entity);
    const entt::entity squad = ignored_formation_squad(reg, entity);
    const auto& settings = world.resource_or_add<AoeNavigationSettings>();
    auto& registry = world.resource_or_add<AoePathfinderRegistry>();
    if (!registry.contains("direct"))
        registry.bind<DirectPathfinderLogic>("direct");
    if (!registry.contains("grid_astar"))
        registry.bind<GridAStarPathfinderLogic>("grid_astar");
    const std::string& id = world.try_resource<AoeLogicMap>()
        ? settings.unit_pathfinder_id : std::string("direct");
    const bool had_usable_path = !path.no_path &&
        path.current < path.waypoints.size();
    const auto result = registry.find(id, world, {
        reg.get<AoePosition>(entity).value, goal,
        {collider.radius_x, collider.radius_y}, entity, squad,
        ignored_dynamic_target, include_dynamic});
    ++path.request_sequence;
    path.requested_goal = goal;
    path.map_revision = result.map_revision;
    path.last_repath_tick = tick;
    path.include_dynamic_obstacles = include_dynamic;
    path.dynamic_repath_requested = false;
    path.blocked_ticks = 0;
    const bool failed = result.status != AoePathStatus::Ready ||
                        result.waypoints.empty();
    if (failed && include_dynamic && had_usable_path) {
        path.no_path = false;
        path.dynamic_repath_failed = true;
        ++world.resource_or_add<AoeGameplayDiagnostics>()
              .dynamic_repath_failures;
        world.resource_or_add<Events<AoeNavigationEvent>>().emit(
            entity, path.request_sequence, AoeNavigationEventStatus::Blocked,
            tick);
        return true;
    }
    path.current = 0;
    path.waypoints = result.waypoints;
    path.no_path = failed;
    path.dynamic_repath_failed = false;
    world.resource_or_add<Events<AoeNavigationEvent>>().emit(
        entity, path.request_sequence,
        path.no_path ? AoeNavigationEventStatus::NoPath
                     : AoeNavigationEventStatus::Ready,
        tick);
    return !path.no_path;
}

void navigation_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    std::vector<entt::entity> invalid;
    for (auto entity : reg.view<AoeAttackOrder, AoePosition, AoeCollider, AoeActionState,
                                AoeUnitDefinitionRef>()) {
        auto& order = reg.get<AoeAttackOrder>(entity);
        auto& state = reg.get<AoeActionState>(entity);
        if (!target_valid(reg, order.target)) { invalid.push_back(entity); continue; }
        const auto* definition = reg.get<AoeUnitDefinitionRef>(entity).value.get();
        if (!definition || !definition->attack) { invalid.push_back(entity); continue; }
        const auto& target_position = reg.get<AoePosition>(order.target.entity);
        const auto& target_collider = reg.get<AoeCollider>(order.target.entity);
        const float gap = aoe_surface_gap(reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
                                          target_position, target_collider);
        if (state.state == UnitState::Attacking) continue;
        if (gap > definition->attack->range + Epsilon) {
            const auto* approach = reg.try_get<AoeEngagementApproach>(entity);
            const bool has_approach = approach &&
                approach->target.entity == order.target.entity &&
                approach->target.instance_id == order.target.instance_id;
            reg.emplace_or_replace<AoeMoveGoal>(entity,
                AoeMoveGoal{
                    has_approach
                        ? attack_approach_destination(world, entity, order.target)
                        : target_position.value,
                    has_approach ? 0.f : definition->attack->range,
                    order.target});
            if (state.state != UnitState::Moving) {
                state.state = UnitState::Moving;
                state.state_started_tick = tick;
            }
        } else {
            reg.remove<AoeMoveGoal>(entity);
            reg.remove<AoeNavigationPath>(entity);
            if (state.state == UnitState::Moving) {
                state.state = UnitState::Idle;
                state.state_started_tick = tick;
            }
        }
    }
    for (auto entity : invalid) {
        if (!reg.valid(entity)) continue;
        if (reg.all_of<AoeAttackMoveOrder>(entity))
            clear_active_engagement(reg, entity);
        else cancel_orders(reg, entity);
        set_idle_if_active(reg, entity, tick);
    }

    const auto* map = world.try_resource<AoeLogicMap>();
    const std::uint64_t map_revision = map ? map->static_revision() : 0;
    const auto& nav_settings = world.resource_or_add<AoeNavigationSettings>();
    for (const auto entity : reg.view<AoePosition, AoeCollider, AoeMoveGoal,
                                      AoeActionState>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
        auto& state = reg.get<AoeActionState>(entity);
        if (state.state == UnitState::Attacking || is_terminal(state.state)) continue;
        auto& goal = reg.get<AoeMoveGoal>(entity);
        if (goal.target.entity != entt::null) {
            if (!target_valid(reg, goal.target)) continue;
            goal.destination = reg.all_of<AoeEngagementApproach>(entity)
                ? attack_approach_destination(world, entity, goal.target)
                : reg.get<AoePosition>(goal.target.entity).value;
        }
        auto* path = reg.try_get<AoeNavigationPath>(entity);
        const float threshold = reg.all_of<AoeSquadMember>(entity) ||
                                goal.target.entity != entt::null
            ? nav_settings.slot_repath_distance : Epsilon;
        bool needs_path = !path ||
            (!path->no_path &&
             (path->waypoints.empty() || path->current >= path->waypoints.size())) ||
            (path && glm::length(path->requested_goal - goal.destination) > threshold) ||
            (path && path->map_revision != map_revision) ||
            (path && path->dynamic_repath_requested);
        bool include_dynamic = path && path->include_dynamic_obstacles;
        const auto repath_cooldown = [&] {
            const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
            return nav_settings.repath_cooldown_ticks +
                static_cast<std::uint32_t>(
                    identity ? identity->instance_id % 3u : 0u);
        };
        if (path && path->dynamic_repath_failed &&
            tick - path->last_repath_tick >= repath_cooldown())
            needs_path = true;
        if (path && path->no_path) {
            if (path->map_revision != map_revision) needs_path = true;
            else if (reg.all_of<AoeSquadMember>(entity) &&
                     glm::length(path->requested_goal - goal.destination) >
                         Epsilon &&
                     tick - path->last_repath_tick >= repath_cooldown())
                needs_path = true;
            else if (path->include_dynamic_obstacles &&
                     tick - path->last_repath_tick >= repath_cooldown())
                needs_path = true;
        }
        if (path && !path->no_path && !needs_path &&
            glm::length(path->requested_goal - goal.destination) > Epsilon &&
            !path->waypoints.empty()) {
            const glm::vec2 tail_start = path->waypoints.size() > 1
                ? path->waypoints[path->waypoints.size() - 2]
                : reg.get<AoePosition>(entity).value;
            const glm::vec2 radii{reg.get<AoeCollider>(entity).radius_x,
                                  reg.get<AoeCollider>(entity).radius_y};
            if (map && map->valid() &&
                map->static_safe_fraction(tail_start, goal.destination,
                                          radii) < 1.f) {
                needs_path = true;
            } else {
                path->waypoints.back() = goal.destination;
                path->requested_goal = goal.destination;
            }
        }
        if (needs_path) {
            if (!plan_navigation_path(world, entity, goal.destination,
                                      include_dynamic, goal.target.entity,
                                      tick)) {
                state.state = include_dynamic ||
                    reg.all_of<AoeSquadMember>(entity)
                    ? UnitState::Moving : UnitState::Idle;
                state.state_started_tick = tick;
            } else if (state.state == UnitState::Idle) {
                state.state = UnitState::Moving;
                state.state_started_tick = tick;
            }
        }
    }
}


void movement_intent_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoePathMotionRequest>())
        reg.get<AoePathMotionRequest>(entity).valid = false;

    for (const auto entity : reg.view<AoePosition, AoeCollider, AoeMovement,
                                      AoeMoveGoal, AoeNavigationPath,
                                      AoeActionState>()) {
        const auto& state = reg.get<AoeActionState>(entity);
        const auto& path = reg.get<AoeNavigationPath>(entity);
        if (state.state == UnitState::Attacking || is_terminal(state.state) ||
            path.no_path || path.current >= path.waypoints.size())
            continue;
        const auto& position = reg.get<AoePosition>(entity);
        const auto& goal = reg.get<AoeMoveGoal>(entity);
        const glm::vec2 delta = path.waypoints[path.current] - position.value;
        const float distance = glm::length(delta);
        if (!(distance > .01f)) continue;
        const glm::vec2 direction = delta / distance;
        float max_speed = reg.get<AoeMovement>(entity).speed;
        if (const auto* limit = reg.try_get<AoeSquadMoveSpeedLimit>(entity))
            max_speed = std::min(max_speed, limit->value);
        float remaining = distance;
        if (goal.target.entity != entt::null &&
            path.current + 1 >= path.waypoints.size() &&
            target_valid(reg, goal.target)) {
            remaining = std::max(0.f, aoe_surface_gap(
                position, reg.get<AoeCollider>(entity),
                reg.get<AoePosition>(goal.target.entity),
                reg.get<AoeCollider>(goal.target.entity)) -
                goal.stopping_distance);
        }
        float speed = std::min(max_speed, remaining / .25f);
        if (const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
            locomotion && glm::length(locomotion->velocity) > Epsilon) {
            const float alignment = glm::dot(
                glm::normalize(locomotion->velocity), direction);
            speed *= std::clamp((alignment + .25f) / 1.25f, .1f, 1.f);
        }
        AoeMovementIntentKind kind = AoeMovementIntentKind::Move;
        if (reg.all_of<AoeEngagementApproach>(entity))
            kind = AoeMovementIntentKind::AttackApproach;
        else if (reg.all_of<AoeSquadMember>(entity))
            kind = AoeMovementIntentKind::FormationSlot;
        reg.emplace_or_replace<AoePathMotionRequest>(entity,
            AoePathMotionRequest{kind, direction * speed,
                path.waypoints[path.current], max_speed,
                path.request_sequence, tick, true});
    }
}

void local_avoidance_intent_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoeMovementIntent>())
        reg.get<AoeMovementIntent>(entity).valid = false;
    world.resource_or_add<AoeSteeringRegistry>();
    world.resource_or_add<AoeCrowdSteeringScratch>();
    world.resource_or_add<AoeNavigationSettings>();
    world.resource_or_add<AoeGameplayDiagnostics>();
    auto& steering = world.resource<AoeSteeringRegistry>();
    if (!steering.contains("local_default"))
        steering.bind<DefaultLocalSteeringLogic>("local_default");
    const auto& settings = world.resource<AoeNavigationSettings>();
    auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
    auto& neighbors = world.resource<AoeCrowdSteeringScratch>().nearest_neighbors;
    neighbors.clear();
    neighbors.reserve(settings.steering_max_neighbors + 1u);
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto* dynamic = world.try_resource<AoeDynamicObstacleIndex>();

    for (const auto entity : reg.view<AoePathMotionRequest, AoePosition,
                                      AoeCollider, AoeLocomotionState>()) {
        const auto& request = reg.get<AoePathMotionRequest>(entity);
        if (!request.valid || request.produced_tick != tick) continue;
        const auto& position = reg.get<AoePosition>(entity);
        const auto& collider = reg.get<AoeCollider>(entity);
        const glm::vec2 radii{collider.radius_x, collider.radius_y};
        auto& locomotion = reg.get<AoeLocomotionState>(entity);
        const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
        const entt::entity ignored_squad =
            ignored_formation_squad(reg, entity);
        neighbors.clear();
        if (map && map->valid() && dynamic &&
            settings.steering_max_neighbors > 0) {
            const float query_radius = std::max(
                request.max_speed * settings.steering_prediction_seconds +
                    settings.steering_separation_padding,
                std::max(radii.x, radii.y) * 4.f);
            dynamic->query(*map, position.value - glm::vec2(query_radius),
                position.value + glm::vec2(query_radius),
                [&](const AoeDynamicObstacleEntry& obstacle) {
                    if (obstacle.entity == entity ||
                        (ignored_squad != entt::null &&
                         obstacle.squad == ignored_squad))
                        return;
                    const AoeSteeringNeighbor candidate{obstacle.entity,
                        obstacle.instance_id, obstacle.center,
                        obstacle.radii, obstacle.velocity};
                    const glm::vec2 candidate_delta =
                        obstacle.center - position.value;
                    const float candidate_distance = glm::dot(
                        candidate_delta, candidate_delta);
                    const auto insertion = std::lower_bound(
                        neighbors.begin(), neighbors.end(), candidate_distance,
                        [&](const AoeSteeringNeighbor& value, float distance2) {
                            const glm::vec2 delta =
                                value.position - position.value;
                            return glm::dot(delta, delta) < distance2;
                        });
                    neighbors.insert(insertion, candidate);
                    if (neighbors.size() > settings.steering_max_neighbors)
                        neighbors.pop_back();
                });
        }
        int preferred_side = locomotion.avoidance_side;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity);
            member && reg.valid(member->squad))
            if (const auto* traffic =
                    reg.try_get<AoeSquadTrafficState>(member->squad);
                traffic && traffic->negotiated_side != 0)
                preferred_side = traffic->negotiated_side;
        bool threatened = false;
        bool imminent = false;
        std::uint64_t threat_signature = 1469598103934665603ull;
        const float request_speed = glm::length(request.velocity);
        const glm::vec2 request_direction = request_speed > Epsilon
            ? request.velocity / request_speed : glm::vec2{0.f};
        const auto heading_x = static_cast<std::int32_t>(
            std::round(request_direction.x * 1024.f));
        const auto heading_y = static_cast<std::int32_t>(
            std::round(request_direction.y * 1024.f));
        threat_signature ^= static_cast<std::uint32_t>(heading_x);
        threat_signature *= 1099511628211ull;
        threat_signature ^= static_cast<std::uint32_t>(heading_y);
        threat_signature *= 1099511628211ull;
        for (const auto& neighbor : neighbors) {
            threat_signature ^= neighbor.instance_id;
            threat_signature *= 1099511628211ull;
            const glm::vec2 relative = neighbor.position - position.value;
            const glm::vec2 relative_velocity =
                request.velocity - neighbor.velocity;
            const float speed2 = glm::dot(relative_velocity,
                                          relative_velocity);
            const float contact_time = speed2 > Epsilon
                ? std::clamp(glm::dot(relative, relative_velocity) / speed2,
                    0.f, settings.steering_prediction_seconds)
                : 0.f;
            const glm::vec2 closest = relative -
                relative_velocity * contact_time;
            const glm::vec2 combined = radii + neighbor.radii +
                glm::vec2(settings.steering_separation_padding);
            const float normalized = closest.x * closest.x /
                    (combined.x * combined.x) +
                closest.y * closest.y / (combined.y * combined.y);
            threatened = threatened || normalized < 4.f;
            if (normalized < 1.f && contact_time <=
                    settings.steering_imminent_collision_seconds) {
                imminent = true;
            }
        }
        diagnostics.steering_neighbors_considered += neighbors.size();
        AoeSteeringResult result{request.velocity};
        const auto interval = std::max(
            1u, settings.steering_full_solve_interval);
        const bool cache_valid = locomotion.last_steering_tick != 0 &&
            std::isfinite(locomotion.cached_target_velocity.x) &&
            std::isfinite(locomotion.cached_target_velocity.y) &&
            glm::dot(locomotion.cached_target_velocity,
                     locomotion.cached_target_velocity) > Epsilon * Epsilon;
        const bool cadence_due = !identity ||
            ((tick + identity->instance_id) % interval == 0);
        const bool signature_changed =
            threat_signature != locomotion.threat_signature;
        const bool full_solve = locomotion.escape_steering || !threatened ||
            imminent || !cache_valid || signature_changed || cadence_due;
        if (!full_solve) {
            result.target_velocity = locomotion.cached_target_velocity;
            result.avoidance_side = locomotion.avoidance_side;
            result.threatened = true;
            ++diagnostics.steering_cached_solves;
        } else if (steering.contains(settings.steering_strategy_id)) {
            result = steering.steer(settings.steering_strategy_id,
                {entity, identity ? identity->instance_id : 0,
                 position.value, radii, locomotion.velocity, request.velocity,
                 request.local_goal, request.max_speed,
                 settings.steering_prediction_seconds,
                 settings.steering_separation_padding,
                 map && map->valid() ? map : nullptr, neighbors,
                 preferred_side,
                 settings.steering_side_switch_margin +
                     (locomotion.avoidance_side_hold_ticks > 0 && !imminent
                          ? 2.f : 0.f),
                 settings.steering_candidate_angle_step,
                 locomotion.escape_steering
                    ? settings.steering_escape_max_angle
                    : settings.steering_normal_max_angle,
                 settings.steering_minimum_safe_fraction});
            locomotion.cached_target_velocity = result.target_velocity;
            locomotion.last_steering_tick = tick;
            locomotion.threat_signature = threat_signature;
            if (locomotion.escape_steering)
                ++diagnostics.steering_escape_solves;
            if (imminent) ++diagnostics.steering_imminent_solves;
            else if (threatened) ++diagnostics.steering_full_solves;
            else ++diagnostics.steering_fast_path;
        } else {
            ++diagnostics.steering_fallbacks;
        }
        if (locomotion.avoidance_side_hold_ticks > 0)
            --locomotion.avoidance_side_hold_ticks;
        if (result.avoidance_side != 0 &&
            result.avoidance_side != locomotion.avoidance_side) {
            if (locomotion.avoidance_side != 0)
                ++diagnostics.steering_side_switches;
            locomotion.avoidance_side = static_cast<std::int8_t>(
                result.avoidance_side);
            locomotion.avoidance_side_hold_ticks =
                static_cast<std::uint8_t>(std::min(
                    settings.steering_side_hold_ticks,
                    static_cast<std::uint32_t>(
                        std::numeric_limits<std::uint8_t>::max())));
        }
        if (!std::isfinite(result.target_velocity.x) ||
            !std::isfinite(result.target_velocity.y)) {
            result.target_velocity = request.velocity;
            ++diagnostics.steering_fallbacks;
        }
        reg.emplace_or_replace<AoeMovementIntent>(entity,
            AoeMovementIntent{request.kind, result.target_velocity,
                request.velocity, request.local_goal,
                static_cast<std::uint32_t>(neighbors.size()),
                static_cast<std::int8_t>(result.avoidance_side),
                result.threatened,
                result.infeasible, tick, true});
    }
}

namespace {
float unit_flow_radius(const AoeUnitFlowRecord& value) {
    return std::max(value.radii.x, value.radii.y);
}

float relative_motion_safe_fraction(glm::vec2 relative_position,
                                    glm::vec2 relative_displacement,
                                    glm::vec2 combined_radii) {
    if (!(combined_radii.x > Epsilon) || !(combined_radii.y > Epsilon))
        return 1.f;
    const float rx2 = combined_radii.x * combined_radii.x;
    const float ry2 = combined_radii.y * combined_radii.y;
    const float c = relative_position.x * relative_position.x / rx2 +
                    relative_position.y * relative_position.y / ry2 - 1.f;
    if (c <= 0.f) {
        // Use the ellipse gradient here, matching the contact projection in
        // unit_flow_tick. A plain world-space dot product gives the wrong
        // entering/leaving answer for non-circular colliders.
        const float radial_displacement =
            relative_position.x * relative_displacement.x / rx2 +
            relative_position.y * relative_displacement.y / ry2;
        return radial_displacement >= 0.f
            ? 1.f : 0.f;
    }
    const float a = relative_displacement.x * relative_displacement.x / rx2 +
                    relative_displacement.y * relative_displacement.y / ry2;
    if (!(a > Epsilon)) return 1.f;
    const float b = 2.f *
        (relative_position.x * relative_displacement.x / rx2 +
         relative_position.y * relative_displacement.y / ry2);
    const float discriminant = b * b - 4.f * a * c;
    if (discriminant < 0.f) return 1.f;
    const float enter = (-b - std::sqrt(discriminant)) / (2.f * a);
    return enter >= 0.f && enter <= 1.f
        ? std::max(0.f, enter - .0001f) : 1.f;
}
} // namespace

void unit_flow_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    world.resource_or_add<AoeNavigationSettings>();
    world.resource_or_add<AoeGameplayDiagnostics>();
    world.resource_or_add<AoeUnitFlowIndex>();
    const auto& settings = world.resource<AoeNavigationSettings>();
    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
    auto& index = world.resource<AoeUnitFlowIndex>();
    index.records.clear();
    index.candidates.clear();
    index.selected.clear();
    index.maximum_reach = 0.f;
    for (const auto entity : reg.view<AoeGlobalMotionDecision>())
        reg.get<AoeGlobalMotionDecision>(entity).valid = false;
    const auto acceleration_limited = [&](entt::entity entity,
                                          glm::vec2 velocity) {
        const float target_speed = glm::length(velocity);
        const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
        const float current_speed = locomotion
            ? glm::length(locomotion->velocity) : 0.f;
        const float change = std::max(0.f,
            settings.steering_max_acceleration) * fixed_dt;
        const float speed = current_speed < target_speed
            ? std::min(target_speed, current_speed + change)
            : std::max(target_speed, current_speed - change);
        return target_speed > Epsilon
            ? velocity * (speed / target_speed) : glm::vec2{0.f};
    };
    for (const auto entity : reg.view<AoeMovementIntent, AoePosition,
                                      AoeCollider, AoeGameplayIdentity,
                                      AoeTeam>()) {
        const auto& intent = reg.get<AoeMovementIntent>(entity);
        if (!intent.valid || intent.produced_tick != tick) continue;
        const auto& collider = reg.get<AoeCollider>(entity);
        entt::entity squad = entt::null;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity))
            squad = member->squad;
        index.records.push_back({entity,
            reg.get<AoeGameplayIdentity>(entity).instance_id, squad,
            reg.get<AoeTeam>(entity).id, intent.kind,
            reg.get<AoePosition>(entity).value,
            {collider.radius_x, collider.radius_y}, intent.velocity});
        index.maximum_reach = std::max(index.maximum_reach,
            unit_flow_radius(index.records.back()) +
            glm::length(intent.velocity) * settings.unit_flow_prediction_seconds);
        (void)reg.get_or_emplace<AoeGlobalMotionState>(entity);
        reg.emplace_or_replace<AoeGlobalMotionDecision>(entity,
            AoeGlobalMotionDecision{.velocity = intent.velocity,
                .produced_tick = tick, .valid = true});
        if (intent.locally_infeasible)
            ++diagnostics.flow_infeasible_assignments;
    }
    diagnostics.flow_active_intents += index.records.size();
    if (!settings.unit_flow_enabled) {
        for (const auto& record : index.records) {
            auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
            decision.velocity = acceleration_limited(
                record.entity, decision.velocity);
        }
        return;
    }
    std::sort(index.records.begin(), index.records.end(),
        [](const AoeUnitFlowRecord& a, const AoeUnitFlowRecord& b) {
            if (std::abs(a.position.x - b.position.x) > Epsilon)
                return a.position.x < b.position.x;
            return a.instance_id < b.instance_id;
        });
    for (std::size_t i = 0; i < index.records.size(); ++i)
        for (std::size_t j = i + 1; j < index.records.size(); ++j) {
            const auto& a = index.records[i];
            const auto& b = index.records[j];
            const float reach = unit_flow_radius(a) +
                glm::length(a.intent_velocity) *
                    settings.unit_flow_prediction_seconds;
            if (b.position.x - a.position.x > reach + index.maximum_reach)
                break;
            const glm::vec2 relative = b.position - a.position;
            const glm::vec2 relative_velocity =
                a.intent_velocity - b.intent_velocity;
            const float relative_speed2 = glm::dot(
                relative_velocity, relative_velocity);
            const float time = relative_speed2 > Epsilon
                ? std::clamp(glm::dot(relative, relative_velocity) /
                                 relative_speed2,
                             0.f, settings.unit_flow_prediction_seconds)
                : 0.f;
            const float closest = glm::length(
                relative - relative_velocity * time);
            const float clearance = unit_flow_radius(a) +
                unit_flow_radius(b) + settings.unit_flow_follow_gap;
            if (closest > clearance) continue;
            index.candidates.push_back({i, j, time, closest});
        }
    std::sort(index.candidates.begin(), index.candidates.end(),
        [&](const AoeUnitFlowConflict& a,
            const AoeUnitFlowConflict& b) {
            if (std::abs(a.time_to_collision - b.time_to_collision) > Epsilon)
                return a.time_to_collision < b.time_to_collision;
            if (std::abs(a.closest_distance - b.closest_distance) > Epsilon)
                return a.closest_distance < b.closest_distance;
            const auto aid = index.records[a.a].instance_id ^
                             index.records[a.b].instance_id;
            const auto bid = index.records[b.a].instance_id ^
                             index.records[b.b].instance_id;
            return aid < bid;
        });
    std::vector<std::uint32_t> selected_count(index.records.size(), 0);
    std::vector<std::uint32_t> candidate_count(index.records.size(), 0);
    for (const auto& edge : index.candidates) {
        ++candidate_count[edge.a];
        ++candidate_count[edge.b];
        if (selected_count[edge.a] >= settings.unit_flow_max_neighbors ||
            selected_count[edge.b] >= settings.unit_flow_max_neighbors)
            continue;
        index.selected.push_back(edge);
        ++selected_count[edge.a];
        ++selected_count[edge.b];
    }
    diagnostics.flow_neighbor_checks += index.candidates.size();
    diagnostics.flow_conflicts += index.selected.size();

    index.parents.resize(index.records.size());
    index.ranks.assign(index.records.size(), 0);
    for (std::size_t i = 0; i < index.parents.size(); ++i)
        index.parents[i] = i;
    const auto find_root = [&](std::size_t value) {
        std::size_t root = value;
        while (index.parents[root] != root) root = index.parents[root];
        while (index.parents[value] != value) {
            const auto next = index.parents[value];
            index.parents[value] = root;
            value = next;
        }
        return root;
    };
    for (const auto& edge : index.selected) {
        auto a = find_root(edge.a);
        auto b = find_root(edge.b);
        if (a == b) continue;
        if (index.ranks[a] < index.ranks[b]) std::swap(a, b);
        index.parents[b] = a;
        if (index.ranks[a] == index.ranks[b]) ++index.ranks[a];
    }
    std::unordered_map<std::size_t, std::uint32_t> groups;
    std::uint32_t next_group = 1;
    std::vector<glm::vec2> lateral(index.records.size(), glm::vec2{0.f});
    std::vector<float> speed_scale(index.records.size(), 1.f);
    std::vector<AoeGlobalMotionMode> modes(
        index.records.size(), AoeGlobalMotionMode::Clear);
    std::vector<AoeMotionDecisionReason> reasons(
        index.records.size(), AoeMotionDecisionReason::None);
    std::vector<std::int8_t> sides(index.records.size(), 0);
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto side_open = [&](const AoeUnitFlowRecord& record, int side) {
        if (!map || !map->valid()) return true;
        const float speed = glm::length(record.intent_velocity);
        if (!(speed > Epsilon)) return false;
        const glm::vec2 direction = record.intent_velocity / speed;
        const glm::vec2 right{direction.y, -direction.x};
        const glm::vec2 offset = right * settings.unit_flow_lateral_clearance *
            (side < 0 ? 1.f : -1.f);
        return map->static_safe_fraction(record.position,
            record.position + offset, record.radii) >= 1.f - Epsilon;
    };
    const auto add_side = [&](std::size_t index_value, int preferred,
                              AoeGlobalMotionMode mode,
                              AoeMotionDecisionReason reason) {
        const auto& record = index.records[index_value];
        const float speed = glm::length(record.intent_velocity);
        if (!(speed > Epsilon)) return false;
        int side = preferred;
        if (!side_open(record, side)) side = -side;
        if (!side_open(record, side)) {
            speed_scale[index_value] = std::min(speed_scale[index_value],
                settings.unit_flow_yield_speed_scale);
            modes[index_value] = AoeGlobalMotionMode::Yielding;
            reasons[index_value] = AoeMotionDecisionReason::SideBlocked;
            return false;
        }
        const glm::vec2 direction = record.intent_velocity / speed;
        const glm::vec2 right{direction.y, -direction.x};
        lateral[index_value] += right * (side < 0 ? 1.f : -1.f);
        modes[index_value] = mode;
        reasons[index_value] = reason;
        sides[index_value] = static_cast<std::int8_t>(side);
        return true;
    };
    const auto yield_to = [&](std::size_t yielding, std::size_t priority,
                              AoeMotionDecisionReason reason) {
        speed_scale[yielding] = std::min(speed_scale[yielding],
            settings.unit_flow_yield_speed_scale);
        modes[yielding] = AoeGlobalMotionMode::Yielding;
        reasons[yielding] = reason;
        auto& decision = reg.get<AoeGlobalMotionDecision>(
            index.records[yielding].entity);
        decision.yielding_to = index.records[priority].entity;
        decision.yielding_to_instance = index.records[priority].instance_id;
    };

    // Every selected edge contributes one constraint. Accumulating all of
    // them before normalizing the result handles a connected traffic group
    // without pretending that repeated passes are an iterative solver.
    for (const auto& edge : index.selected) {
        const auto& a = index.records[edge.a];
        const auto& b = index.records[edge.b];
        const float speed_a = glm::length(a.intent_velocity);
        const float speed_b = glm::length(b.intent_velocity);
        if (!(speed_a > Epsilon) || !(speed_b > Epsilon)) continue;
        const glm::vec2 dir_a = a.intent_velocity / speed_a;
        const glm::vec2 dir_b = b.intent_velocity / speed_b;
        const float alignment = glm::dot(dir_a, dir_b);
        const float speed_difference = std::abs(speed_a - speed_b);
        const bool same_speed = speed_difference <=
            std::max(settings.unit_flow_same_speed_absolute,
                std::max(speed_a, speed_b) *
                    settings.unit_flow_same_speed_relative);
        auto& state_a = reg.get<AoeGlobalMotionState>(a.entity);
        auto& state_b = reg.get<AoeGlobalMotionState>(b.entity);
        bool a_priority = speed_a > speed_b;
        if (same_speed) {
            if (state_a.wait_ticks != state_b.wait_ticks &&
                std::max(state_a.wait_ticks, state_b.wait_ticks) >=
                    settings.unit_flow_starvation_ticks) {
                a_priority = state_a.wait_ticks > state_b.wait_ticks;
                ++diagnostics.flow_starvation_promotions;
            } else
                a_priority = a.instance_id < b.instance_id;
        }
        if (alignment <= settings.unit_flow_head_on_dot) {
            // Negotiate one shared passing convention for the edge. A
            // unilateral side flip would put both opposite-facing units
            // into the same world-space lane.
            if (side_open(a, -1) && side_open(b, -1)) {
                add_side(edge.a, -1, AoeGlobalMotionMode::PassingRight,
                         AoeMotionDecisionReason::HeadOnTraffic);
                add_side(edge.b, -1, AoeGlobalMotionMode::PassingRight,
                         AoeMotionDecisionReason::HeadOnTraffic);
            } else if (side_open(a, 1) && side_open(b, 1)) {
                add_side(edge.a, 1, AoeGlobalMotionMode::PassingLeft,
                         AoeMotionDecisionReason::HeadOnTraffic);
                add_side(edge.b, 1, AoeGlobalMotionMode::PassingLeft,
                         AoeMotionDecisionReason::HeadOnTraffic);
            } else {
                yield_to(a_priority ? edge.b : edge.a,
                         a_priority ? edge.a : edge.b,
                         AoeMotionDecisionReason::SideBlocked);
            }
        } else if (alignment >= settings.unit_flow_same_direction_dot &&
                   same_speed) {
            const float collision_distance =
                unit_flow_radius(a) + unit_flow_radius(b);
            if (edge.closest_distance <= collision_distance + Epsilon) {
                const glm::vec2 right{dir_a.y, -dir_a.x};
                const float lateral_separation = glm::dot(
                    b.position - a.position, right);
                const int separation_side =
                    std::abs(lateral_separation) > Epsilon
                    ? (lateral_separation > 0.f ? 1 : -1)
                    : (a_priority ? -1 : 1);
                add_side(edge.a, separation_side,
                    AoeGlobalMotionMode::SideStep,
                    AoeMotionDecisionReason::SameDirectionConflict);
                add_side(edge.b, -separation_side,
                    AoeGlobalMotionMode::SideStep,
                    AoeMotionDecisionReason::SameDirectionConflict);
            }
        } else {
            const std::size_t yielding = a_priority ? edge.b : edge.a;
            const std::size_t priority = a_priority ? edge.a : edge.b;
            const auto reason = alignment >=
                    settings.unit_flow_same_direction_dot
                ? AoeMotionDecisionReason::FasterTraffic
                : AoeMotionDecisionReason::CrossingTraffic;
            if (add_side(yielding, -1,
                         AoeGlobalMotionMode::SideStep, reason)) {
                auto& decision = reg.get<AoeGlobalMotionDecision>(
                    index.records[yielding].entity);
                decision.yielding_to = index.records[priority].entity;
                decision.yielding_to_instance =
                    index.records[priority].instance_id;
            } else {
                yield_to(yielding, priority,
                         AoeMotionDecisionReason::SideBlocked);
            }
        }
    }

    for (std::size_t i = 0; i < index.records.size(); ++i) {
        const auto& record = index.records[i];
        auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
        auto& state = reg.get<AoeGlobalMotionState>(record.entity);
        const float speed = glm::length(record.intent_velocity);
        if (glm::length(lateral[i]) > Epsilon && speed > Epsilon) {
            const glm::vec2 direction = record.intent_velocity / speed;
            decision.velocity = glm::normalize(direction +
                glm::normalize(lateral[i]) * settings.unit_flow_lateral_bias) *
                speed * speed_scale[i];
        } else {
            decision.velocity = record.intent_velocity * speed_scale[i];
        }
        decision.mode = modes[i];
        decision.reason = reasons[i];
        decision.selected_conflicts = selected_count[i];
        decision.candidate_count = candidate_count[i];
        const auto root = find_root(i);
        auto [group_it, inserted] = groups.emplace(root, next_group);
        if (inserted) ++next_group;
        decision.conflict_group = selected_count[i] > 0 ? group_it->second : 0;
        decision.nearest_time_to_collision =
            settings.unit_flow_prediction_seconds;
        for (const auto& edge : index.selected)
            if (edge.a == i || edge.b == i) {
                decision.nearest_time_to_collision = std::min(
                    decision.nearest_time_to_collision,
                    edge.time_to_collision);
                if (state.peer == entt::null ||
                    edge.time_to_collision <=
                        decision.nearest_time_to_collision + Epsilon) {
                    const auto peer_index = edge.a == i ? edge.b : edge.a;
                    state.peer = index.records[peer_index].entity;
                    state.peer_instance_id =
                        index.records[peer_index].instance_id;
                }
            }
        if (selected_count[i] == 0) {
            state.peer = entt::null;
            state.peer_instance_id = 0;
            if (tick > state.last_conflict_tick +
                    settings.unit_flow_backing_cooldown_ticks) {
                state.backing_ticks = 0;
                state.backing_distance = 0.f;
            }
        }
        const auto* locomotion = reg.try_get<AoeLocomotionState>(record.entity);
        const bool stopped = locomotion && locomotion->actual_speed <=
            settings.steering_stalled_speed;
        if (decision.mode != AoeGlobalMotionMode::Clear && stopped)
            ++state.wait_ticks;
        else if (decision.mode == AoeGlobalMotionMode::Clear)
            state.wait_ticks = 0;
        const float maximum_backing_distance =
            2.f * unit_flow_radius(record) *
            settings.unit_flow_backing_max_diameters;
        const bool backing_available =
            state.backing_ticks < settings.unit_flow_backing_max_ticks &&
            state.backing_distance + Epsilon < maximum_backing_distance;
        if (!backing_available && state.backing_ticks > 0 &&
            tick > state.last_backing_tick +
                settings.unit_flow_backing_cooldown_ticks) {
            state.backing_ticks = 0;
            state.backing_distance = 0.f;
        }
        const bool traffic_deadlock = selected_count[i] > 0 &&
            (decision.mode != AoeGlobalMotionMode::Clear ||
             state.mode != AoeGlobalMotionMode::Clear);
        if (traffic_deadlock &&
            state.wait_ticks >= settings.unit_flow_backing_threshold_ticks &&
            state.backing_ticks < settings.unit_flow_backing_max_ticks &&
            state.backing_distance + Epsilon < maximum_backing_distance) {
            const glm::vec2 backward = speed > Epsilon
                ? -record.intent_velocity / speed : glm::vec2{0.f};
            const float distance = 2.f * unit_flow_radius(record);
            if (!map || !map->valid() || map->static_safe_fraction(
                    record.position, record.position + backward * distance,
                    record.radii) >= 1.f - Epsilon) {
                decision.velocity = backward * speed *
                    settings.unit_flow_backing_speed_scale;
                decision.mode = AoeGlobalMotionMode::Backing;
                decision.reason = AoeMotionDecisionReason::DeadlockEscape;
                state.last_backing_tick = tick;
                ++diagnostics.flow_deadlock_escalations;
            }
        }
        decision.wait_ticks = state.wait_ticks;
        diagnostics.flow_wait_ticks += state.wait_ticks;
        decision.velocity = acceleration_limited(
            record.entity, decision.velocity);
        state.mode = decision.mode;
        state.negotiated_side = sides[i];
        state.last_conflict_tick = selected_count[i] > 0
            ? tick : state.last_conflict_tick;
        switch (decision.mode) {
        case AoeGlobalMotionMode::SideStep: ++diagnostics.flow_following; break;
        case AoeGlobalMotionMode::PassingLeft:
        case AoeGlobalMotionMode::PassingRight: ++diagnostics.flow_passing; break;
        case AoeGlobalMotionMode::Yielding: ++diagnostics.flow_yielding; break;
        case AoeGlobalMotionMode::Backing: ++diagnostics.flow_backing; break;
        case AoeGlobalMotionMode::Recovering: ++diagnostics.flow_recovering; break;
        case AoeGlobalMotionMode::Clear: break;
        }
    }

    // Project the complete conflict group's velocities onto the contact
    // constraints. Pair-policy lateral choices alone can cancel in a dense
    // group (one neighbor above and another below); the projection resolves
    // all current overlaps together before the safety-only clipping stage.
    std::vector<float> velocity_caps(index.records.size(), 0.f);
    for (std::size_t i = 0; i < index.records.size(); ++i)
        velocity_caps[i] = glm::length(acceleration_limited(
            index.records[i].entity, index.records[i].intent_velocity));
    const float recovery_seconds = std::max(
        fixed_dt, settings.unit_flow_overlap_recovery_seconds);
    for (std::uint32_t iteration = 0;
         iteration < std::max(1u, settings.unit_flow_solver_iterations);
         ++iteration) {
        for (const auto& edge : index.selected) {
            const auto& a = index.records[edge.a];
            const auto& b = index.records[edge.b];
            const glm::vec2 combined = a.radii + b.radii;
            if (!(combined.x > Epsilon) || !(combined.y > Epsilon))
                continue;
            const glm::vec2 relative = a.position - b.position;
            const glm::vec2 scaled{relative.x / combined.x,
                                   relative.y / combined.y};
            const float normalized_distance = glm::length(scaled);
            // Safety treats a pair on the contact boundary as blocked when
            // its relative velocity enters the other collider. Project those
            // shallow/contact cases too; skipping them leaves an approaching
            // velocity for safety to reduce to zero forever.
            if (normalized_distance > 1.f + Epsilon) continue;
            glm::vec2 normal{relative.x / (combined.x * combined.x),
                             relative.y / (combined.y * combined.y)};
            const float normal_length = glm::length(normal);
            if (!(normal_length > Epsilon)) {
                const auto parity = a.instance_id < b.instance_id ? 1.f : -1.f;
                normal = {parity, 0.f};
            } else {
                normal /= normal_length;
            }
            auto& decision_a = reg.get<AoeGlobalMotionDecision>(a.entity);
            auto& decision_b = reg.get<AoeGlobalMotionDecision>(b.entity);
            const float cap_a = velocity_caps[edge.a];
            const float cap_b = velocity_caps[edge.b];
            const float penetration = std::max(0.f,
                1.f - normalized_distance);
            const float separation_speed = std::min(cap_a + cap_b,
                penetration *
                    std::min(combined.x, combined.y) / recovery_seconds);
            const float current_separation = glm::dot(
                normal, decision_a.velocity - decision_b.velocity);
            if (current_separation + Epsilon >= separation_speed) continue;
            const float correction = separation_speed - current_separation;
            const float intent_a = glm::length(a.intent_velocity);
            const float intent_b = glm::length(b.intent_velocity);
            const bool same_speed = std::abs(intent_a - intent_b) <=
                std::max(settings.unit_flow_same_speed_absolute,
                    std::max(intent_a, intent_b) *
                        settings.unit_flow_same_speed_relative);
            if (same_speed) {
                decision_a.velocity += normal * (correction * .5f);
                decision_b.velocity -= normal * (correction * .5f);
            } else if (intent_a < intent_b) {
                decision_a.velocity += normal * correction;
            } else {
                decision_b.velocity -= normal * correction;
            }
            ++diagnostics.flow_overlap_projections;
        }
    }
    // Scale every member of a connected conflict group by the same factor.
    // Independent per-unit clamps can turn a separating relative velocity
    // back into an approaching one; a common factor preserves every projected
    // pair constraint while respecting the strictest member's speed cap.
    std::vector<float> group_scale(index.records.size(), 1.f);
    for (std::size_t i = 0; i < index.records.size(); ++i) {
        const float speed = glm::length(
            reg.get<AoeGlobalMotionDecision>(index.records[i].entity).velocity);
        if (speed <= velocity_caps[i] + Epsilon || !(speed > Epsilon))
            continue;
        const auto root = find_root(i);
        group_scale[root] = std::min(
            group_scale[root], velocity_caps[i] / speed);
    }
    for (std::size_t i = 0; i < index.records.size(); ++i)
        reg.get<AoeGlobalMotionDecision>(index.records[i].entity).velocity *=
            group_scale[find_root(i)];
}

void global_motion_safety_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    auto& index = world.resource_or_add<AoeUnitFlowIndex>();
    const auto* map = world.try_resource<AoeLogicMap>();
    for (const auto& record : index.records) {
        auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
        if (!decision.valid || decision.produced_tick != tick) continue;
        decision.static_safe_fraction = map && map->valid()
            ? map->static_safe_fraction(record.position,
                record.position + decision.velocity * dt, record.radii)
            : 1.f;
        decision.dynamic_safe_fraction = 1.f;
    }
    for (const auto& edge : index.candidates) {
        const auto& a = index.records[edge.a];
        const auto& b = index.records[edge.b];
        auto& decision_a = reg.get<AoeGlobalMotionDecision>(a.entity);
        auto& decision_b = reg.get<AoeGlobalMotionDecision>(b.entity);
        const float safe = relative_motion_safe_fraction(
            a.position - b.position,
            (decision_a.velocity - decision_b.velocity) * dt,
            a.radii + b.radii);
        decision_a.dynamic_safe_fraction = std::min(
            decision_a.dynamic_safe_fraction, safe);
        decision_b.dynamic_safe_fraction = std::min(
            decision_b.dynamic_safe_fraction, safe);
    }
    for (const auto& record : index.records) {
        auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
        decision.safe_fraction = std::min(decision.static_safe_fraction,
                                          decision.dynamic_safe_fraction);
        if (decision.safe_fraction < 1.f - Epsilon) {
            if (decision.static_safe_fraction <=
                decision.dynamic_safe_fraction + Epsilon)
                decision.stop_reason =
                    AoeMotionStopReason::StaticSafetyClipped;
            else
                decision.stop_reason =
                    AoeMotionStopReason::DynamicSafetyClipped;
        } else if (decision.mode == AoeGlobalMotionMode::Yielding) {
            decision.stop_reason = AoeMotionStopReason::GlobalYield;
        }
    }
}


void movement_tick(EcsWorld& world, std::uint64_t tick) {
    GLD_PERF_TIME_POINT(movement_started);
    auto& reg = world.reg();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    const float dt = static_cast<float>(settings.fixed_dt);
    auto& diagnostics = world.resource_or_add<AoeGameplayDiagnostics>();
    std::vector<entt::entity> arrived;

    for (const auto entity : reg.view<AoeLocomotionState>()) {
        auto& locomotion = reg.get<AoeLocomotionState>(entity);
        locomotion.previous_velocity = locomotion.velocity;
        locomotion.effective_max_speed = 0.f;
        locomotion.actual_speed = 0.f;
    }
    for (const auto entity : reg.view<AoePosition, AoeCollider, AoeMovement,
                                      AoeMoveGoal, AoeNavigationPath,
                                      AoeActionState, AoeFacing>()) {
        auto& locomotion = reg.get_or_emplace<AoeLocomotionState>(entity);
        auto& state = reg.get<AoeActionState>(entity);
        auto& path = reg.get<AoeNavigationPath>(entity);
        if (state.state == UnitState::Attacking || is_terminal(state.state)) {
            locomotion.velocity = {0.f, 0.f};
            locomotion.stalled_ticks = 0;
            locomotion.escape_steering = false;
            continue;
        }
        if (path.no_path) {
            locomotion.velocity = {0.f, 0.f};
            ++locomotion.stalled_ticks;
            if (auto* decision =
                    reg.try_get<AoeGlobalMotionDecision>(entity))
                decision->stop_reason = AoeMotionStopReason::NoPath;
            continue;
        }
        auto& goal = reg.get<AoeMoveGoal>(entity);
        if (goal.target.entity != entt::null) {
            if (!target_valid(reg, goal.target)) {
                arrived.push_back(entity);
                continue;
            }
            goal.destination = reg.all_of<AoeEngagementApproach>(entity)
                ? attack_approach_destination(world, entity, goal.target)
                : reg.get<AoePosition>(goal.target.entity).value;
        }
        if (path.current >= path.waypoints.size()) {
            arrived.push_back(entity);
            continue;
        }
        auto& position = reg.get<AoePosition>(entity);
        const glm::vec2 delta = path.waypoints[path.current] - position.value;
        const float distance = glm::length(delta);
        const bool final_waypoint = path.current + 1 >= path.waypoints.size();
        bool reached = distance <= .01f;
        if (goal.target.entity != entt::null && final_waypoint && !reached) {
            reached = aoe_surface_gap(position, reg.get<AoeCollider>(entity),
                reg.get<AoePosition>(goal.target.entity),
                reg.get<AoeCollider>(goal.target.entity)) <=
                goal.stopping_distance + Epsilon;
        }
        if (reached) {
            if (!final_waypoint) {
                ++path.current;
                state.state = UnitState::Moving;
            } else {
                arrived.push_back(entity);
            }
            continue;
        }

        const auto* request = reg.try_get<AoePathMotionRequest>(entity);
        auto* decision = reg.try_get<AoeGlobalMotionDecision>(entity);
        const bool valid_decision = decision && decision->valid &&
                                    decision->produced_tick == tick;
        glm::vec2 velocity = valid_decision ? decision->velocity
            : (request && request->valid ? request->velocity : glm::vec2{0.f});
        const float safety = valid_decision
            ? std::clamp(decision->safe_fraction, 0.f, 1.f) : 1.f;
        velocity *= safety;
        float speed = glm::length(velocity);
        const float max_speed = reg.get<AoeMovement>(entity).speed;
        if (speed > max_speed && speed > Epsilon) {
            velocity *= max_speed / speed;
            speed = max_speed;
        }
        locomotion.effective_max_speed = request ? request->max_speed : max_speed;
        glm::vec2 displacement = velocity * dt;
        if (distance > Epsilon && glm::length(displacement) > distance &&
            glm::dot(glm::normalize(displacement), delta / distance) > .9f)
            displacement = delta;
        position.value += displacement;
        locomotion.velocity = dt > 0.f
            ? displacement / dt : glm::vec2{0.f};
        locomotion.actual_speed = glm::length(locomotion.velocity);
        locomotion.distance_travelled += glm::length(displacement);
        const bool backing = valid_decision &&
            decision->mode == AoeGlobalMotionMode::Backing;
        if (locomotion.actual_speed > navigation.steering_stalled_speed &&
            !backing)
            set_locomotion_facing(reg.get<AoeFacing>(entity), locomotion,
                locomotion.velocity, navigation, diagnostics);
        if (backing) {
            auto& motion_state =
                reg.get_or_emplace<AoeGlobalMotionState>(entity);
            motion_state.backing_distance += glm::length(displacement);
            ++motion_state.backing_ticks;
        }

        const glm::vec2 path_direction = request &&
                glm::length(request->velocity) > Epsilon
            ? glm::normalize(request->velocity) : delta / distance;
        const float forward_progress = glm::dot(displacement, path_direction);
        const bool intentional_motion = valid_decision &&
            decision->mode != AoeGlobalMotionMode::Clear;
        const bool made_progress = intentional_motion
            ? locomotion.actual_speed > navigation.steering_stalled_speed
            : forward_progress + Epsilon >=
                navigation.steering_stalled_speed * dt;
        if (!made_progress) {
            ++locomotion.stalled_ticks;
            const auto* intent = reg.try_get<AoeMovementIntent>(entity);
            locomotion.local_avoidance_infeasible = intent &&
                intent->locally_infeasible;
            const std::uint32_t traffic_limit =
                navigation.unit_flow_backing_threshold_ticks +
                navigation.unit_flow_backing_max_ticks;
            const bool traffic_grace = intentional_motion &&
                decision->wait_ticks < traffic_limit;
            if (!traffic_grace) {
                ++path.blocked_ticks;
                locomotion.escape_steering = locomotion.stalled_ticks >=
                    std::max(1u, navigation.steering_escape_stalled_ticks);
                const auto trigger = std::max(1u,
                    navigation.blocked_repath_ticks);
                if (path.blocked_ticks >= trigger) {
                    path.include_dynamic_obstacles = true;
                    path.dynamic_repath_requested = true;
                    path.blocked_ticks = 0;
                    if (decision)
                        decision->stop_reason =
                            AoeMotionStopReason::RepathPending;
                }
            } else {
                path.blocked_ticks = 0;
            }
            if (decision && decision->stop_reason == AoeMotionStopReason::None)
                decision->stop_reason = intentional_motion
                    ? AoeMotionStopReason::GlobalYield
                    : AoeMotionStopReason::Unknown;
        } else {
            path.blocked_ticks = 0;
            locomotion.stalled_ticks = 0;
            locomotion.escape_steering = false;
            locomotion.local_avoidance_infeasible = false;
        }
        state.state = UnitState::Moving;
    }

    for (const auto entity : arrived) {
        if (!reg.valid(entity)) continue;
        if (const auto* goal = reg.try_get<AoeMoveGoal>(entity);
            goal && goal->target.entity == entt::null &&
            reg.all_of<AoePosition>(entity)) {
            auto& position = reg.get<AoePosition>(entity);
            const float snapped = glm::length(goal->destination - position.value);
            position.value = goal->destination;
            if (auto* locomotion = reg.try_get<AoeLocomotionState>(entity))
                locomotion->distance_travelled += snapped;
        }
        reg.remove<AoeMoveGoal, AoeNavigationPath>(entity);
        auto& state = reg.get<AoeActionState>(entity);
        if (!is_terminal(state.state)) {
            state.state = UnitState::Idle;
            state.state_started_tick = tick;
        }
    }
    for (const auto entity : reg.view<AoeLocomotionState>(
             entt::exclude<AoeMoveGoal>)) {
        auto& locomotion = reg.get<AoeLocomotionState>(entity);
        locomotion.velocity = {0.f, 0.f};
        locomotion.actual_speed = 0.f;
        locomotion.stalled_ticks = 0;
        locomotion.escape_steering = false;
    }
    GLD_PERF_MONITOR(
        diagnostics.movement_last_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - movement_started).count();
        diagnostics.movement_peak_ms = std::max(
            diagnostics.movement_peak_ms, diagnostics.movement_last_ms);
    );
}

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

void lifecycle_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& settings = world.resource<AoeGameplaySettings>();
    std::vector<entt::entity> recycle;
    for (auto entity : reg.view<AoeActionState, AoeUnitDefinitionRef>(entt::exclude<AoeRecyclePending>)) {
        auto& state = reg.get<AoeActionState>(entity);
        const auto* definition = reg.get<AoeUnitDefinitionRef>(entity).value.get();
        if (!definition) continue;
        if (!definition->lifecycle.recycle_after_death) continue;
        if (state.state == UnitState::Dying &&
            tick >= state.state_started_tick + seconds_to_ticks(
                definition->lifecycle.death_duration_seconds, settings)) {
            if (definition->lifecycle.disappear_duration_seconds > 0.f) {
                state.state = UnitState::Disappearing;
                state.state_started_tick = tick;
                ++state.sequence;
                emit_event(world, entity, AoeActionEventType::DisappearStarted, state, tick);
            } else recycle.push_back(entity);
        } else if (state.state == UnitState::Disappearing &&
                   tick >= state.state_started_tick + seconds_to_ticks(
                       definition->lifecycle.disappear_duration_seconds, settings)) {
            recycle.push_back(entity);
        }
    }
    for (auto entity : recycle) {
        if (!reg.valid(entity) || reg.all_of<AoeRecyclePending>(entity)) continue;
        auto& state = reg.get<AoeActionState>(entity);
        emit_event(world, entity, AoeActionEventType::RecycleRequested, state, tick);
        reg.emplace<AoeRecyclePending>(entity);
    }
}

void capture_position_history_tick(EcsWorld& world) {
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoePosition, Transform>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
        const auto current = reg.get<AoePosition>(entity).value;
        reg.get_or_emplace<AoePositionHistory>(entity).previous = current;
    }
}

void fixed_tick(EcsWorld& world) {
    auto& clock = world.resource<AoeGameplayClock>();
    ++clock.tick;
    GLD_AOE_GAMEPLAY_PHASE(world, position_history_ms,
        [&] { capture_position_history_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_spawn_resolution_ms,
        [&] { squad_spawn_resolution_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, static_obstacle_index_ms,
        [&] { aoe_map_static_obstacle_system(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, dynamic_obstacle_index_ms,
        [&] { aoe_dynamic_obstacle_index_system(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_command_ms,
        [&] { squad_command_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, command_ms,
        [&] { command_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, membership_cleanup_ms,
        [&] { squad_membership_cleanup_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_traffic_ms,
        [&] { squad_traffic_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_control_ms,
        [&] { squad_control_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, attack_move_acquisition_ms,
        [&] { attack_move_acquisition_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, navigation_ms,
        [&] { navigation_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, movement_intent_ms,
        [&] { movement_intent_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, local_avoidance_ms,
        [&] { local_avoidance_intent_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, unit_flow_ms,
        [&] { unit_flow_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, motion_safety_ms,
        [&] { global_motion_safety_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, movement_ms,
        [&] { movement_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, combat_ms,
        [&] { combat_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, projectile_ms,
        [&] { aoe_projectile_tick(world, clock.tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, lifecycle_ms,
        [&] { lifecycle_tick(world, clock.tick); });
}

void squad_traffic_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& settings = world.resource_or_add<AoeNavigationSettings>();
    const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
    auto& index = world.resource_or_add<AoeSquadTrafficIndex>();
    index.records.clear();
    index.maximum_reach = 0.f;

    for (const auto squad : reg.view<AoeSquadMembers, AoeSquadSpawnState,
                                     AoeSquadFormation, AoeSquadOrder,
                                     AoeSquadState, AoePosition, AoeCollider>()) {
        const auto& spawn = reg.get<AoeSquadSpawnState>(squad);
        const auto& order = reg.get<AoeSquadOrder>(squad);
        const auto& state = reg.get<AoeSquadState>(squad);
        auto& traffic = reg.get_or_emplace<AoeSquadTrafficState>(squad);
        if ((spawn.status != AoeSquadSpawnStatus::Ready &&
             spawn.status != AoeSquadSpawnStatus::Partial) ||
            (order.type != AoeSquadOrderType::MoveTo &&
             order.type != AoeSquadOrderType::AttackMove) ||
            state.phase == AoeSquadPhase::Idle) {
            traffic.mode = AoeSquadTrafficMode::Clear;
            traffic.peer = entt::null;
            traffic.speed_scale = 1.f;
            traffic.target_lateral_offset = 0.f;
            continue;
        }
        glm::vec2 direction = reg.get<AoeSquadFormation>(squad).forward;
        if (const auto* guide = reg.try_get<AoeNavigationPath>(squad);
            guide && !guide->no_path && guide->current < guide->waypoints.size())
            direction = guide->waypoints[guide->current] -
                        reg.get<AoePosition>(squad).value;
        if (glm::length(direction) <= Epsilon) continue;
        direction = glm::normalize(direction);
        traffic.desired_velocity = direction * state.movement_speed;
        const bool hold_decision =
            traffic.mode != AoeSquadTrafficMode::Clear &&
            traffic.mode != AoeSquadTrafficMode::Recovering &&
            tick >= traffic.last_conflict_tick &&
            tick - traffic.last_conflict_tick <=
                settings.squad_traffic_hold_ticks;
        if (!hold_decision) {
            traffic.mode = traffic.mode == AoeSquadTrafficMode::Clear
                ? AoeSquadTrafficMode::Clear : AoeSquadTrafficMode::Recovering;
            traffic.peer = entt::null;
            traffic.speed_scale = 1.f;
            traffic.target_lateral_offset = 0.f;
        }
        float member_radius = Epsilon;
        if (const auto* members = reg.try_get<AoeSquadMembers>(squad))
            for (const auto& member : members->active)
                if (squad_member_valid(reg, member)) {
                    const auto& collider = reg.get<AoeCollider>(member.entity);
                    member_radius = std::max(member_radius,
                        std::max(collider.radius_x, collider.radius_y));
                    break;
                }
        index.records.push_back({squad, reg.get<AoePosition>(squad).value,
            direction, state.movement_speed,
            std::max(reg.get<AoeCollider>(squad).radius_x,
                     reg.get<AoeCollider>(squad).radius_y),
            member_radius,
            static_cast<std::uint64_t>(entt::to_integral(squad))});
        index.maximum_reach = std::max(index.maximum_reach,
            index.records.back().radius + index.records.back().speed *
                settings.squad_traffic_prediction_seconds);
    }

    std::sort(index.records.begin(), index.records.end(),
        [](const AoeSquadTrafficRecord& a, const AoeSquadTrafficRecord& b) {
            if (std::abs(a.center.x - b.center.x) > Epsilon)
                return a.center.x < b.center.x;
            return a.stable_id < b.stable_id;
        });

    const auto set_passing = [&](const AoeSquadTrafficRecord& value,
                                 const AoeSquadTrafficRecord& peer,
                                 int side) {
        auto& traffic = reg.get<AoeSquadTrafficState>(value.squad);
        traffic.mode = side < 0 ? AoeSquadTrafficMode::PassingRight
                                : AoeSquadTrafficMode::PassingLeft;
        traffic.peer = peer.squad;
        // Steering angles use positive for left/CCW, while formation lateral
        // offsets use positive for the local right axis.
        traffic.negotiated_side = static_cast<std::int8_t>(side);
        traffic.target_lateral_offset = side < 0
            ? settings.squad_traffic_lateral_clearance
            : -settings.squad_traffic_lateral_clearance;
        ++traffic.conflict_ticks;
        traffic.last_conflict_tick = tick;
    };
    const auto corridor_open = [&](const AoeSquadTrafficRecord& value,
                                   int side) {
        const auto* map = world.try_resource<AoeLogicMap>();
        if (!map || !map->valid()) return true;
        const glm::vec2 right{value.direction.y, -value.direction.x};
        const glm::vec2 offset = right *
            (side < 0 ? settings.squad_traffic_lateral_clearance
                      : -settings.squad_traffic_lateral_clearance);
        return map->static_safe_fraction(value.center, value.center + offset,
            glm::vec2(value.member_radius)) >= 1.f - Epsilon;
    };
    for (std::size_t i = 0; i < index.records.size(); ++i)
        for (std::size_t j = i + 1; j < index.records.size(); ++j) {
            const auto& a = index.records[i];
            const auto& b = index.records[j];
            const float a_reach = a.radius + a.speed *
                settings.squad_traffic_prediction_seconds;
            if (b.center.x - a.center.x > a_reach + index.maximum_reach)
                break;
            const glm::vec2 relative = b.center - a.center;
            const float range = a.radius + b.radius +
                (a.speed + b.speed) * settings.squad_traffic_prediction_seconds;
            if (glm::length(relative) > range) continue;
            const float alignment = glm::dot(a.direction, b.direction);
            const glm::vec2 relative_velocity =
                a.direction * a.speed - b.direction * b.speed;
            const float relative_speed2 = glm::dot(relative_velocity, relative_velocity);
            const float closest_time = relative_speed2 > Epsilon
                ? std::clamp(glm::dot(relative, relative_velocity) /
                                 relative_speed2,
                             0.f, settings.squad_traffic_prediction_seconds)
                : 0.f;
            const glm::vec2 closest = relative - relative_velocity * closest_time;
            if (glm::length(closest) > a.radius + b.radius +
                                      settings.squad_traffic_follow_gap)
                continue;
            ++world.resource_or_add<AoeGameplayDiagnostics>()
                  .squad_traffic_conflicts;
            if (alignment >= settings.squad_traffic_same_direction_dot) {
                const bool a_behind = glm::dot(relative, a.direction) > 0.f;
                const auto& follower = a_behind ? a : b;
                const auto& leader = a_behind ? b : a;
                auto& traffic = reg.get<AoeSquadTrafficState>(follower.squad);
                traffic.mode = AoeSquadTrafficMode::Following;
                traffic.peer = leader.squad;
                const float speed_scale =
                    leader.speed > Epsilon && follower.speed > Epsilon
                    ? std::clamp(leader.speed / follower.speed, 0.f, 1.f) : 0.f;
                const float surface_gap = std::max(0.f,
                    glm::length(leader.center - follower.center) -
                    leader.radius - follower.radius);
                const float gap_scale = settings.squad_traffic_follow_gap > Epsilon
                    ? std::clamp(surface_gap /
                                     settings.squad_traffic_follow_gap,
                                 0.f, 1.f)
                    : 1.f;
                traffic.speed_scale = std::min(speed_scale, gap_scale);
                ++traffic.conflict_ticks;
                traffic.last_conflict_tick = tick;
            } else if (alignment <= settings.squad_traffic_head_on_dot) {
                int side = -1;
                if (!corridor_open(a, side) || !corridor_open(b, side))
                    side = 1;
                if (corridor_open(a, side) && corridor_open(b, side)) {
                    set_passing(a, b, side);
                    set_passing(b, a, side);
                } else {
                    const auto& yielding = a.stable_id > b.stable_id ? a : b;
                    const auto& priority = a.stable_id > b.stable_id ? b : a;
                    auto& traffic = reg.get<AoeSquadTrafficState>(yielding.squad);
                    traffic.mode = AoeSquadTrafficMode::Yielding;
                    traffic.peer = priority.squad;
                    traffic.speed_scale = settings.squad_traffic_yield_speed_scale;
                    ++traffic.conflict_ticks;
                    traffic.last_conflict_tick = tick;
                }
            } else {
                const auto& yielding = a.stable_id > b.stable_id ? a : b;
                const auto& priority = a.stable_id > b.stable_id ? b : a;
                auto& traffic = reg.get<AoeSquadTrafficState>(yielding.squad);
                traffic.mode = AoeSquadTrafficMode::Yielding;
                traffic.peer = priority.squad;
                traffic.speed_scale = settings.squad_traffic_yield_speed_scale;
                ++traffic.conflict_ticks;
                traffic.last_conflict_tick = tick;
            }
        }

    const float offset_step = std::max(0.f,
        settings.squad_traffic_offset_rate) * dt;
    for (const auto& record : index.records) {
        auto& traffic = reg.get<AoeSquadTrafficState>(record.squad);
        const float delta = traffic.target_lateral_offset - traffic.lateral_offset;
        traffic.lateral_offset += std::clamp(delta, -offset_step, offset_step);
        if (std::abs(traffic.lateral_offset) <= Epsilon &&
            traffic.mode == AoeSquadTrafficMode::Recovering) {
            traffic.lateral_offset = 0.f;
            traffic.mode = AoeSquadTrafficMode::Clear;
            traffic.conflict_ticks = 0;
        }
        if (glm::length(traffic.desired_velocity) > Epsilon)
            traffic.last_progress_tick = tick;
    }
}
} // namespace

std::string PresentationDefinition::animation(const std::string& semantic) const {
    const auto it = animations.find(semantic);
    return it == animations.end() ? std::string{} : it->second;
}

glm::vec2 aoe_interpolated_position(
    const AoePosition& current, const AoePositionHistory* history,
    const AoeGameplayClock& clock, const AoeGameplaySettings& settings) {
    if (!history || !(settings.fixed_dt > 0.0) ||
        !std::isfinite(settings.fixed_dt))
        return current.value;
    const float alpha = static_cast<float>(std::clamp(
        clock.accumulator / settings.fixed_dt, 0.0, 1.0));
    return history->previous + (current.value - history->previous) * alpha;
}

void AoePathfinderRegistry::bind_erased(std::string id, FindFn function) {
    if (id.empty() || !function)
        throw std::invalid_argument(
            "pathfinder binding requires a non-empty id and function");
    if (!entries_.emplace(std::move(id), function).second)
        throw std::invalid_argument("duplicate pathfinder binding");
}

bool AoePathfinderRegistry::contains(std::string_view id) const {
    return entries_.find(std::string(id)) != entries_.end();
}

AoePathResult AoePathfinderRegistry::find(
    std::string_view id, EcsWorld& world,
    const AoePathRequest& request) const {
    const auto it = entries_.find(std::string(id));
    return it == entries_.end()
        ? AoePathResult{AoePathStatus::NoPath}
        : it->second(world, request);
}

void AoeSteeringRegistry::bind_erased(std::string id, SteerFn function) {
    if (id.empty() || !function)
        throw std::invalid_argument(
            "steering binding requires a non-empty id and function");
    if (!entries_.emplace(std::move(id), function).second)
        throw std::invalid_argument("duplicate steering binding");
}

bool AoeSteeringRegistry::contains(std::string_view id) const {
    return entries_.find(std::string(id)) != entries_.end();
}

AoeSteeringResult AoeSteeringRegistry::steer(
    std::string_view id, const AoeSteeringContext& context) const {
    const auto it = entries_.find(std::string(id));
    auto result = it == entries_.end()
        ? AoeSteeringResult{context.preferred_velocity}
        : it->second(context);
    const float speed = glm::length(result.target_velocity);
    result.infeasible =
        glm::length(context.preferred_velocity) > Epsilon && speed <= Epsilon;
    return result;
}

AoeSteeringResult DefaultLocalSteeringLogic::steer(
    const AoeSteeringContext& context) {
    const float preferred_speed = glm::length(context.preferred_velocity);
    if (!(preferred_speed > Epsilon) || !(context.max_speed > 0.f))
        return {};

    const glm::vec2 preferred = context.preferred_velocity / preferred_speed;
    const glm::vec2 current = glm::length(context.current_velocity) > Epsilon
        ? glm::normalize(context.current_velocity) : preferred;
    const float horizon = std::max(0.f, context.prediction_seconds);
    bool threatened = false;
    for (const auto& neighbor : context.neighbors) {
        const glm::vec2 relative_position = neighbor.position - context.position;
        const glm::vec2 relative_velocity =
            context.preferred_velocity - neighbor.velocity;
        const float relative_speed2 = glm::dot(relative_velocity,
                                               relative_velocity);
        const float closest_time = relative_speed2 > Epsilon
            ? std::clamp(glm::dot(relative_position, relative_velocity) /
                             relative_speed2,
                         0.f, horizon)
            : 0.f;
        const glm::vec2 separation = relative_position -
            relative_velocity * closest_time;
        const glm::vec2 combined = context.radii + neighbor.radii +
            glm::vec2(std::max(0.f, context.separation_padding));
        const float normalized2 =
            separation.x * separation.x / (combined.x * combined.x) +
            separation.y * separation.y / (combined.y * combined.y);
        if (normalized2 < 4.f) { threatened = true; break; }
    }

    const float desired_feeler = std::max(
        context.max_speed * horizon,
        std::max(context.radii.x, context.radii.y) * 1.5f);
    const float feeler = std::min(
        desired_feeler,
        std::max(Epsilon, glm::length(context.goal - context.position)));
    float straight_clearance = 1.f;
    if (context.map && context.map->valid() && horizon > 0.f)
        straight_clearance = context.map->static_safe_fraction(
            context.position, context.position + preferred * feeler,
            context.radii);
    if (!threatened && straight_clearance >= 1.f - Epsilon)
        return {context.preferred_velocity, context.preferred_avoidance_side,
                false};

    const std::uint64_t initial_side_seed = context.neighbors.empty()
        ? context.instance_id
        : (context.instance_id ^ context.neighbors.front().instance_id);
    const int held_side = context.preferred_avoidance_side == 0
        ? ((initial_side_seed & 1u) != 0u ? 1 : -1)
        : (context.preferred_avoidance_side > 0 ? 1 : -1);
    const float angle_step = std::clamp(
        context.candidate_angle_step, .0872664626f, .78539816339f);
    const float max_angle = std::clamp(
        context.candidate_max_angle, angle_step, 1.57079632679f);
    const int steps = std::clamp(
        static_cast<int>(std::ceil(max_angle / angle_step)), 1, 4);

    float best_score = -std::numeric_limits<float>::infinity();
    glm::vec2 best = context.preferred_velocity;
    int best_side = held_side;
    // Test the negotiated/held side first so numerically equal candidates are
    // deterministic. The widest escape fan reaches the wall tangent (90 deg)
    // but deliberately contains no backward direction.
    for (int candidate = 0; candidate <= steps * 2; ++candidate) {
        int signed_step = 0;
        if (candidate > 0) {
            const int magnitude = (candidate + 1) / 2;
            signed_step = (candidate & 1) ? held_side * magnitude
                                          : -held_side * magnitude;
        }
        const float angle = std::clamp(
            static_cast<float>(signed_step) * angle_step,
            -max_angle, max_angle);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const glm::vec2 direction{
            preferred.x * cosine - preferred.y * sine,
            preferred.x * sine + preferred.y * cosine};
        const int side = signed_step == 0 ? 0 : (signed_step > 0 ? 1 : -1);
        const glm::vec2 velocity = direction *
            std::min(preferred_speed, context.max_speed);
        float static_clearance = side == 0 ? straight_clearance : 1.f;
        if (side != 0 && context.map && context.map->valid() && horizon > 0.f)
            static_clearance = context.map->static_safe_fraction(
                context.position, context.position + direction * feeler,
                context.radii);

        float dynamic_penalty = 0.f;
        float nearest_margin = 4.f;
        for (const auto& neighbor : context.neighbors) {
            const glm::vec2 relative_position =
                neighbor.position - context.position;
            const glm::vec2 relative_velocity = velocity - neighbor.velocity;
            const float relative_speed2 = glm::dot(
                relative_velocity, relative_velocity);
            const float closest_time = relative_speed2 > Epsilon
                ? std::clamp(glm::dot(relative_position, relative_velocity) /
                                 relative_speed2,
                             0.f, horizon)
                : 0.f;
            const glm::vec2 separation = relative_position -
                relative_velocity * closest_time;
            const glm::vec2 combined = context.radii + neighbor.radii +
                glm::vec2(std::max(0.f, context.separation_padding));
            const float normalized2 =
                separation.x * separation.x / (combined.x * combined.x) +
                separation.y * separation.y / (combined.y * combined.y);
            nearest_margin = std::min(nearest_margin, normalized2);
            if (normalized2 < 1.f)
                dynamic_penalty += (1.f - normalized2) * 12.f + 4.f;
            else if (normalized2 < 4.f)
                dynamic_penalty += (4.f - normalized2) * .25f;
        }

        const float progress = glm::dot(direction, preferred);
        const float continuity = glm::dot(direction, current);
        const float usable_distance = static_clearance * feeler;
        float score = progress * 3.f + continuity * .75f +
                      static_clearance * 8.f + usable_distance * 2.f +
                      std::min(nearest_margin, 4.f) * .15f - dynamic_penalty;
        if (static_clearance < std::clamp(
                context.minimum_safe_fraction, 0.f, 1.f))
            score -= 100.f;
        if (side == held_side)
            score += std::max(0.f, context.side_switch_margin);
        if (score > best_score) {
            best_score = score;
            best = velocity;
            best_side = side == 0 ? held_side : side;
        }
    }
    return {best, best_side, true};
}

AoePathResult DirectPathfinderLogic::find(
    EcsWorld&, const AoePathRequest& request) {
    if (!std::isfinite(request.start.x) || !std::isfinite(request.start.y))
        return {AoePathStatus::InvalidStart};
    if (!std::isfinite(request.goal.x) || !std::isfinite(request.goal.y))
        return {AoePathStatus::InvalidGoal};
    return {AoePathStatus::Ready, {request.goal}, 0};
}

AoePathResult GridAStarPathfinderLogic::find(
    EcsWorld& world, const AoePathRequest& request) {
    auto* map = world.try_resource<AoeLogicMap>();
    if (!map || !map->valid()) return DirectPathfinderLogic::find(world, request);
    if (!std::isfinite(request.start.x) || !std::isfinite(request.start.y) ||
        !map->contains(request.start, request.clearance))
        return {AoePathStatus::InvalidStart, {}, map->static_revision()};
    if (!std::isfinite(request.goal.x) || !std::isfinite(request.goal.y) ||
        !map->contains(request.goal, request.clearance) ||
        map->position_blocked(request.goal, request.clearance))
        return {AoePathStatus::InvalidGoal, {}, map->static_revision()};
    const auto start_cell = map->world_to_cell(request.start);
    const auto goal_cell = map->world_to_cell(request.goal);
    if (!start_cell || !goal_cell)
        return {AoePathStatus::NoPath, {}, map->static_revision()};

    const std::size_t width = map->width();
    const std::size_t count = width * map->height();
    const auto index_of = [width](int x, int y) {
        return static_cast<std::size_t>(y) * width +
               static_cast<std::size_t>(x);
    };
    const auto coords = [width](std::size_t index) {
        return AoeMapCell{static_cast<int>(index % width),
                          static_cast<int>(index / width)};
    };
    const auto heuristic = [](int x, int y, int gx, int gy) {
        const int dx = std::abs(gx - x);
        const int dy = std::abs(gy - y);
        return static_cast<float>(std::max(dx, dy)) +
               .41421356237f * static_cast<float>(std::min(dx, dy));
    };
    const std::size_t start = index_of(start_cell->x, start_cell->y);
    const std::size_t goal = index_of(goal_cell->x, goal_cell->y);
    auto& workspace = world.resource_or_add<AStarWorkspace>();
    workspace.begin(count);
    workspace.generations[start] = workspace.generation;
    workspace.costs[start] = 0.f;
    workspace.parents[start] = start;
    workspace.open.push_back({start, 0.f,
        heuristic(start_cell->x, start_cell->y,
                  goal_cell->x, goal_cell->y)});
    std::push_heap(workspace.open.begin(), workspace.open.end(),
                   AStarOpenCompare{});

    const auto* dynamic = request.include_dynamic_obstacles
        ? world.try_resource<AoeDynamicObstacleIndex>() : nullptr;
    const auto clear_segment = [&](glm::vec2 from, glm::vec2 to) {
        if (map->static_safe_fraction(from, to, request.clearance) < 1.f)
            return false;
        return !dynamic || dynamic->dynamic_safe_fraction(
            *map, from, to, request.clearance, request.subject,
            request.squad) >= 1.f;
    };
    const auto traversable = [&](int x, int y, bool is_goal = false) {
        if (!map->cell_traversable(x, y, request.clearance)) return false;
        return !dynamic || !dynamic->cell_occupied(
            *map, x, y, request.clearance, request.subject, request.squad,
            is_goal ? request.ignored_dynamic_target : entt::null);
    };
    constexpr std::array<AoeMapCell, 8> Neighbors{{
        {1, 0}, {0, 1}, {-1, 0}, {0, -1},
        {1, 1}, {-1, 1}, {-1, -1}, {1, -1}}};
    bool found = false;
    while (!workspace.open.empty()) {
        std::pop_heap(workspace.open.begin(), workspace.open.end(),
                      AStarOpenCompare{});
        const auto current = workspace.open.back();
        workspace.open.pop_back();
        if (workspace.closed[current.index] == workspace.generation) continue;
        if (workspace.generations[current.index] != workspace.generation ||
            current.cost > workspace.costs[current.index] + Epsilon) continue;
        workspace.closed[current.index] = workspace.generation;
        if (current.index == goal) { found = true; break; }
        const auto cell = coords(current.index);
        for (const auto offset : Neighbors) {
            const int nx = cell.x + offset.x;
            const int ny = cell.y + offset.y;
            if (nx < 0 || ny < 0 || nx >= static_cast<int>(map->width()) ||
                ny >= static_cast<int>(map->height())) continue;
            const std::size_t next = index_of(nx, ny);
            if (!traversable(nx, ny, next == goal)) continue;
            const bool diagonal = offset.x != 0 && offset.y != 0;
            if (diagonal &&
                (!traversable(cell.x + offset.x, cell.y) ||
                 !traversable(cell.x, cell.y + offset.y))) continue;
            const glm::vec2 edge_from = current.index == start
                ? request.start : map->cell_center(cell.x, cell.y);
            const glm::vec2 edge_to = next == goal
                ? request.goal : map->cell_center(nx, ny);
            if (!clear_segment(edge_from, edge_to)) continue;
            const float next_cost = workspace.costs[current.index] +
                (diagonal ? 1.41421356237f : 1.f);
            if (workspace.generations[next] == workspace.generation &&
                next_cost + Epsilon >= workspace.costs[next]) continue;
            workspace.generations[next] = workspace.generation;
            workspace.costs[next] = next_cost;
            workspace.parents[next] = current.index;
            workspace.open.push_back({next, next_cost, next_cost +
                heuristic(nx, ny, goal_cell->x, goal_cell->y)});
            std::push_heap(workspace.open.begin(), workspace.open.end(),
                           AStarOpenCompare{});
        }
    }
    if (!found)
        return {AoePathStatus::NoPath, {}, map->static_revision()};

    std::vector<glm::vec2> raw;
    for (std::size_t cursor = goal;; cursor = workspace.parents[cursor]) {
        const auto cell = coords(cursor);
        raw.push_back(map->cell_center(cell.x, cell.y));
        if (cursor == start) break;
    }
    std::reverse(raw.begin(), raw.end());
    if (!raw.empty() && glm::length(raw.front() - request.start) > Epsilon)
        raw.insert(raw.begin(), request.start);
    if (!raw.empty()) raw.back() = request.goal;
    std::vector<glm::vec2> simplified;
    std::size_t cursor = 0;
    while (cursor + 1 < raw.size()) {
        std::size_t next = raw.size() - 1;
        while (next > cursor + 1 && !clear_segment(raw[cursor], raw[next]))
            --next;
        simplified.push_back(raw[next]);
        cursor = next;
    }
    if (raw.size() == 1) simplified.push_back(request.goal);
    return {AoePathStatus::Ready, std::move(simplified),
            map->static_revision()};
}

void aoe_map_static_obstacle_system(EcsWorld& world) {
    auto* map = world.try_resource<AoeLogicMap>();
    if (!map || !map->valid()) return;
    auto& reg = world.reg();
    auto& bindings = world.resource_or_add<AoeStaticObstacleBindings>();
    for (auto it = bindings.entities.begin(); it != bindings.entities.end();) {
        if (!reg.valid(it->first) ||
            !reg.all_of<AoeMapStaticObstacle, AoePosition>(it->first)) {
            map->remove_static_obstacle(it->second);
            it = bindings.entities.erase(it);
        } else ++it;
    }
    for (const auto entity : reg.view<AoeMapStaticObstacle, AoePosition>()) {
        auto& component = reg.get<AoeMapStaticObstacle>(entity);
        const auto center = reg.get<AoePosition>(entity).value;
        AoeStaticObstacleDesc desc;
        desc.shape = component.shape;
        desc.center = center;
        desc.half_extents = component.half_extents;
        desc.radius = component.radius;
        const bool changed = component.obstacle_id == 0 ||
            component.registered_center != center ||
            component.registered_shape != component.shape ||
            component.registered_half_extents != component.half_extents ||
            component.registered_radius != component.radius;
        if (!changed) continue;
        if (!component.obstacle_id)
            component.obstacle_id = map->add_static_obstacle(desc);
        else if (!map->update_static_obstacle(component.obstacle_id, desc))
            component.obstacle_id = map->add_static_obstacle(desc);
        if (!component.obstacle_id) continue;
        bindings.entities[entity] = component.obstacle_id;
        component.registered_center = center;
        component.registered_shape = component.shape;
        component.registered_half_extents = component.half_extents;
        component.registered_radius = component.radius;
    }
}

void aoe_dynamic_obstacle_index_system(EcsWorld& world) {
    auto& index = world.resource_or_add<AoeDynamicObstacleIndex>();
    auto* map = world.try_resource<AoeLogicMap>();
    if (!map || !map->valid()) { index.clear(); return; }
    auto& reg = world.reg();
    const auto view = reg.view<AoePosition, AoeCollider, AoeGameplayIdentity>(
        entt::exclude<AoePooledUnit, AoeRecyclePending>);
    index.reset(*map);
    index.reserve(view.size_hint());
    for (const auto entity : view) {
        const auto& collider = reg.get<AoeCollider>(entity);
        if (!(collider.radius_x > 0.f) || !(collider.radius_y > 0.f)) continue;
        entt::entity squad = entt::null;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity))
            squad = member->squad;
        index.insert(*map, {entity,
            reg.get<AoeGameplayIdentity>(entity).instance_id, squad,
            reg.get<AoePosition>(entity).value,
            {collider.radius_x, collider.radius_y},
            reg.all_of<AoeLocomotionState>(entity)
                ? reg.get<AoeLocomotionState>(entity).velocity
                : glm::vec2{0.f}});
    }
    index.finalize(*map);
}

void AoeFormationRegistry::bind_erased(
    AoeFormationType type, LayoutFn function) {
    if (!function)
        throw std::invalid_argument("formation binding requires a function");
    if (!entries_.emplace(type, function).second)
        throw std::invalid_argument("duplicate formation binding");
}

bool AoeFormationRegistry::contains(AoeFormationType type) const {
    return entries_.contains(type);
}

std::vector<AoeFormationSlot> AoeFormationRegistry::layout(
    AoeFormationType type, const AoeFormationContext& context) const {
    const auto it = entries_.find(type);
    if (it == entries_.end()) return {};
    auto result = it->second(context);
    if (result.size() != context.members.size()) return {};
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto& slot = result[i];
        if (slot.unit.entity == entt::null ||
            !std::isfinite(slot.local_offset.x) ||
            !std::isfinite(slot.local_offset.y)) return {};
        bool belongs = false;
        for (const auto& member : context.members)
            if (member.unit.entity == slot.unit.entity &&
                member.unit.instance_id == slot.unit.instance_id) {
                belongs = true; break;
            }
        if (!belongs) return {};
        for (std::size_t j = 0; j < i; ++j)
            if (result[j].unit.entity == slot.unit.entity &&
                result[j].unit.instance_id == slot.unit.instance_id) return {};
    }
    return result;
}

std::optional<AoeUnitTarget> dispatch_aoe_target(
    AoeTargetAcquisitionType type, const EcsWorld& world,
    const AoeTargetAcquisitionContext& context) {
    switch (type) {
    case AoeTargetAcquisitionType::NearestEnemy:
        return select_aoe_target<
            AoeTargetAcquisitionType::NearestEnemy>(world, context);
    }
    return std::nullopt;
}

std::string_view aoe_target_acquisition_name(
    AoeTargetAcquisitionType type) {
    switch (type) {
    case AoeTargetAcquisitionType::NearestEnemy:
        return "nearest_enemy";
    }
    return "unknown";
}

std::optional<AoeUnitTarget> NearestEnemyAcquisitionStrategy::select(
    const EcsWorld& world, const AoeTargetAcquisitionContext& context) {
    const auto& reg = world.reg();
    if (context.seeker == entt::null || !reg.valid(context.seeker) ||
        !std::isfinite(context.radius) || context.radius < 0.f ||
        !reg.all_of<AoePosition, AoeCollider>(context.seeker))
        return std::nullopt;

    const AoePosition seeker_position{context.origin};
    const auto& seeker_collider = reg.get<AoeCollider>(context.seeker);
    std::optional<AoeUnitTarget> best;
    float best_gap = std::numeric_limits<float>::infinity();
    const auto consider = [&](entt::entity candidate,
                              std::uint64_t instance_id) {
        if (!reg.valid(candidate) ||
            !reg.all_of<AoeTeam, AoePosition, AoeCollider, AoeHealth,
                        AoeActionState, AoeGameplayIdentity,
                        AoeUnitDefinitionRef>(candidate))
            return;
        if (candidate == context.seeker ||
            reg.get<AoeTeam>(candidate).id == context.seeker_team)
            return;
        const auto& identity = reg.get<AoeGameplayIdentity>(candidate);
        const AoeUnitTarget target{candidate, instance_id};
        if (std::find_if(context.excluded.begin(), context.excluded.end(),
                [&](const AoeUnitTarget& value) {
                    return value.entity == target.entity &&
                           value.instance_id == target.instance_id;
                }) != context.excluded.end())
            return;
        if (!target_valid(reg, target)) return;
        const float gap = aoe_surface_gap(
            seeker_position, seeker_collider,
            reg.get<AoePosition>(candidate), reg.get<AoeCollider>(candidate));
        if (!context.use_candidates && gap > context.radius + Epsilon) return;
        const bool closer = gap + Epsilon < best_gap;
        const bool tied = std::abs(gap - best_gap) <= Epsilon;
        const bool stable_before = !best ||
            identity.instance_id < best->instance_id ||
            (identity.instance_id == best->instance_id &&
             entt::to_integral(candidate) < entt::to_integral(best->entity));
        if (closer || (tied && stable_before)) {
            best = target;
            best_gap = gap;
        }
    };
    if (context.use_candidates) {
        for (const auto& candidate : context.candidates)
            consider(candidate.entity, candidate.instance_id);
    } else {
        for (const auto candidate : reg.view<
                 AoeTeam, AoePosition, AoeCollider, AoeHealth,
                 AoeActionState, AoeGameplayIdentity, AoeUnitDefinitionRef>(
                 entt::exclude<AoePooledUnit, AoeRecyclePending>))
            consider(candidate,
                reg.get<AoeGameplayIdentity>(candidate).instance_id);
    }
    return best;
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

float aoe_collider_support_radius(const AoeCollider& collider, glm::vec2 direction) {
    const float length = glm::length(direction);
    if (length <= Epsilon) return 0.f;
    direction /= length;
    const float x = collider.radius_x * direction.x;
    const float y = collider.radius_y * direction.y;
    return std::sqrt(x * x + y * y);
}

float aoe_surface_gap(const AoePosition& a_position, const AoeCollider& a_collider,
                      const AoePosition& b_position, const AoeCollider& b_collider) {
    const glm::vec2 delta = b_position.value - a_position.value;
    const float distance = glm::length(delta);
    if (distance <= Epsilon) return -(std::max(a_collider.radius_x, a_collider.radius_y) +
                                      std::max(b_collider.radius_x, b_collider.radius_y));
    return distance - aoe_collider_support_radius(a_collider, delta) -
           aoe_collider_support_radius(b_collider, -delta);
}

std::shared_ptr<void> AoeUnitDefinitionLoader::load_cpu(
    const AoeUnitDefinitionDesc& desc, const IFileSystem& fs) {
    try {
        const auto text = fs.read_text(desc.path());
        if (!text) return nullptr;
        return parse_definition(json::parse(*text));
    } catch (const std::exception& error) {
        std::fprintf(stderr, "[aoe] failed to parse %s: %s\n", desc.path().c_str(), error.what());
        return nullptr;
    }
}

std::shared_ptr<AoeUnitDefinition> AoeUnitDefinitionLoader::finalize(
    std::shared_ptr<void> cpu, const AoeUnitDefinitionDesc&) {
    return std::static_pointer_cast<AoeUnitDefinition>(std::move(cpu));
}

void AoeUnitDefinitionManager::refresh() {
    records_.clear();
    if (!server_ || !server_->fs) return;
    std::unordered_set<std::string> ids;
    for (const auto& entry : server_->fs->list(root_)) {
        if (entry.is_directory || std::filesystem::path(entry.name).extension() != ".json") continue;
        const std::string path = (std::filesystem::path(root_) / entry.name).generic_string();
        const auto text = server_->fs->read_text(path);
        if (!text) continue;
        try {
            const json source = json::parse(*text);
            const int schema = source.value("schema_version", 0);
            if ((schema != 1 && schema != 2) ||
                source.value("kind", std::string{}) != "aoe_gameplay_unit") continue;
            const std::string id = source.at("id").get<std::string>();
            if (!id.empty() && ids.insert(id).second) records_.push_back({id, path});
        } catch (const std::exception& error) {
            std::fprintf(stderr, "[aoe] invalid definition index entry %s: %s\n",
                         path.c_str(), error.what());
        }
    }
    std::sort(records_.begin(), records_.end(),
        [](const auto& a, const auto& b) { return a.id < b.id; });
}

const AoeUnitDefinitionRecord* AoeUnitDefinitionManager::find(const std::string& id) const {
    const auto it = std::lower_bound(records_.begin(), records_.end(), id,
        [](const auto& record, const std::string& value) { return record.id < value; });
    return it != records_.end() && it->id == id ? &*it : nullptr;
}

Handle<AoeUnitDefinition> AoeUnitDefinitionManager::load(const std::string& id) {
    const auto* record = find(id);
    return record && server_ ? server_->load(AoeUnitDefinitionDesc(record->path))
                             : Handle<AoeUnitDefinition>{};
}

entt::entity spawn_aoe_gameplay_unit(EcsWorld& world, const AoeUnitSpawnOptions& source) {
    auto options = source;
    auto& pool = world.resource_or_add<AoeGameplayPool>();
    entt::entity entity{entt::null};
    while (!pool.available.empty()) {
        const auto candidate = pool.available.back();
        pool.available.pop_back();
        if (world.reg().valid(candidate) && world.reg().all_of<AoePooledUnit>(candidate)) {
            entity = candidate;
            ++pool.reused;
            break;
        }
    }
    if (entity == entt::null) entity = world.spawn();
    else world.reg().remove<AoePooledUnit>(entity);
    world.reg().emplace_or_replace<Transform>(entity, Transform{});
    world.reg().emplace_or_replace<AoePosition>(entity, AoePosition{options.position});
    world.reg().emplace_or_replace<AoePositionHistory>(
        entity, AoePositionHistory{options.position});
    world.reg().emplace_or_replace<AoeGameplaySpawnRequest>(entity, AoeGameplaySpawnRequest{options});
    world.reg().remove<AoeGameplaySpawnError>(entity);
    return entity;
}

entt::entity spawn_aoe_gameplay_squad(
    EcsWorld& world, const AoeSquadSpawnOptions& options) {
    if (options.composition.empty() ||
        !std::isfinite(options.center.x) || !std::isfinite(options.center.y) ||
        !std::isfinite(options.forward.x) || !std::isfinite(options.forward.y) ||
        glm::length(options.forward) <= Epsilon ||
        !std::isfinite(options.formation_spacing) || options.formation_spacing < 0.f ||
        !std::isfinite(options.acquisition_radius) || options.acquisition_radius < 0.f ||
        !std::isfinite(options.disengage_radius) ||
        options.disengage_radius < options.acquisition_radius ||
        options.direction_count <= 0 ||
        options.player_color < 0 || options.player_color > 8)
        return entt::null;
    const auto* formations = world.try_resource<AoeFormationRegistry>();
    if (!formations || !formations->contains(options.formation)) return entt::null;
    std::uint64_t requested = 0;
    for (const auto& entry : options.composition) {
        if (entry.definition_id.empty() || !entry.count ||
            entry.player_color < 0 || entry.player_color > 8)
            return entt::null;
        requested += entry.count;
    }
    if (requested > std::numeric_limits<std::uint32_t>::max()) return entt::null;

    auto& reg = world.reg();
    const auto squad = world.spawn();
    const glm::vec2 forward = glm::normalize(options.forward);
    reg.emplace<AoePosition>(squad, AoePosition{options.center});
    reg.emplace<AoeTeam>(squad, AoeTeam{options.team_id});
    reg.emplace<AoeCollider>(squad, AoeCollider{Epsilon, Epsilon, Epsilon});
    reg.emplace<AoeSquadMembers>(squad);
    reg.emplace<AoeSquadSpawnState>(squad, AoeSquadSpawnState{
        AoeSquadSpawnStatus::Pending,
        static_cast<std::uint32_t>(requested)});
    reg.emplace<AoeSquadFormation>(squad, AoeSquadFormation{
        options.formation, options.formation_spacing, forward, {}, true, true});
    reg.emplace<AoeSquadCombatSettings>(squad, AoeSquadCombatSettings{
        options.acquisition_strategy, options.acquisition_radius,
        options.disengage_radius});
    reg.emplace<AoeSquadOrder>(squad);
    reg.emplace<AoeSquadState>(squad);

    AoeFacing initial_facing{0, options.direction_count};
    set_facing_toward(initial_facing, forward);
    std::uint32_t ordinal = 0;
    auto& members = reg.get<AoeSquadMembers>(squad);
    members.pending.reserve(static_cast<std::size_t>(requested));
    for (const auto& entry : options.composition) {
        for (std::uint32_t i = 0; i < entry.count; ++i) {
            AoeUnitSpawnOptions unit;
            unit.definition_id = entry.definition_id;
            unit.player_color = entry.player_color > 0
                ? entry.player_color : options.player_color;
            unit.team_id = options.team_id;
            unit.direction = initial_facing.direction;
            unit.direction_count = options.direction_count;
            unit.layers = options.layers;
            unit.position = options.center;
            const auto entity = spawn_aoe_gameplay_unit(world, unit);
            members.pending.push_back({entity, ordinal++, entry.definition_id});
        }
    }
    return squad;
}

static bool enqueue(EcsWorld& world, AoeGameplayCommand command) {
    if (!world.reg().valid(command.unit) || world.reg().all_of<AoePooledUnit>(command.unit) ||
        (!world.reg().all_of<AoeActionState>(command.unit) &&
         !world.reg().all_of<AoeGameplaySpawnRequest>(command.unit))) return false;
    world.resource_or_add<AoeGameplayCommands>().queue.push_back(command);
    return true;
}

bool request_aoe_attack(EcsWorld& world, entt::entity unit, entt::entity target) {
    if (!world.reg().valid(target) || unit == target) return false;
    const auto* identity = world.reg().try_get<AoeGameplayIdentity>(target);
    if (!identity) return false;
    AoeGameplayCommand command{AoeCommandType::AttackTarget, unit};
    command.target = {target, identity->instance_id};
    return enqueue(world, command);
}

bool request_aoe_attack_move(
    EcsWorld& world, entt::entity unit, glm::vec2 destination) {
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y))
        return false;
    AoeGameplayCommand command{AoeCommandType::AttackMove, unit};
    command.position = destination;
    return enqueue(world, command);
}

bool request_aoe_move(EcsWorld& world, entt::entity unit, glm::vec2 destination) {
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y)) return false;
    AoeGameplayCommand command{AoeCommandType::MoveTo, unit};
    command.position = destination;
    return enqueue(world, command);
}

bool request_aoe_stop(EcsWorld& world, entt::entity unit) {
    return enqueue(world, AoeGameplayCommand{AoeCommandType::Stop, unit});
}

static bool enqueue_squad(EcsWorld& world, AoeSquadCommand command) {
    if (!world.reg().valid(command.squad) ||
        !world.reg().all_of<AoeSquadSpawnState>(command.squad)) return false;
    world.resource_or_add<AoeSquadCommands>().queue.push_back(command);
    return true;
}

bool request_aoe_squad_move(
    EcsWorld& world, entt::entity squad, glm::vec2 destination) {
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y)) return false;
    AoeSquadCommand command{AoeSquadCommandType::MoveTo, squad};
    command.position = destination;
    return enqueue_squad(world, command);
}

bool request_aoe_squad_attack(
    EcsWorld& world, entt::entity squad, entt::entity target) {
    if (!world.reg().valid(target)) return false;
    const auto* identity = world.reg().try_get<AoeGameplayIdentity>(target);
    if (!identity) return false;
    AoeSquadCommand command{AoeSquadCommandType::AttackTarget, squad};
    command.target = {target, identity->instance_id};
    return enqueue_squad(world, command);
}

bool request_aoe_squad_attack_move(
    EcsWorld& world, entt::entity squad, glm::vec2 destination) {
    if (!std::isfinite(destination.x) || !std::isfinite(destination.y)) return false;
    AoeSquadCommand command{AoeSquadCommandType::AttackMove, squad};
    command.position = destination;
    return enqueue_squad(world, command);
}

bool request_aoe_squad_stop(EcsWorld& world, entt::entity squad) {
    return enqueue_squad(world, AoeSquadCommand{AoeSquadCommandType::Stop, squad});
}

bool set_aoe_squad_formation(
    EcsWorld& world, entt::entity squad, AoeFormationType formation) {
    AoeSquadCommand command{AoeSquadCommandType::SetFormation, squad};
    command.formation = formation;
    return enqueue_squad(world, command);
}

bool disband_aoe_gameplay_squad(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    if (!reg.valid(squad) || !reg.all_of<AoeSquadMembers>(squad)) return false;
    auto& members = reg.get<AoeSquadMembers>(squad);
    for (const auto& member : members.active)
        if (reg.valid(member.entity)) {
            reg.remove<AoeSquadMember>(member.entity);
            reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
        }
    reg.destroy(squad);
    return true;
}

bool set_aoe_unit_facing(EcsWorld& world, entt::entity unit, int direction,
                         int direction_count) {
    if (direction_count <= 0) return false;
    AoeGameplayCommand command{AoeCommandType::SetFacing, unit};
    command.integer = direction;
    command.integer2 = direction_count;
    return enqueue(world, command);
}

bool set_aoe_unit_health(EcsWorld& world, entt::entity unit, float health) {
    if (!std::isfinite(health)) return false;
    AoeGameplayCommand command{AoeCommandType::SetHealth, unit};
    command.number = health;
    return enqueue(world, command);
}

bool set_aoe_unit_level(EcsWorld& world, entt::entity unit, std::uint32_t level) {
    if (level < 1) return false;
    AoeGameplayCommand command{AoeCommandType::SetLevel, unit};
    command.integer = level;
    return enqueue(world, command);
}

double aoe_action_elapsed_seconds(const AoeActionState& state,
                                  const AoeGameplayClock& clock,
                                  const AoeGameplaySettings& settings) {
    return clock.tick >= state.state_started_tick
        ? static_cast<double>(clock.tick - state.state_started_tick) * settings.fixed_dt : 0.0;
}

void clear_aoe_gameplay_events(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    GLD_PERF_MONITOR(
        world.resource_or_add<AoeGameplayPerformanceDiagnostics>().begin_frame();
    );
    world.resource_or_add<Events<AoeActionEvent>>().clear();
    world.resource_or_add<Events<AoeNavigationEvent>>().clear();
    GLD_PERF_MONITOR(
        world.resource<AoeGameplayPerformanceDiagnostics>().clear_events_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void spawn_aoe_gameplay_unit_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    auto& reg = world.reg();
    auto& manager = world.resource<AoeUnitDefinitionManager>();
    std::vector<entt::entity> completed;
    for (auto entity : reg.view<AoeGameplaySpawnRequest>()) {
        auto& request = reg.get<AoeGameplaySpawnRequest>(entity);
        if (!request.requested) {
            request.definition = manager.load(request.options.definition_id);
            request.requested = true;
            if (request.definition.state() == LoadState::NotLoaded) {
                reg.emplace_or_replace<AoeGameplaySpawnError>(entity,
                    AoeGameplaySpawnError{"unknown definition: " + request.options.definition_id});
                completed.push_back(entity); continue;
            }
        }
        if (request.definition.state() == LoadState::Failed) {
            reg.emplace_or_replace<AoeGameplaySpawnError>(entity,
                AoeGameplaySpawnError{"definition load failed: " + request.options.definition_id});
            completed.push_back(entity); continue;
        }
        const auto* definition = request.definition.get();
        if (!definition) continue;
        auto& lifecycle = world.resource<AoeGameplayLifecycle>();
        const std::uint64_t instance = lifecycle.next_instance_id++;
        const auto& settings = world.resource<AoeGameplaySettings>();
        reg.emplace_or_replace<AoeUnitDefinitionRef>(entity, AoeUnitDefinitionRef{request.definition});
        reg.emplace_or_replace<AoeHealth>(entity, AoeHealth{definition->max_hp, definition->max_hp});
        reg.emplace_or_replace<AoeLevel>(entity, AoeLevel{definition->level});
        reg.emplace_or_replace<AoeCollider>(entity, AoeCollider{
            definition->collision.radius_x, definition->collision.radius_y, definition->collision.height});
        reg.emplace_or_replace<AoeMovement>(entity, AoeMovement{definition->movement.speed});
        reg.emplace_or_replace<AoeLocomotionState>(entity);
        reg.emplace_or_replace<AoeTeam>(entity, AoeTeam{request.options.team_id});
        const int direction_count = std::max(1, request.options.direction_count);
        reg.emplace_or_replace<AoeFacing>(entity, AoeFacing{
            ((request.options.direction % direction_count) + direction_count) % direction_count,
            direction_count});
        reg.emplace_or_replace<AoePresentationOptions>(entity, AoePresentationOptions{
            request.options.player_color > 0 ? request.options.player_color
                                             : definition->presentation.default_player_color,
            request.options.layers});
        reg.emplace_or_replace<AoeActionState>(entity, AoeActionState{
            .state_started_tick = world.resource<AoeGameplayClock>().tick});
        reg.emplace_or_replace<AoeGameplayIdentity>(entity, AoeGameplayIdentity{
            instance, mix64(settings.random_seed ^ instance)});
        reg.remove<AoeAttackOrder, AoeAttackMoveOrder, AoeMoveGoal,
                   AoeNavigationPath, AoeRecyclePending>(entity);
        completed.push_back(entity);
    }
    for (auto entity : completed)
        if (reg.valid(entity) && reg.all_of<AoeGameplaySpawnRequest>(entity))
            reg.remove<AoeGameplaySpawnRequest>(entity);
    GLD_PERF_MONITOR(
        world.resource_or_add<AoeGameplayPerformanceDiagnostics>().spawn_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void aoe_gameplay_fixed_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto* time = world.try_resource<Time>();
    if (!time || !(settings.fixed_dt > 0.0)) {
        GLD_PERF_MONITOR(
            world.resource_or_add<AoeGameplayPerformanceDiagnostics>().fixed_total_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - started).count();
        );
        return;
    }
    clock.accumulator += std::max(0.f, time->dt);
    clock.ticks_this_frame = 0;
    while (clock.accumulator + 1e-12 >= settings.fixed_dt &&
           clock.ticks_this_frame < settings.max_catchup_ticks) {
        clock.accumulator -= settings.fixed_dt;
        fixed_tick(world);
        ++clock.ticks_this_frame;
    }
    if (clock.accumulator >= settings.fixed_dt) {
        const double kept = std::fmod(clock.accumulator, settings.fixed_dt);
        clock.dropped_seconds += clock.accumulator - kept;
        clock.accumulator = kept;
    }
    GLD_PERF_MONITOR(
        world.resource_or_add<AoeGameplayPerformanceDiagnostics>().fixed_total_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void aoe_gameplay_recycle_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    auto& reg = world.reg();
    auto& pool = world.resource<AoeGameplayPool>();
    const auto view = reg.view<AoeRecyclePending>();
    std::vector<entt::entity> pending(view.begin(), view.end());
    for (auto entity : pending) {
        if (!reg.valid(entity)) continue;
        reg.remove<AoeGameplaySpawnRequest, AoeGameplaySpawnError, AoeUnitDefinitionRef,
                   AoeHealth, AoeLevel, AoeCollider, AoePosition,
                   AoePositionHistory, AoeMovement, AoeTeam,
                   AoeLocomotionState, AoePathMotionRequest,
                   AoeMovementIntent, AoeGlobalMotionState,
                   AoeGlobalMotionDecision,
                   AoeFacing,
                   AoePresentationOptions, AoeActionState, AoeGameplayIdentity,
                   AoeAttackOrder, AoeAttackMoveOrder, AoeMoveGoal,
                   AoeEngagementApproach, AoeNavigationPath, AoeSquadMember,
                   AoeSquadMoveSpeedLimit,
                   AoeMapStaticObstacle,
                   Transform>(entity);
        reg.remove<AoeRecyclePending>(entity);
        reg.emplace_or_replace<AoePooledUnit>(entity);
        pool.available.push_back(entity);
        ++pool.recycled;
    }
    GLD_PERF_MONITOR(
        world.resource_or_add<AoeGameplayPerformanceDiagnostics>().recycle_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void AoeGameplayPlugin::operator()(App& app) const {
    if (!std::isfinite(settings.fixed_dt) || settings.fixed_dt <= 0.0 ||
        settings.max_catchup_ticks == 0)
        throw std::invalid_argument("AoeGameplayPlugin requires positive finite fixed_dt and max_catchup_ticks");
    auto& server = app.world.resource<AssetServer>();
    server.register_loader<AoeUnitDefinitionDesc>(std::make_shared<AoeUnitDefinitionLoader>());
    server.register_loader<AoeMapDefinitionDesc>(
        std::make_shared<AoeMapDefinitionLoader>());
    auto& manager = app.world.add_resource<AoeUnitDefinitionManager>(server, definitions_root);
    manager.refresh();
    app.world.add_resource<AoeGameplaySettings>(settings);
    app.world.resource_or_add<AoeGameplayClock>();
    app.world.resource_or_add<AoeGameplayLifecycle>();
    app.world.resource_or_add<AoeGameplayPool>();
    app.world.resource_or_add<AoeGameplayCommands>();
    app.world.resource_or_add<AoeSquadCommands>();
    app.world.resource_or_add<Events<AoeActionEvent>>();
    app.world.resource_or_add<Events<AoeNavigationEvent>>();
    app.world.resource_or_add<AoeGameplayDiagnostics>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    app.world.resource_or_add<AoeGameplayPerformanceDiagnostics>();
#endif
    app.world.resource_or_add<AoeCrowdSteeringScratch>();
    app.world.resource_or_add<AoeSquadTrafficIndex>();
    app.world.resource_or_add<AoeUnitFlowIndex>();
    app.world.resource_or_add<AoeNavigationSettings>();
    app.world.resource_or_add<AoeDynamicObstacleIndex>();
    app.world.resource_or_add<AoeStaticObstacleBindings>();
    auto& pathfinders = app.world.resource_or_add<AoePathfinderRegistry>();
    if (!pathfinders.contains("direct"))
        pathfinders.bind<DirectPathfinderLogic>("direct");
    if (!pathfinders.contains("grid_astar"))
        pathfinders.bind<GridAStarPathfinderLogic>("grid_astar");
    auto& steering = app.world.resource_or_add<AoeSteeringRegistry>();
    if (!steering.contains("local_default"))
        steering.bind<DefaultLocalSteeringLogic>("local_default");
    auto& formations = app.world.resource_or_add<AoeFormationRegistry>();
    if (!formations.contains(AoeFormationType::Skirmish))
        formations.bind<AoeFormationType::Skirmish,
                        DefaultSkirmishFormation>();
    auto& projectiles = app.world.resource_or_add<AoeProjectileRegistry>();
    if (!projectiles.contains("arrow"))
        projectiles.bind<ArrowProjectileLogic>("arrow");
    app.add_system(Stage::First, clear_aoe_gameplay_events);
    app.add_system(Stage::PreUpdate, spawn_aoe_gameplay_unit_system);
    app.add_system(Stage::PreUpdate, aoe_gameplay_fixed_system);
    app.add_system(Stage::PostUpdate, aoe_gameplay_recycle_system);
}

} // namespace gld::ecs::aoe
