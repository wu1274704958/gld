#include <aoe2x/Aoe2xNavigation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace gld::ecs::aoe2x {
namespace {
constexpr float Epsilon = 1e-5f;

struct OpenNode { std::uint32_t cell; float cost; float estimate; };
struct OpenCompare {
    bool operator()(const OpenNode& a, const OpenNode& b) const {
        if (std::abs(a.estimate - b.estimate) > Epsilon)
            return a.estimate > b.estimate;
        if (std::abs(a.cost - b.cost) > Epsilon) return a.cost > b.cost;
        return a.cell > b.cell;
    }
};

std::uint32_t cell_index(std::uint32_t width, int x, int y) {
    return static_cast<std::uint32_t>(y) * width + static_cast<std::uint32_t>(x);
}
glm::ivec2 cell_coord(std::uint32_t width, std::uint32_t cell) {
    return {static_cast<int>(cell % width), static_cast<int>(cell / width)};
}
float octile(glm::ivec2 a, glm::ivec2 b) {
    const int dx = std::abs(a.x - b.x), dy = std::abs(a.y - b.y);
    return static_cast<float>(std::max(dx, dy)) + .41421356237f *
        static_cast<float>(std::min(dx, dy));
}
glm::uvec2 clearance_key(const aoe::AoeLogicMap& map, const aoe::AoeCollider& c) {
    const float scale = 8.f / map.tile_size();
    return {static_cast<unsigned int>(std::ceil(std::max(0.f, c.radius_x) * scale)),
            static_cast<unsigned int>(std::ceil(std::max(0.f, c.radius_y) * scale))};
}
std::uint64_t grid_key(glm::uvec2 clearance, std::uint32_t cluster_size) {
    return (static_cast<std::uint64_t>(cluster_size) << 48u) |
           (static_cast<std::uint64_t>(clearance.x) << 24u) | clearance.y;
}
std::uint32_t cluster_id(const Aoe2xHpaCache::Grid& grid, std::uint32_t cell) {
    const auto p = cell_coord(grid.width, cell);
    const std::uint32_t clusters_x = (grid.width + grid.cluster_size - 1u) /
        grid.cluster_size;
    return static_cast<std::uint32_t>(p.y / static_cast<int>(grid.cluster_size)) *
        clusters_x + static_cast<std::uint32_t>(p.x / static_cast<int>(grid.cluster_size));
}

bool local_path(const Aoe2xHpaCache::Grid& grid, std::uint32_t start,
                std::uint32_t goal, std::uint32_t allowed_cluster,
                std::vector<std::uint32_t>* output, float* output_cost,
                std::uint64_t& expanded) {
    if (start >= grid.passable.size() || goal >= grid.passable.size() ||
        !grid.passable[start] || !grid.passable[goal]) return false;
    const std::size_t count = grid.passable.size();
    std::vector<float> costs(count, std::numeric_limits<float>::infinity());
    std::vector<std::uint32_t> parents(count, std::numeric_limits<std::uint32_t>::max());
    std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCompare> open;
    costs[start] = 0.f;
    parents[start] = start;
    open.push({start, 0.f, octile(cell_coord(grid.width, start), cell_coord(grid.width, goal))});
    constexpr glm::ivec2 directions[] = {
        {1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    while (!open.empty()) {
        const auto current = open.top(); open.pop();
        if (current.cost > costs[current.cell] + Epsilon) continue;
        ++expanded;
        if (current.cell == goal) {
            if (output_cost) *output_cost = current.cost;
            if (output) {
                output->clear();
                for (auto cursor = goal;; cursor = parents[cursor]) {
                    output->push_back(cursor);
                    if (cursor == start) break;
                }
                std::reverse(output->begin(), output->end());
            }
            return true;
        }
        const auto from = cell_coord(grid.width, current.cell);
        for (const auto delta : directions) {
            const glm::ivec2 next = from + delta;
            if (next.x < 0 || next.y < 0 || next.x >= static_cast<int>(grid.width) ||
                next.y >= static_cast<int>(grid.height)) continue;
            const auto next_cell = cell_index(grid.width, next.x, next.y);
            if (!grid.passable[next_cell] ||
                cluster_id(grid, next_cell) != allowed_cluster) continue;
            if (delta.x && delta.y) {
                const auto a = cell_index(grid.width, from.x + delta.x, from.y);
                const auto b = cell_index(grid.width, from.x, from.y + delta.y);
                if (!grid.passable[a] || !grid.passable[b]) continue;
            }
            const float step = delta.x && delta.y ? 1.41421356237f : 1.f;
            const float next_cost = current.cost + step;
            if (next_cost + Epsilon >= costs[next_cell]) continue;
            costs[next_cell] = next_cost;
            parents[next_cell] = current.cell;
            open.push({next_cell, next_cost,
                next_cost + octile(next, cell_coord(grid.width, goal))});
        }
    }
    return false;
}

void add_edge(Aoe2xHpaCache::Grid& grid, std::uint32_t from,
              std::uint32_t to, float cost) {
    grid.edges[from].push_back({to, cost});
}

Aoe2xHpaCache::Grid& acquire_grid(EcsWorld& world, const aoe::AoeLogicMap& map,
                                   glm::uvec2 clearance,
                                   const Aoe2xPathfindingSettings& settings) {
    auto& cache = world.resource_or_add<Aoe2xHpaCache>();
    const auto cluster_size = std::max(1u, settings.cluster_size);
    const auto key = grid_key(clearance, cluster_size);
    auto& grid = cache.grids[key];
    if (grid.map_revision == map.static_revision() && grid.width == map.width() &&
        grid.height == map.height() && grid.cluster_size == cluster_size)
        return grid;
    grid = {};
    grid.map_revision = map.static_revision();
    grid.width = map.width(); grid.height = map.height();
    grid.cluster_size = cluster_size;
    grid.clearance_cells = clearance;
    const glm::vec2 baked_clearance = glm::vec2(clearance) * map.tile_size() / 8.f;
    grid.passable.resize(static_cast<std::size_t>(grid.width) * grid.height);
    for (std::uint32_t y = 0; y < grid.height; ++y)
        for (std::uint32_t x = 0; x < grid.width; ++x)
            grid.passable[cell_index(grid.width, x, y)] =
                map.cell_traversable(static_cast<int>(x), static_cast<int>(y),
                                     baked_clearance);
    const std::uint32_t clusters_x = (grid.width + grid.cluster_size - 1u) /
        grid.cluster_size;
    const std::uint32_t clusters_y = (grid.height + grid.cluster_size - 1u) /
        grid.cluster_size;
    grid.cluster_portals.resize(static_cast<std::size_t>(clusters_x) * clusters_y);
    const auto add_portal_pair = [&](std::uint32_t a, std::uint32_t b) {
        const auto ia = static_cast<std::uint32_t>(grid.portals.size());
        grid.portals.push_back({a, cluster_id(grid, a)});
        const auto ib = static_cast<std::uint32_t>(grid.portals.size());
        grid.portals.push_back({b, cluster_id(grid, b)});
        grid.cluster_portals[grid.portals[ia].cluster].push_back(ia);
        grid.cluster_portals[grid.portals[ib].cluster].push_back(ib);
        grid.edges.resize(grid.portals.size());
        add_edge(grid, ia, ib, 1.f); add_edge(grid, ib, ia, 1.f);
    };
    for (std::uint32_t x = grid.cluster_size; x < grid.width; x += grid.cluster_size)
        for (std::uint32_t y = 0; y < grid.height;) {
            if (!grid.passable[cell_index(grid.width, x - 1u, y)] ||
                !grid.passable[cell_index(grid.width, x, y)]) { ++y; continue; }
            const auto first = y;
            while (y < grid.height && grid.passable[cell_index(grid.width, x - 1u, y)] &&
                   grid.passable[cell_index(grid.width, x, y)]) ++y;
            // A single midpoint loses connectivity when the two sides of a
            // wide entrance lead to different regions inside either cluster.
            // Preserve the exact grid topology by retaining every crossing.
            for (auto portal_y = first; portal_y < y; ++portal_y)
                add_portal_pair(cell_index(grid.width, x - 1u, portal_y),
                                cell_index(grid.width, x, portal_y));
        }
    for (std::uint32_t y = grid.cluster_size; y < grid.height; y += grid.cluster_size)
        for (std::uint32_t x = 0; x < grid.width;) {
            if (!grid.passable[cell_index(grid.width, x, y - 1u)] ||
                !grid.passable[cell_index(grid.width, x, y)]) { ++x; continue; }
            const auto first = x;
            while (x < grid.width && grid.passable[cell_index(grid.width, x, y - 1u)] &&
                   grid.passable[cell_index(grid.width, x, y)]) ++x;
            // See the vertical-boundary case above: a complete HPA* graph
            // needs every traversable boundary crossing, not a representative.
            for (auto portal_x = first; portal_x < x; ++portal_x)
                add_portal_pair(cell_index(grid.width, portal_x, y - 1u),
                                cell_index(grid.width, portal_x, y));
        }
    std::uint64_t ignored = 0;
    for (std::uint32_t cluster = 0; cluster < grid.cluster_portals.size(); ++cluster) {
        const auto& portals = grid.cluster_portals[cluster];
        for (std::size_t a = 0; a < portals.size(); ++a)
            for (std::size_t b = a + 1; b < portals.size(); ++b) {
                float cost = 0.f;
                if (!local_path(grid, grid.portals[portals[a]].cell,
                                grid.portals[portals[b]].cell, cluster,
                                nullptr, &cost, ignored)) continue;
                add_edge(grid, portals[a], portals[b], cost);
                add_edge(grid, portals[b], portals[a], cost);
            }
    }
    ++world.resource_or_add<Aoe2xPathfindingDiagnostics>().cache_rebuilds;
    return grid;
}

void append_cells(std::vector<std::uint32_t>& result,
                  const std::vector<std::uint32_t>& cells) {
    for (const auto cell : cells)
        if (result.empty() || result.back() != cell) result.push_back(cell);
}

float route_cost(glm::vec2 start, const std::vector<glm::vec2>& waypoints) {
    float cost = 0.f;
    for (const auto waypoint : waypoints) {
        cost += glm::length(waypoint - start);
        start = waypoint;
    }
    return cost;
}
} // namespace

void Aoe2xPathfindingSystem::run(EcsWorld& world, std::uint64_t) {
    auto* map = world.try_resource<aoe::AoeLogicMap>();
    auto& reg = world.reg();
    auto& states = world.resource_or_add<Aoe2xPathfindingState>();
    auto& diagnostics = world.resource_or_add<Aoe2xPathfindingDiagnostics>();
    const auto settings = world.resource_or_add<Aoe2xPathfindingSettings>();

    for (auto it = states.records.begin(); it != states.records.end();) {
        const auto entity = it->first;
        if (!reg.valid(entity) ||
            !reg.all_of<aoe::AoePosition, aoe::AoeCollider,
                        Aoe2xNavigationDestination>(entity))
            it = states.records.erase(it);
        else
            ++it;
    }

    auto view = reg.view<const aoe::AoePosition, const aoe::AoeCollider,
                         const Aoe2xNavigationDestination>();
    for (const auto entity : view) {
        const bool had_route = reg.all_of<Aoe2xRoutePlan>(entity);
        auto& route = reg.get_or_emplace<Aoe2xRoutePlan>(entity);
        if (!map || !map->valid()) {
            route.waypoints.clear();
            route.total_cost.reset();
            route.status = Aoe2xRouteStatus::Invalid;
            states.records.erase(entity);
            continue;
        }
        const auto& position = view.get<const aoe::AoePosition>(entity).value;
        const auto& collider = view.get<const aoe::AoeCollider>(entity);
        const auto& destination = view.get<const Aoe2xNavigationDestination>(entity).value;
        const auto start_cell = map->world_to_cell(position);
        const auto goal_cell = map->world_to_cell(destination);
        const auto clearance = clearance_key(*map, collider);
        if (!start_cell || !goal_cell || !map->contains(position) || !map->contains(destination) ||
            map->position_blocked(destination, {collider.radius_x, collider.radius_y})) {
            route.waypoints.clear();
            route.total_cost.reset();
            route.status = Aoe2xRouteStatus::Invalid;
            states.records.erase(entity);
            ++diagnostics.no_paths;
            continue;
        }
        const Aoe2xPathfindingState::Record current{
            {start_cell->x, start_cell->y}, {goal_cell->x, goal_cell->y}, position,
            destination, clearance, map->static_revision()};
        if (const auto it = states.records.find(entity); had_route &&
            route.status != Aoe2xRouteStatus::Pending &&
            it != states.records.end() &&
            it->second.start_cell == current.start_cell && it->second.goal_cell == current.goal_cell &&
            it->second.start_position == current.start_position &&
            it->second.goal_position == current.goal_position &&
            it->second.clearance_cells == current.clearance_cells &&
            it->second.map_revision == current.map_revision) continue;
        states.records[entity] = current;
        ++diagnostics.queries;
        route.waypoints.clear();
        route.total_cost.reset();
        route.status = Aoe2xRouteStatus::Pending;
        const glm::vec2 radii{collider.radius_x, collider.radius_y};
        if (settings.direct_path_fast_path &&
            map->static_safe_fraction(position, destination, radii) >= 1.f - Epsilon) {
            route.waypoints.push_back(destination);
            route.total_cost = glm::length(destination - position);
            route.status = Aoe2xRouteStatus::Ready;
            ++diagnostics.direct_paths;
            continue;
        }
        auto& grid = acquire_grid(world, *map, clearance, settings);
        const auto start = cell_index(grid.width, start_cell->x, start_cell->y);
        const auto goal = cell_index(grid.width, goal_cell->x, goal_cell->y);
        if (!grid.passable[start] || !grid.passable[goal]) {
            route.status = Aoe2xRouteStatus::NoPath;
            ++diagnostics.no_paths;
            continue;
        }
        const auto start_cluster = cluster_id(grid, start), goal_cluster = cluster_id(grid, goal);
        std::vector<std::uint32_t> cells;
        if (start_cluster == goal_cluster) {
            if (!local_path(grid, start, goal, start_cluster, &cells,
                            nullptr,
                            diagnostics.local_expanded)) {
                route.status = Aoe2xRouteStatus::NoPath;
                ++diagnostics.no_paths;
                continue;
            }
        } else {
            const auto& starts = grid.cluster_portals[start_cluster];
            const auto& ends = grid.cluster_portals[goal_cluster];
            const auto infinite = std::numeric_limits<float>::infinity();
            std::vector<float> distances(grid.portals.size(), infinite), end_cost(grid.portals.size(), infinite);
            std::vector<std::uint32_t> parents(grid.portals.size(), std::numeric_limits<std::uint32_t>::max());
            std::vector<std::vector<std::uint32_t>> start_paths(grid.portals.size()), end_paths(grid.portals.size());
            std::priority_queue<OpenNode, std::vector<OpenNode>, OpenCompare> open;
            for (const auto portal : starts) {
                float cost = 0.f;
                if (!local_path(grid, start, grid.portals[portal].cell, start_cluster,
                                &start_paths[portal], &cost,
                                diagnostics.local_expanded)) continue;
                distances[portal] = cost;
                open.push({portal, distances[portal], distances[portal]});
            }
            for (const auto portal : ends) {
                float cost = 0.f;
                if (local_path(grid, grid.portals[portal].cell, goal, goal_cluster,
                               &end_paths[portal], &cost,
                               diagnostics.local_expanded))
                    end_cost[portal] = cost;
            }
            float best = infinite; std::uint32_t last = std::numeric_limits<std::uint32_t>::max();
            while (!open.empty()) {
                const auto current_node = open.top(); open.pop();
                if (current_node.cost > distances[current_node.cell] + Epsilon) continue;
                if (current_node.cost >= best) break;
                ++diagnostics.high_level_expanded;
                if (end_cost[current_node.cell] < infinite &&
                    current_node.cost + end_cost[current_node.cell] < best) {
                    best = current_node.cost + end_cost[current_node.cell]; last = current_node.cell;
                }
                for (const auto edge : grid.edges[current_node.cell]) {
                    const float cost = current_node.cost + edge.cost;
                    if (cost + Epsilon >= distances[edge.to]) continue;
                    distances[edge.to] = cost; parents[edge.to] = current_node.cell;
                    open.push({edge.to, cost, cost});
                }
            }
            if (last == std::numeric_limits<std::uint32_t>::max()) {
                route.status = Aoe2xRouteStatus::NoPath;
                ++diagnostics.no_paths;
                continue;
            }
            std::vector<std::uint32_t> portals;
            for (auto cursor = last; cursor != std::numeric_limits<std::uint32_t>::max(); cursor = parents[cursor])
                portals.push_back(cursor);
            std::reverse(portals.begin(), portals.end());
            append_cells(cells, start_paths[portals.front()]);
            for (std::size_t i = 1; i < portals.size(); ++i) {
                const auto a = portals[i - 1u], b = portals[i];
                if (grid.portals[a].cluster == grid.portals[b].cluster) {
                    std::vector<std::uint32_t> part;
                    if (!local_path(grid, grid.portals[a].cell, grid.portals[b].cell,
                                    grid.portals[a].cluster, &part, nullptr,
                                    diagnostics.local_expanded))
                        { cells.clear(); break; }
                    append_cells(cells, part);
                } else if (cells.empty() || cells.back() != grid.portals[b].cell)
                    cells.push_back(grid.portals[b].cell);
            }
            if (cells.empty()) {
                route.status = Aoe2xRouteStatus::NoPath;
                ++diagnostics.no_paths;
                continue;
            }
            append_cells(cells, end_paths[last]);
        }
        for (std::size_t i = 1; i + 1 < cells.size(); ++i)
            route.waypoints.push_back(map->cell_center(cell_coord(grid.width, cells[i]).x,
                                                        cell_coord(grid.width, cells[i]).y));
        route.waypoints.push_back(destination);
        route.total_cost = route_cost(position, route.waypoints);
        route.status = Aoe2xRouteStatus::Ready;
    }
}

} // namespace gld::ecs::aoe2x
