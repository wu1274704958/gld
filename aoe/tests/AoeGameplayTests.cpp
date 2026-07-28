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
    std::abort(); } } while (false)

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
    static std::vector<AoeFormationSlot> layout(const AoeFormationContext&) {
        return {};
    }
};

struct KnownLocalSteering {
    static inline std::uint32_t calls = 0;
    static inline glm::vec2 velocity{0.f};

    static AoeSteeringResult steer(const AoeSteeringContext&) {
        ++calls;
        return AoeSteeringResult{.target_velocity = velocity};
    }
};

struct ContactLocalSteering {
    static AoeSteeringResult steer(const AoeSteeringContext& context) {
        return AoeSteeringResult{.target_velocity =
            context.position.x < 4.2f
                ? glm::vec2{1.f, 0.f} : glm::vec2{-1.f, 0.f}};
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
            aoe_gameplay_fixed_system(world);
        }
    }
};
} // namespace

int main() {
    const auto parsed = parse(definition_json());
    assert(parsed && parsed->id == "test" && parsed->level == 2);
    assert(parsed->movement.speed == 2.f);
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

    AoeSteeringRegistry steering_registry;
    steering_registry.bind<DefaultLocalSteeringLogic>("local_default");
    assert(steering_registry.contains("local_default"));
    const AoeSteeringContext unobstructed_steering{
        .instance_id = 1,
        .preferred_velocity = {2.f, 0.f},
        .goal = {10.f, 0.f},
        .max_speed = 2.f};
    assert(glm::length(steering_registry.steer(
               "local_default", unobstructed_steering).target_velocity -
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
    const auto head_on_result = steering_registry.steer(
        "local_default", head_on_steering);
    assert(std::abs(head_on_result.target_velocity.y) > .1f);
    assert(head_on_result.target_velocity == steering_registry.steer(
        "local_default", head_on_steering).target_velocity);
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
    const auto reverse_result = steering_registry.steer(
        "local_default", reverse_head_on);
    assert(head_on_result.target_velocity.y *
               reverse_result.target_velocity.y < 0.f);
    head_on_steering.preferred_avoidance_side =
        head_on_result.avoidance_side;
    head_on_steering.side_switch_margin = 2.35f;
    auto perturbed_neighbor = head_on_neighbor;
    perturbed_neighbor[0].position.y = .01f;
    head_on_steering.neighbors = perturbed_neighbor;
    const auto held_side_result = steering_registry.steer(
        "local_default", head_on_steering);
    assert(held_side_result.avoidance_side ==
           head_on_result.avoidance_side);

    // Escape steering expands the candidate fan to a wall tangent without
    // introducing a backward direction.
    AoeLogicMap tangent_map(flat_map());
    AoeStaticObstacleDesc tangent_wall;
    tangent_wall.shape = AoeStaticObstacleShape::Aabb;
    tangent_wall.center = {2.65f, 6.f};
    tangent_wall.half_extents = {.25f, 1.2f};
    tangent_map.add_static_obstacle(tangent_wall);
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
    const auto tangent_result = steering_registry.steer(
        "local_default", tangent_context);
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
    const auto formation_slots = TestSquareFormation::layout(formation_context);
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

    AoeFormationRegistry formation_registry;
    formation_registry.bind<AoeFormationType::Skirmish, TestSquareFormation>();
    assert(formation_registry.contains(AoeFormationType::Skirmish));
    assert(formation_registry.layout(
        AoeFormationType::Skirmish, formation_context).size() == 4);
    bool duplicate_formation_rejected = false;
    try {
        formation_registry.bind<AoeFormationType::Skirmish,
                                DefaultSkirmishFormation>();
    } catch (const std::invalid_argument&) {
        duplicate_formation_rejected = true;
    }
    assert(duplicate_formation_rejected);

    const auto schema1 = parse(definition_json(1));
    assert(schema1 && schema1->movement.speed == 1.f);
    assert(!schema1->lifecycle.recycle_after_death);
    auto invalid_speed = definition_json();
    invalid_speed["movement"]["speed"] = 0.0;
    assert(!parse(invalid_speed));
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
    aoe_gameplay_fixed_system(history_fixture.world);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePositionHistory>(interpolated_mover).previous -
           glm::vec2(.6f, 0.f)) < 1e-5f);
    assert(glm::length(history_fixture.world.reg()
               .get<AoePosition>(interpolated_mover).value -
           glm::vec2(.8f, 0.f)) < 1e-5f);

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
    movement_map.add_static_obstacle(movement_wall);
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
    const auto sealed_id = sealed_map.add_static_obstacle(sealed_wall);
    const auto sealed_mover = sealed_map_fixture.unit({2.f, 6.f}, 50.f, 1);
    assert(request_aoe_move(sealed_map_fixture.world, sealed_mover, {10.f, 6.f}));
    sealed_map_fixture.advance_ticks(1);
    assert(sealed_map_fixture.world.reg().get<AoeNavigationPath>(
               sealed_mover).no_path);
    assert(sealed_map_fixture.world.reg().get<AoeActionState>(
               sealed_mover).state == UnitState::Idle);
    assert(sealed_map.remove_static_obstacle(sealed_id));
    sealed_map_fixture.advance_ticks(60);
    assert(glm::length(sealed_map_fixture.world.reg()
               .get<AoePosition>(sealed_mover).value - glm::vec2(10.f, 6.f)) < .05f);

    // A dynamic unit blocks the direct step. After the wait threshold, the
    // mover replans through the spatial index and passes around it.
    Fixture dynamic_map_fixture;
    dynamic_map_fixture.world.add_resource<AoeLogicMap>(flat_map());
    const auto dynamic_mover = dynamic_map_fixture.unit({2.f, 4.f}, 50.f, 1);
    const auto dynamic_blocker = dynamic_map_fixture.unit({5.f, 4.f}, 50.f, 2);
    (void)dynamic_blocker;
    assert(request_aoe_move(dynamic_map_fixture.world,
                            dynamic_mover, {9.f, 4.f}));
    dynamic_map_fixture.advance_ticks(70);
    assert(glm::length(dynamic_map_fixture.world.reg()
               .get<AoePosition>(dynamic_mover).value - glm::vec2(9.f, 4.f)) < .1f);
    assert(dynamic_map_fixture.world.resource<AoeDynamicObstacleIndex>()
               .diagnostics().units_indexed == 2);

    // A short dynamic contact is handled by local steering instead of
    // invalidating the global path on the first blocked fixed tick.
    Fixture immediate_repath_fixture;
    immediate_repath_fixture.world.add_resource<AoeLogicMap>(flat_map());
    const auto immediate_mover = immediate_repath_fixture.unit({2.f, 4.f}, 50.f, 1);
    const auto immediate_blocker = immediate_repath_fixture.unit({2.8f, 4.f}, 50.f, 2);
    (void)immediate_blocker;
    assert(request_aoe_move(immediate_repath_fixture.world,
                            immediate_mover, {8.f, 4.f}));
    bool requested_dynamic_repath = false;
    for (int i = 0; i < 8; ++i) {
        immediate_repath_fixture.advance_ticks(1);
        const auto* path = immediate_repath_fixture.world.reg()
            .try_get<AoeNavigationPath>(immediate_mover);
        requested_dynamic_repath = requested_dynamic_repath ||
            (path && path->include_dynamic_obstacles);
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
    cached_steering_fixture.world.resource<AoeNavigationSettings>()
        .steering_imminent_collision_seconds = 0.f;
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

    // The local solver owns the direction/length intent. Global planning is
    // the next consumer and movement must not invoke local steering again.
    Fixture pipeline_fixture;
    pipeline_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& pipeline_navigation =
        pipeline_fixture.world.resource<AoeNavigationSettings>();
    pipeline_navigation.steering_strategy_id = "known_test";
    pipeline_navigation.unit_flow_enabled = false;
    pipeline_fixture.world.resource_or_add<AoeSteeringRegistry>()
        .bind<KnownLocalSteering>("known_test");
    KnownLocalSteering::calls = 0;
    KnownLocalSteering::velocity = {.75f, 1.25f};
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
    assert(KnownLocalSteering::calls == 1);
    assert(glm::length(pipeline_request.velocity - glm::vec2(2.f, 0.f)) <
           1e-5f);
    assert(glm::length(pipeline_intent.velocity -
                       KnownLocalSteering::velocity) < 1e-5f);
    assert(glm::length(pipeline_decision.velocity -
                       pipeline_intent.velocity) < 1e-5f);
    assert(glm::length(pipeline_fixture.world.reg()
               .get<AoeLocomotionState>(pipeline_unit).velocity -
           pipeline_decision.velocity) < 1e-5f);

    // The last safety stage may shorten the displacement, but it cannot
    // rotate the direction selected by global planning.
    Fixture safety_fixture;
    safety_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& safety_navigation =
        safety_fixture.world.resource<AoeNavigationSettings>();
    safety_navigation.steering_strategy_id = "known_test";
    safety_navigation.unit_flow_enabled = false;
    safety_fixture.world.resource_or_add<AoeSteeringRegistry>()
        .bind<KnownLocalSteering>("known_test");
    AoeStaticObstacleDesc safety_wall;
    safety_wall.shape = AoeStaticObstacleShape::Aabb;
    safety_wall.center = {2.35f, 2.f};
    safety_wall.half_extents = {.05f, 1.f};
    safety_fixture.world.resource<AoeLogicMap>()
        .add_static_obstacle(safety_wall);
    KnownLocalSteering::calls = 0;
    KnownLocalSteering::velocity = {2.f, 0.f};
    const auto safety_unit = safety_fixture.unit({2.f, 2.f}, 50.f, 1);
    assert(request_aoe_move(safety_fixture.world,
                            safety_unit, {10.f, 2.f}));
    safety_fixture.advance_ticks(1);
    const auto& safety_decision = safety_fixture.world.reg()
        .get<AoeGlobalMotionDecision>(safety_unit);
    const auto& safety_locomotion = safety_fixture.world.reg()
        .get<AoeLocomotionState>(safety_unit);
    assert(safety_decision.static_safe_fraction < 1.f);
    assert(safety_decision.velocity.x > 0.f &&
           std::abs(safety_decision.velocity.y) < 1e-5f);
    assert(safety_locomotion.velocity.x >= 0.f &&
           std::abs(safety_locomotion.velocity.y) < 1e-5f);

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
    Fixture contact_flow_fixture;
    contact_flow_fixture.world.add_resource<AoeLogicMap>(flat_map());
    auto& contact_settings =
        contact_flow_fixture.world.resource<AoeNavigationSettings>();
    contact_settings.unit_flow_enabled = true;
    contact_settings.steering_strategy_id = "contact_test";
    contact_flow_fixture.world.resource_or_add<AoeSteeringRegistry>()
        .bind<ContactLocalSteering>("contact_test");
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
        .add_static_obstacle(corridor_wall);
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
    backing_flow_fixture.world.reg().get<AoeLocomotionState>(
        backing_yielder).local_avoidance_infeasible = true;
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
    assert(squad_fixture.world.reg().get<AoeSquadFormation>(squad).slots.size() == 4);
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
        .add_static_obstacle(squad_wall);
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

    // The preview-sized route remains stable with a 64-member formation. The
    // anchor follows its static guide while members independently split around
    // the two barriers and eventually converge at the destination.
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
    stress_squad_fixture.advance_ticks(800);
    assert(stress_squad_fixture.world.reg().get<AoeSquadState>(stress_squad)
               .phase == AoeSquadPhase::Idle);
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
        .resource<AoeLogicMap>().add_static_obstacle(squad_wall);
    assert(request_aoe_squad_move(
        blocked_squad_fixture.world, blocked_squad, {16.f, 6.f}));
    blocked_squad_fixture.advance_ticks(1);
    assert(blocked_squad_fixture.world.reg().get<AoeSquadState>(blocked_squad)
               .phase == AoeSquadPhase::Blocked);
    assert(blocked_squad_fixture.world.resource<AoeLogicMap>()
               .remove_static_obstacle(blocking_wall));
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
