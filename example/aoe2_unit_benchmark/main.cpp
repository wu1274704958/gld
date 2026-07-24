#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <FindPath.hpp>
#include <resource_mgr.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <ecs/App.hpp>
#include <ecs/Window.hpp>
#include <ecs/Input.hpp>
#include <ecs/Components.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>
#include <aoe2/Aoe2Plugin.hpp>

using namespace gld::ecs;
using namespace gld::ecs::aoe2;
namespace fs = std::filesystem;

namespace {

// All benchmark knobs intentionally live in one place. Recompile after editing.
struct BenchmarkConfig {
    static constexpr std::size_t UnitCount = 30000;
    static constexpr float MovingRatio = 0.50f;
    static constexpr float MinSpeed = 20.f;
    static constexpr float MaxSpeed = 80.f;
    static constexpr float UnitScale = 0.30f;
    static constexpr std::uint32_t RandomSeed = 0xA0E20001u;
    static constexpr int WindowWidth = 1920;
    static constexpr int WindowHeight = 1080;
    static constexpr double HudRefreshSeconds = 0.5;
};

static_assert(BenchmarkConfig::MovingRatio >= 0.f && BenchmarkConfig::MovingRatio <= 1.f);
static_assert(BenchmarkConfig::MinSpeed >= 0.f &&
              BenchmarkConfig::MaxSpeed >= BenchmarkConfig::MinSpeed);

constexpr std::uint32_t UnitLayer = 0x1u;
constexpr std::uint32_t HudLayer = 0x2u;

struct Motion {
    glm::vec2 velocity{0.f};
};

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
struct TimingSample {
    double sum = 0.0;
    double maximum = 0.0;
    void add(double value) {
        sum += value;
        maximum = std::max(maximum, value);
    }
    double average(std::uint64_t samples) const {
        return samples ? sum / static_cast<double>(samples) : 0.0;
    }
};

struct BenchmarkTimingWindow {
    std::uint64_t frames = 0;
    TimingSample animation;
    TimingSample movement;
    TimingSample transform;
    TimingSample aoe_total;
    TimingSample aoe_group;
    TimingSample aoe_rebuild;
    TimingSample batch_prepare;
    TimingSample batch_upload;
    TimingSample batch_submit;
    TimingSample aoe_gpu;
    TimingSample present;

    void reset() { *this = {}; }
};
#endif

struct BenchmarkState {
    std::vector<entt::entity> units;
    entt::entity hud_text = entt::null;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    std::size_t moving_count = 0;
    double spawn_ms = 0.0;
    double movement_ms = 0.0;
    std::uint32_t movement_units = 0;
    BenchmarkTimingWindow timing;
#endif
    double hud_seconds = 0.0;
    std::string fatal_error;
    std::string last_report;
    double duration_seconds = 0.0;
    bool report_printed = false;
    std::chrono::steady_clock::time_point process_started =
        std::chrono::steady_clock::now();
};

double benchmark_duration_from_args(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) != "--duration") continue;
        if (++i >= argc) return -1.0;
        char* end = nullptr;
        const double value = std::strtod(argv[i], &end);
        if (!end || *end != '\0' || !std::isfinite(value) || value <= 0.0)
            return -1.0;
        return value;
    }
    return 0.0;
}

std::u32string ascii_to_u32(const std::string& text) {
    std::u32string result;
    result.reserve(text.size());
    for (unsigned char c : text) result.push_back(static_cast<char32_t>(c));
    return result;
}

bool is_walk_animation(const std::string& name) {
    return name.size() >= 4 && name.compare(0, 4, "walk") == 0;
}

int velocity_direction_slot(const glm::vec2& velocity) {
    if (velocity.x == 0.f && velocity.y == 0.f) return 0;
    float clockwise = std::atan2(-velocity.y, velocity.x);
    if (clockwise < 0.f) clockwise += glm::two_pi<float>();
    const float scaled = clockwise * 16.f / glm::two_pi<float>();
    return static_cast<int>(std::floor(scaled + 0.5f)) % 16;
}

void create_hud(EcsWorld& world, AssetServer& server, BenchmarkState& state) {
    state.hud_text = world.spawn();
    Text hud;
    hud.text = U"AoE2 unit benchmark loading...";
    hud.font = server.load(FontDesc(std::string("fonts/AGENCYB.TTF"), 0));
    hud.size = 20;
    hud.color = {0.92f, 0.97f, 1.f, 1.f};
    hud.align = TextAlign::Left;
    hud.anchor = {0.f, 0.f};
    world.reg().emplace<Text>(state.hud_text, std::move(hud));

    const auto& window = world.resource<Window>();
    Transform transform = Transform::from_trs({
        -window.width * 0.5f + 14.f,
         window.height * 0.5f - 14.f,
         0.f
    });
    world.reg().emplace<Transform>(state.hud_text, transform);
    world.reg().emplace<RenderLayer>(state.hud_text, RenderLayer{HudLayer});
}

void spawn_benchmark_units(EcsWorld& world, BenchmarkState& state) {
    const auto& records = world.resource<Aoe2ResourceManager>().list_units();
    if (records.empty()) {
        state.fatal_error = "No AoE2 units found under res/aoe2de_cache";
        return;
    }

    std::vector<std::vector<std::size_t>> walk_animations(records.size());
    std::vector<std::size_t> spawnable_unit_choices;
    std::vector<std::size_t> moving_unit_choices;
    for (std::size_t unit = 0; unit < records.size(); ++unit) {
        if (!records[unit].animations.empty()) spawnable_unit_choices.push_back(unit);
        for (std::size_t animation = 0; animation < records[unit].animations.size(); ++animation) {
            if (is_walk_animation(records[unit].animations[animation]))
                walk_animations[unit].push_back(animation);
        }
        if (!walk_animations[unit].empty()) moving_unit_choices.push_back(unit);
    }
    if (spawnable_unit_choices.empty()) {
        state.fatal_error = "No AoE2 unit has an exported animation";
        return;
    }
    if (moving_unit_choices.empty()) {
        state.fatal_error = "No exported unit has a walk* animation";
        return;
    }

    const std::size_t target_moving = std::min(
        BenchmarkConfig::UnitCount,
        static_cast<std::size_t>(std::llround(
            static_cast<double>(BenchmarkConfig::UnitCount) * BenchmarkConfig::MovingRatio)));
    state.units.reserve(BenchmarkConfig::UnitCount);

    std::mt19937 random(BenchmarkConfig::RandomSeed);
    std::uniform_int_distribution<std::size_t> any_unit(0, spawnable_unit_choices.size() - 1);
    std::uniform_int_distribution<std::size_t> moving_unit(0, moving_unit_choices.size() - 1);
    std::uniform_int_distribution<int> player_color(1, 8);
    std::uniform_int_distribution<int> direction(0, 15);
    std::uniform_real_distribution<float> x_position(
        -BenchmarkConfig::WindowWidth * 0.5f, BenchmarkConfig::WindowWidth * 0.5f);
    std::uniform_real_distribution<float> y_position(
        -BenchmarkConfig::WindowHeight * 0.5f, BenchmarkConfig::WindowHeight * 0.5f);
    std::uniform_real_distribution<float> speed(BenchmarkConfig::MinSpeed, BenchmarkConfig::MaxSpeed);
    std::uniform_real_distribution<float> angle(0.f, glm::two_pi<float>());

#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    for (std::size_t index = 0; index < BenchmarkConfig::UnitCount; ++index) {
        const bool moving = index < target_moving;
        const std::size_t unit_index = moving
            ? moving_unit_choices[moving_unit(random)]
            : spawnable_unit_choices[any_unit(random)];
        const auto& unit = records[unit_index];

        std::size_t animation_index = 0;
        glm::vec2 velocity(0.f);
        int direction_slot = direction(random);
        if (moving) {
            const auto& walks = walk_animations[unit_index];
            std::uniform_int_distribution<std::size_t> walk_choice(0, walks.size() - 1);
            animation_index = walks[walk_choice(random)];
            const float radians = angle(random);
            const float magnitude = speed(random);
            velocity = glm::vec2(std::cos(radians), std::sin(radians)) * magnitude;
            direction_slot = velocity_direction_slot(velocity);
        } else {
            std::uniform_int_distribution<std::size_t> animation_choice(0, unit.animations.size() - 1);
            animation_index = animation_choice(random);
        }

        const float x = x_position(random);
        const float y = y_position(random);
        Transform transform = Transform::from_trs(
            {x, y, -y}, glm::vec3(0.f), glm::vec3(BenchmarkConfig::UnitScale));

        SpawnOptions options;
        options.unit_id = unit.id;
        options.animation = unit.animations[animation_index];
        options.direction = direction_slot;
        options.direction_slot_count = 16;
        options.player_color = player_color(random);
        options.layers = UnitLayer;

        const entt::entity entity = spawn_aoe2_unit(world, options, transform);
        if (moving) world.reg().emplace<Motion>(entity, Motion{velocity});
        state.units.push_back(entity);
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    state.moving_count = target_moving;
    state.spawn_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
#endif
}

void benchmark_input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    if (keyboard && keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
}

void movement_system(EcsWorld& world) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto started = std::chrono::steady_clock::now();
#endif
    auto& state = world.resource<BenchmarkState>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    state.movement_units = 0;
#endif
    const float dt = world.resource<Time>().dt;
    const auto& window = world.resource<Window>();
    const float half_width = std::max(1.f, window.width * 0.5f);
    const float half_height = std::max(1.f, window.height * 0.5f);

    auto& reg = world.reg();
    for (auto entity : reg.view<Motion, Transform>()) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        ++state.movement_units;
#endif
        const auto& motion = reg.get<Motion>(entity);
        glm::vec3 next = reg.get<Transform>(entity).translation() +
            glm::vec3(motion.velocity * dt, 0.f);
        if (next.x > half_width) next.x = -half_width;
        else if (next.x < -half_width) next.x = half_width;
        if (next.y > half_height) next.y = -half_height;
        else if (next.y < -half_height) next.y = half_height;
        // In this 2.5D ortho view, lower feet are closer to the camera.
        next.z = -next.y;
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(next);
        });
    }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    state.movement_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
#endif
}

void benchmark_hud_system(EcsWorld& world) {
    auto& state = world.resource<BenchmarkState>();
    const auto& time = world.resource<Time>();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    const auto& aoe_perf = world.resource_or_add<Aoe2PerformanceDiagnostics>();
    const auto& transform_perf = world.resource_or_add<TransformDiagnostics>();
    const auto& render = world.resource_or_add<RenderDiagnostics>();
    ++state.timing.frames;
    state.timing.animation.add(aoe_perf.animation_ms);
    state.timing.movement.add(state.movement_ms);
    state.timing.transform.add(transform_perf.cpu_ms);
    state.timing.aoe_total.add(aoe_perf.batch_total_ms);
    state.timing.aoe_group.add(aoe_perf.batch_group_ms);
    state.timing.aoe_rebuild.add(aoe_perf.batch_rebuild_ms);
    state.timing.batch_prepare.add(render.batch_prepare_ms);
    state.timing.batch_upload.add(render.batch_upload_ms);
    state.timing.batch_submit.add(render.batch_submit_ms);
    state.timing.aoe_gpu.add(aoe_perf.render_gpu_ms);
    state.timing.present.add(render.present_ms);
#endif
    state.hud_seconds += time.raw_dt;
    if (state.hud_seconds < BenchmarkConfig::HudRefreshSeconds) return;
    state.hud_seconds = 0.0;

    if (state.hud_text == entt::null || !world.reg().valid(state.hud_text)) return;

    auto& reg = world.reg();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    std::size_t created = 0;
    std::size_t drawable = 0;
    std::size_t waiting = 0;
    std::size_t pending = 0;
    std::size_t failed = 0;
    for (auto entity : reg.view<Aoe2UnitRender>()) {
        const auto& unit = reg.get<Aoe2UnitRender>(entity);
        ++created;
        if (unit.has_main) ++drawable;
        if (unit.transition == AnimationTransitionState::Waiting) ++waiting;
        else if (unit.transition == AnimationTransitionState::Failed) ++failed;
    }
    for ([[maybe_unused]] auto entity : reg.view<Aoe2SpawnRequest>()) ++pending;
    for ([[maybe_unused]] auto entity : reg.view<Aoe2SpawnError>()) ++failed;

    std::size_t aoe_batches = 0;
    std::size_t aoe_instances = 0;
    if (const auto* index = world.try_resource<Aoe2BatchIndex>()) {
        for (const auto& group : index->groups) {
            if (!group.active || !reg.valid(group.batch_entity)) continue;
            const auto& batch = reg.get<Aoe2BatchComponent>(group.batch_entity);
            if ((batch.layers & UnitLayer) == 0) continue;
            ++aoe_batches;
            aoe_instances += batch.world_instances.size();
        }
    }

    const auto& assets = world.resource<AssetServerDiagnostics>();
    const auto timing_frames = state.timing.frames;
    auto timing = [&](const TimingSample& sample) {
        std::ostringstream value;
        value << std::fixed << std::setprecision(2)
              << sample.average(timing_frames) << "/" << sample.maximum;
        return value.str();
    };
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << "AOE2 UNIT BENCHMARK\n"
        << "FPS: " << time.fps << "   frame: " << time.raw_dt * 1000.f << " ms\n"
        << "Units: " << state.units.size() << "/" << BenchmarkConfig::UnitCount
        << "   created: " << created << "   drawable: " << drawable << "\n"
        << "Loading: spawn " << pending << "   animation " << waiting
        << "   failed: " << failed << "\n"
        << "Moving: " << state.moving_count << "   stationary: "
        << state.units.size() - state.moving_count << "   speed: "
        << BenchmarkConfig::MinSpeed << "-" << BenchmarkConfig::MaxSpeed << " px/s\n"
        << "AOE batches/draws: " << aoe_batches << "   render instances: " << aoe_instances << "\n"
        << "GPU batch uploads: " << render.batch_uploads << "   "
        << render.batch_upload_bytes / (1024.0 * 1024.0) << " MiB/frame\n"
        << "Upload mode: full " << render.batch_full_uploads << " ("
        << render.batch_full_upload_bytes / (1024.0 * 1024.0) << " MiB)   partial "
        << render.batch_partial_uploads << " ("
        << render.batch_partial_upload_bytes / (1024.0 * 1024.0)
        << " MiB)   ranges " << render.batch_upload_ranges << "\n"
        << "AoE streams: world " << aoe_perf.world_upload_bytes / (1024.0 * 1024.0)
        << " MiB/" << aoe_perf.world_uploads << "   visual "
        << aoe_perf.visual_upload_bytes / (1024.0 * 1024.0) << " MiB/"
        << aoe_perf.visual_uploads << "   ranges " << aoe_perf.upload_ranges << "\n"
        << "CPU avg/max ms: anim " << timing(state.timing.animation)
        << "   move " << timing(state.timing.movement)
        << "   xform " << timing(state.timing.transform) << "\n"
        << "AoE batch ms: total " << timing(state.timing.aoe_total)
        << "   group " << timing(state.timing.aoe_group)
        << "   rebuild " << timing(state.timing.aoe_rebuild) << "\n"
        << "Batch CPU ms: prep " << timing(state.timing.batch_prepare)
        << "   upload " << timing(state.timing.batch_upload)
        << "   submit " << timing(state.timing.batch_submit)
        << "   present " << timing(state.timing.present) << "\n"
        << "AoE GPU ms: " << timing(state.timing.aoe_gpu)
        << "   query skips " << aoe_perf.render_gpu_query_skips << "\n"
        << "AoE dirty work: entities " << aoe_perf.batch_units << "   slots "
        << aoe_perf.batch_sources << "   groups " << aoe_perf.batch_groups
        << "   dirty/steady " << aoe_perf.batch_dirty_groups << "/"
        << aoe_perf.batch_unchanged_groups << "   rebuilt "
        << aoe_perf.batch_rebuilt_instances << "\n"
        << "Membership: attach/detach/move " << aoe_perf.membership_attaches << "/"
        << aoe_perf.membership_detaches << "/" << aoe_perf.membership_migrations
        << "   group +/- " << aoe_perf.group_creates << "/"
        << aoe_perf.group_destroys << "\n"
        << "Instance dirty: frame " << aoe_perf.frame_dirty_instances
        << "   xform " << aoe_perf.transform_dirty_instances
        << "   material " << aoe_perf.material_dirty_instances
        << "   full " << aoe_perf.full_initialized_instances
        << "   steady " << aoe_perf.unchanged_instances << "\n"
        << "Dirty queue: enqueued " << aoe_perf.dirty_enqueued << "   dedup "
        << aoe_perf.dirty_deduplicated << "   mode "
        << (aoe_perf.dirty_dense_mode ? "dense" : "sparse") << "   audit "
        << aoe_perf.dirty_audit_violations << "   transform q/root/visit "
        << transform_perf.queued << "/" << transform_perf.roots << "/"
        << transform_perf.visited << "\n"
        << "Animation work: units " << aoe_perf.animation_units << "   changed "
        << aoe_perf.animation_frame_changes << "   pending "
        << aoe_perf.animation_pending << "   movement " << state.movement_units << "\n"
        << "Assets queued: " << assets.io_queued << "   active: " << assets.io_active
        << "   finalize: " << assets.finalize_queued << "\n"
        << "Depth: test+write, foot Z=-Y   shadows: no depth write\n"
        << "Spawn CPU: " << state.spawn_ms << " ms   seed: " << BenchmarkConfig::RandomSeed;
    if (!state.fatal_error.empty()) out << "\nERROR: " << state.fatal_error;
#else
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << "FPS: " << time.fps;
#endif

    state.last_report = out.str();
    auto& text = reg.get<Text>(state.hud_text);
    text.text = ascii_to_u32(state.last_report);
    ++text.rev;
    patch_transform(world, state.hud_text, [&](TransformEditor& transform) {
        transform.set_translation({
            -world.resource<Window>().width * 0.5f + 14.f,
             world.resource<Window>().height * 0.5f - 14.f,
             0.f
        });
    });
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    state.timing.reset();
#endif
}

void benchmark_auto_close_system(EcsWorld& world) {
    auto& state = world.resource<BenchmarkState>();
    const double process_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - state.process_started).count();
    if (state.duration_seconds <= 0.0 || process_seconds < state.duration_seconds) return;
    if (!state.report_printed) {
        if (!state.last_report.empty())
            std::fprintf(stdout, "%s\n", state.last_report.c_str());
        std::fflush(stdout);
        state.report_printed = true;
    }
    world.resource<Window>().should_close = true;
}

} // namespace

int main(int argc, char** argv) {
    const double duration_seconds = benchmark_duration_from_args(argc, argv);
    if (duration_seconds < 0.0) {
        std::fprintf(stderr, "usage: aoe2_unit_benchmark [--duration seconds]\n");
        return 2;
    }
    const fs::path root = wws::find_path(3, "res", true);
    if (root.empty()) return 1;
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{
        BenchmarkConfig::WindowWidth,
        BenchmarkConfig::WindowHeight,
        "AoE2 Unit Benchmark"
    });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(Aoe2Plugin{"aoe2de_cache"});
    app.add_plugin(RenderPlugin);
    auto& benchmark_state = app.world.add_resource<BenchmarkState>();
    benchmark_state.duration_seconds = duration_seconds;

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = UnitLayer;
        camera.clear_color = {0.12f, 0.14f, 0.17f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        auto& passes = emplace_registered_render_passes(world, camera_entity);
        auto& aoe2_pass = passes.add(Aoe2UnitPassId);
        aoe2_pass.state.depth_test = RenderStateValue::Enabled;
        aoe2_pass.state.depth_write = RenderStateValue::Enabled;

        auto hud_camera_entity = world.spawn();
        Camera hud_camera;
        hud_camera.kind = CameraKind::Ortho;
        hud_camera.priority = 20;
        hud_camera.layers = HudLayer;
        hud_camera.do_clear = false;
        world.reg().emplace<Camera>(hud_camera_entity, hud_camera);
        emplace_render_passes<BatchPass>(world, hud_camera_entity);

        auto& state = world.resource<BenchmarkState>();
        auto& server = world.resource<AssetServer>();
        create_hud(world, server, state);
        spawn_benchmark_units(world, state);
    });
    app.add_system(Stage::Update, benchmark_input_system);
    app.add_system(Stage::Update, movement_system);
    app.add_system(Stage::Last, benchmark_hud_system);
    app.add_system(Stage::Last, benchmark_auto_close_system);

    run_app(app);
    return 0;
}
