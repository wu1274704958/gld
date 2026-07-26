#pragma once

#include <cstdint>
#include <concepts>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <ecs/App.hpp>
#include <ecs/Components.hpp>
#include <ecs/Events.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe {

enum class AttackMode { Melee, Projectile };
enum class UnitState { Idle, Moving, Attacking, Dying, Disappearing };

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
    std::string strategy_id = "nearest_enemy";
    float radius = 6.f;
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
struct AoeCollider { float radius_x = 0.f; float radius_y = 0.f; float height = 0.f; };
struct AoePosition { glm::vec2 value{0.f}; };
// Tick-start authoritative position used only to interpolate presentation.
// Gameplay systems always read AoePosition directly.
struct AoePositionHistory { glm::vec2 previous{0.f}; };
struct AoeMovement { float speed = 1.f; };
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
struct AoeNavigationPath {
    std::vector<glm::vec2> waypoints;
    std::size_t current = 0;
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

struct AoeTargetAcquisitionContext {
    entt::entity seeker{entt::null};
    glm::vec2 origin{0.f};
    float radius = 0.f;
    std::uint32_t seeker_team = 0;
};

template<class T>
concept AoeTargetAcquisitionStrategy = requires(
    const EcsWorld& world, const AoeTargetAcquisitionContext& context) {
    { T::select(world, context) }
        -> std::same_as<std::optional<AoeUnitTarget>>;
};

class AoeTargetAcquisitionRegistry {
public:
    using SelectFn = std::optional<AoeUnitTarget> (*)(
        const EcsWorld&, const AoeTargetAcquisitionContext&);

    template<AoeTargetAcquisitionStrategy T>
    void bind(std::string id) { bind_erased(std::move(id), &T::select); }

    bool contains(std::string_view id) const;
    std::optional<AoeUnitTarget> select(
        std::string_view id, const EcsWorld&,
        const AoeTargetAcquisitionContext&) const;

private:
    void bind_erased(std::string id, SelectFn function);
    std::unordered_map<std::string, SelectFn> entries_;
};

struct NearestEnemyAcquisitionStrategy {
    static std::optional<AoeUnitTarget> select(
        const EcsWorld&, const AoeTargetAcquisitionContext&);
};

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
    std::string acquisition_strategy_id = "nearest_enemy";
    float acquisition_radius = 6.f;
};

enum class AoeSquadSpawnStatus { Pending, Ready, Partial, Failed, Empty };
enum class AoeSquadOrderType { Idle, MoveTo, AttackTarget, AttackMove, Stop };
enum class AoeSquadPhase { Forming, Moving, Engaging, Regrouping, Idle, Empty, Failed };

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
};
struct AoeSquadCombatSettings {
    std::string acquisition_strategy_id = "nearest_enemy";
    float acquisition_radius = 6.f;
};
struct AoeSquadOrder {
    AoeSquadOrderType type = AoeSquadOrderType::Idle;
    glm::vec2 destination{0.f};
    AoeUnitTarget target{};
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
};

struct AoeGameplayPlugin {
    std::string definitions_root = "aoe_units";
    AoeGameplaySettings settings{};
    void operator()(App& app) const;
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
void aoe_gameplay_fixed_system(EcsWorld&);
void aoe_gameplay_recycle_system(EcsWorld&);
void aoe_projectile_tick(EcsWorld&, std::uint64_t tick);

} // namespace gld::ecs::aoe
