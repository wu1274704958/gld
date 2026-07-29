#pragma once

#include <cstdint>
#include <string>

#include <aoe/AoeGameplay.hpp>

namespace gld::ecs::aoe {

struct AoeGpuMotionDiagnostics {
    bool available = false;
    std::string unavailable_reason;
    std::uint32_t map_width_pixels = 0;
    std::uint32_t map_height_pixels = 0;
    std::uint32_t active_width_pixels = 0;
    std::uint32_t active_height_pixels = 0;
    std::uint32_t active_units = 0;
    bool solve_required = false;
    std::uint64_t fixed_ticks = 0;
    std::uint64_t solve_required_ticks = 0;
    std::uint64_t submissions_queued = 0;
    std::uint64_t authoritative_corrections = 0;
    std::uint64_t async_results_applied = 0;
    std::uint64_t async_deadline_misses = 0;
    std::uint64_t async_submissions_skipped = 0;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    double upload_ms = 0.0;
    double dispatch_ms = 0.0;
    double readback_ms = 0.0;
    double last_tick_ms = 0.0;
#endif
};

struct AoeGpuMotionPlugin {
    std::string shader_root = "shaders/aoe_gpu_motion";
    // Eight pixels per world unit gives 0.125-unit footprint precision while
    // reducing full-map image work to one quarter of the original 16px grid.
    float pixels_per_world_unit = 8.f;
    double result_fence_budget_ms = 1.0;
    // CPU gameplay and final safety remain 30 Hz. The global cooperative field
    // may run at a lower configurable cadence and is consumed between solves.
    std::uint32_t solve_interval_ticks = 2;
    void operator()(App& app) const;
};

} // namespace gld::ecs::aoe
