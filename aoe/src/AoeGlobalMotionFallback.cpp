#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

glm::vec2 acceleration_limited_velocity(
    const entt::registry& reg, entt::entity entity, glm::vec2 velocity,
    float acceleration, float fixed_dt) {
    if (!std::isfinite(velocity.x) || !std::isfinite(velocity.y))
        return {0.f, 0.f};
    const float target_speed = glm::length(velocity);
    const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
    const float current_speed = locomotion
        ? glm::length(locomotion->velocity) : 0.f;
    const float change = std::max(0.f, acceleration) * fixed_dt;
    const float speed = current_speed < target_speed
        ? std::min(target_speed, current_speed + change)
        : std::max(target_speed, current_speed - change);
    return target_speed > Epsilon
        ? velocity * (speed / target_speed) : glm::vec2{0.f};
}
} // namespace

void AoePassThroughGlobalMotionPlugin::install(App& app) {
    app.world.resource_or_add<AoeUnitFlowIndex>();
    auto& diagnostics =
        app.world.resource_or_add<AoeGlobalMotionPlannerDiagnostics>();
    diagnostics.requested_backend = "none";
    diagnostics.active_backend = std::string(name);
    diagnostics.fallback_reason.clear();
}

void AoePassThroughGlobalMotionPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    auto& index = world.resource_or_add<AoeUnitFlowIndex>();
    auto& gameplay = world.resource_or_add<AoeGameplayDiagnostics>();
    auto& planner =
        world.resource_or_add<AoeGlobalMotionPlannerDiagnostics>();
    planner.requested_backend = "none";
    planner.active_backend = std::string(name);
    planner.fallback_reason.clear();

    index.records.clear();
    index.candidates.clear();
    index.selected.clear();
    index.parents.clear();
    index.ranks.clear();
    index.maximum_reach = 0.f;
    for (const auto entity : reg.view<AoeGlobalMotionDecision>())
        reg.get<AoeGlobalMotionDecision>(entity).valid = false;

    for (const auto entity : reg.view<AoeMovementIntent, AoePosition,
                                      AoeCollider, AoeGameplayIdentity,
                                      AoeTeam>()) {
        const auto& intent = reg.get<AoeMovementIntent>(entity);
        if (!intent.valid || intent.produced_tick != tick) continue;
        const bool finite = std::isfinite(intent.velocity.x) &&
                            std::isfinite(intent.velocity.y);
        const glm::vec2 velocity = acceleration_limited_velocity(
            reg, entity, finite ? intent.velocity : glm::vec2{0.f},
            navigation.steering_max_acceleration, fixed_dt);
        const auto& collider = reg.get<AoeCollider>(entity);
        entt::entity squad = entt::null;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity))
            squad = member->squad;
        index.records.push_back({entity,
            reg.get<AoeGameplayIdentity>(entity).instance_id, squad,
            reg.get<AoeTeam>(entity).id, intent.kind,
            reg.get<AoePosition>(entity).value,
            {collider.radius_x, collider.radius_y}, velocity});
        index.maximum_reach = std::max(index.maximum_reach,
            std::max(collider.radius_x, collider.radius_y) +
            glm::length(velocity));
        reg.emplace_or_replace<AoeGlobalMotionDecision>(entity,
            AoeGlobalMotionDecision{.velocity = velocity,
                .stop_reason = finite ? AoeMotionStopReason::None
                                      : AoeMotionStopReason::Unknown,
                .produced_tick = tick, .valid = true});
        if (intent.locally_infeasible)
            ++gameplay.flow_infeasible_assignments;
    }
    gameplay.flow_active_intents += index.records.size();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .unit_flow_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe
