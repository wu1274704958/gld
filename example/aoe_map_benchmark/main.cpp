#include <aoe/AoeGameplay.hpp>

#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

using namespace gld::ecs::aoe;

namespace {
float safe_fraction(glm::vec2 from, glm::vec2 to, glm::vec2 radii,
                    const std::vector<AoeSteeringNeighbor>& neighbors) {
    float result = 1.f;
    const glm::vec2 delta = to - from;
    for (const auto& neighbor : neighbors) {
        const glm::vec2 combined = radii + neighbor.radii;
        const glm::vec2 start = from - neighbor.position;
        const float rx2 = combined.x * combined.x;
        const float ry2 = combined.y * combined.y;
        const float a = delta.x * delta.x / rx2 + delta.y * delta.y / ry2;
        const float b = 2.f * (start.x * delta.x / rx2 +
                               start.y * delta.y / ry2);
        const float c = start.x * start.x / rx2 +
                        start.y * start.y / ry2 - 1.f;
        float enter = 1.f;
        if (c <= 0.f) enter = b >= -1e-5f ? 1.f : 0.f;
        else if (a > 1e-5f) {
            const float discriminant = b * b - 4.f * a * c;
            if (discriminant >= 0.f) {
                const float value = (-b - std::sqrt(discriminant)) / (2.f * a);
                if (value >= 0.f && value <= 1.f) enter = value;
            }
        }
        result = std::min(result,
            enter < 1.f ? std::max(0.f, enter - .0001f) : 1.f);
    }
    return result;
}
} // namespace

int main() {
    constexpr std::size_t UnitCount = 100000;
    AoeMapDefinition definition;
    definition.id = "benchmark";
    definition.width = 512;
    definition.height = 512;
    definition.tile_size = 1.f;
    definition.heights.resize(513u * 513u, 0.f);
    AoeLogicMap map(definition);
    AoeDynamicObstacleIndex index;
    index.reserve(UnitCount);
    std::mt19937 random(12345);
    std::uniform_real_distribution<float> coordinate(.5f, 511.5f);
    const auto started = std::chrono::steady_clock::now();
    index.reset(map);
    for (std::size_t i = 0; i < UnitCount; ++i)
        index.insert(map, {
            static_cast<entt::entity>(static_cast<std::uint32_t>(i + 1)),
            i + 1, entt::null, {coordinate(random), coordinate(random)},
            {.2f, .2f}});
    index.finalize(map);
    const auto built = std::chrono::steady_clock::now();
    std::uint64_t hits = 0;
    for (int i = 0; i < 10000; ++i) {
        const glm::vec2 center{coordinate(random), coordinate(random)};
        index.query(map, center - glm::vec2(2.f), center + glm::vec2(2.f),
                    [&](const auto&) { ++hits; });
    }
    const auto completed = std::chrono::steady_clock::now();
    const auto milliseconds = [](auto from, auto to) {
        return std::chrono::duration<double, std::milli>(to - from).count();
    };
    const auto& diagnostics = index.diagnostics();
    std::printf(
        "units=%llu memberships=%llu build_ms=%.3f queries=%llu "
        "candidates=%llu hits=%llu query_ms=%.3f\n",
        static_cast<unsigned long long>(diagnostics.units_indexed),
        static_cast<unsigned long long>(diagnostics.cell_memberships),
        milliseconds(started, built),
        static_cast<unsigned long long>(diagnostics.queries),
        static_cast<unsigned long long>(diagnostics.candidates),
        static_cast<unsigned long long>(hits),
        milliseconds(built, completed));

    // A headless 30 Hz crowd broad-phase workload. This intentionally keeps
    // rendering and asset loading out of the number so steering regressions
    // are visible independently of AoE2 batching.
    constexpr std::size_t CrowdCount = 30000;
    constexpr int CrowdTicks = 30;
    struct Agent { glm::vec2 position; glm::vec2 velocity; };
    std::vector<Agent> agents(CrowdCount);
    for (std::size_t i = 0; i < agents.size(); ++i) {
        const float x = 8.f + static_cast<float>(i % 300u) * 1.6f;
        const float y = 8.f + static_cast<float>(i / 300u) * 1.6f;
        agents[i] = {{x, y}, {(i & 1u) ? 1.f : -1.f, 0.f}};
    }
    std::vector<AoeSteeringNeighbor> nearest;
    nearest.reserve(8);
    const auto crowd_started = std::chrono::steady_clock::now();
    std::uint64_t crowd_candidates = 0;
    for (int tick = 0; tick < CrowdTicks; ++tick) {
        index.reset(map);
        index.reserve(CrowdCount);
        for (std::size_t i = 0; i < agents.size(); ++i)
            index.insert(map, {
                static_cast<entt::entity>(static_cast<std::uint32_t>(i + 1)),
                i + 1, entt::null, agents[i].position, {.2f, .2f},
                agents[i].velocity});
        index.finalize(map);
        for (std::size_t i = 0; i < agents.size(); ++i) {
            nearest.clear();
            index.query(map, agents[i].position - glm::vec2(1.2f),
                agents[i].position + glm::vec2(1.2f),
                [&](const AoeDynamicObstacleEntry& obstacle) {
                    if (obstacle.instance_id == i + 1 || nearest.size() == 8)
                        return;
                    nearest.push_back({obstacle.entity, obstacle.instance_id,
                        obstacle.center, obstacle.radii, obstacle.velocity});
                    ++crowd_candidates;
                });
            const glm::vec2 preferred = agents[i].velocity.x >= 0.f
                ? glm::vec2{1.f, 0.f} : glm::vec2{-1.f, 0.f};
            const auto result = DefaultLocalSteeringLogic::steer({
                .instance_id = i + 1,
                .position = agents[i].position,
                .radii = {.2f, .2f},
                .current_velocity = agents[i].velocity,
                .preferred_velocity = preferred,
                .goal = agents[i].position + preferred * 100.f,
                .max_speed = 1.f,
                .neighbors = nearest});
            agents[i].velocity = result.target_velocity;
            const glm::vec2 candidate = agents[i].position +
                agents[i].velocity * (1.f / 30.f);
            const float safe = safe_fraction(
                agents[i].position, candidate, {.2f, .2f}, nearest);
            agents[i].position += agents[i].velocity * (1.f / 30.f) * safe;
        }
    }
    const double crowd_ms = milliseconds(
        crowd_started, std::chrono::steady_clock::now());
    std::printf(
        "crowd_units=%llu ticks=%d candidates=%llu total_ms=%.3f "
        "average_tick_ms=%.3f\n",
        static_cast<unsigned long long>(CrowdCount), CrowdTicks,
        static_cast<unsigned long long>(crowd_candidates), crowd_ms,
        crowd_ms / CrowdTicks);
    return index.diagnostics().units_indexed == CrowdCount ? 0 : 1;
}
