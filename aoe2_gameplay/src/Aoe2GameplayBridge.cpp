#include <aoe2_gameplay/Aoe2GameplayBridge.hpp>
#include <ecs/PerformanceMonitoring.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include <ecs/systems/TransformSystem.hpp>

namespace gld::ecs::aoe2_gameplay {
using namespace gld::ecs::aoe2;

namespace {
constexpr float PresentationMotionEpsilon = 1e-4f;

bool visually_moving(
    const aoe::AoeActionState& state,
    const aoe::AoeLocomotionState* locomotion,
    const aoe::AoeLocalAvoidanceState* avoidance) {
    if (state.state != aoe::UnitState::Moving ||
        (avoidance && avoidance->escape_steering))
        return false;
    // Presentation-only entities may legitimately omit locomotion. Gameplay
    // units have it, and their measured speed is the visual source of truth.
    return !locomotion ||
        (std::isfinite(locomotion->actual_speed) &&
         locomotion->actual_speed > PresentationMotionEpsilon);
}

std::string desired_animation(const aoe::AoeUnitDefinition& definition,
                               const aoe::AoeActionState& state,
                               const aoe::AoeLocomotionState* locomotion,
                               const aoe::AoeLocalAvoidanceState* avoidance) {
    const auto& presentation = definition.presentation;
    switch (state.state) {
    case aoe::UnitState::Moving: {
        if (!visually_moving(state, locomotion, avoidance))
            return presentation.animation("idle");
        const auto moving = presentation.animation("moving");
        return moving.empty() ? presentation.animation("idle") : moving;
    }
    case aoe::UnitState::Attacking:
        if (state.critical) {
            const auto critical = presentation.animation("critical_attack");
            if (!critical.empty()) return critical;
        }
        return presentation.animation("attack");
    case aoe::UnitState::Dying: {
        const auto death = presentation.animation("death");
        return death.empty() ? presentation.animation("idle") : death;
    }
    case aoe::UnitState::Disappearing: {
        const auto disappear = presentation.animation("disappear");
        return disappear.empty() ? presentation.animation("idle") : disappear;
    }
    default: return presentation.animation("idle");
    }
}

bool looping(aoe::UnitState state) {
    return state == aoe::UnitState::Idle || state == aoe::UnitState::Moving;
}

bool starts_authored_action(aoe::UnitState state) {
    return state == aoe::UnitState::Attacking ||
           state == aoe::UnitState::Dying ||
           state == aoe::UnitState::Disappearing;
}

float update_presentation_playback(
    Aoe2PresentationSnapshot& snapshot, const aoe::AoeActionState& state,
    const aoe::AoeGameplayClock& clock,
    const aoe::AoeGameplaySettings& settings,
    const aoe::AoeLocomotionState* locomotion,
    const aoe::AoeLocalAvoidanceState* avoidance,
    std::string_view animation, float action_elapsed) {
    const bool changed = snapshot.state != state.state ||
        snapshot.sequence != state.sequence ||
        snapshot.critical != state.critical ||
        snapshot.requested_animation != animation;
    const bool moving_visually =
        visually_moving(state, locomotion, avoidance);
    if (moving_visually) {
        if (locomotion && locomotion->effective_max_speed > 0.f &&
            std::isfinite(locomotion->distance_travelled)) {
            const double distance = std::max(
                snapshot.locomotion_distance,
                locomotion->distance_travelled);
            snapshot.playback_time +=
                (distance - snapshot.locomotion_distance) /
                static_cast<double>(locomotion->effective_max_speed);
        }
    } else if (clock.tick >= snapshot.last_gameplay_tick) {
        snapshot.playback_time += static_cast<double>(
            clock.tick - snapshot.last_gameplay_tick) * settings.fixed_dt;
    }
    if (changed && starts_authored_action(state.state))
        snapshot.playback_time = action_elapsed;
    snapshot.state = state.state;
    snapshot.sequence = state.sequence;
    snapshot.critical = state.critical;
    snapshot.requested_animation = animation;
    snapshot.last_gameplay_tick = clock.tick;
    if (locomotion && std::isfinite(locomotion->distance_travelled))
        snapshot.locomotion_distance = locomotion->distance_travelled;
    return static_cast<float>(std::max(0.0, snapshot.playback_time));
}

float normalize_loop_playback(Aoe2PresentationSnapshot& snapshot,
                              const Aoe2UnitAppearance* appearance,
                              std::string_view animation) {
    const auto* clip = appearance
        ? appearance->find_animation(std::string(animation)) : nullptr;
    if (clip && clip->fps > 0.f && clip->frames_per_direction > 0) {
        const double duration = static_cast<double>(clip->frames_per_direction) /
            static_cast<double>(clip->fps);
        if (duration > 0.0)
            snapshot.playback_time = std::fmod(
                std::max(0.0, snapshot.playback_time), duration);
    }
    return static_cast<float>(std::max(0.0, snapshot.playback_time));
}

void sync_pending_request(Aoe2SpawnRequest& request, const std::string& animation,
                          int direction, int direction_count,
                          const aoe::AoePresentationOptions& options,
                          float elapsed, bool loop) {
    request.options.animation = animation;
    request.options.animation_slot = AnimationSlot::Invalid;
    request.options.direction = direction;
    request.options.direction_slot_count = direction_count;
    request.options.player_color = options.player_color;
    request.options.layers = options.layers;
    request.options.playback_mode = Aoe2PlaybackMode::External;
    request.options.playback_time = elapsed;
    request.options.playing = false;
    request.options.loop = loop;
}
} // namespace

int aoe2_projectile_direction(glm::vec2 logical_velocity,
                              int direction_count) {
    if (direction_count <= 0 ||
        glm::dot(logical_velocity, logical_velocity) <= 1e-10f) return 0;
    const glm::vec2 projected{
        logical_velocity.x - logical_velocity.y,
        (logical_velocity.x + logical_velocity.y) * .5f};
    float angle = std::atan2(projected.y, projected.x);
    const float full_turn = 2.f * glm::pi<float>();
    if (angle < 0.f) angle += full_turn;
    const float sector = full_turn / static_cast<float>(direction_count);
    return static_cast<int>(std::floor((angle + sector * .5f) / sector)) %
           direction_count;
}

int aoe2_projectile_pitch_frame(glm::vec3 velocity, int frame_count) {
    if (frame_count <= 1) return 0;
    const float ground_speed = glm::length(glm::vec2(velocity));
    if (ground_speed <= 1e-5f && std::abs(velocity.z) <= 1e-5f)
        return (frame_count - 1) / 2;
    const float pitch = std::atan2(velocity.z, ground_speed);
    const float normalized = std::clamp(
        .5f - pitch / glm::pi<float>(), 0.f, 1.f);
    return std::clamp(static_cast<int>(std::lround(
        normalized * static_cast<float>(frame_count - 1))),
        0, frame_count - 1);
}

void aoe2_gameplay_orphan_cleanup_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    GLD_PERF_MONITOR(
        world.resource_or_add<Aoe2GameplayBridgePerformanceDiagnostics>().begin_frame();
    );
    auto& reg = world.reg();
    std::vector<entt::entity> orphaned;
    for (auto child : reg.view<AoeGameplayOwner>()) {
        const auto owner = reg.get<AoeGameplayOwner>(child).gameplay;
        const auto* link = reg.valid(owner) ? reg.try_get<Aoe2PresentationLink>(owner) : nullptr;
        const bool inactive = reg.valid(owner) &&
            reg.any_of<aoe::AoeRecyclePending, aoe::AoePooledUnit>(owner);
        if (!reg.valid(owner) || inactive || !link || link->render != child)
            orphaned.push_back(child);
    }
    for (auto child : orphaned) if (reg.valid(child)) reg.destroy(child);

    orphaned.clear();
    for (auto child : reg.view<AoeProjectileOwner>()) {
        const auto owner = reg.get<AoeProjectileOwner>(child).gameplay;
        const auto* link = reg.valid(owner)
            ? reg.try_get<Aoe2ProjectilePresentationLink>(owner) : nullptr;
        if (!reg.valid(owner) || !link || link->render != child)
            orphaned.push_back(child);
    }
    for (auto child : orphaned) if (reg.valid(child)) reg.destroy(child);

    const auto pending = reg.view<Aoe2PresentationLink>(
        entt::exclude<aoe::AoeUnitDefinitionRef>);
    std::vector<entt::entity> stale_links(pending.begin(), pending.end());
    for (const auto owner : stale_links) {
        reg.remove<Aoe2PresentationLink, Aoe2PresentationSnapshot,
                   AoePresentationError>(owner);
    }

    std::vector<entt::entity> recycling;
    for (const auto owner : reg.view<Aoe2PresentationLink, aoe::AoeRecyclePending>())
        recycling.push_back(owner);
    for (const auto owner : recycling) {
        const auto child = reg.get<Aoe2PresentationLink>(owner).render;
        if (reg.valid(child)) reg.destroy(child);
        reg.remove<Aoe2PresentationLink, Aoe2PresentationSnapshot,
                   AoePresentationError>(owner);
    }
    GLD_PERF_MONITOR(
        world.resource<Aoe2GameplayBridgePerformanceDiagnostics>().orphan_cleanup_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void aoe2_projectile_presentation_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    auto& reg = world.reg();
    for (const auto entity : reg.view<aoe::AoeProjectile>()) {
        const auto& projectile = reg.get<aoe::AoeProjectile>(entity);
        if (projectile.id != "arrow") {
            reg.emplace_or_replace<AoePresentationError>(entity,
                AoePresentationError{"no AoE2 projectile presentation: " +
                                     projectile.id});
            continue;
        }
        glm::vec2 horizontal{projectile.velocity.x, projectile.velocity.y};
        if (glm::dot(horizontal, horizontal) <= 1e-10f &&
            reg.valid(projectile.target.entity) &&
            reg.all_of<aoe::AoePosition>(projectile.target.entity)) {
            horizontal = reg.get<aoe::AoePosition>(projectile.target.entity).value -
                glm::vec2(projectile.position);
        }
        const int direction = aoe2_projectile_direction(horizontal, 32);
        const int pitch_frame = aoe2_projectile_pitch_frame(projectile.velocity, 11);
        auto* link = reg.try_get<Aoe2ProjectilePresentationLink>(entity);
        if (!link || !reg.valid(link->render)) {
            SpawnOptions spawn;
            spawn.unit_id = "p_arrow";
            spawn.resource_kind = Aoe2ResourceKind::Graphic;
            spawn.animation = "p_arrow_x2";
            spawn.direction = direction;
            spawn.direction_slot_count = 32;
            spawn.layers = projectile.layers;
            spawn.playback_mode = Aoe2PlaybackMode::External;
            spawn.playback_time = static_cast<float>(pitch_frame) / 30.f;
            spawn.playing = false;
            spawn.loop = false;
            const auto child = spawn_aoe2_graphic(world, spawn, Transform{});
            reg.emplace<AoeProjectileOwner>(child, AoeProjectileOwner{entity});
            set_parent(world, child, entity);
            reg.emplace_or_replace<Aoe2ProjectilePresentationLink>(
                entity, Aoe2ProjectilePresentationLink{child});
            reg.emplace_or_replace<Aoe2ProjectilePresentationSnapshot>(entity,
                Aoe2ProjectilePresentationSnapshot{direction, pitch_frame});
            reg.remove<AoePresentationError>(entity);
            continue;
        }

        const auto child = link->render;
        if (auto* pending = reg.try_get<Aoe2SpawnRequest>(child)) {
            pending->options.direction = direction;
            pending->options.direction_slot_count = 32;
            pending->options.playback_mode = Aoe2PlaybackMode::External;
            pending->options.playback_time = static_cast<float>(pitch_frame) / 30.f;
            pending->options.playing = false;
            pending->options.loop = false;
            auto& snapshot = reg.get_or_emplace<Aoe2ProjectilePresentationSnapshot>(entity);
            snapshot.direction = direction;
            snapshot.pitch_frame = pitch_frame;
            continue;
        }
        if (const auto* error = reg.try_get<Aoe2SpawnError>(child)) {
            reg.emplace_or_replace<AoePresentationError>(entity,
                AoePresentationError{error->message});
            continue;
        }
        auto* render = reg.try_get<Aoe2UnitRender>(child);
        if (!render) continue;
        auto& snapshot = reg.get_or_emplace<Aoe2ProjectilePresentationSnapshot>(entity);
        if (snapshot.direction != direction) {
            set_aoe2_direction(world, child, direction, 32);
            snapshot.direction = direction;
        }
        if (snapshot.pitch_frame != pitch_frame) {
            float fps = 30.f;
            if (const auto* appearance = render->appearance.get()) {
                if (const auto* animation = appearance->find_animation("p_arrow_x2"))
                    fps = animation->fps;
            }
            set_aoe2_playback_time(world, child,
                static_cast<float>(pitch_frame) / std::max(fps, 1.f));
            snapshot.pitch_frame = pitch_frame;
        }
        set_aoe2_playback_mode(world, child, Aoe2PlaybackMode::External);
        set_aoe2_playing(world, child, false);
        set_aoe2_looping(world, child, false);
    }
    GLD_PERF_MONITOR(
        world.resource_or_add<Aoe2GameplayBridgePerformanceDiagnostics>()
            .projectile_presentation_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void aoe2_gameplay_presentation_system(EcsWorld& world) {
    GLD_PERF_TIME_POINT(started);
    auto& reg = world.reg();
    const auto& clock = world.resource<aoe::AoeGameplayClock>();
    const auto& settings = world.resource<aoe::AoeGameplaySettings>();
    for (auto entity : reg.view<aoe::AoeUnitDefinitionRef, aoe::AoeActionState,
                                aoe::AoeFacing, aoe::AoePresentationOptions>()) {
        if (reg.any_of<aoe::AoeRecyclePending, aoe::AoePooledUnit>(entity)) continue;
        const auto& reference = reg.get<aoe::AoeUnitDefinitionRef>(entity);
        const auto* definition = reference.value.get();
        if (!definition) continue;
        if (definition->presentation.backend != "aoe2") {
            reg.emplace_or_replace<AoePresentationError>(entity,
                AoePresentationError{"unsupported presentation backend: " +
                    definition->presentation.backend});
            continue;
        }
        const auto& state = reg.get<aoe::AoeActionState>(entity);
        const auto& facing = reg.get<aoe::AoeFacing>(entity);
        const int render_direction = facing.direction;
        const auto& options = reg.get<aoe::AoePresentationOptions>(entity);
        const auto* locomotion = reg.try_get<aoe::AoeLocomotionState>(entity);
        const auto* avoidance =
            reg.try_get<aoe::AoeLocalAvoidanceState>(entity);
        const std::string animation = desired_animation(
            *definition, state, locomotion, avoidance);
        const bool frozen_idle_terminal =
            (state.state == aoe::UnitState::Dying &&
             definition->presentation.animation("death").empty()) ||
            (state.state == aoe::UnitState::Disappearing &&
             definition->presentation.animation("disappear").empty());
        const float action_elapsed = frozen_idle_terminal ? 0.f : static_cast<float>(
            aoe::aoe_action_elapsed_seconds(state, clock, settings));
        const bool should_loop = looping(state.state);
        auto* snapshot = reg.try_get<Aoe2PresentationSnapshot>(entity);
        if (!snapshot) {
            snapshot = &reg.emplace<Aoe2PresentationSnapshot>(entity,
                Aoe2PresentationSnapshot{
                    .state = state.state,
                    .sequence = state.sequence,
                    .last_gameplay_tick = clock.tick,
                    .playback_time = action_elapsed,
                    .locomotion_distance = locomotion
                        ? locomotion->distance_travelled : 0.0,
                    .critical = state.critical,
                    .direction = render_direction,
                    .direction_count = facing.direction_count,
                    .player_color = options.player_color,
                    .requested_animation = animation});
        }
        float playback_time = update_presentation_playback(
            *snapshot, state, clock, settings, locomotion, avoidance,
            animation, action_elapsed);
        if (frozen_idle_terminal) playback_time = 0.f;

        auto* link = reg.try_get<Aoe2PresentationLink>(entity);
        if (!link || !reg.valid(link->render)) {
            SpawnOptions spawn;
            spawn.unit_id = definition->presentation.resource_id;
            spawn.animation = animation;
            spawn.direction = render_direction;
            spawn.direction_slot_count = facing.direction_count;
            spawn.player_color = options.player_color;
            spawn.layers = options.layers;
            spawn.playback_mode = Aoe2PlaybackMode::External;
            spawn.playback_time = playback_time;
            spawn.playing = false;
            spawn.loop = should_loop;
            const auto child = spawn_aoe2_unit(world, spawn, Transform{});
            reg.emplace<AoeGameplayOwner>(child, AoeGameplayOwner{entity});
            set_parent(world, child, entity);
            reg.emplace_or_replace<Aoe2PresentationLink>(entity, Aoe2PresentationLink{child});
            reg.remove<AoePresentationError>(entity);
            continue;
        }

        const entt::entity child = link->render;
        if (auto* pending = reg.try_get<Aoe2SpawnRequest>(child)) {
            if (should_loop && !frozen_idle_terminal)
                playback_time = normalize_loop_playback(
                    *snapshot, pending->appearance.get(), animation);
            sync_pending_request(*pending, animation, render_direction,
                                 facing.direction_count, options, playback_time, should_loop);
            continue;
        }
        if (const auto* error = reg.try_get<Aoe2SpawnError>(child)) {
            reg.emplace_or_replace<AoePresentationError>(entity,
                AoePresentationError{error->message});
            continue;
        }
        auto* render = reg.try_get<Aoe2UnitRender>(child);
        if (!render) continue;
        const bool target_already_active =
            render->animation == animation &&
            render->animation_slot != AnimationSlot::Invalid;
        const bool target_already_pending =
            render->transition == AnimationTransitionState::Waiting &&
            render->pending_animation == animation &&
            render->pending_animation_slot != AnimationSlot::Invalid;
        if (!target_already_active && !target_already_pending) {
            if (!request_aoe2_animation(world, child, animation)) {
                const std::string fallback = state.state == aoe::UnitState::Attacking
                    ? definition->presentation.animation("attack")
                    : definition->presentation.animation("idle");
                if (fallback.empty() || !request_aoe2_animation(world, child, fallback))
                    reg.emplace_or_replace<AoePresentationError>(entity,
                        AoePresentationError{"animation unavailable: " + animation});
            }
        }
        if (snapshot->direction != render_direction ||
            snapshot->direction_count != facing.direction_count) {
            set_aoe2_direction(world, child, render_direction, facing.direction_count);
            snapshot->direction = render_direction;
            snapshot->direction_count = facing.direction_count;
        }
        if (snapshot->player_color != options.player_color) {
            set_aoe2_player_color(world, child, options.player_color, 0);
            snapshot->player_color = options.player_color;
        }
        if (should_loop && !frozen_idle_terminal)
            playback_time = normalize_loop_playback(
                *snapshot, render->appearance.get(), animation);
        set_aoe2_playback_mode(world, child, Aoe2PlaybackMode::External);
        set_aoe2_playing(world, child, false);
        set_aoe2_looping(world, child, should_loop);
        set_aoe2_playback_time(world, child, playback_time);
    }
    GLD_PERF_MONITOR(
        world.resource_or_add<Aoe2GameplayBridgePerformanceDiagnostics>()
            .unit_presentation_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
    );
}

void Aoe2GameplayBridgePlugin::operator()(App& app) const {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    app.world.resource_or_add<Aoe2GameplayBridgePerformanceDiagnostics>();
#endif
    app.add_system(Stage::PreUpdate, aoe2_gameplay_orphan_cleanup_system);
    app.add_system(Stage::PreUpdate, aoe2_gameplay_presentation_system);
    app.add_system(Stage::PreUpdate, aoe2_projectile_presentation_system);
}

} // namespace gld::ecs::aoe2_gameplay
