#include <ecs/render/Gizmo.hpp>

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace gld::ecs {
namespace {
std::uint32_t pack_color(glm::vec4 value) {
    value = glm::clamp(value, glm::vec4(0.f), glm::vec4(1.f));
    const auto byte = [](float component) {
        return static_cast<std::uint32_t>(std::lround(component * 255.f));
    };
    return byte(value.r) | (byte(value.g) << 8u) |
           (byte(value.b) << 16u) | (byte(value.a) << 24u);
}
} // namespace

void Gizmos::clear() { buckets_.clear(); ++revision_; }

void Gizmos::line(glm::vec3 from, glm::vec3 to, glm::vec4 color,
                  std::uint32_t layers) {
    if (layers == 0) return;
    auto& vertices = buckets_[layers];
    const auto packed = pack_color(color);
    vertices.push_back({from, packed});
    vertices.push_back({to, packed});
    ++revision_;
}

void Gizmos::cross(glm::vec3 center, float size, glm::vec4 color,
                   std::uint32_t layers) {
    const float radius = std::max(0.f, size);
    line(center - glm::vec3(radius, 0.f, 0.f), center + glm::vec3(radius, 0.f, 0.f), color, layers);
    line(center - glm::vec3(0.f, radius, 0.f), center + glm::vec3(0.f, radius, 0.f), color, layers);
    line(center - glm::vec3(0.f, 0.f, radius), center + glm::vec3(0.f, 0.f, radius), color, layers);
}

void Gizmos::arrow(glm::vec3 from, glm::vec3 to, float head_size,
                   glm::vec4 color, std::uint32_t layers) {
    line(from, to, color, layers);
    const glm::vec3 delta = to - from;
    const float length = glm::length(delta);
    if (length <= 1e-6f || head_size <= 0.f) return;
    const glm::vec3 direction = delta / length;
    const glm::vec3 helper = std::abs(direction.z) < 0.9f
        ? glm::vec3(0.f, 0.f, 1.f) : glm::vec3(0.f, 1.f, 0.f);
    const glm::vec3 side = glm::normalize(glm::cross(direction, helper));
    const glm::vec3 up = glm::normalize(glm::cross(side, direction));
    const glm::vec3 base = to - direction * head_size;
    const float width = head_size * 0.5f;
    line(to, base + side * width, color, layers);
    line(to, base - side * width, color, layers);
    line(to, base + up * width, color, layers);
    line(to, base - up * width, color, layers);
}

void Gizmos::wire_box(glm::vec3 center, glm::vec3 half_extents,
                      glm::mat3 orientation, glm::vec4 color,
                      std::uint32_t layers) {
    glm::vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 local{(i & 1) ? half_extents.x : -half_extents.x,
                              (i & 2) ? half_extents.y : -half_extents.y,
                              (i & 4) ? half_extents.z : -half_extents.z};
        corners[i] = center + orientation * local;
    }
    static constexpr int edges[12][2] = {
        {0,1},{2,3},{4,5},{6,7}, {0,2},{1,3},{4,6},{5,7},
        {0,4},{1,5},{2,6},{3,7}
    };
    for (const auto& edge : edges) line(corners[edge[0]], corners[edge[1]], color, layers);
}

void Gizmos::wire_ellipse(glm::vec3 center, glm::vec3 axis_a, glm::vec3 axis_b,
                          int segments, glm::vec4 color, std::uint32_t layers) {
    segments = std::max(3, segments);
    glm::vec3 previous = center + axis_a;
    for (int index = 1; index <= segments; ++index) {
        const float angle = glm::two_pi<float>() * static_cast<float>(index) /
                            static_cast<float>(segments);
        const glm::vec3 point = center + std::cos(angle) * axis_a + std::sin(angle) * axis_b;
        line(previous, point, color, layers);
        previous = point;
    }
}

void Gizmos::wire_ellipsoid(glm::vec3 center, glm::vec3 radii,
                            glm::mat3 orientation, int segments,
                            glm::vec4 color, std::uint32_t layers) {
    const glm::vec3 x = orientation[0] * radii.x;
    const glm::vec3 y = orientation[1] * radii.y;
    const glm::vec3 z = orientation[2] * radii.z;
    wire_ellipse(center, x, y, segments, color, layers);
    wire_ellipse(center, x, z, segments, color, layers);
    wire_ellipse(center, y, z, segments, color, layers);
}

void Gizmos::rebuild_stream() {
    if (built_revision_ == revision_) return;
    stream_.clear();
    ranges_.clear();
    for (const auto& [layers, vertices] : buckets_) {
        if (vertices.empty()) continue;
        ranges_.push_back({layers, static_cast<std::uint32_t>(stream_.size()),
                           static_cast<std::uint32_t>(vertices.size())});
        stream_.insert(stream_.end(), vertices.begin(), vertices.end());
    }
    built_revision_ = revision_;
}

const std::vector<GizmoVertex>& Gizmos::vertex_stream() { rebuild_stream(); return stream_; }
const std::vector<GizmoDrawRange>& Gizmos::draw_ranges() { rebuild_stream(); return ranges_; }
std::size_t Gizmos::vertex_count(std::uint32_t layers) const {
    const auto found = buckets_.find(layers);
    return found == buckets_.end() ? 0 : found->second.size();
}

} // namespace gld::ecs
