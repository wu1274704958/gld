#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe2x/Aoe2xRouteFormation.hpp>
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
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2x;

namespace {

constexpr std::uint32_t PreviewLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;
constexpr float Scale = 10.f;
constexpr std::uint32_t UnitCount = 16;

struct PreviewState {
    entt::entity squad{entt::null};
    entt::entity hud{entt::null};
    std::uint64_t tick = 0;
    bool initial_command_sent = false;
};

std::u32string ascii_to_u32(const std::string& value) {
    return {value.begin(), value.end()};
}

AoeMapDefinition make_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_route_formation_preview";
    map.tile_size = 1.f;
    map.width = 112;
    map.height = 72;
    map.heights.resize(113u * 73u, 0.f);

    // A long 1.2-unit corridor forces the 16-member formation to narrow to a
    // single column. Both ends remain open so profile transitions are visible.
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.center = {60.f, 17.7f};
    wall.half_extents = {22.f, 17.7f};
    map.static_obstacles.push_back(wall);
    wall.center.y = 54.3f;
    map.static_obstacles.push_back(wall);

    // Offset obstacles make arbitrary right-click destinations exercise the
    // center pathfinder instead of always producing a straight route.
    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.center = {27.f, 18.f};
    circle.radius = 4.f;
    map.static_obstacles.push_back(circle);
    circle.center = {91.f, 54.f};
    map.static_obstacles.push_back(circle);
    return map;
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

const char* order_status_name(FormationAttackMoveStatus status) {
    switch (status) {
    case FormationAttackMoveStatus::Idle: return "idle";
    case FormationAttackMoveStatus::Running: return "planning";
    case FormationAttackMoveStatus::Completed: return "routes distributed";
    case FormationAttackMoveStatus::Failed: return "failed";
    }
    return "unknown";
}

glm::vec4 member_color(std::size_t index) {
    static constexpr glm::vec4 colors[] = {
        {0.15f, 0.85f, 1.f, .82f}, {0.3f, 1.f, .45f, .82f},
        {0.85f, 0.45f, 1.f, .82f}, {1.f, 0.45f, .55f, .82f},
        {0.25f, .65f, 1.f, .82f}, {0.65f, 1.f, .2f, .82f},
        {1.f, .35f, .85f, .82f}, {0.2f, 1.f, .8f, .82f}};
    return colors[index % std::size(colors)];
}

void issue_attack_move(EcsWorld& world, glm::vec2 destination) {
    const auto squad = world.resource<PreviewState>().squad;
    if (!world.reg().valid(squad)) return;
    request_aoe2x_formation_attack_move(world, squad, destination);
}

void input_system(EcsWorld& world) {
    if (auto* keyboard = world.try_resource<Keyboard>(); keyboard) {
        if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
            world.resource<Window>().should_close = true;
        if (keyboard->just_now_pressed(GLFW_KEY_R))
            issue_attack_move(world, {96.f, 36.f});
    }
    const auto* mouse = world.try_resource<MouseButtons>();
    if (!mouse || !mouse->just_now_pressed(GLFW_MOUSE_BUTTON_RIGHT)) return;
    const auto& window = world.resource<Window>();
    const glm::vec2 cursor = world.resource<CursorPosition>().position -
        glm::vec2{window.width * .5f, window.height * .5f};
    issue_attack_move(world,
        unproject(cursor, world.resource<AoeLogicMap>()));
}

void simulation_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    run_aoe2x_gameplay_system<RouteSquadSpawnSystem>(world, state.tick);
    if (!state.initial_command_sent && world.reg().valid(state.squad) &&
        world.reg().get<FormationSpawnState>(state.squad).status ==
            FormationSpawnStatus::Ready) {
        state.initial_command_sent = true;
        issue_attack_move(world, {96.f, 36.f});
    }
    run_aoe2x_gameplay_system<RouteSquadCommandSystem>(world, state.tick);
    run_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world, state.tick);
    run_aoe2x_gameplay_system<RouteSquadSplitSystem>(world, state.tick);
    run_aoe2x_gameplay_system<RouteSquadCleanupSystem>(world, state.tick++);
}

void draw_path(Gizmos& gizmos, const AoeLogicMap& map, glm::vec2 start,
               const Aoe2xRoutePlan& route, glm::vec4 color,
               float waypoint_size) {
    glm::vec2 previous = start;
    for (const auto waypoint : route.waypoints) {
        gizmos.line(project(previous, map), project(waypoint, map),
                    color, PreviewLayer);
        gizmos.cross(project(waypoint, map), waypoint_size, color,
                     PreviewLayer);
        previous = waypoint;
    }
}

void draw_map(Gizmos& gizmos, const AoeLogicMap& map) {
    const glm::vec2 low = map.origin();
    const glm::vec2 high = low + glm::vec2{
        map.width() * map.tile_size(), map.height() * map.tile_size()};
    const glm::vec2 corners[] = {
        {low.x, low.y}, {high.x, low.y},
        {high.x, high.y}, {low.x, high.y}};
    for (int i = 0; i < 4; ++i)
        gizmos.line(project(corners[i], map),
                    project(corners[(i + 1) % 4], map),
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

void draw_gizmos(EcsWorld& world) {
    auto& gizmos = world.resource<Gizmos>();
    gizmos.clear();
    const auto& map = world.resource<AoeLogicMap>();
    draw_map(gizmos, map);

    const auto& state = world.resource<PreviewState>();
    if (!world.reg().valid(state.squad) ||
        !world.reg().all_of<SquadInfo>(state.squad))
        return;
    const auto& info = world.reg().get<SquadInfo>(state.squad);

    for (std::size_t index = 0; index < info.units.size(); ++index) {
        const auto unit = info.units[index];
        if (!world.reg().valid(unit) ||
            !world.reg().all_of<AoePosition, AoeCollider>(unit))
            continue;
        const glm::vec2 position = world.reg().get<AoePosition>(unit).value;
        const auto& collider = world.reg().get<AoeCollider>(unit);
        const glm::vec4 color = member_color(index);
        gizmos.wire_ellipse(project(position, map),
            {collider.radius_x * Scale, 0.f, 0.f},
            {0.f, collider.radius_y * Scale, 0.f}, 20,
            color, PreviewLayer);
        if (const auto* direction = world.reg().try_get<AoeDirection>(unit))
            gizmos.line(project(position, map),
                project(position + direction->value * collider.radius_x,
                        map),
                {1.f, 1.f, 1.f, 1.f}, PreviewLayer);
        if (const auto* route = world.reg().try_get<Aoe2xRoutePlan>(unit);
            route && route->status == Aoe2xRouteStatus::Ready)
            draw_path(gizmos, map, position, *route, color, 1.8f);
    }

    if (const auto* order =
            world.reg().try_get<FormationAttackMove>(state.squad))
        gizmos.cross(project(order->destination, map), 11.f,
                     {1.f, .15f, .15f, 1.f}, PreviewLayer);
}

void update_hud(EcsWorld& world) {
    const auto& state = world.resource<PreviewState>();
    if (!world.reg().valid(state.hud) ||
        !world.reg().valid(state.squad))
        return;
    const auto* order =
        world.reg().try_get<FormationAttackMove>(state.squad);
    std::size_t assigned = 0;
    std::uint32_t final_columns = 0;
    if (const auto* info = world.reg().try_get<SquadInfo>(state.squad)) {
        for (const auto unit : info->units) {
            if (!world.reg().valid(unit)) continue;
            if (const auto* assignment =
                    world.reg().try_get<RouteSquadRouteAssignment>(unit)) {
                ++assigned;
                final_columns = assignment->final_columns;
            }
        }
    }
    const auto& diagnostics =
        world.resource_or_add<Aoe2xPathfindingDiagnostics>();
    char value[384];
    std::snprintf(value, sizeof(value),
        "RouteSplit Formation | status: %s | members: %zu/%u | final columns: %u | center queries: %llu\n"
        "Colored lines: member routes | Right click: new destination | R: corridor demo | Esc: exit",
        order ? order_status_name(order->status) : "spawning",
        assigned, UnitCount, final_columns,
        static_cast<unsigned long long>(diagnostics.queries));
    auto& text = world.reg().get<Text>(state.hud);
    const auto converted = ascii_to_u32(value);
    if (text.text == converted) return;
    text.text = converted;
    ++text.rev;
}

} // namespace

int main() {
    const auto root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{
        1400, 900, "AoE2x RouteSplit Formation Preview"});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(GizmoPlugin);
    app.add_plugin(RenderPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.world.add_resource<PreviewState>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        world.add_resource<AoeLogicMap>(make_map());
        world.add_resource<Aoe2xPathfindingSettings>(
            Aoe2xPathfindingSettings{8, true});
        world.resource_or_add<FormationRegistry>()
            .bind<FormationType::CompactSquare, CompactSquareFormation>();

        FormationSpawnOptions options;
        options.count = UnitCount;
        options.center = {15.f, 36.f};
        options.spacing = .2f;
        options.unit_radius = .3f;
        options.forward = {1.f, 0.f};
        world.resource<PreviewState>().squad =
            spawn_aoe2x_formation(world, options);

        register_aoe2x_gameplay_system<RouteSquadSpawnSystem>(world);
        register_aoe2x_gameplay_system<RouteSquadCommandSystem>(world);
        register_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world);
        register_aoe2x_gameplay_system<RouteSquadSplitSystem>(world);
        register_aoe2x_gameplay_system<RouteSquadCleanupSystem>(world);

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
        text.text = U"RouteSplit Formation: spawning...";
        text.font = world.resource<AssetServer>().load(
            FontDesc("fonts/AGENCYB.TTF", 0));
        text.size = 19;
        text.leading = 3.f;
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
        std::puts("Right click: new destination. R: corridor demo. Escape: exit.");
    });

    app.add_system(Stage::PreUpdate, input_system);
    app.add_system(Stage::PreUpdate, simulation_system);
    app.add_system(Stage::PostUpdate, draw_gizmos);
    app.add_system(Stage::PostUpdate, update_hud);
    run_app(app);
}
