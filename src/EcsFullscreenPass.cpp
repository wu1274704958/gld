#include <ecs/render/RenderPassExec.hpp>

#include <glm/gtc/type_ptr.hpp>

#include <program.hpp>
#include <texture.hpp>

namespace gld::ecs {

    void destroy_fullscreen_resources_gpu(FullscreenResources& res) {
        if (res.ebo != 0) {
            glDeleteBuffers(1, &res.ebo);
            res.ebo = 0;
        }
        if (res.vbo != 0) {
            glDeleteBuffers(1, &res.vbo);
            res.vbo = 0;
        }
        if (res.vao != 0) {
            glDeleteVertexArrays(1, &res.vao);
            res.vao = 0;
        }
        res.ready = false;
    }

    FullscreenResources::~FullscreenResources() {
        destroy_fullscreen_resources_gpu(*this);
    }

    namespace {
        void ensure_fullscreen_quad(FullscreenResources& res) {
            if (res.ready) return;

            static const float verts[] = {
                -1.f, -1.f, 0.f, 0.f,
                 1.f, -1.f, 1.f, 0.f,
                 1.f,  1.f, 1.f, 1.f,
                -1.f,  1.f, 0.f, 1.f,
            };
            static const unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };

            glGenVertexArrays(1, &res.vao);
            glGenBuffers(1, &res.vbo);
            glGenBuffers(1, &res.ebo);

            glBindVertexArray(res.vao);
            glBindBuffer(GL_ARRAY_BUFFER, res.vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(0));
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

            res.index_count = 6;
            res.ready = true;
        }

        void set_fullscreen_uniform(Program& prog, const FullscreenUniform& uniform) {
            if (prog.uniform_id(uniform.name) == -1)
                prog.locat_uniforms(uniform.name);

            const int loc = prog.uniform_id(uniform.name);
            if (loc < 0) return;

            std::visit([&](const auto& value) {
                using V = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<V, int>) glUniform1i(loc, value);
                else if constexpr (std::is_same_v<V, float>) glUniform1f(loc, value);
                else if constexpr (std::is_same_v<V, glm::vec2>) glUniform2fv(loc, 1, glm::value_ptr(value));
                else if constexpr (std::is_same_v<V, glm::vec3>) glUniform3fv(loc, 1, glm::value_ptr(value));
                else if constexpr (std::is_same_v<V, glm::vec4>) glUniform4fv(loc, 1, glm::value_ptr(value));
            }, uniform.value);
        }
    }

    void render_fullscreen_pass(RenderPassContext& ctx, const FullscreenRenderPass&) {
        auto& w = ctx.world;
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        auto& res = w.resource_or_add<FullscreenResources>();
        ensure_fullscreen_quad(res);

        const auto* pass = w.reg().try_get<FullscreenPass>(ctx.camera_entity);
        if (!pass) {
            ++diag.graph_skipped_invalid;
            return;
        }

        if (pass->shader.state() != LoadState::Loaded) {
            ++diag.mesh_skipped_unloaded;
            return;
        }

        Program* prog = pass->shader.get();
        if (!prog) {
            ++diag.mesh_skipped_invalid;
            return;
        }

        prog->use();
        for (std::size_t i = 0; i < pass->textures.size(); ++i) {
            const auto& slot = pass->textures[i];
            if (!slot.texture) {
                ++diag.graph_skipped_invalid;
                return;
            }
            if (prog->uniform_id(slot.uniform) == -1)
                prog->locat_uniforms(slot.uniform);
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, slot.texture->get_id());
            const int loc = prog->uniform_id(slot.uniform);
            if (loc >= 0) glUniform1i(loc, static_cast<GLint>(i));
        }

        for (const auto& uniform : pass->uniforms)
            set_fullscreen_uniform(*prog, uniform);

        glBindVertexArray(res.vao);
        glDrawElements(GL_TRIANGLES, res.index_count, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
        ++diag.batch_draws;
    }

    void cleanup_fullscreen_pass(EcsWorld& w) {
        if (auto* res = w.try_resource<FullscreenResources>())
            destroy_fullscreen_resources_gpu(*res);
    }
}
