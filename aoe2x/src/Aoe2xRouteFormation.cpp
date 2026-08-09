#include <aoe2x/Aoe2xRouteFormation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <tuple>

namespace gld::ecs::aoe2x {
namespace {

constexpr float Epsilon = 1e-5f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

glm::vec2 normalized_or(glm::vec2 value, glm::vec2 fallback) {
    const float length_squared = glm::dot(value, value);
    return finite(value) && length_squared > Epsilon * Epsilon
        ? value / std::sqrt(length_squared) : fallback;
}

bool valid_options(const FormationSpawnOptions& options) {
    return options.count > 0 && finite(options.center) &&
        std::isfinite(options.spacing) && options.spacing >= 0.f &&
        std::isfinite(options.unit_radius) && options.unit_radius > 0.f &&
        std::isfinite(options.movement_speed) && options.movement_speed > 0.f &&
        finite(options.forward) &&
        glm::dot(options.forward, options.forward) > Epsilon * Epsilon &&
        (!options.spawn_adjustment_radius ||
         (std::isfinite(*options.spawn_adjustment_radius) &&
          *options.spawn_adjustment_radius >= 0.f));
}

bool valid_layout(const FormationLayout& layout, std::size_t count) {
    if (layout.relative_positions.size() != count ||
        layout.captain_index >= count || layout.column_count == 0)
        return false;
    return std::all_of(layout.relative_positions.begin(),
                       layout.relative_positions.end(), finite);
}

// Formation generators expose a captain-first traversal order. Route squads
// keep that order only as stable slot identity; no captain component is made.
std::vector<glm::vec2> ordered_local_offsets(const FormationLayout& layout) {
    std::vector<glm::vec2> result;
    result.reserve(layout.relative_positions.size());
    for (std::size_t i = 0; i < layout.relative_positions.size(); ++i)
        result.push_back(layout.relative_positions[
            (layout.captain_index + i) % layout.relative_positions.size()]);
    return result;
}

glm::vec2 rotate_local(glm::vec2 local, glm::vec2 forward) {
    const glm::vec2 right{forward.y, -forward.x};
    return right * local.x + forward * local.y;
}

void fail_spawn(entt::registry& reg, entt::entity squad,
                FormationSpawnState& state) {
    state.status = FormationSpawnStatus::Failed;
    reg.remove<FormationSpawnRequest>(squad);
}

std::vector<glm::vec2> adjustment_offsets(float radius, float step) {
    std::vector<glm::vec2> result{{0.f, 0.f}};
    if (!(radius > Epsilon)) return result;
    const int extent = std::max(1, static_cast<int>(std::ceil(radius / step)));
    for (int y = -extent; y <= extent; ++y) {
        for (int x = -extent; x <= extent; ++x) {
            if (!x && !y) continue;
            const glm::vec2 offset{x * step, y * step};
            if (glm::dot(offset, offset) <= radius * radius + Epsilon)
                result.push_back(offset);
        }
    }
    std::stable_sort(result.begin() + 1, result.end(),
        [](glm::vec2 lhs, glm::vec2 rhs) {
            return std::tuple{glm::dot(lhs, lhs), lhs.y, lhs.x} <
                   std::tuple{glm::dot(rhs, rhs), rhs.y, rhs.x};
        });
    return result;
}

bool positions_overlap(glm::vec2 candidate,
                       const std::vector<glm::vec2>& assigned,
                       float diameter) {
    const float minimum_squared =
        std::max(0.f, diameter - Epsilon) *
        std::max(0.f, diameter - Epsilon);
    return std::any_of(assigned.begin(), assigned.end(),
        [&](glm::vec2 other) {
            return glm::dot(candidate - other, candidate - other) <
                   minimum_squared;
        });
}

std::vector<glm::vec2> place_members(const aoe::AoeLogicMap& map,
    glm::vec2 center, const std::vector<glm::vec2>& world_offsets,
    float unit_radius, float adjustment_radius) {
    std::vector<glm::vec2> assigned;
    assigned.reserve(world_offsets.size());
    const auto candidates = adjustment_offsets(
        adjustment_radius, std::max(map.tile_size() / 8.f, Epsilon));
    const glm::vec2 clearance{unit_radius, unit_radius};
    for (const auto ideal_offset : world_offsets) {
        const glm::vec2 ideal = center + ideal_offset;
        const auto found = std::find_if(candidates.begin(), candidates.end(),
            [&](glm::vec2 adjustment) {
                const glm::vec2 candidate = ideal + adjustment;
                return !map.position_blocked(candidate, clearance) &&
                    !positions_overlap(candidate, assigned, unit_radius * 2.f);
            });
        if (found == candidates.end()) return {};
        assigned.push_back(ideal + *found);
    }
    return assigned;
}

entt::entity spawn_member(EcsWorld& world,
    const FormationSpawnOptions& options, glm::vec2 position,
    glm::vec2 forward, entt::entity squad, std::uint32_t slot) {
    auto& reg = world.reg();
    const auto unit = world.spawn();
    reg.emplace<aoe::AoePosition>(unit, aoe::AoePosition{position});
    reg.emplace<aoe::AoePositionHistory>(unit,
        aoe::AoePositionHistory{position});
    reg.emplace<aoe::AoeCollider>(unit, aoe::AoeCollider{
        options.unit_radius, options.unit_radius,
        options.unit_radius * 2.f});
    reg.emplace<aoe::AoeMovement>(unit,
        aoe::AoeMovement{options.movement_speed});
    reg.emplace<aoe::AoeLocomotionState>(unit);
    reg.emplace<aoe::AoeDirection>(unit, aoe::AoeDirection{forward});
    reg.emplace<RouteSquadMemberInfo>(unit,
        RouteSquadMemberInfo{squad, slot});
    return unit;
}

bool active_member(const entt::registry& reg, entt::entity unit,
                   entt::entity squad) {
    if (!reg.valid(unit) ||
        !reg.all_of<RouteSquadMemberInfo, aoe::AoePosition,
                    aoe::AoeCollider>(unit) ||
        reg.any_of<Aoe2xUnitState, Aoe2xPooledUnit>(unit))
        return false;
    return reg.get<RouteSquadMemberInfo>(unit).squad == squad;
}

std::vector<entt::entity> active_members(const entt::registry& reg,
    entt::entity squad, const SquadInfo& info) {
    std::vector<entt::entity> result;
    result.reserve(info.units.size());
    for (const auto unit : info.units)
        if (active_member(reg, unit, squad)) result.push_back(unit);
    return result;
}

void clear_assigned_routes(entt::registry& reg, entt::entity squad,
                           const SquadInfo& info) {
    for (const auto unit : info.units) {
        if (!reg.valid(unit)) continue;
        const auto* assignment =
            reg.try_get<RouteSquadRouteAssignment>(unit);
        if (!assignment || assignment->squad != squad) continue;
        reg.remove<RouteSquadRouteAssignment, Aoe2xRoutePlan>(unit);
    }
}

void finish_planning(entt::registry& reg, entt::entity squad,
                     FormationAttackMove& order,
                     FormationAttackMoveStatus status) {
    order.status = status;
    reg.remove<Aoe2xNavigationDestination, Aoe2xRoutePlan,
               RouteSquadPlanningState>(squad);
}

struct Profile {
    std::vector<glm::vec2> offsets;
    std::uint32_t columns = 0;
};

std::vector<Profile> build_profiles(const FormationRegistry& registry,
    FormationType formation, std::size_t count, float cell_size) {
    const auto full = registry.generate(formation,
        FormationGenerateContext{static_cast<std::uint32_t>(count), cell_size});
    if (!valid_layout(full, count)) return {};
    std::vector<Profile> profiles;
    for (std::uint32_t columns = full.column_count; columns >= 1; --columns) {
        const auto layout = registry.generate(formation,
            FormationGenerateContext{static_cast<std::uint32_t>(count),
                                     cell_size, columns});
        if (!valid_layout(layout, count)) return {};
        auto offsets = ordered_local_offsets(layout);
        if (profiles.empty() || profiles.back().offsets != offsets)
            profiles.push_back(Profile{std::move(offsets),
                                       layout.column_count});
        if (columns == 1) break;
    }
    return profiles;
}

std::vector<glm::vec2> resample_center_route(glm::vec2 start,
    const Aoe2xRoutePlan& route, float maximum_step) {
    std::vector<glm::vec2> samples{start};
    for (const glm::vec2 endpoint : route.waypoints) {
        const glm::vec2 from = samples.back();
        const float distance = glm::length(endpoint - from);
        const int steps = std::max(1,
            static_cast<int>(std::ceil(distance / maximum_step)));
        for (int step = 1; step <= steps; ++step)
            samples.push_back(from + (endpoint - from) *
                (static_cast<float>(step) / static_cast<float>(steps)));
    }
    return samples;
}

std::vector<glm::vec2> sample_headings(
    const std::vector<glm::vec2>& samples, glm::vec2 arrival_facing) {
    std::vector<glm::vec2> headings(samples.size(), arrival_facing);
    for (std::size_t i = 0; i + 1 < samples.size(); ++i)
        headings[i] = normalized_or(samples[i + 1] - samples[i],
                                    i ? headings[i - 1] : arrival_facing);
    return headings;
}

glm::vec2 slot_position(const std::vector<glm::vec2>& samples,
    const std::vector<glm::vec2>& headings,
    const std::vector<Profile>& profiles, std::size_t sample,
    std::size_t profile, std::size_t slot) {
    return samples[sample] +
        rotate_local(profiles[profile].offsets[slot], headings[sample]);
}

bool transition_safe(const aoe::AoeLogicMap& map,
    const entt::registry& reg, const std::vector<entt::entity>& members,
    const std::vector<glm::vec2>& samples,
    const std::vector<glm::vec2>& headings,
    const std::vector<Profile>& profiles, std::size_t sample,
    std::optional<std::size_t> previous_profile, std::size_t profile) {
    for (const auto unit : members) {
        const auto& member = reg.get<RouteSquadMemberInfo>(unit);
        if (member.slot_index >= profiles[profile].offsets.size()) return false;
        const glm::vec2 from = previous_profile
            ? slot_position(samples, headings, profiles, sample - 1,
                            *previous_profile, member.slot_index)
            : reg.get<aoe::AoePosition>(unit).value;
        const glm::vec2 to = slot_position(samples, headings, profiles, sample,
                                           profile, member.slot_index);
        const auto& collider = reg.get<aoe::AoeCollider>(unit);
        if (map.static_safe_fraction(from, to,
                {collider.radius_x, collider.radius_y}) < 1.f - Epsilon)
            return false;
    }
    return true;
}

struct DpState {
    std::uint64_t narrowness = std::numeric_limits<std::uint64_t>::max();
    std::uint32_t transitions = std::numeric_limits<std::uint32_t>::max();
    int previous = -1;
    bool reachable = false;
};

bool better(std::uint64_t narrowness, std::uint32_t transitions,
            const DpState& current) {
    return !current.reachable ||
        std::tuple{narrowness, transitions} <
        std::tuple{current.narrowness, current.transitions};
}

std::vector<std::size_t> choose_profiles(const aoe::AoeLogicMap& map,
    const entt::registry& reg, const std::vector<entt::entity>& members,
    const std::vector<glm::vec2>& samples,
    const std::vector<glm::vec2>& headings,
    const std::vector<Profile>& profiles) {
    std::vector<std::vector<DpState>> dp(samples.size(),
        std::vector<DpState>(profiles.size()));
    for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
        if (!transition_safe(map, reg, members, samples, headings, profiles,
                             0, std::nullopt, profile))
            continue;
        dp[0][profile] = DpState{profile, 0, -1, true};
    }
    for (std::size_t sample = 1; sample < samples.size(); ++sample) {
        for (std::size_t profile = 0; profile < profiles.size(); ++profile) {
            for (std::size_t previous = 0; previous < profiles.size(); ++previous) {
                if (!dp[sample - 1][previous].reachable ||
                    std::abs(static_cast<int>(profile) -
                             static_cast<int>(previous)) > 1 ||
                    !transition_safe(map, reg, members, samples, headings,
                                     profiles, sample, previous, profile))
                    continue;
                const auto narrowness = dp[sample - 1][previous].narrowness +
                    static_cast<std::uint64_t>(profile);
                const auto transitions = dp[sample - 1][previous].transitions +
                    static_cast<std::uint32_t>(profile != previous);
                if (better(narrowness, transitions, dp[sample][profile]))
                    dp[sample][profile] = DpState{
                        narrowness, transitions,
                        static_cast<int>(previous), true};
            }
        }
    }
    std::size_t selected = profiles.size();
    for (std::size_t profile = 0; profile < profiles.size(); ++profile)
        if (dp.back()[profile].reachable &&
            (selected == profiles.size() ||
             std::tuple{dp.back()[profile].narrowness,
                        dp.back()[profile].transitions, profile} <
             std::tuple{dp.back()[selected].narrowness,
                        dp.back()[selected].transitions, selected}))
            selected = profile;
    if (selected == profiles.size()) return {};
    std::vector<std::size_t> result(samples.size());
    for (std::size_t sample = samples.size(); sample-- > 0;) {
        result[sample] = selected;
        if (sample) selected = static_cast<std::size_t>(
            dp[sample][selected].previous);
    }
    return result;
}

Aoe2xRoutePlan member_route(const entt::registry& reg, entt::entity unit,
    const std::vector<glm::vec2>& samples,
    const std::vector<glm::vec2>& headings,
    const std::vector<Profile>& profiles,
    const std::vector<std::size_t>& selected) {
    Aoe2xRoutePlan route;
    route.status = Aoe2xRouteStatus::Ready;
    glm::vec2 previous = reg.get<aoe::AoePosition>(unit).value;
    float cost = 0.f;
    const auto slot = reg.get<RouteSquadMemberInfo>(unit).slot_index;
    for (std::size_t sample = 0; sample < samples.size(); ++sample) {
        const glm::vec2 point = slot_position(samples, headings, profiles,
                                              sample, selected[sample], slot);
        const float distance = glm::length(point - previous);
        if (distance <= Epsilon) continue;
        route.waypoints.push_back(point);
        cost += distance;
        previous = point;
    }
    route.total_cost = cost;
    return route;
}

} // namespace

void RouteSquadSpawnSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    const auto* registry = world.try_resource<FormationRegistry>();
    const auto* map = world.try_resource<aoe::AoeLogicMap>();
    const auto view = reg.view<const FormationSpawnRequest,
                               FormationSpawnState,
                               const aoe::AoePosition>();
    for (const auto squad : view) {
        auto& state = view.get<FormationSpawnState>(squad);
        if (state.status != FormationSpawnStatus::Pending) continue;
        const auto options = view.get<const FormationSpawnRequest>(squad).options;
        const glm::vec2 center = view.get<const aoe::AoePosition>(squad).value;
        if (!registry || !map || !map->valid() || !valid_options(options)) {
            fail_spawn(reg, squad, state);
            continue;
        }
        const glm::vec2 forward = normalized_or(options.forward, {1.f, 0.f});
        const float cell_size = options.unit_radius * 2.f + options.spacing;
        const auto layout = registry->generate(options.formation,
            FormationGenerateContext{options.count, cell_size});
        if (!valid_layout(layout, options.count)) {
            fail_spawn(reg, squad, state);
            continue;
        }
        const auto local_offsets = ordered_local_offsets(layout);
        std::vector<glm::vec2> world_offsets;
        world_offsets.reserve(local_offsets.size());
        for (const auto offset : local_offsets)
            world_offsets.push_back(rotate_local(offset, forward));
        const float adjustment = options.spawn_adjustment_radius.value_or(
            map->tile_size());
        const auto positions = place_members(*map, center, world_offsets,
                                             options.unit_radius, adjustment);
        if (positions.size() != options.count) {
            fail_spawn(reg, squad, state);
            continue;
        }

        SquadInfo info;
        info.formation = options.formation;
        info.units.reserve(options.count);
        for (std::uint32_t slot = 0; slot < options.count; ++slot)
            info.units.push_back(spawn_member(world, options, positions[slot],
                                              forward, squad, slot));
        reg.emplace<SquadInfo>(squad, std::move(info));
        reg.emplace<RouteSquadMetadata>(squad,
            RouteSquadMetadata{local_offsets, cell_size, forward});
        reg.emplace<FormationAttackMove>(squad,
            FormationAttackMove{{0.f, 0.f}, FormationAttackMoveStatus::Idle,
                                0, forward});
        state.status = FormationSpawnStatus::Ready;
        reg.remove<FormationSpawnRequest>(squad);
    }
}

void RouteSquadCommandSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    auto& queue = world.resource_or_add<FormationCommands>().queue;
    auto commands = std::move(queue);
    queue.clear();
    for (const auto& command : commands) {
        if (!reg.valid(command.squad) ||
            !reg.all_of<SquadInfo, RouteSquadMetadata,
                        FormationSpawnState>(command.squad) ||
            reg.get<FormationSpawnState>(command.squad).status !=
                FormationSpawnStatus::Ready || !finite(command.destination))
            continue;
        const auto& info = reg.get<SquadInfo>(command.squad);
        const auto members = active_members(reg, command.squad, info);
        auto& order = reg.get_or_emplace<FormationAttackMove>(command.squad);
        clear_assigned_routes(reg, command.squad, info);
        if (members.empty()) {
            finish_planning(reg, command.squad, order,
                            FormationAttackMoveStatus::Failed);
            continue;
        }
        glm::vec2 centroid{0.f};
        float radius_x = 0.f;
        float radius_y = 0.f;
        for (const auto unit : members) {
            centroid += reg.get<aoe::AoePosition>(unit).value;
            const auto& collider = reg.get<aoe::AoeCollider>(unit);
            radius_x = std::max(radius_x, collider.radius_x);
            radius_y = std::max(radius_y, collider.radius_y);
        }
        centroid /= static_cast<float>(members.size());
        const auto& metadata = reg.get<RouteSquadMetadata>(command.squad);
        const glm::vec2 facing = command.destination_facing
            ? normalized_or(*command.destination_facing,
                            metadata.spawn_forward)
            : normalized_or(command.destination - centroid,
                            metadata.spawn_forward);
        order.destination = command.destination;
        order.destination_facing = facing;
        order.status = FormationAttackMoveStatus::Running;
        ++order.revision;
        reg.emplace_or_replace<aoe::AoePosition>(command.squad,
            aoe::AoePosition{centroid});
        reg.emplace_or_replace<aoe::AoeCollider>(command.squad,
            aoe::AoeCollider{radius_x, radius_y,
                             std::max(radius_x, radius_y) * 2.f});
        reg.emplace_or_replace<Aoe2xNavigationDestination>(command.squad,
            Aoe2xNavigationDestination{command.destination});
        reg.emplace_or_replace<Aoe2xRoutePlan>(command.squad);
        reg.emplace_or_replace<RouteSquadPlanningState>(command.squad,
            RouteSquadPlanningState{RouteSquadPlanningPhase::PathPending,
                                    order.revision});
    }
}

void RouteSquadSplitSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    const auto* map = world.try_resource<aoe::AoeLogicMap>();
    const auto* registry = world.try_resource<FormationRegistry>();
    std::vector<entt::entity> squads;
    for (const auto squad : reg.view<SquadInfo, RouteSquadMetadata,
                                     FormationAttackMove,
                                     RouteSquadPlanningState,
                                     Aoe2xRoutePlan>())
        squads.push_back(squad);
    for (const auto squad : squads) {
        auto& order = reg.get<FormationAttackMove>(squad);
        auto& planning = reg.get<RouteSquadPlanningState>(squad);
        const auto& center_route = reg.get<Aoe2xRoutePlan>(squad);
        if (planning.revision != order.revision ||
            order.status != FormationAttackMoveStatus::Running)
            continue;
        if (center_route.status == Aoe2xRouteStatus::Pending) continue;
        if (!map || !map->valid() || !registry ||
            center_route.status != Aoe2xRouteStatus::Ready) {
            finish_planning(reg, squad, order,
                            FormationAttackMoveStatus::Failed);
            continue;
        }
        planning.phase = RouteSquadPlanningPhase::Splitting;
        const auto& info = reg.get<SquadInfo>(squad);
        const auto members = active_members(reg, squad, info);
        if (members.empty()) {
            finish_planning(reg, squad, order,
                            FormationAttackMoveStatus::Failed);
            continue;
        }
        const auto& metadata = reg.get<RouteSquadMetadata>(squad);
        const auto profiles = build_profiles(*registry, info.formation,
                                             info.units.size(),
                                             metadata.cell_size);
        const glm::vec2 center = reg.get<aoe::AoePosition>(squad).value;
        const auto samples = resample_center_route(
            center, center_route, map->tile_size());
        const auto headings = sample_headings(samples,
                                              order.destination_facing);
        const auto selected = choose_profiles(*map, reg, members, samples,
                                              headings, profiles);
        if (profiles.empty() || selected.size() != samples.size()) {
            finish_planning(reg, squad, order,
                            FormationAttackMoveStatus::Failed);
            continue;
        }
        std::vector<Aoe2xRoutePlan> routes;
        routes.reserve(members.size());
        for (const auto unit : members)
            routes.push_back(member_route(reg, unit, samples, headings,
                                          profiles, selected));
        const auto final_columns = profiles[selected.back()].columns;
        for (std::size_t i = 0; i < members.size(); ++i) {
            reg.emplace_or_replace<Aoe2xRoutePlan>(members[i],
                                                   std::move(routes[i]));
            reg.emplace_or_replace<RouteSquadRouteAssignment>(members[i],
                RouteSquadRouteAssignment{squad, order.revision,
                                          final_columns});
        }
        finish_planning(reg, squad, order,
                        FormationAttackMoveStatus::Completed);
    }
}

void RouteSquadCleanupSystem::run(EcsWorld& world, std::uint64_t) {
    auto& reg = world.reg();
    std::vector<entt::entity> inactive;
    for (const auto unit : reg.view<const Aoe2xUnitState>())
        inactive.push_back(unit);
    // Also tolerate running after the shared lifecycle system: Released state
    // has already been removed by then, but the pooled marker remains.
    for (const auto unit : reg.view<const Aoe2xPooledUnit>())
        if (std::find(inactive.begin(), inactive.end(), unit) == inactive.end())
            inactive.push_back(unit);
    for (const auto unit : inactive)
        reg.remove<RouteSquadMemberInfo, RouteSquadRouteAssignment,
                   Aoe2xRoutePlan>(unit);
}

} // namespace gld::ecs::aoe2x
