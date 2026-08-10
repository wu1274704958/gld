#include <aoe/AoeGameplay.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <ranges>
#include <vector>

namespace gld::ecs::aoe {
namespace {

glm::vec2 squad_slot_world(const AoePosition& center,
                           const AoeSquadFormation& formation,
                           const AoeFormationSlot& slot) {
    const glm::vec2 forward = glm::normalize(formation.forward);
    const glm::vec2 right{forward.y, -forward.x};
    return center.value + right * slot.local_offset.x +
           forward * slot.local_offset.y;
}

bool rematch_squad_arrival_slots(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    if (!reg.valid(squad) ||
        !reg.all_of<AoePosition, AoeSquadFormation, AoeSquadMembers>(squad))
        return false;
    auto& formation = reg.get<AoeSquadFormation>(squad);
    if (formation.slots.empty()) return true;
    const auto& members = reg.get<AoeSquadMembers>(squad);
    if (formation.slots.size() != members.active.size()) return false;

    std::vector<AoeUnitTarget> rematched;
    rematched.reserve(formation.slots.size());
    for (const auto& slot : formation.slots) {
        if (!detail::aoe_gameplay_squad_member_valid(reg, slot.unit) ||
            !reg.all_of<AoePosition, AoeSquadMember>(slot.unit.entity))
            return false;
        const auto& membership = reg.get<AoeSquadMember>(slot.unit.entity);
        if (membership.squad != squad ||
            std::ranges::none_of(members.active,
                [&](const AoeUnitTarget& active) {
                    return active.entity == slot.unit.entity &&
                           active.instance_id == slot.unit.instance_id;
                }) ||
            std::ranges::any_of(rematched,
                [&](const AoeUnitTarget& seen) {
                    return seen.entity == slot.unit.entity &&
                           seen.instance_id == slot.unit.instance_id;
                }))
            return false;
        rematched.push_back(slot.unit);
    }

    std::vector<std::int64_t> priorities;
    priorities.reserve(formation.slots.size());
    for (const auto& slot : formation.slots)
        if (std::find(priorities.begin(), priorities.end(), slot.priority) ==
            priorities.end())
            priorities.push_back(slot.priority);

    const auto& center = reg.get<AoePosition>(squad);
    constexpr double CostEpsilon = 1e-12;
    for (const auto priority : priorities) {
        std::vector<std::size_t> slot_indices;
        std::vector<AoeUnitTarget> units;
        for (std::size_t i = 0; i < formation.slots.size(); ++i) {
            if (formation.slots[i].priority != priority) continue;
            slot_indices.push_back(i);
            units.push_back(formation.slots[i].unit);
        }
        if (units.size() <= 1) continue;
        std::stable_sort(units.begin(), units.end(),
            [&](const AoeUnitTarget& a, const AoeUnitTarget& b) {
                const auto& member_a = reg.get<AoeSquadMember>(a.entity);
                const auto& member_b = reg.get<AoeSquadMember>(b.entity);
                if (member_a.ordinal != member_b.ordinal)
                    return member_a.ordinal < member_b.ordinal;
                if (a.instance_id != b.instance_id)
                    return a.instance_id < b.instance_id;
                return entt::to_integral(a.entity) <
                       entt::to_integral(b.entity);
            });

        // Deterministic Hungarian assignment. Rows are stable members and
        // columns are stable slot indices; equal costs prefer the lower slot.
        const std::size_t count = units.size();
        std::vector<double> row_potential(count + 1, 0.0);
        std::vector<double> column_potential(count + 1, 0.0);
        std::vector<std::size_t> matched_row(count + 1, 0);
        std::vector<std::size_t> previous_column(count + 1, 0);
        for (std::size_t row = 1; row <= count; ++row) {
            matched_row[0] = row;
            std::vector<double> minimum(
                count + 1, std::numeric_limits<double>::infinity());
            std::vector<bool> used(count + 1, false);
            std::size_t column = 0;
            do {
                used[column] = true;
                const std::size_t current_row = matched_row[column];
                double delta = std::numeric_limits<double>::infinity();
                std::size_t next_column = 0;
                for (std::size_t candidate = 1; candidate <= count;
                     ++candidate) {
                    if (used[candidate]) continue;
                    const glm::vec2 unit_position = reg.get<AoePosition>(
                        units[current_row - 1].entity).value;
                    const glm::vec2 slot_position = squad_slot_world(
                        center, formation,
                        formation.slots[slot_indices[candidate - 1]]);
                    const glm::vec2 difference = unit_position - slot_position;
                    const double cost = static_cast<double>(
                        glm::dot(difference, difference));
                    const double reduced = cost - row_potential[current_row] -
                                           column_potential[candidate];
                    if (reduced + CostEpsilon < minimum[candidate]) {
                        minimum[candidate] = reduced;
                        previous_column[candidate] = column;
                    }
                    if (minimum[candidate] + CostEpsilon < delta ||
                        (std::abs(minimum[candidate] - delta) <= CostEpsilon &&
                         (next_column == 0 || candidate < next_column))) {
                        delta = minimum[candidate];
                        next_column = candidate;
                    }
                }
                if (next_column == 0 || !std::isfinite(delta)) return false;
                for (std::size_t candidate = 0; candidate <= count;
                     ++candidate) {
                    if (used[candidate]) {
                        row_potential[matched_row[candidate]] += delta;
                        column_potential[candidate] -= delta;
                    } else {
                        minimum[candidate] -= delta;
                    }
                }
                column = next_column;
            } while (matched_row[column] != 0);
            do {
                const std::size_t previous = previous_column[column];
                matched_row[column] = matched_row[previous];
                column = previous;
            } while (column != 0);
        }
        for (std::size_t column = 1; column <= count; ++column) {
            if (matched_row[column] == 0) return false;
            rematched[slot_indices[column - 1]] =
                units[matched_row[column] - 1];
        }
    }

    for (std::size_t i = 0; i < formation.slots.size(); ++i)
        formation.slots[i].unit = rematched[i];
    return true;
}

} // namespace

void AoeFullSquadArrivalRematchPlugin::install(App&) {}

void AoeFullSquadArrivalRematchPlugin::fixed_tick(
    EcsWorld& world, std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& reg = world.reg();
    for (const auto squad : reg.view<AoeSquadArrivalRematchRequest>()) {
        auto& request = reg.get<AoeSquadArrivalRematchRequest>(squad);
        if (!request.valid) continue;

        AoeSquadArrivalRematchStatus status =
            AoeSquadArrivalRematchStatus::Failed;
        std::uint32_t member_count = 0;
        if (reg.all_of<AoeSquadOrder, AoeSquadFormation,
                       AoeSquadMembers>(squad)) {
            const auto& order = reg.get<AoeSquadOrder>(squad);
            const auto& formation = reg.get<AoeSquadFormation>(squad);
            member_count = static_cast<std::uint32_t>(
                reg.get<AoeSquadMembers>(squad).active.size());
            if (order.type == AoeSquadOrderType::AttackMove &&
                order.revision == request.order_revision &&
                !formation.arrival_reflow_done &&
                rematch_squad_arrival_slots(world, squad))
                status = AoeSquadArrivalRematchStatus::Applied;
        }

        request.valid = false;
        reg.emplace_or_replace<AoeSquadArrivalRematchResult>(squad,
            AoeSquadArrivalRematchResult{status, request.order_revision,
                                         tick, member_count, true});
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    world.resource_or_add<AoeGameplayPerformanceDiagnostics>()
        .squad_arrival_rematch_ms +=
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
}

} // namespace gld::ecs::aoe
