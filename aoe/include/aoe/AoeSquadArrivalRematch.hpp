#pragma once

#include <cstdint>
#include <string_view>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

// Static gameplay phase responsible for the optional, one-shot reassignment
// of squad members to same-priority formation slots at AttackMove arrival.
struct AoeSquadArrivalRematchPhase {};

struct AoeSquadArrivalRematchRequest {
    std::uint64_t order_revision = 0;
    std::uint64_t requested_tick = 0;
    bool valid = false;
};

enum class AoeSquadArrivalRematchStatus : std::uint8_t {
    None,
    Applied,
    Failed,
};

struct AoeSquadArrivalRematchResult {
    AoeSquadArrivalRematchStatus status =
        AoeSquadArrivalRematchStatus::None;
    std::uint64_t order_revision = 0;
    std::uint64_t produced_tick = 0;
    std::uint32_t member_count = 0;
    bool valid = false;
};

// Production implementation. A valid request is consumed once and produces a
// result for Formation to observe on the next fixed tick.
struct AoeFullSquadArrivalRematchPlugin {
    using phase = AoeSquadArrivalRematchPhase;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Deliberately empty fallback. Formation leaves one persistent request on the
// squad, which suppresses retries while members continue toward original slots.
struct AoePassThroughSquadArrivalRematchPlugin {
    using phase = AoeSquadArrivalRematchPhase;
    static constexpr std::string_view name = "pass_through";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
