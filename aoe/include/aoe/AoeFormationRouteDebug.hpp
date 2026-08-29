#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include <aoe/AoeFormationRoutePlan.hpp>
#include <aoe/AoeNavMesh.hpp>

namespace gld::ecs::aoe {

// Debug capture is opt-in. Production worlds do not install this resource, so
// RouteSplit performs no debug route copies or snapshot allocations there.
struct AoeFormationRouteDebugCapture {
    bool enabled = true;
};

enum class AoeFormationRouteDebugStage : std::uint8_t {
    None,
    WaitingForCorridor,
    ValidateInput,
    SelectLayout,
    ValidateTurn,
    BuildFollowTopology,
    BuildCenterPath,
    BuildPoseCurve,
    BuildWidthConstraints,
    BuildWidthSchedule,
    RepairTransitionWindows,
    BuildMemberRoutes,
    Commit,
    Completed,
};

struct AoeFormationRouteDebugMemberRoute {
    AoeUnitTarget unit{};
    std::size_t natural_chain = 0;
    glm::vec2 origin{0.f};
    std::vector<glm::vec2> waypoints;
    bool forward_safe = false;
    bool static_safe = false;
    bool accepted = false;
};

// Last RouteSplit attempt for one Squad. Unlike AoeFormationRoutePlan this is
// also published on failure, allowing a planning-only preview to draw the
// corridor and every partial member route that existed at the failure point.
struct AoeFormationRouteDebugSnapshot {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    AoeFormationRouteDebugStage stage = AoeFormationRouteDebugStage::None;
    AoeNavCorridor corridor;
    std::vector<float> portal_usable_widths;
    std::vector<glm::vec2> center_path;
    std::vector<AoeFormationRoutePose> poses;
    std::vector<AoeFormationWidthConstraint> width_constraints;
    AoeFormationWidthSchedule width_schedule;
    std::vector<AoeFormationRouteDebugMemberRoute> member_routes;
    float natural_width = 0.f;
    float bottleneck_width = 0.f;
    float selected_width = 0.f;
    std::size_t failed_chain = static_cast<std::size_t>(-1);
    bool succeeded = false;
};

} // namespace gld::ecs::aoe
