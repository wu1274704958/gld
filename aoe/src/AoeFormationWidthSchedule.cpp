#include <aoe/AoeFormationWidthSchedule.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace gld::ecs::aoe {
namespace {
constexpr float Epsilon = 1e-5f;

bool finite(glm::vec2 value) {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

float smoothstep(float value) {
    value = std::clamp(value, 0.f, 1.f);
    return value * value * (3.f - 2.f * value);
}

std::optional<AoeFormationLayout> align_layout(
    const AoeFormationLayout& natural,
    const AoeFormationLayout& candidate) {
    if (candidate.slots.size() != natural.slots.size()) return std::nullopt;
    std::unordered_map<entt::entity, const AoeFormationSlot*> by_entity;
    by_entity.reserve(candidate.slots.size());
    for (const auto& slot : candidate.slots) {
        if (!by_entity.emplace(slot.unit.entity, &slot).second)
            return std::nullopt;
    }
    AoeFormationLayout result;
    result.bounds = candidate.bounds;
    result.slots.reserve(natural.slots.size());
    std::unordered_map<entt::entity, bool> natural_entities;
    natural_entities.reserve(natural.slots.size());
    for (const auto& natural_slot : natural.slots) {
        if (!natural_entities.emplace(
                natural_slot.unit.entity, true).second)
            return std::nullopt;
        const auto found = by_entity.find(natural_slot.unit.entity);
        if (found == by_entity.end() ||
            found->second->unit.instance_id != natural_slot.unit.instance_id)
            return std::nullopt;
        result.slots.push_back(*found->second);
    }
    return result;
}

float transition_length(glm::vec2 from, glm::vec2 to,
                        const AoeFormationWidthScheduleSettings& settings) {
    const float displacement = glm::length(to - from);
    const float backward_delta = std::max(0.f, from.y - to.y);
    // smoothstep's maximum derivative is 1.5. Account for it both in the
    // general offset velocity limit and in the forward-only Y constraint.
    float result = 1.5f * displacement /
        settings.maximum_layout_change_per_progress;
    result = std::max(result,
        1.5f * backward_delta /
            (1.f - settings.minimum_member_forward_ratio));
    return result;
}

bool valid_layout(const AoeFormationLayout& layout) {
    if (!std::isfinite(layout.bounds.local_min.x) ||
        !std::isfinite(layout.bounds.local_min.y) ||
        !std::isfinite(layout.bounds.local_max.x) ||
        !std::isfinite(layout.bounds.local_max.y) ||
        layout.bounds.local_max.x < layout.bounds.local_min.x ||
        layout.bounds.local_max.y < layout.bounds.local_min.y)
        return false;
    return std::all_of(layout.slots.begin(), layout.slots.end(),
        [](const AoeFormationSlot& slot) {
            return slot.unit.entity != entt::null && finite(slot.local_offset);
        });
}
} // namespace

std::optional<float> project_formation_width_route_progress(
    const std::vector<AoeFormationWidthRouteSample>& route,
    glm::vec2 point) {
    if (route.empty() || !finite(point)) return std::nullopt;
    float best_distance2 = std::numeric_limits<float>::infinity();
    float best_progress = 0.f;
    if (route.size() == 1) {
        if (!finite(route.front().center) ||
            !std::isfinite(route.front().progress))
            return std::nullopt;
        return route.front().progress;
    }
    for (std::size_t index = 1; index < route.size(); ++index) {
        const auto& from = route[index - 1];
        const auto& to = route[index];
        if (!finite(from.center) || !finite(to.center) ||
            !std::isfinite(from.progress) || !std::isfinite(to.progress) ||
            to.progress + Epsilon < from.progress)
            return std::nullopt;
        const glm::vec2 segment = to.center - from.center;
        const float length2 = glm::dot(segment, segment);
        const float alpha = length2 > Epsilon * Epsilon
            ? std::clamp(glm::dot(point - from.center, segment) / length2,
                         0.f, 1.f)
            : 0.f;
        const glm::vec2 projected = from.center + segment * alpha;
        const float distance2 = glm::dot(point - projected,
                                         point - projected);
        if (distance2 < best_distance2) {
            best_distance2 = distance2;
            best_progress = from.progress +
                (to.progress - from.progress) * alpha;
        }
    }
    return best_progress;
}

bool expand_formation_width_constraint_over_turns(
    const std::vector<AoeFormationWidthRouteSample>& route,
    AoeFormationWidthConstraint& constraint) {
    if (route.empty() || !std::isfinite(constraint.begin_progress) ||
        !std::isfinite(constraint.end_progress) ||
        constraint.end_progress < constraint.begin_progress)
        return false;
    // Re-run until stable because expanding over one turn can touch the next
    // adjacent turn run.
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t index = 1; index < route.size(); ++index) {
            const auto& previous = route[index - 1];
            const auto& current = route[index];
            if (!finite(previous.forward) || !finite(current.forward) ||
                !std::isfinite(previous.progress) ||
                !std::isfinite(current.progress) ||
                current.progress + Epsilon < previous.progress)
                return false;
            const float cross = previous.forward.x * current.forward.y -
                previous.forward.y * current.forward.x;
            const float dot = glm::dot(previous.forward, current.forward);
            if (std::abs(std::atan2(cross, dot)) <= 1e-4f) continue;
            const float begin = previous.progress;
            const float end = current.progress;
            if (constraint.end_progress + Epsilon < begin ||
                constraint.begin_progress - Epsilon > end)
                continue;
            const float expanded_begin = std::min(
                constraint.begin_progress, begin);
            const float expanded_end = std::max(
                constraint.end_progress, end);
            changed = changed || expanded_begin <
                constraint.begin_progress - Epsilon || expanded_end >
                constraint.end_progress + Epsilon;
            constraint.begin_progress = expanded_begin;
            constraint.end_progress = expanded_end;
        }
    }
    return true;
}

std::optional<AoeFormationWidthSchedule> make_single_width_schedule(
    const AoeFormationLayout& natural_layout,
    const AoeFormationLayout& narrow_layout,
    std::vector<AoeFormationWidthConstraint> constraints,
    float travel_progress,
    const AoeFormationWidthScheduleSettings& settings) {
    if (!valid_layout(natural_layout) || !valid_layout(narrow_layout) ||
        natural_layout.slots.empty() || !std::isfinite(travel_progress) ||
        travel_progress < 0.f || !std::isfinite(settings.safety_distance) ||
        settings.safety_distance < 0.f ||
        !std::isfinite(settings.maximum_layout_change_per_progress) ||
        settings.maximum_layout_change_per_progress <= 0.f ||
        !std::isfinite(settings.minimum_member_forward_ratio) ||
        settings.minimum_member_forward_ratio < 0.f ||
        settings.minimum_member_forward_ratio >= 1.f)
        return std::nullopt;

    auto aligned_narrow = align_layout(natural_layout, narrow_layout);
    if (!aligned_narrow) return std::nullopt;
    AoeFormationWidthSchedule result;
    result.layouts.push_back(natural_layout);
    result.layouts.push_back(std::move(*aligned_narrow));

    for (const auto& constraint : constraints) {
        if (!std::isfinite(constraint.begin_progress) ||
            !std::isfinite(constraint.end_progress) ||
            !std::isfinite(constraint.maximum_width) ||
            constraint.maximum_width <= 0.f ||
            constraint.end_progress < constraint.begin_progress ||
            result.layouts[1].bounds.width() >
                constraint.maximum_width + Epsilon)
            return std::nullopt;
    }
    std::stable_sort(constraints.begin(), constraints.end(),
        [](const auto& left, const auto& right) {
            if (left.begin_progress != right.begin_progress)
                return left.begin_progress < right.begin_progress;
            return left.end_progress < right.end_progress;
        });
    result.constraints = std::move(constraints);
    if (result.constraints.empty() ||
        result.layouts[1].bounds.width() >=
            result.layouts[0].bounds.width() - Epsilon) {
        result.valid = true;
        return result;
    }

    result.constraint_begin_progress =
        result.constraints.front().begin_progress;
    result.constraint_end_progress = result.constraints.front().end_progress;
    for (const auto& constraint : result.constraints) {
        result.constraint_begin_progress = std::min(
            result.constraint_begin_progress, constraint.begin_progress);
        result.constraint_end_progress = std::max(
            result.constraint_end_progress, constraint.end_progress);
    }

    // Compression travels from the front of the formation to the rear. A
    // slot only has to be narrow before that slot reaches the first portal;
    // requiring every rear row to finish at the front-row deadline makes
    // large formations impossible to plan.
    AoeFormationWidthTransition shrink;
    shrink.begin_progress = std::numeric_limits<float>::infinity();
    shrink.end_progress = -std::numeric_limits<float>::infinity();
    shrink.from_layout = 0;
    shrink.to_layout = 1;
    shrink.slot_progress_windows.reserve(result.layouts[0].slots.size());
    float maximum_front_y = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0;
         index < result.layouts[0].slots.size(); ++index)
        maximum_front_y = std::max({maximum_front_y,
            result.layouts[0].slots[index].local_offset.y,
            result.layouts[1].slots[index].local_offset.y});
    for (std::size_t index = 0;
         index < result.layouts[0].slots.size(); ++index) {
        const glm::vec2 from = result.layouts[0].slots[index].local_offset;
        const glm::vec2 to = result.layouts[1].slots[index].local_offset;
        const float slot_front_y = std::max(from.y, to.y);
        const float end = result.constraint_begin_progress +
            maximum_front_y - slot_front_y;
        const float begin = end - transition_length(from, to, settings);
        shrink.begin_progress = std::min(shrink.begin_progress,
                                         std::max(0.f, begin));
        shrink.end_progress = std::max(shrink.end_progress, end);
        shrink.slot_progress_windows.push_back({std::max(0.f, begin), end});
    }
    result.transitions.push_back(std::move(shrink));

    // Restoration is the same wave in reverse occupancy order: each slot can
    // start widening as soon as its rear edge clears the final constraint.
    AoeFormationWidthTransition restore;
    restore.begin_progress = std::numeric_limits<float>::infinity();
    restore.end_progress = -std::numeric_limits<float>::infinity();
    restore.from_layout = 1;
    restore.to_layout = 0;
    restore.slot_progress_windows.reserve(result.layouts[0].slots.size());
    float minimum_rear_y = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0;
         index < result.layouts[0].slots.size(); ++index)
        minimum_rear_y = std::min({minimum_rear_y,
            result.layouts[0].slots[index].local_offset.y,
            result.layouts[1].slots[index].local_offset.y});
    for (std::size_t index = 0;
         index < result.layouts[0].slots.size(); ++index) {
        const glm::vec2 from = result.layouts[1].slots[index].local_offset;
        const glm::vec2 to = result.layouts[0].slots[index].local_offset;
        const float slot_rear_y = std::min(from.y, to.y);
        const float begin = result.constraint_end_progress -
            (slot_rear_y - minimum_rear_y);
        const float end = begin + transition_length(from, to, settings);
        restore.begin_progress = std::min(restore.begin_progress, begin);
        restore.end_progress = std::max(restore.end_progress, end);
        restore.slot_progress_windows.push_back({begin, end});
    }
    result.transitions.push_back(std::move(restore));
    result.narrowed = true;
    result.valid = true;
    return result;
}

std::optional<AoeFormationWidthSchedule> make_formation_width_schedule(
    const AoeFormationLayout& natural_layout,
    const AoeFormationLayout& narrow_layout,
    std::vector<AoeFormationWidthConstraint> constraints,
    float travel_progress,
    const AoeFormationWidthScheduleSettings& settings) {
    return make_single_width_schedule(natural_layout, narrow_layout,
        std::move(constraints), travel_progress, settings);
}

std::optional<AoeFormationWidthSchedule> make_formation_width_schedule(
    const AoeFormationLayout& natural_layout,
    std::vector<AoeFormationLayout> candidate_layouts,
    std::vector<AoeFormationWidthConstraint> constraints,
    float travel_progress,
    const AoeFormationWidthScheduleSettings& settings) {
    if (!valid_layout(natural_layout) || natural_layout.slots.empty() ||
        !std::isfinite(travel_progress) || travel_progress < 0.f)
        return std::nullopt;

    struct Candidate {
        AoeFormationLayout layout;
        float width = 0.f;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(candidate_layouts.size());
    for (const auto& candidate : candidate_layouts) {
        auto aligned = align_layout(natural_layout, candidate);
        if (!aligned || aligned->bounds.width() >=
                natural_layout.bounds.width() - Epsilon)
            continue;
        candidates.push_back({std::move(*aligned),
                              candidate.bounds.width()});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return left.width > right.width;
        });
    candidates.erase(std::unique(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            return std::abs(left.width - right.width) <= Epsilon;
        }), candidates.end());

    if (constraints.empty() || candidates.empty()) {
        AoeFormationWidthSchedule result;
        result.layouts.push_back(natural_layout);
        result.constraints = std::move(constraints);
        result.valid = true;
        return result;
    }

    struct PendingStage {
        float begin = 0.f;
        float end = 0.f;
        std::size_t candidate = 0;
        float maximum_width = 0.f;
    };
    std::vector<PendingStage> pending;
    pending.reserve(constraints.size());
    for (const auto& constraint : constraints) {
        if (!std::isfinite(constraint.begin_progress) ||
            !std::isfinite(constraint.end_progress) ||
            !std::isfinite(constraint.maximum_width) ||
            constraint.maximum_width <= 0.f ||
            constraint.end_progress < constraint.begin_progress)
            return std::nullopt;
        const auto found = std::find_if(candidates.begin(), candidates.end(),
            [&](const Candidate& candidate) {
                return candidate.width <= constraint.maximum_width + Epsilon;
            });
        if (found == candidates.end()) return std::nullopt;
        pending.push_back({constraint.begin_progress,
            constraint.end_progress,
            static_cast<std::size_t>(found - candidates.begin()),
            constraint.maximum_width});
    }
    std::stable_sort(pending.begin(), pending.end(),
        [](const PendingStage& left, const PendingStage& right) {
            if (left.begin != right.begin) return left.begin < right.begin;
            return left.end < right.end;
        });

    const auto maximum_transition_length = [&](std::size_t candidate) {
        float result = 0.f;
        const auto& layout = candidates[candidate].layout;
        for (std::size_t index = 0; index < natural_layout.slots.size(); ++index)
            result = std::max(result, transition_length(
                natural_layout.slots[index].local_offset,
                layout.slots[index].local_offset, settings));
        return result;
    };
    std::vector<float> transition_lengths(candidates.size(), 0.f);
    for (std::size_t index = 0; index < candidates.size(); ++index)
        transition_lengths[index] = maximum_transition_length(index);

    // Coalesce overlaps first. Then retain a wide gap only when both the
    // preceding restoration and following compression fit in it.
    std::vector<PendingStage> stages;
    for (const auto& next : pending) {
        if (stages.empty()) {
            stages.push_back(next);
            continue;
        }
        auto& previous = stages.back();
        const float required_gap = transition_lengths[previous.candidate] +
            transition_lengths[next.candidate] + settings.safety_distance * 2.f;
        if (next.begin <= previous.end + required_gap + Epsilon) {
            previous.end = std::max(previous.end, next.end);
            previous.begin = std::min(previous.begin, next.begin);
            previous.candidate = candidates[previous.candidate].width <=
                    candidates[next.candidate].width
                ? previous.candidate : next.candidate;
            previous.maximum_width = std::min(
                previous.maximum_width, next.maximum_width);
        } else {
            stages.push_back(next);
        }
    }

    AoeFormationWidthSchedule result;
    result.layouts.push_back(natural_layout);
    result.constraints = constraints;
    result.constraint_begin_progress = stages.front().begin;
    result.constraint_end_progress = stages.back().end;
    result.narrowed = !stages.empty();

    std::vector<std::size_t> layout_by_candidate(
        candidates.size(), std::numeric_limits<std::size_t>::max());
    for (const auto& stage : stages) {
        auto& mapped = layout_by_candidate[stage.candidate];
        if (mapped == std::numeric_limits<std::size_t>::max()) {
            mapped = result.layouts.size();
            result.layouts.push_back(candidates[stage.candidate].layout);
        }
        auto local = make_single_width_schedule(natural_layout,
            candidates[stage.candidate].layout,
            {{stage.begin, stage.end, stage.maximum_width}},
            travel_progress, settings);
        if (!local || !local->valid || local->transitions.size() != 2)
            return std::nullopt;
        for (auto transition : local->transitions) {
            transition.from_layout = transition.from_layout == 0 ? 0 : mapped;
            transition.to_layout = transition.to_layout == 0 ? 0 : mapped;
            result.transitions.push_back(std::move(transition));
        }
        result.stages.push_back({stage.begin, stage.end, mapped,
                                 stage.maximum_width});
    }
    result.valid = true;
    return result;
}

AoeFormationWidthSlotSample sample_formation_width_schedule(
    const AoeFormationWidthSchedule& schedule,
    std::size_t slot_index,
    float progress) {
    AoeFormationWidthSlotSample result;
    if (!schedule.valid || schedule.layouts.empty() ||
        slot_index >= schedule.layouts.front().slots.size())
        return result;
    std::size_t current_layout = 0;
    for (const auto& transition : schedule.transitions) {
        if (transition.from_layout >= schedule.layouts.size() ||
            transition.to_layout >= schedule.layouts.size())
            break;
        const glm::vec2 window =
            transition.slot_progress_windows.size() ==
                    schedule.layouts.front().slots.size()
                ? transition.slot_progress_windows[slot_index]
                : glm::vec2{transition.begin_progress,
                            transition.end_progress};
        if (progress < window.x) break;
        if (progress <= window.y) {
            const float span = window.y - window.x;
            const float linear_alpha = span > Epsilon
                ? (progress - window.x) / span : 1.f;
            const float alpha = smoothstep(linear_alpha);
            result.local_offset = glm::mix(
                schedule.layouts[transition.from_layout]
                    .slots[slot_index].local_offset,
                schedule.layouts[transition.to_layout]
                    .slots[slot_index].local_offset,
                alpha);
            result.from_layout = transition.from_layout;
            result.to_layout = transition.to_layout;
            result.transition_alpha = alpha;
            return result;
        }
        current_layout = transition.to_layout;
    }
    result.local_offset =
        schedule.layouts[current_layout].slots[slot_index].local_offset;
    result.from_layout = current_layout;
    result.to_layout = current_layout;
    result.transition_alpha = 0.f;
    return result;
}

float sample_formation_width(const AoeFormationWidthSchedule& schedule,
                             float progress) {
    if (!schedule.valid || schedule.layouts.empty()) return 0.f;
    if (schedule.layouts.front().slots.empty()) return 0.f;
    float minimum_x = std::numeric_limits<float>::infinity();
    float maximum_x = -std::numeric_limits<float>::infinity();
    for (std::size_t index = 0;
         index < schedule.layouts.front().slots.size(); ++index) {
        const float x = sample_formation_width_schedule(
            schedule, index, progress).local_offset.x;
        minimum_x = std::min(minimum_x, x);
        maximum_x = std::max(maximum_x, x);
    }
    float padding = 0.f;
    for (const auto& layout : schedule.layouts) {
        if (layout.slots.empty()) continue;
        float slot_minimum = std::numeric_limits<float>::infinity();
        float slot_maximum = -std::numeric_limits<float>::infinity();
        for (const auto& slot : layout.slots) {
            slot_minimum = std::min(slot_minimum, slot.local_offset.x);
            slot_maximum = std::max(slot_maximum, slot.local_offset.x);
        }
        padding = std::max(padding,
            layout.bounds.width() - (slot_maximum - slot_minimum));
    }
    return maximum_x - minimum_x + padding;
}

} // namespace gld::ecs::aoe
