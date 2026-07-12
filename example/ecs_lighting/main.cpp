// ecs_lighting - ECS directional / point / spot lighting demo.
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
#include <ecs/render/Lighting.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

struct SpinSystem : BaseSystem<SpinSystem, Transform, AutoRotate> {
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
    app.add_plugin(WindowPlugin{ 1280, 800, "ecs_lighting - directional + point + spot" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root));
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);
    app.add_plugin(TransformPlugin);
    app.add_plugin(LightingPlugin);
    app.add_plugin(InputPlugin);
    app.add_plugin(RenderPlugin);
    app.add_system<SpinSystem>(Stage::Update);

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        auto shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_fg.glsl");
        auto tex = srv.load_texture("textures/container.jpg");
        auto cube = MeshFactory::cube();

        auto& settings = w.resource<LightingSettings>();
        settings.ambient_strength = 0.12f;

        entt::entity camE = w.spawn();
        Camera cam;
        cam.kind = CameraKind::Perspective;
        cam.priority = 0;
        cam.target = 0;
        cam.fov = 60.f;
        cam.clear_color = glm::vec4(0.025f, 0.03f, 0.045f, 1.f);
        cam.view = glm::lookAt(glm::vec3(0.f, 3.2f, 9.f), glm::vec3(0.f, 0.5f, 0.f), glm::vec3(0.f, 1.f, 0.f));
        reg.emplace<Camera>(camE, cam);
        emplace_render_passes<MeshPass>(w, camE);

        for (int z = -2; z <= 2; ++z) {
            for (int x = -3; x <= 3; ++x) {
                entt::entity e = w.spawn();
                Transform tr;
                tr.translation = glm::vec3(static_cast<float>(x) * 1.45f, 0.f, static_cast<float>(z) * 1.35f);
                tr.scale = glm::vec3(0.55f);
                reg.emplace<Transform>(e, tr);
                reg.emplace<GlobalTransform>(e);
                reg.emplace<MeshHandle>(e, cube);
                glm::vec4 tint(0.55f + 0.06f * (x + 3), 0.62f + 0.05f * (z + 2), 0.90f, 1.f);
                reg.emplace<Material>(e, Material{ shader, tex, tint });
                reg.emplace<AutoRotate>(e, AutoRotate{ glm::vec3(0.15f, 0.35f + 0.03f * x, 0.f) });
            }
        }

        entt::entity sun = w.spawn();
        reg.emplace<DirectionalLight>(sun, DirectionalLight{
            glm::vec3(-0.4f, -1.0f, -0.25f), glm::vec3(1.0f, 0.94f, 0.82f), 0.7f
        });

        auto make_point = [&](glm::vec3 pos, glm::vec3 color) {
            entt::entity e = w.spawn();
            Transform tr; tr.translation = pos;
            reg.emplace<Transform>(e, tr);
            reg.emplace<GlobalTransform>(e);
            reg.emplace<PointLight>(e, PointLight{ color, 2.4f, 6.0f });
        };
        make_point({ -3.5f, 2.2f,  1.5f }, { 1.0f, 0.25f, 0.18f });
        make_point({  0.0f, 2.8f, -2.0f }, { 0.25f, 0.65f, 1.0f });
        make_point({  3.5f, 2.1f,  1.0f }, { 0.35f, 1.0f, 0.35f });

        entt::entity spot = w.spawn();
        Transform st;
        st.translation = glm::vec3(0.f, 5.f, 5.f);
        st.rotation.x = -0.75f;
        reg.emplace<Transform>(spot, st);
        reg.emplace<GlobalTransform>(spot);
        reg.emplace<SpotLight>(spot, SpotLight{
            glm::vec3(1.0f, 0.95f, 0.70f), 3.0f, 11.0f, 0.30f, 0.62f
        });

        std::printf("[ecs_lighting] spawned cubes + directional/point/spot lights\n");
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
