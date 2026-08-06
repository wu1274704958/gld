#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cmath>
#include <memory>
#include <random>
#include <string>
#include <vector>

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
// Squad footprints are laid out from these, so they also decide how much of
// the map a stress preset occupies.
constexpr float UnitRadius = .35f;
constexpr float UnitSpacing = .1f;
// CompactSquareFormation refuses counts above this, so a stress run is split
// into several squads instead of one huge formation.
constexpr std::uint32_t MaxSquadSize = 1024u;
constexpr double ProfileWindowSeconds = .5;

enum class RenderDetail : std::uint8_t { Full, Lite, Off };

const char* detail_name(RenderDetail detail) {
    switch (detail) {
    case RenderDetail::Full: return "full";
    case RenderDetail::Lite: return "lite";
    case RenderDetail::Off: return "off";
    }
    return "?";
}

struct StressConfig {
    std::uint32_t preset = 1;
    std::uint32_t total_units = 500;
    std::uint32_t squad_size = 500;
    std::uint32_t squad_count = 1;
    std::uint32_t map_width = 192;
    std::uint32_t map_height = 120;
    RenderDetail detail = RenderDetail::Full;
    // Non-zero closes the window after this many seconds and echoes every
    // profile window to stdout, which is how presets get captured in batch.
    double profile_seconds = 0.0;
    // Units killed per simulation tick, for observing death handling without
    // holding a key down. 0 disables attrition.
    std::uint32_t attrition_per_tick = 0;
};

struct SystemTiming {
    double average_ms = 0.0;
    double peak_ms = 0.0;
};

// Values shown by the HUD. Refreshed once per profile window so a single slow
// frame cannot make the readout unreadable.
struct ProfileSnapshot {
    float fps = 0.f;
    double frame_ms = 0.0;
    double sim_ms = 0.0;
    double gizmo_ms = 0.0;
    double hud_ms = 0.0;
    std::size_t gizmo_vertices = 0;
    SystemTiming spawn, command, pathfinding, formation;
    Aoe2xPathfindingDiagnostics totals;
    Aoe2xPathfindingDiagnostics window;
    float follow_error = 0.f;
};

struct ProfileWindow {
    double elapsed = 0.0;
    std::uint32_t frames = 0;
    double sim_ms = 0.0;
    double gizmo_ms = 0.0;
    double hud_ms = 0.0;
    Aoe2xPathfindingDiagnostics previous;
};

struct PreviewState {
    StressConfig config;
    std::vector<entt::entity> squads;
    entt::entity hud{entt::null};
    std::uint64_t tick = 0;
    bool initial_command_sent = false;
    // Squads ping-pong between their deployment strip and its mirror so the
    // stress run keeps marching instead of parking after the first arrival.
    bool marching_forward = true;
    std::uint32_t march_legs = 0;
    std::uint32_t running_squads = 0;
    std::uint64_t last_leg_tick = 0;
    // Kill counters for the [K] / [C] casualty keys.
    std::uint32_t killed_units = 0;
    std::uint32_t killed_captains = 0;
    float scale = 9.f;
    double run_seconds = 0.0;
    double sim_ms = 0.0;
    double gizmo_ms = 0.0;
    double hud_ms = 0.0;
    ProfileWindow window;
    ProfileSnapshot snapshot;
};

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(
        Clock::now() - started).count();
}

std::u32string ascii_to_u32(const std::string& value) {
    return {value.begin(), value.end()};
}

std::uint32_t env_uint(const char* name, std::uint32_t fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !*raw) return fallback;
    char* end = nullptr;
    const auto parsed = std::strtoul(raw, &end, 10);
    if (end == raw || !parsed) {
        std::printf("[aoe2x] ignoring %s=\"%s\", using %u\n",
                    name, raw, fallback);
        return fallback;
    }
    return static_cast<std::uint32_t>(parsed);
}

StressConfig read_config() {
    StressConfig config;
    config.preset = std::clamp(env_uint("GLD_AOE2X_STRESS_PRESET", 3u), 1u, 5u);
    struct Preset {
        std::uint32_t units, width, height;
    };
    static constexpr Preset presets[] = {
        {500u, 192u, 120u}, {2000u, 192u, 120u}, {5000u, 320u, 200u},
        {10000u, 400u, 250u}, {20000u, 512u, 320u}};
    const auto& preset = presets[config.preset - 1u];
    config.total_units = preset.units;
    config.map_width = preset.width;
    config.map_height = preset.height;
    config.squad_size = std::clamp(
        env_uint("GLD_AOE2X_SQUAD_SIZE", 500u), 1u, MaxSquadSize);
    config.squad_size = std::min(config.squad_size, config.total_units);
    config.squad_count =
        (config.total_units + config.squad_size - 1u) / config.squad_size;
    // The last squad absorbs the remainder by keeping every squad the same
    // size, so the reported unit count matches what is actually spawned.
    config.total_units = config.squad_count * config.squad_size;
    config.detail = config.total_units <= 2000u
        ? RenderDetail::Full : RenderDetail::Lite;
    if (const char* raw = std::getenv("GLD_AOE2X_RENDER_DETAIL"); raw && *raw) {
        const std::string value = raw;
        if (value == "full") config.detail = RenderDetail::Full;
        else if (value == "lite") config.detail = RenderDetail::Lite;
        else if (value == "off") config.detail = RenderDetail::Off;
        else std::printf("[aoe2x] ignoring GLD_AOE2X_RENDER_DETAIL=\"%s\"\n", raw);
    }
    config.profile_seconds =
        static_cast<double>(env_uint("GLD_AOE2X_PROFILE_SECONDS", 0u));
    config.attrition_per_tick = env_uint("GLD_AOE2X_ATTRITION", 0u);
    return config;
}

float squad_block_size(std::uint32_t squad_size) {
    const float cell = UnitRadius * 2.f + UnitSpacing;
    return std::ceil(std::sqrt(static_cast<float>(squad_size))) * cell + cell;
}

void add_wall_column(AoeMapDefinition& map, float x, float height,
                     std::initializer_list<float> gap_centers, float gap_half) {
    float cursor = 0.f;
    auto emit = [&](float low, float high) {
        if (high - low < 1.f) return;
        AoeStaticObstacleDesc wall;
        wall.shape = AoeStaticObstacleShape::Aabb;
        wall.half_extents = {1.f, (high - low) * .5f};
        wall.center = {x, (low + high) * .5f};
        map.static_obstacles.push_back(wall);
    };
    for (const float gap : gap_centers) {
        emit(cursor, gap - gap_half);
        cursor = std::max(cursor, gap + gap_half);
    }
    emit(cursor, height);
}

// Obstacles stay inside the central band so both the deployment strip and the
// destination strip are guaranteed clear at any map size.
AoeMapDefinition make_map(std::uint32_t width, std::uint32_t height) {
    AoeMapDefinition map;
    map.id = "aoe2x_formation_preview";
    map.width = width; map.height = height; map.tile_size = 1.f;
    map.heights.resize(
        static_cast<std::size_t>(width + 1u) * (height + 1u), 0.f);
    const float w = static_cast<float>(width), h = static_cast<float>(height);
    const float gap_half = std::max(9.f, h * .045f);
    add_wall_column(map, w * .38f, h, {h * .22f, h * .58f, h * .88f}, gap_half);
    add_wall_column(map, w * .50f, h, {h * .40f, h * .76f}, gap_half);
    add_wall_column(map, w * .62f, h, {h * .15f, h * .50f, h * .84f}, gap_half);
    AoeStaticObstacleDesc circle;
    circle.shape = AoeStaticObstacleShape::Circle;
    circle.radius = std::max(3.f, h * .03f);
    for (const auto fraction : {glm::vec2{.43f, .30f}, glm::vec2{.44f, .68f},
                                glm::vec2{.56f, .22f}, glm::vec2{.57f, .60f},
                                glm::vec2{.56f, .92f}}) {
        circle.center = {w * fraction.x, h * fraction.y};
        map.static_obstacles.push_back(circle);
    }
    return map;
}

glm::vec3 project(glm::vec2 point, const AoeLogicMap& map, float scale) {
    const glm::vec2 size{map.width() * map.tile_size(),
                         map.height() * map.tile_size()};
    return {(point.x - size.x * .5f) * scale,
            (point.y - size.y * .5f) * scale, 0.f};
}

glm::vec2 unproject(glm::vec2 cursor, const AoeLogicMap& map, float scale) {
    const glm::vec2 size{map.width() * map.tile_size(),
                         map.height() * map.tile_size()};
    return {size.x * .5f + cursor.x / scale,
            size.y * .5f - cursor.y / scale};
}

glm::vec2 rotate_between(
    glm::vec2 value, glm::vec2 original, glm::vec2 current) {
    const float cosine = std::clamp(glm::dot(original, current), -1.f, 1.f);
    const float sine = original.x * current.y - original.y * current.x;
    return {value.x * cosine - value.y * sine,
            value.x * sine + value.y * cosine};
}

// Deployment centres for every squad, packed into the left strip of the map.
std::vector<glm::vec2> squad_centers(const StressConfig& config) {
    const float w = static_cast<float>(config.map_width);
    const float h = static_cast<float>(config.map_height);
    const float block = squad_block_size(config.squad_size);
    const float region_low = w * .03f, region_high = w * .33f;
    const float region_width = std::max(block, region_high - region_low);
    const float region_height = h * .9f;
    float pitch = block * 1.4f;
    auto columns = std::max(1u,
        static_cast<std::uint32_t>(region_width / pitch));
    const auto rows = (config.squad_count + columns - 1u) / columns;
    if (rows > 1u && rows * pitch > region_height)
        pitch = std::max(block * 1.05f, region_height / rows);
    std::vector<glm::vec2> result;
    result.reserve(config.squad_count);
    const float origin_x = region_low + block * .5f;
    const float origin_y = std::max(block * .5f,
        h * .5f - (rows - 1u) * pitch * .5f);
    for (std::uint32_t i = 0; i < config.squad_count; ++i)
        result.push_back({origin_x + (i % columns) * pitch,
                          origin_y + (i / columns) * pitch});
    return result;
}

// Squads march to their mirror image across the map mid-axis, so every squad
// picks a different route through the obstacle band.
glm::vec2 mirror_destination(glm::vec2 center, const StressConfig& config) {
    return {static_cast<float>(config.map_width) - center.x, center.y};
}

void issue_attack_move(EcsWorld& world, glm::vec2 anchor) {
    auto& state = world.resource<PreviewState>();
    const auto centers = squad_centers(state.config);
    const float w = static_cast<float>(state.config.map_width);
    const float h = static_cast<float>(state.config.map_height);
    glm::vec2 origin{0.f};
    for (const auto center : centers) origin += center;
    origin /= static_cast<float>(centers.size());
    for (std::size_t i = 0; i < state.squads.size(); ++i) {
        const glm::vec2 offset = centers[i] - origin;
        const glm::vec2 destination{
            std::clamp(anchor.x + offset.x, 1.f, w - 1.f),
            std::clamp(anchor.y + offset.y, 1.f, h - 1.f)};
        request_aoe2x_formation_attack_move(world, state.squads[i], destination);
    }
}

bool squads_ready(const EcsWorld& world, const PreviewState& state) {
    for (const auto squad : state.squads) {
        if (!world.reg().valid(squad) ||
            !world.reg().all_of<SquadInfo, FormationSpawnState>(squad) ||
            world.reg().get<FormationSpawnState>(squad).status !=
                FormationSpawnStatus::Ready)
            return false;
    }
    return !state.squads.empty();
}

std::uint32_t count_running(const EcsWorld& world, const PreviewState& state) {
    std::uint32_t running = 0;
    for (const auto squad : state.squads) {
        if (!world.reg().valid(squad)) continue;
        const auto* order = world.reg().try_get<FormationAttackMove>(squad);
        if (order && order->status == FormationAttackMoveStatus::Running)
            ++running;
    }
    return running;
}

std::uint32_t count_alive(const EcsWorld& world, const PreviewState& state) {
    std::uint32_t alive = 0;
    for (const auto squad : state.squads)
        if (const auto* info = world.reg().try_get<SquadInfo>(squad))
            alive += static_cast<std::uint32_t>(info->units.size());
    return alive;
}

void issue_march_leg(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    const auto centers = squad_centers(state.config);
    bool accepted = false;
    for (std::size_t i = 0; i < state.squads.size(); ++i)
        accepted |= request_aoe2x_formation_attack_move(world, state.squads[i],
            state.marching_forward
                ? mirror_destination(centers[i], state.config) : centers[i]);
    // A wiped-out army accepts nothing, and counting those as march legs would
    // spin the HUD counter for the rest of the run.
    if (!accepted) return;
    ++state.march_legs;
    state.last_leg_tick = state.tick;
}

// Picks a live victim at random. Squads are walked from a random offset so no
// single squad is ground down first, and so [C] finds a squad that still has a
// captain to lose.
void inflict_casualty(EcsWorld& world, bool captain_only) {
    auto& state = world.resource<PreviewState>();
    if (state.squads.empty()) return;
    static std::mt19937 rng{20260806u};
    const std::size_t start = rng() % state.squads.size();
    for (std::size_t i = 0; i < state.squads.size(); ++i) {
        const auto squad = state.squads[(start + i) % state.squads.size()];
        const auto* info = world.reg().try_get<SquadInfo>(squad);
        if (!info || info->units.empty()) continue;
        const auto victim = captain_only
            ? info->captain
            : info->units[rng() % info->units.size()];
        if (!aoe2x_unit_alive(world.reg(), victim)) continue;
        const bool was_captain = victim == info->captain;
        kill_aoe2x_formation_unit(world, victim);
        ++state.killed_units;
        if (was_captain) ++state.killed_captains;
        return;
    }
}

void input_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    if (auto* keyboard = world.try_resource<Keyboard>()) {
        if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
            world.resource<Window>().should_close = true;
        if (keyboard->just_now_pressed(GLFW_KEY_G)) {
            state.config.detail = static_cast<RenderDetail>(
                (static_cast<std::uint8_t>(state.config.detail) + 1u) % 3u);
            std::printf("[aoe2x] render detail: %s\n",
                        detail_name(state.config.detail));
        }
        // Casualties on demand: [K] takes a random member so the follow chain
        // has to splice around it, [C] takes a captain so the successor has to
        // inherit the route. Hold either key down for sustained attrition.
        const bool kill_any = keyboard->just_now_pressed(GLFW_KEY_K);
        const bool kill_captain = keyboard->just_now_pressed(GLFW_KEY_C);
        if (kill_any || kill_captain) inflict_casualty(world, kill_captain);
    }
    auto* mouse = world.try_resource<MouseButtons>();
    if (!mouse || !mouse->just_now_pressed(GLFW_MOUSE_BUTTON_RIGHT)) return;
    const auto cursor = world.resource<CursorPosition>().position -
        glm::vec2(world.resource<Window>().width * .5f,
                  world.resource<Window>().height * .5f);
    issue_attack_move(world,
        unproject(cursor, world.resource<AoeLogicMap>(), state.scale));
}

void simulation_system(EcsWorld& world) {
    const auto started = Clock::now();
    auto& state = world.resource<PreviewState>();
    run_aoe2x_gameplay_system<SpawnFormationSystem>(world, state.tick);
    if (!state.initial_command_sent && squads_ready(world, state)) {
        state.initial_command_sent = true;
        issue_march_leg(world);
    }
    run_aoe2x_gameplay_system<FormationCommandSystem>(world, state.tick);
    if (state.initial_command_sent)
        for (std::uint32_t i = 0; i < state.config.attrition_per_tick; ++i)
            inflict_casualty(world, false);
    run_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world, state.tick);
    run_aoe2x_gameplay_system<FormationSystem>(world, state.tick);
    run_aoe2x_gameplay_system<Aoe2xUnitLifecycleSystem>(world, state.tick++);
    state.running_squads = count_running(world, state);
    // Re-issuing once every squad has settled keeps both the movement and the
    // pathfinding systems under continuous load for the whole capture. The
    // tick guard stops an unreachable destination from re-commanding forever.
    if (state.initial_command_sent && !state.running_squads &&
        state.tick > state.last_leg_tick + 30u) {
        state.marching_forward = !state.marching_forward;
        issue_march_leg(world);
    }
    state.sim_ms = elapsed_ms(started);
}

void draw_squad_full(EcsWorld& world, const SquadInfo& info,
                     const AoeLogicMap& map, float scale) {
    auto& gizmos = world.resource<Gizmos>();
    for (const auto unit : info.units) {
        if (!world.reg().valid(unit) ||
            !world.reg().all_of<AoePosition, AoeCollider,
                UnitTargetPosition, UnitSquadInfo,
                UnitFormationDirection, AoeDirection>(unit))
            continue;
        const auto position = world.reg().get<AoePosition>(unit).value;
        const auto& collider = world.reg().get<AoeCollider>(unit);
        const glm::vec4 color = unit == info.captain
            ? glm::vec4{.2f, 1.f, .3f, 1.f}
            : glm::vec4{.15f, .75f, 1.f, 1.f};
        gizmos.wire_ellipse(project(position, map, scale),
            {collider.radius_x * scale, 0.f, 0.f},
            {0.f, collider.radius_y * scale, 0.f}, 20, color, PreviewLayer);
        const auto direction = world.reg().get<AoeDirection>(unit).value;
        const float direction_length =
            std::min(collider.radius_x, collider.radius_y);
        gizmos.line(project(position, map, scale),
            project(position + direction * direction_length, map, scale),
            {1.f, 1.f, 1.f, 1.f}, PreviewLayer);
        const auto target = world.reg().get<UnitTargetPosition>(unit).value;
        gizmos.cross(project(target, map, scale), 2.5f,
                     {.65f, .3f, 1.f, .8f}, PreviewLayer);
        const auto& member = world.reg().get<UnitSquadInfo>(unit);
        if (member.followed != entt::null && world.reg().valid(member.followed) &&
            world.reg().all_of<AoePosition>(member.followed)) {
            const auto followed_position =
                world.reg().get<AoePosition>(member.followed).value;
            // Cyan is the actual link; purple is the currently requested link.
            gizmos.line(project(followed_position, map, scale),
                        project(position, map, scale),
                        {.2f, .9f, .8f, .65f}, PreviewLayer);
            gizmos.line(project(followed_position, map, scale),
                        project(target, map, scale),
                        {.75f, .3f, 1.f, .8f}, PreviewLayer);
        }
    }
}

// One two-vertex tick per unit encodes both position and facing, which keeps
// twenty thousand units at a gizmo cost that does not hide the simulation.
void draw_squad_lite(EcsWorld& world, const SquadInfo& info,
                     const AoeLogicMap& map, float scale) {
    auto& gizmos = world.resource<Gizmos>();
    const auto view = world.reg().view<const AoePosition, const AoeDirection,
                                       const AoeCollider>();
    for (const auto unit : info.units) {
        if (!view.contains(unit)) continue;
        const auto position = view.get<const AoePosition>(unit).value;
        const auto direction = view.get<const AoeDirection>(unit).value;
        const auto& collider = view.get<const AoeCollider>(unit);
        const float length = std::max(collider.radius_x, collider.radius_y) * 2.f;
        gizmos.line(project(position - direction * length * .5f, map, scale),
                    project(position + direction * length * .5f, map, scale),
                    {.15f, .75f, 1.f, 1.f}, PreviewLayer);
    }
}

void draw_gizmos(EcsWorld& world) {
    const auto started = Clock::now();
    auto& state = world.resource<PreviewState>();
    auto& gizmos = world.resource<Gizmos>();
    gizmos.clear();
    const auto& map = world.resource<AoeLogicMap>();
    const float scale = state.scale;
    const glm::vec2 high{map.width() * map.tile_size(),
                         map.height() * map.tile_size()};
    const glm::vec2 corners[] = {{0.f, 0.f}, {high.x, 0.f}, high, {0.f, high.y}};
    for (int i = 0; i < 4; ++i)
        gizmos.line(project(corners[i], map, scale),
                    project(corners[(i + 1) % 4], map, scale),
                    {.45f, .5f, .58f, 1.f}, PreviewLayer);
    map.visit_static_obstacles([&](AoeObstacleId, const AoeStaticObstacleDesc& obstacle) {
        if (obstacle.shape == AoeStaticObstacleShape::Aabb)
            gizmos.wire_box(project(obstacle.center, map, scale),
                {obstacle.half_extents.x * scale, obstacle.half_extents.y * scale, 0.f},
                glm::mat3(1.f), {1.f, .3f, .12f, 1.f}, PreviewLayer);
        else
            gizmos.wire_ellipse(project(obstacle.center, map, scale),
                {obstacle.radius * scale, 0.f, 0.f},
                {0.f, obstacle.radius * scale, 0.f}, 32,
                {1.f, .3f, .12f, 1.f}, PreviewLayer);
    });
    for (const auto squad : state.squads) {
        if (!world.reg().valid(squad)) continue;
        const auto* info = world.reg().try_get<SquadInfo>(squad);
        if (!info) continue;
        if (state.config.detail == RenderDetail::Full)
            draw_squad_full(world, *info, map, scale);
        else if (state.config.detail == RenderDetail::Lite)
            draw_squad_lite(world, *info, map, scale);
        if (world.reg().valid(info->captain) &&
            world.reg().all_of<AoePosition>(info->captain)) {
            const auto captain_position =
                world.reg().get<AoePosition>(info->captain).value;
            if (state.config.detail != RenderDetail::Full)
                gizmos.cross(project(captain_position, map, scale), 5.f,
                             {.2f, 1.f, .3f, 1.f}, PreviewLayer);
            if (const auto* route =
                    world.reg().try_get<Aoe2xRoutePlan>(info->captain)) {
                glm::vec2 previous = captain_position;
                for (const auto waypoint : route->waypoints) {
                    gizmos.line(project(previous, map, scale),
                                project(waypoint, map, scale),
                                {1.f, .85f, .1f, 1.f}, PreviewLayer);
                    previous = waypoint;
                }
            }
        }
        const auto* order = world.reg().try_get<FormationAttackMove>(squad);
        if (order && order->status != FormationAttackMoveStatus::Idle)
            gizmos.cross(project(order->destination, map, scale), 7.f,
                         {1.f, .15f, .15f, 1.f}, PreviewLayer);
    }
    state.gizmo_ms = elapsed_ms(started);
}

// Sampling a handful of members per squad keeps the follow-error readout from
// becoming a hotspot of its own at twenty thousand units.
float sample_follow_error(EcsWorld& world, const SquadInfo& info) {
    constexpr std::size_t Samples = 8;
    if (info.units.size() < 2 || !world.reg().valid(info.captain) ||
        !world.reg().all_of<UnitFormationDirection, AoeDirection>(info.captain))
        return 0.f;
    const auto& captain_direction =
        world.reg().get<UnitFormationDirection>(info.captain);
    const auto current = world.reg().get<AoeDirection>(info.captain).value;
    const std::size_t stride =
        std::max<std::size_t>(1u, (info.units.size() - 1u) / Samples);
    float maximum = 0.f;
    for (std::size_t i = 1; i < info.units.size(); i += stride) {
        const auto unit = info.units[i];
        if (!world.reg().valid(unit) ||
            !world.reg().all_of<AoePosition, UnitSquadInfo>(unit))
            continue;
        const auto& member = world.reg().get<UnitSquadInfo>(unit);
        if (member.followed == entt::null ||
            !world.reg().valid(member.followed) ||
            !world.reg().all_of<AoePosition>(member.followed))
            continue;
        const glm::vec2 actual =
            world.reg().get<AoePosition>(member.followed).value -
            world.reg().get<AoePosition>(unit).value;
        const glm::vec2 expected = rotate_between(
            member.followed_relative_to_self,
            captain_direction.original, current);
        maximum = std::max(maximum, glm::length(actual - expected));
    }
    return maximum;
}

SystemTiming read_timing(
    const Aoe2xGameplaySystemRegistry& registry, std::string_view name) {
    const auto* descriptor = registry.find(name);
    if (!descriptor) return {};
    return {descriptor->timing.average_ms.value_or(0.0),
            descriptor->timing.peak_ms.value_or(0.0)};
}

Aoe2xPathfindingDiagnostics diagnostics_delta(
    const Aoe2xPathfindingDiagnostics& current,
    const Aoe2xPathfindingDiagnostics& previous) {
    return {current.queries - previous.queries,
            current.direct_paths - previous.direct_paths,
            current.cache_rebuilds - previous.cache_rebuilds,
            current.high_level_expanded - previous.high_level_expanded,
            current.local_expanded - previous.local_expanded,
            current.no_paths - previous.no_paths,
            current.waypoints_before_smoothing -
                previous.waypoints_before_smoothing,
            current.waypoints_after_smoothing -
                previous.waypoints_after_smoothing};
}

void refresh_snapshot(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    auto& window = state.window;
    auto& snapshot = state.snapshot;
    const double frames = std::max(1u, window.frames);
    snapshot.fps = world.resource<Time>().fps;
    snapshot.frame_ms = window.elapsed * 1000.0 / frames;
    snapshot.sim_ms = window.sim_ms / frames;
    snapshot.gizmo_ms = window.gizmo_ms / frames;
    snapshot.hud_ms = window.hud_ms / frames;
    snapshot.gizmo_vertices = world.resource<Gizmos>().vertex_count(PreviewLayer);
    auto& registry = world.resource_or_add<Aoe2xGameplaySystemRegistry>();
    snapshot.spawn = read_timing(registry, SpawnFormationSystem::name);
    snapshot.command = read_timing(registry, FormationCommandSystem::name);
    snapshot.pathfinding = read_timing(registry, Aoe2xPathfindingSystem::name);
    snapshot.formation = read_timing(registry, FormationSystem::name);
    registry.reset_timing();
    snapshot.totals = world.resource_or_add<Aoe2xPathfindingDiagnostics>();
    snapshot.window = diagnostics_delta(snapshot.totals, window.previous);
    window.previous = snapshot.totals;
    float follow_error = 0.f;
    for (const auto squad : state.squads) {
        if (!world.reg().valid(squad)) continue;
        if (const auto* info = world.reg().try_get<SquadInfo>(squad))
            follow_error = std::max(follow_error,
                sample_follow_error(world, *info));
    }
    snapshot.follow_error = follow_error;
    window = ProfileWindow{};
    window.previous = snapshot.totals;
}

void write_hud_text(EcsWorld& world) {    auto& state = world.resource<PreviewState>();
    const auto& snapshot = state.snapshot;
    const auto& config = state.config;
    const auto& window = snapshot.window;
    char value[1024];
    std::snprintf(value, sizeof(value),
        "preset %u | units %u | squads %u (%u each) | map %ux%u | detail %s\n"
        "running %u/%u | march legs %u | fps %.1f | frame %.2f ms | "
        "sim %.2f ms | gizmo %.2f ms (%zu verts) | hud %.2f ms\n"
        "spawn %.3f/%.3f | command %.3f/%.3f | path %.3f/%.3f | "
        "formation %.3f/%.3f  (avg/peak ms)\n"
        "path total: queries %llu, direct %llu, no-path %llu, rebuilds %llu\n"
        "path window: queries %llu, hi-expand %llu, local-expand %llu, "
        "waypoints %llu -> %llu\n"
        "follow error (sampled) %.3f | alive %u | killed %u (captains %u)\n"
        "[G] cycle detail  [K] kill a unit  [C] kill a captain  "
        "[right click] move  [Esc] exit",
        config.preset, config.total_units, config.squad_count, config.squad_size,
        config.map_width, config.map_height, detail_name(config.detail),
        state.running_squads, config.squad_count, state.march_legs,
        snapshot.fps, snapshot.frame_ms, snapshot.sim_ms, snapshot.gizmo_ms,
        snapshot.gizmo_vertices, snapshot.hud_ms,
        snapshot.spawn.average_ms, snapshot.spawn.peak_ms,
        snapshot.command.average_ms, snapshot.command.peak_ms,
        snapshot.pathfinding.average_ms, snapshot.pathfinding.peak_ms,
        snapshot.formation.average_ms, snapshot.formation.peak_ms,
        static_cast<unsigned long long>(snapshot.totals.queries),
        static_cast<unsigned long long>(snapshot.totals.direct_paths),
        static_cast<unsigned long long>(snapshot.totals.no_paths),
        static_cast<unsigned long long>(snapshot.totals.cache_rebuilds),
        static_cast<unsigned long long>(window.queries),
        static_cast<unsigned long long>(window.high_level_expanded),
        static_cast<unsigned long long>(window.local_expanded),
        static_cast<unsigned long long>(window.waypoints_before_smoothing),
        static_cast<unsigned long long>(window.waypoints_after_smoothing),
        snapshot.follow_error, count_alive(world, state),
        state.killed_units, state.killed_captains);
    auto& text = world.reg().get<Text>(state.hud);
    text.text = ascii_to_u32(value);
    ++text.rev;
    if (config.profile_seconds > 0.0)
        std::printf("%s\n\n", value);
}

void update_hud(EcsWorld& world) {
    const auto started = Clock::now();
    auto& state = world.resource<PreviewState>();
    if (!world.reg().valid(state.hud)) return;
    auto& window = state.window;
    ++window.frames;
    window.elapsed += world.resource<Time>().raw_dt;
    window.sim_ms += state.sim_ms;
    window.gizmo_ms += state.gizmo_ms;
    window.hud_ms += state.hud_ms;
    if (window.elapsed >= ProfileWindowSeconds) {
        refresh_snapshot(world);
        write_hud_text(world);
    }
    if (state.config.profile_seconds > 0.0) {
        state.run_seconds += world.resource<Time>().raw_dt;
        if (state.run_seconds >= state.config.profile_seconds)
            world.resource<Window>().should_close = true;
    }
    state.hud_ms = elapsed_ms(started);
}
} // namespace

int main() {
    const auto root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);
    App app;
    // Vsync would cap the harness well below the rate the simulation can
    // actually sustain, so the stress preview always presents unthrottled.
    app.add_plugin(WindowPlugin{1600, 1000, "AoE2x Formation Preview", false});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin); app.add_plugin(CorePlugin); app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin); app.add_plugin(GizmoPlugin); app.add_plugin(RenderPlugin);
    app.add_plugin(TextPlugin); TextBatchPlugin(app);
    app.world.add_resource<PreviewState>();
    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto& state = world.resource<PreviewState>();
        state.config = read_config();
        const auto& config = state.config;
        world.add_resource<AoeLogicMap>(
            make_map(config.map_width, config.map_height));
        world.add_resource<Aoe2xPathfindingSettings>(
            Aoe2xPathfindingSettings{8, true, true});
        world.resource_or_add<FormationRegistry>()
            .bind<FormationType::CompactSquare, CompactSquareFormation>();
        register_aoe2x_gameplay_system<SpawnFormationSystem>(world);
        register_aoe2x_gameplay_system<FormationCommandSystem>(world);
        register_aoe2x_gameplay_system<Aoe2xPathfindingSystem>(world);
        register_aoe2x_gameplay_system<FormationSystem>(world);
        register_aoe2x_gameplay_system<Aoe2xUnitLifecycleSystem>(world);
        auto& navigation = world.resource_or_add<AoeNavigationSettings>();
        navigation.steering_max_acceleration = 7.f;
        const auto& window = world.resource<Window>();
        state.scale = std::min(
            window.width / static_cast<float>(config.map_width),
            (window.height - 150.f) / static_cast<float>(config.map_height));
        const auto centers = squad_centers(config);
        state.squads.reserve(centers.size());
        for (const auto center : centers) {
            FormationSpawnOptions options;
            options.count = config.squad_size; options.center = center;
            options.spacing = UnitSpacing; options.unit_radius = UnitRadius;
            options.movement_speed = 0.5f;
            state.squads.push_back(spawn_aoe2x_formation(world, options));
        }
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
        text.size = 18; text.color = {.92f, .97f, 1.f, 1.f};
        text.leading = 3.f;
        text.align = TextAlign::Left; text.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(hud, std::move(text));
        world.reg().emplace<Transform>(hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(hud, RenderLayer{HudLayer});
        state.hud = hud;
        std::printf("[aoe2x] preset %u: %u units in %u squads of %u, map %ux%u,"
                    " detail %s\n",
                    config.preset, config.total_units, config.squad_count,
                    config.squad_size, config.map_width, config.map_height,
                    detail_name(config.detail));
        std::puts("[aoe2x] env: GLD_AOE2X_STRESS_PRESET(1..5), "
                  "GLD_AOE2X_SQUAD_SIZE, GLD_AOE2X_RENDER_DETAIL(full|lite|off), "
                  "GLD_AOE2X_PROFILE_SECONDS, GLD_AOE2X_ATTRITION");
    });
    app.add_system(Stage::PreUpdate, input_system);
    app.add_system(Stage::PreUpdate, simulation_system);
    app.add_system(Stage::PostUpdate, draw_gizmos);
    app.add_system(Stage::PostUpdate, update_hud);
    run_app(app);
}
