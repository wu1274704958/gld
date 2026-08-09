#pragma once

#include <cstdint>
#include <concepts>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <ecs/App.hpp>
#include <ecs/Components.hpp>
#include <ecs/Events.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <aoe/AoeGameplayComponents.hpp>
#include <aoe/AoeMap.hpp>

namespace gld::ecs::aoe {

enum class AttackMode { Melee, Projectile };
enum class UnitState { Idle, Moving, Attacking, Dying, Disappearing };
enum class AoeTargetAcquisitionType : std::uint8_t { NearestEnemy };

struct TypedAmount {
    int class_id = 0;
    float amount = 0.f;
};

struct CollisionDefinition {
    float radius_x = 0.f;
    float radius_y = 0.f;
    float height = 0.f;
};

struct AttackDefinition {
    AttackMode mode = AttackMode::Melee;
    std::vector<TypedAmount> damage;
    float range = 0.f;
    float release_seconds = 0.f;
    float animation_duration_seconds = 0.f;
    float cooldown_seconds = 0.f;
    float critical_chance = 0.f;
    float critical_multiplier = 1.f;
    std::string projectile_id;
    std::optional<glm::vec3> projectile_launch_offset;
};

struct MovementDefinition { float speed = 1.f; };
struct TargetAcquisitionDefinition {
    AoeTargetAcquisitionType strategy =
        AoeTargetAcquisitionType::NearestEnemy;
    float radius = 6.f;
    float disengage_radius = 9.f;
};
struct LifecycleDefinition {
    float death_duration_seconds = 0.f;
    float disappear_duration_seconds = 0.f;
    bool recycle_after_death = false;
};

struct PresentationDefinition {
    std::string backend;
    std::string resource_id;
    int default_player_color = 1;
    std::unordered_map<std::string, std::string> animations;

    std::string animation(const std::string& semantic) const;
};

struct AoeUnitDefinition {
    std::string id;
    std::uint32_t level = 1;
    float max_hp = 1.f;
    std::vector<TypedAmount> armor;
    std::vector<std::string> tags;
    CollisionDefinition collision;
    MovementDefinition movement;
    TargetAcquisitionDefinition target_acquisition;
    LifecycleDefinition lifecycle;
    std::optional<AttackDefinition> attack;
    PresentationDefinition presentation;
};

struct AoeUnitDefinitionDesc
    : BaseAssetDesc<AoeUnitDefinitionDesc, std::string> {
    using Asset = AoeUnitDefinition;
    using BaseAssetDesc::BaseAssetDesc;
    const std::string& path() const { return get<0>(); }
};

struct AoeUnitDefinitionLoader : IAssetLoader<AoeUnitDefinitionDesc> {
    std::shared_ptr<void> load_cpu(const AoeUnitDefinitionDesc&,
                                  const IFileSystem&) override;
    std::shared_ptr<AoeUnitDefinition> finalize(
        std::shared_ptr<void>, const AoeUnitDefinitionDesc&) override;
};

struct AoeUnitDefinitionRecord {
    std::string id;
    std::string path;
};

class AoeUnitDefinitionManager {
public:
    AoeUnitDefinitionManager() = default;
    AoeUnitDefinitionManager(AssetServer& server, std::string root)
        : server_(&server), root_(std::move(root)) {}

    void refresh();
    const std::vector<AoeUnitDefinitionRecord>& list() const { return records_; }
    const AoeUnitDefinitionRecord* find(const std::string& id) const;
    Handle<AoeUnitDefinition> load(const std::string& id);
    const std::string& root() const { return root_; }

private:
    AssetServer* server_ = nullptr;
    std::string root_ = "aoe_units";
    std::vector<AoeUnitDefinitionRecord> records_;
};

struct AoeUnitSpawnOptions {
    std::string definition_id;
    int player_color = 0; // 0 uses the definition default
    std::uint32_t team_id = 0;
    int direction = 0;
    int direction_count = 16;
    std::uint32_t layers = 0x1u;
    glm::vec2 position{0.f};
};

struct AoeGameplaySpawnRequest {
    AoeUnitSpawnOptions options;
    Handle<AoeUnitDefinition> definition;
    bool requested = false;
};

struct AoeGameplaySpawnError { std::string message; };
struct AoeUnitDefinitionRef { Handle<AoeUnitDefinition> value; };
struct AoeHealth { float current = 0.f; float maximum = 0.f; };
struct AoeLevel { std::uint32_t value = 1; };
// Tick-start authoritative position used only to interpolate presentation.
// Gameplay systems always read AoePosition directly.
struct AoePositionHistory { glm::vec2 previous{0.f}; };
struct AoeMovement { float speed = 1.f; };

enum class AoeMovementIntentKind {
    None, Move, FormationSlot, AttackApproach
};

// Raw path-following request consumed by the local avoidance solver.
struct AoePathMotionRequest {
    AoeMovementIntentKind kind = AoeMovementIntentKind::None;
    glm::vec2 velocity{0.f};
    glm::vec2 local_goal{0.f};
    float max_speed = 0.f;
    std::uint64_t path_sequence = 0;
    std::uint64_t produced_tick = 0;
    bool valid = false;
};

// Local navigation/avoidance output. The vector direction and length are both
// authoritative inputs to global motion planning.
struct AoeMovementIntent {
    AoeMovementIntentKind kind = AoeMovementIntentKind::None;
    glm::vec2 velocity{0.f};
    glm::vec2 raw_path_velocity{0.f};
    glm::vec2 local_goal{0.f};
    std::uint32_t neighbor_count = 0;
    std::int8_t avoidance_side = 0;
    bool threatened = false;
    bool locally_infeasible = false;
    std::uint64_t produced_tick = 0;
    bool valid = false;
};

enum class AoeGlobalMotionMode {
    Clear, SideStep, PassingLeft, PassingRight, Yielding, Backing, Recovering
};

enum class AoeMotionDecisionReason {
    None, SameDirectionConflict, FasterTraffic, HeadOnTraffic,
    CrossingTraffic, StarvationPriority, SideBlocked, DeadlockEscape
};

enum class AoeMotionStopReason {
    None, NoPath, Arrived, Attacking, LocalAvoidanceInfeasible,
    GlobalYield, GlobalSideStepBlocked, GlobalDeadlock,
    StaticSafetyClipped, DynamicSafetyClipped, RepathPending,
    DynamicRepathFailed, Unknown
};

struct AoeGlobalMotionState {
    AoeGlobalMotionMode mode = AoeGlobalMotionMode::Clear;
    entt::entity peer{entt::null};
    std::uint64_t peer_instance_id = 0;
    std::uint64_t last_conflict_tick = 0;
    std::uint64_t last_backing_tick = 0;
    std::uint32_t wait_ticks = 0;
    std::uint32_t backing_ticks = 0;
    float backing_distance = 0.f;
    std::int8_t negotiated_side = 0;
};

struct AoeGlobalMotionDecision {
    glm::vec2 velocity{0.f};
    float static_safe_fraction = 1.f;
    float dynamic_safe_fraction = 1.f;
    float safe_fraction = 1.f;
    AoeGlobalMotionMode mode = AoeGlobalMotionMode::Clear;
    AoeMotionDecisionReason reason = AoeMotionDecisionReason::None;
    AoeMotionStopReason stop_reason = AoeMotionStopReason::None;
    entt::entity yielding_to{entt::null};
    std::uint64_t yielding_to_instance = 0;
    std::uint32_t conflict_group = 0;
    std::uint32_t selected_conflicts = 0;
    std::uint32_t candidate_count = 0;
    std::uint32_t wait_ticks = 0;
    float nearest_time_to_collision = 0.f;
    std::uint64_t produced_tick = 0;
    bool valid = false;
};

struct AoeLocomotionState {
    glm::vec2 velocity{0.f};
    glm::vec2 previous_velocity{0.f};
    // Maximum speed after persistent gameplay limits (for example a squad's
    // slowest-member cap), but before transient arrive/steering/collision
    // reductions. Presentation uses this to distinguish commanded slow travel
    // from genuinely obstructed movement.
    float effective_max_speed = 0.f;
    float actual_speed = 0.f;
    double distance_travelled = 0.0;
    std::uint32_t stalled_ticks = 0;
    int pending_facing_direction = -1;
    std::uint8_t pending_facing_ticks = 0;
};
// Persistent continuous world-space facing. Unlike velocity, this remains
// meaningful while the unit is stationary.
struct AoeDirection { glm::vec2 value{1.f, 0.f}; };
struct AoeTeam { std::uint32_t id = 0; };
struct AoeFacing { int direction = 0; int direction_count = 16; };
struct AoePresentationOptions { int player_color = 1; std::uint32_t layers = 0x1u; };

struct AoeUnitTarget {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
};
struct AoeAttackOrder { AoeUnitTarget target; };
struct AoeAttackMoveOrder { glm::vec2 destination{0.f}; };
struct AoeMoveGoal {
    glm::vec2 destination{0.f};
    float stopping_distance = 0.f;
    AoeUnitTarget target{};
};
struct AoeEngagementApproach {
    AoeUnitTarget target{};
    glm::vec2 direction{1.f, 0.f};
    float desired_gap = 0.f;
    std::uint64_t assignment_sequence = 0;
    std::uint32_t unreachable_ticks = 0;
};
struct AoeNavigationPath {
    std::vector<glm::vec2> waypoints;
    std::size_t current = 0;
    glm::vec2 requested_goal{0.f};
    std::uint64_t map_revision = 0;
    std::uint64_t request_sequence = 0;
    std::uint64_t last_repath_tick = 0;
    std::uint32_t blocked_ticks = 0;
    bool no_path = false;
    bool include_dynamic_obstacles = false;
    bool dynamic_repath_requested = false;
    // A failed optional dynamic replan must not invalidate a still-usable
    // static route. This flag is diagnostic and is cleared by the next
    // successful plan.
    bool dynamic_repath_failed = false;
};
struct AoeMapStaticObstacle {
    AoeStaticObstacleShape shape = AoeStaticObstacleShape::Aabb;
    glm::vec2 half_extents{.5f};
    float radius = .5f;
    AoeObstacleId obstacle_id = 0;
    glm::vec2 registered_center{0.f};
    glm::vec2 registered_half_extents{0.f};
    float registered_radius = 0.f;
    AoeStaticObstacleShape registered_shape = AoeStaticObstacleShape::Aabb;
};
struct AoeRecyclePending {};
struct AoePooledUnit {};

struct AoeActionState {
    UnitState state = UnitState::Idle;
    bool critical = false;
    bool release_emitted = false;
    std::uint64_t sequence = 0;
    std::uint64_t state_started_tick = 0;
    std::uint64_t release_tick = 0;
    std::uint64_t finish_tick = 0;
    std::uint64_t ready_tick = 0;
};

struct AoeGameplayIdentity {
    std::uint64_t instance_id = 0;
    std::uint64_t rng_state = 1;
};

struct AoePathRequest {
    glm::vec2 start{0.f};
    glm::vec2 goal{0.f};
    glm::vec2 clearance{0.f};
    entt::entity subject{entt::null};
    entt::entity squad{entt::null};
    entt::entity ignored_dynamic_target{entt::null};
    bool include_dynamic_obstacles = false;
};

enum class AoePathStatus { Ready, NoPath, InvalidStart, InvalidGoal };

struct AoePathResult {
    AoePathStatus status = AoePathStatus::NoPath;
    std::vector<glm::vec2> waypoints;
    std::uint64_t map_revision = 0;
};

template<class T>
concept AoePathfinderLogic = requires(EcsWorld& world,
                                      const AoePathRequest& request) {
    { T::find(world, request) } -> std::same_as<AoePathResult>;
};

class AoePathfinderRegistry {
public:
    using FindFn = AoePathResult (*)(EcsWorld&, const AoePathRequest&);

    template<AoePathfinderLogic T>
    void bind(std::string id) { bind_erased(std::move(id), &T::find); }

    bool contains(std::string_view id) const;
    AoePathResult find(std::string_view id, EcsWorld&,
                       const AoePathRequest&) const;

private:
    void bind_erased(std::string id, FindFn function);
    std::unordered_map<std::string, FindFn> entries_;
};

struct DirectPathfinderLogic {
    static AoePathResult find(EcsWorld&, const AoePathRequest&);
};

struct GridAStarPathfinderLogic {
    static AoePathResult find(EcsWorld&, const AoePathRequest&);
};

enum class AoeNavigationEventStatus { Ready, Blocked, NoPath };
struct AoeNavigationEvent {
    entt::entity subject{entt::null};
    std::uint64_t request_sequence = 0;
    AoeNavigationEventStatus status = AoeNavigationEventStatus::Ready;
    std::uint64_t tick = 0;
};

struct AoeTargetAcquisitionContext {
    entt::entity seeker{entt::null};
    glm::vec2 origin{0.f};
    float radius = 0.f;
    std::uint32_t seeker_team = 0;
    std::span<const AoeUnitTarget> excluded{};
    std::span<const AoeUnitTarget> candidates{};
    bool use_candidates = false;
};

template<class T>
concept AoeTargetAcquisitionStrategy = requires(
    const EcsWorld& world, const AoeTargetAcquisitionContext& context) {
    { T::select(world, context) }
        -> std::same_as<std::optional<AoeUnitTarget>>;
};

struct NearestEnemyAcquisitionStrategy {
    static std::optional<AoeUnitTarget> select(
        const EcsWorld&, const AoeTargetAcquisitionContext&);
};

template<AoeTargetAcquisitionType>
struct AoeTargetAcquisitionBinding;

template<>
struct AoeTargetAcquisitionBinding<
    AoeTargetAcquisitionType::NearestEnemy> {
    using type = NearestEnemyAcquisitionStrategy;
};

template<AoeTargetAcquisitionType Type>
std::optional<AoeUnitTarget> select_aoe_target(
    const EcsWorld& world, const AoeTargetAcquisitionContext& context) {
    using Strategy = typename AoeTargetAcquisitionBinding<Type>::type;
    static_assert(AoeTargetAcquisitionStrategy<Strategy>);
    return Strategy::select(world, context);
}

std::optional<AoeUnitTarget> dispatch_aoe_target(
    AoeTargetAcquisitionType, const EcsWorld&,
    const AoeTargetAcquisitionContext&);
std::string_view aoe_target_acquisition_name(AoeTargetAcquisitionType);

template<std::size_t N>
struct AoeFixedString {
    char value[N]{};

    consteval AoeFixedString(const char (&source)[N]) {
        std::copy_n(source, N, value);
    }

    constexpr std::string_view view() const { return {value, N - 1}; }
    constexpr bool operator==(const AoeFixedString&) const = default;
};

template<AoeFixedString Tag, int Priority>
struct AoeFormationTagPriority {
    static constexpr auto tag = Tag;
    static constexpr int priority = Priority;
};

enum class AoeFormationType { Skirmish };

struct AoeFormationMemberInfo {
    AoeUnitTarget unit{};
    std::uint32_t ordinal = 0;
    std::vector<std::string> tags;
    glm::vec2 collision_radius{0.f};
};

struct AoeFormationContext {
    std::vector<AoeFormationMemberInfo> members;
    float spacing = .75f;
};

struct AoeFormationSlot {
    AoeUnitTarget unit{};
    glm::vec2 local_offset{0.f};
    std::int64_t priority = 0;
};

template<class T>
concept AoeFormationLogic = requires(const AoeFormationContext& context) {
    { T::layout(context) } -> std::same_as<std::vector<AoeFormationSlot>>;
};

template<class... TagPriorities>
struct AoeSquareFormation {
private:
    static consteval bool unique_tags() {
        constexpr std::array tags{TagPriorities::tag.view()...};
        for (std::size_t i = 0; i < tags.size(); ++i)
            for (std::size_t j = i + 1; j < tags.size(); ++j)
                if (tags[i] == tags[j]) return false;
        return true;
    }

    template<class Rule>
    static std::int64_t contribution(const AoeFormationMemberInfo& member) {
        return std::find(member.tags.begin(), member.tags.end(),
                         Rule::tag.view()) != member.tags.end()
            ? static_cast<std::int64_t>(Rule::priority) : 0;
    }

public:
    static_assert(unique_tags(), "formation tag priority rules must be unique");

    static std::int64_t priority(const AoeFormationMemberInfo& member) {
        return (std::int64_t{0} + ... + contribution<TagPriorities>(member));
    }

    static std::vector<AoeFormationSlot> layout(
        const AoeFormationContext& context) {
        if (!std::isfinite(context.spacing) || context.spacing < 0.f)
            return {};
        struct Ranked {
            const AoeFormationMemberInfo* member = nullptr;
            std::int64_t priority = 0;
        };
        std::vector<Ranked> ranked;
        ranked.reserve(context.members.size());
        float radius = 0.f;
        for (const auto& member : context.members) {
            ranked.push_back({&member, priority(member)});
            radius = std::max(radius, std::max(
                member.collision_radius.x, member.collision_radius.y));
        }
        std::stable_sort(ranked.begin(), ranked.end(),
            [](const Ranked& a, const Ranked& b) {
                if (a.priority != b.priority) return a.priority > b.priority;
                return a.member->ordinal < b.member->ordinal;
            });
        const std::size_t count = ranked.size();
        if (!count) return {};
        const std::size_t columns = static_cast<std::size_t>(
            std::ceil(std::sqrt(static_cast<double>(count))));
        const std::size_t rows = (count + columns - 1) / columns;
        const float cell = radius * 2.f + context.spacing;
        std::vector<AoeFormationSlot> result;
        result.reserve(count);
        std::size_t index = 0;
        for (std::size_t row = 0; row < rows; ++row) {
            const std::size_t row_count = std::min(columns, count - index);
            const float y = (static_cast<float>(rows - 1) * .5f -
                             static_cast<float>(row)) * cell;
            for (std::size_t column = 0; column < row_count; ++column) {
                const float x = (static_cast<float>(column) -
                    static_cast<float>(row_count - 1) * .5f) * cell;
                result.push_back({ranked[index].member->unit, {x, y},
                                  ranked[index].priority});
                ++index;
            }
        }
        return result;
    }
};

using DefaultSkirmishFormation = AoeSquareFormation<
    AoeFormationTagPriority<"spearman", 300>,
    AoeFormationTagPriority<"cavalry", 200>,
    AoeFormationTagPriority<"scout", 100>,
    AoeFormationTagPriority<"archer", -100>>;

class AoeFormationRegistry {
public:
    using LayoutFn = std::vector<AoeFormationSlot> (*)(
        const AoeFormationContext&);

    template<AoeFormationType Type, AoeFormationLogic T>
    void bind() { bind_erased(Type, &T::layout); }

    bool contains(AoeFormationType) const;
    std::vector<AoeFormationSlot> layout(
        AoeFormationType, const AoeFormationContext&) const;

private:
    void bind_erased(AoeFormationType, LayoutFn);
    std::unordered_map<AoeFormationType, LayoutFn> entries_;
};

struct AoeSquadCompositionEntry {
    std::string definition_id;
    std::uint32_t count = 1;
    int player_color = 0;
};

struct AoeSquadSpawnOptions {
    std::vector<AoeSquadCompositionEntry> composition;
    glm::vec2 center{0.f};
    glm::vec2 forward{1.f, 0.f};
    AoeFormationType formation = AoeFormationType::Skirmish;
    float formation_spacing = .75f;
    std::uint32_t team_id = 0;
    int player_color = 0;
    std::uint32_t layers = 0x1u;
    int direction_count = 16;
    AoeTargetAcquisitionType acquisition_strategy =
        AoeTargetAcquisitionType::NearestEnemy;
    float acquisition_radius = 6.f;
    float disengage_radius = 9.f;
};

enum class AoeSquadSpawnStatus { Pending, Ready, Partial, Failed, Empty };
enum class AoeSquadOrderType { Idle, MoveTo, AttackTarget, AttackMove, Stop };
enum class AoeSquadPhase {
    Forming, Moving, Engaging, Regrouping, Blocked, Idle, Empty, Failed
};

struct AoeSquadPendingMember {
    entt::entity entity{entt::null};
    std::uint32_t ordinal = 0;
    std::string definition_id;
};
struct AoeSquadMembers {
    std::vector<AoeUnitTarget> active;
    std::vector<AoeSquadPendingMember> pending;
};
struct AoeSquadSpawnState {
    AoeSquadSpawnStatus status = AoeSquadSpawnStatus::Pending;
    std::uint32_t requested = 0;
    std::uint32_t succeeded = 0;
    std::uint32_t failed = 0;
    std::vector<std::string> errors;
};
struct AoeSquadFormation {
    AoeFormationType type = AoeFormationType::Skirmish;
    float spacing = .75f;
    glm::vec2 forward{1.f, 0.f};
    std::vector<AoeFormationSlot> slots;
    bool dirty = true;
    bool teleport_on_next_layout = true;
    // Attack Move performs one role-preserving nearest-slot rematch after its
    // anchor reaches the destination. Engagement or a new order resets it.
    bool arrival_reflow_done = false;
};
struct AoeSquadCombatSettings {
    AoeTargetAcquisitionType acquisition_strategy =
        AoeTargetAcquisitionType::NearestEnemy;
    float acquisition_radius = 6.f;
    float disengage_radius = 9.f;
};
struct AoeSquadOrder {
    AoeSquadOrderType type = AoeSquadOrderType::Idle;
    glm::vec2 destination{0.f};
    AoeUnitTarget target{};
    // Incremented for every accepted squad command, including an identical
    // destination. Formation backends use it to identify movement episodes.
    std::uint64_t revision = 0;
};
struct AoeSquadState {
    AoeSquadPhase phase = AoeSquadPhase::Forming;
    float movement_speed = 0.f;
};
struct AoeSquadMember {
    entt::entity squad{entt::null};
    std::uint32_t ordinal = 0;
};
struct AoeSquadMoveSpeedLimit { float value = 0.f; };

enum class AoeSquadTrafficMode {
    Clear, Following, PassingLeft, PassingRight, Yielding, Recovering
};

struct AoeSquadTrafficState {
    AoeSquadTrafficMode mode = AoeSquadTrafficMode::Clear;
    entt::entity peer{entt::null};
    glm::vec2 desired_velocity{0.f};
    float speed_scale = 1.f;
    float lateral_offset = 0.f;
    float target_lateral_offset = 0.f;
    std::int8_t negotiated_side = 0;
    std::uint32_t conflict_ticks = 0;
    std::uint64_t last_conflict_tick = 0;
    std::uint64_t last_progress_tick = 0;
};

struct AoeSquadSlotFollowState {
    glm::vec2 original_destination{0.f};
    glm::vec2 navigation_destination{0.f};
    bool elastic = false;
    std::uint32_t recovery_ticks = 0;
};

enum class AoeSquadCommandType {
    MoveTo, AttackTarget, AttackMove, Stop, SetFormation
};
struct AoeSquadCommand {
    AoeSquadCommandType type = AoeSquadCommandType::Stop;
    entt::entity squad{entt::null};
    glm::vec2 position{0.f};
    AoeUnitTarget target{};
    AoeFormationType formation = AoeFormationType::Skirmish;
};
struct AoeSquadCommands { std::vector<AoeSquadCommand> queue; };

enum class AoeProjectileMissReason {
    None, TargetInvalid, TargetRecycled, TargetDead, Expired, UnknownLogic
};

struct AoeProjectileSpawnContext {
    entt::entity attacker{entt::null};
    AoeUnitTarget target{};
    std::uint64_t attack_sequence = 0;
    bool critical = false;
    float critical_multiplier = 1.f;
    glm::vec3 launch_position{0.f};
    std::uint32_t layers = 0x1u;
    std::vector<TypedAmount> damage;
};

struct AoeProjectile {
    std::string id;
    entt::entity attacker{entt::null};
    AoeUnitTarget target{};
    std::uint64_t attack_sequence = 0;
    bool critical = false;
    float critical_multiplier = 1.f;
    glm::vec3 launch_position{0.f};
    glm::vec3 previous_position{0.f};
    glm::vec3 position{0.f};
    glm::vec3 velocity{0.f};
    std::uint32_t layers = 0x1u;
    std::vector<TypedAmount> damage;
    float speed = 0.f;
    float arc_height = 0.f;
    float collision_radius = 0.f;
    float travelled = 0.f;
    float progress = 0.f;
    std::uint64_t spawn_tick = 0;
    std::uint64_t expire_tick = 0;
};

template<class T>
concept AoeProjectileLogic = requires(EcsWorld& world,
                                      const AoeProjectileSpawnContext& context) {
    { T::spawn(world, context) } -> std::same_as<entt::entity>;
};

class AoeProjectileRegistry {
public:
    using SpawnFn = entt::entity (*)(EcsWorld&, const AoeProjectileSpawnContext&);

    template<AoeProjectileLogic T>
    void bind(std::string id) { bind_erased(std::move(id), &T::spawn); }

    bool contains(std::string_view id) const;
    entt::entity spawn(std::string_view id, EcsWorld&,
                       const AoeProjectileSpawnContext&) const;

private:
    void bind_erased(std::string id, SpawnFn function);
    std::unordered_map<std::string, SpawnFn> entries_;
};

struct ArrowProjectileLogic {
    static constexpr float Speed = 7.f;
    static constexpr float ArcHeight = .65f;
    static constexpr float CollisionRadius = .05f;
    static constexpr float MaxLifetimeSeconds = 5.f;
    static entt::entity spawn(EcsWorld&, const AoeProjectileSpawnContext&);
};

enum class AoeActionEventType {
    AttackStarted, AttackReleased, AttackFinished, DamageApplied,
    DeathStarted, DisappearStarted, RecycleRequested,
    ProjectileSpawned, ProjectileHit, ProjectileMiss, ProjectileSpawnFailed
};
struct AoeActionEvent {
    entt::entity unit{entt::null};
    entt::entity target{entt::null};
    AoeActionEventType type = AoeActionEventType::AttackStarted;
    std::uint64_t tick = 0;
    std::uint64_t sequence = 0;
    bool critical = false;
    float amount = 0.f;
    entt::entity projectile{entt::null};
    std::string projectile_id;
    AoeProjectileMissReason projectile_reason = AoeProjectileMissReason::None;
};

enum class AoeCommandType {
    AttackTarget, AttackMove, MoveTo, Stop, SetFacing, SetHealth, SetLevel
};
struct AoeGameplayCommand {
    AoeCommandType type = AoeCommandType::Stop;
    entt::entity unit{entt::null};
    AoeUnitTarget target{};
    glm::vec2 position{0.f};
    float number = 0.f;
    std::int64_t integer = 0;
    int integer2 = 0;
};
struct AoeGameplayCommands { std::vector<AoeGameplayCommand> queue; };

struct AoeGameplaySettings {
    double fixed_dt = 1.0 / 30.0;
    std::uint32_t max_catchup_ticks = 8;
    std::uint64_t random_seed = 0x6a09e667f3bcc909ull;
};

struct AoeNavigationSettings {
    std::string unit_pathfinder_id = "grid_astar";
    std::string squad_pathfinder_id = "grid_astar";
    // The gameplay module requests the GPU planner by default. Headless builds
    // do not register it and therefore transparently use cpu_unit_flow.
    std::string global_motion_planner_id = "gpu_image";
    std::uint32_t blocked_repath_ticks = 12;
    std::uint32_t repath_cooldown_ticks = 3;
    std::uint32_t steering_facing_stable_ticks = 2;
    float steering_max_acceleration = 4.f;
    float steering_max_turn_radians_per_second = 6.28318530718f;
    float steering_stalled_speed = .05f;
    float squad_leash = 3.f;
    float slot_repath_distance = .5f;
    std::uint32_t formation_slot_recovery_ticks = 4;
    std::uint32_t squad_traffic_hold_ticks = 6;
    float squad_traffic_prediction_seconds = 1.5f;
    float squad_traffic_same_direction_dot = .65f;
    float squad_traffic_head_on_dot = -.5f;
    float squad_traffic_follow_gap = .75f;
    float squad_traffic_yield_speed_scale = .35f;
    float squad_traffic_lateral_clearance = .75f;
    float squad_traffic_offset_rate = 2.f;
    bool unit_flow_enabled = true;
    std::uint32_t unit_flow_max_neighbors = 12;
    std::uint32_t unit_flow_solver_iterations = 8;
    std::uint32_t unit_flow_starvation_ticks = 30;
    std::uint32_t unit_flow_backing_threshold_ticks = 45;
    std::uint32_t unit_flow_backing_max_ticks = 12;
    std::uint32_t unit_flow_backing_cooldown_ticks = 30;
    float unit_flow_prediction_seconds = 1.f;
    float unit_flow_same_direction_dot = .65f;
    float unit_flow_head_on_dot = -.5f;
    float unit_flow_same_speed_absolute = .05f;
    float unit_flow_same_speed_relative = .1f;
    float unit_flow_follow_gap = .2f;
    float unit_flow_yield_speed_scale = .2f;
    float unit_flow_lateral_clearance = .6f;
    float unit_flow_lateral_bias = .75f;
    float unit_flow_overlap_recovery_seconds = .25f;
    float unit_flow_backing_speed_scale = .25f;
    float unit_flow_backing_max_diameters = 1.5f;
};

struct AoeStaticObstacleBindings {
    std::unordered_map<entt::entity, AoeObstacleId> entities;
};

struct AoeGameplayClock {
    double accumulator = 0.0;
    std::uint64_t tick = 0;
    double dropped_seconds = 0.0;
    std::uint32_t ticks_this_frame = 0;
};

struct AoeGameplayLifecycle { std::uint64_t next_instance_id = 1; };
struct AoeGameplayPool {
    std::vector<entt::entity> available;
    std::uint64_t recycled = 0;
    std::uint64_t reused = 0;
};
struct AoeGameplayDiagnostics {
    std::uint64_t attacks_started = 0;
    std::uint64_t commands_rejected = 0;
    std::uint64_t events_emitted = 0;
    std::uint64_t damage_events = 0;
    std::uint64_t projectiles_spawned = 0;
    std::uint64_t projectiles_hit = 0;
    std::uint64_t projectiles_missed = 0;
    std::uint64_t projectiles_failed = 0;
    std::uint64_t steering_fallbacks = 0;
    std::uint64_t steering_fast_path = 0;
    std::uint64_t steering_full_solves = 0;
    std::uint64_t steering_cached_solves = 0;
    std::uint64_t steering_imminent_solves = 0;
    std::uint64_t steering_neighbors_considered = 0;
    std::uint64_t steering_side_switches = 0;
    std::uint64_t facing_changes_suppressed = 0;
    std::uint64_t facing_changes_committed = 0;
    std::uint64_t steering_escape_solves = 0;
    std::uint64_t dynamic_repath_failures = 0;
    std::uint64_t elastic_slot_uses = 0;
    std::uint64_t squad_traffic_conflicts = 0;
    std::uint64_t flow_active_intents = 0;
    std::uint64_t flow_neighbor_checks = 0;
    std::uint64_t flow_conflicts = 0;
    std::uint64_t flow_following = 0;
    std::uint64_t flow_passing = 0;
    std::uint64_t flow_yielding = 0;
    std::uint64_t flow_backing = 0;
    std::uint64_t flow_recovering = 0;
    std::uint64_t flow_starvation_promotions = 0;
    std::uint64_t flow_infeasible_assignments = 0;
    std::uint64_t flow_deadlock_escalations = 0;
    std::uint64_t flow_overlap_projections = 0;
    std::uint64_t flow_wait_ticks = 0;
    double movement_last_ms = 0.0;
    double movement_peak_ms = 0.0;
};

// Per-frame CPU timings. The resource and all clock reads are compiled in only
// when GLD_ENABLE_PERFORMANCE_MONITORING is enabled.
struct AoeGameplayPerformanceDiagnostics {
    double clear_events_ms = 0.0;
    double spawn_ms = 0.0;
    double fixed_total_ms = 0.0;
    double recycle_ms = 0.0;
    double position_history_ms = 0.0;
    double squad_spawn_resolution_ms = 0.0;
    double static_obstacle_index_ms = 0.0;
    double dynamic_obstacle_index_ms = 0.0;
    double squad_command_ms = 0.0;
    double command_ms = 0.0;
    double membership_cleanup_ms = 0.0;
    double squad_engagement_ms = 0.0;
    double squad_control_ms = 0.0;
    double formation_ms = 0.0;
    double squad_traffic_ms = 0.0;
    double attack_move_acquisition_ms = 0.0;
    double navigation_ms = 0.0;
    double movement_intent_ms = 0.0;
    double local_avoidance_ms = 0.0;
    double unit_flow_ms = 0.0;
    double motion_safety_ms = 0.0;
    double movement_ms = 0.0;
    double combat_ms = 0.0;
    double projectile_ms = 0.0;
    double lifecycle_ms = 0.0;
    std::uint64_t movement_speed_samples = 0;
    double movement_base_speed_sum = 0.0;
    double movement_effective_speed_sum = 0.0;
    double movement_desired_speed_sum = 0.0;
    double movement_steering_speed_sum = 0.0;
    double movement_actual_speed_sum = 0.0;
    std::uint64_t movement_squad_limited = 0;
    std::uint64_t movement_arrive_limited = 0;
    std::uint64_t movement_turn_limited = 0;
    std::uint64_t movement_steering_limited = 0;
    std::uint64_t movement_safe_limited = 0;

    // Navigation hotspot instrumentation (Phase 0). Populated only under
    // GLD_ENABLE_PERFORMANCE_MONITORING; reset every render frame.
    std::uint64_t navigation_astar_calls = 0;
    std::uint64_t navigation_astar_cells_expanded = 0;
    std::uint64_t navigation_clear_segment_calls = 0;
    std::uint64_t navigation_repath_units = 0;
    std::uint64_t navigation_goal_cache_hits = 0;
    std::uint64_t navigation_distinct_goals = 0;
    double navigation_astar_find_ms = 0.0;
    double navigation_astar_find_peak_ms = 0.0;

    void begin_frame() { *this = {}; }
};

struct AoeSquadTrafficRecord {
    entt::entity squad{entt::null};
    glm::vec2 center{0.f};
    glm::vec2 direction{0.f};
    float speed = 0.f;
    float radius = 0.f;
    float member_radius = 0.f;
    std::uint64_t stable_id = 0;
};

struct AoeSquadTrafficIndex {
    std::vector<AoeSquadTrafficRecord> records;
    float maximum_reach = 0.f;
};

struct AoeUnitFlowRecord {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
    entt::entity squad{entt::null};
    std::uint32_t team = 0;
    AoeMovementIntentKind kind = AoeMovementIntentKind::None;
    glm::vec2 position{0.f};
    glm::vec2 radii{0.f};
    glm::vec2 intent_velocity{0.f};
};

struct AoeUnitFlowConflict {
    std::size_t a = 0;
    std::size_t b = 0;
    float time_to_collision = 0.f;
    float closest_distance = 0.f;
};

// Fixed-tick scratch reused by the sweep-and-prune traffic coordinator.
struct AoeUnitFlowIndex {
    std::vector<AoeUnitFlowRecord> records;
    std::vector<AoeUnitFlowConflict> candidates;
    std::vector<AoeUnitFlowConflict> selected;
    std::vector<std::size_t> parents;
    std::vector<std::uint32_t> ranks;
    float maximum_reach = 0.f;
};

using AoeGlobalMotionPlanner = std::function<bool(
    EcsWorld&, std::uint64_t, std::string&)>;

class AoeGlobalMotionPlannerRegistry {
public:
    void bind(std::string id, AoeGlobalMotionPlanner planner) {
        if (id.empty() || !planner)
            throw std::invalid_argument("global motion planner binding is invalid");
        planners_[std::move(id)] = std::move(planner);
    }
    bool contains(std::string_view id) const {
        return planners_.find(std::string(id)) != planners_.end();
    }
    AoeGlobalMotionPlanner* find(std::string_view id) {
        const auto it = planners_.find(std::string(id));
        return it == planners_.end() ? nullptr : &it->second;
    }
    bool erase(std::string_view id) {
        return planners_.erase(std::string(id)) != 0;
    }

private:
    std::unordered_map<std::string, AoeGlobalMotionPlanner> planners_;
};

struct AoeGlobalMotionPlannerDiagnostics {
    std::string requested_backend;
    std::string active_backend;
    std::string fallback_reason;
    std::uint64_t gpu_ticks = 0;
    std::uint64_t cpu_ticks = 0;
    std::uint64_t fallback_ticks = 0;
    std::uint64_t failures = 0;
    std::uint64_t authoritative_corrections = 0;
};

entt::entity spawn_aoe_gameplay_unit(EcsWorld&, const AoeUnitSpawnOptions&);
entt::entity spawn_aoe_gameplay_squad(EcsWorld&, const AoeSquadSpawnOptions&);
bool request_aoe_attack(EcsWorld&, entt::entity attacker, entt::entity target);
bool request_aoe_attack_move(EcsWorld&, entt::entity, glm::vec2 destination);
bool request_aoe_move(EcsWorld&, entt::entity, glm::vec2 destination);
bool request_aoe_stop(EcsWorld&, entt::entity);
bool request_aoe_squad_move(EcsWorld&, entt::entity squad, glm::vec2 destination);
bool request_aoe_squad_attack(EcsWorld&, entt::entity squad, entt::entity target);
bool request_aoe_squad_attack_move(
    EcsWorld&, entt::entity squad, glm::vec2 destination);
bool request_aoe_squad_stop(EcsWorld&, entt::entity squad);
bool set_aoe_squad_formation(
    EcsWorld&, entt::entity squad, AoeFormationType);
bool disband_aoe_gameplay_squad(EcsWorld&, entt::entity squad);
bool set_aoe_unit_facing(EcsWorld&, entt::entity, int direction, int direction_count = 16);
bool set_aoe_unit_health(EcsWorld&, entt::entity, float);
bool set_aoe_unit_level(EcsWorld&, entt::entity, std::uint32_t);

double aoe_action_elapsed_seconds(const AoeActionState&, const AoeGameplayClock&,
                                  const AoeGameplaySettings&);
glm::vec2 aoe_interpolated_position(const AoePosition& current,
                                    const AoePositionHistory* history,
                                    const AoeGameplayClock& clock,
                                    const AoeGameplaySettings& settings);
float aoe_collider_support_radius(const AoeCollider&, glm::vec2 direction);
float aoe_surface_gap(const AoePosition&, const AoeCollider&,
                      const AoePosition&, const AoeCollider&);
void clear_aoe_gameplay_events(EcsWorld&);
void spawn_aoe_gameplay_unit_system(EcsWorld&);
void aoe_gameplay_recycle_system(EcsWorld&);
void aoe_projectile_tick(EcsWorld&, std::uint64_t tick);
void aoe_map_static_obstacle_system(EcsWorld&);
void aoe_dynamic_obstacle_index_system(EcsWorld&);

namespace detail {
void install_aoe_gameplay_base(
    App&, const std::string& definitions_root,
    const AoeGameplaySettings& settings);
void aoe_gameplay_fixed_before_formation(EcsWorld&, std::uint64_t tick);
void aoe_gameplay_formation_fixed_tick(
    EcsWorld&, std::uint64_t tick, bool pass_through);
void aoe_gameplay_fixed_after_formation_before_local(
    EcsWorld&, std::uint64_t tick);
void aoe_gameplay_fixed_after_global(EcsWorld&, std::uint64_t tick);

// Shared low-level engagement operations. Squad target-selection policy lives
// in its own plugin, while unit AttackMove continues to use these primitives.
bool aoe_gameplay_target_valid(
    const entt::registry&, const AoeUnitTarget&);
bool aoe_gameplay_squad_member_valid(
    const entt::registry&, const AoeUnitTarget&);
void aoe_gameplay_clear_active_engagement(
    entt::registry&, entt::entity);
void aoe_gameplay_reset_member_action(
    entt::registry&, entt::entity, std::uint64_t tick,
    bool reset_locomotion = true);
void aoe_gameplay_assign_engagement_approach(
    entt::registry&, entt::entity, const AoeUnitTarget&,
    const AttackDefinition&);
void aoe_gameplay_attack_with_squad_member(
    entt::registry&, entt::entity, const AoeUnitTarget&,
    std::uint64_t tick);
std::optional<AoeUnitTarget>
aoe_gameplay_select_stalled_in_range_target(
    EcsWorld&, entt::entity, const AoeUnitTarget& current,
    AoeTargetAcquisitionType, float attack_range);
}

} // namespace gld::ecs::aoe

#include <aoe/AoeFormation.hpp>
#include <aoe/AoeSquadEngagement.hpp>
#include <aoe/AoeLocalAvoidance.hpp>
#include <aoe/AoeGlobalMotion.hpp>

namespace gld::ecs::aoe {

template<class T>
concept AoeGameplayStaticPlugin = requires(
    App& app, EcsWorld& world, std::uint64_t tick) {
    typename T::phase;
    { T::name } -> std::convertible_to<std::string_view>;
    { T::install(app) } -> std::same_as<void>;
    { T::fixed_tick(world, tick) } -> std::same_as<void>;
};

namespace detail {
template<class Phase, class... Plugins>
inline constexpr std::size_t gameplay_phase_count_v =
    (std::size_t{0} + ... +
     (std::is_same_v<typename Plugins::phase, Phase> ? 1u : 0u));

template<class Phase, class First, class... Rest>
struct GameplayPluginForPhase {
    using type = std::conditional_t<
        std::is_same_v<typename First::phase, Phase>, First,
        typename GameplayPluginForPhase<Phase, Rest...>::type>;
};

template<class Phase, class Last>
struct GameplayPluginForPhase<Phase, Last> {
    using type = std::conditional_t<
        std::is_same_v<typename Last::phase, Phase>, Last, void>;
};

template<class Phase, class... Plugins>
using gameplay_plugin_for_phase_t =
    typename GameplayPluginForPhase<Phase, Plugins...>::type;

template<class SquadEngagementPlugin, class FormationPlugin,
         class LocalAvoidancePlugin,
         class GlobalMotionPlugin>
void aoe_gameplay_fixed_system(EcsWorld& world) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto* time = world.try_resource<Time>();
    if (!time || !(settings.fixed_dt > 0.0)) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
            .fixed_total_ms = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
#endif
        return;
    }
    // Runtime gameplay follows wall time even when the render delta is
    // clamped by TimeSettings::max_delta. Tests and headless callers commonly
    // drive only Time::dt, so retain that deterministic fallback when raw_dt
    // has not been populated.
    const float fixed_source_dt =
        std::isfinite(time->raw_dt) && time->raw_dt > 0.f
            ? time->raw_dt : time->dt;
    clock.accumulator += std::max(0.f, fixed_source_dt);
    clock.ticks_this_frame = 0;
    while (clock.accumulator + 1e-12 >= settings.fixed_dt &&
           clock.ticks_this_frame < settings.max_catchup_ticks) {
        clock.accumulator -= settings.fixed_dt;
        ++clock.tick;
        aoe_gameplay_fixed_before_formation(world, clock.tick);
        SquadEngagementPlugin::fixed_tick(world, clock.tick);
        FormationPlugin::fixed_tick(world, clock.tick);
        aoe_gameplay_fixed_after_formation_before_local(world, clock.tick);
        LocalAvoidancePlugin::fixed_tick(world, clock.tick);
        GlobalMotionPlugin::fixed_tick(world, clock.tick);
        aoe_gameplay_fixed_after_global(world, clock.tick);
        ++clock.ticks_this_frame;
    }
    if (clock.accumulator >= settings.fixed_dt) {
        const double kept = std::fmod(clock.accumulator, settings.fixed_dt);
        clock.dropped_seconds += clock.accumulator - kept;
        clock.accumulator = kept;
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .fixed_total_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}
} // namespace detail

template<AoeGameplayStaticPlugin... Plugins>
struct AoeGameplayDef {
    static_assert(sizeof...(Plugins) > 0,
        "AoeGameplayDef requires at least one static gameplay plugin");
    static_assert(detail::gameplay_phase_count_v<
                      AoeSquadEngagementPhase, Plugins...> == 1,
        "AoeGameplayDef requires exactly one squad-engagement phase plugin");
    static_assert(detail::gameplay_phase_count_v<
                      AoeFormationPhase, Plugins...> == 1,
        "AoeGameplayDef requires exactly one formation phase plugin");
    static_assert(detail::gameplay_phase_count_v<
                      AoeLocalAvoidancePhase, Plugins...> == 1,
        "AoeGameplayDef requires exactly one local-avoidance phase plugin");
    static_assert(detail::gameplay_phase_count_v<
                      AoeGlobalMotionPhase, Plugins...> == 1,
        "AoeGameplayDef requires exactly one global-motion phase plugin");

    using SquadEngagementPlugin = detail::gameplay_plugin_for_phase_t<
        AoeSquadEngagementPhase, Plugins...>;
    using FormationPlugin = detail::gameplay_plugin_for_phase_t<
        AoeFormationPhase, Plugins...>;
    using LocalAvoidancePlugin = detail::gameplay_plugin_for_phase_t<
        AoeLocalAvoidancePhase, Plugins...>;
    using GlobalMotionPlugin = detail::gameplay_plugin_for_phase_t<
        AoeGlobalMotionPhase, Plugins...>;

    std::string definitions_root = "aoe_units";
    AoeGameplaySettings settings{};

    void operator()(App& app) const {
        detail::install_aoe_gameplay_base(app, definitions_root, settings);
        (Plugins::install(app), ...);
        app.add_system(Stage::PreUpdate, [](EcsWorld& world) {
            detail::aoe_gameplay_fixed_system<
                SquadEngagementPlugin, FormationPlugin,
                LocalAvoidancePlugin,
                GlobalMotionPlugin>(world);
        });
    }
};

using AoeGameplayPlugin = AoeGameplayDef<
    AoeFullSquadEngagementPlugin, AoeFullFormationPlugin,
    AoeFullLocalAvoidancePlugin,
    AoeDefaultGlobalMotionPlugin>;

} // namespace gld::ecs::aoe
