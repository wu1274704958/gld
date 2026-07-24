#pragma once

// Performance monitoring is deliberately opt-in: even small per-entity
// counters and clock reads can distort a stress benchmark. FPS is maintained
// by the core Time system and does not use these macros.
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
#define GLD_PERF_MONITOR(...) do { __VA_ARGS__; } while (false)
#define GLD_PERF_TIME_POINT(name) \
    const auto name = std::chrono::steady_clock::now()
#else
#define GLD_PERF_MONITOR(...) do { } while (false)
#define GLD_PERF_TIME_POINT(name)
#endif

