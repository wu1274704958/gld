#include <ecs/render/Gizmo.hpp>

#include <cstddef>
#include <utility>

#include <glad/glad.h>
#include <glm/gtc/type_ptr.hpp>

#include <ecs/assets/AssetServer.hpp>
#include <ecs/render/RenderSystem.hpp>

namespace gld::ecs {
namespace {

std::size_t next_capacity(std::size_t required) {
    std::size_t result = 256;
    while (result < required) result *= 2;
    return result;
}

} // namespace

RegisteredRenderPass make_gizmo_pass() {
    return {GizmoPassId, {}};
}

void render_gizmo_pass(RenderPassContext& context, const RegisteredRenderPass&,
                       const ResolvedRenderPassState&) {
    auto* gizmos = context.world.try_resource<Gizmos>();
    auto* resources = context.world.try_resource<GizmoRenderResources>();
    if (!gizmos || !resources) return;
    Program* shader = resources->shader.get();
    if (!shader) return;
    const auto& vertices = gizmos->vertex_stream();
    const auto& ranges = gizmos->draw_ranges();
    if (vertices.empty()) return;

    if (resources->vao == 0) {
        glGenVertexArrays(1, &resources->vao);
        glGenBuffers(1, &resources->vbo);
        glBindVertexArray(resources->vao);
        glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex),
                              reinterpret_cast<const void*>(offsetof(GizmoVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GizmoVertex),
                              reinterpret_cast<const void*>(offsetof(GizmoVertex, color)));
        glBindVertexArray(0);
    }
    if (resources->uploaded_revision != gizmos->revision()) {
        glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);
        if (vertices.size() > resources->gpu_capacity) {
            resources->gpu_capacity = next_capacity(vertices.size());
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(resources->gpu_capacity * sizeof(GizmoVertex)),
                         nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(vertices.size() * sizeof(GizmoVertex)),
                        vertices.data());
        resources->uploaded_revision = gizmos->revision();
    }

    shader->use();
    if (shader->uniform_id("uViewProj") < 0) shader->locat_uniforms("uViewProj");
    const int location = shader->uniform_id("uViewProj");
    const glm::mat4 view_projection = context.camera.projection * context.camera.view;
    if (location >= 0) glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(view_projection));
    glLineWidth(1.f);
    glBindVertexArray(resources->vao);
    for (const auto& range : ranges) {
        if ((range.layers & context.camera.layers) == 0) continue;
        glDrawArrays(GL_LINES, static_cast<GLint>(range.first_vertex),
                     static_cast<GLsizei>(range.vertex_count));
    }
    glBindVertexArray(0);
}

void destroy_gizmo_resources(EcsWorld& world) {
    if (auto* resources = world.try_resource<GizmoRenderResources>()) {
        if (resources->vbo != 0) glDeleteBuffers(1, &resources->vbo);
        if (resources->vao != 0) glDeleteVertexArrays(1, &resources->vao);
        resources->vbo = 0;
        resources->vao = 0;
        resources->gpu_capacity = 0;
        resources->uploaded_revision = 0;
    }
    unregister_render_pass(world, GizmoPassId);
}

void GizmoPlugin(App& app) {
    app.world.resource_or_add<Gizmos>();
    auto& render = app.world.resource_or_add<GizmoRenderResources>();
    auto& server = app.world.resource<AssetServer>();
    render.shader = server.load_program("ecs/gizmo_vs.glsl", "ecs/gizmo_fg.glsl");
    register_render_pass(app.world, GizmoPassId, RegisteredRenderPassHandler{
        RenderPassBatch, render_gizmo_pass, destroy_gizmo_resources
    });
    app.add_system(Stage::First, [](EcsWorld& world) {
        world.resource<Gizmos>().clear();
    });
    app.add_system(Stage::Shutdown, destroy_gizmo_resources);
}

} // namespace gld::ecs
