#pragma once

#include <string>

#include <aoe/AoeGameplay.hpp>
#include <aoe2/Aoe2Plugin.hpp>

namespace gld::ecs::aoe2_gameplay {

struct Aoe2PresentationLink { entt::entity render{entt::null}; };
struct AoeGameplayOwner { entt::entity gameplay{entt::null}; };
struct Aoe2ProjectilePresentationLink { entt::entity render{entt::null}; };
struct AoeProjectileOwner { entt::entity gameplay{entt::null}; };
struct AoePresentationError { std::string message; };

struct Aoe2GameplayBridgePerformanceDiagnostics {
    double orphan_cleanup_ms = 0.0;
    double unit_presentation_ms = 0.0;
    double projectile_presentation_ms = 0.0;

    void begin_frame() { *this = {}; }
};

struct Aoe2PresentationSnapshot {
    aoe::UnitState state = aoe::UnitState::Idle;
    std::uint64_t sequence = 0;
    std::uint64_t last_gameplay_tick = 0;
    double playback_time = 0.0;
    double locomotion_distance = 0.0;
    bool critical = false;
    int direction = 0;
    int direction_count = 16;
    int player_color = 1;
    std::string requested_animation;
};

struct Aoe2ProjectilePresentationSnapshot {
    int direction = 0;
    int pitch_frame = 5;
};

struct Aoe2GameplayBridgePlugin {
    void operator()(App& app) const;
};

void aoe2_gameplay_orphan_cleanup_system(EcsWorld&);
void aoe2_gameplay_presentation_system(EcsWorld&);
void aoe2_projectile_presentation_system(EcsWorld&);
int aoe2_projectile_direction(glm::vec2 logical_velocity,
                              int direction_count = 32);
int aoe2_projectile_pitch_frame(glm::vec3 velocity,
                                int frame_count = 11);

} // namespace gld::ecs::aoe2_gameplay
