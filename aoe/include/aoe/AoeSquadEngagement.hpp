#pragma once

#include <cstdint>
#include <string_view>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

// Static gameplay phase responsible for Squad AttackMove target acquisition
// and for maintaining the member attack orders created by that policy.
struct AoeSquadEngagementPhase {};

enum class AoeSquadEngagementStatus : std::uint8_t {
    Inactive,
    Active,
};

// Produced for a Squad by the full engagement plugin. Consumers must only use
// a valid result from the current fixed tick; a missing/stale result is the
// pass-through behavior and means that formation travel should continue.
struct AoeSquadEngagementResult {
    AoeSquadEngagementStatus status =
        AoeSquadEngagementStatus::Inactive;
    std::uint32_t active_members = 0;
    std::uint64_t produced_tick = 0;
    bool valid = false;
};

struct AoeFullSquadEngagementPlugin {
    using phase = AoeSquadEngagementPhase;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Deliberately empty performance-floor implementation. With no current-tick
// result, Squad AttackMove is forwarded to ordinary formation travel.
struct AoePassThroughSquadEngagementPlugin {
    using phase = AoeSquadEngagementPhase;
    static constexpr std::string_view name = "pass_through";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
