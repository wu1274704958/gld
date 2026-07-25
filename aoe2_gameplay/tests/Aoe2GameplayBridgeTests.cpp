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

int main() {
    EcsWorld world;
    register_transform_lifecycle(world);
    world.add_resource<AoeGameplayClock>(AoeGameplayClock{.tick = 12});
    world.add_resource<AoeGameplaySettings>(AoeGameplaySettings{0.1, 8, 7});

    auto definition = std::make_shared<AoeUnitDefinition>();
    definition->id = "test";
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
    assert(request.options.playback_time == 0.2f);

    // A missing critical mapping falls back to the normal attack semantic.
    definition->presentation.animations.erase("critical_attack");
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2SpawnRequest>(first_child).options.animation == "attackA");

    // With no death asset the bridge freezes the first idle pose.
    definition->presentation.animations.erase("death");
    auto& action = world.reg().get<AoeActionState>(gameplay);
    action.state = UnitState::Dying;
    aoe2_gameplay_presentation_system(world);
    const auto& death_fallback = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(death_fallback.animation == "idleA");
    assert(death_fallback.playback_time == 0.f && !death_fallback.loop);
    action.state = UnitState::Disappearing;
    action.state_started_tick = 11;
    aoe2_gameplay_presentation_system(world);
    const auto& disappear = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(disappear.animation == "decayA");
    assert(disappear.playback_time == 0.1f && !disappear.loop);

    // Moving selects the authored walk semantic and advances it from the
    // deterministic gameplay clock through external looping playback.
    action.state = UnitState::Moving;
    action.state_started_tick = 10;
    aoe2_gameplay_presentation_system(world);
    const auto& moving = world.reg().get<Aoe2SpawnRequest>(first_child).options;
    assert(moving.animation == "walkA");
    assert(moving.playback_mode == Aoe2PlaybackMode::External);
    assert(moving.playback_time == 0.2f && moving.loop && !moving.playing);
    action.state = UnitState::Attacking;

    // Gameplay and SLD use the same +X direction-zero convention, so initial,
    // pending, and loaded render children receive the gameplay slot unchanged.
    world.reg().remove<Aoe2SpawnRequest>(first_child);
    world.reg().emplace<Aoe2UnitRender>(first_child);
    world.reg().get<AoeFacing>(gameplay).direction = 6;
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().get<Aoe2UnitRender>(first_child).direction_slot == 6);
    assert(world.reg().get<Aoe2UnitRender>(first_child).direction_slot_count == 16);

    // A destroyed presentation child is recreated without replacing gameplay.
    world.reg().destroy(first_child);
    aoe2_gameplay_presentation_system(world);
    const auto second_child = world.reg().get<Aoe2PresentationLink>(gameplay).render;
    assert(world.reg().valid(second_child) && second_child != first_child);

    // Presentation errors are attached to the parent and do not mutate its action.
    definition->presentation.backend = "unsupported";
    aoe2_gameplay_presentation_system(world);
    assert(world.reg().all_of<AoePresentationError>(gameplay));
    assert(world.reg().get<AoeActionState>(gameplay).state == UnitState::Attacking);

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
