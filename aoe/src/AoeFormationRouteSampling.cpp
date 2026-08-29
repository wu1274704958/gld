#include "AoeFormationRouteSampling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gld::ecs::aoe::formation_detail {
namespace {
constexpr float Epsilon = 1e-5f;
} // namespace

float signed_turn(glm::vec2 from, glm::vec2 to) {
    if (glm::length(from) <= Epsilon || glm::length(to) <= Epsilon)
        return 0.f;
    from = glm::normalize(from);
    to = glm::normalize(to);
    return std::atan2(from.x * to.y - from.y * to.x,
                      glm::dot(from, to));
}

glm::vec2 rotate_direction(glm::vec2 value, float angle) {
    const float cosine = std::cos(angle);
    const float sine = std::sin(angle);
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

float member_route_progress(const AoeFormationMemberRouteProgress& metadata,
                            const AoeNavigationPath* path,
                            glm::vec2 position, float total_progress) {
    if (!path || path->waypoints.empty() ||
        path->current >= path->waypoints.size())
        return total_progress;
    const std::size_t index = path->current;
    if (index >= metadata.waypoint_progress.size())
        return total_progress;
    const glm::vec2 start = index == 0
        ? metadata.origin : path->waypoints[index - 1];
    const float start_progress = index == 0
        ? metadata.origin_progress
        : metadata.waypoint_progress[index - 1];
    const glm::vec2 segment = path->waypoints[index] - start;
    const float length2 = glm::dot(segment, segment);
    const float alpha = length2 > Epsilon * Epsilon
        ? std::clamp(glm::dot(position - start, segment) / length2, 0.f, 1.f)
        : 1.f;
    return std::lerp(start_progress,
        metadata.waypoint_progress[index], alpha);
}

float project_member_route_progress(
    const AoeFormationMemberRouteProgress& metadata,
    const AoeNavigationPath& path, glm::vec2 position,
    float total_progress) {
    if (path.waypoints.empty() || metadata.waypoint_progress.size() !=
            path.waypoints.size())
        return total_progress;
    float best_distance2 = std::numeric_limits<float>::infinity();
    float best_progress = metadata.origin_progress;
    glm::vec2 from = metadata.origin;
    float from_progress = metadata.origin_progress;
    for (std::size_t index = 0; index < path.waypoints.size(); ++index) {
        const glm::vec2 to = path.waypoints[index];
        const float to_progress = metadata.waypoint_progress[index];
        const glm::vec2 segment = to - from;
        const float length2 = glm::dot(segment, segment);
        const float alpha = length2 > Epsilon * Epsilon
            ? std::clamp(glm::dot(position - from, segment) / length2,
                         0.f, 1.f)
            : 1.f;
        const glm::vec2 projected = from + segment * alpha;
        const float distance2 = glm::dot(
            position - projected, position - projected);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best_progress = std::lerp(from_progress, to_progress, alpha);
        }
        from = to;
        from_progress = to_progress;
    }
    return best_progress;
}

void advance_path_to_progress(AoeNavigationPath& path,
                              const AoeFormationMemberRouteProgress& metadata,
                              float progress) {
    const auto found = std::lower_bound(metadata.waypoint_progress.begin(),
        metadata.waypoint_progress.end(), progress - Epsilon);
    path.current = static_cast<std::size_t>(
        found - metadata.waypoint_progress.begin());
}

MemberRouteSample sample_member_route(
    const AoeFormationMemberRouteProgress& metadata,
    const AoeNavigationPath& path, float progress) {
    if (path.waypoints.empty() ||
        metadata.waypoint_progress.size() != path.waypoints.size())
        return {};
    const auto sample_segment = [&](glm::vec2 from, glm::vec2 to,
                                    float from_progress,
                                    float to_progress,
                                    std::size_t segment) {
        const glm::vec2 delta = to - from;
        const float length = glm::length(delta);
        const glm::vec2 forward = length > Epsilon
            ? delta / length : glm::vec2{1.f, 0.f};
        const float span = to_progress - from_progress;
        const float alpha = span > Epsilon
            ? (progress - from_progress) / span : 1.f;
        const float ratio = segment < metadata.segment_speed_ratio.size()
            ? std::max(0.f, metadata.segment_speed_ratio[segment])
            : (span > Epsilon ? length / span : 1.f);
        return MemberRouteSample{
            from + delta * alpha, forward, ratio, true};
    };

    if (progress <= metadata.origin_progress) {
        auto sample = sample_segment(metadata.origin, path.waypoints.front(),
            metadata.origin_progress, metadata.waypoint_progress.front(), 0);
        // The first segment is also the route's backward extrapolation. This
        // is what lets rear followers occupy space before the leader origin.
        return sample;
    }
    const auto upper = std::lower_bound(metadata.waypoint_progress.begin(),
        metadata.waypoint_progress.end(), progress);
    if (upper == metadata.waypoint_progress.end()) {
        const std::size_t last = path.waypoints.size() - 1;
        if (!last) return sample_segment(metadata.origin,
            path.waypoints.front(), metadata.origin_progress,
            metadata.waypoint_progress.front(), 0);
        return sample_segment(path.waypoints[last - 1], path.waypoints[last],
            metadata.waypoint_progress[last - 1],
            metadata.waypoint_progress[last], last);
    }
    const std::size_t index = static_cast<std::size_t>(
        upper - metadata.waypoint_progress.begin());
    return index == 0
        ? sample_segment(metadata.origin, path.waypoints.front(),
            metadata.origin_progress, metadata.waypoint_progress.front(), 0)
        : sample_segment(path.waypoints[index - 1], path.waypoints[index],
            metadata.waypoint_progress[index - 1],
            metadata.waypoint_progress[index], index);
}

AoeFormationRouteFrame sample_route_frame(
    const AoeFormationRouteTrajectory& trajectory, float progress) {
    if (trajectory.frames.empty()) return {};
    if (progress <= trajectory.frames.front().progress)
        return trajectory.frames.front();
    if (progress >= trajectory.frames.back().progress)
        return trajectory.frames.back();
    const auto upper = std::lower_bound(trajectory.frames.begin(),
        trajectory.frames.end(), progress,
        [](const AoeFormationRouteFrame& frame, float value) {
            return frame.progress < value;
        });
    const auto& next = *upper;
    const auto& previous = *(upper - 1);
    const float span = next.progress - previous.progress;
    const float alpha = span > Epsilon
        ? std::clamp((progress - previous.progress) / span, 0.f, 1.f)
        : 1.f;
    const float angle = signed_turn(previous.forward, next.forward);
    return {progress,
        glm::mix(previous.center, next.center, alpha),
        glm::normalize(rotate_direction(previous.forward, angle * alpha))};
}

bool route_unit_valid(const entt::registry& reg,
                      const AoeFormationRouteUnit& unit) {
    const auto* identity = reg.valid(unit.entity)
        ? reg.try_get<AoeGameplayIdentity>(unit.entity) : nullptr;
    return identity && identity->instance_id == unit.instance_id;
}

} // namespace gld::ecs::aoe::formation_detail
