#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <array>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <aoe/AoeGameplay.hpp>
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
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe;
using namespace gld::ecs::aoe2;
using namespace gld::ecs::aoe2_gameplay;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t UnitLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;
constexpr float TileWidth = 54.f;
constexpr float TileHeight = 27.f;
constexpr float SpriteScale = .60f;
constexpr float DepthUnitsPerTile = 1.f;
constexpr float ElevationPixelsPerUnit = 48.f;
constexpr glm::vec2 BlueSpawn{-11.f, 0.f};
constexpr glm::vec2 RedSpawn{11.f, 0.f};
constexpr glm::vec2 BlueDestination{11.f, 0.f};
constexpr glm::vec2 RedDestination{-11.f, 0.f};

struct StressPreset {
    int key = 2;
    std::uint32_t total = 64;
    std::uint32_t camels = 24;
    std::uint32_t archers = 40;
};

constexpr std::array StressPresets{
    StressPreset{1, 16, 6, 10},
    StressPreset{2, 64, 24, 40},
    StressPreset{3, 128, 48, 80},
};

struct PreviewState {
    entt::entity blue{entt::null};
    entt::entity red{entt::null};
    entt::entity hud{entt::null};
    bool orders_issued = false;
    bool draw_unit_feet = false;
    bool draw_unit_colliders = false;
    bool trace_unit_foot = false;
    bool draw_map = true;
    bool draw_navigation = false;
    int stress_preset = 2;
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
};

const StressPreset& active_stress_preset(const PreviewState& state) {
    const auto it = std::find_if(StressPresets.begin(), StressPresets.end(),
        [&](const StressPreset& value) { return value.key == state.stress_preset; });
    return it == StressPresets.end() ? StressPresets[1] : *it;
}

AoeMapDefinition make_preview_map() {
    AoeMapDefinition result;
    result.id = "squad_preview";
    result.origin = {-18.f, -8.f};
    result.tile_size = 1.f;
    result.width = 36;
    result.height = 16;
    result.heights.assign(
        static_cast<std::size_t>(result.width + 1) * (result.height + 1), 0.f);

    AoeStaticObstacleDesc left;
    left.source_id = "left_gate";
    left.shape = AoeStaticObstacleShape::Aabb;
    left.center = {-4.f, 0.f};
    left.half_extents = {.8f, 2.5f};
    result.static_obstacles.push_back(left);

    AoeStaticObstacleDesc right = left;
    right.source_id = "right_gate";
    right.center = {4.f, 0.f};
    result.static_obstacles.push_back(right);

    AoeStaticObstacleDesc south;
    south.source_id = "south_rock";
    south.shape = AoeStaticObstacleShape::Circle;
    south.center = {0.f, -4.f};
    south.radius = 1.f;
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
        {"camel_scout", preset.camels, color},
        {"archer", preset.archers, color}};
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
        std::to_string(preset.total) + " units per side";
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
    std::uint32_t beyond_leash = 0;
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
        if (const auto* path = reg.try_get<AoeNavigationPath>(member.entity);
            path && path->no_path)
            ++no_path;
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
        << " no-path=" << no_path << " beyond-leash=" << beyond_leash << '\n'
        << "  guide=";
    if (guide) {
        out << guide->current << '/' << guide->waypoints.size()
            << " no-path=" << (guide->no_path ? "yes" : "no")
            << " dynamic=" << (guide->include_dynamic_obstacles ? "yes" : "no");
    } else out << "none";
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
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto& preset = active_stress_preset(preview);
    const auto* render = world.try_resource<RenderDiagnostics>();
    const auto* aoe2_performance = world.try_resource<Aoe2PerformanceDiagnostics>();
    const auto& gameplay = world.resource<AoeGameplayDiagnostics>();
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
        << "1/2/3 = 16/64/128 per side | Space attack-move | S stop | R reset | F5 reload\n"
        << "G map=" << (preview.draw_map ? "ON" : "OFF")
        << " | N navigation=" << (preview.draw_navigation ? "ON" : "OFF")
        << " | C collision=" << (preview.draw_unit_colliders ? "ON" : "OFF")
        << " | P feet=" << (preview.draw_unit_feet ? "ON" : "OFF")
        << " | L trace=" << (preview.trace_unit_foot ? "ON" : "OFF")
        << " | V vsync="
        << (world.resource<Window>().vsync ? "ON" : "OFF")
        << " | Esc quit\n"
        << "preset=" << preset.key << " units/side=" << preset.total
        << " (debug gizmos affect stress measurements)\n"
        << "map=squad_preview grid=36x16 pathfinder=grid_astar\n\n";
    append_squad(out, world, "BLUE", preview.blue);
    append_squad(out, world, "RED ", preview.red);
    out << "\nPERFORMANCE fps=" << time.fps
        << " frame_dt_ms=" << time.raw_dt * 1000.f
        << " fixed_ticks=" << clock.ticks_this_frame
        << " dropped_s=" << clock.dropped_seconds << '\n'
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
        << " facing-committed=" << gameplay.facing_changes_committed << '\n';
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
    app.add_plugin(AoeGameplayPlugin{"aoe_units"});
    app.add_plugin(Aoe2GameplayBridgePlugin{});
    app.add_plugin(GizmoPlugin);
    app.add_plugin(RenderPlugin);
    PreviewState initial_preview;
    if (const char* value = std::getenv("GLD_AOE_STRESS_PRESET")) {
        const long preset = std::strtol(value, nullptr, 10);
        if (preset >= 1 && preset <= 3)
            initial_preview.stress_preset = static_cast<int>(preset);
    }
    app.world.add_resource<PreviewState>(std::move(initial_preview));

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        const auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = UnitLayer;
        camera.clear_color = {.12f, .14f, .17f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
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
            "Controls: 1/2/3 stress preset, Space mutual AttackMove, S stop, "
            "G map, N navigation, C collision, P feet, L trace, R reset, F5 rescan, "
            "Escape quit\n");
    });
    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::Update, projection_system);
    app.add_system(Stage::PostUpdate, submit_map_navigation_gizmos);
    app.add_system(Stage::PostUpdate, submit_unit_collider_gizmos);
    app.add_system(Stage::PostUpdate, submit_unit_foot_gizmos);
    app.add_system(Stage::Last, diagnostics_system);
    app.add_system(Stage::Last, trace_blue_archer_foot);
    run_app(app);
    return 0;
}
