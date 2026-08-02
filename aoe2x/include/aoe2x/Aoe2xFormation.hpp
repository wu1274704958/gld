#pragma once

#include <concepts>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <aoe/AoeGameplay.hpp>
#include <aoe2x/Aoe2xNavigation.hpp>

namespace gld::ecs::aoe2x {

enum class FormationType : std::uint8_t { CompactSquare };

struct FormationGenerateContext {
    std::uint32_t count = 0;
    float cell_size = 1.f;
};

struct FormationLayout {
    // Entries are in follow-chain order after captain_index, wrapping once.
    std::vector<glm::vec2> relative_positions;
    std::size_t captain_index = 0;
};

template<class T>
concept Aoe2xFormationGenerator = requires(const FormationGenerateContext& context) {
    { T::generate(context) } -> std::same_as<FormationLayout>;
};

struct CompactSquareFormation {
    static FormationLayout generate(const FormationGenerateContext&);
};
static_assert(Aoe2xFormationGenerator<CompactSquareFormation>);

class FormationRegistry {
public:
    using GenerateFn = FormationLayout (*)(const FormationGenerateContext&);

    template<FormationType Type, Aoe2xFormationGenerator Generator>
    void bind() { bind_erased(Type, &Generator::generate); }

    bool contains(FormationType) const;
    FormationLayout generate(
        FormationType, const FormationGenerateContext&) const;

private:
    void bind_erased(FormationType, GenerateFn);
    std::unordered_map<FormationType, GenerateFn> entries_;
};

enum class FormationSpawnStatus : std::uint8_t { Pending, Ready, Failed };
enum class FormationAttackMoveStatus : std::uint8_t {
    Idle, Running, Completed, Failed
};

struct FormationSpawnOptions {
    std::uint32_t count = 0;
    glm::vec2 center{0.f};
    FormationType formation = FormationType::CompactSquare;
    float spacing = .2f;
    float unit_radius = .3f;
    float movement_speed = 3.f;
    glm::vec2 forward{1.f, 0.f};
};

struct FormationSpawnRequest { FormationSpawnOptions options; };
struct FormationSpawnState {
    FormationSpawnStatus status = FormationSpawnStatus::Pending;
};

struct SquadInfo {
    FormationType formation = FormationType::CompactSquare;
    std::vector<entt::entity> units;
    entt::entity captain{entt::null};
};

struct UnitSquadInfo {
    entt::entity squad{entt::null};
    entt::entity followed{entt::null};
    // World-space vector from this unit to the entity it follows.
    glm::vec2 followed_relative_to_self{0.f};
};

struct UnitFormationDirection {
    glm::vec2 original{1.f, 0.f};
};

enum class UnitFormationMotionPhase : std::uint8_t {
    TurningToSlot,
    MovingToSlot,
    AligningWithCaptain
};

struct UnitFormationMotionState {
    UnitFormationMotionPhase phase =
        UnitFormationMotionPhase::AligningWithCaptain;
    glm::vec2 locked_move_direction{1.f, 0.f};
};

struct SquadCaptainInfo { entt::entity squad{entt::null}; };
struct UnitTargetPosition { glm::vec2 value{0.f}; };

struct FormationAttackMove {
    glm::vec2 destination{0.f};
    FormationAttackMoveStatus status = FormationAttackMoveStatus::Idle;
    std::uint64_t revision = 0;
};

struct FormationMotionState {
    std::size_t waypoint_index = 0;
    glm::vec2 route_start{0.f};
    std::uint64_t command_revision = 0;
};

struct FormationSettings {
    float waypoint_radius = .08f;
    float arrival_radius = .05f;
    float slot_exit_radius = .12f;
    float movement_reorient_radians = .12f;
    float path_lookahead = .75f;
    float follower_response_seconds = .22f;
    // Followers need some speed reserve to close slot errors while the
    // captain is already moving at its own maximum speed.
    float follower_catchup_speed_multiplier = 1.35f;
};

struct FormationAttackMoveCommand {
    entt::entity squad{entt::null};
    glm::vec2 destination{0.f};
};
struct FormationCommands { std::vector<FormationAttackMoveCommand> queue; };

entt::entity spawn_aoe2x_formation(
    EcsWorld&, const FormationSpawnOptions&);
bool request_aoe2x_formation_attack_move(
    EcsWorld&, entt::entity squad, glm::vec2 destination);

struct SpawnFormationSystem {
    using ReadOnlyComponents = Aoe2xComponentList<>;
    using WriteOnlyComponents = Aoe2xComponentList<
        SquadInfo, UnitSquadInfo, SquadCaptainInfo, UnitTargetPosition,
        UnitFormationDirection, UnitFormationMotionState,
        aoe::AoePositionHistory, aoe::AoeCollider, aoe::AoeMovement,
        aoe::AoeLocomotionState, aoe::AoeDirection, FormationAttackMove>;
    using ReadWriteComponents = Aoe2xComponentList<
        FormationSpawnRequest, FormationSpawnState, aoe::AoePosition>;
    static constexpr std::string_view name = "aoe2x_spawn_formation";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Prepare;
    static void run(EcsWorld&, std::uint64_t);
};

struct FormationCommandSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        SquadInfo, FormationSpawnState, SquadCaptainInfo,
        aoe::AoePosition, aoe::AoeCollider>;
    using WriteOnlyComponents = Aoe2xComponentList<
        Aoe2xNavigationDestination, Aoe2xRoutePlan, FormationMotionState>;
    using ReadWriteComponents = Aoe2xComponentList<FormationAttackMove>;
    static constexpr std::string_view name = "aoe2x_formation_command";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Command;
    static void run(EcsWorld&, std::uint64_t);
};

struct FormationSystem {
    using ReadOnlyComponents = Aoe2xComponentList<
        SquadInfo, FormationSpawnState, UnitSquadInfo, SquadCaptainInfo,
        UnitFormationDirection, aoe::AoeMovement, aoe::AoeCollider>;
    using WriteOnlyComponents = Aoe2xComponentList<aoe::AoePositionHistory>;
    using ReadWriteComponents = Aoe2xComponentList<
        aoe::AoePosition, aoe::AoeLocomotionState, UnitTargetPosition,
        aoe::AoeDirection, UnitFormationMotionState,
        FormationAttackMove, FormationMotionState, Aoe2xRoutePlan,
        Aoe2xNavigationDestination>;
    static constexpr std::string_view name = "aoe2x_formation";
    static constexpr Stage app_stage = Stage::PreUpdate;
    static constexpr Aoe2xGameplayPhase phase = Aoe2xGameplayPhase::Movement;
    static void run(EcsWorld&, std::uint64_t);
};

static_assert(Aoe2xGameplaySystem<SpawnFormationSystem>);
static_assert(Aoe2xGameplaySystem<FormationCommandSystem>);
static_assert(Aoe2xGameplaySystem<FormationSystem>);

} // namespace gld::ecs::aoe2x
