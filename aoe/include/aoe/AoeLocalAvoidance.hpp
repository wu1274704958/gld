#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

class AoeLogicMap;

struct AoeLocalAvoidancePhase {};

struct AoeSteeringNeighbor {
    entt::entity entity{entt::null};
    std::uint64_t instance_id = 0;
    glm::vec2 position{0.f};
    glm::vec2 radii{0.f};
    glm::vec2 velocity{0.f};
};

struct AoeSteeringContext {
    entt::entity subject{entt::null};
    std::uint64_t instance_id = 0;
    glm::vec2 position{0.f};
    glm::vec2 radii{0.f};
    glm::vec2 current_velocity{0.f};
    glm::vec2 preferred_velocity{0.f};
    glm::vec2 goal{0.f};
    float max_speed = 0.f;
    float prediction_seconds = .6f;
    float separation_padding = .15f;
    const AoeLogicMap* map = nullptr;
    std::span<const AoeSteeringNeighbor> neighbors{};
    int preferred_avoidance_side = 0;
    float side_switch_margin = .35f;
    float candidate_angle_step = .39269908169f;
    float candidate_max_angle = .78539816339f;
    float minimum_safe_fraction = .05f;
};

struct AoeSteeringResult {
    glm::vec2 target_velocity{0.f};
    int avoidance_side = 0;
    bool threatened = false;
    bool infeasible = false;
};

struct AoeLocalAvoidanceSettings {
    std::uint32_t max_neighbors = 8;
    std::uint32_t full_solve_interval = 2;
    std::uint32_t side_hold_ticks = 6;
    std::uint32_t escape_stalled_ticks = 4;
    float prediction_seconds = .6f;
    float separation_padding = .15f;
    float imminent_collision_seconds = .2f;
    float side_switch_margin = .35f;
    float candidate_angle_step = .39269908169f;
    float normal_max_angle = .78539816339f;
    float escape_max_angle = 1.57079632679f;
    float minimum_safe_fraction = .05f;
};

struct AoeLocalAvoidanceState {
    glm::vec2 cached_target_velocity{0.f};
    std::uint64_t last_steering_tick = 0;
    std::uint64_t threat_signature = 0;
    std::int8_t avoidance_side = 0;
    std::uint8_t avoidance_side_hold_ticks = 0;
    bool escape_steering = false;
    bool infeasible = false;
};

struct AoeLocalAvoidanceScratch {
    std::vector<AoeSteeringNeighbor> nearest_neighbors;
};

struct DefaultLocalSteeringLogic {
    static AoeSteeringResult steer(const AoeSteeringContext&);
};

struct AoeFullLocalAvoidancePlugin {
    using phase = AoeLocalAvoidancePhase;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

struct AoePassThroughLocalAvoidancePlugin {
    using phase = AoeLocalAvoidancePhase;
    static constexpr std::string_view name = "pass_through";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
