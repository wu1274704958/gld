#include <aoe/AoeNavMesh.hpp>

#include <aoe/AoeMap.hpp>
#include <ecs/EcsWorld.hpp>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <utility>

namespace gld::ecs::aoe {
namespace {
using Clock = std::chrono::steady_clock;

template<class T, void (*Free)(T*)>
using RecastPtr = std::unique_ptr<T, decltype(Free)>;

using HeightfieldPtr = RecastPtr<rcHeightfield, rcFreeHeightField>;
using CompactHeightfieldPtr =
    RecastPtr<rcCompactHeightfield, rcFreeCompactHeightfield>;
using ContourSetPtr = RecastPtr<rcContourSet, rcFreeContourSet>;
using PolyMeshPtr = RecastPtr<rcPolyMesh, rcFreePolyMesh>;
using PolyMeshDetailPtr = RecastPtr<rcPolyMeshDetail, rcFreePolyMeshDetail>;
using NavMeshPtr = RecastPtr<dtNavMesh, dtFreeNavMesh>;
using NavQueryPtr = RecastPtr<dtNavMeshQuery, dtFreeNavMeshQuery>;

double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

glm::vec2 logical_position(const float* recast) {
    return {recast[0], recast[2]};
}

void recast_position(glm::vec2 logical, float* result) {
    result[0] = logical.x;
    result[1] = 0.f;
    result[2] = logical.y;
}

bool valid_settings(const AoeNavMeshBuildSettings& value) {
    return value.agent_height > 0.f && value.agent_radius >= 0.f &&
        value.agent_max_climb >= 0.f && value.max_vertices_per_polygon >= 3 &&
        value.max_vertices_per_polygon <= DT_VERTS_PER_POLYGON &&
        value.circle_segments >= 8 && value.max_query_nodes > 0 &&
        value.max_corridor_polygons > 0;
}

void add_ground_geometry(const AoeLogicMap& map, std::vector<float>& vertices,
                         std::vector<int>& triangles) {
    const std::size_t stride = static_cast<std::size_t>(map.width()) + 1;
    vertices.reserve(stride * (map.height() + 1) * 3);
    for (std::uint32_t y = 0; y <= map.height(); ++y) {
        for (std::uint32_t x = 0; x <= map.width(); ++x) {
            const glm::vec2 point = map.origin() +
                glm::vec2(static_cast<float>(x), static_cast<float>(y)) *
                    map.tile_size();
            vertices.insert(vertices.end(), {point.x, 0.f, point.y});
        }
    }
    triangles.reserve(static_cast<std::size_t>(map.width()) * map.height() * 6);
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const int low = static_cast<int>(static_cast<std::size_t>(y) * stride + x);
            const int high = low + static_cast<int>(stride);
            triangles.insert(triangles.end(),
                {low, high, low + 1, low + 1, high, high + 1});
        }
    }
}

std::vector<float> obstacle_polygon(const AoeStaticObstacleDesc& obstacle,
                                    int circle_segments) {
    std::vector<float> result;
    if (obstacle.shape == AoeStaticObstacleShape::Aabb) {
        const glm::vec2 low = obstacle.center - obstacle.half_extents;
        const glm::vec2 high = obstacle.center + obstacle.half_extents;
        result = {low.x, 0.f, low.y, high.x, 0.f, low.y,
                  high.x, 0.f, high.y, low.x, 0.f, high.y};
        return result;
    }
    result.reserve(static_cast<std::size_t>(circle_segments) * 3);
    constexpr float TwoPi = 6.28318530717958647692f;
    for (int index = 0; index < circle_segments; ++index) {
        const float angle = TwoPi * static_cast<float>(index) /
                            static_cast<float>(circle_segments);
        result.insert(result.end(),
            {obstacle.center.x + std::cos(angle) * obstacle.radius, 0.f,
             obstacle.center.y + std::sin(angle) * obstacle.radius});
    }
    return result;
}

bool portal_points(const dtNavMesh& nav_mesh, dtPolyRef from, dtPolyRef to,
                   float* left, float* right) {
    const dtMeshTile* tile = nullptr;
    const dtPoly* polygon = nullptr;
    if (dtStatusFailed(nav_mesh.getTileAndPolyByRef(from, &tile, &polygon)))
        return false;
    const dtLink* link = nullptr;
    for (unsigned int index = polygon->firstLink; index != DT_NULL_LINK;
         index = tile->links[index].next) {
        if (tile->links[index].ref == to) {
            link = &tile->links[index];
            break;
        }
    }
    if (!link || polygon->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
        return false;
    const int v0 = polygon->verts[link->edge];
    const int v1 = polygon->verts[(link->edge + 1) % polygon->vertCount];
    dtVcopy(left, &tile->verts[v0 * 3]);
    dtVcopy(right, &tile->verts[v1 * 3]);
    if (link->side != 0xff && (link->bmin != 0 || link->bmax != 255)) {
        float edge0[3], edge1[3];
        dtVcopy(edge0, left);
        dtVcopy(edge1, right);
        constexpr float scale = 1.f / 255.f;
        dtVlerp(left, edge0, edge1, link->bmin * scale);
        dtVlerp(right, edge0, edge1, link->bmax * scale);
    }
    return true;
}
} // namespace

struct AoeNavMeshResource::Impl {
    NavMeshPtr nav_mesh{nullptr, dtFreeNavMesh};
    NavQueryPtr query{nullptr, dtFreeNavMeshQuery};
};

AoeNavMeshResource::AoeNavMeshResource() : impl_(std::make_unique<Impl>()) {}
AoeNavMeshResource::~AoeNavMeshResource() = default;
AoeNavMeshResource::AoeNavMeshResource(AoeNavMeshResource&&) noexcept = default;
AoeNavMeshResource& AoeNavMeshResource::operator=(AoeNavMeshResource&&) noexcept = default;

bool AoeNavMeshResource::queryable() const {
    return impl_ && impl_->nav_mesh && impl_->query &&
        (status_ == AoeNavMeshStatus::Ready || status_ == AoeNavMeshStatus::Stale);
}

bool AoeNavMeshResource::stale(std::uint64_t logic_map_revision) const {
    return queryable() && source_map_revision_ != logic_map_revision;
}

void AoeNavMeshResource::clear() {
    impl_ = std::make_unique<Impl>();
    status_ = AoeNavMeshStatus::Uninitialized;
    source_map_revision_ = 0;
    warned_stale_revision_ = 0;
    error_.clear();
    debug_polygons_.clear();
    diagnostics_ = {};
}

AoeNavCorridor AoeNavMeshResource::find_corridor(
    glm::vec2 start, glm::vec2 goal) const {
    const auto begin = Clock::now();
    AoeNavCorridor result;
    result.map_revision = source_map_revision_;
    if (!queryable()) {
        result.status = AoeNavCorridorStatus::NavMeshUnavailable;
    } else {
        const float extent_xz = std::max(settings_.cell_size * 4.f,
                                         settings_.agent_radius * 2.f + .01f);
        const float extents[3]{extent_xz, settings_.agent_height, extent_xz};
        float start_position[3], goal_position[3];
        float start_nearest[3]{}, goal_nearest[3]{};
        recast_position(start, start_position);
        recast_position(goal, goal_position);
        dtQueryFilter filter;
        filter.setIncludeFlags(1);
        dtPolyRef start_ref = 0;
        dtPolyRef goal_ref = 0;
        const dtStatus start_status = impl_->query->findNearestPoly(
            start_position, extents, &filter, &start_ref, start_nearest);
        if (dtStatusFailed(start_status) || !start_ref) {
            result.status = AoeNavCorridorStatus::InvalidStart;
        } else {
            const dtStatus goal_status = impl_->query->findNearestPoly(
                goal_position, extents, &filter, &goal_ref, goal_nearest);
            if (dtStatusFailed(goal_status) || !goal_ref) {
                result.status = AoeNavCorridorStatus::InvalidGoal;
            } else {
                std::vector<dtPolyRef> path(
                    static_cast<std::size_t>(settings_.max_corridor_polygons));
                int path_count = 0;
                const dtStatus path_status = impl_->query->findPath(
                    start_ref, goal_ref, start_nearest, goal_nearest, &filter,
                    path.data(), &path_count, static_cast<int>(path.size()));
                if (dtStatusFailed(path_status)) {
                    result.status = AoeNavCorridorStatus::QueryFailed;
                } else if (!path_count || path[path_count - 1] != goal_ref) {
                    result.status = AoeNavCorridorStatus::NoPath;
                } else {
                    result.start_on_mesh = logical_position(start_nearest);
                    result.goal_on_mesh = logical_position(goal_nearest);
                    result.polygons.reserve(static_cast<std::size_t>(path_count));
                    result.portals.reserve(static_cast<std::size_t>(path_count - 1));
                    for (int index = 0; index < path_count; ++index)
                        result.polygons.push_back(
                            static_cast<AoeNavPolygonId>(path[index]));
                    bool portals_valid = true;
                    for (int index = 1; index < path_count; ++index) {
                        float left[3], right[3];
                        if (!portal_points(*impl_->nav_mesh, path[index - 1],
                                           path[index], left, right)) {
                            portals_valid = false;
                            break;
                        }
                        result.portals.push_back(
                            {logical_position(left), logical_position(right)});
                    }
                    result.status = portals_valid
                        ? AoeNavCorridorStatus::Ready
                        : AoeNavCorridorStatus::QueryFailed;
                    if (!portals_valid) {
                        result.polygons.clear();
                        result.portals.clear();
                    }
                }
            }
        }
    }
    const double elapsed = milliseconds(begin);
    ++diagnostics_.query_count;
    diagnostics_.query_total_ms += elapsed;
    diagnostics_.query_max_ms = std::max(diagnostics_.query_max_ms, elapsed);
    return result;
}

bool AoeTiledBaseToNavMesh::build(
    const AoeLogicMap& map, const AoeNavMeshBuildSettings& requested,
    AoeNavMeshResource& resource) {
    const auto begin = Clock::now();
    resource.clear();
    resource.settings_ = requested;
    resource.settings_.cell_size = requested.cell_size > 0.f
        ? requested.cell_size : map.tile_size() / 4.f;
    resource.settings_.cell_height = requested.cell_height > 0.f
        ? requested.cell_height : map.tile_size() / 5.f;
    auto fail = [&](std::string message) {
        resource.status_ = AoeNavMeshStatus::BuildFailed;
        resource.error_ = std::move(message);
        resource.diagnostics_.build_ms = milliseconds(begin);
        return false;
    };
    if (!map.valid()) return fail("invalid logic map");
    if (!valid_settings(resource.settings_) ||
        !(resource.settings_.cell_size > 0.f) ||
        !(resource.settings_.cell_height > 0.f))
        return fail("invalid nav mesh build settings");

    std::vector<float> vertices;
    std::vector<int> triangles;
    add_ground_geometry(map, vertices, triangles);
    if (vertices.size() / 3 > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        triangles.size() / 3 > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return fail("logic map geometry exceeds Recast limits");

    rcContext context;
    rcConfig config{};
    config.cs = resource.settings_.cell_size;
    config.ch = resource.settings_.cell_height;
    config.walkableSlopeAngle = resource.settings_.agent_max_slope_degrees;
    config.walkableHeight = static_cast<int>(
        std::ceil(resource.settings_.agent_height / config.ch));
    config.walkableClimb = static_cast<int>(
        std::floor(resource.settings_.agent_max_climb / config.ch));
    config.walkableRadius = static_cast<int>(
        std::ceil(resource.settings_.agent_radius / config.cs));
    config.maxEdgeLen = static_cast<int>(
        resource.settings_.edge_max_length / config.cs);
    config.maxSimplificationError = resource.settings_.edge_max_error;
    config.minRegionArea = static_cast<int>(
        rcSqr(resource.settings_.region_min_size));
    config.mergeRegionArea = static_cast<int>(
        rcSqr(resource.settings_.region_merge_size));
    config.maxVertsPerPoly = resource.settings_.max_vertices_per_polygon;
    config.detailSampleDist = resource.settings_.detail_sample_distance < .9f
        ? 0.f : config.cs * resource.settings_.detail_sample_distance;
    config.detailSampleMaxError =
        config.ch * resource.settings_.detail_sample_max_error;
    rcVcopy(config.bmin, vertices.data());
    config.bmax[0] = map.origin().x + map.width() * map.tile_size();
    config.bmax[1] = resource.settings_.agent_height +
                     resource.settings_.agent_max_climb + config.ch * 2.f;
    config.bmax[2] = map.origin().y + map.height() * map.tile_size();
    config.bmin[1] = -config.ch * 2.f;
    rcCalcGridSize(config.bmin, config.bmax, config.cs,
                   &config.width, &config.height);

    HeightfieldPtr heightfield(rcAllocHeightfield(), rcFreeHeightField);
    if (!heightfield || !rcCreateHeightfield(&context, *heightfield,
            config.width, config.height, config.bmin, config.bmax,
            config.cs, config.ch))
        return fail("failed to create Recast heightfield");
    const int triangle_count = static_cast<int>(triangles.size() / 3);
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangle_count));
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle,
        vertices.data(), static_cast<int>(vertices.size() / 3),
        triangles.data(), triangle_count, areas.data());
    if (!rcRasterizeTriangles(&context, vertices.data(),
            static_cast<int>(vertices.size() / 3), triangles.data(),
            areas.data(), triangle_count, *heightfield, config.walkableClimb))
        return fail("failed to rasterize map geometry");
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight,
                       config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    CompactHeightfieldPtr compact(rcAllocCompactHeightfield(),
                                  rcFreeCompactHeightfield);
    if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight,
            config.walkableClimb, *heightfield, *compact))
        return fail("failed to build compact Recast heightfield");
    heightfield.reset();

    map.visit_static_obstacles([&](AoeObstacleId, AoeStaticObstacleKind,
                                   const AoeStaticObstacleDesc& obstacle) {
        auto polygon = obstacle_polygon(
            obstacle, resource.settings_.circle_segments);
        rcMarkConvexPolyArea(&context, polygon.data(),
            static_cast<int>(polygon.size() / 3), config.bmin[1], config.bmax[1],
            RC_NULL_AREA, *compact);
    });
    if (config.walkableRadius > 0 &&
        !rcErodeWalkableArea(&context, config.walkableRadius, *compact))
        return fail("failed to erode walkable area");
    if (!rcBuildDistanceField(&context, *compact) ||
        !rcBuildRegions(&context, *compact, config.borderSize,
                        config.minRegionArea, config.mergeRegionArea))
        return fail("failed to build Recast regions");

    ContourSetPtr contours(rcAllocContourSet(), rcFreeContourSet);
    if (!contours || !rcBuildContours(&context, *compact,
            config.maxSimplificationError, config.maxEdgeLen, *contours))
        return fail("failed to build Recast contours");
    PolyMeshPtr polygon_mesh(rcAllocPolyMesh(), rcFreePolyMesh);
    if (!polygon_mesh || !rcBuildPolyMesh(&context, *contours,
                                           config.maxVertsPerPoly, *polygon_mesh))
        return fail("failed to build Recast polygon mesh");
    PolyMeshDetailPtr detail_mesh(rcAllocPolyMeshDetail(),
                                  rcFreePolyMeshDetail);
    if (!detail_mesh || !rcBuildPolyMeshDetail(&context, *polygon_mesh,
            *compact, config.detailSampleDist, config.detailSampleMaxError,
            *detail_mesh))
        return fail("failed to build Recast detail mesh");
    if (!polygon_mesh->npolys) return fail("Recast produced no walkable polygons");
    for (int index = 0; index < polygon_mesh->npolys; ++index)
        polygon_mesh->flags[index] = polygon_mesh->areas[index] == RC_NULL_AREA
            ? 0 : 1;

    dtNavMeshCreateParams params{};
    params.verts = polygon_mesh->verts;
    params.vertCount = polygon_mesh->nverts;
    params.polys = polygon_mesh->polys;
    params.polyAreas = polygon_mesh->areas;
    params.polyFlags = polygon_mesh->flags;
    params.polyCount = polygon_mesh->npolys;
    params.nvp = polygon_mesh->nvp;
    params.detailMeshes = detail_mesh->meshes;
    params.detailVerts = detail_mesh->verts;
    params.detailVertsCount = detail_mesh->nverts;
    params.detailTris = detail_mesh->tris;
    params.detailTriCount = detail_mesh->ntris;
    params.walkableHeight = resource.settings_.agent_height;
    params.walkableRadius = resource.settings_.agent_radius;
    params.walkableClimb = resource.settings_.agent_max_climb;
    rcVcopy(params.bmin, polygon_mesh->bmin);
    rcVcopy(params.bmax, polygon_mesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;

    unsigned char* nav_data = nullptr;
    int nav_data_size = 0;
    if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size))
        return fail("failed to create Detour nav mesh data");
    NavMeshPtr nav_mesh(dtAllocNavMesh(), dtFreeNavMesh);
    if (!nav_mesh) {
        dtFree(nav_data);
        return fail("failed to allocate Detour nav mesh");
    }
    if (dtStatusFailed(nav_mesh->init(nav_data, nav_data_size,
                                      DT_TILE_FREE_DATA))) {
        dtFree(nav_data);
        return fail("failed to initialize Detour nav mesh");
    }
    NavQueryPtr query(dtAllocNavMeshQuery(), dtFreeNavMeshQuery);
    if (!query || dtStatusFailed(query->init(
                      nav_mesh.get(), resource.settings_.max_query_nodes)))
        return fail("failed to initialize Detour nav mesh query");

    resource.impl_->nav_mesh = std::move(nav_mesh);
    resource.impl_->query = std::move(query);
    resource.source_map_revision_ = map.static_revision();
    resource.status_ = AoeNavMeshStatus::Ready;
    resource.error_.clear();
    const dtNavMesh& debug_nav_mesh = *resource.impl_->nav_mesh;
    for (int tile_index = 0;
         tile_index < debug_nav_mesh.getMaxTiles(); ++tile_index) {
        const dtMeshTile* tile = debug_nav_mesh.getTile(tile_index);
        if (!tile || !tile->header) continue;
        const dtPolyRef base = debug_nav_mesh.getPolyRefBase(tile);
        for (int polygon_index = 0;
             polygon_index < tile->header->polyCount; ++polygon_index) {
            const dtPoly& polygon = tile->polys[polygon_index];
            if (polygon.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
            AoeNavDebugPolygon debug;
            debug.id = static_cast<AoeNavPolygonId>(base | polygon_index);
            debug.vertices.reserve(polygon.vertCount);
            for (int vertex_index = 0; vertex_index < polygon.vertCount;
                 ++vertex_index)
                debug.vertices.push_back(logical_position(
                    &tile->verts[polygon.verts[vertex_index] * 3]));
            resource.debug_polygons_.push_back(std::move(debug));
        }
    }
    resource.diagnostics_.polygon_count = resource.debug_polygons_.size();
    resource.diagnostics_.build_ms = milliseconds(begin);
    return true;
}

void aoe_nav_mesh_build_system(EcsWorld& world) {
    auto* map = world.try_resource<AoeLogicMap>();
    if (!map || !map->valid()) return;
    auto& resource = world.resource_or_add<AoeNavMeshResource>();
    if (resource.status_ == AoeNavMeshStatus::Uninitialized) {
        AoeTiledBaseToNavMesh::build(
            *map, world.resource_or_add<AoeNavMeshBuildSettings>(), resource);
        if (resource.status_ == AoeNavMeshStatus::BuildFailed)
            std::fprintf(stderr, "[aoe-navmesh] build failed: %s\n",
                         resource.error_.c_str());
        return;
    }
    if (!resource.queryable() ||
        resource.source_map_revision_ == map->static_revision()) return;
    resource.status_ = AoeNavMeshStatus::Stale;
    if (resource.warned_stale_revision_ == map->static_revision()) return;
    resource.warned_stale_revision_ = map->static_revision();
    std::fprintf(stderr,
        "[aoe-navmesh] stale: built from revision %llu, logic map is %llu; "
        "continuing with the old mesh\n",
        static_cast<unsigned long long>(resource.source_map_revision_),
        static_cast<unsigned long long>(map->static_revision()));
    // !TODO: choose and implement full rebuild, tiled incremental update, or
    // asynchronous double-buffered replacement when LogicMap revision changes.
}

} // namespace gld::ecs::aoe
