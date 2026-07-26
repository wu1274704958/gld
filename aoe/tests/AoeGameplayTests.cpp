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

struct EmptyAcquisitionStrategy {
    static std::optional<AoeUnitTarget> select(
        const EcsWorld&, const AoeTargetAcquisitionContext&) {
        return std::nullopt;
    }
};

inline constexpr auto InvalidFormationType =
    static_cast<AoeFormationType>(99);

struct InvalidFormation {
    static std::vector<AoeFormationSlot> layout(const AoeFormationContext&) {
        return {};
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
        world.resource_or_add<AoeTargetAcquisitionRegistry>()
            .bind<NearestEnemyAcquisitionStrategy>("nearest_enemy");
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
    assert(parsed->target_acquisition.strategy_id == "nearest_enemy");
    assert(parsed->target_acquisition.radius == 6.f);
    assert(parsed->attack->projectile_launch_offset ==
           std::optional<glm::vec3>(glm::vec3(0.f, .5f, 1.5f)));

    AoeProjectileRegistry registry;
    registry.bind<ArrowProjectileLogic>("arrow");
    assert(registry.contains("arrow") && !registry.contains("missing"));
    bool duplicate_binding_rejected = false;
    try { registry.bind<ArrowProjectileLogic>("arrow"); }
    catch (const std::invalid_argument&) { duplicate_binding_rejected = true; }
    assert(duplicate_binding_rejected);

    AoeTargetAcquisitionRegistry acquisition_registry;
    acquisition_registry.bind<NearestEnemyAcquisitionStrategy>("nearest_enemy");
    acquisition_registry.bind<EmptyAcquisitionStrategy>("empty");
    assert(acquisition_registry.contains("nearest_enemy"));
    assert(acquisition_registry.contains("empty"));
    assert(!acquisition_registry.contains("missing"));
    bool duplicate_acquisition_rejected = false;
    try {
        acquisition_registry.bind<EmptyAcquisitionStrategy>("empty");
    } catch (const std::invalid_argument&) {
        duplicate_acquisition_rejected = true;
    }
    assert(duplicate_acquisition_rejected);

    auto explicit_acquisition = definition_json();
    explicit_acquisition["target_acquisition"] = {
        {"strategy_id", "empty"}, {"radius", 2.5}};
    const auto explicit_parsed = parse(explicit_acquisition);
    assert(explicit_parsed->target_acquisition.strategy_id == "empty");
    assert(explicit_parsed->target_acquisition.radius == 2.5f);
    auto empty_acquisition_id = explicit_acquisition;
    empty_acquisition_id["target_acquisition"]["strategy_id"] = "";
    assert(!parse(empty_acquisition_id));
    auto negative_acquisition_radius = explicit_acquisition;
    negative_acquisition_radius["target_acquisition"]["radius"] = -1.0;
    assert(!parse(negative_acquisition_radius));
    auto infinite_acquisition_radius = explicit_acquisition;
    infinite_acquisition_radius["target_acquisition"]["radius"] =
        std::numeric_limits<double>::infinity();
    assert(!parse(infinite_acquisition_radius));
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

    // The default strategy ignores self/allies/terminal units, compares
    // collider surface gaps, and returns a stable incarnation-aware target.
    Fixture acquisition_fixture;
    const auto seeker = acquisition_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto ally = acquisition_fixture.unit({1.f, 0.f}, 50.f, 1);
    const auto center_near = acquisition_fixture.unit({4.f, 0.f}, 50.f, 2);
    const auto surface_near = acquisition_fixture.unit({4.5f, 0.f}, 50.f, 2);
    acquisition_fixture.world.reg().get<AoeCollider>(surface_near).radius_x = 2.f;
    auto acquired = acquisition_fixture.world.resource<AoeTargetAcquisitionRegistry>().select(
        "nearest_enemy", acquisition_fixture.world,
        {seeker, {0.f, 0.f}, 10.f, 1});
    assert(acquired && acquired->entity == surface_near);
    acquisition_fixture.world.reg().get<AoeHealth>(surface_near).current = 0.f;
    acquired = acquisition_fixture.world.resource<AoeTargetAcquisitionRegistry>().select(
        "nearest_enemy", acquisition_fixture.world,
        {seeker, {0.f, 0.f}, 10.f, 1});
    assert(acquired && acquired->entity == center_near);
    assert(acquired->entity != ally);
    acquired = acquisition_fixture.world.resource<AoeTargetAcquisitionRegistry>().select(
        "nearest_enemy", acquisition_fixture.world,
        {seeker, {0.f, 0.f}, .1f, 1});
    assert(!acquired);

    Fixture tie_fixture;
    const auto tie_seeker = tie_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto first_tie = tie_fixture.unit({3.f, 0.f}, 50.f, 2);
    tie_fixture.unit({3.f, 0.f}, 50.f, 2);
    acquired = tie_fixture.world.resource<AoeTargetAcquisitionRegistry>().select(
        "nearest_enemy", tie_fixture.world,
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
    assert(std::abs(squad_fixture.world.reg().get<AoePosition>(squad).value.x -
                    center_before - .1f) < 1e-5f);
    assert(squad_fixture.world.reg().get<AoeSquadState>(squad).movement_speed == 1.f);

    const auto detached = squad_fixture.world.reg()
        .get<AoeSquadMembers>(squad).active.back().entity;
    assert(request_aoe_move(squad_fixture.world, detached, {-2.f, 1.f}));
    squad_fixture.advance_ticks(1);
    assert(!squad_fixture.world.reg().all_of<AoeSquadMember>(detached));
    assert(squad_fixture.world.reg().get<AoeSquadMembers>(squad).active.size() == 3);

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
    const auto shared_target = squad_combat_fixture.world.reg()
        .get<AoeSquadOrder>(attackers).target;
    assert(shared_target.entity == shared_enemy);
    for (const auto& member : squad_combat_fixture.world.reg()
             .get<AoeSquadMembers>(attackers).active) {
        assert(squad_combat_fixture.world.reg().all_of<AoeAttackOrder>(member.entity));
        assert(squad_combat_fixture.world.reg().get<AoeAttackOrder>(member.entity)
                   .target.entity == shared_enemy);
    }
    for (int i = 0; i < 100 && squad_combat_fixture.world.reg()
             .get<AoeHealth>(shared_enemy).current > 0.f; ++i)
        squad_combat_fixture.advance_ticks(1);
    assert(squad_combat_fixture.world.reg().get<AoeHealth>(shared_enemy).current == 0.f);
    squad_combat_fixture.advance_ticks(1);
    assert(squad_combat_fixture.world.reg().get<AoeSquadOrder>(attackers)
               .target.entity == next_shared_enemy);
    assert(squad_combat_fixture.world.reg().get<AoeSquadState>(attackers).phase ==
           AoeSquadPhase::Engaging);
    for (const auto& member : squad_combat_fixture.world.reg()
             .get<AoeSquadMembers>(attackers).active) {
        assert(squad_combat_fixture.world.reg().all_of<AoeAttackOrder>(member.entity));
        assert(squad_combat_fixture.world.reg().get<AoeAttackOrder>(member.entity)
                   .target.entity == next_shared_enemy);
    }

    for (int i = 0; i < 100 && squad_combat_fixture.world.reg()
             .get<AoeHealth>(next_shared_enemy).current > 0.f; ++i)
        squad_combat_fixture.advance_ticks(1);
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
    explicit_squad_options.composition = {{"test", 1, 1}};
    explicit_squad_options.team_id = 1;
    const auto explicit_squad = spawn_aoe_gameplay_squad(
        explicit_squad_fixture.world, explicit_squad_options);
    spawn_aoe_gameplay_unit_system(explicit_squad_fixture.world);
    explicit_squad_fixture.advance_ticks(1);
    const auto explicit_enemy = explicit_squad_fixture.unit({2.f, 0.f}, 1.f, 2);
    const auto ignored_enemy = explicit_squad_fixture.unit({3.f, 0.f}, 50.f, 2);
    assert(request_aoe_squad_attack(
        explicit_squad_fixture.world, explicit_squad, explicit_enemy));
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
        direction_fixture.advance_ticks(1);
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
        direction_fixture.advance_ticks(1);
        assert(direction_fixture.world.reg().get<AoeFacing>(mover).direction == expected);
    }
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

    // Attack Move acquires a hostile unit while travelling, ignores allies,
    // keeps attacking until it dies, then resumes the original destination.
    Fixture attack_move_fixture;
    const auto attack_mover = attack_move_fixture.unit({0.f, 0.f}, 50.f, 1);
    const auto attack_move_ally = attack_move_fixture.unit({1.f, 0.f}, 50.f, 1);
    const auto attack_move_enemy = attack_move_fixture.unit({6.f, 0.f}, 10.f, 2);
    assert(request_aoe_attack_move(
        attack_move_fixture.world, attack_mover, {10.f, 0.f}));
    attack_move_fixture.advance_ticks(1);
    assert(attack_move_fixture.world.reg().all_of<AoeAttackMoveOrder,
        AoeAttackOrder, AoeMoveGoal>(attack_mover));
    assert(attack_move_fixture.world.reg().get<AoeAttackOrder>(attack_mover)
               .target.entity == attack_move_enemy);
    assert(attack_move_fixture.world.reg().get<AoeAttackOrder>(attack_mover)
               .target.entity != attack_move_ally);
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
    arrival_fixture.advance_ticks(10);
    assert(!arrival_fixture.world.reg().any_of<AoeAttackMoveOrder,
        AoeAttackOrder, AoeMoveGoal, AoeNavigationPath>(arrival_mover));
    assert(arrival_fixture.world.reg().get<AoeActionState>(arrival_mover).state ==
           UnitState::Idle);
    assert(glm::length(arrival_fixture.world.reg().get<AoePosition>(arrival_mover).value -
                       glm::vec2{1.f, 0.f}) < 1e-5f);

    Fixture invalid_strategy_fixture;
    auto* invalid_strategy_definition = const_cast<AoeUnitDefinition*>(
        invalid_strategy_fixture.definition.get());
    invalid_strategy_definition->target_acquisition.strategy_id = "missing";
    const auto invalid_strategy_unit = invalid_strategy_fixture.unit(
        {0.f, 0.f}, 50.f, 1);
    const auto rejected_attack_moves = invalid_strategy_fixture.world
        .resource<AoeGameplayDiagnostics>().commands_rejected;
    assert(request_aoe_attack_move(
        invalid_strategy_fixture.world, invalid_strategy_unit, {2.f, 0.f}));
    invalid_strategy_fixture.advance_ticks(1);
    assert(invalid_strategy_fixture.world.resource<AoeGameplayDiagnostics>()
               .commands_rejected == rejected_attack_moves + 1);
    assert(!invalid_strategy_fixture.world.reg()
        .all_of<AoeAttackMoveOrder>(invalid_strategy_unit));

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
    aoe_gameplay_recycle_system(fixture.world);
    assert(fixture.world.reg().valid(target) && fixture.world.reg().all_of<AoePooledUnit>(target));
    assert(!fixture.world.reg().all_of<AoePositionHistory>(target));
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
