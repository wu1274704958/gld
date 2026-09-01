#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>

#include <aoe/AoeFormationLayout.hpp>
#include <aoe/AoeUnitAction.hpp>

namespace gld::ecs::aoe {

// A member with this component does not currently execute its own route.
// The component stores only stable member-local chain data. The current route
// source and accumulated segment distance live in the Squad topology so an
// Attach/Detach boundary can switch a contiguous part of a natural chain.
struct AoeFormationFollow : UnitAction {
    static constexpr std::uint8_t DefaultPriority = 1;

    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    AoeUnitTarget target{};
    float distance_from_chain_leader = 0.f;
    float following_distance = 0.f;
    std::uint32_t natural_chain = 0;
    std::uint32_t chain_order = 0;
    std::uint64_t follow_token = 0;
    bool temporary = false;

    AoeFormationFollow() : UnitAction(DefaultPriority) {}
    AoeFormationFollow(entt::entity squad_value,
                       std::uint64_t order_revision_value,
                       AoeUnitTarget target_value,
                       float distance_from_chain_leader_value,
                       float following_distance_value,
                       std::uint32_t natural_chain_value,
                       std::uint32_t chain_order_value,
                       bool temporary_value,
                       std::uint64_t follow_token_value = 0)
        : UnitAction(DefaultPriority), squad(squad_value),
          order_revision(order_revision_value), target(target_value),
          distance_from_chain_leader(distance_from_chain_leader_value),
          following_distance(following_distance_value),
          natural_chain(natural_chain_value), chain_order(chain_order_value),
          follow_token(follow_token_value),
          temporary(temporary_value) {}
};

struct AoeFormationFollowMember {
    AoeUnitTarget unit{};
    glm::vec2 natural_offset{0.f};
    float distance_from_leader = 0.f;
    std::uint32_t chain_order = 0;
};

struct AoeFormationFollowChain {
    std::uint32_t natural_chain = 0;
    glm::vec2 natural_leader_offset{0.f};
    glm::vec2 narrow_leader_offset{0.f};
    std::vector<AoeFormationFollowMember> members;
};

// A contiguous range of one natural chain. The root segment remains on its
// own route; appended segments temporarily follow another lane while keeping
// every relationship inside the range unchanged.
struct AoeFormationFollowSegment {
    std::size_t natural_chain = 0;
    std::size_t first_member = 0;
    std::size_t member_count = 0;
};

struct AoeFormationFollowGroup {
    std::size_t root_chain = 0;
    glm::vec2 lane_offset{0.f};
    // Root prefix first, then contiguous overflow segments appended in this
    // order. Their total member count is the narrowed lane length.
    std::vector<AoeFormationFollowSegment> segments;
};

struct AoeFormationFollowPlan {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t layout_revision = 0;
    std::vector<AoeFormationFollowChain> chains;
    // Static grouping for the initially selected travel layout. Runtime
    // partial attachment state remains exclusively in FollowTopology.
    std::vector<AoeFormationFollowGroup> groups;
    float inter_chain_gap = 0.f;
    bool valid = false;
};

// Persistent progress and compatibility state for one natural chain. Active
// segment mappings are authoritative in FollowTopology::active_segments;
// progress remains the monotonic physical progress of this chain's head.
struct AoeFormationFollowChainBinding {
    std::uint32_t natural_chain = 0;
    std::uint32_t root_chain = 0;
    AoeUnitTarget route_source{};
    AoeUnitTarget preceding_tail{};
    float base_distance = 0.f;
    float progress = 0.f;
    std::uint64_t active_follow_token = 0;
    std::uint64_t last_progress_tick = 0;
    bool attached = false;
};

struct AoeFormationFollowTopology {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t layout_revision = 0;
    std::vector<AoeFormationFollowChainBinding> bindings;
    struct SegmentBinding {
        std::uint64_t follow_token = 0;
        std::uint32_t natural_chain = 0;
        std::uint32_t first_member = 0;
        std::uint32_t member_count = 0;
        std::uint32_t root_chain = 0;
        AoeUnitTarget route_source{};
        AoeUnitTarget preceding_tail{};
        float base_distance = 0.f;
        float following_distance = 0.f;
    };
    std::vector<SegmentBinding> active_segments;
    std::uint32_t attached_segments = 0;
    bool valid = false;
};

struct AoeFormationFollowAssignment {
    AoeUnitTarget unit{};
    AoeFormationFollow follow{};
};

struct AoeFormationDetachFollow : UnitAction {
    static constexpr std::uint8_t DefaultPriority = 1;

    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t follow_token = 0;

    AoeFormationDetachFollow() : UnitAction(DefaultPriority) {}
};

struct AoeFormationFollowAttach {
    std::uint32_t natural_chain = 0;
    std::uint32_t first_member = 0;
    std::uint32_t member_count = 0;
    std::uint32_t root_chain = 0;
    AoeUnitTarget route_source{};
    AoeUnitTarget preceding_tail{};
    float base_distance = 0.f;
    float following_distance = 0.f;
};

enum class AoeUnitActionStepKind : std::uint8_t {
    NavigationPath,
    FormationFollow,
    FormationDetachFollow,
};

// RouteSplit owns this deterministic timeline. Progress values refer to the
// shared formation trajectory; the contained components remain normal ECS
// actions and are materialized by MovingControl at their planned boundary.
struct AoeUnitActionStep {
    AoeUnitActionStepKind kind = AoeUnitActionStepKind::NavigationPath;
    float begin_progress = 0.f;
    float end_progress = 0.f;
    std::uint64_t follow_token = 0;
    AoeFormationFollowAttach attach{};
};

struct AoeUnitActionChain {
    entt::entity squad{entt::null};
    std::uint64_t order_revision = 0;
    std::uint64_t unit_instance_id = 0;
    std::uint32_t natural_chain = 0;
    std::uint32_t member_index = 0;
    std::vector<AoeUnitActionStep> steps;
    std::size_t current = 0;
    std::uint64_t active_follow_token = 0;
    bool valid = false;
};

// Builds stable natural column chains and maps C natural columns onto K
// narrow lanes. This is pure layout work and performs no registry access.
std::optional<AoeFormationFollowPlan> make_formation_follow_plan(
    const AoeFormationLayout& natural_layout,
    const AoeFormationLayout& narrow_layout,
    entt::entity squad, std::uint64_t order_revision,
    std::uint64_t layout_revision);

// Produces the stable natural followers. Natural leaders are absent; temporary
// leader relationships are materialized from action payloads at runtime.
std::vector<AoeFormationFollowAssignment>
make_formation_follow_assignments(const AoeFormationFollowPlan&);

// Initializes every chain to its natural route source and zero base distance.
std::optional<AoeFormationFollowTopology>
make_formation_follow_topology(const AoeFormationFollowPlan&);

// Maps every member onto the lane centers of an arbitrary width stage. Each
// lane retains one natural root and receives contiguous overflow segments so
// lane lengths differ by at most one member.
std::optional<std::vector<AoeFormationFollowGroup>>
make_formation_follow_groups(
    const AoeFormationFollowPlan& plan,
    const AoeFormationLayout& lane_leader_layout);

} // namespace gld::ecs::aoe
