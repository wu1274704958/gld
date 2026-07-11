// ecs_text_fx – text rendering effects + backend selection demo.
// Same phrase rendered six ways to compare backends and effects:
//   1) AA plain            (fast coverage path, no effects)
//   2) AA + shadow         (coverage drop shadow)
//   3) SDF fill            (distance-field, crisp)
//   4) SDF + outline       (distance-thresholded outline)
//   5) SDF + shadow        (distance drop shadow)
//   6) SDF + outline + shadow
// Backend is chosen by TextEffects.mode (Auto resolves: outline -> SDF, else AA).
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <cstdio>
#include <memory>
#include <string>
#include <filesystem>
#include <glm/glm.hpp>

#include <ecs/App.hpp>
#include <ecs/Window.hpp>
#include <ecs/Components.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

int main()
{
    fs::path root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{ 1280, 860, "ecs_text_fx - AA/SDF + outline/shadow" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(RenderPlugin);

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        entt::entity camE = w.spawn();
        Camera cam; cam.kind = CameraKind::Ortho; cam.priority = 0; cam.target = 0;
        cam.clear_color = glm::vec4(0.90f, 0.91f, 0.93f, 1.f);   // bright background
        reg.emplace<Camera>(camE, cam);
        emplace_render_passes<BatchPass>(w, camE);

        Handle<FontAsset> font = srv.load(FontDesc(std::string("fonts/simkai.ttf"), 0));

        // Each row is one left-aligned Text: an fx caption + the unified sample
        // phrase, so the effect is self-identifying without reading the code.
        auto row = [&](const std::u32string& s, float y, glm::vec4 color, const TextEffects* fx) {
            entt::entity e = w.spawn();
            Text t;
            t.text = s; t.font = font; t.size = 56; t.color = color;
            t.align = TextAlign::Left; t.anchor = glm::vec2(0.f, 0.5f); t.rev = 1;
            reg.emplace<Text>(e, std::move(t));
            Transform tr; tr.translation = glm::vec3(-560.f, y, 0.f);
            reg.emplace<Transform>(e, tr);
            reg.emplace<GlobalTransform>(e);
            if (fx) reg.emplace<TextEffects>(e, *fx);
            return e;
        };

        const std::u32string sample = U"　演示文本-AA/SDF";
        const glm::vec4 dark (0.13f, 0.14f, 0.18f, 1.f);   // no-effect text on bright bg
        const glm::vec4 blue (0.10f, 0.35f, 0.75f, 1.f);   // shadow-effect text
        const glm::vec4 gold (1.00f, 0.84f, 0.32f, 1.f);   // outline-effect fill

        // 1) AA plain
        row(U"AA 纯文字" + sample, 330.f, dark, nullptr);

        // 2) AA + shadow
        {
            TextEffects fx; fx.mode = TextRenderMode::AA;
            fx.shadow = true; fx.shadow_color = glm::vec4(0.f, 0.f, 0.f, 0.45f);
            fx.shadow_offset = glm::vec2(3.f, -3.f); fx.shadow_softness = 1.f;
            row(U"AA 阴影" + sample, 200.f, blue, &fx);
        }
        // 3) SDF fill
        {
            TextEffects fx; fx.mode = TextRenderMode::SDF;
            row(U"SDF 填充" + sample, 70.f, dark, &fx);
        }
        // 4) SDF + outline
        {
            TextEffects fx; fx.mode = TextRenderMode::SDF;
            fx.outline = true; fx.outline_color = glm::vec4(0.20f, 0.10f, 0.0f, 1.f); fx.outline_width = 4.f;
            row(U"SDF 描边" + sample, -60.f, gold, &fx);
        }
        // 5) SDF + shadow
        {
            TextEffects fx; fx.mode = TextRenderMode::SDF;
            fx.shadow = true; fx.shadow_color = glm::vec4(0.f, 0.f, 0.f, 0.5f);
            fx.shadow_offset = glm::vec2(4.f, -4.f); fx.shadow_softness = 2.f;
            row(U"SDF 阴影" + sample, -190.f, blue, &fx);
        }
        // 6) SDF + outline + shadow
        {
            TextEffects fx; fx.mode = TextRenderMode::SDF;
            fx.outline = true; fx.outline_color = glm::vec4(0.20f, 0.10f, 0.0f, 1.f); fx.outline_width = 3.5f;
            fx.shadow = true; fx.shadow_color = glm::vec4(0.f, 0.f, 0.f, 0.5f);
            fx.shadow_offset = glm::vec2(5.f, -5.f); fx.shadow_softness = 2.f;
            row(U"SDF 描边+阴影" + sample, -320.f, gold, &fx);
        }

        std::printf("[ecs_text_fx] 6 rows spawned (AA/AA+shadow/SDF/SDF+outline/SDF+shadow/SDF+both)\n");
        std::fflush(stdout);
    });

    app.add_system(Stage::Last, [](EcsWorld& w) {
        static int f = 0;
        if (++f != 60) return;
        auto& reg = w.reg();
        std::size_t total = 0, inst = 0;
        for (auto e : reg.view<BatchComponent>()) {
            auto& b = reg.get<BatchComponent>(e);
            if (!b.instances.empty()) { ++total; inst += b.instances.size(); }
        }
        std::printf("[ecs_text_fx] frame 60: non-empty batches=%zu instances=%zu\n", total, inst);
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
