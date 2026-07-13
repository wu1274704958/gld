#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <utility>

#include <program.hpp>
#include <texture.hpp>

#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/render/RenderPassExec.hpp>
#include <ecs/Window.hpp>

namespace gld::ecs {

    void destroy_batch_gpu(BatchComponent& b) {
        if (b.instance_vbo != 0) {
            glDeleteBuffers(1, &b.instance_vbo);
            b.instance_vbo = 0;
        }
        if (b.vao != 0) {
            glDeleteVertexArrays(1, &b.vao);
            b.vao = 0;
        }
        b.gpu_cap = 0;
    }

    BatchComponent::~BatchComponent() {
        destroy_batch_gpu(*this);
    }

    BatchComponent::BatchComponent(BatchComponent&& other) noexcept {
        *this = std::move(other);
    }

    BatchComponent& BatchComponent::operator=(BatchComponent&& other) noexcept {
        if (this == &other) return *this;

        destroy_batch_gpu(*this);

        key = other.key;
        layers = other.layers;
        prog = other.prog;
        atlas_ref = std::move(other.atlas_ref);
        instances = std::move(other.instances);
        vao = other.vao;
        instance_vbo = other.instance_vbo;
        gpu_cap = other.gpu_cap;
        sig = other.sig;
        count = other.count;
        dirty = other.dirty;
        used = other.used;

        other.vao = 0;
        other.instance_vbo = 0;
        other.gpu_cap = 0;
        other.prog = nullptr;
        return *this;
    }

    void destroy_batch_resources_gpu(BatchResources& res) {
        if (res.quad_ebo != 0) {
            glDeleteBuffers(1, &res.quad_ebo);
            res.quad_ebo = 0;
        }
        if (res.quad_vbo != 0) {
            glDeleteBuffers(1, &res.quad_vbo);
            res.quad_vbo = 0;
        }
        res.quad_ready = false;
    }

    BatchResources::~BatchResources() {
        destroy_batch_resources_gpu(*this);
    }

    // Shared unit quad (pos only), centred at origin, size 1x1. Matches the
    // corner/uv assignment in text_vs.glsl (v0=TR, v1=TL, v2=BL, v3=BR).
    static void ensure_quad(BatchResources& res) {
        if (res.quad_ready) return;
        static const float verts[] = {
             0.5f,  0.5f, 0.f,   // 0 TR
            -0.5f,  0.5f, 0.f,   // 1 TL
            -0.5f, -0.5f, 0.f,   // 2 BL
             0.5f, -0.5f, 0.f,   // 3 BR
        };
        static const unsigned int idx[] = { 0, 1, 2, 0, 2, 3 };

        glGenBuffers(1, &res.quad_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, res.quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glGenBuffers(1, &res.quad_ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.quad_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx), idx, GL_STATIC_DRAW);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        res.index_count = 6;
        res.quad_ready = true;
    }

    // One-time per batch: a VAO wiring the shared quad (loc 0) + this batch's own
    // instance VBO (loc 1..11, divisor 1). Attributes are set up ONCE here, not
    // every frame (the old WordPatch re-specified them per draw).
    static void setup_batch_vao(BatchResources& res, BatchComponent& b) {
        glGenVertexArrays(1, &b.vao);
        glGenBuffers(1, &b.instance_vbo);

        glBindVertexArray(b.vao);

        glBindBuffer(GL_ARRAY_BUFFER, res.quad_vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, res.quad_ebo);

        glBindBuffer(GL_ARRAY_BUFFER, b.instance_vbo);
        const GLsizei stride = sizeof(InstanceData);
        auto attr = [&](GLuint loc, std::size_t off) {
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, stride, (void*)off);
            glVertexAttribDivisor(loc, 1);
        };
        attr(1, offsetof(InstanceData, rect));
        attr(2, offsetof(InstanceData, pad));
        attr(3, offsetof(InstanceData, color));
        for (int i = 0; i < 4; ++i) attr(4 + i, offsetof(InstanceData, transform) + i * sizeof(glm::vec4));
        attr(8,  offsetof(InstanceData, mparam0));
        attr(9,  offsetof(InstanceData, mparam1));
        attr(10, offsetof(InstanceData, mparam2));
        attr(11, offsetof(InstanceData, mparam3));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    static std::size_t upload_batch(BatchComponent& b) {
        glBindBuffer(GL_ARRAY_BUFFER, b.instance_vbo);
        const std::size_t bytes = b.instances.size() * sizeof(InstanceData);
        if (b.instances.size() > b.gpu_cap) {
            glBufferData(GL_ARRAY_BUFFER, bytes, b.instances.data(), GL_DYNAMIC_DRAW);
            b.gpu_cap = b.instances.size();
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, b.instances.data());
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        b.dirty = false;
        return bytes;
    }

    void draw_batches(EcsWorld& w, const Camera& cam) {
        auto& res = w.resource_or_add<BatchResources>();
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        auto& reg = w.reg();

        // Render state (blend/depth/target/clear) is set by render_system.
        ensure_quad(res);

        auto view = reg.view<BatchComponent>();
        std::uint32_t groups = 0;
        for (auto e : view) {
            ++groups;
            auto& b = view.get<BatchComponent>(e);
            if (b.instances.empty()) continue;
            if ((cam.layers & b.layers) == 0) continue;   // camera layer filtering

            Program* prog = b.prog;
            if (!prog) continue;

            prog->use();
            if (prog->uniform_id("uViewProj") == -1)
                prog->locat_uniforms("uViewProj", "diffuseTex");
            const glm::mat4 view_proj = cam.projection * cam.view;
            const int view_proj_loc = prog->uniform_id("uViewProj");
            if (view_proj_loc >= 0)
                glUniformMatrix4fv(view_proj_loc, 1, GL_FALSE, glm::value_ptr(view_proj));

            // Bind the glyph atlas by its GL id (the batch key's identity).
            if (b.key.atlas) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, b.key.atlas);
                const int tex_loc = prog->uniform_id("diffuseTex");
                if (tex_loc >= 0)
                    glUniform1i(tex_loc, 0);
            }

            if (b.vao == 0) setup_batch_vao(res, b);
            if (b.dirty || b.instances.size() > b.gpu_cap) {
                diag.batch_upload_bytes += upload_batch(b);
                ++diag.batch_uploads;
            }

            glBindVertexArray(b.vao);
            glDrawElementsInstanced(GL_TRIANGLES, res.index_count, GL_UNSIGNED_INT, nullptr,
                                    static_cast<GLsizei>(b.instances.size()));
            glBindVertexArray(0);
            ++diag.batch_draws;
            diag.batch_instances += static_cast<std::uint32_t>(b.instances.size());
        }
        if (groups > diag.batch_groups) diag.batch_groups = groups;
    }

    void render_batch_pass(RenderPassContext& ctx, const BatchPass&) {
        draw_batches(ctx.world, ctx.camera);
    }
}
