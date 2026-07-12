// ecs_multicam - multi-camera demo showing the new render pipeline:
//   * Camera A (perspective, priority 0)  -> OFFSCREEN FBO, renders a spinning
//                                            textured cube (layer 1).
//   * Camera B (perspective, priority 10) -> window, renders a quad textured
//                                            with A's offscreen colour (layer 2).
//                                            This read-after-write is the render
//                                            dependency (A must run before B).
//   * Camera C (ortho,       priority 20) -> window overlay, renders a text HUD.
// One present at the end (present_system). Priority orders the passes; layer
// masks separate cube vs quad; camera kind separates mesh vs text.
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <cstdio>
#include <memory>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <ecs/App.hpp>
#include <ecs/Window.hpp>
#include <ecs/Input.hpp>
#include <ecs/Components.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/text/FontAsset.hpp>
#include <ecs/text/TextComponents.hpp>
#include <ecs/text/TextSystems.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/render/RenderTarget.hpp>
#include <ecs/render/MeshFactory.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Lighting.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

struct AutoRotateSystem : BaseSystem<AutoRotateSystem, Transform, AutoRotate> {
    void Update(entt::entity, Transform& t, AutoRotate& r) {
        t.rotation += r.speed * res<Time>().dt;
    }
};

int main()
{
    fs::path root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{ 1280, 800, "ecs_multicam - offscreen + main + 2D HUD" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(LightingPlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(TextPlugin);
    TextBatchPlugin(app);
    app.add_plugin(RenderPlugin);
    app.add_system<AutoRotateSystem>(Stage::Update);

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        auto mesh_shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_fg.glsl");
        auto unlit_shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_unlit_fg.glsl");
        auto tex = srv.load_texture("textures/container.jpg");
        auto cube = MeshFactory::cube();
        auto quad = MeshFactory::quad(3.2f);

        reg.emplace<AmbientLight>(w.spawn(), AmbientLight{ glm::vec3(1.f), 0.20f });
        reg.emplace<DirectionalLight>(w.spawn(), DirectionalLight{
            glm::vec3(-0.30f, -1.0f, -0.35f), glm::vec3(1.0f, 0.94f, 0.84f), 0.85f
        });

        // Offscreen render target sampled by camera B.
        auto rt = create_render_target(512, 512);
        w.add_resource<std::shared_ptr<RenderTarget>>(rt); // keep alive

        // Camera A: perspective, renders the cube (layer 1) into the FBO.
        {
            entt::entity a = w.spawn();
            Camera cam;
            cam.kind = CameraKind::Perspective; cam.priority = 0;
            cam.target = rt->fbo; cam.target_size = glm::ivec2(rt->width, rt->height);
            cam.layers = 0x1u;
            cam.clear_color = glm::vec4(0.10f, 0.12f, 0.18f, 1.f);
            cam.view = glm::lookAt(glm::vec3(0.f, 1.4f, 3.6f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
            reg.emplace<Camera>(a, cam);
            emplace_render_passes<MeshPass>(w, a);
        }
        // Camera B: perspective, renders the RT-textured quad (layer 2) to window.
        {
            entt::entity b = w.spawn();
            Camera cam;
            cam.kind = CameraKind::Perspective; cam.priority = 10;
            cam.target = 0; cam.layers = 0x2u;
            cam.clear_color = glm::vec4(0.04f, 0.05f, 0.08f, 1.f);
            cam.view = glm::lookAt(glm::vec3(0.f, 0.f, 4.2f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
            reg.emplace<Camera>(b, cam);
            emplace_render_passes<MeshPass>(w, b);
        }
        // Camera C: ortho HUD overlay (does not clear).
        {
            entt::entity c = w.spawn();
            Camera cam;
            cam.kind = CameraKind::Ortho; cam.priority = 20;
            cam.target = 0; cam.layers = 0x4u; cam.do_clear = false;
            reg.emplace<Camera>(c, cam);
            emplace_render_passes<BatchPass>(w, c);
        }

        // Spinning cube -> layer 1 (only camera A sees it).
        {
            entt::entity e = w.spawn();
            reg.emplace<Transform>(e, Transform{});
            reg.emplace<GlobalTransform>(e);
            reg.emplace<MeshHandle>(e, cube);
            reg.emplace<Material>(e, Material{ mesh_shader, tex, glm::vec4(1.f) });
            reg.emplace<AutoRotate>(e, AutoRotate{ glm::vec3(0.4f, 0.7f, 0.f) });
            reg.emplace<RenderLayer>(e, RenderLayer{ 0x1u });
        }
        // Quad textured with the offscreen result -> layer 2 (only camera B).
        {
            entt::entity e = w.spawn();
            reg.emplace<Transform>(e, Transform{});
            reg.emplace<GlobalTransform>(e);
            reg.emplace<MeshHandle>(e, quad);
            Material m; m.shader = unlit_shader; m.color = glm::vec4(1.f); m.tex_override = rt->color; m.uses_lighting = false;
            reg.emplace<Material>(e, m);
            reg.emplace<AutoRotate>(e, AutoRotate{ glm::vec3(0.f, 0.35f, 0.f) });
            reg.emplace<RenderLayer>(e, RenderLayer{ 0x2u });
        }

        // Text HUD -> ortho camera C.
        {
            Handle<FontAsset> font = srv.load(FontDesc(std::string("fonts/simkai.ttf"), 0));
            entt::entity e = w.spawn();
            Text t;
            t.text = U"多相机：A 离屏 → B 采样 → C 文字叠加";
            t.font = font; t.size = 40; t.color = glm::vec4(0.98f, 0.92f, 0.7f, 1.f);
            t.align = TextAlign::Center; t.anchor = glm::vec2(0.5f, 0.5f); t.rev = 1;
            reg.emplace<Text>(e, std::move(t));
            Transform tr; tr.translation = glm::vec3(0.f, 330.f, 0.f);
            reg.emplace<Transform>(e, tr);
            reg.emplace<GlobalTransform>(e);
            reg.emplace<RenderLayer>(e, RenderLayer{ 0x4u });
        }

        std::printf("[ecs_multicam] 3 cameras (offscreen->quad->HUD) spawned\n");
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
