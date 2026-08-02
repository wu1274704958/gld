#include <aoe2x/Aoe2xNavigation.hpp>

#include <cassert>
#include <cmath>

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
    map.width = 112; map.height = 72; map.tile_size = 1.f;
    map.heights.resize(113u * 73u, 0.f);
    AoeStaticObstacleDesc obstacle;
    obstacle.shape = AoeStaticObstacleShape::Aabb;
    obstacle.center = {24.f, 22.f}; obstacle.half_extents = {.75f, 18.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {48.f, 50.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {72.f, 22.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {96.f, 50.f};
    map.static_obstacles.push_back(obstacle);

    obstacle.half_extents = {7.f, .75f};
    obstacle.center = {36.f, 52.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {60.f, 18.f};
    map.static_obstacles.push_back(obstacle);
    obstacle.center = {84.f, 54.f};
    map.static_obstacles.push_back(obstacle);

    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.radius = 4.f;
    circle.center = {14.f, 55.f};
    map.static_obstacles.push_back(circle);
    circle.center = {36.f, 18.f};
    map.static_obstacles.push_back(circle);
    circle.center = {60.f, 54.f};
    map.static_obstacles.push_back(circle);
    circle.center = {84.f, 18.f};
    map.static_obstacles.push_back(circle);
    circle.center = {104.f, 12.f};
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

bool near(float actual, float expected, float epsilon = 1e-4f) {
    return std::abs(actual - expected) <= epsilon;
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
    const std::vector<glm::vec2> expected_direct{{10.f, 2.f}};
    assert(direct_route.waypoints == expected_direct);
    assert(direct_route.total_cost && near(*direct_route.total_cost, 8.f));
    assert(direct_route.status == Aoe2xRouteStatus::Ready);
    const auto& detour_route = world.reg().get<Aoe2xRoutePlan>(detour);
    const auto& map = world.resource<AoeLogicMap>();
    assert_safe_route(map, {4.f, 16.f}, detour_route, {44.f, 16.f});
    assert(detour_route.total_cost && *detour_route.total_cost > 40.f);
    const auto queries = world.resource<Aoe2xPathfindingDiagnostics>().queries;
    Aoe2xPathfindingSystem::run(world, 2);
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries == queries);
    world.reg().get<Aoe2xNavigationDestination>(detour).value = {44.f, 6.f};
    Aoe2xPathfindingSystem::run(world, 3);
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries == queries + 1);
    world.reg().emplace_or_replace<Aoe2xRoutePlan>(direct, Aoe2xRoutePlan{});
    Aoe2xPathfindingSystem::run(world, 4);
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries == queries + 2);
    assert(world.reg().get<Aoe2xRoutePlan>(direct).status ==
           Aoe2xRouteStatus::Ready);

    EcsWorld preview_world;
    preview_world.add_resource<AoeLogicMap>(make_preview_map());
    preview_world.add_resource<Aoe2xPathfindingSettings>(Aoe2xPathfindingSettings{8, true});
    const auto preview = request(preview_world, {5.f, 36.f}, {107.f, 36.f});
    preview_world.reg().get<AoeCollider>(preview) = {.3f, .3f, 1.f};
    Aoe2xPathfindingSystem::run(preview_world, 1);
    const auto& preview_map = preview_world.resource<AoeLogicMap>();
    assert_safe_route(preview_map, {5.f, 36.f},
                      preview_world.reg().get<Aoe2xRoutePlan>(preview), {107.f, 36.f},
                      {.3f, .3f});
    const auto initial_queries = preview_world.resource<Aoe2xPathfindingDiagnostics>().queries;
    preview_world.reg().get<Aoe2xNavigationDestination>(preview).value = {106.8f, 36.2f};
    Aoe2xPathfindingSystem::run(preview_world, 2);
    assert(preview_world.resource<Aoe2xPathfindingDiagnostics>().queries == initial_queries + 1);
    assert_safe_route(preview_map, {5.f, 36.f},
                      preview_world.reg().get<Aoe2xRoutePlan>(preview), {106.8f, 36.2f},
                      {.3f, .3f});

    EcsWorld diagonal_world;
    auto diagonal_map = make_map();
    diagonal_map.static_obstacles.clear();
    diagonal_world.add_resource<AoeLogicMap>(std::move(diagonal_map));
    diagonal_world.add_resource<Aoe2xPathfindingSettings>(
        Aoe2xPathfindingSettings{8, false});
    const auto diagonal = request(diagonal_world, {1.5f, 1.5f}, {3.5f, 3.5f});
    Aoe2xPathfindingSystem::run(diagonal_world, 1);
    const auto& diagonal_route = diagonal_world.reg().get<Aoe2xRoutePlan>(diagonal);
    assert(diagonal_route.total_cost);
    assert(near(*diagonal_route.total_cost, std::sqrt(8.f)));

    const auto original_goal = glm::vec2{3.5f, 3.5f};
    diagonal_world.reg().get<Aoe2xNavigationDestination>(diagonal).value = {-1.f, -1.f};
    Aoe2xPathfindingSystem::run(diagonal_world, 2);
    const auto& invalid_route = diagonal_world.reg().get<Aoe2xRoutePlan>(diagonal);
    assert(invalid_route.waypoints.empty() && !invalid_route.total_cost);
    assert(invalid_route.status == Aoe2xRouteStatus::Invalid);
    diagonal_world.reg().get<Aoe2xNavigationDestination>(diagonal).value = original_goal;
    Aoe2xPathfindingSystem::run(diagonal_world, 3);
    assert(diagonal_world.reg().get<Aoe2xRoutePlan>(diagonal).total_cost);

    diagonal_world.reg().remove<Aoe2xRoutePlan>(diagonal);
    Aoe2xPathfindingSystem::run(diagonal_world, 4);
    assert(diagonal_world.reg().get<Aoe2xRoutePlan>(diagonal).total_cost);

    const auto disposable = request(diagonal_world, {5.5f, 5.5f}, {6.5f, 6.5f});
    Aoe2xPathfindingSystem::run(diagonal_world, 5);
    assert(diagonal_world.resource<Aoe2xPathfindingState>().records.contains(disposable));
    diagonal_world.reg().destroy(disposable);
    Aoe2xPathfindingSystem::run(diagonal_world, 6);
    assert(!diagonal_world.resource<Aoe2xPathfindingState>().records.contains(disposable));

    EcsWorld normalized_world;
    auto normalized_map = make_map();
    normalized_map.static_obstacles.clear();
    normalized_world.add_resource<AoeLogicMap>(std::move(normalized_map));
    normalized_world.add_resource<Aoe2xPathfindingSettings>(
        Aoe2xPathfindingSettings{0, false});
    const auto normalized = request(normalized_world, {1.5f, 1.5f}, {4.5f, 4.5f});
    Aoe2xPathfindingSystem::run(normalized_world, 1);
    assert(normalized_world.resource<Aoe2xPathfindingDiagnostics>().cache_rebuilds == 1);
    normalized_world.reg().get<Aoe2xNavigationDestination>(normalized).value = {5.5f, 5.5f};
    Aoe2xPathfindingSystem::run(normalized_world, 2);
    assert(normalized_world.resource<Aoe2xPathfindingDiagnostics>().cache_rebuilds == 1);
    return 0;
}
