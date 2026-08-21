#pragma once

#include <cstdint>
#include <string_view>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

struct AoeGlobalMotionPhase {};

// Uses the late-bound planner registry. The CPU unit-flow planner is always
// available, while aoe_gpu_motion may register gpu_image at application setup.
struct AoeDefaultGlobalMotionPlugin {
    using phase = AoeGlobalMotionPhase;
    static constexpr std::string_view name = "default";
    static constexpr bool uses_runtime_planner = true;
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Diagnostic route-playback backend: follows raw path velocity with
// acceleration limiting, but deliberately performs no local, dynamic-unit or
// static-geometry collision correction.
struct AoePassThroughGlobalMotionPlugin {
    using phase = AoeGlobalMotionPhase;
    static constexpr std::string_view name = "pass_through";
    static constexpr bool uses_runtime_planner = false;
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
