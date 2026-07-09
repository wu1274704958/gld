#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <vector>
#include <set>
#include <climits>

#include <vertex_arr.hpp>
#include <program.hpp>
#include <texture.hpp>

#include <ecs/render/RenderSystem.hpp>
#include <ecs/render/BatchSystem.hpp>   // draw_batches
#include <ecs/Components.hpp>
#include <ecs/Window.hpp>

namespace gld::ecs {

    // Allocate contiguous, gap-filling ids to any camera with id < 0. Ids of
    // destroyed cameras disappear from the live set, so the next spawn reuses
    // the smallest free id.
    void spawn_camera_system(EcsWorld& w) {
        auto& reg = w.reg();
        std::set<int> used;
        for (auto e : reg.view<Camera>()) {
            int id = reg.get<Camera>(e).id;
            if (id >= 0) used.insert(id);
        }
        for (auto e : reg.view<Camera>()) {
            auto& c = reg.get<Camera>(e);
            if (c.id < 0) {
                int n = 0;
                while (used.count(n)) ++n;
                c.id = n;
                used.insert(n);
            }
        }
    }

    void camera_matrices_system(EcsWorld& w) {
        auto& reg = w.reg();
        int ww = 1, wh = 1;
        if (auto* win = w.try_resource<Window>()) { ww = win->width; wh = win->height; }

        for (auto e : reg.view<Camera>()) {
            auto& c = reg.get<Camera>(e);
            int tw = c.target_size.x > 0 ? c.target_size.x : ww;
            int th = c.target_size.y > 0 ? c.target_size.y : wh;

            if (c.kind == CameraKind::Perspective) {
                float aspect = th > 0 ? static_cast<float>(tw) / static_cast<float>(th) : 1.f;
                c.projection = glm::perspective(glm::radians(c.fov), aspect, c.near_z, c.far_z);
            } else {
                float W = static_cast<float>(tw), H = static_cast<float>(th);
                c.projection = glm::ortho(-W * 0.5f, W * 0.5f, -H * 0.5f, H * 0.5f, -4000.f, 4000.f);
            }
        }
    }

    // 3D mesh pass for one perspective camera (layer-filtered).
    static void draw_meshes(EcsWorld& w, const Camera& cam) {
        auto& reg = w.reg();
        auto view = reg.view<GlobalTransform, MeshHandle, Material>();
        for (auto e : view) {
            if (auto* vis = reg.try_get<Visibility>(e); vis && !vis->visible) continue;

            std::uint32_t mask = 0x1u;
            if (auto* rl = reg.try_get<RenderLayer>(e)) mask = rl->mask;
            if ((cam.layers & mask) == 0) continue;

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
            Texture<TexType::D2>* bindTex = nullptr;
            if (mat.tex_override)                            bindTex = mat.tex_override.get();
            else if (mat.texture.state() == LoadState::Loaded) bindTex = mat.texture.get();
            if (bindTex) {
                bindTex->active<ActiveTexId::_0>();
                glUniform1i(prog->uniform_id("tex"), 0);
                hasTex = 1;
            }
            glUniform1i(prog->uniform_id("hasTex"), hasTex);
            glUniform4fv(prog->uniform_id("color"), 1, glm::value_ptr(mat.color));

            mesh.vao->bind();
            glDrawElements(mesh.mode, mesh.index_count, GL_UNSIGNED_INT, nullptr);
            mesh.vao->unbind();
        }
    }

    void render_system(EcsWorld& w) {
        auto& reg = w.reg();
        int ww = 1, wh = 1;
        if (auto* win = w.try_resource<Window>()) { ww = win->width; wh = win->height; }

        // Cameras sorted by priority = render dependency order (offscreen first).
        std::vector<entt::entity> cams;
        for (auto e : reg.view<Camera>()) cams.push_back(e);
        std::sort(cams.begin(), cams.end(), [&](entt::entity a, entt::entity b) {
            return reg.get<Camera>(a).priority < reg.get<Camera>(b).priority;
        });

        for (auto e : cams) {
            const Camera& cam = reg.get<Camera>(e);
            int tw = cam.target_size.x > 0 ? cam.target_size.x : ww;
            int th = cam.target_size.y > 0 ? cam.target_size.y : wh;

            glBindFramebuffer(GL_FRAMEBUFFER, cam.target);
            glViewport(0, 0, tw, th);

            if (cam.kind == CameraKind::Ortho) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_BLEND);
                glEnable(GL_DEPTH_TEST);
            }

            if (cam.do_clear) {
                glClearColor(cam.clear_color.r, cam.clear_color.g, cam.clear_color.b, cam.clear_color.a);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }

            if (cam.kind == CameraKind::Perspective) draw_meshes(w, cam);
            else                                     draw_batches(w, cam);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // Primary window camera = target 0 with the highest priority: it owns the
        // present. Insert/refresh a fence for present_system to wait on.
        entt::entity primary = entt::null;
        int best = INT_MIN;
        for (auto e : cams) {
            const Camera& c = reg.get<Camera>(e);
            if (c.target == 0 && c.priority >= best) { best = c.priority; primary = e; }
        }
        if (primary != entt::null) {
            GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            if (auto* wp = reg.try_get<WaitPresent>(primary)) {
                if (wp->fence) glDeleteSync(static_cast<GLsync>(wp->fence));
                wp->fence = fence;
            } else {
                reg.emplace<WaitPresent>(primary, WaitPresent{ static_cast<void*>(fence) });
            }
        }
    }

    void present_system(EcsWorld& w) {
        auto& reg = w.reg();
        auto* win = w.try_resource<Window>();
        GLFWwindow* handle = win ? win->handle : nullptr;

        std::vector<entt::entity> ents;
        for (auto e : reg.view<WaitPresent>()) ents.push_back(e);

        bool presented = false;
        for (auto e : ents) {
            auto& wp = reg.get<WaitPresent>(e);
            if (wp.fence) {
                // Present gate: wait until submitted GPU work completes.
                glClientWaitSync(static_cast<GLsync>(wp.fence), GL_SYNC_FLUSH_COMMANDS_BIT, 50000000ull);
                glDeleteSync(static_cast<GLsync>(wp.fence));
                wp.fence = nullptr;
            }
            if (handle && !presented) { glfwSwapBuffers(handle); presented = true; }
            reg.remove<WaitPresent>(e);
        }
        if (presented && win) win->presented = true;
    }

    void RenderPlugin(App& app) {
        app.add_system(Stage::First, spawn_camera_system);
        app.add_system(Stage::PostUpdate, camera_matrices_system);
        app.add_system(Stage::Render, render_system);
        app.add_system(Stage::Last, present_system);
    }
}
