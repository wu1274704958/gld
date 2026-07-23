#include <aoe2/Aoe2Plugin.hpp>

#include <array>
#include <glad/glad.h>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe2 {
namespace {
std::shared_ptr<Texture<TexType::D2>> create_default_palette() {
    // Eight dark-to-light shades for players blue, red, green, yellow,
    // cyan, purple, grey and orange. Kept as a replaceable ECS resource.
    static constexpr std::array<unsigned char, 8 * 8 * 3> pixels = {
         7, 20, 71,  10, 32,106,  15, 48,145,  21, 67,184,  30, 88,220,  55,118,240,  92,151,250, 143,190,255,
        71,  8,  7, 108, 15, 12, 148, 23, 18, 188, 32, 25, 224, 45, 35, 241, 76, 64, 250,118,103, 255,170,157,
         6, 55, 16,   9, 83, 24,  14,115, 34,  20,148, 45,  29,179, 58,  53,203, 83,  89,223,119, 139,241,166,
        67, 57,  4, 102, 87,  7, 139,120, 11, 177,154, 16, 211,187, 24, 232,211, 52, 244,229, 94, 252,242,151,
         5, 50, 56,   8, 77, 86,  13,108,119,  19,140,153,  29,173,188,  56,198,211,  94,220,231, 146,240,247,
        43,  7, 68,  66, 11,104,  91, 17,142, 119, 25,181, 149, 37,217, 177, 67,236, 203,109,247, 226,163,253,
        42, 42, 42,  62, 62, 62,  85, 85, 85, 111,111,111, 139,139,139, 168,168,168, 201,201,201, 232,232,232,
        71, 30,  3, 109, 47,  5, 149, 66,  8, 190, 87, 12, 226,110, 18, 242,139, 47, 250,172, 86, 255,207,139
    };
    auto texture = std::make_shared<Texture<TexType::D2>>();
    texture->create();
    texture->bind();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    texture->tex_image(0, GL_RGB, 0, GL_RGB, pixels.data(), 8, 8);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texture->measure.width = 8;
    texture->measure.height = 8;
    texture->unbind();
    return texture;
}
} // namespace

void Aoe2Plugin::operator()(App& app) const {
    auto& server = app.world.resource<AssetServer>();
    server.register_loader<Aoe2UnitAppearanceDesc>(
        std::make_shared<Aoe2UnitAppearanceLoader>(server));

    auto& manager = app.world.add_resource<Aoe2ResourceManager>(server, cache_root);
    manager.refresh();
    app.world.resource_or_add<Aoe2BatchIndex>();
    auto& render = app.world.resource_or_add<Aoe2RenderResources>();
    render.sprite_shader = server.load_program("ecs/text_vs.glsl", "ecs/aoe2_unit_fg.glsl");
    render.player_color_shader = server.load_program(
        "ecs/text_vs.glsl", "ecs/aoe2_unit_playercolor_fg.glsl");
    render.shadow_shader = server.load_program("ecs/text_vs.glsl", "ecs/aoe2_unit_shadow_fg.glsl");

    app.add_system(Stage::Startup, [](EcsWorld& world) {
        auto& resources = world.resource<Aoe2RenderResources>();
        if (!resources.palette_texture) resources.palette_texture = create_default_palette();
    });
    app.add_system(Stage::Update, spawn_aoe2_unit_system);
    app.add_system(Stage::Update, aoe2_unit_animation_system);
    app.add_system(Stage::PostUpdate, aoe2_batch_system);
    app.add_system(Stage::Shutdown, destroy_aoe2_batches);
}

} // namespace gld::ecs::aoe2
