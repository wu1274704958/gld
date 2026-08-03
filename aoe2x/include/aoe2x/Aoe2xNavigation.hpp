#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <aoe/AoeMap.hpp>
#include <aoe/AoeGameplayComponents.hpp>
#include <aoe2x/Aoe2xGameplaySystem.hpp>

namespace gld::ecs::aoe2x {

struct Aoe2xNavigationDestination { glm::vec2 value{0.f}; };
enum class Aoe2xRouteStatus : std::uint8_t {
    Pending, Ready, NoPath, Invalid
};
struct Aoe2xRoutePlan {
    std::vector<glm::vec2> waypoints;
    std::optional<float> total_cost;
    Aoe2xRouteStatus status = Aoe2xRouteStatus::Pending;
};

struct Aoe2xPathfindingSettings {
    std::uint32_t cluster_size = 16;
    bool direct_path_fast_path = true;
    // Collapse the raw grid cell-center waypoints with a line-of-sight
    // string-pull so straight stretches become single segments instead of an
    // 8-connected staircase that makes followers turn at every cell corner.
    bool smooth_route_line_of_sight = true;
};

struct Aoe2xPathfindingDiagnostics {
    std::uint64_t queries = 0;
    std::uint64_t direct_paths = 0;
    std::uint64_t cache_rebuilds = 0;
    std::uint64_t high_level_expanded = 0;
    std::uint64_t local_expanded = 0;
    std::uint64_t no_paths = 0;
    std::uint64_t waypoints_before_smoothing = 0;
    std::uint64_t waypoints_after_smoothing = 0;
};

struct Aoe2xHpaCache {
    struct Portal {
        std::uint32_t cell = 0;
        std::uint32_t cluster = 0;
    };
    struct Edge { std::uint32_t to = 0; float cost = 0.f; };
    struct Grid {
        std::uint64_t map_revision = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t cluster_size = 0;
        glm::uvec2 clearance_cells{0u};
        std::vector<std::uint8_t> passable;
        std::vector<Portal> portals;
        std::vector<std::vector<Edge>> edges;
        std::vector<std::vector<std::uint32_t>> cluster_portals;
    };
    std::unordered_map<std::uint64_t, Grid> grids;
};

struct Aoe2xPathfindingState {
    struct Record {
        glm::ivec2 start_cell{-1};
        glm::ivec2 goal_cell{-1};
        glm::vec2 start_position{0.f};
        glm::vec2 goal_position{0.f};
        glm::uvec2 clearance_cells{0u};
        std::uint64_t map_revision = 0;
    };
    std::unordered_map<entt::entity, Record> records;
};

struct Aoe2xPathfindingSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        aoe::AoePosition, aoe::AoeCollider, Aoe2xNavigationDestination>;
    using WriteOnlyComponents = Aoe2xComponentList<Aoe2xRoutePlan>;
    using ReadWriteComponents = Aoe2xComponentList<>;
    static constexpr std::string_view name = "aoe2x_pathfinding";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Navigation;

    static void run(EcsWorld&, std::uint64_t);
};
static_assert(Aoe2xGameplaySystem<Aoe2xPathfindingSystem>);

} // namespace gld::ecs::aoe2x
