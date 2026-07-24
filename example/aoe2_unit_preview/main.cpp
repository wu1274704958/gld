#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <glm/glm.hpp>

#include <ecs/App.hpp>
#include <ecs/Window.hpp>
#include <ecs/Input.hpp>
#include <ecs/Components.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <aoe2/Aoe2Plugin.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe2;
namespace fs = std::filesystem;

namespace {
constexpr std::uint32_t PreviewLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;

struct PreviewState {
    std::vector<entt::entity> units;
    std::size_t unit_index = 0;
    std::size_t animation_index = 0;
    int player_color = 1;
    int mask_debug = 0;
    bool playing = true;
    bool ready_reported = false;
    entt::entity hud_text = entt::null;
    double metric_seconds = 0.0;
    double hud_refresh_seconds = 0.0;
    double cpu_decode_ms = 0.0;
    double texture_upload_ms = 0.0;
    std::uint64_t texture_upload_bytes = 0;
    std::uint32_t texture_uploads = 0;
    std::uint64_t batch_upload_bytes = 0;
    std::uint64_t batch_uploads = 0;
    float last_cpu_decode_ms_per_sec = 0.f;
    float last_texture_upload_ms = 0.f;
    float last_texture_upload_mib = 0.f;
    std::uint32_t last_texture_uploads = 0;
    float last_batch_uploads_per_sec = 0.f;
    float last_batch_upload_kib_per_sec = 0.f;
    float observed_logic_fps = 0.f;
    std::uint64_t logic_advances = 0;
    std::int64_t observed_tick = -1;
    std::string observed_animation;
    std::string last_status;
};

std::u32string ascii_to_u32(const std::string& text) {
    std::u32string result;
    result.reserve(text.size());
    for (unsigned char c : text) result.push_back(static_cast<char32_t>(c));
    return result;
}

const UnitRecord* selected_unit(EcsWorld& world, PreviewState& state) {
    const auto& units = world.resource<Aoe2ResourceManager>().list_units();
    if (units.empty()) return nullptr;
    state.unit_index %= units.size();
    return &units[state.unit_index];
}

std::string selected_animation(const UnitRecord& unit, PreviewState& state) {
    if (unit.animations.empty()) return {};
    state.animation_index %= unit.animations.size();
    return unit.animations[state.animation_index];
}

void print_state(EcsWorld& world, PreviewState& state) {
    const auto* unit = selected_unit(world, state);
    if (!unit) {
        std::printf("[aoe2_unit_preview] no exported units under res/aoe2de_cache\n");
        return;
    }
    const auto animation = selected_animation(*unit, state);
    std::printf("[aoe2_unit_preview] unit=%s animation=%s player=%d mask-debug=%d warnings=%d\n",
        unit->id.c_str(), animation.c_str(), state.player_color, state.mask_debug, unit->warning_count);
    std::fflush(stdout);
}

void respawn_preview(EcsWorld& world, PreviewState& state) {
    auto& reg = world.reg();
    for (auto entity : state.units) if (reg.valid(entity)) reg.destroy(entity);
    state.units.clear();
    const auto* unit = selected_unit(world, state);
    if (!unit || unit->animations.empty()) { print_state(world, state); return; }
    const std::string animation = selected_animation(*unit, state);

    for (int direction = 0; direction < 16; ++direction) {
        const int row = direction / 4;
        const int column = direction % 4;
        Transform transform = Transform::from_trs({
            -420.f + static_cast<float>(column) * 280.f,
             245.f - static_cast<float>(row) * 165.f,
             0.f
        }, glm::vec3(0.f), {0.58f, 0.58f, 1.f});
        SpawnOptions options;
        options.unit_id = unit->id;
        options.animation = animation;
        options.direction = direction;
        options.direction_slot_count = 16;
        options.player_color = state.player_color;
        options.player_color_debug = state.mask_debug;
        options.playing = state.playing;
        options.layers = PreviewLayer;
        state.units.push_back(spawn_aoe2_unit(world, options, transform));
    }
    print_state(world, state);
}

void update_existing(EcsWorld& world, PreviewState& state, bool reset_time) {
    const auto* unit = selected_unit(world, state);
    if (!unit) return;
    const std::string animation = selected_animation(*unit, state);
    auto& reg = world.reg();
    for (auto entity : state.units) {
        if (!reg.valid(entity)) continue;
        if (auto* render = reg.try_get<Aoe2UnitRender>(entity)) {
            if ((render->animation != animation && render->pending_animation != animation) ||
                (render->animation == animation && !render->pending_animation.empty()))
                request_aoe2_animation(world, entity, animation);
            set_aoe2_player_color(world, entity, state.player_color, state.mask_debug);
            set_aoe2_playing(world, entity, state.playing);
            if (reset_time) restart_aoe2_animation(world, entity);
        } else if (auto* request = reg.try_get<Aoe2SpawnRequest>(entity)) {
            request->options.animation = animation;
            request->options.player_color = state.player_color;
            request->options.player_color_debug = state.mask_debug;
            request->options.playing = state.playing;
        }
    }
    print_state(world, state);
}

void preview_input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    auto* state = world.try_resource<PreviewState>();
    if (!keyboard || !state) return;
    auto& manager = world.resource<Aoe2ResourceManager>();
    const auto unit_count = manager.list_units().size();

    if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    if (unit_count && (keyboard->just_now_pressed(GLFW_KEY_LEFT) ||
                       keyboard->just_now_pressed(GLFW_KEY_RIGHT))) {
        std::string previous_animation;
        if (const auto* old_unit = selected_unit(world, *state))
            previous_animation = selected_animation(*old_unit, *state);
        if (keyboard->just_now_pressed(GLFW_KEY_LEFT))
            state->unit_index = (state->unit_index + unit_count - 1) % unit_count;
        else state->unit_index = (state->unit_index + 1) % unit_count;
        state->animation_index = 0;
        if (const auto* new_unit = selected_unit(world, *state)) {
            bool selected = false;
            for (std::size_t i = 0; i < new_unit->animations.size(); ++i) {
                if (new_unit->animations[i] == previous_animation) {
                    state->animation_index = i;
                    selected = true;
                    break;
                }
            }
            if (!selected) {
                for (std::size_t i = 0; i < new_unit->animations.size(); ++i)
                    if (new_unit->animations[i] == "idleA") { state->animation_index = i; break; }
            }
        }
        respawn_preview(world, *state);
    }
    if (const auto* unit = selected_unit(world, *state); unit && !unit->animations.empty()) {
        if (keyboard->just_now_pressed(GLFW_KEY_UP)) {
            state->animation_index = (state->animation_index + unit->animations.size() - 1)
                % unit->animations.size();
            update_existing(world, *state, false);
        }
        if (keyboard->just_now_pressed(GLFW_KEY_DOWN)) {
            state->animation_index = (state->animation_index + 1) % unit->animations.size();
            update_existing(world, *state, false);
        }
    }
    if (keyboard->just_now_pressed(GLFW_KEY_Q)) {
        state->player_color = state->player_color == 1 ? 8 : state->player_color - 1;
        update_existing(world, *state, false);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_E)) {
        state->player_color = state->player_color == 8 ? 1 : state->player_color + 1;
        update_existing(world, *state, false);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_SPACE)) {
        state->playing = !state->playing;
        update_existing(world, *state, false);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_R)) update_existing(world, *state, true);
    if (keyboard->just_now_pressed(GLFW_KEY_M)) {
        state->mask_debug = (state->mask_debug + 1) % 3;
        update_existing(world, *state, false);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_F5)) {
        std::string old_id;
        if (const auto* unit = selected_unit(world, *state)) old_id = unit->id;
        manager.refresh();
        state->unit_index = 0;
        const auto& units = manager.list_units();
        for (std::size_t i = 0; i < units.size(); ++i)
            if (units[i].id == old_id) { state->unit_index = i; break; }
        respawn_preview(world, *state);
    }
}

void preview_diagnostics_system(EcsWorld& world) {
    auto& state = world.resource<PreviewState>();
    const auto& time = world.resource<Time>();
    const auto& asset_diag = world.resource<AssetServerDiagnostics>();
    const auto& render_diag = world.resource_or_add<RenderDiagnostics>();
    const auto& aoe_diag = world.resource_or_add<Aoe2PerformanceDiagnostics>();
    state.metric_seconds += time.raw_dt;
    state.hud_refresh_seconds += time.raw_dt;
    state.cpu_decode_ms += asset_diag.cpu_load_ms;
    state.texture_upload_ms += asset_diag.texture_upload_ms_this_frame;
    state.texture_upload_bytes += asset_diag.texture_upload_bytes_this_frame;
    state.texture_uploads += asset_diag.texture_uploads_this_frame;
    state.batch_upload_bytes += render_diag.batch_upload_bytes;
    state.batch_uploads += render_diag.batch_uploads;

    std::size_t rendered = 0;
    for ([[maybe_unused]] auto entity : world.reg().view<Aoe2UnitRender>()) ++rendered;
    std::size_t instances = 0;
    bool has_player_batch = false;
    for (auto entity : world.reg().view<Aoe2BatchComponent>()) {
        const auto& batch = world.reg().get<Aoe2BatchComponent>(entity);
        instances += batch.world_instances.size();
        has_player_batch = has_player_batch || batch.key.texture_count == 3;
    }
    if (rendered == 16 && instances >= 16 && has_player_batch) {
        if (!state.ready_reported) {
            state.ready_reported = true;
            std::printf("[aoe2_unit_preview] ready: units=%zu batch-instances=%zu\n",
                        rendered, instances);
            std::fflush(stdout);
        }
    }

    Aoe2UnitRender* representative = nullptr;
    for (auto entity : state.units) {
        if (world.reg().valid(entity)) {
            representative = world.reg().try_get<Aoe2UnitRender>(entity);
            if (representative) break;
        }
    }
    const Animation* active_animation = nullptr;
    Aoe2UnitAppearance* appearance = representative ? representative->appearance.get() : nullptr;
    if (appearance && representative)
        active_animation = appearance->find_animation(representative->animation);

    if (representative && active_animation) {
        const auto raw_tick = static_cast<std::int64_t>(std::floor(
            std::max(0.f, representative->playback_time) * active_animation->fps));
        const auto logical_tick = representative->loop ? raw_tick
            : std::min<std::int64_t>(raw_tick, active_animation->frames_per_direction - 1);
        if (state.observed_animation != representative->animation) {
            state.observed_animation = representative->animation;
            state.observed_tick = logical_tick;
        } else if (state.observed_tick >= 0 && logical_tick >= state.observed_tick) {
            state.logic_advances += static_cast<std::uint64_t>(logical_tick - state.observed_tick);
            state.observed_tick = logical_tick;
        } else {
            // Restart (or another explicit time reset) establishes a new
            // baseline and must not look like a huge wrapped frame advance.
            state.observed_tick = logical_tick;
        }
    }

    if (state.metric_seconds >= 1.0) {
        state.observed_logic_fps = static_cast<float>(state.logic_advances / state.metric_seconds);
        state.last_cpu_decode_ms_per_sec = static_cast<float>(state.cpu_decode_ms / state.metric_seconds);
        state.last_texture_upload_ms = static_cast<float>(state.texture_upload_ms);
        state.last_texture_upload_mib = static_cast<float>(state.texture_upload_bytes / (1024.0 * 1024.0));
        state.last_texture_uploads = state.texture_uploads;
        state.last_batch_uploads_per_sec = static_cast<float>(
            state.batch_uploads / state.metric_seconds);
        state.last_batch_upload_kib_per_sec = static_cast<float>(
            state.batch_upload_bytes / 1024.0 / state.metric_seconds);
        state.metric_seconds = 0.0;
        state.logic_advances = 0;
        state.cpu_decode_ms = 0.0;
        state.texture_upload_ms = 0.0;
        state.texture_upload_bytes = 0;
        state.texture_uploads = 0;
        state.batch_upload_bytes = 0;
        state.batch_uploads = 0;
    }

    std::size_t frozen = 0, failed = 0;
    for (auto entity : world.reg().view<Aoe2UnitRender>()) {
        const auto& unit = world.reg().get<Aoe2UnitRender>(entity);
        if (unit.transition != AnimationTransitionState::None) ++frozen;
        if (unit.transition == AnimationTransitionState::Failed) ++failed;
    }
    std::size_t resident_animations = 0;
    std::uint64_t resident_bytes = 0;
    if (appearance) {
        for (const auto& animation : appearance->animations) {
            if (animation.residency != AnimationResidencyState::Ready) continue;
            ++resident_animations;
            auto add_layer = [&](const Layer& layer, std::uint64_t bytes_per_pixel) {
                if (layer.usable()) resident_bytes += static_cast<std::uint64_t>(layer.atlas_width)
                    * static_cast<std::uint64_t>(layer.atlas_height) * bytes_per_pixel;
            };
            add_layer(animation.main, 4u);         // RGBA8
            add_layer(animation.shadow, 1u);       // R8, source R
            add_layer(animation.player_color, 2u); // RG8, source R+A
        }
    }

    std::string transition = "None";
    if (representative && representative->transition == AnimationTransitionState::Waiting)
        transition = "Loading/Frozen";
    else if (representative && representative->transition == AnimationTransitionState::Failed)
        transition = "Failed/Frozen";
    const std::string active_name = representative && !representative->animation.empty()
        ? representative->animation : "<none>";
    const std::string pending_name = representative && !representative->pending_animation.empty()
        ? representative->pending_animation : "-";
    const std::string status = active_name + "|" + pending_name + "|" + transition;
    const bool status_changed = status != state.last_status;
    if (status_changed) {
        std::printf("[aoe2_unit_preview] transition: active=%s pending=%s state=%s\n",
                    active_name.c_str(), pending_name.c_str(), transition.c_str());
        std::fflush(stdout);
    }

    if ((state.hud_refresh_seconds >= 0.5 || status_changed) &&
        state.hud_text != entt::null && world.reg().valid(state.hud_text)) {
        state.hud_refresh_seconds = 0.0;
        state.last_status = status;
        std::ostringstream out;
        out << std::fixed << std::setprecision(1)
            << "FPS: " << time.fps << "   dt: " << time.dt * 1000.f
            << " ms   raw: " << time.raw_dt * 1000.f << " ms\n"
            << "Unit: " << (appearance ? appearance->id : "<loading>") << "\n"
            << "Active: " << active_name << "   configured: "
            << (active_animation ? active_animation->fps : 0.f) << " fps\n"
            << "Frame: " << (representative ? representative->current_frame : 0) << "/"
            << (active_animation ? active_animation->frames_per_direction : 0)
            << "   observed logic: " << state.observed_logic_fps << " fps\n"
            << "Pending: " << pending_name << " [" << transition << "]\n"
            << "Units: " << rendered << "   frozen: " << frozen << "   failed: " << failed << "\n"
            << "Asset IO: queued " << asset_diag.io_queued << "   active " << asset_diag.io_active
            << "   finalize " << asset_diag.finalize_queued << "\n"
            << "CPU decode: " << state.last_cpu_decode_ms_per_sec << " ms/s\n"
            << "Texture upload: " << state.last_texture_uploads << " tex   "
            << state.last_texture_upload_mib << " MiB   "
            << state.last_texture_upload_ms << " ms\n"
            << "Batches: " << render_diag.batch_groups << "   draws: " << render_diag.batch_draws
            << "   instances: " << render_diag.batch_instances << "\n"
            << "AoE GPU: " << aoe_diag.render_gpu_ms << " ms   query skips: "
            << aoe_diag.render_gpu_query_skips << "\n"
            << "Batch upload rate: " << state.last_batch_uploads_per_sec << "/s   "
            << state.last_batch_upload_kib_per_sec << " KiB/s\n"
            << "Resident animations: " << resident_animations << "   textures: "
            << resident_bytes / (1024.0 * 1024.0) << " MiB";
        if (representative && representative->transition == AnimationTransitionState::Failed)
            out << "\nERROR: " << representative->transition_error;
        auto& text = world.reg().get<Text>(state.hud_text);
        text.text = ascii_to_u32(out.str());
        ++text.rev;
        const auto& window = world.resource<Window>();
        patch_transform(world, state.hud_text, [&](TransformEditor& transform) {
            transform.set_translation(
                {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f});
        });
    }
}
} // namespace

int main() {
    const fs::path root = wws::find_path(3, "res", true);
    if (root.empty()) {
        std::fprintf(stderr, "[aoe2_unit_preview] cannot locate res directory\n");
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{1280, 800, "AoE2 Unit Preview"});
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(Aoe2Plugin{"aoe2de_cache"});
    app.add_plugin(RenderPlugin);
    app.world.add_resource<PreviewState>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto& server = world.resource<AssetServer>();
        auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = PreviewLayer;
        camera.clear_color = {0.16f, 0.18f, 0.21f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        emplace_registered_render_passes(world, camera_entity).add(Aoe2UnitPassId);

        auto hud_camera_entity = world.spawn();
        Camera hud_camera;
        hud_camera.kind = CameraKind::Ortho;
        hud_camera.priority = 20;
        hud_camera.layers = HudLayer;
        hud_camera.do_clear = false;
        world.reg().emplace<Camera>(hud_camera_entity, hud_camera);
        emplace_render_passes<BatchPass>(world, hud_camera_entity);

        auto& state = world.resource<PreviewState>();
        state.hud_text = world.spawn();
        Text hud;
        hud.text = U"AoE2 diagnostics loading...";
        hud.font = server.load(FontDesc(std::string("fonts/AGENCYB.TTF"), 0));
        hud.size = 20;
        hud.color = {0.9f, 0.96f, 1.f, 1.f};
        hud.align = TextAlign::Left;
        hud.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(state.hud_text, std::move(hud));
        const auto& window = world.resource<Window>();
        Transform hud_transform = Transform::from_trs(
            {-window.width * .5f + 14.f, window.height * .5f - 14.f, 0.f});
        world.reg().emplace<Transform>(state.hud_text, hud_transform);
        world.reg().emplace<RenderLayer>(state.hud_text, RenderLayer{HudLayer});
        const auto& units = world.resource<Aoe2ResourceManager>().list_units();
        for (std::size_t i = 0; i < units.size(); ++i)
            if (units[i].id == "u_cam_camel_scout") { state.unit_index = i; break; }
        if (!units.empty()) {
            const auto& animations = units[state.unit_index].animations;
            for (std::size_t i = 0; i < animations.size(); ++i)
                if (animations[i] == "idleA") { state.animation_index = i; break; }
        }
        respawn_preview(world, state);
        std::printf("Controls: Left/Right unit, Up/Down animation, Q/E player, Space pause, R restart, M mask debug, F5 refresh\n");
    });
    app.add_system(Stage::Update, preview_input_system);
    app.add_system(Stage::Last, preview_diagnostics_system);
    run_app(app);
    return 0;
}
