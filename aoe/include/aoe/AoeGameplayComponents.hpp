#pragma once

#include <glm/glm.hpp>

namespace gld::ecs::aoe {

// Shared, simulation-neutral spatial components. New gameplay modules may
// depend on this header without depending on the legacy AoE gameplay API.
struct AoeCollider {
    float radius_x = 0.f;
    float radius_y = 0.f;
    float height = 0.f;
};

struct AoePosition { glm::vec2 value{0.f}; };

} // namespace gld::ecs::aoe
