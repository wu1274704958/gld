#include <aoe/AoeFormationFollow.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

std::optional<std::vector<glm::vec2>> chain_leader_offsets(
    const AoeFormationLayout& layout) {
    if (layout.slots.empty()) return std::nullopt;
    std::uint32_t maximum_chain = 0;
    for (const auto& slot : layout.slots) {
        if (slot.unit.entity == entt::null || !finite(slot.local_offset))
            return std::nullopt;
        maximum_chain = std::max(maximum_chain, slot.chain_index);
    }
    std::vector<glm::vec2> result(
        static_cast<std::size_t>(maximum_chain) + 1);
    std::vector<bool> found(result.size(), false);
    std::vector<std::uint32_t> best_order(
        result.size(), std::numeric_limits<std::uint32_t>::max());
    for (const auto& slot : layout.slots) {
        const std::size_t chain = slot.chain_index;
        if (!found[chain] || slot.chain_order < best_order[chain]) {
            result[chain] = slot.local_offset;
            best_order[chain] = slot.chain_order;
            found[chain] = true;
        }
    }
    if (std::find(found.begin(), found.end(), false) != found.end())
        return std::nullopt;
    std::stable_sort(result.begin(), result.end(),
        [](glm::vec2 left, glm::vec2 right) {
            return left.x < right.x;
        });
    return result;
}

std::vector<std::vector<std::size_t>> assign_chains_to_lanes(
    const std::vector<glm::vec2>& chain_offsets,
    const std::vector<glm::vec2>& lane_offsets) {
    std::vector<std::vector<std::size_t>> result(lane_offsets.size());
    if (lane_offsets.empty() || chain_offsets.size() < lane_offsets.size())
        return {};
    std::vector<bool> assigned(chain_offsets.size(), false);

    // Reserve one nearest natural column for every generated lane first.
    // Independent nearest-neighbour assignment can leave holes whenever the
    // natural and narrowed layouts have different parity (for example 50 to
    // 25 columns). Remaining columns then use the ordinary nearest lane.
    for (std::size_t lane = 0; lane < lane_offsets.size(); ++lane) {
        std::size_t best = chain_offsets.size();
        float best_distance = std::numeric_limits<float>::infinity();
        for (std::size_t chain = 0; chain < chain_offsets.size(); ++chain) {
            if (assigned[chain]) continue;
            const float distance = std::abs(
                chain_offsets[chain].x - lane_offsets[lane].x);
            if (distance < best_distance) {
                best_distance = distance;
                best = chain;
            }
        }
        if (best == chain_offsets.size()) return {};
        assigned[best] = true;
        result[lane].push_back(best);
    }
    for (std::size_t chain = 0; chain < chain_offsets.size(); ++chain) {
        if (assigned[chain]) continue;
        std::size_t best = 0;
        float best_distance = std::numeric_limits<float>::infinity();
        for (std::size_t lane = 0; lane < lane_offsets.size(); ++lane) {
            const float distance = std::abs(
                chain_offsets[chain].x - lane_offsets[lane].x);
            if (distance < best_distance) {
                best_distance = distance;
                best = lane;
            }
        }
        result[best].push_back(chain);
    }
    return result;
}
} // namespace

std::optional<AoeFormationFollowPlan> make_formation_follow_plan(
    const AoeFormationLayout& natural_layout,
    const AoeFormationLayout& narrow_layout,
    entt::entity squad, std::uint64_t order_revision,
    std::uint64_t layout_revision) {
    if (squad == entt::null || natural_layout.slots.empty() ||
        narrow_layout.slots.empty())
        return std::nullopt;

    std::uint32_t maximum_chain = 0;
    std::unordered_set<entt::entity> unique_units;
    unique_units.reserve(natural_layout.slots.size());
    for (const auto& slot : natural_layout.slots) {
        if (slot.unit.entity == entt::null || !finite(slot.local_offset) ||
            !unique_units.emplace(slot.unit.entity).second)
            return std::nullopt;
        maximum_chain = std::max(maximum_chain, slot.chain_index);
    }

    AoeFormationFollowPlan result;
    result.squad = squad;
    result.order_revision = order_revision;
    result.layout_revision = layout_revision;
    result.chains.resize(static_cast<std::size_t>(maximum_chain) + 1);
    for (std::size_t index = 0; index < result.chains.size(); ++index)
        result.chains[index].natural_chain =
            static_cast<std::uint32_t>(index);

    for (const auto& slot : natural_layout.slots) {
        auto& chain = result.chains[slot.chain_index];
        chain.members.push_back({slot.unit, slot.local_offset, 0.f,
                                 slot.chain_order});
    }
    for (auto& chain : result.chains) {
        if (chain.members.empty()) return std::nullopt;
        std::stable_sort(chain.members.begin(), chain.members.end(),
            [](const auto& left, const auto& right) {
                return left.chain_order < right.chain_order;
            });
        for (std::size_t index = 1; index < chain.members.size(); ++index) {
            if (chain.members[index - 1].chain_order ==
                chain.members[index].chain_order)
                return std::nullopt;
            const float step = glm::length(
                chain.members[index].natural_offset -
                chain.members[index - 1].natural_offset);
            if (!std::isfinite(step) || step <= Epsilon)
                return std::nullopt;
            chain.members[index].distance_from_leader =
                chain.members[index - 1].distance_from_leader + step;
            if (!(result.inter_chain_gap > Epsilon))
                result.inter_chain_gap = step;
            else
                result.inter_chain_gap =
                    std::min(result.inter_chain_gap, step);
        }
        chain.natural_leader_offset = chain.members.front().natural_offset;
    }
    std::stable_sort(result.chains.begin(), result.chains.end(),
        [](const auto& left, const auto& right) {
            return left.natural_leader_offset.x <
                   right.natural_leader_offset.x;
        });
    for (std::size_t index = 0; index < result.chains.size(); ++index)
        result.chains[index].natural_chain =
            static_cast<std::uint32_t>(index);
    if (!(result.inter_chain_gap > Epsilon)) {
        for (std::size_t index = 1; index < result.chains.size(); ++index) {
            const float step = std::abs(
                result.chains[index].natural_leader_offset.x -
                result.chains[index - 1].natural_leader_offset.x);
            if (step > Epsilon)
                result.inter_chain_gap = result.inter_chain_gap > Epsilon
                    ? std::min(result.inter_chain_gap, step) : step;
        }
    }
    if (!(result.inter_chain_gap > Epsilon)) result.inter_chain_gap = .75f;

    auto lane_offsets = chain_leader_offsets(narrow_layout);
    if (!lane_offsets || lane_offsets->empty()) return std::nullopt;
    const std::size_t natural_count = result.chains.size();
    const std::size_t lane_count = std::min(
        natural_count, lane_offsets->size());
    result.groups.resize(lane_count);
    for (std::size_t lane = 0; lane < lane_count; ++lane)
        result.groups[lane].lane_offset = (*lane_offsets)[lane];

    std::vector<glm::vec2> natural_offsets;
    natural_offsets.reserve(result.chains.size());
    for (const auto& chain : result.chains)
        natural_offsets.push_back(chain.natural_leader_offset);
    const auto assignments = assign_chains_to_lanes(
        natural_offsets, *lane_offsets);
    if (assignments.size() != lane_count) return std::nullopt;
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        result.groups[lane].chains = assignments[lane];
        for (const std::size_t chain : assignments[lane])
            result.chains[chain].narrow_leader_offset =
                result.groups[lane].lane_offset;
    }
    for (auto& group : result.groups) {
        if (group.chains.empty()) return std::nullopt;
        const auto root = *std::min_element(
            group.chains.begin(), group.chains.end(),
            [&](std::size_t left, std::size_t right) {
                const float left_distance = std::abs(
                    result.chains[left].natural_leader_offset.x -
                    group.lane_offset.x);
                const float right_distance = std::abs(
                    result.chains[right].natural_leader_offset.x -
                    group.lane_offset.x);
                return left_distance != right_distance
                    ? left_distance < right_distance : left < right;
            });
        group.root_chain = root;
        // Width compression changes lane ownership, not the route-progress
        // origin of the column heads. The extra rows of a narrow generated
        // layout are represented by temporary head-to-tail follows; copying
        // that layout's front-row Y here would incorrectly teleport every
        // retained head far ahead on long (large-unit) columns.
        group.lane_offset.y =
            result.chains[root].natural_leader_offset.y;
        for (const std::size_t chain : group.chains)
            result.chains[chain].narrow_leader_offset = group.lane_offset;
        std::stable_sort(group.chains.begin(), group.chains.end(),
            [&](std::size_t left, std::size_t right) {
                if (left == right) return false;
                if (left == root) return true;
                if (right == root) return false;
                const float root_x =
                    result.chains[root].natural_leader_offset.x;
                const float left_distance = std::abs(
                    result.chains[left].natural_leader_offset.x - root_x);
                const float right_distance = std::abs(
                    result.chains[right].natural_leader_offset.x - root_x);
                return left_distance != right_distance
                    ? left_distance < right_distance : left < right;
            });
    }
    result.valid = true;
    return result;
}

std::vector<AoeFormationFollowAssignment>
make_formation_follow_assignments(const AoeFormationFollowPlan& plan) {
    std::vector<AoeFormationFollowAssignment> result;
    if (!plan.valid) return result;
    std::size_t member_count = 0;
    for (const auto& chain : plan.chains)
        member_count += chain.members.size();
    result.reserve(member_count - plan.chains.size());

    const auto append_chain = [&](const AoeFormationFollowChain& chain) {
        for (std::size_t index = 0; index < chain.members.size(); ++index) {
            if (index == 0) continue;
            const auto& member = chain.members[index];
            const AoeUnitTarget target = chain.members[index - 1].unit;
            const float target_distance = member.distance_from_leader -
                chain.members[index - 1].distance_from_leader;
            result.push_back({member.unit, AoeFormationFollow{
                plan.squad, plan.order_revision, target,
                member.distance_from_leader,
                target_distance, chain.natural_chain,
                member.chain_order, false, 0}});
        }
    };

    for (const auto& chain : plan.chains) append_chain(chain);
    return result;
}

std::optional<AoeFormationFollowTopology>
make_formation_follow_topology(const AoeFormationFollowPlan& plan) {
    if (!plan.valid || plan.squad == entt::null || plan.chains.empty())
        return std::nullopt;
    AoeFormationFollowTopology result;
    result.squad = plan.squad;
    result.order_revision = plan.order_revision;
    result.layout_revision = plan.layout_revision;
    result.bindings.reserve(plan.chains.size());
    for (std::size_t index = 0; index < plan.chains.size(); ++index) {
        const auto& chain = plan.chains[index];
        if (chain.members.empty()) return std::nullopt;
        result.bindings.push_back(AoeFormationFollowChainBinding{
            static_cast<std::uint32_t>(index),
            static_cast<std::uint32_t>(index),
            chain.members.front().unit});
    }
    result.valid = true;
    return result;
}

std::optional<std::vector<AoeFormationFollowGroup>>
make_formation_follow_groups(
    const AoeFormationFollowPlan& plan,
    const AoeFormationLayout& lane_leader_layout) {
    if (!plan.valid || plan.chains.empty()) return std::nullopt;
    std::vector<glm::vec2> lane_offsets;
    lane_offsets.reserve(lane_leader_layout.slots.size());
    for (const auto& slot : lane_leader_layout.slots) {
        if (slot.unit.entity == entt::null || !finite(slot.local_offset))
            return std::nullopt;
        const auto found = std::find_if(lane_offsets.begin(),
            lane_offsets.end(), [&](glm::vec2 value) {
                return std::abs(value.x - slot.local_offset.x) <= Epsilon;
            });
        if (found == lane_offsets.end())
            lane_offsets.push_back(slot.local_offset);
    }
    std::stable_sort(lane_offsets.begin(), lane_offsets.end(),
        [](glm::vec2 left, glm::vec2 right) { return left.x < right.x; });
    if (lane_offsets.empty()) return std::nullopt;
    const std::size_t lane_count = std::min(
        plan.chains.size(), lane_offsets.size());
    std::vector<AoeFormationFollowGroup> groups(lane_count);
    for (std::size_t lane = 0; lane < lane_count; ++lane)
        groups[lane].lane_offset = lane_offsets[lane];

    std::vector<glm::vec2> natural_offsets;
    natural_offsets.reserve(plan.chains.size());
    for (const auto& chain : plan.chains)
        natural_offsets.push_back(chain.natural_leader_offset);
    const auto assignments = assign_chains_to_lanes(
        natural_offsets, lane_offsets);
    if (assignments.size() != lane_count) return std::nullopt;
    for (std::size_t lane = 0; lane < lane_count; ++lane)
        groups[lane].chains = assignments[lane];
    for (auto& group : groups) {
        if (group.chains.empty()) return std::nullopt;
        const auto root = *std::min_element(group.chains.begin(),
            group.chains.end(), [&](std::size_t left, std::size_t right) {
                const float left_distance = std::abs(
                    plan.chains[left].natural_leader_offset.x -
                    group.lane_offset.x);
                const float right_distance = std::abs(
                    plan.chains[right].natural_leader_offset.x -
                    group.lane_offset.x);
                return left_distance != right_distance
                    ? left_distance < right_distance : left < right;
            });
        group.root_chain = root;
        group.lane_offset.y =
            plan.chains[root].natural_leader_offset.y;
        std::stable_sort(group.chains.begin(), group.chains.end(),
            [&](std::size_t left, std::size_t right) {
                if (left == right) return false;
                if (left == root) return true;
                if (right == root) return false;
                const float root_x = plan.chains[root].natural_leader_offset.x;
                const float left_distance = std::abs(
                    plan.chains[left].natural_leader_offset.x - root_x);
                const float right_distance = std::abs(
                    plan.chains[right].natural_leader_offset.x - root_x);
                return left_distance != right_distance
                    ? left_distance < right_distance : left < right;
            });
    }
    return groups;
}

} // namespace gld::ecs::aoe
