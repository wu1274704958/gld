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
    std::uint32_t active_units = 0;
    std::uint64_t fixed_ticks = 0;
    std::uint64_t authoritative_corrections = 0;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    double upload_ms = 0.0;
    double dispatch_ms = 0.0;
    double readback_ms = 0.0;
    double last_tick_ms = 0.0;
#endif
};

struct AoeGpuMotionPlugin {
    std::string shader_root = "shaders/aoe_gpu_motion";
    void operator()(App& app) const;
};

} // namespace gld::ecs::aoe
