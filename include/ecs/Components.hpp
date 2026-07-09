#pragma once

// Core ECS data components (pure data, no logic) + the Time resource.

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace gld::ecs {

    // Local transform (relative to parent).
    struct Transform {
        glm::vec3 translation{0.f};
        glm::vec3 rotation{0.f};   // euler radians (x,y,z)
        glm::vec3 scale{1.f};
    };

    // World transform, computed by transform_propagate_system.
    struct GlobalTransform {
        glm::mat4 world{1.f};
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
        double elapsed = 0.0;
    };
}
