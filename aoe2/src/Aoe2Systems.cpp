#include <aoe2/Aoe2Systems.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>
#include <ecs/Components.hpp>
#include <ecs/render/RenderComponents.hpp>

namespace gld::ecs::aoe2 {

namespace {
void poll_layer(const Layer& layer, const char* name, bool& ready, std::string& error) {
    if (!layer.usable()) return;
    const auto state = layer.texture.state();
    if (state == LoadState::Failed) {
        error = std::string(name) + " texture failed: " + layer.image_path;
        ready = false;
    } else if (state != LoadState::Loaded) {
        ready = false;
    }
}

void poll_residency(Animation& animation) {
    if (animation.residency != AnimationResidencyState::Loading) return;
    bool ready = true;
    std::string error;
    poll_layer(animation.main, "main", ready, error);
    if (error.empty()) poll_layer(animation.shadow, "shadow", ready, error);
    if (error.empty()) poll_layer(animation.player_color, "player_color", ready, error);
    if (!error.empty()) {
        animation.residency = AnimationResidencyState::Failed;
        animation.residency_error = std::move(error);
        ++animation.residency_revision;
    } else if (ready) {
        animation.residency = AnimationResidencyState::Ready;
        ++animation.residency_revision;
    }
}
} // namespace

void request_animation_residency(AssetServer& server, Aoe2UnitAppearance& appearance,
                                 const std::string& animation_name) {
    auto* animation = appearance.find_animation(animation_name);
    if (!animation || animation->residency != AnimationResidencyState::Unloaded) return;
    if (!animation->main.usable()) {
        animation->residency = AnimationResidencyState::Failed;
        animation->residency_error = "main layer is not usable";
        ++animation->residency_revision;
        return;
    }
    const auto linear = TextureFilter::Linear;
    const auto nearest = TextureFilter::Nearest;
    const auto clamp = TextureWrap::ClampToEdge;
    animation->main.texture = server.load_texture(animation->main.image_path, Channels::RGBA,
        false, false, false, linear, linear, clamp, clamp);
    if (animation->shadow.usable())
        animation->shadow.texture = server.load_texture(animation->shadow.image_path, Channels::Gray,
            false, false, false, linear, linear, clamp, clamp,
            TextureChannelMapping::Red);
    if (animation->player_color.usable())
        animation->player_color.texture = server.load_texture(animation->player_color.image_path,
            Channels::RG, false, false, false, nearest, nearest, clamp, clamp,
            TextureChannelMapping::RedAlpha);
    animation->residency = AnimationResidencyState::Loading;
    animation->residency_error.clear();
    ++animation->residency_revision;
}

bool request_aoe2_animation(Aoe2UnitRender& unit, const std::string& animation_name) {
    auto* appearance = unit.appearance.get();
    if (!appearance || !appearance->find_animation(animation_name)) return false;
    if (animation_name == unit.animation) {
        unit.pending_animation.clear();
        unit.transition = AnimationTransitionState::None;
        unit.transition_error.clear();
        return true;
    }
    unit.pending_animation = animation_name;
    unit.transition = AnimationTransitionState::Waiting;
    unit.transition_error.clear();
    return true;
}

void restart_aoe2_animation(Aoe2UnitRender& unit) {
    unit.restart_requested = true;
}

entt::entity spawn_aoe2_unit(EcsWorld& world, const SpawnOptions& options, const Transform& transform) {
    const auto entity = world.spawn();
    world.reg().emplace<Transform>(entity, transform);
    world.reg().emplace<Aoe2SpawnRequest>(entity, Aoe2SpawnRequest{options});
    world.reg().emplace<RenderLayer>(entity, RenderLayer{options.layers});
    return entity;
}

void spawn_aoe2_unit_system(EcsWorld& world) {
    auto* manager = world.try_resource<Aoe2ResourceManager>();
    if (!manager) return;
    auto& reg = world.reg();
    std::vector<entt::entity> completed;
    for (auto entity : reg.view<Aoe2SpawnRequest>()) {
        auto& request = reg.get<Aoe2SpawnRequest>(entity);
        if (!request.requested) {
            request.appearance = manager->load(request.options.unit_id);
            request.requested = true;
            if (request.appearance.state() == LoadState::NotLoaded) {
                reg.emplace_or_replace<Aoe2SpawnError>(entity,
                    Aoe2SpawnError{"unknown AoE2 unit: " + request.options.unit_id});
                completed.push_back(entity);
                continue;
            }
        }
        if (request.appearance.state() == LoadState::Failed) {
            reg.emplace_or_replace<Aoe2SpawnError>(entity,
                Aoe2SpawnError{"failed to load AoE2 unit: " + request.options.unit_id});
            completed.push_back(entity);
            continue;
        }
        auto* appearance = request.appearance.get();
        if (!appearance) continue;

        Aoe2UnitRender render;
        render.appearance = request.appearance;
        std::string initial_animation = request.options.animation;
        if (!appearance->find_animation(initial_animation)) {
            initial_animation = appearance->find_animation("idleA") ? "idleA"
                : appearance->animation_names.front();
        }
        render.direction_slot = request.options.direction;
        render.direction_slot_count = request.options.direction_slot_count;
        render.direction = request.options.direction;
        render.player_color = std::clamp(request.options.player_color, 1, 8);
        render.player_color_debug = std::clamp(request.options.player_color_debug, 0, 2);
        render.playback_speed = request.options.playback_speed;
        render.playing = request.options.playing;
        render.loop = request.options.loop;
        render.pending_animation = std::move(initial_animation);
        render.transition = AnimationTransitionState::Waiting;
        reg.emplace_or_replace<Aoe2UnitRender>(entity, std::move(render));
        completed.push_back(entity);
    }
    for (auto entity : completed)
        if (reg.valid(entity) && reg.all_of<Aoe2SpawnRequest>(entity)) reg.remove<Aoe2SpawnRequest>(entity);
}

static const Frame* nearest_present(const Animation& animation, const Layer& layer,
                                    int direction, int wanted) {
    const Frame* exact = animation.get(layer, direction, wanted);
    if (exact && exact->present) return exact;
    for (int distance = 1; distance < animation.frames_per_direction; ++distance) {
        const int before = wanted - distance;
        const int after = wanted + distance;
        if (before >= 0) {
            const Frame* frame = animation.get(layer, direction, before);
            if (frame && frame->present) return frame;
        }
        if (after < animation.frames_per_direction) {
            const Frame* frame = animation.get(layer, direction, after);
            if (frame && frame->present) return frame;
        }
    }
    return nullptr;
}

static void resolve_frame(Aoe2UnitRender& render, const Animation& animation, int frame) {
    const Frame* main = nearest_present(animation, animation.main, render.direction, frame);
    const Frame* shadow = nearest_present(animation, animation.shadow, render.direction, frame);
    render.current_frame = frame;
    render.has_main = main != nullptr;
    render.has_shadow = shadow != nullptr;
    if (main) { render.main_frame = *main; render.resolved_foot = main->foot; }
    if (shadow) render.shadow_frame = *shadow;
}

void aoe2_unit_animation_system(EcsWorld& world) {
    const float dt = world.resource_or_add<Time>().dt;
    auto& server = world.resource<AssetServer>();
    auto& reg = world.reg();
    for (auto entity : reg.view<Aoe2UnitRender>()) {
        auto& render = reg.get<Aoe2UnitRender>(entity);
        auto* appearance = render.appearance.get();
        if (!appearance) continue;

        if (!render.pending_animation.empty()) {
            request_animation_residency(server, *appearance, render.pending_animation);
            auto* pending = appearance->find_animation(render.pending_animation);
            if (!pending) {
                render.transition = AnimationTransitionState::Failed;
                render.transition_error = "animation metadata is missing";
                continue;
            }
            poll_residency(*pending);
            if (pending->residency == AnimationResidencyState::Failed) {
                render.transition = AnimationTransitionState::Failed;
                render.transition_error = pending->residency_error;
                continue;
            }
            if (pending->residency != AnimationResidencyState::Ready) {
                render.transition = AnimationTransitionState::Waiting;
                continue;
            }

            render.animation = render.pending_animation;
            render.pending_animation.clear();
            render.transition = AnimationTransitionState::None;
            render.transition_error.clear();
            render.playback_time = 0.f;
            render.direction = render.direction_slot;
            if (render.direction_slot_count > 0)
                render.direction = render.direction_slot * pending->direction_count
                    / render.direction_slot_count;
            render.direction = ((render.direction % pending->direction_count) + pending->direction_count)
                % pending->direction_count;
            resolve_frame(render, *pending, 0);
            ++render.revision;
            continue;
        }

        // A pending transition freezes every part of the active pose. Process
        // restart only when no target atlas is waiting, so even an R press
        // cannot change the old frame before the target commits atomically.
        if (render.restart_requested) {
            render.restart_requested = false;
            render.playback_time = 0.f;
            if (const auto* active = appearance->find_animation(render.animation)) {
                resolve_frame(render, *active, 0);
                ++render.revision;
            }
        }

        const auto* animation = appearance->find_animation(render.animation);
        if (!animation) continue;
        render.direction = ((render.direction % animation->direction_count) + animation->direction_count)
            % animation->direction_count;
        render.player_color = std::clamp(render.player_color, 1, 8);
        if (render.playing) render.playback_time += dt * render.playback_speed;
        int frame = static_cast<int>(std::floor(std::max(0.f, render.playback_time) * animation->fps));
        if (render.loop) frame %= animation->frames_per_direction;
        else frame = std::clamp(frame, 0, animation->frames_per_direction - 1);

        if (frame != render.current_frame) {
            resolve_frame(render, *animation, frame);
            ++render.revision;
        }
    }
}

} // namespace gld::ecs::aoe2
