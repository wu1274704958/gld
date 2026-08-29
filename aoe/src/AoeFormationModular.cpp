#include <aoe/AoeGameplay.hpp>

#include "AoeFormationRouteSampling.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace gld::ecs::aoe {
using formation_detail::advance_path_to_progress;
using formation_detail::member_route_progress;
using formation_detail::project_member_route_progress;
using formation_detail::rotate_direction;
using formation_detail::route_unit_valid;
using formation_detail::sample_member_route;
using formation_detail::sample_route_frame;
using formation_detail::signed_turn;

namespace {
constexpr float Epsilon = 1e-5f;

struct FunnelPortal {
    glm::vec2 left{0.f};
    glm::vec2 right{0.f};
};

float triangle_area_twice(glm::vec2 a, glm::vec2 b, glm::vec2 c) {
    const glm::vec2 ab = b - a;
    const glm::vec2 ac = c - a;
    return ac.x * ab.y - ab.x * ac.y;
}

bool same_point(glm::vec2 a, glm::vec2 b) {
    return glm::dot(a - b, a - b) <= Epsilon * Epsilon;
}

std::vector<glm::vec2> funnel_path(
    glm::vec2 start, glm::vec2 goal,
    const std::vector<FunnelPortal>& corridor) {
    std::vector<FunnelPortal> portals;
    portals.reserve(corridor.size() + 2);
    portals.push_back({start, start});
    portals.insert(portals.end(), corridor.begin(), corridor.end());
    portals.push_back({goal, goal});

    std::vector<glm::vec2> result;
    result.reserve(portals.size());
    glm::vec2 apex = portals.front().left;
    glm::vec2 left = apex;
    glm::vec2 right = apex;
    int apex_index = 0;
    int left_index = 0;
    int right_index = 0;

    for (int index = 1; index < static_cast<int>(portals.size()); ++index) {
        const glm::vec2 next_left = portals[index].left;
        const glm::vec2 next_right = portals[index].right;

        if (triangle_area_twice(apex, right, next_right) <= 0.f) {
            if (same_point(apex, right) ||
                triangle_area_twice(apex, left, next_right) > 0.f) {
                right = next_right;
                right_index = index;
            } else {
                if (!same_point(result.empty() ? start : result.back(), left))
                    result.push_back(left);
                apex = left;
                apex_index = left_index;
                left = apex;
                right = apex;
                left_index = apex_index;
                right_index = apex_index;
                index = apex_index;
                continue;
            }
        }

        if (triangle_area_twice(apex, left, next_left) >= 0.f) {
            if (same_point(apex, left) ||
                triangle_area_twice(apex, right, next_left) < 0.f) {
                left = next_left;
                left_index = index;
            } else {
                if (!same_point(result.empty() ? start : result.back(), right))
                    result.push_back(right);
                apex = right;
                apex_index = right_index;
                left = apex;
                right = apex;
                left_index = apex_index;
                right_index = apex_index;
                index = apex_index;
            }
        }
    }
    if (result.empty() || !same_point(result.back(), goal))
        result.push_back(goal);
    return result;
}

std::vector<FunnelPortal> full_funnel_portals(
    const AoeNavCorridor& corridor) {
    std::vector<FunnelPortal> result;
    result.reserve(corridor.portals.size());
    for (const auto& portal : corridor.portals)
        result.push_back({portal.left, portal.right});
    return result;
}

std::optional<std::vector<FunnelPortal>> contracted_funnel_portals(
    const AoeNavCorridor& corridor, float endpoint_inset) {
    std::vector<FunnelPortal> result;
    result.reserve(corridor.portals.size());
    endpoint_inset = std::max(0.f, endpoint_inset);
    for (const auto& portal : corridor.portals) {
        const glm::vec2 span = portal.right - portal.left;
        const float width = glm::length(span);
        if (width <= endpoint_inset * 2.f + Epsilon)
            return std::nullopt;
        const glm::vec2 direction = span / width;
        result.push_back({portal.left + direction * endpoint_inset,
                          portal.right - direction * endpoint_inset});
    }
    return result;
}

bool path_is_static_safe(const AoeLogicMap* map, glm::vec2 start,
                         const std::vector<glm::vec2>& path,
                         glm::vec2 clearance, float epsilon) {
    if (!map || !map->valid()) return true;
    for (const auto waypoint : path) {
        if (map->static_safe_fraction(start, waypoint, clearance) <
            1.f - epsilon)
            return false;
        start = waypoint;
    }
    return true;
}

glm::vec2 observed_formation_forward(
    const entt::registry& reg, const AoeFormationSquadContext& context) {
    glm::vec2 weighted{0.f};
    for (const auto& slot : context.layout.layout.slots) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, slot.unit) ||
            !reg.all_of<AoePosition>(slot.unit.entity))
            continue;
        const float length2 = glm::dot(slot.local_offset, slot.local_offset);
        if (length2 <= Epsilon * Epsilon) continue;
        const glm::vec2 world =
            reg.get<AoePosition>(slot.unit.entity).value - context.center.value;
        const glm::vec2 candidate{
            (world.x * slot.local_offset.y -
             world.y * slot.local_offset.x) / length2,
            (world.x * slot.local_offset.x +
             world.y * slot.local_offset.y) / length2,
        };
        weighted += candidate * length2;
    }
    if (glm::length(weighted) > Epsilon) return glm::normalize(weighted);
    if (glm::length(context.formation.forward) > Epsilon)
        return glm::normalize(context.formation.forward);
    return {1.f, 0.f};
}

struct DubinsCandidate {
    std::array<char, 3> kind{};
    std::array<float, 3> parameter{};
    float length = 0.f;
};

float positive_angle(float value) {
    constexpr float Tau = 6.28318530717958647692f;
    value = std::fmod(value, Tau);
    return value < 0.f ? value + Tau : value;
}

std::vector<DubinsCandidate> dubins_candidates(
    glm::vec2 start, glm::vec2 start_forward,
    glm::vec2 goal, glm::vec2 goal_forward, float radius) {
    std::vector<DubinsCandidate> result;
    const glm::vec2 offset = goal - start;
    const float distance = glm::length(offset);
    if (distance <= Epsilon || radius <= Epsilon) return result;
    const float line_angle = std::atan2(offset.y, offset.x);
    const float alpha = positive_angle(
        std::atan2(start_forward.y, start_forward.x) - line_angle);
    const float beta = positive_angle(
        std::atan2(goal_forward.y, goal_forward.x) - line_angle);
    const float d = distance / radius;
    const float sin_a = std::sin(alpha);
    const float sin_b = std::sin(beta);
    const float cos_a = std::cos(alpha);
    const float cos_b = std::cos(beta);
    const float cos_ab = std::cos(alpha - beta);
    const auto add = [&](std::array<char, 3> kind,
                         float t, float p, float q) {
        if (!std::isfinite(t) || !std::isfinite(p) || !std::isfinite(q) ||
            t < 0.f || p < 0.f || q < 0.f)
            return;
        result.push_back({kind, {t, p, q}, (t + p + q) * radius});
    };

    float p2 = 2.f + d * d - 2.f * cos_ab +
        2.f * d * (sin_a - sin_b);
    if (p2 >= 0.f) {
        const float tmp = std::atan2(cos_b - cos_a,
            d + sin_a - sin_b);
        add({'L', 'S', 'L'}, positive_angle(-alpha + tmp),
            std::sqrt(p2), positive_angle(beta - tmp));
    }
    p2 = 2.f + d * d - 2.f * cos_ab +
        2.f * d * (sin_b - sin_a);
    if (p2 >= 0.f) {
        const float tmp = std::atan2(cos_a - cos_b,
            d - sin_a + sin_b);
        add({'R', 'S', 'R'}, positive_angle(alpha - tmp),
            std::sqrt(p2), positive_angle(-beta + tmp));
    }
    p2 = -2.f + d * d + 2.f * cos_ab +
        2.f * d * (sin_a + sin_b);
    if (p2 >= 0.f) {
        const float p = std::sqrt(p2);
        const float tmp = std::atan2(-cos_a - cos_b,
            d + sin_a + sin_b) - std::atan2(-2.f, p);
        add({'L', 'S', 'R'}, positive_angle(-alpha + tmp), p,
            positive_angle(-beta + tmp));
    }
    p2 = d * d - 2.f + 2.f * cos_ab -
        2.f * d * (sin_a + sin_b);
    if (p2 >= 0.f) {
        const float p = std::sqrt(p2);
        const float tmp = std::atan2(cos_a + cos_b,
            d - sin_a - sin_b) - std::atan2(2.f, p);
        add({'R', 'S', 'L'}, positive_angle(alpha - tmp), p,
            positive_angle(beta - tmp));
    }
    float tmp = (6.f - d * d + 2.f * cos_ab +
        2.f * d * (sin_a - sin_b)) / 8.f;
    if (std::abs(tmp) <= 1.f) {
        const float p = positive_angle(
            6.28318530717958647692f - std::acos(tmp));
        const float t = positive_angle(alpha - std::atan2(
            cos_a - cos_b, d - sin_a + sin_b) + p * .5f);
        add({'R', 'L', 'R'}, t, p,
            positive_angle(alpha - beta - t + p));
    }
    tmp = (6.f - d * d + 2.f * cos_ab +
        2.f * d * (-sin_a + sin_b)) / 8.f;
    if (std::abs(tmp) <= 1.f) {
        const float p = positive_angle(
            6.28318530717958647692f - std::acos(tmp));
        const float t = positive_angle(-alpha - std::atan2(
            cos_a - cos_b, d + sin_a - sin_b) + p * .5f);
        add({'L', 'R', 'L'}, t, p,
            positive_angle(beta - alpha - t + p));
    }
    std::stable_sort(result.begin(), result.end(),
        [](const DubinsCandidate& a, const DubinsCandidate& b) {
            if (std::abs(a.length - b.length) > Epsilon)
                return a.length < b.length;
            return a.kind.front() == 'L' && b.kind.front() != 'L';
        });
    return result;
}

struct PoseCurveBuilder {
    std::vector<AoeFormationRoutePose> poses;
    float maximum_center_step = .5f;
    float maximum_turn_step = .174532925f;

    void append(glm::vec2 center, glm::vec2 forward) {
        if (poses.empty()) {
            poses.push_back({0.f, center, glm::normalize(forward)});
            return;
        }
        forward = glm::length(forward) > Epsilon
            ? glm::normalize(forward) : poses.back().forward;
        const float translation = glm::length(center - poses.back().center);
        if (translation <= Epsilon &&
            std::abs(signed_turn(poses.back().forward, forward)) <= Epsilon)
            return;
        poses.push_back({poses.back().distance + translation, center, forward});
    }

    void line_to(glm::vec2 goal, glm::vec2 direction) {
        const glm::vec2 start = poses.back().center;
        const float distance = glm::length(goal - start);
        if (distance <= Epsilon) return;
        direction = glm::normalize(direction);
        const std::uint32_t steps = std::max(1u,
            static_cast<std::uint32_t>(
                std::ceil(distance / maximum_center_step)));
        for (std::uint32_t step = 1; step <= steps; ++step)
            append(glm::mix(start, goal,
                static_cast<float>(step) / static_cast<float>(steps)),
                direction);
    }

    void arc(float signed_angle, float radius) {
        if (std::abs(signed_angle) <= Epsilon) return;
        const glm::vec2 start = poses.back().center;
        const glm::vec2 start_forward = poses.back().forward;
        const float start_heading = std::atan2(
            start_forward.y, start_forward.x);
        const std::uint32_t steps = std::max({1u,
            static_cast<std::uint32_t>(std::ceil(
                std::abs(signed_angle) / maximum_turn_step)),
            static_cast<std::uint32_t>(std::ceil(
                std::abs(signed_angle) * radius / maximum_center_step))});
        const float signed_radius = std::copysign(radius, signed_angle);
        for (std::uint32_t step = 1; step <= steps; ++step) {
            const float angle = signed_angle * static_cast<float>(step) /
                static_cast<float>(steps);
            const float heading = start_heading + angle;
            append(start + signed_radius * glm::vec2{
                std::sin(heading) - std::sin(start_heading),
                -std::cos(heading) + std::cos(start_heading)},
                {std::cos(heading), std::sin(heading)});
        }
    }
};

bool append_filleted_suffix(PoseCurveBuilder& builder,
    const std::vector<glm::vec2>& spine, std::size_t first_corner,
    float radius) {
    glm::vec2 available_start = builder.poses.back().center;
    for (std::size_t index = first_corner;
         index + 1 < spine.size(); ++index) {
        const glm::vec2 incoming_delta = spine[index] - available_start;
        const glm::vec2 outgoing_delta = spine[index + 1] - spine[index];
        const float incoming_length = glm::length(incoming_delta);
        const float outgoing_length = glm::length(outgoing_delta);
        if (incoming_length <= Epsilon || outgoing_length <= Epsilon)
            return false;
        const glm::vec2 incoming = incoming_delta / incoming_length;
        const glm::vec2 outgoing = outgoing_delta / outgoing_length;
        const float turn = signed_turn(incoming, outgoing);
        if (std::abs(turn) <= 1e-4f) {
            builder.line_to(spine[index], incoming);
            available_start = spine[index];
            continue;
        }
        if (std::abs(turn) >= 3.14159265358979323846f - 1e-3f)
            return false;
        const float tangent = radius * std::tan(std::abs(turn) * .5f);
        if (tangent >= incoming_length - Epsilon ||
            tangent >= outgoing_length - Epsilon)
            return false;
        const glm::vec2 entry = spine[index] - incoming * tangent;
        const glm::vec2 exit = spine[index] + outgoing * tangent;
        builder.line_to(entry, incoming);
        builder.arc(turn, radius);
        if (glm::length(builder.poses.back().center - exit) > 1e-3f)
            return false;
        builder.poses.back().center = exit;
        builder.poses.back().forward = outgoing;
        available_start = exit;
    }
    const glm::vec2 final_delta = spine.back() - builder.poses.back().center;
    if (glm::length(final_delta) <= Epsilon) return true;
    const glm::vec2 final_direction = glm::normalize(final_delta);
    if (glm::dot(final_direction, builder.poses.back().forward) < 1.f - 1e-3f)
        return false;
    builder.line_to(spine.back(), final_direction);
    return true;
}

std::vector<std::vector<AoeFormationRoutePose>> build_pose_curve_candidates(
    const std::vector<glm::vec2>& center_path,
    glm::vec2 start_forward, float radius,
    const AoeFormationRouteSplitSettings& settings) {
    std::vector<glm::vec2> spine;
    spine.reserve(center_path.size());
    const auto push_spine = [&](glm::vec2 center) {
        if (spine.empty() || !same_point(spine.back(), center))
            spine.push_back(center);
    };
    for (const auto point : center_path) push_spine(point);
    if (spine.size() < 2) return {};
    start_forward = glm::length(start_forward) > Epsilon
        ? glm::normalize(start_forward) : glm::vec2{1.f, 0.f};
    const float maximum_center_step = std::max(
        .01f, settings.maximum_center_step);
    const float maximum_turn = std::max(.001f,
        settings.maximum_rotation_step_degrees *
            3.14159265358979323846f / 180.f);
    radius = std::max(radius, .01f);
    const glm::vec2 first_direction = glm::normalize(
        spine[1] - spine[0]);
    std::vector<std::vector<AoeFormationRoutePose>> result;
    if (std::abs(signed_turn(start_forward, first_direction)) <= 1e-4f) {
        PoseCurveBuilder builder{{{0.f, spine.front(), start_forward}},
            maximum_center_step, maximum_turn};
        if (append_filleted_suffix(builder, spine, 1, radius))
            result.push_back(std::move(builder.poses));
        return result;
    }

    const glm::vec2 target_forward = spine.size() > 2
        ? glm::normalize(spine[2] - spine[1]) : first_direction;
    for (const auto& candidate : dubins_candidates(
             spine.front(), start_forward, spine[1], target_forward, radius)) {
        PoseCurveBuilder builder{{{0.f, spine.front(), start_forward}},
            maximum_center_step, maximum_turn};
        for (std::size_t index = 0; index < candidate.kind.size(); ++index) {
            const char kind = candidate.kind[index];
            const float parameter = candidate.parameter[index];
            if (kind == 'S')
                builder.line_to(builder.poses.back().center +
                    builder.poses.back().forward * (parameter * radius),
                    builder.poses.back().forward);
            else
                builder.arc((kind == 'L' ? 1.f : -1.f) * parameter, radius);
        }
        if (glm::length(builder.poses.back().center - spine[1]) > 2e-3f ||
            std::abs(signed_turn(
                builder.poses.back().forward, target_forward)) > 2e-3f)
            continue;
        builder.poses.back().center = spine[1];
        builder.poses.back().forward = target_forward;
        if (append_filleted_suffix(builder, spine, 2, radius))
            result.push_back(std::move(builder.poses));
    }
    return result;
}

AoeFormationRoutePose sample_pose_curve(
    const std::vector<AoeFormationRoutePose>& poses, float distance) {
    if (poses.empty()) return {};
    if (distance <= poses.front().distance) {
        auto result = poses.front();
        result.center += result.forward * (distance - result.distance);
        result.distance = distance;
        return result;
    }
    if (distance >= poses.back().distance) {
        auto result = poses.back();
        result.center += result.forward * (distance - result.distance);
        result.distance = distance;
        return result;
    }
    const auto upper = std::lower_bound(poses.begin(), poses.end(), distance,
        [](const AoeFormationRoutePose& pose, float value) {
            return pose.distance < value;
        });
    const auto& next = *upper;
    const auto& previous = *(upper - 1);
    const float span = next.distance - previous.distance;
    const float alpha = span > Epsilon
        ? std::clamp((distance - previous.distance) / span, 0.f, 1.f)
        : 1.f;
    const float angle = signed_turn(previous.forward, next.forward);
    return {distance,
        glm::mix(previous.center, next.center, alpha),
        glm::normalize(rotate_direction(previous.forward, angle * alpha))};
}

std::uint64_t route_settings_signature(
    const AoeFormationRouteSplitSettings& settings) {
    std::uint64_t result = 1469598103934665603ull;
    const auto mix = [&](std::uint32_t value) {
        result ^= value;
        result *= 1099511628211ull;
    };
    mix(settings.compression_portal_window);
    mix(std::bit_cast<std::uint32_t>(settings.portal_band_half_width));
    mix(std::bit_cast<std::uint32_t>(settings.width_safety_distance));
    mix(std::bit_cast<std::uint32_t>(
        settings.maximum_layout_change_per_progress));
    mix(std::bit_cast<std::uint32_t>(settings.path_validation_epsilon));
    mix(std::bit_cast<std::uint32_t>(settings.maximum_center_step));
    mix(std::bit_cast<std::uint32_t>(
        settings.maximum_rotation_step_degrees));
    mix(std::bit_cast<std::uint32_t>(settings.maximum_reformation_step));
    mix(std::bit_cast<std::uint32_t>(settings.minimum_center_turn_radius));
    mix(std::bit_cast<std::uint32_t>(
        settings.minimum_member_forward_ratio));
    return result;
}

AoeFormationContext make_formation_generation_context(
    const entt::registry& reg, const AoeFormationSquadContext& context) {
    AoeFormationContext result;
    result.spacing = context.formation.spacing;
    result.members.reserve(context.members.active.size());
    for (const auto& member : context.members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member)) continue;
        const auto* definition =
            reg.get<AoeUnitDefinitionRef>(member.entity).value.get();
        const auto& membership = reg.get<AoeSquadMember>(member.entity);
        const auto& collider = reg.get<AoeCollider>(member.entity);
        result.members.push_back({
            member,
            membership.ordinal,
            definition ? definition->tags : std::vector<std::string>{},
            {collider.radius_x, collider.radius_y},
        });
    }
    return result;
}

void write_member_route(entt::registry& reg, entt::entity entity,
                        const std::vector<glm::vec2>& waypoints,
                        glm::vec2 origin, float origin_progress,
                        const std::vector<float>& waypoint_progress,
                        const std::vector<float>& segment_speed_ratio,
                        glm::vec2 goal, std::uint64_t map_revision,
                        std::uint64_t order_revision, std::uint64_t tick,
                        entt::entity squad, std::uint64_t instance_id) {
    auto& path = reg.emplace_or_replace<AoeNavigationPath>(entity);
    path.waypoints = waypoints;
    path.current = 0;
    path.requested_goal = goal;
    path.map_revision = map_revision;
    path.request_sequence = order_revision;
    path.last_repath_tick = tick;
    path.blocked_ticks = 0;
    path.no_path = waypoints.empty();
    path.include_dynamic_obstacles = false;
    path.dynamic_repath_requested = false;
    path.dynamic_repath_failed = false;
    reg.emplace_or_replace<AoeFormationRouteOwner>(entity,
        AoeFormationRouteOwner{squad, order_revision, instance_id});
    reg.emplace_or_replace<AoeFormationMemberRouteProgress>(entity,
        AoeFormationMemberRouteProgress{squad, order_revision, instance_id,
            origin, origin_progress, waypoint_progress,
            segment_speed_ratio});
}

void clear_owned_routes(entt::registry& reg, entt::entity squad,
                        AoeFormationRouteSplitState& state) {
    if (const auto* follow_plan =
            reg.try_get<AoeFormationFollowPlan>(squad)) {
        for (const auto& chain : follow_plan->chains) {
            for (const auto& member : chain.members) {
                if (!reg.valid(member.unit.entity)) continue;
                const auto* follow =
                    reg.try_get<AoeFormationFollow>(member.unit.entity);
                if (!follow || follow->squad != squad ||
                    follow->order_revision != follow_plan->order_revision)
                    continue;
                reg.remove<AoeFormationFollow, AoeSquadMoveSpeedLimit>(
                    member.unit.entity);
                if (auto* request = reg.try_get<AoePathMotionRequest>(
                        member.unit.entity))
                    request->valid = false;
            }
        }
    }
    for (const auto& unit : state.units) {
        if (!route_unit_valid(reg, unit)) continue;
        const auto* owner = reg.try_get<AoeFormationRouteOwner>(unit.entity);
        if (!owner || owner->squad != squad ||
            owner->unit_instance_id != unit.instance_id)
            continue;
        const auto* move_owner =
            reg.try_get<AoeFormationMoveGoalOwner>(unit.entity);
        if (move_owner && move_owner->squad == squad &&
            move_owner->squad_order_revision ==
                owner->squad_order_revision &&
            move_owner->unit_instance_id == unit.instance_id) {
            reg.remove<AoeMoveGoal>(unit.entity);
            reg.remove<AoeFormationMoveGoalOwner>(unit.entity);
        }
        const auto* progress =
            reg.try_get<AoeFormationMemberRouteProgress>(unit.entity);
        if (progress && progress->squad == squad &&
            progress->unit_instance_id == unit.instance_id) {
            reg.remove<AoeFormationMemberRouteProgress>(unit.entity);
            reg.remove<AoeSquadMoveSpeedLimit>(unit.entity);
        }
        reg.remove<AoeNavigationPath, AoeFormationRouteOwner>(unit.entity);
        reg.remove<AoeUnitActionChain>(unit.entity);
    }
    state.units.clear();
    reg.remove<AoeFormationRoutePlan, AoeFormationRouteTrajectory,
               AoeFormationFollowPlan, AoeFormationFollowTopology,
               AoeFormationMovingState>(squad);
}

void apply_natural_follow_topology(entt::registry& reg,
                                   const AoeFormationFollowPlan& plan) {
    for (const auto& chain : plan.chains) {
        for (const auto& member : chain.members) {
            if (!reg.valid(member.unit.entity)) continue;
            const auto* existing =
                reg.try_get<AoeFormationFollow>(member.unit.entity);
            if (existing && existing->squad == plan.squad &&
                existing->order_revision == plan.order_revision)
                reg.remove<AoeFormationFollow>(member.unit.entity);
        }
    }
    for (auto& assignment : make_formation_follow_assignments(plan))
        reg.emplace_or_replace<AoeFormationFollow>(
            assignment.unit.entity, std::move(assignment.follow));
}

int facing_direction_toward(const AoeFacing& facing, glm::vec2 delta) {
    if (facing.direction_count <= 0 ||
        glm::dot(delta, delta) <= Epsilon * Epsilon)
        return facing.direction;
    const glm::vec2 projected{
        delta.x - delta.y,
        (delta.x + delta.y) * .5f,
    };
    const glm::vec2 direction = glm::normalize(projected);
    float angle = std::atan2(direction.y, direction.x);
    const float full_turn = 2.f * glm::pi<float>();
    angle = std::fmod(angle, full_turn);
    if (angle < 0.f) angle += full_turn;
    const float sector_width =
        full_turn / static_cast<float>(facing.direction_count);
    return static_cast<int>(std::floor(
        (angle + sector_width * .5f) / sector_width)) %
        facing.direction_count;
}

void stop_layout_member(
    entt::registry& reg, entt::entity entity, std::uint64_t tick) {
    detail::aoe_gameplay_clear_active_engagement(reg, entity);
    reg.remove<AoeAttackMoveOrder, AoeSquadMoveSpeedLimit>(entity);
    detail::aoe_gameplay_reset_member_action(reg, entity, tick);
}

AoeFormationModuleResult handle_layout_failure(
    EcsWorld& world, AoeFormationSquadContext& context) {
    auto& reg = world.reg();
    context.formation.dirty = false;
    ++world.resource_or_add<AoeGameplayDiagnostics>().commands_rejected;

    if (context.layout.valid && !context.layout.layout.slots.empty()) {
        constexpr std::string_view message =
            "formation layout rejected; retaining previous slots";
        if (std::find(context.spawn.errors.begin(), context.spawn.errors.end(),
                      message) == context.spawn.errors.end())
            context.spawn.errors.emplace_back(message);
        return AoeFormationModuleResult::StopSquad;
    }

    constexpr std::string_view message = "formation layout failed";
    if (std::find(context.spawn.errors.begin(), context.spawn.errors.end(),
                  message) == context.spawn.errors.end())
        context.spawn.errors.emplace_back(message);
    for (const auto& member : context.members.active)
        if (detail::aoe_gameplay_squad_member_valid(reg, member))
            stop_layout_member(reg, member.entity, context.tick);
    reg.remove<AoeNavigationPath>(context.squad);
    context.order = {};
    context.spawn.status = AoeSquadSpawnStatus::Failed;
    context.state.phase = AoeSquadPhase::Failed;
    reg.emplace_or_replace<AoeFormationResult>(context.squad,
        AoeFormationResult{AoeFormationResultStatus::Failed,
            context.tick, false, false, true});
    return AoeFormationModuleResult::StopSquad;
}
} // namespace

glm::vec2 aoe_formation_slot_world(
    const AoePosition& center, const AoeSquadFormation& formation,
    const AoeFormationSlot& slot) {
    glm::vec2 forward = formation.forward;
    if (glm::length(forward) <= Epsilon) forward = {1.f, 0.f};
    else forward = glm::normalize(forward);
    const glm::vec2 right{forward.y, -forward.x};
    return center.value + right * slot.local_offset.x +
           forward * slot.local_offset.y;
}

void AoeFullSquadLayoutModule::install(App&) {}

AoeFormationModuleResult AoeFullSquadLayoutModule::run(
    EcsWorld& world, AoeFormationSquadContext& context) {
    if (!context.formation.dirty ||
        context.state.phase == AoeSquadPhase::Engaging)
        return AoeFormationModuleResult::Continue;

    auto& reg = world.reg();
    auto* registry = world.try_resource<AoeFormationRegistry>();
    if (!registry || !registry->contains(context.formation.type))
        return handle_layout_failure(world, context);

    AoeFormationContext layout_context;
    layout_context.spacing = context.formation.spacing;
    layout_context.members.reserve(context.members.active.size());
    for (const auto& member : context.members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member)) continue;
        const auto* definition =
            reg.get<AoeUnitDefinitionRef>(member.entity).value.get();
        const auto& membership = reg.get<AoeSquadMember>(member.entity);
        const auto& collider = reg.get<AoeCollider>(member.entity);
        layout_context.members.push_back({
            member,
            membership.ordinal,
            definition ? definition->tags : std::vector<std::string>{},
            {collider.radius_x, collider.radius_y},
        });
    }

    auto generated = registry->generate(context.formation.type, layout_context);
    if (!generated ||
        (!layout_context.members.empty() && generated->slots.empty()))
        return handle_layout_failure(world, context);
    context.layout.layout = std::move(*generated);
    context.layout.valid = true;
    ++context.layout.revision;
    context.formation.dirty = false;

    float bound = 0.f;
    float height = 0.f;
    for (const auto& slot : context.layout.layout.slots) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, slot.unit)) continue;
        const auto& collider = reg.get<AoeCollider>(slot.unit.entity);
        bound = std::max(bound, glm::length(slot.local_offset) +
            std::max(collider.radius_x, collider.radius_y));
        height = std::max(height, collider.height);
        if (!context.formation.teleport_on_next_layout) continue;
        const glm::vec2 position = aoe_formation_slot_world(
            context.center, context.formation, slot);
        reg.get<AoePosition>(slot.unit.entity).value = position;
        reg.get_or_emplace<AoePositionHistory>(
            slot.unit.entity).previous = position;
        auto& facing = reg.get<AoeFacing>(slot.unit.entity);
        facing.direction = facing_direction_toward(
            facing, context.formation.forward);
    }
    context.collider = {
        bound, bound, std::max(height, Epsilon),
    };
    context.formation.teleport_on_next_layout = false;
    if (context.state.phase == AoeSquadPhase::Forming)
        context.state.phase = AoeSquadPhase::Idle;
    return AoeFormationModuleResult::Continue;
}

void AoeNavMeshSquadPathfinderPlugin::install(App&) {}

AoePathResult AoeNavMeshSquadPathfinderPlugin::find(
    EcsWorld& world, const AoePathRequest& request) {
    auto& reg = world.reg();
    const auto* map = world.try_resource<AoeLogicMap>();
    const std::uint64_t logic_revision = map ? map->static_revision() : 0;
    auto clear_corridor = [&] {
        if (reg.valid(request.subject))
            reg.remove<AoeFormationNavCorridor>(request.subject);
    };
    auto* nav_mesh = world.try_resource<AoeNavMeshResource>();
    if (!nav_mesh || !nav_mesh->queryable()) {
        clear_corridor();
        return {AoePathStatus::NoPath, {}, logic_revision};
    }

    auto corridor = nav_mesh->find_corridor(request.start, request.goal);
    AoePathStatus status = AoePathStatus::NoPath;
    switch (corridor.status) {
    case AoeNavCorridorStatus::Ready: status = AoePathStatus::Ready; break;
    case AoeNavCorridorStatus::InvalidStart:
        status = AoePathStatus::InvalidStart;
        break;
    case AoeNavCorridorStatus::InvalidGoal:
        status = AoePathStatus::InvalidGoal;
        break;
    case AoeNavCorridorStatus::NavMeshUnavailable:
    case AoeNavCorridorStatus::NoPath:
    case AoeNavCorridorStatus::QueryFailed:
        status = AoePathStatus::NoPath;
        break;
    }
    if (status != AoePathStatus::Ready) {
        clear_corridor();
        return {status, {}, logic_revision};
    }

    auto waypoints = funnel_path(
        corridor.start_on_mesh, corridor.goal_on_mesh,
        full_funnel_portals(corridor));
    if (waypoints.empty()) {
        clear_corridor();
        return {AoePathStatus::NoPath, {}, logic_revision};
    }
    if (reg.valid(request.subject)) {
        const auto* order = reg.try_get<AoeSquadOrder>(request.subject);
        const auto* clock = world.try_resource<AoeGameplayClock>();
        reg.emplace_or_replace<AoeFormationNavCorridor>(request.subject,
            AoeFormationNavCorridor{
                std::move(corridor),
                order ? order->revision : 0,
                logic_revision,
                clock ? clock->tick : 0,
                true,
            });
    }
    return {AoePathStatus::Ready, std::move(waypoints), logic_revision};
}

void AoeNavMeshRouteSplitModule::install(App& app) {
    app.world.resource_or_add<AoeFormationRouteSplitSettings>();
    app.world.resource_or_add<AoeFormationRouteSplitDiagnostics>();
}

AoeFormationModuleResult AoeNavMeshRouteSplitModule::run(
    EcsWorld& world, AoeFormationSquadContext& context) {
    auto& reg = world.reg();
    auto& state = reg.get_or_emplace<AoeFormationRouteSplitState>(
        context.squad);
    const auto& settings = world.resource_or_add<
        AoeFormationRouteSplitSettings>();
    const std::uint64_t settings_signature =
        route_settings_signature(settings);
    const bool moving_order =
        context.order.type == AoeSquadOrderType::MoveTo ||
        context.order.type == AoeSquadOrderType::AttackMove;
    const auto* debug_capture =
        world.try_resource<AoeFormationRouteDebugCapture>();
    const bool debug_enabled = debug_capture && debug_capture->enabled;
    if (!moving_order) {
        clear_owned_routes(reg, context.squad, state);
        state = {};
        reg.remove<AoeFormationNavCorridor>(context.squad);
        if (debug_enabled)
            reg.remove<AoeFormationRouteDebugSnapshot>(context.squad);
        return AoeFormationModuleResult::Continue;
    }

    const auto* corridor_component =
        reg.try_get<AoeFormationNavCorridor>(context.squad);
    const auto* nav_mesh = world.try_resource<AoeNavMeshResource>();
    if (!nav_mesh || !nav_mesh->queryable() || !corridor_component ||
        !corridor_component->valid ||
        corridor_component->order_revision != context.order.revision ||
        corridor_component->corridor.map_revision !=
            nav_mesh->source_map_revision() ||
        corridor_component->corridor.status != AoeNavCorridorStatus::Ready) {
        clear_owned_routes(reg, context.squad, state);
        state.status = AoeFormationRouteSplitStatus::Failed;
        state.order_revision = context.order.revision;
        state.layout_revision = context.layout.revision;
        state.settings_signature = settings_signature;
        state.produced_tick = context.tick;
        if (debug_enabled) {
            AoeFormationRouteDebugSnapshot snapshot;
            snapshot.squad = context.squad;
            snapshot.order_revision = context.order.revision;
            snapshot.stage =
                AoeFormationRouteDebugStage::WaitingForCorridor;
            if (corridor_component)
                snapshot.corridor = corridor_component->corridor;
            reg.emplace_or_replace<AoeFormationRouteDebugSnapshot>(
                context.squad, std::move(snapshot));
        }
        return AoeFormationModuleResult::Continue;
    }

    const auto& corridor = corridor_component->corridor;
    if (state.order_revision == context.order.revision &&
        state.layout_revision == context.layout.revision &&
        state.logic_map_revision == corridor_component->logic_map_revision &&
        state.nav_mesh_revision == corridor.map_revision &&
        state.settings_signature == settings_signature &&
        state.status != AoeFormationRouteSplitStatus::None) {
        ++world.resource_or_add<AoeFormationRouteSplitDiagnostics>().cache_hits;
        return AoeFormationModuleResult::Continue;
    }

    const auto started = std::chrono::steady_clock::now();
    clear_owned_routes(reg, context.squad, state);
    state.status = AoeFormationRouteSplitStatus::Failed;
    state.order_revision = context.order.revision;
    state.layout_revision = context.layout.revision;
    state.logic_map_revision = corridor_component->logic_map_revision;
    state.nav_mesh_revision = corridor.map_revision;
    state.settings_signature = settings_signature;
    state.produced_tick = context.tick;
    const auto* logic_map = world.try_resource<AoeLogicMap>();
    auto& diagnostics = world.resource_or_add<
        AoeFormationRouteSplitDiagnostics>();
    AoeFormationRouteDebugSnapshot debug_snapshot;
    if (debug_enabled) {
        debug_snapshot.squad = context.squad;
        debug_snapshot.order_revision = context.order.revision;
        debug_snapshot.stage = AoeFormationRouteDebugStage::ValidateInput;
        debug_snapshot.corridor = corridor;
    }

    const auto finish_diagnostics = [&] {
        ++diagnostics.splits;
        const double elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        diagnostics.total_ms += elapsed;
        diagnostics.max_ms = std::max(diagnostics.max_ms, elapsed);
    };

    const auto set_debug_stage = [&](AoeFormationRouteDebugStage stage) {
        if (debug_enabled) debug_snapshot.stage = stage;
    };
    const auto publish_debug = [&](bool succeeded) {
        if (!debug_enabled) return;
        debug_snapshot.succeeded = succeeded;
        reg.emplace_or_replace<AoeFormationRouteDebugSnapshot>(
            context.squad, std::move(debug_snapshot));
    };

    auto fail = [&](std::uint64_t members_failed = 1) {
        state.status = AoeFormationRouteSplitStatus::Failed;
        diagnostics.members_failed += members_failed;
        finish_diagnostics();
        publish_debug(false);
        return AoeFormationModuleResult::Continue;
    };

    if (!context.layout.valid ||
        context.layout.layout.slots.empty() ||
        !std::isfinite(settings.portal_band_half_width) ||
        settings.portal_band_half_width < 0.f ||
        !std::isfinite(settings.width_safety_distance) ||
        settings.width_safety_distance < 0.f ||
        !std::isfinite(settings.maximum_layout_change_per_progress) ||
        settings.maximum_layout_change_per_progress <= 0.f ||
        !std::isfinite(settings.path_validation_epsilon) ||
        settings.path_validation_epsilon < 0.f ||
        !std::isfinite(settings.maximum_reformation_step) ||
        settings.maximum_reformation_step <= 0.f ||
        !std::isfinite(settings.minimum_center_turn_radius) ||
        settings.minimum_center_turn_radius <= 0.f ||
        !std::isfinite(settings.minimum_member_forward_ratio) ||
        settings.minimum_member_forward_ratio < 0.f ||
        settings.minimum_member_forward_ratio >= 1.f)
        return fail();

    const float natural_width = context.layout.layout.bounds.width();
    if (debug_enabled) debug_snapshot.natural_width = natural_width;
    constexpr float WidthEpsilon = 1e-5f;
    float bottleneck_width = std::numeric_limits<float>::infinity();
    const float nav_agent_radius = std::max(
        0.f, nav_mesh->settings().agent_radius);
    std::vector<float> portal_usable_widths;
    portal_usable_widths.reserve(corridor.portals.size());
    for (const auto& portal : corridor.portals) {
        const float physical_width = glm::length(portal.right - portal.left) +
            nav_agent_radius * 2.f;
        const float usable_width = std::max(0.f, physical_width -
            settings.portal_band_half_width * 2.f);
        portal_usable_widths.push_back(usable_width);
        bottleneck_width = std::min(bottleneck_width, usable_width);
    }
    if (!std::isfinite(bottleneck_width)) bottleneck_width = natural_width;
    if (debug_enabled) {
        debug_snapshot.portal_usable_widths = portal_usable_widths;
        debug_snapshot.bottleneck_width = bottleneck_width;
    }
    const float portal_bottleneck_width = bottleneck_width;
    bool turn_narrowed = false;
    const auto base_center_path = funnel_path(context.center.value,
        corridor.goal_on_mesh, full_funnel_portals(corridor));
    const glm::vec2 observed_forward = observed_formation_forward(reg, context);
    glm::vec2 previous_forward = observed_forward;
    bool corridor_turns = false;
    glm::vec2 previous_center = context.center.value;
    for (const auto point : base_center_path) {
        const glm::vec2 delta = point - previous_center;
        if (glm::length(delta) > Epsilon) {
            const glm::vec2 forward = glm::normalize(delta);
            corridor_turns = corridor_turns ||
                std::abs(signed_turn(previous_forward, forward)) > 1e-4f;
            previous_forward = forward;
            previous_center = point;
        }
    }
    auto layout_context = make_formation_generation_context(reg, context);
    auto* formation_registry = world.try_resource<AoeFormationRegistry>();
    if (!formation_registry ||
        layout_context.members.size() != context.layout.layout.slots.size())
        return fail();

    set_debug_stage(AoeFormationRouteDebugStage::SelectLayout);
    std::optional<AoeFormationLayout> travel_layout;
    if (natural_width <= bottleneck_width + WidthEpsilon)
        travel_layout = context.layout.layout;
    else
        travel_layout = formation_registry->generate_for_width(
            context.formation.type, layout_context, bottleneck_width);
    if (!travel_layout ||
        travel_layout->slots.size() != context.layout.layout.slots.size())
        return fail();
    if (debug_enabled)
        debug_snapshot.selected_width = travel_layout->bounds.width();

    set_debug_stage(AoeFormationRouteDebugStage::ValidateTurn);
    const auto layout_has_safe_turn_pose = [&](const AoeFormationLayout& layout) {
        float clearance = 0.f;
        float lateral_offset = 0.f;
        for (const auto& slot : layout.slots) {
            if (slot.chain_order != 0) continue;
            const auto& collider = reg.get<AoeCollider>(slot.unit.entity);
            lateral_offset = std::max(
                lateral_offset, std::abs(slot.local_offset.x));
            clearance = std::max(clearance,
                std::abs(slot.local_offset.x) +
                    std::max(collider.radius_x, collider.radius_y));
        }
        const float minimum_inset = std::max(
            0.f, clearance - nav_agent_radius) +
            settings.portal_band_half_width;
        float maximum_inset = std::numeric_limits<float>::infinity();
        for (const auto& portal : corridor.portals)
            maximum_inset = std::min(maximum_inset,
                glm::length(portal.right - portal.left) * .5f - Epsilon);
        if (!std::isfinite(maximum_inset)) maximum_inset = minimum_inset;

        std::vector<glm::vec2> path;
        const float inset_step = std::max(
            .05f, nav_mesh->settings().cell_size);
        for (float inset = minimum_inset;
             inset <= maximum_inset + Epsilon;) {
            auto portals = contracted_funnel_portals(
                corridor, std::min(inset, maximum_inset));
            if (!portals) break;
            auto candidate = funnel_path(context.center.value,
                corridor.goal_on_mesh, *portals);
            if (!candidate.empty() && path_is_static_safe(logic_map,
                    context.center.value, candidate, glm::vec2{clearance},
                    settings.path_validation_epsilon)) {
                path = std::move(candidate);
                break;
            }
            if (inset >= maximum_inset - Epsilon) break;
            inset = std::min(maximum_inset, inset + inset_step);
        }
        if (path.empty() && logic_map && logic_map->valid() &&
            corridor_component->logic_map_revision == corridor.map_revision) {
            auto repaired = GridAStarPathfinderLogic::find(world, {
                context.center.value, corridor.goal_on_mesh,
                glm::vec2{clearance}, context.squad,
                context.squad, entt::null, false});
            if (repaired.status == AoePathStatus::Ready &&
                !repaired.waypoints.empty())
                path = std::move(repaired.waypoints);
        }
        if (path.empty()) return false;
        path.insert(path.begin(), context.center.value);
        const float sampled_forward_ratio = std::min(
            .99f, settings.minimum_member_forward_ratio + .01f);
        const float radius = std::max(settings.minimum_center_turn_radius,
            lateral_offset / (1.f - sampled_forward_ratio));
        const auto pose_path_safe = [&](const std::vector<glm::vec2>& spine) {
            for (auto& candidate : build_pose_curve_candidates(
                     spine, observed_forward, radius, settings)) {
                std::vector<glm::vec2> centers;
                centers.reserve(candidate.size() - 1);
                for (std::size_t index = 1; index < candidate.size(); ++index)
                    centers.push_back(candidate[index].center);
                if (path_is_static_safe(logic_map, candidate.front().center,
                        centers, glm::vec2{clearance},
                        settings.path_validation_epsilon))
                    return true;
            }
            return false;
        };
        if (pose_path_safe(path)) return true;
        if (logic_map && logic_map->valid() &&
            corridor_component->logic_map_revision == corridor.map_revision) {
            auto repaired = GridAStarPathfinderLogic::find(world, {
                context.center.value, corridor.goal_on_mesh,
                glm::vec2{clearance + radius}, context.squad,
                context.squad, entt::null, false});
            if (repaired.status == AoePathStatus::Ready &&
                !repaired.waypoints.empty()) {
                std::vector<glm::vec2> repaired_path;
                repaired_path.reserve(repaired.waypoints.size() + 1);
                repaired_path.push_back(context.center.value);
                repaired_path.insert(repaired_path.end(),
                    repaired.waypoints.begin(), repaired.waypoints.end());
                return pose_path_safe(repaired_path);
            }
        }
        return false;
    };

    if (corridor_turns && !layout_has_safe_turn_pose(*travel_layout)) {
        const float portal_selected_width = travel_layout->bounds.width();
        float requested_width = std::min(
            portal_bottleneck_width, portal_selected_width) * .5f;
        std::optional<AoeFormationLayout> turn_layout;
        constexpr std::uint32_t MaximumTurnNarrowingAttempts = 8;
        for (std::uint32_t attempt = 0;
             attempt < MaximumTurnNarrowingAttempts; ++attempt) {
            auto candidate = formation_registry->generate_for_width(
                context.formation.type, layout_context, requested_width);
            if (!candidate || candidate->slots.size() !=
                    context.layout.layout.slots.size())
                break;
            if (layout_has_safe_turn_pose(*candidate)) {
                turn_layout = std::move(candidate);
                break;
            }
            requested_width *= .5f;
        }
        if (!turn_layout) return fail();
        travel_layout = std::move(turn_layout);
        turn_narrowed = travel_layout->bounds.width() <
            portal_selected_width - WidthEpsilon;
    }
    if (debug_enabled)
        debug_snapshot.selected_width = travel_layout->bounds.width();

    set_debug_stage(AoeFormationRouteDebugStage::BuildFollowTopology);
    auto follow_plan = make_formation_follow_plan(
        context.layout.layout, *travel_layout, context.squad,
        context.order.revision, context.layout.revision);
    if (!follow_plan || !follow_plan->valid) return fail();
    auto follow_topology = make_formation_follow_topology(*follow_plan);
    if (!follow_topology || !follow_topology->valid) return fail();

    const auto make_leader_layout = [&](const AoeFormationFollowPlan& plan,
                                        bool narrow) {
        AoeFormationLayout result;
        result.slots.reserve(plan.chains.size());
        bool have_bounds = false;
        for (const auto& chain : plan.chains) {
            const auto& leader = chain.members.front();
            const glm::vec2 offset = narrow
                ? chain.narrow_leader_offset
                : chain.natural_leader_offset;
            result.slots.push_back({leader.unit, offset, 0,
                chain.natural_chain, 0});
            const auto& collider = reg.get<AoeCollider>(leader.unit.entity);
            const glm::vec2 radius{collider.radius_x, collider.radius_y};
            const glm::vec2 low = offset - radius;
            const glm::vec2 high = offset + radius;
            if (!have_bounds) {
                result.bounds = {low, high};
                have_bounds = true;
            } else {
                result.bounds.local_min = glm::min(
                    result.bounds.local_min, low);
                result.bounds.local_max = glm::max(
                    result.bounds.local_max, high);
            }
        }
        return result;
    };
    const AoeFormationLayout natural_leader_layout =
        make_leader_layout(*follow_plan, false);
    const AoeFormationLayout narrow_leader_layout =
        make_leader_layout(*follow_plan, true);

    // Detour the Squad center far enough from each portal endpoint for the
    // complete lateral footprint. Recast's portal endpoints already include
    // the NavMesh agent radius, so only the additional formation clearance is
    // inset here. The configured band is a deterministic safety margin and is
    // the same margin used while selecting the width-constrained layout.
    float lateral_clearance = 0.f;
    float maximum_lateral_offset = 0.f;
    for (const auto& slot : narrow_leader_layout.slots) {
        const auto& collider = reg.get<AoeCollider>(slot.unit.entity);
        maximum_lateral_offset = std::max(
            maximum_lateral_offset, std::abs(slot.local_offset.x));
        lateral_clearance = std::max(lateral_clearance,
            std::abs(slot.local_offset.x) +
                std::max(collider.radius_x, collider.radius_y));
    }
    const float minimum_portal_inset = std::max(
        0.f, lateral_clearance - nav_agent_radius) +
        settings.portal_band_half_width;
    float maximum_portal_inset = std::numeric_limits<float>::infinity();
    for (const auto& portal : corridor.portals)
        maximum_portal_inset = std::min(maximum_portal_inset,
            glm::length(portal.right - portal.left) * .5f - Epsilon);
    if (!std::isfinite(maximum_portal_inset))
        maximum_portal_inset = minimum_portal_inset;

    set_debug_stage(AoeFormationRouteDebugStage::BuildCenterPath);
    std::vector<glm::vec2> center_path;
    bool center_path_safe = false;
    const float inset_step = std::max(.05f, nav_mesh->settings().cell_size);
    for (float portal_inset = minimum_portal_inset;
         portal_inset <= maximum_portal_inset + Epsilon;) {
        auto contracted_portals = contracted_funnel_portals(
            corridor, std::min(portal_inset, maximum_portal_inset));
        if (!contracted_portals) break;
        auto candidate = funnel_path(context.center.value,
            corridor.goal_on_mesh, *contracted_portals);
        const glm::vec2 center_clearance{lateral_clearance};
        if (!candidate.empty() && path_is_static_safe(logic_map,
                context.center.value, candidate, center_clearance,
                settings.path_validation_epsilon)) {
            center_path = std::move(candidate);
            center_path_safe = true;
            break;
        }
        if (portal_inset >= maximum_portal_inset - Epsilon) break;
        portal_inset = std::min(
            maximum_portal_inset, portal_inset + inset_step);
    }
    // Recast portals are rasterized for the base agent and can be too coarse
    // to represent an anisotropic formation envelope exactly. Keep Funnel as
    // the fast path, then repair only the single Squad center route against
    // the current static map. Never repair a corridor built from a stale map.
    if (!center_path_safe && logic_map && logic_map->valid() &&
        corridor_component->logic_map_revision == corridor.map_revision) {
        auto repaired = GridAStarPathfinderLogic::find(world, {
            context.center.value, corridor.goal_on_mesh,
            {lateral_clearance, lateral_clearance}, context.squad,
            context.squad, entt::null, false});
        if (repaired.status == AoePathStatus::Ready &&
            !repaired.waypoints.empty()) {
            center_path = std::move(repaired.waypoints);
            center_path_safe = true;
        }
    }
    if (debug_enabled) {
        debug_snapshot.center_path.clear();
        debug_snapshot.center_path.push_back(context.center.value);
        debug_snapshot.center_path.insert(debug_snapshot.center_path.end(),
            center_path.begin(), center_path.end());
    }
    if (center_path.empty() || !center_path_safe) return fail();
    center_path.insert(center_path.begin(), context.center.value);
    const float sampled_forward_ratio = std::min(
        .99f, settings.minimum_member_forward_ratio + .01f);
    const float minimum_turn_radius = std::max(
        settings.minimum_center_turn_radius,
        maximum_lateral_offset /
            (1.f - sampled_forward_ratio));
    set_debug_stage(AoeFormationRouteDebugStage::BuildPoseCurve);
    std::vector<AoeFormationRoutePose> poses;
    const glm::vec2 start_forward = observed_forward;
    const auto select_pose_candidate = [&](
        const std::vector<glm::vec2>& path,
        std::vector<AoeFormationRoutePose>& output) {
        auto candidates = build_pose_curve_candidates(
            path, start_forward, minimum_turn_radius, settings);
        for (auto& candidate : candidates) {
            std::vector<glm::vec2> candidate_centers;
            candidate_centers.reserve(candidate.size() - 1);
            for (std::size_t index = 1; index < candidate.size(); ++index)
                candidate_centers.push_back(candidate[index].center);
            if (!path_is_static_safe(logic_map, candidate.front().center,
                    candidate_centers,
                    {lateral_clearance, lateral_clearance},
                    settings.path_validation_epsilon))
                continue;
            output = std::move(candidate);
            return true;
        }
        return false;
    };
    select_pose_candidate(center_path, poses);
    if (poses.empty() && logic_map && logic_map->valid() &&
        corridor_component->logic_map_revision == corridor.map_revision) {
        auto repaired = GridAStarPathfinderLogic::find(world, {
            context.center.value, corridor.goal_on_mesh,
            {lateral_clearance + minimum_turn_radius,
             lateral_clearance + minimum_turn_radius},
            context.squad, context.squad, entt::null, false});
        if (repaired.status == AoePathStatus::Ready &&
            !repaired.waypoints.empty()) {
            std::vector<glm::vec2> repaired_path;
            repaired_path.reserve(repaired.waypoints.size() + 1);
            repaired_path.push_back(context.center.value);
            repaired_path.insert(repaired_path.end(),
                repaired.waypoints.begin(), repaired.waypoints.end());
            select_pose_candidate(repaired_path, poses);
        }
    }
    if (poses.size() < 2 || poses.back().distance <= Epsilon)
        return fail();
    if (debug_enabled) debug_snapshot.poses = poses;
    const float travel_progress = poses.back().distance;

    std::vector<AoeFormationWidthRouteSample> width_route;
    width_route.reserve(poses.size());
    for (const auto& pose : poses)
        width_route.push_back({pose.distance, pose.center, pose.forward});
    set_debug_stage(AoeFormationRouteDebugStage::BuildWidthConstraints);
    std::vector<AoeFormationWidthConstraint> width_constraints;
    width_constraints.reserve(corridor.portals.size());
    // A width stage queues complete natural columns. Its occupancy interval
    // therefore runs from the front of the full formation to the tail of the
    // full formation, not merely over the row of routed column heads.
    const float maximum_front_y = context.layout.layout.bounds.local_max.y;
    const float minimum_rear_y = context.layout.layout.bounds.local_min.y;
    if (turn_narrowed &&
        travel_layout->bounds.width() < natural_width - WidthEpsilon) {
        float turn_begin = std::numeric_limits<float>::infinity();
        float turn_end = -std::numeric_limits<float>::infinity();
        for (std::size_t index = 1; index < poses.size(); ++index) {
            if (std::abs(signed_turn(
                    poses[index - 1].forward, poses[index].forward)) <=
                1e-4f)
                continue;
            turn_begin = std::min(turn_begin, poses[index - 1].distance);
            turn_end = std::max(turn_end, poses[index].distance);
        }
        if (std::isfinite(turn_begin) && std::isfinite(turn_end)) {
            width_constraints.push_back({
                turn_begin - maximum_front_y -
                    settings.width_safety_distance,
                turn_end - minimum_rear_y +
                    settings.width_safety_distance,
                travel_layout->bounds.width(),
            });
        }
    }
    for (std::size_t index = 0; index < corridor.portals.size(); ++index) {
        const float usable_width = portal_usable_widths[index];
        if (usable_width >= natural_width - WidthEpsilon) continue;
        const auto& portal = corridor.portals[index];
        const auto portal_progress = project_formation_width_route_progress(
            width_route, (portal.left + portal.right) * .5f);
        if (!portal_progress) return fail();
        AoeFormationWidthConstraint constraint{
            *portal_progress - maximum_front_y -
                settings.width_safety_distance,
            *portal_progress - minimum_rear_y +
                settings.width_safety_distance,
            usable_width,
        };
        if (!expand_formation_width_constraint_over_turns(
                width_route, constraint))
            return fail();
        width_constraints.push_back(constraint);
    }
    std::vector<AoeFormationLayout> candidate_leader_layouts;
    candidate_leader_layouts.reserve(width_constraints.size());
    std::vector<float> generated_widths;
    generated_widths.reserve(width_constraints.size());
    for (const auto& constraint : width_constraints) {
        if (std::any_of(generated_widths.begin(), generated_widths.end(),
                [&](float value) {
                    return std::abs(value - constraint.maximum_width) <=
                        WidthEpsilon;
                }))
            continue;
        auto candidate = formation_registry->generate_for_width(
            context.formation.type, layout_context,
            constraint.maximum_width);
        if (!candidate || candidate->slots.size() !=
                context.layout.layout.slots.size())
            return fail();
        auto candidate_follow = make_formation_follow_plan(
            context.layout.layout, *candidate, context.squad,
            context.order.revision, context.layout.revision);
        if (!candidate_follow || !candidate_follow->valid) return fail();
        candidate_leader_layouts.push_back(
            make_leader_layout(*candidate_follow, true));
        generated_widths.push_back(constraint.maximum_width);
    }
    if (debug_enabled) debug_snapshot.width_constraints = width_constraints;
    set_debug_stage(AoeFormationRouteDebugStage::BuildWidthSchedule);
    auto width_schedule = make_formation_width_schedule(
        natural_leader_layout, std::move(candidate_leader_layouts),
        std::move(width_constraints),
        travel_progress, {settings.width_safety_distance,
            settings.maximum_layout_change_per_progress,
            settings.minimum_member_forward_ratio});
    if (!width_schedule || !width_schedule->valid) return fail();
    if (debug_enabled) debug_snapshot.width_schedule = *width_schedule;

    // Include transition boundaries in the generated routes. The pose curve
    // remains the center-line source of truth; these extra samples only make
    // layout changes deterministic even when center pose steps are coarse.
    std::vector<float> travel_samples;
    travel_samples.reserve(
        poses.size() + width_schedule->transitions.size() * 2);
    for (const auto& pose : poses) travel_samples.push_back(pose.distance);
    for (const auto& transition : width_schedule->transitions) {
        if (transition.begin_progress > Epsilon &&
            transition.begin_progress < travel_progress - Epsilon)
            travel_samples.push_back(transition.begin_progress);
        if (transition.end_progress > Epsilon &&
            transition.end_progress < travel_progress - Epsilon)
            travel_samples.push_back(transition.end_progress);
    }
    std::sort(travel_samples.begin(), travel_samples.end());
    travel_samples.erase(std::unique(travel_samples.begin(),
        travel_samples.end(), [](float left, float right) {
            return std::abs(left - right) <= Epsilon;
        }), travel_samples.end());

    // A Detour portal marks a polygon boundary, not necessarily the first
    // point where a wide member envelope touches the obstacle corner. Use the
    // static map to move only the affected slot's wave window earlier/later.
    // This remains linear in members * pose samples and performs no pairwise
    // member checks.
    set_debug_stage(AoeFormationRouteDebugStage::RepairTransitionWindows);
    if (logic_map && logic_map->valid() && width_schedule->narrowed &&
        width_schedule->stages.size() == 1 &&
        width_schedule->transitions.size() >= 2) {
        auto& shrink = width_schedule->transitions.front();
        auto& restore = width_schedule->transitions.back();
        if (shrink.slot_progress_windows.size() !=
                width_schedule->layouts.front().slots.size() ||
            restore.slot_progress_windows.size() !=
                width_schedule->layouts.front().slots.size())
            return fail();
        constexpr std::uint32_t MaximumWindowRepairs = 8;
        for (std::size_t slot_index = 0;
             slot_index < width_schedule->layouts.front().slots.size();
             ++slot_index) {
            const auto& unit =
                width_schedule->layouts.front().slots[slot_index].unit;
            const auto& collider = reg.get<AoeCollider>(unit.entity);
            bool safe = false;
            for (std::uint32_t attempt = 0;
                 attempt < MaximumWindowRepairs && !safe; ++attempt) {
                safe = true;
                glm::vec2 previous_point{0.f};
                float previous_progress = 0.f;
                bool have_previous = false;
                for (const float progress : travel_samples) {
                    const glm::vec2 offset =
                        sample_formation_width_schedule(
                            *width_schedule, slot_index,
                            progress).local_offset;
                    const auto pose = sample_pose_curve(
                        poses, progress + offset.y);
                    const glm::vec2 right{
                        pose.forward.y, -pose.forward.x};
                    const glm::vec2 point = pose.center + right * offset.x;
                    if (!have_previous) {
                        previous_point = point;
                        previous_progress = progress;
                        have_previous = true;
                        continue;
                    }
                    if (logic_map->static_safe_fraction(previous_point, point,
                            {collider.radius_x, collider.radius_y}) >=
                        1.f - settings.path_validation_epsilon) {
                        previous_point = point;
                        previous_progress = progress;
                        continue;
                    }

                    auto& shrink_window =
                        shrink.slot_progress_windows[slot_index];
                    auto& restore_window =
                        restore.slot_progress_windows[slot_index];
                    if (progress <= shrink_window.y + Epsilon) {
                        const float length = shrink_window.y - shrink_window.x;
                        const float new_end = std::min(
                            shrink_window.y, previous_progress);
                        const float new_begin = new_end - length;
                        shrink_window = {std::max(0.f, new_begin), new_end};
                    } else if (progress + Epsilon >= restore_window.x) {
                        const float length = restore_window.y - restore_window.x;
                        const float new_begin = std::max(
                            restore_window.x, progress);
                        restore_window = {new_begin, new_begin + length};
                    } else {
                        // The selected narrow layout itself is statically
                        // unsafe; moving wave timing cannot repair that.
                        if (debug_enabled)
                            debug_snapshot.failed_chain = slot_index;
                        return fail();
                    }
                    safe = false;
                    break;
                }
            }
            if (!safe) {
                if (debug_enabled)
                    debug_snapshot.failed_chain = slot_index;
                return fail();
            }
        }
        const auto refresh_transition_bounds = [](auto& transition) {
            transition.begin_progress =
                std::numeric_limits<float>::infinity();
            transition.end_progress =
                -std::numeric_limits<float>::infinity();
            for (const glm::vec2 window :
                 transition.slot_progress_windows) {
                transition.begin_progress = std::min(
                    transition.begin_progress, window.x);
                transition.end_progress = std::max(
                    transition.end_progress, window.y);
            }
        };
        refresh_transition_bounds(shrink);
        refresh_transition_bounds(restore);
    }
    if (debug_enabled) debug_snapshot.width_schedule = *width_schedule;

    struct MemberEndpoints {
        std::size_t slot_index = 0;
        AoeUnitTarget unit{};
        glm::vec2 actual_start{0.f};
        glm::vec2 snake_start{0.f};
        glm::vec2 snake_end{0.f};
        glm::vec2 natural_end{0.f};
    };
    std::vector<MemberEndpoints> endpoints;
    endpoints.reserve(width_schedule->layouts.front().slots.size());
    float prelude_progress = 0.f;
    float postlude_progress = 0.f;
    const glm::vec2 final_center = corridor.goal_on_mesh;
    const glm::vec2 final_forward = poses.back().forward;
    const glm::vec2 final_right{final_forward.y, -final_forward.x};
    for (std::size_t slot_index = 0;
         slot_index < width_schedule->layouts.front().slots.size();
         ++slot_index) {
        const auto& natural_slot =
            width_schedule->layouts.front().slots[slot_index];
        if (!detail::aoe_gameplay_squad_member_valid(reg, natural_slot.unit))
            return fail();
        const glm::vec2 start_offset = sample_formation_width_schedule(
            *width_schedule, slot_index, 0.f).local_offset;
        const glm::vec2 end_offset = sample_formation_width_schedule(
            *width_schedule, slot_index, travel_progress).local_offset;
        const auto start_pose = sample_pose_curve(
            poses, start_offset.y);
        const auto end_pose = sample_pose_curve(
            poses, travel_progress + end_offset.y);
        const glm::vec2 start_right{
            start_pose.forward.y, -start_pose.forward.x};
        const glm::vec2 end_right{
            end_pose.forward.y, -end_pose.forward.x};
        MemberEndpoints member;
        member.slot_index = slot_index;
        member.unit = natural_slot.unit;
        member.actual_start =
            reg.get<AoePosition>(natural_slot.unit.entity).value;
        member.snake_start = start_pose.center +
            start_right * start_offset.x;
        member.snake_end = end_pose.center +
            end_right * end_offset.x;
        member.natural_end = final_center +
            final_right * natural_slot.local_offset.x +
            final_forward * natural_slot.local_offset.y;
        prelude_progress = std::max(prelude_progress,
            glm::length(member.snake_start - member.actual_start));
        postlude_progress = std::max(postlude_progress,
            glm::length(member.natural_end - member.snake_end));
        endpoints.push_back(member);
    }

    const float total_progress =
        prelude_progress + travel_progress + postlude_progress;
    std::vector<AoeUnitActionChain> leader_action_chains(
        follow_plan->chains.size());
    std::vector<float> action_cursor(follow_plan->chains.size(), 0.f);
    for (std::size_t index = 0; index < follow_plan->chains.size(); ++index) {
        auto& chain = leader_action_chains[index];
        chain.squad = context.squad;
        chain.order_revision = context.order.revision;
        chain.unit_instance_id =
            follow_plan->chains[index].members.front().unit.instance_id;
        chain.valid = true;
    }
    for (std::size_t stage_index = 0;
         stage_index < width_schedule->stages.size(); ++stage_index) {
        const auto& width_stage = width_schedule->stages[stage_index];
        const std::size_t shrink_index = stage_index * 2;
        const std::size_t restore_index = shrink_index + 1;
        if (restore_index >= width_schedule->transitions.size())
            return fail();
        const auto& shrink = width_schedule->transitions[shrink_index];
        const auto& restore = width_schedule->transitions[restore_index];
        if (shrink.from_layout != 0 ||
            shrink.to_layout != width_stage.layout ||
            restore.from_layout != width_stage.layout ||
            restore.to_layout != 0 ||
            shrink.slot_progress_windows.size() !=
                follow_plan->chains.size() ||
            restore.slot_progress_windows.size() !=
                follow_plan->chains.size())
            return fail();
        auto groups = make_formation_follow_groups(
            *follow_plan, width_schedule->layouts[width_stage.layout]);
        if (!groups) return fail();

        for (const auto& group : *groups) {
            float base_distance = 0.f;
            AoeUnitTarget previous_tail{};
            for (std::size_t order = 0; order < group.chains.size(); ++order) {
                const std::size_t chain_index = group.chains[order];
                const auto& natural_chain = follow_plan->chains[chain_index];
                if (order) base_distance += follow_plan->inter_chain_gap;
                if (order) {
                    const glm::vec2 shrink_window =
                        shrink.slot_progress_windows[chain_index];
                    const glm::vec2 restore_window =
                        restore.slot_progress_windows[chain_index];
                    const float attach_progress = shrink_window.x <= Epsilon
                        ? 0.f
                        : std::clamp(prelude_progress + shrink_window.x,
                            0.f, total_progress);
                    const float detach_progress = std::clamp(
                        prelude_progress + restore_window.x, attach_progress,
                        total_progress);
                    auto& actions = leader_action_chains[chain_index];
                    if (attach_progress > action_cursor[chain_index] + Epsilon)
                        actions.steps.push_back({
                            AoeUnitActionStepKind::NavigationPath,
                            action_cursor[chain_index], attach_progress});
                    const std::uint64_t token =
                        (static_cast<std::uint64_t>(stage_index + 1) << 32u) |
                        static_cast<std::uint64_t>(chain_index + 1);
                    const auto route_source = follow_plan->chains[
                        group.root_chain].members.front().unit;
                    AoeFormationFollowAttach attach{
                        static_cast<std::uint32_t>(group.root_chain),
                        route_source, previous_tail, base_distance,
                        follow_plan->inter_chain_gap};
                    actions.steps.push_back({
                        AoeUnitActionStepKind::FormationFollow,
                        attach_progress, detach_progress, token,
                        std::move(attach)});
                    actions.steps.push_back({
                        AoeUnitActionStepKind::FormationDetachFollow,
                        detach_progress, detach_progress, token});
                    action_cursor[chain_index] = detach_progress;
                }
                base_distance +=
                    natural_chain.members.back().distance_from_leader;
                previous_tail = natural_chain.members.back().unit;
            }
        }
    }
    for (std::size_t index = 0; index < leader_action_chains.size(); ++index) {
        auto& actions = leader_action_chains[index];
        if (action_cursor[index] < total_progress - Epsilon)
            actions.steps.push_back({AoeUnitActionStepKind::NavigationPath,
                action_cursor[index], total_progress});
    }
    std::vector<AoeFormationRouteFrame> movement_frames;
    movement_frames.reserve(travel_samples.size() + 4);
    const auto append_movement_frame = [&](float progress, glm::vec2 center,
                                           glm::vec2 forward) {
        if (!movement_frames.empty() &&
            std::abs(movement_frames.back().progress - progress) <= Epsilon) {
            movement_frames.back() = {progress, center, forward};
            return;
        }
        movement_frames.push_back({progress, center, forward});
    };
    append_movement_frame(0.f, context.center.value, poses.front().forward);
    if (prelude_progress > Epsilon)
        append_movement_frame(prelude_progress,
            context.center.value, poses.front().forward);
    for (const float progress : travel_samples) {
        const auto pose = sample_pose_curve(poses, progress);
        append_movement_frame(prelude_progress + progress,
            pose.center, pose.forward);
    }
    if (postlude_progress > Epsilon)
        append_movement_frame(total_progress,
            final_center, final_forward);
    if (movement_frames.size() < 2) return fail();

    struct PendingMemberRoute {
        AoeUnitTarget unit{};
        glm::vec2 origin{0.f};
        float origin_progress = 0.f;
        std::vector<glm::vec2> waypoints;
        std::vector<float> waypoint_progress;
        std::vector<float> speed_ratio;
        glm::vec2 goal{0.f};
    };
    std::vector<PendingMemberRoute> pending;
    pending.reserve(endpoints.size());
    std::uint64_t failed_members = 0;
    const float reformation_step = std::max(
        .01f, settings.maximum_reformation_step);
    const std::uint32_t prelude_steps = prelude_progress > Epsilon
        ? std::max(1u, static_cast<std::uint32_t>(
            std::ceil(prelude_progress / reformation_step))) : 0u;
    const std::uint32_t postlude_steps = postlude_progress > Epsilon
        ? std::max(1u, static_cast<std::uint32_t>(
            std::ceil(postlude_progress / reformation_step))) : 0u;

    const auto navigation_interval_active = [&leader_action_chains](
            std::size_t chain_index, float begin, float end) {
        if (chain_index >= leader_action_chains.size()) return false;
        for (const auto& step : leader_action_chains[chain_index].steps) {
            if (step.kind != AoeUnitActionStepKind::NavigationPath) continue;
            if (std::min(end, step.end_progress) >
                std::max(begin, step.begin_progress) + Epsilon)
                return true;
        }
        return false;
    };

    set_debug_stage(AoeFormationRouteDebugStage::BuildMemberRoutes);
    for (const auto& member : endpoints) {
        PendingMemberRoute route;
        route.unit = member.unit;
        route.origin = member.actual_start;
        route.waypoints.reserve(
            prelude_steps + travel_samples.size() + postlude_steps);
        route.waypoint_progress.reserve(route.waypoints.capacity());
        glm::vec2 previous = route.origin;
        glm::vec2 previous_travel_point{0.f};
        glm::vec2 previous_travel_center{0.f};
        bool have_previous_travel_point = false;
        bool travel_forward_safe = true;
        const auto append_waypoint = [&](glm::vec2 point, float progress) {
            if (same_point(previous, point)) {
                if (route.waypoints.empty()) route.origin_progress = progress;
                else route.waypoint_progress.back() = progress;
                return;
            }
            route.waypoints.push_back(point);
            route.waypoint_progress.push_back(progress);
            previous = point;
        };

        for (std::uint32_t step = 1; step <= prelude_steps; ++step) {
            const float alpha = static_cast<float>(step) /
                static_cast<float>(prelude_steps);
            append_waypoint(glm::mix(
                member.actual_start, member.snake_start, alpha),
                prelude_progress * alpha);
        }
        for (const float progress : travel_samples) {
            const glm::vec2 offset = sample_formation_width_schedule(
                *width_schedule, member.slot_index, progress).local_offset;
            const auto member_pose = sample_pose_curve(poses,
                progress + offset.y);
            const glm::vec2 right{
                member_pose.forward.y, -member_pose.forward.x};
            const glm::vec2 point = member_pose.center +
                right * offset.x;
            if (have_previous_travel_point) {
                const glm::vec2 center_delta =
                    member_pose.center - previous_travel_center;
                const float center_distance2 = glm::dot(
                    center_delta, center_delta);
                if (center_distance2 > Epsilon * Epsilon) {
                    const float forward_ratio = glm::dot(
                        point - previous_travel_point, center_delta) /
                        center_distance2;
                    if (forward_ratio + 1e-3f <
                        settings.minimum_member_forward_ratio)
                        travel_forward_safe = false;
                }
            }
            previous_travel_point = point;
            previous_travel_center = member_pose.center;
            have_previous_travel_point = true;
            append_waypoint(point,
                prelude_progress + progress);
        }
        for (std::uint32_t step = 1; step <= postlude_steps; ++step) {
            const float alpha = static_cast<float>(step) /
                static_cast<float>(postlude_steps);
            append_waypoint(glm::mix(
                member.snake_end, member.natural_end, alpha),
                prelude_progress + travel_progress +
                    postlude_progress * alpha);
        }

        route.speed_ratio.reserve(route.waypoints.size());
        previous = route.origin;
        float previous_progress = route.origin_progress;
        for (std::size_t index = 0; index < route.waypoints.size(); ++index) {
            const float distance = glm::length(
                route.waypoints[index] - previous);
            const float progress_delta = std::max(0.f,
                route.waypoint_progress[index] - previous_progress);
            route.speed_ratio.push_back(progress_delta > Epsilon
                ? distance / progress_delta : 1.f);
            previous = route.waypoints[index];
            previous_progress = route.waypoint_progress[index];
        }
        route.goal = member.natural_end;
        const auto& collider = reg.get<AoeCollider>(route.unit.entity);
        const bool retained_root = std::any_of(
            follow_plan->groups.begin(), follow_plan->groups.end(),
            [&](const AoeFormationFollowGroup& group) {
                return group.root_chain == member.slot_index;
            });
        bool static_safe = !route.waypoints.empty();
        previous = route.origin;
        previous_progress = route.origin_progress;
        for (std::size_t index = 0;
             static_safe && index < route.waypoints.size(); ++index) {
            const float next_progress = route.waypoint_progress[index];
            if (previous_progress + Epsilon >= prelude_progress &&
                navigation_interval_active(member.slot_index,
                    previous_progress, next_progress) &&
                logic_map && logic_map->valid() &&
                logic_map->static_safe_fraction(previous,
                    route.waypoints[index],
                    {collider.radius_x, collider.radius_y}) <
                    1.f - settings.path_validation_epsilon)
                static_safe = false;
            previous = route.waypoints[index];
            previous_progress = next_progress;
        }
        const bool route_safe = (!retained_root || travel_forward_safe) &&
            static_safe;
        if (debug_enabled) {
            AoeFormationRouteDebugMemberRoute debug_route;
            debug_route.unit = route.unit;
            debug_route.natural_chain = member.slot_index;
            debug_route.origin = route.origin;
            debug_route.waypoints = route.waypoints;
            debug_route.forward_safe = travel_forward_safe;
            debug_route.static_safe = static_safe;
            debug_route.accepted = route_safe;
            debug_snapshot.member_routes.push_back(std::move(debug_route));
            if (!route_safe && debug_snapshot.failed_chain ==
                    static_cast<std::size_t>(-1))
                debug_snapshot.failed_chain = member.slot_index;
        }
        if (!route_safe) {
            ++failed_members;
            continue;
        }
        pending.push_back(std::move(route));
    }

    if (failed_members || pending.size() != endpoints.size())
        return fail(std::max<std::uint64_t>(1, failed_members));

    set_debug_stage(AoeFormationRouteDebugStage::Commit);
    AoeFormationRoutePlan route_plan;
    route_plan.squad = context.squad;
    route_plan.order_revision = context.order.revision;
    route_plan.layout_revision = context.layout.revision;
    route_plan.logic_map_revision = corridor_component->logic_map_revision;
    route_plan.nav_mesh_revision = corridor.map_revision;
    route_plan.settings_signature = settings_signature;
    route_plan.travel_layout = *travel_layout;
    route_plan.width_schedule = std::move(*width_schedule);
    route_plan.poses = std::move(poses);
    route_plan.natural_width = natural_width;
    route_plan.bottleneck_width = bottleneck_width;
    route_plan.selected_width = travel_layout->bounds.width();
    route_plan.prelude_progress = prelude_progress;
    route_plan.travel_progress = travel_progress;
    route_plan.postlude_progress = postlude_progress;
    route_plan.total_progress = total_progress;
    route_plan.narrowed =
        route_plan.selected_width < natural_width - WidthEpsilon;
    route_plan.valid = true;
    reg.emplace_or_replace<AoeFormationRoutePlan>(
        context.squad, std::move(route_plan));

    auto& installed_follow_plan =
        reg.emplace_or_replace<AoeFormationFollowPlan>(
            context.squad, std::move(*follow_plan));
    apply_natural_follow_topology(reg, installed_follow_plan);
    reg.emplace_or_replace<AoeFormationFollowTopology>(
        context.squad, std::move(*follow_topology));

    auto& trajectory = reg.emplace_or_replace<AoeFormationRouteTrajectory>(
        context.squad, AoeFormationRouteTrajectory{
            context.squad, context.order.revision,
            context.layout.revision, std::move(movement_frames),
            total_progress, true});
    diagnostics.frames_generated += trajectory.frames.size();
    diagnostics.maximum_frames = std::max<std::uint64_t>(
        diagnostics.maximum_frames, trajectory.frames.size());

    for (auto& route : pending) {
        write_member_route(reg, route.unit.entity, route.waypoints,
            route.origin, route.origin_progress, route.waypoint_progress,
            route.speed_ratio, route.goal,
            corridor_component->logic_map_revision, context.order.revision,
            context.tick, context.squad, route.unit.instance_id);
        state.units.push_back({route.unit.entity, route.unit.instance_id});
    }
    for (std::size_t index = 0; index < installed_follow_plan.chains.size();
         ++index) {
        const auto leader =
            installed_follow_plan.chains[index].members.front().unit;
        reg.emplace_or_replace<AoeUnitActionChain>(
            leader.entity, std::move(leader_action_chains[index]));
    }
    state.status = AoeFormationRouteSplitStatus::Ready;
    diagnostics.members_routed += pending.size();
    diagnostics.portals_processed += corridor.portals.size();

    finish_diagnostics();
    set_debug_stage(AoeFormationRouteDebugStage::Completed);
    publish_debug(true);
    return AoeFormationModuleResult::Continue;
}

AoeFormationModuleResult AoePassThroughMovingControlModule::run(
    EcsWorld& world, AoeFormationSquadContext& context) {
    auto& reg = world.reg();
    const auto* split =
        reg.try_get<AoeFormationRouteSplitState>(context.squad);
    if (!split || split->status != AoeFormationRouteSplitStatus::Ready ||
        split->order_revision != context.order.revision ||
        (context.order.type != AoeSquadOrderType::MoveTo &&
         context.order.type != AoeSquadOrderType::AttackMove))
        return AoeFormationModuleResult::Continue;

    for (const auto& unit : split->units) {
        if (!route_unit_valid(reg, unit) ||
            reg.all_of<AoeAttackOrder>(unit.entity))
            continue;
        const auto* route_owner =
            reg.try_get<AoeFormationRouteOwner>(unit.entity);
        const auto* path = reg.try_get<AoeNavigationPath>(unit.entity);
        if (!route_owner || route_owner->squad != context.squad ||
            route_owner->squad_order_revision != context.order.revision ||
            route_owner->unit_instance_id != unit.instance_id || !path ||
            path->no_path || path->waypoints.empty() ||
            path->current >= path->waypoints.size())
            continue;

        reg.emplace_or_replace<AoeMoveGoal>(unit.entity,
            AoeMoveGoal{path->requested_goal, 0.f, {}});
        reg.emplace_or_replace<AoeFormationMoveGoalOwner>(unit.entity,
            AoeFormationMoveGoalOwner{context.squad,
                context.order.revision, unit.instance_id});
    }
    return AoeFormationModuleResult::Continue;
}

void AoeSynchronizedFormationMovingControlModule::install(App& app) {
    app.world.resource_or_add<AoeFormationMovingSettings>();
    app.world.resource_or_add<AoeFormationMovingDiagnostics>();
}

AoeFormationModuleResult AoeSynchronizedFormationMovingControlModule::run(
    EcsWorld& world, AoeFormationSquadContext& context) {
    return aoe_synchronized_follow_motion_system(world, context);
}

AoeFormationModuleResult AoeRouteCommandCompletionModule::run(
    EcsWorld& world, AoeFormationSquadContext& context) {
    const bool moving_order =
        context.order.type == AoeSquadOrderType::MoveTo ||
        context.order.type == AoeSquadOrderType::AttackMove;
    if (!moving_order) return AoeFormationModuleResult::Continue;

    auto& reg = world.reg();
    auto* split = reg.try_get<AoeFormationRouteSplitState>(context.squad);
    const auto* trajectory =
        reg.try_get<AoeFormationRouteTrajectory>(context.squad);
    const auto* follow_plan =
        reg.try_get<AoeFormationFollowPlan>(context.squad);
    const auto* topology =
        reg.try_get<AoeFormationFollowTopology>(context.squad);
    const auto* moving =
        reg.try_get<AoeFormationMovingState>(context.squad);
    if (!split || !trajectory || !follow_plan || !topology || !moving ||
        split->status != AoeFormationRouteSplitStatus::Ready ||
        moving->status != AoeFormationMovingStatus::Arrived ||
        split->order_revision != context.order.revision ||
        trajectory->order_revision != context.order.revision ||
        follow_plan->order_revision != context.order.revision ||
        topology->order_revision != context.order.revision ||
        moving->order_revision != context.order.revision ||
        !trajectory->valid || !follow_plan->valid || !topology->valid ||
        topology->attached_chains != 0 ||
        topology->bindings.size() != follow_plan->chains.size())
        return AoeFormationModuleResult::Continue;

    const auto& settings =
        world.resource_or_add<AoeFormationMovingSettings>();
    const float tolerance = std::max(.01f, settings.spacing_tolerance);
    for (std::size_t index = 0; index < follow_plan->chains.size(); ++index) {
        const auto& chain = follow_plan->chains[index];
        const auto& binding = topology->bindings[index];
        if (chain.members.empty() || binding.attached ||
            binding.natural_chain != index || binding.root_chain != index ||
            binding.active_follow_token != 0 ||
            binding.progress + settings.progress_epsilon <
                trajectory->total_progress)
            return AoeFormationModuleResult::Continue;
        const auto leader = chain.members.front().unit;
        if (!detail::aoe_gameplay_squad_member_valid(reg, leader))
            continue;
        const auto* actions =
            reg.try_get<AoeUnitActionChain>(leader.entity);
        if (!actions || !actions->valid ||
            actions->squad != context.squad ||
            actions->order_revision != context.order.revision ||
            actions->unit_instance_id != leader.instance_id ||
            actions->current != actions->steps.size() ||
            actions->active_follow_token != 0)
            return AoeFormationModuleResult::Continue;
    }

    for (const auto& slot : context.layout.layout.slots) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, slot.unit))
            continue;
        if (reg.all_of<AoeAttackOrder>(slot.unit.entity))
            return AoeFormationModuleResult::Continue;
        const glm::vec2 target = aoe_formation_slot_world(
            context.center, context.formation, slot);
        if (glm::length(reg.get<AoePosition>(slot.unit.entity).value - target) >
            tolerance)
            return AoeFormationModuleResult::Continue;
    }

    const glm::vec2 destination = context.order.destination;
    const std::uint64_t completed_revision = context.order.revision;
    clear_owned_routes(reg, context.squad, *split);
    reg.remove<AoeFormationRouteSplitState, AoeFormationNavCorridor,
               AoeNavigationPath>(context.squad);

    for (const auto& member : context.members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member)) continue;
        reg.remove<AoeSquadMoveSpeedLimit>(member.entity);
        if (auto* request = reg.try_get<AoePathMotionRequest>(member.entity))
            request->valid = false;
        if (auto* intent =
                reg.try_get<AoeUnitMovementIntentState>(member.entity))
            intent->valid = false;
        if (auto* decision =
                reg.try_get<AoeGlobalMotionDecision>(member.entity))
            decision->valid = false;
        if (auto* locomotion = reg.try_get<AoeLocomotionState>(member.entity)) {
            locomotion->velocity = {0.f, 0.f};
            locomotion->actual_speed = 0.f;
            locomotion->effective_max_speed = 0.f;
            locomotion->stalled_ticks = 0;
        }
        auto& action = reg.get<AoeActionState>(member.entity);
        if (action.state == UnitState::Attacking ||
            action.state == UnitState::Dying ||
            action.state == UnitState::Disappearing)
            continue;
        if (action.state != UnitState::Idle) ++action.sequence;
        action.state = UnitState::Idle;
        action.state_started_tick = context.tick;
        action.critical = false;
        action.release_emitted = false;
    }

    context.order = {};
    context.order.revision = completed_revision;
    context.center.value = destination;
    context.state.phase = AoeSquadPhase::Idle;
    context.state.movement_speed = 0.f;
    return AoeFormationModuleResult::StopSquad;
}

namespace detail {
void aoe_modular_formation_fixed_tick(EcsWorld& world, std::uint64_t tick,
    const std::array<AoeFormationModuleRun, 5>& modules) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto formation_started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    for (const auto squad : reg.view<AoeFormationIntent>())
        reg.get<AoeFormationIntent>(squad).valid = false;
    for (const auto squad : reg.view<AoeFormationResult>())
        reg.get<AoeFormationResult>(squad).valid = false;

    const auto view = reg.view<
        AoeSquadMembers, AoeSquadSpawnState, AoeSquadFormation,
        AoeSquadCombatSettings, AoeSquadOrder, AoeSquadState,
        AoePosition, AoeCollider, AoeTeam>();
    for (const auto squad : view) {
        auto& spawn = view.get<AoeSquadSpawnState>(squad);
        if (spawn.status == AoeSquadSpawnStatus::Pending ||
            spawn.status == AoeSquadSpawnStatus::Failed ||
            spawn.status == AoeSquadSpawnStatus::Empty)
            continue;
        auto& layout = reg.get_or_emplace<AoeSquadLayoutState>(squad);
        AoeFormationSquadContext context{
            squad,
            tick,
            view.get<AoeSquadMembers>(squad),
            spawn,
            view.get<AoeSquadFormation>(squad),
            layout,
            view.get<AoeSquadCombatSettings>(squad),
            view.get<AoeSquadOrder>(squad),
            view.get<AoeSquadState>(squad),
            view.get<AoePosition>(squad),
            view.get<AoeCollider>(squad),
            view.get<AoeTeam>(squad),
        };
        for (std::size_t index = 0; index < modules.size(); ++index) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            const auto module_started = std::chrono::steady_clock::now();
#endif
            const auto result = modules[index](world, context);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            const double elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - module_started).count();
            auto& diagnostics = world.resource_or_add<
                AoeGameplayPerformanceDiagnostics>();
            switch (index) {
            case 0: diagnostics.formation_layout_ms += elapsed; break;
            case 1: diagnostics.formation_route_split_ms += elapsed; break;
            case 2: diagnostics.formation_moving_control_ms += elapsed; break;
            case 3: diagnostics.formation_attack_control_ms += elapsed; break;
            case 4:
                diagnostics.formation_command_completion_ms += elapsed;
                break;
            default: break;
            }
#endif
            if (result == AoeFormationModuleResult::StopSquad) break;
        }
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>().formation_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - formation_started).count();
#endif
}
} // namespace detail
} // namespace gld::ecs::aoe
