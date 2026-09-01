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

    const auto find_active_segment = [&](std::uint64_t token) {
        return std::find_if(topology->active_segments.begin(),
            topology->active_segments.end(), [&](const auto& segment) {
                return segment.follow_token == token;
            });
    };
    const auto update_chain_progress = [&]() {
        std::vector<int> leader_segment_for_chain(
            topology->bindings.size(), -1);
        for (std::size_t index = 0;
             index < topology->active_segments.size(); ++index) {
            const auto& segment = topology->active_segments[index];
            if (segment.first_member != 0) continue;
            if (segment.natural_chain >= leader_segment_for_chain.size() ||
                leader_segment_for_chain[segment.natural_chain] >= 0)
                return false;
            leader_segment_for_chain[segment.natural_chain] =
                static_cast<int>(index);
        }
        // Retained roots and not-yet-attached columns measure their own path.
        for (std::size_t index = 0; index < topology->bindings.size(); ++index) {
            auto& binding = topology->bindings[index];
            if (leader_segment_for_chain[index] >= 0) continue;
            if (binding.root_chain != index || binding.attached ||
                binding.route_source.entity != route_sources[index].unit.entity ||
                binding.route_source.instance_id !=
                    route_sources[index].unit.instance_id)
                return false;
            const float measured = formation_detail::member_route_progress(
                *route_sources[index].metadata, route_sources[index].path,
                reg.get<AoePosition>(route_sources[index].unit.entity).value,
                trajectory->total_progress);
            if (!advance_binding(binding, measured)) return false;
        }
        // An overflow column's leader is itself a segment head. Its physical
        // progress follows the retained lane root while its dormant natural
        // route stays available for restoration.
        for (const auto& segment : topology->active_segments) {
            if (segment.first_member != 0) continue;
            if (segment.natural_chain >= topology->bindings.size() ||
                segment.root_chain >= topology->bindings.size() ||
                segment.natural_chain == segment.root_chain ||
                !std::isfinite(segment.base_distance) ||
                segment.base_distance < 0.f)
                return false;
            auto& binding = topology->bindings[segment.natural_chain];
            const auto& root = topology->bindings[segment.root_chain];
            if (!binding.attached ||
                binding.root_chain != segment.root_chain ||
                binding.active_follow_token != segment.follow_token ||
                binding.route_source.entity != segment.route_source.entity ||
                binding.route_source.instance_id !=
                    segment.route_source.instance_id ||
                std::abs(binding.base_distance - segment.base_distance) >
                    Epsilon ||
                root.attached || root.root_chain != segment.root_chain ||
                segment.route_source.entity != root.route_source.entity ||
                segment.route_source.instance_id !=
                    root.route_source.instance_id)
                return false;
            if (!advance_binding(binding,
                    root.progress - segment.base_distance))
                return false;
        }
        return true;
    };
    if (!update_chain_progress()) return fail();

    // Segment heads, not only natural leaders, can own action timelines.
    for (std::size_t chain_index = 0;
         chain_index < follow_plan->chains.size(); ++chain_index) {
        const auto& chain = follow_plan->chains[chain_index];
        for (std::size_t member_index = 0;
             member_index < chain.members.size(); ++member_index) {
            const auto& member = chain.members[member_index];
            auto* actions = reg.try_get<AoeUnitActionChain>(member.unit.entity);
            if (!actions) continue;
            if (!actions->valid || actions->squad != context.squad ||
                actions->order_revision != context.order.revision ||
                actions->unit_instance_id != member.unit.instance_id ||
                actions->natural_chain != chain_index ||
                actions->member_index != member_index)
                return fail();
            auto& chain_binding = topology->bindings[chain_index];
            std::size_t immediate_guard = actions->steps.size() + 1;
            while (actions->current < actions->steps.size() &&
                   immediate_guard--) {
                const auto& step = actions->steps[actions->current];
                if (chain_binding.progress + Epsilon < step.begin_progress) {
                    immediate_guard = 0;
                    continue;
                }
                switch (step.kind) {
                case AoeUnitActionStepKind::NavigationPath:
                    if (chain_binding.progress + Epsilon < step.end_progress)
                        immediate_guard = 0;
                    else
                        ++actions->current;
                    break;
                case AoeUnitActionStepKind::FormationFollow: {
                    const auto& attach = step.attach;
                    if (!step.follow_token ||
                        attach.natural_chain != chain_index ||
                        attach.first_member != member_index ||
                        !attach.member_count ||
                        attach.member_count > chain.members.size() - member_index ||
                        attach.root_chain >= topology->bindings.size() ||
                        attach.root_chain == chain_index ||
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
                    auto active = find_active_segment(step.follow_token);
                    if (actions->active_follow_token &&
                        actions->active_follow_token != step.follow_token)
                        return fail();
                    if (active == topology->active_segments.end()) {
                        topology->active_segments.push_back({
                            step.follow_token, attach.natural_chain,
                            attach.first_member, attach.member_count,
                            attach.root_chain, attach.route_source,
                            attach.preceding_tail, attach.base_distance,
                            attach.following_distance});
                        if (member_index == 0) {
                            chain_binding.root_chain = attach.root_chain;
                            chain_binding.route_source = attach.route_source;
                            chain_binding.preceding_tail = attach.preceding_tail;
                            chain_binding.base_distance = attach.base_distance;
                            chain_binding.active_follow_token = step.follow_token;
                            chain_binding.attached = true;
                        }
                    } else if (
                        active->natural_chain != attach.natural_chain ||
                        active->first_member != attach.first_member ||
                        active->member_count != attach.member_count ||
                        active->root_chain != attach.root_chain ||
                        active->route_source.entity !=
                            attach.route_source.entity ||
                        active->route_source.instance_id !=
                            attach.route_source.instance_id ||
                        active->preceding_tail.entity !=
                            attach.preceding_tail.entity ||
                        active->preceding_tail.instance_id !=
                            attach.preceding_tail.instance_id ||
                        std::abs(active->base_distance -
                            attach.base_distance) > Epsilon ||
                        std::abs(active->following_distance -
                            attach.following_distance) > Epsilon) {
                        return fail();
                    }
                    reg.emplace_or_replace<AoeFormationFollow>(member.unit.entity,
                        AoeFormationFollow{context.squad,
                            context.order.revision, attach.preceding_tail,
                            member.distance_from_leader,
                            attach.following_distance,
                            static_cast<std::uint32_t>(chain_index),
                            static_cast<std::uint32_t>(member_index), true,
                            step.follow_token});
                    actions->active_follow_token = step.follow_token;
                    if (chain_binding.progress + Epsilon < step.end_progress)
                        immediate_guard = 0;
                    else
                        ++actions->current;
                    break;
                }
                case AoeUnitActionStepKind::FormationDetachFollow: {
                    if (!step.follow_token ||
                        actions->active_follow_token != step.follow_token)
                        return fail();
                    auto active = find_active_segment(step.follow_token);
                    if (active == topology->active_segments.end() ||
                        active->natural_chain != chain_index ||
                        active->first_member != member_index)
                        return fail();
                    const auto* follow = reg.try_get<AoeFormationFollow>(
                        member.unit.entity);
                    if (!follow || !follow->temporary ||
                        follow->follow_token != step.follow_token ||
                        follow->squad != context.squad ||
                        follow->order_revision != context.order.revision)
                        return fail();
                    if (member_index == 0) {
                        reg.remove<AoeFormationFollow>(member.unit.entity);
                        formation_detail::advance_path_to_progress(
                            *route_sources[chain_index].path,
                            *route_sources[chain_index].metadata,
                            std::max(step.end_progress,
                                     chain_binding.progress));
                        const float progress = chain_binding.progress;
                        const std::uint64_t last_tick =
                            chain_binding.last_progress_tick;
                        chain_binding = AoeFormationFollowChainBinding{
                            static_cast<std::uint32_t>(chain_index),
                            static_cast<std::uint32_t>(chain_index),
                            route_sources[chain_index].unit, {}, 0.f,
                            progress, 0, last_tick, false};
                    } else {
                        const auto& previous = chain.members[member_index - 1];
                        reg.emplace_or_replace<AoeFormationFollow>(
                            member.unit.entity,
                            AoeFormationFollow{context.squad,
                                context.order.revision, previous.unit,
                                member.distance_from_leader,
                                member.distance_from_leader -
                                    previous.distance_from_leader,
                                static_cast<std::uint32_t>(chain_index),
                                static_cast<std::uint32_t>(member_index),
                                false, 0});
                    }
                    topology->active_segments.erase(active);
                    actions->active_follow_token = 0;
                    ++actions->current;
                    break;
                }
                }
            }
        }
    }
    if (!update_chain_progress()) return fail();
    topology->attached_segments = static_cast<std::uint32_t>(
        topology->active_segments.size());

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
    std::vector<std::vector<int>> active_segment_for_member;
    active_segment_for_member.reserve(follow_plan->chains.size());
    for (const auto& chain : follow_plan->chains)
        active_segment_for_member.emplace_back(chain.members.size(), -1);
    for (std::size_t segment_index = 0;
         segment_index < topology->active_segments.size(); ++segment_index) {
        const auto& segment = topology->active_segments[segment_index];
        if (segment.natural_chain >= follow_plan->chains.size() ||
            !segment.member_count ||
            segment.first_member >= active_segment_for_member[
                segment.natural_chain].size() ||
            segment.member_count > active_segment_for_member[
                segment.natural_chain].size() - segment.first_member)
            return fail();
        for (std::size_t member_index = segment.first_member;
             member_index < segment.first_member + segment.member_count;
             ++member_index) {
            auto& owner = active_segment_for_member[
                segment.natural_chain][member_index];
            if (owner >= 0) return fail();
            owner = static_cast<int>(segment_index);
        }
    }

    for (std::size_t chain_index = 0;
         chain_index < follow_plan->chains.size(); ++chain_index) {
        const auto& chain = follow_plan->chains[chain_index];

        for (std::size_t member_index = 0;
             member_index < chain.members.size(); ++member_index) {
            const auto& member = chain.members[member_index];
            const int active_index =
                active_segment_for_member[chain_index][member_index];
            const auto* active = active_index >= 0
                ? &topology->active_segments[
                    static_cast<std::size_t>(active_index)] : nullptr;
            const std::size_t root_index = active
                ? active->root_chain : chain_index;
            if (root_index >= route_sources.size()) return fail();
            const auto& root = topology->bindings[root_index];
            const auto& source = route_sources[root_index];
            if (!source.valid || root.attached ||
                root.root_chain != root_index ||
                source.unit.entity != root.route_source.entity ||
                source.unit.instance_id != root.route_source.instance_id ||
                (active && (active->route_source.entity !=
                    source.unit.entity || active->route_source.instance_id !=
                    source.unit.instance_id)))
                return fail();
            group_present[root_index] = true;
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
            const bool segment_head = active &&
                member_index == active->first_member;
            const bool should_follow = member_index != 0 || active;
            if (should_follow) {
                if (!follow_component ||
                    follow_component->squad != context.squad ||
                    follow_component->order_revision != context.order.revision ||
                    follow_component->natural_chain != chain_index ||
                    std::abs(follow_component->distance_from_chain_leader -
                        member.distance_from_leader) > Epsilon ||
                    (segment_head &&
                        (!follow_component->temporary ||
                         follow_component->follow_token !=
                            active->follow_token)) ||
                    (!segment_head && follow_component->temporary))
                    return fail();
                if (const auto* path = reg.try_get<AoeNavigationPath>(
                        member.unit.entity);
                    path && path->priority > follow_component->priority)
                    continue;
            } else if (follow_component && follow_component->temporary) {
                return fail();
            }

            const float distance = active
                ? active->base_distance + member.distance_from_leader -
                    chain.members[active->first_member].distance_from_leader
                : member.distance_from_leader;
            auto sample = formation_detail::sample_member_route(
                *source.metadata, *source.path, root.progress - distance);
            if (!sample.valid || !std::isfinite(sample.speed_ratio) ||
                sample.speed_ratio <= Epsilon)
                return fail();
            // Before a natural follower's historical route anchor reaches
            // the leader origin, do not use sample_member_route's first-segment
            // backward extrapolation. Attached segments remain on their shared
            // root route: projecting their effective distance onto this
            // initial-forward bootstrap can create an unvalidated straight
            // target across static obstacles.
            if (!active && member_index != 0 &&
                root.progress + Epsilon < distance) {
                const auto& start_frame = trajectory->frames.front();
                const float forward_length2 = glm::dot(start_frame.forward,
                    start_frame.forward);
                if (!std::isfinite(forward_length2) ||
                    forward_length2 <= Epsilon * Epsilon)
                    return fail();
                sample.forward = glm::normalize(start_frame.forward);
                sample.position = source.metadata->origin + sample.forward *
                    (root.progress - distance);
            }
            const auto& movement = reg.get<AoeMovement>(member.unit.entity);
            sustainable_speed[root_index] = std::min(
                sustainable_speed[root_index],
                movement.speed / sample.speed_ratio);
            if (chain_index == root_index && member_index == 0 && !active)
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
