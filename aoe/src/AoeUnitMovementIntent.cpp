#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <cmath>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;
constexpr float ArrivalSeconds = .25f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

void refresh_turn_cache(AoeUnitMovementIntentState& state,
                        float rotation_speed, float fixed_dt) {
    const float radians = std::min(glm::pi<float>(),
        std::max(0.f, rotation_speed) * std::max(0.f, fixed_dt));
    if (std::abs(state.cached_turn_radians - radians) <= Epsilon) return;
    state.cached_turn_radians = radians;
    state.cached_turn_cosine = std::cos(radians);
    state.cached_turn_sine = std::sin(radians);
}

glm::vec2 constrain_velocity_cached(
    glm::vec2 previous_velocity, glm::vec2 candidate_velocity,
    float max_speed, const AoeUnitMovementIntentState& turn,
    bool* turn_limited) {
    if (turn_limited) *turn_limited = false;
    if (!finite(candidate_velocity) || !(max_speed > 0.f)) return {0.f, 0.f};

    const float candidate_speed = glm::length(candidate_velocity);
    if (!(candidate_speed > Epsilon)) return {0.f, 0.f};
    const float speed = std::min(candidate_speed, max_speed);
    const glm::vec2 target = candidate_velocity / candidate_speed;
    if (!finite(previous_velocity) || glm::length(previous_velocity) <= Epsilon)
        return target * speed;

    const glm::vec2 current = glm::normalize(previous_velocity);
    const float dot = std::clamp(glm::dot(current, target), -1.f, 1.f);
    if (turn.cached_turn_radians >= glm::pi<float>() - Epsilon ||
        dot >= turn.cached_turn_cosine - Epsilon)
        return target * speed;

    const float cross = current.x * target.y - current.y * target.x;
    // An exact 180-degree reversal deterministically turns counter-clockwise.
    const float sine = cross < 0.f
        ? -turn.cached_turn_sine : turn.cached_turn_sine;
    const glm::vec2 rotated{
        current.x * turn.cached_turn_cosine - current.y * sine,
        current.x * sine + current.y * turn.cached_turn_cosine};
    if (turn_limited) *turn_limited = true;
    return rotated * speed;
}

float effective_max_speed(const entt::registry& reg, entt::entity entity) {
    float result = reg.get<AoeMovement>(entity).speed;
    if (const auto* limit = reg.try_get<AoeSquadMoveSpeedLimit>(entity))
        result = std::min(result, limit->value);
    return std::max(0.f, result);
}

bool target_valid_for_gap(
    const entt::registry& reg, const AoeUnitTarget& target) {
    if (target.entity == entt::null || !reg.valid(target.entity) ||
        reg.any_of<AoePooledUnit, AoeRecyclePending>(target.entity))
        return false;
    const auto* identity = reg.try_get<AoeGameplayIdentity>(target.entity);
    const auto* health = reg.try_get<AoeHealth>(target.entity);
    const auto* action = reg.try_get<AoeActionState>(target.entity);
    return identity && identity->instance_id == target.instance_id &&
           health && health->current > 0.f && action &&
           action->state != UnitState::Dying &&
           action->state != UnitState::Disappearing &&
           reg.all_of<AoePosition, AoeCollider>(target.entity);
}
} // namespace

glm::vec2 aoe_constrain_unit_velocity(
    glm::vec2 previous_velocity, glm::vec2 candidate_velocity,
    float max_speed, float rotation_speed_radians_per_second, float fixed_dt,
    bool* turn_limited) {
    AoeUnitMovementIntentState turn;
    refresh_turn_cache(
        turn, rotation_speed_radians_per_second, fixed_dt);
    return constrain_velocity_cached(previous_velocity, candidate_velocity,
                                     max_speed, turn, turn_limited);
}

void AoeDefaultUnitMovementIntentPlugin::install(App&) {}

void AoeDefaultUnitMovementIntentPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);

    for (const auto entity : reg.view<AoePathMotionRequest>())
        reg.get<AoePathMotionRequest>(entity).valid = false;
    for (const auto entity : reg.view<AoeUnitMovementIntentState>())
        reg.get<AoeUnitMovementIntentState>(entity).valid = false;

    for (const auto entity : reg.view<AoePosition, AoeCollider, AoeMovement,
                                      AoeMoveGoal, AoeNavigationPath,
                                      AoeActionState>()) {
        const auto& action = reg.get<AoeActionState>(entity);
        const auto& path = reg.get<AoeNavigationPath>(entity);
        if (action.state == UnitState::Attacking ||
            action.state == UnitState::Dying ||
            action.state == UnitState::Disappearing || path.no_path ||
            path.current >= path.waypoints.size())
            continue;

        const auto& position = reg.get<AoePosition>(entity);
        const auto& goal = reg.get<AoeMoveGoal>(entity);
        const glm::vec2 local_goal = path.waypoints[path.current];
        const glm::vec2 delta = local_goal - position.value;
        const float distance = glm::length(delta);
        if (!(distance > .01f)) continue;

        const float max_speed = effective_max_speed(reg, entity);
        float remaining = distance;
        const bool final_waypoint = path.current + 1 >= path.waypoints.size();
        if (final_waypoint && goal.target.entity != entt::null &&
            target_valid_for_gap(reg, goal.target)) {
            remaining = std::max(0.f, aoe_surface_gap(
                position, reg.get<AoeCollider>(entity),
                reg.get<AoePosition>(goal.target.entity),
                reg.get<AoeCollider>(goal.target.entity)) -
                goal.stopping_distance);
        }
        // Intermediate waypoints describe route geometry, not destinations.
        // Arrival braking there causes a repeated fast/slow pulse and is
        // therefore reserved for the final waypoint only.
        const float desired_speed = final_waypoint
            ? std::min(max_speed, remaining / ArrivalSeconds)
            : max_speed;
        const glm::vec2 desired_velocity = delta * (desired_speed / distance);

        auto& continuity =
            reg.get_or_emplace<AoeUnitMovementIntentState>(entity);
        glm::vec2 previous{0.f};
        if (continuity.produced_tick + 1u == tick &&
            glm::length(continuity.velocity) > Epsilon) {
            previous = continuity.velocity;
        } else if (const auto* locomotion =
                       reg.try_get<AoeLocomotionState>(entity)) {
            previous = locomotion->velocity;
        }
        bool turn_limited = false;
        const auto& movement = reg.get<AoeMovement>(entity);
        refresh_turn_cache(continuity,
            movement.rotation_speed_radians_per_second, fixed_dt);
        const glm::vec2 velocity = constrain_velocity_cached(
            previous, desired_velocity, max_speed,
            continuity, &turn_limited);
        continuity.velocity = velocity;
        continuity.produced_tick = tick;
        continuity.valid = true;

        AoeMovementIntentKind kind = AoeMovementIntentKind::Move;
        if (reg.all_of<AoeEngagementApproach>(entity))
            kind = AoeMovementIntentKind::AttackApproach;
        else if (reg.all_of<AoeSquadMember>(entity))
            kind = AoeMovementIntentKind::FormationSlot;
        reg.emplace_or_replace<AoePathMotionRequest>(entity,
            AoePathMotionRequest{kind, velocity, local_goal, max_speed,
                path.request_sequence, tick, true});

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        auto& diagnostics =
            world.resource_or_add<AoeGameplayPerformanceDiagnostics>();
        ++diagnostics.movement_speed_samples;
        diagnostics.movement_base_speed_sum += movement.speed;
        diagnostics.movement_effective_speed_sum += max_speed;
        diagnostics.movement_desired_speed_sum += desired_speed;
        diagnostics.movement_steering_speed_sum += glm::length(velocity);
        if (max_speed + Epsilon < movement.speed)
            ++diagnostics.movement_squad_limited;
        if (final_waypoint && desired_speed + Epsilon < max_speed)
            ++diagnostics.movement_arrive_limited;
        if (turn_limited) ++diagnostics.movement_turn_limited;
#endif
    }
}

void aoe_apply_final_unit_movement_constraints(
    EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    for (const auto entity :
         reg.view<AoeGlobalMotionDecision, AoeMovement>()) {
        auto& decision = reg.get<AoeGlobalMotionDecision>(entity);
        if (!decision.valid || decision.produced_tick != tick) continue;
        const auto& movement = reg.get<AoeMovement>(entity);
        float max_speed = movement.speed;
        if (const auto* request = reg.try_get<AoePathMotionRequest>(entity);
            request && request->valid && request->produced_tick == tick)
            max_speed = std::min(max_speed, request->max_speed);
        glm::vec2 previous{0.f};
        if (const auto* locomotion = reg.try_get<AoeLocomotionState>(entity))
            previous = locomotion->velocity;
        auto& turn = reg.get_or_emplace<AoeUnitMovementIntentState>(entity);
        refresh_turn_cache(
            turn, movement.rotation_speed_radians_per_second, fixed_dt);
        bool limited = false;
        const glm::vec2 before = decision.velocity;
        decision.velocity = constrain_velocity_cached(
            previous, decision.velocity, max_speed, turn, &limited);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        if (limited || glm::length(before - decision.velocity) > Epsilon)
            ++world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
                  .movement_steering_limited;
#endif
    }
}

} // namespace gld::ecs::aoe
