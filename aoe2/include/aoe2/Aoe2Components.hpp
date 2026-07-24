#pragma once

#include "Aoe2Assets.hpp"
#include <cstdint>
#include <limits>

namespace gld::ecs::aoe2 {

struct SpawnOptions {
    std::string unit_id;
    std::string animation;
    AnimationSlot animation_slot = AnimationSlot::Invalid;
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
    AnimationSlot animation_slot = AnimationSlot::Invalid;
    AnimationSlot pending_animation_slot = AnimationSlot::Invalid;
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

inline constexpr std::uint32_t InvalidAoe2BatchGroup =
    std::numeric_limits<std::uint32_t>::max();

enum class Aoe2BatchSourceKind : std::uint8_t { Main, Shadow };

struct Aoe2BatchSlot {
    std::uint32_t group_id = InvalidAoe2BatchGroup;
    std::uint32_t instance_index = 0;
    bool needs_full_update = false;

    bool valid() const { return group_id != InvalidAoe2BatchGroup; }
    void reset() {
        group_id = InvalidAoe2BatchGroup;
        instance_index = 0;
        needs_full_update = false;
    }
};

// AoE2-only persistent membership. It stores two compact reverse slots and
// snapshots the fields that independently affect per-instance GPU data.
struct Aoe2UnitBatchMembership {
    Aoe2BatchSlot main;
    Aoe2BatchSlot shadow;
    std::uint64_t seen_render_revision = 0;
    std::uint32_t seen_transform_version = 0;
    int seen_player_color = 0;
    int seen_player_color_debug = 0;
    glm::vec4 seen_tint{0.f};
    bool initialized = false;
    const Aoe2UnitAppearance* seen_appearance = nullptr;
    AnimationSlot seen_animation_slot = AnimationSlot::Invalid;
    std::uint64_t seen_batch_resource_key = 0;
    std::uint32_t seen_layers = 0;
    bool seen_visible = false;
    bool seen_has_main = false;
    bool seen_has_shadow = false;
    bool topology_initialized = false;
    std::uint8_t pending_instance_dirty = 0;
};

} // namespace gld::ecs::aoe2
