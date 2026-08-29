#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include <aoe/AoeFormationLayout.hpp>

namespace gld::ecs::aoe {

// Width changes are expressed in Squad route progress rather than ticks. This
// keeps the schedule independent from movement speed and lets RouteSplit use
// the same data for route generation, diagnostics and future replanning.
struct AoeFormationWidthScheduleSettings {
    float safety_distance = .25f;
    float maximum_layout_change_per_progress = .5f;
    float minimum_member_forward_ratio = .05f;
};

struct AoeFormationWidthRouteSample {
    float progress = 0.f;
    glm::vec2 center{0.f};
    glm::vec2 forward{1.f, 0.f};
};

struct AoeFormationWidthConstraint {
    float begin_progress = 0.f;
    float end_progress = 0.f;
    float maximum_width = 0.f;
};

struct AoeFormationWidthTransition {
    float begin_progress = 0.f;
    float end_progress = 0.f;
    std::size_t from_layout = 0;
    std::size_t to_layout = 0;
    // Optional per-slot windows implement a travelling compression wave.
    // They are aligned with layouts[*].slots and keep runtime sampling O(1).
    std::vector<glm::vec2> slot_progress_windows;
};

// A maximal route interval that requires one concrete layout. Separate
// bottlenecks remain separate stages when the open run between them is long
// enough to restore and compress again.
struct AoeFormationWidthStage {
    float begin_progress = 0.f;
    float end_progress = 0.f;
    std::size_t layout = 0;
    float maximum_width = 0.f;
};

struct AoeFormationWidthSchedule {
    // Layout zero is always the natural layout. Other layouts are reordered
    // once to exactly the same stable unit order, so sampling needs no map.
    std::vector<AoeFormationLayout> layouts;
    std::vector<AoeFormationWidthConstraint> constraints;
    std::vector<AoeFormationWidthTransition> transitions;
    std::vector<AoeFormationWidthStage> stages;
    float constraint_begin_progress = 0.f;
    float constraint_end_progress = 0.f;
    bool narrowed = false;
    bool valid = false;
};

struct AoeFormationWidthSlotSample {
    glm::vec2 local_offset{0.f};
    std::size_t from_layout = 0;
    std::size_t to_layout = 0;
    float transition_alpha = 0.f;
};

// Projects a point to a sampled route and returns route progress at the
// nearest point. Samples must be ordered by non-decreasing progress.
std::optional<float> project_formation_width_route_progress(
    const std::vector<AoeFormationWidthRouteSample>& route,
    glm::vec2 point);

// If a constraint touches a turning run, extend it over the complete run.
// This prevents a layout transition from starting or ending during a turn.
bool expand_formation_width_constraint_over_turns(
    const std::vector<AoeFormationWidthRouteSample>& route,
    AoeFormationWidthConstraint& constraint);

std::optional<AoeFormationWidthSchedule> make_formation_width_schedule(
    const AoeFormationLayout& natural_layout,
    const AoeFormationLayout& narrow_layout,
    std::vector<AoeFormationWidthConstraint> constraints,
    float travel_progress,
    const AoeFormationWidthScheduleSettings& settings = {});

// General corridor builder. Candidate layouts may contain any number of
// generator-owned width variants. For each constraint the widest fitting
// candidate is selected; incompatible or too-close runs are conservatively
// coalesced while independent bottlenecks retain independent transitions.
std::optional<AoeFormationWidthSchedule> make_formation_width_schedule(
    const AoeFormationLayout& natural_layout,
    std::vector<AoeFormationLayout> candidate_layouts,
    std::vector<AoeFormationWidthConstraint> constraints,
    float travel_progress,
    const AoeFormationWidthScheduleSettings& settings = {});

AoeFormationWidthSlotSample sample_formation_width_schedule(
    const AoeFormationWidthSchedule& schedule,
    std::size_t slot_index,
    float progress);

float sample_formation_width(const AoeFormationWidthSchedule& schedule,
                             float progress);

} // namespace gld::ecs::aoe
