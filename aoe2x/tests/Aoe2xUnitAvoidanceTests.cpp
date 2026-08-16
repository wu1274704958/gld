#include <aoe2x/Aoe2xUnitAvoidance.hpp>

#include <cassert>
#include <cmath>
#include <vector>

#include <aoe/AoeMap.hpp>

using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2x;

namespace {
constexpr float Epsilon = 1e-4f;

Aoe2xUnitStep unit(std::uint32_t id, glm::vec2 position,
    glm::vec2 velocity, glm::vec2 target, float radius = .1f) {
    Aoe2xUnitStep result;
    result.entity = static_cast<entt::entity>(id);
    result.velocity = velocity;
    result.target = target;
    result.direction = glm::length(velocity) > Epsilon
        ? glm::normalize(velocity) : glm::vec2{0.f, 1.f};
    result.speed_limit = glm::length(velocity);
    result.start_position = position;
    result.radii = {radius, radius};
    return result;
}

Aoe2xUnitAvoidanceSolveStats solve(std::vector<Aoe2xUnitStep>& units,
    const Aoe2xUnitAvoidanceSettings& settings = {},
    const AoeLogicMap* map = nullptr) {
    Aoe2xUnitAvoidanceWorkspace workspace;
    return resolve_aoe2x_unit_overlaps(
        units, 1.f, settings, map, workspace);
}

float gap(const Aoe2xUnitStep& first, const Aoe2xUnitStep& second) {
    const glm::vec2 first_position =
        first.start_position + first.velocity;
    const glm::vec2 second_position =
        second.start_position + second.velocity;
    return glm::length(second_position - first_position) -
        std::max(first.radii.x, first.radii.y) -
        std::max(second.radii.x, second.radii.y);
}

AoeLogicMap obstacle_map() {
    AoeMapDefinition definition;
    definition.id = "aoe2x_unit_avoidance_test";
    definition.width = definition.height = 10;
    definition.tile_size = 1.f;
    definition.heights.resize(11u * 11u, 0.f);
    definition.static_obstacles.push_back(AoeStaticObstacleDesc{
        "wall", AoeStaticObstacleShape::Aabb, {2.f, 1.f}, {.2f, .4f}});
    return AoeLogicMap(definition);
}
} // namespace

int main() {
    {
        std::vector<Aoe2xUnitStep> units;
        units.reserve(100);
        for (std::uint32_t i = 0; i < 100; ++i) {
            const glm::vec2 position{static_cast<float>(i) * 10.f, 0.f};
            units.push_back(unit(i + 1u, position, {}, position));
        }
        units[1].target += glm::vec2{2000.f, 0.f};
        units.back().target += glm::vec2{2000.f, 0.f};
        solve(units);
        assert(units.front().priority == 9900u);
        assert(units.back().priority == 100u);
        assert(units[2].priority > units[1].priority);
    }

    {
        // The captain keeps its ideal position. The lower-priority follower
        // receives the minimum translation needed to separate both circles.
        std::vector<Aoe2xUnitStep> units{
            unit(1, {-1.f, 0.f}, {1.f, 0.f}, {0.f, 0.f}),
            unit(2, {0.f, -1.f}, {0.f, .9f}, {0.f, 2.f})};
        solve(units);
        assert(glm::length(units[0].velocity - glm::vec2{1.f, 0.f}) < Epsilon);
        assert(units[1].resolution ==
            Aoe2xUnitAvoidanceResolution::Diverted);
        assert(gap(units[0], units[1]) >= -Epsilon);
        assert(units[1].frame_correction.y < 0.f);
    }

    {
        // With no priority weights, equal-priority units split the projection.
        Aoe2xUnitAvoidanceSettings settings;
        settings.chain_weight = 0;
        settings.target_distance_weight = 0;
        std::vector<Aoe2xUnitStep> units{
            unit(1, {-1.f, 0.f}, {.95f, 0.f}, {}, .1f),
            unit(2, {1.f, 0.f}, {-.95f, 0.f}, {}, .1f)};
        solve(units, settings);
        assert(gap(units[0], units[1]) >= -Epsilon);
        assert(units[0].frame_correction.x < 0.f);
        assert(units[1].frame_correction.x > 0.f);
        assert(std::abs(glm::length(units[0].frame_correction) -
                        glm::length(units[1].frame_correction)) < Epsilon);
    }

    {
        // Large squads quantise several leading chain indices to the same
        // score. Captain identity, rather than that tie, keeps the navigation
        // anchor on its ideal step.
        Aoe2xUnitAvoidanceSettings settings;
        settings.chain_weight = 0;
        settings.target_distance_weight = 0;
        std::vector<Aoe2xUnitStep> units{
            unit(1, {-1.f, 0.f}, {.95f, 0.f}, {}, .1f),
            unit(2, {1.f, 0.f}, {-.95f, 0.f}, {}, .1f)};
        units[0].captain = true;
        solve(units, settings);
        assert(glm::length(
            units[0].velocity - glm::vec2{.95f, 0.f}) < Epsilon);
        assert(units[1].frame_correction.x > 0.f);
        assert(gap(units[0], units[1]) >= -Epsilon);
    }

    {
        // Three iterations resolve a short priority cascade without searching
        // a fixed set of directions.
        std::vector<Aoe2xUnitStep> units{
            unit(1, {0.f, 0.f}, {}, {}, .1f),
            unit(2, {0.f, -.15f}, {}, {}, .1f),
            unit(3, {0.f, -.3f}, {}, {}, .1f)};
        solve(units);
        assert(gap(units[0], units[1]) >= -Epsilon);
        assert(gap(units[1], units[2]) >= -Epsilon);
    }

    {
        // A dense 10x10 squad exercises the grid, constraint ordering and
        // repeated projection path with many simultaneous neighbor pairs.
        std::vector<Aoe2xUnitStep> units;
        units.reserve(100);
        for (std::uint32_t y = 0; y < 10; ++y)
            for (std::uint32_t x = 0; x < 10; ++x) {
                const glm::vec2 position{
                    static_cast<float>(x) * .195f,
                    static_cast<float>(y) * .195f};
                units.push_back(unit(
                    y * 10u + x + 1u, position, {}, position, .1f));
            }
        solve(units);
        std::size_t unresolved = 0;
        for (const auto& value : units)
            unresolved += value.resolution ==
                Aoe2xUnitAvoidanceResolution::Unresolved;
        assert(unresolved == 0u);
    }

    {
        // Correction history adds a stable tangent while preserving the normal
        // separation component.
        std::vector<Aoe2xUnitStep> units{
            unit(1, {0.f, 0.f}, {}, {}, .1f),
            unit(2, {.15f, 0.f}, {}, {}, .1f)};
        units[1].previous_correction = {0.f, 1.f};
        solve(units);
        assert(units[1].frame_correction.x > 0.f);
        assert(units[1].frame_correction.y > 0.f);
        assert(gap(units[0], units[1]) >= -Epsilon);
    }

    {
        // A hard per-frame budget leaves a deep overlap unresolved instead of
        // teleporting the low-priority unit.
        Aoe2xUnitAvoidanceSettings settings;
        settings.maximum_correction_radius_ratio = .25f;
        std::vector<Aoe2xUnitStep> units{
            unit(1, {0.f, 0.f}, {}, {}, 1.f),
            unit(2, {0.f, -.5f}, {}, {}, 1.f)};
        solve(units, settings);
        assert(units[1].resolution ==
            Aoe2xUnitAvoidanceResolution::Unresolved);
        assert(glm::length(units[1].frame_correction) <= .25f + Epsilon);
    }

    {
        auto map = obstacle_map();
        std::vector<Aoe2xUnitStep> units{
            unit(1, {1.f, 1.f}, {2.f, 0.f}, {4.f, 1.f}, .1f)};
        solve(units, {}, &map);
        assert(units[0].start_position.x + units[0].velocity.x < 1.71f);
    }

    {
        // Separate squad solves deliberately do not see each other.
        std::vector<Aoe2xUnitStep> first{
            unit(1, {-1.f, 0.f}, {1.f, 0.f}, {0.f, 0.f})};
        std::vector<Aoe2xUnitStep> second{
            unit(2, {1.f, 0.f}, {-1.f, 0.f}, {0.f, 0.f})};
        solve(first);
        solve(second);
        assert(glm::length(first[0].velocity - glm::vec2{1.f, 0.f}) < Epsilon);
        assert(glm::length(second[0].velocity - glm::vec2{-1.f, 0.f}) < Epsilon);
    }

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    {
        Aoe2xUnitAvoidanceDiagnostics diagnostics;
        diagnostics.record_frame(1.25, 2, {10, 4, 3, 1});
        assert(diagnostics.average_ms && diagnostics.peak_ms);
        assert(diagnostics.last_unresolved == 1u);
        diagnostics.reset_window();
        assert(!diagnostics.average_ms && !diagnostics.peak_ms);
    }
#endif
}
