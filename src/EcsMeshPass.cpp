#include <ecs/render/RenderPassExec.hpp>
#include <ecs/PerformanceMonitoring.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <program.hpp>
#include <texture.hpp>

#include <ecs/Components.hpp>
#include <ecs/render/Lighting.hpp>

namespace gld::ecs {

    void render_mesh_pass(RenderPassContext& ctx, const MeshPass&) {
        auto& w = ctx.world;
        auto& reg = w.reg();
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        const Camera& cam = ctx.camera;
        auto* light_gpu = w.try_resource<LightingGpuResource>();
        auto* light_settings = w.try_resource<LightingSettings>();

        auto view = reg.view<GlobalTransform, MeshHandle, Material>();
        bool needs_lighting = false;
        for (auto e : view) {
            if (auto* vis = reg.try_get<Visibility>(e); vis && !vis->visible) continue;

            std::uint32_t mask = 0x1u;
            if (auto* rl = reg.try_get<RenderLayer>(e)) mask = rl->mask;
            if ((cam.layers & mask) == 0) continue;

            const auto& mat = view.get<Material>(e);
            if (mat.uses_lighting) {
                needs_lighting = true;
                break;
            }
        }
        if (needs_lighting && light_gpu) light_gpu->bind();

        for (auto e : view) {
            if (auto* vis = reg.try_get<Visibility>(e); vis && !vis->visible) continue;

            std::uint32_t mask = 0x1u;
            if (auto* rl = reg.try_get<RenderLayer>(e)) mask = rl->mask;
            if ((cam.layers & mask) == 0) continue;

            auto& mat = view.get<Material>(e);
            if (mat.shader.state() != LoadState::Loaded) {
                GLD_PERF_MONITOR(++diag.mesh_skipped_unloaded);
                continue;
            }
            Program* prog = mat.shader.get();
            if (!prog) {
                GLD_PERF_MONITOR(++diag.mesh_skipped_invalid);
                continue;
            }

            auto& mesh = view.get<MeshHandle>(e);
            if (!mesh.vao || mesh.index_count <= 0) {
                GLD_PERF_MONITOR(++diag.mesh_skipped_invalid);
                continue;
            }

            auto& g = view.get<GlobalTransform>(e);

            prog->use();
            if (prog->uniform_id("projection") == -1)
                prog->locat_uniforms(
                    "projection", "view", "model", "tex", "hasTex", "color",
                    "viewPos", "ambientColor", "ambientStrength", "lightingEnabled",
                    "dirLightCount", "pointLightCount", "spotLightCount", "emissiveStrength");

            glUniformMatrix4fv(prog->uniform_id("projection"), 1, GL_FALSE, glm::value_ptr(cam.projection));
            glUniformMatrix4fv(prog->uniform_id("view"), 1, GL_FALSE, glm::value_ptr(cam.view));
            glUniformMatrix4fv(prog->uniform_id("model"), 1, GL_FALSE, glm::value_ptr(g.world));

            glm::vec3 view_pos = glm::vec3(glm::inverse(cam.view)[3]);
            if (prog->uniform_id("viewPos") >= 0)
                glUniform3fv(prog->uniform_id("viewPos"), 1, glm::value_ptr(view_pos));
            if (prog->uniform_id("ambientStrength") >= 0)
                glUniform1f(prog->uniform_id("ambientStrength"),
                            light_settings ? light_settings->ambient_strength : 0.35f);
            if (prog->uniform_id("ambientColor") >= 0) {
                glm::vec3 ambient = light_gpu ? light_gpu->ambient : glm::vec3(light_settings ? light_settings->ambient_strength : 0.35f);
                glUniform3fv(prog->uniform_id("ambientColor"), 1, glm::value_ptr(ambient));
            }
            if (prog->uniform_id("lightingEnabled") >= 0)
                glUniform1i(prog->uniform_id("lightingEnabled"),
                            (mat.uses_lighting && needs_lighting && light_settings && light_settings->enabled && light_gpu) ? 1 : 0);
            if (prog->uniform_id("dirLightCount") >= 0)
                glUniform1i(prog->uniform_id("dirLightCount"),
                            (needs_lighting && light_gpu) ? static_cast<GLint>(light_gpu->directional.size()) : 0);
            if (prog->uniform_id("pointLightCount") >= 0)
                glUniform1i(prog->uniform_id("pointLightCount"),
                            (needs_lighting && light_gpu) ? static_cast<GLint>(light_gpu->points.size()) : 0);
            if (prog->uniform_id("spotLightCount") >= 0)
                glUniform1i(prog->uniform_id("spotLightCount"),
                            (needs_lighting && light_gpu) ? static_cast<GLint>(light_gpu->spots.size()) : 0);
            if (prog->uniform_id("emissiveStrength") >= 0)
                glUniform1f(prog->uniform_id("emissiveStrength"), mat.emissive_strength);

            int hasTex = 0;
            Texture<TexType::D2>* bindTex = nullptr;
            if (mat.tex_override) bindTex = mat.tex_override.get();
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
            GLD_PERF_MONITOR(++diag.mesh_draws);
        }
    }
}
