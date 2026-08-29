#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

namespace gld::ecs::aoe {

// Common base for unit behaviours that can produce movement. Higher values
// pre-empt lower values; ordering inside a planned action chain remains
// explicit and never depends on this value.
struct UnitAction {
    std::uint8_t priority = 0;

    constexpr UnitAction() = default;
    constexpr explicit UnitAction(std::uint8_t value) : priority(value) {}
};

struct AoeNavigationPath : UnitAction {
    static constexpr std::uint8_t DefaultPriority = 0;

    std::vector<glm::vec2> waypoints;
    std::size_t current = 0;
    glm::vec2 requested_goal{0.f};
    std::uint64_t map_revision = 0;
    std::uint64_t request_sequence = 0;
    std::uint64_t last_repath_tick = 0;
    std::uint32_t blocked_ticks = 0;
    bool no_path = false;
    bool include_dynamic_obstacles = false;
    bool dynamic_repath_requested = false;
    // A failed optional dynamic replan must not invalidate a still-usable
    // static route. This flag is diagnostic and is cleared by the next
    // successful plan.
    bool dynamic_repath_failed = false;

    AoeNavigationPath() : UnitAction(DefaultPriority) {}
    AoeNavigationPath(std::vector<glm::vec2> points,
                      std::size_t cursor = 0)
        : UnitAction(DefaultPriority), waypoints(std::move(points)),
          current(cursor) {}
};

} // namespace gld::ecs::aoe
