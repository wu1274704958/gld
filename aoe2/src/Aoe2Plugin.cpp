#include <aoe2/Aoe2Plugin.hpp>

#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe2 {

void Aoe2Plugin::operator()(App& app) const {
    auto& server = app.world.resource<AssetServer>();
    server.register_loader<Aoe2UnitAppearanceDesc>(
        std::make_shared<Aoe2UnitAppearanceLoader>(server));

    auto& manager = app.world.add_resource<Aoe2ResourceManager>(server, cache_root);
    manager.refresh();
    app.world.resource_or_add<Aoe2BatchIndex>();
    register_aoe2_batch_lifecycle(app.world);
    auto& render = app.world.resource_or_add<Aoe2RenderResources>();
    render.sprite_shader = server.load_program("ecs/aoe2_unit_vs.glsl", "ecs/aoe2_unit_fg.glsl");
    render.player_color_shader = server.load_program(
        "ecs/aoe2_unit_vs.glsl", "ecs/aoe2_unit_playercolor_fg.glsl");
    render.shadow_shader = server.load_program(
        "ecs/aoe2_unit_vs.glsl", "ecs/aoe2_unit_shadow_fg.glsl");
    register_render_pass(app.world, Aoe2UnitPassId, RegisteredRenderPassHandler{
        RenderPassBatch,
        render_aoe2_unit_pass,
        [](EcsWorld& world) { destroy_aoe2_batches(world); }
    });

    app.add_system(Stage::Update, spawn_aoe2_unit_system);
    app.add_system(Stage::Update, aoe2_unit_animation_system);
    app.add_system(Stage::PostUpdate, aoe2_batch_system);
    app.add_system(Stage::Shutdown, destroy_aoe2_batches);
}

} // namespace gld::ecs::aoe2
