#include <aoe/AoeGameplay.hpp>

#include <cassert>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>
#include <ecs/assets/FileSystem.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;

#undef assert
#define assert(...) do { if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "assertion failed at line %d: %s\n", __LINE__, #__VA_ARGS__); \
    std::exit(EXIT_FAILURE); } } while (false)

namespace {
struct MemoryFileSystem final : IFileSystem {
    std::unordered_map<std::string, std::string> texts;
    bool exists(const std::string& path) const override { return texts.contains(path); }
    std::optional<std::vector<std::byte>> read_bytes(const std::string&) const override {
        return std::nullopt;
    }
    std::optional<std::string> read_text(const std::string& path) const override {
        const auto it = texts.find(path);
        return it == texts.end() ? std::nullopt : std::optional<std::string>(it->second);
    }
    std::vector<FileSystemEntry> list(const std::string& root) const override {
        std::vector<FileSystemEntry> result;
        const std::string prefix = root + "/";
        for (const auto& [path, _] : texts)
            if (path.starts_with(prefix)) result.push_back({path.substr(prefix.size()), false, true});
        return result;
    }
};

inline constexpr auto InvalidFormationType =
    static_cast<AoeFormationType>(99);

struct InvalidFormation {
    static std::optional<AoeFormationLayout> generate(
        const AoeFormationContext&) {
        return AoeFormationLayout{};
    }

    static std::optional<AoeFormationLayout> generate_for_width(
        const AoeFormationContext&, float) {
        return AoeFormationLayout{};
    }
};

struct MalformedBoundsFormation {
    static std::optional<AoeFormationLayout> generate(
        const AoeFormationContext& context) {
        auto result = DefaultSkirmishFormation::generate(context);
        if (result) result->bounds.local_max.x =
            result->bounds.local_min.x - 1.f;
        return result;
    }

    static std::optional<AoeFormationLayout> generate_for_width(
        const AoeFormationContext& context, float maximum_width) {
        return generate(context);
    }
};

struct MalformedSlotFormation {
    static std::optional<AoeFormationLayout> generate(
        const AoeFormationContext& context) {
        auto result = DefaultSkirmishFormation::generate(context);
        if (result && !result->slots.empty())
            result->slots.front().local_offset.x =
                std::numeric_limits<float>::infinity();
        return result;
    }

    static std::optional<AoeFormationLayout> generate_for_width(
        const AoeFormationContext& context, float maximum_width) {
        return generate(context);
    }
};

struct IgnoresMaximumWidthFormation {
    static std::optional<AoeFormationLayout> generate(
        const AoeFormationContext& context) {
        return DefaultSkirmishFormation::generate(context);
    }

    static std::optional<AoeFormationLayout> generate_for_width(
        const AoeFormationContext& context, float maximum_width) {
        return generate(context);
    }
};

struct StaticGameplayPluginProbeState {
    std::uint32_t engagement_calls = 0;
    std::uint32_t formation_calls = 0;
    std::uint32_t arrival_rematch_calls = 0;
    std::uint32_t movement_intent_calls = 0;
    std::uint32_t local_calls = 0;
    std::uint32_t global_calls = 0;
    std::uint32_t sequence = 0;
    std::uint64_t last_tick = 0;
};

struct StaticSquadEngagementProbe {
    using phase = AoeSquadEngagementPhase;
    static constexpr std::string_view name = "test_engagement_probe";

    static void install(App& app) {
        app.world.resource_or_add<StaticGameplayPluginProbeState>();
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.engagement_calls;
        probe.sequence = probe.sequence == 0 ? 1 : 99;
        probe.last_tick = tick;
    }
};

struct StaticFormationProbe {
    using phase = AoeFormationPhase;
    static constexpr std::string_view name = "test_formation_probe";

    static void install(App& app) {
        app.world.resource_or_add<StaticGameplayPluginProbeState>();
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.formation_calls;
        probe.sequence = probe.sequence == 1 ? 2 : 99;
        probe.last_tick = tick;
    }
};

struct StaticLocalAvoidanceProbe {
    using phase = AoeLocalAvoidancePhase;
    static constexpr std::string_view name = "test_probe";

    static void install(App& app) {
        app.world.resource_or_add<StaticGameplayPluginProbeState>();
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.local_calls;
        probe.sequence = probe.sequence == 4 ? 5 : 99;
        probe.last_tick = tick;
    }
};

struct StaticSquadArrivalRematchProbe {
    using phase = AoeSquadArrivalRematchPhase;
    static constexpr std::string_view name = "test_arrival_rematch_probe";

    static void install(App& app) {
        app.world.resource_or_add<StaticGameplayPluginProbeState>();
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.arrival_rematch_calls;
        probe.sequence = probe.sequence == 2 ? 3 : 99;
        probe.last_tick = tick;
    }
};

struct StaticUnitMovementIntentProbe {
    using phase = AoeUnitMovementIntentPhase;
    static constexpr std::string_view name = "test_movement_intent_probe";

    static void install(App& app) {
        app.world.resource_or_add<StaticGameplayPluginProbeState>();
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.movement_intent_calls;
        probe.sequence = probe.sequence == 3 ? 4 : 99;
        probe.last_tick = tick;
    }
};

struct StaticGlobalMotionProbe {
    using phase = AoeGlobalMotionPhase;
    static constexpr std::string_view name = "test_global_probe";
    static constexpr bool uses_runtime_planner = false;

    static void install(App&) {}

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& probe = world.resource<StaticGameplayPluginProbeState>();
        ++probe.global_calls;
        probe.sequence = probe.sequence == 5 ? 6 : 99;
        probe.last_tick = tick;
    }
};

struct SidewaysGlobalMotionProbe {
    using phase = AoeGlobalMotionPhase;
    static constexpr std::string_view name = "sideways_test";
    static constexpr bool uses_runtime_planner = false;

    static void install(App&) {}

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        auto& reg = world.reg();
        for (const auto entity : reg.view<AoePathMotionRequest>()) {
            const auto& request = reg.get<AoePathMotionRequest>(entity);
            if (!request.valid || request.produced_tick != tick) continue;
            reg.emplace_or_replace<AoeGlobalMotionDecision>(entity,
                AoeGlobalMotionDecision{
                    .velocity = {0.f, request.max_speed},
                    .produced_tick = tick,
                    .valid = true});
        }
    }
};

struct FormationModuleProbeState {
    std::vector<std::uint32_t> calls;
};

template<class Role, std::uint32_t Sequence, bool Stops = false>
struct FormationModuleProbe {
    using role = Role;
    static constexpr std::string_view name = Stops
        ? "stop_probe" : "order_probe";

    static void install(App& app) {
        app.world.resource_or_add<FormationModuleProbeState>();
    }

    static AoeFormationModuleResult run(
        EcsWorld& world, AoeFormationSquadContext&) {
        world.resource_or_add<FormationModuleProbeState>()
            .calls.push_back(Sequence);
        if constexpr (Stops)
            return AoeFormationModuleResult::StopSquad;
        return AoeFormationModuleResult::Continue;
    }
};

using FormationLayoutProbe =
    FormationModuleProbe<AoeSquadLayoutRole, 1>;
using FormationRouteProbe =
    FormationModuleProbe<AoeFormationRouteSplitRole, 2>;
using FormationStoppingRouteProbe =
    FormationModuleProbe<AoeFormationRouteSplitRole, 2, true>;
using FormationMovingProbe =
    FormationModuleProbe<AoeFormationMovingControlRole, 3>;
using FormationAttackProbe =
    FormationModuleProbe<AoeFormationAttackControlRole, 4>;
using FormationCompletionProbe =
    FormationModuleProbe<AoeFormationCommandCompletionRole, 5>;
using FormationOrderProbePlugin = AoeFormationPlugin<
    FormationLayoutProbe, FormationRouteProbe, FormationMovingProbe,
    FormationAttackProbe, FormationCompletionProbe>;
using FormationStopProbePlugin = AoeFormationPlugin<
    FormationLayoutProbe, FormationStoppingRouteProbe, FormationMovingProbe,
    FormationAttackProbe, FormationCompletionProbe>;

struct CountingPathfinderState {
    std::uint32_t calls = 0;
    bool fail = false;
};

struct CountingPathfinderLogic {
    static constexpr std::string_view name = "counting";
    static AoePathResult find(EcsWorld& world,
                              const AoePathRequest& request) {
        auto& state = world.resource_or_add<CountingPathfinderState>();
        ++state.calls;
        const auto* map = world.try_resource<AoeLogicMap>();
        const std::uint64_t revision = map ? map->static_revision() : 0;
        if (state.fail)
            return {AoePathStatus::NoPath, {}, revision};
        return {AoePathStatus::Ready, {request.goal}, revision};
    }
};

nlohmann::json definition_json(int schema = 2) {
    nlohmann::json value = {
        {"schema_version", schema}, {"kind", "aoe_gameplay_unit"}, {"id", "test"},
        {"level", 2}, {"max_hp", 50.0},
        {"tags", nlohmann::json::array({"scout", "cavalry"})},
        {"armor", nlohmann::json::array({{{"class_id", 3}, {"amount", 2.0}}})},
        {"collision", {{"radius_x", 0.2}, {"radius_y", 0.3}, {"height", 1.8}}},
        {"attack", {
            {"mode", "projectile"},
            {"damage", nlohmann::json::array({{{"class_id", 3}, {"amount", 7.0}}})},
            {"range", 4.0}, {"release_seconds", 0.2},
            {"animation_duration_seconds", 0.3}, {"cooldown_seconds", 0.5},
            {"critical_chance", 1.0}, {"critical_multiplier", 2.0},
            {"projectile_id", "arrow"},
            {"projectile_launch_offset", {{"x", 0.0}, {"y", 0.5}, {"z", 1.5}}}
        }},
        {"presentation", {
            {"backend", "aoe2"}, {"resource_id", "u_test"},
            {"default_player_color", 1},
            {"animations", {{"idle", "idleA"}, {"moving", "walkA"},
                            {"attack", "attackA"}, {"critical_attack", "attackB"},
                            {"death", "deathA"}, {"disappear", "decayA"}}}
        }}
    };
    if (schema == 2) {
        value["movement"] = {{"speed", 2.0}};
        value["lifecycle"] = {
            {"death_duration_seconds", 0.2},
            {"disappear_duration_seconds", 0.2}};
    }
    return value;
}

std::shared_ptr<AoeUnitDefinition> parse(nlohmann::json source) {
    MemoryFileSystem fs;
    fs.texts["unit.json"] = source.dump();
    AoeUnitDefinitionLoader loader;
    return loader.finalize(loader.load_cpu(AoeUnitDefinitionDesc("unit.json"), fs),
                           AoeUnitDefinitionDesc("unit.json"));
}

AoeMapDefinition flat_map(std::uint32_t width = 20,
                          std::uint32_t height = 12) {
    AoeMapDefinition result;
    result.id = "gameplay_map";
    result.origin = {0.f, 0.f};
    result.tile_size = 1.f;
    result.width = width;
    result.height = height;
    result.heights.resize(static_cast<std::size_t>(width + 1) *
                          (height + 1), 0.f);
    return result;
}

AoeMapDefinition centered_flat_map(std::uint32_t width = 40,
                                   std::uint32_t height = 40) {
    auto result = flat_map(width, height);
    result.id = "centered_gameplay_map";
    result.origin = {-static_cast<float>(width) * .5f,
                     -static_cast<float>(height) * .5f};
    return result;
}

AoeMapDefinition squad_stress_map() {
    AoeMapDefinition result;
    result.id = "squad_stress_map";
    result.origin = {-18.f, -8.f};
    result.tile_size = 1.f;
    result.width = 36;
    result.height = 16;
    result.heights.resize(37u * 17u, 0.f);
    AoeStaticObstacleDesc left;
    left.shape = AoeStaticObstacleShape::Aabb;
    left.center = {-4.f, 0.f};
    left.half_extents = {.8f, 2.5f};
    result.static_obstacles.push_back(left);
    AoeStaticObstacleDesc right = left;
    right.center = {4.f, 0.f};
    result.static_obstacles.push_back(right);
    AoeStaticObstacleDesc south;
    south.shape = AoeStaticObstacleShape::Circle;
    south.center = {0.f, -4.f};
    south.radius = 1.f;
    result.static_obstacles.push_back(south);
    return result;
}

AoeFormationRoutePose sample_route_plan_pose(
    const AoeFormationRoutePlan& plan, float distance) {
    assert(!plan.poses.empty());
    if (distance <= plan.poses.front().distance) {
        auto result = plan.poses.front();
        result.center += result.forward * (distance - result.distance);
        result.distance = distance;
        return result;
    }
    if (distance >= plan.poses.back().distance) {
        auto result = plan.poses.back();
        result.center += result.forward * (distance - result.distance);
        result.distance = distance;
        return result;
    }
    const auto upper = std::lower_bound(plan.poses.begin(), plan.poses.end(),
        distance, [](const AoeFormationRoutePose& pose, float value) {
            return pose.distance < value;
        });
    const auto& next = *upper;
    const auto& previous = *(upper - 1);
    const float alpha = (distance - previous.distance) /
        (next.distance - previous.distance);
    const float angle = std::atan2(
        previous.forward.x * next.forward.y -
            previous.forward.y * next.forward.x,
        glm::dot(previous.forward, next.forward));
    const float cosine = std::cos(angle * alpha);
    const float sine = std::sin(angle * alpha);
    return {distance, glm::mix(previous.center, next.center, alpha),
        glm::normalize(glm::vec2{
            previous.forward.x * cosine - previous.forward.y * sine,
            previous.forward.x * sine + previous.forward.y * cosine})};
}

void assert_snake_travel_moves_forward(
    const AoeFormationRoutePlan& plan, float minimum_ratio) {
    assert(plan.valid && plan.poses.size() >= 2);
    for (const auto& slot : plan.travel_layout.slots) {
        bool have_previous = false;
        glm::vec2 previous_center{0.f};
        glm::vec2 previous_point{0.f};
        for (const auto& pose : plan.poses) {
            const auto sampled = sample_route_plan_pose(
                plan, pose.distance + slot.local_offset.y);
            const glm::vec2 right{sampled.forward.y, -sampled.forward.x};
            const glm::vec2 point = sampled.center +
                right * slot.local_offset.x;
            if (have_previous) {
                const glm::vec2 center_delta = sampled.center - previous_center;
                const float center_distance2 = glm::dot(
                    center_delta, center_delta);
                if (center_distance2 > 1e-10f) {
                    const float ratio = glm::dot(
                        point - previous_point, center_delta) /
                        center_distance2;
                    assert(ratio + 1e-3f >= minimum_ratio);
                }
            }
            previous_center = sampled.center;
            previous_point = point;
            have_previous = true;
        }
    }
}

template<class LocalAvoidancePlugin = AoeFullLocalAvoidancePlugin,
         class GlobalMotionPlugin = AoeDefaultGlobalMotionPlugin,
         class FormationPlugin = AoeFullFormationPlugin,
         class SquadEngagementPlugin = AoeFullSquadEngagementPlugin,
         class SquadArrivalRematchPlugin =
             AoeFullSquadArrivalRematchPlugin,
         class UnitPathfinderPlugin = AoeGridAStarUnitPathfinderPlugin,
         class SquadPathfinderPlugin = AoeGridAStarSquadPathfinderPlugin,
         class UnitMovementIntentPlugin =
             AoeDefaultUnitMovementIntentPlugin>
struct Fixture {
    EcsWorld world;
    std::shared_ptr<MemoryFileSystem> fs = std::make_shared<MemoryFileSystem>();
    std::unique_ptr<AssetServer> server = std::make_unique<AssetServer>();
    Handle<AoeUnitDefinition> definition;
    std::uint64_t next_instance = 1;

    Fixture() {
        fs->texts["units/test.json"] = definition_json().dump();
        server->world = &world;
        server->fs = fs;
        server->register_loader<AoeUnitDefinitionDesc>(
            std::make_shared<AoeUnitDefinitionLoader>());
        definition = server->load_sync(AoeUnitDefinitionDesc("units/test.json"));
        assert(definition.get());
        auto& manager = world.add_resource<AoeUnitDefinitionManager>(*server, "units");
        manager.refresh();
        world.add_resource<AoeGameplaySettings>(AoeGameplaySettings{0.1, 64, 1234});
        world.resource_or_add<AoeGameplayClock>();
        world.resource_or_add<AoeGameplayCommands>();
        world.resource_or_add<AoeSquadCommands>();
        world.resource_or_add<Events<AoeActionEvent>>();
        world.resource_or_add<AoeGameplayDiagnostics>();
        auto& navigation = world.resource_or_add<AoeNavigationSettings>();
        // Most legacy tests assert exact constant-speed positions. Dedicated
        // steering tests below exercise the production acceleration defaults.
        navigation.steering_max_acceleration = 1000.f;
        // Legacy tests intentionally overlap independent movers while checking
        // exact positions/facing. Unit-flow behavior is enabled explicitly by
        // its dedicated traffic fixtures below.
        navigation.unit_flow_enabled = false;
        if constexpr (std::is_same_v<
                          LocalAvoidancePlugin,
                          AoeFullLocalAvoidancePlugin>) {
            world.resource_or_add<AoeLocalAvoidanceSettings>();
            world.resource_or_add<AoeLocalAvoidanceScratch>();
        }
        world.resource_or_add<AoeFormationRegistry>()
            .bind<AoeFormationType::Skirmish, DefaultSkirmishFormation>();
        world.resource_or_add<AoeProjectileRegistry>()
            .bind<ArrowProjectileLogic>("arrow");
        world.add_resource<AoeGameplayLifecycle>(AoeGameplayLifecycle{100});
        world.resource_or_add<AoeGameplayPool>();
        world.resource_or_add<Time>();
    }

    ~Fixture() { server->shutdown(); }

    entt::entity unit(glm::vec2 position, float hp = 50.f,
                      std::uint32_t team = 0) {
        const auto entity = world.spawn();
        world.reg().emplace<AoeUnitDefinitionRef>(entity, AoeUnitDefinitionRef{definition});
        world.reg().emplace<AoeHealth>(entity, AoeHealth{hp, hp});
        world.reg().emplace<AoeLevel>(entity, AoeLevel{2});
        world.reg().emplace<AoeCollider>(entity, AoeCollider{0.2f, 0.3f, 1.8f});
        world.reg().emplace<AoePosition>(entity, AoePosition{position});
        world.reg().emplace<AoeMovement>(entity, AoeMovement{2.f});
        world.reg().emplace<AoeLocomotionState>(entity);
        world.reg().emplace<AoeTeam>(entity, AoeTeam{team});
        world.reg().emplace<AoeFacing>(entity);
        world.reg().emplace<AoePresentationOptions>(entity);
        world.reg().emplace<AoeActionState>(entity);
        world.reg().emplace<AoeGameplayIdentity>(entity,
            AoeGameplayIdentity{next_instance++, 99});
        return entity;
    }

    void advance_ticks(int count) {
        for (int i = 0; i < count; ++i) {
            world.resource<Time>().dt = 0.1f;
            gld::ecs::aoe::detail::aoe_gameplay_fixed_system<
                SquadEngagementPlugin, FormationPlugin,
                SquadArrivalRematchPlugin,
                UnitMovementIntentPlugin,
                LocalAvoidancePlugin,
                GlobalMotionPlugin,
                UnitPathfinderPlugin,
                SquadPathfinderPlugin>(world);
        }
    }
};
} // namespace

using FullGameplayDef = AoeGameplayDef<
    AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
    AoeFullSquadArrivalRematchPlugin,
    AoeDefaultUnitMovementIntentPlugin,
    AoeFullLocalAvoidancePlugin,
    AoeDefaultGlobalMotionPlugin>;
using PassThroughGameplayDef =
    AoeGameplayDef<AoeFullSquadEngagementPlugin,
                   AoeFullFormationPlugin,
                   AoeFullSquadArrivalRematchPlugin,
                   AoeDefaultUnitMovementIntentPlugin,
                   AoePassThroughLocalAvoidancePlugin,
                   AoeDefaultGlobalMotionPlugin>;
using MotionFloorGameplayDef =
    AoeGameplayDef<AoePassThroughSquadEngagementPlugin,
                   AoePassThroughFormationPlugin,
                   AoePassThroughSquadArrivalRematchPlugin,
                   AoeDefaultUnitMovementIntentPlugin,
                   AoePassThroughLocalAvoidancePlugin,
                   AoePassThroughGlobalMotionPlugin>;
using ProbeGameplayDef =
    AoeGameplayDef<StaticSquadEngagementProbe, StaticFormationProbe,
                   StaticSquadArrivalRematchProbe,
                   StaticUnitMovementIntentProbe,
                   StaticLocalAvoidanceProbe,
                   StaticGlobalMotionProbe>;
static_assert(AoeGameplayStaticPlugin<AoeFullSquadEngagementPlugin>);
static_assert(AoeGameplayStaticPlugin<AoePassThroughSquadEngagementPlugin>);
static_assert(AoeGameplayStaticPlugin<AoeFullFormationPlugin>);
static_assert(AoeGameplayStaticPlugin<AoePassThroughFormationPlugin>);
static_assert(AoeGameplayStaticPlugin<AoeLayoutOnlyFormationPlugin>);
static_assert(AoeGameplayStaticPlugin<
    AoeLayoutRouteSplitFormationPlugin>);
static_assert(AoeGameplayStaticPlugin<
    AoeLayoutRouteSplitMovingFormationPlugin>);
static_assert(AoeFormationModule<
    AoeFullSquadLayoutModule, AoeSquadLayoutRole>);
static_assert(AoeFormationModule<
    AoePassThroughRouteSplitModule, AoeFormationRouteSplitRole>);
static_assert(AoeFormationModule<
    AoeNavMeshRouteSplitModule, AoeFormationRouteSplitRole>);
static_assert(AoeFormationModule<
    AoePassThroughMovingControlModule, AoeFormationMovingControlRole>);
static_assert(AoeFormationModule<
    AoeSynchronizedFormationMovingControlModule,
    AoeFormationMovingControlRole>);
static_assert(AoeFormationModule<
    AoePassThroughAttackControlModule, AoeFormationAttackControlRole>);
static_assert(AoeFormationModule<
    AoePassThroughCommandCompletionModule,
    AoeFormationCommandCompletionRole>);
static_assert(std::same_as<
    AoeLayoutOnlyFormationPlugin::SquadLayoutModule,
    AoeFullSquadLayoutModule>);
static_assert(AoeGameplayStaticPlugin<AoeFullSquadArrivalRematchPlugin>);
static_assert(AoeGameplayStaticPlugin<
    AoePassThroughSquadArrivalRematchPlugin>);
static_assert(AoeGameplayStaticPlugin<
    AoeDefaultUnitMovementIntentPlugin>);
static_assert(AoeGameplayStaticPlugin<AoeFullLocalAvoidancePlugin>);
static_assert(AoeGameplayStaticPlugin<AoePassThroughLocalAvoidancePlugin>);
static_assert(AoeGameplayStaticPlugin<AoeDefaultGlobalMotionPlugin>);
static_assert(AoeGameplayStaticPlugin<AoePassThroughGlobalMotionPlugin>);
static_assert(AoeGameplayPathfinderPlugin<
    AoeGridAStarUnitPathfinderPlugin>);
static_assert(AoeGameplayPathfinderPlugin<
    AoeGridAStarSquadPathfinderPlugin>);
static_assert(AoeGameplayPathfinderPlugin<
    AoeNavMeshSquadPathfinderPlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::SquadEngagementPlugin,
    AoeFullSquadEngagementPlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::FormationPlugin,
    AoeFullFormationPlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::SquadArrivalRematchPlugin,
    AoeFullSquadArrivalRematchPlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::UnitMovementIntentPlugin,
    AoeDefaultUnitMovementIntentPlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::LocalAvoidancePlugin,
    AoeFullLocalAvoidancePlugin>);
static_assert(std::same_as<
    AoeGameplayPlugin::GlobalMotionPlugin,
    AoeDefaultGlobalMotionPlugin>);
static_assert(std::same_as<
    PassThroughGameplayDef::LocalAvoidancePlugin,
    AoePassThroughLocalAvoidancePlugin>);
static_assert(std::same_as<
    MotionFloorGameplayDef::GlobalMotionPlugin,
    AoePassThroughGlobalMotionPlugin>);

int main() {
    // Registry-backed adapters preserve runtime selection for callers that
    // explicitly opt into the compatibility path.
    {
        EcsWorld adapter_world;
        adapter_world.resource_or_add<AoeNavigationSettings>()
            .unit_pathfinder_id = "counting";
        adapter_world.resource<AoeNavigationSettings>()
            .squad_pathfinder_id = "counting";
        adapter_world.resource_or_add<AoePathfinderRegistry>()
            .bind<CountingPathfinderLogic>("counting");
        const AoePathRequest request{{0.f, 0.f}, {1.f, 0.f}};
        assert(AoeRegisteredUnitPathfinderPlugin::find(
                   adapter_world, request).status == AoePathStatus::Ready);
        assert(AoeRegisteredSquadPathfinderPlugin::find(
                   adapter_world, request).status == AoePathStatus::Ready);
        assert(adapter_world.resource<CountingPathfinderState>().calls == 2);
    }

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    {
        EcsWorld metrics_world;
        const auto subject = metrics_world.spawn();
        auto& queue = metrics_world.resource_or_add<
            AoeUnitPathfinderRequests>().values;
        queue.push_back({AoePathfinderRequestKind::Unit,
                         {{0.f, 0.f}, {1.f, 0.f}, {}, subject},
                         {1.f, 0.f}, 1, 1});
        gld::ecs::aoe::detail::run_aoe_pathfinder_requests<
            AoeDirectUnitPathfinderPlugin,
            AoeUnitPathfinderRequests>(metrics_world);
        const auto calls = metrics_world.resource<
            AoePathfinderPerformanceDiagnostics>().unit.calls;
        metrics_world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
            .begin_frame();
        assert(calls == 1);
        assert(metrics_world.resource<
            AoePathfinderPerformanceDiagnostics>().unit.calls == calls);
    }
#endif

    // Install and schedule a real statically composed gameplay definition.
    // A phase probe avoids feeding a deliberately partial unit into the
    // production pipeline while proving that App::tick reaches the selected
    // static plugin with the authoritative fixed tick.
    {
        App app;
        auto app_fs = std::make_shared<MemoryFileSystem>();
        FileSystemPlugin(app, app_fs);
        auto& app_server = app.world.add_resource<AssetServer>();
        app_server.world = &app.world;
        app_server.fs = app_fs;
        app.world.resource_or_add<Time>();
        app.add_plugin(ProbeGameplayDef{
            "units", AoeGameplaySettings{0.1, 4, 1234}});
        const auto& installed_probe =
            app.world.resource<StaticGameplayPluginProbeState>();
        assert(installed_probe.engagement_calls == 0 &&
               installed_probe.formation_calls == 0 &&
               installed_probe.arrival_rematch_calls == 0 &&
               installed_probe.movement_intent_calls == 0 &&
               installed_probe.local_calls == 0 &&
               installed_probe.global_calls == 0 &&
               installed_probe.sequence == 0 &&
               installed_probe.last_tick == 0);
        auto& app_time = app.world.resource<Time>();
        app_time.dt = .1f;
        app_time.raw_dt = 0.f;
        app.tick();
        const auto& app_clock = app.world.resource<AoeGameplayClock>();
        assert(app_clock.tick == 1 && app_clock.ticks_this_frame == 1);
        const auto& ran_probe =
            app.world.resource<StaticGameplayPluginProbeState>();
        assert(ran_probe.engagement_calls == 1 &&
               ran_probe.formation_calls == 1 &&
               ran_probe.arrival_rematch_calls == 1 &&
               ran_probe.movement_intent_calls == 1 &&
               ran_probe.local_calls == 1 && ran_probe.global_calls == 1 &&
               ran_probe.sequence == 6 && ran_probe.last_tick == 1);
        app_server.shutdown();
        app.shutdown();
    }

    // Global motion backends are late-bound so headless gameplay does not gain
    // an OpenGL dependency. The production default requests GPU, while the
    // fixed pipeline owns the cpu_unit_flow fallback.
    assert(AoeNavigationSettings{}.global_motion_planner_id == "gpu_image");
    {
        App app;
        AoeDefaultGlobalMotionPlugin::install(app);
        // Reinstalling a statically composed plugin must not replace or
        // reject its built-in headless fallback.
        AoeDefaultGlobalMotionPlugin::install(app);
        const auto& installed_registry =
            app.world.resource<AoeGlobalMotionPlannerRegistry>();
        assert(installed_registry.contains("cpu_unit_flow"));
        assert(app.world.try_resource<AoeGlobalMotionPlannerDiagnostics>());
        assert(app.world.try_resource<AoeUnitFlowIndex>());
        app.shutdown();
    }
    AoeGlobalMotionPlannerRegistry motion_registry;
    int planner_calls = 0;
    motion_registry.bind("test", [&](EcsWorld&, std::uint64_t tick,
                                      std::string&) {
        ++planner_calls;
        return tick == 7;
    });
    EcsWorld planner_world;
    std::string planner_error;
    assert((*motion_registry.find("test"))(planner_world, 7, planner_error));
    assert(planner_calls == 1 && motion_registry.erase("test") &&
           !motion_registry.contains("test"));

    const auto parsed = parse(definition_json());
    assert(parsed && parsed->id == "test" && parsed->level == 2);
    assert(parsed->movement.speed == 2.f);
    assert(std::abs(parsed->movement.rotation_speed_radians_per_second -
                    2.f * glm::pi<float>()) < 1e-5f);
    auto turning_definition = definition_json();
    turning_definition["movement"]
        ["rotation_speed_degrees_per_second"] = 90.f;
    assert(std::abs(parse(turning_definition)->movement
                        .rotation_speed_radians_per_second -
                    glm::half_pi<float>()) < 1e-5f);
    assert(parsed->tags == std::vector<std::string>({"scout", "cavalry"}));
    assert(parsed->lifecycle.recycle_after_death);
    assert(parsed->lifecycle.death_duration_seconds == 0.2f);
    assert(parsed->presentation.animation("disappear") == "decayA");
    assert(parsed->target_acquisition.strategy ==
           AoeTargetAcquisitionType::NearestEnemy);
    assert(parsed->target_acquisition.radius == 6.f);
    assert(parsed->target_acquisition.disengage_radius == 9.f);
    assert(parsed->attack->projectile_launch_offset ==
           std::optional<glm::vec3>(glm::vec3(0.f, .5f, 1.5f)));

    // Unit Movement Intent preserves speed while limiting only vector length
    // and angular change; no acceleration state lives in this phase.
    bool turn_limited = false;
    const glm::vec2 quarter_turn = aoe_constrain_unit_velocity(
        {2.f, 0.f}, {0.f, 4.f}, 2.f, glm::pi<float>(), .1f,
        &turn_limited);
    assert(turn_limited && std::abs(glm::length(quarter_turn) - 2.f) < 1e-5f);
    assert(std::abs(std::atan2(quarter_turn.y, quarter_turn.x) -
                    glm::radians(18.f)) < 1e-5f);
    const glm::vec2 half_turn = aoe_constrain_unit_velocity(
        {2.f, 0.f}, {-2.f, 0.f}, 2.f, glm::pi<float>(), .1f);
    assert(std::abs(std::atan2(half_turn.y, half_turn.x)) <=
           glm::radians(18.f) + 1e-5f);
    glm::vec2 jitter_velocity{2.f, 0.f};
    for (int index = 0; index < 12; ++index) {
        const glm::vec2 target = index % 2 == 0
            ? glm::vec2{0.f, 2.f} : glm::vec2{0.f, -2.f};
        const glm::vec2 next = aoe_constrain_unit_velocity(
            jitter_velocity, target, 2.f, glm::half_pi<float>(), .1f);
        const float change = std::acos(std::clamp(glm::dot(
            glm::normalize(jitter_velocity), glm::normalize(next)),
            -1.f, 1.f));
        assert(change <= glm::radians(9.f) + 1e-4f);
        jitter_velocity = next;
    }

    // The plugin reads AoeNavigationPath without advancing it. Core cursor
    // traversal consumes coincident intermediate waypoints before the plugin,
    // allowing the next segment to be emitted in the same tick at full speed.
    Fixture intent_fixture;
    const auto intent_unit = intent_fixture.unit({0.f, 0.f}, 50.f, 1);
    intent_fixture.world.reg().emplace<AoeMoveGoal>(
        intent_unit, AoeMoveGoal{.destination = {1.f, 0.f}});
    intent_fixture.world.reg().emplace<AoeSquadMember>(intent_unit);
    intent_fixture.world.reg().emplace<AoeNavigationPath>(intent_unit,
        AoeNavigationPath{.waypoints = {{.1f, 0.f}, {1.f, 0.f}},
                          .requested_goal = {1.f, 0.f},
                          .request_sequence = 41});
    const AoeNavigationPath path_before = intent_fixture.world.reg()
        .get<AoeNavigationPath>(intent_unit);
    AoeDefaultUnitMovementIntentPlugin::fixed_tick(
        intent_fixture.world, 1);
    const auto& path_after_plugin = intent_fixture.world.reg()
        .get<AoeNavigationPath>(intent_unit);
    assert(path_after_plugin.waypoints == path_before.waypoints &&
           path_after_plugin.current == path_before.current &&
           path_after_plugin.requested_goal == path_before.requested_goal &&
           path_after_plugin.map_revision == path_before.map_revision &&
           path_after_plugin.request_sequence == path_before.request_sequence &&
           path_after_plugin.last_repath_tick == path_before.last_repath_tick &&
           path_after_plugin.blocked_ticks == path_before.blocked_ticks &&
           path_after_plugin.no_path == path_before.no_path &&
           path_after_plugin.include_dynamic_obstacles ==
               path_before.include_dynamic_obstacles &&
           path_after_plugin.dynamic_repath_requested ==
               path_before.dynamic_repath_requested &&
           path_after_plugin.dynamic_repath_failed ==
               path_before.dynamic_repath_failed);
    assert(std::abs(glm::length(intent_fixture.world.reg()
               .get<AoePathMotionRequest>(intent_unit).velocity) - 2.f) < 1e-5f);

    auto& intent_path = intent_fixture.world.reg()
        .get<AoeNavigationPath>(intent_unit);
    intent_path.waypoints = {{0.f, 0.f}, {.1f, 0.f}, {1.f, 0.f}};
    intent_path.current = 0;
    intent_fixture.world.reg().emplace<AoeFormationMemberRouteProgress>(
        intent_unit, AoeFormationMemberRouteProgress{
            .origin = {0.f, 0.f},
            .waypoint_progress = {0.f, .1f, 1.f},
            .segment_speed_ratio = {1.f, 1.f, 1.f}});
    gld::ecs::aoe::detail::
        aoe_gameplay_fixed_after_unit_pathfinder_before_local(
        intent_fixture.world, 2);
    AoeDefaultUnitMovementIntentPlugin::fixed_tick(
        intent_fixture.world, 2);
    assert(intent_path.current == 1);
    const auto& middle_request = intent_fixture.world.reg()
        .get<AoePathMotionRequest>(intent_unit);
    assert(middle_request.valid &&
           middle_request.kind == AoeMovementIntentKind::FormationSlot &&
           std::abs(glm::length(middle_request.velocity) - 2.f) < 1e-5f);

    // A turn-limited member that crosses the intermediate waypoint plane
    // inside the bounded capture corridor advances instead of orbiting it.
    intent_fixture.world.reg().get<AoePosition>(intent_unit).value = {.15f, 0.f};
    gld::ecs::aoe::detail::
        aoe_gameplay_fixed_after_unit_pathfinder_before_local(
        intent_fixture.world, 3);
    assert(intent_path.current == 2);
    intent_fixture.world.reg().get<AoePosition>(intent_unit).value = {.9f, 0.f};
    AoeDefaultUnitMovementIntentPlugin::fixed_tick(
        intent_fixture.world, 3);
    const auto& final_request = intent_fixture.world.reg()
        .get<AoePathMotionRequest>(intent_unit);
    assert(final_request.valid &&
           std::abs(glm::length(final_request.velocity) - .4f) < 1e-4f);
    intent_path.no_path = true;
    AoeDefaultUnitMovementIntentPlugin::fixed_tick(
        intent_fixture.world, 4);
    assert(!intent_fixture.world.reg()
                .get<AoeUnitMovementIntentState>(intent_unit).valid);
    assert(!intent_fixture.world.reg()
                .get<AoePathMotionRequest>(intent_unit).valid);

    Fixture<AoePassThroughLocalAvoidancePlugin,
            SidewaysGlobalMotionProbe> final_constraint_fixture;
    const auto constrained_unit = final_constraint_fixture.unit(
        {0.f, 0.f}, 50.f, 1);
    auto& constrained_movement = final_constraint_fixture.world.reg()
        .get<AoeMovement>(constrained_unit);
    constrained_movement.rotation_speed_radians_per_second = glm::pi<float>();
    final_constraint_fixture.world.reg().get<AoeLocomotionState>(
        constrained_unit).velocity = {2.f, 0.f};
    assert(request_aoe_move(final_constraint_fixture.world,
                            constrained_unit, {10.f, 0.f}));
    final_constraint_fixture.advance_ticks(1);
    const glm::vec2 constrained_velocity = final_constraint_fixture.world.reg()
        .get<AoeLocomotionState>(constrained_unit).velocity;
    assert(glm::length(constrained_velocity) <= 2.f + 1e-5f);
    assert(std::abs(std::atan2(constrained_velocity.y,
                               constrained_velocity.x)) <=
           glm::radians(18.f) + 1e-4f);

    AoeProjectileRegistry registry;
    registry.bind<ArrowProjectileLogic>("arrow");
    assert(registry.contains("arrow") && !registry.contains("missing"));
    bool duplicate_binding_rejected = false;
    try { registry.bind<ArrowProjectileLogic>("arrow"); }
    catch (const std::invalid_argument&) { duplicate_binding_rejected = true; }
    assert(duplicate_binding_rejected);

    static_assert(AoeTargetAcquisitionStrategy<
                  NearestEnemyAcquisitionStrategy>);
    static_assert(std::same_as<
        AoeTargetAcquisitionBinding<
            AoeTargetAcquisitionType::NearestEnemy>::type,
        NearestEnemyAcquisitionStrategy>);
    assert(aoe_target_acquisition_name(
               AoeTargetAcquisitionType::NearestEnemy) == "nearest_enemy");

    const AoeSteeringContext unobstructed_steering{
        .instance_id = 1,
        .preferred_velocity = {2.f, 0.f},
        .goal = {10.f, 0.f},
        .max_speed = 2.f};
    assert(glm::length(DefaultLocalSteeringLogic::steer(
               unobstructed_steering).target_velocity -
           glm::vec2(2.f, 0.f)) < 1e-5f);
    const std::array<AoeSteeringNeighbor, 1> head_on_neighbor{{
        {entt::entity{2}, 2, {1.f, 0.f}, {.2f, .2f}, {-2.f, 0.f}}}};
    AoeSteeringContext head_on_steering{
        .subject = entt::entity{1},
        .instance_id = 1,
        .position = {0.f, 0.f},
        .radii = {.2f, .2f},
        .current_velocity = {2.f, 0.f},
        .preferred_velocity = {2.f, 0.f},
        .goal = {10.f, 0.f},
        .max_speed = 2.f,
        .neighbors = head_on_neighbor};
    const auto head_on_result = DefaultLocalSteeringLogic::steer(
        head_on_steering);
    assert(std::abs(head_on_result.target_velocity.y) > .1f);
    assert(head_on_result.target_velocity == DefaultLocalSteeringLogic::steer(
        head_on_steering).target_velocity);
    const std::array<AoeSteeringNeighbor, 1> reverse_neighbor{{
        {entt::entity{1}, 1, {0.f, 0.f}, {.2f, .2f}, {2.f, 0.f}}}};
    AoeSteeringContext reverse_head_on = head_on_steering;
    reverse_head_on.subject = entt::entity{2};
    reverse_head_on.instance_id = 2;
    reverse_head_on.position = {1.f, 0.f};
    reverse_head_on.current_velocity = {-2.f, 0.f};
    reverse_head_on.preferred_velocity = {-2.f, 0.f};
    reverse_head_on.goal = {-10.f, 0.f};
    reverse_head_on.neighbors = reverse_neighbor;
    const auto reverse_result = DefaultLocalSteeringLogic::steer(
        reverse_head_on);
    assert(head_on_result.target_velocity.y *
               reverse_result.target_velocity.y < 0.f);
    head_on_steering.preferred_avoidance_side =
        head_on_result.avoidance_side;
    head_on_steering.side_switch_margin = 2.35f;
    auto perturbed_neighbor = head_on_neighbor;
    perturbed_neighbor[0].position.y = .01f;
    head_on_steering.neighbors = perturbed_neighbor;
    const auto held_side_result = DefaultLocalSteeringLogic::steer(
        head_on_steering);
    assert(held_side_result.avoidance_side ==
           head_on_result.avoidance_side);

    // Escape steering expands the candidate fan to a wall tangent without
    // introducing a backward direction.
    AoeLogicMap tangent_map(flat_map());
    AoeStaticObstacleDesc tangent_wall;
    tangent_wall.shape = AoeStaticObstacleShape::Aabb;
    tangent_wall.center = {2.65f, 6.f};
    tangent_wall.half_extents = {.25f, 1.2f};
    tangent_map.add_runtime_static_obstacle(tangent_wall);
    AoeSteeringContext tangent_context{
        .instance_id = 7,
        .position = {2.f, 6.f},
        .radii = {.2f, .2f},
        .current_velocity = {1.f, 0.f},
        .preferred_velocity = {1.f, 0.f},
        .goal = {8.f, 6.f},
        .max_speed = 1.f,
        .prediction_seconds = 1.f,
        .map = &tangent_map,
        .candidate_angle_step = .39269908169f,
        .candidate_max_angle = 1.57079632679f};
    const auto tangent_result = DefaultLocalSteeringLogic::steer(
        tangent_context);
    assert(std::abs(tangent_result.target_velocity.y) > .5f);
    assert(tangent_result.target_velocity.x >= -1e-5f);

    auto explicit_acquisition = definition_json();
    explicit_acquisition["target_acquisition"] = {
        {"strategy_id", "nearest_enemy"}, {"radius", 2.5},
        {"disengage_radius", 3.5}};
    const auto explicit_parsed = parse(explicit_acquisition);
    assert(explicit_parsed->target_acquisition.strategy ==
           AoeTargetAcquisitionType::NearestEnemy);
    assert(explicit_parsed->target_acquisition.radius == 2.5f);
    assert(explicit_parsed->target_acquisition.disengage_radius == 3.5f);
    auto unknown_acquisition_id = explicit_acquisition;
    unknown_acquisition_id["target_acquisition"]["strategy_id"] = "missing";
    assert(!parse(unknown_acquisition_id));
    auto negative_acquisition_radius = explicit_acquisition;
    negative_acquisition_radius["target_acquisition"]["radius"] = -1.0;
    assert(!parse(negative_acquisition_radius));
    auto infinite_acquisition_radius = explicit_acquisition;
    infinite_acquisition_radius["target_acquisition"]["radius"] =
        std::numeric_limits<double>::infinity();
    assert(!parse(infinite_acquisition_radius));
    auto short_disengage_radius = explicit_acquisition;
    short_disengage_radius["target_acquisition"]["disengage_radius"] = 2.0;
    assert(!parse(short_disengage_radius));
    auto duplicate_tags = definition_json();
    duplicate_tags["tags"] = nlohmann::json::array({"archer", "archer"});
    assert(!parse(duplicate_tags));
    auto empty_tag = definition_json();
    empty_tag["tags"] = nlohmann::json::array({""});
    assert(!parse(empty_tag));

    using TestSquareFormation = AoeSquareFormation<
        AoeFormationTagPriority<"cavalry", 200>,
        AoeFormationTagPriority<"scout", 100>,
        AoeFormationTagPriority<"archer", -100>>;
    AoeFormationContext formation_context;
    formation_context.spacing = .5f;
    formation_context.members = {
        {{entt::entity{1}, 1}, 0, {"archer"}, {.2f, .2f}},
        {{entt::entity{2}, 2}, 1, {"cavalry"}, {.2f, .2f}},
        {{entt::entity{3}, 3}, 2, {"cavalry", "scout"}, {.2f, .2f}},
        {{entt::entity{4}, 4}, 3, {"unknown"}, {.2f, .2f}}};
    const auto generated_formation =
        TestSquareFormation::generate(formation_context);
    assert(generated_formation);
    const auto& formation_slots = generated_formation->slots;
    assert(formation_slots.size() == 4);
    assert(formation_slots[0].unit.entity == entt::entity{3});
    assert(formation_slots[0].priority == 300);
    assert(formation_slots[1].unit.entity == entt::entity{2});
    assert(formation_slots[1].priority == 200);
    assert(formation_slots[2].unit.entity == entt::entity{4});
    assert(formation_slots[2].priority == 0);
    assert(formation_slots[3].unit.entity == entt::entity{1});
    assert(formation_slots[3].priority == -100);
    assert(formation_slots[0].local_offset.y > formation_slots[2].local_offset.y);
    assert(std::abs(glm::length(formation_slots[1].local_offset -
                                formation_slots[0].local_offset) - .9f) < 1e-5f);
    assert(std::abs(generated_formation->bounds.width() - 1.3f) < 1e-5f);
    assert(std::abs(generated_formation->bounds.height() - 1.3f) < 1e-5f);

    const auto unchanged_width =
        TestSquareFormation::generate_for_width(formation_context, 1.3f);
    assert(unchanged_width && unchanged_width->slots.size() == 4);
    for (std::size_t index = 0; index < formation_slots.size(); ++index) {
        assert(unchanged_width->slots[index].unit.entity ==
               formation_slots[index].unit.entity);
        assert(unchanged_width->slots[index].priority ==
               formation_slots[index].priority);
        assert(glm::length(unchanged_width->slots[index].local_offset -
                           formation_slots[index].local_offset) < 1e-5f);
    }
    const auto narrow_formation =
        TestSquareFormation::generate_for_width(formation_context, .4f);
    assert(narrow_formation && narrow_formation->slots.size() == 4);
    assert(std::abs(narrow_formation->bounds.width() - .4f) < 1e-5f);
    assert(std::abs(narrow_formation->bounds.height() - 3.1f) < 1e-5f);
    for (std::size_t index = 0; index < formation_slots.size(); ++index) {
        assert(narrow_formation->slots[index].unit.entity ==
               formation_slots[index].unit.entity);
        assert(narrow_formation->slots[index].priority ==
               formation_slots[index].priority);
    }
    assert(!TestSquareFormation::generate_for_width(
        formation_context, .399f));
    assert(!TestSquareFormation::generate_for_width(
        formation_context, 0.f));
    assert(!TestSquareFormation::generate_for_width(
        formation_context, std::numeric_limits<float>::infinity()));

    AoeFormationRegistry formation_registry;
    formation_registry.bind<AoeFormationType::Skirmish, TestSquareFormation>();
    assert(formation_registry.contains(AoeFormationType::Skirmish));
    const auto registered_formation = formation_registry.generate(
        AoeFormationType::Skirmish, formation_context);
    assert(registered_formation && registered_formation->slots.size() == 4);
    const auto registered_narrow = formation_registry.generate_for_width(
        AoeFormationType::Skirmish, formation_context, .4f);
    assert(registered_narrow &&
           std::abs(registered_narrow->bounds.width() - .4f) < 1e-5f);
    bool duplicate_formation_rejected = false;
    try {
        formation_registry.bind<AoeFormationType::Skirmish,
                                DefaultSkirmishFormation>();
    } catch (const std::invalid_argument&) {
        duplicate_formation_rejected = true;
    }
    assert(duplicate_formation_rejected);

    AoeFormationRegistry malformed_bounds_registry;
    malformed_bounds_registry.bind<AoeFormationType::Skirmish,
                                   MalformedBoundsFormation>();
    assert(!malformed_bounds_registry.generate(
        AoeFormationType::Skirmish, formation_context));

    AoeFormationRegistry malformed_slot_registry;
    malformed_slot_registry.bind<AoeFormationType::Skirmish,
                                 MalformedSlotFormation>();
    assert(!malformed_slot_registry.generate(
        AoeFormationType::Skirmish, formation_context));

    AoeFormationRegistry ignores_width_registry;
    ignores_width_registry.bind<AoeFormationType::Skirmish,
                                IgnoresMaximumWidthFormation>();
    assert(!ignores_width_registry.generate_for_width(
        AoeFormationType::Skirmish, formation_context, .4f));

    // A large Squad can request a legal single-column variant without
    // changing member identity, ordering, or spacing.
    AoeFormationContext large_formation_context;
    large_formation_context.spacing = .5f;
    large_formation_context.members.reserve(2500);
    for (std::uint32_t index = 0; index < 2500; ++index) {
        large_formation_context.members.push_back({
            {static_cast<entt::entity>(index + 1), index + 1},
            index,
            {},
            {.2f, .2f},
        });
    }
    const auto large_narrow = TestSquareFormation::generate_for_width(
        large_formation_context, .4f);
    assert(large_narrow && large_narrow->slots.size() == 2500);
    assert(std::abs(large_narrow->bounds.width() - .4f) < 1e-5f);
    assert(std::abs(large_narrow->bounds.height() - 2249.5f) < 1e-3f);
    for (std::uint32_t index = 0; index < 2500; ++index) {
        assert(large_narrow->slots[index].unit.entity ==
               static_cast<entt::entity>(index + 1));
        assert(std::abs(large_narrow->slots[index].local_offset.x) < 1e-5f);
        if (index > 0)
            assert(std::abs(large_narrow->slots[index - 1].local_offset.y -
                            large_narrow->slots[index].local_offset.y - .9f) <
                   1e-3f);
    }
    const auto large_ten_columns = TestSquareFormation::generate_for_width(
        large_formation_context, 8.5f);
    assert(large_ten_columns &&
           std::abs(large_ten_columns->bounds.width() - 8.5f) < 1e-4f);
    assert(std::abs(large_ten_columns->bounds.height() - 224.5f) < 1e-3f);

    // The modular formation composition runs in its declared order and a
    // module can stop processing the current squad without invoking later
    // modules.
    AoeSquadSpawnOptions module_probe_options;
    module_probe_options.composition = {{"test", 1, 1}};
    module_probe_options.center = {3.f, 4.f};
    {
        Fixture<AoeFullLocalAvoidancePlugin,
                AoeDefaultGlobalMotionPlugin,
                FormationOrderProbePlugin> fixture;
        const auto squad = spawn_aoe_gameplay_squad(
            fixture.world, module_probe_options);
        assert(squad != entt::null);
        spawn_aoe_gameplay_unit_system(fixture.world);
        fixture.advance_ticks(1);
        assert(fixture.world.resource<FormationModuleProbeState>().calls ==
               std::vector<std::uint32_t>({1, 2, 3, 4, 5}));
    }
    {
        Fixture<AoeFullLocalAvoidancePlugin,
                AoeDefaultGlobalMotionPlugin,
                FormationStopProbePlugin> fixture;
        const auto squad = spawn_aoe_gameplay_squad(
            fixture.world, module_probe_options);
        assert(squad != entt::null);
        spawn_aoe_gameplay_unit_system(fixture.world);
        fixture.advance_ticks(1);
        assert(fixture.world.resource<FormationModuleProbeState>().calls ==
               std::vector<std::uint32_t>({1, 2}));
    }

    // The extracted SquadLayout is behaviorally identical to the legacy Full
    // backend for initial placement, facing, history and squad bounds.
    Fixture<> full_layout_fixture;
    Fixture<AoeFullLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin,
            AoeLayoutOnlyFormationPlugin> modular_layout_fixture;
    AoeSquadSpawnOptions layout_options;
    layout_options.composition = {{"test", 4, 1}};
    layout_options.center = {6.f, 5.f};
    layout_options.forward = {0.f, 1.f};
    layout_options.formation_spacing = .4f;
    layout_options.team_id = 3;
    const auto full_layout_squad = spawn_aoe_gameplay_squad(
        full_layout_fixture.world, layout_options);
    const auto modular_layout_squad = spawn_aoe_gameplay_squad(
        modular_layout_fixture.world, layout_options);
    spawn_aoe_gameplay_unit_system(full_layout_fixture.world);
    spawn_aoe_gameplay_unit_system(modular_layout_fixture.world);
    full_layout_fixture.advance_ticks(1);
    modular_layout_fixture.advance_ticks(1);
    const auto& full_formation = full_layout_fixture.world.reg()
        .get<AoeSquadFormation>(full_layout_squad);
    auto& modular_formation = modular_layout_fixture.world.reg()
        .get<AoeSquadFormation>(modular_layout_squad);
    const auto& full_layout = full_layout_fixture.world.reg()
        .get<AoeSquadLayoutState>(full_layout_squad).layout;
    auto& modular_layout_state = modular_layout_fixture.world.reg()
        .get<AoeSquadLayoutState>(modular_layout_squad);
    auto& modular_layout = modular_layout_state.layout;
    assert(full_layout.slots.size() == modular_layout.slots.size());
    for (std::size_t index = 0; index < full_layout.slots.size(); ++index) {
        const auto& full_slot = full_layout.slots[index];
        const auto& modular_slot = modular_layout.slots[index];
        const auto full_ordinal = full_layout_fixture.world.reg()
            .get<AoeSquadMember>(full_slot.unit.entity).ordinal;
        const auto modular_ordinal = modular_layout_fixture.world.reg()
            .get<AoeSquadMember>(modular_slot.unit.entity).ordinal;
        assert(full_ordinal == modular_ordinal);
        assert(full_slot.priority == modular_slot.priority);
        assert(glm::length(full_slot.local_offset -
                           modular_slot.local_offset) < 1e-5f);
        const auto full_position = full_layout_fixture.world.reg()
            .get<AoePosition>(full_slot.unit.entity).value;
        const auto modular_position = modular_layout_fixture.world.reg()
            .get<AoePosition>(modular_slot.unit.entity).value;
        assert(glm::length(full_position - modular_position) < 1e-5f);
        assert(glm::length(modular_position - aoe_formation_slot_world(
            modular_layout_fixture.world.reg().get<AoePosition>(
                modular_layout_squad), modular_formation,
            modular_slot)) < 1e-5f);
        assert(glm::length(full_layout_fixture.world.reg()
            .get<AoePositionHistory>(full_slot.unit.entity).previous -
            modular_layout_fixture.world.reg()
            .get<AoePositionHistory>(modular_slot.unit.entity).previous) <
            1e-5f);
        assert(full_layout_fixture.world.reg()
            .get<AoeFacing>(full_slot.unit.entity).direction ==
            modular_layout_fixture.world.reg()
            .get<AoeFacing>(modular_slot.unit.entity).direction);
    }
    const auto full_bound = full_layout_fixture.world.reg()
        .get<AoeCollider>(full_layout_squad);
    const auto modular_bound = modular_layout_fixture.world.reg()
        .get<AoeCollider>(modular_layout_squad);
    assert(std::abs(full_bound.radius_x - modular_bound.radius_x) < 1e-5f);
    assert(std::abs(full_bound.radius_y - modular_bound.radius_y) < 1e-5f);
    assert(std::abs(full_bound.height - modular_bound.height) < 1e-5f);
    assert(!modular_formation.dirty &&
           !modular_formation.teleport_on_next_layout);
    assert(modular_layout_fixture.world.reg().get<AoeSquadState>(
               modular_layout_squad).phase == AoeSquadPhase::Idle);

    // A later dirty rebuild updates slots and bounds, but never teleports
    // members or rewrites interpolation history.
    std::vector<glm::vec2> positions_before_rebuild;
    std::vector<glm::vec2> history_before_rebuild;
    for (const auto& member : modular_layout_fixture.world.reg()
             .get<AoeSquadMembers>(modular_layout_squad).active) {
        positions_before_rebuild.push_back(modular_layout_fixture.world.reg()
            .get<AoePosition>(member.entity).value);
        history_before_rebuild.push_back(modular_layout_fixture.world.reg()
            .get<AoePositionHistory>(member.entity).previous);
    }
    const auto first_offset_before_rebuild =
        modular_layout.slots.front().local_offset;
    const auto revision_before_rebuild = modular_layout_state.revision;
    modular_formation.spacing += .75f;
    modular_formation.dirty = true;
    modular_formation.teleport_on_next_layout = false;
    AoeLayoutOnlyFormationPlugin::fixed_tick(
        modular_layout_fixture.world, 2);
    assert(!modular_formation.dirty);
    assert(modular_layout_state.revision == revision_before_rebuild + 1);
    assert(glm::length(modular_layout.slots.front().local_offset -
                       first_offset_before_rebuild) > 1e-5f);
    const auto& rebuilt_members = modular_layout_fixture.world.reg()
        .get<AoeSquadMembers>(modular_layout_squad).active;
    for (std::size_t index = 0; index < rebuilt_members.size(); ++index) {
        assert(glm::length(modular_layout_fixture.world.reg()
            .get<AoePosition>(rebuilt_members[index].entity).value -
            positions_before_rebuild[index]) < 1e-5f);
        assert(glm::length(modular_layout_fixture.world.reg()
            .get<AoePositionHistory>(rebuilt_members[index].entity).previous -
            history_before_rebuild[index]) < 1e-5f);
    }

    // A rejected rebuild is recoverable when prior slots exist.
    const auto retained_slots = modular_layout.slots;
    const auto retained_bounds = modular_layout.bounds;
    const auto retained_revision = modular_layout_state.revision;
    modular_formation.type = InvalidFormationType;
    modular_formation.dirty = true;
    AoeLayoutOnlyFormationPlugin::fixed_tick(
        modular_layout_fixture.world, 3);
    assert(!modular_formation.dirty);
    assert(modular_layout_state.revision == retained_revision);
    assert(modular_layout.bounds.local_min == retained_bounds.local_min);
    assert(modular_layout.bounds.local_max == retained_bounds.local_max);
    assert(modular_layout.slots.size() == retained_slots.size());
    for (std::size_t index = 0; index < retained_slots.size(); ++index) {
        assert(modular_layout.slots[index].unit.entity ==
               retained_slots[index].unit.entity);
        assert(modular_layout.slots[index].priority ==
               retained_slots[index].priority);
        assert(glm::length(modular_layout.slots[index].local_offset -
                           retained_slots[index].local_offset) < 1e-5f);
    }
    assert(modular_layout_fixture.world.reg().get<AoeSquadSpawnState>(
               modular_layout_squad).status == AoeSquadSpawnStatus::Ready);
    assert(modular_layout_fixture.world.reg().get<AoeSquadSpawnState>(
               modular_layout_squad).errors ==
           std::vector<std::string>{
               "formation layout rejected; retaining previous slots"});

    // With no previous layout, failure is terminal and clears every command
    // or route that could keep the squad and its members active.
    Fixture<AoeFullLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin,
            AoeLayoutOnlyFormationPlugin> failed_layout_fixture;
    const auto failed_layout_squad = spawn_aoe_gameplay_squad(
        failed_layout_fixture.world, layout_options);
    spawn_aoe_gameplay_unit_system(failed_layout_fixture.world);
    failed_layout_fixture.advance_ticks(1);
    auto& failed_layout = failed_layout_fixture.world.reg()
        .get<AoeSquadFormation>(failed_layout_squad);
    failed_layout_fixture.world.reg().get<AoeSquadLayoutState>(
        failed_layout_squad) = {};
    failed_layout.type = InvalidFormationType;
    failed_layout.dirty = true;
    failed_layout_fixture.world.reg().get<AoeSquadOrder>(
        failed_layout_squad) = {
            AoeSquadOrderType::MoveTo, {15.f, 5.f}, {}, 9};
    failed_layout_fixture.world.reg().get<AoeSquadState>(
        failed_layout_squad).phase = AoeSquadPhase::Moving;
    failed_layout_fixture.world.reg().emplace_or_replace<AoeNavigationPath>(
        failed_layout_squad, AoeNavigationPath{{{15.f, 5.f}}, 0});
    for (const auto& member : failed_layout_fixture.world.reg()
             .get<AoeSquadMembers>(failed_layout_squad).active) {
        failed_layout_fixture.world.reg().emplace_or_replace<
            AoeAttackMoveOrder>(member.entity,
                               AoeAttackMoveOrder{{15.f, 5.f}});
        failed_layout_fixture.world.reg().emplace_or_replace<
            AoeMoveGoal>(member.entity, AoeMoveGoal{{15.f, 5.f}});
        failed_layout_fixture.world.reg().emplace_or_replace<
            AoeNavigationPath>(member.entity,
                              AoeNavigationPath{{{15.f, 5.f}}, 0});
        failed_layout_fixture.world.reg().emplace_or_replace<
            AoeSquadMoveSpeedLimit>(member.entity,
                                   AoeSquadMoveSpeedLimit{1.f});
    }
    AoeLayoutOnlyFormationPlugin::fixed_tick(
        failed_layout_fixture.world, 10);
    assert(failed_layout_fixture.world.reg().get<AoeSquadSpawnState>(
               failed_layout_squad).status == AoeSquadSpawnStatus::Failed);
    assert(failed_layout_fixture.world.reg().get<AoeSquadState>(
               failed_layout_squad).phase == AoeSquadPhase::Failed);
    assert(failed_layout_fixture.world.reg().get<AoeSquadOrder>(
               failed_layout_squad).type == AoeSquadOrderType::Idle);
    assert(!failed_layout_fixture.world.reg().all_of<AoeNavigationPath>(
        failed_layout_squad));
    const auto& terminal_layout_result = failed_layout_fixture.world.reg()
        .get<AoeFormationResult>(failed_layout_squad);
    assert(terminal_layout_result.valid &&
           terminal_layout_result.produced_tick == 10 &&
           terminal_layout_result.status ==
               AoeFormationResultStatus::Failed);
    for (const auto& member : failed_layout_fixture.world.reg()
             .get<AoeSquadMembers>(failed_layout_squad).active) {
        assert(!failed_layout_fixture.world.reg().all_of<
            AoeAttackMoveOrder>(member.entity));
        assert(!failed_layout_fixture.world.reg().all_of<
            AoeMoveGoal>(member.entity));
        assert(!failed_layout_fixture.world.reg().all_of<
            AoeNavigationPath>(member.entity));
        assert(!failed_layout_fixture.world.reg().all_of<
            AoeSquadMoveSpeedLimit>(member.entity));
        assert(failed_layout_fixture.world.reg().get<AoeActionState>(
                   member.entity).state == UnitState::Idle);
    }

    // Without RouteSplit, the minimal MovingControl bridge has no member route
    // to activate. Commands remain pending and no member execution state is
    // synthesized while only SquadLayout is enabled.
    Fixture<AoeFullLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin,
            AoeLayoutOnlyFormationPlugin,
            AoePassThroughSquadEngagementPlugin>
        layout_only_behavior_fixture;
    const auto layout_only_squad = spawn_aoe_gameplay_squad(
        layout_only_behavior_fixture.world, layout_options);
    spawn_aoe_gameplay_unit_system(layout_only_behavior_fixture.world);
    layout_only_behavior_fixture.advance_ticks(1);
    const auto layout_only_anchor = layout_only_behavior_fixture.world.reg()
        .get<AoePosition>(layout_only_squad).value;
    assert(request_aoe_squad_move(
        layout_only_behavior_fixture.world, layout_only_squad,
        {16.f, 5.f}));
    layout_only_behavior_fixture.advance_ticks(3);
    assert(glm::length(layout_only_behavior_fixture.world.reg()
        .get<AoePosition>(layout_only_squad).value -
        layout_only_anchor) < 1e-5f);
    assert(layout_only_behavior_fixture.world.reg().get<AoeSquadOrder>(
               layout_only_squad).type == AoeSquadOrderType::MoveTo);
    for (const auto& member : layout_only_behavior_fixture.world.reg()
             .get<AoeSquadMembers>(layout_only_squad).active) {
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeMoveGoal>(member.entity));
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeNavigationPath>(member.entity));
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeAttackOrder>(member.entity));
    }
    const auto layout_only_enemy = layout_only_behavior_fixture.unit(
        {8.f, 5.f}, 50.f, 9);
    assert(request_aoe_squad_attack(
        layout_only_behavior_fixture.world, layout_only_squad,
        layout_only_enemy));
    layout_only_behavior_fixture.advance_ticks(2);
    assert(layout_only_behavior_fixture.world.reg().get<AoeSquadOrder>(
               layout_only_squad).type == AoeSquadOrderType::AttackTarget);
    assert(glm::length(layout_only_behavior_fixture.world.reg()
        .get<AoePosition>(layout_only_squad).value -
        layout_only_anchor) < 1e-5f);
    for (const auto& member : layout_only_behavior_fixture.world.reg()
             .get<AoeSquadMembers>(layout_only_squad).active) {
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeMoveGoal>(member.entity));
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeNavigationPath>(member.entity));
        assert(!layout_only_behavior_fixture.world.reg().all_of<
            AoeAttackOrder>(member.entity));
    }

    // RouteSplit consumes one squad-level NavMesh corridor and emits one
    // statically safe virtual-portal Funnel route per slot. Re-running the
    // same episode hits the split cache and performs no additional query.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoePassThroughGlobalMotionPlugin,
            AoeLayoutRouteSplitMovingFormationPlugin,
            AoePassThroughSquadEngagementPlugin,
            AoePassThroughSquadArrivalRematchPlugin,
            AoeGridAStarUnitPathfinderPlugin,
            AoeNavMeshSquadPathfinderPlugin>
        route_split_fixture;
    route_split_fixture.world.add_resource<AoeLogicMap>(squad_stress_map());
    AoeSquadSpawnOptions route_split_options;
    route_split_options.composition = {{"test", 4, 1}};
    route_split_options.center = {-12.f, 0.f};
    route_split_options.forward = {1.f, 0.f};
    route_split_options.formation_spacing = .4f;
    const auto route_split_squad = spawn_aoe_gameplay_squad(
        route_split_fixture.world, route_split_options);
    spawn_aoe_gameplay_unit_system(route_split_fixture.world);
    route_split_fixture.advance_ticks(1);
    assert(request_aoe_squad_move(
        route_split_fixture.world, route_split_squad, {12.f, 0.f}));
    std::vector<glm::vec2> split_initial_positions;
    for (const auto& member : route_split_fixture.world.reg().get<
             AoeSquadMembers>(route_split_squad).active)
        split_initial_positions.push_back(route_split_fixture.world.reg().get<
            AoePosition>(member.entity).value);
    route_split_fixture.advance_ticks(1);
    const auto& split_corridor = route_split_fixture.world.reg().get<
        AoeFormationNavCorridor>(route_split_squad);
    const auto& split_state = route_split_fixture.world.reg().get<
        AoeFormationRouteSplitState>(route_split_squad);
    assert(split_corridor.valid);
    assert(split_state.status == AoeFormationRouteSplitStatus::Ready);
    assert(split_state.units.size() == 4);
    const auto& split_trajectory = route_split_fixture.world.reg().get<
        AoeFormationRouteTrajectory>(route_split_squad);
    const auto& split_plan = route_split_fixture.world.reg().get<
        AoeFormationRoutePlan>(route_split_squad);
    assert(split_plan.valid && !split_plan.narrowed);
    assert(split_plan.travel_layout.slots.size() == 4);
    assert(split_plan.poses.size() >= 2);
    assert(std::abs(split_plan.natural_width -
                    split_plan.selected_width) < 1e-5f);
    assert(std::abs(split_plan.prelude_progress +
                    split_plan.travel_progress +
                    split_plan.postlude_progress -
                    split_plan.total_progress) < 1e-4f);
    assert(split_trajectory.valid && split_trajectory.frames.size() >= 2 &&
           split_trajectory.total_progress > 0.f);
    const auto& split_diagnostics = route_split_fixture.world.resource<
        AoeFormationRouteSplitDiagnostics>();
    assert(split_diagnostics.splits == 1);
    assert(split_diagnostics.members_routed == 4);
    assert(split_diagnostics.members_failed == 0);
    assert(route_split_fixture.world.resource<AoeNavMeshResource>()
               .diagnostics().query_count == 1);
    std::vector<glm::vec2> split_goals;
    const auto& split_map = route_split_fixture.world.resource<AoeLogicMap>();
    for (const auto& slot : route_split_fixture.world.reg().get<
             AoeSquadLayoutState>(route_split_squad).layout.slots) {
        const auto entity = slot.unit.entity;
        const auto& path = route_split_fixture.world.reg().get<
            AoeNavigationPath>(entity);
        const auto& collider = route_split_fixture.world.reg().get<
            AoeCollider>(entity);
        assert(!path.no_path && !path.waypoints.empty());
        assert(route_split_fixture.world.reg().all_of<
            AoeFormationRouteOwner>(entity));
        const auto& route_progress = route_split_fixture.world.reg().get<
            AoeFormationMemberRouteProgress>(entity);
        assert(route_progress.squad == route_split_squad &&
               route_progress.waypoint_progress.size() ==
                   path.waypoints.size() &&
               route_progress.segment_speed_ratio.size() ==
                   path.waypoints.size());
        assert(route_split_fixture.world.reg().all_of<
            AoeMoveGoal, AoeFormationMoveGoalOwner>(entity));
        assert(path.request_sequence == route_split_fixture.world.reg().get<
            AoeSquadOrder>(route_split_squad).revision);
        assert(glm::length(route_split_fixture.world.reg().get<
            AoeMoveGoal>(entity).destination - path.requested_goal) < 1e-5f);
        glm::vec2 previous = route_split_fixture.world.reg().get<
            AoePosition>(entity).value;
        for (const auto waypoint : path.waypoints) {
            assert(split_map.static_safe_fraction(previous, waypoint,
                {collider.radius_x, collider.radius_y}) >= .999f);
            previous = waypoint;
        }
        split_goals.push_back(path.requested_goal);
    }
    bool distinct_split_goals = false;
    for (std::size_t index = 1; index < split_goals.size(); ++index)
        distinct_split_goals = distinct_split_goals ||
            glm::length(split_goals[index] - split_goals.front()) > .1f;
    assert(distinct_split_goals);
    bool split_member_moved = false;
    const auto& split_members = route_split_fixture.world.reg().get<
        AoeSquadMembers>(route_split_squad).active;
    for (std::size_t index = 0; index < split_members.size(); ++index)
        split_member_moved = split_member_moved || glm::length(
            route_split_fixture.world.reg().get<AoePosition>(
                split_members[index].entity).value -
            split_initial_positions[index]) > 1e-5f;
    assert(split_member_moved);
    route_split_fixture.advance_ticks(1);
    assert(route_split_fixture.world.resource<AoeNavMeshResource>()
               .diagnostics().query_count == 1);
    assert(route_split_fixture.world.resource<
               AoeFormationRouteSplitDiagnostics>().splits == 1);
    assert(route_split_fixture.world.resource<
               AoeFormationRouteSplitDiagnostics>().cache_hits >= 1);
    assert(request_aoe_squad_stop(
        route_split_fixture.world, route_split_squad));
    route_split_fixture.advance_ticks(1);
    for (const auto& member : route_split_fixture.world.reg().get<
             AoeSquadMembers>(route_split_squad).active) {
        assert(!route_split_fixture.world.reg().all_of<
            AoeFormationRouteOwner>(member.entity));
        assert(!route_split_fixture.world.reg().all_of<
            AoeNavigationPath>(member.entity));
        assert(!route_split_fixture.world.reg().all_of<
            AoeMoveGoal, AoeFormationMoveGoalOwner>(member.entity));
        assert(!route_split_fixture.world.reg().any_of<
            AoeFormationMemberRouteProgress,
            AoeSquadMoveSpeedLimit>(member.entity));
    }
    assert(!route_split_fixture.world.reg().any_of<
        AoeFormationRouteTrajectory, AoeFormationMovingState>(
            route_split_squad));

    // A corridor narrower than the natural 5x5 footprint selects one
    // generator-owned travel layout for the complete route. The authoritative
    // natural Squad layout remains unchanged and is restored by Postlude.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoePassThroughGlobalMotionPlugin,
            AoeLayoutRouteSplitMovingFormationPlugin,
            AoePassThroughSquadEngagementPlugin,
            AoePassThroughSquadArrivalRematchPlugin,
            AoeGridAStarUnitPathfinderPlugin,
            AoeNavMeshSquadPathfinderPlugin>
        narrow_route_fixture;
    narrow_route_fixture.world.add_resource<AoeLogicMap>(squad_stress_map());
    AoeSquadSpawnOptions narrow_route_options = route_split_options;
    narrow_route_options.composition = {{"test", 25, 1}};
    const auto narrow_route_squad = spawn_aoe_gameplay_squad(
        narrow_route_fixture.world, narrow_route_options);
    spawn_aoe_gameplay_unit_system(narrow_route_fixture.world);
    narrow_route_fixture.advance_ticks(1);
    const auto narrow_natural_revision = narrow_route_fixture.world.reg().get<
        AoeSquadLayoutState>(narrow_route_squad).revision;
    const auto narrow_natural_layout = narrow_route_fixture.world.reg().get<
        AoeSquadLayoutState>(narrow_route_squad).layout;
    assert(request_aoe_squad_move(
        narrow_route_fixture.world, narrow_route_squad, {12.f, 0.f}));
    narrow_route_fixture.advance_ticks(1);
    assert(narrow_route_fixture.world.reg().all_of<
        AoeFormationRoutePlan>(narrow_route_squad));
    const auto& narrow_plan = narrow_route_fixture.world.reg().get<
        AoeFormationRoutePlan>(narrow_route_squad);
    assert(narrow_plan.valid && narrow_plan.narrowed);
    assert(narrow_plan.natural_width > narrow_plan.bottleneck_width);
    assert(narrow_plan.selected_width <=
           narrow_plan.bottleneck_width + 1e-5f);
    assert(narrow_plan.travel_layout.slots.size() == 25);
    const auto& narrow_layout_after = narrow_route_fixture.world.reg().get<
        AoeSquadLayoutState>(narrow_route_squad);
    assert(narrow_layout_after.revision == narrow_natural_revision);
    assert(std::abs(narrow_layout_after.layout.bounds.width() -
                    narrow_natural_layout.bounds.width()) < 1e-5f);
    for (std::size_t index = 0;
         index < narrow_natural_layout.slots.size(); ++index) {
        assert(narrow_layout_after.layout.slots[index].unit.entity ==
               narrow_natural_layout.slots[index].unit.entity);
        assert(glm::length(
            narrow_layout_after.layout.slots[index].local_offset -
               narrow_natural_layout.slots[index].local_offset) < 1e-5f);
    }
    // Turn-aware RouteSplit keeps one immutable slot assignment while it
    // generates a shared, subdivided 90-degree moving turn. MovingControl
    // publishes per-member speed limits that map different turn radii back to
    // the same Squad progress in one O(member_count) pass.
    using TurnFixture = Fixture<
        AoePassThroughLocalAvoidancePlugin,
        AoePassThroughGlobalMotionPlugin,
        AoeLayoutRouteSplitMovingFormationPlugin,
        AoePassThroughSquadEngagementPlugin,
        AoePassThroughSquadArrivalRematchPlugin,
        AoeGridAStarUnitPathfinderPlugin,
        AoeNavMeshSquadPathfinderPlugin>;
    TurnFixture quarter_turn_fixture;
    quarter_turn_fixture.world.add_resource<AoeLogicMap>(centered_flat_map());
    AoeSquadSpawnOptions turn_options;
    turn_options.composition = {{"test", 5, 1}};
    turn_options.center = {0.f, 0.f};
    turn_options.forward = {1.f, 0.f};
    turn_options.formation_spacing = .4f;
    const auto quarter_turn_squad = spawn_aoe_gameplay_squad(
        quarter_turn_fixture.world, turn_options);
    spawn_aoe_gameplay_unit_system(quarter_turn_fixture.world);
    quarter_turn_fixture.advance_ticks(1);
    const auto slots_before_turn = quarter_turn_fixture.world.reg().get<
        AoeSquadLayoutState>(quarter_turn_squad).layout.slots;
    const auto layout_revision_before_turn = quarter_turn_fixture.world.reg()
        .get<AoeSquadLayoutState>(quarter_turn_squad).revision;
    assert(request_aoe_squad_move(
        quarter_turn_fixture.world, quarter_turn_squad, {0.f, 10.f}));
    quarter_turn_fixture.advance_ticks(1);
    const auto& quarter_turn_trajectory = quarter_turn_fixture.world.reg().get<
        AoeFormationRouteTrajectory>(quarter_turn_squad);
    const auto& quarter_turn_plan = quarter_turn_fixture.world.reg().get<
        AoeFormationRoutePlan>(quarter_turn_squad);
    assert(quarter_turn_trajectory.valid);
    assert_snake_travel_moves_forward(quarter_turn_plan,
        quarter_turn_fixture.world.resource<
            AoeFormationRouteSplitSettings>().minimum_member_forward_ratio);
    std::size_t quarter_turn_rotation_frames = 0;
    std::size_t quarter_turn_in_place_frames = 0;
    float quarter_turn_rotation = 0.f;
    for (std::size_t index = 1;
         index < quarter_turn_trajectory.frames.size(); ++index) {
        const auto& previous = quarter_turn_trajectory.frames[index - 1];
        const auto& current = quarter_turn_trajectory.frames[index];
        const float angle = std::acos(std::clamp(
            glm::dot(previous.forward, current.forward), -1.f, 1.f));
        assert(angle <= glm::radians(10.f) + 1e-4f);
        if (angle > 1e-5f) {
            ++quarter_turn_rotation_frames;
            quarter_turn_rotation += angle;
            if (glm::length(current.center - previous.center) < 1e-5f)
                ++quarter_turn_in_place_frames;
        }
    }
    assert(quarter_turn_rotation_frames >= 9);
    assert(quarter_turn_in_place_frames == 0);
    assert(quarter_turn_rotation >= glm::radians(89.f));
    const auto& layout_after_turn = quarter_turn_fixture.world.reg().get<
        AoeSquadLayoutState>(quarter_turn_squad);
    assert(layout_after_turn.revision ==
           layout_revision_before_turn);
    assert(layout_after_turn.layout.slots.size() == slots_before_turn.size());
    float minimum_turn_speed = std::numeric_limits<float>::infinity();
    float maximum_turn_speed = 0.f;
    for (std::size_t index = 0; index < slots_before_turn.size(); ++index) {
        const auto& before = slots_before_turn[index];
        const auto& after = layout_after_turn.layout.slots[index];
        assert(after.unit.entity == before.unit.entity &&
               after.unit.instance_id == before.unit.instance_id &&
               after.priority == before.priority &&
               glm::length(after.local_offset - before.local_offset) < 1e-5f);
        const auto& path = quarter_turn_fixture.world.reg().get<
            AoeNavigationPath>(after.unit.entity);
        const auto& progress = quarter_turn_fixture.world.reg().get<
            AoeFormationMemberRouteProgress>(after.unit.entity);
        assert(progress.waypoint_progress.size() == path.waypoints.size() &&
               progress.segment_speed_ratio.size() == path.waypoints.size());
        const float speed = quarter_turn_fixture.world.reg().get<
            AoeSquadMoveSpeedLimit>(after.unit.entity).value;
        minimum_turn_speed = std::min(minimum_turn_speed, speed);
        maximum_turn_speed = std::max(maximum_turn_speed, speed);
    }
    assert(maximum_turn_speed - minimum_turn_speed > .05f);
    quarter_turn_fixture.advance_ticks(8);
    const auto& synchronized_turn = quarter_turn_fixture.world.reg().get<
        AoeFormationMovingState>(quarter_turn_squad);
    const auto& moving_settings = quarter_turn_fixture.world.resource<
        AoeFormationMovingSettings>();
    assert(synchronized_turn.maximum_lead <=
           moving_settings.allowed_progress_lead + .05f);

    // Reissuing an order replaces all ownership revisions. Removing a member
    // dirties the layout, rebuilds the shared routes, and strips the dead
    // member's route metadata in the same fixed tick.
    const auto previous_order_revision = quarter_turn_fixture.world.reg().get<
        AoeSquadOrder>(quarter_turn_squad).revision;
    assert(request_aoe_squad_move(
        quarter_turn_fixture.world, quarter_turn_squad, {-10.f, 0.f}));
    quarter_turn_fixture.advance_ticks(1);
    const auto replacement_revision = quarter_turn_fixture.world.reg().get<
        AoeSquadOrder>(quarter_turn_squad).revision;
    assert(replacement_revision > previous_order_revision);
    for (const auto& member : quarter_turn_fixture.world.reg().get<
             AoeSquadMembers>(quarter_turn_squad).active) {
        assert(quarter_turn_fixture.world.reg().get<
                   AoeFormationRouteOwner>(member.entity)
                   .squad_order_revision == replacement_revision);
        assert(quarter_turn_fixture.world.reg().get<
                   AoeFormationMemberRouteProgress>(member.entity)
                   .squad_order_revision == replacement_revision);
    }
    const auto removed_turn_member = quarter_turn_fixture.world.reg().get<
        AoeSquadMembers>(quarter_turn_squad).active.front().entity;
    assert(set_aoe_unit_health(
        quarter_turn_fixture.world, removed_turn_member, 0.f));
    quarter_turn_fixture.advance_ticks(1);
    assert(!quarter_turn_fixture.world.reg().any_of<
        AoeFormationRouteOwner, AoeFormationMoveGoalOwner,
        AoeFormationMemberRouteProgress, AoeSquadMoveSpeedLimit,
        AoeMoveGoal, AoeNavigationPath>(removed_turn_member));

    // A 180-degree command uses a forward-only bounded-radius turn. No
    // adjacent orientation frame may exceed the configured ten-degree bound,
    // no rotation frame stops, and no member reverses along the Travel curve.
    TurnFixture half_turn_fixture;
    half_turn_fixture.world.add_resource<AoeLogicMap>(centered_flat_map());
    const auto half_turn_squad = spawn_aoe_gameplay_squad(
        half_turn_fixture.world, turn_options);
    spawn_aoe_gameplay_unit_system(half_turn_fixture.world);
    half_turn_fixture.advance_ticks(1);
    const auto half_turn_layout_revision = half_turn_fixture.world.reg().get<
        AoeSquadLayoutState>(half_turn_squad).revision;
    assert(request_aoe_squad_move(
        half_turn_fixture.world, half_turn_squad, {-10.f, 0.f}));
    half_turn_fixture.advance_ticks(1);
    const auto& half_turn_trajectory = half_turn_fixture.world.reg().get<
        AoeFormationRouteTrajectory>(half_turn_squad);
    const auto& half_turn_plan = half_turn_fixture.world.reg().get<
        AoeFormationRoutePlan>(half_turn_squad);
    assert_snake_travel_moves_forward(half_turn_plan,
        half_turn_fixture.world.resource<
            AoeFormationRouteSplitSettings>().minimum_member_forward_ratio);
    std::size_t half_turn_rotation_frames = 0;
    std::size_t half_turn_in_place_frames = 0;
    float half_turn_rotation = 0.f;
    for (std::size_t index = 1;
         index < half_turn_trajectory.frames.size(); ++index) {
        const auto& previous = half_turn_trajectory.frames[index - 1];
        const auto& current = half_turn_trajectory.frames[index];
        const float angle = std::acos(std::clamp(
            glm::dot(previous.forward, current.forward), -1.f, 1.f));
        assert(angle <= glm::radians(10.f) + 1e-4f);
        if (angle > 1e-5f) {
            ++half_turn_rotation_frames;
            half_turn_rotation += angle;
            if (glm::length(current.center - previous.center) < 1e-5f)
                ++half_turn_in_place_frames;
        }
    }
    assert(half_turn_trajectory.valid &&
           half_turn_rotation_frames >= 18 &&
           half_turn_in_place_frames == 0 &&
           half_turn_rotation >= glm::radians(179.f));
    bool half_turn_selected_left = false;
    for (const auto& pose : half_turn_plan.poses)
        half_turn_selected_left = half_turn_selected_left || pose.center.y > .1f;
    assert(half_turn_selected_left);
    assert(half_turn_fixture.world.reg().get<AoeSquadLayoutState>(
               half_turn_squad).revision == half_turn_layout_revision);

    // A static obstacle occupying the deterministic left U-turn candidate
    // forces the same 180-degree command to select the safe right candidate.
    auto right_turn_map = centered_flat_map();
    AoeStaticObstacleDesc left_turn_blocker;
    left_turn_blocker.shape = AoeStaticObstacleShape::Aabb;
    left_turn_blocker.center = {0.f, 2.f};
    left_turn_blocker.half_extents = {3.f, .4f};
    right_turn_map.static_obstacles.push_back(left_turn_blocker);
    TurnFixture right_turn_fixture;
    right_turn_fixture.world.add_resource<AoeLogicMap>(right_turn_map);
    const auto right_turn_squad = spawn_aoe_gameplay_squad(
        right_turn_fixture.world, turn_options);
    spawn_aoe_gameplay_unit_system(right_turn_fixture.world);
    right_turn_fixture.advance_ticks(1);
    assert(request_aoe_squad_move(
        right_turn_fixture.world, right_turn_squad, {-10.f, 0.f}));
    right_turn_fixture.advance_ticks(1);
    const auto& right_turn_plan = right_turn_fixture.world.reg().get<
        AoeFormationRoutePlan>(right_turn_squad);
    bool right_turn_selected_right = false;
    for (const auto& pose : right_turn_plan.poses)
        right_turn_selected_right =
            right_turn_selected_right || pose.center.y < -.1f;
    assert(right_turn_selected_right);
    assert_snake_travel_moves_forward(right_turn_plan,
        right_turn_fixture.world.resource<
            AoeFormationRouteSplitSettings>().minimum_member_forward_ratio);

    // The clockwise mirror must also finish. This catches members circling a
    // dense turn waypoint while MovingControl waits on the slowest progress.
    TurnFixture mirrored_turn_fixture;
    mirrored_turn_fixture.world.add_resource<AoeLogicMap>(centered_flat_map());
    const auto mirrored_turn_squad = spawn_aoe_gameplay_squad(
        mirrored_turn_fixture.world, turn_options);
    spawn_aoe_gameplay_unit_system(mirrored_turn_fixture.world);
    mirrored_turn_fixture.advance_ticks(1);
    assert(request_aoe_squad_move(
        mirrored_turn_fixture.world, mirrored_turn_squad, {0.f, -10.f}));
    for (int tick = 0; tick < 200 &&
         mirrored_turn_fixture.world.reg().get<AoeSquadOrder>(
             mirrored_turn_squad).type != AoeSquadOrderType::Idle; ++tick)
        mirrored_turn_fixture.advance_ticks(1);
    assert(mirrored_turn_fixture.world.reg().get<AoeSquadOrder>(
               mirrored_turn_squad).type == AoeSquadOrderType::Idle);
    for (const auto& member : mirrored_turn_fixture.world.reg().get<
             AoeSquadMembers>(mirrored_turn_squad).active)
        assert(mirrored_turn_fixture.world.reg().get<AoeLocomotionState>(
                   member.entity).stalled_ticks == 0);

    // RouteSplit validates the full member sweep against current static
    // geometry even when the diagnostic GlobalMotion backend later skips all
    // collision checks. A stale corridor crossing a new wall must fail closed.
    TurnFixture unsafe_turn_fixture;
    unsafe_turn_fixture.world.add_resource<AoeLogicMap>(centered_flat_map());
    const auto unsafe_turn_squad = spawn_aoe_gameplay_squad(
        unsafe_turn_fixture.world, turn_options);
    spawn_aoe_gameplay_unit_system(unsafe_turn_fixture.world);
    unsafe_turn_fixture.advance_ticks(1);
    AoeStaticObstacleDesc turn_wall;
    turn_wall.shape = AoeStaticObstacleShape::Aabb;
    turn_wall.center = {0.f, 2.f};
    turn_wall.half_extents = {6.f, .2f};
    unsafe_turn_fixture.world.resource<AoeLogicMap>()
        .add_runtime_static_obstacle(turn_wall);
    assert(request_aoe_squad_move(
        unsafe_turn_fixture.world, unsafe_turn_squad, {0.f, 10.f}));
    unsafe_turn_fixture.advance_ticks(1);
    assert(unsafe_turn_fixture.world.reg().get<
               AoeFormationRouteSplitState>(unsafe_turn_squad).status ==
           AoeFormationRouteSplitStatus::Failed);
    for (const auto& member : unsafe_turn_fixture.world.reg().get<
             AoeSquadMembers>(unsafe_turn_squad).active)
        assert(!unsafe_turn_fixture.world.reg().all_of<AoeMoveGoal>(
            member.entity));

    const auto schema1 = parse(definition_json(1));
    assert(schema1 && schema1->movement.speed == 1.f);
    assert(!schema1->lifecycle.recycle_after_death);
    auto invalid_speed = definition_json();
    invalid_speed["movement"]["speed"] = 0.0;
    assert(!parse(invalid_speed));
    auto invalid_rotation_speed = definition_json();
    invalid_rotation_speed["movement"]
        ["rotation_speed_degrees_per_second"] = 0.0;
    assert(!parse(invalid_rotation_speed));
    auto invalid_lifecycle = definition_json();
    invalid_lifecycle["lifecycle"]["death_duration_seconds"] = -0.1;
    assert(!parse(invalid_lifecycle));
    auto missing_disappear = definition_json();
    missing_disappear["presentation"]["animations"].erase("disappear");
    assert(!parse(missing_disappear));

    const AoeCollider ellipse{2.f, 1.f, 1.f};
    assert(std::abs(aoe_collider_support_radius(ellipse, {1.f, 0.f}) - 2.f) < 1e-5f);
    assert(std::abs(aoe_collider_support_radius(ellipse, {0.f, 1.f}) - 1.f) < 1e-5f);
    assert(std::abs(aoe_surface_gap({{0.f, 0.f}}, ellipse,
                                    {{5.f, 0.f}}, {1.f, 1.f, 1.f}) - 2.f) < 1e-5f);

    // Presentation interpolation stays a pure read of authoritative fixed-tick
    // state. Missing history falls back to current and alpha is clamped.
    const AoePosition interpolation_current{{4.f, 2.f}};
    const AoePositionHistory interpolation_history{{0.f, -2.f}};
    AoeGameplaySettings interpolation_settings{0.1, 8, 1};
    AoeGameplayClock interpolation_clock;
    assert(aoe_interpolated_position(interpolation_current, nullptr,
                                     interpolation_clock,
                                     interpolation_settings) ==
           interpolation_current.value);
    assert(aoe_interpolated_position(interpolation_current,
                                     &interpolation_history,
                                     interpolation_clock,
                                     interpolation_settings) ==
           interpolation_history.previous);
    interpolation_clock.accumulator = .05;
    assert(glm::length(aoe_interpolated_position(
               interpolation_current, &interpolation_history,
               interpolation_clock, interpolation_settings) -
           glm::vec2(2.f, 0.f)) < 1e-5f);
    interpolation_clock.accumulator = .2;
    assert(aoe_interpolated_position(interpolation_current,
                                     &interpolation_history,
                                     interpolation_clock,
                                     interpolation_settings) ==
           interpolation_current.value);
    const AoePositionHistory static_history{interpolation_current.value};
    interpolation_clock.accumulator = .05;
    assert(aoe_interpolated_position(interpolation_current, &static_history,
                                     interpolation_clock,
                                     interpolation_settings) ==
           interpolation_current.value);

    // Spawn and pool reuse initialize history from the new incarnation rather
    // than allowing a presentation blend from stale storage.
    Fixture history_fixture;
    AoeUnitSpawnOptions history_spawn;
    history_spawn.definition_id = "test";
    history_spawn.position = {3.f, -4.f};
    const auto history_spawned = spawn_aoe_gameplay_unit(
        history_fixture.world, history_spawn);
    assert(history_fixture.world.reg().get<AoePositionHistory>(
               history_spawned).previous == history_spawn.position);

    // A fixed tick captures the tick-start position. Catch-up keeps the start
    // of the last fixed tick, never the previous render-frame position.
    const auto interpolated_mover = history_fixture.unit({0.f, 0.f}, 50.f, 1);
    history_fixture.world.reg().emplace<Transform>(
        interpolated_mover, Transform{});
    assert(request_aoe_move(history_fixture.world, interpolated_mover,
                            {10.f, 0.f}));
    history_fixture.advance_ticks(1);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePositionHistory>(interpolated_mover).previous) < 1e-5f);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePosition>(interpolated_mover).value -
           glm::vec2(.2f, 0.f)) < 1e-5f);
    history_fixture.world.resource<AoeGameplayClock>().accumulator = .05;
    assert(glm::length(aoe_interpolated_position(
               history_fixture.world.reg().get<AoePosition>(interpolated_mover),
               history_fixture.world.reg().try_get<AoePositionHistory>(
                   interpolated_mover),
               history_fixture.world.resource<AoeGameplayClock>(),
               history_fixture.world.resource<AoeGameplaySettings>()) -
           glm::vec2(.1f, 0.f)) < 1e-5f);
    history_fixture.world.resource<AoeGameplayClock>().accumulator = 0.0;
    history_fixture.world.resource<Time>().dt = .3f;
    gld::ecs::aoe::detail::aoe_gameplay_fixed_system<
        AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
        AoeFullSquadArrivalRematchPlugin,
        AoeDefaultUnitMovementIntentPlugin,
        AoeFullLocalAvoidancePlugin,
        AoeDefaultGlobalMotionPlugin>(
        history_fixture.world);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePositionHistory>(interpolated_mover).previous -
           glm::vec2(.6f, 0.f)) < 1e-5f);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePosition>(interpolated_mover).value -
           glm::vec2(.8f, 0.f)) < 1e-5f);

    // Runtime fixed time follows raw wall time rather than the render delta
    // clamped by TimeSettings::max_delta.
    Fixture raw_time_fixture;
    auto& raw_time = raw_time_fixture.world.resource<Time>();
    raw_time.dt = .1f;
    raw_time.raw_dt = .35f;
    gld::ecs::aoe::detail::aoe_gameplay_fixed_system<
        AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
        AoeFullSquadArrivalRematchPlugin,
        AoeDefaultUnitMovementIntentPlugin,
        AoeFullLocalAvoidancePlugin,
        AoeDefaultGlobalMotionPlugin>(raw_time_fixture.world);
    const auto& raw_clock =
        raw_time_fixture.world.resource<AoeGameplayClock>();
    assert(raw_clock.tick == 3 && raw_clock.ticks_this_frame == 3);
    assert(std::abs(raw_clock.accumulator - .05) < 1e-5);

    // Headless callers that set only dt retain deterministic fixed stepping.
    Fixture dt_fallback_fixture;
    auto& fallback_time = dt_fallback_fixture.world.resource<Time>();
    fallback_time.dt = .25f;
    fallback_time.raw_dt = 0.f;
    gld::ecs::aoe::detail::aoe_gameplay_fixed_system<
        AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
        AoeFullSquadArrivalRematchPlugin,
        AoeDefaultUnitMovementIntentPlugin,
        AoeFullLocalAvoidancePlugin,
        AoeDefaultGlobalMotionPlugin>(dt_fallback_fixture.world);
    const auto& fallback_clock =
        dt_fallback_fixture.world.resource<AoeGameplayClock>();
    assert(fallback_clock.tick == 2 && fallback_clock.ticks_this_frame == 2);
    assert(std::abs(fallback_clock.accumulator - .05) < 1e-5);

    Fixture acceleration_fixture;
    acceleration_fixture.world.resource<AoeNavigationSettings>()
        .steering_max_acceleration = 4.f;
    const auto accelerating = acceleration_fixture.unit({0.f, 0.f}, 50.f, 1);
    assert(request_aoe_move(acceleration_fixture.world, accelerating,
                            {10.f, 0.f}));
    acceleration_fixture.advance_ticks(1);
    const auto& first_motion = acceleration_fixture.world.reg()
        .get<AoeLocomotionState>(accelerating);
    assert(std::abs(first_motion.actual_speed - .4f) < 1e-5f);
    assert(std::abs(first_motion.distance_travelled - .04) < 1e-5);
    const double first_distance = first_motion.distance_travelled;
    acceleration_fixture.advance_ticks(1);
    const auto& second_motion = acceleration_fixture.world.reg()
        .get<AoeLocomotionState>(accelerating);
    assert(std::abs(second_motion.actual_speed - .8f) < 1e-5f);
    assert(second_motion.distance_travelled > first_distance);

    // Grid A* drives authoritative movement around static obstacles. Removing
    // a sealing obstacle changes the map revision and resumes a retained goal.
    Fixture map_movement_fixture;
    map_movement_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& movement_map = map_movement_fixture.world.resource<AoeLogicMap>();
    AoeStaticObstacleDesc movement_wall;
    movement_wall.shape = AoeStaticObstacleShape::Aabb;
    movement_wall.center = {6.f, 6.f};
    movement_wall.half_extents = {.45f, 1.5f};
    movement_map.add_runtime_static_obstacle(movement_wall);
    const auto map_mover = map_movement_fixture.unit({2.f, 6.f}, 50.f, 1);
    assert(request_aoe_move(map_movement_fixture.world, map_mover, {10.f, 6.f}));
    map_movement_fixture.advance_ticks(1);
    const auto& detour_path = map_movement_fixture.world.reg()
        .get<AoeNavigationPath>(map_mover);
    assert(!detour_path.no_path && !detour_path.waypoints.empty());
    bool path_leaves_centerline = false;
    for (const auto waypoint : detour_path.waypoints)
        path_leaves_centerline = path_leaves_centerline ||
            std::abs(waypoint.y - 6.f) > .5f;
    assert(path_leaves_centerline);
    map_movement_fixture.advance_ticks(60);
    assert(glm::length(map_movement_fixture.world.reg()
               .get<AoePosition>(map_mover).value - glm::vec2(10.f, 6.f)) < .05f);

    Fixture sealed_map_fixture;
    sealed_map_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& sealed_map = sealed_map_fixture.world.resource<AoeLogicMap>();
    AoeStaticObstacleDesc sealed_wall;
    sealed_wall.shape = AoeStaticObstacleShape::Aabb;
    sealed_wall.center = {6.f, 6.f};
    sealed_wall.half_extents = {.45f, 6.f};
    const auto sealed_id = sealed_map.add_runtime_static_obstacle(sealed_wall);
    const auto sealed_mover = sealed_map_fixture.unit({2.f, 6.f}, 50.f, 1);
    assert(request_aoe_move(sealed_map_fixture.world, sealed_mover, {10.f, 6.f}));
    sealed_map_fixture.advance_ticks(1);
    assert(sealed_map_fixture.world.reg().get<AoeNavigationPath>(
               sealed_mover).no_path);
    assert(sealed_map_fixture.world.reg().get<AoeActionState>(
               sealed_mover).state == UnitState::Idle);
    assert(sealed_map.remove_runtime_static_obstacle(sealed_id));
    sealed_map_fixture.advance_ticks(60);
    assert(glm::length(sealed_map_fixture.world.reg()
               .get<AoePosition>(sealed_mover).value - glm::vec2(10.f, 6.f)) < .05f);

    // Dynamic units remain indexed for local/global motion, but global A*
    // deliberately ignores them even when an old caller explicitly requests
    // dynamic obstacles. The direct path therefore remains one segment and
    // performs no dynamic-index query.
    Fixture dynamic_map_fixture;
    dynamic_map_fixture.world.add_resource<AoeLogicMap>(flat_map());
    const auto dynamic_mover = dynamic_map_fixture.unit({2.f, 4.f}, 50.f, 1);
    const auto dynamic_blocker = dynamic_map_fixture.unit({5.f, 4.f}, 50.f, 2);
    (void)dynamic_blocker;
    aoe_dynamic_obstacle_index_system(dynamic_map_fixture.world);
    const auto dynamic_queries_before = dynamic_map_fixture.world
        .resource<AoeDynamicObstacleIndex>().diagnostics().queries;
    const auto static_only_result = GridAStarPathfinderLogic::find(
        dynamic_map_fixture.world,
        AoePathRequest{{2.f, 4.f}, {9.f, 4.f}, {.3f, .3f},
                       dynamic_mover, entt::null, entt::null, true});
    assert(static_only_result.status == AoePathStatus::Ready);
    assert(static_only_result.waypoints.size() == 1);
    assert(static_only_result.waypoints.back() == glm::vec2(9.f, 4.f));
    assert(dynamic_map_fixture.world.resource<AoeDynamicObstacleIndex>()
               .diagnostics().queries == dynamic_queries_before);
    assert(request_aoe_move(dynamic_map_fixture.world,
                            dynamic_mover, {9.f, 4.f}));
    dynamic_map_fixture.advance_ticks(70);
    assert(glm::length(dynamic_map_fixture.world.reg()
               .get<AoePosition>(dynamic_mover).value - glm::vec2(9.f, 4.f)) < .1f);
    assert(dynamic_map_fixture.world.resource<AoeDynamicObstacleIndex>()
               .diagnostics().units_indexed == 2);

    // Dynamic contact is handled by local/global motion without promoting the
    // route into dynamic mode or requesting a dynamic replan.
    Fixture immediate_repath_fixture;
    immediate_repath_fixture.world.add_resource<AoeLogicMap>(flat_map());
    const auto immediate_mover = immediate_repath_fixture.unit({2.f, 4.f}, 50.f, 1);
    const auto immediate_blocker = immediate_repath_fixture.unit({2.8f, 4.f}, 50.f, 2);
    (void)immediate_blocker;
    assert(request_aoe_move(immediate_repath_fixture.world,
                            immediate_mover, {8.f, 4.f}));
    bool requested_dynamic_repath = false;
    immediate_repath_fixture.world.resource<AoeNavigationSettings>()
        .blocked_repath_ticks = 1;
    for (int i = 0; i < 24; ++i) {
        immediate_repath_fixture.advance_ticks(1);
        const auto* path = immediate_repath_fixture.world.reg()
            .try_get<AoeNavigationPath>(immediate_mover);
        requested_dynamic_repath = requested_dynamic_repath ||
            (path && (path->include_dynamic_obstacles ||
                      path->dynamic_repath_requested ||
                      path->dynamic_repath_failed));
    }
    assert(!requested_dynamic_repath);
    assert(immediate_repath_fixture.world.resource<AoeGameplayDiagnostics>()
               .steering_imminent_solves > 0);
    immediate_repath_fixture.advance_ticks(70);
    assert(glm::length(immediate_repath_fixture.world.reg()
               .get<AoePosition>(immediate_mover).value -
           glm::vec2(8.f, 4.f)) < .1f);

    Fixture cached_steering_fixture;
    cached_steering_fixture.world.add_resource<AoeLogicMap>(flat_map());
    cached_steering_fixture.world.resource<AoeLocalAvoidanceSettings>()
        .imminent_collision_seconds = 0.f;
    cached_steering_fixture.world.resource<AoeNavigationSettings>()
        .steering_max_acceleration = 0.f;
    const auto cached_follower = cached_steering_fixture.unit(
        {2.f, 4.f}, 50.f, 1);
    const auto cached_leader = cached_steering_fixture.unit(
        {3.f, 4.f}, 50.f, 1);
    assert(request_aoe_move(cached_steering_fixture.world,
                            cached_follower, {10.f, 4.f}));
    assert(request_aoe_move(cached_steering_fixture.world,
                            cached_leader, {11.f, 4.f}));
    const auto steering_entity_count = cached_steering_fixture.world.reg()
        .storage<entt::entity>().free_list();
    cached_steering_fixture.advance_ticks(6);
    assert(cached_steering_fixture.world.reg()
               .storage<entt::entity>().free_list() == steering_entity_count);
    assert(cached_steering_fixture.world.resource<AoeGameplayDiagnostics>()
               .steering_cached_solves > 0);

    // The full static plugin owns the local intent and creates its private
    // settings, scratch and per-unit cache without a runtime registry lookup.
    Fixture pipeline_fixture;
    pipeline_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& pipeline_navigation =
        pipeline_fixture.world.resource<AoeNavigationSettings>();
    pipeline_navigation.unit_flow_enabled = false;
    const auto pipeline_unit = pipeline_fixture.unit({2.f, 2.f}, 50.f, 1);
    assert(request_aoe_move(pipeline_fixture.world,
                            pipeline_unit, {10.f, 2.f}));
    pipeline_fixture.advance_ticks(1);
    const auto& pipeline_request = pipeline_fixture.world.reg()
        .get<AoePathMotionRequest>(pipeline_unit);
    const auto& pipeline_intent = pipeline_fixture.world.reg()
        .get<AoeMovementIntent>(pipeline_unit);
    const auto& pipeline_decision = pipeline_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(pipeline_unit);
    assert(glm::length(pipeline_request.velocity - glm::vec2(2.f, 0.f)) <
           1e-5f);
    assert(glm::length(pipeline_intent.velocity - pipeline_request.velocity) <
           1e-5f);
    assert(glm::length(pipeline_decision.velocity -
                       pipeline_intent.velocity) < 1e-5f);
    assert(glm::length(pipeline_fixture.world.reg()
               .get<AoeLocomotionState>(pipeline_unit).velocity -
           pipeline_decision.velocity) < 1e-5f);

    assert(pipeline_fixture.world.reg().all_of<AoeLocalAvoidanceState>(
        pipeline_unit));
    assert(pipeline_fixture.world.try_resource<AoeLocalAvoidanceSettings>());
    assert(pipeline_fixture.world.try_resource<AoeLocalAvoidanceScratch>());

    // The pass-through plugin is a distinct compile-time pipeline. It creates
    // no full-plugin resources or per-unit cache, and emits a neutral raw
    // intent for global planning and final collision safety.
    Fixture<AoePassThroughLocalAvoidancePlugin> local_bypass_fixture;
    local_bypass_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& local_bypass_navigation =
        local_bypass_fixture.world.resource<AoeNavigationSettings>();
    local_bypass_navigation.unit_flow_enabled = false;
    const auto local_bypass_unit = local_bypass_fixture.unit(
        {2.f, 2.f}, 50.f, 1);
    assert(request_aoe_move(local_bypass_fixture.world,
                            local_bypass_unit, {10.f, 2.f}));
    local_bypass_fixture.advance_ticks(1);
    const auto& bypass_request = local_bypass_fixture.world.reg()
        .get<AoePathMotionRequest>(local_bypass_unit);
    const auto& bypass_intent = local_bypass_fixture.world.reg()
        .get<AoeMovementIntent>(local_bypass_unit);
    const auto& bypass_decision = local_bypass_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(local_bypass_unit);
    assert(glm::length(bypass_intent.velocity - bypass_request.velocity) <
           1e-5f);
    assert(glm::length(bypass_intent.raw_path_velocity -
                       bypass_request.velocity) < 1e-5f);
    assert(bypass_intent.neighbor_count == 0 &&
           bypass_intent.avoidance_side == 0 &&
           !bypass_intent.threatened && !bypass_intent.locally_infeasible);
    assert(glm::length(bypass_decision.velocity - bypass_intent.velocity) <
           1e-5f);
    assert(!local_bypass_fixture.world.reg().all_of<
        AoeLocalAvoidanceState>(local_bypass_unit));
    assert(!local_bypass_fixture.world.try_resource<
        AoeLocalAvoidanceSettings>());
    assert(!local_bypass_fixture.world.try_resource<
        AoeLocalAvoidanceScratch>());

    // Global pass-through is a diagnostic route-playback pipeline. It follows
    // raw path velocity, applies the normal acceleration cap, and leaves the
    // safety index empty so neither static nor dynamic collision clips it.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoePassThroughGlobalMotionPlugin> motion_floor_fixture;
    motion_floor_fixture.world.add_resource<AoeLogicMap>(flat_map());
    motion_floor_fixture.world.resource<AoeNavigationSettings>()
        .steering_max_acceleration = 4.f;
    const auto motion_floor_unit = motion_floor_fixture.unit(
        {2.f, 2.f}, 50.f, 1);
    assert(request_aoe_move(motion_floor_fixture.world,
                            motion_floor_unit, {10.f, 2.f}));
    motion_floor_fixture.advance_ticks(1);
    const auto& motion_floor_intent = motion_floor_fixture.world.reg()
        .get<AoeMovementIntent>(motion_floor_unit);
    const auto& motion_floor_decision = motion_floor_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(motion_floor_unit);
    const auto& motion_floor_index =
        motion_floor_fixture.world.resource<AoeUnitFlowIndex>();
    assert(std::abs(glm::length(motion_floor_intent.velocity) - 2.f) < 1e-5f);
    assert(std::abs(glm::length(motion_floor_decision.velocity) - .4f) < 1e-5f);
    assert(motion_floor_decision.valid &&
           motion_floor_decision.produced_tick == 1 &&
           motion_floor_decision.mode == AoeGlobalMotionMode::Clear &&
           motion_floor_decision.reason == AoeMotionDecisionReason::None);
    assert(motion_floor_index.records.empty() &&
           motion_floor_index.candidates.empty() &&
           motion_floor_index.selected.empty());
    assert(!motion_floor_fixture.world.reg().all_of<AoeGlobalMotionState>(
        motion_floor_unit));
    const auto& motion_floor_planner = motion_floor_fixture.world.resource<
        AoeGlobalMotionPlannerDiagnostics>();
    assert(motion_floor_planner.requested_backend == "none" &&
           motion_floor_planner.active_backend == "pass_through");

    // Stale intents stay invalid, while a current non-finite intent produces a
    // safe zero decision rather than leaking NaN into movement fallback.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoePassThroughGlobalMotionPlugin> invalid_motion_fixture;
    const auto invalid_motion_unit = invalid_motion_fixture.unit(
        {2.f, 2.f}, 50.f, 1);
    invalid_motion_fixture.world.reg().emplace<AoeMovementIntent>(
        invalid_motion_unit,
        AoeMovementIntent{.velocity = {1.f, 0.f},
            .produced_tick = 6, .valid = true});
    AoePassThroughGlobalMotionPlugin::fixed_tick(
        invalid_motion_fixture.world, 7);
    const auto* stale_decision = invalid_motion_fixture.world.reg()
        .try_get<AoeGlobalMotionDecision>(invalid_motion_unit);
    assert(!stale_decision || !stale_decision->valid);
    auto& invalid_intent = invalid_motion_fixture.world.reg()
        .get<AoeMovementIntent>(invalid_motion_unit);
    invalid_intent.raw_path_velocity = {
        std::numeric_limits<float>::quiet_NaN(), 1.f};
    invalid_intent.produced_tick = 7;
    AoePassThroughGlobalMotionPlugin::fixed_tick(
        invalid_motion_fixture.world, 7);
    const auto& invalid_decision = invalid_motion_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(invalid_motion_unit);
    assert(invalid_decision.valid && invalid_decision.produced_tick == 7 &&
           glm::length(invalid_decision.velocity) == 0.f &&
           invalid_decision.stop_reason == AoeMotionStopReason::Unknown);
    invalid_intent.velocity = {2.f, 0.f};
    invalid_intent.raw_path_velocity = {0.f, 2.f};
    invalid_intent.produced_tick = 8;
    AoePassThroughGlobalMotionPlugin::fixed_tick(
        invalid_motion_fixture.world, 8);
    const auto& raw_route_decision = invalid_motion_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(invalid_motion_unit);
    assert(glm::length(raw_route_decision.velocity - glm::vec2{0.f, 2.f}) <
           1e-5f);

    // The diagnostic pass-through backend is not clipped even when its raw
    // route enters a static obstacle.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoePassThroughGlobalMotionPlugin,
            AoeFullFormationPlugin,
            AoeFullSquadEngagementPlugin,
            AoeFullSquadArrivalRematchPlugin,
            AoeDirectUnitPathfinderPlugin> safety_fixture;
    safety_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& safety_navigation =
        safety_fixture.world.resource<AoeNavigationSettings>();
    safety_navigation.unit_pathfinder_id = "direct";
    safety_navigation.unit_flow_enabled = false;
    AoeStaticObstacleDesc safety_wall;
    safety_wall.shape = AoeStaticObstacleShape::Aabb;
    safety_wall.center = {2.35f, 2.f};
    safety_wall.half_extents = {.05f, 1.f};
    safety_fixture.world.resource<AoeLogicMap>()
        .add_runtime_static_obstacle(safety_wall);
    const auto safety_unit = safety_fixture.unit({2.f, 2.f}, 50.f, 1);
    assert(request_aoe_move(safety_fixture.world,
                            safety_unit, {10.f, 2.f}));
    safety_fixture.advance_ticks(1);
    const auto& safety_decision = safety_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(safety_unit);
    const auto& safety_locomotion = safety_fixture.world.reg()
        .get<AoeLocomotionState>(safety_unit);
    assert(safety_decision.static_safe_fraction == 1.f &&
           safety_decision.dynamic_safe_fraction == 1.f &&
           safety_decision.safe_fraction == 1.f);
    assert(safety_decision.velocity.x > 0.f &&
           std::abs(safety_decision.velocity.y) < 1e-5f);
    assert(safety_locomotion.velocity.x >= 0.f &&
           std::abs(safety_locomotion.velocity.y) < 1e-5f);
    assert(safety_fixture.world.reg().get<AoePosition>(
               safety_unit).value.x > 2.1f);

    // Unit-level flow coordination consumes local intent. Same-direction,
    // same-speed traffic stays unchanged when no collision is predicted,
    // while head-on units receive complementary right-hand directions.
    Fixture unit_flow_fixture;
    unit_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& unit_flow_settings =
        unit_flow_fixture.world.resource<AoeNavigationSettings>();
    unit_flow_settings.unit_flow_enabled = true;
    const auto flow_leader = unit_flow_fixture.unit({4.7f, 3.f}, 50.f, 1);
    const auto flow_follower = unit_flow_fixture.unit({4.f, 3.f}, 50.f, 1);
    assert(request_aoe_move(unit_flow_fixture.world, flow_leader, {12.f, 3.f}));
    assert(request_aoe_move(unit_flow_fixture.world, flow_follower, {12.f, 3.f}));
    unit_flow_fixture.advance_ticks(1);
    const auto& follower_intent = unit_flow_fixture.world.reg()
        .get<AoeMovementIntent>(flow_follower);
    const auto& follower_flow = unit_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(flow_follower);
    assert(follower_flow.mode == AoeGlobalMotionMode::Clear);
    assert(glm::length(follower_flow.velocity - follower_intent.velocity) < 1e-5f);
    assert(!unit_flow_fixture.world.reg()
                .get<AoeNavigationPath>(flow_follower).no_path);

    Fixture overlap_flow_fixture;
    overlap_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    overlap_flow_fixture.world.resource<AoeNavigationSettings>()
        .unit_flow_enabled = true;
    const auto overlap_front =
        overlap_flow_fixture.unit({4.3f, 3.f}, 50.f, 1);
    const auto overlap_back =
        overlap_flow_fixture.unit({4.f, 3.f}, 50.f, 1);
    assert(request_aoe_move(overlap_flow_fixture.world,
                            overlap_front, {12.f, 3.f}));
    assert(request_aoe_move(overlap_flow_fixture.world,
                            overlap_back, {12.f, 3.f}));
    overlap_flow_fixture.advance_ticks(1);
    const auto& overlap_front_decision = overlap_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(overlap_front);
    const auto& overlap_back_decision = overlap_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(overlap_back);
    assert(glm::dot(glm::vec2(.3f, 0.f),
               overlap_front_decision.velocity -
                   overlap_back_decision.velocity) >= -1e-5f);
    assert(overlap_front_decision.dynamic_safe_fraction > 0.f);
    assert(overlap_back_decision.dynamic_safe_fraction > 0.f);

    // Exact/shallow contact must participate in the global projection too.
    // Otherwise the final safety stage clips an approaching pair to zero on
    // every tick even though both units have valid motion intents.
    Fixture<AoePassThroughLocalAvoidancePlugin> contact_flow_fixture;
    contact_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& contact_settings =
        contact_flow_fixture.world.resource<AoeNavigationSettings>();
    contact_settings.unit_flow_enabled = true;
    const auto contact_left =
        contact_flow_fixture.unit({4.f, 3.f}, 50.f, 1);
    const auto contact_right =
        contact_flow_fixture.unit({4.4f, 3.f}, 50.f, 2);
    assert(request_aoe_move(contact_flow_fixture.world,
                            contact_left, {12.f, 3.f}));
    assert(request_aoe_move(contact_flow_fixture.world,
                            contact_right, {1.f, 3.f}));
    contact_flow_fixture.advance_ticks(1);
    const auto& contact_left_decision = contact_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(contact_left);
    const auto& contact_right_decision = contact_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(contact_right);
    assert(contact_left_decision.dynamic_safe_fraction > 0.f);
    assert(contact_right_decision.dynamic_safe_fraction > 0.f);
    assert(glm::dot(glm::vec2(-.4f, 0.f),
               contact_left_decision.velocity -
                   contact_right_decision.velocity) >= -1e-5f);

    Fixture head_on_flow_fixture;
    head_on_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    head_on_flow_fixture.world.resource<AoeNavigationSettings>()
        .unit_flow_enabled = true;
    const auto eastbound = head_on_flow_fixture.unit({4.f, 6.f}, 50.f, 1);
    const auto westbound = head_on_flow_fixture.unit({6.f, 6.f}, 50.f, 2);
    assert(request_aoe_move(head_on_flow_fixture.world, eastbound, {12.f, 6.f}));
    assert(request_aoe_move(head_on_flow_fixture.world, westbound, {1.f, 6.f}));
    head_on_flow_fixture.advance_ticks(1);
    const auto& east_flow = head_on_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(eastbound);
    const auto& west_flow = head_on_flow_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(westbound);
    assert(east_flow.mode == AoeGlobalMotionMode::PassingRight);
    assert(west_flow.mode == AoeGlobalMotionMode::PassingRight);
    assert(east_flow.velocity.y < 0.f);
    assert(west_flow.velocity.y > 0.f);
    assert(!head_on_flow_fixture.world.reg()
                .get<AoeNavigationPath>(eastbound).no_path);
    assert(!head_on_flow_fixture.world.reg()
                .get<AoeNavigationPath>(westbound).no_path);
    assert(head_on_flow_fixture.world.resource<AoeGameplayDiagnostics>()
               .flow_neighbor_checks > 0);

    // A one-unit-wide edge corridor cannot provide complementary side lanes.
    // The lower-priority unit escalates from an aged yield to a bounded
    // backwards maneuver instead of turning the traffic wait into no_path.
    Fixture backing_flow_fixture;
    backing_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    AoeStaticObstacleDesc corridor_wall;
    corridor_wall.shape = AoeStaticObstacleShape::Aabb;
    corridor_wall.center = {10.f, 1.1f};
    corridor_wall.half_extents = {10.f, .25f};
    backing_flow_fixture.world.resource<AoeLogicMap>()
        .add_runtime_static_obstacle(corridor_wall);
    auto& backing_settings =
        backing_flow_fixture.world.resource<AoeNavigationSettings>();
    backing_settings.unit_flow_enabled = true;
    backing_settings.unit_flow_backing_threshold_ticks = 1;
    const auto backing_priority =
        backing_flow_fixture.unit({4.f, .5f}, 50.f, 1);
    const auto backing_yielder =
        backing_flow_fixture.unit({6.f, .5f}, 50.f, 2);
    assert(request_aoe_move(backing_flow_fixture.world,
                            backing_priority, {12.f, .5f}));
    assert(request_aoe_move(backing_flow_fixture.world,
                            backing_yielder, {1.f, .5f}));
    backing_flow_fixture.world.reg().emplace<AoeGlobalMotionState>(
        backing_yielder, AoeGlobalMotionState{
            .mode = AoeGlobalMotionMode::Yielding, .wait_ticks = 1});
    // wait_ticks is advanced from the previous tick's authoritative movement
    // result before the planner overwrites its decision.  Seed that complete
    // history here instead of only seeding the derived counter.
    backing_flow_fixture.world.reg().emplace<AoeGlobalMotionDecision>(
        backing_yielder, AoeGlobalMotionDecision{
            .mode = AoeGlobalMotionMode::Yielding,
            .produced_tick = 0,
            .valid = true});
    const int backing_facing = backing_flow_fixture.world.reg()
        .get<AoeFacing>(backing_yielder).direction;
    backing_flow_fixture.advance_ticks(1);
    assert(backing_flow_fixture.world.reg().get<AoeGlobalMotionDecision>(
               backing_yielder).mode == AoeGlobalMotionMode::Backing);
    assert(!backing_flow_fixture.world.reg().get<AoeNavigationPath>(
                backing_yielder).no_path);
    assert(backing_flow_fixture.world.reg().get<AoePosition>(
               backing_yielder).value.x > 6.f);
    assert(backing_flow_fixture.world.reg().get<AoeFacing>(
               backing_yielder).direction == backing_facing);

    // The default strategy ignores self/allies/terminal units, compares
    // collider surface gaps, and returns a stable incarnation-aware target.
    Fixture acquisition_fixture;
    const auto seeker = acquisition_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto ally = acquisition_fixture.unit({1.f, 0.f}, 50.f, 1);
    const auto center_near = acquisition_fixture.unit({4.f, 0.f}, 50.f, 2);
    const auto surface_near = acquisition_fixture.unit({4.5f, 0.f}, 50.f, 2);
    acquisition_fixture.world.reg().get<AoeCollider>(surface_near).radius_x = 2.f;
    auto acquired = dispatch_aoe_target(
        AoeTargetAcquisitionType::NearestEnemy, acquisition_fixture.world,
        {seeker, {0.f, 0.f}, 10.f, 1});
    assert(acquired && acquired->entity == surface_near);
    acquisition_fixture.world.reg().get<AoeHealth>(surface_near).current = 0.f;
    acquired = dispatch_aoe_target(
        AoeTargetAcquisitionType::NearestEnemy, acquisition_fixture.world,
        {seeker, {0.f, 0.f}, 10.f, 1});
    assert(acquired && acquired->entity == center_near);
    assert(acquired->entity != ally);
    acquired = dispatch_aoe_target(
        AoeTargetAcquisitionType::NearestEnemy, acquisition_fixture.world,
        {seeker, {0.f, 0.f}, .1f, 1});
    assert(!acquired);

    Fixture tie_fixture;
    const auto tie_seeker = tie_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto first_tie = tie_fixture.unit({3.f, 0.f}, 50.f, 2);
    tie_fixture.unit({3.f, 0.f}, 50.f, 2);
    acquired = dispatch_aoe_target(
        AoeTargetAcquisitionType::NearestEnemy, tie_fixture.world,
        {tie_seeker, {0.f, 0.f}, 6.f, 1});
    assert(acquired && acquired->entity == first_tie);

    // Squad spawn resolves asynchronous unit placeholders, lays out a stable
    // collision-aware square, and executes commands queued while pending.
    Fixture squad_fixture;
    AoeSquadSpawnOptions squad_options;
    squad_options.composition = {{"test", 4, 1}};
    squad_options.center = {0.f, 0.f};
    squad_options.forward = {1.f, 0.f};
    squad_options.team_id = 1;
    const auto squad = spawn_aoe_gameplay_squad(
        squad_fixture.world, squad_options);
    assert(squad != entt::null);
    assert(squad_fixture.world.reg().get<AoeSquadSpawnState>(squad).status ==
           AoeSquadSpawnStatus::Pending);
    assert(request_aoe_squad_move(squad_fixture.world, squad, {3.f, 0.f}));
    spawn_aoe_gameplay_unit_system(squad_fixture.world);
    squad_fixture.advance_ticks(1);
    const auto& squad_spawn = squad_fixture.world.reg()
        .get<AoeSquadSpawnState>(squad);
    assert(squad_spawn.status == AoeSquadSpawnStatus::Ready);
    assert(squad_spawn.succeeded == 4 && squad_spawn.failed == 0);
    const auto& squad_members = squad_fixture.world.reg()
        .get<AoeSquadMembers>(squad);
    assert(squad_members.active.size() == 4);
    assert(squad_fixture.world.reg().get<AoeSquadLayoutState>(
               squad).layout.slots.size() == 4);
    assert(squad_fixture.world.reg().get<AoeSquadState>(squad).movement_speed == 2.f);
    assert(squad_fixture.world.reg().get<AoePosition>(squad).value.x > 0.f);
    for (const auto& member : squad_members.active) {
        assert(squad_fixture.world.reg().all_of<AoeSquadMember,
            AoeSquadMoveSpeedLimit>(member.entity));
        assert(squad_fixture.world.reg().get<AoeTeam>(member.entity).id == 1);
    }

    Fixture formation_history_fixture;
    AoeSquadSpawnOptions formation_history_options;
    formation_history_options.composition = {{"test", 4, 1}};
    formation_history_options.center = {5.f, -3.f};
    const auto formation_history_squad = spawn_aoe_gameplay_squad(
        formation_history_fixture.world, formation_history_options);
    spawn_aoe_gameplay_unit_system(formation_history_fixture.world);
    formation_history_fixture.advance_ticks(1);
    const auto& formation_history_members = formation_history_fixture.world.reg()
        .get<AoeSquadMembers>(formation_history_squad);
    for (const auto& member : formation_history_members.active)
        assert(formation_history_fixture.world.reg().get<AoePositionHistory>(
                   member.entity).previous ==
               formation_history_fixture.world.reg().get<AoePosition>(
                   member.entity).value);

    const auto slow_member = squad_members.active.front().entity;
    squad_fixture.world.reg().get<AoeMovement>(slow_member).speed = 1.f;
    const float center_before = squad_fixture.world.reg().get<AoePosition>(squad).value.x;
    assert(request_aoe_squad_move(squad_fixture.world, squad, {4.f, 0.f}));
    squad_fixture.advance_ticks(1);
    for (int i = 0; i < 20 &&
         squad_fixture.world.reg().get<AoePosition>(squad).value.x <=
             center_before + 1e-5f; ++i)
        squad_fixture.advance_ticks(1);
    assert(squad_fixture.world.reg().get<AoePosition>(squad).value.x >
           center_before);
    assert(squad_fixture.world.reg().get<AoeSquadState>(squad).movement_speed == 1.f);
    for (const auto& member : squad_members.active) {
        const auto entity = member.entity;
        const auto& locomotion = squad_fixture.world.reg()
            .get<AoeLocomotionState>(entity);
        assert(std::abs(locomotion.effective_max_speed - 1.f) < 1e-5f);
        // The faster members retain their base capability while obeying the
        // squad's slowest-member cap.
        if (entity != slow_member)
            assert(std::abs(squad_fixture.world.reg()
                                .get<AoeMovement>(entity).speed - 2.f) < 1e-5f);
    }

    const auto detached = squad_fixture.world.reg()
        .get<AoeSquadMembers>(squad).active.back().entity;
    assert(request_aoe_move(squad_fixture.world, detached, {-2.f, 1.f}));
    squad_fixture.advance_ticks(1);
    assert(!squad_fixture.world.reg().all_of<AoeSquadMember>(detached));
    assert(!squad_fixture.world.reg().all_of<AoeSquadMoveSpeedLimit>(detached));
    assert(std::abs(squad_fixture.world.reg()
                        .get<AoeLocomotionState>(detached).effective_max_speed -
                    2.f) < 1e-5f);
    assert(squad_fixture.world.reg().get<AoeSquadMembers>(squad).active.size() == 3);

    // Attack-move target acquisition runs every fixed tick. Finding no target
    // must not reset formation locomotion and force every member to repeat the
    // first acceleration step forever.
    Fixture attack_move_speed_fixture;
    attack_move_speed_fixture.world.resource<AoeNavigationSettings>()
        .steering_max_acceleration = 4.f;
    AoeSquadSpawnOptions attack_move_speed_options;
    attack_move_speed_options.composition = {{"test", 4, 1}};
    attack_move_speed_options.center = {0.f, 0.f};
    attack_move_speed_options.forward = {1.f, 0.f};
    const auto attack_move_speed_squad = spawn_aoe_gameplay_squad(
        attack_move_speed_fixture.world, attack_move_speed_options);
    assert(request_aoe_squad_attack_move(attack_move_speed_fixture.world,
                                         attack_move_speed_squad,
                                         {10.f, 0.f}));
    spawn_aoe_gameplay_unit_system(attack_move_speed_fixture.world);
    attack_move_speed_fixture.advance_ticks(6);
    const auto& accelerating_members = attack_move_speed_fixture.world.reg()
        .get<AoeSquadMembers>(attack_move_speed_squad);
    for (const auto& member : accelerating_members.active) {
        const auto& locomotion = attack_move_speed_fixture.world.reg()
            .get<AoeLocomotionState>(member.entity);
        assert(std::abs(locomotion.effective_max_speed - 2.f) < 1e-5f);
        assert(locomotion.actual_speed > .4f + 1e-5f);
    }

    // A squad guide routes the virtual anchor around static geometry while
    // every member independently follows its moving formation slot.
    Fixture mapped_squad_fixture;
    mapped_squad_fixture.world.add_resource<AoeLogicMap>(flat_map());
    AoeSquadSpawnOptions mapped_options;
    mapped_options.composition = {{"test", 4, 1}};
    mapped_options.center = {3.f, 6.f};
    mapped_options.forward = {1.f, 0.f};
    mapped_options.team_id = 1;
    const auto mapped_squad = spawn_aoe_gameplay_squad(
        mapped_squad_fixture.world, mapped_options);
    spawn_aoe_gameplay_unit_system(mapped_squad_fixture.world);
    mapped_squad_fixture.advance_ticks(1);
    AoeStaticObstacleDesc squad_wall;
    squad_wall.shape = AoeStaticObstacleShape::Aabb;
    squad_wall.center = {9.f, 6.f};
    squad_wall.half_extents = {.45f, 2.5f};
    mapped_squad_fixture.world.resource<AoeLogicMap>()
        .add_runtime_static_obstacle(squad_wall);
    assert(request_aoe_squad_move(
        mapped_squad_fixture.world, mapped_squad, {16.f, 6.f}));
    mapped_squad_fixture.advance_ticks(1);
    const auto& mapped_guide = mapped_squad_fixture.world.reg()
        .get<AoeNavigationPath>(mapped_squad);
    assert(!mapped_guide.no_path && !mapped_guide.waypoints.empty());
    bool guide_detours = false;
    for (const auto waypoint : mapped_guide.waypoints)
        guide_detours = guide_detours || std::abs(waypoint.y - 6.f) > 2.f;
    assert(guide_detours);
    for (const auto& member : mapped_squad_fixture.world.reg()
             .get<AoeSquadMembers>(mapped_squad).active)
        assert(mapped_squad_fixture.world.reg()
                   .all_of<AoeNavigationPath>(member.entity));
    mapped_squad_fixture.advance_ticks(160);
    assert(mapped_squad_fixture.world.reg().get<AoeSquadState>(mapped_squad)
               .phase == AoeSquadPhase::Idle);
    assert(glm::length(mapped_squad_fixture.world.reg()
               .get<AoePosition>(mapped_squad).value - glm::vec2(16.f, 6.f)) < .05f);

    // One accepted squad movement command owns one center-path query. Members
    // consume provisional direct waypoints and never multiply that query count.
    // Exhaustion, combat pause/resume and NoPath do not retry; a new command or
    // a new static-map revision each creates exactly one new query.
    using CountingUnitPathfinder = AoeStaticPathfinderPlugin<
        CountingPathfinderLogic, AoeUnitPathfinderPhase>;
    using CountingSquadPathfinder = AoeStaticPathfinderPlugin<
        CountingPathfinderLogic, AoeSquadPathfinderPhase>;
    Fixture<AoeFullLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin,
            AoeFullFormationPlugin,
            AoeFullSquadEngagementPlugin,
            AoeFullSquadArrivalRematchPlugin,
            CountingUnitPathfinder,
            CountingSquadPathfinder> single_query_fixture;
    single_query_fixture.world.add_resource<AoeLogicMap>(flat_map(100, 20));
    single_query_fixture.world.resource_or_add<CountingPathfinderState>();
    AoeSquadSpawnOptions single_query_options;
    single_query_options.composition = {{"test", 8, 1}};
    single_query_options.center = {5.f, 10.f};
    single_query_options.forward = {1.f, 0.f};
    single_query_options.team_id = 1;
    const auto single_query_squad = spawn_aoe_gameplay_squad(
        single_query_fixture.world, single_query_options);
    spawn_aoe_gameplay_unit_system(single_query_fixture.world);
    single_query_fixture.advance_ticks(1);
    assert(single_query_fixture.world.resource<CountingPathfinderState>().calls == 0);

    assert(request_aoe_squad_attack_move(
        single_query_fixture.world, single_query_squad, {90.f, 10.f}));
    single_query_fixture.advance_ticks(1);
    const auto query_calls = [&] {
        return single_query_fixture.world
            .resource<CountingPathfinderState>().calls;
    };
    assert(query_calls() == 1);
    const auto& query_members = single_query_fixture.world.reg()
        .get<AoeSquadMembers>(single_query_squad);
    for (const auto& member : query_members.active) {
        const auto& member_path = single_query_fixture.world.reg()
            .get<AoeNavigationPath>(member.entity);
        assert(member_path.waypoints.size() == 1);
        assert(member_path.request_sequence == 0);
    }
    auto& exhausted_guide = single_query_fixture.world.reg()
        .get<AoeNavigationPath>(single_query_squad);
    exhausted_guide.current = exhausted_guide.waypoints.size();
    single_query_fixture.advance_ticks(3);
    assert(query_calls() == 1);

    // An identical destination is still a new accepted command revision.
    assert(request_aoe_squad_attack_move(
        single_query_fixture.world, single_query_squad, {90.f, 10.f}));
    single_query_fixture.advance_ticks(1);
    assert(query_calls() == 2);
    AoeStaticObstacleDesc revision_probe;
    revision_probe.shape = AoeStaticObstacleShape::Circle;
    revision_probe.center = {50.f, 2.f};
    revision_probe.radius = .25f;
    single_query_fixture.world.resource<AoeLogicMap>()
        .add_runtime_static_obstacle(revision_probe);
    single_query_fixture.advance_ticks(3);
    assert(query_calls() == 3);

    single_query_fixture.world.resource<CountingPathfinderState>().fail = true;
    assert(request_aoe_squad_attack_move(
        single_query_fixture.world, single_query_squad, {80.f, 10.f}));
    for (int i = 0; i < 100 && query_calls() == 3; ++i)
        single_query_fixture.advance_ticks(1);
    assert(query_calls() == 4);
    assert(single_query_fixture.world.reg()
        .get<AoeNavigationPath>(single_query_squad).no_path);
    single_query_fixture.advance_ticks(8);
    assert(query_calls() == 4);

    // Formation members approach combat targets through direct waypoints.
    single_query_fixture.world.resource<CountingPathfinderState>().fail = false;
    assert(request_aoe_squad_attack_move(
        single_query_fixture.world, single_query_squad, {90.f, 10.f}));
    for (int i = 0; i < 100 && query_calls() == 4; ++i)
        single_query_fixture.advance_ticks(1);
    assert(query_calls() == 5);
    const auto combat_probe = single_query_fixture.unit(
        single_query_fixture.world.reg().get<AoePosition>(
            single_query_squad).value + glm::vec2{5.f, 0.f}, 500.f, 2);
    single_query_fixture.advance_ticks(2);
    assert(query_calls() == 5);
    bool found_direct_approach = false;
    for (const auto& member : query_members.active) {
        if (!single_query_fixture.world.reg().all_of<
                AoeAttackOrder, AoeMoveGoal, AoeNavigationPath>(member.entity))
            continue;
        found_direct_approach = true;
        assert(single_query_fixture.world.reg()
            .get<AoeNavigationPath>(member.entity).request_sequence == 0);
    }
    assert(found_direct_approach);
    single_query_fixture.world.reg().get<AoeHealth>(combat_probe).current = 0.f;
    single_query_fixture.advance_ticks(5);
    assert(query_calls() == 5);

    // A member explicitly detached by an individual command returns to normal
    // unit pathfinding and therefore contributes one query of its own.
    const auto detached_query_member = query_members.active.back().entity;
    assert(request_aoe_move(single_query_fixture.world,
                            detached_query_member, {2.f, 2.f}));
    single_query_fixture.advance_ticks(1);
    assert(!single_query_fixture.world.reg()
        .all_of<AoeSquadMember>(detached_query_member));
    assert(query_calls() == 6);

    // Attack Move performs one role-preserving nearest-slot rematch when the
    // anchor reaches its destination. Members already standing in each
    // other's same-priority slots finish without crossing through one another.
    Fixture arrival_reflow_fixture;
    AoeSquadSpawnOptions arrival_reflow_options;
    arrival_reflow_options.composition = {{"test", 4, 1}};
    arrival_reflow_options.center = {5.f, 5.f};
    arrival_reflow_options.forward = {1.f, 0.f};
    arrival_reflow_options.team_id = 1;
    const auto arrival_reflow_squad = spawn_aoe_gameplay_squad(
        arrival_reflow_fixture.world, arrival_reflow_options);
    spawn_aoe_gameplay_unit_system(arrival_reflow_fixture.world);
    arrival_reflow_fixture.advance_ticks(1);
    auto& arrival_formation = arrival_reflow_fixture.world.reg()
        .get<AoeSquadFormation>(arrival_reflow_squad);
    auto& arrival_slots = arrival_reflow_fixture.world.reg()
        .get<AoeSquadLayoutState>(arrival_reflow_squad).layout.slots;
    assert(arrival_slots.size() == 4);
    const auto slot_world = [&](const AoeFormationSlot& slot) {
        const auto center = arrival_reflow_fixture.world.reg()
            .get<AoePosition>(arrival_reflow_squad).value;
        const glm::vec2 forward = glm::normalize(arrival_formation.forward);
        const glm::vec2 right{forward.y, -forward.x};
        return center + right * slot.local_offset.x +
               forward * slot.local_offset.y;
    };
    const auto first_unit = arrival_slots[0].unit;
    const auto second_unit = arrival_slots[1].unit;
    arrival_reflow_fixture.world.reg().get<AoePosition>(
        first_unit.entity).value = slot_world(arrival_slots[1]);
    arrival_reflow_fixture.world.reg().get<AoePosition>(
        second_unit.entity).value = slot_world(arrival_slots[0]);
    auto& arrival_order = arrival_reflow_fixture.world.reg()
        .get<AoeSquadOrder>(arrival_reflow_squad);
    arrival_order = {AoeSquadOrderType::AttackMove,
                     arrival_reflow_fixture.world.reg()
                         .get<AoePosition>(arrival_reflow_squad).value, {}};
    arrival_reflow_fixture.world.reg().get<AoeSquadState>(
        arrival_reflow_squad).phase = AoeSquadPhase::Moving;
    arrival_formation.arrival_reflow_done = false;
    // Formation publishes on the first tick, then consumes the full plugin's
    // result on the next tick before completing the order.
    arrival_reflow_fixture.advance_ticks(2);
    assert(arrival_formation.arrival_reflow_done);
    assert(arrival_slots[0].unit.entity == second_unit.entity);
    assert(arrival_slots[1].unit.entity == first_unit.entity);
    arrival_reflow_fixture.advance_ticks(20);
    assert(arrival_reflow_fixture.world.reg().get<AoeSquadState>(
               arrival_reflow_squad).phase == AoeSquadPhase::Idle);
    assert(arrival_reflow_fixture.world.reg().get<AoeSquadOrder>(
               arrival_reflow_squad).type == AoeSquadOrderType::Idle);

    // A globally nearer slot in another priority group is never assigned.
    Fixture role_reflow_fixture;
    const auto role_reflow_squad = spawn_aoe_gameplay_squad(
        role_reflow_fixture.world, arrival_reflow_options);
    spawn_aoe_gameplay_unit_system(role_reflow_fixture.world);
    role_reflow_fixture.advance_ticks(1);
    auto& role_formation = role_reflow_fixture.world.reg()
        .get<AoeSquadFormation>(role_reflow_squad);
    auto& role_slots = role_reflow_fixture.world.reg()
        .get<AoeSquadLayoutState>(role_reflow_squad).layout.slots;
    assert(role_slots.size() == 4);
    role_slots[0].priority = 300;
    role_slots[1].priority = 300;
    role_slots[2].priority = -100;
    role_slots[3].priority = -100;
    const auto role_zero = role_slots[0].unit;
    const auto role_two = role_slots[2].unit;
    const auto role_center = role_reflow_fixture.world.reg()
        .get<AoePosition>(role_reflow_squad).value;
    const glm::vec2 role_forward = glm::normalize(role_formation.forward);
    const glm::vec2 role_right{role_forward.y, -role_forward.x};
    const auto role_slot_world = [&](std::size_t index) {
        const auto& slot = role_slots[index];
        return role_center + role_right * slot.local_offset.x +
               role_forward * slot.local_offset.y;
    };
    role_reflow_fixture.world.reg().get<AoePosition>(role_zero.entity).value =
        role_slot_world(2);
    role_reflow_fixture.world.reg().get<AoePosition>(role_two.entity).value =
        role_slot_world(0);
    role_reflow_fixture.world.reg().get<AoeSquadOrder>(role_reflow_squad) = {
        AoeSquadOrderType::AttackMove, role_center, {}};
    role_reflow_fixture.world.reg().get<AoeSquadState>(
        role_reflow_squad).phase = AoeSquadPhase::Moving;
    role_formation.arrival_reflow_done = false;
    role_reflow_fixture.advance_ticks(2);
    assert(role_formation.arrival_reflow_done);
    assert(role_slots[0].unit.entity == role_zero.entity);
    assert(role_slots[2].unit.entity == role_two.entity);
    assert(role_slots[0].priority == 300);
    assert(role_slots[2].priority == -100);

    // The pass-through arrival-rematch phase performs no assignment work. Its
    // persistent request suppresses per-tick retries while members continue to
    // their original slots and complete through ordinary formation movement.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin, AoeFullFormationPlugin,
            AoeFullSquadEngagementPlugin,
            AoePassThroughSquadArrivalRematchPlugin>
        pass_through_reflow_fixture;
    const auto pass_through_reflow_squad = spawn_aoe_gameplay_squad(
        pass_through_reflow_fixture.world, arrival_reflow_options);
    spawn_aoe_gameplay_unit_system(pass_through_reflow_fixture.world);
    pass_through_reflow_fixture.advance_ticks(1);
    auto& pass_through_formation = pass_through_reflow_fixture.world.reg()
        .get<AoeSquadFormation>(pass_through_reflow_squad);
    auto& pass_through_slots = pass_through_reflow_fixture.world.reg()
        .get<AoeSquadLayoutState>(pass_through_reflow_squad).layout.slots;
    const auto pass_first = pass_through_slots[0].unit;
    const auto pass_second = pass_through_slots[1].unit;
    const auto pass_center = pass_through_reflow_fixture.world.reg()
        .get<AoePosition>(pass_through_reflow_squad).value;
    const glm::vec2 pass_forward = glm::normalize(
        pass_through_formation.forward);
    const glm::vec2 pass_right{pass_forward.y, -pass_forward.x};
    const auto pass_slot_world = [&](std::size_t index) {
        const auto& slot = pass_through_slots[index];
        return pass_center + pass_right * slot.local_offset.x +
               pass_forward * slot.local_offset.y;
    };
    pass_through_reflow_fixture.world.reg().get<AoePosition>(
        pass_first.entity).value = pass_slot_world(1);
    pass_through_reflow_fixture.world.reg().get<AoePosition>(
        pass_second.entity).value = pass_slot_world(0);
    for (const auto& member : pass_through_reflow_fixture.world.reg()
             .get<AoeSquadMembers>(pass_through_reflow_squad).active)
        pass_through_reflow_fixture.world.reg().get<AoeMovement>(
            member.entity).speed = .05f;
    pass_through_reflow_fixture.world.reg().get<AoeSquadOrder>(
        pass_through_reflow_squad) = {
            AoeSquadOrderType::AttackMove, pass_center, {}, 7};
    pass_through_reflow_fixture.world.reg().get<AoeSquadState>(
        pass_through_reflow_squad).phase = AoeSquadPhase::Moving;
    pass_through_formation.arrival_reflow_done = false;
    pass_through_reflow_fixture.advance_ticks(1);
    const auto first_request = pass_through_reflow_fixture.world.reg()
        .get<AoeSquadArrivalRematchRequest>(pass_through_reflow_squad);
    assert(first_request.valid && first_request.order_revision == 7);
    assert(!pass_through_reflow_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchResult>(pass_through_reflow_squad));
    pass_through_reflow_fixture.advance_ticks(1);
    const auto& persistent_request = pass_through_reflow_fixture.world.reg()
        .get<AoeSquadArrivalRematchRequest>(pass_through_reflow_squad);
    assert(persistent_request.valid &&
           persistent_request.requested_tick == first_request.requested_tick);
    assert(pass_through_slots[0].unit.entity == pass_first.entity);
    assert(pass_through_slots[1].unit.entity == pass_second.entity);
    assert(!pass_through_formation.arrival_reflow_done);
    for (const auto& member : pass_through_reflow_fixture.world.reg()
             .get<AoeSquadMembers>(pass_through_reflow_squad).active)
        pass_through_reflow_fixture.world.reg().get<AoeMovement>(
            member.entity).speed = 2.f;
    pass_through_reflow_fixture.advance_ticks(20);
    assert(pass_through_reflow_fixture.world.reg().get<AoeSquadOrder>(
               pass_through_reflow_squad).type == AoeSquadOrderType::Idle);
    assert(!pass_through_reflow_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchRequest>(pass_through_reflow_squad));

    // A request is exclusive to AttackMove after anchor arrival with members
    // still outside their slots. MoveTo and an in-flight anchor never publish.
    Fixture no_reflow_request_fixture;
    AoeSquadSpawnOptions no_reflow_options;
    no_reflow_options.composition = {{"test", 1, 1}};
    no_reflow_options.center = {5.f, 5.f};
    no_reflow_options.team_id = 1;
    const auto no_reflow_squad = spawn_aoe_gameplay_squad(
        no_reflow_request_fixture.world, no_reflow_options);
    spawn_aoe_gameplay_unit_system(no_reflow_request_fixture.world);
    no_reflow_request_fixture.advance_ticks(1);
    assert(request_aoe_squad_attack_move(
        no_reflow_request_fixture.world, no_reflow_squad, {10.f, 5.f}));
    no_reflow_request_fixture.advance_ticks(1);
    assert(!no_reflow_request_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchRequest>(no_reflow_squad));
    assert(request_aoe_squad_move(
        no_reflow_request_fixture.world, no_reflow_squad, {9.f, 5.f}));
    no_reflow_request_fixture.advance_ticks(1);
    assert(!no_reflow_request_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchRequest>(no_reflow_squad));
    assert(request_aoe_squad_stop(
        no_reflow_request_fixture.world, no_reflow_squad));
    no_reflow_request_fixture.advance_ticks(1);
    const auto settled_center = no_reflow_request_fixture.world.reg()
        .get<AoePosition>(no_reflow_squad).value;
    auto& settled_order = no_reflow_request_fixture.world.reg()
        .get<AoeSquadOrder>(no_reflow_squad);
    settled_order = {AoeSquadOrderType::AttackMove, settled_center, {},
                     settled_order.revision + 1};
    no_reflow_request_fixture.world.reg().get<AoeSquadState>(
        no_reflow_squad).phase = AoeSquadPhase::Moving;
    no_reflow_request_fixture.advance_ticks(1);
    assert(!no_reflow_request_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchRequest>(no_reflow_squad));

    // A stale request is replaced once with the current order revision.
    Fixture<AoePassThroughLocalAvoidancePlugin,
            AoeDefaultGlobalMotionPlugin, AoeFullFormationPlugin,
            AoeFullSquadEngagementPlugin,
            AoePassThroughSquadArrivalRematchPlugin>
        stale_reflow_fixture;
    const auto stale_reflow_squad = spawn_aoe_gameplay_squad(
        stale_reflow_fixture.world, arrival_reflow_options);
    spawn_aoe_gameplay_unit_system(stale_reflow_fixture.world);
    stale_reflow_fixture.advance_ticks(1);
    auto& stale_formation = stale_reflow_fixture.world.reg()
        .get<AoeSquadFormation>(stale_reflow_squad);
    const auto& stale_slots = stale_reflow_fixture.world.reg()
        .get<AoeSquadLayoutState>(stale_reflow_squad).layout.slots;
    const auto stale_center = stale_reflow_fixture.world.reg()
        .get<AoePosition>(stale_reflow_squad).value;
    stale_reflow_fixture.world.reg().get<AoePosition>(
        stale_slots[0].unit.entity).value += glm::vec2{2.f, 0.f};
    stale_reflow_fixture.world.reg().get<AoeSquadOrder>(stale_reflow_squad) = {
        AoeSquadOrderType::AttackMove, stale_center, {}, 9};
    stale_reflow_fixture.world.reg().get<AoeSquadState>(
        stale_reflow_squad).phase = AoeSquadPhase::Moving;
    stale_reflow_fixture.world.reg().emplace<
        AoeSquadArrivalRematchRequest>(stale_reflow_squad,
            AoeSquadArrivalRematchRequest{8, 1, true});
    stale_reflow_fixture.advance_ticks(1);
    const auto& replaced_request = stale_reflow_fixture.world.reg()
        .get<AoeSquadArrivalRematchRequest>(stale_reflow_squad);
    assert(replaced_request.valid && replaced_request.order_revision == 9);

    // Invalid assignment input yields one Failed result. Formation consumes
    // it as a handled episode and does not publish another request.
    Fixture failed_reflow_fixture;
    const auto failed_reflow_squad = spawn_aoe_gameplay_squad(
        failed_reflow_fixture.world, arrival_reflow_options);
    spawn_aoe_gameplay_unit_system(failed_reflow_fixture.world);
    failed_reflow_fixture.advance_ticks(1);
    auto& failed_formation = failed_reflow_fixture.world.reg()
        .get<AoeSquadFormation>(failed_reflow_squad);
    failed_reflow_fixture.world.reg().get<AoeSquadLayoutState>(
        failed_reflow_squad).layout.slots.pop_back();
    const auto failed_center = failed_reflow_fixture.world.reg()
        .get<AoePosition>(failed_reflow_squad).value;
    failed_reflow_fixture.world.reg().get<AoeSquadOrder>(
        failed_reflow_squad) = {
            AoeSquadOrderType::AttackMove, failed_center, {}, 13};
    failed_reflow_fixture.world.reg().get<AoeSquadState>(
        failed_reflow_squad).phase = AoeSquadPhase::Moving;
    failed_reflow_fixture.world.reg().emplace<
        AoeSquadArrivalRematchRequest>(failed_reflow_squad,
            AoeSquadArrivalRematchRequest{13, 98, true});
    AoeFullSquadArrivalRematchPlugin::fixed_tick(
        failed_reflow_fixture.world, 99);
    const auto& failed_result = failed_reflow_fixture.world.reg()
        .get<AoeSquadArrivalRematchResult>(failed_reflow_squad);
    assert(failed_result.valid &&
           failed_result.status == AoeSquadArrivalRematchStatus::Failed &&
           failed_result.order_revision == 13);
    gld::ecs::aoe::detail::aoe_gameplay_formation_fixed_tick(
        failed_reflow_fixture.world, 100, false);
    assert(failed_formation.arrival_reflow_done);
    assert(!failed_reflow_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchRequest>(failed_reflow_squad));
    assert(!failed_reflow_fixture.world.reg()
        .all_of<AoeSquadArrivalRematchResult>(failed_reflow_squad));

    // The preview-sized squad computes one center guide. Until route splitting
    // is implemented, members keep direct provisional waypoints and do not run
    // independent A* searches around the barriers.
    Fixture stress_squad_fixture;
    stress_squad_fixture.world.add_resource<AoeLogicMap>(squad_stress_map());
    AoeSquadSpawnOptions stress_options;
    stress_options.composition = {{"test", 64, 1}};
    stress_options.center = {-11.f, 0.f};
    stress_options.forward = {1.f, 0.f};
    stress_options.formation_spacing = .2f;
    stress_options.team_id = 1;
    const auto stress_squad = spawn_aoe_gameplay_squad(
        stress_squad_fixture.world, stress_options);
    spawn_aoe_gameplay_unit_system(stress_squad_fixture.world);
    stress_squad_fixture.advance_ticks(1);
    assert(stress_squad_fixture.world.reg().get<AoeSquadMembers>(stress_squad)
               .active.size() == 64);
    assert(request_aoe_squad_move(
        stress_squad_fixture.world, stress_squad, {11.f, 0.f}));
    stress_squad_fixture.advance_ticks(1);
    const auto& stress_guide = stress_squad_fixture.world.reg()
        .get<AoeNavigationPath>(stress_squad);
    assert(!stress_guide.no_path && !stress_guide.waypoints.empty());
    bool stress_detours = false;
    for (const auto waypoint : stress_guide.waypoints)
        stress_detours = stress_detours || std::abs(waypoint.y) > 2.5f;
    assert(stress_detours);
    for (const auto& member : stress_squad_fixture.world.reg()
             .get<AoeSquadMembers>(stress_squad).active) {
        const auto& path = stress_squad_fixture.world.reg()
            .get<AoeNavigationPath>(member.entity);
        assert(path.request_sequence == 0);
        assert(path.waypoints.size() == 1);
    }
    stress_squad_fixture.advance_ticks(800);
    assert(glm::length(stress_squad_fixture.world.reg()
               .get<AoePosition>(stress_squad).value - glm::vec2(11.f, 0.f)) < .05f);

    // Two head-on squads negotiate the same relative (right-hand) passing
    // rule, which produces complementary world-space lanes.
    Fixture traffic_fixture;
    traffic_fixture.world.add_resource<AoeLogicMap>(flat_map());
    AoeSquadSpawnOptions traffic_a;
    traffic_a.composition = {{"test", 4, 1}};
    traffic_a.center = {5.f, 6.f};
    traffic_a.forward = {1.f, 0.f};
    traffic_a.team_id = 1;
    AoeSquadSpawnOptions traffic_b = traffic_a;
    traffic_b.center = {11.f, 6.f};
    traffic_b.forward = {-1.f, 0.f};
    traffic_b.team_id = 2;
    const auto squad_a = spawn_aoe_gameplay_squad(
        traffic_fixture.world, traffic_a);
    const auto squad_b = spawn_aoe_gameplay_squad(
        traffic_fixture.world, traffic_b);
    spawn_aoe_gameplay_unit_system(traffic_fixture.world);
    traffic_fixture.advance_ticks(1);
    assert(request_aoe_squad_move(traffic_fixture.world, squad_a, {15.f, 6.f}));
    assert(request_aoe_squad_move(traffic_fixture.world, squad_b, {1.f, 6.f}));
    traffic_fixture.advance_ticks(3);
    const auto& traffic_state_a = traffic_fixture.world.reg()
        .get<AoeSquadTrafficState>(squad_a);
    const auto& traffic_state_b = traffic_fixture.world.reg()
        .get<AoeSquadTrafficState>(squad_b);
    assert(traffic_state_a.mode == AoeSquadTrafficMode::PassingRight);
    assert(traffic_state_b.mode == AoeSquadTrafficMode::PassingRight);
    assert(traffic_state_a.negotiated_side == -1 &&
           traffic_state_b.negotiated_side == -1);
    assert(traffic_state_a.lateral_offset > 0.f &&
           traffic_state_b.lateral_offset > 0.f);

    // A sealed map produces a stable Blocked state. Changing the static map
    // revision invalidates the guide and lets the same order resume.
    Fixture blocked_squad_fixture;
    blocked_squad_fixture.world.add_resource<AoeLogicMap>(flat_map());
    AoeSquadSpawnOptions blocked_options = mapped_options;
    const auto blocked_squad = spawn_aoe_gameplay_squad(
        blocked_squad_fixture.world, blocked_options);
    spawn_aoe_gameplay_unit_system(blocked_squad_fixture.world);
    blocked_squad_fixture.advance_ticks(1);
    squad_wall.half_extents = {.45f, 6.f};
    const auto blocking_wall = blocked_squad_fixture.world
        .resource<AoeLogicMap>().add_runtime_static_obstacle(squad_wall);
    assert(request_aoe_squad_move(
        blocked_squad_fixture.world, blocked_squad, {16.f, 6.f}));
    blocked_squad_fixture.advance_ticks(1);
    assert(blocked_squad_fixture.world.reg().get<AoeSquadState>(blocked_squad)
               .phase == AoeSquadPhase::Blocked);
    assert(blocked_squad_fixture.world.resource<AoeLogicMap>()
               .remove_runtime_static_obstacle(blocking_wall));
    blocked_squad_fixture.advance_ticks(120);
    assert(blocked_squad_fixture.world.reg().get<AoeSquadState>(blocked_squad)
               .phase == AoeSquadPhase::Idle);

    // A lagging member no longer freezes the virtual anchor and every other
    // formation slot. The leash remains diagnostic-only while the member
    // independently catches up at the destination.
    Fixture leash_fixture;
    leash_fixture.world.add_resource<AoeLogicMap>(flat_map());
    const auto leash_squad = spawn_aoe_gameplay_squad(
        leash_fixture.world, mapped_options);
    spawn_aoe_gameplay_unit_system(leash_fixture.world);
    leash_fixture.advance_ticks(1);
    const auto lagging_member = leash_fixture.world.reg()
        .get<AoeSquadMembers>(leash_squad).active.front().entity;
    assert(request_aoe_squad_move(
        leash_fixture.world, leash_squad, {12.f, 6.f}));
    leash_fixture.advance_ticks(1);
    leash_fixture.world.reg().get<AoePosition>(lagging_member).value = {3.f, 11.f};
    const glm::vec2 leash_anchor = leash_fixture.world.reg()
        .get<AoePosition>(leash_squad).value;
    leash_fixture.advance_ticks(1);
    assert(leash_fixture.world.reg().get<AoePosition>(leash_squad).value.x >
           leash_anchor.x);
    leash_fixture.world.reg().get<AoePosition>(lagging_member).value = leash_anchor;

    Fixture partial_squad_fixture;
    AoeSquadSpawnOptions partial_options;
    partial_options.composition = {{"test", 1, 1}, {"missing", 1, 1}};
    partial_options.team_id = 1;
    const auto partial_squad = spawn_aoe_gameplay_squad(
        partial_squad_fixture.world, partial_options);
    spawn_aoe_gameplay_unit_system(partial_squad_fixture.world);
    partial_squad_fixture.advance_ticks(1);
    const auto& partial_spawn = partial_squad_fixture.world.reg()
        .get<AoeSquadSpawnState>(partial_squad);
    assert(partial_spawn.status == AoeSquadSpawnStatus::Partial);
    assert(partial_spawn.succeeded == 1 && partial_spawn.failed == 1);
    assert(partial_spawn.errors.size() == 1);
    assert(partial_squad_fixture.world.reg().get<AoeSquadMembers>(partial_squad)
               .active.size() == 1);

    Fixture failed_squad_fixture;
    AoeSquadSpawnOptions failed_options;
    failed_options.composition = {{"missing", 2, 1}};
    const auto failed_squad = spawn_aoe_gameplay_squad(
        failed_squad_fixture.world, failed_options);
    spawn_aoe_gameplay_unit_system(failed_squad_fixture.world);
    failed_squad_fixture.advance_ticks(1);
    assert(failed_squad_fixture.world.reg().get<AoeSquadSpawnState>(failed_squad)
               .status == AoeSquadSpawnStatus::Failed);
    assert(!request_aoe_squad_move(
        failed_squad_fixture.world, entt::null, {1.f, 0.f}));

    Fixture invalid_formation_fixture;
    invalid_formation_fixture.world.resource<AoeFormationRegistry>()
        .bind<InvalidFormationType, InvalidFormation>();
    AoeSquadSpawnOptions invalid_formation_options;
    invalid_formation_options.composition = {{"test", 1, 1}};
    invalid_formation_options.formation = InvalidFormationType;
    const auto invalid_formation_squad = spawn_aoe_gameplay_squad(
        invalid_formation_fixture.world, invalid_formation_options);
    assert(invalid_formation_squad != entt::null);
    spawn_aoe_gameplay_unit_system(invalid_formation_fixture.world);
    invalid_formation_fixture.advance_ticks(1);
    const auto& invalid_formation_spawn = invalid_formation_fixture.world.reg()
        .get<AoeSquadSpawnState>(invalid_formation_squad);
    assert(invalid_formation_spawn.status == AoeSquadSpawnStatus::Failed);
    assert(invalid_formation_fixture.world.reg()
               .get<AoeSquadState>(invalid_formation_squad).phase ==
           AoeSquadPhase::Failed);
    assert(invalid_formation_spawn.succeeded == 1);
    assert(invalid_formation_spawn.errors ==
           std::vector<std::string>{"formation layout failed"});

    // The pass-through engagement phase is intentionally empty. AttackMove
    // therefore remains formation travel even with a nearby enemy, while an
    // explicit Squad AttackTarget still uses the base squad-control path.
    Fixture<AoeFullLocalAvoidancePlugin, AoeDefaultGlobalMotionPlugin,
            AoeFullFormationPlugin,
            AoePassThroughSquadEngagementPlugin>
        pass_through_engagement_fixture;
    AoeSquadSpawnOptions pass_through_engagement_options;
    pass_through_engagement_options.composition = {{"test", 2, 1}};
    pass_through_engagement_options.team_id = 1;
    const auto pass_through_engagement_squad = spawn_aoe_gameplay_squad(
        pass_through_engagement_fixture.world,
        pass_through_engagement_options);
    spawn_aoe_gameplay_unit_system(pass_through_engagement_fixture.world);
    pass_through_engagement_fixture.advance_ticks(1);
    const auto pass_through_enemy = pass_through_engagement_fixture.unit(
        {2.f, 0.f}, 500.f, 2);
    const float pass_through_center_before =
        pass_through_engagement_fixture.world.reg().get<AoePosition>(
            pass_through_engagement_squad).value.x;
    assert(request_aoe_squad_attack_move(
        pass_through_engagement_fixture.world,
        pass_through_engagement_squad, {8.f, 0.f}));
    pass_through_engagement_fixture.advance_ticks(2);
    assert(!pass_through_engagement_fixture.world.reg().all_of<
        AoeSquadEngagementResult>(pass_through_engagement_squad));
    for (const auto& member : pass_through_engagement_fixture.world.reg()
             .get<AoeSquadMembers>(pass_through_engagement_squad).active)
        assert(!pass_through_engagement_fixture.world.reg().all_of<
            AoeAttackOrder>(member.entity));
    assert(pass_through_engagement_fixture.world.reg().get<AoePosition>(
               pass_through_engagement_squad).value.x >
           pass_through_center_before);
    assert(request_aoe_squad_attack(
        pass_through_engagement_fixture.world,
        pass_through_engagement_squad, pass_through_enemy));
    pass_through_engagement_fixture.advance_ticks(1);
    for (const auto& member : pass_through_engagement_fixture.world.reg()
             .get<AoeSquadMembers>(pass_through_engagement_squad).active)
        assert(pass_through_engagement_fixture.world.reg()
            .get<AoeAttackOrder>(member.entity).target.entity ==
            pass_through_enemy);

    Fixture squad_combat_fixture;
    AoeSquadSpawnOptions attackers_options;
    attackers_options.composition = {{"test", 2, 1}};
    attackers_options.team_id = 1;
    const auto attackers = spawn_aoe_gameplay_squad(
        squad_combat_fixture.world, attackers_options);
    spawn_aoe_gameplay_unit_system(squad_combat_fixture.world);
    squad_combat_fixture.advance_ticks(1);
    const auto shared_enemy = squad_combat_fixture.unit({3.f, 0.f}, 10.f, 2);
    const auto next_shared_enemy = squad_combat_fixture.unit({4.f, 0.f}, 10.f, 2);
    assert(request_aoe_squad_attack_move(
        squad_combat_fixture.world, attackers, {8.f, 0.f}));
    squad_combat_fixture.advance_ticks(1);
    const auto& engagement_result = squad_combat_fixture.world.reg()
        .get<AoeSquadEngagementResult>(attackers);
    assert(engagement_result.valid &&
           engagement_result.produced_tick ==
               squad_combat_fixture.world.resource<AoeGameplayClock>().tick &&
           engagement_result.status == AoeSquadEngagementStatus::Active &&
           engagement_result.active_members == 2);
    assert(squad_combat_fixture.world.reg().get<AoeSquadOrder>(attackers)
               .target.entity == entt::null);
    for (const auto& member : squad_combat_fixture.world.reg()
             .get<AoeSquadMembers>(attackers).active) {
        assert(squad_combat_fixture.world.reg().all_of<AoeAttackOrder>(member.entity));
        assert(squad_combat_fixture.world.reg()
            .get<AoeAttackOrder>(member.entity).target.entity == shared_enemy);
        assert(squad_combat_fixture.world.reg()
            .all_of<AoeEngagementApproach>(member.entity));
        assert(std::abs(squad_combat_fixture.world.reg()
            .get<AoeEngagementApproach>(member.entity).desired_gap - 3.2f) < 1e-5f);
    }
    for (int i = 0; i < 100 &&
         (squad_combat_fixture.world.reg().get<AoeHealth>(shared_enemy).current > 0.f ||
          squad_combat_fixture.world.reg().get<AoeHealth>(next_shared_enemy).current > 0.f);
         ++i)
        squad_combat_fixture.advance_ticks(1);
    assert(squad_combat_fixture.world.reg().get<AoeHealth>(shared_enemy).current == 0.f);
    assert(squad_combat_fixture.world.reg().get<AoeHealth>(next_shared_enemy).current == 0.f);
    const float center_before_resume = squad_combat_fixture.world.reg()
        .get<AoePosition>(attackers).value.x;
    squad_combat_fixture.advance_ticks(1);
    assert(squad_combat_fixture.world.reg().get<AoeSquadOrder>(attackers)
               .target.entity == entt::null);
    assert(squad_combat_fixture.world.reg().get<AoeSquadOrder>(attackers).type ==
           AoeSquadOrderType::AttackMove);
    assert(squad_combat_fixture.world.reg().get<AoeSquadState>(attackers).phase ==
           AoeSquadPhase::Moving);
    assert(squad_combat_fixture.world.reg().get<AoePosition>(attackers).value.x >
           center_before_resume);

    Fixture partial_engagement_fixture;
    AoeSquadSpawnOptions partial_engagement_options;
    partial_engagement_options.composition = {{"test", 2, 1}};
    partial_engagement_options.team_id = 1;
    const auto partial_engagement_squad = spawn_aoe_gameplay_squad(
        partial_engagement_fixture.world, partial_engagement_options);
    spawn_aoe_gameplay_unit_system(partial_engagement_fixture.world);
    partial_engagement_fixture.advance_ticks(1);
    partial_engagement_fixture.unit({2.f, 0.f}, 500.f, 2);
    assert(request_aoe_squad_attack_move(partial_engagement_fixture.world,
                                         partial_engagement_squad,
                                         {8.f, 0.f}));
    partial_engagement_fixture.advance_ticks(1);
    std::size_t engaged_members = 0;
    for (const auto& member : partial_engagement_fixture.world.reg()
             .get<AoeSquadMembers>(partial_engagement_squad).active) {
        engaged_members += partial_engagement_fixture.world.reg()
            .all_of<AoeAttackOrder>(member.entity);
    }
    assert(engaged_members == 2);
    const float partial_anchor = partial_engagement_fixture.world.reg()
        .get<AoePosition>(partial_engagement_squad).value.x;
    partial_engagement_fixture.advance_ticks(1);
    assert(std::abs(partial_engagement_fixture.world.reg()
               .get<AoePosition>(partial_engagement_squad).value.x -
           partial_anchor) < 1e-5f);

    // Squad awareness is shared: a rear member outside its own acquisition
    // radius still selects from the enemy set discovered by the front member.
    Fixture shared_awareness_fixture;
    AoeSquadSpawnOptions shared_awareness_options;
    shared_awareness_options.composition = {{"test", 2, 1}};
    shared_awareness_options.team_id = 1;
    const auto shared_awareness_squad = spawn_aoe_gameplay_squad(
        shared_awareness_fixture.world, shared_awareness_options);
    spawn_aoe_gameplay_unit_system(shared_awareness_fixture.world);
    shared_awareness_fixture.advance_ticks(1);
    const auto& awareness_members = shared_awareness_fixture.world.reg()
        .get<AoeSquadMembers>(shared_awareness_squad).active;
    assert(awareness_members.size() == 2);
    shared_awareness_fixture.world.reg().get<AoePosition>(
        awareness_members[0].entity).value = {0.f, 0.f};
    shared_awareness_fixture.world.reg().get<AoePosition>(
        awareness_members[1].entity).value = {-8.f, 0.f};
    const auto awareness_enemy = shared_awareness_fixture.unit(
        {5.f, 0.f}, 500.f, 2);
    shared_awareness_fixture.world.reg().get<AoeSquadFormation>(
        shared_awareness_squad).dirty = false;
    shared_awareness_fixture.world.reg().get<AoeSquadOrder>(
        shared_awareness_squad) = {
            AoeSquadOrderType::AttackMove, {10.f, 0.f}, {}};
    shared_awareness_fixture.world.reg().get<AoeSquadState>(
        shared_awareness_squad).phase = AoeSquadPhase::Moving;
    const float awareness_anchor = shared_awareness_fixture.world.reg()
        .get<AoePosition>(shared_awareness_squad).value.x;
    shared_awareness_fixture.advance_ticks(1);
    for (const auto& member : awareness_members)
        assert(shared_awareness_fixture.world.reg().get<AoeAttackOrder>(
                   member.entity).target.entity == awareness_enemy);
    assert(std::abs(shared_awareness_fixture.world.reg().get<AoePosition>(
               shared_awareness_squad).value.x - awareness_anchor) < 1e-5f);

    // A stalled Squad member may immediately engage a different enemy that is
    // already inside its own weapon range. The Squad keeps its Attack Move
    // order and remains engaged; this is not the shared-candidate acquisition
    // path, whose candidates may be outside this member's real weapon range.
    Fixture stalled_squad_fixture;
    AoeSquadSpawnOptions stalled_squad_options;
    stalled_squad_options.composition = {{"test", 1, 1}};
    stalled_squad_options.team_id = 1;
    const auto stalled_squad = spawn_aoe_gameplay_squad(
        stalled_squad_fixture.world, stalled_squad_options);
    spawn_aoe_gameplay_unit_system(stalled_squad_fixture.world);
    stalled_squad_fixture.advance_ticks(1);
    const auto stalled_member = stalled_squad_fixture.world.reg()
        .get<AoeSquadMembers>(stalled_squad).active.front();
    const glm::vec2 stalled_member_start = stalled_squad_fixture.world.reg()
        .get<AoePosition>(stalled_member.entity).value;
    const auto stalled_squad_primary = stalled_squad_fixture.unit(
        stalled_member_start + glm::vec2{5.5f, 0.f}, 500.f, 2);
    assert(request_aoe_squad_attack_move(
        stalled_squad_fixture.world, stalled_squad,
        stalled_member_start + glm::vec2{12.f, 0.f}));
    stalled_squad_fixture.advance_ticks(1);
    assert(stalled_squad_fixture.world.reg().get<AoeAttackOrder>(
               stalled_member.entity).target.entity == stalled_squad_primary);
    const auto stalled_squad_nearby = stalled_squad_fixture.unit(
        stalled_squad_fixture.world.reg().get<AoePosition>(
            stalled_member.entity).value + glm::vec2{.6f, 0.f}, 500.f, 2);
    stalled_squad_fixture.world.reg().get<AoeLocomotionState>(
        stalled_member.entity).stalled_ticks = 1;
    stalled_squad_fixture.advance_ticks(1);
    assert(stalled_squad_fixture.world.reg().get<AoeAttackOrder>(
               stalled_member.entity).target.entity == stalled_squad_nearby);
    assert(stalled_squad_fixture.world.reg().get<AoeSquadOrder>(
               stalled_squad).type == AoeSquadOrderType::AttackMove);
    assert(stalled_squad_fixture.world.reg().get<AoeSquadState>(
               stalled_squad).phase == AoeSquadPhase::Engaging);

    const auto disband_member = squad_combat_fixture.world.reg()
        .get<AoeSquadMembers>(attackers).active.front().entity;
    assert(disband_aoe_gameplay_squad(squad_combat_fixture.world, attackers));
    assert(!squad_combat_fixture.world.reg().valid(attackers));
    assert(squad_combat_fixture.world.reg().valid(disband_member));
    assert(!squad_combat_fixture.world.reg().all_of<AoeSquadMember>(disband_member));

    // Explicit AttackTarget ends with its requested target and never chains to
    // another nearby enemy.
    Fixture explicit_squad_fixture;
    AoeSquadSpawnOptions explicit_squad_options;
    explicit_squad_options.composition = {{"test", 2, 1}};
    explicit_squad_options.team_id = 1;
    const auto explicit_squad = spawn_aoe_gameplay_squad(
        explicit_squad_fixture.world, explicit_squad_options);
    spawn_aoe_gameplay_unit_system(explicit_squad_fixture.world);
    explicit_squad_fixture.advance_ticks(1);
    const auto explicit_enemy = explicit_squad_fixture.unit({2.f, 0.f}, 1.f, 2);
    const auto ignored_enemy = explicit_squad_fixture.unit({3.f, 0.f}, 50.f, 2);
    assert(request_aoe_squad_attack(
        explicit_squad_fixture.world, explicit_squad, explicit_enemy));
    explicit_squad_fixture.advance_ticks(1);
    for (const auto& member : explicit_squad_fixture.world.reg()
             .get<AoeSquadMembers>(explicit_squad).active) {
        assert(explicit_squad_fixture.world.reg().get<AoeAttackOrder>(
                   member.entity).target.entity == explicit_enemy);
        assert(explicit_squad_fixture.world.reg().all_of<
                   AoeEngagementApproach>(member.entity));
    }
    for (int i = 0; i < 100 && explicit_squad_fixture.world.reg()
             .get<AoeHealth>(explicit_enemy).current > 0.f; ++i)
        explicit_squad_fixture.advance_ticks(1);
    assert(explicit_squad_fixture.world.reg().get<AoeHealth>(explicit_enemy).current == 0.f);
    explicit_squad_fixture.advance_ticks(1);
    const auto& explicit_order = explicit_squad_fixture.world.reg()
        .get<AoeSquadOrder>(explicit_squad);
    assert(explicit_order.type == AoeSquadOrderType::Idle);
    assert(explicit_order.target.entity == entt::null);
    for (const auto& member : explicit_squad_fixture.world.reg()
             .get<AoeSquadMembers>(explicit_squad).active) {
        const auto* attack = explicit_squad_fixture.world.reg()
            .try_get<AoeAttackOrder>(member.entity);
        assert(!attack || attack->target.entity != ignored_enemy);
    }

    // Logical movement is projected into the 2:1 isometric screen-facing space
    // before selecting the nearest clockwise SLD sector around screen +X.
    // Locomotion-facing changes require two stable fixed ticks so velocity
    // jitter at a sector boundary cannot dirty the render direction every tick.
    Fixture direction_fixture;
    const std::pair<glm::vec2, int> direction_cases[] = {
        {{ 1.f, -1.f},  0}, {{ 1.f,  0.f},  1},
        {{ 1.f,  1.f},  4}, {{ 0.f,  1.f},  7},
        {{-1.f,  1.f},  8}, {{-1.f,  0.f},  9},
        {{-1.f, -1.f}, 12}, {{ 0.f, -1.f}, 15},
    };
    for (const auto& [destination, expected] : direction_cases) {
        const auto mover = direction_fixture.unit({0.f, 0.f});
        assert(request_aoe_move(direction_fixture.world, mover, destination));
        direction_fixture.advance_ticks(2);
        assert(direction_fixture.world.reg().get<AoeFacing>(mover).direction == expected);
    }

    auto logical_vector_for_screen_angle = [](float clockwise_degrees) {
        const float radians = glm::radians(clockwise_degrees);
        const glm::vec2 screen{std::cos(radians), std::sin(radians)};
        return glm::vec2{screen.x * .5f + screen.y,
                         -screen.x * .5f + screen.y} * 10.f;
    };
    const std::pair<float, int> boundary_cases[] = {
        {11.24f, 0}, {11.26f, 1}, {33.74f, 1}, {33.76f, 2},
        {348.74f, 15}, {348.76f, 0},
    };
    for (const auto& [degrees, expected] : boundary_cases) {
        const auto mover = direction_fixture.unit({0.f, 0.f});
        assert(request_aoe_move(direction_fixture.world, mover,
                                logical_vector_for_screen_angle(degrees)));
        direction_fixture.advance_ticks(2);
        assert(direction_fixture.world.reg().get<AoeFacing>(mover).direction == expected);
    }
    const auto jittering = direction_fixture.unit({0.f, 0.f});
    for (int i = 0; i < 8; ++i) {
        const float angle = (i & 1) ? 11.24f : 11.26f;
        const auto current = direction_fixture.world.reg()
            .get<AoePosition>(jittering).value;
        assert(request_aoe_move(direction_fixture.world, jittering,
            current + logical_vector_for_screen_angle(angle)));
        direction_fixture.advance_ticks(1);
    }
    assert(direction_fixture.world.reg().get<AoeFacing>(jittering).direction == 0);
    assert(direction_fixture.world.resource<AoeGameplayDiagnostics>()
               .facing_changes_suppressed > 0);
    const auto stationary = direction_fixture.unit({2.f, 3.f});
    direction_fixture.world.reg().get<AoeFacing>(stationary).direction = 7;
    assert(request_aoe_move(direction_fixture.world, stationary, {2.f, 3.f}));
    direction_fixture.advance_ticks(1);
    assert(direction_fixture.world.reg().get<AoeFacing>(stationary).direction == 7);

    // DAT launch offsets rotate with the locked SLD-facing slot. Direction
    // zero is opposite raw DAT +Y and slots advance clockwise.
    struct LaunchCase { glm::vec2 target; int direction; glm::vec2 offset; };
    const LaunchCase launch_cases[] = {
        {{ 1.f, -1.f},  0, { 0.f, -.5f}},
        {{ 1.f,  1.f},  4, {-.5f,  0.f}},
        {{-1.f,  1.f},  8, { 0.f,  .5f}},
        {{-1.f, -1.f}, 12, { .5f,  0.f}},
    };
    for (const auto& launch_case : launch_cases) {
        Fixture launch_fixture;
        const auto source = launch_fixture.unit({0.f, 0.f});
        const auto destination = launch_fixture.unit(launch_case.target);
        assert(request_aoe_attack(launch_fixture.world, source, destination));
        launch_fixture.advance_ticks(3);
        assert(launch_fixture.world.reg().get<AoeFacing>(source).direction ==
               launch_case.direction);
        const auto projectiles = launch_fixture.world.reg().view<AoeProjectile>();
        assert(!projectiles.empty());
        const auto& projectile = projectiles.get<AoeProjectile>(*projectiles.begin());
        assert(glm::length(glm::vec2(projectile.launch_position) -
                           launch_case.offset) < 1e-5f);
        assert(std::abs(projectile.launch_position.z - 1.5f) < 1e-5f);
        assert(projectile.position == projectile.launch_position);
    }

    // Projectile release creates a stationary-on-spawn-tick gameplay entity;
    // health changes only after a later swept impact.
    Fixture impact_fixture;
    const auto impact_attacker = impact_fixture.unit({0.f, 0.f});
    const auto impact_target = impact_fixture.unit({2.f, 0.f});
    assert(request_aoe_attack(impact_fixture.world, impact_attacker, impact_target));
    impact_fixture.advance_ticks(3);
    assert(impact_fixture.world.reg().get<AoeHealth>(impact_target).current == 50.f);
    assert(impact_fixture.world.reg().storage<AoeProjectile>().size() == 1);
    const auto released_projectile =
        *impact_fixture.world.reg().view<AoeProjectile>().begin();
    const auto& released =
        impact_fixture.world.reg().get<AoeProjectile>(released_projectile);
    assert(released.position == released.launch_position);
    assert(request_aoe_stop(impact_fixture.world, impact_attacker));
    impact_fixture.advance_ticks(10);
    assert(impact_fixture.world.reg().get<AoeHealth>(impact_target).current == 40.f);
    assert(impact_fixture.world.resource<AoeGameplayDiagnostics>().projectiles_spawned == 1);
    assert(impact_fixture.world.resource<AoeGameplayDiagnostics>().projectiles_hit == 1);

    // Arrow logic continuously seeks the target's live position rather than a
    // release-time snapshot.
    Fixture homing_fixture;
    const auto homing_attacker = homing_fixture.unit({0.f, 0.f});
    const auto homing_target = homing_fixture.unit({3.f, 0.f});
    AoeProjectileSpawnContext homing_context;
    homing_context.attacker = homing_attacker;
    homing_context.target = {homing_target,
        homing_fixture.world.reg().get<AoeGameplayIdentity>(homing_target).instance_id};
    homing_context.launch_position = {0.f, 0.f, 1.f};
    homing_context.damage = {{3, 7.f}};
    const auto homing_arrow = ArrowProjectileLogic::spawn(
        homing_fixture.world, homing_context);
    homing_fixture.world.reg().get<AoePosition>(homing_target).value = {3.f, 2.f};
    aoe_projectile_tick(homing_fixture.world, 1);
    assert(homing_fixture.world.reg().valid(homing_arrow));
    assert(homing_fixture.world.reg().get<AoeProjectile>(homing_arrow).velocity.y > 0.f);
    homing_fixture.world.reg().get<AoeGameplayIdentity>(homing_target).instance_id++;
    aoe_projectile_tick(homing_fixture.world, 2);
    assert(!homing_fixture.world.reg().valid(homing_arrow));
    assert(homing_fixture.world.resource<AoeGameplayDiagnostics>().projectiles_missed == 1);

    // An attack turns toward the target only when range and cooldown permit the
    // attack to start. That direction remains locked for the animation, then the
    // next attack start aims again at the target's new position.
    Fixture facing_fixture;
    const auto facing_attacker = facing_fixture.unit({0.f, 0.f});
    const auto facing_target = facing_fixture.unit({1.f, -1.f});
    facing_fixture.world.reg().get<AoeFacing>(facing_attacker).direction = 8;
    assert(request_aoe_attack(facing_fixture.world, facing_attacker, facing_target));
    facing_fixture.advance_ticks(1);
    const auto first_attack_sequence =
        facing_fixture.world.reg().get<AoeActionState>(facing_attacker).sequence;
    assert(facing_fixture.world.reg().get<AoeActionState>(facing_attacker).state ==
           UnitState::Attacking);
    assert(facing_fixture.world.reg().get<AoeFacing>(facing_attacker).direction == 0);

    facing_fixture.world.reg().get<AoePosition>(facing_target).value = {-1.f, 1.f};
    facing_fixture.advance_ticks(1);
    assert(facing_fixture.world.reg().get<AoeActionState>(facing_attacker).state ==
           UnitState::Attacking);
    assert(facing_fixture.world.reg().get<AoeFacing>(facing_attacker).direction == 0);
    while (facing_fixture.world.reg().get<AoeActionState>(facing_attacker).state ==
           UnitState::Attacking)
        facing_fixture.advance_ticks(1);
    assert(facing_fixture.world.reg().get<AoeFacing>(facing_attacker).direction == 0);
    while (facing_fixture.world.reg().get<AoeActionState>(facing_attacker).sequence ==
           first_attack_sequence)
        facing_fixture.advance_ticks(1);
    assert(facing_fixture.world.reg().get<AoeActionState>(facing_attacker).state ==
           UnitState::Attacking);
    assert(facing_fixture.world.reg().get<AoeFacing>(facing_attacker).direction == 8);

    // A persistent attack order first drives navigation, then repeatedly attacks
    // without being reissued. Critical damage is (7 - 2) * 2 = 10.
    Fixture fixture;
    const auto attacker = fixture.unit({0.f, 0.f});
    const auto target = fixture.unit({6.f, 0.f}, 25.f);
    assert(request_aoe_attack(fixture.world, attacker, target));
    fixture.advance_ticks(1);
    assert(fixture.world.reg().get<AoeActionState>(attacker).state == UnitState::Moving);
    assert(fixture.world.reg().all_of<AoeMoveGoal, AoeNavigationPath>(attacker));
    const float original_x = fixture.world.reg().get<AoePosition>(attacker).value.x;
    fixture.advance_ticks(8);
    assert(fixture.world.reg().get<AoePosition>(attacker).value.x > original_x);
    assert(fixture.world.reg().all_of<AoeAttackOrder>(attacker));

    for (int i = 0; i < 40 && fixture.world.reg().get<AoeHealth>(target).current > 0.f; ++i)
        fixture.advance_ticks(1);
    assert(fixture.world.reg().get<AoeHealth>(target).current == 0.f);
    assert(fixture.world.reg().get<AoeActionState>(target).state == UnitState::Dying ||
           fixture.world.reg().get<AoeActionState>(target).state == UnitState::Disappearing ||
           fixture.world.reg().all_of<AoeRecyclePending>(target));
    assert(fixture.world.resource<AoeGameplayDiagnostics>().attacks_started >= 3);
    assert(fixture.world.resource<AoeGameplayDiagnostics>().damage_events == 3);

    bool saw_damage = false;
    bool saw_death = false;
    for (const auto& event : fixture.world.resource<Events<AoeActionEvent>>().read()) {
        if (event.type == AoeActionEventType::DamageApplied) {
            saw_damage = true;
            assert(event.unit == attacker && event.target == target && event.amount == 10.f);
        }
        if (event.type == AoeActionEventType::DeathStarted) saw_death = true;
    }
    assert(saw_damage && saw_death);

    // Attack Move acquires within the configured awareness radius rather than
    // the weapon range, locks that target, then resumes the original goal.
    Fixture attack_move_fixture;
    const auto attack_mover = attack_move_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto attack_move_ally = attack_move_fixture.unit({1.f, 0.f}, 50.f, 1);
    const auto attack_move_enemy = attack_move_fixture.unit({6.f, 0.f}, 10.f, 2);
    assert(request_aoe_attack_move(
        attack_move_fixture.world, attack_mover, {10.f, 0.f}));
    attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().all_of<AoeAttackMoveOrder,
        AoeAttackOrder>(attack_mover));
    assert(attack_move_fixture.world.reg().all_of<AoeAttackOrder>(attack_mover));
    assert(attack_move_fixture.world.reg().get<AoeAttackOrder>(attack_mover)
               .target.entity == attack_move_enemy);
    assert(attack_move_fixture.world.reg().get<AoeAttackOrder>(attack_mover)
               .target.entity != attack_move_ally);

    Fixture target_lock_fixture;
    const auto lock_mover = target_lock_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto locked_enemy = target_lock_fixture.unit({5.f, 0.f}, 500.f, 2);
    assert(request_aoe_attack_move(
        target_lock_fixture.world, lock_mover, {12.f, 0.f}));
    target_lock_fixture.advance_ticks(1);
    assert(target_lock_fixture.world.reg().get<AoeAttackOrder>(lock_mover)
               .target.entity == locked_enemy);
    const auto closer_enemy = target_lock_fixture.unit({1.f, 0.f}, 500.f, 2);
    target_lock_fixture.advance_ticks(1);
    assert(target_lock_fixture.world.reg().get<AoeAttackOrder>(lock_mover)
               .target.entity == locked_enemy);
    target_lock_fixture.world.reg().get<AoeHealth>(locked_enemy).current = 0.f;
    target_lock_fixture.advance_ticks(1);
    assert(target_lock_fixture.world.reg().get<AoeAttackOrder>(lock_mover)
               .target.entity == closer_enemy);

    // One stalled fixed tick permits an enemy already inside the real weapon
    // range to preempt a farther locked Attack Move target. An enemy merely
    // inside acquisition range does not preempt, and no suspended target is
    // restored after the opportunistic enemy dies.
    Fixture stalled_target_fixture;
    const auto stalled_mover = stalled_target_fixture.unit(
        {0.f, 0.f}, 50.f, 1);
    const auto stalled_primary = stalled_target_fixture.unit(
        {5.5f, 0.f}, 500.f, 2);
    assert(request_aoe_attack_move(
        stalled_target_fixture.world, stalled_mover, {12.f, 0.f}));
    stalled_target_fixture.advance_ticks(1);
    assert(stalled_target_fixture.world.reg().get<AoeAttackOrder>(
               stalled_mover).target.entity == stalled_primary);
    const glm::vec2 stalled_position = stalled_target_fixture.world.reg()
        .get<AoePosition>(stalled_mover).value;
    const auto outside_weapon_range = stalled_target_fixture.unit(
        stalled_position + glm::vec2{4.6f, 0.f}, 500.f, 2);
    stalled_target_fixture.world.reg().get<AoeLocomotionState>(
        stalled_mover).stalled_ticks = 1;
    stalled_target_fixture.advance_ticks(1);
    assert(stalled_target_fixture.world.reg().get<AoeAttackOrder>(
               stalled_mover).target.entity == stalled_primary);
    const auto nearby_enemy = stalled_target_fixture.unit(
        stalled_target_fixture.world.reg().get<AoePosition>(
            stalled_mover).value + glm::vec2{.6f, 0.f}, 500.f, 2);
    stalled_target_fixture.world.reg().get<AoeLocomotionState>(
        stalled_mover).stalled_ticks = 1;
    stalled_target_fixture.advance_ticks(1);
    assert(stalled_target_fixture.world.reg().get<AoeAttackOrder>(
               stalled_mover).target.entity == nearby_enemy);
    assert(stalled_target_fixture.world.reg().all_of<AoeAttackMoveOrder>(
        stalled_mover));
    stalled_target_fixture.world.reg().get<AoeHealth>(nearby_enemy).current = 0.f;
    stalled_target_fixture.advance_ticks(1);
    assert(stalled_target_fixture.world.reg().get<AoeAttackOrder>(
               stalled_mover).target.entity == outside_weapon_range);
    assert(stalled_target_fixture.world.reg().get<AoeAttackOrder>(
               stalled_mover).target.entity != stalled_primary);

    const auto unreachable_enemy = target_lock_fixture.unit({3.f, 0.f}, 500.f, 2);
    target_lock_fixture.world.reg().get<AoePosition>(closer_enemy).value = {20.f, 0.f};
    for (int i = 0; i < 10 &&
         target_lock_fixture.world.reg().get<AoeAttackOrder>(lock_mover)
             .target.entity != unreachable_enemy; ++i)
        target_lock_fixture.advance_ticks(1);
    assert(target_lock_fixture.world.reg().get<AoeAttackOrder>(lock_mover)
               .target.entity == unreachable_enemy);
    Fixture unreachable_fixture;
    const auto unreachable_mover = unreachable_fixture.unit(
        {0.f, 0.f}, 50.f, 1);
    const auto unreachable_target = unreachable_fixture.unit(
        {5.5f, 0.f}, 500.f, 2);
    assert(request_aoe_attack_move(
        unreachable_fixture.world, unreachable_mover, {12.f, 0.f}));
    unreachable_fixture.advance_ticks(1);
    assert(unreachable_fixture.world.reg().get<AoeAttackOrder>(
               unreachable_mover).target.entity == unreachable_target);
    auto& unreachable_path = unreachable_fixture.world.reg()
        .get<AoeNavigationPath>(unreachable_mover);
    unreachable_path.no_path = true;
    unreachable_fixture.world.reg().get<AoeEngagementApproach>(
        unreachable_mover).unreachable_ticks = unreachable_fixture.world
            .resource<AoeNavigationSettings>().blocked_repath_ticks - 1;
    const auto reachable_alternative = unreachable_fixture.unit(
        {2.f, 1.f}, 500.f, 2);
    unreachable_fixture.advance_ticks(1);
    assert(unreachable_fixture.world.reg().get<AoeAttackOrder>(
               unreachable_mover)
               .target.entity == reachable_alternative);
    for (int i = 0; i < 100 &&
         attack_move_fixture.world.reg().get<AoeHealth>(attack_move_enemy).current > 0.f;
         ++i)
        attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().get<AoeHealth>(attack_move_enemy).current == 0.f);
    attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().all_of<AoeAttackMoveOrder,
        AoeMoveGoal, AoeNavigationPath>(attack_mover));
    assert(!attack_move_fixture.world.reg().all_of<AoeAttackOrder>(attack_mover));
    const float resume_x = attack_move_fixture.world.reg()
        .get<AoePosition>(attack_mover).value.x;
    attack_move_fixture.advance_ticks(3);
    assert(attack_move_fixture.world.reg().get<AoePosition>(attack_mover).value.x >
           resume_x);

    const auto second_enemy_position = attack_move_fixture.world.reg()
        .get<AoePosition>(attack_mover).value + glm::vec2{2.f, 0.f};
    const auto second_enemy = attack_move_fixture.unit(
        second_enemy_position, 10.f, 2);
    attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().all_of<AoeAttackOrder>(attack_mover));
    assert(attack_move_fixture.world.reg().get<AoeAttackOrder>(attack_mover)
               .target.entity == second_enemy);
    assert(request_aoe_move(attack_move_fixture.world, attack_mover, {9.f, 1.f}));
    attack_move_fixture.advance_ticks(1);
    assert(!attack_move_fixture.world.reg().any_of<AoeAttackMoveOrder,
        AoeAttackOrder>(attack_mover));
    assert(attack_move_fixture.world.reg().all_of<AoeMoveGoal>(attack_mover));
    assert(request_aoe_attack_move(
        attack_move_fixture.world, attack_mover, {10.f, 0.f}));
    attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().all_of<AoeAttackMoveOrder>(attack_mover));
    assert(request_aoe_attack(
        attack_move_fixture.world, attack_mover, second_enemy));
    attack_move_fixture.advance_ticks(1);
    assert(!attack_move_fixture.world.reg().all_of<AoeAttackMoveOrder>(attack_mover));
    assert(attack_move_fixture.world.reg().all_of<AoeAttackOrder>(attack_mover));
    assert(request_aoe_attack_move(
        attack_move_fixture.world, attack_mover, {10.f, 0.f}));
    attack_move_fixture.advance_ticks(1);
    assert(request_aoe_stop(attack_move_fixture.world, attack_mover));
    attack_move_fixture.advance_ticks(1);
    assert(!attack_move_fixture.world.reg().any_of<AoeAttackMoveOrder,
        AoeAttackOrder, AoeMoveGoal, AoeNavigationPath>(attack_mover));

    Fixture arrival_fixture;
    const auto arrival_mover = arrival_fixture.unit({0.f, 0.f}, 50.f, 1);
    arrival_fixture.unit({.5f, 0.f}, 50.f, 1);
    assert(request_aoe_attack_move(arrival_fixture.world, arrival_mover, {1.f, 0.f}));
    arrival_fixture.advance_ticks(20);
    assert(!arrival_fixture.world.reg().any_of<AoeAttackMoveOrder,
        AoeAttackOrder, AoeMoveGoal, AoeNavigationPath>(arrival_mover));
    assert(arrival_fixture.world.reg().get<AoeActionState>(arrival_mover).state ==
           UnitState::Idle);
    assert(glm::length(arrival_fixture.world.reg().get<AoePosition>(arrival_mover).value -
                       glm::vec2{1.f, 0.f}) < 1e-5f);

    Fixture no_attack_fixture;
    auto* no_attack_definition = const_cast<AoeUnitDefinition*>(
        no_attack_fixture.definition.get());
    no_attack_definition->attack.reset();
    const auto no_attack_unit = no_attack_fixture.unit({0.f, 0.f}, 50.f, 1);
    assert(request_aoe_attack_move(
        no_attack_fixture.world, no_attack_unit, {2.f, 0.f}));
    no_attack_fixture.advance_ticks(1);
    assert(!no_attack_fixture.world.reg().all_of<AoeAttackMoveOrder>(no_attack_unit));

    // The target runs death -> disappear -> recycle and the entity itself is
    // retained in the pool. Reuse increments its incarnation id.
    const auto old_identity = fixture.world.reg().get<AoeGameplayIdentity>(target).instance_id;
    fixture.advance_ticks(4);
    assert(fixture.world.reg().all_of<AoeRecyclePending>(target));
    const auto lifecycle_events = fixture.world.resource<Events<AoeActionEvent>>().read();
    auto death_index = lifecycle_events.size();
    auto disappear_index = lifecycle_events.size();
    auto recycle_index = lifecycle_events.size();
    for (std::size_t i = 0; i < lifecycle_events.size(); ++i) {
        if (lifecycle_events[i].unit != target) continue;
        if (lifecycle_events[i].type == AoeActionEventType::DeathStarted) death_index = i;
        if (lifecycle_events[i].type == AoeActionEventType::DisappearStarted) disappear_index = i;
        if (lifecycle_events[i].type == AoeActionEventType::RecycleRequested) recycle_index = i;
    }
    assert(death_index < disappear_index && disappear_index < recycle_index);
    assert(lifecycle_events[disappear_index].tick - lifecycle_events[death_index].tick == 2);
    assert(lifecycle_events[recycle_index].tick - lifecycle_events[disappear_index].tick == 2);
    fixture.world.reg().emplace<AoeMapStaticObstacle>(target);
    fixture.world.reg().emplace<AoeEngagementApproach>(target);
    fixture.world.reg().emplace<AoeMovementIntent>(target);
    fixture.world.reg().emplace<AoePathMotionRequest>(target);
    fixture.world.reg().emplace<AoeUnitMovementIntentState>(target);
    fixture.world.reg().emplace<AoeGlobalMotionState>(target);
    fixture.world.reg().emplace<AoeGlobalMotionDecision>(target);
    aoe_gameplay_recycle_system(fixture.world);
    assert(fixture.world.reg().valid(target) && fixture.world.reg().all_of<AoePooledUnit>(target));
    assert(!fixture.world.reg().all_of<AoePositionHistory>(target));
    assert(!fixture.world.reg().all_of<AoeLocomotionState>(target));
    assert(!fixture.world.reg().all_of<AoeMapStaticObstacle>(target));
    assert(!fixture.world.reg().all_of<AoeEngagementApproach>(target));
    assert(!fixture.world.reg().all_of<AoeMovementIntent>(target));
    assert(!fixture.world.reg().all_of<AoePathMotionRequest>(target));
    assert(!fixture.world.reg().all_of<AoeUnitMovementIntentState>(target));
    assert(!fixture.world.reg().all_of<AoeGlobalMotionState>(target));
    assert(!fixture.world.reg().all_of<AoeGlobalMotionDecision>(target));
    assert(fixture.world.resource<AoeGameplayPool>().available.size() == 1);

    AoeUnitSpawnOptions reuse_options;
    reuse_options.definition_id = "test";
    reuse_options.team_id = 7;
    reuse_options.position = {3.f, -2.f};
    const auto reused = spawn_aoe_gameplay_unit(fixture.world, reuse_options);
    assert(reused == target);
    spawn_aoe_gameplay_unit_system(fixture.world);
    assert(!fixture.world.reg().all_of<AoeGameplaySpawnRequest>(reused));
    assert(fixture.world.reg().get<AoePosition>(reused).value == glm::vec2(3.f, -2.f));
    assert(fixture.world.reg().get<AoePositionHistory>(reused).previous ==
           glm::vec2(3.f, -2.f));
    assert(fixture.world.reg().get<AoeTeam>(reused).id == 7);
    const auto& reused_motion = fixture.world.reg()
        .get<AoeLocomotionState>(reused);
    assert(reused_motion.velocity == glm::vec2(0.f));
    assert(reused_motion.distance_travelled == 0.0);
    assert(fixture.world.reg().get<AoeGameplayIdentity>(reused).instance_id != old_identity);

    AoeGameplayCommand stale{AoeCommandType::AttackTarget, attacker};
    stale.target = {reused, old_identity};
    fixture.world.resource<AoeGameplayCommands>().queue.push_back(stale);
    const auto rejected_before = fixture.world.resource<AoeGameplayDiagnostics>().commands_rejected;
    fixture.advance_ticks(1);
    assert(fixture.world.resource<AoeGameplayDiagnostics>().commands_rejected == rejected_before + 1);

    // Move and stop are explicit commands and replace an existing attack order.
    assert(request_aoe_attack(fixture.world, attacker, reused));
    fixture.advance_ticks(1);
    assert(fixture.world.reg().all_of<AoeAttackOrder>(attacker));
    assert(request_aoe_move(fixture.world, attacker, {-3.f, 1.f}));
    fixture.advance_ticks(1);
    assert(!fixture.world.reg().all_of<AoeAttackOrder>(attacker));
    assert(fixture.world.reg().all_of<AoeMoveGoal>(attacker));
    assert(request_aoe_stop(fixture.world, attacker));
    fixture.advance_ticks(1);
    assert(!fixture.world.reg().all_of<AoeMoveGoal>(attacker));
    assert(fixture.world.reg().get<AoeActionState>(attacker).state == UnitState::Idle);

    // Regression: a handle is false while Loading. Spawn must not report it as
    // an unknown definition until the asynchronous load actually completes.
    EcsWorld async_world;
    auto async_fs = std::make_shared<MemoryFileSystem>();
    auto camel_definition = definition_json();
    camel_definition["id"] = "camel_scout";
    async_fs->texts["units/camel_scout.json"] = camel_definition.dump();
    auto invalid_definition = definition_json();
    invalid_definition["id"] = "invalid";
    invalid_definition["max_hp"] = -1.0;
    async_fs->texts["units/invalid.json"] = invalid_definition.dump();
    auto& async_server = async_world.add_resource<AssetServer>();
    async_server.world = &async_world;
    async_server.fs = async_fs;
    async_server.register_loader<AoeUnitDefinitionDesc>(
        std::make_shared<AoeUnitDefinitionLoader>());
    auto& async_manager = async_world.add_resource<AoeUnitDefinitionManager>(
        async_server, "units");
    async_manager.refresh();
    async_world.add_resource<AoeGameplaySettings>();
    async_world.add_resource<AoeGameplayClock>();
    async_world.add_resource<AoeGameplayLifecycle>();
    async_world.add_resource<AoeGameplayPool>();

    AoeUnitSpawnOptions camel_options;
    camel_options.definition_id = "camel_scout";
    camel_options.position = {1.f, 2.f};
    const auto asynchronous = spawn_aoe_gameplay_unit(async_world, camel_options);
    spawn_aoe_gameplay_unit_system(async_world);
    assert(async_world.reg().all_of<AoeGameplaySpawnRequest>(asynchronous));
    assert(!async_world.reg().all_of<AoeGameplaySpawnError>(asynchronous));
    assert(async_world.reg().get<AoeGameplaySpawnRequest>(asynchronous)
               .definition.state() == LoadState::Loading);

    auto pump_spawn = [&](entt::entity entity) {
        for (int attempt = 0; attempt < 1000 &&
             async_world.reg().all_of<AoeGameplaySpawnRequest>(entity); ++attempt) {
            asset_update_system(async_world);
            spawn_aoe_gameplay_unit_system(async_world);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    };
    pump_spawn(asynchronous);
    assert((async_world.reg().all_of<AoeUnitDefinitionRef, AoeHealth, AoePosition,
                                     AoeMovement, AoeGameplayIdentity>(asynchronous)));
    assert(async_world.reg().get<AoePosition>(asynchronous).value == glm::vec2(1.f, 2.f));

    const auto unknown = spawn_aoe_gameplay_unit(
        async_world, AoeUnitSpawnOptions{.definition_id = "missing"});
    spawn_aoe_gameplay_unit_system(async_world);
    assert(async_world.reg().get<AoeGameplaySpawnError>(unknown).message ==
           "unknown definition: missing");

    const auto invalid = spawn_aoe_gameplay_unit(
        async_world, AoeUnitSpawnOptions{.definition_id = "invalid"});
    spawn_aoe_gameplay_unit_system(async_world);
    pump_spawn(invalid);
    assert(async_world.reg().get<AoeGameplaySpawnError>(invalid).message ==
           "definition load failed: invalid");
    async_server.shutdown();
    return 0;
}
