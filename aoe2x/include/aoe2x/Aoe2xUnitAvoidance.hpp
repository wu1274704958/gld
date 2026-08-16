#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/vec2.hpp>

namespace gld::ecs::aoe {
class AoeLogicMap;
}

namespace gld::ecs::aoe2x {

enum class Aoe2xUnitAvoidanceResolution : std::uint8_t {
    Ideal,
    Diverted,
    Unresolved
};

// Transient formation motion record. Formation planning fills the first six
// fields; the avoidance solver annotates and, when necessary, changes velocity
// before FormationSystem commits the step to ECS components.
struct Aoe2xUnitStep {
    entt::entity entity{entt::null};
    glm::vec2 velocity{0.f};
    glm::vec2 target{0.f};
    glm::vec2 direction{1.f, 0.f};
    float speed_limit = 0.f;
    bool captain = false;

    glm::vec2 start_position{0.f};
    glm::vec2 radii{0.f};
    std::size_t chain_index = 0;
    std::uint16_t priority = 0;
    Aoe2xUnitAvoidanceResolution resolution =
        Aoe2xUnitAvoidanceResolution::Ideal;
    glm::vec2 previous_correction{0.f};
    glm::vec2 frame_correction{0.f};
};

struct Aoe2xUnitAvoidanceState {
    glm::vec2 previous_correction{0.f};
};

struct Aoe2xUnitAvoidanceSettings {
    bool enabled = true;
    std::uint16_t chain_weight = 70;
    std::uint16_t target_distance_weight = 30;
    float target_distance_cap = 1000.f;
    float overlap_epsilon = 1e-5f;
    std::uint32_t solver_iterations = 3;
    // Total projection applied to one unit in a tick is capped to this many
    // dynamic collider radii. 1 permits a full-radius correction.
    float maximum_correction_radius_ratio = 1.f;
    // Retains a small tangential part of last tick's correction without
    // reducing the minimum normal displacement needed to separate a pair.
    float correction_persistence = .2f;
    float idle_correction_decay = .5f;
};

struct Aoe2xUnitAvoidanceSolveStats {
    std::uint64_t units = 0;
    std::uint64_t conflicts = 0;
    std::uint64_t diverted = 0;
    std::uint64_t unresolved = 0;
};

// Populated only by monitoring-enabled aoe2x builds. A normal build never
// creates this resource, reads a clock, or increments profiling counters.
struct Aoe2xUnitAvoidanceDiagnostics {
    std::uint64_t frames = 0;
    std::uint64_t squads = 0;
    std::uint64_t units = 0;
    std::uint64_t conflicts = 0;
    std::uint64_t diverted = 0;
    std::uint64_t unresolved = 0;
    std::uint64_t last_unresolved = 0;
    std::optional<double> average_ms;
    std::optional<double> peak_ms;

    void record_frame(double elapsed_ms, std::uint64_t squad_count,
        const Aoe2xUnitAvoidanceSolveStats&);
    void reset_window();

private:
    double total_ms_ = 0.0;
};

// Reused between squads. The open-addressed grid uses generation stamps, so a
// solve clears logical state without clearing or reallocating every bucket.
struct Aoe2xUnitAvoidanceWorkspace {
    struct Pair {
        std::size_t first = 0;
        std::size_t second = 0;
    };

    struct GridSlot {
        std::uint64_t key = 0;
        std::size_t head = static_cast<std::size_t>(-1);
        std::uint32_t generation = 0;
    };

    std::vector<std::size_t> next;
    std::vector<GridSlot> grid;
    std::vector<Pair> pairs;
    std::vector<float> correction_used;
    std::vector<std::uint8_t> unresolved;
    std::uint32_t generation = 0;
};

Aoe2xUnitAvoidanceSolveStats resolve_aoe2x_unit_overlaps(
    std::span<Aoe2xUnitStep> steps, float dt,
    const Aoe2xUnitAvoidanceSettings&, const aoe::AoeLogicMap*,
    Aoe2xUnitAvoidanceWorkspace&);

} // namespace gld::ecs::aoe2x
