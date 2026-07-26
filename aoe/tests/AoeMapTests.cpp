#include <aoe/AoeGameplay.hpp>

#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;

#undef assert
#define assert(...) do { if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "assertion failed at line %d: %s\n", __LINE__, #__VA_ARGS__); \
    std::abort(); } } while (false)

namespace {
struct MemoryFileSystem final : IFileSystem {
    std::unordered_map<std::string, std::string> texts;
    bool exists(const std::string& path) const override {
        return texts.contains(path);
    }
    std::optional<std::vector<std::byte>> read_bytes(
        const std::string&) const override { return std::nullopt; }
    std::optional<std::string> read_text(const std::string& path) const override {
        const auto it = texts.find(path);
        return it == texts.end() ? std::nullopt
                                 : std::optional<std::string>(it->second);
    }
    std::vector<FileSystemEntry> list(const std::string&) const override {
        return {};
    }
};

AoeMapDefinition make_map(std::uint32_t width = 8,
                          std::uint32_t height = 8) {
    AoeMapDefinition result;
    result.id = "test_map";
    result.origin = {0.f, 0.f};
    result.tile_size = 1.f;
    result.width = width;
    result.height = height;
    result.heights.resize(static_cast<std::size_t>(width + 1) * (height + 1));
    for (std::uint32_t y = 0; y <= height; ++y)
        for (std::uint32_t x = 0; x <= width; ++x)
            result.heights[static_cast<std::size_t>(y) * (width + 1) + x] =
                static_cast<float>(x + y * 2);
    return result;
}
} // namespace

int main() {
    auto definition = make_map();
    AoeLogicMap map(definition);
    assert(map.valid() && map.width() == 8 && map.height() == 8);
    assert(map.contains({0.f, 0.f}));
    assert(!map.contains({8.f, 8.f}));
    assert(!map.sample_height({-1.f, 0.f}));
    const auto sampled = map.sample_height({.5f, .5f});
    assert(sampled && std::abs(*sampled - 1.5f) < 1e-5f);
    const auto sampled2 = map.sample_height({2.25f, 3.75f});
    assert(sampled2 && std::abs(*sampled2 - 9.75f) < 1e-5f);

    AoeStaticObstacleDesc box;
    box.source_id = "box";
    box.shape = AoeStaticObstacleShape::Aabb;
    box.center = {4.f, 4.f};
    box.half_extents = {.5f, 1.f};
    const auto before_revision = map.static_revision();
    const auto box_id = map.add_static_obstacle(box);
    assert(box_id && map.static_revision() > before_revision);
    assert(map.position_blocked({4.f, 4.f}, {.2f, .2f}));
    assert(!map.position_blocked({2.f, 4.f}, {.2f, .2f}));
    assert(map.static_safe_fraction({2.f, 4.f}, {6.f, 4.f}, {.2f, .2f}) < 1.f);
    box.center = {6.f, 6.f};
    assert(map.update_static_obstacle(box_id, box));
    assert(!map.position_blocked({4.f, 4.f}, {.2f, .2f}));
    assert(map.remove_static_obstacle(box_id));
    assert(!map.static_obstacle(box_id));

    AoeStaticObstacleDesc circle;
    circle.source_id = "circle";
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.center = {3.f, 3.f};
    circle.radius = .5f;
    assert(map.add_static_obstacle(circle));
    assert(map.position_blocked({3.6f, 3.f}, {.2f, .2f}));

    // Obstacles entirely outside the finite map must not leak into a clamped
    // edge bucket. A unit resting on an expanded AABB boundary may slide along
    // it or move away, but may not move farther into it.
    AoeStaticObstacleDesc outside;
    outside.shape = AoeStaticObstacleShape::Aabb;
    outside.center = {-100.f, -100.f};
    outside.half_extents = {1.f, 1.f};
    assert(map.add_static_obstacle(outside));
    assert(!map.position_blocked({.2f, .2f}, {.1f, .1f}));
    AoeStaticObstacleDesc contact_box;
    contact_box.shape = AoeStaticObstacleShape::Aabb;
    contact_box.center = {4.f, 4.f};
    contact_box.half_extents = {1.f, 1.f};
    AoeLogicMap contact_map(make_map());
    assert(contact_map.add_static_obstacle(contact_box));
    assert(contact_map.static_safe_fraction(
               {2.8f, 4.f}, {2.8f, 5.f}, {.2f, .2f}) == 1.f);
    assert(contact_map.static_safe_fraction(
               {2.8f, 4.f}, {3.2f, 4.f}, {.2f, .2f}) < 1.f);
    EcsWorld contact_world;
    contact_world.add_resource<AoeLogicMap>(make_map());
    contact_world.resource<AoeLogicMap>().add_static_obstacle(contact_box);
    const auto leave_contact = GridAStarPathfinderLogic::find(contact_world,
        {{2.8f, 4.f}, {1.5f, 4.f}, {.2f, .2f}});
    assert(leave_contact.status == AoePathStatus::Ready &&
           !leave_contact.waypoints.empty());

    AoeDynamicObstacleIndex dynamic;
    dynamic.reset(map);
    dynamic.insert(map, {entt::entity{1}, 11, entt::null,
                         {2.f, 2.f}, {.3f, .4f}});
    dynamic.insert(map, {entt::entity{2}, 12, entt::entity{99},
                         {5.f, 2.f}, {.4f, .4f}});
    assert(dynamic.entries().size() == 2);
    int found = 0;
    dynamic.query(map, {1.f, 1.f}, {3.f, 3.f},
                  [&](const auto&) { ++found; });
    assert(found == 1);
    assert(dynamic.dynamic_safe_fraction(
               map, {0.f, 2.f}, {4.f, 2.f}, {.2f, .2f},
               entt::entity{9}) < 1.f);
    assert(dynamic.dynamic_safe_fraction(
               map, {4.f, 2.f}, {6.f, 2.f}, {.2f, .2f},
               entt::entity{9}, entt::entity{99}) == 1.f);

    EcsWorld world;
    world.add_resource<AoeLogicMap>(make_map());
    auto& registry = world.add_resource<AoePathfinderRegistry>();
    registry.bind<DirectPathfinderLogic>("direct");
    registry.bind<GridAStarPathfinderLogic>("grid_astar");
    auto& world_map = world.resource<AoeLogicMap>();
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.center = {4.f, 4.f};
    wall.half_extents = {.45f, 2.f};
    world_map.add_static_obstacle(wall);
    const AoePathRequest request{{1.5f, 4.f}, {6.5f, 4.f}, {.2f, .2f}};
    const auto path = registry.find("grid_astar", world, request);
    assert(path.status == AoePathStatus::Ready && !path.waypoints.empty());
    assert(path.waypoints.back() == request.goal);
    bool detoured = false;
    for (const auto waypoint : path.waypoints)
        detoured = detoured || std::abs(waypoint.y - 4.f) > 1.f;
    assert(detoured);
    const auto same_path = registry.find("grid_astar", world, request);
    assert(same_path.waypoints == path.waypoints);

    AoeStaticObstacleDesc sealed;
    sealed.shape = AoeStaticObstacleShape::Aabb;
    sealed.center = {4.f, 4.f};
    sealed.half_extents = {.45f, 4.f};
    world_map.update_static_obstacle(1, sealed);
    assert(registry.find("grid_astar", world, request).status ==
           AoePathStatus::NoPath);

    // ECS static-obstacle binding updates and unregisters with entity lifetime.
    const auto building = world.spawn();
    world.reg().emplace<AoePosition>(building, AoePosition{{2.f, 6.f}});
    world.reg().emplace<AoeMapStaticObstacle>(building,
        AoeMapStaticObstacle{AoeStaticObstacleShape::Aabb, {.5f, .5f}});
    const auto count_before = world_map.static_obstacle_count();
    aoe_map_static_obstacle_system(world);
    assert(world_map.static_obstacle_count() == count_before + 1);
    world.reg().get<AoePosition>(building).value = {2.f, 7.f};
    aoe_map_static_obstacle_system(world);
    assert(world_map.position_blocked({2.f, 7.f}, {.1f, .1f}));
    world.reg().destroy(building);
    aoe_map_static_obstacle_system(world);
    assert(world_map.static_obstacle_count() == count_before);

    // JSON asset format and validation.
    nlohmann::json json = {
        {"schema_version", 1}, {"kind", "aoe_logic_map"}, {"id", "json"},
        {"origin", {{"x", 0.0}, {"y", 0.0}}}, {"tile_size", 1.0},
        {"width", 1}, {"height", 1}, {"heights", {0.0, 1.0, 2.0, 3.0}},
        {"static_obstacles", nlohmann::json::array({{
            {"id", "tree"}, {"shape", "circle"},
            {"center", {{"x", .5}, {"y", .5}}}, {"radius", .1}}})}
    };
    MemoryFileSystem fs;
    fs.texts["map.json"] = json.dump();
    AoeMapDefinitionLoader loader;
    auto loaded = loader.finalize(
        loader.load_cpu(AoeMapDefinitionDesc("map.json"), fs),
        AoeMapDefinitionDesc("map.json"));
    assert(loaded && loaded->id == "json" && loaded->heights.size() == 4 &&
           loaded->static_obstacles.size() == 1);
    json["heights"] = {0.0};
    fs.texts["bad.json"] = json.dump();
    assert(!loader.load_cpu(AoeMapDefinitionDesc("bad.json"), fs));
    return 0;
}
