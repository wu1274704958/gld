#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe/AoeGameplay.hpp>
#include <ecs/App.hpp>
#include <ecs/Input.hpp>
#include <ecs/Window.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Gizmo.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;

namespace {

constexpr std::uint32_t PreviewLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;
constexpr float Scale = 7.f;
constexpr glm::vec2 MapOrigin{-96.f, -56.f};
constexpr std::uint32_t MapWidth = 192;
constexpr std::uint32_t MapHeight = 112;
constexpr glm::vec2 Spawn{-52.f, 0.f};
constexpr glm::vec2 DefaultDestination{52.f, 0.f};
constexpr float Epsilon = 1e-5f;

struct StressPreset {
    int key = 1;
    std::uint32_t camels = 24;
    std::uint32_t archers = 40;

    std::uint32_t count() const { return camels + archers; }
};

// These are the per-Squad counts from aoe_gameplay_squad_preview. Preset 4
// therefore reproduces one of the two 2,500-member Squads from that preview.
constexpr std::array StressPresets{
    StressPreset{1, 24, 40},
    StressPreset{2, 96, 160},
    StressPreset{3, 375, 625},
    StressPreset{4, 938, 1562},
    StressPreset{5, 1875, 3125},
    StressPreset{6, 3750, 6250},
};

struct PreviewPlanningMovingControlModule {
    using role = AoeFormationMovingControlRole;
    static constexpr std::string_view name = "preview_no_movement";
    static void install(App&) {}
    static AoeFormationModuleResult run(
        EcsWorld&, AoeFormationSquadContext&) {
        return AoeFormationModuleResult::Continue;
    }
};

struct PreviewPlanningMovementIntentPlugin {
    using phase = AoeUnitMovementIntentPhase;
    static constexpr std::string_view name = "preview_no_movement";
    static void install(App&) {}
    static void fixed_tick(EcsWorld&, std::uint64_t) {}
};

struct PreviewPlanningLocalAvoidancePlugin {
    using phase = AoeLocalAvoidancePhase;
    static constexpr std::string_view name = "preview_no_movement";
    static void install(App&) {}
    static void fixed_tick(EcsWorld&, std::uint64_t) {}
};

struct PreviewPlanningGlobalMotionPlugin {
    using phase = AoeGlobalMotionPhase;
    static constexpr std::string_view name = "preview_no_movement";
    static constexpr bool uses_runtime_planner = false;
    static void install(App&) {}
    static void fixed_tick(EcsWorld&, std::uint64_t) {}
};

using PlanningFormationPlugin = AoeFormationPlugin<
    AoeFullSquadLayoutModule,
    AoeNavMeshRouteSplitModule,
    PreviewPlanningMovingControlModule,
    AoePassThroughAttackControlModule,
    AoePassThroughCommandCompletionModule>;

using PlanningGameplayPlugin = AoeGameplayDef<
    AoePassThroughSquadEngagementPlugin,
    PlanningFormationPlugin,
    AoePassThroughSquadArrivalRematchPlugin,
    PreviewPlanningMovementIntentPlugin,
    PreviewPlanningLocalAvoidancePlugin,
    PreviewPlanningGlobalMotionPlugin,
    AoeGridAStarUnitPathfinderPlugin,
    AoeNavMeshSquadPathfinderPlugin>;

struct PreviewState {
    entt::entity squad{entt::null};
    entt::entity hud{entt::null};
    int preset = 4;
    glm::vec2 destination = DefaultDestination;
    float sample_progress = 0.f;
    bool command_sent = false;
    bool draw_nav_mesh = true;
    bool draw_member_routes = true;
    bool draw_width_schedule = true;
    bool draw_actions = true;
    bool draw_units = true;
    bool smoke_mode = false;
    std::uint32_t smoke_frames = 0;
};

const StressPreset& active_preset(const PreviewState& state) {
    const auto found = std::find_if(StressPresets.begin(), StressPresets.end(),
        [&](const auto& preset) { return preset.key == state.preset; });
    return found == StressPresets.end() ? StressPresets.front() : *found;
}

AoeMapDefinition make_map() {
    AoeMapDefinition result;
    result.id = "aoe_formation_route_preview";
    result.origin = MapOrigin;
    result.tile_size = 1.f;
    result.width = MapWidth;
    result.height = MapHeight;
    result.heights.assign(
        static_cast<std::size_t>(result.width + 1) * (result.height + 1), 0.f);

    AoeStaticObstacleDesc gate;
    gate.source_id = "left_gate";
    gate.shape = AoeStaticObstacleShape::Aabb;
    gate.center = {-16.f, 0.f};
    gate.half_extents = {1.5f, 12.f};
    result.static_obstacles.push_back(gate);
    gate.source_id = "right_gate";
    gate.center = {16.f, 0.f};
    result.static_obstacles.push_back(gate);

    AoeStaticObstacleDesc rock;
    rock.source_id = "south_rock";
    rock.shape = AoeStaticObstacleShape::Circle;
    rock.center = {0.f, -28.f};
    rock.radius = 5.f;
    result.static_obstacles.push_back(rock);
    return result;
}

glm::vec3 project(glm::vec2 point, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(),
                         map.height() * map.tile_size()};
    return {(point.x - map.origin().x - size.x * .5f) * Scale,
            (point.y - map.origin().y - size.y * .5f) * Scale, 0.f};
}

glm::vec2 unproject(glm::vec2 cursor, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(),
                         map.height() * map.tile_size()};
    return map.origin() + glm::vec2{
        size.x * .5f + cursor.x / Scale,
        size.y * .5f - cursor.y / Scale};
}

glm::vec4 chain_color(std::size_t index, float alpha = .82f) {
    static constexpr glm::vec4 colors[] = {
        {.15f, .85f, 1.f, 1.f}, {.3f, 1.f, .45f, 1.f},
        {.85f, .45f, 1.f, 1.f}, {1.f, .45f, .55f, 1.f},
        {.25f, .65f, 1.f, 1.f}, {.65f, 1.f, .2f, 1.f},
        {1.f, .35f, .85f, 1.f}, {.2f, 1.f, .8f, 1.f},
    };
    auto result = colors[index % std::size(colors)];
    result.a = alpha;
    return result;
}

const char* debug_stage_name(AoeFormationRouteDebugStage stage) {
    switch (stage) {
    case AoeFormationRouteDebugStage::None: return "none";
    case AoeFormationRouteDebugStage::WaitingForCorridor: return "waiting-corridor";
    case AoeFormationRouteDebugStage::ValidateInput: return "validate-input";
    case AoeFormationRouteDebugStage::SelectLayout: return "select-layout";
    case AoeFormationRouteDebugStage::ValidateTurn: return "validate-turn";
    case AoeFormationRouteDebugStage::BuildFollowTopology: return "follow-topology";
    case AoeFormationRouteDebugStage::BuildCenterPath: return "center-path";
    case AoeFormationRouteDebugStage::BuildPoseCurve: return "pose-curve";
    case AoeFormationRouteDebugStage::BuildWidthConstraints: return "width-constraints";
    case AoeFormationRouteDebugStage::BuildWidthSchedule: return "width-schedule";
    case AoeFormationRouteDebugStage::RepairTransitionWindows: return "repair-windows";
    case AoeFormationRouteDebugStage::BuildMemberRoutes: return "member-routes";
    case AoeFormationRouteDebugStage::Commit: return "commit";
    case AoeFormationRouteDebugStage::Completed: return "completed";
    }
    return "unknown";
}

void destroy_squad(EcsWorld& world, entt::entity squad) {
    auto& reg = world.reg();
    if (!reg.valid(squad)) return;
    std::vector<entt::entity> units;
    if (const auto* members = reg.try_get<AoeSquadMembers>(squad)) {
        units.reserve(members->active.size() + members->pending.size());
        for (const auto& member : members->active)
            units.push_back(member.entity);
        for (const auto& member : members->pending)
            units.push_back(member.entity);
    }
    disband_aoe_gameplay_squad(world, squad);
    for (const auto unit : units)
        if (reg.valid(unit)) reg.destroy(unit);
}

void reset_scene(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    destroy_squad(world, state.squad);
    const auto& preset = active_preset(state);
    AoeSquadSpawnOptions options;
    options.composition = {
        {"camel_scout", preset.camels, 1},
        {"archer", preset.archers, 1},
    };
    options.center = Spawn;
    options.forward = {1.f, 0.f};
    options.team_id = 1;
    options.player_color = 1;
    options.layers = PreviewLayer;
    options.formation_spacing = .2f;
    options.acquisition_radius = 0.f;
    options.disengage_radius = 0.f;
    state.squad = spawn_aoe_gameplay_squad(world, options);
    state.destination = DefaultDestination;
    state.sample_progress = 0.f;
    state.command_sent = false;
}

bool squad_ready(const entt::registry& reg, entt::entity squad) {
    const auto* spawn = reg.valid(squad)
        ? reg.try_get<AoeSquadSpawnState>(squad) : nullptr;
    return spawn && (spawn->status == AoeSquadSpawnStatus::Ready ||
                     spawn->status == AoeSquadSpawnStatus::Partial);
}

void issue_plan(EcsWorld& world, glm::vec2 destination) {
    auto& state = world.resource<PreviewState>();
    if (!squad_ready(world.reg(), state.squad)) return;
    state.destination = destination;
    state.sample_progress = 0.f;
    state.command_sent = request_aoe_squad_move(
        world, state.squad, destination);
}

float travel_progress(const EcsWorld& world, const PreviewState& state) {
    if (!world.reg().valid(state.squad)) return 0.f;
    if (const auto* plan = world.reg().try_get<AoeFormationRoutePlan>(
            state.squad); plan && plan->valid)
        return plan->travel_progress;
    if (const auto* debug = world.reg().try_get<
            AoeFormationRouteDebugSnapshot>(state.squad);
        debug && !debug->poses.empty())
        return debug->poses.back().distance;
    return 0.f;
}

void input_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    auto& keyboard = world.resource<Keyboard>();
    if (!state.command_sent && squad_ready(world.reg(), state.squad))
        issue_plan(world, state.destination);

    if (keyboard.just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    if (keyboard.just_now_pressed(GLFW_KEY_R))
        issue_plan(world, DefaultDestination);
    if (keyboard.just_now_pressed(GLFW_KEY_N))
        state.draw_member_routes = !state.draw_member_routes;
    if (keyboard.just_now_pressed(GLFW_KEY_M))
        state.draw_nav_mesh = !state.draw_nav_mesh;
    if (keyboard.just_now_pressed(GLFW_KEY_W))
        state.draw_width_schedule = !state.draw_width_schedule;
    if (keyboard.just_now_pressed(GLFW_KEY_A))
        state.draw_actions = !state.draw_actions;
    if (keyboard.just_now_pressed(GLFW_KEY_U))
        state.draw_units = !state.draw_units;

    for (const auto& preset : StressPresets) {
        if (!keyboard.just_now_pressed(GLFW_KEY_0 + preset.key)) continue;
        state.preset = preset.key;
        reset_scene(world);
        break;
    }

    const float maximum = travel_progress(world, state);
    const float step = keyboard.is_pressed(GLFW_KEY_LEFT_SHIFT) ? 5.f : 1.f;
    if (keyboard.just_now_pressed(GLFW_KEY_LEFT))
        state.sample_progress = std::max(0.f, state.sample_progress - step);
    if (keyboard.just_now_pressed(GLFW_KEY_RIGHT))
        state.sample_progress = std::min(maximum, state.sample_progress + step);
    if (keyboard.just_now_pressed(GLFW_KEY_HOME)) state.sample_progress = 0.f;
    if (keyboard.just_now_pressed(GLFW_KEY_END)) state.sample_progress = maximum;

    const auto& mouse = world.resource<MouseButtons>();
    if (mouse.just_now_pressed(GLFW_MOUSE_BUTTON_LEFT)) {
        const auto& window = world.resource<Window>();
        const glm::vec2 cursor = world.resource<CursorPosition>().position -
            glm::vec2{window.width * .5f, window.height * .5f};
        const auto& map = world.resource<AoeLogicMap>();
        const glm::vec2 destination = unproject(cursor, map);
        const glm::vec2 low = map.origin();
        const glm::vec2 high = low + glm::vec2{
            map.width() * map.tile_size(), map.height() * map.tile_size()};
        issue_plan(world, glm::clamp(destination, low, high));
    }
}

AoeFormationRoutePose sample_pose(
    const std::vector<AoeFormationRoutePose>& poses, float progress) {
    if (poses.empty()) return {};
    if (progress <= poses.front().distance) return poses.front();
    if (progress >= poses.back().distance) return poses.back();
    const auto upper = std::lower_bound(poses.begin(), poses.end(), progress,
        [](const auto& pose, float value) { return pose.distance < value; });
    if (upper == poses.begin()) return *upper;
    const auto& from = *(upper - 1);
    const auto& to = *upper;
    const float span = to.distance - from.distance;
    const float alpha = span > Epsilon
        ? (progress - from.distance) / span : 1.f;
    glm::vec2 forward = glm::mix(from.forward, to.forward, alpha);
    if (glm::length(forward) > Epsilon) forward = glm::normalize(forward);
    else forward = from.forward;
    return {progress, glm::mix(from.center, to.center, alpha), forward};
}

glm::vec2 sample_member_progress(
    const AoeFormationMemberRouteProgress& metadata,
    const AoeNavigationPath& path, float progress) {
    if (path.waypoints.empty() || metadata.waypoint_progress.size() !=
            path.waypoints.size())
        return metadata.origin;
    if (progress <= metadata.origin_progress) return metadata.origin;
    for (std::size_t index = 0; index < path.waypoints.size(); ++index) {
        const float from_progress = index
            ? metadata.waypoint_progress[index - 1] : metadata.origin_progress;
        const float to_progress = metadata.waypoint_progress[index];
        if (progress > to_progress && index + 1 < path.waypoints.size())
            continue;
        const glm::vec2 from = index
            ? path.waypoints[index - 1] : metadata.origin;
        const float span = to_progress - from_progress;
        const float alpha = span > Epsilon
            ? std::clamp((progress - from_progress) / span, 0.f, 1.f) : 1.f;
        return glm::mix(from, path.waypoints[index], alpha);
    }
    return path.waypoints.back();
}

void draw_map(Gizmos& gizmos, const AoeLogicMap& map) {
    const glm::vec2 low = map.origin();
    const glm::vec2 high = low + glm::vec2{
        map.width() * map.tile_size(), map.height() * map.tile_size()};
    const glm::vec2 corners[] = {
        {low.x, low.y}, {high.x, low.y},
        {high.x, high.y}, {low.x, high.y},
    };
    for (int index = 0; index < 4; ++index)
        gizmos.line(project(corners[index], map),
            project(corners[(index + 1) % 4], map),
            {.45f, .5f, .58f, 1.f}, PreviewLayer);
    map.visit_static_obstacles(
        [&](AoeObstacleId, const AoeStaticObstacleDesc& obstacle) {
            const glm::vec4 color{1.f, .3f, .12f, 1.f};
            if (obstacle.shape == AoeStaticObstacleShape::Aabb)
                gizmos.wire_box(project(obstacle.center, map),
                    {obstacle.half_extents.x * Scale,
                     obstacle.half_extents.y * Scale, 0.f},
                    glm::mat3{1.f}, color, PreviewLayer);
            else
                gizmos.wire_ellipse(project(obstacle.center, map),
                    {obstacle.radius * Scale, 0.f, 0.f},
                    {0.f, obstacle.radius * Scale, 0.f},
                    32, color, PreviewLayer);
        });
}

void draw_path(Gizmos& gizmos, const AoeLogicMap& map, glm::vec2 origin,
               const std::vector<glm::vec2>& waypoints, glm::vec4 color) {
    glm::vec2 previous = origin;
    for (const auto waypoint : waypoints) {
        gizmos.line(project(previous, map), project(waypoint, map),
                    color, PreviewLayer);
        previous = waypoint;
    }
}

void draw_progress_interval(Gizmos& gizmos, const AoeLogicMap& map,
                            const std::vector<AoeFormationRoutePose>& poses,
                            float begin, float end, glm::vec4 color) {
    if (poses.size() < 2 || end < begin) return;
    begin = std::max(begin, poses.front().distance);
    end = std::min(end, poses.back().distance);
    if (end < begin) return;
    glm::vec2 previous = sample_pose(poses, begin).center;
    for (const auto& pose : poses) {
        if (pose.distance <= begin || pose.distance >= end) continue;
        gizmos.line(project(previous, map), project(pose.center, map),
                    color, PreviewLayer);
        previous = pose.center;
    }
    gizmos.line(project(previous, map),
                project(sample_pose(poses, end).center, map),
                color, PreviewLayer);
}

void draw_route_debug(EcsWorld& world) {
    auto& gizmos = world.resource<Gizmos>();
    gizmos.clear();
    const auto& map = world.resource<AoeLogicMap>();
    draw_map(gizmos, map);
    auto& state = world.resource<PreviewState>();
    auto& reg = world.reg();
    if (!reg.valid(state.squad)) return;

    const auto* debug = reg.try_get<AoeFormationRouteDebugSnapshot>(state.squad);
    if (state.draw_nav_mesh) {
        if (const auto* nav_mesh = world.try_resource<AoeNavMeshResource>();
            nav_mesh && nav_mesh->queryable()) {
            for (const auto& polygon : nav_mesh->debug_polygons()) {
                const bool corridor = debug && std::find(
                    debug->corridor.polygons.begin(),
                    debug->corridor.polygons.end(), polygon.id) !=
                    debug->corridor.polygons.end();
                const glm::vec4 color = corridor
                    ? glm::vec4{.1f, 1.f, .65f, .78f}
                    : glm::vec4{.15f, .72f, .82f, .18f};
                for (std::size_t index = 0;
                     index < polygon.vertices.size(); ++index)
                    gizmos.line(project(polygon.vertices[index], map),
                        project(polygon.vertices[
                            (index + 1) % polygon.vertices.size()], map),
                        color, PreviewLayer);
            }
        }
        if (debug) {
            for (const auto& portal : debug->corridor.portals) {
                gizmos.line(project(portal.left, map), project(portal.right, map),
                            {1.f, .82f, .12f, .95f}, PreviewLayer);
                gizmos.cross(project((portal.left + portal.right) * .5f, map),
                             2.2f, {1.f, .95f, .25f, 1.f}, PreviewLayer);
            }
        }
    }

    if (const auto* layout = reg.try_get<AoeSquadLayoutState>(state.squad);
        layout && layout->valid) {
        for (const auto& slot : layout->layout.slots) {
            if (!reg.valid(slot.unit.entity) ||
                !reg.all_of<AoePosition>(slot.unit.entity))
                continue;
            const glm::vec2 position = reg.get<AoePosition>(slot.unit.entity).value;
            if (state.draw_units)
                gizmos.cross(project(position, map),
                    slot.chain_order == 0 ? 2.4f : .7f,
                    chain_color(slot.chain_index,
                        slot.chain_order == 0 ? .9f : .22f), PreviewLayer);
        }
    }

    gizmos.cross(project(Spawn, map), 6.f,
                 {.2f, 1.f, .35f, 1.f}, PreviewLayer);
    gizmos.cross(project(state.destination, map), 7.f,
                 {1.f, .15f, .2f, 1.f}, PreviewLayer);
    if (!debug) return;

    if (debug->center_path.size() > 1)
        draw_path(gizmos, map, debug->center_path.front(),
            debug->center_path,
            {1.f, .92f, .2f, .72f});
    if (debug->poses.size() > 1) {
        for (std::size_t index = 1; index < debug->poses.size(); ++index)
            gizmos.line(project(debug->poses[index - 1].center, map),
                project(debug->poses[index].center, map),
                {.1f, .85f, 1.f, .9f}, PreviewLayer);
    }

    if (state.draw_width_schedule && !debug->poses.empty()) {
        for (const auto& constraint : debug->width_constraints)
            draw_progress_interval(gizmos, map, debug->poses,
                constraint.begin_progress, constraint.end_progress,
                {.25f, .45f, 1.f, .95f});
        for (std::size_t index = 0;
             index < debug->width_schedule.transitions.size(); ++index) {
            const auto& transition = debug->width_schedule.transitions[index];
            draw_progress_interval(gizmos, map, debug->poses,
                transition.begin_progress, transition.end_progress,
                index % 2 == 0
                    ? glm::vec4{1.f, .5f, .05f, 1.f}
                    : glm::vec4{.2f, 1.f, .35f, 1.f});
        }
    }

    if (state.draw_member_routes) {
        for (const auto& route : debug->member_routes)
            draw_path(gizmos, map, route.origin, route.waypoints,
                route.accepted
                    ? chain_color(route.natural_chain, .62f)
                    : glm::vec4{1.f, .05f, .05f, 1.f});
    }

    if (debug->width_schedule.valid && !debug->poses.empty()) {
        const float progress = std::clamp(state.sample_progress, 0.f,
            debug->poses.back().distance);
        for (std::size_t index = 0;
             index < debug->width_schedule.layouts.front().slots.size();
             ++index) {
            const auto slot = sample_formation_width_schedule(
                debug->width_schedule, index, progress);
            const auto pose = sample_pose(debug->poses,
                progress + slot.local_offset.y);
            const glm::vec2 right{pose.forward.y, -pose.forward.x};
            const glm::vec2 point = pose.center + right * slot.local_offset.x;
            gizmos.cross(project(point, map), 3.4f,
                chain_color(index, 1.f), PreviewLayer);
            gizmos.line(project(point, map),
                project(point + pose.forward * .8f, map),
                {1.f, 1.f, 1.f, .8f}, PreviewLayer);
        }
    }

    if (state.draw_actions) {
        const auto* plan = reg.try_get<AoeFormationRoutePlan>(state.squad);
        const auto* follow = reg.try_get<AoeFormationFollowPlan>(state.squad);
        if (plan && follow) {
            for (const auto& natural_chain : follow->chains) {
                const auto leader = natural_chain.members.front().unit.entity;
                const auto* actions = reg.try_get<AoeUnitActionChain>(leader);
                const auto* path = reg.try_get<AoeNavigationPath>(leader);
                const auto* progress = reg.try_get<
                    AoeFormationMemberRouteProgress>(leader);
                if (!actions || !path || !progress) continue;
                for (const auto& step : actions->steps) {
                    if (step.kind == AoeUnitActionStepKind::NavigationPath)
                        continue;
                    const glm::vec2 point = sample_member_progress(
                        *progress, *path, step.begin_progress);
                    gizmos.cross(project(point, map), 4.8f,
                        step.kind == AoeUnitActionStepKind::FormationFollow
                            ? glm::vec4{1.f, .48f, .05f, 1.f}
                            : glm::vec4{.15f, 1.f, .25f, 1.f},
                        PreviewLayer);
                }
            }
        }
    }
}

std::u32string ascii_to_u32(const std::string& value) {
    return {value.begin(), value.end()};
}

void update_hud(EcsWorld& world) {
    const auto& state = world.resource<PreviewState>();
    if (!world.reg().valid(state.hud)) return;
    const auto& preset = active_preset(state);
    const auto* spawn = world.reg().valid(state.squad)
        ? world.reg().try_get<AoeSquadSpawnState>(state.squad) : nullptr;
    const auto* split = world.reg().valid(state.squad)
        ? world.reg().try_get<AoeFormationRouteSplitState>(state.squad) : nullptr;
    const auto* debug = world.reg().valid(state.squad)
        ? world.reg().try_get<AoeFormationRouteDebugSnapshot>(state.squad)
        : nullptr;
    std::size_t accepted = 0;
    std::size_t failed = 0;
    if (debug) {
        for (const auto& route : debug->member_routes)
            route.accepted ? ++accepted : ++failed;
    }
    const char* split_status = "waiting";
    if (split) {
        if (split->status == AoeFormationRouteSplitStatus::Ready)
            split_status = "ready";
        else if (split->status == AoeFormationRouteSplitStatus::Failed)
            split_status = "failed";
    }
    char text[1024];
    std::snprintf(text, sizeof(text),
        "AoE Formation Route Preview | preset %d | units %u | spawn %u/%u | split %s | stage %s\n"
        "width natural %.2f | bottleneck %.2f | selected %.2f | portals %zu | constraints %zu | stages %zu\n"
        "leader routes accepted %zu failed %zu | failed-chain %s | sample %.1f / %.1f\n"
        "1-6 preset | Left click destination | R reset route | Left/Right scrub (Shift=5) | Home/End\n"
        "N routes %s | M navmesh %s | W width %s | A actions %s | U units %s | Esc exit",
        state.preset, preset.count(), spawn ? spawn->succeeded : 0u,
        spawn ? spawn->requested : preset.count(), split_status,
        debug ? debug_stage_name(debug->stage) : "waiting",
        debug ? debug->natural_width : 0.f,
        debug ? debug->bottleneck_width : 0.f,
        debug ? debug->selected_width : 0.f,
        debug ? debug->corridor.portals.size() : 0u,
        debug ? debug->width_constraints.size() : 0u,
        debug ? debug->width_schedule.stages.size() : 0u,
        accepted, failed,
        debug && debug->failed_chain != static_cast<std::size_t>(-1)
            ? std::to_string(debug->failed_chain).c_str() : "none",
        state.sample_progress, travel_progress(world, state),
        state.draw_member_routes ? "on" : "off",
        state.draw_nav_mesh ? "on" : "off",
        state.draw_width_schedule ? "on" : "off",
        state.draw_actions ? "on" : "off",
        state.draw_units ? "on" : "off");
    auto& hud = world.reg().get<Text>(state.hud);
    const auto value = ascii_to_u32(text);
    if (hud.text == value) return;
    hud.text = value;
    ++hud.rev;
}

void smoke_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    if (!state.smoke_mode) return;
    ++state.smoke_frames;
    const auto* split = world.reg().valid(state.squad)
        ? world.reg().try_get<AoeFormationRouteSplitState>(state.squad)
        : nullptr;
    const auto* debug = world.reg().valid(state.squad)
        ? world.reg().try_get<AoeFormationRouteDebugSnapshot>(state.squad)
        : nullptr;
    const bool terminal = split && debug &&
        split->status != AoeFormationRouteSplitStatus::None &&
        debug->stage != AoeFormationRouteDebugStage::WaitingForCorridor;
    if (!terminal && state.smoke_frames < 1200) return;
    std::printf(
        "ROUTE_PREVIEW_RESULT status=%s stage=%s routes=%zu failed_chain=%zu\n",
        split && split->status == AoeFormationRouteSplitStatus::Ready
            ? "ready" : "failed",
        debug ? debug_stage_name(debug->stage) : "timeout",
        debug ? debug->member_routes.size() : 0u,
        debug ? debug->failed_chain : static_cast<std::size_t>(-1));
    world.resource<Window>().should_close = true;
}

} // namespace

int main() {
    const auto root = wws::find_path(3, "res", true);
    if (root.empty()) {
        std::fprintf(stderr,
            "[aoe_formation_route_preview] cannot locate res directory\n");
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{
        1440, 900, "AoE Formation Route Preview", false});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(PlanningGameplayPlugin{"aoe_units"});
    app.add_plugin(GizmoPlugin);
    app.add_plugin(RenderPlugin);
    PreviewState initial_state;
    initial_state.smoke_mode =
        std::getenv("GLD_AOE_ROUTE_PREVIEW_SMOKE") != nullptr;
    app.world.add_resource<PreviewState>(std::move(initial_state));
    app.world.add_resource<AoeFormationRouteDebugCapture>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        world.add_resource<AoeLogicMap>(make_map());

        const auto camera = world.spawn();
        Camera camera_value;
        camera_value.kind = CameraKind::Ortho;
        camera_value.layers = PreviewLayer;
        camera_value.clear_color = {.07f, .09f, .12f, 1.f};
        world.reg().emplace<Camera>(camera, camera_value);
        auto& passes = emplace_registered_render_passes(world, camera);
        auto& gizmo_pass = passes.add(GizmoPassId);
        gizmo_pass.state.depth_test = RenderStateValue::Disabled;
        gizmo_pass.state.depth_write = RenderStateValue::Disabled;
        gizmo_pass.state.blend = RenderStateValue::Enabled;
        gizmo_pass.state.blend_src = BlendFactor::SrcAlpha;
        gizmo_pass.state.blend_dst = BlendFactor::OneMinusSrcAlpha;

        const auto hud_camera = world.spawn();
        Camera hud_camera_value;
        hud_camera_value.kind = CameraKind::Ortho;
        hud_camera_value.priority = 20;
        hud_camera_value.layers = HudLayer;
        hud_camera_value.do_clear = false;
        world.reg().emplace<Camera>(hud_camera, hud_camera_value);
        emplace_render_passes<BatchPass>(world, hud_camera);

        const auto hud = world.spawn();
        Text text;
        text.text = U"Formation Route Preview: spawning...";
        text.font = world.resource<AssetServer>().load(
            FontDesc("fonts/AGENCYB.TTF", 0));
        text.size = 17;
        text.leading = 2.f;
        text.color = {.94f, .97f, 1.f, 1.f};
        text.align = TextAlign::Left;
        text.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(hud, std::move(text));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(hud, Transform::from_trs(
            {-window.width * .5f + 14.f,
              window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(hud, RenderLayer{HudLayer});
        world.resource<PreviewState>().hud = hud;

        reset_scene(world);
        std::puts(
            "Formation planning only: units never enter the movement pipeline.");
    });

    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::PostUpdate, draw_route_debug);
    app.add_system(Stage::Last, update_hud);
    app.add_system(Stage::Last, smoke_system);
    run_app(app);
    return 0;
}
