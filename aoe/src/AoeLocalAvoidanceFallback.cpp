#include <aoe/AoeGameplay.hpp>

#include <chrono>

#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe {

void AoePassThroughLocalAvoidancePlugin::install(App&) {}

void AoePassThroughLocalAvoidancePlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoeMovementIntent>())
        reg.get<AoeMovementIntent>(entity).valid = false;
    for (const auto entity : reg.view<AoePathMotionRequest, AoePosition,
                                      AoeCollider, AoeLocomotionState>()) {
        const auto& request = reg.get<AoePathMotionRequest>(entity);
        if (!request.valid || request.produced_tick != tick) continue;
        reg.emplace_or_replace<AoeMovementIntent>(entity,
            AoeMovementIntent{request.kind, request.velocity,
                request.velocity, request.local_goal, 0, 0, false, false,
                tick, true});
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .local_avoidance_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe
