#include <cassert>
#include <cmath>
#include <memory>

#include <aoe2_gameplay/Aoe2GameplayBridge.hpp>
#include <ecs/systems/TransformSystem.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2;
using namespace gld::ecs::aoe2_gameplay;

#undef assert
#define assert(...) do { if (!(__VA_ARGS__)) std::abort(); } while (false)

namespace {
bool near(float actual, float expected, float epsilon = 1e-5f) {
    return std::abs(actual - expected) <= epsilon;
}
}

int main() {
    EcsWorld world;
    register_transform_lifecycle(world);
    world.add_resource<AoeGameplayClock>(AoeGameplayClock{.tick = 12});
    world.add_resource<AoeGameplaySettings>(AoeGameplaySettings{0.1, 8, 7});

    auto definition = std::make_shared<AoeUnitDefinition>();
    definition->id = "test";
    definition->movement.speed = 1.f;
    definition->presentation.backend = "aoe2";
    definition->presentation.resource_id = "u_test";
    definition->presentation.animations = {
        {"idle", "idleA"}, {"moving", "walkA"}, {"attack", "attackA"},
        {"critical_attack", "attackB"}, {"death", "deathA"},
        {"disappear", "decayA"}};
    constexpr AssetId DefinitionId = 77;
    auto& store = world.resource_or_add<AssetManager>().store<AoeUnitDefinition>();
    store.set_loaded(DefinitionId, definition);
    auto definition_handle = store.acquire(DefinitionId);

    const auto gameplay = world.spawn();
    world.reg().emplace<Transform>(gameplay, Transform::from_trs({4.f, 5.f, 0.f}));
    world.reg().emplace<AoeUnitDefinitionRef>(gameplay,
        AoeUnitDefinitionRef{definition_handle});
    world.reg().emplace<AoeActionState>(gameplay, AoeActionState{
        .state = UnitState::Attacking,
        .critical = true,
        .sequence = 3,
        .state_started_tick = 10});
    world.reg().emplace<AoeFacing>(gameplay, AoeFacing{5, 16});
    world.reg().emplace<AoePresentationOptions>(gameplay,
        AoePresentationOptions{2, 0x4u});
    world.reg().emplace<AoeLocomotionState>(gameplay);

    aoe2_gameplay_presentation_system(world);
    const auto first_child = world.reg().get<Aoe2PresentationLink>(gameplay).render;
    assert(world.reg().valid(first_child));
    assert(world.reg().get<AoeGameplayOwner>(first_child).gameplay == gameplay);
    assert(world.reg().get<Parent>(first_child).value == gameplay);
    const auto& request = world.reg().get<Aoe2SpawnRequest>(first_child);
    assert(request.options.unit_id == "u_test");
    assert(request.options.animation == "attackB");
    assert(request.options.direction == 5 && request.options.player_color == 2);
    assert(request.options.layers == 0x4u);
    assert(request.options.playback_mode == Aoe2PlaybackMode::External);
    assert(!request.options.playing && !request.options.loop);
    assert(near(request.options.playback_time, 0.2f));

    // A missing critical mapping falls back to the normal attack semantic.
    definition->presentation.animations.erase("critical_attack");
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2SpawnRequest>(first_child).options.animation == "attackA");
    assert(near(world.reg().get<Aoe2SpawnRequest>(first_child)
                    .options.playback_time, 0.2f));

    // Direction is presentation-only. Advancing one gameplay tick while only
    // changing direction must continue the same authored action at 0.3 s.
    auto& action = world.reg().get<AoeActionState>(gameplay);
    world.resource<AoeGameplayClock>().tick = 13;
    world.reg().get<AoeFacing>(gameplay).direction = 6;
    aoe2_gameplay_presentation_system(world);
    const auto& turned = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(turned.direction == 6);
    assert(turned.animation == "attackA");
    assert(near(turned.playback_time, 0.3f));

    // Locomotion transitions retain the presentation cursor instead of
    // restarting each atlas. Its fixed-clock progression is independent of
    // the state's action-relative timer.
    action.state = UnitState::Moving;
    action.sequence = 4;
    action.state_started_tick = 13;
    aoe2_gameplay_presentation_system(world);
    const auto& moving_start = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(moving_start.animation == "walkA");
    assert(near(moving_start.playback_time, 0.3f));
    assert(moving_start.loop && !moving_start.playing);

    world.resource<AoeGameplayClock>().tick = 14;
    auto& moving_locomotion = world.reg().get<AoeLocomotionState>(gameplay);
    moving_locomotion.effective_max_speed = 1.f;
    moving_locomotion.distance_travelled = .1;
    aoe2_gameplay_presentation_system(world);
    const auto& moving = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(moving.playback_mode == Aoe2PlaybackMode::External);
    assert(near(moving.playback_time, 0.4f));

    // A locomotion intent that has entered escape/stalled handling presents
    // idle instead of freezing an arbitrary walk frame. Clearing the stalled
    // state restores walk without changing the authoritative action state.
    moving_locomotion.escape_steering = true;
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2SpawnRequest>(first_child).options.animation ==
           "idleA");
    moving_locomotion.escape_steering = false;
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2SpawnRequest>(first_child).options.animation ==
           "walkA");

    action.state = UnitState::Idle;
    action.sequence = 5;
    action.state_started_tick = 14;
    aoe2_gameplay_presentation_system(world);
    const auto& idle = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(idle.animation == "idleA");
    assert(near(idle.playback_time, 0.4f));

    // Authored combat starts immediately at frame zero; the bridge does not
    // wait for the current idle/walk loop to finish.
    action.state = UnitState::Attacking;
    action.sequence = 6;
    action.state_started_tick = 14;
    aoe2_gameplay_presentation_system(world);
    const auto& attack_start = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(attack_start.animation == "attackA");
    assert(near(attack_start.playback_time, 0.f) && !attack_start.loop);

    world.resource<AoeGameplayClock>().tick = 15;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(first_child)
                    .options.playback_time, 0.1f));

    // Returning to locomotion inherits the attack-end cursor. A one-second
    // attack therefore lands near idle frame 30 at 30 FPS, rather than frame 0.
    world.resource<AoeGameplayClock>().tick = 24;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(first_child)
                    .options.playback_time, 1.f));
    action.state = UnitState::Idle;
    action.sequence = 7;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    const auto& after_attack = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(near(after_attack.playback_time, 1.f));
    assert(std::lround(after_attack.playback_time * 30.f) == 30);

    // A new authored action resets even when it resolves to the same attack
    // animation used by an earlier sequence.
    action.state = UnitState::Attacking;
    action.sequence = 8;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(first_child)
                    .options.playback_time, 0.f));

    action.state = UnitState::Dying;
    action.sequence = 9;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    const auto& death = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(death.animation == "deathA");
    assert(near(death.playback_time, 0.f) && !death.loop);

    action.state = UnitState::Disappearing;
    action.sequence = 10;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    const auto& disappear_start = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(disappear_start.animation == "decayA");
    assert(near(disappear_start.playback_time, 0.f) && !disappear_start.loop);

    // A missing terminal asset deliberately freezes the first idle frame and
    // never fabricates elapsed playback for that fallback.
    definition->presentation.animations.erase("death");
    action.state = UnitState::Dying;
    action.sequence = 11;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    const auto& death_fallback = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(death_fallback.animation == "idleA");
    assert(near(death_fallback.playback_time, 0.f) && !death_fallback.loop);

    // Re-enter disappear with its real animation, then let it advance once.
    // Destroying the render child must not destroy the presentation cursor.
    action.state = UnitState::Disappearing;
    action.sequence = 12;
    action.state_started_tick = 24;
    aoe2_gameplay_presentation_system(world);
    world.resource<AoeGameplayClock>().tick = 25;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(first_child)
                    .options.playback_time, 0.1f));

    // Gameplay and SLD use the same +X direction-zero convention, so initial,
    // pending, and loaded render children receive the gameplay slot unchanged.
    world.reg().remove<Aoe2SpawnRequest>(first_child);
    world.reg().emplace<Aoe2UnitRender>(first_child);
    world.reg().get<AoeFacing>(gameplay).direction = 7;
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2UnitRender>(first_child).direction_slot == 7);
    assert(world.reg().get<Aoe2UnitRender>(first_child).direction_slot_count == 16);

    // A destroyed presentation child is recreated without replacing gameplay
    // or resetting the non-looping disappear cursor.
    world.reg().destroy(first_child);
    aoe2_gameplay_presentation_system(world);
    const auto second_child = world.reg().get<Aoe2PresentationLink>(gameplay).render;
    assert(world.reg().valid(second_child) && second_child != first_child);
    assert(near(world.reg().get<Aoe2SpawnRequest>(second_child)
                    .options.playback_time, 0.1f));

    // Moving playback consumes authoritative travelled distance relative to
    // the effective gameplay speed cap. A zero cap freezes even if an external
    // correction changes distance; reaching a 0.5 squad cap advances at 1x.
    action.state = UnitState::Moving;
    action.sequence = 13;
    action.state_started_tick = 25;
    world.reg().get<AoeLocomotionState>(gameplay).effective_max_speed = 0.f;
    aoe2_gameplay_presentation_system(world);
    const float blocked_cursor = world.reg().get<Aoe2SpawnRequest>(second_child)
        .options.playback_time;
    world.resource<AoeGameplayClock>().tick = 26;
    world.reg().get<AoeLocomotionState>(gameplay).distance_travelled += .05;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(second_child)
                    .options.playback_time, blocked_cursor));
    auto& limited_locomotion = world.reg().get<AoeLocomotionState>(gameplay);
    limited_locomotion.effective_max_speed = .5f;
    limited_locomotion.distance_travelled += .05;
    world.resource<AoeGameplayClock>().tick = 27;
    aoe2_gameplay_presentation_system(world);
    assert(near(world.reg().get<Aoe2SpawnRequest>(second_child)
                    .options.playback_time, blocked_cursor + .1f));
    action.state = UnitState::Disappearing;
    action.sequence = 14;
    action.state_started_tick = 27;

    // Presentation errors are attached to the parent and do not mutate its action.
    definition->presentation.backend = "unsupported";
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().all_of<AoePresentationError>(gameplay));
    assert(world.reg().get<AoeActionState>(gameplay).state == UnitState::Disappearing);

    // Recycle pending is a gameplay-owned terminal state: the bridge removes
    // its child and all presentation bookkeeping before the pool strips data.
    definition->presentation.backend = "aoe2";
    world.reg().emplace<AoeRecyclePending>(gameplay);
    aoe2_gameplay_orphan_cleanup_system(world);
    assert(!world.reg().valid(second_child));
    assert(!world.reg().all_of<Aoe2PresentationLink>(gameplay));
    assert(!world.reg().all_of<Aoe2PresentationSnapshot>(gameplay));
    assert(!world.reg().all_of<AoePresentationError>(gameplay));
    world.reg().remove<AoeRecyclePending>(gameplay);

    // Destroying gameplay leaves a render orphan; cleanup owns and removes it.
    aoe2_gameplay_presentation_system(world);
    const auto third_child = world.reg().get<Aoe2PresentationLink>(gameplay).render;
    world.reg().destroy(gameplay);
    aoe2_gameplay_orphan_cleanup_system(world);
    assert(!world.reg().valid(third_child));

    assert(aoe2_projectile_direction({1.f, -1.f}, 32) == 0);
    assert(aoe2_projectile_direction({-1.f, 1.f}, 32) == 16);
    assert(aoe2_projectile_pitch_frame({1.f, 0.f, 100.f}, 11) == 0);
    assert(aoe2_projectile_pitch_frame({1.f, 0.f, 0.f}, 11) == 5);
    assert(aoe2_projectile_pitch_frame({1.f, 0.f, -100.f}, 11) == 10);

    const auto projectile_entity = world.spawn();
    world.reg().emplace<Transform>(projectile_entity);
    AoeProjectile projectile;
    projectile.id = "arrow";
    projectile.position = {1.f, 2.f, 1.f};
    projectile.velocity = {1.f, -1.f, 0.f};
    world.reg().emplace<AoeProjectile>(projectile_entity, projectile);
    aoe2_projectile_presentation_system(world);
    const auto arrow_child = world.reg()
        .get<Aoe2ProjectilePresentationLink>(projectile_entity).render;
    assert(world.reg().valid(arrow_child));
    const auto& arrow_request = world.reg().get<Aoe2SpawnRequest>(arrow_child);
    assert(arrow_request.options.resource_kind == Aoe2ResourceKind::Graphic);
    assert(arrow_request.options.unit_id == "p_arrow");
    assert(arrow_request.options.animation == "p_arrow_x2");
    assert(arrow_request.options.direction == 0);
    assert(arrow_request.options.direction_slot_count == 32);
    assert(std::abs(arrow_request.options.playback_time - 5.f / 30.f) < 1e-6f);
    world.reg().destroy(projectile_entity);
    aoe2_gameplay_orphan_cleanup_system(world);
    assert(!world.reg().valid(arrow_child));
    disconnect_transform_lifecycle(world);
    return 0;
}
