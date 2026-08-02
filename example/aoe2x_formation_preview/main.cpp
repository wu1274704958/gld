#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe2x/Aoe2xFormation.hpp>
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
constexpr float Scale = 9.f;

// TEMP: keep the captain stationary and rotate its logical direction slowly.
// Disable this flag to restore the normal automatic AttackMove preview.
constexpr bool CaptainRotationTest = false;
constexpr float CaptainRotationRadiansPerSecond = .35f;

struct PreviewState {
    entt::entity squad{entt::null};
    entt::entity hud{entt::null};
    std::uint64_t tick = 0;
    bool initial_command = true;
};

std::u32string ascii_to_u32(const std::string& value) {
    return {value.begin(), value.end()};
}

AoeMapDefinition make_map() {
    AoeMapDefinition map;
    map.id = "aoe2x_formation_preview";
    map.width = 144; map.height = 90; map.tile_size = 1.f;
    map.heights.resize(145u * 91u, 0.f);
    AoeStaticObstacleDesc wall;
    wall.shape = AoeStaticObstacleShape::Aabb;
    wall.half_extents = {.75f, 22.f};
    for (const auto center : {glm::vec2{24.f, 25.f}, glm::vec2{48.f, 65.f},
                              glm::vec2{72.f, 25.f}, glm::vec2{96.f, 65.f},
                              glm::vec2{120.f, 25.f}}) {
        wall.center = center;
        map.static_obstacles.push_back(wall);
    }
    wall.half_extents = {7.f, .75f};
    for (const auto center : {glm::vec2{36.f, 67.f}, glm::vec2{60.f, 20.f},
                              glm::vec2{84.f, 69.f}, glm::vec2{108.f, 20.f},
                              glm::vec2{132.f, 67.f}}) {
        wall.center = center;
        map.static_obstacles.push_back(wall);
    }
    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.radius = 4.f;
    for (const auto center : {glm::vec2{14.f, 70.f}, glm::vec2{36.f, 20.f},
                              glm::vec2{60.f, 69.f}, glm::vec2{84.f, 20.f},
                              glm::vec2{108.f, 70.f}, glm::vec2{132.f, 14.f}}) {
        circle.center = center;
        map.static_obstacles.push_back(circle);
    }
    return map;
}

glm::vec3 project(glm::vec2 point, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(), map.height() * map.tile_size()};
    return {(point.x - size.x * .5f) * Scale,
            (point.y - size.y * .5f) * Scale, 0.f};
}

glm::vec2 unproject(glm::vec2 cursor, const AoeLogicMap& map) {
    const glm::vec2 size{map.width() * map.tile_size(), map.height() * map.tile_size()};
    return {size.x * .5f + cursor.x / Scale,
            size.y * .5f - cursor.y / Scale};
}

glm::vec2 rotate_between(
    glm::vec2 value, glm::vec2 original, glm::vec2 current) {
    const float cosine = std::clamp(glm::dot(original, current), -1.f, 1.f);
    const float sine = original.x * current.y - original.y * current.x;
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

void input_system(EcsWorld& world) {
    if (auto* keyboard = world.try_resource<Keyboard>();
        keyboard && keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    auto* mouse = world.try_resource<MouseButtons>();
    if (!mouse || !mouse->just_now_pressed(GLFW_MOUSE_BUTTON_RIGHT)) return;
    if (CaptainRotationTest) return;
    const auto squad = world.resource<PreviewState>().squad;
    if (!world.reg().valid(squad) || !world.reg().all_of<SquadInfo>(squad)) return;
    const auto cursor = world.resource<CursorPosition>().position -
        glm::vec2(world.resource<Window>().width * .5f,
                  world.resource<Window>().height * .5f);
    request_aoe2x_formation_attack_move(
        world, squad, unproject(cursor, world.resource<AoeLogicMap>()));
}

void simulation_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    SpawnFormationSystem::run(world, state.tick);
    if (!state.initial_command && world.reg().valid(state.squad) &&
        world.reg().all_of<SquadInfo>(state.squad)) {
        if (CaptainRotationTest) {
            // Keep a synthetic command active but omit its route, so the
            // captain stays in place while the test rotates its direction.
            auto& order = world.reg().get<FormationAttackMove>(state.squad);
            order.status = FormationAttackMoveStatus::Running;
            order.destination = {139.f, 45.f};
        } else {
            request_aoe2x_formation_attack_move(
                world, state.squad, {139.f, 45.f});
        }
        state.initial_command = true;
    }
    if (CaptainRotationTest && world.reg().valid(state.squad)) {
        if (const auto* info = world.reg().try_get<SquadInfo>(state.squad);
            info && world.reg().valid(info->captain) &&
            world.reg().all_of<AoeDirection>(info->captain)) {
            const float dt = static_cast<float>(
                world.resource_or_add<AoeGameplaySettings>().fixed_dt);
            const float angle = static_cast<float>(state.tick) * dt *
                CaptainRotationRadiansPerSecond;
            world.reg().get<AoeDirection>(info->captain).value =
                {std::cos(angle), std::sin(angle)};
        }
    }
    FormationCommandSystem::run(world, state.tick);
    if (!CaptainRotationTest)
        Aoe2xPathfindingSystem::run(world, state.tick);
    FormationSystem::run(world, state.tick++);
}

void draw_gizmos(EcsWorld& world) {
    auto& gizmos = world.resource<Gizmos>();
    gizmos.clear();
    const auto& map = world.resource<AoeLogicMap>();
    const glm::vec2 high{map.width() * map.tile_size(), map.height() * map.tile_size()};
    const glm::vec2 corners[] = {{0.f, 0.f}, {high.x, 0.f}, high, {0.f, high.y}};
    for (int i = 0; i < 4; ++i)
        gizmos.line(project(corners[i], map), project(corners[(i + 1) % 4], map),
                    {.45f, .5f, .58f, 1.f}, PreviewLayer);
    map.visit_static_obstacles([&](AoeObstacleId, const AoeStaticObstacleDesc& obstacle) {
        if (obstacle.shape == AoeStaticObstacleShape::Aabb)
            gizmos.wire_box(project(obstacle.center, map),
                {obstacle.half_extents.x * Scale, obstacle.half_extents.y * Scale, 0.f},
                glm::mat3(1.f), {1.f, .3f, .12f, 1.f}, PreviewLayer);
        else
            gizmos.wire_ellipse(project(obstacle.center, map),
                {obstacle.radius * Scale, 0.f, 0.f},
                {0.f, obstacle.radius * Scale, 0.f}, 32,
                {1.f, .3f, .12f, 1.f}, PreviewLayer);
    });
    const auto squad = world.resource<PreviewState>().squad;
    if (!world.reg().valid(squad)) return;
    const auto* info = world.reg().try_get<SquadInfo>(squad);
    if (!info) return;
    for (const auto unit : info->units) {
        if (!world.reg().valid(unit) ||
            !world.reg().all_of<AoePosition, AoeCollider,
                UnitTargetPosition, UnitSquadInfo,
                UnitFormationDirection, AoeDirection>(unit))
            continue;
        const auto position = world.reg().get<AoePosition>(unit).value;
        const auto& collider = world.reg().get<AoeCollider>(unit);
        const glm::vec4 color = unit == info->captain
            ? glm::vec4{.2f, 1.f, .3f, 1.f}
            : glm::vec4{.15f, .75f, 1.f, 1.f};
        gizmos.wire_ellipse(project(position, map),
            {collider.radius_x * Scale, 0.f, 0.f},
            {0.f, collider.radius_y * Scale, 0.f}, 20, color, PreviewLayer);
        const auto direction = world.reg().get<AoeDirection>(unit).value;
        const float direction_length =
            std::min(collider.radius_x, collider.radius_y);
        gizmos.line(project(position, map),
            project(position + direction * direction_length, map),
            {1.f, 1.f, 1.f, 1.f}, PreviewLayer);
        const auto target = world.reg().get<UnitTargetPosition>(unit).value;
        gizmos.cross(project(target, map), 2.5f, {.65f, .3f, 1.f, .8f}, PreviewLayer);
        const auto& member = world.reg().get<UnitSquadInfo>(unit);
        if (member.followed != entt::null && world.reg().valid(member.followed) &&
            world.reg().all_of<AoePosition>(member.followed)) {
            const auto followed_position =
                world.reg().get<AoePosition>(member.followed).value;
            // Cyan is the actual link; purple is the currently requested link.
            gizmos.line(project(world.reg().get<AoePosition>(member.followed).value, map),
                        project(position, map), {.2f, .9f, .8f, .65f}, PreviewLayer);
            gizmos.line(project(followed_position, map), project(target, map),
                        {.75f, .3f, 1.f, .8f}, PreviewLayer);
        }
    }
    if (world.reg().valid(info->captain) &&
        world.reg().all_of<AoePosition>(info->captain))
    if (const auto* route = world.reg().try_get<Aoe2xRoutePlan>(info->captain)) {
        glm::vec2 previous = world.reg().get<AoePosition>(info->captain).value;
        for (const auto waypoint : route->waypoints) {
            gizmos.line(project(previous, map), project(waypoint, map),
                        {1.f, .85f, .1f, 1.f}, PreviewLayer);
            previous = waypoint;
        }
    }
    const auto* order = world.reg().try_get<FormationAttackMove>(squad);
    if (order && order->status != FormationAttackMoveStatus::Idle)
        gizmos.cross(project(order->destination, map), 7.f,
                     {1.f, .15f, .15f, 1.f}, PreviewLayer);
}

const char* status_name(FormationAttackMoveStatus status) {
    switch (status) {
    case FormationAttackMoveStatus::Idle: return "Idle";
    case FormationAttackMoveStatus::Running: return "Running";
    case FormationAttackMoveStatus::Completed: return "Completed";
    case FormationAttackMoveStatus::Failed: return "Failed";
    }
    return "Unknown";
}

void update_hud(EcsWorld& world) {
    const auto& state = world.resource<PreviewState>();
    if (!world.reg().valid(state.hud)) return;
    char value[192];
    const auto* info = world.reg().valid(state.squad)
        ? world.reg().try_get<SquadInfo>(state.squad) : nullptr;
    if (!info) {
        std::snprintf(value, sizeof(value), "Formation: spawning...");
    } else {
        const auto* order = world.reg().try_get<FormationAttackMove>(state.squad);
        if (!order) return;
        float maximum_error = 0.f;
        for (std::size_t i = 1; i < info->units.size(); ++i) {
            const auto unit = info->units[i];
            if (!world.reg().valid(unit) ||
                !world.reg().all_of<AoePosition, UnitSquadInfo>(unit))
                continue;
            const auto& member = world.reg().get<UnitSquadInfo>(unit);
            if (member.followed == entt::null ||
                !world.reg().valid(member.followed) ||
                !world.reg().valid(info->captain) ||
                !world.reg().all_of<AoePosition>(member.followed) ||
                !world.reg().all_of<UnitFormationDirection,
                    AoeDirection>(info->captain))
                continue;
            const glm::vec2 actual = world.reg().get<AoePosition>(member.followed).value -
                                     world.reg().get<AoePosition>(unit).value;
            const auto& captain_direction =
                world.reg().get<UnitFormationDirection>(info->captain);
            const glm::vec2 expected = rotate_between(
                member.followed_relative_to_self,
                captain_direction.original,
                world.reg().get<AoeDirection>(info->captain).value);
            maximum_error = std::max(maximum_error,
                glm::length(actual - expected));
        }
        std::snprintf(value, sizeof(value),
            "Formation: %s | units: %zu | max follow error: %.3f",
            status_name(order->status), info->units.size(), maximum_error);
    }
    auto& text = world.reg().get<Text>(state.hud);
    const auto converted = ascii_to_u32(value);
    if (text.text != converted) { text.text = converted; ++text.rev; }
}
} // namespace

int main() {
    const auto root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);
    App app;
    app.add_plugin(WindowPlugin{1600, 1000, "AoE2x Formation Preview"});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin); app.add_plugin(CorePlugin); app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin); app.add_plugin(GizmoPlugin); app.add_plugin(RenderPlugin);
    app.add_plugin(TextPlugin); TextBatchPlugin(app);
    app.world.add_resource<PreviewState>();
    app.add_system(Stage::Startup, [](EcsWorld& world) {
        world.add_resource<AoeLogicMap>(make_map());
        world.add_resource<Aoe2xPathfindingSettings>(Aoe2xPathfindingSettings{8, true});
        world.resource_or_add<FormationRegistry>()
            .bind<FormationType::CompactSquare, CompactSquareFormation>();
        auto& navigation = world.resource_or_add<AoeNavigationSettings>();
        navigation.steering_max_acceleration = 7.f;
        FormationSpawnOptions options;
        options.count = 16; options.center = {10.f, 45.f};
        options.spacing = .22f; options.unit_radius = .9f;
        options.movement_speed = 5.f;
        world.resource<PreviewState>().squad = spawn_aoe2x_formation(world, options);
        const auto camera = world.spawn();
        Camera camera_value; camera_value.kind = CameraKind::Ortho;
        camera_value.layers = PreviewLayer; camera_value.clear_color = {.08f, .1f, .13f, 1.f};
        world.reg().emplace<Camera>(camera, camera_value);
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
        Text text; text.text = U"Formation: spawning...";
        text.font = world.resource<AssetServer>().load(FontDesc("fonts/AGENCYB.TTF", 0));
        text.size = 20; text.color = {.92f, .97f, 1.f, 1.f};
        text.align = TextAlign::Left; text.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(hud, std::move(text));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(hud, RenderLayer{HudLayer});
        world.resource<PreviewState>().hud = hud;
        if (CaptainRotationTest)
            std::puts("TEMP rotation test: captain rotates slowly; cyan=actual, "
                      "purple=target link. Escape: exit.");
        else
            std::puts("Right click: replace AttackMove destination. Escape: exit.");
    });
    app.add_system(Stage::PreUpdate, input_system);
    app.add_system(Stage::PreUpdate, simulation_system);
    app.add_system(Stage::PostUpdate, draw_gizmos);
    app.add_system(Stage::PostUpdate, update_hud);
    run_app(app);
}
