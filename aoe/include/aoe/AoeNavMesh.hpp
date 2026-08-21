#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace gld::ecs {
struct EcsWorld;
}

namespace gld::ecs::aoe {

class AoeLogicMap;

using AoeNavPolygonId = std::uint64_t;

struct AoeNavMeshBuildSettings {
    float cell_size = 0.f; // <= 0 selects map.tile_size() / 4.
    float cell_height = 0.f; // <= 0 selects map.tile_size() / 5.
    float agent_height = 1.8f;
    float agent_radius = .2f;
    float agent_max_climb = .5f;
    float agent_max_slope_degrees = 45.f;
    float region_min_size = 8.f;
    float region_merge_size = 20.f;
    float edge_max_length = 12.f;
    float edge_max_error = 1.3f;
    int max_vertices_per_polygon = 6;
    float detail_sample_distance = 6.f;
    float detail_sample_max_error = 1.f;
    int circle_segments = 16;
    int max_query_nodes = 4096;
    int max_corridor_polygons = 2048;
};

enum class AoeNavMeshStatus : std::uint8_t {
    Uninitialized,
    Ready,
    Stale,
    BuildFailed,
};

enum class AoeNavCorridorStatus : std::uint8_t {
    Ready,
    NavMeshUnavailable,
    InvalidStart,
    InvalidGoal,
    NoPath,
    QueryFailed,
};

struct AoeNavPortal {
    glm::vec2 left{0.f};
    glm::vec2 right{0.f};
};

struct AoeNavCorridor {
    AoeNavCorridorStatus status = AoeNavCorridorStatus::NavMeshUnavailable;
    glm::vec2 start_on_mesh{0.f};
    glm::vec2 goal_on_mesh{0.f};
    std::vector<AoeNavPolygonId> polygons;
    std::vector<AoeNavPortal> portals;
    std::uint64_t map_revision = 0;
};

struct AoeNavDebugPolygon {
    AoeNavPolygonId id = 0;
    std::vector<glm::vec2> vertices;
};

struct AoeNavMeshDiagnostics {
    double build_ms = 0.0;
    std::uint64_t query_count = 0;
    double query_total_ms = 0.0;
    double query_max_ms = 0.0;
    std::size_t polygon_count = 0;

    double query_average_ms() const {
        return query_count ? query_total_ms / static_cast<double>(query_count)
                           : 0.0;
    }
};

class AoeNavMeshResource {
public:
    AoeNavMeshResource();
    ~AoeNavMeshResource();
    AoeNavMeshResource(AoeNavMeshResource&&) noexcept;
    AoeNavMeshResource& operator=(AoeNavMeshResource&&) noexcept;
    AoeNavMeshResource(const AoeNavMeshResource&) = delete;
    AoeNavMeshResource& operator=(const AoeNavMeshResource&) = delete;

    AoeNavMeshStatus status() const { return status_; }
    bool queryable() const;
    bool stale(std::uint64_t logic_map_revision) const;
    std::uint64_t source_map_revision() const { return source_map_revision_; }
    const std::string& error() const { return error_; }
    const AoeNavMeshBuildSettings& settings() const { return settings_; }
    const AoeNavMeshDiagnostics& diagnostics() const { return diagnostics_; }
    const std::vector<AoeNavDebugPolygon>& debug_polygons() const {
        return debug_polygons_;
    }

    AoeNavCorridor find_corridor(glm::vec2 start, glm::vec2 goal) const;
    void clear();

private:
    struct Impl;
    friend class AoeTiledBaseToNavMesh;
    friend void aoe_nav_mesh_build_system(EcsWorld&);

    std::unique_ptr<Impl> impl_;
    AoeNavMeshStatus status_ = AoeNavMeshStatus::Uninitialized;
    std::uint64_t source_map_revision_ = 0;
    std::uint64_t warned_stale_revision_ = 0;
    AoeNavMeshBuildSettings settings_;
    std::string error_;
    std::vector<AoeNavDebugPolygon> debug_polygons_;
    mutable AoeNavMeshDiagnostics diagnostics_;
};

class AoeTiledBaseToNavMesh {
public:
    static bool build(const AoeLogicMap&, const AoeNavMeshBuildSettings&,
                      AoeNavMeshResource&);
};

// Runs after runtime-static obstacle synchronization and before squad path
// requests. The first version builds once; revision-driven rebuilding remains
// an explicit TODO.
void aoe_nav_mesh_build_system(EcsWorld&);

} // namespace gld::ecs::aoe
