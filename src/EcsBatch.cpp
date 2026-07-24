#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <utility>
#include <vector>

#include <program.hpp>
#include <texture.hpp>

#include <ecs/render/BatchSystem.hpp>
#include <ecs/render/Batch.hpp>
#include <ecs/render/RenderComponents.hpp>
#include <ecs/PerformanceMonitoring.hpp>
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
        texture_refs = std::move(other.texture_refs);
        sampler_locations = other.sampler_locations;
        order = other.order;
        instances = std::move(other.instances);
        dirty_ranges = std::move(other.dirty_ranges);
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
        other.texture_refs.fill(nullptr);
        other.sampler_locations.fill(-1);
        other.order = 0;
        other.dirty_ranges.clear();
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

    struct BatchUploadResult {
        std::size_t bytes = 0;
        std::uint32_t calls = 0;
        bool full = false;
    };

    static BatchUploadResult upload_batch(BatchComponent& b) {
        BatchUploadResult result;
        glBindBuffer(GL_ARRAY_BUFFER, b.instance_vbo);
        const std::size_t bytes = b.instances.size() * sizeof(InstanceData);
        const bool grow = b.instances.size() > b.gpu_cap;
        auto plan = plan_batch_upload(b.dirty_ranges, b.instances.size(), b.dirty || grow);
        if (grow) {
            b.gpu_cap = next_batch_gpu_capacity(b.instances.size());
            glBufferData(GL_ARRAY_BUFFER, b.gpu_cap * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);
        }
        if (plan.full) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, b.instances.data());
            result.bytes = bytes;
            result.calls = 1;
            result.full = true;
        } else {
            for (const auto& range : plan.ranges) {
                const std::size_t offset =
                    static_cast<std::size_t>(range.first_instance) * sizeof(InstanceData);
                const std::size_t range_bytes =
                    static_cast<std::size_t>(range.instance_count) * sizeof(InstanceData);
                glBufferSubData(GL_ARRAY_BUFFER, offset, range_bytes,
                                b.instances.data() + range.first_instance);
                result.bytes += range_bytes;
                ++result.calls;
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        b.dirty = false;
        b.dirty_ranges.clear();
        return result;
    }

    void draw_batches(EcsWorld& w, const Camera& cam, RenderStateCache& state_cache,
                      const ResolvedRenderPassState& pass_state) {
        GLD_PERF_TIME_POINT(prepare_started);
        auto& res = w.resource_or_add<BatchResources>();
        auto& diag = w.resource_or_add<RenderDiagnostics>();
        auto& reg = w.reg();

        // Render state (blend/depth/target/clear) is set by render_system.
        ensure_quad(res);

        auto view = reg.view<BatchComponent>();
        std::vector<entt::entity> ordered;
        for (auto e : view) {
            ordered.push_back(e);
        }
        std::sort(ordered.begin(), ordered.end(), [&](entt::entity lhs, entt::entity rhs) {
            const auto& a = view.get<BatchComponent>(lhs);
            const auto& b = view.get<BatchComponent>(rhs);
            if (a.order != b.order) return a.order < b.order;
            return static_cast<std::uint32_t>(lhs) < static_cast<std::uint32_t>(rhs);
        });
        GLD_PERF_MONITOR(
            diag.batch_prepare_ms += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - prepare_started).count();
        );
        GLD_PERF_TIME_POINT(submit_started);
        double upload_ms = 0.0;

        for (auto e : ordered) {
            auto& b = view.get<BatchComponent>(e);
            if (b.instances.empty()) continue;
            if ((cam.layers & b.layers) == 0) continue;   // camera layer filtering

            state_cache.depth(resolve_batch_state(b.key.depth_test, pass_state.depth_test));
            state_cache.depth_write(resolve_batch_state(b.key.depth_write, pass_state.depth_write));

            Program* prog = b.prog;
            if (!prog) continue;

            prog->use();
            if (prog->uniform_id("uViewProj") == -1)
                prog->locat_uniforms("uViewProj", "diffuseTex");
            const glm::mat4 view_proj = cam.projection * cam.view;
            const int view_proj_loc = prog->uniform_id("uViewProj");
            if (view_proj_loc >= 0)
                glUniformMatrix4fv(view_proj_loc, 1, GL_FALSE, glm::value_ptr(view_proj));

            // Preserve the original one-texture hot path. Multi-texture batches
            // use fixed slots and never depend on bindings left by a prior draw.
            if (b.key.texture_count == 1 && b.key.textures[0]) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, b.key.textures[0]);
                int tex_loc = b.sampler_locations[0];
                if (tex_loc < 0) {
                    if (prog->uniform_id("diffuseTex") == -1) prog->locat_uniforms("diffuseTex");
                    tex_loc = prog->uniform_id("diffuseTex");
                }
                if (tex_loc >= 0)
                    glUniform1i(tex_loc, 0);
            } else {
                for (std::size_t slot = 0; slot < b.key.texture_count; ++slot) {
                    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + slot));
                    glBindTexture(GL_TEXTURE_2D, b.key.textures[slot]);
                    if (b.sampler_locations[slot] >= 0)
                        glUniform1i(b.sampler_locations[slot], static_cast<int>(slot));
                }
                glActiveTexture(GL_TEXTURE0);
            }

            if (b.vao == 0) setup_batch_vao(res, b);
            if (b.dirty || !b.dirty_ranges.empty() || b.instances.size() > b.gpu_cap) {
                GLD_PERF_TIME_POINT(upload_started);
                const auto upload = upload_batch(b);
                GLD_PERF_MONITOR(
                    diag.batch_upload_bytes += upload.bytes;
                    diag.batch_upload_ranges += upload.calls;
                    if (upload.full) {
                        ++diag.batch_full_uploads;
                        diag.batch_full_upload_bytes += upload.bytes;
                    } else {
                        ++diag.batch_partial_uploads;
                        diag.batch_partial_upload_bytes += upload.bytes;
                    }
                    const auto elapsed = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - upload_started).count();
                    upload_ms += elapsed;
                    diag.batch_upload_ms += elapsed;
                    ++diag.batch_uploads;
                );
            }

            glBindVertexArray(b.vao);
            glDrawElementsInstanced(GL_TRIANGLES, res.index_count, GL_UNSIGNED_INT, nullptr,
                                    static_cast<GLsizei>(b.instances.size()));
            glBindVertexArray(0);
            GLD_PERF_MONITOR(
                ++diag.batch_draws;
                diag.batch_instances += static_cast<std::uint32_t>(b.instances.size());
            );
        }
        GLD_PERF_MONITOR(
            const auto submit_total = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - submit_started).count();
            diag.batch_submit_ms += std::max(0.0, submit_total - upload_ms);
            if (ordered.size() > diag.batch_groups)
                diag.batch_groups = static_cast<std::uint32_t>(ordered.size());
        );
    }

    void render_batch_pass(RenderPassContext& ctx, const BatchPass& pass) {
        const auto pass_state = resolve_render_pass_state(BatchPass::id, pass.state);
        draw_batches(ctx.world, ctx.camera, ctx.state_cache, pass_state);
    }
}
