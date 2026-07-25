#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "../App.hpp"
#include "../assets/Handle.hpp"
#include "RenderPassExec.hpp"

namespace gld::ecs {

inline constexpr RegisteredRenderPassId GizmoPassId = 0x47495A4Du; // 'GIZM'

struct GizmoVertex {
    glm::vec3 position{0.f};
    std::uint32_t color = 0xffffffffu;
};

struct GizmoDrawRange {
    std::uint32_t layers = 0;
    std::uint32_t first_vertex = 0;
    std::uint32_t vertex_count = 0;
};

class Gizmos {
public:
    void clear();
    void line(glm::vec3 from, glm::vec3 to, glm::vec4 color,
              std::uint32_t layers = ~0u);
    void cross(glm::vec3 center, float size, glm::vec4 color,
               std::uint32_t layers = ~0u);
    void arrow(glm::vec3 from, glm::vec3 to, float head_size, glm::vec4 color,
               std::uint32_t layers = ~0u);
    void wire_box(glm::vec3 center, glm::vec3 half_extents, glm::mat3 orientation,
                  glm::vec4 color, std::uint32_t layers = ~0u);
    void wire_ellipse(glm::vec3 center, glm::vec3 axis_a, glm::vec3 axis_b,
                      int segments, glm::vec4 color,
                      std::uint32_t layers = ~0u);
    void wire_ellipsoid(glm::vec3 center, glm::vec3 radii, glm::mat3 orientation,
                        int segments, glm::vec4 color,
                        std::uint32_t layers = ~0u);

    const std::vector<GizmoVertex>& vertex_stream();
    const std::vector<GizmoDrawRange>& draw_ranges();
    std::size_t vertex_count(std::uint32_t layers) const;
    std::uint64_t revision() const { return revision_; }

private:
    void rebuild_stream();
    std::map<std::uint32_t, std::vector<GizmoVertex>> buckets_;
    std::vector<GizmoVertex> stream_;
    std::vector<GizmoDrawRange> ranges_;
    std::uint64_t revision_ = 1;
    std::uint64_t built_revision_ = 0;
};

struct GizmoRenderResources {
    Handle<Program> shader;
    unsigned int vao = 0;
    unsigned int vbo = 0;
    std::size_t gpu_capacity = 0;
    std::uint64_t uploaded_revision = 0;
};

RegisteredRenderPass make_gizmo_pass();
void render_gizmo_pass(RenderPassContext&, const RegisteredRenderPass&,
                       const ResolvedRenderPassState&);
void destroy_gizmo_resources(EcsWorld&);
void GizmoPlugin(App& app);

} // namespace gld::ecs
