#include <aoe2x/Aoe2xRouteFormation.hpp>

#include <cassert>
#include <cmath>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2x;

namespace {

AoeMapDefinition open_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_route_formation_test";
    map.width = 64;
    map.height = 48;
    map.tile_size = 1.f;
    map.heights.resize(65u * 49u, 0.f);
    return map;
}

void install(EcsWorld& world, AoeMapDefinition map = open_map()) {
    world.add_resource<AoeLogicMap>(map);
    world.resource_or_add<FormationRegistry>()
        .bind<FormationType::CompactSquare, CompactSquareFormation>();
    world.resource_or_add<Aoe2xPathfindingSettings>() = {8, true};
}

entt::entity spawn(EcsWorld& world, FormationSpawnOptions options) {
    const auto squad = spawn_aoe2x_formation(world, options);
    RouteSquadSpawnSystem::run(world, 0);
    return squad;
}

void plan(EcsWorld& world, std::uint64_t tick = 1) {
    RouteSquadCommandSystem::run(world, tick);
    Aoe2xPathfindingSystem::run(world, tick);
    RouteSquadSplitSystem::run(world, tick);
}

bool near(glm::vec2 lhs, glm::vec2 rhs, float epsilon = 1e-4f) {
    return glm::length(lhs - rhs) <= epsilon;
}

void assert_member_routes_safe(EcsWorld& world, entt::entity squad) {
    const auto& map = world.resource<AoeLogicMap>();
    const auto& info = world.reg().get<SquadInfo>(squad);
    for (const auto unit : info.units) {
        if (!world.reg().valid(unit) || world.reg().all_of<Aoe2xUnitState>(unit))
            continue;
        assert((world.reg().all_of<Aoe2xRoutePlan,
                                   RouteSquadRouteAssignment>(unit)));
        const auto& route = world.reg().get<Aoe2xRoutePlan>(unit);
        assert(route.status == Aoe2xRouteStatus::Ready);
        glm::vec2 previous = world.reg().get<AoePosition>(unit).value;
        const auto& collider = world.reg().get<AoeCollider>(unit);
        for (const auto point : route.waypoints) {
            assert(map.static_safe_fraction(previous, point,
                {collider.radius_x, collider.radius_y}) >= .999f);
            previous = point;
        }
    }
}

} // namespace

int main() {
    EcsWorld world;
    install(world);
    FormationSpawnOptions options;
    options.count = 9;
    options.center = {8.f, 8.f};
    options.forward = {1.f, 0.f};
    const auto squad = spawn(world, options);
    assert(world.reg().get<FormationSpawnState>(squad).status ==
           FormationSpawnStatus::Ready);
    const auto& info = world.reg().get<SquadInfo>(squad);
    assert(info.units.size() == 9);
    assert(info.captain == entt::null);
    assert(info.slot_edges.empty());
    for (std::size_t i = 0; i < info.units.size(); ++i) {
        const auto unit = info.units[i];
        assert((!world.reg().all_of<UnitSquadInfo, SquadCaptainInfo>(unit)));
        const auto& member = world.reg().get<RouteSquadMemberInfo>(unit);
        assert(member.squad == squad && member.slot_index == i);
        const auto position = world.reg().get<AoePosition>(unit).value;
        assert(!world.resource<AoeLogicMap>().position_blocked(position,
                                                               {.3f, .3f}));
    }

    const auto before_queries =
        world.resource_or_add<Aoe2xPathfindingDiagnostics>().queries;
    assert(request_aoe2x_formation_attack_move(
        world, squad, {40.f, 12.f}, glm::vec2{0.f, 1.f}));
    plan(world);
    const auto& order = world.reg().get<FormationAttackMove>(squad);
    assert(order.status == FormationAttackMoveStatus::Completed);
    assert(near(order.destination_facing, {0.f, 1.f}));
    assert(world.resource<Aoe2xPathfindingDiagnostics>().queries ==
           before_queries + 1);
    assert((!world.reg().all_of<Aoe2xNavigationDestination,
                                RouteSquadPlanningState>(squad)));
    assert_member_routes_safe(world, squad);

    // Replacing an order removes every route previously distributed by this
    // squad before the new center query runs.
    assert(request_aoe2x_formation_attack_move(world, squad, {42.f, 20.f}));
    RouteSquadCommandSystem::run(world, 2);
    for (const auto unit : info.units)
        assert((!world.reg().all_of<Aoe2xRoutePlan,
                                    RouteSquadRouteAssignment>(unit)));
    Aoe2xPathfindingSystem::run(world, 2);
    RouteSquadSplitSystem::run(world, 2);
    assert(world.reg().get<FormationAttackMove>(squad).status ==
           FormationAttackMoveStatus::Completed);
    assert_member_routes_safe(world, squad);

    // One blocked ideal slot is shifted locally while the squad center and
    // stable ideal offsets remain unchanged.
    auto adjusted_map = open_map();
    AoeStaticObstacleDesc blocker;
    blocker.shape = AoeStaticObstacleShape::Circle;
    blocker.center = {12.f, 12.f};
    blocker.radius = .35f;
    adjusted_map.static_obstacles.push_back(blocker);
    EcsWorld adjusted_world;
    install(adjusted_world, adjusted_map);
    FormationSpawnOptions adjusted_options;
    adjusted_options.count = 1;
    adjusted_options.center = blocker.center;
    adjusted_options.unit_radius = .2f;
    adjusted_options.spawn_adjustment_radius = 1.f;
    const auto adjusted_squad = spawn(adjusted_world, adjusted_options);
    assert(adjusted_world.reg().get<FormationSpawnState>(adjusted_squad).status ==
           FormationSpawnStatus::Ready);
    assert(near(adjusted_world.reg().get<AoePosition>(adjusted_squad).value,
                blocker.center));
    const auto adjusted_unit =
        adjusted_world.reg().get<SquadInfo>(adjusted_squad).units.front();
    assert(!near(adjusted_world.reg().get<AoePosition>(adjusted_unit).value,
                 blocker.center));

    // The center can take a direct path through this corridor, but the full
    // three-column layout cannot. Profile selection must narrow the squad to
    // one column without issuing per-member pathfinding queries.
    auto corridor_map = open_map();
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.center = {36.f, 11.725f};
    wall.half_extents = {16.f, 11.725f};
    corridor_map.static_obstacles.push_back(wall);
    wall.center.y = 36.275f;
    corridor_map.static_obstacles.push_back(wall);
    EcsWorld corridor_world;
    install(corridor_world, corridor_map);
    FormationSpawnOptions corridor_options;
    corridor_options.count = 9;
    corridor_options.center = {10.f, 24.f};
    corridor_options.forward = {1.f, 0.f};
    const auto corridor_squad = spawn(corridor_world, corridor_options);
    assert(corridor_world.reg().get<FormationSpawnState>(corridor_squad).status ==
           FormationSpawnStatus::Ready);
    assert(request_aoe2x_formation_attack_move(
        corridor_world, corridor_squad, {40.f, 24.f}));
    plan(corridor_world);
    assert(corridor_world.reg().get<FormationAttackMove>(corridor_squad).status ==
           FormationAttackMoveStatus::Completed);
    assert(corridor_world.resource<Aoe2xPathfindingDiagnostics>().queries == 1);
    const auto corridor_units =
        corridor_world.reg().get<SquadInfo>(corridor_squad).units;
    for (const auto unit : corridor_units)
        assert(corridor_world.reg().get<RouteSquadRouteAssignment>(unit)
                   .final_columns == 1);
    assert_member_routes_safe(corridor_world, corridor_squad);

    // A zero adjustment radius makes the same spawn fail transactionally.
    EcsWorld failed_world;
    install(failed_world, adjusted_map);
    adjusted_options.spawn_adjustment_radius = 0.f;
    const auto failed_squad = spawn(failed_world, adjusted_options);
    assert(failed_world.reg().get<FormationSpawnState>(failed_squad).status ==
           FormationSpawnStatus::Failed);
    assert(!failed_world.reg().all_of<SquadInfo>(failed_squad));
    assert(failed_world.reg().view<RouteSquadMemberInfo>().empty());

    // Invalid members are ignored. If none remain, command acceptance is
    // followed by a deterministic Failed result without a path query.
    EcsWorld loss_world;
    install(loss_world);
    FormationSpawnOptions loss_options;
    loss_options.count = 2;
    loss_options.center = {8.f, 8.f};
    const auto loss_squad = spawn(loss_world, loss_options);
    auto loss_units = loss_world.reg().get<SquadInfo>(loss_squad).units;
    loss_world.reg().emplace<Aoe2xUnitState>(loss_units.front());
    assert(request_aoe2x_formation_attack_move(
        loss_world, loss_squad, {20.f, 8.f}));
    plan(loss_world);
    assert(loss_world.reg().get<FormationAttackMove>(loss_squad).status ==
           FormationAttackMoveStatus::Completed);
    assert(!loss_world.reg().all_of<Aoe2xRoutePlan>(loss_units.front()));
    assert(loss_world.reg().all_of<Aoe2xRoutePlan>(loss_units.back()));
    loss_world.reg().emplace<Aoe2xUnitState>(loss_units.back());
    assert(request_aoe2x_formation_attack_move(
        loss_world, loss_squad, {24.f, 8.f}));
    RouteSquadCommandSystem::run(loss_world, 2);
    assert(loss_world.reg().get<FormationAttackMove>(loss_squad).status ==
           FormationAttackMoveStatus::Failed);
    RouteSquadCleanupSystem::run(loss_world, 2);
    for (const auto unit : loss_units)
        assert(!loss_world.reg().all_of<RouteSquadMemberInfo>(unit));

    // Cleanup remains safe if the shared lifecycle pipeline already replaced
    // Released state with the pooled marker earlier in the same phase.
    const auto pooled = world.reg().get<SquadInfo>(squad).units.front();
    world.reg().emplace<Aoe2xPooledUnit>(pooled);
    RouteSquadCleanupSystem::run(world, 3);
    assert(!world.reg().all_of<RouteSquadMemberInfo>(pooled));

    return 0;
}
