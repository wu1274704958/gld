#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <program.hpp>
#include <texture.hpp>

#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/Window.hpp>

namespace gld::ecs {

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
        attr(1, offsetof(InstanceData, uv));
        attr(2, offsetof(InstanceData, uv2));
        attr(3, offsetof(InstanceData, color));
        for (int i = 0; i < 4; ++i) attr(4 + i, offsetof(InstanceData, model) + i * sizeof(glm::vec4));
        for (int i = 0; i < 4; ++i) attr(8 + i, offsetof(InstanceData, local) + i * sizeof(glm::vec4));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    static void upload_batch(BatchComponent& b) {
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
    }

    void draw_batches(EcsWorld& w, const Camera& cam) {
        auto& res = w.resource_or_add<BatchResources>();
        auto& reg = w.reg();

        // Render state (blend/depth/target/clear) is set by render_system.
        ensure_quad(res);

        auto view = reg.view<BatchComponent>();
        for (auto e : view) {
            auto& b = view.get<BatchComponent>(e);
            if (b.instances.empty()) continue;
            if ((cam.layers & b.layers) == 0) continue;   // camera layer filtering

            auto* prog  = static_cast<Program*>(const_cast<void*>(b.key.shader));
            auto* atlas = static_cast<Texture<TexType::D2>*>(const_cast<void*>(b.key.atlas));
            if (!prog) continue;

            prog->use();
            if (prog->uniform_id("uViewProj") == -1)
                prog->locat_uniforms("uViewProj", "diffuseTex");
            glUniformMatrix4fv(prog->uniform_id("uViewProj"), 1, GL_FALSE, glm::value_ptr(cam.projection));

            if (atlas) {
                atlas->active<ActiveTexId::_0>();
                glUniform1i(prog->uniform_id("diffuseTex"), 0);
            }

            if (b.vao == 0) setup_batch_vao(res, b);
            if (b.dirty || b.instances.size() > b.gpu_cap) upload_batch(b);

            glBindVertexArray(b.vao);
            glDrawElementsInstanced(GL_TRIANGLES, res.index_count, GL_UNSIGNED_INT, nullptr,
                                    static_cast<GLsizei>(b.instances.size()));
            glBindVertexArray(0);
        }
    }
}
