#pragma once

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

// Static gameplay phase that converts the current navigation waypoint into a
// continuous, speed- and turn-limited world-space velocity request. It never
// advances or otherwise mutates AoeNavigationPath.
struct AoeUnitMovementIntentPhase {};

struct AoeUnitMovementIntentState {
    glm::vec2 velocity{0.f};
    std::uint64_t produced_tick = 0;
    float cached_turn_radians = -1.f;
    float cached_turn_cosine = 1.f;
    float cached_turn_sine = 0.f;
    bool valid = false;
};

struct AoeDefaultUnitMovementIntentPlugin {
    using phase = AoeUnitMovementIntentPhase;
    static constexpr std::string_view name = "default";

    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Limits a candidate world velocity against the Unit's speed and angular
// velocity. Exposed so the fixed pipeline can apply the same rule after local
// avoidance and Global Motion have adjusted the raw path intent.
glm::vec2 aoe_constrain_unit_velocity(
    glm::vec2 previous_velocity, glm::vec2 candidate_velocity,
    float max_speed, float rotation_speed_radians_per_second, float fixed_dt,
    bool* turn_limited = nullptr);

// Common post-GlobalMotion constraint. Kept with the intent phase so custom
// Local Avoidance and Global Motion plugins cannot bypass Unit speed/turning.
void aoe_apply_final_unit_movement_constraints(
    EcsWorld&, std::uint64_t tick);

} // namespace gld::ecs::aoe
