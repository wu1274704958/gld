#include <aoe/AoeMap.hpp>
#include <aoe/AoeNavMesh.hpp>
#include <ecs/EcsWorld.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace gld::ecs;
using namespace gld::ecs::aoe;

#undef assert
#define assert(...) do { if (!(__VA_ARGS__)) { \
    std::fprintf(stderr, "assertion failed at line %d: %s\n", __LINE__, #__VA_ARGS__); \
    std::abort(); } } while (false)

namespace {
AoeMapDefinition make_map() {
    AoeMapDefinition result;
    result.id = "navmesh_test";
    result.origin = {0.f, 0.f};
    result.tile_size = 1.f;
    result.width = 24;
    result.height = 16;
    result.heights.assign(25 * 17, 0.f);

    AoeStaticObstacleDesc base;
    base.source_id = "base_block";
    base.shape = AoeStaticObstacleShape::Aabb;
    base.center = {12.f, 8.f};
    base.half_extents = {1.5f, 3.f};
    result.static_obstacles.push_back(base);
    return result;
}
} // namespace

int main() {
    AoeLogicMap map(make_map());
    assert(map.base_static_obstacle_count() == 1);
    assert(map.runtime_static_obstacle_count() == 0);

    AoeStaticObstacleDesc runtime;
    runtime.source_id = "runtime_circle";
    runtime.shape = AoeStaticObstacleShape::Circle;
    runtime.center = {5.f, 3.f};
    runtime.radius = 1.f;
    const auto runtime_id = map.add_runtime_static_obstacle(runtime);
    assert(runtime_id != 0);
    assert(map.runtime_static_obstacle_count() == 1);
    assert(map.static_obstacle_kind(runtime_id) == AoeStaticObstacleKind::Runtime);

    bool saw_base = false;
    bool saw_runtime = false;
    AoeObstacleId base_id = 0;
    map.visit_static_obstacles([&](AoeObstacleId id, AoeStaticObstacleKind kind,
                                   const AoeStaticObstacleDesc&) {
        saw_base = saw_base || kind == AoeStaticObstacleKind::Base;
        saw_runtime = saw_runtime || kind == AoeStaticObstacleKind::Runtime;
        if (kind == AoeStaticObstacleKind::Base) base_id = id;
    });
    assert(saw_base && saw_runtime);
    assert(base_id != 0);
    assert(!map.update_runtime_static_obstacle(base_id, runtime));
    assert(!map.remove_runtime_static_obstacle(base_id));

    AoeNavMeshResource nav_mesh;
    AoeNavMeshBuildSettings settings;
    assert(AoeTiledBaseToNavMesh::build(map, settings, nav_mesh));
    assert(nav_mesh.status() == AoeNavMeshStatus::Ready);
    assert(nav_mesh.source_map_revision() == map.static_revision());
    assert(nav_mesh.diagnostics().polygon_count > 0);
    assert(nav_mesh.diagnostics().build_ms > 0.0);

    const auto corridor = nav_mesh.find_corridor({2.f, 8.f}, {22.f, 8.f});
    assert(corridor.status == AoeNavCorridorStatus::Ready);
    assert(!corridor.polygons.empty());
    assert(corridor.portals.size() + 1 == corridor.polygons.size());
    assert(corridor.map_revision == map.static_revision());
    assert(nav_mesh.diagnostics().query_count == 1);
    bool detoured = false;
    for (const auto& portal : corridor.portals) {
        const float midpoint_y = (portal.left.y + portal.right.y) * .5f;
        detoured = detoured || midpoint_y < 5.f || midpoint_y > 11.f;
    }
    assert(detoured);
    const auto repeated = nav_mesh.find_corridor({2.f, 8.f}, {22.f, 8.f});
    assert(repeated.status == AoeNavCorridorStatus::Ready);
    assert(repeated.polygons == corridor.polygons);
    assert(nav_mesh.find_corridor({-100.f, -100.f}, {22.f, 8.f}).status ==
           AoeNavCorridorStatus::InvalidStart);
    const auto projected_from_circle = nav_mesh.find_corridor(
        runtime.center, {22.f, 3.f});
    assert(projected_from_circle.status == AoeNavCorridorStatus::Ready);
    assert(glm::length(projected_from_circle.start_on_mesh - runtime.center) >=
           runtime.radius);

    EcsWorld world;
    world.add_resource<AoeLogicMap>(make_map());
    aoe_nav_mesh_build_system(world);
    auto& world_nav = world.resource<AoeNavMeshResource>();
    auto& world_map = world.resource<AoeLogicMap>();
    assert(world_nav.status() == AoeNavMeshStatus::Ready);
    const auto built_revision = world_nav.source_map_revision();
    assert(world_map.add_runtime_static_obstacle(runtime));
    assert(world_map.static_revision() != built_revision);
    aoe_nav_mesh_build_system(world);
    assert(world_nav.status() == AoeNavMeshStatus::Stale);
    assert(world_nav.queryable());
    assert(world_nav.stale(world_map.static_revision()));
    assert(world_nav.source_map_revision() == built_revision);
    assert(world_nav.find_corridor({2.f, 8.f}, {22.f, 8.f}).status ==
           AoeNavCorridorStatus::Ready);
    return 0;
}
