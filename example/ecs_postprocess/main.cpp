// ecs_postprocess - graph-backed post-processing demo.
// A normal scene camera is redirected to an offscreen target by
// PostProcessManager, then Fog and Bloom descriptors expand into fullscreen
// graph passes and present the final composite to the window.
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <ecs/App.hpp>
#include <ecs/Window.hpp>
#include <ecs/Input.hpp>
#include <ecs/Components.hpp>
#include <ecs/systems/TransformSystem.hpp>
#include <ecs/assets/FileSystem.hpp>
#include <ecs/assets/AssetServer.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderSystem.hpp>
#include <ecs/render/MeshFactory.hpp>
#include <ecs/render/PostProcess.hpp>
#include <ecs/render/Lighting.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

struct SpinSystem : BaseSystem<SpinSystem, Transform, AutoRotate> {
    void Update(entt::entity, Transform& t, AutoRotate& r) {
        t.rotation += r.speed * res<Time>().dt;
    }
};

struct PostHandles {
    entt::entity camera = entt::null;
    PostProcessHandle effect;
    bool enabled = true;
};

static void toggle_post_system(EcsWorld& w) {
    auto& kb = w.resource_or_add<Keyboard>();
    auto* handles = w.try_resource<PostHandles>();
    auto* ppm = w.try_resource<PostProcessManager>();
    if (!handles || !ppm) return;

    bool changed = false;
    if (kb.just_now_pressed(GLFW_KEY_P)) {
        handles->enabled = !handles->enabled;
        changed = true;
    }
    if (!changed) return;

    ppm->set_enabled(handles->effect, handles->enabled);
    std::printf("[ecs_postprocess] dag fog+bloom=%s\n", handles->enabled ? "on" : "off");
    std::fflush(stdout);
}

int main()
{
    fs::path root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{ 1280, 800, "ecs_postprocess - RenderGraph post effects (P toggles)" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(LightingPlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(PostProcessPlugin);
    app.add_plugin(RenderPlugin);
    app.add_system<SpinSystem>(Stage::Update);
    app.add_system(Stage::Update, toggle_post_system);

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        auto shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_fg.glsl");
        auto emissive_shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_emissive_fg.glsl");
        auto tex = srv.load_texture("textures/container.jpg");
        auto cube = MeshFactory::cube();

        reg.emplace<AmbientLight>(w.spawn(), AmbientLight{ glm::vec3(1.f), 0.18f });
        reg.emplace<DirectionalLight>(w.spawn(), DirectionalLight{
            glm::vec3(-0.25f, -1.0f, -0.35f), glm::vec3(1.0f, 0.95f, 0.82f), 0.75f
        });

        entt::entity camE = w.spawn();
        Camera cam;
        cam.kind = CameraKind::Perspective;
        cam.priority = 0;
        cam.target = 0;
        cam.fov = 60.f;
        cam.near_z = 0.1f;
        cam.far_z = 90.f;
        cam.clear_color = glm::vec4(0.015f, 0.018f, 0.028f, 1.f);
        cam.view = glm::lookAt(glm::vec3(0.f, 3.0f, 12.f), glm::vec3(0.f, 1.1f, -16.f), glm::vec3(0.f, 1.f, 0.f));
        reg.emplace<Camera>(camE, cam);
        emplace_render_passes<MeshPass>(w, camE);

        auto make_box = [&](glm::vec3 pos, glm::vec3 scale, glm::vec4 color, bool textured, glm::vec3 spin) {
            entt::entity e = w.spawn();
            Transform tr;
            tr.translation = pos;
            tr.scale = scale;
            reg.emplace<Transform>(e, tr);
            reg.emplace<GlobalTransform>(e);
            reg.emplace<MeshHandle>(e, cube);
            Material mat;
            mat.shader = shader;
            mat.color = color;
            if (textured) mat.texture = tex;
            reg.emplace<Material>(e, mat);
            if (spin != glm::vec3(0.f))
                reg.emplace<AutoRotate>(e, AutoRotate{ spin });
            return e;
        };
        auto make_emissive_box = [&](glm::vec3 pos, glm::vec3 scale, glm::vec4 color, float strength, glm::vec3 spin) {
            entt::entity e = make_box(pos, scale, color, false, spin);
            auto& mat = reg.get<Material>(e);
            mat.shader = emissive_shader;
            mat.uses_lighting = false;
            mat.emissive_strength = strength;
            return e;
        };

        // Ground and depth markers.
        make_box({ 0.f, -0.62f, -13.f }, { 10.f, 0.08f, 34.f }, { 0.11f, 0.12f, 0.16f, 1.f }, false, {});
        for (int i = 0; i < 9; ++i) {
            float z = 4.f - static_cast<float>(i) * 4.f;
            make_box({ -4.8f, -0.25f, z }, { 0.08f, 0.45f, 1.3f }, { 0.22f, 0.24f, 0.30f, 1.f }, false, {});
            make_box({  4.8f, -0.25f, z }, { 0.08f, 0.45f, 1.3f }, { 0.22f, 0.24f, 0.30f, 1.f }, false, {});
        }

        // Near / mid / far objects so depth fog is obvious.
        make_box({ -2.4f, 0.2f,  3.0f }, { 0.9f, 0.9f, 0.9f }, { 0.95f, 0.86f, 0.70f, 1.f }, true,  { 0.25f, 0.45f, 0.f });
        make_box({  0.2f, 0.2f, -8.0f }, { 1.2f, 1.2f, 1.2f }, { 0.70f, 0.85f, 1.00f, 1.f }, true,  { 0.18f, 0.38f, 0.f });
        make_box({  2.4f, 0.2f, -23.f }, { 1.5f, 1.5f, 1.5f }, { 0.65f, 0.75f, 0.95f, 1.f }, true,  { 0.12f, 0.25f, 0.f });
        make_box({ -1.2f, 0.2f, -35.f }, { 1.8f, 1.8f, 1.8f }, { 0.55f, 0.65f, 0.90f, 1.f }, true,  { 0.08f, 0.20f, 0.f });

        // Bright bloom emitters.
        make_emissive_box({  3.1f, 1.1f,  0.0f }, { 0.32f, 0.32f, 0.32f }, { 1.0f, 0.92f, 0.22f, 1.f }, 2.8f, { 0.f, 1.5f, 0.f });
        make_emissive_box({ -3.0f, 1.4f, -9.0f }, { 0.40f, 0.40f, 0.40f }, { 1.0f, 0.55f, 0.12f, 1.f }, 3.2f, { 0.f, 1.2f, 0.f });
        make_emissive_box({  3.0f, 1.7f, -18.f }, { 0.50f, 0.50f, 0.50f }, { 0.75f, 0.95f, 1.0f, 1.f }, 3.5f, { 0.f, 1.0f, 0.f });

        auto& ppm = w.resource<PostProcessManager>();
        auto effect = ppm.add_post_process(camE, FogBloomPostProcessDesc{
            glm::vec4(0.28f, 0.34f, 0.45f, 1.f),
            7.0f,
            38.0f,
            0.018f,
            0.96f,
            0.62f,
            0.28f,
            2.2f,
            1.10f,
            0.55f
            });
        w.add_resource<PostHandles>(PostHandles{ camE, effect, true });

        std::printf("[ecs_postprocess] DAG depth fog + bloom scene spawned; P toggles effect\n");
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
