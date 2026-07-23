#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <string>
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
    static constexpr std::size_t UnitCount = 2000;
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

struct BenchmarkState {
    std::vector<entt::entity> units;
    entt::entity hud_text = entt::null;
    std::size_t moving_count = 0;
    double spawn_ms = 0.0;
    double hud_seconds = 0.0;
    std::string fatal_error;
};

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

    Transform transform;
    const auto& window = world.resource<Window>();
    transform.translation = {
        -window.width * 0.5f + 14.f,
         window.height * 0.5f - 14.f,
         0.f
    };
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
    state.moving_count = target_moving;
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

    const auto started = std::chrono::steady_clock::now();
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
        Transform transform;
        transform.translation = {x, y, -y};
        transform.scale = glm::vec3(BenchmarkConfig::UnitScale);

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
    state.spawn_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
}

void benchmark_input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    if (keyboard && keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
}

void movement_system(EcsWorld& world) {
    const float dt = world.resource<Time>().dt;
    const auto& window = world.resource<Window>();
    const float half_width = std::max(1.f, window.width * 0.5f);
    const float half_height = std::max(1.f, window.height * 0.5f);

    auto& reg = world.reg();
    for (auto entity : reg.view<Motion, Transform>()) {
        const auto& motion = reg.get<Motion>(entity);
        auto& transform = reg.get<Transform>(entity);
        transform.translation.x += motion.velocity.x * dt;
        transform.translation.y += motion.velocity.y * dt;

        if (transform.translation.x > half_width) transform.translation.x = -half_width;
        else if (transform.translation.x < -half_width) transform.translation.x = half_width;
        if (transform.translation.y > half_height) transform.translation.y = -half_height;
        else if (transform.translation.y < -half_height) transform.translation.y = half_height;

        // In this 2.5D ortho view, lower feet are closer to the camera.
        transform.translation.z = -transform.translation.y;
    }
}

void benchmark_hud_system(EcsWorld& world) {
    auto& state = world.resource<BenchmarkState>();
    const auto& time = world.resource<Time>();
    state.hud_seconds += time.raw_dt;
    if (state.hud_seconds < BenchmarkConfig::HudRefreshSeconds) return;
    state.hud_seconds = 0.0;

    if (state.hud_text == entt::null || !world.reg().valid(state.hud_text)) return;

    auto& reg = world.reg();
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
        for (const auto& [_, entity] : index->map) {
            if (!reg.valid(entity)) continue;
            const auto& batch = reg.get<BatchComponent>(entity);
            if (!batch.used || (batch.layers & UnitLayer) == 0) continue;
            ++aoe_batches;
            aoe_instances += batch.instances.size();
        }
    }

    const auto& render = world.resource_or_add<RenderDiagnostics>();
    const auto& assets = world.resource<AssetServerDiagnostics>();
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
        << "Assets queued: " << assets.io_queued << "   active: " << assets.io_active
        << "   finalize: " << assets.finalize_queued << "\n"
        << "Depth: test+write, foot Z=-Y   shadows: no depth write\n"
        << "Spawn CPU: " << state.spawn_ms << " ms   seed: " << BenchmarkConfig::RandomSeed;
    if (!state.fatal_error.empty()) out << "\nERROR: " << state.fatal_error;

    auto& text = reg.get<Text>(state.hud_text);
    text.text = ascii_to_u32(out.str());
    ++text.rev;
    auto& transform = reg.get<Transform>(state.hud_text);
    transform.translation = {
        -world.resource<Window>().width * 0.5f + 14.f,
         world.resource<Window>().height * 0.5f - 14.f,
         0.f
    };
}

} // namespace

int main() {
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
    app.world.add_resource<BenchmarkState>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = UnitLayer;
        camera.clear_color = {0.12f, 0.14f, 0.17f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        auto& passes = emplace_render_passes<BatchPass>(world, camera_entity);
        auto* batch_pass = passes.get<BatchPass>();
        batch_pass->state.depth_test = RenderStateValue::Enabled;
        batch_pass->state.depth_write = RenderStateValue::Enabled;

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

    run_app(app);
    return 0;
}
