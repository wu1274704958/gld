#include <aoe2x/Aoe2xNavigation.hpp>

#include <cassert>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2x;

namespace {
AoeMapDefinition make_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_hpa_test";
    map.width = 48; map.height = 32; map.tile_size = 1.f;
    map.heights.resize(49u * 33u, 0.f);
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.center = {24.f, 16.f}; wall.half_extents = {.5f, 11.f};
    map.static_obstacles.push_back(wall);
    return map;
}

AoeMapDefinition make_preview_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_hpa_preview_regression";
    map.width = 56; map.height = 36; map.tile_size = 1.f;
    map.heights.resize(57u * 37u, 0.f);
    AoeStaticObstacleDesc obstacle;
    obstacle.shape = AoeStaticObstacleShape::Aabb;
    obstacle.center = {28.f, 18.f}; obstacle.half_extents = {1.f, 12.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {13.f, 10.f}; obstacle.half_extents = {5.f, 1.f};
    map.static_obstacles.push_back(obstacle);
    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.center = {42.f, 10.f}; circle.radius = 3.f;
    map.static_obstacles.push_back(circle);
    return map;
}

entt::entity request(EcsWorld& world, glm::vec2 start, glm::vec2 goal) {
    const auto entity = world.spawn();
    world.reg().emplace<AoePosition>(entity, AoePosition{start});
    world.reg().emplace<AoeCollider>(entity, AoeCollider{.2f, .2f, 1.f});
    world.reg().emplace<Aoe2xNavigationDestination>(entity,
        Aoe2xNavigationDestination{goal});
    return entity;
}

void assert_safe_route(const AoeLogicMap& map, glm::vec2 start,
                       const Aoe2xRoutePlan& route, glm::vec2 goal,
                       glm::vec2 radii = {.2f, .2f}) {
    assert(!route.waypoints.empty());
    assert(route.waypoints.back() == goal);
    glm::vec2 previous = start;
    for (const auto waypoint : route.waypoints) {
        assert(map.static_safe_fraction(previous, waypoint, radii) >= .999f);
        previous = waypoint;
    }
}
}

int main() {
    EcsWorld world;
    world.add_resource<AoeLogicMap>(make_map());
    world.add_resource<Aoe2xPathfindingSettings>(Aoe2xPathfindingSettings{8, true});
    const auto direct = request(world, {2.f, 2.f}, {10.f, 2.f});
    const auto detour = request(world, {4.f, 16.f}, {44.f, 16.f});
    Aoe2xPathfindingSystem::run(world, 1);
    const auto& direct_route = world.reg().get<Aoe2xRoutePlan>(direct);
    assert(direct_route.waypoints == std::vector<glm::vec2>{{10.f, 2.f}});
    const auto& detour_route = world.reg().get<Aoe2xRoutePlan>(detour);
    const auto& map = world.resource<AoeLogicMap>();
    assert_safe_route(map, {4.f, 16.f}, detour_route, {44.f, 16.f});
    const auto queries = world.resource<Aoe2xPathfindingDiagnostics>().queries;
    Aoe2xPathfindingSystem::run(world, 2);
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries == queries);
    world.reg().get<Aoe2xNavigationDestination>(detour).value = {44.f, 6.f};
    Aoe2xPathfindingSystem::run(world, 3);
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries == queries + 1);

    EcsWorld preview_world;
    preview_world.add_resource<AoeLogicMap>(make_preview_map());
    preview_world.add_resource<Aoe2xPathfindingSettings>(Aoe2xPathfindingSettings{8, true});
    const auto preview = request(preview_world, {5.f, 18.f}, {50.f, 18.f});
    preview_world.reg().get<AoeCollider>(preview) = {.3f, .3f, 1.f};
    Aoe2xPathfindingSystem::run(preview_world, 1);
    const auto& preview_map = preview_world.resource<AoeLogicMap>();
    assert_safe_route(preview_map, {5.f, 18.f},
                      preview_world.reg().get<Aoe2xRoutePlan>(preview), {50.f, 18.f},
                      {.3f, .3f});
    const auto initial_queries = preview_world.resource<Aoe2xPathfindingDiagnostics>().queries;
    preview_world.reg().get<Aoe2xNavigationDestination>(preview).value = {50.2f, 18.2f};
    Aoe2xPathfindingSystem::run(preview_world, 2);
    assert(preview_world.resource<Aoe2xPathfindingDiagnostics>().queries == initial_queries + 1);
    assert_safe_route(preview_map, {5.f, 18.f},
                      preview_world.reg().get<Aoe2xRoutePlan>(preview), {50.2f, 18.2f},
                      {.3f, .3f});
    return 0;
}
