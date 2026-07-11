// ECS demo: parent-child hierarchy + async asset loading + input + multi-camera
// rendering. A textured parent cube spins; child cubes are parented so they
// orbit. The Camera is now a COMPONENT (an entity), driven by an orbit control.
#define _CRT_SECURE_NO_WARNINGS
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <resource_mgr.hpp>
#include <FindPath.hpp>
#include <cstdio>
#include <memory>
#include <cmath>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
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
#include <ecs/render/MeshFactory.hpp>

using namespace gld::ecs;
namespace fs = std::filesystem;

// Orbit parameters attached to a camera entity.
struct OrbitControl { float yaw = 0.6f, pitch = 0.4f, dist = 7.f; glm::vec3 target{ 0.f }; };

// BaseSystem demo: spin entities by their AutoRotate speed (reads Time resource).
struct AutoRotateSystem : BaseSystem<AutoRotateSystem, Transform, AutoRotate> {
    void Update(entt::entity, Transform& t, AutoRotate& r) {
        t.rotation += r.speed * res<Time>().dt;
    }
};

// Left-drag orbits every camera that has an OrbitControl; R resets. Writes the
// camera's view matrix directly.
static void camera_orbit_system(EcsWorld& w) {
    auto& mouse = w.resource_or_add<MouseButtons>();
    auto& cur = w.resource_or_add<CursorPosition>();
    auto& kb = w.resource_or_add<Keyboard>();
    auto& reg = w.reg();

    for (auto e : reg.view<Camera, OrbitControl>()) {
        auto& cam = reg.get<Camera>(e);
        auto& orb = reg.get<OrbitControl>(e);

        if (mouse.is_pressed(GLFW_MOUSE_BUTTON_LEFT)) {
            orb.yaw -= cur.delta.x * 0.005f;
            orb.pitch += cur.delta.y * 0.005f;
            orb.pitch = glm::clamp(orb.pitch, -1.4f, 1.4f);
        }
        if (kb.just_now_pressed(GLFW_KEY_R)) { orb.yaw = 0.6f; orb.pitch = 0.4f; orb.dist = 7.f; }

        float cp = std::cos(orb.pitch), sp = std::sin(orb.pitch);
        float cy = std::cos(orb.yaw),   sy = std::sin(orb.yaw);
        glm::vec3 dir(cp * sy, sp, cp * cy);
        glm::vec3 eye = orb.target + orb.dist * dir;
        cam.view = glm::lookAt(eye, orb.target, glm::vec3(0.f, 1.f, 0.f));
    }
}

int main()
{
    fs::path root = wws::find_path(3, "res", true);
    gld::ResMgrWithGlslPreProcess::create_instance(root);
    gld::DefResMgr::create_instance(root);

    App app;
    app.add_plugin(WindowPlugin{ 1024, 768, "ecs_demo" });
    FileSystemPlugin(app, std::make_shared<StdFileSystem>(root)); // asset FS root = res/
    app.add_plugin(AssetPlugin);
    app.add_plugin(CorePlugin);       // Time
    app.add_plugin(TransformPlugin);  // hierarchy propagation
    app.add_plugin(InputPlugin);
    app.add_plugin(RenderPlugin);     // multi-camera + present
    app.add_system<AutoRotateSystem>(Stage::Update);
    app.add_system(Stage::Update, camera_orbit_system);

    app.add_system(Stage::Startup, [](EcsWorld& w) {
        auto& srv = w.resource<AssetServer>();
        auto& reg = w.reg();

        MeshHandle mesh = MeshFactory::cube();
        auto shader = srv.load_program("ecs/mesh_vs.glsl", "ecs/mesh_fg.glsl");
        auto tex = srv.load_texture("textures/container.jpg");

        // Camera entity (perspective, renders to the window).
        entt::entity camE = w.spawn();
        Camera cam; cam.kind = CameraKind::Perspective; cam.priority = 0; cam.target = 0; cam.fov = 60.f;
        reg.emplace<Camera>(camE, cam);
        emplace_render_passes<MeshPass>(w, camE);
        reg.emplace<OrbitControl>(camE, OrbitControl{});

        // Parent cube (spins about Y).
        entt::entity parent = w.spawn();
        reg.emplace<Transform>(parent, Transform{});
        reg.emplace<GlobalTransform>(parent);
        reg.emplace<MeshHandle>(parent, mesh);
        reg.emplace<Material>(parent, Material{ shader, tex, glm::vec4(1.f) });
        reg.emplace<AutoRotate>(parent, AutoRotate{ glm::vec3(0.f, 0.8f, 0.f) });
        reg.emplace<Children>(parent);

        // Child cubes parented to it -> they orbit as the parent spins.
        const int N = 4;
        const glm::vec4 tints[4] = {
            {1.f,0.5f,0.4f,1.f}, {0.4f,1.f,0.6f,1.f},
            {0.5f,0.6f,1.f,1.f}, {1.f,0.9f,0.4f,1.f}
        };
        for (int i = 0; i < N; ++i) {
            float a = (float)i / N * glm::two_pi<float>();
            entt::entity c = w.spawn();
            Transform ct;
            ct.translation = glm::vec3(std::cos(a) * 2.4f, 0.f, std::sin(a) * 2.4f);
            ct.scale = glm::vec3(0.5f);
            reg.emplace<Transform>(c, ct);
            reg.emplace<GlobalTransform>(c);
            reg.emplace<Parent>(c, Parent{ parent });
            reg.emplace<MeshHandle>(c, mesh);
            reg.emplace<Material>(c, Material{ shader, tex, tints[i] });
            reg.emplace<AutoRotate>(c, AutoRotate{ glm::vec3(0.6f, 0.6f, 0.f) });
            reg.get<Children>(parent).value.push_back(c);
        }
        std::printf("[ECS_DEMO] scene spawned: 1 camera + 1 parent + %d children\n", N);
        std::fflush(stdout);
    });

    run_app(app);
    return 0;
}
