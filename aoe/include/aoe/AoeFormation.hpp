#pragma once

#include <array>
#include <concepts>
#include <cstdint>
#include <string_view>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <aoe/AoeNavMesh.hpp>
#include <aoe/AoeFormationFollow.hpp>
#include <aoe/AoeFormationRouteDebug.hpp>
#include <aoe/AoeFormationRoutePlan.hpp>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

struct AoeCollider;
struct AoePosition;
struct AoeSquadCombatSettings;
struct AoeSquadFormation;
struct AoeSquadLayoutState;
struct AoeFormationSlot;
struct AoeSquadMembers;
struct AoeSquadOrder;
struct AoeSquadSpawnState;
struct AoeSquadState;
struct AoeTeam;
struct AoePathRequest;
struct AoePathResult;
struct AoeSquadPathfinderPhase;

// Static gameplay phase responsible for turning squad-level formation state
// into stable per-member movement goals.
struct AoeFormationPhase {};

enum class AoeFormationIntentMode : std::uint8_t {
    None, Form, Regroup, Follow, Hold, Arrive
};

struct AoeFormationIntent {
    AoeFormationIntentMode mode = AoeFormationIntentMode::None;
    glm::vec2 center_target{0.f};
    glm::vec2 forward{1.f, 0.f};
    float speed_limit = 0.f;
    std::uint64_t order_revision = 0;
    std::uint64_t produced_tick = 0;
    bool anchor_arrived = false;
    bool allow_arrival_rematch = false;
    bool valid = false;
};

enum class AoeFormationResultStatus : std::uint8_t { None, Ready, Failed };

struct AoeFormationResult {
    AoeFormationResultStatus status = AoeFormationResultStatus::None;
    std::uint64_t produced_tick = 0;
    bool members_arrived = false;
    bool rematched = false;
    bool valid = false;
};

struct AoeFormationNavCorridor {
    AoeNavCorridor corridor;
    std::uint64_t order_revision = 0;
    std::uint64_t logic_map_revision = 0;
    std::uint64_t produced_tick = 0;
    bool valid = false;
};

struct AoeFormationRouteOwner {
    entt::entity squad{entt::null};
    std::uint64_t squad_order_revision = 0;
    std::uint64_t unit_instance_id = 0;
};

// Marks the AoeMoveGoal published from a RouteSplit-owned member route.  Goal
// ownership is separate from route ownership so cleanup never removes a goal
// later installed by combat or another controller.
struct AoeFormationMoveGoalOwner {
    entt::entity squad{entt::null};
    std::uint64_t squad_order_revision = 0;
    std::uint64_t unit_instance_id = 0;
};

struct AoeFormationRouteUnit {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
};

struct AoeFormationRouteFrame {
    float progress = 0.f;
    glm::vec2 center{0.f};
    glm::vec2 forward{1.f, 0.f};
};

struct AoeFormationRouteTrajectory {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t layout_revision = 0;
    std::vector<AoeFormationRouteFrame> frames;
    float total_progress = 0.f;
    bool valid = false;
};

// Per-unit correspondence between AoeNavigationPath waypoints and the shared
// Squad trajectory. MovingControl uses it to synchronize inner/outer turns in
// O(member_count), without pairwise collision checks.
struct AoeFormationMemberRouteProgress {
    entt::entity squad{entt::null};
    std::uint64_t squad_order_revision = 0;
    std::uint64_t unit_instance_id = 0;
    glm::vec2 origin{0.f};
    float origin_progress = 0.f;
    std::vector<float> waypoint_progress;
    std::vector<float> segment_speed_ratio;
};

enum class AoeFormationRouteSplitStatus : std::uint8_t {
    None,
    Ready,
    Failed,
};

struct AoeFormationRouteSplitState {
    AoeFormationRouteSplitStatus status =
        AoeFormationRouteSplitStatus::None;
    std::uint64_t order_revision = 0;
    std::uint64_t layout_revision = 0;
    std::uint64_t logic_map_revision = 0;
    std::uint64_t nav_mesh_revision = 0;
    std::uint64_t settings_signature = 0;
    std::uint64_t produced_tick = 0;
    std::vector<AoeFormationRouteUnit> units;
};

struct AoeFormationRouteSplitSettings {
    std::uint32_t compression_portal_window = 2;
    float portal_band_half_width = .1f;
    float width_safety_distance = .25f;
    // Route progress slows through high-ratio segments, so this bounds layout
    // offset per Squad progress without allowing a unit to exceed its speed.
    float maximum_layout_change_per_progress = 2.5f;
    float path_validation_epsilon = .001f;
    float maximum_center_step = .5f;
    float maximum_rotation_step_degrees = 10.f;
    float maximum_reformation_step = .5f;
    // Center turns use forward-only circular arcs. The effective radius is
    // raised further when needed to keep the innermost slot moving forward.
    float minimum_center_turn_radius = .5f;
    float minimum_member_forward_ratio = .05f;
};

struct AoeFormationRouteSplitDiagnostics {
    std::uint64_t splits = 0;
    std::uint64_t cache_hits = 0;
    std::uint64_t members_routed = 0;
    std::uint64_t members_failed = 0;
    std::uint64_t portals_processed = 0;
    std::uint64_t frames_generated = 0;
    std::uint64_t maximum_frames = 0;
    double total_ms = 0.0;
    double max_ms = 0.0;

    double average_ms() const {
        return splits ? total_ms / static_cast<double>(splits) : 0.0;
    }
};

enum class AoeFormationMovingStatus : std::uint8_t {
    None,
    Moving,
    Arrived,
    Failed,
};

struct AoeFormationMovingState {
    AoeFormationMovingStatus status = AoeFormationMovingStatus::None;
    std::uint64_t order_revision = 0;
    float shared_progress = 0.f;
    float slowest_progress = 0.f;
    float maximum_lead = 0.f;
    std::uint32_t active_members = 0;
};

struct AoeFormationMovingSettings {
    float allowed_progress_lead = .25f;
    float progress_epsilon = .0001f;
    // Proportional spacing-error correction (world units per second of speed
    // bias per world unit of gap error) and the dead zone where no bias is
    // applied. Keeps a follow chain from collapsing or stretching during
    // transients while the progress anchor governs the long-term speed.
    float spacing_gain = 2.f;
    float spacing_tolerance = .05f;
};

struct AoeFormationMovingDiagnostics {
    std::uint64_t updates = 0;
    std::uint64_t members_synchronized = 0;
    double total_ms = 0.0;
    double max_ms = 0.0;

    double average_ms() const {
        return updates ? total_ms / static_cast<double>(updates) : 0.0;
    }
};

struct AoeSquadLayoutRole {};
struct AoeFormationRouteSplitRole {};
struct AoeFormationMovingControlRole {};
struct AoeFormationAttackControlRole {};
struct AoeFormationCommandCompletionRole {};

enum class AoeFormationModuleResult : std::uint8_t {
    Continue,
    StopSquad,
};

// Common references are resolved once per squad by the composite plugin.
// Individual modules may query optional components through world when needed.
struct AoeFormationSquadContext {
    entt::entity squad{entt::null};
    std::uint64_t tick = 0;
    AoeSquadMembers& members;
    AoeSquadSpawnState& spawn;
    AoeSquadFormation& formation;
    AoeSquadLayoutState& layout;
    AoeSquadCombatSettings& combat;
    AoeSquadOrder& order;
    AoeSquadState& state;
    AoePosition& center;
    AoeCollider& collider;
    AoeTeam& team;
};

template<class T, class Role>
concept AoeFormationModule = requires(
    App& app, EcsWorld& world, AoeFormationSquadContext& context) {
    typename T::role;
    requires std::same_as<typename T::role, Role>;
    { T::name } -> std::convertible_to<std::string_view>;
    { T::install(app) } -> std::same_as<void>;
    { T::run(world, context) } ->
        std::same_as<AoeFormationModuleResult>;
};

struct AoeFullSquadLayoutModule {
    using role = AoeSquadLayoutRole;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&);
};

struct AoeNavMeshRouteSplitModule {
    using role = AoeFormationRouteSplitRole;
    static constexpr std::string_view name = "navmesh_funnel_v1";
    static void install(App&);
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&);
};

struct AoeNavMeshSquadPathfinderPlugin {
    using phase = AoeSquadPathfinderPhase;
    static constexpr std::string_view name = "navmesh_corridor";
    static void install(App&);
    static AoePathResult find(EcsWorld&, const AoePathRequest&);
};

// Minimal RouteSplit execution bridge.  It does not perform squad speed or
// progress control; it only publishes the per-member AoeMoveGoal required by
// the existing movement pipeline.
struct AoePassThroughMovingControlModule {
    using role = AoeFormationMovingControlRole;
    static constexpr std::string_view name = "route_goal_bridge";
    static void install(App&) {}
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&);
};

struct AoeSynchronizedFormationMovingControlModule {
    using role = AoeFormationMovingControlRole;
    static constexpr std::string_view name = "synchronized_route_progress";
    static void install(App&);
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&);
};

// Completes a synchronized route only after every natural slot has arrived,
// then atomically releases RouteSplit/Follow ownership for the whole squad.
struct AoeRouteCommandCompletionModule {
    using role = AoeFormationCommandCompletionRole;
    static constexpr std::string_view name = "route_arrival";
    static void install(App&) {}
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&);
};

// The whole MovingControl stage for one squad as a standalone system: it
// materializes RouteSplit's Follow/Detach timeline, maintains the shared
// column progress/speed, and emits every follower's AoePathMotionRequest.
AoeFormationModuleResult aoe_synchronized_follow_motion_system(
    EcsWorld&, AoeFormationSquadContext&);

#define GLD_AOE_DECLARE_PASS_FORMATION_MODULE(type_name, role_name) \
struct type_name { \
    using role = role_name; \
    static constexpr std::string_view name = "pass_through"; \
    static void install(App&) {} \
    static AoeFormationModuleResult run( \
        EcsWorld&, AoeFormationSquadContext&) { \
        return AoeFormationModuleResult::Continue; \
    } \
}

GLD_AOE_DECLARE_PASS_FORMATION_MODULE(
    AoePassThroughSquadLayoutModule, AoeSquadLayoutRole);
GLD_AOE_DECLARE_PASS_FORMATION_MODULE(
    AoePassThroughRouteSplitModule, AoeFormationRouteSplitRole);
GLD_AOE_DECLARE_PASS_FORMATION_MODULE(
    AoePassThroughAttackControlModule, AoeFormationAttackControlRole);
GLD_AOE_DECLARE_PASS_FORMATION_MODULE(
    AoePassThroughCommandCompletionModule,
    AoeFormationCommandCompletionRole);

#undef GLD_AOE_DECLARE_PASS_FORMATION_MODULE

using AoeFormationModuleRun = AoeFormationModuleResult (*)(
    EcsWorld&, AoeFormationSquadContext&);

namespace detail {
void aoe_modular_formation_fixed_tick(EcsWorld&, std::uint64_t,
    const std::array<AoeFormationModuleRun, 5>&);
}

template<
    AoeFormationModule<AoeSquadLayoutRole> SquadLayout,
    AoeFormationModule<AoeFormationRouteSplitRole> RouteSplit,
    AoeFormationModule<AoeFormationMovingControlRole> MovingControl,
    AoeFormationModule<AoeFormationAttackControlRole> AttackControl,
    AoeFormationModule<AoeFormationCommandCompletionRole> CommandCompletion>
struct AoeFormationPlugin {
    using phase = AoeFormationPhase;
    using SquadLayoutModule = SquadLayout;
    using RouteSplitModule = RouteSplit;
    using MovingControlModule = MovingControl;
    using AttackControlModule = AttackControl;
    using CommandCompletionModule = CommandCompletion;

    static constexpr std::string_view name = "modular";

    static void install(App& app) {
        SquadLayout::install(app);
        RouteSplit::install(app);
        MovingControl::install(app);
        AttackControl::install(app);
        CommandCompletion::install(app);
    }

    static void fixed_tick(EcsWorld& world, std::uint64_t tick) {
        static constexpr std::array<AoeFormationModuleRun, 5> modules{
            &SquadLayout::run,
            &RouteSplit::run,
            &MovingControl::run,
            &AttackControl::run,
            &CommandCompletion::run,
        };
        detail::aoe_modular_formation_fixed_tick(world, tick, modules);
    }
};

using AoeLayoutOnlyFormationPlugin = AoeFormationPlugin<
    AoeFullSquadLayoutModule,
    AoePassThroughRouteSplitModule,
    AoePassThroughMovingControlModule,
    AoePassThroughAttackControlModule,
    AoePassThroughCommandCompletionModule>;

using AoeLayoutRouteSplitFormationPlugin = AoeFormationPlugin<
    AoeFullSquadLayoutModule,
    AoeNavMeshRouteSplitModule,
    AoePassThroughMovingControlModule,
    AoePassThroughAttackControlModule,
    AoePassThroughCommandCompletionModule>;

using AoeLayoutRouteSplitMovingFormationPlugin = AoeFormationPlugin<
    AoeFullSquadLayoutModule,
    AoeNavMeshRouteSplitModule,
    AoeSynchronizedFormationMovingControlModule,
    AoePassThroughAttackControlModule,
    AoeRouteCommandCompletionModule>;

glm::vec2 aoe_formation_slot_world(
    const AoePosition&, const AoeSquadFormation&,
    const AoeFormationSlot&);

// Production implementation: moving/elastic slots, speed limiting and
// publication/consumption of the optional arrival-rematch contract.
struct AoeFullFormationPlugin {
    using phase = AoeFormationPhase;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Performance-floor implementation. It lays out a squad once and gives every
// member one stable destination per order episode. Individual navigation is
// deliberately retained, so map/A* cost remains observable.
struct AoePassThroughFormationPlugin {
    using phase = AoeFormationPhase;
    static constexpr std::string_view name = "pass_through";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
