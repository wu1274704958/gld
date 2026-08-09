#include <aoe/AoeGameplay.hpp>

#include <array>
#include <chrono>
#include <limits>
#include <span>
#include <vector>

namespace gld::ecs::aoe {
namespace {

constexpr float Epsilon = 1e-5f;

bool target_within_squad_radius(
    const entt::registry& reg, const AoeSquadMembers& members,
    const AoeUnitTarget& target, float radius) {
    if (!detail::aoe_gameplay_target_valid(reg, target) ||
        !reg.all_of<AoePosition, AoeCollider>(target.entity))
        return false;
    for (const auto& member : members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member) ||
            !reg.all_of<AoePosition, AoeCollider>(member.entity))
            continue;
        if (aoe_surface_gap(
                reg.get<AoePosition>(member.entity),
                reg.get<AoeCollider>(member.entity),
                reg.get<AoePosition>(target.entity),
                reg.get<AoeCollider>(target.entity)) <= radius + Epsilon)
            return true;
    }
    return false;
}

std::vector<AoeUnitTarget> collect_squad_targets(
    EcsWorld& world, const AoeSquadMembers& members,
    std::uint32_t seeker_team, float radius) {
    auto& reg = world.reg();
    std::vector<AoeUnitTarget> result;
    glm::vec2 low{std::numeric_limits<float>::infinity()};
    glm::vec2 high{-std::numeric_limits<float>::infinity()};
    bool have_member = false;
    for (const auto& member : members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member) ||
            !reg.all_of<AoePosition, AoeCollider>(member.entity))
            continue;
        const auto& position = reg.get<AoePosition>(member.entity);
        const auto& collider = reg.get<AoeCollider>(member.entity);
        const glm::vec2 radii{collider.radius_x, collider.radius_y};
        low = glm::min(low, position.value - radii);
        high = glm::max(high, position.value + radii);
        have_member = true;
    }
    if (!have_member) return result;

    const auto consider = [&](entt::entity entity,
                              std::uint64_t instance_id,
                              bool already_bounded) {
        if (!reg.valid(entity) ||
            !reg.all_of<AoeTeam, AoePosition, AoeCollider, AoeHealth,
                        AoeActionState, AoeGameplayIdentity,
                        AoeUnitDefinitionRef>(entity) ||
            reg.get<AoeTeam>(entity).id == seeker_team)
            return;
        const AoeUnitTarget target{entity, instance_id};
        if (!detail::aoe_gameplay_target_valid(reg, target)) return;
        if (!already_bounded &&
            !target_within_squad_radius(reg, members, target, radius))
            return;
        result.push_back(target);
    };

    const auto* map = world.try_resource<AoeLogicMap>();
    const auto* dynamic = world.try_resource<AoeDynamicObstacleIndex>();
    if (map && map->valid() && dynamic) {
        dynamic->query(*map, low - glm::vec2(radius),
                       high + glm::vec2(radius),
            [&](const AoeDynamicObstacleEntry& entry) {
                consider(entry.entity, entry.instance_id, true);
            });
    } else {
        for (const auto entity : reg.view<
                 AoeTeam, AoePosition, AoeCollider, AoeHealth,
                 AoeActionState, AoeGameplayIdentity, AoeUnitDefinitionRef>(
                 entt::exclude<AoePooledUnit, AoeRecyclePending>))
            consider(entity,
                reg.get<AoeGameplayIdentity>(entity).instance_id, false);
    }
    return result;
}

std::optional<AoeUnitTarget> select_member_target(
    EcsWorld& world, entt::entity entity,
    AoeTargetAcquisitionType acquisition,
    std::span<const AoeUnitTarget> candidates,
    std::span<const AoeUnitTarget> excluded = {}) {
    auto& reg = world.reg();
    const auto* reference = reg.try_get<AoeUnitDefinitionRef>(entity);
    const auto* definition = reference ? reference->value.get() : nullptr;
    if (!definition || !definition->attack ||
        !reg.all_of<AoePosition, AoeTeam>(entity))
        return std::nullopt;
    auto target = dispatch_aoe_target(acquisition, world,
        AoeTargetAcquisitionContext{
            .seeker = entity,
            .origin = reg.get<AoePosition>(entity).value,
            .seeker_team = reg.get<AoeTeam>(entity).id,
            .excluded = excluded,
            .candidates = candidates,
            .use_candidates = true});
    if (!target || !detail::aoe_gameplay_target_valid(reg, *target))
        return std::nullopt;
    return target;
}

std::uint32_t update_squad_attack_move_engagement(
    EcsWorld& world, entt::entity squad, std::uint64_t tick) {
    auto& reg = world.reg();
    auto& members = reg.get<AoeSquadMembers>(squad);
    const auto& settings = reg.get<AoeSquadCombatSettings>(squad);
    const auto& navigation = world.resource_or_add<AoeNavigationSettings>();
    const auto candidates = collect_squad_targets(
        world, members, reg.get<AoeTeam>(squad).id,
        settings.acquisition_radius);
    std::uint32_t active_members = 0;
    for (const auto& member : members.active) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, member)) continue;
        if (const auto* current = reg.try_get<AoeAttackOrder>(member.entity);
            current && detail::aoe_gameplay_target_valid(
                           reg, current->target)) {
            const auto& action = reg.get<AoeActionState>(member.entity);
            const auto* definition = reg.get<AoeUnitDefinitionRef>(
                member.entity).value.get();
            if (definition && definition->attack &&
                (action.state == UnitState::Attacking ||
                 target_within_squad_radius(
                     reg, members, current->target,
                     settings.disengage_radius))) {
                auto* approach = reg.try_get<AoeEngagementApproach>(
                    member.entity);
                if (!approach ||
                    approach->target.entity != current->target.entity ||
                    approach->target.instance_id !=
                        current->target.instance_id) {
                    detail::aoe_gameplay_assign_engagement_approach(
                        reg, member.entity, current->target,
                        *definition->attack);
                    approach = reg.try_get<AoeEngagementApproach>(
                        member.entity);
                }
                if (action.state != UnitState::Attacking) {
                    const auto nearby =
                        detail::aoe_gameplay_select_stalled_in_range_target(
                            world, member.entity, current->target,
                            settings.acquisition_strategy,
                            definition->attack->range);
                    if (nearby) {
                        detail::aoe_gameplay_clear_active_engagement(
                            reg, member.entity);
                        detail::aoe_gameplay_attack_with_squad_member(
                            reg, member.entity, *nearby, tick);
                        ++active_members;
                        continue;
                    }
                }
                const auto* path = reg.try_get<AoeNavigationPath>(
                    member.entity);
                const bool unreachable =
                    action.state != UnitState::Attacking && path &&
                    path->no_path;
                if (approach) {
                    if (unreachable && approach->unreachable_ticks <
                                           std::numeric_limits<
                                               std::uint32_t>::max())
                        ++approach->unreachable_ticks;
                    else if (!unreachable)
                        approach->unreachable_ticks = 0;
                }
                const bool replace_unreachable = approach && unreachable &&
                    approach->unreachable_ticks >=
                        std::max(1u, navigation.blocked_repath_ticks);
                if (replace_unreachable) {
                    const std::array excluded{current->target};
                    auto replacement = select_member_target(
                        world, member.entity,
                        settings.acquisition_strategy, candidates, excluded);
                    if (replacement) {
                        detail::aoe_gameplay_clear_active_engagement(
                            reg, member.entity);
                        detail::aoe_gameplay_attack_with_squad_member(
                            reg, member.entity, *replacement, tick);
                    }
                }
                ++active_members;
                continue;
            }
        }

        // Only an existing member attack order owns a combat approach path.
        // A no-target scan for a member still following its formation slot must
        // not erase that slot movement every fixed tick.
        if (reg.all_of<AoeAttackOrder>(member.entity))
            detail::aoe_gameplay_clear_active_engagement(
                reg, member.entity);
        auto target = select_member_target(
            world, member.entity, settings.acquisition_strategy, candidates);
        if (!target ||
            !detail::aoe_gameplay_target_valid(reg, *target)) {
            if (reg.get<AoeActionState>(member.entity).state ==
                UnitState::Attacking)
                detail::aoe_gameplay_reset_member_action(
                    reg, member.entity, tick, false);
            continue;
        }
        detail::aoe_gameplay_attack_with_squad_member(
            reg, member.entity, *target, tick);
        ++active_members;
    }
    return active_members;
}

} // namespace

void AoeFullSquadEngagementPlugin::install(App&) {}

void AoeFullSquadEngagementPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    for (const auto squad : reg.view<AoeSquadEngagementResult>())
        reg.get<AoeSquadEngagementResult>(squad).valid = false;

    for (const auto squad : reg.view<
             AoeSquadMembers, AoeSquadSpawnState, AoeSquadCombatSettings,
             AoeSquadOrder, AoeSquadState, AoeTeam>()) {
        const auto& spawn = reg.get<AoeSquadSpawnState>(squad);
        const auto& order = reg.get<AoeSquadOrder>(squad);
        if ((spawn.status != AoeSquadSpawnStatus::Ready &&
             spawn.status != AoeSquadSpawnStatus::Partial) ||
            order.type != AoeSquadOrderType::AttackMove)
            continue;

        const auto active_members =
            update_squad_attack_move_engagement(world, squad, tick);
        reg.emplace_or_replace<AoeSquadEngagementResult>(squad,
            AoeSquadEngagementResult{
                active_members > 0
                    ? AoeSquadEngagementStatus::Active
                    : AoeSquadEngagementStatus::Inactive,
                active_members, tick, true});
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .squad_engagement_ms += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe
