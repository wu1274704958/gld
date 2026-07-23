#pragma once

#include "Aoe2Assets.hpp"
#include <cstdint>

namespace gld::ecs::aoe2 {

struct SpawnOptions {
    std::string unit_id;
    std::string animation;
    int direction = 0;
    int direction_slot_count = 0; // optional preview-space direction count
    int player_color = 1;
    int player_color_debug = 0;
    float playback_speed = 1.f;
    bool playing = true;
    bool loop = true;
    std::uint32_t layers = 0x1u;
};

struct Aoe2SpawnRequest {
    SpawnOptions options;
    Handle<Aoe2UnitAppearance> appearance;
    bool requested = false;
};

struct Aoe2SpawnError { std::string message; };

enum class AnimationTransitionState { None, Waiting, Failed };

struct Aoe2UnitRender {
    Handle<Aoe2UnitAppearance> appearance;
    std::string animation;
    std::string pending_animation;
    AnimationTransitionState transition = AnimationTransitionState::None;
    std::string transition_error;
    int direction_slot = 0;
    int direction_slot_count = 0;
    int direction = 0;
    int player_color = 1;
    int player_color_debug = 0; // 0 normal, 1 mask coverage, 2 subcolor indices
    float playback_time = 0.f;
    float playback_speed = 1.f;
    bool playing = true;
    bool loop = true;
    bool visible = true;
    bool restart_requested = false;
    glm::vec4 tint{1.f};
    int current_frame = 0;
    Frame main_frame;
    Frame shadow_frame;
    bool has_main = false;
    bool has_shadow = false;
    glm::vec2 resolved_foot{0.f};
    std::uint64_t revision = 1;
};

} // namespace gld::ecs::aoe2
