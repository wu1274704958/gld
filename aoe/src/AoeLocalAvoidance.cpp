#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

bool target_valid(const entt::registry& reg, const AoeUnitTarget& target) {
    if (target.entity == entt::null || !reg.valid(target.entity) ||
        reg.any_of<AoePooledUnit, AoeRecyclePending>(target.entity))
        return false;
    const auto* identity = reg.try_get<AoeGameplayIdentity>(target.entity);
    const auto* health = reg.try_get<AoeHealth>(target.entity);
    return identity && identity->instance_id == target.instance_id &&
           health && health->current > 0.f;
}

entt::entity ignored_formation_squad(
    const entt::registry& reg, entt::entity entity) {
    const auto* member = reg.try_get<AoeSquadMember>(entity);
    if (!member || !reg.valid(member->squad)) return entt::null;
    const auto* attack = reg.try_get<AoeAttackOrder>(entity);
    return attack && target_valid(reg, attack->target)
        ? entt::null : member->squad;
}

float steering_dynamic_safe_fraction(
    glm::vec2 from, glm::vec2 to, glm::vec2 radii,
    std::span<const AoeSteeringNeighbor> neighbors) {
    float result = 1.f;
    const glm::vec2 delta = to - from;
    for (const auto& neighbor : neighbors) {
        const glm::vec2 combined = radii + neighbor.radii;
        if (!(combined.x > 0.f) || !(combined.y > 0.f)) continue;
        const glm::vec2 start = from - neighbor.position;
        const float rx2 = combined.x * combined.x;
        const float ry2 = combined.y * combined.y;
        const float a = delta.x * delta.x / rx2 + delta.y * delta.y / ry2;
        const float b = 2.f * (start.x * delta.x / rx2 +
                              start.y * delta.y / ry2);
        const float c = start.x * start.x / rx2 + start.y * start.y / ry2 - 1.f;
        float enter = 1.f;
        if (c <= 0.f) enter = b >= -Epsilon ? 1.f : 0.f;
        else if (a > Epsilon) {
            const float discriminant = b * b - 4.f * a * c;
            if (discriminant >= 0.f) {
                const float value = (-b - std::sqrt(std::max(0.f, discriminant))) /
                                    (2.f * a);
                if (value >= 0.f && value <= 1.f) enter = value;
            }
        }
        if (enter < 1.f)
            result = std::min(result, std::max(0.f, enter - .0001f));
    }
    return result;
}
} // namespace

void AoeFullLocalAvoidancePlugin::install(App& app) {
    app.world.resource_or_add<AoeLocalAvoidanceSettings>();
    app.world.resource_or_add<AoeLocalAvoidanceScratch>();
}

void AoeFullLocalAvoidancePlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoeMovementIntent>())
        reg.get<AoeMovementIntent>(entity).valid = false;
    const auto& settings = world.resource<AoeLocalAvoidanceSettings>();
    auto& diagnostics = world.resource_or_add<AoeGameplayDiagnostics>();
    auto& neighbors = world.resource<AoeLocalAvoidanceScratch>().nearest_neighbors;
    neighbors.clear();
    neighbors.reserve(settings.max_neighbors + 1u);
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto* dynamic = world.try_resource<AoeDynamicObstacleIndex>();

    for (const auto entity : reg.view<AoePathMotionRequest, AoePosition,
                                      AoeCollider, AoeLocomotionState>()) {
        const auto& request = reg.get<AoePathMotionRequest>(entity);
        if (!request.valid || request.produced_tick != tick) continue;
        const auto& position = reg.get<AoePosition>(entity);
        const auto& collider = reg.get<AoeCollider>(entity);
        const glm::vec2 radii{collider.radius_x, collider.radius_y};
        const auto& locomotion = reg.get<AoeLocomotionState>(entity);
        auto& local = reg.get_or_emplace<AoeLocalAvoidanceState>(entity);
        local.escape_steering = locomotion.stalled_ticks >=
            std::max(1u, settings.escape_stalled_ticks);
        const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
        const entt::entity ignored_squad =
            ignored_formation_squad(reg, entity);
        neighbors.clear();
        if (map && map->valid() && dynamic && settings.max_neighbors > 0) {
            const float query_radius = std::max(
                request.max_speed * settings.prediction_seconds +
                    settings.separation_padding,
                std::max(radii.x, radii.y) * 4.f);
            dynamic->query(*map, position.value - glm::vec2(query_radius),
                position.value + glm::vec2(query_radius),
                [&](const AoeDynamicObstacleEntry& obstacle) {
                    if (obstacle.entity == entity ||
                        (ignored_squad != entt::null &&
                         obstacle.squad == ignored_squad))
                        return;
                    const AoeSteeringNeighbor candidate{obstacle.entity,
                        obstacle.instance_id, obstacle.center,
                        obstacle.radii, obstacle.velocity};
                    const glm::vec2 candidate_delta =
                        obstacle.center - position.value;
                    const float candidate_distance = glm::dot(
                        candidate_delta, candidate_delta);
                    const auto insertion = std::lower_bound(
                        neighbors.begin(), neighbors.end(), candidate_distance,
                        [&](const AoeSteeringNeighbor& value, float distance2) {
                            const glm::vec2 delta = value.position - position.value;
                            return glm::dot(delta, delta) < distance2;
                        });
                    neighbors.insert(insertion, candidate);
                    if (neighbors.size() > settings.max_neighbors)
                        neighbors.pop_back();
                });
        }
        int preferred_side = local.avoidance_side;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity);
            member && reg.valid(member->squad))
            if (const auto* traffic =
                    reg.try_get<AoeSquadTrafficState>(member->squad);
                traffic && traffic->negotiated_side != 0)
                preferred_side = traffic->negotiated_side;
        bool threatened = false;
        bool imminent = false;
        std::uint64_t threat_signature = 1469598103934665603ull;
        const float request_speed = glm::length(request.velocity);
        const glm::vec2 request_direction = request_speed > Epsilon
            ? request.velocity / request_speed : glm::vec2{0.f};
        const auto heading_x = static_cast<std::int32_t>(
            std::round(request_direction.x * 1024.f));
        const auto heading_y = static_cast<std::int32_t>(
            std::round(request_direction.y * 1024.f));
        threat_signature ^= static_cast<std::uint32_t>(heading_x);
        threat_signature *= 1099511628211ull;
        threat_signature ^= static_cast<std::uint32_t>(heading_y);
        threat_signature *= 1099511628211ull;
        for (const auto& neighbor : neighbors) {
            threat_signature ^= neighbor.instance_id;
            threat_signature *= 1099511628211ull;
            const glm::vec2 relative = neighbor.position - position.value;
            const glm::vec2 relative_velocity = request.velocity - neighbor.velocity;
            const float speed2 = glm::dot(relative_velocity, relative_velocity);
            const float contact_time = speed2 > Epsilon
                ? std::clamp(glm::dot(relative, relative_velocity) / speed2,
                    0.f, settings.prediction_seconds)
                : 0.f;
            const glm::vec2 closest = relative - relative_velocity * contact_time;
            const glm::vec2 combined = radii + neighbor.radii +
                glm::vec2(settings.separation_padding);
            const float normalized = closest.x * closest.x /
                    (combined.x * combined.x) +
                closest.y * closest.y / (combined.y * combined.y);
            threatened = threatened || normalized < 4.f;
            imminent = imminent || (normalized < 1.f && contact_time <=
                settings.imminent_collision_seconds);
        }
        diagnostics.steering_neighbors_considered += neighbors.size();
        AoeSteeringResult result{request.velocity};
        const auto interval = std::max(1u, settings.full_solve_interval);
        const bool cache_valid = local.last_steering_tick != 0 &&
            std::isfinite(local.cached_target_velocity.x) &&
            std::isfinite(local.cached_target_velocity.y) &&
            glm::dot(local.cached_target_velocity,
                     local.cached_target_velocity) > Epsilon * Epsilon;
        const bool cadence_due = !identity ||
            ((tick + identity->instance_id) % interval == 0);
        const bool signature_changed =
            threat_signature != local.threat_signature;
        const bool full_solve = local.escape_steering || !threatened ||
            imminent || !cache_valid || signature_changed || cadence_due;
        if (!full_solve) {
            result.target_velocity = local.cached_target_velocity;
            result.avoidance_side = local.avoidance_side;
            result.threatened = true;
            ++diagnostics.steering_cached_solves;
        } else {
            result = DefaultLocalSteeringLogic::steer(
                {entity, identity ? identity->instance_id : 0,
                 position.value, radii, locomotion.velocity, request.velocity,
                 request.local_goal, request.max_speed,
                 settings.prediction_seconds, settings.separation_padding,
                 map && map->valid() ? map : nullptr, neighbors,
                 preferred_side,
                 settings.side_switch_margin +
                     (local.avoidance_side_hold_ticks > 0 && !imminent
                          ? 2.f : 0.f),
                 settings.candidate_angle_step,
                 local.escape_steering ? settings.escape_max_angle
                                       : settings.normal_max_angle,
                 settings.minimum_safe_fraction});
            result.infeasible = glm::length(request.velocity) > Epsilon &&
                                glm::length(result.target_velocity) <= Epsilon;
            local.cached_target_velocity = result.target_velocity;
            local.last_steering_tick = tick;
            local.threat_signature = threat_signature;
            if (local.escape_steering) ++diagnostics.steering_escape_solves;
            if (imminent) ++diagnostics.steering_imminent_solves;
            else if (threatened) ++diagnostics.steering_full_solves;
            else ++diagnostics.steering_fast_path;
        }
        if (local.avoidance_side_hold_ticks > 0)
            --local.avoidance_side_hold_ticks;
        if (result.avoidance_side != 0 &&
            result.avoidance_side != local.avoidance_side) {
            if (local.avoidance_side != 0)
                ++diagnostics.steering_side_switches;
            local.avoidance_side = static_cast<std::int8_t>(result.avoidance_side);
            local.avoidance_side_hold_ticks = static_cast<std::uint8_t>(
                std::min(settings.side_hold_ticks,
                    static_cast<std::uint32_t>(
                        std::numeric_limits<std::uint8_t>::max())));
        }
        if (!std::isfinite(result.target_velocity.x) ||
            !std::isfinite(result.target_velocity.y)) {
            result.target_velocity = request.velocity;
            ++diagnostics.steering_fallbacks;
        }
        local.infeasible = result.infeasible;
        reg.emplace_or_replace<AoeMovementIntent>(entity,
            AoeMovementIntent{request.kind, result.target_velocity,
                request.velocity, request.local_goal,
                static_cast<std::uint32_t>(neighbors.size()),
                static_cast<std::int8_t>(result.avoidance_side),
                result.threatened, result.infeasible, tick, true});
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .local_avoidance_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

AoeSteeringResult DefaultLocalSteeringLogic::steer(
    const AoeSteeringContext& context) {
    const float preferred_speed = glm::length(context.preferred_velocity);
    if (!(preferred_speed > Epsilon) || !(context.max_speed > 0.f)) return {};
    const glm::vec2 preferred = context.preferred_velocity / preferred_speed;
    const glm::vec2 current = glm::length(context.current_velocity) > Epsilon
        ? glm::normalize(context.current_velocity) : preferred;
    const float horizon = std::max(0.f, context.prediction_seconds);
    bool threatened = false;
    for (const auto& neighbor : context.neighbors) {
        const glm::vec2 relative_position = neighbor.position - context.position;
        const glm::vec2 relative_velocity =
            context.preferred_velocity - neighbor.velocity;
        const float relative_speed2 = glm::dot(relative_velocity, relative_velocity);
        const float closest_time = relative_speed2 > Epsilon
            ? std::clamp(glm::dot(relative_position, relative_velocity) /
                             relative_speed2,
                         0.f, horizon)
            : 0.f;
        const glm::vec2 separation = relative_position -
            relative_velocity * closest_time;
        const glm::vec2 combined = context.radii + neighbor.radii +
            glm::vec2(std::max(0.f, context.separation_padding));
        const float normalized2 = separation.x * separation.x /
                (combined.x * combined.x) +
            separation.y * separation.y / (combined.y * combined.y);
        if (normalized2 < 4.f) { threatened = true; break; }
    }
    const float desired_feeler = std::max(context.max_speed * horizon,
        std::max(context.radii.x, context.radii.y) * 1.5f);
    const float feeler = std::min(desired_feeler,
        std::max(Epsilon, glm::length(context.goal - context.position)));
    float straight_clearance = 1.f;
    if (context.map && context.map->valid() && horizon > 0.f)
        straight_clearance = context.map->static_safe_fraction(
            context.position, context.position + preferred * feeler,
            context.radii);
    if (!threatened && straight_clearance >= 1.f - Epsilon)
        return {context.preferred_velocity, context.preferred_avoidance_side,
                false};
    const std::uint64_t initial_side_seed = context.neighbors.empty()
        ? context.instance_id
        : (context.instance_id ^ context.neighbors.front().instance_id);
    const int held_side = context.preferred_avoidance_side == 0
        ? ((initial_side_seed & 1u) != 0u ? 1 : -1)
        : (context.preferred_avoidance_side > 0 ? 1 : -1);
    const float angle_step = std::clamp(context.candidate_angle_step,
        .0872664626f, .78539816339f);
    const float max_angle = std::clamp(context.candidate_max_angle,
        angle_step, 1.57079632679f);
    const int steps = std::clamp(
        static_cast<int>(std::ceil(max_angle / angle_step)), 1, 4);
    float best_score = -std::numeric_limits<float>::infinity();
    glm::vec2 best = context.preferred_velocity;
    int best_side = held_side;
    for (int candidate = 0; candidate <= steps * 2; ++candidate) {
        int signed_step = 0;
        if (candidate > 0) {
            const int magnitude = (candidate + 1) / 2;
            signed_step = (candidate & 1) ? held_side * magnitude
                                          : -held_side * magnitude;
        }
        const float angle = std::clamp(
            static_cast<float>(signed_step) * angle_step, -max_angle, max_angle);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const glm::vec2 direction{preferred.x * cosine - preferred.y * sine,
                                  preferred.x * sine + preferred.y * cosine};
        const int side = signed_step == 0 ? 0 : (signed_step > 0 ? 1 : -1);
        const glm::vec2 velocity = direction *
            std::min(preferred_speed, context.max_speed);
        float static_clearance = side == 0 ? straight_clearance : 1.f;
        if (side != 0 && context.map && context.map->valid() && horizon > 0.f)
            static_clearance = context.map->static_safe_fraction(
                context.position, context.position + direction * feeler,
                context.radii);
        float dynamic_penalty = 0.f;
        float nearest_margin = 4.f;
        for (const auto& neighbor : context.neighbors) {
            const glm::vec2 relative_position =
                neighbor.position - context.position;
            const glm::vec2 relative_velocity = velocity - neighbor.velocity;
            const float relative_speed2 = glm::dot(
                relative_velocity, relative_velocity);
            const float closest_time = relative_speed2 > Epsilon
                ? std::clamp(glm::dot(relative_position, relative_velocity) /
                                 relative_speed2,
                             0.f, horizon)
                : 0.f;
            const glm::vec2 separation = relative_position -
                relative_velocity * closest_time;
            const glm::vec2 combined = context.radii + neighbor.radii +
                glm::vec2(std::max(0.f, context.separation_padding));
            const float normalized2 = separation.x * separation.x /
                    (combined.x * combined.x) +
                separation.y * separation.y / (combined.y * combined.y);
            nearest_margin = std::min(nearest_margin, normalized2);
            if (normalized2 < 1.f)
                dynamic_penalty += (1.f - normalized2) * 12.f + 4.f;
            else if (normalized2 < 4.f)
                dynamic_penalty += (4.f - normalized2) * .25f;
        }
        const float progress = glm::dot(direction, preferred);
        const float continuity = glm::dot(direction, current);
        const float usable_distance = static_clearance * feeler;
        float score = progress * 3.f + continuity * .75f +
            static_clearance * 8.f + usable_distance * 2.f +
            std::min(nearest_margin, 4.f) * .15f - dynamic_penalty;
        if (static_clearance < std::clamp(
                context.minimum_safe_fraction, 0.f, 1.f))
            score -= 100.f;
        if (side == held_side)
            score += std::max(0.f, context.side_switch_margin);
        if (score > best_score) {
            best_score = score;
            best = velocity;
            best_side = side == 0 ? held_side : side;
        }
    }
    return {best, best_side, true};
}

} // namespace gld::ecs::aoe
