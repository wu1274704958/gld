#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <memory>
#include <string>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe/AoeMap.hpp>
#include <aoe2x/Aoe2xNavigation.hpp>
#include <ecs/App.hpp>
#include <ecs/Input.hpp>
#include <ecs/Window.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/render/Gizmo.hpp>
#include <ecs/render/BatchSystem.hpp>
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
constexpr float Scale = 9.f;

struct PreviewState {
    entt::entity squad{entt::null};
    entt::entity hud{entt::null};
    std::uint64_t tick = 0;
};

std::u32string ascii_to_u32(const std::string& value) {
    return {value.begin(), value.end()};
}

AoeMapDefinition make_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_hpa_preview";
    map.origin = glm::vec2(0.f); map.tile_size = 1.f; map.width = 112; map.height = 72;
    map.heights.resize(113u * 73u, 0.f);
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.center = {24.f, 22.f}; wall.half_extents = {.75f, 18.f};
    map.static_obstacles.push_back(wall);
    wall.center = {48.f, 50.f};
    map.static_obstacles.push_back(wall);
    wall.center = {72.f, 22.f};
    map.static_obstacles.push_back(wall);
    wall.center = {96.f, 50.f};
    map.static_obstacles.push_back(wall);

    wall.half_extents = {7.f, .75f};
    wall.center = {36.f, 52.f};
    map.static_obstacles.push_back(wall);
    wall.center = {60.f, 18.f};
    map.static_obstacles.push_back(wall);
    wall.center = {84.f, 54.f};
    map.static_obstacles.push_back(wall);

    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.radius = 4.f;
    circle.center = {14.f, 55.f};
    map.static_obstacles.push_back(circle);
    circle.center = {36.f, 18.f};
    map.static_obstacles.push_back(circle);
    circle.center = {60.f, 54.f};
    map.static_obstacles.push_back(circle);
    circle.center = {84.f, 18.f};
    map.static_obstacles.push_back(circle);
    circle.center = {104.f, 12.f};
    map.static_obstacles.push_back(circle);
    return map;
}

glm::vec3 project(glm::vec2 point, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(), map.height() * map.tile_size()};
    return {(point.x - map.origin().x - size.x * .5f) * Scale,
            (point.y - map.origin().y - size.y * .5f) * Scale, 0.f};
}
glm::vec2 unproject(glm::vec2 cursor, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(), map.height() * map.tile_size()};
    return map.origin() + glm::vec2(size.x * .5f + cursor.x / Scale,
                                    size.y * .5f - cursor.y / Scale);
}

void input_system(EcsWorld& world) {
    if (auto* keyboard = world.try_resource<Keyboard>();
        keyboard && keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    auto* mouse = world.try_resource<MouseButtons>();
    if (!mouse || !mouse->just_now_pressed(GLFW_MOUSE_BUTTON_LEFT)) return;
    const auto squad = world.resource<PreviewState>().squad;
    if (!world.reg().valid(squad)) return;
    auto destination = unproject(world.resource<CursorPosition>().position -
        glm::vec2(world.resource<Window>().width * .5f, world.resource<Window>().height * .5f),
        world.resource<AoeLogicMap>());
    world.reg().get<Aoe2xNavigationDestination>(squad).value = destination;
}

void draw_gizmos(EcsWorld& world) {
    auto& gizmos = world.resource<Gizmos>(); gizmos.clear();
    const auto& map = world.resource<AoeLogicMap>();
    const glm::vec2 low = map.origin();
    const glm::vec2 high = low + glm::vec2(map.width() * map.tile_size(),
                                            map.height() * map.tile_size());
    const glm::vec2 corners[] = {{low.x, low.y}, {high.x, low.y},
                                 {high.x, high.y}, {low.x, high.y}};
    for (int i = 0; i < 4; ++i)
        gizmos.line(project(corners[i], map), project(corners[(i + 1) % 4], map),
                    {.45f, .5f, .58f, 1.f}, PreviewLayer);
    map.visit_static_obstacles([&](AoeObstacleId, const AoeStaticObstacleDesc& obstacle) {
        if (obstacle.shape == AoeStaticObstacleShape::Aabb)
            gizmos.wire_box(project(obstacle.center, map),
                {obstacle.half_extents.x * Scale, obstacle.half_extents.y * Scale, 0.f},
                glm::mat3(1.f), {1.f, .35f, .1f, 1.f}, PreviewLayer);
        else gizmos.wire_ellipse(project(obstacle.center, map),
            {obstacle.radius * Scale, 0.f, 0.f}, {0.f, obstacle.radius * Scale, 0.f},
            32, {1.f, .35f, .1f, 1.f}, PreviewLayer);
    });
    const auto squad = world.resource<PreviewState>().squad;
    if (!world.reg().valid(squad)) return;
    const auto start = world.reg().get<AoePosition>(squad).value;
    const auto goal = world.reg().get<Aoe2xNavigationDestination>(squad).value;
    gizmos.cross(project(start, map), 8.f, {.2f, 1.f, .3f, 1.f}, PreviewLayer);
    gizmos.cross(project(goal, map), 8.f, {1.f, .2f, .2f, 1.f}, PreviewLayer);
    glm::vec2 previous = start;
    if (const auto* route = world.reg().try_get<Aoe2xRoutePlan>(squad))
        for (const auto waypoint : route->waypoints) {
            gizmos.line(project(previous, map), project(waypoint, map),
                        {1.f, .85f, .1f, 1.f}, PreviewLayer);
            gizmos.cross(project(waypoint, map), 5.f, {.1f, .9f, 1.f, 1.f}, PreviewLayer);
            previous = waypoint;
        }
}

void update_hud(EcsWorld& world) {
    const auto& state = world.resource<PreviewState>();
    const auto hud = state.hud;
    if (!world.reg().valid(hud)) return;
    auto* descriptor = world.resource<Aoe2xGameplaySystemRegistry>()
        .find(Aoe2xPathfindingSystem::name);
    const auto* route = world.reg().valid(state.squad)
        ? world.reg().try_get<Aoe2xRoutePlan>(state.squad) : nullptr;
    const auto waypoint_count = route ? route->waypoints.size() : 0u;
    char value[192];
    if (descriptor && descriptor->timing.average_ms) {
        if (route && route->total_cost)
            std::snprintf(value, sizeof(value),
                "Pathfinding avg: %.3f ms (%llu samples) | waypoints: %zu | cost: %.3f",
                *descriptor->timing.average_ms,
                static_cast<unsigned long long>(descriptor->timing.samples),
                waypoint_count, *route->total_cost);
        else
            std::snprintf(value, sizeof(value),
                "Pathfinding avg: %.3f ms (%llu samples) | no path",
                *descriptor->timing.average_ms,
                static_cast<unsigned long long>(descriptor->timing.samples));
    } else if (route && route->total_cost) {
        std::snprintf(value, sizeof(value),
            "Pathfinding avg: collecting... | waypoints: %zu | cost: %.3f",
            waypoint_count, *route->total_cost);
    } else {
        std::snprintf(value, sizeof(value), "Pathfinding avg: collecting... | no path");
    }
    auto& text = world.reg().get<Text>(hud);
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
    app.add_plugin(WindowPlugin{1280, 800, "AoE2x HPA* Pathfinding Preview"});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin); app.add_plugin(CorePlugin); app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin); app.add_plugin(GizmoPlugin); app.add_plugin(RenderPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.world.add_resource<PreviewState>();
    app.add_system(Stage::Startup, [](EcsWorld& world) {
        world.add_resource<AoeLogicMap>(make_map());
        world.add_resource<Aoe2xPathfindingSettings>(Aoe2xPathfindingSettings{8, true});
        const auto squad = world.spawn();
        world.reg().emplace<AoePosition>(squad, AoePosition{{5.f, 36.f}});
        world.reg().emplace<AoeCollider>(squad, AoeCollider{.3f, .3f, 1.f});
        world.reg().emplace<Aoe2xNavigationDestination>(squad,
            Aoe2xNavigationDestination{{107.f, 36.f}});
        world.resource<PreviewState>().squad = squad;
        register_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world);
        const auto camera = world.spawn();
        Camera value; value.kind = CameraKind::Ortho; value.layers = PreviewLayer;
        value.clear_color = {.08f, .1f, .13f, 1.f};
        world.reg().emplace<Camera>(camera, value);
        auto& passes = emplace_registered_render_passes(world, camera);
        auto& pass = passes.add(GizmoPassId);
        pass.state.depth_test = RenderStateValue::Disabled;
        pass.state.depth_write = RenderStateValue::Disabled;
        const auto hud_camera = world.spawn();
        Camera hud_camera_value; hud_camera_value.kind = CameraKind::Ortho;
        hud_camera_value.priority = 20; hud_camera_value.layers = HudLayer;
        hud_camera_value.do_clear = false;
        world.reg().emplace<Camera>(hud_camera, hud_camera_value);
        emplace_render_passes<BatchPass>(world, hud_camera);
        const auto hud = world.spawn();
        Text text;
        text.text = U"Pathfinding avg: collecting...";
        text.font = world.resource<AssetServer>().load(FontDesc("fonts/AGENCYB.TTF", 0));
        text.size = 20; text.color = {.92f, .97f, 1.f, 1.f};
        text.align = TextAlign::Left; text.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(hud, std::move(text));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(hud, RenderLayer{HudLayer});
        world.resource<PreviewState>().hud = hud;
        std::puts("Left click: choose destination. Escape: exit.");
    });
    app.add_system(Stage::PreUpdate, input_system);
    app.add_system(Stage::PreUpdate, [](EcsWorld& world) {
        auto& state = world.resource<PreviewState>();
        run_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world, state.tick++);
    });
    app.add_system(Stage::PostUpdate, draw_gizmos);
    app.add_system(Stage::PostUpdate, update_hud);
    run_app(app);
}
