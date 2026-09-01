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
    result.erase(std::unique(result.begin(), result.end(),
        [](glm::vec2 left, glm::vec2 right) {
            return std::abs(left.x - right.x) <= Epsilon;
        }), result.end());
    return result;
}

std::optional<std::vector<AoeFormationFollowGroup>> make_balanced_groups(
    const std::vector<AoeFormationFollowChain>& chains,
    const std::vector<glm::vec2>& lane_offsets) {
    const std::size_t chain_count = chains.size();
    const std::size_t lane_count = lane_offsets.size();
    if (!chain_count || !lane_count || lane_count > chain_count)
        return std::nullopt;

    // An unchanged lane count is not a narrowing transition. Preserve the
    // natural topology exactly instead of needlessly slicing or rebalancing
    // columns whose lengths can differ by one.
    if (lane_count == chain_count) {
        std::vector<AoeFormationFollowGroup> result(lane_count);
        for (std::size_t lane = 0; lane < lane_count; ++lane) {
            if (chains[lane].members.empty()) return std::nullopt;
            result[lane].root_chain = lane;
            result[lane].lane_offset = lane_offsets[lane];
            result[lane].lane_offset.y =
                chains[lane].natural_leader_offset.y;
            result[lane].segments.push_back(
                {lane, 0, chains[lane].members.size()});
        }
        return result;
    }

    // Select one distinct root per lane with an order-preserving minimum-cost
    // match. Preserving order prevents lane crossings; the dynamic program
    // makes parity and irregular spacing deterministic instead of depending
    // on which lane happened to reserve its root first.
    const float infinity = std::numeric_limits<float>::infinity();
    std::vector<std::vector<float>> cost(
        lane_count, std::vector<float>(chain_count, infinity));
    std::vector<std::vector<std::size_t>> parent(
        lane_count, std::vector<std::size_t>(chain_count, chain_count));
    for (std::size_t chain = 0;
         chain + lane_count <= chain_count; ++chain) {
        cost[0][chain] = std::abs(
            chains[chain].natural_leader_offset.x - lane_offsets[0].x);
    }
    for (std::size_t lane = 1; lane < lane_count; ++lane) {
        const std::size_t maximum_chain = chain_count - (lane_count - lane);
        for (std::size_t chain = lane; chain <= maximum_chain; ++chain) {
            float best = infinity;
            std::size_t best_parent = chain_count;
            for (std::size_t previous = lane - 1;
                 previous < chain; ++previous) {
                if (!std::isfinite(cost[lane - 1][previous])) continue;
                const float candidate = cost[lane - 1][previous];
                if (candidate < best - Epsilon ||
                    (std::abs(candidate - best) <= Epsilon &&
                     previous < best_parent)) {
                    best = candidate;
                    best_parent = previous;
                }
            }
            if (best_parent == chain_count) continue;
            cost[lane][chain] = best + std::abs(
                chains[chain].natural_leader_offset.x -
                lane_offsets[lane].x);
            parent[lane][chain] = best_parent;
        }
    }
    std::size_t last_root = chain_count;
    float best_total = infinity;
    for (std::size_t chain = lane_count - 1; chain < chain_count; ++chain) {
        if (cost[lane_count - 1][chain] < best_total - Epsilon ||
            (std::abs(cost[lane_count - 1][chain] - best_total) <= Epsilon &&
             chain < last_root)) {
            best_total = cost[lane_count - 1][chain];
            last_root = chain;
        }
    }
    if (last_root == chain_count) return std::nullopt;
    std::vector<std::size_t> roots(lane_count);
    for (std::size_t lane = lane_count; lane-- > 0;) {
        roots[lane] = last_root;
        if (lane) last_root = parent[lane][last_root];
    }

    std::size_t member_count = 0;
    for (const auto& chain : chains) {
        if (chain.members.empty()) return std::nullopt;
        member_count += chain.members.size();
    }
    std::vector<std::size_t> target_lengths(
        lane_count, member_count / lane_count);
    const std::size_t remainder = member_count % lane_count;
    std::vector<std::size_t> centered_lanes(lane_count);
    for (std::size_t lane = 0; lane < lane_count; ++lane)
        centered_lanes[lane] = lane;
    std::stable_sort(centered_lanes.begin(), centered_lanes.end(),
        [&](std::size_t left, std::size_t right) {
            const float left_distance = std::abs(lane_offsets[left].x);
            const float right_distance = std::abs(lane_offsets[right].x);
            return left_distance != right_distance
                ? left_distance < right_distance
                : lane_offsets[left].x < lane_offsets[right].x;
        });
    for (std::size_t index = 0; index < remainder; ++index)
        ++target_lengths[centered_lanes[index]];

    std::vector<AoeFormationFollowGroup> result(lane_count);
    std::vector<bool> is_root(chain_count, false);
    std::vector<std::size_t> root_prefix(chain_count, 0);
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        const std::size_t root = roots[lane];
        is_root[root] = true;
        root_prefix[root] = std::min(
            chains[root].members.size(), target_lengths[lane]);
        if (!root_prefix[root]) return std::nullopt;
        result[lane].root_chain = root;
        result[lane].lane_offset = lane_offsets[lane];
        result[lane].lane_offset.y =
            chains[root].natural_leader_offset.y;
        result[lane].segments.push_back(
            {root, 0, root_prefix[root]});
    }

    struct Supply {
        std::size_t natural_chain = 0;
        std::size_t first_member = 0;
        std::size_t member_count = 0;
    };
    std::vector<Supply> supplies;
    for (std::size_t chain = 0; chain < chain_count; ++chain) {
        const std::size_t first = is_root[chain] ? root_prefix[chain] : 0;
        if (first < chains[chain].members.size())
            supplies.push_back(
                {chain, first, chains[chain].members.size() - first});
    }

    std::size_t supply_index = 0;
    std::size_t supply_cursor = 0;
    for (std::size_t lane = 0; lane < lane_count; ++lane) {
        std::size_t remaining = target_lengths[lane] -
            result[lane].segments.front().member_count;
        while (remaining) {
            if (supply_index >= supplies.size()) return std::nullopt;
            const auto& supply = supplies[supply_index];
            const std::size_t available =
                supply.member_count - supply_cursor;
            const std::size_t take = std::min(remaining, available);
            result[lane].segments.push_back({supply.natural_chain,
                supply.first_member + supply_cursor, take});
            remaining -= take;
            supply_cursor += take;
            if (supply_cursor == supply.member_count) {
                ++supply_index;
                supply_cursor = 0;
            }
        }
    }
    if (supply_index != supplies.size() || supply_cursor != 0)
        return std::nullopt;
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
    auto groups = make_balanced_groups(result.chains, *lane_offsets);
    if (!groups) return std::nullopt;
    result.groups = std::move(*groups);
    for (const auto& group : result.groups)
        for (const auto& segment : group.segments)
            if (segment.first_member == 0)
                result.chains[segment.natural_chain].narrow_leader_offset =
                    group.lane_offset;
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
    const auto lane_offsets = chain_leader_offsets(lane_leader_layout);
    if (!lane_offsets || lane_offsets->empty()) return std::nullopt;
    return make_balanced_groups(plan.chains, *lane_offsets);
}

} // namespace gld::ecs::aoe
