#pragma once

#include <cstdint>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <aoe/AoeFormationLayout.hpp>
#include <aoe/AoeFormationWidthSchedule.hpp>

namespace gld::ecs::aoe {

// Arc-length pose field followed by the rows of a travelling formation.  A
// member at local Y samples the curve at squad_progress + local_y, which makes
// turns propagate from the front rows to the rear rows instead of rotating the
// whole formation as one rigid body.
struct AoeFormationRoutePose {
    float distance = 0.f;
    glm::vec2 center{0.f};
    glm::vec2 forward{1.f, 0.f};
};

struct AoeFormationRoutePlan {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t layout_revision = 0;
    std::uint64_t logic_map_revision = 0;
    std::uint64_t nav_mesh_revision = 0;
    std::uint64_t settings_signature = 0;

    // Caller-owned width-constrained variant.  The Squad's natural layout is
    // never overwritten by route planning.
    AoeFormationLayout travel_layout;
    AoeFormationWidthSchedule width_schedule;
    std::vector<AoeFormationRoutePose> poses;

    float natural_width = 0.f;
    float bottleneck_width = 0.f;
    float selected_width = 0.f;
    float prelude_progress = 0.f;
    float travel_progress = 0.f;
    float postlude_progress = 0.f;
    float total_progress = 0.f;
    bool narrowed = false;
    bool valid = false;
};

} // namespace gld::ecs::aoe
