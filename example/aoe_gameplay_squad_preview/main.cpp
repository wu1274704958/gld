#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe/AoeGameplay.hpp>
#include <aoe_gpu_motion/AoeGpuMotion.hpp>
#include <aoe2/Aoe2Plugin.hpp>
#include <aoe2_gameplay/Aoe2GameplayBridge.hpp>
#include <ecs/App.hpp>
#include <ecs/Components.hpp>
#include <ecs/Input.hpp>
#include <ecs/Window.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Gizmo.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/systems/OrthographicCameraControlSystem.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2;
using namespace gld::ecs::aoe2_gameplay;
namespace fs = std::filesystem;

using SquadGameplayDef = AoeGameplayDef<
    AoePassThroughSquadEngagementPlugin,
    AoePassThroughFormationPlugin,
    AoePassThroughSquadArrivalRematchPlugin,
    AoePassThroughLocalAvoidancePlugin,
    AoePassThroughGlobalMotionPlugin>;

namespace {
constexpr std::uint32_t UnitLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;
constexpr float TileWidth = 54.f;
constexpr float TileHeight = 27.f;
constexpr float SpriteScale = .60f;
constexpr float DepthUnitsPerTile = 1.f;
constexpr float ElevationPixelsPerUnit = 48.f;
constexpr glm::vec2 PreviewMapOrigin{-96.f, -56.f};
constexpr std::uint32_t PreviewMapWidth = 192;
constexpr std::uint32_t PreviewMapHeight = 112;
constexpr glm::vec2 BlueSpawn{-52.f, 0.f};
constexpr glm::vec2 RedSpawn{52.f, 0.f};
constexpr glm::vec2 BlueDestination{52.f, 0.f};
constexpr glm::vec2 RedDestination{-52.f, 0.f};

struct StressPreset {
    int key = 1;
    std::uint32_t camels_per_side = 24;
    std::uint32_t archers_per_side = 40;

    constexpr std::uint32_t units_per_side() const {
        return camels_per_side + archers_per_side;
    }
    constexpr std::uint32_t total_units() const {
        return units_per_side() * 2u;
    }
};

constexpr std::array StressPresets{
    StressPreset{1, 24, 40},
    StressPreset{2, 96, 160},
    StressPreset{3, 375, 625},
    StressPreset{4, 938, 1562},
    StressPreset{5, 1875, 3125},
    StressPreset{6, 3750, 6250},
};

static_assert(StressPresets[0].total_units() == 128);
static_assert(StressPresets[1].total_units() == 512);
static_assert(StressPresets[2].total_units() == 2000);
static_assert(StressPresets[3].total_units() == 5000);
static_assert(StressPresets[4].total_units() == 10000);
static_assert(StressPresets[5].total_units() == 20000);

struct PreviewState {
    entt::entity blue{entt::null};
    entt::entity red{entt::null};
    entt::entity world_camera{entt::null};
    entt::entity hud{entt::null};
    bool orders_issued = false;
    bool draw_unit_feet = false;
    bool draw_unit_colliders = false;
    bool trace_unit_foot = false;
    bool draw_map = true;
    bool draw_navigation = false;
    int stress_preset = 1;
    std::vector<AoeStaticObstacleDesc> map_obstacles;
    std::uint64_t last_dynamic_queries = 0;
    std::uint64_t last_dynamic_candidates = 0;
    std::uint64_t interval_dynamic_queries = 0;
    std::uint64_t interval_dynamic_candidates = 0;
    std::size_t entity_high_water = 0;
    entt::entity trace_entity{entt::null};
    std::uint64_t trace_instance_id = 0;
    int trace_direction = -1;
    glm::vec2 trace_raw_foot{0.f};
    bool trace_has_previous = false;
    std::string latest = "waiting for both squads";
    double hud_elapsed = 0.0;
    double frame_stats_elapsed = 0.0;
    std::vector<float> frame_stats_samples;
    std::uint32_t frame_stats_fixed_ticks = 0;
    std::uint32_t frame_stats_pose_change_frames = 0;
    std::uint64_t frame_stats_pose_changes = 0;
    float displayed_fps = 0.f;
    float displayed_frame_dt_ms = 0.f;
    float displayed_frame_p95_ms = 0.f;
    float displayed_frame_max_ms = 0.f;
    float displayed_fixed_hz = 0.f;
    float displayed_pose_change_hz = 0.f;
    float displayed_pose_changes_per_second = 0.f;
};

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
struct SystemProfileState {
    bool enabled = false;
    bool frame_started = false;
    bool capturing = false;
    fs::path output;
    std::chrono::steady_clock::time_point frame_start{};
    double stable_seconds = 0.0;
    double capture_seconds = 0.0;
    double capture_target_seconds = 15.0;
    std::ostringstream csv;
};

struct MotionDecisionTraceState {
    bool capturing = false;
    fs::path directory = "build/profile";
    std::ofstream decisions;
    std::ofstream summaries;
    std::uint64_t last_tick = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t last_summary_tick = 0;
};
#endif

const StressPreset& active_stress_preset(const PreviewState& state) {
    const auto it = std::find_if(StressPresets.begin(), StressPresets.end(),
        [&](const StressPreset& value) { return value.key == state.stress_preset; });
    return it == StressPresets.end() ? StressPresets.front() : *it;
}

void update_frame_statistics(PreviewState& preview, const Time& time,
                             std::uint32_t fixed_ticks,
                             std::uint32_t animation_frame_changes) {
    if (!(time.raw_dt > 0.f) || !std::isfinite(time.raw_dt)) return;
    preview.frame_stats_elapsed += time.raw_dt;
    preview.frame_stats_samples.push_back(time.raw_dt);
    preview.frame_stats_fixed_ticks += fixed_ticks;
    preview.frame_stats_pose_change_frames += animation_frame_changes > 0 ? 1u : 0u;
    preview.frame_stats_pose_changes += animation_frame_changes;
    if (preview.frame_stats_elapsed < 1.0) return;

    std::sort(preview.frame_stats_samples.begin(), preview.frame_stats_samples.end());
    double total = 0.0;
    for (const float sample : preview.frame_stats_samples) total += sample;
    const auto count = preview.frame_stats_samples.size();
    const std::size_t p95_index = std::min(count - 1,
        static_cast<std::size_t>(std::ceil(static_cast<double>(count) * .95)) - 1);
    preview.displayed_fps = static_cast<float>(count / total);
    preview.displayed_frame_dt_ms = static_cast<float>(total / count * 1000.0);
    preview.displayed_frame_p95_ms = preview.frame_stats_samples[p95_index] * 1000.f;
    preview.displayed_frame_max_ms = preview.frame_stats_samples.back() * 1000.f;
    preview.displayed_fixed_hz = static_cast<float>(
        preview.frame_stats_fixed_ticks / total);
    preview.displayed_pose_change_hz = static_cast<float>(
        preview.frame_stats_pose_change_frames / total);
    preview.displayed_pose_changes_per_second = static_cast<float>(
        preview.frame_stats_pose_changes / total);
    preview.frame_stats_elapsed = 0.0;
    preview.frame_stats_samples.clear();
    preview.frame_stats_fixed_ticks = 0;
    preview.frame_stats_pose_change_frames = 0;
    preview.frame_stats_pose_changes = 0;
}

AoeMapDefinition make_preview_map() {
    AoeMapDefinition result;
    result.id = "squad_preview";
    result.origin = PreviewMapOrigin;
    result.tile_size = 1.f;
    result.width = PreviewMapWidth;
    result.height = PreviewMapHeight;
    result.heights.assign(
        static_cast<std::size_t>(result.width + 1) * (result.height + 1), 0.f);

    AoeStaticObstacleDesc left;
    left.source_id = "left_gate";
    left.shape = AoeStaticObstacleShape::Aabb;
    left.center = {-16.f, 0.f};
    left.half_extents = {1.5f, 12.f};
    result.static_obstacles.push_back(left);

    AoeStaticObstacleDesc right = left;
    right.source_id = "right_gate";
    right.center = {16.f, 0.f};
    result.static_obstacles.push_back(right);

    AoeStaticObstacleDesc south;
    south.source_id = "south_rock";
    south.shape = AoeStaticObstacleShape::Circle;
    south.center = {0.f, -28.f};
    south.radius = 5.f;
    result.static_obstacles.push_back(south);
    return result;
}

std::u32string ascii_to_u32(const std::string& value) {
    std::u32string result;
    result.reserve(value.size());
    for (const unsigned char c : value) result.push_back(static_cast<char32_t>(c));
    return result;
}

const char* spawn_name(AoeSquadSpawnStatus value) {
    switch (value) {
    case AoeSquadSpawnStatus::Pending: return "pending";
    case AoeSquadSpawnStatus::Ready: return "ready";
    case AoeSquadSpawnStatus::Partial: return "partial";
    case AoeSquadSpawnStatus::Failed: return "failed";
    case AoeSquadSpawnStatus::Empty: return "empty";
    }
    return "?";
}

const char* phase_name(AoeSquadPhase value) {
    switch (value) {
    case AoeSquadPhase::Forming: return "forming";
    case AoeSquadPhase::Moving: return "moving";
    case AoeSquadPhase::Blocked: return "blocked";
    case AoeSquadPhase::Engaging: return "engaging";
    case AoeSquadPhase::Regrouping: return "regrouping";
    case AoeSquadPhase::Idle: return "idle";
    case AoeSquadPhase::Empty: return "empty";
    case AoeSquadPhase::Failed: return "failed";
    }
    return "?";
}

const char* traffic_name(AoeSquadTrafficMode value) {
    switch (value) {
    case AoeSquadTrafficMode::Clear: return "clear";
    case AoeSquadTrafficMode::Following: return "following";
    case AoeSquadTrafficMode::PassingLeft: return "passing-left";
    case AoeSquadTrafficMode::PassingRight: return "passing-right";
    case AoeSquadTrafficMode::Yielding: return "yielding";
    case AoeSquadTrafficMode::Recovering: return "recovering";
    }
    return "?";
}

const char* action_name(UnitState value) {
    switch (value) {
    case UnitState::Idle: return "idle";
    case UnitState::Moving: return "moving";
    case UnitState::Attacking: return "attacking";
    case UnitState::Dying: return "dying";
    case UnitState::Disappearing: return "disappearing";
    }
    return "?";
}

glm::vec3 project_logical_position(glm::vec2 logical, float elevation = 0.f) {
    const float depth = logical.x + logical.y;
    return {(logical.x - logical.y) * TileWidth * .5f,
            -depth * TileHeight * .5f + 35.f +
                elevation * ElevationPixelsPerUnit,
            depth * DepthUnitsPerTile};
}

void fit_preview_camera_system(EcsWorld& world) {
    const auto* preview = world.try_resource<PreviewState>();
    const auto* map = world.try_resource<AoeLogicMap>();
    const auto* window = world.try_resource<Window>();
    if (!preview || !map || !map->valid() || !window ||
        !world.reg().valid(preview->world_camera))
        return;
    auto* camera = world.reg().try_get<Camera>(preview->world_camera);
    if (!camera) return;

    const glm::vec2 logical_min = map->origin();
    const glm::vec2 logical_max = logical_min + glm::vec2(
        map->width() * map->tile_size(),
        map->height() * map->tile_size());
    const std::array corners{
        project_logical_position({logical_min.x, logical_min.y}),
        project_logical_position({logical_max.x, logical_min.y}),
        project_logical_position({logical_max.x, logical_max.y}),
        project_logical_position({logical_min.x, logical_max.y})};
    glm::vec2 projected_min{std::numeric_limits<float>::infinity()};
    glm::vec2 projected_max{-std::numeric_limits<float>::infinity()};
    for (const auto& corner : corners) {
        projected_min = glm::min(projected_min, glm::vec2(corner));
        projected_max = glm::max(projected_max, glm::vec2(corner));
    }
    const glm::vec2 extent = projected_max - projected_min;
    if (!(extent.x > 0.f) || !(extent.y > 0.f) ||
        window->width <= 0 || window->height <= 0)
        return;

    constexpr float ViewportUsage = .92f;
    const float scale = std::min(
        static_cast<float>(window->width) * ViewportUsage / extent.x,
        static_cast<float>(window->height) * ViewportUsage / extent.y);
    const glm::vec2 center = (projected_min + projected_max) * .5f;
    auto* control = world.reg().try_get<OrthographicCameraControl>(
        preview->world_camera);
    if (control) set_orthographic_camera_fit(*control, center, scale);
}

void projection_system(EcsWorld& world) {
    auto& reg = world.reg();
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto* map = world.try_resource<AoeLogicMap>();
    for (const auto entity : reg.view<AoePosition, Transform>(
             entt::exclude<AoePooledUnit>)) {
        const auto logical = aoe_interpolated_position(
            reg.get<AoePosition>(entity),
            reg.try_get<AoePositionHistory>(entity), clock, settings);
        const float height = map && map->valid()
            ? map->sample_height(logical).value_or(0.f) : 0.f;
        const auto screen = project_logical_position(logical, height);
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(screen);
            transform.set_scale({SpriteScale, SpriteScale, 1.f});
        });
    }
    for (const auto entity : reg.view<AoeProjectile, Transform>()) {
        const auto& projectile = reg.get<AoeProjectile>(entity);
        const glm::vec2 logical{projectile.position.x, projectile.position.y};
        const float ground = map && map->valid()
            ? map->sample_height(logical).value_or(0.f) : 0.f;
        auto screen = project_logical_position(logical,
                                                ground + projectile.position.z);
        screen.z += .001f;
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(screen);
            transform.set_scale({SpriteScale, SpriteScale, 1.f});
        });
    }
}

void destroy_unit(EcsWorld& world, entt::entity entity) {
    auto& reg = world.reg();
    if (!reg.valid(entity)) return;
    if (const auto* link = reg.try_get<Aoe2PresentationLink>(entity);
        link && reg.valid(link->render)) reg.destroy(link->render);
    reg.destroy(entity);
}

void destroy_squad(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    if (!reg.valid(squad)) return;
    std::vector<entt::entity> units;
    if (const auto* members = reg.try_get<AoeSquadMembers>(squad)) {
        for (const auto& member : members->active) units.push_back(member.entity);
        for (const auto& pending : members->pending) units.push_back(pending.entity);
    }
    disband_aoe_gameplay_squad(world, squad);
    for (const auto unit : units) destroy_unit(world, unit);
}

entt::entity spawn_squad(EcsWorld& world, glm::vec2 center,
                         glm::vec2 forward, std::uint32_t team,
                         int color) {
    const auto& preset = active_stress_preset(world.resource<PreviewState>());
    AoeSquadSpawnOptions options;
    options.composition = {
        {"camel_scout", preset.camels_per_side, color},
        {"archer", preset.archers_per_side, color}};
    options.center = center;
    options.forward = forward;
    options.team_id = team;
    options.player_color = color;
    options.layers = UnitLayer;
    options.formation_spacing = .2f;
    options.acquisition_radius = 6.f;
    return spawn_aoe_gameplay_squad(world, options);
}

void destroy_projectiles(EcsWorld& world) {
    auto& reg = world.reg();
    std::vector<entt::entity> projectiles;
    for (const auto entity : reg.view<AoeProjectile>())
        projectiles.push_back(entity);
    for (const auto entity : projectiles) {
        if (!reg.valid(entity)) continue;
        if (const auto* link = reg.try_get<Aoe2ProjectilePresentationLink>(entity);
            link && reg.valid(link->render))
            reg.destroy(link->render);
        reg.destroy(entity);
    }
}

void reset_scene(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    if (world.reg().valid(state.world_camera))
        if (auto* control = world.reg().try_get<OrthographicCameraControl>(
                state.world_camera))
            reset_orthographic_camera_control(*control);
    destroy_squad(world, state.blue);
    destroy_squad(world, state.red);
    destroy_projectiles(world);
    state.blue = spawn_squad(world, BlueSpawn, {1.f, 0.f}, 1, 1);
    state.red = spawn_squad(world, RedSpawn, {-1.f, 0.f}, 2, 2);
    state.orders_issued = false;
    state.trace_entity = entt::null;
    state.trace_instance_id = 0;
    state.trace_direction = -1;
    state.trace_has_previous = false;
    const auto& preset = active_stress_preset(state);
    state.latest = "spawned two squads, " +
        std::to_string(preset.units_per_side()) + " units per side, " +
        std::to_string(preset.total_units()) + " total";
}

bool squad_operational(const entt::registry& reg, entt::entity squad) {
    if (!reg.valid(squad)) return false;
    const auto* spawn = reg.try_get<AoeSquadSpawnState>(squad);
    return spawn && (spawn->status == AoeSquadSpawnStatus::Ready ||
                     spawn->status == AoeSquadSpawnStatus::Partial);
}

void issue_mutual_attack_move(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    auto& reg = world.reg();
    if (!squad_operational(reg, state.blue) ||
        !squad_operational(reg, state.red)) return;
    const bool blue = request_aoe_squad_attack_move(
        world, state.blue, BlueDestination);
    const bool red = request_aoe_squad_attack_move(
        world, state.red, RedDestination);
    state.orders_issued = blue && red;
    state.latest = state.orders_issued
        ? "both squads received AttackMove" : "AttackMove rejected";
}

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
const char* motion_mode_name(AoeGlobalMotionMode value) {
    switch (value) {
    case AoeGlobalMotionMode::Clear: return "clear";
    case AoeGlobalMotionMode::SideStep: return "side_step";
    case AoeGlobalMotionMode::PassingLeft: return "passing_left";
    case AoeGlobalMotionMode::PassingRight: return "passing_right";
    case AoeGlobalMotionMode::Yielding: return "yielding";
    case AoeGlobalMotionMode::Backing: return "backing";
    case AoeGlobalMotionMode::Recovering: return "recovering";
    }
    return "unknown";
}

const char* motion_reason_name(AoeMotionDecisionReason value) {
    switch (value) {
    case AoeMotionDecisionReason::None: return "none";
    case AoeMotionDecisionReason::SameDirectionConflict:
        return "same_direction_conflict";
    case AoeMotionDecisionReason::FasterTraffic: return "faster_traffic";
    case AoeMotionDecisionReason::HeadOnTraffic: return "head_on_traffic";
    case AoeMotionDecisionReason::CrossingTraffic: return "crossing_traffic";
    case AoeMotionDecisionReason::StarvationPriority:
        return "starvation_priority";
    case AoeMotionDecisionReason::SideBlocked: return "side_blocked";
    case AoeMotionDecisionReason::DeadlockEscape: return "deadlock_escape";
    }
    return "unknown";
}

const char* motion_stop_name(AoeMotionStopReason value) {
    switch (value) {
    case AoeMotionStopReason::None: return "none";
    case AoeMotionStopReason::NoPath: return "no_path";
    case AoeMotionStopReason::Arrived: return "arrived";
    case AoeMotionStopReason::Attacking: return "attacking";
    case AoeMotionStopReason::LocalAvoidanceInfeasible:
        return "local_avoidance_infeasible";
    case AoeMotionStopReason::GlobalYield: return "global_yield";
    case AoeMotionStopReason::GlobalSideStepBlocked:
        return "global_side_step_blocked";
    case AoeMotionStopReason::GlobalDeadlock: return "global_deadlock";
    case AoeMotionStopReason::StaticSafetyClipped:
        return "static_safety_clipped";
    case AoeMotionStopReason::DynamicSafetyClipped:
        return "dynamic_safety_clipped";
    case AoeMotionStopReason::RepathPending: return "repath_pending";
    case AoeMotionStopReason::DynamicRepathFailed:
        return "dynamic_repath_failed";
    case AoeMotionStopReason::Unknown: return "unknown";
    }
    return "unknown";
}

AoeMotionStopReason observed_stop_reason(const entt::registry& reg,
                                         entt::entity entity) {
    if (const auto* decision = reg.try_get<AoeGlobalMotionDecision>(entity);
        decision && decision->stop_reason != AoeMotionStopReason::None)
        return decision->stop_reason;
    if (const auto* path = reg.try_get<AoeNavigationPath>(entity)) {
        if (path->no_path) return AoeMotionStopReason::NoPath;
        if (path->dynamic_repath_failed)
            return AoeMotionStopReason::DynamicRepathFailed;
        if (path->dynamic_repath_requested)
            return AoeMotionStopReason::RepathPending;
    }
    if (const auto* action = reg.try_get<AoeActionState>(entity);
        action && action->state == UnitState::Attacking)
        return AoeMotionStopReason::Attacking;
    if (const auto* intent = reg.try_get<AoeMovementIntent>(entity);
        intent && intent->locally_infeasible)
        return AoeMotionStopReason::LocalAvoidanceInfeasible;
    return AoeMotionStopReason::None;
}

bool begin_motion_trace(MotionDecisionTraceState& trace) {
    std::error_code error;
    fs::create_directories(trace.directory, error);
    trace.decisions.open(trace.directory / "aoe_motion_decisions.csv",
        std::ios::binary | std::ios::trunc);
    trace.summaries.open(trace.directory / "aoe_motion_stall_summary.log",
        std::ios::binary | std::ios::trunc);
    if (!trace.decisions || !trace.summaries) {
        trace.decisions.close();
        trace.summaries.close();
        return false;
    }
    trace.decisions
        << "tick,entity,instance,team,squad,pos_x,pos_y,radius_x,radius_y,"
           "action,path_current,"
           "path_count,goal_x,goal_y,requested_goal_x,requested_goal_y,no_path,"
           "path_dynamic,dynamic_repath_requested,dynamic_repath_failed,"
           "raw_x,raw_y,local_x,local_y,local_speed,local_threat,"
           "local_infeasible,local_neighbors,candidates,selected,group,ttc,"
           "global_mode,global_reason,yielding_to,peer_instance,wait_ticks,"
           "global_x,global_y,"
           "static_safe,dynamic_safe,safe_fraction,actual_x,actual_y,"
           "actual_speed,stalled_ticks,stop_reason\n";
    trace.last_tick = std::numeric_limits<std::uint64_t>::max();
    trace.last_summary_tick = 0;
    trace.capturing = true;
    return true;
}

void end_motion_trace(MotionDecisionTraceState& trace) {
    trace.capturing = false;
    trace.decisions.close();
    trace.summaries.close();
}

void toggle_motion_trace(EcsWorld& world) {
    auto& trace = world.resource<MotionDecisionTraceState>();
    auto& preview = world.resource<PreviewState>();
    if (trace.capturing) {
        end_motion_trace(trace);
        preview.latest = "motion decision trace stopped: " +
            trace.directory.string();
    } else if (begin_motion_trace(trace)) {
        preview.latest = "motion decision trace started: " +
            trace.directory.string();
    } else {
        preview.latest = "failed to open motion decision trace files";
    }
}
#endif

void input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    auto* state = world.try_resource<PreviewState>();
    if (!keyboard || !state) return;
    if (!state->orders_issued) issue_mutual_attack_move(world);
    if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    if (keyboard->just_now_pressed(GLFW_KEY_V)) {
        auto& window = world.resource<Window>();
        set_window_vsync(window, !window.vsync);
        state->latest = window.vsync ? "VSync enabled" : "VSync disabled";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_SPACE)) {
        state->orders_issued = false;
        issue_mutual_attack_move(world);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_S)) {
        request_aoe_squad_stop(world, state->blue);
        request_aoe_squad_stop(world, state->red);
        state->orders_issued = true;
        state->latest = "both squads stopped";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_P)) {
        state->draw_unit_feet = !state->draw_unit_feet;
        state->latest = state->draw_unit_feet
            ? "unit foot markers enabled" : "unit foot markers disabled";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_C)) {
        state->draw_unit_colliders = !state->draw_unit_colliders;
        state->latest = state->draw_unit_colliders
            ? "gameplay collision ellipses enabled"
            : "gameplay collision ellipses disabled";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_G)) {
        state->draw_map = !state->draw_map;
        state->latest = state->draw_map
            ? "map obstacle gizmos enabled" : "map obstacle gizmos disabled";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_N)) {
        state->draw_navigation = !state->draw_navigation;
        state->latest = state->draw_navigation
            ? "navigation gizmos enabled (affects performance)"
            : "navigation gizmos disabled";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_L)) {
        state->trace_unit_foot = !state->trace_unit_foot;
        state->trace_entity = entt::null;
        state->trace_instance_id = 0;
        state->trace_direction = -1;
        state->trace_has_previous = false;
        state->latest = state->trace_unit_foot
            ? "blue Archer foot trace enabled"
            : "blue Archer foot trace disabled";
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    if (keyboard->just_now_pressed(GLFW_KEY_T))
        toggle_motion_trace(world);
#endif
    for (const auto& preset : StressPresets) {
        if (!keyboard->just_now_pressed(GLFW_KEY_0 + preset.key)) continue;
        state->stress_preset = preset.key;
        reset_scene(world);
        break;
    }
    if (keyboard->just_now_pressed(GLFW_KEY_R)) reset_scene(world);
    if (keyboard->just_now_pressed(GLFW_KEY_F5)) {
        world.resource<AoeUnitDefinitionManager>().refresh();
        reset_scene(world);
        state->latest = "definitions rescanned and squads rebuilt";
    }
}

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
void motion_decision_trace_system(EcsWorld& world) {
    auto* trace = world.try_resource<MotionDecisionTraceState>();
    if (!trace || !trace->capturing) return;
    const auto& clock = world.resource<AoeGameplayClock>();
    if (trace->last_tick == clock.tick) return;
    trace->last_tick = clock.tick;
    auto& reg = world.reg();
    struct StalledUnit {
        std::uint64_t instance = 0;
        std::uint32_t stalled_ticks = 0;
        AoeMotionStopReason reason = AoeMotionStopReason::None;
    };
    constexpr std::size_t StopReasonCount =
        static_cast<std::size_t>(AoeMotionStopReason::Unknown) + 1u;
    std::array<std::uint32_t, StopReasonCount> stop_counts{};
    std::vector<StalledUnit> stalled_units;

    trace->decisions << std::fixed << std::setprecision(6);
    for (const auto entity : reg.view<AoeGameplayIdentity, AoePosition,
                                      AoeTeam>(entt::exclude<AoePooledUnit,
                                                            AoeRecyclePending>)) {
        const auto& identity = reg.get<AoeGameplayIdentity>(entity);
        const auto& position = reg.get<AoePosition>(entity);
        const auto& team = reg.get<AoeTeam>(entity);
        const auto* collider = reg.try_get<AoeCollider>(entity);
        const auto* member = reg.try_get<AoeSquadMember>(entity);
        const auto* action = reg.try_get<AoeActionState>(entity);
        const auto* path = reg.try_get<AoeNavigationPath>(entity);
        const auto* goal = reg.try_get<AoeMoveGoal>(entity);
        const auto* request = reg.try_get<AoePathMotionRequest>(entity);
        const auto* intent = reg.try_get<AoeMovementIntent>(entity);
        const auto* decision = reg.try_get<AoeGlobalMotionDecision>(entity);
        const auto* motion_state = reg.try_get<AoeGlobalMotionState>(entity);
        const auto* locomotion = reg.try_get<AoeLocomotionState>(entity);
        const bool request_valid = request && request->valid &&
            request->produced_tick == clock.tick;
        const bool intent_valid = intent && intent->valid &&
            intent->produced_tick == clock.tick;
        const bool decision_valid = decision && decision->valid &&
            decision->produced_tick == clock.tick;
        const glm::vec2 raw = request_valid
            ? request->velocity : glm::vec2{0.f};
        const glm::vec2 local = intent_valid
            ? intent->velocity : glm::vec2{0.f};
        const glm::vec2 global = decision_valid
            ? decision->velocity : glm::vec2{0.f};
        const glm::vec2 actual = locomotion
            ? locomotion->velocity : glm::vec2{0.f};
        const auto stop = observed_stop_reason(reg, entity);
        ++stop_counts[static_cast<std::size_t>(stop)];
        if (locomotion && locomotion->stalled_ticks > 0)
            stalled_units.push_back(
                {identity.instance_id, locomotion->stalled_ticks, stop});

        trace->decisions
            << clock.tick << ',' << static_cast<std::uint32_t>(entity) << ','
            << identity.instance_id << ',' << team.id << ','
            << (member ? static_cast<std::uint32_t>(member->squad) : 0u) << ','
            << position.value.x << ',' << position.value.y << ','
            << (collider ? collider->radius_x : 0.f) << ','
            << (collider ? collider->radius_y : 0.f) << ','
            << (action ? action_name(action->state) : "none") << ','
            << (path ? path->current : 0u) << ','
            << (path ? path->waypoints.size() : 0u) << ','
            << (goal ? goal->destination.x : 0.f) << ','
            << (goal ? goal->destination.y : 0.f) << ','
            << (path ? path->requested_goal.x : 0.f) << ','
            << (path ? path->requested_goal.y : 0.f) << ','
            << (path && path->no_path ? 1 : 0) << ','
            << (path && path->include_dynamic_obstacles ? 1 : 0) << ','
            << (path && path->dynamic_repath_requested ? 1 : 0) << ','
            << (path && path->dynamic_repath_failed ? 1 : 0) << ','
            << raw.x << ',' << raw.y << ',' << local.x << ',' << local.y << ','
            << glm::length(local) << ','
            << (intent_valid && intent->threatened ? 1 : 0) << ','
            << (intent_valid && intent->locally_infeasible ? 1 : 0) << ','
            << (intent_valid ? intent->neighbor_count : 0u) << ','
            << (decision_valid ? decision->candidate_count : 0u) << ','
            << (decision_valid ? decision->selected_conflicts : 0u) << ','
            << (decision_valid ? decision->conflict_group : 0u) << ','
            << (decision_valid ? decision->nearest_time_to_collision : 0.f)
            << ',' << (decision_valid
                ? motion_mode_name(decision->mode) : "none") << ','
            << (decision_valid
                ? motion_reason_name(decision->reason) : "none") << ','
            << (decision_valid ? decision->yielding_to_instance : 0u) << ','
            << (motion_state ? motion_state->peer_instance_id : 0u) << ','
            << (decision_valid ? decision->wait_ticks : 0u) << ','
            << global.x << ',' << global.y << ','
            << (decision_valid ? decision->static_safe_fraction : 1.f) << ','
            << (decision_valid ? decision->dynamic_safe_fraction : 1.f) << ','
            << (decision_valid ? decision->safe_fraction : 1.f) << ','
            << actual.x << ',' << actual.y << ',' << glm::length(actual) << ','
            << (locomotion ? locomotion->stalled_ticks : 0u) << ','
            << motion_stop_name(stop) << '\n';
    }

    const auto& gameplay_settings = world.resource<AoeGameplaySettings>();
    const auto summary_interval = static_cast<std::uint64_t>(std::max(
        1.0, std::round(1.0 / gameplay_settings.fixed_dt)));
    if (trace->last_summary_tick == 0 ||
        clock.tick - trace->last_summary_tick >= summary_interval) {
        trace->last_summary_tick = clock.tick;
        std::sort(stalled_units.begin(), stalled_units.end(),
            [](const StalledUnit& a, const StalledUnit& b) {
                if (a.stalled_ticks != b.stalled_ticks)
                    return a.stalled_ticks > b.stalled_ticks;
                return a.instance < b.instance;
            });
        trace->summaries << "tick=" << clock.tick
                         << " stalled=" << stalled_units.size();
        for (std::size_t i = 0; i < stop_counts.size(); ++i) {
            if (stop_counts[i] == 0) continue;
            trace->summaries << ' ' << motion_stop_name(
                static_cast<AoeMotionStopReason>(i)) << '=' << stop_counts[i];
        }
        trace->summaries << " longest=";
        const auto longest = std::min<std::size_t>(10, stalled_units.size());
        for (std::size_t i = 0; i < longest; ++i) {
            if (i > 0) trace->summaries << ';';
            trace->summaries << stalled_units[i].instance << ':'
                             << stalled_units[i].stalled_ticks << ':'
                             << motion_stop_name(stalled_units[i].reason);
        }
        trace->summaries << '\n';
        trace->decisions.flush();
        trace->summaries.flush();
    }
}
#endif

glm::vec2 formation_slot_world(const AoePosition& center,
                               const AoeSquadFormation& formation,
                               const AoeFormationSlot& slot) {
    const glm::vec2 forward = glm::length(formation.forward) > 1e-5f
        ? glm::normalize(formation.forward) : glm::vec2(1.f, 0.f);
    const glm::vec2 right{forward.y, -forward.x};
    return center.value + right * slot.local_offset.x +
           forward * slot.local_offset.y;
}

void draw_navigation_path(Gizmos& gizmos, glm::vec2 start,
                          const AoeNavigationPath& path, glm::vec4 color) {
    if (path.no_path || path.current >= path.waypoints.size()) return;
    glm::vec3 from = project_logical_position(start);
    for (std::size_t i = path.current; i < path.waypoints.size(); ++i) {
        const glm::vec3 to = project_logical_position(path.waypoints[i]);
        gizmos.line(from, to, color, UnitLayer);
        from = to;
    }
}

void draw_squad_navigation(EcsWorld& world, entt::entity squad,
                           glm::vec4 guide_color, glm::vec4 member_color) {
    auto& reg = world.reg();
    if (!reg.valid(squad) ||
        !reg.all_of<AoePosition, AoeSquadFormation, AoeSquadMembers>(squad))
        return;
    auto& gizmos = world.resource<Gizmos>();
    const auto& center = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    gizmos.cross(project_logical_position(center.value), 5.f,
                 {1.f, 1.f, 1.f, 1.f}, UnitLayer);
    if (const auto* path = reg.try_get<AoeNavigationPath>(squad))
        draw_navigation_path(gizmos, center.value, *path, guide_color);

    for (const auto& slot : formation.slots) {
        const glm::vec2 destination = formation_slot_world(
            center, formation, slot);
        gizmos.cross(project_logical_position(destination), 2.5f,
                     {1.f, .88f, .15f, .8f}, UnitLayer);
        if (!reg.valid(slot.unit.entity) ||
            !reg.all_of<AoePosition, AoeGameplayIdentity>(slot.unit.entity) ||
            reg.get<AoeGameplayIdentity>(slot.unit.entity).instance_id !=
                slot.unit.instance_id)
            continue;
        if (const auto* path = reg.try_get<AoeNavigationPath>(slot.unit.entity))
            draw_navigation_path(gizmos,
                reg.get<AoePosition>(slot.unit.entity).value,
                *path, member_color);
    }
}

void submit_map_navigation_gizmos(EcsWorld& world) {
    const auto& preview = world.resource<PreviewState>();
    const auto* map = world.try_resource<AoeLogicMap>();
    if (!map || !map->valid()) return;
    auto& gizmos = world.resource<Gizmos>();
    if (preview.draw_map) {
        const glm::vec2 min = map->origin();
        const glm::vec2 max = min + glm::vec2(
            map->width() * map->tile_size(),
            map->height() * map->tile_size());
        const std::array boundary{
            glm::vec2{min.x, min.y}, glm::vec2{max.x, min.y},
            glm::vec2{max.x, max.y}, glm::vec2{min.x, max.y}};
        for (std::size_t i = 0; i < boundary.size(); ++i)
            gizmos.line(project_logical_position(boundary[i]),
                        project_logical_position(boundary[(i + 1) % boundary.size()]),
                        {.55f, .62f, .7f, .9f}, UnitLayer);

        for (const auto& obstacle : preview.map_obstacles) {
            if (obstacle.shape == AoeStaticObstacleShape::Aabb) {
                const glm::vec2 low = obstacle.center - obstacle.half_extents;
                const glm::vec2 high = obstacle.center + obstacle.half_extents;
                const std::array corners{
                    glm::vec2{low.x, low.y}, glm::vec2{high.x, low.y},
                    glm::vec2{high.x, high.y}, glm::vec2{low.x, high.y}};
                for (std::size_t i = 0; i < corners.size(); ++i)
                    gizmos.line(project_logical_position(corners[i]),
                        project_logical_position(corners[(i + 1) % corners.size()]),
                        {1.f, .45f, .08f, 1.f}, UnitLayer);
            } else {
                const glm::vec3 center = project_logical_position(obstacle.center);
                const glm::vec3 axis_x = project_logical_position(
                    obstacle.center + glm::vec2(obstacle.radius, 0.f)) - center;
                const glm::vec3 axis_y = project_logical_position(
                    obstacle.center + glm::vec2(0.f, obstacle.radius)) - center;
                gizmos.wire_ellipse(center, axis_x, axis_y, 32,
                                    {1.f, .25f, .08f, 1.f}, UnitLayer);
            }
        }
    }
    if (preview.draw_navigation) {
        draw_squad_navigation(world, preview.blue,
            {.2f, .65f, 1.f, 1.f}, {.2f, .65f, 1.f, .3f});
        draw_squad_navigation(world, preview.red,
            {1.f, .25f, .3f, 1.f}, {1.f, .25f, .3f, .3f});
    }
}

void submit_unit_foot_gizmos(EcsWorld& world) {
    const auto& preview = world.resource<PreviewState>();
    if (!preview.draw_unit_feet) return;

    auto& reg = world.reg();
    auto& gizmos = world.resource<Gizmos>();
    for (const auto gameplay : reg.view<Aoe2PresentationLink>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
        const auto child = reg.get<Aoe2PresentationLink>(gameplay).render;
        if (!reg.valid(child)) continue;
        const auto* render = reg.try_get<Aoe2UnitRender>(child);
        const auto* global = reg.try_get<GlobalTransform>(child);
        if (!render || !global || !render->visible || !render->has_main) continue;

        // Every exported frame has a crop-local SLD foot. The AoE2 vertex
        // shader subtracts that foot from the quad, so its final world-space
        // location is exactly the render child's transformed local origin.
        const glm::vec3 foot = glm::vec3(global->world[3]);
        gizmos.cross(foot, 3.5f, {1.f, 1.f, 1.f, 1.f}, UnitLayer);
    }
}

void submit_unit_collider_gizmos(EcsWorld& world) {
    const auto& preview = world.resource<PreviewState>();
    if (!preview.draw_unit_colliders) return;
    auto& reg = world.reg();
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const auto* map = world.try_resource<AoeLogicMap>();
    auto& gizmos = world.resource<Gizmos>();
    for (const auto entity : reg.view<AoeGameplayIdentity, AoePosition,
                                      AoeCollider, AoeTeam>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
        const auto logical = aoe_interpolated_position(
            reg.get<AoePosition>(entity),
            reg.try_get<AoePositionHistory>(entity), clock, settings);
        const float height = map && map->valid()
            ? map->sample_height(logical).value_or(0.f) : 0.f;
        const auto center = project_logical_position(logical, height);
        const auto& collider = reg.get<AoeCollider>(entity);
        const glm::vec3 axis_x = project_logical_position(
            logical + glm::vec2(collider.radius_x, 0.f), height) - center;
        const glm::vec3 axis_y = project_logical_position(
            logical + glm::vec2(0.f, collider.radius_y), height) - center;
        const auto team = reg.get<AoeTeam>(entity).id;
        const glm::vec4 color = team == 1
            ? glm::vec4(.2f, .75f, 1.f, .9f)
            : team == 2 ? glm::vec4(1.f, .3f, .25f, .9f)
                        : glm::vec4(.7f, 1.f, .35f, .9f);
        gizmos.wire_ellipse(center, axis_x, axis_y, 24, color, UnitLayer);
    }
}

void trace_blue_archer_foot(EcsWorld& world) {
    auto& preview = world.resource<PreviewState>();
    if (!preview.trace_unit_foot) return;

    auto& reg = world.reg();
    entt::entity selected{entt::null};
    std::uint64_t selected_instance = 0;
    std::uint32_t selected_ordinal = std::numeric_limits<std::uint32_t>::max();
    if (reg.valid(preview.blue)) {
        if (const auto* members = reg.try_get<AoeSquadMembers>(preview.blue)) {
            for (const auto& member : members->active) {
                if (!reg.valid(member.entity) ||
                    reg.any_of<AoePooledUnit, AoeRecyclePending>(member.entity) ||
                    !reg.all_of<AoeGameplayIdentity, AoeUnitDefinitionRef,
                                AoeSquadMember, AoePosition,
                                Aoe2PresentationLink>(member.entity))
                    continue;
                const auto& identity = reg.get<AoeGameplayIdentity>(member.entity);
                if (identity.instance_id != member.instance_id) continue;
                const auto* definition = reg.get<AoeUnitDefinitionRef>(
                    member.entity).value.get();
                if (!definition || definition->id != "archer") continue;
                const auto ordinal = reg.get<AoeSquadMember>(member.entity).ordinal;
                if (ordinal < selected_ordinal) {
                    selected = member.entity;
                    selected_instance = identity.instance_id;
                    selected_ordinal = ordinal;
                }
            }
        }
    }

    if (selected == entt::null) {
        if (preview.trace_entity != entt::null)
            std::printf("[aoe-foot] no valid blue Archer\n");
        preview.trace_entity = entt::null;
        preview.trace_instance_id = 0;
        preview.trace_direction = -1;
        preview.trace_has_previous = false;
        std::fflush(stdout);
        return;
    }
    if (selected != preview.trace_entity ||
        selected_instance != preview.trace_instance_id) {
        preview.trace_entity = selected;
        preview.trace_instance_id = selected_instance;
        preview.trace_direction = -1;
        preview.trace_has_previous = false;
    }

    const auto child = reg.get<Aoe2PresentationLink>(selected).render;
    if (!reg.valid(child)) return;
    const auto* render = reg.try_get<Aoe2UnitRender>(child);
    const auto* global = reg.try_get<GlobalTransform>(child);
    const auto* facing = reg.try_get<AoeFacing>(selected);
    const auto* action = reg.try_get<AoeActionState>(selected);
    const auto* history = reg.try_get<AoePositionHistory>(selected);
    if (!render || !global || !facing || !action || !render->visible ||
        !render->has_main)
        return;

    const auto& position = reg.get<AoePosition>(selected);
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    const glm::vec2 interpolated = aoe_interpolated_position(
        position, history, clock, settings);
    const glm::vec3 expected = project_logical_position(interpolated);
    const glm::vec3 origin = glm::vec3(global->world[3]);
    const glm::vec3 error = origin - expected;
    const bool turned = preview.trace_has_previous &&
                        render->direction != preview.trace_direction;
    const glm::vec2 hotspot_delta = turned
        ? render->main_frame.foot - preview.trace_raw_foot
        : glm::vec2(0.f);
    const glm::vec2 previous = history ? history->previous : position.value;
    const auto& time = world.resource<Time>();

    std::printf(
        "[aoe-foot] frame=%llu tick=%llu entity=%u instance=%llu "
        "state=%s facing=%d/%d resolved=%d animation=%s anim_frame=%d "
        "crop=%dx%d raw_foot=(%.1f,%.1f) history=(%.4f,%.4f) "
        "authoritative=(%.4f,%.4f) interpolated=(%.4f,%.4f) "
        "origin=(%.3f,%.3f,%.3f) expected=(%.3f,%.3f,%.3f) "
        "origin_error=(%.5f,%.5f,%.5f) turn=%d hotspot_delta=(%.1f,%.1f)\n",
        static_cast<unsigned long long>(time.frame),
        static_cast<unsigned long long>(clock.tick),
        static_cast<unsigned>(selected),
        static_cast<unsigned long long>(selected_instance),
        action_name(action->state), facing->direction, facing->direction_count,
        render->direction, render->animation.c_str(), render->current_frame,
        render->main_frame.width, render->main_frame.height,
        render->main_frame.foot.x, render->main_frame.foot.y,
        previous.x, previous.y, position.value.x, position.value.y,
        interpolated.x, interpolated.y, origin.x, origin.y, origin.z,
        expected.x, expected.y, expected.z, error.x, error.y, error.z,
        turned ? 1 : 0, hotspot_delta.x, hotspot_delta.y);
    std::fflush(stdout);
    preview.trace_direction = render->direction;
    preview.trace_raw_foot = render->main_frame.foot;
    preview.trace_has_previous = true;
}

void append_squad(std::ostringstream& out, EcsWorld& world,
                  const char* label, entt::entity squad) {
    auto& reg = world.reg();
    out << label << " squad=" << static_cast<std::uint32_t>(squad);
    if (!reg.valid(squad) ||
        !reg.all_of<AoeSquadSpawnState, AoeSquadState, AoePosition,
                    AoeSquadFormation, AoeSquadOrder, AoeSquadMembers,
                    AoeTeam>(squad)) {
        out << " invalid\n";
        return;
    }
    const auto& spawn = reg.get<AoeSquadSpawnState>(squad);
    const auto& state = reg.get<AoeSquadState>(squad);
    const auto& position = reg.get<AoePosition>(squad);
    const auto& formation = reg.get<AoeSquadFormation>(squad);
    const auto& order = reg.get<AoeSquadOrder>(squad);
    const auto& members = reg.get<AoeSquadMembers>(squad);
    std::uint32_t camels = 0;
    std::uint32_t archers = 0;
    std::uint32_t idle = 0;
    std::uint32_t moving = 0;
    std::uint32_t attacking = 0;
    std::uint32_t dying = 0;
    std::uint32_t disappearing = 0;
    std::uint32_t render_ready = 0;
    std::uint32_t no_path = 0;
    std::uint32_t dynamic_repath_failed = 0;
    std::uint32_t beyond_leash = 0;
    std::uint32_t stalled = 0;
    std::uint32_t escaping = 0;
    std::uint32_t elastic = 0;
    std::uint32_t flow_following = 0;
    std::uint32_t flow_passing = 0;
    std::uint32_t flow_yielding = 0;
    std::uint32_t flow_backing = 0;
    std::uint32_t flow_infeasible = 0;
    float current_hp = 0.f;
    float max_hp = 0.f;
    const float leash = world.resource_or_add<AoeNavigationSettings>().squad_leash;
    for (const auto& member : members.active) {
        if (!reg.valid(member.entity) ||
            !reg.all_of<AoeGameplayIdentity, AoeUnitDefinitionRef,
                        AoeHealth, AoeActionState, AoePosition>(member.entity) ||
            reg.get<AoeGameplayIdentity>(member.entity).instance_id !=
                member.instance_id)
            continue;
        const auto* definition = reg.get<AoeUnitDefinitionRef>(member.entity).value.get();
        if (definition && definition->id == "camel_scout") ++camels;
        if (definition && definition->id == "archer") ++archers;
        const auto& action = reg.get<AoeActionState>(member.entity);
        switch (action.state) {
        case UnitState::Idle: ++idle; break;
        case UnitState::Moving: ++moving; break;
        case UnitState::Attacking: ++attacking; break;
        case UnitState::Dying: ++dying; break;
        case UnitState::Disappearing: ++disappearing; break;
        }
        const auto& health = reg.get<AoeHealth>(member.entity);
        current_hp += health.current;
        max_hp += health.maximum;
        if (const auto* path = reg.try_get<AoeNavigationPath>(member.entity); path) {
            if (path->no_path) ++no_path;
            if (path->dynamic_repath_failed) ++dynamic_repath_failed;
        }
        if (const auto* locomotion =
                reg.try_get<AoeLocomotionState>(member.entity)) {
            if (locomotion->stalled_ticks > 0) ++stalled;
        }
        if (const auto* avoidance =
                reg.try_get<AoeLocalAvoidanceState>(member.entity)) {
            if (avoidance->escape_steering) ++escaping;
            if (avoidance->infeasible) ++flow_infeasible;
        }
        if (const auto* flow =
                reg.try_get<AoeGlobalMotionDecision>(member.entity)) {
            switch (flow->mode) {
            case AoeGlobalMotionMode::SideStep: ++flow_following; break;
            case AoeGlobalMotionMode::PassingLeft:
            case AoeGlobalMotionMode::PassingRight: ++flow_passing; break;
            case AoeGlobalMotionMode::Yielding: ++flow_yielding; break;
            case AoeGlobalMotionMode::Backing: ++flow_backing; break;
            case AoeGlobalMotionMode::Clear:
            case AoeGlobalMotionMode::Recovering: break;
            }
        }
        if (const auto* follow =
                reg.try_get<AoeSquadSlotFollowState>(member.entity);
            follow && follow->elastic)
            ++elastic;
        if (const auto* link = reg.try_get<Aoe2PresentationLink>(member.entity);
            link && reg.valid(link->render) &&
            reg.all_of<Aoe2UnitRender>(link->render))
            ++render_ready;
    }
    for (const auto& slot : formation.slots) {
        if (!reg.valid(slot.unit.entity) ||
            !reg.all_of<AoeGameplayIdentity, AoePosition>(slot.unit.entity) ||
            reg.get<AoeGameplayIdentity>(slot.unit.entity).instance_id !=
                slot.unit.instance_id)
            continue;
        const glm::vec2 destination = formation_slot_world(
            position, formation, slot);
        if (glm::length(reg.get<AoePosition>(slot.unit.entity).value -
                        destination) > leash)
            ++beyond_leash;
    }
    const auto* guide = reg.try_get<AoeNavigationPath>(squad);
    out << " team=" << reg.get<AoeTeam>(squad).id
        << " spawn=" << spawn_name(spawn.status)
        << " phase=" << phase_name(state.phase)
        << " members=" << members.active.size() << '/' << spawn.requested
        << " render=" << render_ready << '\n'
        << "  center=(" << position.value.x << ',' << position.value.y << ')'
        << " forward=(" << formation.forward.x << ',' << formation.forward.y << ')'
        << " speed=" << state.movement_speed;
    if (order.type == AoeSquadOrderType::MoveTo ||
        order.type == AoeSquadOrderType::AttackMove)
        out << " destination=(" << order.destination.x << ',' << order.destination.y << ')';
    if (order.target.entity != entt::null)
        out << " target=" << static_cast<std::uint32_t>(order.target.entity)
            << ':' << order.target.instance_id;
    out << '\n'
        << "  types camel=" << camels << " archer=" << archers
        << " hp=" << current_hp << '/' << max_hp << '\n'
        << "  states I=" << idle << " M=" << moving << " A=" << attacking
        << " D=" << dying << " X=" << disappearing
        << " no-path=" << no_path << " dyn-repath-failed="
        << dynamic_repath_failed << " beyond-leash=" << beyond_leash << '\n'
        << "  locomotion stalled=" << stalled << " escape=" << escaping
        << " elastic-slot=" << elastic << '\n'
        << "  global-motion side=" << flow_following << " pass=" << flow_passing
        << " yield=" << flow_yielding << " back=" << flow_backing
        << " infeasible=" << flow_infeasible << '\n'
        << "  guide=";
    if (guide) {
        out << guide->current << '/' << guide->waypoints.size()
            << " no-path=" << (guide->no_path ? "yes" : "no")
            << " dynamic=" << (guide->include_dynamic_obstacles ? "yes" : "no");
    } else out << "none";
    if (const auto* traffic = reg.try_get<AoeSquadTrafficState>(squad))
        out << " traffic=" << traffic_name(traffic->mode)
            << " speed-scale=" << traffic->speed_scale
            << " lateral=" << traffic->lateral_offset;
    out << '\n';
    const std::size_t error_limit = std::min<std::size_t>(spawn.errors.size(), 3);
    for (std::size_t i = 0; i < error_limit; ++i)
        out << "    spawn error: " << spawn.errors[i] << '\n';
    if (spawn.errors.size() > error_limit)
        out << "    ... " << (spawn.errors.size() - error_limit)
            << " more spawn errors\n";
}

void diagnostics_system(EcsWorld& world) {
    auto& preview = world.resource<PreviewState>();
    const auto& time = world.resource<Time>();
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto* aoe2_performance = world.try_resource<Aoe2PerformanceDiagnostics>();
    update_frame_statistics(preview, time, clock.ticks_this_frame,
        aoe2_performance ? aoe2_performance->animation_frame_changes : 0u);
    preview.hud_elapsed += time.raw_dt;
    if (preview.hud_elapsed < .25 || !world.reg().valid(preview.hud)) return;
    preview.hud_elapsed = 0.0;
    const auto* dynamic = world.try_resource<AoeDynamicObstacleIndex>();
    if (dynamic) {
        const auto& value = dynamic->diagnostics();
        preview.interval_dynamic_queries = value.queries >= preview.last_dynamic_queries
            ? value.queries - preview.last_dynamic_queries : value.queries;
        preview.interval_dynamic_candidates =
            value.candidates >= preview.last_dynamic_candidates
            ? value.candidates - preview.last_dynamic_candidates : value.candidates;
        preview.last_dynamic_queries = value.queries;
        preview.last_dynamic_candidates = value.candidates;
    }
    const auto& preset = active_stress_preset(preview);
    const auto* render = world.try_resource<RenderDiagnostics>();
    const auto& gameplay = world.resource<AoeGameplayDiagnostics>();
    const auto* motion_planner =
        world.try_resource<AoeGlobalMotionPlannerDiagnostics>();
    const auto* gpu_motion = world.try_resource<AoeGpuMotionDiagnostics>();
    const auto* logic_map = world.try_resource<AoeLogicMap>();
    const auto* pool = world.try_resource<AoeGameplayPool>();
    std::size_t active_units = 0;
    for ([[maybe_unused]] const auto entity :
         world.reg().view<AoeGameplayIdentity>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>))
        ++active_units;
    const auto projectiles = world.reg().view<AoeProjectile>().size();
    const std::size_t total_entities =
        world.reg().storage<entt::entity>().free_list();
    const std::size_t render_units =
        world.reg().view<AoeGameplayOwner>().size();
    const std::size_t render_projectiles =
        world.reg().view<AoeProjectileOwner>().size();
    const std::size_t squads = world.reg().view<AoeSquadMembers>().size();
    const std::size_t batch_entities =
        world.reg().view<Aoe2BatchComponent>().size();
    const std::size_t pooled = pool ? pool->available.size() : 0;
    const std::size_t classified = active_units + pooled + projectiles +
        render_units + render_projectiles + squads + batch_entities;
    const std::size_t other_entities = total_entities > classified
        ? total_entities - classified : 0;
    preview.entity_high_water = std::max(
        preview.entity_high_water, total_entities);
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "AoE gameplay mapped squad stress preview\n"
        << "1-6 total units = 128/512/2000/5000/10000/20000"
           " | Space attack-move | S stop | R reset | F5 reload\n"
        << "G map=" << (preview.draw_map ? "ON" : "OFF")
        << " | squad-engagement="
        << SquadGameplayDef::SquadEngagementPlugin::name << " (static)"
        << " | formation="
        << SquadGameplayDef::FormationPlugin::name << " (static)"
        << " | arrival-rematch="
        << SquadGameplayDef::SquadArrivalRematchPlugin::name << " (static)"
        << " | local-avoidance="
        << SquadGameplayDef::LocalAvoidancePlugin::name << " (static)"
        << " | global-motion="
        << SquadGameplayDef::GlobalMotionPlugin::name << " (static)"
        << " | N navigation=" << (preview.draw_navigation ? "ON" : "OFF")
        << " | C collision=" << (preview.draw_unit_colliders ? "ON" : "OFF")
        << " | P feet=" << (preview.draw_unit_feet ? "ON" : "OFF")
        << " | L trace=" << (preview.trace_unit_foot ? "ON" : "OFF")
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        << " | T motion-log="
        << (world.resource<MotionDecisionTraceState>().capturing ? "ON" : "OFF")
#endif
        << " | V vsync="
        << (world.resource<Window>().vsync ? "ON" : "OFF")
        << " | Esc quit\n"
        << "preset=" << preset.key
        << " units/side=" << preset.units_per_side()
        << " units/total=" << preset.total_units()
        << " (debug gizmos affect stress measurements)\n"
        << "map=squad_preview grid=";
    if (logic_map && logic_map->valid())
        out << logic_map->width() << 'x' << logic_map->height();
    else
        out << "unavailable";
    out << " pathfinder=grid_astar\n\n";
    append_squad(out, world, "BLUE", preview.blue);
    append_squad(out, world, "RED ", preview.red);
    const float displayed_fps = preview.displayed_fps > 0.f
        ? preview.displayed_fps : time.fps;
    const float displayed_frame_dt = preview.displayed_frame_dt_ms > 0.f
        ? preview.displayed_frame_dt_ms : time.raw_dt * 1000.f;
    out << "\nPERFORMANCE render_fps=" << displayed_fps
        << " frame_dt_ms=" << displayed_frame_dt
        << " p95_ms=" << preview.displayed_frame_p95_ms
        << " max_ms=" << preview.displayed_frame_max_ms
        << " fixed_ticks=" << clock.ticks_this_frame
        << " dropped_s=" << clock.dropped_seconds << '\n'
        << "cadence fixed_hz=" << preview.displayed_fixed_hz
        << " pose_change_hz=" << preview.displayed_pose_change_hz
        << " pose_changes_s=" << preview.displayed_pose_changes_per_second << '\n'
        << "entities live=" << total_entities
        << " peak=" << preview.entity_high_water
        << " gameplay=" << active_units
        << " render=" << render_units
        << " squads=" << squads
        << " batches=" << batch_entities
        << " other=" << other_entities << '\n'
        << "gameplay pooled=" << pooled
        << " projectiles=" << projectiles
        << " projectile-render=" << render_projectiles
        << " attacks=" << gameplay.attacks_started
        << " damage=" << gameplay.damage_events
        << " rejected=" << gameplay.commands_rejected << '\n';
    if (motion_planner) {
        out << "motion backend=" << motion_planner->active_backend
            << " requested=" << motion_planner->requested_backend
            << " gpu_ticks=" << motion_planner->gpu_ticks
            << " cpu_ticks=" << motion_planner->cpu_ticks
            << " fallbacks=" << motion_planner->fallback_ticks
            << " corrections=" << motion_planner->authoritative_corrections;
        if (!motion_planner->fallback_reason.empty())
            out << " reason=" << motion_planner->fallback_reason;
        out << '\n';
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    if (gpu_motion && gpu_motion->available)
        out << "gpu-motion total_ms=" << gpu_motion->last_tick_ms
            << " upload_ms=" << gpu_motion->upload_ms
            << " dispatch_ms=" << gpu_motion->dispatch_ms
            << " readback_ms=" << gpu_motion->readback_ms
            << " map=" << gpu_motion->map_width_pixels << 'x'
            << gpu_motion->map_height_pixels << '\n';
#else
    (void)gpu_motion;
#endif
    if (dynamic) {
        const auto& value = dynamic->diagnostics();
        out << "dynamic-index units=" << value.units_indexed
            << " memberships=" << value.cell_memberships
            << " interval_queries=" << preview.interval_dynamic_queries
            << " candidates=" << preview.interval_dynamic_candidates << '\n';
    }
    out << "steering fast=" << gameplay.steering_fast_path
        << " full=" << gameplay.steering_full_solves
        << " cached=" << gameplay.steering_cached_solves
        << " imminent=" << gameplay.steering_imminent_solves
        << " neighbors=" << gameplay.steering_neighbors_considered << '\n'
        << "continuity side-switch=" << gameplay.steering_side_switches
        << " facing-suppressed=" << gameplay.facing_changes_suppressed
        << " facing-committed=" << gameplay.facing_changes_committed << '\n'
        << "unit-flow intents=" << gameplay.flow_active_intents
        << " checks=" << gameplay.flow_neighbor_checks
        << " conflicts=" << gameplay.flow_conflicts
        << " follow=" << gameplay.flow_following
        << " pass=" << gameplay.flow_passing
        << " yield=" << gameplay.flow_yielding
        << " back=" << gameplay.flow_backing
        << " infeasible=" << gameplay.flow_infeasible_assignments
        << " projections=" << gameplay.flow_overlap_projections
        << " escalations=" << gameplay.flow_deadlock_escalations << '\n';
    out << "movement ms=" << gameplay.movement_last_ms
        << " peak=" << gameplay.movement_peak_ms << '\n';
    if (aoe2_performance) {
        out << "aoe2 anim_ms=" << aoe2_performance->animation_ms
            << " batch_ms=" << aoe2_performance->batch_total_ms
            << " units=" << aoe2_performance->batch_units
            << " groups=" << aoe2_performance->batch_groups
            << " rebuilt=" << aoe2_performance->batch_rebuilt_instances
            << " unchanged=" << aoe2_performance->unchanged_instances << '\n';
    }
    if (render) {
        out << "render draws=" << render->batch_draws
            << " instances=" << render->batch_instances
            << " uploads=" << render->batch_uploads
            << " upload_kib=" << render->batch_upload_bytes / 1024.0
            << " upload_ms=" << render->batch_upload_ms
            << " submit_ms=" << render->batch_submit_ms << '\n';
    }
    out << "latest: " << preview.latest;
    auto& text = world.reg().get<Text>(preview.hud);
    text.text = ascii_to_u32(out.str());
    ++text.rev;
    if (std::getenv("GLD_AOE_PROFILE")) {
        std::fprintf(stderr, "%s\n", out.str().c_str());
        std::ofstream profile("aoe_gameplay_squad_profile.log",
                              std::ios::app);
        profile << out.str() << '\n';
    }
}

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
void system_profile_begin(EcsWorld& world) {
    auto* profile = world.try_resource<SystemProfileState>();
    if (!profile || !profile->enabled) return;
    profile->frame_start = std::chrono::steady_clock::now();
    profile->frame_started = true;
}

std::size_t active_gameplay_units(EcsWorld& world) {
    std::size_t result = 0;
    for ([[maybe_unused]] const auto entity :
         world.reg().view<AoeGameplayIdentity>(
             entt::exclude<AoePooledUnit, AoeRecyclePending>))
        ++result;
    return result;
}

void write_system_profile(SystemProfileState& profile, EcsWorld& world) {
    const auto parent = profile.output.parent_path();
    if (!parent.empty()) fs::create_directories(parent);
    std::ofstream output(profile.output, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::fprintf(stderr, "[aoe_squad_profile] cannot write %s\n",
                     profile.output.string().c_str());
    } else {
        output << profile.csv.str();
        std::printf("[aoe_squad_profile] wrote %s\n",
                    profile.output.string().c_str());
    }
    profile.enabled = false;
    world.resource<Window>().should_close = true;
}

void system_profile_end(EcsWorld& world) {
    auto* profile = world.try_resource<SystemProfileState>();
    if (!profile || !profile->enabled || !profile->frame_started) return;
    const double frame_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - profile->frame_start).count();
    profile->frame_started = false;

    const auto& time = world.resource<Time>();
    const auto& preview = world.resource<PreviewState>();
    const auto& aoe2 = world.resource<Aoe2PerformanceDiagnostics>();
    const bool no_gameplay_spawns =
        world.reg().view<AoeGameplaySpawnRequest>().size() == 0;
    const bool no_aoe2_spawns = world.reg().view<Aoe2SpawnRequest>().size() == 0;
    const std::size_t gameplay_units = active_gameplay_units(world);
    const std::size_t render_units = world.reg().view<AoeGameplayOwner>().size();
    const std::size_t expected_units =
        active_stress_preset(preview).total_units();
    const bool stable = preview.orders_issued && no_gameplay_spawns && no_aoe2_spawns &&
        gameplay_units >= expected_units && render_units >= expected_units;
    if (!profile->capturing) {
        profile->stable_seconds = stable
            ? profile->stable_seconds + std::max(0.f, time.raw_dt) : 0.0;
        if (profile->stable_seconds < 3.0) return;
        profile->capturing = true;
        profile->csv
            << "frame,capture_s,frame_ms,preceding_raw_dt_ms,time_fps,hud_fps,hud_avg_ms,"
               "hud_p95_ms,hud_max_ms,fixed_ticks,dropped_s,gameplay_units,"
               "render_units,squad_engagement_backend,formation_backend,arrival_rematch_backend,local_avoidance_backend,global_motion_backend,"
               "projectiles,g_clear_ms,g_spawn_ms,g_fixed_ms,"
               "g_recycle_ms,g_history_ms,g_squad_spawn_ms,g_static_index_ms,"
               "g_dynamic_index_ms,g_squad_command_ms,g_command_ms,"
               "g_membership_ms,g_squad_traffic_ms,g_squad_engagement_ms,g_squad_control_ms,g_formation_ms,g_arrival_rematch_ms,"
               "g_acquisition_ms,g_navigation_ms,g_movement_intent_ms,"
               "g_local_avoidance_ms,g_unit_flow_ms,g_motion_safety_ms,"
               "g_movement_ms,g_combat_ms,g_projectile_ms,g_lifecycle_ms,"
               "move_speed_samples,move_base_avg,move_effective_avg,move_desired_avg,"
               "move_steering_avg,move_actual_avg,move_actual_base_ratio,"
               "move_actual_effective_ratio,move_squad_limited,move_arrive_limited,"
               "move_turn_limited,move_steering_limited,move_safe_limited,"
               "aoe2_spawn_ms,aoe2_animation_ms,aoe2_batch_ms,aoe2_membership_ms,"
               "aoe2_instance_ms,bridge_orphan_ms,bridge_unit_ms,bridge_projectile_ms,"
               "transform_ms,render_prepare_ms,render_upload_ms,render_submit_ms,"
               "render_gpu_ms,present_ms,animation_frame_changes,frame_dirty_instances,"
               "transform_dirty_instances,unchanged_instances,transform_changed,"
               "known_cpu_ms,residual_ms,"
               "nav_astar_calls,nav_cells_expanded,nav_clear_segment_calls,"
               "nav_repath_units,nav_find_ms,nav_find_peak_ms,"
               "gpu_total_ms,gpu_upload_ms,gpu_dispatch_ms,gpu_readback_ms\n";
        profile->csv << std::fixed << std::setprecision(6);
        std::printf("[aoe_squad_profile] stable; capturing %.1f seconds\n",
                    profile->capture_target_seconds);
        return;
    }

    const auto& gameplay = world.resource<AoeGameplayPerformanceDiagnostics>();
    const auto& bridge = world.resource<Aoe2GameplayBridgePerformanceDiagnostics>();
    const auto& transform = world.resource<TransformDiagnostics>();
    const auto& render = world.resource<RenderDiagnostics>();
    const auto& clock = world.resource<AoeGameplayClock>();
    const double speed_samples = static_cast<double>(
        gameplay.movement_speed_samples);
    const double base_average = speed_samples > 0.0
        ? gameplay.movement_base_speed_sum / speed_samples : 0.0;
    const double effective_average = speed_samples > 0.0
        ? gameplay.movement_effective_speed_sum / speed_samples : 0.0;
    const double desired_average = speed_samples > 0.0
        ? gameplay.movement_desired_speed_sum / speed_samples : 0.0;
    const double steering_average = speed_samples > 0.0
        ? gameplay.movement_steering_speed_sum / speed_samples : 0.0;
    const double actual_average = speed_samples > 0.0
        ? gameplay.movement_actual_speed_sum / speed_samples : 0.0;
    const std::size_t projectiles = world.reg().view<AoeProjectile>().size();
    const double gameplay_total = gameplay.clear_events_ms + gameplay.spawn_ms +
        gameplay.fixed_total_ms + gameplay.recycle_ms;
    const double aoe2_total = aoe2.spawn_ms + aoe2.animation_ms +
        aoe2.batch_total_ms;
    const double bridge_total = bridge.orphan_cleanup_ms +
        bridge.unit_presentation_ms + bridge.projectile_presentation_ms;
    // render_upload_ms is a subset of render_submit_ms and is not added twice.
    const double known_cpu = gameplay_total + aoe2_total + bridge_total +
        transform.cpu_ms + aoe2.render_prepare_ms + aoe2.render_submit_ms +
        render.present_ms;
    const double residual = std::max(0.0, frame_ms - known_cpu);
    const auto* gpu_motion_profile = world.try_resource<AoeGpuMotionDiagnostics>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const double gpu_upload_total = gpu_motion_profile ? gpu_motion_profile->last_tick_ms : 0.0;
    const double gpu_upload_ms_v = gpu_motion_profile ? gpu_motion_profile->upload_ms : 0.0;
    const double gpu_dispatch_ms_v = gpu_motion_profile ? gpu_motion_profile->dispatch_ms : 0.0;
    const double gpu_readback_ms_v = gpu_motion_profile ? gpu_motion_profile->readback_ms : 0.0;
#else
    const double gpu_upload_total = 0.0, gpu_upload_ms_v = 0.0,
                 gpu_dispatch_ms_v = 0.0, gpu_readback_ms_v = 0.0;
    (void)gpu_motion_profile;
#endif
    // Time::raw_dt is measured at the beginning of this tick, so it describes
    // the preceding start-to-start interval. Use it for wall-clock capture
    // length, while frame_ms and all named system timings describe this row's
    // current frame.
    profile->capture_seconds += std::max(0.f, time.raw_dt);
    profile->csv
        << time.frame << ',' << profile->capture_seconds << ',' << frame_ms << ','
        << time.raw_dt * 1000.f << ',' << time.fps << ',' << preview.displayed_fps << ','
        << preview.displayed_frame_dt_ms << ',' << preview.displayed_frame_p95_ms << ','
        << preview.displayed_frame_max_ms << ',' << clock.ticks_this_frame << ','
        << clock.dropped_seconds << ',' << gameplay_units << ',' << render_units << ','
        << SquadGameplayDef::SquadEngagementPlugin::name << ','
        << SquadGameplayDef::FormationPlugin::name << ','
        << SquadGameplayDef::SquadArrivalRematchPlugin::name << ','
        << SquadGameplayDef::LocalAvoidancePlugin::name << ','
        << SquadGameplayDef::GlobalMotionPlugin::name << ','
        << projectiles << ',' << gameplay.clear_events_ms << ',' << gameplay.spawn_ms << ','
        << gameplay.fixed_total_ms << ',' << gameplay.recycle_ms << ','
        << gameplay.position_history_ms << ',' << gameplay.squad_spawn_resolution_ms << ','
        << gameplay.static_obstacle_index_ms << ',' << gameplay.dynamic_obstacle_index_ms << ','
        << gameplay.squad_command_ms << ',' << gameplay.command_ms << ','
        << gameplay.membership_cleanup_ms << ',' << gameplay.squad_traffic_ms << ','
        << gameplay.squad_engagement_ms << ',' << gameplay.squad_control_ms << ','
        << gameplay.formation_ms << ',' << gameplay.squad_arrival_rematch_ms << ','
        << gameplay.attack_move_acquisition_ms << ',' << gameplay.navigation_ms << ','
        << gameplay.movement_intent_ms << ',' << gameplay.local_avoidance_ms << ','
        << gameplay.unit_flow_ms << ',' << gameplay.motion_safety_ms << ','
        << gameplay.movement_ms << ',' << gameplay.combat_ms << ','
        << gameplay.projectile_ms << ',' << gameplay.lifecycle_ms << ','
        << gameplay.movement_speed_samples << ',' << base_average << ','
        << effective_average << ',' << desired_average << ','
        << steering_average << ',' << actual_average << ','
        << (base_average > 0.0 ? actual_average / base_average : 0.0) << ','
        << (effective_average > 0.0 ? actual_average / effective_average : 0.0) << ','
        << gameplay.movement_squad_limited << ','
        << gameplay.movement_arrive_limited << ','
        << gameplay.movement_turn_limited << ','
        << gameplay.movement_steering_limited << ','
        << gameplay.movement_safe_limited << ','
        << aoe2.spawn_ms << ',' << aoe2.animation_ms << ',' << aoe2.batch_total_ms << ','
        << aoe2.membership_sync_ms << ',' << aoe2.instance_update_ms << ','
        << bridge.orphan_cleanup_ms << ',' << bridge.unit_presentation_ms << ','
        << bridge.projectile_presentation_ms << ',' << transform.cpu_ms << ','
        << aoe2.render_prepare_ms << ',' << aoe2.render_upload_ms << ','
        << aoe2.render_submit_ms << ',' << aoe2.render_gpu_ms << ',' << render.present_ms << ','
        << aoe2.animation_frame_changes << ',' << aoe2.frame_dirty_instances << ','
        << aoe2.transform_dirty_instances << ',' << aoe2.unchanged_instances << ','
        << transform.changed << ','
        << known_cpu << ',' << residual << ','
        << gameplay.navigation_astar_calls << ','
        << gameplay.navigation_astar_cells_expanded << ','
        << gameplay.navigation_clear_segment_calls << ','
        << gameplay.navigation_repath_units << ','
        << gameplay.navigation_astar_find_ms << ','
        << gameplay.navigation_astar_find_peak_ms << ','
        << gpu_upload_total << ',' << gpu_upload_ms_v << ','
        << gpu_dispatch_ms_v << ',' << gpu_readback_ms_v << '\n';
    // Closing the window is the marker for an interactive profile: flush in
    // Stage::Last while the ECS resources still exist, before run_app shuts
    // the application down.
    if (profile->capture_seconds >= profile->capture_target_seconds ||
        world.resource<Window>().should_close)
        write_system_profile(*profile, world);
}
#endif
} // namespace

int main() {
    const fs::path root = wws::find_path(3, "res", true);
    if (root.empty()) {
        std::fprintf(stderr, "[aoe_squad_preview] cannot locate res directory\n");
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{
        1440, 900, "AoE Gameplay Squad Preview", false});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(Aoe2Plugin{"aoe2de_cache"});
    app.add_plugin(SquadGameplayDef{"aoe_units"});
    if constexpr (
        SquadGameplayDef::GlobalMotionPlugin::uses_runtime_planner)
        app.add_plugin(AoeGpuMotionPlugin{});
    app.add_plugin(Aoe2GameplayBridgePlugin{});
    app.add_plugin(GizmoPlugin);
    app.add_plugin(RenderPlugin);
    auto& camera_control =
        app.world.resource_or_add<OrthographicCameraControlConfig>();
    camera_control.pan_speed = 1.f;
    camera_control.zoom_speed = .15f;
    camera_control.minimum_zoom = .25f;
    camera_control.maximum_zoom = 8.f;
    PreviewState initial_preview;
    if (const char* value = std::getenv("GLD_AOE_STRESS_PRESET")) {
        const long preset = std::strtol(value, nullptr, 10);
        if (std::ranges::any_of(StressPresets,
                [&](const StressPreset& candidate) {
                    return candidate.key == preset;
                }))
            initial_preview.stress_preset = static_cast<int>(preset);
    }
    app.world.add_resource<PreviewState>(std::move(initial_preview));
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    SystemProfileState profile;
    if (const char* value = std::getenv("GLD_AOE_SYSTEM_PROFILE")) {
        profile.enabled = true;
        profile.output = *value && std::string_view(value) != "1"
            ? fs::path(value)
            : fs::path("build/profile/aoe_gameplay_squad_systems.csv");
        if (const char* seconds = std::getenv("GLD_AOE_SYSTEM_PROFILE_SECONDS")) {
            const double requested = std::strtod(seconds, nullptr);
            if (std::isfinite(requested) && requested > 0.0)
                profile.capture_target_seconds = requested;
        }
    }
    app.world.add_resource<SystemProfileState>(std::move(profile));
    MotionDecisionTraceState motion_trace;
    if (const char* value = std::getenv("GLD_AOE_MOTION_TRACE")) {
        if (*value && std::string_view(value) != "1")
            motion_trace.directory = fs::path(value);
        if (!begin_motion_trace(motion_trace))
            std::fprintf(stderr,
                "[aoe_squad_preview] failed to start motion trace in %s\n",
                motion_trace.directory.string().c_str());
    }
    app.world.add_resource<MotionDecisionTraceState>(std::move(motion_trace));
#endif

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        const auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = UnitLayer;
        camera.clear_color = {.12f, .14f, .17f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        world.reg().emplace<OrthographicCameraControl>(camera_entity);
        world.resource<PreviewState>().world_camera = camera_entity;
        auto& passes = emplace_registered_render_passes(world, camera_entity);
        auto& pass = passes.add(Aoe2UnitPassId);
        pass.state.depth_test = RenderStateValue::Enabled;
        pass.state.depth_write = RenderStateValue::Enabled;
        pass.state.blend = RenderStateValue::Enabled;
        auto& gizmo_pass = passes.add(GizmoPassId);
        gizmo_pass.state.depth_test = RenderStateValue::Disabled;
        gizmo_pass.state.depth_write = RenderStateValue::Disabled;
        gizmo_pass.state.blend = RenderStateValue::Enabled;
        gizmo_pass.state.blend_src = BlendFactor::SrcAlpha;
        gizmo_pass.state.blend_dst = BlendFactor::OneMinusSrcAlpha;

        const auto hud_camera = world.spawn();
        Camera overlay;
        overlay.kind = CameraKind::Ortho;
        overlay.priority = 20;
        overlay.layers = HudLayer;
        overlay.do_clear = false;
        world.reg().emplace<Camera>(hud_camera, overlay);
        emplace_render_passes<BatchPass>(world, hud_camera);

        auto& preview = world.resource<PreviewState>();
        auto map_definition = make_preview_map();
        preview.map_obstacles = map_definition.static_obstacles;
        world.add_resource<AoeLogicMap>(map_definition);
        preview.hud = world.spawn();
        Text hud;
        hud.text = U"Loading squad preview...";
        hud.font = world.resource<AssetServer>().load(
            FontDesc("fonts/AGENCYB.TTF", 0));
        hud.size = 15;
        hud.color = {.92f, .96f, 1.f, 1.f};
        hud.align = TextAlign::Left;
        hud.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(preview.hud, std::move(hud));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(preview.hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(preview.hud, RenderLayer{HudLayer});
        reset_scene(world);
        std::printf(
            "Controls: 1-6 stress preset "
            "(128/512/2000/5000/10000/20000 total), "
            "Space mutual AttackMove, S stop, "
            "G map, N navigation, C collision, P feet, "
            "L trace, R reset, F5 rescan, "
            "Middle-drag pan, Wheel zoom, "
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            "T motion decision log, "
#endif
            "Escape quit\n");
    });
    app.add_system(Stage::Update, input_system);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    // AoeGameplayPlugin registered its fixed-step driver earlier in
    // PreUpdate, so this samples the final authoritative decision after all
    // fixed ticks completed for the rendered frame.
    app.add_system(Stage::PreUpdate, motion_decision_trace_system);
#endif
    app.add_system(Stage::Update, fit_preview_camera_system);
    app.add_plugin(OrthographicCameraControlPlugin);
    app.add_system(Stage::Update, projection_system);
    app.add_system(Stage::PostUpdate, submit_map_navigation_gizmos);
    app.add_system(Stage::PostUpdate, submit_unit_collider_gizmos);
    app.add_system(Stage::PostUpdate, submit_unit_foot_gizmos);
    app.add_system(Stage::Last, diagnostics_system);
    app.add_system(Stage::Last, trace_blue_archer_foot);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    app.add_system(Stage::First, system_profile_begin);
    app.add_system(Stage::Last, system_profile_end);
#endif
    run_app(app);
    return 0;
}
