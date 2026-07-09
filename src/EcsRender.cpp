#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

#include <vertex_arr.hpp>
#include <program.hpp>
#include <texture.hpp>

#include <ecs/render/RenderSystem.hpp>
#include <ecs/Components.hpp>
#include <ecs/Window.hpp>

namespace gld::ecs {

    void camera_system(EcsWorld& w) {
        auto& cam = w.resource_or_add<Camera>();
        int width = 1, height = 1;
        if (auto* win = w.try_resource<Window>()) { width = win->width; height = win->height; }
        float aspect = (height > 0) ? (float)width / (float)height : 1.f;

        cam.projection = glm::perspective(glm::radians(cam.fov), aspect, 0.1f, 200.f);

        float cp = std::cos(cam.pitch), sp = std::sin(cam.pitch);
        float cy = std::cos(cam.yaw),   sy = std::sin(cam.yaw);
        glm::vec3 dir(cp * sy, sp, cp * cy);
        glm::vec3 eye = cam.target + cam.dist * dir;
        cam.view = glm::lookAt(eye, cam.target, glm::vec3(0.f, 1.f, 0.f));
    }

    void render_system(EcsWorld& w) {
        auto& cam = w.resource_or_add<Camera>();

        glEnable(GL_DEPTH_TEST);
        glClearColor(0.06f, 0.07f, 0.09f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        auto& reg = w.reg();
        auto view = reg.view<GlobalTransform, MeshHandle, Material>();
        for (auto e : view) {
            if (auto* vis = reg.try_get<Visibility>(e); vis && !vis->visible) continue;

            auto& mat = view.get<Material>(e);
            if (mat.shader.state() != LoadState::Loaded) continue;
            Program* prog = mat.shader.get();
            if (!prog) continue;

            auto& mesh = view.get<MeshHandle>(e);
            if (!mesh.vao || mesh.index_count <= 0) continue;

            auto& g = view.get<GlobalTransform>(e);

            prog->use();
            if (prog->uniform_id("projection") == -1)
                prog->locat_uniforms("projection", "view", "model", "tex", "hasTex", "color");

            glUniformMatrix4fv(prog->uniform_id("projection"), 1, GL_FALSE, glm::value_ptr(cam.projection));
            glUniformMatrix4fv(prog->uniform_id("view"), 1, GL_FALSE, glm::value_ptr(cam.view));
            glUniformMatrix4fv(prog->uniform_id("model"), 1, GL_FALSE, glm::value_ptr(g.world));

            int hasTex = 0;
            if (mat.texture.state() == LoadState::Loaded) {
                if (auto* t = mat.texture.get()) {
                    t->active<ActiveTexId::_0>();
                    glUniform1i(prog->uniform_id("tex"), 0);
                    hasTex = 1;
                }
            }
            glUniform1i(prog->uniform_id("hasTex"), hasTex);
            glUniform4fv(prog->uniform_id("color"), 1, glm::value_ptr(mat.color));

            mesh.vao->bind();
            glDrawElements(mesh.mode, mesh.index_count, GL_UNSIGNED_INT, nullptr);
            mesh.vao->unbind();
        }
    }

    void RenderPlugin(App& app) {
        app.world.resource_or_add<Camera>();
        app.add_system(Stage::PostUpdate, camera_system);
        app.add_system(Stage::Render, render_system);
    }
}
