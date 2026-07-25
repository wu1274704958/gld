#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <ecs/App.hpp>
#include <ecs/Components.hpp>
#include <ecs/Input.hpp>
#include <ecs/Window.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Gizmo.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>
#include <aoe2/Aoe2Plugin.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe2;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t PreviewLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;

struct Aoe2IsoProjectionSettings {
    float tile_width_px = 96.f;
    float tile_height_px = 48.f;
    float elevation_px_per_unit = 48.f;
    // SLD direction 0 faces opposite the raw DAT +Y spawn-offset axis.
    float direction_zero_angle_bias_radians = glm::pi<float>();
    float aim_direction_length_units = 0.75f;
    int ellipse_segments = 48;
};

struct PreviewState {
    std::size_t unit_index = 0;
    std::size_t animation_index = 0;
    int direction = 0;
    bool playing = true;
    entt::entity unit = entt::null;
    entt::entity hud = entt::null;
};

std::u32string ascii_to_u32(const std::string& value) {
    std::u32string result;
    result.reserve(value.size());
    for (unsigned char c : value) result.push_back(static_cast<char32_t>(c));
    return result;
}

const UnitRecord* selected_record(EcsWorld& world, PreviewState& state) {
    const auto& units = world.resource<Aoe2ResourceManager>().list_units();
    if (units.empty()) return nullptr;
    state.unit_index %= units.size();
    return &units[state.unit_index];
}

void select_preferred_animation(PreviewState& state, const UnitRecord& record) {
    state.animation_index = 0;
    for (const char* wanted : {"attackA", "idleA"}) {
        for (std::size_t i = 0; i < record.animations.size(); ++i) {
            if (record.animations[i] == wanted) {
                state.animation_index = i;
                return;
            }
        }
    }
}

std::string selected_animation(const UnitRecord& record, PreviewState& state) {
    if (record.animations.empty()) return {};
    state.animation_index %= record.animations.size();
    return record.animations[state.animation_index];
}

void respawn(EcsWorld& world, PreviewState& state) {
    if (state.unit != entt::null && world.reg().valid(state.unit))
        world.reg().destroy(state.unit);
    state.unit = entt::null;
    const auto* record = selected_record(world, state);
    if (!record || record->animations.empty()) return;
    SpawnOptions options;
    options.unit_id = record->id;
    options.animation = selected_animation(*record, state);
    options.direction = state.direction;
    options.direction_slot_count = 16;
    options.player_color = 1;
    options.playing = state.playing;
    options.layers = PreviewLayer;
    state.unit = spawn_aoe2_unit(world, options,
        Transform::from_trs({0.f, -40.f, 0.f}));
}

glm::vec3 rotate_dat_z(glm::vec3 point, float angle) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    return {c * point.x - s * point.y, s * point.x + c * point.y, point.z};
}

glm::vec3 project_dat(glm::vec3 point, const Aoe2IsoProjectionSettings& settings) {
    // The documented AoE formula produces screen coordinates with Y down.
    // GLD's orthographic camera uses world Y up, so negate screen_y here.
    return {
        (point.x - point.y) * settings.tile_width_px * 0.5f,
        (point.x + point.y) * settings.tile_height_px * 0.5f +
            point.z * settings.elevation_px_per_unit - 40.f,
        0.f
    };
}

void submit_metadata_gizmos(EcsWorld& world) {
    auto& gizmos = world.resource<Gizmos>();
    auto& state = world.resource<PreviewState>();
    if (state.unit == entt::null || !world.reg().valid(state.unit)) return;
    auto* render = world.reg().try_get<Aoe2UnitRender>(state.unit);
    const auto* appearance = render ? render->appearance.get() : nullptr;

    const glm::vec3 foot = project_dat(glm::vec3(0.f),
        world.resource<Aoe2IsoProjectionSettings>());
    gizmos.cross(foot, 8.f, {1.f, 1.f, 1.f, 1.f}, PreviewLayer);
    if (!appearance || !appearance->dat_metadata) return;

    const auto& settings = world.resource<Aoe2IsoProjectionSettings>();
    const auto& metadata = *appearance->dat_metadata;
    const float angle = settings.direction_zero_angle_bias_radians -
        glm::two_pi<float>() * static_cast<float>(state.direction) / 16.f;
    // DAT X/Y are footprint radii while Z is the full collision height.
    const glm::vec3 collision = metadata.collision_size;
    const float half_height = collision.z * 0.5f;
    const glm::vec3 center_dat{0.f, 0.f, half_height};
    const glm::vec3 center = project_dat(center_dat, settings);
    const glm::vec3 axis_x = project_dat(
        rotate_dat_z({collision.x, 0.f, 0.f}, angle), settings) - foot;
    const glm::vec3 axis_y = project_dat(
        rotate_dat_z({0.f, collision.y, 0.f}, angle), settings) - foot;
    const glm::vec3 axis_z = project_dat({0.f, 0.f, half_height}, settings) - foot;
    const glm::vec4 green{0.2f, 1.f, 0.3f, 0.9f};
    gizmos.wire_ellipse(center, axis_x, axis_y, settings.ellipse_segments, green, PreviewLayer);
    gizmos.wire_ellipse(center, axis_x, axis_z, settings.ellipse_segments, green, PreviewLayer);
    gizmos.wire_ellipse(center, axis_y, axis_z, settings.ellipse_segments, green, PreviewLayer);

    if (metadata.outline_size) {
        const glm::vec3 outline = *metadata.outline_size;
        const float outline_half_height = outline.z * 0.5f;
        const glm::vec3 outline_center = project_dat(
            {0.f, 0.f, outline_half_height}, settings);
        const glm::vec3 outline_axis_x = project_dat(
            rotate_dat_z({outline.x, 0.f, 0.f}, angle), settings) - foot;
        const glm::vec3 outline_axis_y = project_dat(
            rotate_dat_z({0.f, outline.y, 0.f}, angle), settings) - foot;
        const glm::vec3 outline_axis_z = project_dat(
            {0.f, 0.f, outline_half_height}, settings) - foot;
        const glm::vec4 magenta{1.f, 0.25f, 0.9f, 0.7f};
        gizmos.wire_ellipse(outline_center, outline_axis_x, outline_axis_y,
            settings.ellipse_segments, magenta, PreviewLayer);
        gizmos.wire_ellipse(outline_center, outline_axis_x, outline_axis_z,
            settings.ellipse_segments, magenta, PreviewLayer);
        gizmos.wire_ellipse(outline_center, outline_axis_y, outline_axis_z,
            settings.ellipse_segments, magenta, PreviewLayer);
    }

    const glm::vec3 target = project_dat({0.f, 0.f, half_height}, settings);
    gizmos.cross(target, 6.f, {0.1f, 1.f, 1.f, 1.f}, PreviewLayer);
    if (!metadata.combat) return;
    // A type-50 Combat block also exists on melee units.  Its -1 projectile
    // sentinel means there is no projectile instance or launch point to show.
    if (metadata.combat->projectile_unit_id < 0) return;
    const auto* animation = appearance->animation_at(render->animation_slot);
    const bool release_frame_in_range = animation &&
        metadata.combat->frame_delay < animation->frames_per_direction;
    const bool release_now = render->animation == "attackA" &&
        release_frame_in_range && render->current_frame == metadata.combat->frame_delay;
    const float launch_alpha = release_now ? 1.f : 0.25f;
    const glm::vec3 weapon_dat = rotate_dat_z(metadata.combat->weapon_offset, angle);
    const glm::vec3 projectile = project_dat(weapon_dat, settings);
    // This is an offset vector, not the direction in which the projectile flies.
    gizmos.line(foot, projectile, {1.f, 0.85f, 0.05f, launch_alpha}, PreviewLayer);
    gizmos.cross(projectile, 7.f, {1.f, 0.15f, 0.1f, launch_alpha}, PreviewLayer);

    // DAT has no per-frame aim socket/direction.  Show a short, explicitly
    // derived facing vector separately from the static graphic displacement.
    const glm::vec3 facing_dat = rotate_dat_z(
        {0.f, settings.aim_direction_length_units, 0.f}, angle);
    const glm::vec3 aim_end = project_dat(weapon_dat + facing_dat, settings);
    gizmos.arrow(projectile, aim_end, 10.f, {1.f, 0.5f, 0.05f, 0.9f}, PreviewLayer);
}

void input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    auto& state = world.resource<PreviewState>();
    if (!keyboard) return;
    if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    auto& manager = world.resource<Aoe2ResourceManager>();
    const auto count = manager.list_units().size();
    if (count && (keyboard->just_now_pressed(GLFW_KEY_LEFT) ||
                  keyboard->just_now_pressed(GLFW_KEY_RIGHT))) {
        state.unit_index = keyboard->just_now_pressed(GLFW_KEY_LEFT)
            ? (state.unit_index + count - 1) % count : (state.unit_index + 1) % count;
        select_preferred_animation(state, manager.list_units()[state.unit_index]);
        respawn(world, state);
    }
    if (const auto* record = selected_record(world, state); record && !record->animations.empty()) {
        if (keyboard->just_now_pressed(GLFW_KEY_UP)) {
            state.animation_index = (state.animation_index + record->animations.size() - 1) %
                                    record->animations.size();
            request_aoe2_animation(world, state.unit, selected_animation(*record, state));
        }
        if (keyboard->just_now_pressed(GLFW_KEY_DOWN)) {
            state.animation_index = (state.animation_index + 1) % record->animations.size();
            request_aoe2_animation(world, state.unit, selected_animation(*record, state));
        }
    }
    if (keyboard->just_now_pressed(GLFW_KEY_A) || keyboard->just_now_pressed(GLFW_KEY_D)) {
        state.direction = keyboard->just_now_pressed(GLFW_KEY_A)
            ? (state.direction + 15) % 16 : (state.direction + 1) % 16;
        set_aoe2_direction(world, state.unit, state.direction, 16);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_SPACE)) {
        state.playing = !state.playing;
        set_aoe2_playing(world, state.unit, state.playing);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_F5)) {
        std::string old_id;
        if (const auto* old = selected_record(world, state)) old_id = old->id;
        manager.refresh();
        state.unit_index = 0;
        for (std::size_t i = 0; i < manager.list_units().size(); ++i)
            if (manager.list_units()[i].id == old_id) state.unit_index = i;
        if (const auto* record = selected_record(world, state))
            select_preferred_animation(state, *record);
        respawn(world, state);
    }
}

void hud_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    if (state.hud == entt::null || !world.reg().valid(state.hud)) return;
    const auto* record = selected_record(world, state);
    auto* render = state.unit != entt::null && world.reg().valid(state.unit)
        ? world.reg().try_get<Aoe2UnitRender>(state.unit) : nullptr;
    const auto* appearance = render ? render->appearance.get() : nullptr;
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "AoE2 DAT metadata preview\n"
        << "Resource: " << (record ? record->id : "<none>")
        << "   schema: " << (record ? record->schema_version : 0)
        << "   animation: " << (render ? render->animation : "<loading>") << "\n"
        << "Direction: " << state.direction << "/15   playing: "
        << (state.playing ? "yes" : "no") << "\n";
    if (!appearance || !appearance->dat_metadata) {
        out << "DAT metadata unavailable\n"
            << "Only the white SLD foot/gameplay origin marker is shown.";
    } else {
        const auto& dat = *appearance->dat_metadata;
        const auto* active_animation = appearance->animation_at(render->animation_slot);
        out << "Mapping: " << dat.mapping_source << "   civ/unit: "
            << dat.civ_id << "/" << dat.unit_id << "   type: " << dat.unit_type << "\n"
            << "Gameplay collision radius X/Y, height Z: " << dat.collision_size.x << ", "
            << dat.collision_size.y << ", " << dat.collision_size.z << "\n";
        if (dat.outline_size) {
            out << "Selection outline radius X/Y, height Z: "
                << dat.outline_size->x << ", " << dat.outline_size->y << ", "
                << dat.outline_size->z << "\n";
        } else {
            out << "DAT selection outline unavailable\n";
        }
        if (!dat.combat) {
            out << "Combat metadata unavailable";
        } else {
            const auto& combat = *dat.combat;
            out << "Weapon offset XYZ: " << combat.weapon_offset.x << ", "
                << combat.weapon_offset.y << ", " << combat.weapon_offset.z << "\n"
                << "Projectile IDs: " << combat.projectile_unit_id << " / "
                << (combat.secondary_projectile_unit_id
                    ? std::to_string(*combat.secondary_projectile_unit_id) : "n/a")
                << "   frame delay: " << combat.frame_delay << "\n"
                << "Current frame: " << render->current_frame << "/"
                << (active_animation ? active_animation->frames_per_direction - 1 : 0);
            const bool release_in_range = active_animation &&
                combat.frame_delay < active_animation->frames_per_direction;
            if (!release_in_range) {
                out << "   release frame: out of range\n";
            } else {
                const bool release_now = render->animation == "attackA" &&
                    render->current_frame == combat.frame_delay;
                out << "   release frame now: " << (release_now ? "yes" : "no") << "\n";
            }
            if (render->animation != "attackA")
                out << "Release timing applies to DAT attackA graphic only\n";
            out
                << "Range: " << combat.min_range << ".." << combat.max_range
                << "   accuracy: " << combat.accuracy_percent << "% +/-"
                << combat.accuracy_dispersion << "   reload: " << combat.reload_time << "\n"
                << "Blast width/level: " << combat.blast_width << "/"
                << combat.blast_attack_level << "   attack graphic: "
                << combat.attack_graphic_id << "\n";
            if (combat.projectile_spawning_area) {
                const auto area = *combat.projectile_spawning_area;
                out << "Projectile count: " << *combat.projectile_min_count << ".."
                    << *combat.projectile_max_count << "   spawn W/L/random: "
                    << area.x << "/" << area.y << "/" << area.z << "\n";
            }
            if (combat.projectile_unit_id < 0) {
                out << "No primary projectile; launch gizmos hidden\n";
            } else {
                out << "Yellow: static foot-to-spawn offset; red: spawn point\n"
                    << "Orange: derived facing direction (not a DAT trajectory)\n";
            }
            out << "Cyan: derived target center (not a DAT impact socket)";
        }
    }
    auto& text = world.reg().get<Text>(state.hud);
    text.text = ascii_to_u32(out.str());
    ++text.rev;
}
} // namespace

int main() {
    const fs::path root = wws::find_path(3, "res", true);
    if (root.empty()) {
        std::fprintf(stderr, "[aoe2_unit_metadata_preview] cannot locate res directory\n");
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{1280, 800, "AoE2 Unit Metadata Preview"});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(Aoe2Plugin{"aoe2de_cache"});
    app.add_plugin(GizmoPlugin);
    app.add_plugin(RenderPlugin);
    app.world.add_resource<PreviewState>();
    app.world.add_resource<Aoe2IsoProjectionSettings>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = PreviewLayer;
        camera.clear_color = {0.12f, 0.14f, 0.17f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        auto& passes = emplace_registered_render_passes(world, camera_entity);
        passes.add(Aoe2UnitPassId);
        auto& gizmo_pass = passes.add(GizmoPassId);
        gizmo_pass.state.depth_test = RenderStateValue::Disabled;
        gizmo_pass.state.depth_write = RenderStateValue::Disabled;
        gizmo_pass.state.blend = RenderStateValue::Enabled;
        gizmo_pass.state.blend_src = BlendFactor::SrcAlpha;
        gizmo_pass.state.blend_dst = BlendFactor::OneMinusSrcAlpha;

        auto hud_camera_entity = world.spawn();
        Camera hud_camera;
        hud_camera.kind = CameraKind::Ortho;
        hud_camera.priority = 20;
        hud_camera.layers = HudLayer;
        hud_camera.do_clear = false;
        world.reg().emplace<Camera>(hud_camera_entity, hud_camera);
        emplace_render_passes<BatchPass>(world, hud_camera_entity);

        auto& state = world.resource<PreviewState>();
        state.hud = world.spawn();
        Text text;
        text.text = U"Loading AoE2 DAT metadata...";
        text.font = world.resource<AssetServer>().load(
            FontDesc(std::string("fonts/AGENCYB.TTF"), 0));
        text.size = 20;
        text.color = {0.92f, 0.96f, 1.f, 1.f};
        text.align = TextAlign::Left;
        text.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(state.hud, std::move(text));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(state.hud, Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f}));
        world.reg().emplace<RenderLayer>(state.hud, RenderLayer{HudLayer});
        if (const auto* record = selected_record(world, state))
            select_preferred_animation(state, *record);
        respawn(world, state);
        std::printf("Controls: Left/Right unit, Up/Down animation, A/D facing, Space pause, F5 refresh, Escape quit\n");
    });
    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::PostUpdate, submit_metadata_gizmos);
    app.add_system(Stage::Last, hud_system);
    run_app(app);
    return 0;
}
