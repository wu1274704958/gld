#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace gld::ecs::aoe {
namespace {
using json = nlohmann::json;
constexpr float Epsilon = 1e-5f;

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
        result->target_acquisition.strategy_id = acquisition.value(
            "strategy_id", result->target_acquisition.strategy_id);
        result->target_acquisition.radius = acquisition.contains("radius")
            ? non_negative(acquisition.at("radius"), "target_acquisition.radius")
            : result->target_acquisition.radius;
        if (result->target_acquisition.strategy_id.empty())
            throw std::runtime_error(
                "target_acquisition.strategy_id must not be empty");
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

void set_facing_toward(AoeFacing& facing, glm::vec2 delta) {
    if (facing.direction_count <= 0 ||
        glm::dot(delta, delta) <= Epsilon * Epsilon) return;

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
    facing.direction = sector % facing.direction_count;
}

void clear_active_engagement(entt::registry& reg, entt::entity entity) {
    reg.remove<AoeAttackOrder>(entity);
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
                         std::uint64_t tick) {
    auto* state = reg.try_get<AoeActionState>(entity);
    if (!state || is_terminal(state->state)) return;
    const bool changed = state->state != UnitState::Idle;
    state->state = UnitState::Idle;
    state->state_started_tick = tick;
    state->critical = false;
    state->release_emitted = false;
    if (changed) ++state->sequence;
}

void stop_squad_member(entt::registry& reg, entt::entity entity,
                       std::uint64_t tick, bool remove_limit = true) {
    cancel_orders(reg, entity);
    if (remove_limit) reg.remove<AoeSquadMoveSpeedLimit>(entity);
    reset_member_action(reg, entity, tick);
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
            reg.get<AoePosition>(slot.unit.entity).value = squad_slot_world(
                reg.get<AoePosition>(squad), formation, slot);
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
    order = {};
    spawn.status = AoeSquadSpawnStatus::Failed;
    state.phase = AoeSquadPhase::Failed;
}

void drive_squad_slots(entt::registry& reg, entt::entity squad,
                       float speed_limit, std::uint64_t tick) {
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    for (const auto& slot : formation.slots) {
        if (!squad_member_valid(reg, slot.unit)) continue;
        const auto entity = slot.unit.entity;
        const glm::vec2 destination = squad_slot_world(center, formation, slot);
        reg.remove<AoeAttackOrder, AoeAttackMoveOrder>(entity);
        reg.emplace_or_replace<AoeSquadMoveSpeedLimit>(entity,
            AoeSquadMoveSpeedLimit{speed_limit});
        auto& goal = reg.emplace_or_replace<AoeMoveGoal>(entity,
            AoeMoveGoal{destination, 0.f, {}});
        (void)goal;
        auto* path = reg.try_get<AoeNavigationPath>(entity);
        if (!path || path->waypoints.empty())
            reg.emplace_or_replace<AoeNavigationPath>(entity,
                AoeNavigationPath{{destination}, 0});
        else {
            path->waypoints.back() = destination;
            path->current = std::min(path->current, path->waypoints.size() - 1);
        }
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
        const auto* strategies = world.try_resource<AoeTargetAcquisitionRegistry>();
        if (!definition || !definition->attack || !strategies ||
            !strategies->contains(definition->target_acquisition.strategy_id) ||
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
    cancel_orders(reg, command.unit);
    reg.emplace_or_replace<AoeAttackOrder>(command.unit, AoeAttackOrder{command.target});
    if (state->state == UnitState::Attacking) {
        state->state = UnitState::Idle;
        state->state_started_tick = tick;
        state->critical = false;
        state->release_emitted = false;
        ++state->sequence;
    }
}

void attack_move_acquisition_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    auto* strategies = world.try_resource<AoeTargetAcquisitionRegistry>();
    if (!strategies) return;
    std::vector<entt::entity> finish;
    for (const auto entity : reg.view<AoeAttackMoveOrder, AoePosition,
                                      AoeActionState, AoeUnitDefinitionRef,
                                      AoeTeam>(entt::exclude<AoePooledUnit,
                                                            AoeRecyclePending>)) {
        auto& state = reg.get<AoeActionState>(entity);
        if (is_terminal(state.state)) continue;
        const auto* definition = reg.get<AoeUnitDefinitionRef>(entity).value.get();
        if (!definition || !definition->attack ||
            !strategies->contains(definition->target_acquisition.strategy_id)) {
            finish.push_back(entity);
            reject(world);
            continue;
        }

        if (const auto* attack = reg.try_get<AoeAttackOrder>(entity)) {
            if (target_valid(reg, attack->target)) continue;
            clear_active_engagement(reg, entity);
            set_idle_if_active(reg, entity, tick);
        }

        const auto& position = reg.get<AoePosition>(entity);
        const auto& acquisition = definition->target_acquisition;
        auto target = strategies->select(acquisition.strategy_id, world,
            AoeTargetAcquisitionContext{
                entity, position.value, acquisition.radius,
                reg.get<AoeTeam>(entity).id});
        if (target && (target->entity == entity || !target_valid(reg, *target)))
            target.reset();
        if (target) {
            reg.remove<AoeMoveGoal>(entity);
            reg.remove<AoeNavigationPath>(entity);
            reg.emplace_or_replace<AoeAttackOrder>(entity,
                AoeAttackOrder{*target});
            set_idle_if_active(reg, entity, tick);
            continue;
        }

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

std::optional<AoeUnitTarget> select_squad_target(
    EcsWorld& world, entt::entity squad,
    const AoeTargetAcquisitionRegistry* acquisition) {
    if (!acquisition) return std::nullopt;
    auto& reg = world.reg();
    const auto& combat = reg.get<AoeSquadCombatSettings>(squad);
    auto target = acquisition->select(combat.acquisition_strategy_id, world,
        {squad, reg.get<AoePosition>(squad).value, combat.acquisition_radius,
         reg.get<AoeTeam>(squad).id});
    if (!target || !target_valid(reg, *target)) return std::nullopt;
    return target;
}

void squad_control_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
    auto* acquisition = world.try_resource<AoeTargetAcquisitionRegistry>();
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
            if (order.type == AoeSquadOrderType::AttackTarget) {
                order.type = AoeSquadOrderType::Idle;
                state.phase = AoeSquadPhase::Regrouping;
            } else if (order.type == AoeSquadOrderType::AttackMove) {
                acquisition_attempted = true;
                if (auto next = select_squad_target(world, squad, acquisition)) {
                    order.target = *next;
                    engage_squad_target(world, squad, *next, tick);
                    continue;
                }
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
            drive_squad_slots(reg, squad, state.movement_speed, tick);
            if (!squad_slots_arrived(reg, squad)) continue;
            state.phase = (order.type == AoeSquadOrderType::MoveTo ||
                           order.type == AoeSquadOrderType::AttackMove)
                ? AoeSquadPhase::Moving : AoeSquadPhase::Idle;
        }

        if (order.type == AoeSquadOrderType::AttackMove &&
            !acquisition_attempted) {
            if (auto target = select_squad_target(world, squad, acquisition)) {
                order.target = *target;
                engage_squad_target(world, squad, *target, tick);
                continue;
            }
        }

        if (order.type == AoeSquadOrderType::MoveTo ||
            order.type == AoeSquadOrderType::AttackMove) {
            const glm::vec2 delta = order.destination - center.value;
            const float distance = glm::length(delta);
            if (distance > Epsilon && state.movement_speed > 0.f) {
                formation.forward = delta / distance;
                center.value += formation.forward * std::min(
                    distance, state.movement_speed * dt);
                state.phase = AoeSquadPhase::Moving;
            }
            drive_squad_slots(reg, squad, state.movement_speed, tick);
            if (glm::length(order.destination - center.value) <= Epsilon &&
                squad_slots_arrived(reg, squad)) {
                clear_squad_member_orders(reg, members, tick);
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
            reg.emplace_or_replace<AoeMoveGoal>(entity,
                AoeMoveGoal{target_position.value, definition->attack->range, order.target});
            reg.emplace_or_replace<AoeNavigationPath>(entity,
                AoeNavigationPath{{target_position.value}, 0});
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
}

void movement_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
    std::vector<entt::entity> arrived;
    for (auto entity : reg.view<AoePosition, AoeMovement, AoeMoveGoal,
                                AoeNavigationPath, AoeActionState, AoeFacing>()) {
        auto& state = reg.get<AoeActionState>(entity);
        if (state.state == UnitState::Attacking || is_terminal(state.state)) continue;
        auto& goal = reg.get<AoeMoveGoal>(entity);
        auto& path = reg.get<AoeNavigationPath>(entity);
        if (goal.target.entity != entt::null) {
            if (!target_valid(reg, goal.target)) { arrived.push_back(entity); continue; }
            goal.destination = reg.get<AoePosition>(goal.target.entity).value;
        }
        if (path.waypoints.empty() || path.current >= path.waypoints.size()) {
            arrived.push_back(entity);
            continue;
        }
        if (goal.target.entity != entt::null) path.waypoints.back() = goal.destination;
        auto& position = reg.get<AoePosition>(entity);
        glm::vec2 delta = path.waypoints[path.current] - position.value;
        const float distance = glm::length(delta);
        bool reached = distance <= Epsilon;
        float remaining = distance;
        const bool final_waypoint = path.current + 1 >= path.waypoints.size();
        if (goal.target.entity != entt::null && final_waypoint && !reached) {
            remaining = std::max(0.f, aoe_surface_gap(position, reg.get<AoeCollider>(entity),
                reg.get<AoePosition>(goal.target.entity), reg.get<AoeCollider>(goal.target.entity)) -
                goal.stopping_distance);
            reached = remaining <= Epsilon;
        }
        if (!reached) {
            const glm::vec2 direction = delta / distance;
            float speed = reg.get<AoeMovement>(entity).speed;
            if (const auto* limit = reg.try_get<AoeSquadMoveSpeedLimit>(entity))
                speed = std::min(speed, limit->value);
            const float step = std::min(remaining, speed * dt);
            position.value += direction * step;
            set_facing_toward(reg.get<AoeFacing>(entity), direction);
            state.state = UnitState::Moving;
        } else if (!final_waypoint) {
            ++path.current;
            state.state = UnitState::Moving;
        } else arrived.push_back(entity);
    }
    for (auto entity : arrived) {
        if (!reg.valid(entity)) continue;
        reg.remove<AoeMoveGoal>(entity);
        reg.remove<AoeNavigationPath>(entity);
        auto& state = reg.get<AoeActionState>(entity);
        if (!is_terminal(state.state)) {
            state.state = UnitState::Idle;
            state.state_started_tick = tick;
        }
    }
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

void fixed_tick(EcsWorld& world) {
    auto& clock = world.resource<AoeGameplayClock>();
    ++clock.tick;
    squad_spawn_resolution_tick(world);
    squad_command_tick(world, clock.tick);
    command_tick(world, clock.tick);
    squad_membership_cleanup_tick(world);
    squad_control_tick(world, clock.tick);
    attack_move_acquisition_tick(world, clock.tick);
    navigation_tick(world, clock.tick);
    movement_tick(world, clock.tick);
    combat_tick(world, clock.tick);
    aoe_projectile_tick(world, clock.tick);
    lifecycle_tick(world, clock.tick);
}
} // namespace

std::string PresentationDefinition::animation(const std::string& semantic) const {
    const auto it = animations.find(semantic);
    return it == animations.end() ? std::string{} : it->second;
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

void AoeTargetAcquisitionRegistry::bind_erased(
    std::string id, SelectFn function) {
    if (id.empty() || !function)
        throw std::invalid_argument(
            "target acquisition binding requires a non-empty id and function");
    if (!entries_.emplace(std::move(id), function).second)
        throw std::invalid_argument("duplicate target acquisition binding");
}

bool AoeTargetAcquisitionRegistry::contains(std::string_view id) const {
    return entries_.find(std::string(id)) != entries_.end();
}

std::optional<AoeUnitTarget> AoeTargetAcquisitionRegistry::select(
    std::string_view id, const EcsWorld& world,
    const AoeTargetAcquisitionContext& context) const {
    const auto it = entries_.find(std::string(id));
    return it == entries_.end() ? std::nullopt : it->second(world, context);
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
    for (const auto candidate : reg.view<AoeTeam, AoePosition, AoeCollider,
                                         AoeHealth, AoeActionState,
                                         AoeGameplayIdentity,
                                         AoeUnitDefinitionRef>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
        if (candidate == context.seeker ||
            reg.get<AoeTeam>(candidate).id == context.seeker_team)
            continue;
        const auto& identity = reg.get<AoeGameplayIdentity>(candidate);
        const AoeUnitTarget target{candidate, identity.instance_id};
        if (!target_valid(reg, target)) continue;
        const float gap = aoe_surface_gap(
            seeker_position, seeker_collider,
            reg.get<AoePosition>(candidate), reg.get<AoeCollider>(candidate));
        if (gap > context.radius + Epsilon) continue;
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
        options.acquisition_strategy_id.empty() || options.direction_count <= 0 ||
        options.player_color < 0 || options.player_color > 8)
        return entt::null;
    const auto* formations = world.try_resource<AoeFormationRegistry>();
    if (!formations || !formations->contains(options.formation)) return entt::null;
    const auto* acquisition = world.try_resource<AoeTargetAcquisitionRegistry>();
    if (!acquisition ||
        !acquisition->contains(options.acquisition_strategy_id)) return entt::null;
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
        options.acquisition_strategy_id, options.acquisition_radius});
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
    world.resource_or_add<Events<AoeActionEvent>>().clear();
}

void spawn_aoe_gameplay_unit_system(EcsWorld& world) {
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
}

void aoe_gameplay_fixed_system(EcsWorld& world) {
    auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto* time = world.try_resource<Time>();
    if (!time || !(settings.fixed_dt > 0.0)) return;
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
}

void aoe_gameplay_recycle_system(EcsWorld& world) {
    auto& reg = world.reg();
    auto& pool = world.resource<AoeGameplayPool>();
    const auto view = reg.view<AoeRecyclePending>();
    std::vector<entt::entity> pending(view.begin(), view.end());
    for (auto entity : pending) {
        if (!reg.valid(entity)) continue;
        reg.remove<AoeGameplaySpawnRequest, AoeGameplaySpawnError, AoeUnitDefinitionRef,
                   AoeHealth, AoeLevel, AoeCollider, AoePosition, AoeMovement, AoeTeam,
                   AoeFacing,
                   AoePresentationOptions, AoeActionState, AoeGameplayIdentity,
                   AoeAttackOrder, AoeAttackMoveOrder, AoeMoveGoal,
                   AoeNavigationPath, AoeSquadMember, AoeSquadMoveSpeedLimit,
                   Transform>(entity);
        reg.remove<AoeRecyclePending>(entity);
        reg.emplace_or_replace<AoePooledUnit>(entity);
        pool.available.push_back(entity);
        ++pool.recycled;
    }
}

void AoeGameplayPlugin::operator()(App& app) const {
    if (!std::isfinite(settings.fixed_dt) || settings.fixed_dt <= 0.0 ||
        settings.max_catchup_ticks == 0)
        throw std::invalid_argument("AoeGameplayPlugin requires positive finite fixed_dt and max_catchup_ticks");
    auto& server = app.world.resource<AssetServer>();
    server.register_loader<AoeUnitDefinitionDesc>(std::make_shared<AoeUnitDefinitionLoader>());
    auto& manager = app.world.add_resource<AoeUnitDefinitionManager>(server, definitions_root);
    manager.refresh();
    app.world.add_resource<AoeGameplaySettings>(settings);
    app.world.resource_or_add<AoeGameplayClock>();
    app.world.resource_or_add<AoeGameplayLifecycle>();
    app.world.resource_or_add<AoeGameplayPool>();
    app.world.resource_or_add<AoeGameplayCommands>();
    app.world.resource_or_add<AoeSquadCommands>();
    app.world.resource_or_add<Events<AoeActionEvent>>();
    app.world.resource_or_add<AoeGameplayDiagnostics>();
    auto& formations = app.world.resource_or_add<AoeFormationRegistry>();
    if (!formations.contains(AoeFormationType::Skirmish))
        formations.bind<AoeFormationType::Skirmish,
                        DefaultSkirmishFormation>();
    auto& acquisition = app.world.resource_or_add<AoeTargetAcquisitionRegistry>();
    if (!acquisition.contains("nearest_enemy"))
        acquisition.bind<NearestEnemyAcquisitionStrategy>("nearest_enemy");
    auto& projectiles = app.world.resource_or_add<AoeProjectileRegistry>();
    if (!projectiles.contains("arrow"))
        projectiles.bind<ArrowProjectileLogic>("arrow");
    app.add_system(Stage::First, clear_aoe_gameplay_events);
    app.add_system(Stage::PreUpdate, spawn_aoe_gameplay_unit_system);
    app.add_system(Stage::PreUpdate, aoe_gameplay_fixed_system);
    app.add_system(Stage::PostUpdate, aoe_gameplay_recycle_system);
}

} // namespace gld::ecs::aoe
