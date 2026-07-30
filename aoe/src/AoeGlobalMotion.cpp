#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <unordered_map>

#include <ecs/PerformanceMonitoring.hpp>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

float unit_flow_radius(const AoeUnitFlowRecord& value) {
    return std::max(value.radii.x, value.radii.y);
}

void unit_flow_tick(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    world.resource_or_add<AoeNavigationSettings>();
    world.resource_or_add<AoeGameplayDiagnostics>();
    world.resource_or_add<AoeUnitFlowIndex>();
    const auto& settings = world.resource<AoeNavigationSettings>();
    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
    auto& index = world.resource<AoeUnitFlowIndex>();
    index.records.clear();
    index.candidates.clear();
    index.selected.clear();
    index.maximum_reach = 0.f;
    for (const auto entity : reg.view<AoeGlobalMotionDecision>())
        reg.get<AoeGlobalMotionDecision>(entity).valid = false;
    const auto acceleration_limited = [&](entt::entity entity,
                                          glm::vec2 velocity) {
        const float target_speed = glm::length(velocity);
        const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
        const float current_speed = locomotion
            ? glm::length(locomotion->velocity) : 0.f;
        const float change = std::max(0.f,
            settings.steering_max_acceleration) * fixed_dt;
        const float speed = current_speed < target_speed
            ? std::min(target_speed, current_speed + change)
            : std::max(target_speed, current_speed - change);
        return target_speed > Epsilon
            ? velocity * (speed / target_speed) : glm::vec2{0.f};
    };
    for (const auto entity : reg.view<AoeMovementIntent, AoePosition,
                                      AoeCollider, AoeGameplayIdentity,
                                      AoeTeam>()) {
        const auto& intent = reg.get<AoeMovementIntent>(entity);
        if (!intent.valid || intent.produced_tick != tick) continue;
        const auto& collider = reg.get<AoeCollider>(entity);
        entt::entity squad = entt::null;
        if (const auto* member = reg.try_get<AoeSquadMember>(entity))
            squad = member->squad;
        index.records.push_back({entity,
            reg.get<AoeGameplayIdentity>(entity).instance_id, squad,
            reg.get<AoeTeam>(entity).id, intent.kind,
            reg.get<AoePosition>(entity).value,
            {collider.radius_x, collider.radius_y}, intent.velocity});
        index.maximum_reach = std::max(index.maximum_reach,
            unit_flow_radius(index.records.back()) +
            glm::length(intent.velocity) * settings.unit_flow_prediction_seconds);
        (void)reg.get_or_emplace<AoeGlobalMotionState>(entity);
        reg.emplace_or_replace<AoeGlobalMotionDecision>(entity,
            AoeGlobalMotionDecision{.velocity = intent.velocity,
                .produced_tick = tick, .valid = true});
        if (intent.locally_infeasible)
            ++diagnostics.flow_infeasible_assignments;
    }
    diagnostics.flow_active_intents += index.records.size();
    if (!settings.unit_flow_enabled) {
        for (const auto& record : index.records) {
            auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
            decision.velocity = acceleration_limited(
                record.entity, decision.velocity);
        }
        return;
    }
    std::sort(index.records.begin(), index.records.end(),
        [](const AoeUnitFlowRecord& a, const AoeUnitFlowRecord& b) {
            if (std::abs(a.position.x - b.position.x) > Epsilon)
                return a.position.x < b.position.x;
            return a.instance_id < b.instance_id;
        });
    for (std::size_t i = 0; i < index.records.size(); ++i)
        for (std::size_t j = i + 1; j < index.records.size(); ++j) {
            const auto& a = index.records[i];
            const auto& b = index.records[j];
            const float reach = unit_flow_radius(a) +
                glm::length(a.intent_velocity) *
                    settings.unit_flow_prediction_seconds;
            if (b.position.x - a.position.x > reach + index.maximum_reach)
                break;
            const glm::vec2 relative = b.position - a.position;
            const glm::vec2 relative_velocity =
                a.intent_velocity - b.intent_velocity;
            const float relative_speed2 = glm::dot(
                relative_velocity, relative_velocity);
            const float time = relative_speed2 > Epsilon
                ? std::clamp(glm::dot(relative, relative_velocity) /
                                 relative_speed2,
                             0.f, settings.unit_flow_prediction_seconds)
                : 0.f;
            const float closest = glm::length(
                relative - relative_velocity * time);
            const float clearance = unit_flow_radius(a) +
                unit_flow_radius(b) + settings.unit_flow_follow_gap;
            if (closest > clearance) continue;
            index.candidates.push_back({i, j, time, closest});
        }
    std::sort(index.candidates.begin(), index.candidates.end(),
        [&](const AoeUnitFlowConflict& a,
            const AoeUnitFlowConflict& b) {
            if (std::abs(a.time_to_collision - b.time_to_collision) > Epsilon)
                return a.time_to_collision < b.time_to_collision;
            if (std::abs(a.closest_distance - b.closest_distance) > Epsilon)
                return a.closest_distance < b.closest_distance;
            const auto aid = index.records[a.a].instance_id ^
                             index.records[a.b].instance_id;
            const auto bid = index.records[b.a].instance_id ^
                             index.records[b.b].instance_id;
            return aid < bid;
        });
    std::vector<std::uint32_t> selected_count(index.records.size(), 0);
    std::vector<std::uint32_t> candidate_count(index.records.size(), 0);
    for (const auto& edge : index.candidates) {
        ++candidate_count[edge.a];
        ++candidate_count[edge.b];
        if (selected_count[edge.a] >= settings.unit_flow_max_neighbors ||
            selected_count[edge.b] >= settings.unit_flow_max_neighbors)
            continue;
        index.selected.push_back(edge);
        ++selected_count[edge.a];
        ++selected_count[edge.b];
    }
    diagnostics.flow_neighbor_checks += index.candidates.size();
    diagnostics.flow_conflicts += index.selected.size();

    index.parents.resize(index.records.size());
    index.ranks.assign(index.records.size(), 0);
    for (std::size_t i = 0; i < index.parents.size(); ++i)
        index.parents[i] = i;
    const auto find_root = [&](std::size_t value) {
        std::size_t root = value;
        while (index.parents[root] != root) root = index.parents[root];
        while (index.parents[value] != value) {
            const auto next = index.parents[value];
            index.parents[value] = root;
            value = next;
        }
        return root;
    };
    for (const auto& edge : index.selected) {
        auto a = find_root(edge.a);
        auto b = find_root(edge.b);
        if (a == b) continue;
        if (index.ranks[a] < index.ranks[b]) std::swap(a, b);
        index.parents[b] = a;
        if (index.ranks[a] == index.ranks[b]) ++index.ranks[a];
    }
    std::unordered_map<std::size_t, std::uint32_t> groups;
    std::uint32_t next_group = 1;
    std::vector<glm::vec2> lateral(index.records.size(), glm::vec2{0.f});
    std::vector<float> speed_scale(index.records.size(), 1.f);
    std::vector<AoeGlobalMotionMode> modes(
        index.records.size(), AoeGlobalMotionMode::Clear);
    std::vector<AoeMotionDecisionReason> reasons(
        index.records.size(), AoeMotionDecisionReason::None);
    std::vector<std::int8_t> sides(index.records.size(), 0);
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto side_open = [&](const AoeUnitFlowRecord& record, int side) {
        if (!map || !map->valid()) return true;
        const float speed = glm::length(record.intent_velocity);
        if (!(speed > Epsilon)) return false;
        const glm::vec2 direction = record.intent_velocity / speed;
        const glm::vec2 right{direction.y, -direction.x};
        const glm::vec2 offset = right * settings.unit_flow_lateral_clearance *
            (side < 0 ? 1.f : -1.f);
        return map->static_safe_fraction(record.position,
            record.position + offset, record.radii) >= 1.f - Epsilon;
    };
    const auto add_side = [&](std::size_t index_value, int preferred,
                              AoeGlobalMotionMode mode,
                              AoeMotionDecisionReason reason) {
        const auto& record = index.records[index_value];
        const float speed = glm::length(record.intent_velocity);
        if (!(speed > Epsilon)) return false;
        int side = preferred;
        if (!side_open(record, side)) side = -side;
        if (!side_open(record, side)) {
            speed_scale[index_value] = std::min(speed_scale[index_value],
                settings.unit_flow_yield_speed_scale);
            modes[index_value] = AoeGlobalMotionMode::Yielding;
            reasons[index_value] = AoeMotionDecisionReason::SideBlocked;
            return false;
        }
        const glm::vec2 direction = record.intent_velocity / speed;
        const glm::vec2 right{direction.y, -direction.x};
        lateral[index_value] += right * (side < 0 ? 1.f : -1.f);
        modes[index_value] = mode;
        reasons[index_value] = reason;
        sides[index_value] = static_cast<std::int8_t>(side);
        return true;
    };
    const auto yield_to = [&](std::size_t yielding, std::size_t priority,
                              AoeMotionDecisionReason reason) {
        speed_scale[yielding] = std::min(speed_scale[yielding],
            settings.unit_flow_yield_speed_scale);
        modes[yielding] = AoeGlobalMotionMode::Yielding;
        reasons[yielding] = reason;
        auto& decision = reg.get<AoeGlobalMotionDecision>(
            index.records[yielding].entity);
        decision.yielding_to = index.records[priority].entity;
        decision.yielding_to_instance = index.records[priority].instance_id;
    };

    // Every selected edge contributes one constraint. Accumulating all of
    // them before normalizing the result handles a connected traffic group
    // without pretending that repeated passes are an iterative solver.
    for (const auto& edge : index.selected) {
        const auto& a = index.records[edge.a];
        const auto& b = index.records[edge.b];
        const float speed_a = glm::length(a.intent_velocity);
        const float speed_b = glm::length(b.intent_velocity);
        if (!(speed_a > Epsilon) || !(speed_b > Epsilon)) continue;
        const glm::vec2 dir_a = a.intent_velocity / speed_a;
        const glm::vec2 dir_b = b.intent_velocity / speed_b;
        const float alignment = glm::dot(dir_a, dir_b);
        const float speed_difference = std::abs(speed_a - speed_b);
        const bool same_speed = speed_difference <=
            std::max(settings.unit_flow_same_speed_absolute,
                std::max(speed_a, speed_b) *
                    settings.unit_flow_same_speed_relative);
        auto& state_a = reg.get<AoeGlobalMotionState>(a.entity);
        auto& state_b = reg.get<AoeGlobalMotionState>(b.entity);
        bool a_priority = speed_a > speed_b;
        if (same_speed) {
            if (state_a.wait_ticks != state_b.wait_ticks &&
                std::max(state_a.wait_ticks, state_b.wait_ticks) >=
                    settings.unit_flow_starvation_ticks) {
                a_priority = state_a.wait_ticks > state_b.wait_ticks;
                ++diagnostics.flow_starvation_promotions;
            } else
                a_priority = a.instance_id < b.instance_id;
        }
        if (alignment <= settings.unit_flow_head_on_dot) {
            // Negotiate one shared passing convention for the edge. A
            // unilateral side flip would put both opposite-facing units
            // into the same world-space lane.
            if (side_open(a, -1) && side_open(b, -1)) {
                add_side(edge.a, -1, AoeGlobalMotionMode::PassingRight,
                         AoeMotionDecisionReason::HeadOnTraffic);
                add_side(edge.b, -1, AoeGlobalMotionMode::PassingRight,
                         AoeMotionDecisionReason::HeadOnTraffic);
            } else if (side_open(a, 1) && side_open(b, 1)) {
                add_side(edge.a, 1, AoeGlobalMotionMode::PassingLeft,
                         AoeMotionDecisionReason::HeadOnTraffic);
                add_side(edge.b, 1, AoeGlobalMotionMode::PassingLeft,
                         AoeMotionDecisionReason::HeadOnTraffic);
            } else {
                yield_to(a_priority ? edge.b : edge.a,
                         a_priority ? edge.a : edge.b,
                         AoeMotionDecisionReason::SideBlocked);
            }
        } else if (alignment >= settings.unit_flow_same_direction_dot &&
                   same_speed) {
            const float collision_distance =
                unit_flow_radius(a) + unit_flow_radius(b);
            if (edge.closest_distance <= collision_distance + Epsilon) {
                const glm::vec2 right{dir_a.y, -dir_a.x};
                const float lateral_separation = glm::dot(
                    b.position - a.position, right);
                const int separation_side =
                    std::abs(lateral_separation) > Epsilon
                    ? (lateral_separation > 0.f ? 1 : -1)
                    : (a_priority ? -1 : 1);
                add_side(edge.a, separation_side,
                    AoeGlobalMotionMode::SideStep,
                    AoeMotionDecisionReason::SameDirectionConflict);
                add_side(edge.b, -separation_side,
                    AoeGlobalMotionMode::SideStep,
                    AoeMotionDecisionReason::SameDirectionConflict);
            }
        } else {
            const std::size_t yielding = a_priority ? edge.b : edge.a;
            const std::size_t priority = a_priority ? edge.a : edge.b;
            const auto reason = alignment >=
                    settings.unit_flow_same_direction_dot
                ? AoeMotionDecisionReason::FasterTraffic
                : AoeMotionDecisionReason::CrossingTraffic;
            if (add_side(yielding, -1,
                         AoeGlobalMotionMode::SideStep, reason)) {
                auto& decision = reg.get<AoeGlobalMotionDecision>(
                    index.records[yielding].entity);
                decision.yielding_to = index.records[priority].entity;
                decision.yielding_to_instance =
                    index.records[priority].instance_id;
            } else {
                yield_to(yielding, priority,
                         AoeMotionDecisionReason::SideBlocked);
            }
        }
    }

    for (std::size_t i = 0; i < index.records.size(); ++i) {
        const auto& record = index.records[i];
        auto& decision = reg.get<AoeGlobalMotionDecision>(record.entity);
        auto& state = reg.get<AoeGlobalMotionState>(record.entity);
        const float speed = glm::length(record.intent_velocity);
        if (glm::length(lateral[i]) > Epsilon && speed > Epsilon) {
            const glm::vec2 direction = record.intent_velocity / speed;
            decision.velocity = glm::normalize(direction +
                glm::normalize(lateral[i]) * settings.unit_flow_lateral_bias) *
                speed * speed_scale[i];
        } else {
            decision.velocity = record.intent_velocity * speed_scale[i];
        }
        decision.mode = modes[i];
        decision.reason = reasons[i];
        decision.selected_conflicts = selected_count[i];
        decision.candidate_count = candidate_count[i];
        const auto root = find_root(i);
        auto [group_it, inserted] = groups.emplace(root, next_group);
        if (inserted) ++next_group;
        decision.conflict_group = selected_count[i] > 0 ? group_it->second : 0;
        decision.nearest_time_to_collision =
            settings.unit_flow_prediction_seconds;
        for (const auto& edge : index.selected)
            if (edge.a == i || edge.b == i) {
                decision.nearest_time_to_collision = std::min(
                    decision.nearest_time_to_collision,
                    edge.time_to_collision);
                if (state.peer == entt::null ||
                    edge.time_to_collision <=
                        decision.nearest_time_to_collision + Epsilon) {
                    const auto peer_index = edge.a == i ? edge.b : edge.a;
                    state.peer = index.records[peer_index].entity;
                    state.peer_instance_id =
                        index.records[peer_index].instance_id;
                }
            }
        if (selected_count[i] == 0) {
            state.peer = entt::null;
            state.peer_instance_id = 0;
            if (tick > state.last_conflict_tick +
                    settings.unit_flow_backing_cooldown_ticks) {
                state.backing_ticks = 0;
                state.backing_distance = 0.f;
            }
        }
        const float maximum_backing_distance =
            2.f * unit_flow_radius(record) *
            settings.unit_flow_backing_max_diameters;
        const bool backing_available =
            state.backing_ticks < settings.unit_flow_backing_max_ticks &&
            state.backing_distance + Epsilon < maximum_backing_distance;
        if (!backing_available && state.backing_ticks > 0 &&
            tick > state.last_backing_tick +
                settings.unit_flow_backing_cooldown_ticks) {
            state.backing_ticks = 0;
            state.backing_distance = 0.f;
        }
        const bool traffic_deadlock = selected_count[i] > 0 &&
            (decision.mode != AoeGlobalMotionMode::Clear ||
             state.mode != AoeGlobalMotionMode::Clear);
        if (traffic_deadlock &&
            state.wait_ticks >= settings.unit_flow_backing_threshold_ticks &&
            state.backing_ticks < settings.unit_flow_backing_max_ticks &&
            state.backing_distance + Epsilon < maximum_backing_distance) {
            const glm::vec2 backward = speed > Epsilon
                ? -record.intent_velocity / speed : glm::vec2{0.f};
            const float distance = 2.f * unit_flow_radius(record);
            if (!map || !map->valid() || map->static_safe_fraction(
                    record.position, record.position + backward * distance,
                    record.radii) >= 1.f - Epsilon) {
                decision.velocity = backward * speed *
                    settings.unit_flow_backing_speed_scale;
                decision.mode = AoeGlobalMotionMode::Backing;
                decision.reason = AoeMotionDecisionReason::DeadlockEscape;
                state.last_backing_tick = tick;
                ++diagnostics.flow_deadlock_escalations;
            }
        }
        decision.wait_ticks = state.wait_ticks;
        diagnostics.flow_wait_ticks += state.wait_ticks;
        decision.velocity = acceleration_limited(
            record.entity, decision.velocity);
        state.mode = decision.mode;
        state.negotiated_side = sides[i];
        state.last_conflict_tick = selected_count[i] > 0
            ? tick : state.last_conflict_tick;
        switch (decision.mode) {
        case AoeGlobalMotionMode::SideStep: ++diagnostics.flow_following; break;
        case AoeGlobalMotionMode::PassingLeft:
        case AoeGlobalMotionMode::PassingRight: ++diagnostics.flow_passing; break;
        case AoeGlobalMotionMode::Yielding: ++diagnostics.flow_yielding; break;
        case AoeGlobalMotionMode::Backing: ++diagnostics.flow_backing; break;
        case AoeGlobalMotionMode::Recovering: ++diagnostics.flow_recovering; break;
        case AoeGlobalMotionMode::Clear: break;
        }
    }

    // Project the complete conflict group's velocities onto the contact
    // constraints. Pair-policy lateral choices alone can cancel in a dense
    // group (one neighbor above and another below); the projection resolves
    // all current overlaps together before the safety-only clipping stage.
    std::vector<float> velocity_caps(index.records.size(), 0.f);
    for (std::size_t i = 0; i < index.records.size(); ++i)
        velocity_caps[i] = glm::length(acceleration_limited(
            index.records[i].entity, index.records[i].intent_velocity));
    const float recovery_seconds = std::max(
        fixed_dt, settings.unit_flow_overlap_recovery_seconds);
    for (std::uint32_t iteration = 0;
         iteration < std::max(1u, settings.unit_flow_solver_iterations);
         ++iteration) {
        for (const auto& edge : index.selected) {
            const auto& a = index.records[edge.a];
            const auto& b = index.records[edge.b];
            const glm::vec2 combined = a.radii + b.radii;
            if (!(combined.x > Epsilon) || !(combined.y > Epsilon))
                continue;
            const glm::vec2 relative = a.position - b.position;
            const glm::vec2 scaled{relative.x / combined.x,
                                   relative.y / combined.y};
            const float normalized_distance = glm::length(scaled);
            // Safety treats a pair on the contact boundary as blocked when
            // its relative velocity enters the other collider. Project those
            // shallow/contact cases too; skipping them leaves an approaching
            // velocity for safety to reduce to zero forever.
            if (normalized_distance > 1.f + Epsilon) continue;
            glm::vec2 normal{relative.x / (combined.x * combined.x),
                             relative.y / (combined.y * combined.y)};
            const float normal_length = glm::length(normal);
            if (!(normal_length > Epsilon)) {
                const auto parity = a.instance_id < b.instance_id ? 1.f : -1.f;
                normal = {parity, 0.f};
            } else {
                normal /= normal_length;
            }
            auto& decision_a = reg.get<AoeGlobalMotionDecision>(a.entity);
            auto& decision_b = reg.get<AoeGlobalMotionDecision>(b.entity);
            const float cap_a = velocity_caps[edge.a];
            const float cap_b = velocity_caps[edge.b];
            const float penetration = std::max(0.f,
                1.f - normalized_distance);
            const float separation_speed = std::min(cap_a + cap_b,
                penetration *
                    std::min(combined.x, combined.y) / recovery_seconds);
            const float current_separation = glm::dot(
                normal, decision_a.velocity - decision_b.velocity);
            if (current_separation + Epsilon >= separation_speed) continue;
            const float correction = separation_speed - current_separation;
            const float intent_a = glm::length(a.intent_velocity);
            const float intent_b = glm::length(b.intent_velocity);
            const bool same_speed = std::abs(intent_a - intent_b) <=
                std::max(settings.unit_flow_same_speed_absolute,
                    std::max(intent_a, intent_b) *
                        settings.unit_flow_same_speed_relative);
            if (same_speed) {
                decision_a.velocity += normal * (correction * .5f);
                decision_b.velocity -= normal * (correction * .5f);
            } else if (intent_a < intent_b) {
                decision_a.velocity += normal * correction;
            } else {
                decision_b.velocity -= normal * correction;
            }
            ++diagnostics.flow_overlap_projections;
        }
    }
    // Scale every member of a connected conflict group by the same factor.
    // Independent per-unit clamps can turn a separating relative velocity
    // back into an approaching one; a common factor preserves every projected
    // pair constraint while respecting the strictest member's speed cap.
    std::vector<float> group_scale(index.records.size(), 1.f);
    for (std::size_t i = 0; i < index.records.size(); ++i) {
        const float speed = glm::length(
            reg.get<AoeGlobalMotionDecision>(index.records[i].entity).velocity);
        if (speed <= velocity_caps[i] + Epsilon || !(speed > Epsilon))
            continue;
        const auto root = find_root(i);
        group_scale[root] = std::min(
            group_scale[root], velocity_caps[i] / speed);
    }
    for (std::size_t i = 0; i < index.records.size(); ++i)
        reg.get<AoeGlobalMotionDecision>(index.records[i].entity).velocity *=
            group_scale[find_root(i)];
}

void update_global_motion_wait_states(EcsWorld& world, std::uint64_t tick) {
    auto& reg = world.reg();
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    for (const auto entity : reg.view<AoeGlobalMotionState>()) {
        auto& state = reg.get<AoeGlobalMotionState>(entity);
        const auto* intent = reg.try_get<AoeMovementIntent>(entity);
        const bool active_intent = intent && intent->valid &&
            intent->produced_tick == tick &&
            glm::length(intent->velocity) > Epsilon;
        if (!active_intent) {
            state = AoeGlobalMotionState{};
            continue;
        }
        const auto* previous = reg.try_get<AoeGlobalMotionDecision>(entity);
        const bool contiguous = previous && previous->valid &&
            previous->produced_tick < tick && previous->produced_tick + 1u == tick;
        if (!contiguous) {
            state.wait_ticks = 0;
            continue;
        }
        const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
        const bool progressed = locomotion && locomotion->actual_speed >
            navigation.steering_stalled_speed;
        if (progressed) state.wait_ticks = 0;
        else if (state.wait_ticks < std::numeric_limits<std::uint32_t>::max())
            ++state.wait_ticks;
    }
}

void global_motion_planner_tick(EcsWorld& world, std::uint64_t tick) {
    // wait_ticks 统一表示“连续有意向但上一 tick 没有实际进展”。在 backend
    // 覆盖上一条 decision 前更新，CPU/GPU/fallback 因而共享完全相同的语义。
    update_global_motion_wait_states(world, tick);
    const auto& settings = world.resource_or_add<AoeNavigationSettings>();
    auto& registry = world.resource_or_add<AoeGlobalMotionPlannerRegistry>();
    auto& diagnostics =
        world.resource_or_add<AoeGlobalMotionPlannerDiagnostics>();
    diagnostics.requested_backend = settings.global_motion_planner_id;
    diagnostics.fallback_reason.clear();

    std::string failure;
    bool success = false;
    if (auto* planner = registry.find(settings.global_motion_planner_id)) {
        try {
            success = (*planner)(world, tick, failure);
        } catch (const std::exception& error) {
            failure = error.what();
        } catch (...) {
            failure = "unknown planner exception";
        }
    } else {
        failure = "requested backend is not registered";
    }

    if (success) {
        diagnostics.active_backend = settings.global_motion_planner_id;
        if (diagnostics.active_backend == "gpu_image") ++diagnostics.gpu_ticks;
        else ++diagnostics.cpu_ticks;
        return;
    }

    ++diagnostics.failures;
    ++diagnostics.fallback_ticks;
    diagnostics.fallback_reason = failure.empty()
        ? "requested backend returned failure" : std::move(failure);
    diagnostics.active_backend = "cpu_unit_flow";
    if (auto* cpu = registry.find("cpu_unit_flow")) {
        std::string ignored;
        if ((*cpu)(world, tick, ignored)) {
            ++diagnostics.cpu_ticks;
            return;
        }
    }
    // A malformed registry must not leave stale motion decisions behind.
    unit_flow_tick(world, tick);
    ++diagnostics.cpu_ticks;
}

} // namespace

void AoeDefaultGlobalMotionPlugin::install(App& app) {
    auto& planners =
        app.world.resource_or_add<AoeGlobalMotionPlannerRegistry>();
    if (!planners.contains("cpu_unit_flow"))
        planners.bind("cpu_unit_flow",
            [](EcsWorld& world, std::uint64_t tick, std::string&) {
                unit_flow_tick(world, tick);
                return true;
            });
    app.world.resource_or_add<AoeGlobalMotionPlannerDiagnostics>();
    app.world.resource_or_add<AoeUnitFlowIndex>();
}

void AoeDefaultGlobalMotionPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    global_motion_planner_tick(world, tick);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .unit_flow_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe

