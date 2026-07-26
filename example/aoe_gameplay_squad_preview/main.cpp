#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
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
constexpr float TileWidth = 96.f;
constexpr float TileHeight = 48.f;
constexpr float SpriteScale = 1.15f;
constexpr float DepthUnitsPerTile = 1.f;
constexpr float ElevationPixelsPerUnit = 48.f;

struct PreviewState {
    entt::entity blue{entt::null};
    entt::entity red{entt::null};
    entt::entity hud{entt::null};
    bool orders_issued = false;
    bool draw_unit_feet = false;
    bool trace_unit_foot = false;
    entt::entity trace_entity{entt::null};
    std::uint64_t trace_instance_id = 0;
    int trace_direction = -1;
    glm::vec2 trace_raw_foot{0.f};
    bool trace_has_previous = false;
    std::string latest = "waiting for both squads";
    double hud_elapsed = 0.0;
};

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

glm::vec3 project_logical_position(glm::vec2 logical) {
    const float depth = logical.x + logical.y;
    return {(logical.x - logical.y) * TileWidth * .5f,
            -depth * TileHeight * .5f + 35.f,
            depth * DepthUnitsPerTile};
}

void projection_system(EcsWorld& world) {
    auto& reg = world.reg();
    const auto& clock = world.resource<AoeGameplayClock>();
    const auto& settings = world.resource<AoeGameplaySettings>();
    for (const auto entity : reg.view<AoePosition, Transform>(
             entt::exclude<AoePooledUnit>)) {
        const auto logical = aoe_interpolated_position(
            reg.get<AoePosition>(entity),
            reg.try_get<AoePositionHistory>(entity), clock, settings);
        const auto screen = project_logical_position(logical);
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(screen);
            transform.set_scale({SpriteScale, SpriteScale, 1.f});
        });
    }
    for (const auto entity : reg.view<AoeProjectile, Transform>()) {
        const auto& projectile = reg.get<AoeProjectile>(entity);
        auto screen = project_logical_position(
            {projectile.position.x, projectile.position.y});
        screen.y += projectile.position.z * ElevationPixelsPerUnit;
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
    AoeSquadSpawnOptions options;
    options.composition = {{"camel_scout", 3, color}, {"archer", 5, color}};
    options.center = center;
    options.forward = forward;
    options.team_id = team;
    options.player_color = color;
    options.layers = UnitLayer;
    options.formation_spacing = .7f;
    options.acquisition_radius = 6.f;
    return spawn_aoe_gameplay_squad(world, options);
}

void reset_scene(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    destroy_squad(world, state.blue);
    destroy_squad(world, state.red);
    state.blue = spawn_squad(world, {-6.f, 0.f}, {1.f, 0.f}, 1, 1);
    state.red = spawn_squad(world, {6.f, 0.f}, {-1.f, 0.f}, 2, 2);
    state.orders_issued = false;
    state.trace_entity = entt::null;
    state.trace_instance_id = 0;
    state.trace_direction = -1;
    state.trace_has_previous = false;
    state.latest = "spawned two mixed squads";
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
    const bool blue = request_aoe_squad_attack_move(world, state.blue, {8.f, 0.f});
    const bool red = request_aoe_squad_attack_move(world, state.red, {-8.f, 0.f});
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
    if (keyboard->just_now_pressed(GLFW_KEY_R)) reset_scene(world);
    if (keyboard->just_now_pressed(GLFW_KEY_F5)) {
        world.resource<AoeUnitDefinitionManager>().refresh();
        reset_scene(world);
        state->latest = "definitions rescanned and squads rebuilt";
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
        gizmos.cross(foot, 6.f, {1.f, 1.f, 1.f, 1.f}, UnitLayer);
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

std::string tags_text(const std::vector<std::string>& tags) {
    std::ostringstream out;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        if (i) out << ',';
        out << tags[i];
    }
    return tags.empty() ? "none" : out.str();
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
    out << " team=" << reg.get<AoeTeam>(squad).id
        << " spawn=" << spawn_name(spawn.status)
        << " phase=" << phase_name(state.phase)
        << " members=" << members.active.size() << '/' << spawn.requested << '\n'
        << "  center=(" << position.value.x << ',' << position.value.y << ')'
        << " forward=(" << formation.forward.x << ',' << formation.forward.y << ')'
        << " speed=" << state.movement_speed;
    if (order.type == AoeSquadOrderType::MoveTo ||
        order.type == AoeSquadOrderType::AttackMove)
        out << " destination=(" << order.destination.x << ',' << order.destination.y << ')';
    if (order.target.entity != entt::null)
        out << " target=" << static_cast<std::uint32_t>(order.target.entity)
            << ':' << order.target.instance_id;
    out << '\n';
    for (const auto& slot : formation.slots) {
        if (!reg.valid(slot.unit.entity) ||
            !reg.all_of<AoeGameplayIdentity, AoeUnitDefinitionRef, AoeHealth,
                        AoeSquadMember>(slot.unit.entity) ||
            reg.get<AoeGameplayIdentity>(slot.unit.entity).instance_id !=
                slot.unit.instance_id)
            continue;
        const auto* definition = reg.get<AoeUnitDefinitionRef>(slot.unit.entity).value.get();
        const auto& health = reg.get<AoeHealth>(slot.unit.entity);
        out << "    #" << reg.get<AoeSquadMember>(slot.unit.entity).ordinal
            << ' ' << (definition ? definition->id : "?")
            << " hp=" << health.current
            << " tags=" << (definition ? tags_text(definition->tags) : "?")
            << " priority=" << slot.priority
            << " slot=(" << slot.local_offset.x << ',' << slot.local_offset.y << ")\n";
    }
    for (const auto& error : spawn.errors) out << "    spawn error: " << error << '\n';
}

void diagnostics_system(EcsWorld& world) {
    auto& preview = world.resource<PreviewState>();
    preview.hud_elapsed += world.resource<Time>().raw_dt;
    if (preview.hud_elapsed < .1 || !world.reg().valid(preview.hud)) return;
    preview.hud_elapsed = 0.0;
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "AoE gameplay squad preview\n"
        << "Space AttackMove | S stop | P feet | L trace | R reset | F5 rescan | Esc quit\n"
        << "Foot markers: " << (preview.draw_unit_feet ? "ON" : "OFF")
        << " (white cross = rendered SLD foot/world origin)\n"
        << "Blue Archer trace: " << (preview.trace_unit_foot ? "ON" : "OFF")
        << " (console, every render frame)\n"
        << "Skirmish priority: cavalry 200 + scout 100; archer -100\n\n";
    append_squad(out, world, "BLUE", preview.blue);
    append_squad(out, world, "RED ", preview.red);
    const auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
    out << "\nattacks=" << diagnostics.attacks_started
        << " damage=" << diagnostics.damage_events
        << " projectiles=" << diagnostics.projectiles_spawned
        << " rejected=" << diagnostics.commands_rejected << '\n'
        << "latest: " << preview.latest;
    auto& text = world.reg().get<Text>(preview.hud);
    text.text = ascii_to_u32(out.str());
    ++text.rev;
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
    app.add_plugin(WindowPlugin{1440, 900, "AoE Gameplay Squad Preview"});
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
    app.world.add_resource<PreviewState>();

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
        preview.hud = world.spawn();
        Text hud;
        hud.text = U"Loading squad preview...";
        hud.font = world.resource<AssetServer>().load(
            FontDesc("fonts/AGENCYB.TTF", 0));
        hud.size = 17;
        hud.color = {.92f, .96f, 1.f, 1.f};
        hud.align = TextAlign::Left;
        hud.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(preview.hud, std::move(hud));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(preview.hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(preview.hud, RenderLayer{HudLayer});
        reset_scene(world);
        std::printf("Controls: Space mutual AttackMove, S stop, P unit feet, "
                    "L blue Archer foot trace, R reset, F5 rescan, Escape quit\n");
    });
    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::Update, projection_system);
    app.add_system(Stage::PostUpdate, submit_unit_foot_gizmos);
    app.add_system(Stage::Last, diagnostics_system);
    app.add_system(Stage::Last, trace_blue_archer_foot);
    run_app(app);
    return 0;
}
