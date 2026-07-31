#include <aoe2x/Aoe2xNavigation.hpp>

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace gld::ecs;
using namespace gld::ecs::aoe2x;

struct ProbeRead {};
struct ProbeWrite {};
struct ProbeReadWrite {};
struct ProbeState {
    std::uint32_t calls = 0;
    std::uint64_t last_tick = 0;
};

struct ProbeSystem {
    using ReadOnlyComponents = Aoe2xComponentList<ProbeRead>;
    using WriteOnlyComponents = Aoe2xComponentList<ProbeWrite>;
    using ReadWriteComponents = Aoe2xComponentList<ProbeReadWrite>;
    static constexpr std::string_view name = "aoe2x_system_probe";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Navigation;

    static void run(EcsWorld& world, std::uint64_t tick) {
        auto& state = world.resource_or_add<ProbeState>();
        ++state.calls;
        state.last_tick = tick;
    }
};

struct DuplicateAccessSystem : ProbeSystem {
    using ReadOnlyComponents = Aoe2xComponentList<ProbeRead, ProbeRead>;
};

struct OverlappingAccessSystem : ProbeSystem {
    using WriteOnlyComponents = Aoe2xComponentList<ProbeRead>;
};

static_assert(Aoe2xGameplaySystem<ProbeSystem>);
static_assert(!Aoe2xGameplaySystem<DuplicateAccessSystem>);
static_assert(!Aoe2xGameplaySystem<OverlappingAccessSystem>);
static_assert(Aoe2xGameplaySystem<Aoe2xPathfindingSystem>);

int main() {
    EcsWorld world;
    auto& descriptor = register_aoe2x_gameplay_system<ProbeSystem>(world);
    assert(descriptor.name == ProbeSystem::name);
    assert(descriptor.app_stage == Stage::PreUpdate);
    assert(descriptor.phase == Aoe2xGameplayPhase::Navigation);
    assert(descriptor.read_only_components == std::vector<entt::id_type>{
        entt::type_hash<ProbeRead>::value()});
    assert(descriptor.write_only_components == std::vector<entt::id_type>{
        entt::type_hash<ProbeWrite>::value()});
    assert(descriptor.read_write_components == std::vector<entt::id_type>{
        entt::type_hash<ProbeReadWrite>::value()});

    bool duplicate_rejected = false;
    try { register_aoe2x_gameplay_system<ProbeSystem>(world); }
    catch (const std::invalid_argument&) { duplicate_rejected = true; }
    assert(duplicate_rejected);

    run_aoe2x_gameplay_system<ProbeSystem>(world, 7);
    run_aoe2x_gameplay_system<ProbeSystem>(world, 8);
    const auto& state = world.resource<ProbeState>();
    assert(state.calls == 2 && state.last_tick == 8);

    auto& registry = world.resource<Aoe2xGameplaySystemRegistry>();
    const auto* measured = registry.find(ProbeSystem::name);
    assert(measured);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    assert(measured->timing.samples == 2);
    assert(measured->timing.average_ms && measured->timing.peak_ms);
    assert(*measured->timing.peak_ms >= *measured->timing.average_ms);
#else
    assert(measured->timing.samples == 0);
    assert(!measured->timing.average_ms && !measured->timing.peak_ms);
#endif
    registry.reset_timing();
    assert(measured->timing.samples == 0);
    assert(!measured->timing.average_ms && !measured->timing.peak_ms);
    return 0;
}
