#include <aoe/AoeGameplay.hpp>
#include "AoeGameplayInternal.hpp"

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

struct FormationExecutionState {
    bool pass_through = false;
};

// Marker used to ensure the fallback only clears/reissues movement that it
// owns. Combat and direct unit orders are never removed through this path.
struct FallbackFormationOwnedMovement {
    entt::entity squad{entt::null};
    std::uint64_t squad_revision = 0;
    std::uint64_t unit_instance_id = 0;
    AoeFormationIntentMode mode = AoeFormationIntentMode::None;
};

struct FallbackFormationRecord {
    entt::entity squad{entt::null};
    std::uint64_t squad_revision = 0;
    std::uint64_t unit_instance_id = 0;
    AoeFormationIntentMode mode = AoeFormationIntentMode::None;
    glm::vec2 destination{0.f};
    std::uint64_t last_issue_tick = 0;
    bool arrived = false;
};

struct FallbackFormationCache {
    std::unordered_map<entt::entity, FallbackFormationRecord> records;
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
            formation->arrival_reflow_done = false;
            std::erase_if(formation->slots,
                [entity](const AoeFormationSlot& slot) {
                    return slot.unit.entity == entity;
                });
        }
    }
    reg.remove<AoeSquadMember>(entity);
    reg.remove<AoeSquadMoveSpeedLimit>(entity);
    reg.remove<FallbackFormationOwnedMovement>(entity);
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

std::optional<AoeUnitTarget> select_stalled_in_range_target(
    EcsWorld& world, entt::entity entity, const AoeUnitTarget& current,
    AoeTargetAcquisitionType acquisition, float attack_range) {
    auto& reg = world.reg();
    const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
    if (!locomotion || locomotion->stalled_ticks == 0 ||
        !target_valid(reg, current) || !std::isfinite(attack_range) ||
        attack_range < 0.f ||
        !reg.all_of<AoePosition, AoeCollider, AoeTeam>(entity))
        return std::nullopt;
    if (aoe_surface_gap(
            reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
            reg.get<AoePosition>(current.entity),
            reg.get<AoeCollider>(current.entity)) <= attack_range + Epsilon)
        return std::nullopt;

    const std::array excluded{current};
    auto target = dispatch_aoe_target(acquisition, world,
        AoeTargetAcquisitionContext{
            .seeker = entity,
            .origin = reg.get<AoePosition>(entity).value,
            .radius = attack_range,
            .seeker_team = reg.get<AoeTeam>(entity).id,
            .excluded = excluded});
    if (!target || !target_valid(reg, *target)) return std::nullopt;
    const float gap = aoe_surface_gap(
        reg.get<AoePosition>(entity), reg.get<AoeCollider>(entity),
        reg.get<AoePosition>(target->entity),
        reg.get<AoeCollider>(target->entity));
    return gap <= attack_range + Epsilon ? target : std::nullopt;
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

bool rematch_squad_arrival_slots(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    if (!reg.valid(squad) ||
        !reg.all_of<AoePosition, AoeSquadFormation, AoeSquadMembers>(squad))
        return false;
    auto& formation = reg.get<AoeSquadFormation>(squad);
    if (formation.slots.empty()) return true;
    const auto& members = reg.get<AoeSquadMembers>(squad);
    if (formation.slots.size() != members.active.size()) return false;

    std::vector<AoeUnitTarget> rematched;
    rematched.reserve(formation.slots.size());
    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit) ||
            !reg.all_of<AoePosition, AoeSquadMember>(slot.unit.entity))
            return false;
        const auto& membership = reg.get<AoeSquadMember>(slot.unit.entity);
        if (membership.squad != squad ||
            std::ranges::none_of(members.active,
                [&](const AoeUnitTarget& active) {
                    return active.entity == slot.unit.entity &&
                           active.instance_id == slot.unit.instance_id;
                }) ||
            std::ranges::any_of(rematched,
                [&](const AoeUnitTarget& seen) {
                    return seen.entity == slot.unit.entity &&
                           seen.instance_id == slot.unit.instance_id;
                }))
            return false;
        rematched.push_back(slot.unit);
    }

    std::vector<std::int64_t> priorities;
    priorities.reserve(formation.slots.size());
    for (const auto& slot : formation.slots)
        if (std::find(priorities.begin(), priorities.end(), slot.priority) ==
            priorities.end())
            priorities.push_back(slot.priority);

    const auto& center = reg.get<AoePosition>(squad);
    constexpr double CostEpsilon = 1e-12;
    for (const auto priority : priorities) {
        std::vector<std::size_t> slot_indices;
        std::vector<AoeUnitTarget> units;
        for (std::size_t i = 0; i < formation.slots.size(); ++i) {
            if (formation.slots[i].priority != priority) continue;
            slot_indices.push_back(i);
            units.push_back(formation.slots[i].unit);
        }
        if (units.size() <= 1) continue;
        std::stable_sort(units.begin(), units.end(),
            [&](const AoeUnitTarget& a, const AoeUnitTarget& b) {
                const auto& member_a = reg.get<AoeSquadMember>(a.entity);
                const auto& member_b = reg.get<AoeSquadMember>(b.entity);
                if (member_a.ordinal != member_b.ordinal)
                    return member_a.ordinal < member_b.ordinal;
                if (a.instance_id != b.instance_id)
                    return a.instance_id < b.instance_id;
                return entt::to_integral(a.entity) <
                       entt::to_integral(b.entity);
            });

        // Deterministic Hungarian assignment. Rows are stable members and
        // columns are stable slot indices; equal costs prefer the lower slot.
        const std::size_t count = units.size();
        std::vector<double> row_potential(count + 1, 0.0);
        std::vector<double> column_potential(count + 1, 0.0);
        std::vector<std::size_t> matched_row(count + 1, 0);
        std::vector<std::size_t> previous_column(count + 1, 0);
        for (std::size_t row = 1; row <= count; ++row) {
            matched_row[0] = row;
            std::vector<double> minimum(count + 1,
                                        std::numeric_limits<double>::infinity());
            std::vector<bool> used(count + 1, false);
            std::size_t column = 0;
            do {
                used[column] = true;
                const std::size_t current_row = matched_row[column];
                double delta = std::numeric_limits<double>::infinity();
                std::size_t next_column = 0;
                for (std::size_t candidate = 1; candidate <= count;
                     ++candidate) {
                    if (used[candidate]) continue;
                    const glm::vec2 unit_position = reg.get<AoePosition>(
                        units[current_row - 1].entity).value;
                    const glm::vec2 slot_position = squad_slot_world(
                        center, formation,
                        formation.slots[slot_indices[candidate - 1]]);
                    const glm::vec2 difference = unit_position - slot_position;
                    const double cost = static_cast<double>(
                        glm::dot(difference, difference));
                    const double reduced = cost - row_potential[current_row] -
                                           column_potential[candidate];
                    if (reduced + CostEpsilon < minimum[candidate]) {
                        minimum[candidate] = reduced;
                        previous_column[candidate] = column;
                    }
                    if (minimum[candidate] + CostEpsilon < delta ||
                        (std::abs(minimum[candidate] - delta) <= CostEpsilon &&
                         (next_column == 0 || candidate < next_column))) {
                        delta = minimum[candidate];
                        next_column = candidate;
                    }
                }
                if (next_column == 0 || !std::isfinite(delta)) return false;
                for (std::size_t candidate = 0; candidate <= count;
                     ++candidate) {
                    if (used[candidate]) {
                        row_potential[matched_row[candidate]] += delta;
                        column_potential[candidate] -= delta;
                    } else {
                        minimum[candidate] -= delta;
                    }
                }
                column = next_column;
            } while (matched_row[column] != 0);
            do {
                const std::size_t previous = previous_column[column];
                matched_row[column] = matched_row[previous];
                column = previous;
            } while (column != 0);
        }
        for (std::size_t column = 1; column <= count; ++column) {
            if (matched_row[column] == 0) return false;
            rematched[slot_indices[column - 1]] =
                units[matched_row[column] - 1];
        }
    }

    for (std::size_t i = 0; i < formation.slots.size(); ++i)
        formation.slots[i].unit = rematched[i];
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

void assign_direct_waypoint(entt::registry& reg, entt::entity entity,
                            glm::vec2 destination,
                            std::uint64_t map_revision,
                            std::uint64_t tick) {
    auto& path = reg.emplace_or_replace<AoeNavigationPath>(entity);
    path.waypoints.assign(1, destination);
    path.current = 0;
    path.requested_goal = destination;
    path.map_revision = map_revision;
    path.request_sequence = 0;
    path.last_repath_tick = tick;
    path.blocked_ticks = 0;
    path.no_path = false;
    path.include_dynamic_obstacles = false;
    path.dynamic_repath_requested = false;
    path.dynamic_repath_failed = false;
}

void drive_squad_slots_full(EcsWorld& world, entt::entity squad,
                            float speed_limit, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    const auto* map = world.try_resource<AoeLogicMap>();
    const std::uint64_t map_revision = map ? map->static_revision() : 0;
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
        // Member-route splitting is a later formation stage. Until then, a
        // member follows its current slot through one direct waypoint. This is
        // deliberately not a pathfinding request: only the squad guide may
        // query the pathfinder for a formation movement episode.
        assign_direct_waypoint(reg, entity, destination, map_revision, tick);
        auto& state = reg.get<AoeActionState>(entity);
        if (state.state == UnitState::Attacking) reset_member_action(reg, entity, tick);
    }
}

AoeFormationIntentMode fallback_intent_mode(
    const AoeSquadOrder& order, const AoeSquadState& state) {
    if (state.phase == AoeSquadPhase::Forming)
        return AoeFormationIntentMode::Form;
    if (state.phase == AoeSquadPhase::Regrouping)
        return AoeFormationIntentMode::Regroup;
    if (state.phase == AoeSquadPhase::Engaging)
        return AoeFormationIntentMode::Hold;
    if (order.type == AoeSquadOrderType::MoveTo ||
        order.type == AoeSquadOrderType::AttackMove)
        return AoeFormationIntentMode::Follow;
    return AoeFormationIntentMode::None;
}

void clear_fallback_formation_movement(
    EcsWorld& world, entt::entity squad, const AoeSquadMembers& members) {
    auto& reg = world.reg();
    auto& cache = world.resource_or_add<FallbackFormationCache>();
    for (const auto& member : members.active) {
        if (!reg.valid(member.entity)) {
            cache.records.erase(member.entity);
            continue;
        }
        const auto* owned =
            reg.try_get<FallbackFormationOwnedMovement>(member.entity);
        if (owned && owned->squad == squad) {
            if (!reg.any_of<AoeAttackOrder, AoeEngagementApproach>(
                    member.entity))
                reg.remove<AoeMoveGoal, AoeNavigationPath>(member.entity);
            reg.remove<FallbackFormationOwnedMovement>(member.entity);
        }
        reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
        cache.records.erase(member.entity);
    }
}

void drive_squad_slots_fallback(EcsWorld& world, entt::entity squad,
                                float speed_limit, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    const auto& order = reg.get<AoeSquadOrder>(squad);
    const auto& state = reg.get<AoeSquadState>(squad);
    const auto mode = fallback_intent_mode(order, state);
    if (mode == AoeFormationIntentMode::None) return;

    glm::vec2 episode_center = center.value;
    if (mode == AoeFormationIntentMode::Follow &&
        (order.type == AoeSquadOrderType::MoveTo ||
         order.type == AoeSquadOrderType::AttackMove))
        episode_center = order.destination;
    glm::vec2 forward = formation.forward;
    if (glm::length(forward) <= Epsilon) forward = {1.f, 0.f};
    else forward = glm::normalize(forward);
    const glm::vec2 right{forward.y, -forward.x};
    const auto* traffic = reg.try_get<AoeSquadTrafficState>(squad);
    const float traffic_speed = traffic
        ? std::clamp(traffic->speed_scale, 0.f, 1.f) : 1.f;
    auto& cache = world.resource_or_add<FallbackFormationCache>();
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();

    auto& intent = reg.emplace_or_replace<AoeFormationIntent>(squad);
    intent = {mode, episode_center, forward, speed_limit,
              order.revision, tick, false, false, true};

    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit)) continue;
        const auto entity = slot.unit.entity;
        if (reg.any_of<AoeAttackOrder, AoeEngagementApproach>(entity))
            continue;
        const glm::vec2 destination = episode_center +
            right * slot.local_offset.x + forward * slot.local_offset.y;
        auto [record_it, inserted] = cache.records.try_emplace(entity);
        auto& record = record_it->second;
        const bool new_episode = inserted || record.squad != squad ||
            record.squad_revision != order.revision ||
            record.unit_instance_id != slot.unit.instance_id ||
            record.mode != mode;
        if (new_episode) {
            if (const auto* owned =
                    reg.try_get<FallbackFormationOwnedMovement>(entity);
                owned && owned->squad == squad)
                reg.remove<AoeMoveGoal, AoeNavigationPath>(entity);
            record = {squad, order.revision, slot.unit.instance_id, mode,
                      destination, 0, false};
        }

        reg.emplace_or_replace<AoeSquadMoveSpeedLimit>(entity,
            AoeSquadMoveSpeedLimit{speed_limit * traffic_speed});
        const float distance = glm::length(
            reg.get<AoePosition>(entity).value - record.destination);
        if (distance <= .05f) record.arrived = true;
        if (record.arrived) continue;

        const bool owns_movement = [&] {
            const auto* owned =
                reg.try_get<FallbackFormationOwnedMovement>(entity);
            return owned && owned->squad == squad &&
                   owned->squad_revision == record.squad_revision &&
                   owned->unit_instance_id == record.unit_instance_id &&
                   owned->mode == record.mode;
        }();
        const bool has_goal = owns_movement && reg.all_of<AoeMoveGoal>(entity);
        const bool has_path = owns_movement &&
                              reg.all_of<AoeNavigationPath>(entity);
        const std::uint32_t cooldown =
            navigation.repath_cooldown_ticks +
            static_cast<std::uint32_t>(slot.unit.instance_id % 3u);
        if (!new_episode && (has_goal || has_path ||
            tick - record.last_issue_tick < std::max(1u, cooldown)))
            continue;

        reg.remove<AoeAttackMoveOrder>(entity);
        reg.emplace_or_replace<AoeMoveGoal>(entity,
            AoeMoveGoal{record.destination, 0.f, {}});
        reg.emplace_or_replace<AoeNavigationPath>(entity,
            AoeNavigationPath{{record.destination}, 0});
        reg.emplace_or_replace<FallbackFormationOwnedMovement>(entity,
            FallbackFormationOwnedMovement{squad, order.revision,
                                            slot.unit.instance_id, mode});
        record.last_issue_tick = tick;
        auto& action = reg.get<AoeActionState>(entity);
        if (action.state == UnitState::Attacking)
            reset_member_action(reg, entity, tick);
    }
}

void drive_squad_slots(EcsWorld& world, entt::entity squad,
                       float speed_limit, std::uint64_t tick) {
    const auto* execution = world.try_resource<FormationExecutionState>();
    if (execution && execution->pass_through)
        drive_squad_slots_fallback(world, squad, speed_limit, tick);
    else
        drive_squad_slots_full(world, squad, speed_limit, tick);
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
                if (state.state != UnitState::Attacking) {
                    const auto nearby = select_stalled_in_range_target(
                        world, entity, attack->target,
                        definition->target_acquisition.strategy,
                        definition->attack->range);
                    if (nearby) {
                        clear_active_engagement(reg, entity);
                        reg.emplace_or_replace<AoeAttackOrder>(entity,
                            AoeAttackOrder{*nearby});
                        assign_engagement_approach(
                            reg, entity, *nearby, *definition->attack);
                        set_idle_if_active(reg, entity, tick);
                        continue;
                    }
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
            auto& formation = reg.get<AoeSquadFormation>(squad);
            formation.dirty = true;
            formation.arrival_reflow_done = false;
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
        if (members.active.size() != old_size)
            formation.arrival_reflow_done = false;
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
    const std::uint64_t next_revision = order.revision + 1;
    if (command.type == AoeSquadCommandType::SetFormation) {
        const auto* registry = world.try_resource<AoeFormationRegistry>();
        if (!registry || !registry->contains(command.formation)) {
            reject(world); return;
        }
        formation.type = command.formation;
        formation.dirty = true;
        formation.arrival_reflow_done = false;
        order.revision = next_revision;
        if (state.phase != AoeSquadPhase::Engaging)
            state.phase = AoeSquadPhase::Regrouping;
        return;
    }
    clear_squad_member_orders(reg, members, tick);
    reg.remove<AoeNavigationPath>(command.squad);
    formation.arrival_reflow_done = false;
    if (command.type == AoeSquadCommandType::Stop) {
        reg.get<AoePosition>(command.squad).value = squad_centroid(
            reg, members, reg.get<AoePosition>(command.squad).value);
        order = {};
        order.revision = next_revision;
        state.phase = AoeSquadPhase::Idle;
        return;
    }
    if (command.type == AoeSquadCommandType::AttackTarget) {
        if (!target_valid(reg, command.target)) { reject(world); return; }
        order = {AoeSquadOrderType::AttackTarget, {}, command.target,
                 next_revision};
        state.phase = AoeSquadPhase::Engaging;
        return;
    }
    const auto& center = reg.get<AoePosition>(command.squad);
    const glm::vec2 delta = command.position - center.value;
    if (glm::length(delta) > Epsilon) formation.forward = glm::normalize(delta);
    order = {command.type == AoeSquadCommandType::AttackMove
                 ? AoeSquadOrderType::AttackMove : AoeSquadOrderType::MoveTo,
             command.position, {}, next_revision};
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

void squad_control_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    for (const auto squad : reg.view<AoeFormationIntent>())
        reg.get<AoeFormationIntent>(squad).valid = false;
    for (const auto squad : reg.view<AoeFormationResult>())
        reg.get<AoeFormationResult>(squad).valid = false;
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
        const auto* engagement =
            reg.try_get<AoeSquadEngagementResult>(squad);
        const bool attack_move_active =
            order.type == AoeSquadOrderType::AttackMove && engagement &&
            engagement->valid && engagement->produced_tick == tick &&
            engagement->status == AoeSquadEngagementStatus::Active;
        const bool engagement_ended =
            order.type == AoeSquadOrderType::AttackMove &&
            state.phase == AoeSquadPhase::Engaging &&
            !attack_move_active;

        if (order.type == AoeSquadOrderType::AttackMove) {
            order.target = {};
            if (attack_move_active) {
                formation.arrival_reflow_done = false;
                state.phase = AoeSquadPhase::Engaging;
            } else if (engagement_ended) {
                center.value = squad_centroid(reg, members, center.value);
                formation.dirty = true;
                formation.arrival_reflow_done = false;
                state.phase = AoeSquadPhase::Moving;
            }
        }

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

        if (order.target.entity != entt::null && !target_valid(reg, order.target)) {
            clear_squad_member_orders(reg, members, tick);
            order.target = {};
            center.value = squad_centroid(reg, members, center.value);
            formation.dirty = true;
            formation.arrival_reflow_done = false;
            // Attack Move keeps its center guide while combat temporarily
            // pauses formation travel. Resuming the same order must continue
            // the existing route rather than issue another path query.
            if (order.type != AoeSquadOrderType::AttackMove)
                reg.remove<AoeNavigationPath>(squad);
            if (order.type == AoeSquadOrderType::AttackTarget) {
                order.type = AoeSquadOrderType::Idle;
                state.phase = AoeSquadPhase::Regrouping;
            } else if (order.type == AoeSquadOrderType::AttackMove) {
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
            // apply_squad_command removes the old guide for every accepted
            // movement command. Consequently a missing guide means this order
            // has not been planned yet. An exhausted or failed guide is still
            // the result for this order and must not be queried again. Static
            // map revision changes are the sole in-order replan trigger.
            const bool needs_guide = !guide || guide->map_revision != revision;
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
            const bool anchor_arrived =
                guide->current >= guide->waypoints.size() &&
                glm::length(order.destination - center.value) <= Epsilon;
            if (anchor_arrived &&
                order.type == AoeSquadOrderType::AttackMove &&
                !formation.arrival_reflow_done) {
                if (!squad_slots_arrived(reg, squad) &&
                    rematch_squad_arrival_slots(world, squad))
                    drive_squad_slots(
                        world, squad, state.movement_speed, tick);
                formation.arrival_reflow_done = true;
            }
            if (anchor_arrived && squad_slots_arrived(reg, squad)) {
                clear_squad_member_orders(reg, members, tick);
                reg.remove<AoeNavigationPath>(squad);
                order = {};
                state.phase = AoeSquadPhase::Idle;
            }
            continue;
        }

        if (state.phase == AoeSquadPhase::Idle) {
            const auto* execution =
                world.try_resource<FormationExecutionState>();
            if (execution && execution->pass_through)
                clear_fallback_formation_movement(world, squad, members);
            else
                for (const auto& member : members.active)
                    if (squad_member_valid(reg, member))
                        reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
        }

        reg.emplace_or_replace<AoeFormationResult>(squad,
            AoeFormationResult{AoeFormationResultStatus::Ready, tick,
                squad_slots_arrived(reg, squad),
                formation.arrival_reflow_done, true});
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
// Dynamic-obstacle replanning is temporarily disabled. Keep this fallback in
// place so it can be restored together with the blocked-unit trigger below.
#if 0
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
#else
    (void)had_usable_path;
#endif
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
        if (reg.all_of<AoeSquadMember>(entity)) {
            // Formation members consume direct provisional waypoints until
            // the squad-center route splitter supplies full member routes.
            // They never enter the unit Pathfinder while still attached to a
            // squad, including when directly approaching a combat target.
            assign_direct_waypoint(
                reg, entity, goal.destination, map_revision, tick);
            if (state.state == UnitState::Idle) {
                state.state = UnitState::Moving;
                state.state_started_tick = tick;
            }
            continue;
        }
        auto* path = reg.try_get<AoeNavigationPath>(entity);
        const float threshold = goal.target.entity != entt::null
            ? nav_settings.slot_repath_distance : Epsilon;
        bool needs_path = !path ||
            (!path->no_path &&
             (path->waypoints.empty() || path->current >= path->waypoints.size())) ||
            (path && glm::length(path->requested_goal - goal.destination) > threshold) ||
            (path && path->map_revision != map_revision)
#if 0
            // Dynamic-obstacle replanning is temporarily disabled.
            || (path && path->dynamic_repath_requested)
#endif
            ;
        // Global pathfinding currently plans against static map geometry only.
        // Other units remain active in local/global motion and safety systems.
        constexpr bool include_dynamic = false;
#if 0
        const auto repath_cooldown = [&] {
            const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
            return nav_settings.repath_cooldown_ticks +
                static_cast<std::uint32_t>(
                    identity ? identity->instance_id % 3u : 0u);
        };
        if (path && path->dynamic_repath_failed &&
            tick - path->last_repath_tick >= repath_cooldown())
            needs_path = true;
#endif
        if (path && path->no_path) {
            if (path->map_revision != map_revision) needs_path = true;
#if 0
            else if (path->include_dynamic_obstacles &&
                     tick - path->last_repath_tick >= repath_cooldown())
                needs_path = true;
#endif
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
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            ++world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
                  .navigation_repath_units;
#endif
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

namespace {
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
            if (auto* planner = world.try_resource<
                    AoeGlobalMotionPlannerDiagnostics>();
                planner && planner->active_backend == "gpu_image")
                ++planner->authoritative_corrections;
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
            const std::uint32_t traffic_limit =
                navigation.unit_flow_backing_threshold_ticks +
                navigation.unit_flow_backing_max_ticks;
            const bool traffic_grace = intentional_motion &&
                decision->wait_ticks < traffic_limit;
            if (!traffic_grace) {
#if 0
                // Dynamic-obstacle replanning is temporarily disabled. Local
                // and global motion continue resolving persistent congestion.
                ++path.blocked_ticks;
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
#else
                path.blocked_ticks = 0;
#endif
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
    }
    GLD_PERF_MONITOR(
        diagnostics.movement_last_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - movement_started).count();
        diagnostics.movement_peak_ms = std::max(
            diagnostics.movement_peak_ms, diagnostics.movement_last_ms);
    );
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

void fixed_before_formation(EcsWorld& world, std::uint64_t tick) {
    GLD_AOE_GAMEPLAY_PHASE(world, position_history_ms,
        [&] { capture_position_history_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_spawn_resolution_ms,
        [&] { squad_spawn_resolution_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, static_obstacle_index_ms,
        [&] { aoe_map_static_obstacle_system(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, dynamic_obstacle_index_ms,
        [&] { aoe_dynamic_obstacle_index_system(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_command_ms,
        [&] { squad_command_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, command_ms,
        [&] { command_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, membership_cleanup_ms,
        [&] { squad_membership_cleanup_tick(world); });
    GLD_AOE_GAMEPLAY_PHASE(world, squad_traffic_ms,
        [&] { squad_traffic_tick(world, tick); });
}

void fixed_after_formation_before_local(
    EcsWorld& world, std::uint64_t tick) {
    GLD_AOE_GAMEPLAY_PHASE(world, attack_move_acquisition_ms,
        [&] { attack_move_acquisition_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, navigation_ms,
        [&] { navigation_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, movement_intent_ms,
        [&] { movement_intent_tick(world, tick); });
}

void fixed_after_global(EcsWorld& world, std::uint64_t tick) {
    GLD_AOE_GAMEPLAY_PHASE(world, motion_safety_ms,
        [&] { global_motion_safety_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, movement_ms,
        [&] { movement_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, combat_ms,
        [&] { combat_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, projectile_ms,
        [&] { aoe_projectile_tick(world, tick); });
    GLD_AOE_GAMEPLAY_PHASE(world, lifecycle_ms,
        [&] { lifecycle_tick(world, tick); });
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

namespace detail {
bool aoe_gameplay_target_valid(
    const entt::registry& reg, const AoeUnitTarget& target) {
    return target_valid(reg, target);
}

bool aoe_gameplay_squad_member_valid(
    const entt::registry& reg, const AoeUnitTarget& member) {
    return squad_member_valid(reg, member);
}

void aoe_gameplay_clear_active_engagement(
    entt::registry& reg, entt::entity entity) {
    clear_active_engagement(reg, entity);
}

void aoe_gameplay_reset_member_action(
    entt::registry& reg, entt::entity entity, std::uint64_t tick,
    bool reset_locomotion) {
    reset_member_action(reg, entity, tick, reset_locomotion);
}

void aoe_gameplay_assign_engagement_approach(
    entt::registry& reg, entt::entity entity,
    const AoeUnitTarget& target, const AttackDefinition& attack) {
    assign_engagement_approach(reg, entity, target, attack);
}

void aoe_gameplay_attack_with_squad_member(
    entt::registry& reg, entt::entity entity,
    const AoeUnitTarget& target, std::uint64_t tick) {
    attack_with_squad_member(reg, entity, target, tick);
}

std::optional<AoeUnitTarget>
aoe_gameplay_select_stalled_in_range_target(
    EcsWorld& world, entt::entity entity,
    const AoeUnitTarget& current,
    AoeTargetAcquisitionType acquisition, float attack_range) {
    return select_stalled_in_range_target(
        world, entity, current, acquisition, attack_range);
}

void aoe_gameplay_fixed_before_formation(
    EcsWorld& world, std::uint64_t tick) {
    fixed_before_formation(world, tick);
}

void aoe_gameplay_formation_fixed_tick(
    EcsWorld& world, std::uint64_t tick, bool pass_through) {
    world.resource_or_add<FormationExecutionState>().pass_through =
        pass_through;
    if (pass_through) {
        auto& reg = world.reg();
        auto& records = world.resource_or_add<FallbackFormationCache>().records;
        std::erase_if(records, [&](const auto& entry) {
            const auto entity = entry.first;
            const auto& record = entry.second;
            const auto* identity = reg.valid(entity)
                ? reg.try_get<AoeGameplayIdentity>(entity) : nullptr;
            const auto* member = reg.valid(entity)
                ? reg.try_get<AoeSquadMember>(entity) : nullptr;
            return !identity || !member ||
                   identity->instance_id != record.unit_instance_id ||
                   member->squad != record.squad ||
                   !reg.valid(record.squad);
        });
    }
    GLD_AOE_GAMEPLAY_PHASE(world, squad_control_ms,
        [&] { squad_control_tick(world, tick); });
}

void aoe_gameplay_fixed_after_formation_before_local(
    EcsWorld& world, std::uint64_t tick) {
    fixed_after_formation_before_local(world, tick);
}

void aoe_gameplay_fixed_after_global(EcsWorld& world, std::uint64_t tick) {
    fixed_after_global(world, tick);
}
} // namespace detail

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
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    std::size_t profiled_cells = 0;
    std::size_t profiled_segments = 0;
    const auto profiled_start = std::chrono::steady_clock::now();
    struct FindProfiler {
        EcsWorld& world;
        std::chrono::steady_clock::time_point start;
        const std::size_t& cells;
        const std::size_t& segments;
        ~FindProfiler() {
            auto& diagnostics =
                world.resource_or_add<AoeGameplayPerformanceDiagnostics>();
            const double ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            ++diagnostics.navigation_astar_calls;
            diagnostics.navigation_astar_find_ms += ms;
            diagnostics.navigation_astar_find_peak_ms =
                std::max(diagnostics.navigation_astar_find_peak_ms, ms);
            diagnostics.navigation_astar_cells_expanded += cells;
            diagnostics.navigation_clear_segment_calls += segments;
        }
    } find_profiler{world, profiled_start, profiled_cells, profiled_segments};
#endif
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

#if 0
    // Dynamic units are intentionally excluded from global path planning.
    // Preserve the lookup for a future opt-in restoration.
    const auto* dynamic = request.include_dynamic_obstacles
        ? world.try_resource<AoeDynamicObstacleIndex>() : nullptr;
#else
    const AoeDynamicObstacleIndex* dynamic = nullptr;
#endif
    const auto clear_segment = [&](glm::vec2 from, glm::vec2 to) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        ++profiled_segments;
#endif
        if (map->static_safe_fraction(from, to, request.clearance) < 1.f)
            return false;
        return !dynamic || dynamic->dynamic_safe_fraction(
            *map, from, to, request.clearance, request.subject,
            request.squad) >= 1.f;
    };
    // Fast path: a clear straight shot to the goal skips the full grid search.
    // This dominates formation-slot following on open terrain, where members
    // would otherwise re-run A* every tick toward a nearby, moving slot.
    if (clear_segment(request.start, request.goal))
        return {AoePathStatus::Ready, {request.goal}, map->static_revision()};

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
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        ++profiled_cells;
#endif
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
                   AoeNavigationPath, AoeLocalAvoidanceState,
                   AoeRecyclePending>(entity);
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
                   AoeMovementIntent, AoeLocalAvoidanceState,
                   AoeGlobalMotionState,
                   AoeGlobalMotionDecision,
                   AoeFacing,
                   AoePresentationOptions, AoeActionState, AoeGameplayIdentity,
                   AoeAttackOrder, AoeAttackMoveOrder, AoeMoveGoal,
                   AoeEngagementApproach, AoeNavigationPath, AoeSquadMember,
                   AoeSquadMoveSpeedLimit, FallbackFormationOwnedMovement,
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

void detail::install_aoe_gameplay_base(
    App& app, const std::string& definitions_root,
    const AoeGameplaySettings& settings) {
    if (!std::isfinite(settings.fixed_dt) || settings.fixed_dt <= 0.0 ||
        settings.max_catchup_ticks == 0)
        throw std::invalid_argument(
            "AoeGameplayDef requires positive finite fixed_dt and max_catchup_ticks");
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
    app.world.resource_or_add<AoeSquadTrafficIndex>();
    app.world.resource_or_add<AoeNavigationSettings>();
    app.world.resource_or_add<AoeDynamicObstacleIndex>();
    app.world.resource_or_add<AoeStaticObstacleBindings>();
    auto& pathfinders = app.world.resource_or_add<AoePathfinderRegistry>();
    if (!pathfinders.contains("direct"))
        pathfinders.bind<DirectPathfinderLogic>("direct");
    if (!pathfinders.contains("grid_astar"))
        pathfinders.bind<GridAStarPathfinderLogic>("grid_astar");
    auto& formations = app.world.resource_or_add<AoeFormationRegistry>();
    if (!formations.contains(AoeFormationType::Skirmish))
        formations.bind<AoeFormationType::Skirmish,
                        DefaultSkirmishFormation>();
    auto& projectiles = app.world.resource_or_add<AoeProjectileRegistry>();
    if (!projectiles.contains("arrow"))
        projectiles.bind<ArrowProjectileLogic>("arrow");
    app.add_system(Stage::First, clear_aoe_gameplay_events);
    app.add_system(Stage::PreUpdate, spawn_aoe_gameplay_unit_system);
    app.add_system(Stage::PostUpdate, aoe_gameplay_recycle_system);
}

} // namespace gld::ecs::aoe
