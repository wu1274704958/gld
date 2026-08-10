#pragma once

#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

namespace gld::ecs {
class App;
struct EcsWorld;
}

namespace gld::ecs::aoe {

// Static gameplay phase responsible for turning squad-level formation state
// into stable per-member movement goals.
struct AoeFormationPhase {};

enum class AoeFormationIntentMode : std::uint8_t {
    None, Form, Regroup, Follow, Hold, Arrive
};

struct AoeFormationIntent {
    AoeFormationIntentMode mode = AoeFormationIntentMode::None;
    glm::vec2 center_target{0.f};
    glm::vec2 forward{1.f, 0.f};
    float speed_limit = 0.f;
    std::uint64_t order_revision = 0;
    std::uint64_t produced_tick = 0;
    bool anchor_arrived = false;
    bool allow_arrival_rematch = false;
    bool valid = false;
};

enum class AoeFormationResultStatus : std::uint8_t { None, Ready, Failed };

struct AoeFormationResult {
    AoeFormationResultStatus status = AoeFormationResultStatus::None;
    std::uint64_t produced_tick = 0;
    bool members_arrived = false;
    bool rematched = false;
    bool valid = false;
};

// Production implementation: moving/elastic slots, speed limiting and
// publication/consumption of the optional arrival-rematch contract.
struct AoeFullFormationPlugin {
    using phase = AoeFormationPhase;
    static constexpr std::string_view name = "full";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

// Performance-floor implementation. It lays out a squad once and gives every
// member one stable destination per order episode. Individual navigation is
// deliberately retained, so map/A* cost remains observable.
struct AoePassThroughFormationPlugin {
    using phase = AoeFormationPhase;
    static constexpr std::string_view name = "pass_through";
    static void install(App&);
    static void fixed_tick(EcsWorld&, std::uint64_t tick);
};

} // namespace gld::ecs::aoe
