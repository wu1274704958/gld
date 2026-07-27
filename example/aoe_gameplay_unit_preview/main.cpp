#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <random>
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
constexpr float SpriteScale = 1.35f;
constexpr float DepthUnitsPerTile = 1.f;
constexpr float ElevationPixelsPerUnit = 48.f;

struct PreviewState {
    std::size_t definition_index = 0;
    entt::entity player{entt::null};
    entt::entity enemy{entt::null};
    entt::entity hud{entt::null};
    int direction = 0;
    std::mt19937 random{0xA0E2u};
    std::string latest_event = "none";
    std::string latest_command = "Space: issue attack order";
    double hud_elapsed = 0.0;
};

std::u32string ascii_to_u32(const std::string& value) {
    std::u32string result;
    result.reserve(value.size());
    for (const unsigned char c : value) result.push_back(static_cast<char32_t>(c));
    return result;
}

const char* state_name(UnitState value) {
    switch (value) {
    case UnitState::Idle: return "idle";
    case UnitState::Moving: return "moving";
    case UnitState::Attacking: return "attacking";
    case UnitState::Dying: return "dying";
    case UnitState::Disappearing: return "disappearing";
    }
    return "unknown";
}

const char* event_name(AoeActionEventType value) {
    switch (value) {
    case AoeActionEventType::AttackStarted: return "AttackStarted";
    case AoeActionEventType::AttackReleased: return "AttackReleased";
    case AoeActionEventType::AttackFinished: return "AttackFinished";
    case AoeActionEventType::DamageApplied: return "DamageApplied";
    case AoeActionEventType::DeathStarted: return "DeathStarted";
    case AoeActionEventType::DisappearStarted: return "DisappearStarted";
    case AoeActionEventType::RecycleRequested: return "RecycleRequested";
    case AoeActionEventType::ProjectileSpawned: return "ProjectileSpawned";
    case AoeActionEventType::ProjectileHit: return "ProjectileHit";
    case AoeActionEventType::ProjectileMiss: return "ProjectileMiss";
    case AoeActionEventType::ProjectileSpawnFailed: return "ProjectileSpawnFailed";
    }
    return "unknown";
}

std::string typed_amounts(const std::vector<TypedAmount>& values) {
    if (values.empty()) return "none";
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out << ", ";
        out << values[i].class_id << ':' << values[i].amount;
    }
    return out.str();
}

glm::vec2 random_enemy_position(PreviewState& state) {
    std::uniform_real_distribution<float> coordinate(-5.2f, 5.2f);
    glm::vec2 position;
    do position = {coordinate(state.random), coordinate(state.random)};
    while (glm::length(position) < 3.5f);
    return position;
}

void destroy_gameplay_with_presentation(EcsWorld& world, entt::entity entity) {
    auto& reg = world.reg();
    if (!reg.valid(entity)) return;
    if (const auto* link = reg.try_get<Aoe2PresentationLink>(entity);
        link && reg.valid(link->render)) reg.destroy(link->render);
    reg.destroy(entity);
}

void spawn_player(EcsWorld& world, PreviewState& state) {
    const auto& definitions = world.resource<AoeUnitDefinitionManager>().list();
    if (definitions.empty()) return;
    state.definition_index %= definitions.size();
    destroy_gameplay_with_presentation(world, state.player);
    AoeUnitSpawnOptions options;
    options.definition_id = definitions[state.definition_index].id;
    options.player_color = 1;
    options.team_id = 1;
    options.direction = state.direction;
    options.direction_count = 16;
    options.layers = UnitLayer;
    options.position = {0.f, 0.f};
    state.player = spawn_aoe_gameplay_unit(world, options);
    state.latest_command = "spawned player definition " + options.definition_id;
}

void spawn_enemy_from_pool(EcsWorld& world, PreviewState& state) {
    AoeUnitSpawnOptions options;
    options.definition_id = "camel_scout";
    options.player_color = 2;
    options.team_id = 2;
    options.direction_count = 16;
    options.layers = UnitLayer;
    options.position = random_enemy_position(state);
    const auto old = state.enemy;
    state.enemy = spawn_aoe_gameplay_unit(world, options);
    std::ostringstream message;
    message << (state.enemy == old ? "reused" : "spawned") << " enemy entity "
            << static_cast<std::uint32_t>(state.enemy) << " at ("
            << options.position.x << ", " << options.position.y << ')';
    state.latest_command = message.str();
}

glm::vec3 project_logical_position(glm::vec2 logical) {
    const float depth = logical.x + logical.y;
    return {
        (logical.x - logical.y) * TileWidth * .5f,
        -depth * TileHeight * .5f + 35.f,
        depth * DepthUnitsPerTile};
}

void projection_system(EcsWorld& world) {
    auto& reg = world.reg();
    for (const auto entity : reg.view<AoePosition, Transform>(
             entt::exclude<AoePooledUnit>)) {
        const auto logical = reg.get<AoePosition>(entity).value;
        const glm::vec3 screen = project_logical_position(logical);
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(screen);
            transform.set_scale({SpriteScale, SpriteScale, 1.f});
        });
    }
    for (const auto entity : reg.view<AoeProjectile, Transform>()) {
        const auto& projectile = reg.get<AoeProjectile>(entity);
        glm::vec3 screen = project_logical_position(
            {projectile.position.x, projectile.position.y});
        // GLD's orthographic camera uses world Y up. Positive gameplay Z must
        // therefore raise the projectile, matching metadata-preview projection.
        screen.y += projectile.position.z * ElevationPixelsPerUnit;
        screen.z += 0.001f;
        patch_transform(world, entity, [&](TransformEditor& transform) {
            transform.set_translation(screen);
            transform.set_scale({SpriteScale, SpriteScale, 1.f});
        });
    }
}

void input_system(EcsWorld& world) {
    auto* keyboard = world.try_resource<Keyboard>();
    auto* state = world.try_resource<PreviewState>();
    if (!keyboard || !state) return;
    auto& reg = world.reg();
    auto& manager = world.resource<AoeUnitDefinitionManager>();
    const auto count = manager.list().size();

    if (keyboard->just_now_pressed(GLFW_KEY_ESCAPE))
        world.resource<Window>().should_close = true;
    if (count && keyboard->just_now_pressed(GLFW_KEY_LEFT)) {
        state->definition_index = (state->definition_index + count - 1) % count;
        spawn_player(world, *state);
    }
    if (count && keyboard->just_now_pressed(GLFW_KEY_RIGHT)) {
        state->definition_index = (state->definition_index + 1) % count;
        spawn_player(world, *state);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_SPACE)) {
        const bool accepted = request_aoe_attack(world, state->player, state->enemy);
        state->latest_command = accepted
            ? "attack order accepted; it persists until target dies"
            : "attack order rejected (wait for both units or press R after pooling)";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_M)) {
        bool accepted = false;
        glm::vec2 destination{8.f, 0.f};
        if (reg.valid(state->player) && reg.valid(state->enemy) &&
            reg.all_of<AoePosition>(state->player) &&
            reg.all_of<AoePosition>(state->enemy)) {
            const auto player_position = reg.get<AoePosition>(state->player).value;
            const auto enemy_position = reg.get<AoePosition>(state->enemy).value;
            const auto delta = enemy_position - player_position;
            destination = enemy_position + (glm::length(delta) > 1e-5f
                ? glm::normalize(delta) * 4.f : glm::vec2{4.f, 0.f});
            accepted = request_aoe_attack_move(
                world, state->player, destination);
        }
        std::ostringstream message;
        message << (accepted ? "attack-move accepted" : "attack-move rejected")
                << " destination=(" << destination.x << ", "
                << destination.y << ')';
        state->latest_command = message.str();
    }
    if (keyboard->just_now_pressed(GLFW_KEY_A) && reg.valid(state->player)) {
        state->direction = (state->direction + 15) % 16;
        set_aoe_unit_facing(world, state->player, state->direction, 16);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_D) && reg.valid(state->player)) {
        state->direction = (state->direction + 1) % 16;
        set_aoe_unit_facing(world, state->player, state->direction, 16);
    }
    if (keyboard->just_now_pressed(GLFW_KEY_R)) {
        if (reg.valid(state->enemy) && reg.all_of<AoePooledUnit>(state->enemy))
            spawn_enemy_from_pool(world, *state);
        else state->latest_command = "R ignored: enemy has not reached the pool";
    }
    if (keyboard->just_now_pressed(GLFW_KEY_F5)) {
        std::string selected;
        if (count) selected = manager.list()[state->definition_index].id;
        manager.refresh();
        state->definition_index = 0;
        for (std::size_t i = 0; i < manager.list().size(); ++i)
            if (manager.list()[i].id == selected) state->definition_index = i;
        state->latest_command = "definition cache rescanned";
    }
}

void append_unit_status(std::ostringstream& out, EcsWorld& world,
                        const char* label, entt::entity entity) {
    auto& reg = world.reg();
    out << label << " entity=" << static_cast<std::uint32_t>(entity);
    if (!reg.valid(entity)) { out << " invalid\n"; return; }
    if (reg.all_of<AoePooledUnit>(entity)) { out << " POOLED (press R to reuse enemy)\n"; return; }
    if (const auto* error = reg.try_get<AoeGameplaySpawnError>(entity)) {
        out << " spawn error: " << error->message << '\n'; return;
    }
    if (reg.all_of<AoeGameplaySpawnRequest>(entity)) { out << " loading...\n"; return; }
    const auto* definition_ref = reg.try_get<AoeUnitDefinitionRef>(entity);
    const auto* identity = reg.try_get<AoeGameplayIdentity>(entity);
    const auto* health = reg.try_get<AoeHealth>(entity);
    const auto* position = reg.try_get<AoePosition>(entity);
    const auto* action = reg.try_get<AoeActionState>(entity);
    const auto* facing = reg.try_get<AoeFacing>(entity);
    const auto* team = reg.try_get<AoeTeam>(entity);
    if (!definition_ref || !identity || !health || !position || !action || !facing) {
        out << " incomplete\n"; return;
    }
    const auto* definition = definition_ref->value.get();
    out << " instance=" << identity->instance_id
        << " definition=" << (definition ? definition->id : "?") << '\n'
        << "  HP=" << health->current << '/' << health->maximum
        << " state=" << state_name(action->state)
        << " team=" << (team ? team->id : 0)
        << " position=(" << position->value.x << ", " << position->value.y << ')'
        << " gameplay-facing=" << facing->direction;
    if (const auto* link = reg.try_get<Aoe2PresentationLink>(entity);
        link && reg.valid(link->render)) {
        if (const auto* render = reg.try_get<Aoe2UnitRender>(link->render))
            out << " sld-facing=" << render->direction_slot;
        else if (const auto* pending = reg.try_get<Aoe2SpawnRequest>(link->render))
            out << " sld-facing=" << pending->options.direction;
    }
    if (reg.all_of<AoeRecyclePending>(entity)) out << " recycle-pending";
    out << '\n';
}

void diagnostics_system(EcsWorld& world) {
    auto& preview = world.resource<PreviewState>();
    for (const auto& event : world.resource<Events<AoeActionEvent>>().read()) {
        std::ostringstream text;
        text << event_name(event.type) << " unit=" << static_cast<std::uint32_t>(event.unit)
             << " target=" << static_cast<std::uint32_t>(event.target)
             << " tick=" << event.tick << " seq=" << event.sequence;
        if (event.type == AoeActionEventType::AttackStarted)
            text << " critical=" << (event.critical ? "yes" : "no");
        if (event.type == AoeActionEventType::DamageApplied)
            text << " damage=" << event.amount;
        if (!event.projectile_id.empty())
            text << " projectile=" << event.projectile_id << '#'
                 << static_cast<std::uint32_t>(event.projectile);
        if (event.type == AoeActionEventType::ProjectileMiss ||
            event.type == AoeActionEventType::ProjectileSpawnFailed)
            text << " reason=" << static_cast<int>(event.projectile_reason);
        preview.latest_event = text.str();
    }

    preview.hud_elapsed += world.resource<Time>().raw_dt;
    if (preview.hud_elapsed < 0.08 || !world.reg().valid(preview.hud)) return;
    preview.hud_elapsed = 0.0;

    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << "AoE gameplay combat preview\n"
        << "Left/Right player unit | Space attack enemy | M attack-move | A/D facing\n"
        << "R reuse pooled enemy | F5 rescan definitions | Esc quit\n\n";
    append_unit_status(out, world, "PLAYER", preview.player);
    append_unit_status(out, world, "ENEMY ", preview.enemy);

    auto& reg = world.reg();
    if (reg.valid(preview.player) && reg.valid(preview.enemy) &&
        reg.all_of<AoePosition, AoeCollider, AoeUnitDefinitionRef>(preview.player) &&
        reg.all_of<AoePosition, AoeCollider>(preview.enemy)) {
        const auto* definition = reg.get<AoeUnitDefinitionRef>(preview.player).value.get();
        const float gap = aoe_surface_gap(reg.get<AoePosition>(preview.player),
            reg.get<AoeCollider>(preview.player), reg.get<AoePosition>(preview.enemy),
            reg.get<AoeCollider>(preview.enemy));
        out << "Surface gap=" << gap << " attack range="
            << (definition && definition->attack ? definition->attack->range : 0.f)
            << " order=" << (reg.all_of<AoeAttackOrder>(preview.player) ? "attack" : "none")
            << " moving=" << (reg.all_of<AoeMoveGoal>(preview.player) ? "yes" : "no") << '\n';
        if (definition) {
            out << "Player armor [class:value]: " << typed_amounts(definition->armor) << '\n';
            out << "Acquisition strategy="
                << aoe_target_acquisition_name(
                       definition->target_acquisition.strategy)
                << " radius=" << definition->target_acquisition.radius
                << " disengage="
                << definition->target_acquisition.disengage_radius << '\n';
            if (definition->attack)
                out << "Damage [class:value]: " << typed_amounts(definition->attack->damage)
                    << " cooldown=" << definition->attack->cooldown_seconds << "s\n";
        }
    }
    if (reg.valid(preview.player)) {
        if (const auto* order = reg.try_get<AoeAttackMoveOrder>(preview.player)) {
            const char* phase = reg.all_of<AoeAttackOrder>(preview.player)
                ? "engaging"
                : (reg.all_of<AoeMoveGoal>(preview.player)
                    ? "moving/resuming" : "arrived");
            out << "AttackMove destination=(" << order->destination.x << ", "
                << order->destination.y << ") phase=" << phase;
            if (const auto* attack = reg.try_get<AoeAttackOrder>(preview.player))
                out << " target=" << static_cast<std::uint32_t>(
                    attack->target.entity) << ':' << attack->target.instance_id;
            out << '\n';
        }
    }
    const auto& pool = world.resource<AoeGameplayPool>();
    const auto& diagnostics = world.resource<AoeGameplayDiagnostics>();
    out << "Pool available=" << pool.available.size() << " recycled=" << pool.recycled
        << " reused=" << pool.reused << '\n'
        << "Attacks=" << diagnostics.attacks_started
        << " damage events=" << diagnostics.damage_events
        << " rejected commands=" << diagnostics.commands_rejected << '\n'
        << "Projectiles active=" << reg.storage<AoeProjectile>().size()
        << " spawned=" << diagnostics.projectiles_spawned
        << " hit=" << diagnostics.projectiles_hit
        << " missed=" << diagnostics.projectiles_missed
        << " failed=" << diagnostics.projectiles_failed << '\n';
    if (const auto projectiles = reg.view<AoeProjectile>(); !projectiles.empty()) {
        const auto entity = *projectiles.begin();
        const auto& projectile = projectiles.get<AoeProjectile>(entity);
        out << "  Arrow entity=" << static_cast<std::uint32_t>(entity)
            << " pos=(" << projectile.position.x << ", "
            << projectile.position.y << ", " << projectile.position.z << ')'
            << " progress=" << projectile.progress
            << " target-instance=" << projectile.target.instance_id << '\n';
    }
    out
        << "Latest command: " << preview.latest_command << '\n'
        << "Latest event: " << preview.latest_event << '\n'
        << "\nProjectile attacks apply damage on the gameplay impact tick.";

    auto& text = reg.get<Text>(preview.hud);
    text.text = ascii_to_u32(out.str());
    ++text.rev;
}
} // namespace

int main() {
    const fs::path root = wws::find_path(3, "res", true);
    if (root.empty()) {
        std::fprintf(stderr, "[aoe_gameplay_preview] cannot locate res directory\n");
        return 1;
    }
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{1280, 800, "AoE Gameplay Combat Preview"});
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
    app.add_plugin(RenderPlugin);
    app.world.add_resource<PreviewState>();

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        const auto camera_entity = world.spawn();
        Camera camera;
        camera.kind = CameraKind::Ortho;
        camera.layers = UnitLayer;
        camera.clear_color = {0.13f, 0.15f, 0.18f, 1.f};
        world.reg().emplace<Camera>(camera_entity, camera);
        auto& unit_pass = emplace_registered_render_passes(world, camera_entity)
            .add(Aoe2UnitPassId);
        unit_pass.state.depth_test = RenderStateValue::Enabled;
        unit_pass.state.depth_write = RenderStateValue::Enabled;
        unit_pass.state.blend = RenderStateValue::Enabled;

        const auto hud_camera_entity = world.spawn();
        Camera hud_camera;
        hud_camera.kind = CameraKind::Ortho;
        hud_camera.priority = 20;
        hud_camera.layers = HudLayer;
        hud_camera.do_clear = false;
        world.reg().emplace<Camera>(hud_camera_entity, hud_camera);
        emplace_render_passes<BatchPass>(world, hud_camera_entity);

        auto& preview = world.resource<PreviewState>();
        preview.hud = world.spawn();
        Text hud;
        hud.text = U"Loading AoE gameplay combat preview...";
        hud.font = world.resource<AssetServer>().load(FontDesc("fonts/AGENCYB.TTF", 0));
        hud.size = 19;
        hud.color = {0.92f, 0.96f, 1.f, 1.f};
        hud.align = TextAlign::Left;
        hud.anchor = {0.f, 0.f};
        world.reg().emplace<Text>(preview.hud, std::move(hud));
        const auto& window = world.resource<Window>();
        world.reg().emplace<Transform>(preview.hud, Transform::from_trs(
            {-window.width * .5f + 16.f, window.height * .5f - 16.f, 0.f}));
        world.reg().emplace<RenderLayer>(preview.hud, RenderLayer{HudLayer});

        const auto& definitions = world.resource<AoeUnitDefinitionManager>().list();
        for (std::size_t i = 0; i < definitions.size(); ++i)
            if (definitions[i].id == "archer") preview.definition_index = i;
        spawn_player(world, preview);
        spawn_enemy_from_pool(world, preview);
    });
    app.add_system(Stage::Update, input_system);
    app.add_system(Stage::Update, projection_system);
    app.add_system(Stage::Last, diagnostics_system);
    run_app(app);
    return 0;
}
