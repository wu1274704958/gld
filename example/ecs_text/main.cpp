// ecs_text – ECS text-rendering demo. A short classical poem is laid out in
// screen space (each line its own Text entity) plus a strip of many small
// labels, all sharing one font/size. The generic batch collector groups every
// glyph that shares the same atlas + shader into a SINGLE instanced draw, so the
// whole screen collapses to one (or very few) batches — printed at frame 60.
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
    app.add_plugin(WindowPlugin{ 1280, 800, "ecs_text - screen-space text + global batching" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root)); // asset FS root = res/
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);       // Time
    app.add_plugin(TransformPlugin);  // GlobalTransform propagation
    app.add_plugin(TextPlugin);       // font loader + glyph atlas + layout
    TextBatchPlugin(app);             // text -> BatchComponent collection
    app.add_plugin(RenderPlugin);     // multi-camera render + present

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        // 2D camera: ortho screen space, renders the text batches to the window.
        entt::entity camE = w.spawn();
        Camera cam; cam.kind = CameraKind::Ortho; cam.priority = 0; cam.target = 0;
        reg.emplace<Camera>(camE, cam);
        emplace_render_passes<BatchPass>(w, camE);

        Handle<FontAsset> font = srv.load(FontDesc(std::string("fonts/simkai.ttf"), 0));

        auto make_text = [&](const std::u32string& s, int size, glm::vec3 pos,
                             glm::vec4 color, TextAlign align = TextAlign::Center) {
            entt::entity e = w.spawn();
            Text t;
            t.text = s; t.font = font; t.size = size; t.color = color;
            t.align = align; t.anchor = glm::vec2(0.5f, 0.5f); t.rev = 1;
            Transform tr = Transform::from_trs(pos);
            reg.emplace<Text>(e, std::move(t));
            reg.emplace<Transform>(e, tr);
            reg.emplace<GlobalTransform>(e);
            return e;
        };

        // --- poem block (each line an independent Text entity) ---
        const glm::vec4 gold(0.98f, 0.90f, 0.62f, 1.f);
        const glm::vec4 dim (0.72f, 0.74f, 0.80f, 1.f);
        const glm::vec4 ink (0.93f, 0.92f, 0.88f, 1.f);

        make_text(U"静夜思",              96, { 0.f,  250.f, 0.f }, gold);
        make_text(U"〔唐〕李白",          40, { 0.f,  170.f, 0.f }, dim);
        make_text(U"床前明月光，疑是地上霜。", 64, { 0.f,   70.f, 0.f }, ink);
        make_text(U"举头望明月，低头思故乡。", 64, { 0.f,  -10.f, 0.f }, ink);

        // --- many small labels sharing the SAME font+size: they must all end up
        //     in ONE batch with the poem (same atlas + same shader). ---
        int count = 0;
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 12; ++c) {
                std::u32string s = U"字";
                s[0] = U'一' + (char32_t)((r * 12 + c) % 40);
                glm::vec3 p(-560.f + c * 100.f, -180.f - r * 60.f, 0.f);
                make_text(s, 44, p, glm::vec4(0.55f, 0.75f, 1.f, 1.f), TextAlign::Center);
                ++count;
            }
        }
        std::printf("[ecs_text] spawned 4 poem lines + %d labels\n", count);
        std::fflush(stdout);
    });

    // Diagnostic: after a warm-up, report how many batches / instances the whole
    // scene collapsed into (proves the global batching).
    app.add_system(Stage::Last, [](EcsWorld& w) {
        static int f = 0;
        if (++f != 60) return;
        auto& reg = w.reg();
        std::size_t total = 0, nonempty = 0, inst = 0;
        for (auto e : reg.view<BatchComponent>()) {
            auto& b = reg.get<BatchComponent>(e);
            ++total; inst += b.instances.size();
            if (!b.instances.empty()) ++nonempty;
        }
        std::printf("[ecs_text] frame 60: batches=%zu (non-empty=%zu) instances=%zu\n",
                    total, nonempty, inst);
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
