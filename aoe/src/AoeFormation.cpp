#include <aoe/AoeGameplay.hpp>

#include <chrono>

namespace gld::ecs::aoe {

void AoeFullFormationPlugin::install(App&) {}

void AoeFullFormationPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    detail::aoe_gameplay_formation_fixed_tick(world, tick, false);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>().formation_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe
