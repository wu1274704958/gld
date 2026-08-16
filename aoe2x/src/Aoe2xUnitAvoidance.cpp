#include <aoe2x/Aoe2xUnitAvoidance.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

#include <aoe/AoeMap.hpp>

namespace gld::ecs::aoe2x {
namespace {
constexpr float Epsilon = 1e-5f;
constexpr std::size_t Empty = static_cast<std::size_t>(-1);

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::uint16_t mapped_score(float fraction) {
    const float clamped = std::clamp(fraction, 0.f, 1.f);
    return static_cast<std::uint16_t>(
        99u - static_cast<unsigned>(std::floor(98.f * clamped)));
}

std::uint16_t priority_for(const Aoe2xUnitStep& step, std::size_t count,
    glm::vec2 ideal_position, const Aoe2xUnitAvoidanceSettings& settings) {
    const float chain_fraction = count > 1u
        ? static_cast<float>(step.chain_index) /
            static_cast<float>(count - 1u) : 0.f;
    const auto chain = mapped_score(chain_fraction);
    const float distance = finite(ideal_position) && finite(step.target)
        ? glm::length(step.target - ideal_position)
        : settings.target_distance_cap;
    const float cap = std::max(settings.target_distance_cap, Epsilon);
    const auto target = mapped_score(distance / cap);
    const std::uint32_t priority =
        static_cast<std::uint32_t>(settings.chain_weight) * chain +
        static_cast<std::uint32_t>(settings.target_distance_weight) * target;
    return static_cast<std::uint16_t>(std::min<std::uint32_t>(
        priority, std::numeric_limits<std::uint16_t>::max()));
}

glm::vec2 static_constrained_displacement(const aoe::AoeLogicMap* map,
    glm::vec2 position, glm::vec2 displacement, glm::vec2 radii) {
    if (!map || !map->valid()) return displacement;
    const float fraction = map->static_safe_fraction(
        position, position + displacement, radii);
    if (fraction >= 1.f - Epsilon) return displacement;
    const glm::vec2 advanced = displacement * fraction;
    const glm::vec2 contact = position + advanced;
    const glm::vec2 remaining = displacement - advanced;
    const glm::vec2 candidates[]{{remaining.x, 0.f}, {0.f, remaining.y}};
    glm::vec2 slide{0.f};
    float slide_length = 0.f;
    for (const auto axis : candidates) {
        const float length = glm::length(axis);
        if (!(length > Epsilon) || length <= slide_length) continue;
        if (map->static_safe_fraction(contact, contact + axis, radii) >=
            1.f - Epsilon) {
            slide = axis;
            slide_length = length;
        }
    }
    if (!(slide_length > Epsilon)) return advanced;
    const glm::vec2 result = advanced + slide;
    return map->static_safe_fraction(position, position + result, radii) >=
        1.f - Epsilon ? result : advanced;
}

std::uint64_t cell_key(int x, int y) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u) |
        static_cast<std::uint32_t>(y);
}

std::size_t hash_key(std::uint64_t key) {
    key ^= key >> 30u;
    key *= 0xbf58476d1ce4e5b9ULL;
    key ^= key >> 27u;
    key *= 0x94d049bb133111ebULL;
    key ^= key >> 31u;
    return static_cast<std::size_t>(key);
}

void prepare_workspace(Aoe2xUnitAvoidanceWorkspace& workspace,
    std::size_t count) {
    workspace.next.resize(count);
    workspace.correction_used.assign(count, 0.f);
    workspace.unresolved.assign(count, 0u);
    const std::size_t required = std::max<std::size_t>(8u,
        std::bit_ceil(std::max<std::size_t>(count * 2u, 1u)));
    if (workspace.grid.size() < required) {
        workspace.grid.assign(required, {});
        workspace.generation = 0u;
    }
    const std::size_t expected_pairs = count * 8u;
    if (workspace.pairs.capacity() < expected_pairs)
        workspace.pairs.reserve(expected_pairs);
}

void begin_grid(Aoe2xUnitAvoidanceWorkspace& workspace) {
    std::fill(workspace.next.begin(), workspace.next.end(), Empty);
    if (++workspace.generation == 0u) {
        for (auto& slot : workspace.grid) slot.generation = 0u;
        workspace.generation = 1u;
    }
}

Aoe2xUnitAvoidanceWorkspace::GridSlot* find_slot(
    Aoe2xUnitAvoidanceWorkspace& workspace, std::uint64_t key, bool create) {
    const std::size_t mask = workspace.grid.size() - 1u;
    std::size_t index = hash_key(key) & mask;
    for (;;) {
        auto& slot = workspace.grid[index];
        if (slot.generation != workspace.generation) {
            if (!create) return nullptr;
            slot.key = key;
            slot.head = Empty;
            slot.generation = workspace.generation;
            return &slot;
        }
        if (slot.key == key) return &slot;
        index = (index + 1u) & mask;
    }
}

glm::vec2 resolved_position(const Aoe2xUnitStep& step) {
    return step.start_position + step.velocity;
}

void insert_unit(std::size_t index, float cell_size,
    std::span<const Aoe2xUnitStep> steps,
    Aoe2xUnitAvoidanceWorkspace& workspace) {
    const glm::vec2 position = resolved_position(steps[index]);
    const int x = static_cast<int>(std::floor(position.x / cell_size));
    const int y = static_cast<int>(std::floor(position.y / cell_size));
    auto* slot = find_slot(workspace, cell_key(x, y), true);
    workspace.next[index] = slot->head;
    slot->head = index;
}

void collect_pairs(std::span<const Aoe2xUnitStep> steps, float cell_size,
    float epsilon, Aoe2xUnitAvoidanceWorkspace& workspace) {
    workspace.pairs.clear();
    begin_grid(workspace);
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const auto& step = steps[index];
        const glm::vec2 position = resolved_position(step);
        const float radius = std::max(step.radii.x, step.radii.y);
        const int cell_x = static_cast<int>(std::floor(position.x / cell_size));
        const int cell_y = static_cast<int>(std::floor(position.y / cell_size));
        for (int y = cell_y - 1; y <= cell_y + 1; ++y)
            for (int x = cell_x - 1; x <= cell_x + 1; ++x) {
                const auto* slot = find_slot(
                    workspace, cell_key(x, y), false);
                if (!slot) continue;
                for (std::size_t other = slot->head; other != Empty;
                     other = workspace.next[other]) {
                    const float other_radius = std::max(
                        steps[other].radii.x, steps[other].radii.y);
                    const float combined = radius + other_radius;
                    const glm::vec2 delta =
                        position - resolved_position(steps[other]);
                    if (glm::dot(delta, delta) + epsilon < combined * combined)
                        workspace.pairs.push_back({other, index});
                }
            }
        insert_unit(index, cell_size, steps, workspace);
    }
}

glm::vec2 deterministic_normal(
    const Aoe2xUnitStep& first, const Aoe2xUnitStep& second) {
    const glm::vec2 delta = resolved_position(second) -
        resolved_position(first);
    if (glm::dot(delta, delta) > Epsilon * Epsilon)
        return glm::normalize(delta);
    const glm::vec2 original = second.start_position - first.start_position;
    if (glm::dot(original, original) > Epsilon * Epsilon)
        return glm::normalize(original);
    static constexpr glm::vec2 directions[]{
        {1.f, 0.f}, {0.f, 1.f}, {-1.f, 0.f}, {0.f, -1.f},
        {.70710678f, .70710678f}, {-.70710678f, .70710678f},
        {-.70710678f, -.70710678f}, {.70710678f, -.70710678f}};
    const auto mixed = static_cast<std::uint32_t>(
        entt::to_integral(first.entity) * 1664525u +
        entt::to_integral(second.entity) * 1013904223u);
    return directions[mixed & 7u];
}

glm::vec2 persistent_projection(const Aoe2xUnitStep& step,
    glm::vec2 push_direction, float distance, float persistence) {
    glm::vec2 result = push_direction * distance;
    const glm::vec2 tangent = step.previous_correction - push_direction *
        glm::dot(step.previous_correction, push_direction);
    const float tangent_squared = glm::dot(tangent, tangent);
    if (tangent_squared > Epsilon * Epsilon)
        result += tangent / std::sqrt(tangent_squared) *
            distance * std::clamp(persistence, 0.f, 1.f);
    return result;
}

float apply_projection(Aoe2xUnitStep& step, std::size_t index,
    glm::vec2 correction, float maximum_correction,
    const aoe::AoeLogicMap* map,
    Aoe2xUnitAvoidanceWorkspace& workspace) {
    const float remaining = maximum_correction -
        workspace.correction_used[index];
    const float length = glm::length(correction);
    if (!(remaining > Epsilon) || !(length > Epsilon)) return 0.f;
    if (length > remaining) correction *= remaining / length;
    const glm::vec2 constrained = static_constrained_displacement(
        map, resolved_position(step), correction, step.radii);
    const float applied = glm::length(constrained);
    if (!(applied > Epsilon)) return 0.f;
    step.velocity += constrained;
    step.frame_correction += constrained;
    workspace.correction_used[index] += applied;
    return applied;
}

bool pair_order(const Aoe2xUnitAvoidanceWorkspace::Pair& lhs,
    const Aoe2xUnitAvoidanceWorkspace::Pair& rhs,
    std::span<const Aoe2xUnitStep> steps) {
    const auto lhs_high = std::max(
        steps[lhs.first].priority, steps[lhs.second].priority);
    const auto rhs_high = std::max(
        steps[rhs.first].priority, steps[rhs.second].priority);
    if (lhs_high != rhs_high) return lhs_high > rhs_high;
    const auto lhs_low = std::min(
        steps[lhs.first].priority, steps[lhs.second].priority);
    const auto rhs_low = std::min(
        steps[rhs.first].priority, steps[rhs.second].priority);
    if (lhs_low != rhs_low) return lhs_low > rhs_low;
    if (lhs.first != rhs.first) return lhs.first < rhs.first;
    return lhs.second < rhs.second;
}
} // namespace

void Aoe2xUnitAvoidanceDiagnostics::record_frame(double elapsed_ms,
    std::uint64_t squad_count, const Aoe2xUnitAvoidanceSolveStats& stats) {
    ++frames;
    squads += squad_count;
    units += stats.units;
    conflicts += stats.conflicts;
    diverted += stats.diverted;
    unresolved += stats.unresolved;
    last_unresolved = stats.unresolved;
    total_ms_ += elapsed_ms;
    average_ms = total_ms_ / static_cast<double>(frames);
    peak_ms = std::max(peak_ms.value_or(0.0), elapsed_ms);
}

void Aoe2xUnitAvoidanceDiagnostics::reset_window() {
    frames = squads = units = conflicts = diverted = unresolved = 0;
    last_unresolved = 0;
    average_ms.reset();
    peak_ms.reset();
    total_ms_ = 0.0;
}

Aoe2xUnitAvoidanceSolveStats resolve_aoe2x_unit_overlaps(
    std::span<Aoe2xUnitStep> steps, float dt,
    const Aoe2xUnitAvoidanceSettings& settings, const aoe::AoeLogicMap* map,
    Aoe2xUnitAvoidanceWorkspace& workspace) {
    Aoe2xUnitAvoidanceSolveStats stats;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    stats.units = steps.size();
#endif
    if (steps.empty() || !(dt > 0.f) || !std::isfinite(dt)) return stats;

    float maximum_radius = Epsilon;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        auto& step = steps[i];
        step.chain_index = i;
        step.resolution = Aoe2xUnitAvoidanceResolution::Ideal;
        step.frame_correction = {0.f, 0.f};
        const glm::vec2 ideal_displacement = static_constrained_displacement(
            map, step.start_position, step.velocity * dt, step.radii);
        step.priority = priority_for(
            step, steps.size(), step.start_position + ideal_displacement,
            settings);
        step.velocity = ideal_displacement;
        maximum_radius = std::max(maximum_radius,
            std::max(step.radii.x, step.radii.y));
    }
    if (!settings.enabled || steps.size() < 2u) {
        for (auto& step : steps) step.velocity /= dt;
        return stats;
    }

    prepare_workspace(workspace, steps.size());
    const float cell_size = std::max(2.f * maximum_radius, Epsilon);
    const float overlap_epsilon = std::max(0.f, settings.overlap_epsilon);
    const std::uint32_t iterations = std::min(settings.solver_iterations, 16u);
    const float persistence = std::clamp(
        settings.correction_persistence, 0.f, 1.f);
    bool final_pairs_current = false;
    for (std::uint32_t iteration = 0; iteration < iterations; ++iteration) {
        collect_pairs(steps, cell_size, overlap_epsilon, workspace);
        if (workspace.pairs.empty()) {
            final_pairs_current = true;
            break;
        }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        if (iteration == 0u) stats.conflicts = workspace.pairs.size();
#endif
        std::stable_sort(workspace.pairs.begin(), workspace.pairs.end(),
            [&](const auto& lhs, const auto& rhs) {
                return pair_order(lhs, rhs, steps);
            });
        for (const auto pair : workspace.pairs) {
            auto& first = steps[pair.first];
            auto& second = steps[pair.second];
            const glm::vec2 normal = deterministic_normal(first, second);
            const float distance = glm::length(
                resolved_position(second) - resolved_position(first));
            const float first_radius = std::max(first.radii.x, first.radii.y);
            const float second_radius = std::max(second.radii.x, second.radii.y);
            const float penetration = first_radius + second_radius - distance;
            if (!(penetration > 0.f)) continue;
            const float separation = penetration + overlap_epsilon;
            // The captain is the squad's pathfinding anchor. Quantised chain
            // scores deliberately produce ties in large squads, but such a
            // tie must never let followers project the anchor off its route.
            const float first_share = first.captain != second.captain
                ? (first.captain ? 0.f : 1.f)
                : (first.priority == second.priority
                    ? .5f : (first.priority < second.priority ? 1.f : 0.f));
            const float second_share = first.captain != second.captain
                ? (second.captain ? 0.f : 1.f)
                : (first.priority == second.priority
                    ? .5f : (second.priority < first.priority ? 1.f : 0.f));
            const float correction_ratio = std::max(
                0.f, settings.maximum_correction_radius_ratio);
            if (first_share > 0.f)
                apply_projection(first, pair.first,
                    persistent_projection(first, -normal,
                        separation * first_share, persistence),
                    first_radius * correction_ratio, map, workspace);
            if (second_share > 0.f)
                apply_projection(second, pair.second,
                    persistent_projection(second, normal,
                        separation * second_share, persistence),
                    second_radius * correction_ratio, map, workspace);
        }
        final_pairs_current = false;
    }

    if (!final_pairs_current)
        collect_pairs(steps, cell_size, overlap_epsilon, workspace);
    for (const auto pair : workspace.pairs) {
        const auto& first = steps[pair.first];
        const auto& second = steps[pair.second];
        if (first.captain != second.captain) {
            workspace.unresolved[first.captain ? pair.second : pair.first] = 1u;
        } else if (first.priority == second.priority) {
            workspace.unresolved[pair.first] = 1u;
            workspace.unresolved[pair.second] = 1u;
        } else if (first.priority < second.priority) {
            workspace.unresolved[pair.first] = 1u;
        } else {
            workspace.unresolved[pair.second] = 1u;
        }
    }
    const float decay = std::clamp(settings.idle_correction_decay, 0.f, 1.f);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        auto& step = steps[i];
        if (workspace.unresolved[i]) {
            step.resolution = Aoe2xUnitAvoidanceResolution::Unresolved;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            ++stats.unresolved;
#endif
        } else if (glm::dot(step.frame_correction, step.frame_correction) >
                   Epsilon * Epsilon) {
            step.resolution = Aoe2xUnitAvoidanceResolution::Diverted;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            ++stats.diverted;
#endif
        }
        step.previous_correction =
            glm::dot(step.frame_correction, step.frame_correction) >
                Epsilon * Epsilon
            ? step.frame_correction : step.previous_correction * decay;
        step.velocity /= dt;
    }
    return stats;
}

} // namespace gld::ecs::aoe2x
