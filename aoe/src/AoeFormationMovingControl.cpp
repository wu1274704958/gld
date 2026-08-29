#include <aoe/AoeFormation.hpp>
#include <aoe/AoeFormationFollow.hpp>
#include <aoe/AoeGameplay.hpp>

#include "AoeFormationRouteSampling.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <vector>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

bool formation_member_active(const entt::registry& reg, entt::entity entity) {
    if (!reg.valid(entity) ||
        !reg.all_of<AoePosition, AoeMovement, AoeActionState>(entity))
        return false;
    const auto state = reg.get<AoeActionState>(entity).state;
    return state != UnitState::Attacking && state != UnitState::Dying &&
        state != UnitState::Disappearing &&
        !reg.all_of<AoeAttackOrder>(entity);
}
} // namespace

// Materializes RouteSplit's Attach/Detach timeline, maintains one monotonic
// progress value per natural chain, synchronizes each currently connected
// Follow group at a sustainable speed, and publishes final follower requests.
AoeFormationModuleResult aoe_synchronized_follow_motion_system(
    EcsWorld& world, AoeFormationSquadContext& context) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    const auto* split =
        reg.try_get<AoeFormationRouteSplitState>(context.squad);
    const auto* trajectory =
        reg.try_get<AoeFormationRouteTrajectory>(context.squad);
    const auto* follow_plan =
        reg.try_get<AoeFormationFollowPlan>(context.squad);
    auto* topology =
        reg.try_get<AoeFormationFollowTopology>(context.squad);
    const bool moving_order =
        context.order.type == AoeSquadOrderType::MoveTo ||
        context.order.type == AoeSquadOrderType::AttackMove;
    if (!moving_order) {
        reg.remove<AoeFormationMovingState>(context.squad);
        return AoeFormationModuleResult::Continue;
    }

    auto& moving = reg.get_or_emplace<AoeFormationMovingState>(context.squad);
    const auto fail = [&]() {
        if (follow_plan) {
            for (const auto& chain : follow_plan->chains) {
                for (const auto& member : chain.members) {
                    if (!reg.valid(member.unit.entity)) continue;
                    reg.remove<AoeSquadMoveSpeedLimit>(member.unit.entity);
                    if (auto* request = reg.try_get<AoePathMotionRequest>(
                            member.unit.entity))
                        request->valid = false;
                }
            }
        }
        moving.status = AoeFormationMovingStatus::Failed;
        moving.order_revision = context.order.revision;
        moving.active_members = 0;
        context.state.movement_speed = 0.f;
        return AoeFormationModuleResult::Continue;
    };

    if (!split || !trajectory || !trajectory->valid || !follow_plan ||
        !follow_plan->valid || !topology || !topology->valid ||
        split->status != AoeFormationRouteSplitStatus::Ready ||
        split->order_revision != context.order.revision ||
        trajectory->order_revision != context.order.revision ||
        follow_plan->order_revision != context.order.revision ||
        topology->order_revision != context.order.revision ||
        topology->layout_revision != follow_plan->layout_revision ||
        topology->bindings.size() != follow_plan->chains.size())
        return fail();

    struct RouteSourceRuntime {
        AoeUnitTarget unit{};
        AoeNavigationPath* path = nullptr;
        const AoeFormationMemberRouteProgress* metadata = nullptr;
        bool valid = false;
    };
    std::vector<RouteSourceRuntime> route_sources(follow_plan->chains.size());
    for (std::size_t index = 0; index < follow_plan->chains.size(); ++index) {
        const auto& chain = follow_plan->chains[index];
        auto& binding = topology->bindings[index];
        if (chain.members.empty() || binding.natural_chain != index)
            return fail();
        const auto leader = chain.members.front().unit;
        auto& source = route_sources[index];
        source.unit = leader;
        if (!detail::aoe_gameplay_squad_member_valid(reg, leader) ||
            !reg.all_of<AoePosition, AoeMovement>(leader.entity))
            return fail();
        source.path = reg.try_get<AoeNavigationPath>(leader.entity);
        source.metadata = reg.try_get<AoeFormationMemberRouteProgress>(
            leader.entity);
        const auto* owner = reg.try_get<AoeFormationRouteOwner>(leader.entity);
        if (!source.path || !source.metadata || !owner ||
            owner->squad != context.squad ||
            owner->squad_order_revision != context.order.revision ||
            owner->unit_instance_id != leader.instance_id ||
            source.metadata->squad != context.squad ||
            source.metadata->squad_order_revision != context.order.revision ||
            source.metadata->unit_instance_id != leader.instance_id)
            return fail();
        source.valid = true;
    }

    const auto advance_binding = [&](AoeFormationFollowChainBinding& binding,
                                     float measured) {
        if (!std::isfinite(measured)) return false;
        measured = std::clamp(measured, 0.f, trajectory->total_progress);
        const float next = std::max(binding.progress, measured);
        if (next > binding.progress + Epsilon)
            binding.last_progress_tick = context.tick;
        binding.progress = next;
        return true;
    };

    for (std::size_t index = 0; index < topology->bindings.size(); ++index) {
        auto& binding = topology->bindings[index];
        if (binding.attached) continue;
        if (binding.root_chain != index ||
            binding.route_source.entity != route_sources[index].unit.entity ||
            binding.route_source.instance_id !=
                route_sources[index].unit.instance_id)
            return fail();
        const float measured = formation_detail::member_route_progress(
            *route_sources[index].metadata, route_sources[index].path,
            reg.get<AoePosition>(route_sources[index].unit.entity).value,
            trajectory->total_progress);
        if (!advance_binding(binding, measured)) return fail();
    }
    const auto update_attached_progress = [&]() {
        for (std::size_t index = 0; index < topology->bindings.size(); ++index) {
            auto& binding = topology->bindings[index];
            if (!binding.attached) continue;
            if (binding.root_chain >= topology->bindings.size() ||
                binding.root_chain == index ||
                !std::isfinite(binding.base_distance) ||
                binding.base_distance < 0.f)
                return false;
            const auto& root = topology->bindings[binding.root_chain];
            if (root.attached || root.root_chain != binding.root_chain ||
                binding.route_source.entity != root.route_source.entity ||
                binding.route_source.instance_id !=
                    root.route_source.instance_id)
                return false;
            if (!advance_binding(binding,
                    root.progress - binding.base_distance))
                return false;
        }
        return true;
    };
    if (!update_attached_progress()) return fail();

    for (std::size_t index = 0; index < follow_plan->chains.size(); ++index) {
        const auto& chain = follow_plan->chains[index];
        const auto leader = chain.members.front().unit;
        auto& binding = topology->bindings[index];
        auto* actions = reg.try_get<AoeUnitActionChain>(leader.entity);
        auto* path = route_sources[index].path;
        const auto* metadata = route_sources[index].metadata;
        if (!actions || !actions->valid ||
            actions->squad != context.squad ||
            actions->order_revision != context.order.revision ||
            actions->unit_instance_id != leader.instance_id)
            return fail();

        std::size_t immediate_guard = actions->steps.size() + 1;
        while (actions->current < actions->steps.size() && immediate_guard--) {
            const auto& step = actions->steps[actions->current];
            switch (step.kind) {
            case AoeUnitActionStepKind::NavigationPath:
                if (binding.progress + Epsilon < step.end_progress)
                    immediate_guard = 0;
                else
                    ++actions->current;
                break;
            case AoeUnitActionStepKind::FormationFollow: {
                const auto& attach = step.attach;
                if (!step.follow_token ||
                    attach.root_chain >= topology->bindings.size() ||
                    attach.root_chain == index ||
                    !std::isfinite(attach.base_distance) ||
                    attach.base_distance < 0.f ||
                    !std::isfinite(attach.following_distance) ||
                    attach.following_distance < 0.f)
                    return fail();
                const auto& root = topology->bindings[attach.root_chain];
                if (root.attached || root.root_chain != attach.root_chain ||
                    attach.route_source.entity != root.route_source.entity ||
                    attach.route_source.instance_id !=
                        root.route_source.instance_id)
                    return fail();
                if (binding.attached &&
                    binding.active_follow_token != step.follow_token)
                    return fail();
                if (!binding.attached) {
                    binding.root_chain = attach.root_chain;
                    binding.route_source = attach.route_source;
                    binding.preceding_tail = attach.preceding_tail;
                    binding.base_distance = attach.base_distance;
                    binding.active_follow_token = step.follow_token;
                    binding.attached = true;
                }
                reg.emplace_or_replace<AoeFormationFollow>(leader.entity,
                    AoeFormationFollow{context.squad,
                        context.order.revision, attach.preceding_tail, 0.f,
                        attach.following_distance,
                        static_cast<std::uint32_t>(index), 0, true,
                        step.follow_token});
                actions->active_follow_token = step.follow_token;
                if (binding.progress + Epsilon < step.end_progress)
                    immediate_guard = 0;
                else
                    ++actions->current;
                break;
            }
            case AoeUnitActionStepKind::FormationDetachFollow: {
                if (!step.follow_token || !binding.attached ||
                    binding.active_follow_token != step.follow_token ||
                    actions->active_follow_token != step.follow_token)
                    return fail();
                const auto* follow = reg.try_get<AoeFormationFollow>(
                    leader.entity);
                if (!follow || !follow->temporary ||
                    follow->follow_token != step.follow_token ||
                    follow->squad != context.squad ||
                    follow->order_revision != context.order.revision)
                    return fail();
                reg.remove<AoeFormationFollow>(leader.entity);
                formation_detail::advance_path_to_progress(
                    *path, *metadata,
                    std::max(step.end_progress, binding.progress));
                const float progress = binding.progress;
                const std::uint64_t last_tick = binding.last_progress_tick;
                binding = AoeFormationFollowChainBinding{
                    static_cast<std::uint32_t>(index),
                    static_cast<std::uint32_t>(index), leader, {}, 0.f,
                    progress, 0, last_tick, false};
                actions->active_follow_token = 0;
                ++actions->current;
                break;
            }
            }
        }
    }
    if (!update_attached_progress()) return fail();
    topology->attached_chains = static_cast<std::uint32_t>(std::count_if(
        topology->bindings.begin(), topology->bindings.end(),
        [](const auto& binding) { return binding.attached; }));

    struct MemberRuntime {
        AoeUnitTarget unit{};
        std::size_t root = 0;
        formation_detail::MemberRouteSample sample{};
        AoeFormationFollow follow{};
        bool follows = false;
    };
    std::vector<MemberRuntime> members;
    members.reserve(context.members.active.size());
    std::vector<float> sustainable_speed(follow_plan->chains.size(),
        std::numeric_limits<float>::infinity());
    std::vector<float> root_speed_ratio(follow_plan->chains.size(), 0.f);
    std::vector<bool> group_present(follow_plan->chains.size(), false);
    std::uint64_t followers_seen = 0;

    for (std::size_t chain_index = 0;
         chain_index < follow_plan->chains.size(); ++chain_index) {
        const auto& chain = follow_plan->chains[chain_index];
        const auto& binding = topology->bindings[chain_index];
        if (binding.root_chain >= route_sources.size()) return fail();
        const std::size_t root_index = binding.root_chain;
        const auto& root = topology->bindings[root_index];
        const auto& source = route_sources[root_index];
        if (!source.valid || root.attached || root.root_chain != root_index ||
            source.unit.entity != binding.route_source.entity ||
            source.unit.instance_id != binding.route_source.instance_id)
            return fail();
        group_present[root_index] = true;

        for (std::size_t member_index = 0;
             member_index < chain.members.size(); ++member_index) {
            const auto& member = chain.members[member_index];
            if (!detail::aoe_gameplay_squad_member_valid(reg, member.unit))
                continue;
            if (!formation_member_active(reg, member.unit.entity)) {
                if (auto* request = reg.try_get<AoePathMotionRequest>(
                        member.unit.entity))
                    request->valid = false;
                reg.remove<AoeSquadMoveSpeedLimit>(member.unit.entity);
                continue;
            }

            const auto* follow_component =
                reg.try_get<AoeFormationFollow>(member.unit.entity);
            const bool should_follow = member_index != 0 || binding.attached;
            if (should_follow) {
                if (!follow_component ||
                    follow_component->squad != context.squad ||
                    follow_component->order_revision != context.order.revision ||
                    follow_component->natural_chain != chain_index ||
                    std::abs(follow_component->distance_from_chain_leader -
                        member.distance_from_leader) > Epsilon ||
                    (member_index == 0 &&
                        (!follow_component->temporary ||
                         follow_component->follow_token !=
                            binding.active_follow_token)) ||
                    (member_index != 0 && follow_component->temporary))
                    return fail();
                if (const auto* path = reg.try_get<AoeNavigationPath>(
                        member.unit.entity);
                    path && path->priority > follow_component->priority)
                    continue;
            } else if (follow_component && follow_component->temporary) {
                return fail();
            }

            const float distance = binding.base_distance +
                member.distance_from_leader;
            auto sample = formation_detail::sample_member_route(
                *source.metadata, *source.path, root.progress - distance);
            if (!sample.valid || !std::isfinite(sample.speed_ratio) ||
                sample.speed_ratio <= Epsilon)
                return fail();
            const auto& movement = reg.get<AoeMovement>(member.unit.entity);
            sustainable_speed[root_index] = std::min(
                sustainable_speed[root_index],
                movement.speed / sample.speed_ratio);
            if (chain_index == root_index && member_index == 0)
                root_speed_ratio[root_index] = sample.speed_ratio;

            MemberRuntime runtime;
            runtime.unit = member.unit;
            runtime.root = root_index;
            runtime.sample = sample;
            runtime.follows = should_follow;
            if (follow_component) runtime.follow = *follow_component;
            members.push_back(std::move(runtime));
            if (should_follow) ++followers_seen;
        }
    }

    float slowest = trajectory->total_progress;
    float fastest = 0.f;
    bool have_group = false;
    for (std::size_t index = 0; index < group_present.size(); ++index) {
        if (!group_present[index]) continue;
        const auto& binding = topology->bindings[index];
        if (binding.attached || binding.root_chain != index) continue;
        slowest = std::min(slowest, binding.progress);
        fastest = std::max(fastest, binding.progress);
        have_group = true;
    }
    if (!have_group) return fail();

    const auto& settings = world.resource_or_add<AoeFormationMovingSettings>();
    std::vector<float> commanded_speed(follow_plan->chains.size(), 0.f);
    float squad_speed = std::numeric_limits<float>::infinity();
    std::uint32_t active_members = 0;
    for (std::size_t index = 0; index < group_present.size(); ++index) {
        if (!group_present[index]) continue;
        const auto& binding = topology->bindings[index];
        if (binding.attached || binding.root_chain != index) continue;
        const float base_speed = std::isfinite(sustainable_speed[index])
            ? sustainable_speed[index] : 0.f;
        const float lead = std::max(0.f, binding.progress - slowest);
        const float lead_scale = settings.allowed_progress_lead >
                settings.progress_epsilon
            ? std::clamp(1.f - lead / settings.allowed_progress_lead,
                         0.f, 1.f)
            : (lead <= settings.progress_epsilon ? 1.f : 0.f);
        const bool route_complete =
            binding.progress + settings.progress_epsilon >=
                trajectory->total_progress;
        commanded_speed[index] = route_complete
            ? 0.f : base_speed * lead_scale;

        const auto& source = route_sources[index];
        const bool active = formation_member_active(reg, source.unit.entity) &&
            !source.path->no_path &&
            source.path->current < source.path->waypoints.size();
        if (!active) {
            reg.remove<AoeSquadMoveSpeedLimit>(source.unit.entity);
            continue;
        }
        if (!(root_speed_ratio[index] > Epsilon)) return fail();
        reg.emplace_or_replace<AoeMoveGoal>(source.unit.entity,
            AoeMoveGoal{source.path->requested_goal, 0.f, {}});
        reg.emplace_or_replace<AoeFormationMoveGoalOwner>(source.unit.entity,
            AoeFormationMoveGoalOwner{context.squad,
                context.order.revision, source.unit.instance_id});
        reg.emplace_or_replace<AoeSquadMoveSpeedLimit>(source.unit.entity,
            AoeSquadMoveSpeedLimit{
                commanded_speed[index] * root_speed_ratio[index]});
        squad_speed = std::min(squad_speed, commanded_speed[index]);
        ++active_members;
    }
    if (!std::isfinite(squad_speed)) squad_speed = 0.f;

    const float fixed_dt = static_cast<float>(
        world.resource<AoeGameplaySettings>().fixed_dt);
    std::uint32_t active_followers = 0;
    for (auto& member : members) {
        if (!member.follows) continue;
        if (auto* request = reg.try_get<AoePathMotionRequest>(
                member.unit.entity))
            request->valid = false;
        reg.remove<AoeSquadMoveSpeedLimit>(member.unit.entity);

        auto sample = member.sample;
        const auto& follow = member.follow;
        if (follow.temporary) {
            if (!detail::aoe_gameplay_squad_member_valid(reg, follow.target) ||
                !reg.all_of<AoePosition>(follow.target.entity))
                return fail();
            glm::vec2 target_forward = sample.forward;
            if (const auto* direction = reg.try_get<AoeDirection>(
                    follow.target.entity);
                direction && glm::dot(direction->value, direction->value) >
                    Epsilon * Epsilon)
                target_forward = glm::normalize(direction->value);
            sample.forward = target_forward;
            sample.position = reg.get<AoePosition>(follow.target.entity).value -
                target_forward * follow.following_distance;
        }

        const glm::vec2 position =
            reg.get<AoePosition>(member.unit.entity).value;
        const glm::vec2 error = sample.position - position;
        const glm::vec2 right{sample.forward.y, -sample.forward.x};
        const float longitudinal_error = glm::dot(error, sample.forward);
        const float lateral_error = glm::dot(error, right);
        const float distance = glm::length(error);
        const auto& movement = reg.get<AoeMovement>(member.unit.entity);
        const float cruise_speed = commanded_speed[member.root] > Epsilon
            ? std::min(movement.speed,
                commanded_speed[member.root] * sample.speed_ratio)
            : 0.f;
        float max_speed = cruise_speed;

        float spacing_error = 0.f;
        if (follow.target.entity != entt::null &&
            detail::aoe_gameplay_squad_member_valid(reg, follow.target) &&
            reg.all_of<AoePosition>(follow.target.entity)) {
            const float gap = glm::length(
                reg.get<AoePosition>(follow.target.entity).value - position);
            spacing_error = gap - follow.following_distance;
        }
        const float behind = std::max(0.f, longitudinal_error);
        const bool settling_at_destination = !follow.temporary &&
            topology->bindings[member.root].progress +
                settings.progress_epsilon >= trajectory->total_progress;
        if (behind > settings.progress_epsilon ||
            spacing_error > settings.spacing_tolerance)
            max_speed = std::max(max_speed,
                movement.speed *
                    std::max(1.f, movement.catch_up_speed_ratio));
        else if (std::abs(lateral_error) > settings.spacing_tolerance)
            max_speed = std::max(max_speed, movement.speed);
        if (settling_at_destination &&
            distance > settings.progress_epsilon)
            max_speed = std::max(max_speed, movement.speed);

        float forward_speed = cruise_speed +
            (longitudinal_error + spacing_error) * settings.spacing_gain;
        if (follow.temporary && longitudinal_error <= 0.f)
            forward_speed = 0.f;
        forward_speed = std::clamp(forward_speed,
            settling_at_destination ? -max_speed : 0.f, max_speed);
        const float lateral_speed = std::clamp(
            lateral_error * settings.spacing_gain, -max_speed, max_speed);
        glm::vec2 desired_velocity =
            sample.forward * forward_speed + right * lateral_speed;
        if (const float desired_length = glm::length(desired_velocity);
            desired_length > max_speed && desired_length > Epsilon)
            desired_velocity *= max_speed / desired_length;

        auto& continuity = reg.get_or_emplace<AoeUnitMovementIntentState>(
            member.unit.entity);
        glm::vec2 previous{0.f};
        if (continuity.produced_tick + 1u == context.tick &&
            glm::length(continuity.velocity) > Epsilon)
            previous = continuity.velocity;
        else if (const auto* locomotion = reg.try_get<AoeLocomotionState>(
                     member.unit.entity))
            previous = locomotion->velocity;
        glm::vec2 velocity = aoe_constrain_unit_velocity_cached(
            continuity, previous, desired_velocity, max_speed,
            movement.rotation_speed_radians_per_second, fixed_dt);
        const float backward_velocity = glm::dot(velocity, sample.forward);
        if (backward_velocity < 0.f && !settling_at_destination)
            velocity -= sample.forward * backward_velocity;
        if (glm::length(velocity) > Epsilon)
            reg.get_or_emplace<AoeDirection>(member.unit.entity).value =
                glm::normalize(velocity);
        continuity.velocity = velocity;
        continuity.produced_tick = context.tick;
        continuity.valid = true;
        const auto& source = route_sources[member.root];
        reg.emplace_or_replace<AoePathMotionRequest>(member.unit.entity,
            AoePathMotionRequest{AoeMovementIntentKind::FormationSlot,
                velocity, sample.position, max_speed,
                source.path->request_sequence, context.tick, true});
        if (distance > .01f) ++active_followers;
    }

    const auto frame = formation_detail::sample_route_frame(*trajectory,
                                                            slowest);
    context.center.value = frame.center;
    context.formation.forward = frame.forward;
    context.state.movement_speed = squad_speed;
    active_members += active_followers;
    if (active_members > 0) context.state.phase = AoeSquadPhase::Moving;
    moving.status = active_members > 0
        ? AoeFormationMovingStatus::Moving
        : AoeFormationMovingStatus::Arrived;
    moving.order_revision = context.order.revision;
    moving.shared_progress = slowest;
    moving.slowest_progress = slowest;
    moving.maximum_lead = std::max(0.f, fastest - slowest);
    moving.active_members = active_members;

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    auto& diagnostics =
        world.resource_or_add<AoeFormationMovingDiagnostics>();
    ++diagnostics.updates;
    diagnostics.members_synchronized += members.size() + followers_seen;
    diagnostics.total_ms += elapsed;
    diagnostics.max_ms = std::max(diagnostics.max_ms, elapsed);
#endif
    return AoeFormationModuleResult::Continue;
}

} // namespace gld::ecs::aoe
