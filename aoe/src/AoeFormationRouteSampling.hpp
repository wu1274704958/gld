#pragma once
#include <aoe/AoeFormation.hpp>
#include <aoe/AoeGameplay.hpp>

// Route geometry primitives shared by the RouteSplit and MovingControl
// formation modules. Kept in a src-local header because they operate on
// internal per-member route metadata and are not part of the public API.
namespace gld::ecs::aoe::formation_detail {

float signed_turn(glm::vec2 from, glm::vec2 to);
glm::vec2 rotate_direction(glm::vec2 value, float angle);

// O(1) cursor-based progress estimate for a member on its own route.
float member_route_progress(const AoeFormationMemberRouteProgress& metadata,
                            const AoeNavigationPath* path,
                            glm::vec2 position, float total_progress);

// Full closest-point progress projection. O(waypoints), used where accuracy
// matters more than cost (action-chain timeline decisions).
float project_member_route_progress(
    const AoeFormationMemberRouteProgress& metadata,
    const AoeNavigationPath& path, glm::vec2 position,
    float total_progress);

// Snaps a dormant path cursor forward to the progress of a detach boundary.
void advance_path_to_progress(AoeNavigationPath& path,
                              const AoeFormationMemberRouteProgress& metadata,
                              float progress);

struct MemberRouteSample {
    glm::vec2 position{0.f};
    glm::vec2 forward{1.f, 0.f};
    float speed_ratio = 1.f;
    bool valid = false;
};

// Samples the route polyline at an arbitrary progress value. Progress at or
// before the leader origin extrapolates backward along the first segment.
MemberRouteSample sample_member_route(
    const AoeFormationMemberRouteProgress& metadata,
    const AoeNavigationPath& path, float progress);

// Squad-level route frame: interpolated center and slerped forward axis.
AoeFormationRouteFrame sample_route_frame(
    const AoeFormationRouteTrajectory& trajectory, float progress);

bool route_unit_valid(const entt::registry& reg,
                      const AoeFormationRouteUnit& unit);

} // namespace gld::ecs::aoe::formation_detail
