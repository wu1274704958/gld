#include <aoe2x/Aoe2xFormation.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2x;

namespace {
struct InvalidGenerator {};
static_assert(!Aoe2xFormationGenerator<InvalidGenerator>);

struct BrokenGenerator {
    static FormationLayout generate(const FormationGenerateContext& context) {
        FormationLayout result;
        for (std::uint32_t i = 0; i < context.count; ++i)
            result.relative_positions.push_back(
                {static_cast<float>(i) * context.cell_size * 3.f, 0.f});
        return result;
    }
};
static_assert(Aoe2xFormationGenerator<BrokenGenerator>);

AoeMapDefinition open_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_formation_test";
    map.width = 40;
    map.height = 30;
    map.tile_size = 1.f;
    map.heights.resize(41u * 31u, 0.f);
    return map;
}

void install(EcsWorld& world) {
    world.resource_or_add<FormationRegistry>()
        .bind<FormationType::CompactSquare, CompactSquareFormation>();
    auto& gameplay = world.resource_or_add<AoeGameplaySettings>();
    gameplay.fixed_dt = 1.0 / 30.0;
    world.resource_or_add<AoeNavigationSettings>().steering_max_acceleration = 6.f;
    world.resource_or_add<Aoe2xPathfindingSettings>() = {8, true};
}

void run_tick(EcsWorld& world, std::uint64_t tick) {
    FormationCommandSystem::run(world, tick);
    Aoe2xPathfindingSystem::run(world, tick);
    FormationSystem::run(world, tick);
    Aoe2xUnitLifecycleSystem::run(world, tick);
}

glm::vec2 rotate_between(
    glm::vec2 value, glm::vec2 original, glm::vec2 current) {
    const float cosine = std::clamp(glm::dot(original, current), -1.f, 1.f);
    const float sine = original.x * current.y - original.y * current.x;
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}
}

int main() {
    for (std::uint32_t count = 1; count <= 32; ++count) {
        const float cell = .8f;
        const auto layout = CompactSquareFormation::generate({count, cell});
        assert(layout.relative_positions.size() == count);
        assert(layout.captain_index < count);
        const auto captain = layout.relative_positions[layout.captain_index];
        for (const auto position : layout.relative_positions)
            assert(captain.y >= position.y);
        for (std::size_t i = 1; i < count; ++i) {
            const auto a = layout.relative_positions[
                (layout.captain_index + i - 1u) % count];
            const auto b = layout.relative_positions[
                (layout.captain_index + i) % count];
            assert(glm::length(b - a) <= cell * std::sqrt(2.f) + 1e-4f);
        }
    }

    EcsWorld invalid_world;
    invalid_world.resource_or_add<FormationRegistry>()
        .bind<FormationType::CompactSquare, BrokenGenerator>();
    const auto invalid_squad = spawn_aoe2x_formation(invalid_world,
        FormationSpawnOptions{4, {5.f, 5.f}});
    SpawnFormationSystem::run(invalid_world, 0);
    assert(invalid_world.reg().get<FormationSpawnState>(invalid_squad).status ==
           FormationSpawnStatus::Failed);
    assert(!invalid_world.reg().all_of<SquadInfo>(invalid_squad));
    FormationSpawnOptions bad_direction;
    bad_direction.count = 2;
    bad_direction.forward = {0.f, 0.f};
    assert(spawn_aoe2x_formation(invalid_world, bad_direction) == entt::null);
    bad_direction.forward = {
        std::numeric_limits<float>::quiet_NaN(), 0.f};
    assert(spawn_aoe2x_formation(invalid_world, bad_direction) == entt::null);

    EcsWorld directed_world;
    install(directed_world);
    FormationSpawnOptions directed_options;
    directed_options.count = 4;
    directed_options.center = {10.f, 10.f};
    directed_options.forward = {0.f, 2.f};
    const auto directed_squad = spawn_aoe2x_formation(
        directed_world, directed_options);
    SpawnFormationSystem::run(directed_world, 0);
    const auto& directed_info =
        directed_world.reg().get<SquadInfo>(directed_squad);
    for (const auto unit : directed_info.units) {
        const auto& direction =
            directed_world.reg().get<UnitFormationDirection>(unit);
        const auto& unit_direction =
            directed_world.reg().get<AoeDirection>(unit).value;
        assert(glm::length(direction.original - glm::vec2(0.f, 1.f)) < 1e-5f);
        assert(glm::length(unit_direction - direction.original) < 1e-5f);
    }
    assert(glm::length(directed_world.reg().get<AoePosition>(
        directed_info.captain).value - directed_options.center) < 1e-5f);
    assert(directed_world.reg().get<AoePosition>(
        directed_info.units[1]).value.y < directed_options.center.y);

    // Formation geometry is driven only by captain rotation. A follower's
    // facing may lag behind without changing the target of the next link.
    auto& directed_navigation =
        directed_world.resource<AoeNavigationSettings>();
    directed_navigation.steering_max_turn_radians_per_second = .6f;
    const auto directed_captain = directed_info.captain;
    const auto& directed_captain_formation_direction =
        directed_world.reg().get<UnitFormationDirection>(directed_captain);
    auto& directed_captain_direction =
        directed_world.reg().get<AoeDirection>(directed_captain).value;
    directed_captain_direction = {-1.f, 0.f};
    const auto first_follower = directed_info.units[1];
    const auto second_follower = directed_info.units[2];
    const auto third_follower = directed_info.units[3];
    const auto& first_member =
        directed_world.reg().get<UnitSquadInfo>(first_follower);
    const auto& second_member =
        directed_world.reg().get<UnitSquadInfo>(second_follower);
    const auto& third_member =
        directed_world.reg().get<UnitSquadInfo>(third_follower);
    const glm::vec2 first_target = directed_world.reg().get<AoePosition>(
        directed_captain).value - rotate_between(
            first_member.followed_relative_to_self,
            directed_captain_formation_direction.original,
            directed_captain_direction);
    directed_world.reg().get<AoePosition>(first_follower).value = first_target;
    directed_world.reg().get<AoeDirection>(first_follower).value =
        {0.f, 1.f};
    const glm::vec2 second_target = first_target - rotate_between(
        second_member.followed_relative_to_self,
        directed_captain_formation_direction.original,
        directed_captain_direction);
    const glm::vec2 displaced_second_position =
        second_target + glm::vec2(2.f, 0.f);
    directed_world.reg().get<AoePosition>(second_follower).value =
        displaced_second_position;
    directed_world.reg().get<AoeDirection>(second_follower).value =
        {0.f, 1.f};
    const glm::vec2 third_target = second_target - rotate_between(
        third_member.followed_relative_to_self,
        directed_captain_formation_direction.original,
        directed_captain_direction);
    FormationSystem::run(directed_world, 1);
    assert(glm::length(directed_world.reg().get<UnitTargetPosition>(
        second_follower).value - second_target) < 1e-5f);
    assert(glm::length(directed_world.reg().get<UnitTargetPosition>(
        third_follower).value - third_target) < 1e-5f);
    assert(directed_world.reg().get<UnitFormationMotionState>(
        first_follower).phase ==
        UnitFormationMotionPhase::HoldingSlot);
    assert(directed_world.reg().get<UnitFormationMotionState>(
        second_follower).phase ==
        UnitFormationMotionPhase::TurningToSlot);

    // A captain turn changes slot targets but does not immediately rotate a
    // follower that is already in its slot. A displaced follower with a large
    // travel-angle error still uses the explicit turn-in-place phase.
    const float turn_step = .6f / 30.f;
    const glm::vec2 expected_second_direction{
        -std::sin(turn_step), std::cos(turn_step)};
    assert(glm::length(directed_world.reg().get<AoeDirection>(
        first_follower).value - glm::vec2(0.f, 1.f)) < 1e-5f);
    assert(glm::length(directed_world.reg().get<AoeLocomotionState>(
        first_follower).velocity) < 1e-5f);
    const auto second_velocity = directed_world.reg().get<AoeLocomotionState>(
        second_follower).velocity;
    const auto second_direction =
        directed_world.reg().get<AoeDirection>(second_follower).value;
    assert(glm::length(second_velocity) < 1e-5f);
    assert(glm::length(second_direction - expected_second_direction) < 1e-5f);
    assert(glm::length(directed_world.reg().get<AoePosition>(
        second_follower).value - displaced_second_position) < 1e-5f);
    for (std::uint64_t tick = 2; tick <= 85; ++tick) {
        FormationSystem::run(directed_world, tick);
        assert(glm::length(directed_world.reg().get<AoeDirection>(
            first_follower).value - glm::vec2(0.f, 1.f)) < 1e-5f);
        assert(directed_world.reg().get<UnitFormationMotionState>(
            first_follower).phase ==
            UnitFormationMotionPhase::HoldingSlot);
    }
    const auto moving_direction =
        directed_world.reg().get<AoeDirection>(second_follower).value;
    const auto moving_velocity = directed_world.reg().get<AoeLocomotionState>(
        second_follower).velocity;
    assert(glm::length(moving_velocity) > 1e-5f);
    assert(glm::dot(moving_velocity, moving_direction) > 0.f);
    assert(glm::length(moving_velocity / glm::length(moving_velocity) -
        moving_direction) < 1e-5f);

    // Small slot errors may translate freely without changing a substantially
    // different facing. Outside the configured radius turn-first resumes.
    auto& third_motion_state =
        directed_world.reg().get<UnitFormationMotionState>(third_follower);
    third_motion_state.phase = UnitFormationMotionPhase::TurningToSlot;
    directed_world.reg().get<AoePosition>(third_follower).value =
        third_target + glm::vec2(.08f, 0.f);
    directed_world.reg().get<AoeDirection>(third_follower).value = {0.f, 1.f};
    directed_world.reg().get<AoeLocomotionState>(third_follower).velocity =
        {0.f, 0.f};
    FormationSystem::run(directed_world, 86);
    assert(third_motion_state.phase ==
        UnitFormationMotionPhase::HoldingSlot);
    assert(glm::length(directed_world.reg().get<AoeDirection>(
        third_follower).value - glm::vec2(0.f, 1.f)) < 1e-5f);
    assert(directed_world.reg().get<AoeLocomotionState>(
        third_follower).velocity.x < 0.f);
    directed_world.reg().get<AoePosition>(third_follower).value =
        third_target + glm::vec2(.13f, 0.f);
    FormationSystem::run(directed_world, 87);
    assert(third_motion_state.phase ==
        UnitFormationMotionPhase::TurningToSlot);

    // A small travel-angle correction redirects immediately and may use this
    // unit's own aligned speed multiplier (1.1 means 110% maximum speed).
    auto& first_motion_state =
        directed_world.reg().get<UnitFormationMotionState>(first_follower);
    first_motion_state.phase = UnitFormationMotionPhase::TurningToSlot;
    const glm::vec2 small_turn_direction{
        -std::cos(.05f), -std::sin(.05f)};
    directed_world.reg().get<AoePosition>(first_follower).value =
        first_target - small_turn_direction * 2.f;
    directed_world.reg().get<AoeLocomotionState>(first_follower).velocity =
        {0.f, 0.f};
    directed_world.reg().get<AoeDirection>(first_follower).value =
        directed_captain_direction;
    FormationSystem::run(directed_world, 88);
    const auto redirected_direction =
        directed_world.reg().get<AoeDirection>(first_follower).value;
    const auto boosted_velocity =
        directed_world.reg().get<AoeLocomotionState>(first_follower).velocity;
    const float first_base_speed =
        directed_world.reg().get<AoeMovement>(first_follower).speed;
    assert(std::abs(first_member.aligned_speed_multiplier - 1.1f) < 1e-6f);
    assert(first_motion_state.phase ==
        UnitFormationMotionPhase::MovingToSlot);
    assert(glm::length(redirected_direction - small_turn_direction) < 1e-5f);
    assert(std::abs(glm::length(boosted_velocity) -
        first_base_speed * first_member.aligned_speed_multiplier) < 1e-5f);
    assert(std::abs(directed_world.reg().get<AoeLocomotionState>(
        first_follower).effective_max_speed -
        first_base_speed * first_member.aligned_speed_multiplier) < 1e-5f);

    // A larger travel-angle error still consumes the normal turn budget and
    // does not receive the aligned speed reserve.
    const glm::vec2 large_turn_direction{
        -std::cos(.13f), -std::sin(.13f)};
    first_motion_state.phase = UnitFormationMotionPhase::MovingToSlot;
    directed_world.reg().get<AoePosition>(first_follower).value =
        first_target - large_turn_direction * 2.f;
    directed_world.reg().get<AoeLocomotionState>(first_follower).velocity =
        {0.f, 0.f};
    directed_world.reg().get<AoeDirection>(first_follower).value =
        directed_captain_direction;
    FormationSystem::run(directed_world, 89);
    assert(first_motion_state.phase ==
        UnitFormationMotionPhase::TurningToSlot);
    assert(glm::length(directed_world.reg().get<AoeLocomotionState>(
        first_follower).velocity) < 1e-6f);
    assert(std::abs(directed_world.reg().get<AoeLocomotionState>(
        first_follower).effective_max_speed - first_base_speed) < 1e-5f);

    // Inside the configurable free-translation radius, a correction behind
    // the unit may move backward without first making an unnecessary U-turn.
    auto& directed_formation_settings =
        directed_world.resource_or_add<FormationSettings>();
    directed_formation_settings.slot_free_translation_radius = .08f;
    first_motion_state.phase = UnitFormationMotionPhase::TurningToSlot;
    first_motion_state.locked_move_direction = {1.f, 0.f};
    directed_world.reg().get<AoePosition>(first_follower).value =
        first_target - glm::vec2(.06f, 0.f);
    directed_world.reg().get<AoeLocomotionState>(first_follower).velocity =
        {0.f, 0.f};
    directed_world.reg().get<AoeDirection>(first_follower).value =
        directed_captain_direction;
    FormationSystem::run(directed_world, 90);
    const auto backward_velocity =
        directed_world.reg().get<AoeLocomotionState>(first_follower).velocity;
    assert(first_motion_state.phase ==
        UnitFormationMotionPhase::HoldingSlot);
    assert(glm::length(directed_world.reg().get<AoeDirection>(
        first_follower).value - directed_captain_direction) < 1e-6f);
    assert(glm::dot(backward_velocity, directed_captain_direction) < 0.f);

    // Reducing the radius makes the same-sized correction use normal
    // turn-first movement again, proving that the exception is configurable.
    directed_formation_settings.slot_free_translation_radius = .03f;
    first_motion_state.phase = UnitFormationMotionPhase::TurningToSlot;
    first_motion_state.locked_move_direction = {1.f, 0.f};
    directed_world.reg().get<AoePosition>(first_follower).value =
        first_target - glm::vec2(.04f, 0.f);
    directed_world.reg().get<AoeLocomotionState>(first_follower).velocity =
        {0.f, 0.f};
    directed_world.reg().get<AoeDirection>(first_follower).value =
        directed_captain_direction;
    FormationSystem::run(directed_world, 91);
    assert(first_motion_state.phase ==
        UnitFormationMotionPhase::TurningToSlot);
    assert(glm::length(directed_world.reg().get<AoeLocomotionState>(
        first_follower).velocity) < 1e-6f);

    EcsWorld world;
    install(world);
    world.add_resource<AoeLogicMap>(open_map());
    FormationSpawnOptions options;
    options.count = 12;
    options.center = {5.f, 15.f};
    options.spacing = .25f;
    options.unit_radius = .3f;
    options.movement_speed = 3.f;
    const auto squad = spawn_aoe2x_formation(world, options);
    assert(squad != entt::null);
    SpawnFormationSystem::run(world, 0);
    assert(world.reg().get<FormationSpawnState>(squad).status ==
           FormationSpawnStatus::Ready);
    const auto& squad_info = world.reg().get<SquadInfo>(squad);
    assert(squad_info.units.size() == options.count);
    assert(squad_info.captain == squad_info.units.front());
    assert(world.reg().all_of<SquadCaptainInfo>(squad_info.captain));
    for (std::size_t i = 0; i < squad_info.units.size(); ++i) {
        const auto unit = squad_info.units[i];
        const bool has_formation_components = world.reg().all_of<
            AoePosition, AoeCollider, AoeMovement, AoeLocomotionState,
            AoeDirection, UnitTargetPosition, UnitSquadInfo,
            UnitFormationDirection, UnitFormationMotionState>(unit);
        assert(has_formation_components);
        const auto& member = world.reg().get<UnitSquadInfo>(unit);
        assert(member.squad == squad);
        assert(member.followed == (i ? squad_info.units[i - 1u] : entt::null));
        assert(std::abs(member.aligned_speed_multiplier - 1.1f) < 1e-6f);
    }

    const glm::vec2 first_destination{22.f, 15.f};
    assert(request_aoe2x_formation_attack_move(
        world, squad, first_destination));
    glm::vec2 previous_velocity{0.f};
    for (std::uint64_t tick = 1; tick <= 40; ++tick) {
        run_tick(world, tick);
        const auto velocity = world.reg().get<AoeLocomotionState>(
            squad_info.captain).velocity;
        assert(glm::length(velocity - previous_velocity) <=
               6.f / 30.f + 1e-3f);
        previous_velocity = velocity;
        if (glm::length(velocity) > 1e-5f) {
            const auto direction = world.reg().get<AoeDirection>(
                squad_info.captain).value;
            assert(glm::dot(velocity / glm::length(velocity), direction) >
                .9999f);
        }
    }
    const glm::vec2 final_destination{
        world.reg().get<AoePosition>(squad_info.captain).value.x, 22.f};
    assert(request_aoe2x_formation_attack_move(
        world, squad, final_destination));
    for (std::uint64_t tick = 41; tick <= 900; ++tick) {
        run_tick(world, tick);
        if (world.reg().get<FormationAttackMove>(squad).status ==
            FormationAttackMoveStatus::Completed)
            break;
    }
    const auto& completed = world.reg().get<FormationAttackMove>(squad);
    assert(completed.status == FormationAttackMoveStatus::Completed);
    assert(glm::length(world.reg().get<AoePosition>(squad_info.captain).value -
                       final_destination) <= 1e-4f);
    for (std::uint64_t tick = 901; tick <= 1500; ++tick)
        run_tick(world, tick);
    const auto& captain_formation_direction =
        world.reg().get<UnitFormationDirection>(
        squad_info.captain);
    const auto& captain_direction =
        world.reg().get<AoeDirection>(squad_info.captain).value;
    assert(captain_direction.y > .9f);
    for (std::size_t i = 1; i < squad_info.units.size(); ++i) {
        const auto unit = squad_info.units[i];
        const auto& member = world.reg().get<UnitSquadInfo>(unit);
        const glm::vec2 actual =
            world.reg().get<AoePosition>(member.followed).value -
            world.reg().get<AoePosition>(unit).value;
        const glm::vec2 expected = rotate_between(
            member.followed_relative_to_self,
            captain_formation_direction.original, captain_direction);
        const auto& unit_direction =
            world.reg().get<AoeDirection>(unit).value;
        assert(std::abs(glm::length(unit_direction) - 1.f) < 1e-4f);
        const float link_error = glm::length(actual - expected);
        assert(link_error < .08f);
    }

    // A mid-chain loss used to fail the whole order outright. The chain now
    // absorbs it: the successor re-hooks onto the nearest living unit ahead
    // and keeps its own link offset, so the tail closes up into the gap.
    const auto removed = squad_info.units[5];
    const auto predecessor = squad_info.units[4];
    const auto successor = squad_info.units[6];
    const glm::vec2 kept =
        world.reg().get<UnitSquadInfo>(successor).followed_relative_to_self;
    const std::size_t before_loss = squad_info.units.size();
    kill_aoe2x_formation_unit(world, removed);
    FormationSystem::run(world, 1021);
    assert(world.reg().get<FormationAttackMove>(squad).status ==
           FormationAttackMoveStatus::Completed);
    assert(squad_info.units.size() == before_loss - 1u);
    assert(std::find(squad_info.units.begin(), squad_info.units.end(),
                     removed) == squad_info.units.end());
    const auto& relinked = world.reg().get<UnitSquadInfo>(successor);
    assert(relinked.followed == predecessor);
    assert(glm::length(relinked.followed_relative_to_self - kept) < 1e-5f);
    // The corpse is handed over to the lifecycle system and reclaimed on a
    // fixed timer once the formation is done reading it.
    assert(world.reg().get<Aoe2xUnitState>(removed).lifecycle ==
           Aoe2xUnitLifecycle::Released);
    for (std::uint64_t tick = 1022; tick <= 1022 + kAoe2xReleaseTicks; ++tick)
        run_tick(world, tick);
    assert(!world.reg().valid(removed));

    EcsWorld detour_world;
    install(detour_world);
    auto detour_map_definition = open_map();
    AoeStaticObstacleDesc detour_wall;
    detour_wall.shape = AoeStaticObstacleShape::Aabb;
    detour_wall.center = {20.f, 15.f};
    detour_wall.half_extents = {.75f, 10.f};
    detour_map_definition.static_obstacles.push_back(detour_wall);
    detour_world.add_resource<AoeLogicMap>(std::move(detour_map_definition));
    FormationSpawnOptions detour_options;
    detour_options.count = 5;
    detour_options.center = {4.f, 15.f};
    detour_options.movement_speed = 4.f;
    const auto detour_squad = spawn_aoe2x_formation(
        detour_world, detour_options);
    SpawnFormationSystem::run(detour_world, 0);
    const auto detour_captain =
        detour_world.reg().get<SquadInfo>(detour_squad).captain;
    const glm::vec2 detour_destination{36.f, 15.f};
    assert(request_aoe2x_formation_attack_move(
        detour_world, detour_squad, detour_destination));
    const auto& detour_map = detour_world.resource<AoeLogicMap>();
    for (std::uint64_t tick = 1; tick <= 1800; ++tick) {
        const auto previous = detour_world.reg().get<AoePosition>(
            detour_captain).value;
        run_tick(detour_world, tick);
        const auto current = detour_world.reg().get<AoePosition>(
            detour_captain).value;
        assert(detour_map.static_safe_fraction(
            previous, current, {.3f, .3f}) >= .999f);
        if (detour_world.reg().get<FormationAttackMove>(detour_squad).status ==
            FormationAttackMoveStatus::Completed)
            break;
    }
    assert(detour_world.reg().get<FormationAttackMove>(detour_squad).status ==
           FormationAttackMoveStatus::Completed);

    EcsWorld blocked_world;
    install(blocked_world);
    auto blocked_map = open_map();
    AoeStaticObstacleDesc obstacle;
    obstacle.shape = AoeStaticObstacleShape::Circle;
    obstacle.center = {15.f, 15.f};
    obstacle.radius = 2.f;
    blocked_map.static_obstacles.push_back(obstacle);
    blocked_world.add_resource<AoeLogicMap>(std::move(blocked_map));
    FormationSpawnOptions blocked_options;
    blocked_options.count = 3;
    blocked_options.center = {5.f, 15.f};
    const auto blocked_squad = spawn_aoe2x_formation(
        blocked_world, blocked_options);
    SpawnFormationSystem::run(blocked_world, 0);
    const glm::vec2 blocked_destination{15.f, 15.f};
    assert(request_aoe2x_formation_attack_move(
        blocked_world, blocked_squad, blocked_destination));
    run_tick(blocked_world, 1);
    assert(blocked_world.reg().get<FormationAttackMove>(blocked_squad).status ==
           FormationAttackMoveStatus::Failed);

    // Losses mid-march: the surviving slots must not move, and a fallen
    // captain must hand its route over rather than trigger a fresh search.
    EcsWorld loss_world;
    install(loss_world);
    AoeMapDefinition loss_map = open_map();
    AoeStaticObstacleDesc loss_wall;
    loss_wall.shape = AoeStaticObstacleShape::Aabb;
    loss_wall.center = {20.f, 12.f};
    loss_wall.half_extents = {.75f, 8.f};
    loss_map.static_obstacles.push_back(loss_wall);
    loss_world.add_resource<AoeLogicMap>(std::move(loss_map));
    FormationSpawnOptions loss_options;
    loss_options.count = 9;
    loss_options.center = {5.f, 15.f};
    loss_options.movement_speed = 4.f;
    const auto loss_squad = spawn_aoe2x_formation(loss_world, loss_options);
    SpawnFormationSystem::run(loss_world, 0);
    const auto& loss_info = loss_world.reg().get<SquadInfo>(loss_squad);
    assert(request_aoe2x_formation_attack_move(
        loss_world, loss_squad, glm::vec2{34.f, 15.f}));
    for (std::uint64_t tick = 1; tick <= 60; ++tick)
        run_tick(loss_world, tick);
    assert(loss_world.reg().get<FormationAttackMove>(loss_squad).status ==
           FormationAttackMoveStatus::Running);

    // Splicing must leave the survivors' own link offsets untouched: each one
    // simply re-hooks onto the nearest living unit ahead, and that is what
    // closes the gap.
    const auto lost_middle = loss_info.units[3];
    const auto lost_predecessor = loss_info.units[2];
    const auto lost_successor = loss_info.units[4];
    std::vector<std::pair<entt::entity, glm::vec2>> tail_offsets;
    for (std::size_t i = 4; i < loss_info.units.size(); ++i)
        tail_offsets.emplace_back(loss_info.units[i],
            loss_world.reg().get<UnitSquadInfo>(
                loss_info.units[i]).followed_relative_to_self);
    const glm::vec2 vacated =
        loss_world.reg().get<UnitTargetPosition>(lost_middle).value;
    const glm::vec2 successor_before =
        loss_world.reg().get<UnitTargetPosition>(lost_successor).value;
    kill_aoe2x_formation_unit(loss_world, lost_middle);
    FormationSystem::run(loss_world, 61);
    for (const auto& [unit, offset] : tail_offsets)
        assert(glm::length(offset - loss_world.reg().get<UnitSquadInfo>(unit)
            .followed_relative_to_self) < 1e-5f);
    assert(loss_world.reg().get<UnitSquadInfo>(lost_successor).followed ==
           lost_predecessor);
    // The successor takes over the slot the casualty left instead of holding
    // station, so the formation closes ranks rather than keeping a hole.
    const glm::vec2 successor_after =
        loss_world.reg().get<UnitTargetPosition>(lost_successor).value;
    assert(glm::length(successor_after - vacated) <
           glm::length(successor_before - vacated) * .25f);

    const auto& diagnostics =
        loss_world.resource_or_add<Aoe2xPathfindingDiagnostics>();
    const auto fallen_captain = loss_info.captain;
    const auto heir = loss_info.units[1];
    const auto& fallen_motion =
        loss_world.reg().get<FormationMotionState>(fallen_captain);
    const std::size_t inherited_index = fallen_motion.waypoint_index;
    const std::size_t inherited_waypoints =
        loss_world.reg().get<Aoe2xRoutePlan>(fallen_captain).waypoints.size();
    const glm::vec2 fallen_position =
        loss_world.reg().get<AoePosition>(fallen_captain).value;
    const std::uint64_t queries_before = diagnostics.queries;
    kill_aoe2x_formation_unit(loss_world, fallen_captain);
    FormationSystem::run(loss_world, 62);
    assert(loss_info.captain == heir);
    assert(loss_world.reg().all_of<SquadCaptainInfo>(heir));
    assert(!loss_world.reg().all_of<SquadCaptainInfo>(fallen_captain));
    assert(loss_world.reg().get<UnitSquadInfo>(heir).followed == entt::null);
    const auto& heir_route = loss_world.reg().get<Aoe2xRoutePlan>(heir);
    const auto& heir_motion =
        loss_world.reg().get<FormationMotionState>(heir);
    assert(heir_route.waypoints.size() ==
           inherited_waypoints - inherited_index + 1u);
    // The route is rebuilt from the cursor so the consumed prefix cannot be
    // mistaken for the segment the successor is currently on.
    assert(heir_motion.waypoint_index == 0);
    // The rejoin point is the exact spot the captain fell on, put ahead of the
    // remaining route so the successor retraces a stretch already proven clear.
    assert(glm::length(heir_route.waypoints[0] - fallen_position) < 1e-4f);
    assert(!loss_world.reg().all_of<Aoe2xRoutePlan>(fallen_captain));
    assert(diagnostics.queries == queries_before);
    for (std::uint64_t tick = 63; tick <= 400; ++tick) {
        run_tick(loss_world, tick);
        if (loss_world.reg().get<FormationAttackMove>(loss_squad).status !=
            FormationAttackMoveStatus::Running)
            break;
    }
    // Never re-issued a destination, so the march finished on the inherited
    // polyline alone.
    assert(loss_world.reg().get<FormationAttackMove>(loss_squad).status ==
           FormationAttackMoveStatus::Completed);
    assert(diagnostics.queries == queries_before);

    // Losing everyone is the only remaining way to fail an order.
    while (!loss_info.units.empty()) {
        kill_aoe2x_formation_unit(loss_world, loss_info.units.front());
        FormationSystem::run(loss_world, 401);
    }
    assert(request_aoe2x_formation_attack_move(
               loss_world, loss_squad, glm::vec2{10.f, 15.f}) == false);
    return 0;
}
