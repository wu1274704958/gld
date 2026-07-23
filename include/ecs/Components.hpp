#pragma once

// Core ECS data components (pure data, no logic) + the Time resource.

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <chrono>

namespace gld::ecs {

    // Local transform (relative to parent).
    struct Transform {
        glm::vec3 translation{0.f};
        glm::vec3 rotation{0.f};   // euler radians (x,y,z)
        glm::vec3 scale{1.f};
    };

    // World transform, computed by transform_propagate_system. `version` bumps
    // only when `world` actually changes, so consumers (e.g. the batcher) can do
    // cheap change detection without hashing.
    struct GlobalTransform {
        glm::mat4 world{1.f};
        std::uint32_t version = 0;
    };

    // Hierarchy.
    struct Parent {
        entt::entity value{entt::null};
    };
    struct Children {
        std::vector<entt::entity> value;
    };

    // ---- resources ----
    struct Time {
        float dt = 0.f;
        float raw_dt = 0.f;
        double elapsed = 0.0;
        double wall_elapsed = 0.0;
        std::uint64_t frame = 0;
        float fps = 0.f;
    };

    struct TimeSettings {
        float max_delta = 0.1f;
    };

    struct TimeClock {
        using Clock = std::chrono::steady_clock;
        Clock::time_point previous{};
        bool initialized = false;
    };
}
