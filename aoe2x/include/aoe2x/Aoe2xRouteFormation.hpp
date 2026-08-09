#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <aoe2x/Aoe2xFormation.hpp>

namespace gld::ecs::aoe2x {

// Route-splitting formations reuse the public spawn/order envelope but own no
// captain or follow chain. Ideal offsets remain stable even when map-aware
// spawn moves an individual unit away from its requested slot.
struct RouteSquadMetadata {
    std::vector<glm::vec2> ideal_offsets;
    float cell_size = 0.f;
    glm::vec2 spawn_forward{1.f, 0.f};
};

struct RouteSquadMemberInfo {
    entt::entity squad{entt::null};
    std::uint32_t slot_index = 0;
};

struct RouteSquadRouteAssignment {
    entt::entity squad{entt::null};
    std::uint64_t revision = 0;
    std::uint32_t final_columns = 0;
};

enum class RouteSquadPlanningPhase : std::uint8_t {
    PathPending,
    Splitting
};

struct RouteSquadPlanningState {
    RouteSquadPlanningPhase phase = RouteSquadPlanningPhase::PathPending;
    std::uint64_t revision = 0;
};

// Alternative Prepare pipeline. Do not register together with
// SpawnFormationSystem in one app: both consume FormationSpawnRequest.
struct RouteSquadSpawnSystem {
    using ReadOnlyComponents = Aoe2xComponentList<>;
    using WriteOnlyComponents = Aoe2xComponentList<
        SquadInfo, RouteSquadMetadata, RouteSquadMemberInfo,
        FormationAttackMove, aoe::AoePositionHistory, aoe::AoeCollider,
        aoe::AoeMovement, aoe::AoeLocomotionState, aoe::AoeDirection>;
    using ReadWriteComponents = Aoe2xComponentList<
        FormationSpawnRequest, FormationSpawnState, aoe::AoePosition>;
    static constexpr std::string_view name = "aoe2x_route_squad_spawn";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Prepare;
    static void run(EcsWorld&, std::uint64_t);
};

// Alternative Command pipeline. It consumes the shared FormationCommands
// queue and is therefore mutually exclusive with FormationCommandSystem.
struct RouteSquadCommandSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        SquadInfo, RouteSquadMetadata, RouteSquadMemberInfo>;
    using WriteOnlyComponents = Aoe2xComponentList<
        Aoe2xNavigationDestination, RouteSquadPlanningState>;
    using ReadWriteComponents = Aoe2xComponentList<
        FormationAttackMove, aoe::AoePosition, aoe::AoeCollider,
        Aoe2xRoutePlan, RouteSquadRouteAssignment>;
    static constexpr std::string_view name = "aoe2x_route_squad_command";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Command;
    static void run(EcsWorld&, std::uint64_t);
};

struct RouteSquadSplitSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        SquadInfo, RouteSquadMetadata, RouteSquadMemberInfo,
        aoe::AoePosition, aoe::AoeCollider>;
    using WriteOnlyComponents = Aoe2xComponentList<RouteSquadRouteAssignment>;
    using ReadWriteComponents = Aoe2xComponentList<
        FormationAttackMove, RouteSquadPlanningState,
        Aoe2xNavigationDestination, Aoe2xRoutePlan>;
    static constexpr std::string_view name = "aoe2x_route_squad_split";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase =
        Aoe2xGameplayPhase::MovementIntent;
    static void run(EcsWorld&, std::uint64_t);
};

struct RouteSquadCleanupSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        Aoe2xUnitState, Aoe2xPooledUnit>;
    using WriteOnlyComponents = Aoe2xComponentList<>;
    using ReadWriteComponents = Aoe2xComponentList<
        RouteSquadMemberInfo, RouteSquadRouteAssignment, Aoe2xRoutePlan>;
    static constexpr std::string_view name = "aoe2x_route_squad_cleanup";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Lifecycle;
    static void run(EcsWorld&, std::uint64_t);
};

static_assert(Aoe2xGameplaySystem<RouteSquadSpawnSystem>);
static_assert(Aoe2xGameplaySystem<RouteSquadCommandSystem>);
static_assert(Aoe2xGameplaySystem<RouteSquadSplitSystem>);
static_assert(Aoe2xGameplaySystem<RouteSquadCleanupSystem>);

} // namespace gld::ecs::aoe2x
